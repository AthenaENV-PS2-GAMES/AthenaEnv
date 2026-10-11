#ifndef ATHENA_SCENE3D_JS_H
#define ATHENA_SCENE3D_JS_H
#include <ath_env.h>
#include <athena/scene3d.h>
/* Borrowed node of a live Scene3D.Node handle; NULL with a pending TypeError. */
AthenaNode3D *athena_node3d_from_value(JSContext *ctx,JSValueConst value);
/* A new Scene3D.Node handle holding its own reference; null for NULL. */
JSValue athena_node3d_to_value(JSContext *ctx,AthenaNode3D *node);
#endif
