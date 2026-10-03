#include <assert.h>
#include <sys/fcntl.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>

#include <ath_env.h>
#include <ath_gil.h>
#include <athena_js_module.h>
#include <athena/config.h>
#include <athena/memory.h>

#ifdef ATHENA_MODULE_GAMEPAD
#include <timer.h>
#include <athena/gamepad.h>
#endif

#define TRUE 1
#define JSFILE_NOTFOUND -5656

/* Main-thread stack kept for the C code JavaScript calls (decoders, FreeType). */
#define MAIN_STACK_RESERVE (24 * 1024)

JSModuleDef *athena_push_module(JSContext* ctx, JSModuleInitFunc *func, const JSCFunctionListEntry *func_list, int len, const char* module_name) {
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, func);
    if (!m)
        return NULL;
    JS_AddModuleExportList(ctx, m, func_list, len);

    dbgprintf("AthenaCore: %s module registered at 0x%p\n", module_name, (void*)m);
    return m;
}

int athena_register_class(JSContext *ctx, JSClassID *class_id, const JSClassDef *class_def) {
    JSRuntime *rt = JS_GetRuntime(ctx);

    JS_NewClassID(class_id);
    if (JS_IsRegisteredClass(rt, *class_id))
        return 0;
    return JS_NewClass(rt, *class_id, class_def) < 0 ? -1 : 0;
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
            dbgprintf("[AthenaCore] Module bootstrap source ready\n");
            rc = qjs_eval_buf(ctx, modules_bootstrap, strlen(modules_bootstrap), "<bootstrap-modules>", JS_EVAL_TYPE_MODULE);
            dbgprintf("[AthenaCore] Module bootstrap returned %d\n", rc);
            if (rc != 0) {
                free(buf);
                return retval;
            }
            dbgprintf("[AthenaCore] Module bootstrap completed; evaluating entry body\n");
        }
    }

    dbgprintf("[AthenaCore] Evaluating entry body: %s\n", filename);
    rc = qjs_eval_buf(ctx, (void *) buf, bufoff - 1, filename, JS_EVAL_TYPE_MODULE);
    dbgprintf("[AthenaCore] Entry body evaluation completed: %s (%d)\n", filename, rc);
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

/*
 * Module loader: JavaScript modules embedded in the binary first, then the
 * default loader (files and .erl native modules). An embedded module is
 * compiled from its source on first import, under its module name.
 */
static JSModuleDef *athena_module_loader(JSContext *ctx, const char *module_name,
                                         void *opaque)
{
    size_t length;
    const char *source = athena_find_js_module(module_name, &length);
    char *buf;
    JSValue func_val;
    JSModuleDef *m;

    if (!source)
        return js_module_loader(ctx, module_name, opaque);

    /* JS_Eval() needs a NUL-terminated buffer. */
    buf = js_malloc(ctx, length + 1);
    if (!buf)
        return NULL;
    memcpy(buf, source, length);
    buf[length] = '\0';

    func_val = JS_Eval(ctx, buf, length, module_name,
                       JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    js_free(ctx, buf);
    if (JS_IsException(func_val))
        return NULL;
    /* No realpath(): the module has no file. import.meta.url is file://<name>. */
    js_module_set_import_meta(ctx, func_val, 0, 0);
    /* The module is already referenced, so this reference is dropped. */
    m = JS_VALUE_GET_PTR(func_val);
    JS_FreeValue(ctx, func_val);
    dbgprintf("AthenaCore: JavaScript module %s compiled (%u bytes)\n", module_name, (unsigned)length);
    return m;
}

static JSContext *JS_NewCustomContext(JSRuntime *rt)
{
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx)
        return NULL;

    /* Also runs for worker runtimes, after quickjs-libc set its default loader. */
    JS_SetModuleLoaderFunc(rt, NULL, athena_module_loader, NULL);

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

/* ---- Script switching: std.reload() and the return to a launcher ---- */

#define SCRIPT_PATH_MAX 256

static char reload_script[SCRIPT_PATH_MAX];
static char reload_return[SCRIPT_PATH_MAX];
static volatile bool reload_pending;
static bool reload_by_exit;
/* Where the running script returns when it ends, or "" (it was not launched). */
static char return_script[SCRIPT_PATH_MAX];

static AthenaRunStatus last_status = ATHENA_RUN_NONE;
static char last_script[SCRIPT_PATH_MAX];
static char last_error[4096];

/* Copies `src` (NULL is empty), cut to fit `size`. */
static void copy_text(char *dst, size_t size, const char *src) {
    size_t length = src ? strlen(src) : 0;

    if (length >= size)
        length = size - 1;
    if (length)
        memcpy(dst, src, length);
    dst[length] = '\0';
}

void athena_runtime_request_reload(const char *script, const char *return_to) {
    copy_text(reload_script, sizeof(reload_script), script);
    copy_text(reload_return, sizeof(reload_return), return_to);
    reload_by_exit = false;
    reload_pending = true;
}

#ifdef ATHENA_MODULE_GAMEPAD
/* SELECT + START held on the pad in port 1, polled at most every EXIT_POLL_MS. */
#define EXIT_BUTTONS (0x0001 | 0x0008)
#define EXIT_HOLD_MS 1000
#define EXIT_POLL_MS 50

static uint64_t exit_last_poll;
static uint64_t exit_held_since;
/* The pad is read on the script thread only: JavaScript threads run the interrupt handler too. */
static int exit_script_thread = -1;

static uint64_t runtime_now_ms(void) {
    return GetTimerSystemTime() / (kBUSCLK / 1000);
}

static void poll_exit_buttons(void) {
    uint64_t now = runtime_now_ms();

    if (now - exit_last_poll < EXIT_POLL_MS)
        return;
    exit_last_poll = now;
    if ((athena_gamepad_core_peek(0) & EXIT_BUTTONS) != EXIT_BUTTONS) {
        exit_held_since = 0;
        return;
    }
    if (!exit_held_since) {
        exit_held_since = now;
    } else if (now - exit_held_since >= EXIT_HOLD_MS) {
        dbgprintf("[AthenaCore] SELECT+START: returning to %s\n", return_script);
        athena_runtime_request_reload(return_script, NULL);
        reload_by_exit = true;
        exit_held_since = 0;
    }
}
#endif

/* Called by QuickJS's interrupt handler and by js_std_loop(), on the script's thread. */
int athena_runtime_stop_requested(void) {
#ifdef ATHENA_MODULE_GAMEPAD
    if (!reload_pending && return_script[0] && GetThreadId() == exit_script_thread)
        poll_exit_buttons();
#endif
    return reload_pending;
}

const char *athena_runtime_next_script(const char *script, const char *error) {
    static char next[SCRIPT_PATH_MAX];

    copy_text(last_script, sizeof(last_script), script);
    last_error[0] = '\0';
    athena_runtime_output_rotate();

    if (reload_pending) {
        /* The error is the interruption that unwound the script. */
        last_status = reload_by_exit ? ATHENA_RUN_EXITED : ATHENA_RUN_RELOADED;
        copy_text(next, sizeof(next), reload_script);
        copy_text(return_script, sizeof(return_script), reload_return);
        reload_pending = false;
    } else {
        last_status = error ? ATHENA_RUN_ERROR : ATHENA_RUN_FINISHED;
        copy_text(last_error, sizeof(last_error), error);
        if (!return_script[0])
            return NULL;
        copy_text(next, sizeof(next), return_script);
        return_script[0] = '\0';
    }

#ifdef ATHENA_MODULE_GAMEPAD
    /* SELECT+START reads the pad even when the script never uses Gamepad. */
    if (return_script[0])
        athena_gamepad_core_init();
    exit_held_since = 0;
#endif
    return next;
}

AthenaRunStatus athena_runtime_last_run(const char **script, const char **error,
                                        const char **output) {
    if (script)
        *script = last_script;
    if (error)
        *error = last_error[0] ? last_error : NULL;
    if (output)
        *output = athena_runtime_output_last();
    return last_status;
}

const char* run_script(const char* script, bool isBuffer)
{
    size_t memoryLimit = (GetMemorySize() - get_used_memory()) >> 1;

    dbgprintf("\n[AthenaCore] Starting QuickJS runtime...\n");
    /*
     * Item 3.0 checklist:
     * - Main-thread gate entry covers runtime/context setup, qjs_handle_file,
     *   error handling, js_std_loop, and destroy_vm in this function.
     * - The gate, main-thread protection, and worker protection are one
     *   functional commit/PR and must not be merged independently.
     * - No main-thread JS_* path below runs without the gate while the worker
     *   requires it.
     */
    athena_js_gil_init();
    athena_js_gil_lock();

#ifdef ATHENA_MODULE_GAMEPAD
    exit_script_thread = GetThreadId();
#endif
    JSRuntime *rt = JS_NewRuntime(); 
    if (!rt) { 
        athena_js_gil_unlock();
        athena_js_gil_destroy();
        return "AthenaError: Runtime creation failed"; 
    }
    
    athena_js_gil_set_runtime(rt);
    /*
     * QuickJS assumes 256 KB of stack; the main thread has get_stack_size()
     * (MAIN_STACK_SIZE in the Makefile). Past this budget deep recursion throws a catchable
     * "stack overflow" instead of overwriting the memory below the stack.
     */
    athena_js_gil_set_stack_budget(get_stack_size() - MAIN_STACK_RESERVE);
    JS_SetMaxStackSize(rt, get_stack_size() - MAIN_STACK_RESERVE);
    js_std_set_worker_new_context_func(JS_NewCustomContext);
    js_std_init_handlers(rt);
    js_std_set_interrupt_handler(rt);

    /*
     * QuickJS's adaptive cycle collection (it runs again once the heap grew
     * by half) keeps collections short. Images, textures and glyphs are
     * allocated outside the JS heap, so waiting for the heap to fill would
     * keep them alive long after they became garbage.
     */
    JS_SetMemoryLimit(rt, memoryLimit);

    JSContext *ctx = JS_NewCustomContext(rt); 
    if (!ctx) { 
        athena_js_gil_set_runtime(NULL);
        athena_js_gil_unlock();
        athena_js_gil_destroy();
        JS_FreeRuntime(rt);
        return "AthenaError: Context creation failed"; 
    }

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
            /*
             * No exception object: memory ran out so completely that the
             * error itself could not be created, or the event loop already
             * printed an error from a promise job to the output.
             */
            if (JS_IsNull(exception_val) || JS_IsUndefined(exception_val)) {
                snprintf(error_buf, sizeof(error_buf), "%s",
                    "InternalError: the script stopped without an error object: out of memory, "
                    "or an error printed above in its output");
                goto teardown;
            }
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

    teardown:
        dbgprintf("[AthenaCore] Destroying QuickJS runtime after error\n");
        athena_js_gil_unlock();
        athena_modules_quiesce();
        athena_js_gil_lock();
        destroy_vm(ctx);
        athena_js_gil_set_runtime(NULL);
        athena_js_gil_unlock();
        athena_js_gil_destroy();
        return error_buf; 
    }
    
    dbgprintf("[AthenaCore] Destroying QuickJS runtime\n");
    athena_js_gil_unlock();
    athena_modules_quiesce();
    athena_js_gil_lock();
    destroy_vm(ctx);
    athena_js_gil_set_runtime(NULL);
    athena_js_gil_unlock();
    athena_js_gil_destroy();
    dbgprintf("[AthenaCore] QuickJS runtime destroyed\n");
    return NULL;
}
