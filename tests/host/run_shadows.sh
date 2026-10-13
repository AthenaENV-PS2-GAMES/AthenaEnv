#!/bin/sh
# CPU and packet tests; does not emulate VU1 execution or rasterization.
# ATHENA_3D_SANITIZERS=address,undefined enables ASan in the i386 test image.
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-gcc}
OUT=${TMPDIR:-/tmp}/athena-3d-tests
mkdir -p "$OUT"
SANITIZERS=${ATHENA_3D_SANITIZERS:-undefined}
FLAGS="-std=gnu11 -O1 -g -fno-omit-frame-pointer -fsanitize=$SANITIZERS -fno-sanitize-recover=undefined"
INC="-Isrc/core/include -Isrc/modules/vector/include -Isrc/modules/matrix4/include -Isrc/modules/quaternion/include -Isrc/modules/camera3d/include -Isrc/modules/model3d/include -Isrc/modules/lights/include -Isrc/modules/render3d/include -Isrc/modules/render3d/native -Isrc/modules/scene3d/include -Isrc/modules/loop/include"
for dma_ref in 0 1; do
$CC $FLAGS -DATHENA_RENDER3D_DMA_REF=$dma_ref -DATHENA_OWL_DIAGNOSTICS=1 $INC -Itests/host/3d_stubs -Isrc/modules/graphics/include -Wall -Wextra -Werror \
    -o "$OUT/render3d_packet_test_$dma_ref" tests/host/render3d_packet_test.c \
    src/modules/matrix4/native/matrix4.c src/modules/quaternion/native/quaternion.c \
    src/modules/camera3d/native/camera3d.c src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c src/modules/model3d/native/model3d_detail.c tests/host/texture3d_host.c src/modules/lights/native/lights.c src/modules/render3d/native/render3d_shade.c \
    src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_gs.c src/modules/render3d/native/render3d_clip.c src/modules/graphics/native/owl_packet.c -lm
"$OUT/render3d_packet_test_$dma_ref"
done
$CC $FLAGS $INC -Itests/host/3d_stubs -Isrc/modules/graphics/include -Wall -Wextra -Werror \
    -Wl,--wrap=memalign -o "$OUT/texture3d_gs_test" tests/host/texture3d_gs_test.c \
    src/modules/model3d/native/texture3d.c src/modules/model3d/native/model3d_detail.c src/modules/model3d/native/texture3d_gs.c \
    src/modules/graphics/native/graphics_sync.c
"$OUT/texture3d_gs_test"
