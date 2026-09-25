/*
 * Host runner for module test scripts: AthenaEnv's QuickJS built for a
 * 32-bit host (NaN-boxing and the float32 values, as on the EE) with
 * AddressSanitizer, running bin/tests/<name>.js with the module globals,
 * console.log and std.gc. JS_FreeRuntime() at the end asserts that every
 * object was released. Run with tests/js/run.sh.
 *
 * Only modules without hardware dependencies can be linked here (Box2D).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>

JSModuleDef *athena_box2d_init(JSContext *ctx);
void athena_box2d_cleanup(JSContext *ctx);

/* <athena/math.h>, used by quickjs.c; the EE versions are approximations. */
float athena_cosf(float x) { return cosf(x); }
float athena_sinf(float x) { return sinf(x); }
float athena_tanf(float x) { return tanf(x); }
float athena_asinf(float x) { return asinf(x); }
float athena_acosf(float x) { return acosf(x); }
float athena_atan2f(float y, float x) { return atan2f(y, x); }
float athena_randomf(float min, float max) { return min + (max - min) * (float)rand() / (float)RAND_MAX; }
int athena_randomi(int min, int max) { return min + rand() % (max - min + 1); }

/* Same as src/runtime/quickjs/ath_env.c. */
JSModuleDef *athena_push_module(JSContext *ctx, JSModuleInitFunc *func, const JSCFunctionListEntry *func_list,
    int len, const char *module_name) {
    JSModuleDef *m = JS_NewCModule(ctx, module_name, func);
    if (!m)
        return NULL;
    JS_AddModuleExportList(ctx, m, func_list, len);
    return m;
}

int athena_register_class(JSContext *ctx, JSClassID *class_id, const JSClassDef *class_def) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    JS_NewClassID(class_id);
    if (JS_IsRegisteredClass(rt, *class_id))
        return 0;
    return JS_NewClass(rt, *class_id, class_def) < 0 ? -1 : 0;
}

static JSValue js_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    for (int i = 0; i < argc; i++) {
        const char *text = JS_ToCString(ctx, argv[i]);
        printf("%s%s", i ? " " : "", text ? text : "<?>");
        JS_FreeCString(ctx, text);
    }
    printf("\n");
    return JS_UNDEFINED;
}

static JSValue js_gc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JS_RunGC(JS_GetRuntime(ctx));
    return JS_UNDEFINED;
}

static char *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    char *data;

    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    *length = (size_t)ftell(file);
    fseek(file, 0, SEEK_SET);
    data = malloc(*length + 1);
    if (data && fread(data, 1, *length, file) != *length) {
        free(data);
        data = NULL;
    }
    if (data)
        data[*length] = 0;
    fclose(file);
    return data;
}

static int eval_module(JSContext *ctx, const char *code, size_t length, const char *name) {
    JSValue value = JS_Eval(ctx, code, length, name, JS_EVAL_TYPE_MODULE);
    JSContext *job_ctx;

    if (JS_IsException(value)) {
        JSValue error = JS_GetException(ctx);
        JSValue stack = JS_GetPropertyStr(ctx, error, "stack");
        const char *message = JS_ToCString(ctx, error);
        const char *trace = JS_ToCString(ctx, stack);
        printf("Uncaught %s\n%s\n", message ? message : "exception", trace ? trace : "");
        JS_FreeCString(ctx, message);
        JS_FreeCString(ctx, trace);
        JS_FreeValue(ctx, stack);
        JS_FreeValue(ctx, error);
        return -1;
    }
    JS_FreeValue(ctx, value);
    while (JS_ExecutePendingJob(JS_GetRuntime(ctx), &job_ctx) > 0)
        ;
    return 0;
}

int main(int argc, char **argv) {
    /* As generated in src/generated/js_registry.c. */
    static const char bootstrap[] = "import * as Box2D from 'Box2D'; globalThis.Box2D = Box2D;";
    JSRuntime *rt;
    JSContext *ctx;
    JSValue global, console, std;
    size_t length;
    char *code;
    int ret;

    if (argc != 2) {
        fprintf(stderr, "usage: %s script.js\n", argv[0]);
        return 2;
    }
    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);
    global = JS_GetGlobalObject(ctx);
    console = JS_NewObject(ctx);
    std = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_log, "log", 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    JS_SetPropertyStr(ctx, std, "gc", JS_NewCFunction(ctx, js_gc, "gc", 0));
    JS_SetPropertyStr(ctx, global, "std", std);
    JS_FreeValue(ctx, global);

    athena_box2d_init(ctx);
    if (eval_module(ctx, bootstrap, strlen(bootstrap), "<bootstrap>") < 0)
        return 2;
    code = read_file(argv[1], &length);
    if (!code) {
        printf("cannot read %s\n", argv[1]);
        return 2;
    }
    ret = eval_module(ctx, code, length, argv[1]);
    free(code);

    athena_box2d_cleanup(ctx);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return ret < 0 ? 1 : 0;
}
