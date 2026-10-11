#!/bin/sh
# Real decoder libraries; Debian/Ubuntu: libpng-dev libjpeg-dev, Alpine: libpng-dev libjpeg-turbo-dev.
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-gcc}
OUT=${TMPDIR:-/tmp}/athena-image-tests
mkdir -p "$OUT"
${CC} -std=gnu11 -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    -Itests/host/image_stubs -Isrc/core/include \
    tests/host/image_loaders_test.c src/modules/graphics/native/image_loaders.c \
    -Wl,--wrap=memalign,--wrap=calloc,--wrap=malloc -lpng -ljpeg -lm -o "$OUT/image_loaders_test"
"$OUT/image_loaders_test"
