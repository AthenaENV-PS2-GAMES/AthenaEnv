#!/bin/sh
# Module test scripts (bin/tests/*.js) on the build machine, under
# AddressSanitizer: AthenaEnv's QuickJS needs 32-bit pointers (NaN-boxing and
# the float32 values), so this runs in an i386 userland:
#   docker compose run --rm js-tests
# Module code and Box2D also run under UBSan (fatal); QuickJS itself only
# under ASan, as it has known benign UBSan reports.
set -e
cd "$(dirname "$0")/../.."

CC=${CC:-gcc}
OUT=${TMPDIR:-/tmp}/athena-js-tests
mkdir -p "$OUT/obj"
BASE="-O1 -g -fno-omit-frame-pointer -D_GNU_SOURCE -DCONFIG_BIGNUM -DCONFIG_VERSION=\"host\""
ASAN="-fsanitize=address"
UBSAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
INC="-Itests/js/stub -Isrc/quickjs -Isrc/core/include -Isrc/modules/box2d/include \
    -Isrc/modules/random/include -Isrc/modules/noise/include \
    -Isrc/modules/debug/include -Isrc/modules/color/include"
# The EE has no SIMD for Box2D: build the same scalar path.
B2FLAGS="-DBOX2D_DISABLE_SIMD -DB2_ENABLE_ASSERT"

build() { # object source flags...
    obj=$1; src=$2; shift 2
    [ "$obj" -nt "$src" ] || $CC $BASE "$@" $INC -c "$src" -o "$obj"
}

for f in cutils libbf libregexp libunicode quickjs; do
    build "$OUT/obj/$f.o" "src/quickjs/$f.c" $ASAN -w
done
for f in src/modules/box2d/native/box2d/*.c; do
    build "$OUT/obj/b2_$(basename "$f" .c).o" "$f" $UBSAN $B2FLAGS -w
done
# MemoryCard runs against the fake card of tests/host/fake_libmc.h.
MC="-Itests/host -Itests/host/stubs -Isrc/modules/memcard/include -Isrc/modules/memcard/native -Isrc/modules/thread/include"
$CC $BASE $UBSAN $B2FLAGS -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
    -Wno-missing-field-initializers -Wno-cast-function-type -Werror $MC $INC \
    -o "$OUT/runner" tests/js/runner.c src/modules/box2d/native/box2d.c src/modules/box2d/quickjs/*.c \
    tests/js/memcard_host.c src/modules/memcard/native/memcard.c src/modules/memcard/native/memcard_job.c \
    src/modules/thread/native/job.c src/modules/thread/quickjs/ath_job.c \
    src/modules/memcard/quickjs/ath_memcard.c \
    src/modules/random/native/random.c src/modules/random/quickjs/ath_random.c \
    src/modules/noise/native/noise.c src/modules/noise/native/noise_job.c \
    src/modules/noise/quickjs/ath_noise.c \
    src/modules/debug/native/debug_overlay.c src/modules/debug/quickjs/ath_debug.c \
    src/runtime/quickjs/ath_output.c \
    "$OUT"/obj/*.o -lm -lpthread

# runner_font: the same runner with the real Font binding (over the native
# stand-in tests/js/font_host.c) instead of the JavaScript stub, for scripts
# that must take the console's path: FontRender lifetimes, preload slices.
$CC $BASE $UBSAN $B2FLAGS -DRUNNER_REAL_FONT -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
    -Wno-missing-field-initializers -Wno-cast-function-type -Werror $MC $INC -Isrc/modules/font/include \
    -o "$OUT/runner_font" tests/js/runner.c src/modules/box2d/native/box2d.c src/modules/box2d/quickjs/*.c \
    tests/js/memcard_host.c src/modules/memcard/native/memcard.c src/modules/memcard/native/memcard_job.c \
    src/modules/thread/native/job.c src/modules/thread/quickjs/ath_job.c \
    src/modules/memcard/quickjs/ath_memcard.c \
    src/modules/random/native/random.c src/modules/random/quickjs/ath_random.c \
    src/modules/noise/native/noise.c src/modules/noise/native/noise_job.c \
    src/modules/noise/quickjs/ath_noise.c \
    src/modules/debug/native/debug_overlay.c src/modules/debug/quickjs/ath_debug.c \
    src/runtime/quickjs/ath_output.c \
    src/modules/font/quickjs/ath_font.c tests/js/font_host.c \
    "$OUT"/obj/*.o -lm -lpthread

cd bin

# check RUNNER SCRIPT MARKER: runs SCRIPT, shows its output, and fails when
# the runner exits non-zero (a crash, a sanitizer, the leak assertion of
# JS_FreeRuntime after the script ended) or MARKER was not printed. A pipe
# into tee would hide the exit code.
check() {
    runner=$1; script=$2; marker=$3
    log="$OUT/$(basename "$script" .js).$(basename "$runner").log"
    status=0
    "$runner" "$script" > "$log" 2>&1 || status=$?
    cat "$log"
    if [ "$status" -ne 0 ]; then
        echo "FAIL: $script exited with $status"
        exit 1
    fi
    if ! grep -q "$marker" "$log"; then
        echo "FAIL: $script did not print \"$marker\""
        exit 1
    fi
}

echo "== tests/box2d_test.js"
check "$OUT/runner" tests/box2d_test.js "Result: .* 0 failed"
# The summaries come from promise callbacks: check the printed result too.
# Ease and Tween are JavaScript modules, loaded from their sources.
echo "== tests/tween_test.js"
check "$OUT/runner" tests/tween_test.js "Tween module test passed"
echo "== tests/debug_test.js"
check "$OUT/runner" tests/debug_test.js "Debug module test passed"
# Again with the real Font binding: the console's path (preload slices,
# FontRenders freed at teardown), whose leaks abort JS_FreeRuntime.
echo "== tests/debug_test.js (real Font)"
check "$OUT/runner_font" tests/debug_test.js "Debug module test passed"
echo "== tests/teardown_gc_test.js (real Font)"
check "$OUT/runner_font" tests/teardown_gc_test.js "Teardown GC test done"
echo "== tests/random_noise_test.js"
check "$OUT/runner" tests/random_noise_test.js "Random and Noise module test passed"
echo "== tests/memcard_test.js"
check "$OUT/runner" tests/memcard_test.js "Result: .* 0 failed"
