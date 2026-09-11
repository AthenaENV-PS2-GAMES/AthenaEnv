#include <assert.h>
#include <sys/fcntl.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>

#include <ath_env.h>
#include <athena_module.h>
#include <memory.h>

#define TRUE 1
#define JSFILE_NOTFOUND -5656

JSModuleDef *athena_push_module(JSContext* ctx, JSModuleInitFunc *func, const JSCFunctionListEntry *func_list, int len, const char* module_name) {
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, func);
    if (!m)
        return NULL;
    JS_AddModuleExportList(ctx, m, func_list, len);

    dbgprintf("AthenaCore: %s module registered at 0x%p\n", module_name, (void*)m);
    return m;
}

static int qjs_eval_buf(JSContext *ctx, const void *buf, int buf_len,
                    const char *filename, int eval_flags)
{
    JSValue val;
    int ret;

    if ((eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE) {
        val = JS_Eval(ctx, buf, buf_len, filename,
                      eval_flags | JS_EVAL_FLAG_COMPILE_ONLY);
        if (!JS_IsException(val)) {
            js_module_set_import_meta(ctx, val, TRUE, TRUE);
            val = JS_EvalFunction(ctx, val);
        }
    } else {
        val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);
    }
	
    if (JS_IsException(val)) {
        ret = -1;
    } else {
        ret = 0;
    }

    JS_FreeValue(ctx, val);
    return ret;
}

static int qjs_handle_fh(JSContext *ctx, FILE *f, const char *filename) {
    char *buf = NULL;
    size_t bufsz = 1024;
    size_t bufoff = 0;
    size_t got;
    int rc;
    int retval = -1;

    buf = (char *) malloc(bufsz);
    if (!buf) {
        return retval;
    }

    for (;;) {
        size_t avail = bufsz - bufoff;
        if (avail < 1024) {
            size_t newsz = bufsz + (bufsz >> 2) + 1024;
            char *buf_new = (char *) realloc(buf, newsz);
            if (!buf_new) {
                free(buf);
                return retval;
            }
            buf = buf_new;
            bufsz = newsz;
        }

        avail = bufsz - bufoff;
        got = fread((void *) (buf + bufoff), (size_t) 1, avail, f);
        if (got == 0) {
            break;
        }
        bufoff += got;
    }

    buf[bufoff++] = 0;

    dbgprintf("[AthenaCore] Adding QuickJS std helpers\n");
    js_std_add_helpers(ctx, 0, NULL);
    dbgprintf("[AthenaCore] QuickJS std helpers added\n");

    // Bootstrap global namespaces
    {
        const char *base_bootstrap = 
            "import * as std from 'std';\n"
            "import * as os from 'os';\n"
            "globalThis.std = std;\n"
            "globalThis.os = os;\n"
            "globalThis.setTimeout = os.setTimeout;\n"
            "globalThis.setInterval = os.setInterval;\n"
            "globalThis.setImmediate = os.setImmediate;\n"
            "globalThis.clearTimeout = os.clearTimeout;\n"
            "globalThis.clearInterval = os.clearInterval;\n"
            "globalThis.clearImmediate = os.clearImmediate;\n";

        dbgprintf("[AthenaCore] Evaluating base bootstrap\n");
        rc = qjs_eval_buf(ctx, base_bootstrap, strlen(base_bootstrap), "<bootstrap-base>", JS_EVAL_TYPE_MODULE);
        dbgprintf("[AthenaCore] Base bootstrap returned %d\n", rc);
        if (rc != 0) { 
            free(buf);
            return retval; 
        }

        dbgprintf("[AthenaCore] Evaluating module bootstrap\n");
        const char *modules_bootstrap = athena_get_modules_bootstrap_script();
        if (modules_bootstrap && modules_bootstrap[0] != '\0') {
            rc = qjs_eval_buf(ctx, modules_bootstrap, strlen(modules_bootstrap), "<bootstrap-modules>", JS_EVAL_TYPE_MODULE);
            dbgprintf("[AthenaCore] Module bootstrap returned %d\n", rc);
            if (rc != 0) {
                free(buf);
                return retval;
            }
        }
    }

    rc = qjs_eval_buf(ctx, (void *) buf, bufoff - 1, filename, JS_EVAL_TYPE_MODULE);
    free(buf);
    
    if (rc != 0) { 
        return retval; 
    }
    
    return 0;
}

static int qjs_handle_file(JSContext *ctx, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        return JSFILE_NOTFOUND;
    }

    int retval = qjs_handle_fh(ctx, f, filename);
    fclose(f);
    return retval;
}

static JSContext *JS_NewCustomContext(JSRuntime *rt)
{
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx)
        return NULL;

    /* Base system modules */
    js_init_module_std(ctx, "std");
    js_init_module_os(ctx, "os");

    /* Register all configured Athena modules */
    athena_register_all_modules(ctx);

    return ctx;
}

static char error_buf[4096];

void destroy_vm(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    athena_cleanup_all_modules(ctx);
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

static jmp_buf vm_reset_buf;

jmp_buf *get_reset_buf() {
    return &vm_reset_buf;
}

const char* run_script(const char* script, bool isBuffer)
{
    size_t memoryLimit = (GetMemorySize() - get_used_memory()) >> 1;

    dbgprintf("\n[AthenaCore] Starting QuickJS runtime...\n");
    JSRuntime *rt = JS_NewRuntime(); 
    if (!rt) { 
        return "AthenaError: Runtime creation failed"; 
    }
    
    js_std_set_worker_new_context_func(JS_NewCustomContext);
    js_std_init_handlers(rt);

    JS_SetMemoryLimit(rt, memoryLimit);
    JS_SetGCThreshold(rt, memoryLimit - 2097152); // 2MB margin for GC

    JSContext *ctx = JS_NewCustomContext(rt); 
    if (!ctx) { 
        JS_FreeRuntime(rt);
        return "AthenaError: Context creation failed"; 
    }

    JS_SetModuleLoaderFunc(rt, NULL, js_module_loader, NULL);

    dbgprintf("[AthenaCore] Executing entry script: %s\n", script);
    int s = qjs_handle_file(ctx, script);
    dbgprintf("[AthenaCore] Entry script evaluation returned %d\n", s);

    if (s >= 0) {
        // Run event loop (timers, promises, microtasks)
        dbgprintf("[AthenaCore] Starting QuickJS event loop\n");
        s = js_std_loop(ctx);
        dbgprintf("[AthenaCore] QuickJS event loop returned %d\n", s);
    }

    if (s < 0) { 
        if (s == JSFILE_NOTFOUND) {
            snprintf(error_buf, sizeof(error_buf), 
                "AthenaError: Failed to open '%s'\n"
                "Tip: Ensure the file exists at current path and device is mounted.\n", 
                script);
        } else {
            JSValue exception_val = JS_GetException(ctx);
            const char* exception = JS_ToCString(ctx, exception_val);
            JSValue stack_val = JS_GetPropertyStr(ctx, exception_val, "stack");
            const char* stack = JS_ToCString(ctx, stack_val);
            
            snprintf(error_buf, sizeof(error_buf), "%s\n%s", 
                exception ? exception : "Unknown Exception", 
                stack ? stack : "");

            if (exception) JS_FreeCString(ctx, exception);
            if (stack) JS_FreeCString(ctx, stack);
            JS_FreeValue(ctx, exception_val);
            JS_FreeValue(ctx, stack_val);
        }
        
        dbgprintf("[AthenaCore] Destroying QuickJS runtime after error\n");
        destroy_vm(ctx);
        return error_buf; 
    }
    
    dbgprintf("[AthenaCore] Destroying QuickJS runtime\n");
    destroy_vm(ctx);
    dbgprintf("[AthenaCore] QuickJS runtime destroyed\n");
    return NULL;
}
