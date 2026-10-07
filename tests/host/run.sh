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
$CC $CFLAGS -Itests/host -Itests/host/stubs -Isrc/modules/thread/include -Isrc/modules/thread/native \
    -o "$OUT/job_test" tests/host/job_test.c -lpthread
$CC $CFLAGS -Itests/host -Itests/host/stubs -Isrc/modules/memcard/include -Isrc/modules/memcard/native \
    -Isrc/modules/thread/include -Isrc/modules/thread/native \
    -o "$OUT/memcard_test" tests/host/memcard_test.c -lpthread
$CC $CFLAGS -Isrc/readini/include -o "$OUT/readini_test" \
    tests/host/readini_test.c src/readini/src/readini.c
$CC $CFLAGS -Itests/host/stubs -Isrc/modules/sound/include -Isrc/modules/sound/native \
    -o "$OUT/sound_stream_test" tests/host/sound_stream_test.c -lpthread -lm
$CC $CFLAGS -Itests/host/stubs -Isrc/modules/sound/include -Isrc/modules/sound/native \
    -Isrc/modules/thread/include -Isrc/modules/thread/native \
    -o "$OUT/sound_sfx_test" tests/host/sound_sfx_test.c -lpthread -lm
$CC $CFLAGS -Itests/host/stubs -Isrc/modules/video/include -Isrc/modules/video/native \
    -o "$OUT/video_test" tests/host/video_test.c -lpthread
$CC $CFLAGS -Isrc/modules/random/include -Isrc/modules/noise/include \
    -o "$OUT/random_noise_test" tests/host/random_noise_test.c \
    src/modules/random/native/random.c src/modules/noise/native/noise.c -lm
$CC $CFLAGS -Isrc/modules/debug/include -o "$OUT/debug_overlay_test" \
    tests/host/debug_overlay_test.c src/modules/debug/native/debug_overlay.c -lm
$CC $CFLAGS -Isrc/modules/graphics/include -Isrc/modules/camera2d/include \
    -o "$OUT/camera2d_test" tests/host/camera2d_test.c src/modules/graphics/native/view.c \
    src/modules/camera2d/native/camera2d.c -lm
$CC $CFLAGS -Itests/host/stubs -Isrc/modules/image/include -Isrc/modules/tilemap/include \
    -Isrc/modules/graphics/include -Isrc/modules/sprite/include -o "$OUT/sprite_test" tests/host/sprite_test.c \
    src/modules/sprite/native/sprite.c -lm
$CC $CFLAGS -Isrc/modules/collision/include -o "$OUT/collision_test" tests/host/collision_test.c \
    src/modules/collision/native/collision.c -lm
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
"$OUT/job_test"
"$OUT/memcard_test"
"$OUT/readini_test"
"$OUT/sound_sfx_test"
"$OUT/sound_stream_test"
"$OUT/video_test"
"$OUT/random_noise_test"
"$OUT/debug_overlay_test"
"$OUT/camera2d_test"
"$OUT/sprite_test"
"$OUT/collision_test"
"$OUT/box2d_test"
sh tests/host/run_3d.sh

# VU microprograms: each committed .vsm must be what OpenVCL makes of its
# .vcl (the Makefile regenerates it only when the .vcl is newer). Line ends
# are ignored: a Windows checkout may turn the .vsm into CRLF.
if command -v openvcl > /dev/null && command -v masp > /dev/null; then
    vcl_checks=0
    for vcl in $(find src -name '*.vcl'); do
        vcl_checks=$((vcl_checks + 1))
        openvcl --gasp masp -g -o"$OUT/vcl.vsm" "$vcl"
        if ! tr -d '\r' < "${vcl%.vcl}.vsm" | cmp -s - "$OUT/vcl.vsm"; then
            echo "  FAIL vcl: ${vcl%.vcl}.vsm is not the output of $vcl (run make)"
            exit 1
        fi
    done
    echo "vcl: $vcl_checks programs match their .vsm"
else
    echo "vcl: openvcl or masp not found, .vsm check skipped"
fi
# OpenVCL scheduling hazards in the committed .vsm (values lost across a
# loop's back edge, two slots writing one register in a cycle).
if command -v python3 > /dev/null; then
    python3 tools/check_vsm_loops.py $(find src -name '*.vsm') || exit 1
fi

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
