#include <math.h>
#include <timer.h>

#include <ath_env.h>
#include <ath_gil.h>
#include <athena/graphics.h>
#include <athena/loop.h>

#include "ath_loop.h"

/* Longest vsyncInterval accepted: one frame per second on NTSC. */
#define LOOP_MAX_VSYNC_INTERVAL 60

/*
 * The frame runs from js_std_loop() through its frame handler, so timers,
 * promises and async functions keep working alongside the loop. The loop
 * only starts once the entry script has finished evaluating.
 */
typedef struct {
    JSContext *ctx;             /* context that owns the handlers, NULL when idle */
    JSValue target;             /* `this` of the handlers: the handlers object */
    JSValue update;
    JSValue draw;
    uint32_t generation;        /* changes on every run() and stop() */
    bool clear;
    Color clear_color;
    float fixed_step;           /* seconds; 0 runs update once per frame */
    int max_steps;
    uint32_t vsync_interval;
    float time_scale;           /* kept across runs until the runtime ends */
    AthenaLoopClock clock;
    uint32_t flip_vblank;       /* vblank of the last flip */
    uint64_t flip_end;          /* bus-clock ticks when the last flip returned */
    float cpu_ms;
    int steps;
    float alpha;
} LoopState;

static LoopState loop_state = {
    .target = JS_UNDEFINED,
    .update = JS_UNDEFINED,
    .draw = JS_UNDEFINED,
    .time_scale = 1.0f,
};

typedef struct {
    bool clear;
    Color clear_color;
    float max_delta;
    float fixed_step;
    int max_steps;
    uint32_t vsync_interval;
} LoopOptions;

static int loop_argc(JSContext *ctx, int argc, int minimum, int maximum,
    const char *name) {
    if (argc < minimum || argc > maximum) {
        if (minimum == maximum) {
            JS_ThrowTypeError(ctx, "%s expects %d argument%s", name, minimum,
                minimum == 1 ? "" : "s");
        } else {
            JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments",
                name, minimum, maximum);
        }
        return 0;
    }
    return 1;
}

static float loop_seconds_since(uint64_t ticks) {
    return (float)((double)(GetTimerSystemTime() - ticks) / (double)kBUSCLK);
}

/* Calls a handler with one number; the handler may replace or stop the loop. */
static int loop_call(JSContext *ctx, JSValueConst func, double value) {
    JSValue target = JS_DupValue(ctx, loop_state.target);
    JSValue callback = JS_DupValue(ctx, func);
    JSValue arg = JS_NewFloat64(ctx, value);
    JSValue ret = JS_Call(ctx, callback, target, 1, &arg);

    JS_FreeValue(ctx, callback);
    JS_FreeValue(ctx, target);
    if (JS_IsException(ret))
        return -1;
    JS_FreeValue(ctx, ret);
    return 0;
}

/*
 * Holds the flip until `vsync_interval` vblanks after the previous one. With
 * VSync on, the flip itself waits for the last of them.
 */
static void loop_wait_interval(LoopState *state) {
    uint32_t target;

    if (state->vsync_interval <= 1)
        return;
    target = state->flip_vblank + state->vsync_interval - (getVSync() ? 1 : 0);
    if ((int32_t)(graphicVblankCount() - target) >= 0)
        return;

    athena_js_gil_unlock();
    while ((int32_t)(graphicVblankCount() - target) < 0)
        graphicWaitVblankStart();
    athena_js_gil_lock();
}

static int loop_frame(JSContext *ctx, void *opaque) {
    LoopState *state = opaque;
    uint32_t generation = state->generation;

    athena_loop_clock_tick(&state->clock);
    if (state->clear)
        clearScreen(state->clear_color);

    state->steps = 0;
    if (state->fixed_step > 0.0f) {
        int steps = athena_loop_clock_steps(&state->clock, state->fixed_step,
            state->max_steps);
        while (steps-- > 0 && generation == state->generation) {
            if (!JS_IsUndefined(state->update) &&
                loop_call(ctx, state->update, state->fixed_step) < 0)
                return -1;
            state->steps++;
        }
        state->alpha = athena_loop_clock_alpha(&state->clock, state->fixed_step);
    } else {
        if (!JS_IsUndefined(state->update)) {
            if (loop_call(ctx, state->update, state->clock.delta) < 0)
                return -1;
            state->steps = 1;
        }
        state->alpha = 1.0f;
    }

    if (generation == state->generation && !JS_IsUndefined(state->draw) &&
        loop_call(ctx, state->draw, state->alpha) < 0)
        return -1;

    /* Everything since the previous flip, timers and promises included. */
    state->cpu_ms = 1000.0f * (state->flip_end ?
        loop_seconds_since(state->flip_end) :
        athena_loop_clock_since_tick(&state->clock));

    loop_wait_interval(state);
    flipScreen();
    state->flip_vblank = graphicVblankCount();
    state->flip_end = GetTimerSystemTime();
    return 0;
}

static void loop_free_handlers(JSContext *ctx) {
    JS_FreeValue(ctx, loop_state.target);
    JS_FreeValue(ctx, loop_state.update);
    JS_FreeValue(ctx, loop_state.draw);
    loop_state.target = JS_UNDEFINED;
    loop_state.update = JS_UNDEFINED;
    loop_state.draw = JS_UNDEFINED;
}

static void loop_release(JSContext *ctx) {
    js_std_set_frame_handler(JS_GetRuntime(ctx), NULL, NULL);
    loop_free_handlers(ctx);
    loop_state.ctx = NULL;
    loop_state.generation++;
}

/* Reads an optional handler; 0 on error, with an exception pending. */
static int loop_handler(JSContext *ctx, JSValueConst handlers, const char *name,
    JSValue *result) {
    JSValue value = JS_GetPropertyStr(ctx, handlers, name);

    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value) && !JS_IsFunction(ctx, value)) {
        JS_FreeValue(ctx, value);
        JS_ThrowTypeError(ctx, "Loop.run %s must be a function", name);
        return 0;
    }
    *result = value;
    return 1;
}

/*
 * Reads an optional number option into `result`, left untouched when the
 * option is absent. 0 on error, with an exception pending.
 */
static int loop_number_option(JSContext *ctx, JSValueConst options,
    const char *name, double *result) {
    JSValue value = JS_GetPropertyStr(ctx, options, name);
    int ok = 1;

    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value)) {
        if (!JS_IsNumber(value)) {
            JS_ThrowTypeError(ctx, "Loop.run %s must be a number", name);
            ok = 0;
        } else if (JS_ToFloat64(ctx, result, value)) {
            ok = 0;
        }
    }
    JS_FreeValue(ctx, value);
    return ok;
}

static int loop_options(JSContext *ctx, JSValueConst options,
    LoopOptions *result) {
    JSValue value;
    double number;

    if (JS_IsUndefined(options))
        return 1;
    if (!JS_IsObject(options) || JS_IsArray(ctx, options) ||
        JS_IsFunction(ctx, options)) {
        JS_ThrowTypeError(ctx, "Loop.run options must be an object");
        return 0;
    }

    value = JS_GetPropertyStr(ctx, options, "clear");
    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value)) {
        if (!JS_IsBool(value)) {
            JS_FreeValue(ctx, value);
            JS_ThrowTypeError(ctx, "Loop.run clear must be a boolean");
            return 0;
        }
        result->clear = JS_ToBool(ctx, value);
    }
    JS_FreeValue(ctx, value);

    value = JS_GetPropertyStr(ctx, options, "clearColor");
    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value)) {
        uint32_t color;
        if (JS_ToUint32(ctx, &color, value)) {
            JS_FreeValue(ctx, value);
            return 0;
        }
        result->clear_color = color;
    }
    JS_FreeValue(ctx, value);

    number = result->max_delta;
    if (!loop_number_option(ctx, options, "maxDelta", &number))
        return 0;
    if (!(number >= 0.0)) {
        JS_ThrowRangeError(ctx, "Loop.run maxDelta must be zero or positive");
        return 0;
    }
    result->max_delta = (float)number;

    number = result->fixed_step;
    if (!loop_number_option(ctx, options, "fixedStep", &number))
        return 0;
    if (!(number >= 0.0 && number <= 1.0)) {
        JS_ThrowRangeError(ctx, "Loop.run fixedStep must be between 0 and 1 second");
        return 0;
    }
    result->fixed_step = (float)number;

    number = result->max_steps;
    if (!loop_number_option(ctx, options, "maxSteps", &number))
        return 0;
    if (!(number >= 1.0 && number <= 100.0) || number != floor(number)) {
        JS_ThrowRangeError(ctx, "Loop.run maxSteps must be an integer between 1 and 100");
        return 0;
    }
    result->max_steps = (int)number;

    number = result->vsync_interval;
    if (!loop_number_option(ctx, options, "vsyncInterval", &number))
        return 0;
    if (!(number >= 1.0 && number <= LOOP_MAX_VSYNC_INTERVAL) ||
        number != floor(number)) {
        JS_ThrowRangeError(ctx, "Loop.run vsyncInterval must be an integer between 1 and %d",
            LOOP_MAX_VSYNC_INTERVAL);
        return 0;
    }
    result->vsync_interval = (uint32_t)number;
    return 1;
}

static JSValue loop_run(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    LoopOptions options = {
        .clear = true,
        .clear_color = GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0),
        .max_delta = ATHENA_LOOP_DEFAULT_MAX_DELTA,
        .fixed_step = 0.0f,
        .max_steps = ATHENA_LOOP_DEFAULT_MAX_STEPS,
        .vsync_interval = 1,
    };
    JSValue target = JS_UNDEFINED, update = JS_UNDEFINED, draw = JS_UNDEFINED;

    if (!loop_argc(ctx, argc, 1, 2, "Loop.run"))
        return JS_EXCEPTION;
    if (argc == 2 && !loop_options(ctx, argv[1], &options))
        return JS_EXCEPTION;
    if (loop_state.ctx && loop_state.ctx != ctx)
        return JS_ThrowInternalError(ctx, "Loop is already running in another context");
    if (!flipScreen)
        return JS_ThrowInternalError(ctx, "Graphics service is not initialized");

    if (JS_IsFunction(ctx, argv[0])) {
        update = JS_DupValue(ctx, argv[0]);
    } else if (JS_IsObject(argv[0]) && !JS_IsArray(ctx, argv[0])) {
        if (!loop_handler(ctx, argv[0], "update", &update))
            return JS_EXCEPTION;
        if (!loop_handler(ctx, argv[0], "draw", &draw)) {
            JS_FreeValue(ctx, update);
            return JS_EXCEPTION;
        }
        if (JS_IsUndefined(update) && JS_IsUndefined(draw))
            return JS_ThrowTypeError(ctx, "Loop.run handlers need update or draw");
        target = JS_DupValue(ctx, argv[0]);
    } else {
        return JS_ThrowTypeError(ctx,
            "Loop.run expects a function or an object with update and draw");
    }

    if (loop_state.ctx) {
        /* Replacing a running loop (a scene change) keeps its timing. */
        loop_free_handlers(ctx);
        loop_state.clock.max_delta = options.max_delta;
        loop_state.clock.accumulator = 0.0;
    } else {
        athena_loop_clock_reset(&loop_state.clock, options.max_delta);
        loop_state.clock.time_scale = loop_state.time_scale;
        loop_state.ctx = ctx;
        loop_state.flip_vblank = graphicVblankCount();
        loop_state.flip_end = 0;
        loop_state.cpu_ms = 0.0f;
        loop_state.steps = 0;
        loop_state.alpha = 0.0f;
        js_std_set_frame_handler(JS_GetRuntime(ctx), loop_frame, &loop_state);
    }
    loop_state.generation++;
    loop_state.target = target;
    loop_state.update = update;
    loop_state.draw = draw;
    loop_state.clear = options.clear;
    loop_state.clear_color = options.clear_color;
    loop_state.fixed_step = options.fixed_step;
    loop_state.max_steps = options.max_steps;
    loop_state.vsync_interval = options.vsync_interval;
    return JS_UNDEFINED;
}

static JSValue loop_stop(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!loop_argc(ctx, argc, 0, 0, "Loop.stop"))
        return JS_EXCEPTION;
    if (loop_state.ctx == ctx)
        loop_release(ctx);
    return JS_UNDEFINED;
}

static JSValue loop_is_running(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!loop_argc(ctx, argc, 0, 0, "Loop.isRunning"))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, loop_state.ctx == ctx);
}

static JSValue loop_set_time_scale(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    double scale;

    if (!loop_argc(ctx, argc, 1, 1, "Loop.setTimeScale"))
        return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &scale, argv[0]))
        return JS_EXCEPTION;
    if (!(scale >= 0.0 && scale <= 100.0))
        return JS_ThrowRangeError(ctx, "Loop.setTimeScale scale must be between 0 and 100");
    loop_state.time_scale = (float)scale;
    loop_state.clock.time_scale = (float)scale;
    return JS_UNDEFINED;
}

static JSValue loop_get_time_scale(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    if (!loop_argc(ctx, argc, 0, 0, "Loop.getTimeScale"))
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, loop_state.time_scale);
}

static JSValue loop_get_delta_time(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    if (!loop_argc(ctx, argc, 0, 0, "Loop.getDeltaTime"))
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, loop_state.clock.delta);
}

static JSValue loop_get_elapsed_time(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    if (!loop_argc(ctx, argc, 0, 0, "Loop.getElapsedTime"))
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, loop_state.clock.elapsed);
}

static JSValue loop_get_real_elapsed_time(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    if (!loop_argc(ctx, argc, 0, 0, "Loop.getRealElapsedTime"))
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, loop_state.clock.real_elapsed);
}

static JSValue loop_get_frame_count(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    if (!loop_argc(ctx, argc, 0, 0, "Loop.getFrameCount"))
        return JS_EXCEPTION;
    return JS_NewUint32(ctx, loop_state.clock.frames);
}

static JSValue loop_get_stats(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    JSValue stats;

    if (!loop_argc(ctx, argc, 0, 0, "Loop.getStats"))
        return JS_EXCEPTION;
    stats = JS_NewObject(ctx);
    if (JS_IsException(stats))
        return stats;
    JS_SetPropertyStr(ctx, stats, "fps", JS_NewFloat64(ctx, loop_state.clock.fps));
    JS_SetPropertyStr(ctx, stats, "frameMs",
        JS_NewFloat64(ctx, loop_state.clock.real_delta * 1000.0f));
    JS_SetPropertyStr(ctx, stats, "cpuMs", JS_NewFloat64(ctx, loop_state.cpu_ms));
    JS_SetPropertyStr(ctx, stats, "steps", JS_NewInt32(ctx, loop_state.steps));
    JS_SetPropertyStr(ctx, stats, "alpha", JS_NewFloat64(ctx, loop_state.alpha));
    return stats;
}

static const JSCFunctionListEntry loop_funcs[] = {
    JS_CFUNC_DEF("run", 2, loop_run),
    JS_CFUNC_DEF("stop", 0, loop_stop),
    JS_CFUNC_DEF("isRunning", 0, loop_is_running),
    JS_CFUNC_DEF("setTimeScale", 1, loop_set_time_scale),
    JS_CFUNC_DEF("getTimeScale", 0, loop_get_time_scale),
    JS_CFUNC_DEF("getDeltaTime", 0, loop_get_delta_time),
    JS_CFUNC_DEF("getElapsedTime", 0, loop_get_elapsed_time),
    JS_CFUNC_DEF("getRealElapsedTime", 0, loop_get_real_elapsed_time),
    JS_CFUNC_DEF("getFrameCount", 0, loop_get_frame_count),
    JS_CFUNC_DEF("getStats", 0, loop_get_stats),
};

static int loop_module_init(JSContext *ctx, JSModuleDef *module) {
    graphics_service_init();
    return JS_SetModuleExportList(ctx, module, loop_funcs, countof(loop_funcs));
}

JSModuleDef *athena_loop_init(JSContext *ctx) {
    return athena_push_module(ctx, loop_module_init, loop_funcs,
        countof(loop_funcs), "Loop");
}

void athena_loop_cleanup(JSContext *ctx) {
    /* Another context still running its loop keeps its state. */
    if (loop_state.ctx && loop_state.ctx != ctx)
        return;
    if (loop_state.ctx)
        loop_release(ctx);
    loop_state.time_scale = 1.0f;
    athena_loop_clock_reset(&loop_state.clock, ATHENA_LOOP_DEFAULT_MAX_DELTA);
}
