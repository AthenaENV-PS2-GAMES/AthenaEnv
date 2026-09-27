#ifndef ATHENA_DEBUG_OVERLAY_H
#define ATHENA_DEBUG_OVERLAY_H

#include <stddef.h>
#include <stdint.h>

/*
 * Data behind the debug overlay, without any drawing, so C games can use it
 * with their own renderer and tests can run it on the host:
 *
 *   - a ring buffer of frame samples (frame and CPU milliseconds), with
 *     averages and peaks and the bars of a frame-time graph;
 *   - the tail of a text (the script's output) as the last N screen lines,
 *     wrapped at a column count.
 *
 * (<athena/debug.h> is the core's dbgprintf, unrelated.)
 */

#define ATHENA_DEBUG_GRAPH_CAPACITY 240

typedef struct {
    float frame_ms[ATHENA_DEBUG_GRAPH_CAPACITY];
    float cpu_ms[ATHENA_DEBUG_GRAPH_CAPACITY];
    uint32_t next;          /* slot of the next sample */
    uint32_t count;         /* samples kept, up to the capacity */
} AthenaDebugGraph;

typedef struct {
    uint32_t samples;       /* how many the figures cover */
    float frame_avg, frame_max;
    float cpu_avg, cpu_max;
} AthenaDebugGraphStats;

/* How a bar compares with the frame budget. */
typedef enum {
    ATHENA_DEBUG_BAR_OK,        /* CPU time under 75% of the budget */
    ATHENA_DEBUG_BAR_TIGHT,     /* up to the budget */
    ATHENA_DEBUG_BAR_OVER,      /* CPU time above the budget */
    ATHENA_DEBUG_BAR_DROPPED,   /* the frame took over 1.5 budgets: a frame was missed */
} AthenaDebugBarLevel;

typedef struct {
    float x, y, width, height;
    AthenaDebugBarLevel level;
} AthenaDebugBar;

void athena_debug_graph_reset(AthenaDebugGraph *graph);

/* Adds a frame; the oldest is dropped when full. Negative values count as 0. */
void athena_debug_graph_push(AthenaDebugGraph *graph, float frame_ms,
    float cpu_ms);

/* Figures over the last `last` samples (all of them when 0 or more). */
void athena_debug_graph_stats(const AthenaDebugGraph *graph, uint32_t last,
    AthenaDebugGraphStats *stats);

/*
 * Bars of the newest samples in the box (x, y, width, height), oldest on the
 * left, one per sample and at least one pixel wide: up to `max_bars`, and
 * fewer when the box is narrow. A bar is `cpu_ms` tall, scaled so that
 * `scale_ms` fills the box, and grows up from its bottom. Returns how many.
 */
size_t athena_debug_graph_bars(const AthenaDebugGraph *graph, float x,
    float y, float width, float height, float budget_ms, float scale_ms,
    AthenaDebugBar *bars, size_t max_bars);

/*
 * Queue of timed shapes (hitboxes, rays...): a ring of at most
 * ATHENA_DEBUG_MAX_SHAPES, dropping the oldest when full, so a shape added
 * per entity per frame costs no allocation and cannot fill the heap.
 */
#define ATHENA_DEBUG_MAX_SHAPES 2048

typedef enum {
    ATHENA_DEBUG_SHAPE_RECT,    /* x, y, a = width, b = height */
    ATHENA_DEBUG_SHAPE_LINE,    /* x, y to a, b */
    ATHENA_DEBUG_SHAPE_CIRCLE,  /* x, y, a = radius */
} AthenaDebugShapeKind;

#define ATHENA_DEBUG_SHAPE_WORLD  0x1   /* through the view, not screen space */
#define ATHENA_DEBUG_SHAPE_FILLED 0x2   /* rect and circle */

typedef struct {
    float x, y, a, b;
    float remaining;        /* seconds; drawn once more when it reaches 0 */
    uint32_t color;
    uint8_t kind;
    uint8_t flags;
} AthenaDebugShape;

typedef struct {
    AthenaDebugShape shapes[ATHENA_DEBUG_MAX_SHAPES];
    uint32_t start;         /* ring index of the oldest */
    uint32_t count;
    uint32_t dropped;       /* discarded because the queue was full */
} AthenaDebugShapes;

void athena_debug_shapes_reset(AthenaDebugShapes *shapes);
/* Adds a shape; when full the oldest goes and `dropped` counts it. */
void athena_debug_shapes_push(AthenaDebugShapes *shapes,
    const AthenaDebugShape *shape);
/* Takes `dt` seconds from every shape. */
void athena_debug_shapes_age(AthenaDebugShapes *shapes, float dt);
/* Removes the shapes whose time is up (remaining <= 0), keeping the order. */
void athena_debug_shapes_prune(AthenaDebugShapes *shapes);
/* The i-th shape, oldest first (i < count). */
const AthenaDebugShape *athena_debug_shapes_at(const AthenaDebugShapes *shapes,
    uint32_t i);

/*
 * The last `lines` screen lines of `text`, wrapped every `columns`
 * characters (UTF-8 aware; 0 does not wrap), joined by '\n' into `out`
 * (always terminated, cut at `out_size`). A trailing newline in `text` does
 * not count as an empty line, and '\r' is dropped. Returns the lines written.
 */
int athena_debug_text_tail(const char *text, size_t length, int lines,
    int columns, char *out, size_t out_size);

#endif /* ATHENA_DEBUG_OVERLAY_H */
