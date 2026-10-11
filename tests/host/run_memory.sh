#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-gcc}
OUT=${TMPDIR:-/tmp}/athena-memory-tests
mkdir -p "$OUT"
FLAGS="-std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=${ATHENA_SAFETY_SANITIZERS:-undefined} -fno-sanitize-recover=undefined"
$CC $FLAGS -Itests/host/memory_stubs -Isrc/core/include \
    -Dmalloc=ath_malloc -Dcalloc=ath_calloc -Drealloc=ath_realloc \
    -Dmemalign=ath_memalign -Dfree=ath_free \
    -c src/core/memory.c -o "$OUT/memory.o"
$CC $FLAGS -Itests/host/memory_stubs -Isrc/core/include \
    tests/host/memory_accounting_test.c "$OUT/memory.o" -pthread -o "$OUT/memory_test"
"$OUT/memory_test"
