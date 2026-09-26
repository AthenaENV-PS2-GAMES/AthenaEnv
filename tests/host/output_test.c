/* Host test of the script output kept for std.lastRun(): capture, rotation and dropping old lines. */
#include <stdio.h>
#include <string.h>

#include "ath_output.c"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void out(const char *text) {
    athena_runtime_output(text, strlen(text));
}

int main(void) {
    char line[64];
    const char *last;

    CHECK(strcmp(athena_runtime_output_last(), "") == 0, "empty before the first rotation");

    out("hello");
    out(" ");
    out("world\n");
    athena_runtime_output_rotate();
    CHECK(strcmp(athena_runtime_output_last(), "hello world\n") == 0, "'%s'", athena_runtime_output_last());

    /* A new script starts empty; rotating an empty run clears the last output. */
    athena_runtime_output_rotate();
    CHECK(strcmp(athena_runtime_output_last(), "") == 0, "cleared");

    /* Overflow drops whole lines from the start and keeps the end. */
    for (int i = 0; i < 2000; i++) {
        snprintf(line, sizeof(line), "line %04d\n", i);
        out(line);
    }
    athena_runtime_output_rotate();
    last = athena_runtime_output_last();
    CHECK(strncmp(last, OUTPUT_DROPPED, strlen(OUTPUT_DROPPED)) == 0, "dropped marker");
    CHECK(strstr(last, "line 1999\n") != NULL, "last line kept");
    CHECK(strstr(last, "line 0000\n") == NULL, "first line dropped");
    {
        const char *first = last + strlen(OUTPUT_DROPPED);
        CHECK(strncmp(first, "line ", 5) == 0 && first[9] == '\n', "starts on a whole line: '%.12s'", first);
    }
    CHECK(strlen(last) <= OUTPUT_SIZE - 1 + strlen(OUTPUT_DROPPED), "bounded: %zu", strlen(last));

    /* A single write larger than the buffer keeps its end. */
    {
        static char big[OUTPUT_SIZE * 2];
        memset(big, 'x', sizeof(big) - 2);
        big[sizeof(big) - 2] = '!';
        big[sizeof(big) - 1] = '\0';
        out("before\n");
        out(big);
        athena_runtime_output_rotate();
        last = athena_runtime_output_last();
        CHECK(last[strlen(last) - 1] == '!', "end of the big write kept");
        CHECK(strstr(last, "before") == NULL, "older text dropped");
    }

    /* After a drop, the next script starts without the marker. */
    out("fresh\n");
    athena_runtime_output_rotate();
    CHECK(strcmp(athena_runtime_output_last(), "fresh\n") == 0, "'%s'", athena_runtime_output_last());

    if (failures) {
        printf("output_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("output_test: all checks passed\n");
    return 0;
}
