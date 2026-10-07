#!/bin/sh
# Run inside the PS2SDK build image, after module configuration on the host:
# node tools/modules.js configure --modules=screen,loop,scene3d,animation3d,gltf3d,camerarig3d,collision3d,physics3d,tween3d,tween,particles2d,particles3d,image,color,draw,tilemap,system,timer,usbmass
# docker run --rm --entrypoint /bin/sh -v "$PWD:/src" -w /src <build-image> tools/build_3d.sh
set -eu
cd "$(dirname "$0")/.."
make -j4 RUNTIME=quickjs EE_BIN_PREF=athena_3d_js
make -j4 RUNTIME=native APP_SRCS=samples/native/3d/main.c EE_BIN_PREF=athena_3d_native
make -j4 RUNTIME=native APP_SRCS=samples/native/3d_clipping/main.c EE_BIN_PREF=athena_3d_clip_native
make -j4 RUNTIME=native APP_SRCS=samples/native/3d_regression/main.c EE_BIN_PREF=athena_3d_regression_native
make -j4 RUNTIME=native APP_SRCS=samples/native/3d_lighting/main.c EE_BIN_PREF=athena_3d_lighting_native
make -j4 RUNTIME=native APP_SRCS=samples/native/3d_textures/main.c EE_BIN_PREF=athena_3d_textures_native
make -j4 RUNTIME=native APP_SRCS=samples/native/3d_scene/main.c EE_BIN_PREF=athena_3d_scene_native
make -j4 RUNTIME=native APP_SRCS=samples/native/3d_profile/main.c EE_BIN_PREF=athena_3d_profile_native
sh tests/host/run.sh
