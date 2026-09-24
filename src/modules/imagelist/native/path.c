#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <athena/imagelist.h>

static bool path_is_separator(char value)
{
    return value == '/' || value == '\\';
}

char *athena_path_normalize(const char *path)
{
    char *normalized;
    size_t *starts;
    size_t length;
    size_t input = 0;
    size_t output = 0;
    size_t root = 0;
    unsigned int depth = 0;
    bool anchored = false;

    if (!path)
        return NULL;
    length = strlen(path);
    normalized = malloc(length + 2);
    starts = malloc((length + 1) * sizeof(*starts));
    if (!normalized || !starts) {
        free(normalized);
        free(starts);
        return NULL;
    }
    while (input < length && !path_is_separator(path[input]) &&
        path[input] != ':')
        input++;
    if (input < length && path[input] == ':') {
        input++;
        memcpy(normalized, path, input);
        output = input;
        anchored = true;
    } else {
        input = 0;
    }
    if (input < length && path_is_separator(path[input])) {
        normalized[output++] = '/';
        anchored = true;
    }
    root = output;
    while (input < length) {
        size_t begin;
        size_t segment_length;

        while (input < length && path_is_separator(path[input]))
            input++;
        begin = input;
        while (input < length && !path_is_separator(path[input]))
            input++;
        segment_length = input - begin;
        if (segment_length == 0 ||
            (segment_length == 1 && path[begin] == '.'))
            continue;
        if (segment_length == 2 && path[begin] == '.' &&
            path[begin + 1] == '.') {
            if (depth > 0) {
                output = starts[--depth];
                continue;
            }
            if (anchored)
                continue;
            /* A leading ".." of a relative path can never be removed. */
            if (output > root)
                normalized[output++] = '/';
            memcpy(normalized + output, path + begin, 2);
            output += 2;
            continue;
        }
        starts[depth++] = output;
        if (output > root)
            normalized[output++] = '/';
        memcpy(normalized + output, path + begin, segment_length);
        output += segment_length;
    }
    if (output == 0)
        normalized[output++] = '.';
    normalized[output] = '\0';
    free(starts);
    return normalized;
}
