/*
 * Host runner for module test scripts: AthenaEnv's QuickJS built for a
 * 32-bit host (NaN-boxing and the float32 values, as on the EE) with
 * AddressSanitizer, running bin/tests/<name>.js with the module globals,
 * console.log and std.gc. JS_FreeRuntime() at the end asserts that every
 * object was released. Run with tests/js/run.sh.
 *
 * Only modules without hardware dependencies can be linked here: Box2D,
 * and MemoryCard against the fake card of tests/host/fake_libmc.h. The
 * JavaScript modules (Ease, Tween) load from their sources, with a
 * JavaScript stand-in for Loop. A minimal setTimeout runs after the script,
 * for awaited MemoryCard jobs and the Loop stand-in's frames.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <ath_env.h>

JSModuleDef *athena_box2d_init(JSContext *ctx);
void athena_box2d_cleanup(JSContext *ctx);
JSModuleDef *athena_memcard_init(JSContext *ctx);
void athena_js_job_class_init(JSContext *ctx);
void memcard_host_init(void);

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

static void print_exception(JSContext *ctx) {
    JSValue error = JS_GetException(ctx);
    JSValue stack = JS_GetPropertyStr(ctx, error, "stack");
    const char *message = JS_ToCString(ctx, error);
    const char *trace = JS_ToCString(ctx, stack);
    printf("Uncaught %s\n%s\n", message ? message : "exception", trace ? trace : "");
    JS_FreeCString(ctx, message);
    JS_FreeCString(ctx, trace);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, error);
}

static void run_jobs(JSContext *ctx) {
    JSContext *job_ctx;
    int ret;

    while ((ret = JS_ExecutePendingJob(JS_GetRuntime(ctx), &job_ctx)) != 0)
        if (ret < 0)
            print_exception(job_ctx);
}

/*
 * JavaScript modules (module.json "js") by module name, compiled from their
 * sources as the runtime's loader compiles the embedded copies. Loop is a
 * JavaScript stand-in, since the real one needs the GS. Paths from bin/.
 */
static const struct {
    const char *name;
    const char *path;
} js_modules[] = {
    { "Ease", "../src/modules/ease/js/ease.js" },
    { "Tween", "../src/modules/tween/js/tween.js" },
    { "Loop", "../tests/js/stub/Loop.js" },
};

static JSModuleDef *load_module(JSContext *ctx, const char *name, void *opaque) {
    const char *path = NULL;
    JSValue func;
    JSModuleDef *module;
    size_t length;
    char *code;

    for (size_t i = 0; i < sizeof(js_modules) / sizeof(js_modules[0]); i++)
        if (strcmp(js_modules[i].name, name) == 0)
            path = js_modules[i].path;
    code = path ? read_file(path, &length) : NULL;
    if (!code) {
        JS_ThrowReferenceError(ctx, "could not load module '%s'", name);
        return NULL;
    }
    func = JS_Eval(ctx, code, length, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(code);
    if (JS_IsException(func))
        return NULL;
    module = JS_VALUE_GET_PTR(func);
    JS_FreeValue(ctx, func);
    return module;
}

static int eval_module(JSContext *ctx, const char *code, size_t length, const char *name) {
    JSValue value = JS_Eval(ctx, code, length, name, JS_EVAL_TYPE_MODULE);

    if (JS_IsException(value)) {
        print_exception(ctx);
        return -1;
    }
    JS_FreeValue(ctx, value);
    run_jobs(ctx);
    return 0;
}

/* setTimeout(func, ms): one-shot timers, run by run_timers() in due order. */
typedef struct Timer {
    JSValue func;
    double due;
    struct Timer *next;
} Timer;

static Timer *timers;

static double clock_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static JSValue js_set_timeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    double delay = 0;
    Timer *timer;

    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "setTimeout expects a function");
    if (argc > 1 && JS_ToFloat64(ctx, &delay, argv[1]))
        return JS_EXCEPTION;
    timer = malloc(sizeof(*timer));
    if (!timer)
        return JS_ThrowOutOfMemory(ctx);
    timer->func = JS_DupValue(ctx, argv[0]);
    timer->due = clock_ms() + (delay > 0 ? delay : 0);
    timer->next = timers;
    timers = timer;
    return JS_UNDEFINED;
}

static int run_timers(JSContext *ctx) {
    int ret = 0;

    while (timers) {
        Timer **earliest = &timers, *timer;
        JSValue result;
        double wait;

        for (Timer **t = &timers; *t; t = &(*t)->next)
            if ((*t)->due < (*earliest)->due)
                earliest = t;
        timer = *earliest;
        *earliest = timer->next;
        wait = timer->due - clock_ms();
        if (wait > 0)
            usleep((useconds_t)(wait * 1000));
        result = JS_Call(ctx, timer->func, JS_UNDEFINED, 0, NULL);
        JS_FreeValue(ctx, timer->func);
        free(timer);
        if (JS_IsException(result)) {
            print_exception(ctx);
            ret = -1;
        }
        JS_FreeValue(ctx, result);
        run_jobs(ctx);
    }
    return ret;
}

int main(int argc, char **argv) {
    /* As generated in src/generated/js_registry.c. */
    static const char bootstrap[] = "import * as Box2D from 'Box2D'; globalThis.Box2D = Box2D;"
        "import * as MemoryCard from 'MemoryCard'; globalThis.MemoryCard = MemoryCard;"
        "import * as Ease from 'Ease'; globalThis.Ease = Ease;"
        "import * as Tween from 'Tween'; globalThis.Tween = Tween;";
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
    JS_SetModuleLoaderFunc(rt, NULL, load_module, NULL);
    global = JS_GetGlobalObject(ctx);
    console = JS_NewObject(ctx);
    std = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_log, "log", 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    JS_SetPropertyStr(ctx, std, "gc", JS_NewCFunction(ctx, js_gc, "gc", 0));
    JS_SetPropertyStr(ctx, global, "std", std);
    JS_SetPropertyStr(ctx, global, "setTimeout", JS_NewCFunction(ctx, js_set_timeout, "setTimeout", 2));
    JS_FreeValue(ctx, global);

    athena_box2d_init(ctx);
    memcard_host_init();
    athena_js_job_class_init(ctx);   /* as the Thread module does */
    athena_memcard_init(ctx);
    if (eval_module(ctx, bootstrap, strlen(bootstrap), "<bootstrap>") < 0)
        return 2;
    code = read_file(argv[1], &length);
    if (!code) {
        printf("cannot read %s\n", argv[1]);
        return 2;
    }
    ret = eval_module(ctx, code, length, argv[1]);
    free(code);
    if (run_timers(ctx) < 0)
        ret = -1;

    athena_box2d_cleanup(ctx);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return ret < 0 ? 1 : 0;
}
