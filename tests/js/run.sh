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
INC="-Itests/js/stub -Isrc/quickjs -Isrc/core/include -Isrc/modules/box2d/include"
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
MC="-Itests/host -Itests/host/stubs -Isrc/modules/memcard/include -Isrc/modules/memcard/native"
$CC $BASE $UBSAN $B2FLAGS -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
    -Wno-missing-field-initializers -Wno-cast-function-type -Werror $MC $INC \
    -o "$OUT/runner" tests/js/runner.c src/modules/box2d/native/box2d.c src/modules/box2d/quickjs/*.c \
    tests/js/memcard_host.c src/modules/memcard/native/memcard.c src/modules/memcard/native/memcard_job.c \
    src/modules/memcard/quickjs/ath_memcard.c "$OUT"/obj/*.o -lm -lpthread

cd bin
for test in tests/box2d_test.js; do
    echo "== $test"
    "$OUT/runner" "$test"
done
# The summary comes from a promise callback: check the printed result too.
echo "== tests/memcard_test.js"
"$OUT/runner" tests/memcard_test.js | tee "$OUT/memcard.log"
grep -q "Result: .* 0 failed" "$OUT/memcard.log"
