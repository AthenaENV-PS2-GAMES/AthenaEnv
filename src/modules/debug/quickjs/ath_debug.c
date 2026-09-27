#include <math.h>
#include <stdint.h>
#include <string.h>

#include <ath_env.h>
#include <athena/color.h>
#include <athena/debug_overlay.h>
#include <athena/gamepad.h>
#include <athena/graphics.h>
#include <athena/graphics/view.h>
#include "ath_debug.h"

/*
 * DebugNative: the parts of the Debug module that run per frame or over
 * many elements, used by its JavaScript side (js/debug.js). The frame-time
 * graph is drawn here, bar by bar, in one call; the on-screen console reads
 * the running script's output straight from the runtime buffer.
 *
 * The samples belong to the context: a hidden object bound to every function
 * as function data, freed with the context (workers and std.reload()).
 */

/* Rows of text returned by output(); 16 KiB covers the runtime buffer. */
#define DEBUG_OUTPUT_MAX 16384

/* Per-context state: the samples and the bars of the last graph() call. */
typedef struct {
    AthenaDebugGraph graph;
    AthenaDebugBar bars[ATHENA_DEBUG_GRAPH_CAPACITY];   /* scratch, off the stack */
    bool pad_ready;     /* athena_gamepad_core_init() done for pad() */
    AthenaDebugShapes *shapes;  /* allocated by the first shape (about 57 KB) */
    /* Lines batched for draw_line_list(); off the stack (8 KB JS stacks). */
    prim_line *lines;
    int line_count;
} DebugState;

/* Lines per draw_line_list() call: one GS packet header for all of them. */
#define DEBUG_LINE_BATCH 256

static JSClassID debug_graph_class_id;

static void debug_graph_finalizer(JSRuntime *rt, JSValue value) {
    DebugState *debug = JS_GetOpaque(value, debug_graph_class_id);

    if (debug) {
        js_free_rt(rt, debug->shapes);
        js_free_rt(rt, debug->lines);
    }
    js_free_rt(rt, debug);
}

static JSClassDef debug_graph_class = {
    "DebugGraph",
    .finalizer = debug_graph_finalizer,
};

static DebugState *debug_state(JSValueConst *func_data) {
    return JS_GetOpaque(func_data[0], debug_graph_class_id);
}

static AthenaDebugGraph *debug_graph(JSValueConst *func_data) {
    return &debug_state(func_data)->graph;
}

/* A finite number, or `fallback` when the argument is absent. */
static int debug_number(JSContext *ctx, int argc, JSValueConst *argv,
    int index, double fallback, double *out, const char *name) {
    if (index >= argc || JS_IsUndefined(argv[index])) {
        *out = fallback;
        return 0;
    }
    if (JS_ToFloat64(ctx, out, argv[index]))
        return -1;
    if (!isfinite(*out)) {
        JS_ThrowRangeError(ctx, "%s arguments must be finite numbers", name);
        return -1;
    }
    return 0;
}

/*
 * A finite float for the per-shape calls: int and float32 values are read by
 * their tag and bits (no soft-float double on the EE, whose FPU also cannot
 * tell Infinity by comparing); other numbers go through a double.
 */
static int debug_float(JSContext *ctx, JSValueConst value, float *out,
    const char *name) {
    uint32_t bits;
    double number;

    switch (JS_VALUE_GET_TAG(value)) {
    case JS_TAG_INT:
        *out = (float)JS_VALUE_GET_INT(value);
        return 0;
    case JS_CUSTOM_TAG_FLOAT32:
        *out = JS_VALUE_GET_FLOAT32(value);
        memcpy(&bits, out, sizeof(bits));
        if ((bits & 0x7F800000u) != 0x7F800000u)
            return 0;
        break;
    default:
        if (JS_ToFloat64(ctx, &number, value))
            return -1;
        if (isfinite(number) && fabs(number) <= 1e30) {
            *out = (float)number;
            return 0;
        }
        break;
    }
    JS_ThrowRangeError(ctx, "%s arguments must be finite numbers", name);
    return -1;
}

static JSValue debug_record(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic, JSValue *func_data) {
    double frame_ms, cpu_ms;

    if (debug_number(ctx, argc, argv, 0, 0, &frame_ms, "DebugNative.record") ||
        debug_number(ctx, argc, argv, 1, 0, &cpu_ms, "DebugNative.record"))
        return JS_EXCEPTION;
    athena_debug_graph_push(debug_graph(func_data), (float)frame_ms,
        (float)cpu_ms);
    return JS_UNDEFINED;
}

static JSValue debug_reset(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic, JSValue *func_data) {
    athena_debug_graph_reset(debug_graph(func_data));
    return JS_UNDEFINED;
}

static void debug_set_number(JSContext *ctx, JSValue object, const char *key,
    double value) {
    JS_DefinePropertyValueStr(ctx, object, key, JS_NewFloat64(ctx, value),
        JS_PROP_C_W_E);
}

/* stats(last?): { samples, frameAvg, frameMax, cpuAvg, cpuMax }. */
static JSValue debug_stats(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic, JSValue *func_data) {
    AthenaDebugGraphStats stats;
    double last;
    JSValue object;

    if (debug_number(ctx, argc, argv, 0, 0, &last, "DebugNative.stats"))
        return JS_EXCEPTION;
    athena_debug_graph_stats(debug_graph(func_data),
        last > 0 ? (uint32_t)(last < 1e9 ? last : 1e9) : 0, &stats);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    debug_set_number(ctx, object, "samples", stats.samples);
    debug_set_number(ctx, object, "frameAvg", stats.frame_avg);
    debug_set_number(ctx, object, "frameMax", stats.frame_max);
    debug_set_number(ctx, object, "cpuAvg", stats.cpu_avg);
    debug_set_number(ctx, object, "cpuMax", stats.cpu_max);
    return object;
}

/*
 * Line batching: draw_line() sends a whole packet (tags and a GIF header) per
 * line; draw_line_list() shares one header, about 2 of 5 qwords per line.
 * Other primitives flush the batch first, so the drawing order is kept.
 */
/* The batch buffer, allocated on first use: 0, or -1 with an exception. */
static int debug_lines_ready(JSContext *ctx, DebugState *debug) {
    if (!debug->lines) {
        debug->lines = js_malloc(ctx, DEBUG_LINE_BATCH * sizeof(*debug->lines));
        if (!debug->lines)
            return -1;
    }
    return 0;
}

static void debug_lines_flush(DebugState *debug) {
    if (debug->line_count) {
        draw_line_list(0.0f, 0.0f, debug->lines, debug->line_count);
        debug->line_count = 0;
    }
}

static void debug_line(DebugState *debug, float x1, float y1, float x2, float y2,
    Color color) {
    prim_line *line;

    if (debug->line_count == DEBUG_LINE_BATCH)
        debug_lines_flush(debug);
    line = &debug->lines[debug->line_count++];
    line->x = x1;
    line->y = y1;
    line->x2 = x2;
    line->y2 = y2;
    line->rgba = color;
}

/*
 * graph(x, y, width, height, budgetMs?, scaleMs?): draws the bars of the
 * newest samples and the budget line; returns how many bars. The budget
 * defaults to a 60 Hz frame and the scale to two budgets.
 */
static JSValue debug_draw_graph(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *func_data) {
    static const uint32_t level_rgb[4][3] = {
        { 60, 200, 90 }, { 230, 200, 40 }, { 235, 70, 50 }, { 210, 70, 220 },
    };
    const char *name = "DebugNative.graph";
    DebugState *debug = debug_state(func_data);
    AthenaDebugBar *bars = debug->bars;
    double x, y, width, height, budget, scale;
    size_t count;

    if (debug_number(ctx, argc, argv, 0, 0, &x, name) ||
        debug_number(ctx, argc, argv, 1, 0, &y, name) ||
        debug_number(ctx, argc, argv, 2, 120, &width, name) ||
        debug_number(ctx, argc, argv, 3, 40, &height, name) ||
        debug_number(ctx, argc, argv, 4, 1000.0 / 60.0, &budget, name))
        return JS_EXCEPTION;
    if (debug_number(ctx, argc, argv, 5, budget * 2, &scale, name))
        return JS_EXCEPTION;
    if (!getGSGLOBAL())
        return JS_ThrowInternalError(ctx, "Graphics service is not initialized");
    if (fabs(x) > 1e6 || fabs(y) > 1e6 || width > 4096 || height > 4096)
        return JS_ThrowRangeError(ctx, "%s: the box is off any screen", name);
    if (debug_lines_ready(ctx, debug) < 0)
        return JS_EXCEPTION;

    count = athena_debug_graph_bars(debug_graph(func_data), (float)x, (float)y,
        (float)width, (float)height, (float)budget, (float)scale, bars,
        ATHENA_DEBUG_GRAPH_CAPACITY);
    for (size_t i = 0; i < count; i++) {
        /* Whole pixels: bars touch without gaps or overlaps. */
        int left = (int)floorf(bars[i].x);
        int right = (int)floorf(bars[i].x + bars[i].width);
        int bar_height = (int)(bars[i].height + 0.5f);
        const uint32_t *rgb = level_rgb[bars[i].level];

        int bar_width = right > left ? right - left : 1;
        float bottom = (float)(y + height);
        Color color = (Color)athena_color_new(rgb[0], rgb[1], rgb[2], 0x80);

        if (bar_height <= 0)
            continue;
        if (bar_width <= 2) {
            /* A column of lines in the batch: one packet for the whole graph. */
            for (int column = 0; column < bar_width; column++) {
                float line_x = (float)(left + column) + 0.5f;
                debug_line(debug, line_x, bottom, line_x, bottom - (float)bar_height, color);
            }
        } else {
            debug_lines_flush(debug);
            draw_sprite((float)left, bottom - (float)bar_height, bar_width, bar_height, color);
        }
    }
    if (budget > 0 && scale > 0 && budget <= scale) {
        float line_y = (float)(y + height - height * budget / scale);
        debug_line(debug, (float)x, line_y, (float)(x + width), line_y,
            (Color)athena_color_new(255, 255, 255, 0x60));
    }
    debug_lines_flush(debug);
    return JS_NewUint32(ctx, (uint32_t)count);
}

/*
 * output(lines, columns?): the last `lines` screen lines of what the running
 * script printed, wrapped at `columns`.
 */
static JSValue debug_output(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic, JSValue *func_data) {
    const char *name = "DebugNative.output";
    double lines, columns;
    size_t length;
    const char *text;
    char *out;
    JSValue result;

    if (debug_number(ctx, argc, argv, 0, 10, &lines, name) ||
        debug_number(ctx, argc, argv, 1, 0, &columns, name))
        return JS_EXCEPTION;
    if (lines < 0 || lines > 1000 || columns < 0 || columns > 1000)
        return JS_ThrowRangeError(ctx, "%s: lines and columns must be from 0 to 1000", name);
    out = js_malloc(ctx, DEBUG_OUTPUT_MAX);
    if (!out)
        return JS_EXCEPTION;
    text = athena_runtime_output_current(&length);
    athena_debug_text_tail(text, length, (int)lines, (int)columns, out,
        DEBUG_OUTPUT_MAX);
    result = JS_NewString(ctx, out);
    js_free(ctx, out);
    return result;
}

/* A number that changes whenever the script prints: rebuild output() then. */
static JSValue debug_output_version(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *func_data) {
    return JS_NewUint32(ctx, athena_runtime_output_version());
}

/*
 * shape(kind, x, y, a, b, color, seconds, flags): queues a rect, line or
 * circle (AthenaDebugShapeKind, ATHENA_DEBUG_SHAPE_* flags). Only numbers
 * cross: no object per shape.
 */
static JSValue debug_shape(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic, JSValue *func_data) {
    const char *name = "DebugNative.shape";
    DebugState *debug = debug_state(func_data);
    AthenaDebugShape shape;
    int32_t kind, flags;
    uint32_t color;

    if (argc < 8)
        return JS_ThrowTypeError(ctx, "%s expects 8 arguments", name);
    if (JS_ToInt32(ctx, &kind, argv[0]) ||
        debug_float(ctx, argv[1], &shape.x, name) ||
        debug_float(ctx, argv[2], &shape.y, name) ||
        debug_float(ctx, argv[3], &shape.a, name) ||
        debug_float(ctx, argv[4], &shape.b, name) ||
        JS_ToUint32(ctx, &color, argv[5]) ||
        debug_float(ctx, argv[6], &shape.remaining, name) ||
        JS_ToInt32(ctx, &flags, argv[7]))
        return JS_EXCEPTION;
    if (kind < ATHENA_DEBUG_SHAPE_RECT || kind > ATHENA_DEBUG_SHAPE_CIRCLE)
        return JS_ThrowRangeError(ctx, "%s kind must be 0 (rect), 1 (line) or 2 (circle)", name);
    if (!debug->shapes) {
        debug->shapes = js_malloc(ctx, sizeof(*debug->shapes));
        if (!debug->shapes)
            return JS_EXCEPTION;
        athena_debug_shapes_reset(debug->shapes);
    }
    shape.kind = (uint8_t)kind;
    shape.flags = (uint8_t)(flags & (ATHENA_DEBUG_SHAPE_WORLD | ATHENA_DEBUG_SHAPE_FILLED));
    shape.color = color;
    athena_debug_shapes_push(debug->shapes, &shape);
    return JS_UNDEFINED;
}

/*
 * shapes(kind, float32Array, color, seconds, flags): queues one rect or line
 * per 4 floats (x, y, width, height or x1, y1, x2, y2) in one call, for
 * hitboxes of many entities. Values that are not finite are skipped.
 * Returns how many were queued.
 */
static JSValue debug_shapes_batch(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *func_data) {
    const char *name = "DebugNative.shapes";
    DebugState *debug = debug_state(func_data);
    size_t offset = 0, byte_length = 0, element_size = 0, buffer_length = 0;
    AthenaDebugShape shape;
    int32_t kind, flags;
    uint32_t color, queued = 0;
    const float *values;
    JSValue buffer;
    uint8_t *data;

    if (argc < 5)
        return JS_ThrowTypeError(ctx, "%s expects 5 arguments", name);
    if (JS_ToInt32(ctx, &kind, argv[0]) || JS_ToUint32(ctx, &color, argv[2]) ||
        debug_float(ctx, argv[3], &shape.remaining, name) ||
        JS_ToInt32(ctx, &flags, argv[4]))
        return JS_EXCEPTION;
    if (kind != ATHENA_DEBUG_SHAPE_RECT && kind != ATHENA_DEBUG_SHAPE_LINE)
        return JS_ThrowRangeError(ctx, "%s kind must be 0 (rect) or 1 (line)", name);
    /* The array last: nothing after this point runs JavaScript. */
    if (JS_GetTypedArrayType(argv[1]) != JS_TYPED_ARRAY_FLOAT32)
        return JS_ThrowTypeError(ctx, "%s expects a Float32Array", name);
    buffer = JS_GetTypedArrayBuffer(ctx, argv[1], &offset, &byte_length, &element_size);
    if (JS_IsException(buffer))
        return JS_EXCEPTION;
    data = JS_GetArrayBuffer(ctx, &buffer_length, buffer);
    JS_FreeValue(ctx, buffer);
    if (!data)
        return JS_EXCEPTION;
    if (offset + byte_length > buffer_length)
        return JS_ThrowRangeError(ctx, "%s: the array is out of bounds of its buffer", name);
    if ((byte_length / sizeof(float)) % 4)
        return JS_ThrowRangeError(ctx, "%s: the array length must be a multiple of 4", name);
    if (!debug->shapes) {
        debug->shapes = js_malloc(ctx, sizeof(*debug->shapes));
        if (!debug->shapes)
            return JS_EXCEPTION;
        athena_debug_shapes_reset(debug->shapes);
    }
    values = (const float *)(data + offset);
    shape.kind = (uint8_t)kind;
    shape.flags = (uint8_t)(flags & (ATHENA_DEBUG_SHAPE_WORLD | ATHENA_DEBUG_SHAPE_FILLED));
    shape.color = color;
    for (size_t i = 0; i + 4 <= byte_length / sizeof(float); i += 4) {
        uint32_t bits[4];
        memcpy(bits, &values[i], sizeof(bits));
        /* Exponent all ones: Infinity or NaN (the EE cannot tell by comparing). */
        if ((bits[0] & 0x7F800000u) == 0x7F800000u || (bits[1] & 0x7F800000u) == 0x7F800000u ||
            (bits[2] & 0x7F800000u) == 0x7F800000u || (bits[3] & 0x7F800000u) == 0x7F800000u)
            continue;
        shape.x = values[i];
        shape.y = values[i + 1];
        shape.a = values[i + 2];
        shape.b = values[i + 3];
        athena_debug_shapes_push(debug->shapes, &shape);
        queued++;
    }
    return JS_NewUint32(ctx, queued);
}

static JSValue debug_shapes_age(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *func_data) {
    DebugState *debug = debug_state(func_data);
    float dt;

    if (argc < 1 || debug_float(ctx, argv[0], &dt, "DebugNative.shapesAge"))
        return argc < 1 ? JS_ThrowTypeError(ctx, "DebugNative.shapesAge expects dt") :
            JS_EXCEPTION;
    if (debug->shapes)
        athena_debug_shapes_age(debug->shapes, dt);
    return JS_UNDEFINED;
}

/*
 * shapesDraw(viewX, viewY, scale, draw?): draws the queue (unless `draw` is
 * false: hidden, the shapes still expire), world shapes through the view,
 * then drops the ones whose time is up. Returns how many remain.
 *
 * While a camera publishes the world view of the frame (Camera2D.current,
 * athena/graphics/view.h), world shapes go through it instead, rotation and
 * zoom included, and the view arguments are ignored.
 */
static JSValue debug_shapes_draw(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *func_data) {
    const char *name = "DebugNative.shapesDraw";
    DebugState *debug = debug_state(func_data);
    AthenaDebugShapes *shapes = debug->shapes;
    float view_x, view_y, view_scale;
    bool draw = argc < 4 || JS_ToBool(ctx, argv[3]);
    AthenaAffine2D world_view, screen_view;
    bool camera = athena_view_get_world(&world_view);
    /* Which view is set: -1 none yet, 0 screen, 1 world. */
    int applied = -1;

    if (argc < 3)
        return JS_ThrowTypeError(ctx, "%s expects viewX, viewY and scale", name);
    if (debug_float(ctx, argv[0], &view_x, name) ||
        debug_float(ctx, argv[1], &view_y, name) ||
        debug_float(ctx, argv[2], &view_scale, name))
        return JS_EXCEPTION;
    if (!shapes || !shapes->count)
        return JS_NewInt32(ctx, 0);
    if (draw && !getGSGLOBAL())
        return JS_ThrowInternalError(ctx, "Graphics service is not initialized");
    if (draw && debug_lines_ready(ctx, debug) < 0)
        return JS_EXCEPTION;
    athena_view_get(&screen_view);

    for (uint32_t i = 0; draw && i < shapes->count; i++) {
        const AthenaDebugShape *shape = athena_debug_shapes_at(shapes, i);
        bool world = shape->flags & ATHENA_DEBUG_SHAPE_WORLD;
        /* Under a camera, world shapes keep world coordinates: the view maps them. */
        bool mapped = world && camera;
        float scale = world && !mapped ? view_scale : 1.0f;
        float ox = world && !mapped ? view_x : 0.0f;
        float oy = world && !mapped ? view_y : 0.0f;

        if (camera && applied != (int)mapped) {
            debug_lines_flush(debug);
            athena_view_set(mapped ? &world_view : &screen_view);
            applied = mapped;
        }
        float x = (shape->x - ox) * scale, y = (shape->y - oy) * scale;
        Color color = (Color)shape->color;

        switch (shape->kind) {
        case ATHENA_DEBUG_SHAPE_RECT: {
            float w = shape->a * scale, h = shape->b * scale;
            if (shape->flags & ATHENA_DEBUG_SHAPE_FILLED) {
                /* draw_sprite takes whole, positive sizes. */
                float left = w < 0 ? x + w : x, top = h < 0 ? y + h : y;
                int width = (int)fabsf(w), height = (int)fabsf(h);
                if (mapped) {
                    /* World units through the camera: fractions show once zoomed. */
                    if (w != 0.0f && h != 0.0f) {
                        debug_lines_flush(debug);
                        draw_rect_f(left, top, fabsf(w), fabsf(h), color);
                    }
                } else if (width >= 1 && height >= 1) {
                    debug_lines_flush(debug);
                    draw_sprite(left, top, width, height, color);
                }
            } else {
                debug_line(debug, x, y, x + w, y, color);
                debug_line(debug, x + w, y, x + w, y + h, color);
                debug_line(debug, x + w, y + h, x, y + h, color);
                debug_line(debug, x, y + h, x, y, color);
            }
            break;
        }
        case ATHENA_DEBUG_SHAPE_LINE:
            debug_line(debug, x, y, (shape->a - ox) * scale, (shape->b - oy) * scale, color);
            break;
        default: {
            float radius = fabsf(shape->a * scale);
            if (radius > 0.0f) {
                debug_lines_flush(debug);
                draw_circle(x, y, radius, color,
                    (shape->flags & ATHENA_DEBUG_SHAPE_FILLED) ? 1 : 0);
            }
            break;
        }
        }
    }
    if (draw)
        debug_lines_flush(debug);
    if (applied >= 0)
        athena_view_set(&screen_view);
    athena_debug_shapes_prune(shapes);
    return JS_NewUint32(ctx, shapes->count);
}

/*
 * worldView(): the world view the current camera published this frame, as
 * [xx, xy, yx, yy, tx, ty], or null without one; for world-space texts.
 */
static JSValue debug_world_view(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *func_data) {
    AthenaAffine2D m;
    float values[6];
    JSValue array;

    if (!athena_view_get_world(&m))
        return JS_NULL;
    values[0] = m.xx; values[1] = m.xy; values[2] = m.yx;
    values[3] = m.yy; values[4] = m.tx; values[5] = m.ty;
    array = JS_NewArray(ctx);
    if (JS_IsException(array))
        return array;
    for (uint32_t i = 0; i < 6; i++) {
        if (JS_SetPropertyUint32(ctx, array, i, JS_NewFloat64(ctx, values[i])) < 0) {
            JS_FreeValue(ctx, array);
            return JS_EXCEPTION;
        }
    }
    return array;
}

static JSValue debug_shapes_clear(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *func_data) {
    AthenaDebugShapes *shapes = debug_state(func_data)->shapes;

    if (shapes)
        athena_debug_shapes_reset(shapes);
    return JS_UNDEFINED;
}

/* Shapes discarded because the queue was full, since the last clear. */
static JSValue debug_shapes_dropped(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *func_data) {
    AthenaDebugShapes *shapes = debug_state(func_data)->shapes;

    return JS_NewUint32(ctx, shapes ? shapes->dropped : 0);
}

/*
 * pad(port): buttons held on the controller of `port` (0 or 1), read without
 * touching the snapshot of Gamepad.update(), so the game's justPressed()
 * state is unaffected; 0 when no pad is ready. For the toggle shortcut.
 */
static JSValue debug_pad(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic, JSValue *func_data) {
    DebugState *debug = debug_state(func_data);
    double port;

    if (debug_number(ctx, argc, argv, 0, 0, &port, "DebugNative.pad"))
        return JS_EXCEPTION;
    if (port != 0 && port != 1)
        return JS_ThrowRangeError(ctx, "DebugNative.pad port must be 0 or 1");
    if (!debug->pad_ready) {
        /* Loads padman if the game has not: safe to call more than once. */
        athena_gamepad_core_init();
        debug->pad_ready = true;
    }
    return JS_NewUint32(ctx, athena_gamepad_core_peek((int)port));
}

static const struct {
    const char *name;
    int length;
    JSCFunctionData *func;
} debug_funcs[] = {
    { "record", 2, debug_record },
    { "reset", 0, debug_reset },
    { "stats", 1, debug_stats },
    { "graph", 6, debug_draw_graph },
    { "output", 2, debug_output },
    { "outputVersion", 0, debug_output_version },
    { "pad", 1, debug_pad },
    { "shape", 8, debug_shape },
    { "shapes", 5, debug_shapes_batch },
    { "shapesAge", 1, debug_shapes_age },
    { "shapesDraw", 4, debug_shapes_draw },
    { "shapesClear", 0, debug_shapes_clear },
    { "shapesDropped", 0, debug_shapes_dropped },
    { "worldView", 0, debug_world_view },
};

static int debug_module_init(JSContext *ctx, JSModuleDef *m) {
    DebugState *debug;
    JSValue state;

    if (athena_register_class(ctx, &debug_graph_class_id, &debug_graph_class) < 0)
        return -1;
    state = JS_NewObjectClass(ctx, debug_graph_class_id);
    if (JS_IsException(state))
        return -1;
    debug = js_mallocz(ctx, sizeof(*debug));
    if (!debug) {
        JS_FreeValue(ctx, state);
        return -1;
    }
    JS_SetOpaque(state, debug);
    for (size_t i = 0; i < countof(debug_funcs); i++) {
        JSValue func = JS_NewCFunctionData(ctx, debug_funcs[i].func,
            debug_funcs[i].length, 0, 1, &state);
        if (JS_IsException(func) ||
            JS_SetModuleExport(ctx, m, debug_funcs[i].name, func) < 0) {
            JS_FreeValue(ctx, state);
            return -1;
        }
    }
    JS_FreeValue(ctx, state);   /* the functions keep it alive */
    return 0;
}

JSModuleDef *athena_debug_init(JSContext *ctx) {
    JSModuleDef *module = athena_push_module(ctx, debug_module_init, NULL, 0,
        "DebugNative");

    if (module)
        for (size_t i = 0; i < countof(debug_funcs); i++)
            JS_AddModuleExport(ctx, module, debug_funcs[i].name);
    return module;
}
