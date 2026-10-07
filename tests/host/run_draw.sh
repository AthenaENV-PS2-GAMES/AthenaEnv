#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-gcc}
OUT=${TMPDIR:-/tmp}/athena-draw-tests
mkdir -p "$OUT"
FLAGS="-std=gnu11 -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter -ffunction-sections -fdata-sections -fno-omit-frame-pointer -fsanitize=${ATHENA_SAFETY_SANITIZERS:-undefined} -fno-sanitize-recover=undefined"
INC="-Itests/host/draw_stubs -Itests/host/3d_stubs -Itests/js/stub -Isrc/core/include -Isrc/modules/graphics/include"
$CC $FLAGS $INC tests/host/draw_lists_test.c tests/host/draw_packet_harness.c \
    src/modules/graphics/native/owl_draw.c src/modules/graphics/native/owl_packet.c \
    -Wl,--wrap=owl_query_packet,--gc-sections -lm -o "$OUT/draw_lists_test"
"$OUT/draw_lists_test"
# FreeType is needed to compile fntsys.c; tests inject layouts and exercise
# its real draw paths, with unused rasterizer sections discarded by the linker.
if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists freetype2; then
    echo "atlas_font: FreeType headers/pkg-config unavailable; test skipped"
    exit 0
fi
FT_CFLAGS=$(pkg-config --cflags freetype2)
$CC $FLAGS -Wno-old-style-declaration $INC -Itests/host/stubs -Isrc/modules/font/include $FT_CFLAGS \
    tests/host/atlas_font_test.c tests/host/draw_packet_harness.c \
    src/modules/graphics/native/owl_draw.c src/modules/graphics/native/owl_packet.c \
    -Wl,--wrap=owl_query_packet,--gc-sections -lm -o "$OUT/atlas_font_test"
"$OUT/atlas_font_test"
