#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-gcc}
OUT=${TMPDIR:-/tmp}/athena-safety-tests
mkdir -p "$OUT"
${CC} -std=gnu11 -O2 -g -Wall -Wextra -Werror \
    -fsanitize=${ATHENA_SAFETY_SANITIZERS:-undefined} -fno-sanitize-recover=undefined \
    -Itests/host/3d_stubs tests/host/owl_bounds_test.c \
    src/modules/graphics/native/owl_packet.c -o "$OUT/owl_bounds_test"
"$OUT/owl_bounds_test"
${CC} -std=gnu11 -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -ffunction-sections -fdata-sections -fsanitize=${ATHENA_SAFETY_SANITIZERS:-undefined} -fno-sanitize-recover=undefined \
    -Itests/host/font_stubs -Itests/host/3d_stubs -Itests/js/stub \
    -Isrc/core/include -Isrc/modules/font/include -Isrc/modules/graphics/include \
    tests/host/bitmap_font_test.c src/modules/font/native/image_font.c \
    src/modules/graphics/native/owl_packet.c -Wl,--gc-sections -lm -o "$OUT/bitmap_font_test"
"$OUT/bitmap_font_test"
${CC} -std=gnu11 -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=${ATHENA_SAFETY_SANITIZERS:-undefined} -fno-sanitize-recover=undefined \
    -Itests/host/clear_stubs -Itests/host/3d_stubs -Itests/js/stub \
    -Isrc/modules/graphics/include tests/host/page_clear_test.c \
    src/modules/graphics/native/owl_packet.c -o "$OUT/page_clear_test"
"$OUT/page_clear_test"
${CC} -std=gnu11 -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=${ATHENA_SAFETY_SANITIZERS:-undefined} -fno-sanitize-recover=undefined \
    -Itests/host/3d_stubs -Isrc/modules/graphics/include \
    tests/host/mpg_manager_test.c src/modules/graphics/native/mpg_manager.c \
    src/modules/graphics/native/owl_packet.c -o "$OUT/mpg_manager_test"
"$OUT/mpg_manager_test"

sh tests/host/run_draw.sh

sh tests/host/run_memory.sh
