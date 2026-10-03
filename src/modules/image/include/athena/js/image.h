#ifndef ATHENA_IMAGE_QUICKJS_H
#define ATHENA_IMAGE_QUICKJS_H

#include <ath_env.h>
#include <athena/image.h>

JSModuleDef *athena_image_init(JSContext *ctx);
AthenaImage *athena_image_from_value(JSContext *ctx, JSValueConst value);
/* Returns NULL without throwing when the Image was freed or finalized. */
AthenaImage *athena_image_peek(JSValueConst value);
JSValue athena_image_to_value(JSContext *ctx, AthenaImage *image);
JSValue athena_image_error_value(JSContext *ctx, const AthenaImage *image,
	const char *path);

#endif
