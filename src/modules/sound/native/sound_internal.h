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

/*
 * `path` made absolute with the current directory (malloc'd), so a file
 * reopened after an IOP reset is found even if the script changed directory.
 */
char *sound_absolute_path(const char *path);

/* Sets athena_sound_error_detail(); NULL clears it. */
void sound_set_detail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Stops the streaming thread and mutes the stream (sound_stream.c). */
void sound_stream_halt(void);
/* Applies the stream volume after audsrv is (re)initialized. */
void sound_stream_audsrv_started(void);

/*
 * audsrv started or is stopping: every sample and voice of the session is
 * gone. Forgets the channel owners and the SPU2 memory map (sound_sfx.c).
 */
void sound_sfx_forget_session(void);

#endif /* ATH_NATIVE_SOUND_INTERNAL_H */
