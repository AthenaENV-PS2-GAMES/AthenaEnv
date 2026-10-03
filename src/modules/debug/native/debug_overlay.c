#include <string.h>

#include <athena/debug_overlay.h>

void athena_debug_graph_reset(AthenaDebugGraph *graph) {
    memset(graph, 0, sizeof(*graph));
}

void athena_debug_graph_push(AthenaDebugGraph *graph, float frame_ms,
    float cpu_ms) {
    /* Written so that NaN also becomes 0. */
    graph->frame_ms[graph->next] = frame_ms > 0.0f ? frame_ms : 0.0f;
    graph->cpu_ms[graph->next] = cpu_ms > 0.0f ? cpu_ms : 0.0f;
    graph->next = (graph->next + 1) % ATHENA_DEBUG_GRAPH_CAPACITY;
    if (graph->count < ATHENA_DEBUG_GRAPH_CAPACITY)
        graph->count++;
}

/* Slot of the i-th of the last `n` samples, oldest first. */
static uint32_t graph_slot(const AthenaDebugGraph *graph, uint32_t n,
    uint32_t i) {
    return (graph->next + ATHENA_DEBUG_GRAPH_CAPACITY - n + i) %
        ATHENA_DEBUG_GRAPH_CAPACITY;
}

void athena_debug_graph_stats(const AthenaDebugGraph *graph, uint32_t last,
    AthenaDebugGraphStats *stats) {
    uint32_t n = last && last < graph->count ? last : graph->count;
    float frame_sum = 0.0f, cpu_sum = 0.0f;

    memset(stats, 0, sizeof(*stats));
    for (uint32_t i = 0; i < n; i++) {
        uint32_t slot = graph_slot(graph, n, i);
        float frame = graph->frame_ms[slot], cpu = graph->cpu_ms[slot];

        frame_sum += frame;
        cpu_sum += cpu;
        stats->frame_max = frame > stats->frame_max ? frame : stats->frame_max;
        stats->cpu_max = cpu > stats->cpu_max ? cpu : stats->cpu_max;
    }
    stats->samples = n;
    if (n) {
        stats->frame_avg = frame_sum / (float)n;
        stats->cpu_avg = cpu_sum / (float)n;
    }
}

size_t athena_debug_graph_bars(const AthenaDebugGraph *graph, float x,
    float y, float width, float height, float budget_ms, float scale_ms,
    AthenaDebugBar *bars, size_t max_bars) {
    size_t n = graph->count;
    float bar_width;

    if (!(width >= 1.0f) || !(height > 0.0f) || !(scale_ms > 0.0f))
        return 0;
    if (n > max_bars)
        n = max_bars;
    if (n > (size_t)width)
        n = (size_t)width;
    if (!n)
        return 0;
    bar_width = width / (float)n;
    for (size_t i = 0; i < n; i++) {
        uint32_t slot = graph_slot(graph, (uint32_t)n, (uint32_t)i);
        float cpu = graph->cpu_ms[slot], frame = graph->frame_ms[slot];
        float fraction = cpu / scale_ms;
        AthenaDebugBar *bar = &bars[i];

        fraction = fraction > 1.0f ? 1.0f : fraction;
        bar->x = x + bar_width * (float)i;
        bar->width = bar_width;
        bar->height = height * fraction;
        bar->y = y + height - bar->height;
        if (budget_ms > 0.0f && frame > budget_ms * 1.5f)
            bar->level = ATHENA_DEBUG_BAR_DROPPED;
        else if (budget_ms > 0.0f && cpu > budget_ms)
            bar->level = ATHENA_DEBUG_BAR_OVER;
        else if (budget_ms > 0.0f && cpu > budget_ms * 0.75f)
            bar->level = ATHENA_DEBUG_BAR_TIGHT;
        else
            bar->level = ATHENA_DEBUG_BAR_OK;
    }
    return n;
}

/* Characters (not bytes) of a UTF-8 run: bytes that do not continue one. */
static size_t text_chars(const char *text, size_t length) {
    size_t chars = 0;

    for (size_t i = 0; i < length; i++)
        chars += ((unsigned char)text[i] & 0xC0) != 0x80;
    return chars;
}

/* Screen lines of a text line: at least one, even when empty. */
static size_t text_rows(const char *line, size_t length, int columns) {
    size_t chars = text_chars(line, length);

    if (columns <= 0 || chars == 0)
        return 1;
    return (chars + (size_t)columns - 1) / (size_t)columns;
}

/* Byte offset after `skip` characters of `line`. */
static size_t text_skip(const char *line, size_t length, size_t skip) {
    size_t i = 0;

    while (i < length && skip) {
        i++;
        while (i < length && ((unsigned char)line[i] & 0xC0) == 0x80)
            i++;
        skip--;
    }
    return i;
}

int athena_debug_text_tail(const char *text, size_t length, int lines,
    int columns, char *out, size_t out_size) {
    size_t start, end = length, used = 0, rows = 0, skip_rows = 0;
    size_t written = 0, column = 0;
    int emitted = 0;

    if (!out_size)
        return 0;
    out[0] = '\0';
    if (lines <= 0 || !length)
        return 0;
    if (text[end - 1] == '\n')
        end--;

    /* Back from the end, whole text lines until they fill `lines` rows. */
    start = end;
    for (;;) {
        size_t line_start = start;
        while (line_start > 0 && text[line_start - 1] != '\n')
            line_start--;
        rows = text_rows(text + line_start, start - line_start, columns);
        used += rows;
        start = line_start;
        if (used >= (size_t)lines || start == 0)
            break;
        start--;   /* the '\n' before this line */
    }
    /* The first line may have more rows than room left: skip its first ones. */
    if (used > (size_t)lines)
        skip_rows = used - (size_t)lines;
    if (skip_rows) {
        size_t line_end = start;
        while (line_end < end && text[line_end] != '\n')
            line_end++;
        start += text_skip(text + start, line_end - start,
            skip_rows * (size_t)columns);
    }

    /* Forward copy, breaking every `columns` characters. */
    emitted = 1;
    for (size_t i = start; i < end; i++) {
        unsigned char c = (unsigned char)text[i];
        int starts_char = (c & 0xC0) != 0x80;

        if (c == '\r')
            continue;
        if (c == '\n') {
            column = 0;
        } else if (starts_char && columns > 0 && column == (size_t)columns) {
            if (written + 1 >= out_size)
                break;
            out[written++] = '\n';
            emitted++;
            column = 0;
        }
        if (written + 1 >= out_size)
            break;
        out[written++] = (char)c;
        if (c == '\n')
            emitted++;
        else if (starts_char)
            column++;
    }
    out[written] = '\0';
    return emitted;
}

void athena_debug_shapes_reset(AthenaDebugShapes *shapes) {
    shapes->start = 0;
    shapes->count = 0;
    shapes->dropped = 0;
}

void athena_debug_shapes_push(AthenaDebugShapes *shapes,
    const AthenaDebugShape *shape) {
    if (shapes->count == ATHENA_DEBUG_MAX_SHAPES) {
        shapes->start = (shapes->start + 1) % ATHENA_DEBUG_MAX_SHAPES;
        shapes->count--;
        shapes->dropped++;
    }
    shapes->shapes[(shapes->start + shapes->count) % ATHENA_DEBUG_MAX_SHAPES] = *shape;
    shapes->count++;
}

void athena_debug_shapes_age(AthenaDebugShapes *shapes, float dt) {
    for (uint32_t i = 0; i < shapes->count; i++)
        shapes->shapes[(shapes->start + i) % ATHENA_DEBUG_MAX_SHAPES].remaining -= dt;
}

/* In place: the write position never passes the read one. */
void athena_debug_shapes_prune(AthenaDebugShapes *shapes) {
    uint32_t kept = 0;

    for (uint32_t i = 0; i < shapes->count; i++) {
        const AthenaDebugShape *shape =
            &shapes->shapes[(shapes->start + i) % ATHENA_DEBUG_MAX_SHAPES];
        /* Written so that a NaN remaining also goes. */
        if (!(shape->remaining > 0.0f))
            continue;
        if (kept != i)
            shapes->shapes[(shapes->start + kept) % ATHENA_DEBUG_MAX_SHAPES] = *shape;
        kept++;
    }
    shapes->count = kept;
    if (!kept)
        shapes->start = 0;
}

const AthenaDebugShape *athena_debug_shapes_at(const AthenaDebugShapes *shapes,
    uint32_t i) {
    return &shapes->shapes[(shapes->start + i) % ATHENA_DEBUG_MAX_SHAPES];
}
