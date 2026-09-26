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

$CC $CFLAGS -Itests/host/stubs -Isrc/modules/loop/include -Isrc/modules/loop/native \
    -o "$OUT/loop_test" tests/host/loop_test.c -lm
$CC $CFLAGS -Isrc/modules/loop/include -Isrc/modules/loop/native \
    -o "$OUT/loop_systems_test" tests/host/loop_systems_test.c
$CC $CFLAGS -Isrc/runtime/quickjs -o "$OUT/output_test" tests/host/output_test.c
$CC $CFLAGS -Itests/host -Itests/host/stubs -Isrc/modules/memcard/include -Isrc/modules/memcard/native \
    -o "$OUT/memcard_test" tests/host/memcard_test.c -lpthread
$CC $CFLAGS -Isrc/readini/include -o "$OUT/readini_test" \
    tests/host/readini_test.c src/readini/src/readini.c
$CC $CFLAGS -Itests/host/stubs -Isrc/modules/sound/include -Isrc/modules/sound/native \
    -o "$OUT/sound_stream_test" tests/host/sound_stream_test.c -lpthread -lm
$CC $CFLAGS -Itests/host/stubs -Isrc/modules/sound/include -Isrc/modules/sound/native \
    -o "$OUT/sound_sfx_test" tests/host/sound_sfx_test.c -lpthread -lm
$CC $CFLAGS -Itests/host/stubs -Isrc/modules/video/include -Isrc/modules/video/native \
    -o "$OUT/video_test" tests/host/video_test.c -lpthread
# Box2D: the vendored library keeps upstream warnings (-w), the AthenaEnv
# helpers and the test use the flags above. Both run under UBSan.
B2=src/modules/box2d
mkdir -p "$OUT/box2d"
for src in "$B2"/native/box2d/*.c; do
    $CC -std=gnu11 -O1 -g -w -fsanitize=undefined -fsanitize-undefined-trap-on-error \
        -I"$B2/include" -c "$src" -o "$OUT/box2d/$(basename "$src" .c).o"
done
$CC $CFLAGS -I"$B2/include" -o "$OUT/box2d_test" tests/host/box2d_test.c "$B2/native/box2d.c" \
    "$OUT"/box2d/*.o -lpthread -lm

"$OUT/loop_test"
"$OUT/loop_systems_test"
"$OUT/output_test"
"$OUT/memcard_test"
"$OUT/readini_test"
"$OUT/sound_sfx_test"
"$OUT/sound_stream_test"
"$OUT/video_test"
"$OUT/box2d_test"

# wav2adp: the C port (make adp) must write the same bytes as tools/wav2adp.js
# (references from tests/host/wav2adp/make_refs.mjs) and, for 16-bit mono,
# as the PS2SDK's adpenc.
$CC -std=c99 -O2 -Wall -Werror -o "$OUT/wav2adp" tools/wav2adp/wav2adp.c -lm
refs=tests/host/wav2adp
wav2adp_checks=0
wav2adp_failures=0
compare() {
    wav2adp_checks=$((wav2adp_checks + 1))
    if ! cmp -s "$1" "$2"; then
        echo "  FAIL wav2adp: $1 differs from $2"
        wav2adp_failures=$((wav2adp_failures + 1))
    fi
}
for name in short rate16k stereo8 float pcm24 pcm32; do
    input=bin/tests/sound/$name.wav
    [ -f "$input" ] || input=$refs/$name.wav
    "$OUT/wav2adp" "$input" "$OUT/$name.adp" > /dev/null
    "$OUT/wav2adp" -L "$input" "$OUT/$name.loop.adp" > /dev/null
    compare "$OUT/$name.adp" "$refs/$name.adp"
    compare "$OUT/$name.loop.adp" "$refs/$name.loop.adp"
done
adpenc=$(command -v adpenc || true)
[ -n "$adpenc" ] || [ ! -x "${PS2SDK:-/nonexistent}/bin/adpenc" ] || adpenc=$PS2SDK/bin/adpenc
if [ -n "$adpenc" ]; then
    "$adpenc" bin/tests/sound/rate16k.wav "$OUT/rate16k.sdk.adp" > /dev/null
    "$adpenc" -L bin/tests/sound/rate16k.wav "$OUT/rate16k.sdk.loop.adp" > /dev/null
    compare "$OUT/rate16k.adp" "$OUT/rate16k.sdk.adp"
    compare "$OUT/rate16k.loop.adp" "$OUT/rate16k.sdk.loop.adp"
else
    echo "  (adpenc not found: comparison with the PS2SDK skipped)"
fi
echo "wav2adp: $wav2adp_checks checks, $wav2adp_failures failures"
[ "$wav2adp_failures" -eq 0 ]
