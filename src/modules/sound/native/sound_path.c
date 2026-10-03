#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sound_internal.h"

char *sound_absolute_path(const char *path) {
    char cwd[256];
    size_t cwd_length;
    char *absolute;

    /* "host:x", "mass:/x", "/x": already absolute. */
    if (strchr(path, ':') || path[0] == '/' || !getcwd(cwd, sizeof(cwd)))
        return strdup(path);
    cwd_length = strlen(cwd);
    absolute = malloc(cwd_length + strlen(path) + 2);
    if (absolute)
        sprintf(absolute, "%s%s%s", cwd,
            cwd_length && cwd[cwd_length - 1] == '/' ? "" : "/", path);
    return absolute;
}
