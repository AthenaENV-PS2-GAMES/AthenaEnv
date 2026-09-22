#ifndef ATHENA_IMAGE_QUICKJS_H
#define ATHENA_IMAGE_QUICKJS_H

#include <ath_env.h>
#include "../native/image.h"

JSModuleDef *athena_image_init(JSContext *ctx);
AthenaImage *athena_image_from_value(JSContext *ctx, JSValueConst value);
JSValue athena_image_to_value(JSContext *ctx, AthenaImage *image);

#endif
