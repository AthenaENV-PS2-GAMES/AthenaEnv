/*
 * Host runner for module test scripts: AthenaEnv's QuickJS built for a
 * 32-bit host (NaN-boxing and the float32 values, as on the EE) with
 * AddressSanitizer, running bin/tests/<name>.js with the module globals,
 * console.log and std.gc. JS_FreeRuntime() at the end asserts that every
 * object was released. Run with tests/js/run.sh.
 *
 * Only modules without hardware dependencies can be linked here: Box2D,
 * Random, Noise, MemoryCard against the fake card of
 * tests/host/fake_libmc.h, and Debug against stubs of the GS calls it makes
 * and of the pad (counted and set from scripts, see the __ globals). The JavaScript modules (Ease, Tween) load from their sources, with a
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
#include <athena/graphics/view.h>
#include <athena/loop.h>

JSModuleDef *athena_box2d_init(JSContext *ctx);
void athena_box2d_cleanup(JSContext *ctx);
JSModuleDef *athena_memcard_init(JSContext *ctx);
JSModuleDef *athena_random_init(JSContext *ctx);
JSModuleDef *athena_matrix4_init(JSContext *ctx);
JSModuleDef *athena_quaternion_js_init(JSContext *ctx);
JSModuleDef *athena_camera3d_js_init(JSContext *ctx);
void athena_camera3d_js_cleanup(JSContext *ctx);
JSModuleDef *athena_model3d_js_init(JSContext *ctx);
JSModuleDef *athena_render3d_js_init(JSContext *ctx);
JSModuleDef *athena_lights_js_init(JSContext *ctx);
JSModuleDef *athena_scene3d_js_init(JSContext *ctx);
void athena_scene3d_js_cleanup(JSContext *ctx);
void athena_render3d_js_cleanup(JSContext *ctx);
JSModuleDef *athena_animation3d_js_init(JSContext *ctx);
void athena_animation3d_js_cleanup(JSContext *ctx);
JSModuleDef *athena_gltf3d_js_init(JSContext *ctx);
JSModuleDef *athena_camerarig3d_js_init(JSContext *ctx);
void athena_camerarig3d_js_cleanup(JSContext *ctx);
JSModuleDef *athena_tween3d_js_init(JSContext *ctx);
void athena_tween3d_js_cleanup(JSContext *ctx);
JSModuleDef *athena_collision3d_js_init(JSContext *ctx);
void athena_collision3d_js_cleanup(JSContext *ctx);
JSModuleDef *athena_physics3d_js_init(JSContext *ctx);
void athena_physics3d_js_cleanup(JSContext *ctx);
JSModuleDef *athena_particles2d_js_init(JSContext *ctx);
void athena_particles2d_js_cleanup(JSContext *ctx);
JSModuleDef *athena_particles3d_js_init(JSContext *ctx);
void athena_particles3d_js_cleanup(JSContext *ctx);
JSModuleDef *athena_noise_init(JSContext *ctx);
JSModuleDef *athena_debug_init(JSContext *ctx);
JSModuleDef *athena_profiler_js_init(JSContext *ctx);
JSModuleDef *athena_debug3d_js_init(JSContext *ctx);
JSModuleDef *athena_savegame_js_init(JSContext *ctx);
JSModuleDef *athena_meshbuilder_js_init(JSContext *ctx);
JSModuleDef *athena_voxel_js_init(JSContext *ctx);
JSModuleDef *athena_lod_js_init(JSContext *ctx);
void athena_lod_js_cleanup(JSContext *ctx);
JSModuleDef *athena_triggers3d_js_init(JSContext *ctx);
JSModuleDef *athena_nav_js_init(JSContext *ctx);
JSModuleDef *athena_audio3d_js_init(JSContext *ctx);
void athena_audio3d_js_cleanup(JSContext *ctx);
JSModuleDef *athena_sky_js_init(JSContext *ctx);
void sound_stub_init(JSContext *ctx);
void athena_voxel_js_cleanup(JSContext *ctx);
void athena_debug3d_js_cleanup(JSContext *ctx);
void athena_profiler_js_cleanup(JSContext *ctx);
JSModuleDef *athena_camera2d_js_init(JSContext *ctx);
void athena_camera2d_js_cleanup(JSContext *ctx);
JSModuleDef *athena_sprite_js_init(JSContext *ctx);
void athena_sprite_js_cleanup(JSContext *ctx);
/* tests/js/sprite_host.c: Image and TileMap stand-ins for the Sprite binding. */
void sprite_host_init(JSContext *ctx);
JSModuleDef *athena_collision_js_init(JSContext *ctx);
void athena_collision_js_cleanup(JSContext *ctx);
/* Thread.readFileAsync() (thread/quickjs/ath_file_job.c), without the rest of Thread. */
JSValue athena_thread_read_file_async(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
#ifdef RUNNER_REAL_FONT
/*
 * runner_font: the real Font binding (quickjs/ath_font.c) over the native
 * stand-in tests/js/font_host.c, for scripts that must run the console's
 * code path (preload slices, FontRender lifetimes). The __ hooks of the Draw
 * and pad stubs are left out, so scripts take their console branch.
 */
JSModuleDef *athena_font_init(JSContext *ctx);
#endif

/*
 * Implementations of the stub headers tests/host/stubs/athena/graphics.h and
 * tests/js/stub/athena/gamepad.h, for the Debug binding: calls are counted,
 * the pad is set by the script.
 */
typedef struct GSCONTEXT GSCONTEXT;
typedef unsigned int StubColor;   /* Color of tests/host/stubs/athena/graphics.h */
static char stub_gs;
static int stub_sprites, stub_lines, stub_circles, stub_line_lists;
static float stub_first_line[2], stub_last_circle[4];
static unsigned int stub_pad;
GSCONTEXT *getGSGLOBAL(void) { return (GSCONTEXT *)&stub_gs; }
void draw_sprite(float x, float y, int width, int height, StubColor color) {
    if (width < 1 || height < 1) {
        fprintf(stderr, "draw_sprite with an empty size %dx%d\n", width, height);
        abort();
    }
    stub_sprites++;
}
void draw_rect_f(float x, float y, float width, float height, StubColor color) {
    if (!(width > 0.0f) || !(height > 0.0f)) {
        fprintf(stderr, "draw_rect_f with an empty size %gx%g\n", width, height);
        abort();
    }
    stub_sprites++;
}
void draw_line(float x, float y, float x2, float y2, StubColor color) {
    if (!stub_lines++) {
        stub_first_line[0] = x;
        stub_first_line[1] = y;
    }
}
/* prim_line of tests/host/stubs/athena/graphics.h. */
typedef struct { float x, y, x2, y2; StubColor rgba; } StubLine;
void draw_line_list(float x, float y, StubLine *list, int list_size) {
    if (list_size <= 0 || list_size > 256) {
        fprintf(stderr, "draw_line_list with %d lines\n", list_size);
        abort();
    }
    stub_line_lists++;
    for (int i = 0; i < list_size; i++)
        draw_line(x + list[i].x, y + list[i].y, x + list[i].x2, y + list[i].y2, list[i].rgba);
}
static int stub_quads;
static StubColor stub_last_quad[4];
void draw_quad_gouraud(float x, float y, float x2, float y2, float x3, float y3, float x4, float y4,
    StubColor c1, StubColor c2, StubColor c3, StubColor c4) {
    stub_quads++; stub_last_quad[0] = c1; stub_last_quad[1] = c2; stub_last_quad[2] = c3; stub_last_quad[3] = c4;
}
void draw_circle(float x, float y, float radius, StubColor color, unsigned char filled) {
    stub_circles++;
    stub_last_circle[0] = x;
    stub_last_circle[1] = y;
    stub_last_circle[2] = radius;
    stub_last_circle[3] = filled;
}
int athena_gamepad_core_init(void) { return 0; }
unsigned short athena_gamepad_core_peek(int port) { return port == 0 ? (unsigned short)stub_pad : 0; }

/*
 * __nativeDraws(): { sprites, lines, lineLists, circles, firstLine: [x, y],
 * lastCircle: [x, y, radius, filled] } drawn by C since the last call;
 * `lines` counts the lines of draw_line_list() batches too.
 */
static JSValue js_native_draws(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue counts = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, counts, "sprites", JS_NewInt32(ctx, stub_sprites));
    JS_SetPropertyStr(ctx, counts, "lines", JS_NewInt32(ctx, stub_lines));
    JS_SetPropertyStr(ctx, counts, "circles", JS_NewInt32(ctx, stub_circles));
    JS_SetPropertyStr(ctx, counts, "lineLists", JS_NewInt32(ctx, stub_line_lists));
    JS_SetPropertyStr(ctx, counts, "quads", JS_NewInt32(ctx, stub_quads));
    JS_SetPropertyStr(ctx, counts, "lastQuadTop", JS_NewUint32(ctx, stub_last_quad[0]));
    JSValue first = JS_NewArray(ctx), circle = JS_NewArray(ctx);
    for (int i = 0; i < 2; i++)
        JS_SetPropertyUint32(ctx, first, i, JS_NewFloat64(ctx, stub_first_line[i]));
    for (int i = 0; i < 4; i++)
        JS_SetPropertyUint32(ctx, circle, i, JS_NewFloat64(ctx, stub_last_circle[i]));
    JS_SetPropertyStr(ctx, counts, "firstLine", first);
    JS_SetPropertyStr(ctx, counts, "lastCircle", circle);
    stub_sprites = stub_lines = stub_circles = stub_line_lists = stub_quads = 0;
    return counts;
}

/* __setPad(buttons): what athena_gamepad_core_peek(0) returns. */
static JSValue js_set_pad(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    uint32_t buttons = 0;
    if (argc > 0 && JS_ToUint32(ctx, &buttons, argv[0]))
        return JS_EXCEPTION;
    stub_pad = buttons;
    return JS_UNDEFINED;
}
/*
 * The GS side of the 2D view (graphics/native/view_gs.c) for Camera2D: a
 * clip rectangle within a screen the script can resize with __setScreen().
 * __view() shows the view, clip and world view the camera left behind.
 */
static int stub_screen[2] = { 640, 448 };
static int stub_clip[4] = { 0, 0, 640, 448 };
void athena_view_screen_size(int *width, int *height) {
    *width = stub_screen[0];
    *height = stub_screen[1];
}
void athena_view_set_clip(int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) {
        x = y = 0;
        width = stub_screen[0];
        height = stub_screen[1];
    }
    stub_clip[0] = x;
    stub_clip[1] = y;
    stub_clip[2] = width;
    stub_clip[3] = height;
}
void athena_view_get_clip(int *x, int *y, int *width, int *height) {
    *x = stub_clip[0];
    *y = stub_clip[1];
    *width = stub_clip[2];
    *height = stub_clip[3];
}
bool athena_view_visible_bounds(AthenaRect2D *world) {
    AthenaAffine2D inverse;
    AthenaRect2D screen = { (float)stub_clip[0], (float)stub_clip[1],
        (float)(stub_clip[0] + stub_clip[2]), (float)(stub_clip[1] + stub_clip[3]) };

    if (!athena_affine_invert(&athena_view_matrix, &inverse))
        return false;
    athena_affine_bounds(&inverse, &screen, world);
    return true;
}
bool athena_view_screen_box_visible(float x0, float y0, float x1, float y1) {
    return x1 >= stub_clip[0] && x0 <= stub_clip[0] + stub_clip[2] &&
        y1 >= stub_clip[1] && y0 <= stub_clip[1] + stub_clip[3];
}
/* As graphics/native/view_gs.c, over the stub clip rectangle (Sprite batches). */
bool athena_view_culler_init(AthenaViewCuller *culler) {
    if (athena_view_kind() == ATHENA_VIEW_IDENTITY)
        return false;
    culler->m = athena_view_matrix;
    culler->rotated = athena_view_kind() == ATHENA_VIEW_ROTATED;
    culler->clip.x0 = (float)stub_clip[0];
    culler->clip.y0 = (float)stub_clip[1];
    culler->clip.x1 = (float)(stub_clip[0] + stub_clip[2]);
    culler->clip.y1 = (float)(stub_clip[1] + stub_clip[3]);
    return true;
}

static JSValue js_view(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue view = JS_NewObject(ctx), clip = JS_NewArray(ctx);
    AthenaAffine2D world;
    const float m[6] = { athena_view_matrix.xx, athena_view_matrix.xy, athena_view_matrix.yx,
        athena_view_matrix.yy, athena_view_matrix.tx, athena_view_matrix.ty };
    static const char *const names[6] = { "xx", "xy", "yx", "yy", "tx", "ty" };

    for (int i = 0; i < 6; i++)
        JS_SetPropertyStr(ctx, view, names[i], JS_NewFloat64(ctx, m[i]));
    JS_SetPropertyStr(ctx, view, "kind", JS_NewInt32(ctx, athena_view_kind()));
    for (int i = 0; i < 4; i++)
        JS_SetPropertyUint32(ctx, clip, i, JS_NewInt32(ctx, stub_clip[i]));
    JS_SetPropertyStr(ctx, view, "clip", clip);
    JS_SetPropertyStr(ctx, view, "world", athena_view_get_world(&world) ?
        JS_NewFloat64(ctx, world.tx) : JS_NULL);
    return view;
}

static JSValue js_set_screen(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 2 || JS_ToInt32(ctx, &stub_screen[0], argv[0]) || JS_ToInt32(ctx, &stub_screen[1], argv[1]))
        return JS_EXCEPTION;
    return JS_UNDEFINED;
}

/* __runNativeSystems(phase, value, realValue): the C systems, for the Loop stand-in. */
static JSValue js_run_native_systems(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    int32_t phase;
    double value, real;

    if (argc < 3 || JS_ToInt32(ctx, &phase, argv[0]) || JS_ToFloat64(ctx, &value, argv[1]) ||
        JS_ToFloat64(ctx, &real, argv[2]))
        return JS_EXCEPTION;
    if (phase < 0 || phase >= ATHENA_LOOP_PHASE_COUNT)
        return JS_ThrowRangeError(ctx, "bad phase %d", phase);
    if (athena_loop_systems_run((AthenaLoopPhase)phase, (float)value, (float)real, NULL) < 0)
        return JS_EXCEPTION;   /* the system left its exception pending */
    return JS_UNDEFINED;
}

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

static char *read_file(const char *path, size_t *length);

/* std.loadFile(path): the file's text, or null (Scene.Assets data). */
static JSValue js_load_file(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *path = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
    size_t length = 0;
    char *text;
    JSValue result;

    if (!path)
        return JS_EXCEPTION;
    text = read_file(path, &length);
    JS_FreeCString(ctx, path);
    if (!text)
        return JS_NULL;
    result = JS_NewStringLen(ctx, text, length);
    free(text);
    return result;
}

static JSValue js_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    for (int i = 0; i < argc; i++) {
        size_t length = 0;
        const char *text = JS_ToCStringLen(ctx, &length, argv[i]);
        printf("%s%s", i ? " " : "", text ? text : "<?>");
        /* Kept like the runtime's print does, for Debug.console(). */
        if (i)
            athena_runtime_output(" ", 1);
        if (text)
            athena_runtime_output(text, length);
        JS_FreeCString(ctx, text);
    }
    printf("\n");
    athena_runtime_output("\n", 1);
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
    { "Debug", "../src/modules/debug/js/debug.js" },
    { "Profiler", "../src/modules/profiler/js/profiler.js" },
    { "Bench", "../src/modules/bench/js/bench.js" },
    { "Input", "../src/modules/input/js/input.js" },
    { "Replay", "../src/modules/replay/js/replay.js" },
    { "SaveGame", "../src/modules/savegame/js/savegame.js" },
    { "Assets3D", "../src/modules/assets3d/js/assets3d.js" },
    { "Triggers3D", "../src/modules/triggers3d/js/triggers3d.js" },
    { "Scene", "../src/modules/scene/js/scene.js" },
    { "Draw", "../tests/js/stub/Draw.js" },
#ifndef RUNNER_REAL_FONT
    { "Font", "../tests/js/stub/Font.js" },
#endif
    { "Screen", "../tests/js/stub/Screen.js" },
    { "System", "../tests/js/stub/System.js" },
    { "Color", "../tests/js/stub/Color.js" },
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

static int run_script(int argc, char **argv) {
    /* As generated in src/generated/js_registry.c. */
    static const char bootstrap[] = "import * as Box2D from 'Box2D'; globalThis.Box2D = Box2D;"
        "import * as MemoryCard from 'MemoryCard'; globalThis.MemoryCard = MemoryCard;"
        "import * as Random from 'Random'; globalThis.Random = Random;"
        "import * as Noise from 'Noise'; globalThis.Noise = Noise;"
        "import * as Ease from 'Ease'; globalThis.Ease = Ease;"
        "import * as Tween from 'Tween'; globalThis.Tween = Tween;"
        "import * as Debug from 'Debug'; globalThis.Debug = Debug;"
        "import * as Camera2D from 'Camera2D'; globalThis.Camera2D = Camera2D;"
        "import * as Image from 'Image'; globalThis.Image = Image.Image;"
        "import * as TileMap from 'TileMap'; globalThis.TileMap = TileMap;"
        "import * as Sprite from 'Sprite'; globalThis.Sprite = Sprite;"
        "import * as Collision from 'Collision'; globalThis.Collision = Collision;"
        "import * as Scene from 'Scene'; globalThis.Scene = Scene.Scene;";
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
    /* Unbuffered: an abort (a leak assertion, a sanitizer) keeps what was printed. */
    setvbuf(stdout, NULL, _IONBF, 0);
    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);
    JS_SetModuleLoaderFunc(rt, NULL, load_module, NULL);
    global = JS_GetGlobalObject(ctx);
    console = JS_NewObject(ctx);
    std = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_log, "log", 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    JS_SetPropertyStr(ctx, std, "gc", JS_NewCFunction(ctx, js_gc, "gc", 0));
    JS_SetPropertyStr(ctx, std, "loadFile", JS_NewCFunction(ctx, js_load_file, "loadFile", 1));
    JS_SetPropertyStr(ctx, global, "std", std);
    JS_SetPropertyStr(ctx, global, "setTimeout", JS_NewCFunction(ctx, js_set_timeout, "setTimeout", 2));
    JS_FreeValue(ctx, global);

    athena_box2d_init(ctx);
    memcard_host_init();
    athena_js_job_class_init(ctx);   /* as the Thread module does */
    {
        JSValue thread = JS_NewObject(ctx), global_object = JS_GetGlobalObject(ctx);

        JS_SetPropertyStr(ctx, thread, "readFileAsync",
            JS_NewCFunction(ctx, athena_thread_read_file_async, "readFileAsync", 2));
        JS_SetPropertyStr(ctx, global_object, "Thread", thread);
        JS_FreeValue(ctx, global_object);
    }
    athena_memcard_init(ctx);
    athena_random_init(ctx);
    athena_matrix4_init(ctx);
    athena_quaternion_js_init(ctx);
    athena_camera3d_js_init(ctx);
    athena_model3d_js_init(ctx);
    athena_render3d_js_init(ctx);
    athena_lights_js_init(ctx);
    athena_scene3d_js_init(ctx);
    athena_animation3d_js_init(ctx);
    athena_gltf3d_js_init(ctx);
    athena_camerarig3d_js_init(ctx);
    athena_tween3d_js_init(ctx);
    athena_collision3d_js_init(ctx);
    athena_physics3d_js_init(ctx);
    athena_particles2d_js_init(ctx);
    athena_particles3d_js_init(ctx);
    athena_noise_init(ctx);
    athena_debug_init(ctx);
    athena_profiler_js_init(ctx);
    athena_debug3d_js_init(ctx);
    athena_savegame_js_init(ctx);
    athena_meshbuilder_js_init(ctx);
    athena_voxel_js_init(ctx);
    athena_lod_js_init(ctx);
    athena_triggers3d_js_init(ctx);
    athena_nav_js_init(ctx);
    sound_stub_init(ctx);
    athena_audio3d_js_init(ctx);
    athena_sky_js_init(ctx);
    athena_camera2d_js_init(ctx);
    sprite_host_init(ctx);
    athena_sprite_js_init(ctx);
    athena_collision_js_init(ctx);
    global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__view", JS_NewCFunction(ctx, js_view, "__view", 0));
    JS_SetPropertyStr(ctx, global, "__setScreen", JS_NewCFunction(ctx, js_set_screen, "__setScreen", 2));
    JS_SetPropertyStr(ctx, global, "__runNativeSystems",
        JS_NewCFunction(ctx, js_run_native_systems, "__runNativeSystems", 3));
    JS_FreeValue(ctx, global);
#ifdef RUNNER_REAL_FONT
    athena_font_init(ctx);
    (void)js_native_draws;
    (void)js_set_pad;
#else
    global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__nativeDraws", JS_NewCFunction(ctx, js_native_draws, "__nativeDraws", 0));
    JS_SetPropertyStr(ctx, global, "__setPad", JS_NewCFunction(ctx, js_set_pad, "__setPad", 1));
    JS_FreeValue(ctx, global);
#endif
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
    athena_collision_js_cleanup(ctx);
    athena_sprite_js_cleanup(ctx);
    athena_camera2d_js_cleanup(ctx);
    athena_scene3d_js_cleanup(ctx);
    athena_render3d_js_cleanup(ctx);
    athena_camera3d_js_cleanup(ctx);
    athena_profiler_js_cleanup(ctx);
    athena_debug3d_js_cleanup(ctx);
    athena_voxel_js_cleanup(ctx);
    athena_lod_js_cleanup(ctx);
    athena_audio3d_js_cleanup(ctx);
    athena_animation3d_js_cleanup(ctx);
    athena_camerarig3d_js_cleanup(ctx);
    athena_tween3d_js_cleanup(ctx);
    athena_collision3d_js_cleanup(ctx);
    athena_physics3d_js_cleanup(ctx);
    athena_particles2d_js_cleanup(ctx);
    athena_particles3d_js_cleanup(ctx);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return ret < 0 ? 1 : 0;
}

/* Repeat with a fresh runtime to exercise process-wide JSClassID reuse. */
int main(int argc, char **argv) {
    const char *repeat = getenv("ATHENA_TEST_REPEAT");
    int count = repeat ? atoi(repeat) : 1;
    if (count < 1 || count > 10) return 2;
    for (int i = 0; i < count; i++) {
        int result = run_script(argc, argv);
        if (result) return result;
    }
    return 0;
}
