#!/bin/sh
# Module test scripts (bin/tests/*.js) on the build machine, under
# AddressSanitizer: AthenaEnv's QuickJS needs 32-bit pointers (NaN-boxing and
# the float32 values), so this runs in an i386 userland:
#   docker compose run --rm js-tests
# Module code and Box2D also run under UBSan (fatal); QuickJS itself only
# under ASan, as it has known benign UBSan reports.
set -e
cd "$(dirname "$0")/../.."
ATHENA_SAFETY_SANITIZERS=address,undefined sh tests/host/run_safety.sh
sh tests/host/run_images.sh
ATHENA_3D_SANITIZERS=address,undefined sh tests/host/run_3d.sh

CC=${CC:-gcc}
OUT=${TMPDIR:-/tmp}/athena-js-tests
mkdir -p "$OUT/obj"
BASE="-O1 -g -fno-omit-frame-pointer -D_GNU_SOURCE -DCONFIG_BIGNUM -DCONFIG_VERSION=\"host\""
ASAN="-fsanitize=address"
UBSAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
INC="-Itests/js/stub -Isrc/quickjs -Isrc/core/include -Isrc/modules/box2d/include \
    -Isrc/modules/random/include -Isrc/modules/noise/include \
    -Isrc/modules/debug/include -Isrc/modules/color/include -Isrc/modules/profiler/include -Isrc/modules/debug3d/include -Isrc/modules/savegame/include -Isrc/modules/meshbuilder/include -Isrc/modules/voxel/include \
    -Isrc/modules/lod/include -Isrc/modules/triggers3d/include -Isrc/modules/nav/include -Isrc/modules/audio3d/include \
    -Isrc/modules/sky/include -Isrc/modules/sound/include \
    -Isrc/modules/graphics/include -Isrc/modules/camera2d/include -Isrc/modules/loop/include \
    -Isrc/modules/sprite/include -Isrc/modules/image/include -Isrc/modules/tilemap/include \
    -Isrc/modules/collision/include -Isrc/runtime/quickjs/include \
    -Isrc/modules/vector/include -Isrc/modules/matrix4/include -Isrc/modules/quaternion/include \
    -Isrc/modules/camera3d/include -Isrc/modules/model3d/include -Isrc/modules/lights/include -Isrc/modules/render3d/include -Isrc/modules/render3d/native -Isrc/modules/scene3d/include -Isrc/modules/animation3d/include -Isrc/modules/camerarig3d/include -Isrc/modules/tween3d/include -Isrc/modules/particles2d/include -Isrc/modules/particles3d/include -Isrc/modules/gltf3d/include -Isrc/modules/collision3d/include -Isrc/modules/physics3d/include"
THREE_D="src/modules/matrix4/native/matrix4.c src/modules/matrix4/quickjs/ath_matrix4.c \
    src/modules/quaternion/native/quaternion.c src/modules/quaternion/quickjs/ath_quaternion.c \
    src/modules/camera3d/native/camera3d.c src/modules/camera3d/quickjs/ath_camera3d.c \
    src/modules/lights/native/lights.c src/modules/lights/quickjs/ath_lights.c src/modules/render3d/native/render3d_shade.c src/modules/model3d/native/model3d.c src/modules/model3d/native/texture3d.c src/modules/model3d/native/model3d_detail.c tests/host/texture3d_host.c src/modules/model3d/quickjs/ath_model3d.c \
    src/modules/render3d/native/render3d.c src/modules/render3d/native/render3d_clip.c src/modules/render3d/quickjs/ath_render3d.c \
    src/modules/scene3d/native/scene3d.c src/modules/scene3d/quickjs/ath_scene3d.c \
    src/modules/animation3d/native/animation3d.c src/modules/animation3d/quickjs/ath_animation3d.c \
    src/modules/gltf3d/native/gltf3d.c src/modules/gltf3d/quickjs/ath_gltf3d.c \
    src/modules/camerarig3d/native/camerarig3d.c src/modules/camerarig3d/quickjs/ath_camerarig3d.c \
    src/modules/tween3d/native/tween3d.c src/modules/tween3d/native/ease_curves.c src/modules/tween3d/quickjs/ath_tween3d.c \
    src/modules/collision3d/native/collision3d.c src/modules/collision3d/native/character3d.c src/modules/collision3d/quickjs/ath_collision3d.c \
    src/modules/physics3d/native/physics3d.c src/modules/physics3d/native/joint3d.c src/modules/physics3d/quickjs/ath_physics3d.c \
    tests/host/render3d_host.c"
# Camera2D and the math of the 2D view, with the C side of Loop systems; the
# GS side of the view (clip rectangle) is stubbed in runner.c.
CAMERA="src/modules/camera2d/native/camera2d.c src/modules/camera2d/native/camera2d_gs.c \
    src/modules/camera2d/quickjs/ath_camera2d.c src/modules/graphics/native/view.c \
    src/modules/loop/native/loop_systems.c"
# Sprite, over the Image and TileMap stand-ins of tests/js/sprite_host.c.
SPRITE="src/modules/sprite/native/sprite.c src/modules/sprite/native/sprite_gs.c \
    src/modules/sprite/native/sprite_job.c \
    src/modules/sprite/quickjs/ath_sprite.c tests/js/sprite_host.c \
    src/modules/particles2d/native/particles2d.c src/modules/particles2d/quickjs/ath_particles2d.c \
    src/modules/particles3d/native/particles3d.c src/modules/particles3d/quickjs/ath_particles3d.c"
# Collision; its debug drawing goes to the Draw stubs of runner.c.
COLLISION="src/modules/collision/native/collision.c src/modules/collision/native/collision_gs.c \
    src/modules/collision/quickjs/ath_collision.c"
# The EE has no SIMD for Box2D: build the same scalar path.
B2FLAGS="-DBOX2D_DISABLE_SIMD -DB2_ENABLE_ASSERT"

build() { # object source flags...
    obj=$1; src=$2; shift 2
    rebuild=0
    [ "$obj" -nt "$src" ] || rebuild=1
    case "$src" in src/quickjs/*)
        for header in src/quickjs/*.h; do
            [ "$obj" -nt "$header" ] || rebuild=1
        done
        ;;
    esac
    [ "$rebuild" = 0 ] || $CC $BASE "$@" $INC -c "$src" -o "$obj"
}

for f in cutils libbf libregexp libunicode quickjs; do
    build "$OUT/obj/$f.o" "src/quickjs/$f.c" $ASAN -w
done
build "$OUT/obj/model3d_load.o" src/modules/model3d/native/model3d_load.c $UBSAN -w
build "$OUT/obj/fast_obj.o" src/modules/model3d/native/fast_obj/fast_obj.c $UBSAN -w
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
    src/modules/thread/native/file_job.c src/modules/thread/quickjs/ath_file_job.c \
    src/modules/memcard/quickjs/ath_memcard.c \
    src/modules/random/native/random.c src/modules/random/quickjs/ath_random.c \
    src/modules/noise/native/noise.c src/modules/noise/native/noise_job.c \
    src/modules/noise/quickjs/ath_noise.c \
    src/modules/debug/native/debug_overlay.c src/modules/debug/quickjs/ath_debug.c \
    src/modules/profiler/native/profiler.c src/modules/profiler/quickjs/ath_profiler.c \
    src/modules/debug3d/native/debug3d.c src/modules/debug3d/native/debug3d_gs.c src/modules/debug3d/quickjs/ath_debug3d.c \
    src/modules/savegame/native/savegame_codec.c src/modules/savegame/quickjs/ath_savegame.c \
    src/modules/meshbuilder/native/meshbuilder.c src/modules/meshbuilder/quickjs/ath_meshbuilder.c \
    src/modules/voxel/native/voxel.c src/modules/voxel/quickjs/ath_voxel.c \
    src/modules/lod/native/lod.c src/modules/lod/quickjs/ath_lod.c \
    src/modules/triggers3d/native/triggers3d.c src/modules/triggers3d/quickjs/ath_triggers3d.c \
    src/modules/nav/native/nav.c src/modules/nav/quickjs/ath_nav.c \
    src/modules/audio3d/native/audio3d.c src/modules/audio3d/quickjs/ath_audio3d.c tests/js/sound_stub.c \
    src/modules/sky/native/sky.c src/modules/sky/native/sky_gs.c src/modules/sky/quickjs/ath_sky.c \
    src/runtime/quickjs/ath_output.c $CAMERA $SPRITE $COLLISION $THREE_D \
    "$OUT"/obj/*.o -lm -lpthread

# runner_font: the same runner with the real Font binding (over the native
# stand-in tests/js/font_host.c) instead of the JavaScript stub, for scripts
# that must take the console's path: FontRender lifetimes, preload slices.
$CC $BASE $UBSAN $B2FLAGS -DRUNNER_REAL_FONT -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
    -Wno-missing-field-initializers -Wno-cast-function-type -Werror $MC $INC -Isrc/modules/font/include \
    -o "$OUT/runner_font" tests/js/runner.c src/modules/box2d/native/box2d.c src/modules/box2d/quickjs/*.c \
    tests/js/memcard_host.c src/modules/memcard/native/memcard.c src/modules/memcard/native/memcard_job.c \
    src/modules/thread/native/job.c src/modules/thread/quickjs/ath_job.c \
    src/modules/thread/native/file_job.c src/modules/thread/quickjs/ath_file_job.c \
    src/modules/memcard/quickjs/ath_memcard.c \
    src/modules/random/native/random.c src/modules/random/quickjs/ath_random.c \
    src/modules/noise/native/noise.c src/modules/noise/native/noise_job.c \
    src/modules/noise/quickjs/ath_noise.c \
    src/modules/debug/native/debug_overlay.c src/modules/debug/quickjs/ath_debug.c \
    src/modules/profiler/native/profiler.c src/modules/profiler/quickjs/ath_profiler.c \
    src/modules/debug3d/native/debug3d.c src/modules/debug3d/native/debug3d_gs.c src/modules/debug3d/quickjs/ath_debug3d.c \
    src/modules/savegame/native/savegame_codec.c src/modules/savegame/quickjs/ath_savegame.c \
    src/modules/meshbuilder/native/meshbuilder.c src/modules/meshbuilder/quickjs/ath_meshbuilder.c \
    src/modules/voxel/native/voxel.c src/modules/voxel/quickjs/ath_voxel.c \
    src/modules/lod/native/lod.c src/modules/lod/quickjs/ath_lod.c \
    src/modules/triggers3d/native/triggers3d.c src/modules/triggers3d/quickjs/ath_triggers3d.c \
    src/modules/nav/native/nav.c src/modules/nav/quickjs/ath_nav.c \
    src/modules/audio3d/native/audio3d.c src/modules/audio3d/quickjs/ath_audio3d.c tests/js/sound_stub.c \
    src/modules/sky/native/sky.c src/modules/sky/native/sky_gs.c src/modules/sky/quickjs/ath_sky.c \
    src/runtime/quickjs/ath_output.c $CAMERA $SPRITE $COLLISION $THREE_D \
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
echo "== tests/array_literal_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/array_literal_test.js "QuickJS array literal tests passed"
echo "== tests/three_d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/three_d_test.js "3D module tests passed"
echo "== tests/lights_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/lights_test.js "3D lighting tests passed"
echo "== tests/geometry3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/geometry3d_test.js "3D procedural geometry tests passed"
echo "== tests/textures3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/textures3d_test.js "3D texture tests passed"
echo "== tests/scene3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/scene3d_test.js "Scene3D tests passed"
echo "== tests/animation3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/animation3d_test.js "Animation3D tests passed"
echo "== tests/gltf3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/gltf3d_test.js "glTF3D tests passed"
echo "== tests/camerarig3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/camerarig3d_test.js "CameraRig3D tests passed"
echo "== tests/tween3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/tween3d_test.js "Tween3D tests passed"
echo "== tests/collision3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/collision3d_test.js "Collision3D tests passed"
echo "== tests/physics3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/physics3d_test.js "Physics3D tests passed"
echo "== tests/particles2d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/particles2d_test.js "Particles2D tests passed"
echo "== tests/particles3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/particles3d_test.js "Particles3D tests passed"
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
echo "== tests/profiler_test.js"
check "$OUT/runner" tests/profiler_test.js "Profiler module test passed"
echo "== tests/bench_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/bench_test.js "Bench module test passed"
echo "== tests/debug3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/debug3d_test.js "Debug3D module test passed"
echo "== tests/input_test.js"
check "$OUT/runner" tests/input_test.js "Input module test passed"
echo "== tests/replay_test.js"
check "$OUT/runner" tests/replay_test.js "Replay module test passed"
echo "== tests/savegame_test.js"
check "$OUT/runner" tests/savegame_test.js "SaveGame module test passed"
echo "== tests/assets3d_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/assets3d_test.js "Assets3D module test passed"
echo "== tests/meshbuilder_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/meshbuilder_test.js "MeshBuilder module test passed"
echo "== tests/voxel_test.js (two fresh runtimes)"
ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/voxel_test.js "Voxel module test passed"
for t in lod triggers3d nav audio3d sky; do
    echo "== tests/${t}_test.js (two fresh runtimes)"
    ATHENA_TEST_REPEAT=2 check "$OUT/runner" tests/${t}_test.js "module test passed"
done
echo "== tests/random_noise_test.js"
check "$OUT/runner" tests/random_noise_test.js "Random and Noise module test passed"
echo "== tests/camera2d_test.js"
check "$OUT/runner" tests/camera2d_test.js "Camera2D module test passed"
echo "== tests/sprite_test.js"
check "$OUT/runner" tests/sprite_test.js "Sprite module test passed"
echo "== tests/collision_test.js"
check "$OUT/runner" tests/collision_test.js "Collision module test passed"
echo "== tests/scene_test.js"
check "$OUT/runner" tests/scene_test.js "Scene module test passed"
echo "== tests/memcard_test.js"
check "$OUT/runner" tests/memcard_test.js "Result: .* 0 failed"
