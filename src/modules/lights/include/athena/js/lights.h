#ifndef ATHENA_JS_LIGHTS_H
#define ATHENA_JS_LIGHTS_H
#include <ath_env.h>
#include <athena/lights.h>
AthenaLights *athena_lights_from_value(JSContext *ctx,JSValueConst value);
#endif
