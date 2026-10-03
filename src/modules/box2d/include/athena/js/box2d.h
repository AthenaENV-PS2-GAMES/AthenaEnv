#ifndef ATHENA_JS_BOX2D_H
#define ATHENA_JS_BOX2D_H

#include <quickjs.h>

#include <box2d/box2d.h>

/*
 * For modules extending Box2D from scripts (e.g. box2ddraw): the Box2D world
 * of a live World object, or b2_nullWorldId with a TypeError set (not a
 * World, or destroyed). `where` names the calling function in the message.
 */
b2WorldId athena_box2d_js_world(JSContext *ctx, JSValueConst value, const char *where);

#endif /* ATHENA_JS_BOX2D_H */
