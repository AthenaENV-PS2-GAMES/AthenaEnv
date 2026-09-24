#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kernel.h>
#include <delaythread.h>
#include <audsrv.h>
#include <vorbis/vorbisfile.h>

#include <athena/debug.h>
#include <athena/mutex.h>
#include <athena/sound.h>
#include <athena/thread.h>

#include "sound_internal.h"

/*
 * One stream plays at a time, fed by a worker thread into audsrv's ring
 * buffer on the IOP. That ring holds only ~100 ms and keeps whatever was
 * last written: after a pause, seek or track change, the first ~50 ms read
 * after audsrv_set_format() would be stale audio. So the worker never stops
 * abruptly: it mutes, queues one ring of silence ("drain") and only then
 * stops or restarts, leaving the ring clean for the next start.
 *
 * audsrv_wait_audio() is not used: it holds audsrv's RPC lock while it waits,
 * which would block sound effects on the script thread. The worker polls
 * audsrv_available() and sleeps for the time the missing bytes take to play.
 */

#define STREAM_CHUNK 4096
#define STREAM_STACK_SIZE (32 * 1024)
/* Above the script thread, which may never block, so audio does not starve. */
#define STREAM_PRIORITY (ATHENA_THREAD_DEFAULT_PRIORITY - 4)
#define STREAM_MIN_SLEEP_US 1000
#define STREAM_MAX_SLEEP_US 10000

struct AthenaSoundStream {
    AthenaSoundStreamType type;
    FILE *file;
    OggVorbis_File *ogg;
    struct audsrv_fmt_t fmt;
    uint32_t frame_bytes;
    uint32_t total_frames;
    /* WAV: file offset of the first sample. */
    long data_offset;
    /* Next frame the decoder produces. */
    uint32_t frame;
    /* While playing: decoder frame at the end of the audio queued so far. */
    uint32_t submitted;
    /* While playing: frame playback started from; the position never goes below. */
    uint32_t start_frame;
    /* While playing: the decoder wrapped around at least once. */
    bool wrapped;
    bool loop;
};

typedef enum {
    PLAYER_IDLE,
    PLAYER_PLAYING,
    /* Queuing silence; then PLAYING again (resume) or IDLE. */
    PLAYER_DRAINING,
} PlayerState;

static struct {
    AthenaMutex *lock;
    AthenaThread *thread;
    /* The worker loops; cleared by the worker itself, under the lock. */
    bool thread_running;
    bool halting;
    AthenaSoundStream *current;
    PlayerState state;
    bool resume_after_drain;
    /* Volume forced to 0 while a pause/seek/switch drains. */
    bool muted;
    /* audsrv_set_format() is due before `current` is fed. */
    bool format_pending;
    uint32_t drain_left;
    int volume;
    /* Last value sent to audsrv, -1 when unknown. */
    int applied_volume;
    /* Format audsrv is configured with. */
    uint32_t frame_bytes;
    uint32_t ring_bytes;
    uint32_t bytes_per_second;
} player = {
    .volume = ATHENA_SOUND_MAX_VOLUME,
    .applied_volume = -1,
};

/* Worker-only buffer. */
static char stream_chunk[STREAM_CHUNK] __attribute__((aligned(64)));

/* Formats audsrv can upsample (iop/sound/audsrv/src/upsamplers.c). */
static const struct {
    int freq;
    int bits;
    int channels;
} supported_formats[] = {
    { 11025, 8, 1 }, { 11025, 8, 2 }, { 11025, 16, 1 }, { 11025, 16, 2 },
    { 12000, 16, 2 },
    { 22050, 8, 1 }, { 22050, 16, 1 }, { 22050, 16, 2 },
    { 24000, 16, 2 },
    { 32000, 8, 1 }, { 32000, 16, 1 }, { 32000, 16, 2 },
    { 44100, 8, 1 }, { 44100, 16, 1 }, { 44100, 16, 2 },
    { 48000, 16, 1 }, { 48000, 16, 2 },
};

static bool format_supported(const struct audsrv_fmt_t *fmt) {
    for (size_t i = 0; i < sizeof(supported_formats) / sizeof(supported_formats[0]); i++) {
        if (supported_formats[i].freq == fmt->freq &&
            supported_formats[i].bits == fmt->bits &&
            supported_formats[i].channels == fmt->channels)
            return true;
    }
    return false;
}

static void player_lock(void) {
    athena_mutex_core_lock(player.lock);
}

static void player_unlock(void) {
    athena_mutex_core_unlock(player.lock);
}

int athena_sound_module_init(void) {
    player.lock = athena_mutex_core_create();
    return player.lock ? 0 : -1;
}

/* --- Decoding ----------------------------------------------------------- */

static uint32_t read_le32(const uint8_t *bytes) {
    return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

/* Walks the RIFF chunks: "fmt " may be longer than 16 bytes and other chunks
 * (LIST, fact, ...) may come before "data". */
static int stream_open_wav(AthenaSoundStream *stream, FILE *file) {
    uint8_t header[12];
    uint8_t chunk[8];
    uint8_t format[16];
    bool have_format = false;
    long file_size;
    uint32_t data_size = 0;

    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0)
        return ATHENA_SOUND_ERR_READ;
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
        return ATHENA_SOUND_ERR_FORMAT;

    for (;;) {
        uint32_t size;
        long next;

        if (fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk))
            return ATHENA_SOUND_ERR_FORMAT;
        size = read_le32(chunk + 4);
        if (memcmp(chunk, "data", 4) == 0) {
            stream->data_offset = ftell(file);
            if (stream->data_offset < 0)
                return ATHENA_SOUND_ERR_READ;
            data_size = size;
            /* Streamed writers leave 0 or 0xFFFFFFFF: the data runs to EOF. */
            if (data_size == 0 || data_size > (uint32_t)(file_size - stream->data_offset))
                data_size = (uint32_t)(file_size - stream->data_offset);
            break;
        }
        if (memcmp(chunk, "fmt ", 4) == 0) {
            if (size < sizeof(format) ||
                fread(format, 1, sizeof(format), file) != sizeof(format))
                return ATHENA_SOUND_ERR_FORMAT;
            have_format = true;
            size -= sizeof(format);
        }
        next = ftell(file) + (long)size + (long)(size & 1);
        if (next < 0 || next > file_size || fseek(file, next, SEEK_SET) != 0)
            return ATHENA_SOUND_ERR_FORMAT;
    }
    if (!have_format)
        return ATHENA_SOUND_ERR_FORMAT;

    /* 1 = PCM, 0xFFFE = WAVE_FORMAT_EXTENSIBLE (PCM in practice). */
    if (read_le16(format) != 1 && read_le16(format) != 0xFFFE)
        return ATHENA_SOUND_ERR_FORMAT;
    stream->fmt.channels = read_le16(format + 2);
    stream->fmt.freq = (int)read_le32(format + 4);
    stream->fmt.bits = read_le16(format + 14);
    if (!format_supported(&stream->fmt))
        return ATHENA_SOUND_ERR_FORMAT;

    stream->type = ATHENA_SOUND_STREAM_WAV;
    stream->file = file;
    stream->frame_bytes = (uint32_t)(stream->fmt.channels * (stream->fmt.bits / 8));
    stream->total_frames = data_size / stream->frame_bytes;
    return fseek(file, stream->data_offset, SEEK_SET) == 0 ?
        ATHENA_SOUND_OK : ATHENA_SOUND_ERR_READ;
}

static int stream_open_ogg(AthenaSoundStream *stream, FILE *file) {
    vorbis_info *info;
    ogg_int64_t total;

    stream->ogg = calloc(1, sizeof(*stream->ogg));
    if (!stream->ogg)
        return ATHENA_SOUND_ERR_MEMORY;
    if (fseek(file, 0, SEEK_SET) != 0 ||
        ov_open_callbacks(file, stream->ogg, NULL, 0, OV_CALLBACKS_DEFAULT) < 0) {
        free(stream->ogg);
        stream->ogg = NULL;
        return ATHENA_SOUND_ERR_FORMAT;
    }
    /* From here ov_clear() closes the file. */
    stream->type = ATHENA_SOUND_STREAM_OGG;
    info = ov_info(stream->ogg, -1);
    if (!info)
        return ATHENA_SOUND_ERR_FORMAT;
    stream->fmt.channels = info->channels;
    stream->fmt.freq = (int)info->rate;
    stream->fmt.bits = 16;
    if (!format_supported(&stream->fmt))
        return ATHENA_SOUND_ERR_FORMAT;
    stream->frame_bytes = (uint32_t)(stream->fmt.channels * 2);
    total = ov_pcm_total(stream->ogg, -1);
    stream->total_frames = total > 0 && total <= (ogg_int64_t)UINT32_MAX ? (uint32_t)total : 0;
    return ATHENA_SOUND_OK;
}

static void stream_close(AthenaSoundStream *stream) {
    if (stream->ogg) {
        ov_clear(stream->ogg);
        free(stream->ogg);
        stream->ogg = NULL;
    } else if (stream->file) {
        fclose(stream->file);
    }
    stream->file = NULL;
}

AthenaSoundStream *athena_sound_stream_open(const char *path, int *result) {
    AthenaSoundStream *stream;
    FILE *file;
    uint8_t magic[4];
    int status;

    if (!path || !path[0]) {
        status = ATHENA_SOUND_ERR_ARGS;
        goto fail;
    }
    stream = calloc(1, sizeof(*stream));
    if (!stream) {
        status = ATHENA_SOUND_ERR_MEMORY;
        goto fail;
    }
    file = fopen(path, "rb");
    if (!file) {
        free(stream);
        status = ATHENA_SOUND_ERR_OPEN;
        goto fail;
    }
    if (fread(magic, 1, sizeof(magic), file) != sizeof(magic)) {
        fclose(file);
        free(stream);
        status = ATHENA_SOUND_ERR_FORMAT;
        goto fail;
    }
    stream->file = file;
    if (memcmp(magic, "OggS", 4) == 0) {
        status = stream_open_ogg(stream, file);
    } else if (memcmp(magic, "RIFF", 4) == 0) {
        status = stream_open_wav(stream, file);
    } else {
        status = ATHENA_SOUND_ERR_FORMAT;
    }
    if (status == ATHENA_SOUND_OK && stream->total_frames == 0)
        status = ATHENA_SOUND_ERR_FORMAT;
    if (status != ATHENA_SOUND_OK) {
        stream_close(stream);
        free(stream);
        goto fail;
    }
    if (result)
        *result = ATHENA_SOUND_OK;
    return stream;

fail:
    if (result)
        *result = status;
    return NULL;
}

static void stream_seek(AthenaSoundStream *stream, uint32_t frame) {
    if (frame > stream->total_frames)
        frame = stream->total_frames;
    if (stream->type == ATHENA_SOUND_STREAM_OGG) {
        if (ov_pcm_seek(stream->ogg, (ogg_int64_t)frame) != 0) {
            dbgprintf("[Sound] ov_pcm_seek(%u) failed\n", (unsigned)frame);
            frame = (uint32_t)ov_pcm_tell(stream->ogg);
        }
    } else {
        fseek(stream->file, stream->data_offset + (long)frame * (long)stream->frame_bytes,
            SEEK_SET);
    }
    stream->frame = frame;
}

/* Decodes up to `bytes` (a multiple of the frame size); *end at end of data. */
static uint32_t stream_decode(AthenaSoundStream *stream, char *out, uint32_t bytes, bool *end) {
    uint32_t filled = 0;

    *end = false;
    if (stream->type == ATHENA_SOUND_STREAM_WAV) {
        uint32_t left = (stream->total_frames - stream->frame) * stream->frame_bytes;
        size_t got;

        if (bytes > left)
            bytes = left;
        got = fread(out, 1, bytes, stream->file);
        got -= got % stream->frame_bytes;
        stream->frame += (uint32_t)got / stream->frame_bytes;
        *end = got < bytes || stream->frame >= stream->total_frames;
        return (uint32_t)got;
    }

    while (filled < bytes) {
        int section = 0;
        long got = ov_read(stream->ogg, out + filled, (int)(bytes - filled), 0, 2, 1, &section);

        if (got == OV_HOLE)
            continue;
        if (got <= 0) {
            if (got < 0)
                dbgprintf("[Sound] ov_read failed: %ld\n", got);
            *end = true;
            break;
        }
        filled += (uint32_t)got;
    }
    stream->frame = (uint32_t)ov_pcm_tell(stream->ogg);
    return filled;
}

/* Fills `bytes`, wrapping to the start of looping streams. */
static uint32_t stream_fill(AthenaSoundStream *stream, char *out, uint32_t bytes, bool *ended) {
    uint32_t filled = 0;

    *ended = false;
    while (filled < bytes) {
        bool end;
        uint32_t got = stream_decode(stream, out + filled, bytes - filled, &end);

        filled += got;
        if (!end)
            continue;
        if (!stream->loop || (got == 0 && stream->frame == 0)) {
            *ended = true;
            break;
        }
        stream_seek(stream, 0);
        stream->wrapped = true;
        stream->start_frame = 0;
    }
    return filled;
}

/* --- Player (all under player.lock) ------------------------------------- */

static uint32_t player_live_frame(AthenaSoundStream *stream) {
    int queued = audsrv_queued();
    uint32_t queued_frames = queued > 0 ? (uint32_t)queued / player.frame_bytes : 0;
    uint32_t frame;

    if (stream->submitted >= queued_frames)
        frame = stream->submitted - queued_frames;
    else if (stream->wrapped)
        frame = stream->total_frames - (queued_frames - stream->submitted) % stream->total_frames;
    else
        frame = 0;
    /* The ring starts with ~50 ms of silence queued before the first chunk. */
    if (!stream->wrapped && frame < stream->start_frame)
        frame = stream->start_frame;
    return frame < stream->total_frames ? frame : stream->total_frames;
}

/* Rewinds the decoder of the playing stream to the frame being heard. */
static void player_keep_heard_position(void) {
    AthenaSoundStream *stream = player.current;

    if (stream && player.state == PLAYER_PLAYING) {
        stream_seek(stream, player_live_frame(stream));
        stream->wrapped = false;
    }
}

static void player_set_volume(int volume) {
    if (player.applied_volume != volume && athena_sound_ready()) {
        audsrv_set_volume(volume);
        player.applied_volume = volume;
    }
}

static void player_drain(bool resume, bool mute) {
    if (player.state == PLAYER_PLAYING) {
        player.state = PLAYER_DRAINING;
        player.drain_left = player.ring_bytes + STREAM_CHUNK;
    }
    player.resume_after_drain = resume;
    if (mute) {
        player.muted = true;
        player_set_volume(0);
    }
}

static void player_start(void) {
    player.state = PLAYER_PLAYING;
    player.format_pending = true;
    player.muted = false;
}

static void stream_worker(void *arg) {
    AthenaThread *self;

    (void)arg;
    /* Set by player_ensure_worker(), which holds the lock until we start. */
    player_lock();
    self = player.thread;
    player_unlock();

    for (;;) {
        struct audsrv_fmt_t format;
        bool set_format = false;
        bool exiting = false;
        int volume = -1;
        int available;
        uint32_t threshold;
        uint32_t bytes;

        player_lock();
        if (player.halting || athena_thread_core_stop_requested() ||
            player.state == PLAYER_IDLE) {
            player.state = PLAYER_IDLE;
            player.thread_running = false;
            exiting = true;
        } else {
            if (player.state == PLAYER_PLAYING && player.format_pending) {
                AthenaSoundStream *stream = player.current;

                format = stream->fmt;
                set_format = true;
                player.format_pending = false;
                player.frame_bytes = stream->frame_bytes;
                player.bytes_per_second = (uint32_t)format.freq * stream->frame_bytes;
                /* audsrv: 10 feeds of 512 output samples, in input bytes. */
                player.ring_bytes = (uint32_t)((512 * format.freq) / 48000) *
                    stream->frame_bytes * 10;
                stream->submitted = stream->frame;
                stream->start_frame = stream->frame;
                stream->wrapped = false;
            }
            if (!set_format && player.frame_bytes == 0) {
                /* Nothing was ever configured to feed. */
                player.state = PLAYER_IDLE;
                player_unlock();
                continue;
            }
            volume = player.muted ? 0 : player.volume;
            if (volume == player.applied_volume)
                volume = -1;
            else
                player.applied_volume = volume;
        }
        player_unlock();

        if (exiting) {
            audsrv_stop_audio();
            break;
        }
        if (set_format) {
            /* Validated at open; also resets the ring's read and write heads. */
            if (audsrv_set_format(&format) != AUDSRV_ERR_NOERROR) {
                dbgprintf("[Sound] audsrv_set_format(%d, %d, %d) failed\n",
                    format.freq, format.bits, format.channels);
                player_lock();
                player.state = PLAYER_IDLE;
                player_unlock();
                continue;
            }
        }
        if (volume >= 0)
            audsrv_set_volume(volume);

        available = audsrv_available();
        threshold = player.ring_bytes / 2 < STREAM_CHUNK ? player.ring_bytes / 2 : STREAM_CHUNK;
        if (available < 0 || (uint32_t)available < threshold) {
            uint32_t missing = threshold - (available > 0 ? (uint32_t)available : 0);
            uint32_t sleep_us = (uint32_t)(((uint64_t)missing * 1000000) / player.bytes_per_second);

            if (sleep_us < STREAM_MIN_SLEEP_US)
                sleep_us = STREAM_MIN_SLEEP_US;
            else if (sleep_us > STREAM_MAX_SLEEP_US)
                sleep_us = STREAM_MAX_SLEEP_US;
            DelayThread(sleep_us);
            continue;
        }
        bytes = (uint32_t)available < STREAM_CHUNK ? (uint32_t)available : STREAM_CHUNK;
        bytes -= bytes % player.frame_bytes;

        /* Held across the RPC so the position can be read consistently. */
        player_lock();
        if (player.state == PLAYER_PLAYING && player.current) {
            AthenaSoundStream *stream = player.current;
            bool ended;
            uint32_t got = stream_fill(stream, stream_chunk, bytes, &ended);

            if (got > 0)
                audsrv_play_audio(stream_chunk, (int)got);
            stream->submitted = stream->frame;
            if (ended) {
                /* Let the tail play out; the next play() starts over. */
                stream_seek(stream, 0);
                player_drain(false, false);
            }
        } else if (player.state == PLAYER_DRAINING) {
            if (bytes > player.drain_left)
                bytes = player.drain_left;
            memset(stream_chunk, 0, bytes);
            audsrv_play_audio(stream_chunk, (int)bytes);
            player.drain_left -= bytes;
            if (player.drain_left == 0) {
                if (player.resume_after_drain && player.current)
                    player_start();
                else
                    player.state = PLAYER_IDLE;
            }
        }
        player_unlock();
    }

    athena_thread_core_worker_finished(self);
    ExitThread();
}

/* Joins a worker that has left its loop. */
static void player_join(AthenaThread *thread) {
    if (!thread)
        return;
    athena_thread_core_stop(thread);
    athena_thread_core_wait(thread);
    /* worker_finished() is signalled just before ExitThread(). */
    for (int attempts = 0; attempts < 100 &&
        athena_thread_core_get_status(thread) != THS_DORMANT; attempts++)
        DelayThread(100);
    athena_thread_core_finalize(thread);
}

/* Starts the worker if it is not looping. Called under the lock. */
static int player_ensure_worker(void) {
    ee_thread_status_t status;
    int priority = STREAM_PRIORITY;

    if (player.thread_running)
        return ATHENA_SOUND_OK;
    /* A previous worker has already stopped the audio and is exiting. */
    player_join(player.thread);
    player.thread = NULL;
    /* Stay above the script thread even if it runs at a raised priority. */
    if (ReferThreadStatus(GetThreadId(), &status) >= 0 &&
        status.current_priority <= priority)
        priority = status.current_priority > 1 ? status.current_priority - 1 : 1;
    player.thread = athena_thread_core_create("Sound stream", stream_worker, NULL,
        STREAM_STACK_SIZE, priority);
    if (!player.thread)
        return ATHENA_SOUND_ERR_THREAD;
    player.thread_running = true;
    if (athena_thread_core_start(player.thread) < 0) {
        player.thread_running = false;
        athena_thread_core_destroy(player.thread);
        player.thread = NULL;
        return ATHENA_SOUND_ERR_THREAD;
    }
    return ATHENA_SOUND_OK;
}

void sound_stream_halt(void) {
    AthenaThread *thread;

    if (!player.lock)
        return;
    player_lock();
    player.halting = true;
    thread = player.thread;
    player_unlock();

    player_join(thread);

    player_lock();
    player.thread = NULL;
    player.halting = false;
    player.thread_running = false;
    player.state = PLAYER_IDLE;
    player.muted = false;
    player.applied_volume = -1;
    player_unlock();
}

void sound_stream_audsrv_started(void) {
    /* No worker runs here: audsrv was just (re)loaded. */
    player.applied_volume = -1;
}

/* --- Public API ---------------------------------------------------------- */

void athena_sound_stream_destroy(AthenaSoundStream *stream) {
    if (!stream)
        return;
    if (player.lock) {
        player_lock();
        if (player.current == stream) {
            player_drain(false, true);
            player.current = NULL;
        }
        player_unlock();
    }
    stream_close(stream);
    free(stream);
}

int athena_sound_stream_play(AthenaSoundStream *stream) {
    int result;

    if (!stream)
        return ATHENA_SOUND_ERR_ARGS;
    result = athena_sound_ensure();
    if (result < 0)
        return result;

    player_lock();
    if (player.current != stream) {
        player_keep_heard_position();
        player.current = stream;
        if (player.state == PLAYER_PLAYING)
            player_drain(true, true);
    }
    if (player.state == PLAYER_IDLE)
        player_start();
    else if (player.state == PLAYER_DRAINING)
        player.resume_after_drain = true;
    result = player_ensure_worker();
    if (result < 0)
        player.state = PLAYER_IDLE;
    player_unlock();
    return result;
}

void athena_sound_stream_pause(AthenaSoundStream *stream) {
    if (!stream)
        return;
    player_lock();
    if (player.current == stream) {
        player_keep_heard_position();
        if (player.state != PLAYER_IDLE)
            player_drain(false, true);
    }
    player_unlock();
}

bool athena_sound_stream_is_playing(AthenaSoundStream *stream) {
    bool playing;

    player_lock();
    playing = player.current == stream && (player.state == PLAYER_PLAYING ||
        (player.state == PLAYER_DRAINING && player.resume_after_drain));
    player_unlock();
    return playing;
}

int athena_sound_stream_set_position(AthenaSoundStream *stream, uint32_t ms) {
    uint64_t frame;

    if (!stream)
        return ATHENA_SOUND_ERR_ARGS;
    frame = ((uint64_t)ms * (uint64_t)stream->fmt.freq) / 1000;
    if (frame > stream->total_frames)
        frame = stream->total_frames;

    player_lock();
    stream_seek(stream, (uint32_t)frame);
    stream->wrapped = false;
    if (player.current == stream) {
        /* Restart from the new position once the queued audio is flushed. */
        if (player.state == PLAYER_PLAYING)
            player_drain(true, true);
        else if (player.state == PLAYER_DRAINING && player.resume_after_drain)
            player.muted = true;
    }
    player_unlock();
    return ATHENA_SOUND_OK;
}

void athena_sound_stream_rewind(AthenaSoundStream *stream) {
    athena_sound_stream_set_position(stream, 0);
}

uint32_t athena_sound_stream_get_position(AthenaSoundStream *stream) {
    uint32_t frame;

    if (!stream)
        return 0;
    player_lock();
    if (player.current == stream && player.state == PLAYER_PLAYING && !player.format_pending)
        frame = player_live_frame(stream);
    else
        frame = stream->frame;
    player_unlock();
    return (uint32_t)(((uint64_t)frame * 1000) / (uint64_t)stream->fmt.freq);
}

uint32_t athena_sound_stream_get_length(const AthenaSoundStream *stream) {
    if (!stream)
        return 0;
    return (uint32_t)(((uint64_t)stream->total_frames * 1000) / (uint64_t)stream->fmt.freq);
}

void athena_sound_stream_set_loop(AthenaSoundStream *stream, bool loop) {
    if (!stream)
        return;
    player_lock();
    stream->loop = loop;
    player_unlock();
}

bool athena_sound_stream_get_loop(AthenaSoundStream *stream) {
    return stream && stream->loop;
}

AthenaSoundStreamType athena_sound_stream_get_type(const AthenaSoundStream *stream) {
    return stream ? stream->type : ATHENA_SOUND_STREAM_WAV;
}

int athena_sound_stream_get_rate(const AthenaSoundStream *stream) {
    return stream ? stream->fmt.freq : 0;
}

int athena_sound_stream_get_channels(const AthenaSoundStream *stream) {
    return stream ? stream->fmt.channels : 0;
}

int athena_sound_stream_get_bits(const AthenaSoundStream *stream) {
    return stream ? stream->fmt.bits : 0;
}

int athena_sound_set_volume(int volume) {
    if (volume < 0 || volume > ATHENA_SOUND_MAX_VOLUME)
        return ATHENA_SOUND_ERR_ARGS;
    player_lock();
    player.volume = volume;
    if (!player.muted)
        player_set_volume(volume);
    player_unlock();
    return ATHENA_SOUND_OK;
}

int athena_sound_get_volume(void) {
    return player.volume;
}
