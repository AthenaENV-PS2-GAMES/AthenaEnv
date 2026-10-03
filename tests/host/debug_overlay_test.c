/* Host test of the debug overlay data: frame-time ring buffer, figures, graph bars and the text tail of the on-screen console. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <athena/debug_overlay.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_graph(void) {
    static AthenaDebugGraph graph;
    AthenaDebugGraphStats stats;
    AthenaDebugBar bars[ATHENA_DEBUG_GRAPH_CAPACITY];
    size_t n;

    athena_debug_graph_reset(&graph);
    athena_debug_graph_stats(&graph, 0, &stats);
    CHECK(stats.samples == 0 && stats.cpu_avg == 0.0f, "empty stats");
    CHECK(athena_debug_graph_bars(&graph, 0, 0, 100, 50, 16.7f, 33.3f, bars, 240) == 0, "no bars without samples");

    athena_debug_graph_push(&graph, 16.0f, 4.0f);
    athena_debug_graph_push(&graph, 17.0f, 8.0f);
    athena_debug_graph_push(&graph, -1.0f, NAN);   /* clamped to 0 */
    athena_debug_graph_stats(&graph, 0, &stats);
    CHECK(stats.samples == 3 && fabsf(stats.cpu_avg - 4.0f) < 1e-5f && stats.cpu_max == 8.0f &&
        fabsf(stats.frame_avg - 11.0f) < 1e-5f && stats.frame_max == 17.0f,
        "stats %u cpu %f/%f frame %f/%f", stats.samples, stats.cpu_avg, stats.cpu_max,
        stats.frame_avg, stats.frame_max);
    athena_debug_graph_stats(&graph, 2, &stats);
    CHECK(stats.samples == 2 && fabsf(stats.cpu_avg - 4.0f) < 1e-5f && stats.cpu_max == 8.0f,
        "last 2: %u %f %f", stats.samples, stats.cpu_avg, stats.cpu_max);

    /* The ring keeps the newest CAPACITY samples. */
    athena_debug_graph_reset(&graph);
    for (int i = 0; i < ATHENA_DEBUG_GRAPH_CAPACITY + 10; i++)
        athena_debug_graph_push(&graph, 16.6f, (float)i);
    athena_debug_graph_stats(&graph, 0, &stats);
    CHECK(stats.samples == ATHENA_DEBUG_GRAPH_CAPACITY && stats.cpu_max == ATHENA_DEBUG_GRAPH_CAPACITY + 9,
        "ring: %u samples, max %f", stats.samples, stats.cpu_max);
    athena_debug_graph_stats(&graph, 1, &stats);
    CHECK(stats.cpu_avg == ATHENA_DEBUG_GRAPH_CAPACITY + 9, "newest sample %f", stats.cpu_avg);

    /* Bars: oldest on the left, bottom-aligned, levels by the budget. */
    athena_debug_graph_reset(&graph);
    athena_debug_graph_push(&graph, 16.6f, 5.0f);     /* ok */
    athena_debug_graph_push(&graph, 16.6f, 15.0f);    /* tight */
    athena_debug_graph_push(&graph, 20.0f, 20.0f);    /* over */
    athena_debug_graph_push(&graph, 33.3f, 10.0f);    /* dropped a frame */
    athena_debug_graph_push(&graph, 16.6f, 100.0f);   /* taller than the box: clamped */
    n = athena_debug_graph_bars(&graph, 10, 20, 50, 40, 16.6f, 33.2f, bars, 240);
    CHECK(n == 5, "bars %zu", n);
    CHECK(bars[0].x == 10.0f && bars[0].width == 10.0f && bars[4].x == 50.0f, "bar x %f %f w %f",
        bars[0].x, bars[4].x, bars[0].width);
    CHECK(fabsf(bars[0].height - 40.0f * 5.0f / 33.2f) < 1e-4f && fabsf(bars[0].y + bars[0].height - 60.0f) < 1e-4f,
        "bar height %f bottom %f", bars[0].height, bars[0].y + bars[0].height);
    CHECK(bars[4].height == 40.0f && bars[4].y == 20.0f, "clamped bar %f at %f", bars[4].height, bars[4].y);
    CHECK(bars[0].level == ATHENA_DEBUG_BAR_OK && bars[1].level == ATHENA_DEBUG_BAR_TIGHT &&
        bars[2].level == ATHENA_DEBUG_BAR_OVER && bars[3].level == ATHENA_DEBUG_BAR_DROPPED,
        "levels %d %d %d %d", bars[0].level, bars[1].level, bars[2].level, bars[3].level);

    /* Fewer bars than samples: a narrow box or a small array keep the newest. */
    n = athena_debug_graph_bars(&graph, 0, 0, 3, 10, 16.6f, 33.2f, bars, 240);
    CHECK(n == 3 && bars[2].height == 10.0f && bars[2].width == 1.0f, "narrow box: %zu bars", n);
    n = athena_debug_graph_bars(&graph, 0, 0, 100, 10, 16.6f, 33.2f, bars, 2);
    CHECK(n == 2 && bars[1].height == 10.0f, "max_bars: %zu", n);
    CHECK(athena_debug_graph_bars(&graph, 0, 0, 0.5f, 10, 16.6f, 33.2f, bars, 240) == 0, "box under a pixel");
    CHECK(athena_debug_graph_bars(&graph, 0, 0, 50, 10, 16.6f, 0.0f, bars, 240) == 0, "no scale");
}

static void check_tail(int line, const char *text, int lines, int columns, const char *expected,
    int expected_lines) {
    char out[256];
    int got = athena_debug_text_tail(text, strlen(text), lines, columns, out, sizeof(out));

    if (strcmp(out, expected) != 0 || got != expected_lines) {
        failures++;
        printf("  FAIL line %d: tail(%d, %d) gave %d lines \"%s\", expected %d \"%s\"\n",
            line, lines, columns, got, out, expected_lines, expected);
    }
}
#define TAIL(text, lines, columns, expected, n) check_tail(__LINE__, text, lines, columns, expected, n)

static void test_text_tail(void) {
    char small[8];

    TAIL("", 3, 0, "", 0);
    TAIL("one\ntwo\nthree\n", 2, 0, "two\nthree", 2);
    TAIL("one\ntwo\nthree", 2, 0, "two\nthree", 2);
    TAIL("one\ntwo", 5, 0, "one\ntwo", 2);
    TAIL("a\n\nb\n", 3, 0, "a\n\nb", 3);
    TAIL("one\r\ntwo\r\n", 2, 0, "one\ntwo", 2);
    TAIL("x", 0, 0, "", 0);

    /* Wrapping: long lines take several rows, and only the last rows show. */
    TAIL("abcdefgh\n", 5, 3, "abc\ndef\ngh", 3);
    TAIL("abcdefgh\n", 2, 3, "def\ngh", 2);
    TAIL("abcdefgh\nij\n", 2, 3, "gh\nij", 2);
    TAIL("abcdef", 2, 3, "abc\ndef", 2);   /* exact multiple: no empty row */
    TAIL("ab\ncdefgh", 1, 3, "fgh", 1);

    /* UTF-8: columns count characters, never split a character. */
    TAIL("\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\n", 2, 2, "\xc3\xa1\xc3\xa9\n\xc3\xad\xc3\xb3", 2);
    TAIL("\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\n", 1, 3, "\xc3\xb3", 1);

    /* A small output buffer cuts the text but stays terminated. */
    athena_debug_text_tail("0123456789", 10, 1, 0, small, sizeof(small));
    CHECK(strcmp(small, "0123456") == 0, "cut output \"%s\"", small);
    CHECK(athena_debug_text_tail("abc", 3, 1, 0, small, 0) == 0, "no output buffer");
}

static void test_shapes(void) {
    static AthenaDebugShapes queue;
    AthenaDebugShape shape = { 0 };
    int ordered = 1;

    athena_debug_shapes_reset(&queue);
    for (int i = 0; i < 5; i++) {
        shape.x = (float)i;
        shape.remaining = i % 2 ? 1.0f : 0.0f;   /* odd ones last a second */
        athena_debug_shapes_push(&queue, &shape);
    }
    CHECK(queue.count == 5 && athena_debug_shapes_at(&queue, 4)->x == 4.0f, "push keeps the order");
    athena_debug_shapes_prune(&queue);
    CHECK(queue.count == 2 && athena_debug_shapes_at(&queue, 0)->x == 1.0f &&
        athena_debug_shapes_at(&queue, 1)->x == 3.0f, "prune drops one-frame shapes, keeps the order");
    athena_debug_shapes_age(&queue, 0.6f);
    athena_debug_shapes_prune(&queue);
    CHECK(queue.count == 2, "not yet expired");
    athena_debug_shapes_age(&queue, 0.6f);
    athena_debug_shapes_prune(&queue);
    CHECK(queue.count == 0 && queue.start == 0, "expired after their seconds");

    /* Full: the oldest go, counted; order survives the wrap and a prune. */
    athena_debug_shapes_reset(&queue);
    for (int i = 0; i < ATHENA_DEBUG_MAX_SHAPES + 100; i++) {
        shape.x = (float)i;
        shape.remaining = i % 3 ? 5.0f : 0.0f;
        athena_debug_shapes_push(&queue, &shape);
    }
    CHECK(queue.count == ATHENA_DEBUG_MAX_SHAPES && queue.dropped == 100, "cap: %u kept, %u dropped",
        queue.count, queue.dropped);
    CHECK(athena_debug_shapes_at(&queue, 0)->x == 100.0f &&
        athena_debug_shapes_at(&queue, ATHENA_DEBUG_MAX_SHAPES - 1)->x == ATHENA_DEBUG_MAX_SHAPES + 99,
        "oldest dropped first");
    athena_debug_shapes_prune(&queue);
    for (uint32_t i = 1; i < queue.count; i++)
        ordered &= athena_debug_shapes_at(&queue, i)->x > athena_debug_shapes_at(&queue, i - 1)->x;
    CHECK(ordered && queue.count > 1300 && queue.count < 1400, "prune across the wrap keeps %u in order", queue.count);
    shape.remaining = NAN;
    athena_debug_shapes_push(&queue, &shape);
    athena_debug_shapes_prune(&queue);
    CHECK(athena_debug_shapes_at(&queue, queue.count - 1)->remaining == 5.0f, "NaN time is pruned");
}

int main(void) {
    test_graph();
    test_shapes();
    test_text_tail();
    if (failures) {
        printf("debug_overlay_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("debug_overlay_test: all checks passed\n");
    return 0;
}
