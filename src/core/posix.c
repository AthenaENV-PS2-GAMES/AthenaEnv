#include <reent.h>
#include <unistd.h>

/*
 * POSIX close() returns 0 or -1. PS2SDK's libcglue passes a positive IOP
 * driver result through unchanged (the USB mass storage driver returns one
 * on success), which callers such as zlib's gzclose() report as a failure.
 * Failures still come back as -1 with errno set, and the descriptor is
 * released either way.
 */
int close(int fd) {
    int ret = _close_r(_REENT, fd);
    return ret > 0 ? 0 : ret;
}
