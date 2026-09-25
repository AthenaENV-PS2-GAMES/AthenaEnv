#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <kernel.h>
#include <delaythread.h>
#include <audsrv.h>
#include <vorbis/vorbisfile.h>

#include <athena/debug.h>
#include <athena/iop_manager.h>
#include <athena/mutex.h>
#include <athena/sound.h>
#include <athena/thread.h>

#include "sound_internal.h"

/*
 * One stream plays at a time, through two threads:
 *
 * - the reader decodes (and converts) the current stream into a queue of
 *   blocks, up to ~0.5 s ahead. File I/O only ever blocks this thread;
 * - the feeder copies blocks into audsrv's ring on the IOP. That ring holds
 *   only ~100 ms, so the feeder never touches the file.
 *
 * Every write to audsrv is recorded as a segment (byte offset, file frame).
 * The byte being heard is `written - audsrv_queued()`, so the position is
 * the segment it falls in, and loop/end events fire when they are heard.
 * A seek, or a switch to a stream with the same format, drops the queued
 * blocks and appends the new audio after what audsrv already holds: no gap.
 *
 * The ring keeps whatever was last written and audsrv keeps reading it, so
 * whenever feeding stops the feeder first writes a ring of silence (the
 * "drain"), and when the reader falls behind it writes silence rather than
 * let old audio replay. A format change drains the ring, muted, before
 * audsrv_set_format(), which would otherwise replay half of it.
 *
 * audsrv_wait_audio() is not used: it holds audsrv's RPC lock while it waits,
 * which would block sound effects on the script thread. The feeder polls
 * audsrv_available() and sleeps for the time the missing bytes take to play.
 *
 * Files audsrv cannot play as they are (16 kHz, 8-bit stereo, 24-bit, float)
 * are converted by the reader to 16-bit at a rate audsrv supports, with
 * linear interpolation. Positions are always in frames of the file.
 */

/* Decoded audio queued between the reader and the feeder. */
#define BLOCK_BYTES 4096
#define BLOCK_COUNT 32
#define READ_AHEAD_MS 500
/* Writes to audsrv the position can still be mapped through. */
#define SEGMENT_COUNT 64
/* Source bytes decoded ahead by the converter. */
#define STREAM_SOURCE_CHUNK 2048
#define STREAM_STACK_SIZE (32 * 1024)
/* Above the script thread, which may never block, so audio does not starve. */
#define STREAM_PRIORITY (ATHENA_THREAD_DEFAULT_PRIORITY - 4)
/* Ring bytes left free (a whole frame, and audsrv's 4-byte alignment). */
#define STREAM_RING_RESERVE 4
#define STREAM_MIN_SLEEP_US 1000
#define STREAM_MAX_SLEEP_US 10000
#define READER_IDLE_US 3000
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
    /* Descriptor of `path`, valid while no IOP reset happened since `io_reset`. */
    int fd;
    char *path;
    uint32_t io_reset;
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
    bool loop;

    /* Decoder, used by the reader thread only (and before it starts). */
    /* Next frame the decoder produces. */
    uint32_t frame;
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

    /* Under player.lock. */
    /* Where playback continues from; the position when not playing. */
    uint32_t resume_frame;
    /* The reader must move the decoder to resume_frame first. */
    bool reseek;
    /* The reader queued the last block. */
    bool eof;
    /* The next block starts after a loop wrap. */
    bool wrap_next;
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

enum {
    /* The block starts right after a loop wrap. */
    BLOCK_WRAP = 1,
    /* The stream ends after this block (which may be empty). */
    BLOCK_END = 2,
};

typedef struct {
    char data[BLOCK_BYTES] __attribute__((aligned(64)));
    uint32_t len;
    /* Bytes already sent to audsrv. */
    uint32_t sent;
    /* File frame of data[0]. */
    uint32_t frame;
    uint8_t flags;
} Block;

typedef enum {
    /* Audio of `stream` from `frame` on. */
    SEGMENT_DATA,
    /* Silence while `stream` waits at `frame`. */
    SEGMENT_HOLD,
    /* `stream` ended here. */
    SEGMENT_END,
} SegmentKind;

typedef struct {
    int64_t offset;
    AthenaSoundStream *stream;
    uint32_t frame;
    uint8_t kind;
    /* DATA: the audio starts right after a loop wrap. */
    bool wrap;
    /* Its event (loop, end) was processed. */
    bool fired;
} Segment;

static struct {
    AthenaMutex *lock;
    AthenaThread *feeder;
    AthenaThread *reader;
    /* The threads loop; each clears its flag itself, under the lock. */
    bool feeder_running;
    bool reader_running;
    bool halting;
    AthenaSoundStream *current;
    PlayerState state;
    bool resume_after_drain;
    /* Draining after the current stream's last block: it plays until heard. */
    bool end_pending;
    /* Volume forced to 0 while a pause/switch drains. */
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
    uint32_t ring_bytes;
    uint32_t bytes_per_second;

    /* Reader -> feeder queue. */
    Block blocks[BLOCK_COUNT];
    uint32_t block_head;
    uint32_t block_count;
    uint32_t queued_bytes;
    /* Bumped to discard what the reader is decoding. */
    uint32_t read_gen;
    /* Stream the reader decodes outside the lock. */
    AthenaSoundStream *decoding;

    /* Bytes written to audsrv since audsrv_set_format(). */
    int64_t written;
    Segment segments[SEGMENT_COUNT];
    uint32_t segment_head;
    uint32_t segment_count;
    /* A seek or switch not heard yet: until `seek_offset`, the position is `seek_frame`. */
    bool seek_active;
    int64_t seek_offset;
    uint32_t seek_frame;
    /* File frame right after the audio written last. */
    uint32_t next_frame;

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

/* Feeder-only buffer for silence. */
static char silence_chunk[BLOCK_BYTES] __attribute__((aligned(64)));

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

/*
 * Files are read through raw descriptors. An IOP reset closes them all, and
 * the reloaded fileXio numbers new files from the start again, so a stale
 * descriptor could name another file: it is never used or closed again,
 * only replaced by reopening the path (see stream_reopen()).
 */
static bool stream_file_stale(const AthenaSoundStream *stream) {
    return stream->io_reset != iopman_reset_count();
}

/* read() until `bytes` or the end of the file; -1 on error. */
static long file_read(int fd, void *out, size_t bytes) {
    size_t done = 0;

    while (done < bytes) {
        long got = (long)read(fd, (char *)out + done, bytes - done);
        if (got < 0)
            return done > 0 ? (long)done : -1;
        if (got == 0)
            break;
        done += (size_t)got;
    }
    return (long)done;
}

/* libvorbisfile reads through the stream's descriptor too. */
static size_t ogg_read(void *out, size_t size, size_t count, void *source) {
    AthenaSoundStream *stream = source;
    long got;

    if (size == 0 || count == 0)
        return 0;
    got = file_read(stream->fd, out, size * count);
    return got > 0 ? (size_t)got / size : 0;
}

static int ogg_seek(void *source, ogg_int64_t offset, int whence) {
    AthenaSoundStream *stream = source;
    return lseek(stream->fd, (off_t)offset, whence) < 0 ? -1 : 0;
}

/* The stream closes its descriptor itself (never a stale one). */
static int ogg_close(void *source) {
    (void)source;
    return 0;
}

static long ogg_tell(void *source) {
    AthenaSoundStream *stream = source;
    return (long)lseek(stream->fd, 0, SEEK_CUR);
}

static const ov_callbacks ogg_callbacks = { ogg_read, ogg_seek, ogg_close, ogg_tell };

/* Walks the RIFF chunks: "fmt " may be longer than 16 bytes and other chunks
 * (LIST, fact, ...) may come before "data". */
static int stream_open_wav(AthenaSoundStream *stream) {
    int fd = stream->fd;
    uint8_t header[12];
    uint8_t chunk[8];
    /* Up to WAVE_FORMAT_EXTENSIBLE's sub-format GUID. */
    uint8_t format[40];
    uint32_t format_size = 0;
    long file_size;
    uint32_t data_size = 0;
    uint16_t tag;
    int status;

    if ((file_size = (long)lseek(fd, 0, SEEK_END)) < 0 || lseek(fd, 0, SEEK_SET) != 0)
        return ATHENA_SOUND_ERR_READ;
    if (file_read(fd, header, sizeof(header)) != (long)sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
        return ATHENA_SOUND_ERR_FORMAT;

    for (;;) {
        uint32_t size, pad;
        long here, next;

        if (file_read(fd, chunk, sizeof(chunk)) != (long)sizeof(chunk)) {
            sound_set_detail("no \"data\" chunk");
            return ATHENA_SOUND_ERR_FORMAT;
        }
        size = read_le32(chunk + 4);
        /* Chunks are padded to an even size. */
        pad = size & 1;
        here = (long)lseek(fd, 0, SEEK_CUR);
        if (here < 0)
            return ATHENA_SOUND_ERR_READ;
        if (memcmp(chunk, "data", 4) == 0) {
            stream->data_offset = here;
            data_size = size;
            /* Streamed writers leave 0 or 0xFFFFFFFF: the data runs to EOF. */
            if (data_size == 0 || data_size > (uint32_t)(file_size - stream->data_offset))
                data_size = (uint32_t)(file_size - stream->data_offset);
            break;
        }
        next = here + (long)size + (long)pad;
        if (memcmp(chunk, "fmt ", 4) == 0) {
            format_size = size < sizeof(format) ? size : sizeof(format);
            if (format_size < 16 || file_read(fd, format, format_size) != (long)format_size)
                return ATHENA_SOUND_ERR_FORMAT;
        }
        if (next > file_size || lseek(fd, next, SEEK_SET) != next) {
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
    status = stream_setup(stream);
    if (status < 0)
        return status;
    stream->total_frames = data_size / stream->frame_bytes;
    return lseek(fd, stream->data_offset, SEEK_SET) == stream->data_offset ?
        ATHENA_SOUND_OK : ATHENA_SOUND_ERR_READ;
}

static int stream_open_ogg(AthenaSoundStream *stream) {
    vorbis_info *info;
    ogg_int64_t total;
    int status;

    stream->ogg = calloc(1, sizeof(*stream->ogg));
    if (!stream->ogg)
        return ATHENA_SOUND_ERR_MEMORY;
    if (lseek(stream->fd, 0, SEEK_SET) != 0 ||
        ov_open_callbacks(stream, stream->ogg, NULL, 0, ogg_callbacks) < 0) {
        free(stream->ogg);
        stream->ogg = NULL;
        sound_set_detail("not an Ogg Vorbis stream");
        return ATHENA_SOUND_ERR_FORMAT;
    }
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

static void stream_close_file(AthenaSoundStream *stream) {
    if (stream->ogg)
        ov_clear(stream->ogg);
    if (stream->fd >= 0 && !stream_file_stale(stream))
        close(stream->fd);
    stream->fd = -1;
}

static void stream_close(AthenaSoundStream *stream) {
    stream_close_file(stream);
    free(stream->ogg);
    stream->ogg = NULL;
    free(stream->source);
    stream->source = NULL;
    free(stream->path);
    stream->path = NULL;
}

/*
 * After an IOP reset: opens the file again (the decoder is then at its
 * start). Reader thread, or before the stream ever played.
 */
static bool stream_reopen(AthenaSoundStream *stream) {
    stream_close_file(stream);
    stream->io_reset = iopman_reset_count();
    stream->fd = open(stream->path, O_RDONLY);
    if (stream->fd < 0)
        return false;
    if (stream->type == ATHENA_SOUND_STREAM_OGG &&
        ov_open_callbacks(stream, stream->ogg, NULL, 0, ogg_callbacks) < 0) {
        close(stream->fd);
        stream->fd = -1;
        return false;
    }
    stream->frame = 0;
    return true;
}

AthenaSoundStream *athena_sound_stream_open(const char *path, int *result) {
    AthenaSoundStream *stream;
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
    stream->fd = -1;
    stream->path = sound_absolute_path(path);
    if (!stream->path) {
        free(stream);
        status = ATHENA_SOUND_ERR_MEMORY;
        goto fail;
    }
    stream->io_reset = iopman_reset_count();
    stream->fd = open(stream->path, O_RDONLY);
    if (stream->fd < 0) {
        stream_close(stream);
        free(stream);
        status = ATHENA_SOUND_ERR_OPEN;
        goto fail;
    }
    if (file_read(stream->fd, magic, sizeof(magic)) != (long)sizeof(magic)) {
        status = ATHENA_SOUND_ERR_FORMAT;
    } else if (memcmp(magic, "OggS", 4) == 0) {
        status = stream_open_ogg(stream);
    } else if (memcmp(magic, "RIFF", 4) == 0) {
        status = stream_open_wav(stream);
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

/* --- Decoding (reader thread) ------------------------------------------- */

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
        lseek(stream->fd, stream->data_offset + (off_t)frame * (off_t)stream->frame_bytes,
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

/* Decodes up to `bytes` (a multiple of the frame size); *end at end of data. */
static uint32_t stream_decode(AthenaSoundStream *stream, char *out, uint32_t bytes, bool *end) {
    uint32_t filled = 0;

    *end = false;
    if (stream->type == ATHENA_SOUND_STREAM_WAV) {
        uint32_t left = (stream->total_frames - stream->frame) * stream->frame_bytes;
        long read_bytes;
        uint32_t got;

        if (bytes > left)
            bytes = left;
        /* An error ends the stream like the end of the file would. */
        read_bytes = file_read(stream->fd, out, bytes);
        got = read_bytes > 0 ? (uint32_t)read_bytes : 0;
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
 * With stop_at_wrap it returns at the wrap instead (setting *wrapped), so a
 * buffer never holds frames from both sides of it.
 */
static uint32_t stream_fill(AthenaSoundStream *stream, char *out, uint32_t bytes, bool *ended,
    bool stop_at_wrap, bool *wrapped) {
    uint32_t filled = 0;

    *ended = false;
    if (wrapped)
        *wrapped = false;
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
        if (wrapped)
            *wrapped = true;
        if (stop_at_wrap)
            break;
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
            stream->source_len = stream_fill(stream, (char *)stream->source, chunk, &ended, true, NULL);
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


/*
 * Fills `bytes` of 16-bit output, resampled. Returns early at a loop wrap,
 * so the next call starts right after it (*wrap_at_start); *first is the
 * file frame of the first output frame; *ended once the file ran out.
 */
static uint32_t stream_convert(AthenaSoundStream *stream, char *out, uint32_t bytes, bool *ended,
    bool *wrap_at_start, uint32_t *first) {
    int16_t *samples = (int16_t *)out;
    uint32_t frames = bytes / stream->out_frame_bytes;
    int channels = stream->out.channels;
    uint32_t done = 0;

    *ended = false;
    *wrap_at_start = false;
    *first = stream->frame;
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

    while (done < frames) {
        while (stream->phase >= FIXED_ONE) {
            if (stream->exhausted) {
                *ended = true;
                return done * stream->out_frame_bytes;
            }
            /* Promoting would cross the loop wrap: that starts the next buffer. */
            if (stream->next_frame < stream->cur_frame) {
                if (done > 0)
                    return done * stream->out_frame_bytes;
                *wrap_at_start = true;
            }
            memcpy(stream->cur, stream->next, sizeof(stream->cur));
            stream->cur_frame = stream->next_frame;
            if (!stream_next_frame(stream, stream->next, &stream->next_frame)) {
                stream->next_frame = stream->cur_frame;
                stream->exhausted = true;
            }
            stream->phase -= FIXED_ONE;
        }
        if (done == 0)
            *first = stream->cur_frame;
        for (int c = 0; c < channels; c++) {
            int32_t delta = stream->next[c] - stream->cur[c];
            samples[done * channels + c] =
                (int16_t)(stream->cur[c] + (int32_t)(((int64_t)delta * stream->phase) >> 16));
        }
        done++;
        stream->phase += stream->step;
    }
    return done * stream->out_frame_bytes;
}

/* --- Player (under player.lock unless noted) ----------------------------- */

static Block *player_block(uint32_t index) {
    return &player.blocks[(player.block_head + index) % BLOCK_COUNT];
}

/* Drops the queued blocks and whatever the reader is decoding. */
static void player_flush(void) {
    player.read_gen++;
    player.block_count = 0;
    player.queued_bytes = 0;
}

static uint32_t player_read_ahead(const AthenaSoundStream *stream) {
    uint32_t bytes = (uint32_t)(((uint64_t)stream->out.freq * stream->out_frame_bytes *
        READ_AHEAD_MS) / 1000);

    return bytes < BLOCK_BYTES ? BLOCK_BYTES : bytes;
}

/* Playback of `stream` continues from `frame`: the reader moves there first. */
static void stream_request_seek(AthenaSoundStream *stream, uint32_t frame) {
    stream->resume_frame = frame;
    stream->reseek = true;
    stream->eof = false;
    stream->wrap_next = false;
}

static Segment *player_segment(uint32_t index) {
    return &player.segments[(player.segment_head + index) % SEGMENT_COUNT];
}

static void player_clear_segments(void) {
    player.segment_head = 0;
    player.segment_count = 0;
}

/* A loop or end is counted when its audio is heard. */
static void player_fire(Segment *segment) {
    AthenaSoundStream *stream = segment->stream;

    if (segment->fired)
        return;
    segment->fired = true;
    if (!stream)
        return;
    if (segment->kind == SEGMENT_DATA && segment->wrap) {
        stream->loops++;
    } else if (segment->kind == SEGMENT_END) {
        stream->ends++;
        if (stream == player.current)
            player.end_pending = false;
        /* Not when play() already started it over during its tail. */
        if (!(stream == player.current && player.resume_after_drain))
            stream->ended = true;
    }
}

static void player_add_segment(AthenaSoundStream *stream, SegmentKind kind, uint32_t frame,
    bool wrap) {
    Segment *segment;

    if (player.segment_count == SEGMENT_COUNT) {
        player_fire(player_segment(0));
        player.segment_head = (player.segment_head + 1) % SEGMENT_COUNT;
        player.segment_count--;
    }
    segment = player_segment(player.segment_count++);
    segment->offset = player.written;
    segment->stream = stream;
    segment->frame = frame;
    segment->kind = (uint8_t)kind;
    segment->wrap = wrap;
    segment->fired = false;
}

/* Offset in the written audio that audsrv is playing now. */
static int64_t player_heard(int queued) {
    return player.written - (queued > 0 ? queued : 0);
}

/* Fires the events heard and forgets segments the position no longer needs. */
static void player_process_heard(int64_t heard) {
    for (uint32_t i = 0; i < player.segment_count; i++) {
        Segment *segment = player_segment(i);
        if (segment->offset > heard)
            break;
        player_fire(segment);
    }
    while (player.segment_count >= 2 && player_segment(1)->offset <= heard) {
        player.segment_head = (player.segment_head + 1) % SEGMENT_COUNT;
        player.segment_count--;
    }
    if (player.seek_active && heard >= player.seek_offset)
        player.seek_active = false;
}

/* File frame of `stream` at `heard`. */
static uint32_t player_frame_at(AthenaSoundStream *stream, int64_t heard) {
    const Segment *found = NULL;
    bool last = false;
    uint32_t frame;

    /* A seek or switch plays after what audsrv still holds: report its target. */
    if (player.seek_active && heard < player.seek_offset)
        return player.seek_frame;
    for (uint32_t i = 0; i < player.segment_count; i++) {
        const Segment *segment = player_segment(i);
        if (segment->offset > heard)
            break;
        found = segment;
        last = i + 1 == player.segment_count;
    }
    if (!found || found->stream != stream || found->kind == SEGMENT_END)
        return stream->resume_frame;
    if (found->kind == SEGMENT_HOLD)
        return found->frame;
    frame = found->frame + (uint32_t)(((uint64_t)((heard - found->offset) / player.frame_bytes) *
        stream->step) >> 16);
    /* audsrv ran past the audio written (late feeder): do not run ahead. */
    if (last && frame > player.next_frame)
        frame = player.next_frame;
    return frame < stream->total_frames ? frame : stream->total_frames;
}

/* Playback of the current stream will continue from the frame being heard. */
static void player_keep_heard_position(void) {
    AthenaSoundStream *stream = player.current;

    if (stream && player.state == PLAYER_PLAYING && !player.format_pending)
        stream_request_seek(stream, player_frame_at(stream, player_heard(audsrv_queued())));
}

static void player_set_volume(int volume) {
    if (player.applied_volume != volume && athena_sound_ready()) {
        audsrv_set_volume(volume);
        player.applied_volume = volume;
    }
}

static void player_drain(bool resume, bool mute) {
    /*
     * Nothing was fed since play(): the ring still holds the last drain's
     * silence, and after audsrv_stop_audio() the IOP consumes nothing until
     * audsrv is written to, so a drain now could never finish. Resuming
     * keeps the pending audsrv_set_format(), for the current stream.
     */
    if (player.state == PLAYER_PLAYING && player.format_pending) {
        if (!resume)
            player.state = PLAYER_IDLE;
        return;
    }
    if (player.state == PLAYER_PLAYING) {
        player.state = PLAYER_DRAINING;
        /* One ring of silence overwrites everything audsrv could replay. */
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

/* Pauses (or stops) the current stream now, at the position heard. */
static void player_halt_current(FadeAction action) {
    AthenaSoundStream *stream = player.current;

    player_keep_heard_position();
    if (player.state == PLAYER_PLAYING) {
        player_flush();
        player_drain(false, true);
    } else if (player.state == PLAYER_DRAINING) {
        player.resume_after_drain = false;
    }
    player_reset_fade();
    if (action == FADE_THEN_STOP && stream)
        stream_request_seek(stream, 0);
}

/* A fade-out that reached silence pauses or stops the stream. */
static void player_finish_fade(void) {
    if (player.fade_action != FADE_THEN_NOTHING && player.gain_done >= player.gain_frames &&
        player.current)
        player_halt_current(player.fade_action);
}

static void player_write_silence(uint32_t bytes) {
    memset(silence_chunk, player.bits == 8 ? 0x80 : 0, bytes);
    audsrv_play_audio(silence_chunk, (int)bytes);
    player.written += bytes;
}

/* Feeds up to `bytes` of the current stream. */
static void player_feed(uint32_t bytes, int queued) {
    AthenaSoundStream *stream = player.current;
    Block *block;
    uint32_t count;

    if (player.block_count == 0) {
        /* The reader is late: silence, rather than let audsrv replay old audio. */
        if ((uint32_t)(queued > 0 ? queued : 0) < player.ring_bytes / 4) {
            count = bytes < player.ring_bytes / 4 ? bytes : player.ring_bytes / 4;
            count -= count % player.frame_bytes;
            if (count > 0) {
                player_add_segment(stream, SEGMENT_HOLD, player.next_frame, false);
                player_write_silence(count);
            }
        }
        return;
    }

    block = player_block(0);
    count = block->len - block->sent;
    if (count > bytes)
        count = bytes;
    if (count > 0) {
        uint32_t frame = block->frame + (uint32_t)(((uint64_t)(block->sent / player.frame_bytes) *
            stream->step) >> 16);

        player_apply_gain(block->data + block->sent, count);
        player_add_segment(stream, SEGMENT_DATA, frame,
            block->sent == 0 && (block->flags & BLOCK_WRAP));
        audsrv_play_audio(block->data + block->sent, (int)count);
        player.written += count;
        block->sent += count;
        player.queued_bytes -= count;
        player.next_frame = block->frame + (uint32_t)(((uint64_t)(block->sent /
            player.frame_bytes) * stream->step) >> 16);
    }
    if (block->sent == block->len) {
        uint8_t flags = block->flags;

        player.block_head = (player.block_head + 1) % BLOCK_COUNT;
        player.block_count--;
        if (flags & BLOCK_END) {
            /* Plays (and counts as playing) until its last sample is heard. */
            player_add_segment(stream, SEGMENT_END, 0, false);
            player_reset_fade();
            player.end_pending = true;
            player_drain(false, false);
            return;
        }
    }
    player_finish_fade();
}

static void player_feed_drain(uint32_t bytes) {
    if (bytes > player.drain_left)
        bytes = player.drain_left;
    player_write_silence(bytes);
    player.drain_left -= bytes;
    if (player.drain_left > 0)
        return;
    if (player.resume_after_drain && player.current) {
        player_start();
    } else {
        player.state = PLAYER_IDLE;
        /* Everything written has been heard by now. */
        player_process_heard(player.written);
        player.end_pending = false;
    }
}

/* Feeds audsrv; never touches files. */
static void stream_feeder(void *arg) {
    AthenaThread *self;

    (void)arg;
    /* Set by player_ensure_threads(), which holds the lock until we start. */
    player_lock();
    self = player.feeder;
    player_unlock();

    for (;;) {
        struct audsrv_fmt_t format;
        bool set_format = false;
        bool exiting = false;
        int volume = -1;
        int available, queued;
        uint32_t threshold;
        uint32_t bytes;

        player_lock();
        if (player.halting || athena_thread_core_stop_requested() ||
            player.state == PLAYER_IDLE) {
            player.state = PLAYER_IDLE;
            player.feeder_running = false;
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
                player.bytes_per_second = (uint32_t)format.freq * stream->out_frame_bytes;
                /* audsrv: 10 feeds of 512 output samples, in input bytes. */
                player.ring_bytes = (uint32_t)((512 * format.freq) / 48000) *
                    stream->out_frame_bytes * 10;
                /* The ring restarts: events still pending were heard already. */
                player_process_heard(INT64_MAX);
                player_clear_segments();
                player.written = 0;
                player.seek_active = true;
                player.seek_offset = 0;
                player.seek_frame = stream->resume_frame;
                player.next_frame = stream->resume_frame;
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
        threshold = player.ring_bytes / 4 < BLOCK_BYTES ? player.ring_bytes / 4 : BLOCK_BYTES;
        threshold -= threshold % player.frame_bytes;
        if (threshold == 0)
            threshold = player.frame_bytes;
        if (available < 0 || (uint32_t)available < threshold) {
            uint32_t missing = threshold - (available > 0 ? (uint32_t)available : 0);
            uint32_t sleep_us = (uint32_t)(((uint64_t)missing * 1000000) / player.bytes_per_second);

            player_lock();
            player_process_heard(player_heard(audsrv_queued()));
            player_unlock();
            if (sleep_us < STREAM_MIN_SLEEP_US)
                sleep_us = STREAM_MIN_SLEEP_US;
            else if (sleep_us > STREAM_MAX_SLEEP_US)
                sleep_us = STREAM_MAX_SLEEP_US;
            DelayThread(sleep_us);
            continue;
        }
        bytes = (uint32_t)available < BLOCK_BYTES ? (uint32_t)available : BLOCK_BYTES;
        bytes -= bytes % player.frame_bytes;

        /* Held across the RPCs so the position can be read consistently. */
        player_lock();
        queued = audsrv_queued();
        if (player.state == PLAYER_PLAYING && player.current && !player.format_pending)
            player_feed(bytes, queued);
        else if (player.state == PLAYER_DRAINING)
            player_feed_drain(bytes);
        if (player.state != PLAYER_IDLE)
            player_process_heard(player_heard(audsrv_queued()));
        player_unlock();
    }

    athena_thread_core_worker_finished(self);
    ExitThread();
}

/* Whether the reader should decode another block of `stream`. */
static bool reader_wants(const AthenaSoundStream *stream) {
    if (!stream || stream->eof)
        return false;
    if (player.state != PLAYER_PLAYING &&
        !(player.state == PLAYER_DRAINING && player.resume_after_drain))
        return false;
    return player.block_count < BLOCK_COUNT && player.queued_bytes < player_read_ahead(stream);
}

/* Decodes the current stream into blocks; the only thread doing file I/O. */
static void stream_reader(void *arg) {
    AthenaThread *self;

    (void)arg;
    player_lock();
    self = player.reader;
    player_unlock();

    for (;;) {
        AthenaSoundStream *stream;
        Block *block;
        uint32_t gen, seek_to, len, first;
        bool seek, ended = false, wrap_at_start = false, wrapped_after = false;
        bool reopen_failed = false;

        player_lock();
        if (player.halting || athena_thread_core_stop_requested() ||
            player.state == PLAYER_IDLE) {
            player.reader_running = false;
            player_unlock();
            break;
        }
        stream = player.current;
        if (!reader_wants(stream)) {
            player_unlock();
            DelayThread(READER_IDLE_US);
            continue;
        }
        gen = player.read_gen;
        seek = stream->reseek;
        seek_to = stream->resume_frame;
        stream->reseek = false;
        /* The slot after the queue: the feeder never reads it, a flush leaves it free. */
        block = player_block(player.block_count);
        player.decoding = stream;
        player_unlock();

        if (stream_file_stale(stream)) {
            /* An IOP reset closed the file: reopen it, then go back to where playback resumes. */
            if (!stream_reopen(stream)) {
                dbgprintf("[Sound] cannot reopen %s after an IOP reset\n", stream->path);
                reopen_failed = true;
            }
            seek = true;
        }
        if (seek && !reopen_failed)
            stream_seek(stream, seek_to);
        len = BLOCK_BYTES - BLOCK_BYTES % stream->out_frame_bytes;
        if (reopen_failed) {
            /* It ends as if the file ended here. */
            len = 0;
            ended = true;
            first = seek_to;
        } else if (stream->convert) {
            len = stream_convert(stream, block->data, len, &ended, &wrap_at_start, &first);
        } else {
            first = stream->frame;
            len = stream_fill(stream, block->data, len, &ended, true, &wrapped_after);
        }

        player_lock();
        player.decoding = NULL;
        /* Otherwise a seek, switch or pause came meanwhile, and asked for a reseek. */
        if (gen == player.read_gen && player.current == stream) {
            if (len > 0 || ended) {
                block->len = len;
                block->sent = 0;
                block->frame = first;
                block->flags = (uint8_t)(((stream->wrap_next || wrap_at_start) ? BLOCK_WRAP : 0) |
                    (ended ? BLOCK_END : 0));
                stream->wrap_next = false;
                player.block_count++;
                player.queued_bytes += len;
            }
            if (wrapped_after)
                stream->wrap_next = true;
            if (ended) {
                /* Played again, it starts over. */
                stream->eof = true;
                stream->resume_frame = 0;
                stream->reseek = true;
            }
        }
        player_unlock();
    }

    athena_thread_core_worker_finished(self);
    ExitThread();
}

/* Joins a thread that has left its loop. Called under the lock. */
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

static int player_spawn(AthenaThread **slot, bool *running, const char *name,
    void (*func)(void *), int priority) {
    if (*running)
        return ATHENA_SOUND_OK;
    /* A previous instance has left its loop and is exiting. */
    player_join(*slot);
    *slot = athena_thread_core_create(name, func, NULL, STREAM_STACK_SIZE, priority);
    if (!*slot)
        return ATHENA_SOUND_ERR_THREAD;
    *running = true;
    if (athena_thread_core_start(*slot) < 0) {
        *running = false;
        athena_thread_core_destroy(*slot);
        *slot = NULL;
        return ATHENA_SOUND_ERR_THREAD;
    }
    return ATHENA_SOUND_OK;
}

/* Starts the feeder and the reader if they are not looping. Called under the lock. */
static int player_ensure_threads(void) {
    ee_thread_status_t status;
    int priority = STREAM_PRIORITY;
    int result;

    /* Both stay above the script thread, even at a raised priority; the feeder highest. */
    if (ReferThreadStatus(GetThreadId(), &status) >= 0 &&
        status.current_priority <= priority + 1)
        priority = status.current_priority > 2 ? status.current_priority - 2 : 1;
    result = player_spawn(&player.feeder, &player.feeder_running, "Sound stream",
        stream_feeder, priority);
    if (result == ATHENA_SOUND_OK)
        result = player_spawn(&player.reader, &player.reader_running, "Sound reader",
            stream_reader, priority + 1);
    return result;
}

void sound_stream_halt(void) {
    AthenaThread *feeder, *reader;

    if (!player.lock)
        return;
    player_lock();
    /* audsrv still runs here (IOP reset, shutdown): remember what was heard. */
    player_keep_heard_position();
    player.halting = true;
    feeder = player.feeder;
    reader = player.reader;
    player_unlock();

    player_join(feeder);
    player_join(reader);

    player_lock();
    player.feeder = NULL;
    player.reader = NULL;
    player.halting = false;
    player.feeder_running = false;
    player.reader_running = false;
    player.state = PLAYER_IDLE;
    player.muted = false;
    player.end_pending = false;
    player.applied_volume = -1;
    player_reset_fade();
    player_flush();
    player_clear_segments();
    player.seek_active = false;
    if (player.current)
        stream_request_seek(player.current, player.current->resume_frame);
    player_unlock();
}

void sound_stream_audsrv_started(void) {
    /* No thread runs here: audsrv was just (re)loaded. */
    player.applied_volume = -1;
}

/* --- Public API ---------------------------------------------------------- */

void athena_sound_stream_destroy(AthenaSoundStream *stream) {
    if (!stream)
        return;
    if (player.lock) {
        player_lock();
        if (player.current == stream) {
            if (player.state == PLAYER_PLAYING)
                player_drain(false, true);
            else if (player.state == PLAYER_DRAINING)
                player.resume_after_drain = false;
            player_flush();
            player_reset_fade();
            player.end_pending = false;
            player.current = NULL;
        }
        for (uint32_t i = 0; i < player.segment_count; i++) {
            if (player_segment(i)->stream == stream)
                player_segment(i)->stream = NULL;
        }
        /* The reader may be in the middle of a block of it. */
        while (player.decoding == stream) {
            player_unlock();
            DelayThread(1000);
            player_lock();
        }
        player_unlock();
    }
    stream_close(stream);
    free(stream);
}

static bool same_output(const AthenaSoundStream *a, const AthenaSoundStream *b) {
    return a->out.freq == b->out.freq && a->out.bits == b->out.bits &&
        a->out.channels == b->out.channels;
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
        AthenaSoundStream *old = player.current;
        bool feeding = player.state == PLAYER_PLAYING && !player.format_pending;

        player_keep_heard_position();
        player_flush();
        player.end_pending = false;
        player.current = stream;
        stream_request_seek(stream, stream->resume_frame);
        if (feeding && old && same_output(old, stream)) {
            /* Same format: the new stream follows what audsrv still holds. */
            player.seek_active = true;
            player.seek_offset = player.written;
            player.seek_frame = stream->resume_frame;
            player.next_frame = stream->resume_frame;
        } else if (feeding) {
            player_drain(true, true);
        }
        /* A new stream starts from silence when it fades in. */
        player_reset_fade();
        if (fade_ms)
            player.gain_from = player.gain_to = 0;
    } else if (player.state == PLAYER_IDLE ||
        (player.state == PLAYER_DRAINING && !player.resume_after_drain)) {
        /* Resuming after a pause, or after (or during the tail of) its end. */
        stream_request_seek(stream, stream->resume_frame);
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
    result = player_ensure_threads();
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
    if (player.current == stream && player.state == PLAYER_PLAYING && !player.format_pending &&
        fade_ms > 0) {
        player_fade(stream, 0, fade_ms, action);
    } else if (player.current == stream) {
        player_halt_current(action);
    } else if (action == FADE_THEN_STOP) {
        stream_request_seek(stream, 0);
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
        (player.state == PLAYER_DRAINING && (player.resume_after_drain || player.end_pending)));
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
    stream->ended = false;
    stream_request_seek(stream, (uint32_t)frame);
    if (player.current == stream && player.state != PLAYER_IDLE) {
        player_flush();
        if (player.state == PLAYER_PLAYING && !player.format_pending) {
            /* No gap: the new position follows what audsrv still holds. */
            player.seek_active = true;
            player.seek_offset = player.written;
            player.seek_frame = (uint32_t)frame;
            player.next_frame = (uint32_t)frame;
        } else if (player.state == PLAYER_DRAINING && player.end_pending) {
            /* Seeking during the tail of its end keeps it playing. */
            player.resume_after_drain = true;
        }
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
    if (player.current == stream && !player.format_pending &&
        (player.state == PLAYER_PLAYING || (player.state == PLAYER_DRAINING && player.end_pending)))
        frame = player_frame_at(stream, player_heard(audsrv_queued()));
    else
        frame = stream->resume_frame;
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
    if (stream->loop != loop) {
        stream->loop = loop;
        /* What is queued was decoded with the old setting: decode it again. */
        if (player.current == stream && player.state == PLAYER_PLAYING &&
            !player.format_pending) {
            uint32_t frame = player.next_frame;

            if (player.block_count > 0) {
                Block *block = player_block(0);
                frame = block->frame + (uint32_t)(((uint64_t)(block->sent /
                    player.frame_bytes) * stream->step) >> 16);
            }
            stream_request_seek(stream, frame);
            player_flush();
        }
    }
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
