#ifndef ATHENA_SOUND_JS_H
#define ATHENA_SOUND_JS_H
#include <ath_env.h>
#include <athena/sound.h>
/* The native sample of a live Sound.Sfx, or NULL (not an Sfx, or freed).
 * Needs no context and throws nothing: modules that keep the JS object can
 * look the sample up again each frame, so free() never leaves them dangling. */
AthenaSfx *athena_sfx_js_peek(JSValueConst value);
#endif
