/*
 * Script output kept for std.lastRun(): what the running script printed
 * (console.log, print, dumped errors) and what the previous one printed, so
 * a launcher can show it on screen. When full, the oldest lines are dropped.
 * Plain C, tested on the host by tests/host/output_test.c.
 */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define OUTPUT_SIZE 16384
#define OUTPUT_DROPPED "[earlier output dropped]\n"

static char run_output[OUTPUT_SIZE];
static size_t run_output_length;
static bool run_output_dropped;
static char last_output[OUTPUT_SIZE + sizeof(OUTPUT_DROPPED)];

void athena_runtime_output(const char *text, size_t length) {
    size_t room = OUTPUT_SIZE - 1;

    if (length > room) {
        text += length - room;
        length = room;
        run_output_dropped = true;
    }
    if (run_output_length + length > room) {
        /* Drop whole lines from the start, at least enough for `text`. */
        size_t drop = run_output_length + length - room;
        const char *newline = drop < run_output_length ?
            memchr(run_output + drop, '\n', run_output_length - drop) : NULL;

        drop = newline ? (size_t)(newline + 1 - run_output) : run_output_length;
        memmove(run_output, run_output + drop, run_output_length - drop);
        run_output_length -= drop;
        run_output_dropped = true;
    }
    memcpy(run_output + run_output_length, text, length);
    run_output_length += length;
    run_output[run_output_length] = '\0';
}

/* Keeps the output of the script that ended as the last output, and starts afresh. */
void athena_runtime_output_rotate(void) {
    size_t prefix = run_output_dropped ? strlen(OUTPUT_DROPPED) : 0;

    memcpy(last_output, OUTPUT_DROPPED, prefix);
    memcpy(last_output + prefix, run_output, run_output_length);
    last_output[prefix + run_output_length] = '\0';
    run_output_length = 0;
    run_output[0] = '\0';
    run_output_dropped = false;
}

const char *athena_runtime_output_last(void) {
    return last_output;
}
