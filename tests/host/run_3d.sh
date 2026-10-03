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
INC="-Isrc/core/include -Isrc/modules/vector/include -Isrc/modules/matrix4/include -Isrc/modules/quaternion/include -Isrc/modules/camera3d/include -Isrc/modules/model3d/include -Isrc/modules/render3d/include -Isrc/modules/render3d/native"
$CC $FLAGS -DPS2 -Isrc/quickjs -Wall -Wextra -Werror \
    -o "$OUT/quickjs_operand_test" tests/host/quickjs_operand_test.c
"$OUT/quickjs_operand_test"
# Vendor parsers keep their own warning policy; sanitizers remain enabled.
$CC $FLAGS $INC -w -c src/modules/model3d/native/model3d_load.c -o "$OUT/load.o"
$CC $FLAGS $INC -w -c src/modules/model3d/native/fast_obj/fast_obj.c -o "$OUT/obj.o"
$CC $FLAGS $INC -Wall -Wextra -Werror -o "$OUT/three_d_test" \
    tests/host/three_d_test.c tests/host/render3d_host.c \
    src/modules/matrix4/native/matrix4.c src/modules/quaternion/native/quaternion.c \
    src/modules/camera3d/native/camera3d.c src/modules/model3d/native/model3d.c \
    src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_clip.c "$OUT/load.o" "$OUT/obj.o" -lm
"$OUT/three_d_test"
$CC $FLAGS $INC -Wall -Wextra -Werror -o "$OUT/render3d_clip_test" \
    tests/host/render3d_clip_test.c src/modules/render3d/native/render3d_clip.c \
    src/modules/matrix4/native/matrix4.c src/modules/quaternion/native/quaternion.c \
    src/modules/camera3d/native/camera3d.c src/modules/model3d/native/model3d.c -lm
"$OUT/render3d_clip_test"
$CC $FLAGS $INC -Itests/host/3d_stubs -Wall -Wextra -Werror \
    -o "$OUT/render3d_packet_test" tests/host/render3d_packet_test.c \
    src/modules/matrix4/native/matrix4.c src/modules/quaternion/native/quaternion.c \
    src/modules/camera3d/native/camera3d.c src/modules/model3d/native/model3d.c \
    src/modules/render3d/native/render3d_gs.c src/modules/render3d/native/render3d_clip.c src/modules/graphics/native/owl_packet.c -lm
"$OUT/render3d_packet_test"
