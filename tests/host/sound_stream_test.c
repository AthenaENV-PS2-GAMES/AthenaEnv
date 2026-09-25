/*
 * Host test of src/modules/sound/native/sound_stream.c: the real reader and
 * feeder threads (as pthreads) against a simulated audsrv IOP ring that
 * follows iop/sound/audsrv/src/audsrv.c: audsrv_set_format() puts the read
 * head half a ring ahead, available/queued report 0 when the heads meet,
 * and the play thread consumes one feed per 512 output samples, in real
 * time, whether or not new audio was written.
 *
 * Run with tests/host/run.sh from the repository root (fixtures are read
 * from bin/tests/sound/). Takes ~20 s: playback runs in real time.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

/* File reads go through this hook, so a test can stall the reader thread. */
static volatile int stall_next_read_ms;
static ssize_t hooked_read(int fd, void *buffer, size_t bytes) {
    int ms = __atomic_exchange_n(&stall_next_read_ms, 0, __ATOMIC_SEQ_CST);
    if (ms > 0)
        usleep((useconds_t)ms * 1000);
    return read(fd, buffer, bytes);
}
#define read hooked_read
#include "sound_stream.c"
#undef read
#include "sound_path.c"
#include "host_runtime.h"

/* --- Stubs: the rest of the Sound module ------------------------------- */
/* IOP.reset(): the tests bump it after closing the descriptors themselves. */
static uint32_t host_reset_count;
uint32_t iopman_reset_count(void) { return host_reset_count; }
static char detail[160];
void sound_set_detail(const char *fmt, ...) {
    va_list args;
    if (!fmt) { detail[0] = 0; return; }
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
}
int athena_sound_ensure(void) { return 0; }
bool athena_sound_ready(void) { return true; }
uint32_t sound_iop_generation(void) { return 1; }

/* --- Stub: the IOP side of audsrv ---------------------------------------- */
static pthread_mutex_t iop = PTHREAD_MUTEX_INITIALIZER;
static int ringbuf_size = 1, readpos, writepos, iop_playing, feed_size, iop_bits = 16;
static double feed_period_ms = 512.0 / 48.0;
/* Peak (0..32768) of every write, with its time. */
static struct { double t; int peak; int bytes; } writes[40000];
static int write_count;
static volatile int iop_running = 1;

static int available_locked(void) {
    return writepos <= readpos ? readpos - writepos : ringbuf_size - (writepos - readpos);
}
int audsrv_set_format(struct audsrv_fmt_t *fmt) {
    int shift = (fmt->bits == 16) + (fmt->channels == 2);
    pthread_mutex_lock(&iop);
    iop_bits = fmt->bits;
    feed_size = ((512 * fmt->freq) / 48000) << shift;
    ringbuf_size = feed_size * 10;
    writepos = 0;
    readpos = (feed_size * 5) & ~3;
    pthread_mutex_unlock(&iop);
    return 0;
}
int audsrv_available(void) {
    pthread_mutex_lock(&iop);
    int a = available_locked();
    pthread_mutex_unlock(&iop);
    return a;
}
int audsrv_queued(void) {
    pthread_mutex_lock(&iop);
    int q = writepos < readpos ? ringbuf_size - (readpos - writepos) : writepos - readpos;
    pthread_mutex_unlock(&iop);
    return q;
}
int audsrv_play_audio(const char *buf, int len) {
    int peak = 0;
    pthread_mutex_lock(&iop);
    iop_playing = 1;
    if (len > available_locked())
        len = available_locked();
    for (int i = 0; i < len;) {
        int v;
        if (iop_bits == 16) {
            v = abs((int16_t)((uint8_t)buf[i] | ((uint8_t)buf[i + 1] << 8)));
            i += 2;
        } else {
            v = abs(((uint8_t)buf[i] - 128) * 256);
            i += 1;
        }
        if (v > peak) peak = v;
    }
    writepos = (writepos + len) % ringbuf_size;
    if (write_count < (int)(sizeof(writes) / sizeof(writes[0]))) {
        writes[write_count].t = now_ms();
        writes[write_count].peak = peak;
        writes[write_count].bytes = len;
        write_count++;
    }
    pthread_mutex_unlock(&iop);
    return len;
}
int audsrv_stop_audio(void) { pthread_mutex_lock(&iop); iop_playing = 0; pthread_mutex_unlock(&iop); return 0; }
int audsrv_set_volume(int v) { (void)v; return 0; }

/* The IOP play thread: one feed per 512 output samples, even past writepos. */
static void *iop_thread(void *p) {
    double next = now_ms();
    (void)p;
    while (iop_running) {
        next += feed_period_ms;
        double wait = next - now_ms();
        if (wait > 0) usleep((useconds_t)(wait * 1000));
        pthread_mutex_lock(&iop);
        if (iop_playing && feed_size) {
            readpos += feed_size;
            if (readpos >= ringbuf_size) readpos = 0;
        }
        pthread_mutex_unlock(&iop);
    }
    return NULL;
}

/* --- Helpers ------------------------------------------------------------- */
#define FIXTURES "bin/tests/sound/"

static AthenaSoundStream *open_path(const char *path) {
    int result;
    AthenaSoundStream *s = athena_sound_stream_open(path, &result);
    if (!s) printf("  open %s failed: %d (%s)\n", path, result, detail);
    return s;
}
static AthenaSoundStream *open_fixture(const char *name) {
    char path[256];
    snprintf(path, sizeof(path), FIXTURES "%s", name);
    return open_path(path);
}

/* Source frames of a WAV fixture as 16-bit, like stream_sample(). */
static int16_t *source_frames(AthenaSoundStream *s, const char *name, uint32_t *count) {
    char path[256];
    snprintf(path, sizeof(path), FIXTURES "%s", name);
    FILE *f = fopen(path, "rb");
    uint32_t n = s->total_frames, channels = s->fmt.channels;
    uint8_t *raw = malloc(n * s->frame_bytes);
    int16_t *out = malloc(n * 2 * sizeof(int16_t));
    fseek(f, s->data_offset, SEEK_SET);
    if (fread(raw, 1, n * s->frame_bytes, f) != n * s->frame_bytes) printf("  short read of %s\n", name);
    fclose(f);
    for (uint32_t i = 0; i < n; i++) {
        out[i * 2] = stream_sample(s, raw + i * s->frame_bytes);
        out[i * 2 + 1] = channels > 1 ?
            stream_sample(s, raw + i * s->frame_bytes + s->frame_bytes / 2) : out[i * 2];
    }
    free(raw);
    *count = n;
    return out;
}

/* Runs the converter in uneven chunks, like successive blocks. */
static uint32_t convert_all(AthenaSoundStream *s, int16_t *out, uint32_t max_frames, bool *ended,
    int *wraps, uint32_t *first_of_first) {
    static const uint32_t sizes[] = { 1, 7, 64, 333, 1024 };
    uint32_t frames = 0;
    int k = 0;
    *ended = false;
    if (wraps) *wraps = 0;
    while (frames < max_frames && !*ended) {
        uint32_t want = sizes[k++ % 5], first;
        bool wrap;
        if (want > max_frames - frames) want = max_frames - frames;
        uint32_t got = stream_convert(s, (char *)(out + frames * s->out.channels),
            want * s->out_frame_bytes, ended, &wrap, &first);
        if (frames == 0 && first_of_first) *first_of_first = first;
        if (wrap && wraps) (*wraps)++;
        frames += got / s->out_frame_bytes;
    }
    return frames;
}

/* 44.1 kHz stereo square wave of constant amplitude: write peaks show the gain. */
static void write_square(const char *path, int rate, int ms) {
    uint32_t frames = (uint32_t)rate * ms / 1000, bytes = frames * 4, v;
    FILE *f = fopen(path, "wb");
    uint8_t h[44] = "RIFF....WAVEfmt ";
#define LE32(o, x) (v = (x), h[o] = v, h[o + 1] = v >> 8, h[o + 2] = v >> 16, h[o + 3] = v >> 24)
    LE32(4, 36 + bytes); LE32(16, 16); h[20] = 1; h[22] = 2; LE32(24, rate); LE32(28, rate * 4);
    h[32] = 4; h[34] = 16;
    memcpy(h + 36, "data", 4); LE32(40, bytes);
#undef LE32
    fwrite(h, 1, 44, f);
    for (uint32_t i = 0; i < frames; i++) {
        int16_t s = (i / 50) & 1 ? 10000 : -10000;
        int16_t frame[2] = { s, s };
        fwrite(frame, 2, 2, f);
    }
    fclose(f);
}

static bool wait_until(bool (*cond)(AthenaSoundStream *), AthenaSoundStream *s, int ms) {
    for (double end = now_ms() + ms; now_ms() < end; sleep_ms(5))
        if (cond(s)) return true;
    return cond(s);
}
static bool not_playing(AthenaSoundStream *s) { return !athena_sound_stream_is_playing(s); }
static bool is_ended(AthenaSoundStream *s) { return athena_sound_stream_ended(s); }

/* Lowest and highest write peak in [from, to) ms. */
static void peaks_between(double from, double to, int *low, int *high) {
    *low = 1 << 30;
    *high = -1;
    pthread_mutex_lock(&iop);
    for (int i = 0; i < write_count; i++) {
        if (writes[i].t < from || writes[i].t >= to) continue;
        if (writes[i].peak > *high) *high = writes[i].peak;
        if (writes[i].peak < *low) *low = writes[i].peak;
    }
    pthread_mutex_unlock(&iop);
}

/* No silence was written in [from, to): the square wave never stopped. */
static void check_no_gap(const char *what, double from, double to) {
    int low, high;
    peaks_between(from, to, &low, &high);
    CHECK(high >= 9900 && low >= 9900, "%s: write peaks %d..%d (silence was written)", what, low, high);
}

/*
 * The position heard follows the time actually elapsed since `since_ms`
 * (from `base` ms), minus the ~107 ms audsrv ring. Measured, not assumed:
 * sleep_ms() oversleeps under load, so fixed windows fail spuriously.
 */
static void check_heard(const char *what, uint32_t pos, uint32_t base, double since_ms) {
    double expected = base + (now_ms() - since_ms);
    CHECK(pos > expected - 250 && pos < expected + 20,
        "%s: position %u, expected about %.0f - 107 ms", what, pos, expected);
}

/* --- Tests --------------------------------------------------------------- */
static void test_open(void) {
    printf("open / format selection\n");
    AthenaSoundStream *s;
    int r;

    s = open_fixture("short.wav");
    CHECK(s && !s->convert && s->out.freq == 22050 && s->total_frames == 11025, "short.wav direct 22050 mono");
    athena_sound_stream_destroy(s);
    s = open_fixture("rate16k.wav");
    CHECK(s && s->convert && s->out.freq == 32000 && s->out.bits == 16 && s->step == 0x8000, "16 kHz -> 32 kHz, step 0.5");
    athena_sound_stream_destroy(s);
    s = open_fixture("float.wav");
    CHECK(s && s->convert && s->out.freq == 44100 && s->step == 0x10000 && s->out.channels == 2, "float 44.1k stereo -> 16-bit");
    athena_sound_stream_destroy(s);
    s = open_fixture("stereo8.wav");
    CHECK(s && s->convert && s->out.freq == 22050 && s->out.bits == 16, "8-bit stereo 22050 -> 16-bit");
    athena_sound_stream_destroy(s);
    s = open_fixture("bg.wav");
    CHECK(s && !s->convert && s->fmt.freq == 44100, "bg.wav direct");
    athena_sound_stream_destroy(s);

    s = athena_sound_stream_open(FIXTURES "mulaw.wav", &r);
    CHECK(!s && r == ATHENA_SOUND_ERR_FORMAT && strstr(detail, "0x7"), "mu-law refused: %d '%s'", r, detail);
    s = athena_sound_stream_open(FIXTURES "surround.wav", &r);
    CHECK(!s && r == ATHENA_SOUND_ERR_FORMAT && strstr(detail, "only mono and stereo"), "6ch refused: '%s'", detail);
    s = athena_sound_stream_open(FIXTURES "nodata.wav", &r);
    CHECK(!s && r == ATHENA_SOUND_ERR_FORMAT && strstr(detail, "data"), "nodata refused: '%s'", detail);
    s = athena_sound_stream_open(FIXTURES "missing.wav", &r);
    CHECK(!s && r == ATHENA_SOUND_ERR_OPEN, "missing -> OPEN");

    /* 8 kHz picks 32 kHz (x4); 96 kHz goes down to 48 kHz. */
    CHECK(output_rate(8000, 1) == 32000, "8000 -> %d", output_rate(8000, 1));
    CHECK(output_rate(24000, 1) == 48000, "24000 mono -> %d", output_rate(24000, 1));
    CHECK(output_rate(24000, 2) == 24000, "24000 stereo -> %d", output_rate(24000, 2));
    CHECK(output_rate(96000, 2) == 48000, "96000 -> %d", output_rate(96000, 2));
    CHECK(output_rate(37000, 2) == 44100, "37000 -> %d", output_rate(37000, 2));
}

static void test_converter(void) {
    printf("converter\n");
    uint32_t n, frames, first = 0;
    bool ended;
    int wraps, bad;

    /* 16 kHz mono, step 0.5: every source frame, then the midpoint. */
    AthenaSoundStream *s = open_fixture("rate16k.wav");
    int16_t *src = source_frames(s, "rate16k.wav", &n);
    int16_t *out = malloc(sizeof(int16_t) * 2 * (n * 3 + 16));
    frames = convert_all(s, out, n * 3, &ended, NULL, NULL);
    CHECK(ended && frames == 2 * n, "16k: %u output frames for %u source, ended %d", frames, n, ended);
    bad = 0;
    for (uint32_t k = 0; k + 1 < n && bad < 3; k++) {
        int mid = src[k * 2] + (int)(((int64_t)(src[(k + 1) * 2] - src[k * 2]) * 0x8000) >> 16);
        if (out[2 * k] != src[k * 2] || out[2 * k + 1] != mid) {
            bad++;
            printf("    k=%u out=%d,%d src=%d,%d\n", k, out[2 * k], out[2 * k + 1], src[k * 2], src[(k + 1) * 2]);
        }
    }
    CHECK(bad == 0, "16k interpolation");
    /* Seek: the first output frame is the source frame sought. */
    stream_seek(s, 1234);
    convert_all(s, out, 10, &ended, NULL, &first);
    CHECK(first == 1234 && out[0] == src[1234 * 2] && out[2] == src[1235 * 2],
        "seek to 1234: first %u, %d vs %d", first, out[0], src[1234 * 2]);
    CHECK(s->cur_frame == 1238, "frame after 10 outputs at step 0.5: %u", s->cur_frame);
    free(src);
    free(out);
    athena_sound_stream_destroy(s);

    /* Float stereo, step 1: exact copies of the converted samples. */
    s = open_fixture("float.wav");
    src = source_frames(s, "float.wav", &n);
    out = malloc(sizeof(int16_t) * 2 * (n + 16));
    frames = convert_all(s, out, n + 10, &ended, NULL, NULL);
    CHECK(ended && frames == n, "float: %u frames of %u", frames, n);
    CHECK(memcmp(out, src, n * 4) == 0, "float samples");
    int peak = 0;
    for (uint32_t i = 0; i < n * 2; i++) if (abs(out[i]) > peak) peak = abs(out[i]);
    CHECK(peak > 8000 && peak < 8300, "float peak %d (0.25 * 32767)", peak);
    free(src);
    free(out);
    athena_sound_stream_destroy(s);

    /* 8-bit stereo, looping: seamless, and every buffer stops at the wrap. */
    s = open_fixture("stereo8.wav");
    src = source_frames(s, "stereo8.wav", &n);
    out = malloc(sizeof(int16_t) * 2 * (n * 3));
    s->loop = true;
    frames = convert_all(s, out, n * 5 / 2, &ended, &wraps, NULL);
    CHECK(!ended && frames == n * 5 / 2, "loop: %u frames", frames);
    bad = 0;
    for (uint32_t i = 0; i < frames && bad < 3; i++)
        if (out[i * 2] != src[(i % n) * 2] || out[i * 2 + 1] != src[(i % n) * 2 + 1]) { bad++; printf("    i=%u\n", i); }
    CHECK(bad == 0, "looped samples");
    CHECK(wraps == 2, "buffers starting at a wrap: %d", wraps);
    free(src);
    free(out);
    athena_sound_stream_destroy(s);
}

static void test_gain(void) {
    printf("gain\n");
    int16_t buf[400];
    uint8_t u8[100];

    player.bits = 16; player.channels = 2; player.frame_bytes = 4;
    for (int i = 0; i < 400; i++) buf[i] = 10000;
    player.gain_from = FIXED_ONE; player.gain_to = 0; player.gain_frames = 100; player.gain_done = 0;
    player_apply_gain((char *)buf, 200 * 4);
    CHECK(buf[0] == 10000 && buf[1] == 10000, "first frame at full gain: %d", buf[0]);
    CHECK(abs(buf[100] - 5000) <= 100, "mid-ramp %d", buf[100]);
    CHECK(buf[199 * 2] == 0 && buf[150 * 2] == 0, "after the ramp: %d", buf[150 * 2]);
    bool monotonic = true;
    for (int f = 1; f < 200; f++) if (buf[f * 2] > buf[(f - 1) * 2]) monotonic = false;
    CHECK(monotonic, "ramp is monotonic");
    CHECK(player.gain_done == 100, "gain_done %u", player.gain_done);

    memset(u8, 200, sizeof(u8));
    player.bits = 8; player.channels = 1; player.frame_bytes = 1;
    player.gain_from = 0; player.gain_to = 0; player.gain_frames = 0; player.gain_done = 0;
    player_apply_gain((char *)u8, 100);
    CHECK(u8[0] == 128 && u8[99] == 128, "8-bit silence is 128: %d", u8[0]);
    player_reset_fade();
    memset(u8, 200, sizeof(u8));
    player_apply_gain((char *)u8, 100);
    CHECK(u8[50] == 200, "full gain leaves samples untouched");
}

static void test_segments(void) {
    printf("position through segments\n");
    AthenaSoundStream a = { .total_frames = 100000, .step = 0x8000, .resume_frame = 7 };
    AthenaSoundStream b = { .total_frames = 100000, .step = FIXED_ONE, .resume_frame = 9 };

    player_lock();
    player_clear_segments();
    player.frame_bytes = 2;
    player.written = 0;
    player.seek_active = false;
    player_add_segment(&a, SEGMENT_DATA, 1000, false);
    player.written = 400;
    player.next_frame = 1100;
    CHECK(player_frame_at(&a, -50) == 7, "before any audio: resume frame");
    CHECK(player_frame_at(&a, 200) == 1050, "100 output frames at step 0.5: %u", player_frame_at(&a, 200));
    CHECK(player_frame_at(&a, 900) == 1100, "late feeder: clamped to the end of the audio written");
    player_add_segment(&a, SEGMENT_HOLD, 1100, false);
    player.written = 600;
    CHECK(player_frame_at(&a, 500) == 1100, "silence holds the position");
    /* A seek not heard yet reports its target. */
    player.seek_active = true;
    player.seek_offset = 600;
    player.seek_frame = 5000;
    CHECK(player_frame_at(&a, 450) == 5000, "pending seek");
    player_add_segment(&b, SEGMENT_DATA, 5000, true);
    player.written = 1000;
    player.next_frame = 5200;
    CHECK(player_frame_at(&b, 800) == 5100, "after the seek is heard: %u", player_frame_at(&b, 800));
    CHECK(player_frame_at(&a, 800) == 7, "another stream's audio: resume frame");
    player.current = NULL;
    player_process_heard(800);
    CHECK(b.loops == 1 && !player.seek_active, "wrap event fired, seek heard");
    CHECK(player.segment_count == 1, "segments before the heard one dropped: %u", player.segment_count);
    player_clear_segments();
    player_unlock();
}

static void test_playback(void) {
    AthenaSoundStream *s, *b;
    double t, took, latency, seek_at, switch_at;
    uint32_t ends, loops, pos, paused;
    int result;

    printf("playback: direct 22050 Hz mono\n");
    s = open_fixture("short.wav");
    t = now_ms();
    CHECK(athena_sound_stream_play(s, 0) == 0, "play");
    CHECK(wait_until(not_playing, s, 2000), "short.wav never ended");
    took = now_ms() - t;
    /* Ends when its last sample is heard: 500 ms + ~50 ms of initial silence. */
    CHECK(took > 480 && took < 750, "ended after %.0f ms (500 ms file)", took);
    athena_sound_stream_get_events(s, &ends, &loops);
    CHECK(ends == 1 && athena_sound_stream_ended(s), "ends %u ended %d", ends, athena_sound_stream_ended(s));
    CHECK(athena_sound_stream_get_position(s) == 0, "position after end %u", athena_sound_stream_get_position(s));

    printf("playback: loop, and loop = false despite the read-ahead\n");
    s->loop = true;
    athena_sound_stream_play(s, 0);
    CHECK(!athena_sound_stream_ended(s), "ended cleared by play");
    sleep_ms(1300);
    athena_sound_stream_get_events(s, &ends, &loops);
    CHECK(athena_sound_stream_is_playing(s) && loops >= 2, "looping: playing %d loops %u", athena_sound_stream_is_playing(s), loops);
    pos = athena_sound_stream_get_position(s);
    CHECK(pos < 500, "position within the file while looping: %u", pos);
    t = now_ms();
    athena_sound_stream_set_loop(s, false);
    CHECK(wait_until(not_playing, s, 1000), "never ended after loop = false");
    took = now_ms() - t;
    CHECK(took < 700, "ended %.0f ms after loop = false (at most one pass + the ring)", took);
    athena_sound_stream_get_events(s, &ends, &loops);
    CHECK(ends == 2, "ends %u", ends);
    athena_sound_stream_destroy(s);

    printf("playback: converted 16 kHz, gapless seek\n");
    s = open_fixture("rate16k.wav");
    t = now_ms();
    athena_sound_stream_play(s, 0);
    while (athena_sound_stream_get_position(s) == 0 && now_ms() - t < 1000) sleep_ms(2);
    latency = now_ms() - t;
    CHECK(latency < 250, "start latency %.0f ms", latency);
    sleep_ms(500);
    pos = athena_sound_stream_get_position(s);
    CHECK(pos > 400 && pos < 600, "position 500 ms after it started: %u", pos);
    athena_sound_stream_set_position(s, 1000);
    pos = athena_sound_stream_get_position(s);
    CHECK(pos == 1000, "right after seek: %u", pos);
    t = now_ms();
    while (athena_sound_stream_get_position(s) <= 1000 && now_ms() - t < 1000) sleep_ms(2);
    latency = now_ms() - t;
    CHECK(latency < 150, "seek heard after %.0f ms (at most the ring)", latency);
    sleep_ms(300);
    pos = athena_sound_stream_get_position(s);
    CHECK(pos > 1230 && pos < 1370, "300 ms after the seek was heard: %u", pos);
    athena_sound_stream_pause(s, 0);
    paused = athena_sound_stream_get_position(s);
    sleep_ms(200);
    CHECK(!athena_sound_stream_is_playing(s) && athena_sound_stream_get_position(s) == paused, "paused at %u", paused);
    t = now_ms();
    athena_sound_stream_play(s, 0);
    CHECK(wait_until(is_ended, s, 1500), "never ended");
    took = now_ms() - t;
    CHECK(took > 2000 - paused && took < 2000 - paused + 300, "remaining %.0f ms after resuming at %u", took, paused);
    athena_sound_stream_destroy(s);

    printf("playback: no gap on seek, same-format switch, or a 300 ms read stall\n");
    write_square("/tmp/square_a.wav", 44100, 6000);
    write_square("/tmp/square_b.wav", 44100, 6000);
    s = open_path("/tmp/square_a.wav");
    b = open_path("/tmp/square_b.wav");
    athena_sound_stream_play(s, 0);
    sleep_ms(400);
    t = now_ms();
    seek_at = t;
    athena_sound_stream_set_position(s, 3000);
    sleep_ms(400);
    check_no_gap("seek", t - 100, now_ms());
    check_heard("after the seek", athena_sound_stream_get_position(s), 3000, seek_at);
    t = now_ms();
    switch_at = t;
    athena_sound_stream_play(b, 0);
    CHECK(!athena_sound_stream_is_playing(s) && athena_sound_stream_is_playing(b), "b replaced s");
    sleep_ms(400);
    check_no_gap("switch", t - 100, now_ms());
    check_heard("b after the switch", athena_sound_stream_get_position(b), 0, switch_at);
    /* The old stream resumes where it was heard: 3000 + its playing time - the ~107 ms ring. */
    pos = athena_sound_stream_get_position(s);
    CHECK(pos > 3000 + (switch_at - seek_at) - 250 && pos < 3000 + (switch_at - seek_at) + 20,
        "s kept its position: %u after %.0f ms", pos, switch_at - seek_at);
    t = now_ms();
    stall_next_read_ms = 300;
    sleep_ms(700);
    check_no_gap("read stall", t, now_ms());
    CHECK(stall_next_read_ms == 0, "the stall happened");
    check_heard("b after the stall", athena_sound_stream_get_position(b), 0, switch_at);
    CHECK(player.queued_bytes <= player_read_ahead(b) + BLOCK_BYTES, "read-ahead bounded: %u", player.queued_bytes);

    printf("playback: fades (constant amplitude 10000)\n");
    t = now_ms();
    athena_sound_stream_pause(b, 0);
    athena_sound_stream_play(b, 400);
    sleep_ms(900);
    int low, high, first = -1, later;
    pthread_mutex_lock(&iop);
    for (int i = 0; i < write_count && first < 0; i++)
        if (writes[i].t >= t && writes[i].peak > 0) first = writes[i].peak;
    pthread_mutex_unlock(&iop);
    peaks_between(t + 700, t + 900, &low, &later);
    CHECK(first >= 0 && first < 3000, "fade-in starts quiet: %d", first);
    CHECK(later >= 9900, "fade-in reaches full: %d", later);
    t = now_ms();
    athena_sound_stream_pause(b, 400);
    sleep_ms(150);
    CHECK(athena_sound_stream_is_playing(b), "still playing during the fade-out");
    CHECK(wait_until(not_playing, b, 1500), "fade-out never paused");
    took = now_ms() - t;
    CHECK(took > 250 && took < 700, "paused %.0f ms after pause({fade: 400})", took);
    peaks_between(t + 150, t + 250, &low, &high);
    CHECK(high > 1000 && high < 9000, "mid fade-out peak %d", high);
    paused = athena_sound_stream_get_position(b);
    sleep_ms(300);
    CHECK(athena_sound_stream_get_position(b) == paused, "stays paused");

    printf("playback: play() cancels a fade-out; stop({fade})\n");
    athena_sound_stream_play(b, 0);
    sleep_ms(200);
    athena_sound_stream_pause(b, 600);
    sleep_ms(100);
    athena_sound_stream_play(b, 100);
    sleep_ms(900);
    CHECK(athena_sound_stream_is_playing(b), "paused anyway");
    t = now_ms();
    peaks_between(t - 300, t, &low, &high);
    CHECK(low >= 9900, "back to full volume: %d", low);
    athena_sound_stream_stop(b, 200);
    CHECK(athena_sound_stream_is_playing(b), "playing during stop fade");
    CHECK(wait_until(not_playing, b, 1000), "never stopped");
    CHECK(athena_sound_stream_get_position(b) == 0, "position after stop %u", athena_sound_stream_get_position(b));

    printf("playback: pause right after play(), before audsrv was reconfigured\n");
    /*
     * The threads exited and called audsrv_stop_audio(): the IOP no longer
     * consumes, and its ring is full of the last drain's silence. A drain
     * now could never write, nor restart the IOP (PCSX2 run, 2026-09-25).
     */
    CHECK(wait_until(not_playing, b, 1000), "b idle");
    sleep_ms(300);
    CHECK(!player.feeder_running && !player.reader_running, "threads exited");
    athena_sound_stream_play(b, 0);
    athena_sound_stream_pause(b, 600);
    sleep_ms(100);
    athena_sound_stream_play(b, 100);
    sleep_ms(600);
    pos = athena_sound_stream_get_position(b);
    CHECK(athena_sound_stream_is_playing(b) && pos > 200, "plays after the early pause: position %u", pos);
    athena_sound_stream_stop(b, 300);
    CHECK(athena_sound_stream_is_playing(b), "stop({fade}) fades");
    CHECK(wait_until(not_playing, b, 1000), "stopped");
    /* The same with a stream switch of another format. */
    sleep_ms(300);
    AthenaSoundStream *d = open_fixture("short.wav");
    athena_sound_stream_play(b, 0);
    athena_sound_stream_play(d, 0);
    CHECK(wait_until(is_ended, d, 1500), "d plays to its end after an early switch");
    athena_sound_stream_destroy(d);

    printf("playback: IOP reset while playing: the file is reopened\n");
    {
        int old_fd, intruder;
        uint32_t before;
        char byte;

        uint32_t start = athena_sound_stream_get_position(s);
        athena_sound_stream_play(s, 0);
        sleep_ms(500);
        old_fd = s->fd;
        /* What audsrv's end hook does in iopman_reset(), then the reset itself. */
        sound_stream_halt();
        before = athena_sound_stream_get_position(s);
        CHECK(before > start + 300 && before < start + 600, "position kept at the reset: %u (from %u)",
            before, start);
        close(old_fd);
        host_reset_count++;
        /* The reloaded fileXio hands the old number to the next file opened. */
        intruder = open("/tmp/square_b.wav", O_RDONLY);
        CHECK(intruder == old_fd, "descriptor number reused (%d, was %d)", intruder, old_fd);
        CHECK(athena_sound_stream_play(s, 0) == 0, "play after the reset");
        sleep_ms(500);
        pos = athena_sound_stream_get_position(s);
        CHECK(athena_sound_stream_is_playing(s) && pos > before + 250 && pos < before + 500,
            "resumed from %u: %u", before, pos);
        CHECK(s->fd >= 0 && s->fd != old_fd && s->io_reset == host_reset_count, "reopened (fd %d)", s->fd);
        athena_sound_stream_destroy(s);
        CHECK(pread(intruder, &byte, 1, 0) == 1, "the file that took the old number is still open");
        close(intruder);

        /* The file is gone after the reset: the stream ends instead of failing. */
        write_square("/tmp/square_c.wav", 44100, 3000);
        s = open_path("/tmp/square_c.wav");
        athena_sound_stream_play(s, 0);
        sleep_ms(300);
        sound_stream_halt();
        close(s->fd);
        host_reset_count++;
        unlink("/tmp/square_c.wav");
        athena_sound_stream_play(s, 0);
        CHECK(wait_until(is_ended, s, 1500), "a stream whose file vanished ends");
        athena_sound_stream_destroy(s);
        s = open_path("/tmp/square_a.wav");
    }

    printf("playback: format switch, destroy while playing\n");
    AthenaSoundStream *c = open_fixture("short.wav");
    athena_sound_stream_play(s, 0);
    sleep_ms(200);
    athena_sound_stream_play(c, 0);
    CHECK(!athena_sound_stream_is_playing(s) && athena_sound_stream_is_playing(c), "c replaced s");
    sleep_ms(100);
    athena_sound_stream_destroy(c);
    CHECK(player.current == NULL, "destroy clears current");
    sleep_ms(400);
    CHECK(!player.feeder_running && !player.reader_running && threads_alive == 0,
        "threads exit once idle (%d alive)", threads_alive);
    result = athena_sound_stream_play(s, 0);
    sleep_ms(300);
    CHECK(result == 0 && athena_sound_stream_is_playing(s), "plays again after a destroy");
    athena_sound_stream_destroy(s);
    athena_sound_stream_destroy(b);
    sound_stream_halt();
    CHECK(threads_alive == 0, "halt joins both threads (%d alive)", threads_alive);
}

int main(void) {
    pthread_t iop_id;
    athena_sound_module_init();
    pthread_create(&iop_id, NULL, iop_thread, NULL);
    test_open();
    test_converter();
    test_gain();
    test_segments();
    test_playback();
    iop_running = 0;
    pthread_join(iop_id, NULL);
    printf("sound_stream: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
