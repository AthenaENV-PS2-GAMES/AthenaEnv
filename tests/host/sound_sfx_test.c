/*
 * Host test of src/modules/sound/native/sound_sfx.c against a simulated
 * audsrv ADPCM side that follows iop/sound/audsrv/src/adpcm.c: samples are
 * appended after the last one (from 0x5010) and memory is only reclaimed
 * at the end; a voice sets ENDX when it reaches an end flag, which for a
 * looping sample happens after its first pass while it keeps sounding.
 *
 * Run with tests/host/run.sh from the repository root.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound_sfx.c"
#include "job.c"
#include "sound_path.c"
#include "host_runtime.h"

/* --- Stubs: the rest of the Sound module ------------------------------- */
static char detail[160];
static uint32_t generation = 1;
void sound_set_detail(const char *fmt, ...) {
    va_list args;
    if (!fmt) { detail[0] = 0; return; }
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
}
const char *athena_sound_error_detail(void) { return detail; }
const char *athena_sound_result_string(int result) { (void)result; return "error"; }
int athena_sound_ensure(void) { return 0; }
bool athena_sound_ready(void) { return true; }
uint32_t sound_iop_generation(void) { return generation; }

/* --- Stub: the IOP side of audsrv's ADPCM voices ------------------------ */
typedef struct {
    audsrv_adpcm_t *id;
    uint32_t addr, size, samples;
    int pitch, loop;
} IopSample;

static IopSample iop_samples[512];
static int iop_count;
static int iop_rejected;
static struct { uint32_t addr; double start, duration; bool keyed; int volume, pan; } voices[24];

static IopSample *iop_find(audsrv_adpcm_t *id) {
    for (int i = 0; i < iop_count; i++)
        if (iop_samples[i].id == id) return &iop_samples[i];
    return NULL;
}
static uint32_t iop_next_address(void) {
    return iop_count ? iop_samples[iop_count - 1].addr + iop_samples[iop_count - 1].size : 0x5010;
}
static bool endx(int ch) {
    return !voices[ch].keyed || now_ms() >= voices[ch].start + voices[ch].duration;
}
int audsrv_load_adpcm(audsrv_adpcm_t *adpcm, void *buffer, int size) {
    const uint32_t *header = buffer;
    uint32_t addr;
    if (iop_find(adpcm)) return 0;
    addr = iop_next_address();
    if (addr + size - 16 > 2097152) { iop_rejected++; return -AUDSRV_ERR_OUT_OF_MEMORY; }
    iop_samples[iop_count++] = (IopSample){ adpcm, addr, (uint32_t)size - 16, header[3],
        (int)header[2], (int)((header[1] >> 16) & 0xff) };
    adpcm->pitch = (int)header[2];
    adpcm->loop = (int)((header[1] >> 16) & 0xff);
    return 0;
}
int audsrv_free_adpcm(audsrv_adpcm_t *adpcm) {
    for (int i = 0; i < iop_count; i++) {
        if (iop_samples[i].id != adpcm) continue;
        memmove(&iop_samples[i], &iop_samples[i + 1], sizeof(IopSample) * (iop_count - i - 1));
        iop_count--;
        break;
    }
    return 0;
}
int audsrv_ch_play_adpcm(int ch, audsrv_adpcm_t *adpcm) {
    IopSample *s = iop_find(adpcm);
    if (!s) return 5; /* AUDSRV_ERR_ARGS, positive as in audsrv */
    if (!endx(ch)) return -AUDSRV_ERR_NO_MORE_CHANNELS;
    voices[ch].addr = s->addr;
    voices[ch].start = now_ms();
    voices[ch].duration = s->samples * 1000.0 / (s->pitch * 48000.0 / 4096);
    voices[ch].keyed = true;
    return ch;
}
int audsrv_is_adpcm_playing(int ch, audsrv_adpcm_t *adpcm) {
    IopSample *s;
    if (endx(ch)) return 0;
    s = iop_find(adpcm);
    if (!s) return 5;
    return voices[ch].addr == s->addr;
}
int audsrv_adpcm_set_volume_and_pan(int ch, int volume, int pan) {
    voices[ch].volume = volume;
    voices[ch].pan = pan;
    return 0;
}
/* Unused by sound_sfx.c; declared by the shared audsrv stub. */
int audsrv_set_format(struct audsrv_fmt_t *fmt) { (void)fmt; return 0; }
int audsrv_available(void) { return 0; }
int audsrv_queued(void) { return 0; }
int audsrv_play_audio(const char *buf, int len) { (void)buf; return len; }
int audsrv_stop_audio(void) { return 0; }
int audsrv_set_volume(int v) { (void)v; return 0; }

/* IOP reset: audsrv comes back empty. */
static void iop_reset(void) {
    iop_count = 0;
    memset(voices, 0, sizeof(voices));
    generation++;
    sound_sfx_forget_session();
}

/* --- Helpers ------------------------------------------------------------- */
#define FIXTURES "bin/tests/sound/"

static AthenaSfx *load(const char *name, int *result) {
    char path[256];
    snprintf(path, sizeof(path), FIXTURES "%s", name);
    return athena_sfx_load(path, result);
}

/* The EE mirror agrees with what the IOP holds. */
static bool mirror_matches(void) {
    AthenaSoundMemoryStats stats;
    athena_sfx_get_memory_stats(&stats);
    return stats.used == iop_next_address() - 0x5010 && stats.samples == (uint32_t)iop_count;
}

/* --- Tests --------------------------------------------------------------- */
static void test_load(void) {
    AthenaSoundMemoryStats stats;
    AthenaSfx *sfx;
    int r;

    printf("load and validation\n");
    sfx = load("pop.adp", &r);
    athena_sfx_get_memory_stats(&stats);
    CHECK(sfx && stats.used == 3408 && stats.samples == 1 && mirror_matches(), "pop.adp: used %u", stats.used);
    CHECK(sfx && sfx->path[0] == '/', "path made absolute: %s", sfx ? sfx->path : "");
    athena_sfx_destroy(sfx);
    CHECK(mirror_matches() && iop_count == 0, "free");

    CHECK(!load("bad.adp", &r) && r == ATHENA_SOUND_ERR_FORMAT, "bad magic: %d", r);
    CHECK(!load("garbage.bin", &r) && r == ATHENA_SOUND_ERR_FORMAT, "text file: %d", r);
    CHECK(!load("missing.adp", &r) && r == ATHENA_SOUND_ERR_OPEN, "missing: %d", r);
    CHECK(!load("truncated.adp", &r) && r == ATHENA_SOUND_ERR_CORRUPT && strstr(detail, "16-byte"),
        "truncated: %d '%s'", r, detail);
    CHECK(!load("noend.adp", &r) && r == ATHENA_SOUND_ERR_CORRUPT && strstr(detail, "end flag"),
        "no end flag: %d '%s'", r, detail);
    CHECK(iop_count == 0, "nothing invalid reached the IOP");
}

static void test_memory_mirror(void) {
    static const char *names[] = { "pop.adp", "over.adp", "loop.adp" };
    AthenaSfx *live[256];
    int count = 0, refused = 0, mismatches = 0;

    printf("SPU2 memory mirror (random loads and frees)\n");
    srand(1234);
    for (int op = 0; op < 600; op++) {
        if (count > 0 && (rand() % 3 == 0 || count == 256)) {
            int i = rand() % count;
            athena_sfx_destroy(live[i]);
            live[i] = live[--count];
        } else {
            int r;
            AthenaSfx *sfx = load(names[rand() % 3], &r);
            if (sfx) live[count++] = sfx;
            else if (r == ATHENA_SOUND_ERR_SPU_MEMORY) refused++;
            else CHECK(0, "load failed: %d", r);
        }
        if (!mirror_matches()) mismatches++;
    }
    CHECK(mismatches == 0, "EE and IOP disagreed %d times", mismatches);
    CHECK(refused > 0, "memory never filled up (%d refused)", refused);
    CHECK(iop_rejected == 0, "the IOP had to refuse %d uploads the EE let through", iop_rejected);
    while (count > 0)
        athena_sfx_destroy(live[--count]);
    CHECK(mirror_matches() && iop_count == 0, "all freed");
}

static void test_channels(void) {
    AthenaSfx *pop, *loop, *loop2;
    int r, channels = 0;

    printf("channels, loops, stop, master volume\n");
    pop = load("pop.adp", &r);
    loop = load("loop.adp", &r);
    for (int i = 0; i < 26; i++)
        if (athena_sfx_play(pop, -1) >= 0) channels++;
    CHECK(channels == 24 && athena_sfx_find_channel() == -1, "24 voices: %d", channels);
    sleep_ms((int)athena_sfx_get_length(pop) + 50);
    CHECK(athena_sfx_find_channel() == 0, "voices free after the sample ended");

    /* A loop past its first pass (200 ms): ENDX is set, but it still plays. */
    CHECK(athena_sfx_play(loop, 23) == 23, "loop on 23");
    sleep_ms(300);
    CHECK(endx(23), "the simulated SPU2 set ENDX after the first pass");
    CHECK(athena_sfx_is_playing(loop, 23) == 1, "loop still playing");
    CHECK(athena_sfx_play(pop, 23) == -1, "its channel is busy");
    CHECK(athena_sfx_play(pop, -1) != 23, "auto search skips it");
    athena_sfx_stop(loop, 23);
    CHECK(athena_sfx_is_playing(loop, 23) == 0 && voices[23].volume == 0, "stop mutes and releases");
    CHECK(athena_sfx_play(pop, 23) == 23, "channel reused after stop");

    /* free() of a loop still sounding mutes its voice. */
    loop2 = load("loop.adp", &r);
    CHECK(athena_sfx_play(loop2, 22) == 22, "loop on 22");
    sleep_ms(300);
    athena_sfx_destroy(loop2);
    CHECK(voices[22].volume == 0, "freed loop muted: volume %d", voices[22].volume);
    CHECK(athena_sfx_play(pop, 22) == 22, "channel reused after free");

    /* Master volume: sounding voices follow, stopped ones stay muted. */
    sleep_ms((int)athena_sfx_get_length(pop) + 50);
    athena_sfx_set_volume(pop, 80);
    CHECK(athena_sfx_play(pop, 5) == 5 && athena_sfx_play(loop, 6) == 6, "play on 5 and 6");
    athena_sfx_stop(loop, 6);
    athena_sfx_set_master_volume(50);
    CHECK(voices[5].volume == 40, "sounding voice at 80%% of 50: %d", voices[5].volume);
    CHECK(voices[6].volume == 0, "stopped voice stays muted: %d", voices[6].volume);
    athena_sfx_set_master_volume(100);
    athena_sfx_destroy(pop);
    athena_sfx_destroy(loop);
    CHECK(mirror_matches() && iop_count == 0, "all freed");
}

static void test_reload_after_reset(void) {
    char cwd[256];
    AthenaSfx *pop;
    int r;

    printf("reload after an IOP reset, from another directory\n");
    pop = load("pop.adp", &r);
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = 0;
    iop_reset();
    CHECK(athena_sfx_is_playing(pop, 0) == 0, "stale sample does not play");
    CHECK(chdir("/tmp") == 0, "chdir");
    r = athena_sfx_play(pop, -1);
    CHECK(r >= 0 && iop_count == 1 && mirror_matches(), "played after reloading: %d", r);
    CHECK(chdir(cwd) == 0, "chdir back");
    athena_sfx_destroy(pop);
    CHECK(iop_count == 0, "freed");
}

static void test_async(void) {
    AthenaSfxJob *job;
    AthenaSfx *sfx, *again;
    AthenaSfx *fill[128];
    int r, filled = 0;

    printf("loading on a worker thread\n");
    job = athena_sfx_load_async(FIXTURES "pop.adp", &r);
    CHECK(job && athena_sfx_job_wait(job, 2000), "wait");
    CHECK(iop_count == 0, "nothing uploaded before poll()");
    CHECK(athena_sfx_job_poll(job, &sfx, &r) == ATHENA_SFX_JOB_DONE && sfx, "done with a sample");
    CHECK(iop_count == 1 && mirror_matches(), "uploaded by poll()");
    CHECK(athena_sfx_job_poll(job, &again, &r) == ATHENA_SFX_JOB_DONE && !again, "handed over once");
    CHECK(athena_sfx_play(sfx, -1) >= 0, "plays");
    athena_sfx_job_destroy(job);
    athena_sfx_destroy(sfx);

    job = athena_sfx_load_async(FIXTURES "missing.adp", &r);
    athena_sfx_job_wait(job, 2000);
    CHECK(athena_sfx_job_poll(job, &sfx, &r) == ATHENA_SFX_JOB_FAILED && r == ATHENA_SOUND_ERR_OPEN,
        "missing: %d", r);
    athena_sfx_job_destroy(job);

    job = athena_sfx_load_async(FIXTURES "noend.adp", &r);
    athena_sfx_job_wait(job, 2000);
    CHECK(athena_sfx_job_poll(job, &sfx, &r) == ATHENA_SFX_JOB_FAILED &&
        r == ATHENA_SOUND_ERR_CORRUPT && strstr(detail, "end flag"), "corrupt: %d '%s'", r, detail);
    detail[0] = 0;
    CHECK(athena_sfx_job_poll(job, &sfx, &r) == ATHENA_SFX_JOB_FAILED && strstr(detail, "end flag"),
        "the detail comes back on every poll: '%s'", detail);
    athena_sfx_job_destroy(job);

    job = athena_sfx_load_async(FIXTURES "over.adp", &r);
    athena_sfx_job_cancel(job);
    athena_sfx_job_wait(job, 2000);
    CHECK(athena_sfx_job_poll(job, &sfx, &r) == ATHENA_SFX_JOB_CANCELLED && !sfx, "cancelled");
    CHECK(iop_count == 0, "a cancelled job uploads nothing");
    athena_sfx_job_destroy(job);

    /* Dropped while reading: destroy does not wait; the pool frees the read when it ends. */
    job = athena_sfx_load_async(FIXTURES "over.adp", &r);
    athena_sfx_job_destroy(job);

    /* The upload in poll() still checks SPU2 memory. */
    while (filled < 128 && (fill[filled] = load("over.adp", &r)))
        filled++;
    job = athena_sfx_load_async(FIXTURES "over.adp", &r);
    athena_sfx_job_wait(job, 2000);
    CHECK(athena_sfx_job_poll(job, &sfx, &r) == ATHENA_SFX_JOB_FAILED &&
        r == ATHENA_SOUND_ERR_SPU_MEMORY, "full SPU2 memory: %d", r);
    athena_sfx_job_destroy(job);
    while (filled > 0)
        athena_sfx_destroy(fill[--filled]);
    /* Reads run on the shared job pool, whose workers stop with the runtime. */
    athena_job_pool_stop();
    CHECK(threads_alive == 0 && iop_count == 0 && iop_rejected == 0,
        "workers joined (%d alive), memory freed", threads_alive);
}

int main(void) {
    test_load();
    test_memory_mirror();
    test_channels();
    test_reload_after_reset();
    test_async();
    printf("sound_sfx: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
