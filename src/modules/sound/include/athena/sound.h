#ifndef ATH_NATIVE_SOUND_H
#define ATH_NATIVE_SOUND_H

/*
 * Sound: ADPCM sound effects on the SPU2 voices and one streamed WAV/OGG
 * track, both through audsrv.
 *
 * audsrv (and libsd) are loaded on the IOP the first time any function below
 * needs them; `audsrv = true` in athena.ini still loads them at boot. An IOP
 * reset unloads them: streams keep working after the next call, sound effects
 * loaded before the reset become invalid and report ATHENA_SOUND_ERR_STALE.
 */

#include <stdbool.h>
#include <stdint.h>

#define ATHENA_SOUND_CHANNELS 24
#define ATHENA_SOUND_MAX_VOLUME 100
#define ATHENA_SOUND_MIN_PAN (-100)
#define ATHENA_SOUND_MAX_PAN 100

typedef enum {
    ATHENA_SOUND_OK = 0,
    /* Arguments out of range. */
    ATHENA_SOUND_ERR_ARGS = -1,
    /* audsrv could not be loaded or initialized on the IOP. */
    ATHENA_SOUND_ERR_IOP = -2,
    ATHENA_SOUND_ERR_OPEN = -3,
    ATHENA_SOUND_ERR_READ = -4,
    /* Not a WAV/OGG/ADPCM file, or an encoding audsrv cannot play. */
    ATHENA_SOUND_ERR_FORMAT = -5,
    ATHENA_SOUND_ERR_MEMORY = -6,
    /* Not enough SPU2 or IOP memory for the sample. */
    ATHENA_SOUND_ERR_SPU_MEMORY = -7,
    /* The sound effect was loaded before an IOP reset. */
    ATHENA_SOUND_ERR_STALE = -8,
    /* The streaming thread could not be started. */
    ATHENA_SOUND_ERR_THREAD = -9,
} AthenaSoundResult;

const char *athena_sound_result_string(int result);

/* Loads audsrv on the IOP if needed. */
int athena_sound_ensure(void);
bool athena_sound_ready(void);

/* Stream volume, 0..100. Applied when audsrv is (re)loaded. */
int athena_sound_set_volume(int volume);
int athena_sound_get_volume(void);

/* --- Streams: one plays at a time --------------------------------------- */

typedef struct AthenaSoundStream AthenaSoundStream;

typedef enum {
    ATHENA_SOUND_STREAM_WAV,
    ATHENA_SOUND_STREAM_OGG,
} AthenaSoundStreamType;

/* Opens a PCM WAV (8/16-bit, mono/stereo) or an Ogg Vorbis file. */
AthenaSoundStream *athena_sound_stream_open(const char *path, int *result);
/* Stops the stream if it is the one playing. */
void athena_sound_stream_destroy(AthenaSoundStream *stream);

/* Starts or resumes; replaces the stream that was playing. */
int athena_sound_stream_play(AthenaSoundStream *stream);
/* Pauses at the position heard; play() resumes from there. */
void athena_sound_stream_pause(AthenaSoundStream *stream);
bool athena_sound_stream_is_playing(AthenaSoundStream *stream);
void athena_sound_stream_rewind(AthenaSoundStream *stream);

/* Milliseconds. set_position clamps to the stream length. */
uint32_t athena_sound_stream_get_length(const AthenaSoundStream *stream);
uint32_t athena_sound_stream_get_position(AthenaSoundStream *stream);
int athena_sound_stream_set_position(AthenaSoundStream *stream, uint32_t ms);

void athena_sound_stream_set_loop(AthenaSoundStream *stream, bool loop);
bool athena_sound_stream_get_loop(AthenaSoundStream *stream);

AthenaSoundStreamType athena_sound_stream_get_type(const AthenaSoundStream *stream);
int athena_sound_stream_get_rate(const AthenaSoundStream *stream);
int athena_sound_stream_get_channels(const AthenaSoundStream *stream);
int athena_sound_stream_get_bits(const AthenaSoundStream *stream);

/* --- Sound effects: ADPCM samples resident in SPU2 memory --------------- */

typedef struct AthenaSfx AthenaSfx;

/* Uploads an .adp file (APCM header, as written by adpenc). */
AthenaSfx *athena_sfx_load(const char *path, int *result);
void athena_sfx_destroy(AthenaSfx *sfx);

/*
 * Plays on `channel` (0..23), or on a free channel when `channel` is < 0.
 * Returns the channel, -1 when it (or every channel) is busy, or a negative
 * AthenaSoundResult below -1.
 */
int athena_sfx_play(AthenaSfx *sfx, int channel);
/* 1 playing, 0 not playing, < 0 AthenaSoundResult. */
int athena_sfx_is_playing(AthenaSfx *sfx, int channel);
/* A channel no sound effect is playing on, or -1. */
int athena_sfx_find_channel(void);

uint32_t athena_sfx_get_length(const AthenaSfx *sfx);
int athena_sfx_get_rate(const AthenaSfx *sfx);
bool athena_sfx_get_loop(const AthenaSfx *sfx);
int athena_sfx_get_volume(const AthenaSfx *sfx);
int athena_sfx_set_volume(AthenaSfx *sfx, int volume);
int athena_sfx_get_pan(const AthenaSfx *sfx);
int athena_sfx_set_pan(AthenaSfx *sfx, int pan);

#endif /* ATH_NATIVE_SOUND_H */
