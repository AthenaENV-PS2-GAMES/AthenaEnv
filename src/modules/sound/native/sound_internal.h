#ifndef ATH_NATIVE_SOUND_INTERNAL_H
#define ATH_NATIVE_SOUND_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

/*
 * State shared by the Sound sources. audsrv is started and stopped only from
 * the script thread (first use, IOP.loadModule, IOP.reset, shutdown).
 */

/* Changes every time audsrv is initialized; samples of older ones are gone. */
uint32_t sound_iop_generation(void);

/* Stops the streaming thread and mutes the stream (sound_stream.c). */
void sound_stream_halt(void);
/* Applies the stream volume after audsrv is (re)initialized. */
void sound_stream_audsrv_started(void);

/* Forgets the channel owners; the IOP drops every sample (sound_sfx.c). */
void sound_sfx_audsrv_started(void);

#endif /* ATH_NATIVE_SOUND_INTERNAL_H */
