/*
 * The PS2 zlib port installs minizip's ioapi.h but not the ints.h it
 * includes. ioapi.h only needs ui64_t (for ZPOS64_T), defined here as in
 * zlib's contrib/minizip/ints.h. MIN_INTS_H is upstream's guard, so a real
 * ints.h included first takes precedence.
 */
#ifndef MIN_INTS_H
#define MIN_INTS_H

#include <stdint.h>

typedef uint64_t ui64_t;

#endif /* MIN_INTS_H */
