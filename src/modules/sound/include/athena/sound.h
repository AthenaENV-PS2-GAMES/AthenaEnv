#ifndef ATH_NATIVE_SOUND_H
#define ATH_NATIVE_SOUND_H

/*
 * Sound: ADPCM sound effects on the SPU2 voices and one streamed WAV/OGG
 * track, both through audsrv.
 *
 * audsrv (and libsd) are loaded on the IOP the first time any function below
 * needs them; `audsrv = true` in athena.ini still loads them at boot. An IOP
 * reset unloads them: streams keep working after the next call, and sound
 * effects are uploaded again from their file the next time they play.
 */

#include <stdbool.h>
#include <stdint.h>

#define ATHENA_SOUND_CHANNELS 24
#define ATHENA_SOUND_MAX_VOLUME 100
#define ATHENA_SOUND_MIN_PAN (-100)
#define ATHENA_SOUND_MAX_PAN 100
/* Longest fade accepted by play/pause/stop, in milliseconds. */
#define ATHENA_SOUND_MAX_FADE_MS 60000

typedef enum {
    ATHENA_SOUND_OK = 0,
    /* Arguments out of range. */
    ATHENA_SOUND_ERR_ARGS = -1,
    /* audsrv could not be loaded or initialized on the IOP. */
    ATHENA_SOUND_ERR_IOP = -2,
    ATHENA_SOUND_ERR_OPEN = -3,
    ATHENA_SOUND_ERR_READ = -4,
    /* Not a WAV/OGG/ADPCM file, or an encoding that cannot be played. */
    ATHENA_SOUND_ERR_FORMAT = -5,
    ATHENA_SOUND_ERR_MEMORY = -6,
    /* Not enough SPU2 or IOP memory for the sample. */
    ATHENA_SOUND_ERR_SPU_MEMORY = -7,
    /* ADPCM data the SPU2 would play past its end (truncated, no end flag). */
    ATHENA_SOUND_ERR_CORRUPT = -8,
    /* The streaming thread could not be started. */
    ATHENA_SOUND_ERR_THREAD = -9,
} AthenaSoundResult;

const char *athena_sound_result_string(int result);
/* Stable identifier for scripts: "BAD_FORMAT", "SPU_MEMORY"... */
const char *athena_sound_result_code(int result);
/*
 * Detail of the last failed open/load on the calling thread's side (script
 * thread), e.g. "needs 40000 bytes, 12000 free"; "" when there is none.
 */
const char *athena_sound_error_detail(void);

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

/*
 * Opens a WAV (PCM 8/16/24/32-bit or 32-bit float) or an Ogg Vorbis file,
 * mono or stereo. Formats audsrv cannot play directly (e.g. 16 kHz, 8-bit
 * stereo, float) are converted on the EE while they play.
 */
AthenaSoundStream *athena_sound_stream_open(const char *path, int *result);
/* Stops the stream if it is the one playing. */
void athena_sound_stream_destroy(AthenaSoundStream *stream);

/*
 * Starts or resumes; replaces the stream that was playing. With fade_ms > 0
 * the stream starts silent and reaches its volume after fade_ms.
 */
int athena_sound_stream_play(AthenaSoundStream *stream, uint32_t fade_ms);
/* Pauses at the position heard (after fading out for fade_ms). */
void athena_sound_stream_pause(AthenaSoundStream *stream, uint32_t fade_ms);
/* Pauses and rewinds (after fading out for fade_ms). */
void athena_sound_stream_stop(AthenaSoundStream *stream, uint32_t fade_ms);
/* True from play() until paused, stopped or finished (fades included). */
bool athena_sound_stream_is_playing(AthenaSoundStream *stream);
void athena_sound_stream_rewind(AthenaSoundStream *stream);

/* Milliseconds. set_position clamps to the stream length. */
uint32_t athena_sound_stream_get_length(const AthenaSoundStream *stream);
uint32_t athena_sound_stream_get_position(AthenaSoundStream *stream);
int athena_sound_stream_set_position(AthenaSoundStream *stream, uint32_t ms);

void athena_sound_stream_set_loop(AthenaSoundStream *stream, bool loop);
bool athena_sound_stream_get_loop(AthenaSoundStream *stream);

/*
 * Event counters, updated by the streaming thread: `ends` when a stream
 * without loop reaches its end, `loops` when a looping stream wraps. Both
 * are counted when the decoder gets there, up to ~0.1 s before it is heard.
 */
void athena_sound_stream_get_events(AthenaSoundStream *stream, uint32_t *ends, uint32_t *loops);
/* Reached its end; cleared by play(), a seek or rewind(). */
bool athena_sound_stream_ended(AthenaSoundStream *stream);

AthenaSoundStreamType athena_sound_stream_get_type(const AthenaSoundStream *stream);
/* Format of the file, not the one sent to audsrv. */
int athena_sound_stream_get_rate(const AthenaSoundStream *stream);
int athena_sound_stream_get_channels(const AthenaSoundStream *stream);
int athena_sound_stream_get_bits(const AthenaSoundStream *stream);
/* Converted on the EE because audsrv cannot play the file's format. */
bool athena_sound_stream_is_converted(const AthenaSoundStream *stream);

/* --- Sound effects: ADPCM samples resident in SPU2 memory --------------- */

typedef struct AthenaSfx AthenaSfx;

/*
 * Uploads an .adp file (APCM header, as written by adpenc or
 * tools/wav2adp.js). The file is checked first: data that does not end with
 * an end flag is refused, since the SPU2 would play into other samples.
 */
AthenaSfx *athena_sfx_load(const char *path, int *result);
void athena_sfx_destroy(AthenaSfx *sfx);

/*
 * Plays on `channel` (0..23), or on a free channel when `channel` is < 0.
 * Returns the channel, -1 when it (or every channel) is busy, or a negative
 * AthenaSoundResult below -1. A sample unloaded by an IOP reset is uploaded
 * again from its file first.
 */
int athena_sfx_play(AthenaSfx *sfx, int channel);
/*
 * 1 playing, 0 not playing, < 0 AthenaSoundResult. A looping sample plays
 * until stopped, freed or replaced (the SPU2 flags a loop as ended after
 * its first pass, so this is tracked on the EE).
 */
int athena_sfx_is_playing(AthenaSfx *sfx, int channel);
/*
 * Silences `sfx` on `channel`, or on every channel when `channel` is < 0.
 * audsrv cannot key a voice off, so the voice is muted: a one-shot ends by
 * itself, a loop keeps its channel until another sample plays there.
 */
int athena_sfx_stop(AthenaSfx *sfx, int channel);
/* A channel no sound effect is playing on, or -1. */
int athena_sfx_find_channel(void);

uint32_t athena_sfx_get_length(const AthenaSfx *sfx);
int athena_sfx_get_rate(const AthenaSfx *sfx);
bool athena_sfx_get_loop(const AthenaSfx *sfx);
int athena_sfx_get_volume(const AthenaSfx *sfx);
int athena_sfx_set_volume(AthenaSfx *sfx, int volume);
int athena_sfx_get_pan(const AthenaSfx *sfx);
int athena_sfx_set_pan(AthenaSfx *sfx, int pan);

/* Scales every sound effect's volume, 0..100; also updates playing voices. */
int athena_sfx_set_master_volume(int volume);
int athena_sfx_get_master_volume(void);

/*
 * SPU2 sample memory, in bytes. audsrv places each sample after the last
 * one and only reclaims memory freed at the end, so a sample freed before
 * later ones leaves a hole (`wasted`) until those are freed too.
 */
typedef struct {
    uint32_t total;
    /* From the start of sample memory to the end of the last sample. */
    uint32_t used;
    /* Free after the last sample: the largest sample that still fits. */
    uint32_t free;
    /* Freed but not reusable yet. */
    uint32_t wasted;
    uint32_t samples;
} AthenaSoundMemoryStats;

void athena_sfx_get_memory_stats(AthenaSoundMemoryStats *stats);

#endif /* ATH_NATIVE_SOUND_H */
