#!/bin/sh
# Host tests: C code of the runtime compiled for the build machine against
# stubs of the PS2SDK, so logic bugs show up without PCSX2 or a console.
# Run from anywhere; `docker compose run --rm host-tests` does it in the
# build image. Needs gcc with pthreads.
set -e
cd "$(dirname "$0")/../.."

CC=${CC:-gcc}
OUT=${TMPDIR:-/tmp}/athena-host-tests
mkdir -p "$OUT"
CFLAGS="-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
    -Wno-sign-compare -Werror -fsanitize=undefined -fsanitize-undefined-trap-on-error"

$CC $CFLAGS -Isrc/readini/include -o "$OUT/readini_test" \
    tests/host/readini_test.c src/readini/src/readini.c
$CC $CFLAGS -Itests/host/stubs -Isrc/modules/sound/include -Isrc/modules/sound/native \
    -o "$OUT/sound_stream_test" tests/host/sound_stream_test.c -lpthread -lm
$CC $CFLAGS -Itests/host/stubs -Isrc/modules/sound/include -Isrc/modules/sound/native \
    -o "$OUT/sound_sfx_test" tests/host/sound_sfx_test.c -lpthread -lm

"$OUT/readini_test"
"$OUT/sound_sfx_test"
"$OUT/sound_stream_test"
