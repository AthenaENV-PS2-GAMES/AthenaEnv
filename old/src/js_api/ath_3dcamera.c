#include <ath_env.h>
#include <athena/camera3d.h>

static JSValue js_camsave(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
    AthenaCamera3dState state;
    athena_camera3d_save(&state);

    JSValue obj = JS_NewObject(ctx);

    JSValue pos = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, pos, "x", JS_NewFloat32(ctx, state.position[0]));
    JS_SetPropertyStr(ctx, pos, "y", JS_NewFloat32(ctx, state.position[1]));
    JS_SetPropertyStr(ctx, pos, "z", JS_NewFloat32(ctx, state.position[2]));
    JS_SetPropertyStr(ctx, obj, "position", pos);

    JSValue tgt = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, tgt, "x", JS_NewFloat32(ctx, state.target[0]));
    JS_SetPropertyStr(ctx, tgt, "y", JS_NewFloat32(ctx, state.target[1]));
    JS_SetPropertyStr(ctx, tgt, "z", JS_NewFloat32(ctx, state.target[2]));
    JS_SetPropertyStr(ctx, obj, "target", tgt);

    JSValue up = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, up, "x", JS_NewFloat32(ctx, state.up[0]));
    JS_SetPropertyStr(ctx, up, "y", JS_NewFloat32(ctx, state.up[1]));
    JS_SetPropertyStr(ctx, up, "z", JS_NewFloat32(ctx, state.up[2]));
    JS_SetPropertyStr(ctx, obj, "up", up);

    JSValue lup = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, lup, "x", JS_NewFloat32(ctx, state.local_up[0]));
    JS_SetPropertyStr(ctx, lup, "y", JS_NewFloat32(ctx, state.local_up[1]));
    JS_SetPropertyStr(ctx, lup, "z", JS_NewFloat32(ctx, state.local_up[2]));
    JS_SetPropertyStr(ctx, obj, "local_up", lup);

    return obj;
}

static JSValue js_camrestore(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
    if (argc < 1) return JS_ThrowSyntaxError(ctx, "expected state object");
    JSValue state_obj = argv[0];
    AthenaCamera3dState state;

    JSValue pos = JS_GetPropertyStr(ctx, state_obj, "position");
    JS_ToFloat32(ctx, &state.position[0], JS_GetPropertyStr(ctx, pos, "x"));
    JS_ToFloat32(ctx, &state.position[1], JS_GetPropertyStr(ctx, pos, "y"));
    JS_ToFloat32(ctx, &state.position[2], JS_GetPropertyStr(ctx, pos, "z"));
    state.position[3] = 0.0f;
    JS_FreeValue(ctx, pos);

    JSValue tgt = JS_GetPropertyStr(ctx, state_obj, "target");
    JS_ToFloat32(ctx, &state.target[0], JS_GetPropertyStr(ctx, tgt, "x"));
    JS_ToFloat32(ctx, &state.target[1], JS_GetPropertyStr(ctx, tgt, "y"));
    JS_ToFloat32(ctx, &state.target[2], JS_GetPropertyStr(ctx, tgt, "z"));
    state.target[3] = 0.0f;
    JS_FreeValue(ctx, tgt);

    JSValue up = JS_GetPropertyStr(ctx, state_obj, "up");
    if (!JS_IsUndefined(up) && !JS_IsNull(up)) {
        JS_ToFloat32(ctx, &state.up[0], JS_GetPropertyStr(ctx, up, "x"));
        JS_ToFloat32(ctx, &state.up[1], JS_GetPropertyStr(ctx, up, "y"));
        JS_ToFloat32(ctx, &state.up[2], JS_GetPropertyStr(ctx, up, "z"));
        state.up[3] = 0.0f;
    } else {
        state.up[0] = 0.0f; state.up[1] = 1.0f; state.up[2] = 0.0f; state.up[3] = 0.0f;
    }
    JS_FreeValue(ctx, up);

    JSValue lup = JS_GetPropertyStr(ctx, state_obj, "local_up");
    if (!JS_IsUndefined(lup) && !JS_IsNull(lup)) {
        JS_ToFloat32(ctx, &state.local_up[0], JS_GetPropertyStr(ctx, lup, "x"));
        JS_ToFloat32(ctx, &state.local_up[1], JS_GetPropertyStr(ctx, lup, "y"));
        JS_ToFloat32(ctx, &state.local_up[2], JS_GetPropertyStr(ctx, lup, "z"));
        state.local_up[3] = 1.0f;
    } else {
        state.local_up[0] = 0.0f; state.local_up[1] = 1.0f; state.local_up[2] = 0.0f; state.local_up[3] = 1.0f;
    }
    JS_FreeValue(ctx, lup);

    athena_camera3d_restore(&state);
    return JS_UNDEFINED;
}

static JSValue js_camposition(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	if (argc != 3) return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
	float x, y, z;
	JS_ToFloat32(ctx, &x, argv[0]);
	JS_ToFloat32(ctx, &y, argv[1]);
	JS_ToFloat32(ctx, &z, argv[2]);

	athena_camera3d_position(x, y, z);
	return JS_UNDEFINED;
}

static JSValue js_camtarget(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	if (argc != 3) return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
	float x, y, z;
	JS_ToFloat32(ctx, &x, argv[0]);
	JS_ToFloat32(ctx, &y, argv[1]);
	JS_ToFloat32(ctx, &z, argv[2]);

	athena_camera3d_target(x, y, z);
	return JS_UNDEFINED;
}

static JSValue js_camorbit(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	float yaw, pitch;
	JS_ToFloat32(ctx, &yaw, argv[0]);
	JS_ToFloat32(ctx, &pitch, argv[1]);

	athena_camera3d_orbit(yaw, pitch);
	return JS_UNDEFINED;
}

static JSValue js_camturn(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	float yaw, pitch;
	JS_ToFloat32(ctx, &yaw, argv[0]);
	JS_ToFloat32(ctx, &pitch, argv[1]);

	athena_camera3d_turn(yaw, pitch);
	return JS_UNDEFINED;
}

static JSValue js_campan(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	float x, y;
	JS_ToFloat32(ctx, &x, argv[0]);
	JS_ToFloat32(ctx, &y, argv[1]);

	athena_camera3d_pan(x, y);
	return JS_UNDEFINED;
}

static JSValue js_camdolly(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	float dist;
	JS_ToFloat32(ctx, &dist, argv[0]);

	athena_camera3d_dolly(dist);
	return JS_UNDEFINED;
}

static JSValue js_camzoom(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	float dist;
	JS_ToFloat32(ctx, &dist, argv[0]);

	athena_camera3d_zoom(dist);
	return JS_UNDEFINED;
}

static JSValue js_camupdate(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	athena_camera3d_update();
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry camera_funcs[] = {
  JS_CFUNC_DEF("position", 3, js_camposition),
  JS_CFUNC_DEF("target", 3, js_camtarget),
  JS_CFUNC_DEF("orbit", 2, js_camorbit),
  JS_CFUNC_DEF("turn", 2, js_camturn),
  JS_CFUNC_DEF("dolly", 1, js_camdolly),
  JS_CFUNC_DEF("zoom", 1, js_camzoom),
  JS_CFUNC_DEF("pan", 2, js_campan),
  JS_CFUNC_DEF("save", 0, js_camsave),
  JS_CFUNC_DEF("restore", 1, js_camrestore),
  JS_CFUNC_DEF("update", 0, js_camupdate),
};

static int js_camera_init(JSContext *ctx, JSModuleDef *m)
{
    return JS_SetModuleExportList(ctx, m, camera_funcs, countof(camera_funcs));
}

JSModuleDef *athena_3dcamera_init(JSContext* ctx){
	return athena_push_module(ctx, js_camera_init, camera_funcs, countof(camera_funcs), "Camera");
}
