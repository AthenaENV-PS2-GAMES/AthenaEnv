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
 *
 * Files audsrv cannot play as they are (16 kHz, 8-bit stereo, 24-bit, float)
 * are converted by the worker to 16-bit at a rate audsrv supports, with
 * linear interpolation. Positions are always in frames of the file.
 */

#define STREAM_CHUNK 4096
/* Source bytes decoded ahead by the converter. */
#define STREAM_SOURCE_CHUNK 2048
/* Bigger stdio buffer: fewer, larger reads through fileXio. */
#define STREAM_FILE_BUFFER (16 * 1024)
#define STREAM_STACK_SIZE (32 * 1024)
/* Above the script thread, which may never block, so audio does not starve. */
#define STREAM_PRIORITY (ATHENA_THREAD_DEFAULT_PRIORITY - 4)
/* Ring bytes left free (a whole frame, and audsrv's 4-byte alignment). */
#define STREAM_RING_RESERVE 4
#define STREAM_MIN_SLEEP_US 1000
#define STREAM_MAX_SLEEP_US 10000
#define STREAM_MIN_RATE 1000
#define STREAM_MAX_RATE 192000
/* 16.16 fixed point: gains and resampling steps. */
#define FIXED_ONE 0x10000u

typedef enum {
    SAMPLE_PCM,
    SAMPLE_FLOAT,
} SampleEncoding;

struct AthenaSoundStream {
    AthenaSoundStreamType type;
    FILE *file;
    OggVorbis_File *ogg;
    /* Format of the file. */
    struct audsrv_fmt_t fmt;
    SampleEncoding encoding;
    uint32_t frame_bytes;
    /* Format sent to audsrv: `fmt` itself unless `convert`. */
    struct audsrv_fmt_t out;
    uint32_t out_frame_bytes;
    bool convert;
    /* Source frames per output frame, 16.16. */
    uint32_t step;
    uint32_t total_frames;
    /* WAV: file offset of the first sample. */
    long data_offset;
    /* Next frame the decoder produces. */
    uint32_t frame;
    /* While playing: frame at the end of the audio queued so far. */
    uint32_t submitted;
    /* While playing: frame playback started from; the position never goes below. */
    uint32_t start_frame;
    /* While playing: playback wrapped around at least once. */
    bool wrapped;
    bool loop;

    /* Converter: output frames interpolate between `cur` and `next`. */
    uint8_t *source;
    uint32_t source_len;
    uint32_t source_pos;
    /* File frame of source[0]; a buffer never spans a loop wrap. */
    uint32_t source_first;
    int16_t cur[2];
    int16_t next[2];
    uint32_t cur_frame;
    uint32_t next_frame;
    uint32_t phase;
    bool primed;
    /* The file ended (no loop): `next` repeats `cur`. */
    bool exhausted;

    /* Events, counted by the worker. */
    uint32_t ends;
    uint32_t loops;
    bool ended;
};

typedef enum {
    PLAYER_IDLE,
    PLAYER_PLAYING,
    /* Queuing silence; then PLAYING again (resume) or IDLE. */
    PLAYER_DRAINING,
} PlayerState;

/* What happens when a fade-out reaches silence. */
typedef enum {
    FADE_THEN_NOTHING,
    FADE_THEN_PAUSE,
    FADE_THEN_STOP,
} FadeAction;

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
    int bits;
    int channels;
    uint32_t step;
    uint32_t ring_bytes;
    uint32_t bytes_per_second;
    /*
     * Fade applied to the samples (audsrv's own volume has 26 steps): gain
     * goes from gain_from to gain_to (16.16) over gain_frames output frames.
     */
    uint32_t gain_from;
    uint32_t gain_to;
    uint32_t gain_frames;
    uint32_t gain_done;
    FadeAction fade_action;
} player = {
    .volume = ATHENA_SOUND_MAX_VOLUME,
    .applied_volume = -1,
    .gain_from = FIXED_ONE,
    .gain_to = FIXED_ONE,
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

static const int output_rates[] = { 11025, 12000, 22050, 24000, 32000, 44100, 48000 };

static bool format_supported(const struct audsrv_fmt_t *fmt) {
    for (size_t i = 0; i < sizeof(supported_formats) / sizeof(supported_formats[0]); i++) {
        if (supported_formats[i].freq == fmt->freq &&
            supported_formats[i].bits == fmt->bits &&
            supported_formats[i].channels == fmt->channels)
            return true;
    }
    return false;
}

/* 16-bit rate for converted audio: an exact multiple if any, else the next above. */
static int output_rate(int rate, int channels) {
    struct audsrv_fmt_t fmt = { .bits = 16, .channels = channels };
    int above = 0;

    for (size_t i = 0; i < sizeof(output_rates) / sizeof(output_rates[0]); i++) {
        fmt.freq = output_rates[i];
        if (!format_supported(&fmt))
            continue;
        if (fmt.freq % rate == 0)
            return fmt.freq;
        if (!above && fmt.freq >= rate)
            above = fmt.freq;
    }
    return above ? above : 48000;
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

/* --- Opening ------------------------------------------------------------ */

static uint32_t read_le32(const uint8_t *bytes) {
    return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

/* Checks the file format and picks what audsrv receives. */
static int stream_setup(AthenaSoundStream *stream) {
    if (stream->fmt.channels < 1 || stream->fmt.channels > 2) {
        sound_set_detail("%d channels; only mono and stereo are supported", stream->fmt.channels);
        return ATHENA_SOUND_ERR_FORMAT;
    }
    if (stream->fmt.freq < STREAM_MIN_RATE || stream->fmt.freq > STREAM_MAX_RATE) {
        sound_set_detail("%d Hz is outside %d..%d Hz", stream->fmt.freq, STREAM_MIN_RATE,
            STREAM_MAX_RATE);
        return ATHENA_SOUND_ERR_FORMAT;
    }
    stream->frame_bytes = (uint32_t)(stream->fmt.channels * (stream->fmt.bits / 8));

    if (stream->encoding == SAMPLE_PCM && (stream->fmt.bits == 8 || stream->fmt.bits == 16) &&
        format_supported(&stream->fmt)) {
        stream->out = stream->fmt;
        stream->convert = false;
        stream->step = FIXED_ONE;
    } else {
        stream->out.bits = 16;
        stream->out.channels = stream->fmt.channels;
        stream->out.freq = output_rate(stream->fmt.freq, stream->fmt.channels);
        stream->convert = true;
        stream->step = (uint32_t)(((uint64_t)stream->fmt.freq << 16) / (uint64_t)stream->out.freq);
        stream->source = malloc(STREAM_SOURCE_CHUNK);
        if (!stream->source)
            return ATHENA_SOUND_ERR_MEMORY;
    }
    stream->out_frame_bytes = (uint32_t)(stream->out.channels * (stream->out.bits / 8));
    return ATHENA_SOUND_OK;
}

/* Walks the RIFF chunks: "fmt " may be longer than 16 bytes and other chunks
 * (LIST, fact, ...) may come before "data". */
static int stream_open_wav(AthenaSoundStream *stream, FILE *file) {
    uint8_t header[12];
    uint8_t chunk[8];
    /* Up to WAVE_FORMAT_EXTENSIBLE's sub-format GUID. */
    uint8_t format[40];
    uint32_t format_size = 0;
    long file_size;
    uint32_t data_size = 0;
    uint16_t tag;
    int status;

    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0)
        return ATHENA_SOUND_ERR_READ;
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
        return ATHENA_SOUND_ERR_FORMAT;

    for (;;) {
        uint32_t size, pad;
        long next;

        if (fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk)) {
            sound_set_detail("no \"data\" chunk");
            return ATHENA_SOUND_ERR_FORMAT;
        }
        size = read_le32(chunk + 4);
        /* Chunks are padded to an even size. */
        pad = size & 1;
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
            format_size = size < sizeof(format) ? size : sizeof(format);
            if (format_size < 16 || fread(format, 1, format_size, file) != format_size)
                return ATHENA_SOUND_ERR_FORMAT;
            size -= format_size;
        }
        next = ftell(file) + (long)size + (long)pad;
        if (next < 0 || next > file_size || fseek(file, next, SEEK_SET) != 0) {
            sound_set_detail("no \"data\" chunk");
            return ATHENA_SOUND_ERR_FORMAT;
        }
    }
    if (format_size == 0) {
        sound_set_detail("no \"fmt \" chunk");
        return ATHENA_SOUND_ERR_FORMAT;
    }

    tag = read_le16(format);
    /* WAVE_FORMAT_EXTENSIBLE: the real tag opens the sub-format GUID. */
    if (tag == 0xFFFE && format_size >= 26)
        tag = read_le16(format + 24);
    stream->fmt.channels = read_le16(format + 2);
    stream->fmt.freq = (int)read_le32(format + 4);
    stream->fmt.bits = read_le16(format + 14);
    if (tag == 1 && (stream->fmt.bits == 8 || stream->fmt.bits == 16 ||
            stream->fmt.bits == 24 || stream->fmt.bits == 32)) {
        stream->encoding = SAMPLE_PCM;
    } else if (tag == 3 && stream->fmt.bits == 32) {
        stream->encoding = SAMPLE_FLOAT;
    } else {
        sound_set_detail("WAV encoding 0x%x with %d-bit samples; PCM (8/16/24/32-bit) "
            "and 32-bit float are supported", tag, stream->fmt.bits);
        return ATHENA_SOUND_ERR_FORMAT;
    }

    stream->type = ATHENA_SOUND_STREAM_WAV;
    stream->file = file;
    status = stream_setup(stream);
    if (status < 0)
        return status;
    stream->total_frames = data_size / stream->frame_bytes;
    return fseek(file, stream->data_offset, SEEK_SET) == 0 ?
        ATHENA_SOUND_OK : ATHENA_SOUND_ERR_READ;
}

static int stream_open_ogg(AthenaSoundStream *stream, FILE *file) {
    vorbis_info *info;
    ogg_int64_t total;
    int status;

    stream->ogg = calloc(1, sizeof(*stream->ogg));
    if (!stream->ogg)
        return ATHENA_SOUND_ERR_MEMORY;
    if (fseek(file, 0, SEEK_SET) != 0 ||
        ov_open_callbacks(file, stream->ogg, NULL, 0, OV_CALLBACKS_DEFAULT) < 0) {
        free(stream->ogg);
        stream->ogg = NULL;
        sound_set_detail("not an Ogg Vorbis stream");
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
    stream->encoding = SAMPLE_PCM;
    status = stream_setup(stream);
    if (status < 0)
        return status;
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
    free(stream->source);
    stream->source = NULL;
}

AthenaSoundStream *athena_sound_stream_open(const char *path, int *result) {
    AthenaSoundStream *stream;
    FILE *file;
    uint8_t magic[4];
    int status;

    sound_set_detail(NULL);
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
    setvbuf(file, NULL, _IOFBF, STREAM_FILE_BUFFER);
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
    if (status == ATHENA_SOUND_OK && stream->total_frames == 0) {
        sound_set_detail("no audio data");
        status = ATHENA_SOUND_ERR_FORMAT;
    }
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

/* --- Decoding ----------------------------------------------------------- */

/* Moves the decoder only; the converter keeps its state (loop wrap). */
static void stream_seek_decoder(AthenaSoundStream *stream, uint32_t frame) {
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

/* Seeks and forgets whatever the converter had buffered. */
static void stream_seek(AthenaSoundStream *stream, uint32_t frame) {
    stream_seek_decoder(stream, frame);
    stream->source_len = 0;
    stream->source_pos = 0;
    stream->primed = false;
    stream->exhausted = false;
    stream->phase = 0;
}

/* Frame being fed next, in frames of the file. */
static uint32_t stream_feed_frame(const AthenaSoundStream *stream) {
    return stream->convert && stream->primed ? stream->cur_frame : stream->frame;
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

/*
 * Fills `bytes` of file data, wrapping to the start of looping streams.
 * With stop_at_wrap it returns at the wrap instead, so a converter buffer
 * never holds frames from both sides of it.
 */
static uint32_t stream_fill(AthenaSoundStream *stream, char *out, uint32_t bytes, bool *ended,
    bool stop_at_wrap) {
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
        stream_seek_decoder(stream, 0);
        stream->loops++;
        if (stop_at_wrap)
            break;
        stream->wrapped = true;
        stream->start_frame = 0;
    }
    return filled;
}

static int16_t stream_sample(const AthenaSoundStream *stream, const uint8_t *bytes) {
    switch (stream->fmt.bits) {
    case 8:
        return (int16_t)((bytes[0] - 128) * 256);
    case 16:
        return (int16_t)(bytes[0] | (bytes[1] << 8));
    case 24:
        return (int16_t)(bytes[1] | (bytes[2] << 8));
    default:
        if (stream->encoding == SAMPLE_FLOAT) {
            float value;

            memcpy(&value, bytes, sizeof(value));
            /* NaN fails both comparisons and becomes silence. */
            if (value >= 1.0f)
                return 32767;
            if (value <= -1.0f)
                return -32768;
            if (!(value > -1.0f))
                return 0;
            return (int16_t)(value * 32767.0f);
        }
        return (int16_t)(bytes[2] | (bytes[3] << 8));
    }
}

/* Next file frame as 16-bit samples; false once a file without loop ended. */
static bool stream_next_frame(AthenaSoundStream *stream, int16_t frame[2], uint32_t *index) {
    const uint8_t *bytes;

    if (stream->source_pos >= stream->source_len) {
        uint32_t chunk = STREAM_SOURCE_CHUNK - STREAM_SOURCE_CHUNK % stream->frame_bytes;

        stream->source_pos = 0;
        stream->source_len = 0;
        /* A wrap exactly at the chunk start returns nothing: try once more. */
        for (int attempt = 0; attempt < 2 && stream->source_len == 0; attempt++) {
            bool ended;

            stream->source_first = stream->frame;
            stream->source_len = stream_fill(stream, (char *)stream->source, chunk, &ended, true);
            if (ended && stream->source_len == 0)
                return false;
        }
        if (stream->source_len == 0)
            return false;
    }
    bytes = stream->source + stream->source_pos;
    *index = stream->source_first + stream->source_pos / stream->frame_bytes;
    frame[0] = stream_sample(stream, bytes);
    frame[1] = stream->fmt.channels > 1 ?
        stream_sample(stream, bytes + stream->frame_bytes / 2) : frame[0];
    stream->source_pos += stream->frame_bytes;
    return true;
}

/* Fills `bytes` of 16-bit output, resampled; *ended once the file ran out. */
static uint32_t stream_convert(AthenaSoundStream *stream, char *out, uint32_t bytes, bool *ended) {
    int16_t *samples = (int16_t *)out;
    uint32_t frames = bytes / stream->out_frame_bytes;
    int channels = stream->out.channels;

    *ended = false;
    if (!stream->primed) {
        if (!stream_next_frame(stream, stream->cur, &stream->cur_frame)) {
            *ended = true;
            return 0;
        }
        if (!stream_next_frame(stream, stream->next, &stream->next_frame)) {
            memcpy(stream->next, stream->cur, sizeof(stream->next));
            stream->next_frame = stream->cur_frame;
            stream->exhausted = true;
        }
        stream->phase = 0;
        stream->primed = true;
    }

    for (uint32_t done = 0; done < frames; done++) {
        for (int c = 0; c < channels; c++) {
            int32_t delta = stream->next[c] - stream->cur[c];
            samples[done * channels + c] =
                (int16_t)(stream->cur[c] + (int32_t)(((int64_t)delta * stream->phase) >> 16));
        }
        stream->phase += stream->step;
        while (stream->phase >= FIXED_ONE) {
            stream->phase -= FIXED_ONE;
            if (stream->exhausted) {
                *ended = true;
                return (done + 1) * stream->out_frame_bytes;
            }
            if (stream->next_frame < stream->cur_frame) {
                stream->wrapped = true;
                stream->start_frame = 0;
            }
            memcpy(stream->cur, stream->next, sizeof(stream->cur));
            stream->cur_frame = stream->next_frame;
            if (!stream_next_frame(stream, stream->next, &stream->next_frame)) {
                stream->next_frame = stream->cur_frame;
                stream->exhausted = true;
            }
        }
    }
    return frames * stream->out_frame_bytes;
}

/* --- Player (all under player.lock) ------------------------------------- */

static uint32_t player_live_frame(AthenaSoundStream *stream) {
    int queued = audsrv_queued();
    uint32_t queued_out = queued > 0 ? (uint32_t)queued / player.frame_bytes : 0;
    uint32_t queued_frames = (uint32_t)(((uint64_t)queued_out * player.step) >> 16);
    uint32_t submitted = stream->submitted;
    uint32_t frame;

    if (submitted >= queued_frames)
        frame = submitted - queued_frames;
    else if (stream->wrapped)
        frame = stream->total_frames - (queued_frames - submitted) % stream->total_frames;
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
        /* One ring of silence overwrites everything audsrv_set_format() could replay. */
        player.drain_left = player.ring_bytes;
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

static uint32_t player_gain_now(void) {
    if (player.gain_done >= player.gain_frames)
        return player.gain_to;
    return (uint32_t)((int64_t)player.gain_from +
        ((int64_t)player.gain_to - (int64_t)player.gain_from) *
        (int64_t)player.gain_done / (int64_t)player.gain_frames);
}

/* Ramps the gain to `target` over `ms` of `stream`'s output; 0 ms is at once. */
static void player_fade(const AthenaSoundStream *stream, uint32_t target, uint32_t ms,
    FadeAction action) {
    player.gain_from = ms ? player_gain_now() : target;
    player.gain_to = target;
    player.gain_frames = (uint32_t)(((uint64_t)ms * (uint64_t)stream->out.freq) / 1000);
    player.gain_done = 0;
    player.fade_action = action;
}

static void player_reset_fade(void) {
    player.gain_from = player.gain_to = FIXED_ONE;
    player.gain_frames = player.gain_done = 0;
    player.fade_action = FADE_THEN_NOTHING;
}

/* Applies the fade to audio about to be queued. */
static void player_apply_gain(char *buffer, uint32_t bytes) {
    uint32_t frames = bytes / player.frame_bytes;

    if (player.gain_done >= player.gain_frames && player.gain_to == FIXED_ONE)
        return;
    for (uint32_t f = 0; f < frames; f++) {
        uint32_t gain = player_gain_now();

        if (player.bits == 16) {
            int16_t *samples = (int16_t *)buffer + f * player.channels;
            for (int c = 0; c < player.channels; c++)
                samples[c] = (int16_t)(((int32_t)samples[c] * (int32_t)gain) >> 16);
        } else {
            uint8_t *samples = (uint8_t *)buffer + f * player.channels;
            for (int c = 0; c < player.channels; c++)
                samples[c] = (uint8_t)((((samples[c] - 128) * (int32_t)gain) >> 16) + 128);
        }
        if (player.gain_done < player.gain_frames)
            player.gain_done++;
    }
}

/* A fade-out that reached silence pauses or stops the stream. */
static void player_finish_fade(void) {
    AthenaSoundStream *stream = player.current;
    FadeAction action = player.fade_action;

    if (action == FADE_THEN_NOTHING || player.gain_done < player.gain_frames || !stream)
        return;
    player_keep_heard_position();
    player_drain(false, true);
    if (action == FADE_THEN_STOP)
        stream_seek(stream, 0);
    player_reset_fade();
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

                format = stream->out;
                set_format = true;
                player.format_pending = false;
                player.frame_bytes = stream->out_frame_bytes;
                player.bits = format.bits;
                player.channels = format.channels;
                player.step = stream->step;
                player.bytes_per_second = (uint32_t)format.freq * stream->out_frame_bytes;
                /* audsrv: 10 feeds of 512 output samples, in input bytes. */
                player.ring_bytes = (uint32_t)((512 * format.freq) / 48000) *
                    stream->out_frame_bytes * 10;
                stream->submitted = stream_feed_frame(stream);
                stream->start_frame = stream->submitted;
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
        /*
         * Never fill the ring completely: with the write position on the read
         * position audsrv reports 0 queued (and 0 available), so the heard
         * position would jump ahead by a whole ring and then back.
         */
        if (available > STREAM_RING_RESERVE)
            available -= STREAM_RING_RESERVE;
        else if (available > 0)
            available = 0;
        /*
         * After audsrv_set_format() the IOP reports (ring / 2) & ~3 free
         * bytes and consumes nothing until the first audsrv_play_audio(), so
         * waiting for ring / 2 could deadlock (22050 Hz mono: 2348 < 2350).
         */
        threshold = player.ring_bytes / 4 < STREAM_CHUNK ? player.ring_bytes / 4 : STREAM_CHUNK;
        threshold -= threshold % player.frame_bytes;
        if (threshold == 0)
            threshold = player.frame_bytes;
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
            uint32_t got = stream->convert ?
                stream_convert(stream, stream_chunk, bytes, &ended) :
                stream_fill(stream, stream_chunk, bytes, &ended, false);

            if (got > 0) {
                player_apply_gain(stream_chunk, got);
                audsrv_play_audio(stream_chunk, (int)got);
            }
            stream->submitted = stream_feed_frame(stream);
            if (ended) {
                /* Let the tail play out; the next play() starts over. */
                stream->ends++;
                stream->ended = true;
                stream_seek(stream, 0);
                player_reset_fade();
                player_drain(false, false);
            } else {
                player_finish_fade();
            }
        } else if (player.state == PLAYER_DRAINING) {
            if (bytes > player.drain_left)
                bytes = player.drain_left;
            memset(stream_chunk, player.bits == 8 ? 0x80 : 0, bytes);
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
    player_reset_fade();
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
            player_reset_fade();
            player.current = NULL;
        }
        player_unlock();
    }
    stream_close(stream);
    free(stream);
}

int athena_sound_stream_play(AthenaSoundStream *stream, uint32_t fade_ms) {
    int result;

    if (!stream || fade_ms > ATHENA_SOUND_MAX_FADE_MS)
        return ATHENA_SOUND_ERR_ARGS;
    result = athena_sound_ensure();
    if (result < 0)
        return result;

    player_lock();
    stream->ended = false;
    if (player.current != stream) {
        player_keep_heard_position();
        player.current = stream;
        if (player.state == PLAYER_PLAYING)
            player_drain(true, true);
        /* A new stream starts from silence when it fades in. */
        player_reset_fade();
        if (fade_ms)
            player.gain_from = player.gain_to = 0;
    } else if (player.state == PLAYER_IDLE ||
        (player.state == PLAYER_DRAINING && !player.resume_after_drain)) {
        /* Resuming from a pause. */
        player_reset_fade();
        if (fade_ms)
            player.gain_from = player.gain_to = 0;
    }
    /* Also cancels a pending fade-out, rising from the current gain. */
    player_fade(stream, FIXED_ONE, fade_ms, FADE_THEN_NOTHING);
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

/* Pause or stop, now or after a fade-out. */
static void stream_halt(AthenaSoundStream *stream, uint32_t fade_ms, FadeAction action) {
    if (!stream)
        return;
    player_lock();
    if (player.current == stream && player.state == PLAYER_PLAYING && fade_ms > 0) {
        player_fade(stream, 0, fade_ms, action);
        player_unlock();
        return;
    }
    if (player.current == stream) {
        player_keep_heard_position();
        if (player.state != PLAYER_IDLE)
            player_drain(false, true);
        player_reset_fade();
    }
    if (action == FADE_THEN_STOP) {
        stream_seek(stream, 0);
        stream->wrapped = false;
    }
    player_unlock();
}

void athena_sound_stream_pause(AthenaSoundStream *stream, uint32_t fade_ms) {
    stream_halt(stream, fade_ms, FADE_THEN_PAUSE);
}

void athena_sound_stream_stop(AthenaSoundStream *stream, uint32_t fade_ms) {
    stream_halt(stream, fade_ms, FADE_THEN_STOP);
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
    stream->ended = false;
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
        frame = stream_feed_frame(stream);
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

void athena_sound_stream_get_events(AthenaSoundStream *stream, uint32_t *ends, uint32_t *loops) {
    if (!stream)
        return;
    player_lock();
    if (ends)
        *ends = stream->ends;
    if (loops)
        *loops = stream->loops;
    player_unlock();
}

bool athena_sound_stream_ended(AthenaSoundStream *stream) {
    bool ended;

    if (!stream)
        return false;
    player_lock();
    ended = stream->ended;
    player_unlock();
    return ended;
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

bool athena_sound_stream_is_converted(const AthenaSoundStream *stream) {
    return stream && stream->convert;
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
