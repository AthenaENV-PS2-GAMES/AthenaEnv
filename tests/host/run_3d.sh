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
$CC $FLAGS -DPS2 -Isrc/quickjs -Wall -Wextra -Werror \
    -o "$OUT/quickjs_operand_test" tests/host/quickjs_operand_test.c
"$OUT/quickjs_operand_test"
# Vendor parsers keep their own warning policy; sanitizers remain enabled.
$CC $FLAGS $INC -w -c src/modules/model3d/native/model3d_load.c -o "$OUT/load.o"
$CC $FLAGS $INC -w -c src/modules/model3d/native/fast_obj/fast_obj.c -o "$OUT/obj.o"
$CC $FLAGS $INC -Wall -Wextra -Werror -o "$OUT/three_d_test" \
    tests/host/three_d_test.c tests/host/render3d_host.c \
    src/modules/matrix4/native/matrix4.c src/modules/quaternion/native/quaternion.c \
    src/modules/camera3d/native/camera3d.c src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c tests/host/texture3d_host.c src/modules/lights/native/lights.c src/modules/render3d/native/render3d_shade.c \
    src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_clip.c "$OUT/load.o" "$OUT/obj.o" -lm
"$OUT/three_d_test"
$CC $FLAGS $INC -Wall -Wextra -Werror -o "$OUT/render3d_clip_test" \
    tests/host/render3d_clip_test.c src/modules/render3d/native/render3d_clip.c \
    src/modules/matrix4/native/matrix4.c src/modules/quaternion/native/quaternion.c \
    src/modules/camera3d/native/camera3d.c src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c tests/host/texture3d_host.c src/modules/lights/native/lights.c src/modules/render3d/native/render3d_shade.c -lm
"$OUT/render3d_clip_test"
$CC $FLAGS $INC -Wall -Wextra -Werror -o "$OUT/render3d_shade_test" \
    tests/host/render3d_shade_test.c src/modules/render3d/native/render3d_clip.c \
    src/modules/render3d/native/render3d_shade.c src/modules/lights/native/lights.c \
    src/modules/matrix4/native/matrix4.c src/modules/quaternion/native/quaternion.c \
    src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c tests/host/texture3d_host.c "$OUT/load.o" "$OUT/obj.o" -lm
"$OUT/render3d_shade_test"
$CC $FLAGS $INC -Itests/host/3d_stubs -Wall -Wextra -Werror \
    -o "$OUT/render3d_packet_test" tests/host/render3d_packet_test.c \
    src/modules/matrix4/native/matrix4.c src/modules/quaternion/native/quaternion.c \
    src/modules/camera3d/native/camera3d.c src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c tests/host/texture3d_host.c src/modules/lights/native/lights.c src/modules/render3d/native/render3d_shade.c \
    src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_gs.c src/modules/render3d/native/render3d_clip.c src/modules/graphics/native/owl_packet.c -lm
"$OUT/render3d_packet_test"
$CC $FLAGS $INC -Wall -Wextra -Werror -o "$OUT/texture3d_test" \
    tests/host/texture3d_test.c tests/host/texture3d_host.c tests/host/render3d_host.c \
    src/modules/model3d/native/texture3d.c src/modules/model3d/native/model3d.c \
    src/modules/matrix4/native/matrix4.c src/modules/quaternion/native/quaternion.c \
    src/modules/camera3d/native/camera3d.c src/modules/lights/native/lights.c \
    src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_clip.c \
    src/modules/render3d/native/render3d_shade.c "$OUT/load.o" "$OUT/obj.o" -lm
"$OUT/texture3d_test"
$CC $FLAGS $INC -Itests/host/3d_stubs -Isrc/modules/graphics/include -Wall -Wextra -Werror \
    -o "$OUT/texture3d_gs_test" tests/host/texture3d_gs_test.c \
    src/modules/model3d/native/texture3d.c src/modules/model3d/native/texture3d_gs.c \
    src/modules/graphics/native/graphics_sync.c
"$OUT/texture3d_gs_test"
$CC $FLAGS $INC -Wall -Wextra -Werror -o "$OUT/scene3d_test" \
    tests/host/scene3d_test.c src/modules/scene3d/native/scene3d.c src/modules/loop/native/loop_systems.c \
    tests/host/render3d_host.c src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_clip.c \
    src/modules/render3d/native/render3d_shade.c src/modules/matrix4/native/matrix4.c \
    src/modules/quaternion/native/quaternion.c src/modules/camera3d/native/camera3d.c \
    src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c tests/host/texture3d_host.c \
    src/modules/lights/native/lights.c "$OUT/load.o" "$OUT/obj.o" -lm
"$OUT/scene3d_test"
$CC $FLAGS $INC -Isrc/modules/animation3d/include -Wall -Wextra -Werror -o "$OUT/animation3d_test" \
    tests/host/animation3d_test.c src/modules/animation3d/native/animation3d.c \
    src/modules/scene3d/native/scene3d.c src/modules/loop/native/loop_systems.c \
    tests/host/render3d_host.c src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_clip.c \
    src/modules/render3d/native/render3d_shade.c src/modules/matrix4/native/matrix4.c \
    src/modules/quaternion/native/quaternion.c src/modules/camera3d/native/camera3d.c \
    src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c tests/host/texture3d_host.c \
    src/modules/lights/native/lights.c "$OUT/load.o" "$OUT/obj.o" -lm
"$OUT/animation3d_test"
$CC $FLAGS $INC -Isrc/modules/animation3d/include -Isrc/modules/gltf3d/include -Wall -Wextra -Werror -o "$OUT/gltf3d_test" \
    tests/host/gltf3d_test.c src/modules/gltf3d/native/gltf3d.c src/modules/animation3d/native/animation3d.c \
    src/modules/scene3d/native/scene3d.c src/modules/loop/native/loop_systems.c \
    tests/host/render3d_host.c src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_clip.c \
    src/modules/render3d/native/render3d_shade.c src/modules/matrix4/native/matrix4.c \
    src/modules/quaternion/native/quaternion.c src/modules/camera3d/native/camera3d.c \
    src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c tests/host/texture3d_host.c \
    src/modules/lights/native/lights.c "$OUT/load.o" "$OUT/obj.o" -lm
"$OUT/gltf3d_test"
$CC $FLAGS $INC -Isrc/modules/camerarig3d/include -Wall -Wextra -Werror -o "$OUT/camerarig3d_test" \
    tests/host/camerarig3d_test.c src/modules/camerarig3d/native/camerarig3d.c \
    src/modules/scene3d/native/scene3d.c src/modules/loop/native/loop_systems.c \
    tests/host/render3d_host.c src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_clip.c \
    src/modules/render3d/native/render3d_shade.c src/modules/matrix4/native/matrix4.c \
    src/modules/quaternion/native/quaternion.c src/modules/camera3d/native/camera3d.c \
    src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c tests/host/texture3d_host.c \
    src/modules/lights/native/lights.c "$OUT/load.o" "$OUT/obj.o" -lm
"$OUT/camerarig3d_test"
$CC $FLAGS $INC -Isrc/modules/tween3d/include -Wall -Wextra -Werror -o "$OUT/tween3d_test" \
    tests/host/tween3d_test.c src/modules/tween3d/native/tween3d.c src/modules/tween3d/native/ease_curves.c \
    src/modules/scene3d/native/scene3d.c src/modules/loop/native/loop_systems.c \
    tests/host/render3d_host.c src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_clip.c \
    src/modules/render3d/native/render3d_shade.c src/modules/matrix4/native/matrix4.c \
    src/modules/quaternion/native/quaternion.c src/modules/camera3d/native/camera3d.c \
    src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c tests/host/texture3d_host.c \
    src/modules/lights/native/lights.c "$OUT/load.o" "$OUT/obj.o" -lm
"$OUT/tween3d_test"
$CC $FLAGS $INC -Isrc/modules/collision3d/include -Wall -Wextra -Werror -o "$OUT/collision3d_test" \
    tests/host/collision3d_test.c src/modules/collision3d/native/collision3d.c src/modules/collision3d/native/character3d.c \
    src/modules/scene3d/native/scene3d.c src/modules/loop/native/loop_systems.c \
    tests/host/render3d_host.c src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_clip.c \
    src/modules/render3d/native/render3d_shade.c src/modules/matrix4/native/matrix4.c \
    src/modules/quaternion/native/quaternion.c src/modules/camera3d/native/camera3d.c \
    src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c tests/host/texture3d_host.c \
    src/modules/lights/native/lights.c "$OUT/load.o" "$OUT/obj.o" -lm
"$OUT/collision3d_test"
$CC $FLAGS $INC -Isrc/modules/collision3d/include -Isrc/modules/physics3d/include -Wall -Wextra -Werror -o "$OUT/physics3d_test" \
    tests/host/physics3d_test.c src/modules/physics3d/native/physics3d.c src/modules/physics3d/native/joint3d.c \
    src/modules/collision3d/native/collision3d.c src/modules/collision3d/native/character3d.c \
    src/modules/scene3d/native/scene3d.c src/modules/loop/native/loop_systems.c \
    tests/host/render3d_host.c src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_clip.c \
    src/modules/render3d/native/render3d_shade.c src/modules/matrix4/native/matrix4.c \
    src/modules/quaternion/native/quaternion.c src/modules/camera3d/native/camera3d.c \
    src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c tests/host/texture3d_host.c \
    src/modules/lights/native/lights.c "$OUT/load.o" "$OUT/obj.o" -lm
"$OUT/physics3d_test"
$CC $FLAGS -Isrc/core/include -Isrc/modules/particles2d/include -Isrc/modules/loop/include \
    -Itests/host/particles_stubs -Wall -Wextra -Werror \
    -o "$OUT/particles2d_test" tests/host/particles2d_test.c src/modules/particles2d/native/particles2d.c \
    src/modules/loop/native/loop_systems.c -lm
"$OUT/particles2d_test"
$CC $FLAGS -Isrc/core/include -Isrc/modules/particles3d/include -Isrc/modules/loop/include \
    -Isrc/modules/camera3d/include -Isrc/modules/matrix4/include -Isrc/modules/vector/include \
    -Itests/host/particles_stubs -Wall -Wextra -Werror \
    -o "$OUT/particles3d_test" tests/host/particles3d_test.c src/modules/particles3d/native/particles3d.c \
    src/modules/camera3d/native/camera3d.c src/modules/matrix4/native/matrix4.c \
    src/modules/loop/native/loop_systems.c -lm
"$OUT/particles3d_test"
