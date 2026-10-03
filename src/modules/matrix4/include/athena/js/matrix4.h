#ifndef ATHENA_MATRIX4_JS_H
#define ATHENA_MATRIX4_JS_H
#include <ath_env.h>
#include <athena/matrix4.h>
AthenaMatrix4 *athena_matrix4_from_value(JSContext *ctx, JSValueConst value);
/* Always an independently owned copy, never a view into a parent object. */
JSValue athena_matrix4_to_value(JSContext *ctx, const AthenaMatrix4 *matrix);
#endif
