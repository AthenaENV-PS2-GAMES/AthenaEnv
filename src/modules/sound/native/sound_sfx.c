#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kernel.h>
#include <audsrv.h>

#include <athena/debug.h>
#include <athena/sound.h>

#include "sound_internal.h"

/* Header written by adpenc in front of the SPU2 ADPCM blocks. */
typedef struct {
    char magic[4]; /* "APCM" */
    uint8_t version;
    uint8_t channels;
    uint8_t loop;
    uint8_t reserved;
    uint32_t pitch;
    uint32_t samples;
} AdpcmHeader;

#define ADPCM_HEADER_SIZE 16
#define ADPCM_BLOCK_SIZE 16
#define ADPCM_BLOCK_SAMPLES 28
/* Block flag: last block of the sample (with 0x2, jump to the loop start). */
#define ADPCM_FLAG_END 0x01
/* SPU2 voice pitch register: 14 bits, 0x1000 = 48 kHz. */
#define ADPCM_MAX_PITCH 0x3FFF
/* audsrv places samples from 0x5010 up to the end of the 2 MiB SPU2 RAM. */
#define SPU_SAMPLE_BASE 0x5010u
#define SPU_SAMPLE_END (2u * 1024 * 1024)
#define ADPCM_MAX_SIZE (SPU_SAMPLE_END - SPU_SAMPLE_BASE + ADPCM_HEADER_SIZE)
/* Not yet sent to the channel: forces the first volume/pan update. */
#define CHANNEL_LEVEL_UNKNOWN INT_MIN

struct AthenaSfx {
    /* Its address is the sample id on the IOP side: it must not move. */
    audsrv_adpcm_t adpcm;
    /* Kept to upload the sample again after an IOP reset. */
    char *path;
    /* audsrv session the sample was uploaded to. */
    uint32_t generation;
    /* Mirror of audsrv's sample list, in upload order (see memory stats). */
    AthenaSfx *prev;
    AthenaSfx *next;
    bool resident;
    uint32_t spu_addr;
    uint32_t spu_size;
    uint32_t samples;
    int rate;
    int volume;
    int pan;
    bool loop;
};

static bool stereo_warned;

static AthenaSfx *channel_owner[ATHENA_SOUND_CHANNELS];
static int channel_volume[ATHENA_SOUND_CHANNELS];
static int channel_pan[ATHENA_SOUND_CHANNELS];
/* Muted by stop(): the voice may still run, but it no longer counts. */
static bool channel_stopped[ATHENA_SOUND_CHANNELS];
static int master_volume = ATHENA_SOUND_MAX_VOLUME;

/*
 * audsrv appends each sample after the last one (tail) and only moves the
 * tail back when the last sample is freed, so this list is enough to know
 * where the next sample goes and how much memory freed samples still hold.
 */
static AthenaSfx *resident_head;
static AthenaSfx *resident_tail;
static uint32_t resident_bytes;
static uint32_t resident_count;

static void sfx_forget_channel_levels(void) {
    for (int i = 0; i < ATHENA_SOUND_CHANNELS; i++) {
        channel_volume[i] = CHANNEL_LEVEL_UNKNOWN;
        channel_pan[i] = CHANNEL_LEVEL_UNKNOWN;
    }
}

static uint32_t sfx_next_address(void) {
    return resident_tail ? resident_tail->spu_addr + resident_tail->spu_size : SPU_SAMPLE_BASE;
}

static void sfx_link(AthenaSfx *sfx, uint32_t size) {
    sfx->spu_addr = sfx_next_address();
    sfx->spu_size = size;
    sfx->prev = resident_tail;
    sfx->next = NULL;
    if (resident_tail)
        resident_tail->next = sfx;
    else
        resident_head = sfx;
    resident_tail = sfx;
    sfx->resident = true;
    resident_bytes += size;
    resident_count++;
}

static void sfx_unlink(AthenaSfx *sfx) {
    if (!sfx->resident)
        return;
    if (sfx->prev)
        sfx->prev->next = sfx->next;
    else
        resident_head = sfx->next;
    if (sfx->next)
        sfx->next->prev = sfx->prev;
    else
        resident_tail = sfx->prev;
    sfx->prev = sfx->next = NULL;
    sfx->resident = false;
    resident_bytes -= sfx->spu_size;
    resident_count--;
}

void sound_sfx_audsrv_started(void) {
    /* The new audsrv session starts with empty SPU2 sample memory. */
    while (resident_head)
        sfx_unlink(resident_head);
    memset(channel_owner, 0, sizeof(channel_owner));
    memset(channel_stopped, 0, sizeof(channel_stopped));
    sfx_forget_channel_levels();
}

static bool sfx_is_current(const AthenaSfx *sfx) {
    return athena_sound_ready() && sfx->resident && sfx->generation == sound_iop_generation();
}

static int sfx_read_file(const char *path, uint8_t **out, int *out_size) {
    FILE *file = fopen(path, "rb");
    long size;
    size_t padded;
    uint8_t *buffer;

    if (!file)
        return ATHENA_SOUND_ERR_OPEN;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ATHENA_SOUND_ERR_READ;
    }
    if (size <= ADPCM_HEADER_SIZE) {
        fclose(file);
        return ATHENA_SOUND_ERR_FORMAT;
    }
    if ((unsigned long)size > ADPCM_MAX_SIZE) {
        fclose(file);
        sound_set_detail("%ld bytes, SPU2 sample memory holds %u", size - ADPCM_HEADER_SIZE,
            SPU_SAMPLE_END - SPU_SAMPLE_BASE);
        return ATHENA_SOUND_ERR_SPU_MEMORY;
    }
    /* SIF DMA moves 16-byte blocks from a cache-line aligned buffer. */
    padded = ((size_t)size + 63) & ~(size_t)63;
    buffer = memalign(64, padded);
    if (!buffer) {
        fclose(file);
        return ATHENA_SOUND_ERR_MEMORY;
    }
    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(buffer);
        return ATHENA_SOUND_ERR_READ;
    }
    fclose(file);
    memset(buffer + size, 0, padded - (size_t)size);
    SyncDCache(buffer, buffer + padded);
    *out = buffer;
    *out_size = (int)size;
    return ATHENA_SOUND_OK;
}

/*
 * Checks what the SPU2 relies on before the data leaves the EE: a voice
 * plays blocks until one has the end flag, so a truncated file would run on
 * into the samples placed after it.
 */
static int sfx_validate(const uint8_t *buffer, int size, AdpcmHeader *header) {
    uint32_t data_size = (uint32_t)size - ADPCM_HEADER_SIZE;
    uint32_t blocks;

    memcpy(header, buffer, sizeof(*header));
    if (memcmp(header->magic, "APCM", 4) != 0)
        return ATHENA_SOUND_ERR_FORMAT;
    if (header->pitch == 0 || header->pitch > ADPCM_MAX_PITCH) {
        sound_set_detail("pitch 0x%x out of range", (unsigned)header->pitch);
        return ATHENA_SOUND_ERR_FORMAT;
    }
    if (data_size % ADPCM_BLOCK_SIZE != 0) {
        sound_set_detail("%u data bytes are not whole 16-byte blocks; truncated?",
            (unsigned)data_size);
        return ATHENA_SOUND_ERR_CORRUPT;
    }
    blocks = data_size / ADPCM_BLOCK_SIZE;
    if (!(buffer[ADPCM_HEADER_SIZE + (blocks - 1) * ADPCM_BLOCK_SIZE + 1] & ADPCM_FLAG_END)) {
        sound_set_detail("last block has no end flag; truncated?");
        return ATHENA_SOUND_ERR_CORRUPT;
    }
    if (header->samples > blocks * ADPCM_BLOCK_SAMPLES) {
        sound_set_detail("header says %u samples, data holds %u",
            (unsigned)header->samples, (unsigned)(blocks * ADPCM_BLOCK_SAMPLES));
        return ATHENA_SOUND_ERR_CORRUPT;
    }
    return ATHENA_SOUND_OK;
}

/* Reads, checks and uploads sfx->path into the current audsrv session. */
static int sfx_upload(AthenaSfx *sfx) {
    AdpcmHeader header;
    uint8_t *buffer = NULL;
    uint32_t data_size, next_address;
    int size = 0;
    int status;

    status = athena_sound_ensure();
    if (status < 0)
        return status;
    status = sfx_read_file(sfx->path, &buffer, &size);
    if (status < 0)
        return status;
    status = sfx_validate(buffer, size, &header);
    if (status < 0) {
        free(buffer);
        return status;
    }

    data_size = (uint32_t)size - ADPCM_HEADER_SIZE;
    next_address = sfx_next_address();
    if (next_address + data_size > SPU_SAMPLE_END) {
        uint32_t free_bytes = SPU_SAMPLE_END - next_address;
        uint32_t wasted = next_address - SPU_SAMPLE_BASE - resident_bytes;

        if (wasted > 0)
            sound_set_detail("needs %u bytes, %u free; %u more are held by freed samples "
                "until the ones loaded after them are freed",
                (unsigned)data_size, (unsigned)free_bytes, (unsigned)wasted);
        else
            sound_set_detail("needs %u bytes, %u free", (unsigned)data_size, (unsigned)free_bytes);
        free(buffer);
        return ATHENA_SOUND_ERR_SPU_MEMORY;
    }

    status = audsrv_load_adpcm(&sfx->adpcm, buffer, size);
    /* The IOP keeps its own copy in SPU2 RAM; the EE buffer is not needed. */
    free(buffer);
    sfx->adpcm.buffer = NULL;
    if (status != AUDSRV_ERR_NOERROR) {
        dbgprintf("[Sound] audsrv_load_adpcm(%s) failed: %d\n", sfx->path, status);
        if (status == -AUDSRV_ERR_OUT_OF_MEMORY) {
            /* The EE side also stages the file in IOP heap memory. */
            sound_set_detail("the IOP heap has no room for %d bytes", size);
            return ATHENA_SOUND_ERR_SPU_MEMORY;
        }
        return ATHENA_SOUND_ERR_IOP;
    }

    sfx_link(sfx, data_size);
    sfx->generation = sound_iop_generation();
    sfx->samples = header.samples;
    sfx->rate = (int)(((uint64_t)header.pitch * 48000) / 4096);
    sfx->loop = header.loop != 0;
    /* Once per session: games load the same sample many times. */
    if (header.channels > 1 && !stereo_warned) {
        dbgprintf("[Sound] %s: audsrv plays only the first channel of a stereo .adp "
            "(tools/wav2adp.js mixes stereo down to mono)\n", sfx->path);
        stereo_warned = true;
    }
    return ATHENA_SOUND_OK;
}

AthenaSfx *athena_sfx_load(const char *path, int *result) {
    AthenaSfx *sfx;
    int status;

    sound_set_detail(NULL);
    if (!path || !path[0]) {
        status = ATHENA_SOUND_ERR_ARGS;
        goto fail;
    }
    sfx = calloc(1, sizeof(*sfx));
    if (!sfx || !(sfx->path = strdup(path))) {
        free(sfx);
        status = ATHENA_SOUND_ERR_MEMORY;
        goto fail;
    }
    status = sfx_upload(sfx);
    if (status < 0) {
        free(sfx->path);
        free(sfx);
        goto fail;
    }
    sfx->volume = ATHENA_SOUND_MAX_VOLUME;
    sfx->pan = 0;
    if (result)
        *result = ATHENA_SOUND_OK;
    return sfx;

fail:
    if (result)
        *result = status;
    return NULL;
}

static void sfx_release_channel(int channel) {
    channel_owner[channel] = NULL;
    channel_stopped[channel] = false;
}

/*
 * Whether the channel's owner still sounds. The SPU2 sets ENDX when a voice
 * reaches an end flag, also the one that sends a looping sample back to its
 * loop start, so audsrv reports a loop as finished after its first pass: a
 * looping owner counts as playing until stop(), free() or another play()
 * on the channel (which audsrv then allows) replaces it.
 */
static bool sfx_channel_sounding(int channel) {
    AthenaSfx *owner = channel_owner[channel];

    if (!owner || channel_stopped[channel])
        return false;
    if (owner->loop)
        return true;
    return audsrv_is_adpcm_playing(channel, &owner->adpcm) == 1;
}

/*
 * audsrv cannot key a voice off: muting is all that is possible. A muted
 * one-shot voice ends by itself; a muted loop runs until the channel plays
 * something else.
 */
static void sfx_silence_channel(int channel) {
    audsrv_adpcm_set_volume_and_pan(channel, 0, 0);
    channel_volume[channel] = 0;
    channel_pan[channel] = 0;
}

void athena_sfx_destroy(AthenaSfx *sfx) {
    bool current;

    if (!sfx)
        return;
    current = sfx_is_current(sfx);
    for (int i = 0; i < ATHENA_SOUND_CHANNELS; i++) {
        if (channel_owner[i] != sfx)
            continue;
        /* It would go on reading freed memory, which the next upload overwrites. */
        if (current && sfx_channel_sounding(i))
            sfx_silence_channel(i);
        sfx_release_channel(i);
    }
    /* The IOP matches samples by address: a later allocation may reuse it. */
    if (current)
        audsrv_free_adpcm(&sfx->adpcm);
    sfx_unlink(sfx);
    free(sfx->path);
    free(sfx);
}

/* Uploads the sample again if an IOP reset dropped it. */
static int sfx_ensure_resident(AthenaSfx *sfx) {
    int status;

    if (sfx_is_current(sfx))
        return ATHENA_SOUND_OK;
    sound_set_detail(NULL);
    /* Its old list entry belonged to the previous session. */
    sfx_unlink(sfx);
    status = sfx_upload(sfx);
    if (status < 0)
        dbgprintf("[Sound] reloading %s after an IOP reset failed: %s\n", sfx->path,
            athena_sound_result_string(status));
    return status;
}

static bool sfx_channel_idle(int channel) {
    AthenaSfx *owner = channel_owner[channel];

    if (!owner)
        return true;
    if (owner->loop ? !channel_stopped[channel] :
        audsrv_is_adpcm_playing(channel, &owner->adpcm) == 1)
        return false;
    sfx_release_channel(channel);
    return true;
}

int athena_sfx_find_channel(void) {
    if (athena_sound_ensure() < 0)
        return -1;
    for (int i = 0; i < ATHENA_SOUND_CHANNELS; i++) {
        if (sfx_channel_idle(i))
            return i;
    }
    return -1;
}

static int sfx_effective_volume(const AthenaSfx *sfx) {
    return (sfx->volume * master_volume + ATHENA_SOUND_MAX_VOLUME / 2) / ATHENA_SOUND_MAX_VOLUME;
}

static void sfx_apply_levels(const AthenaSfx *sfx, int channel) {
    int volume = sfx_effective_volume(sfx);

    if (channel_volume[channel] != volume || channel_pan[channel] != sfx->pan) {
        audsrv_adpcm_set_volume_and_pan(channel, volume, sfx->pan);
        channel_volume[channel] = volume;
        channel_pan[channel] = sfx->pan;
    }
}

/* Returns the channel, -1 when it is busy, or an AthenaSoundResult. */
static int sfx_start(AthenaSfx *sfx, int channel) {
    int result;

    sfx_apply_levels(sfx, channel);
    result = audsrv_ch_play_adpcm(channel, &sfx->adpcm);
    if (result == channel) {
        channel_owner[channel] = sfx;
        channel_stopped[channel] = false;
        return channel;
    }
    if (result == -AUDSRV_ERR_NO_MORE_CHANNELS)
        return -1;
    dbgprintf("[Sound] audsrv_ch_play_adpcm(%d) failed: %d\n", channel, result);
    return ATHENA_SOUND_ERR_IOP;
}

int athena_sfx_play(AthenaSfx *sfx, int channel) {
    int result;

    if (!sfx || channel >= ATHENA_SOUND_CHANNELS)
        return ATHENA_SOUND_ERR_ARGS;
    result = sfx_ensure_resident(sfx);
    if (result < 0)
        return result;
    /* audsrv would re-key a loop past its first pass: for us it is busy. */
    if (channel >= 0)
        return sfx_channel_idle(channel) ? sfx_start(sfx, channel) : -1;

    /*
     * A channel may still be busy with a sample that was freed while it
     * played, so a refused channel is not the end of the search.
     */
    for (int i = 0; i < ATHENA_SOUND_CHANNELS; i++) {
        if (!sfx_channel_idle(i))
            continue;
        result = sfx_start(sfx, i);
        if (result != -1)
            return result;
    }
    return -1;
}

int athena_sfx_is_playing(AthenaSfx *sfx, int channel) {
    if (!sfx || channel < 0 || channel >= ATHENA_SOUND_CHANNELS)
        return ATHENA_SOUND_ERR_ARGS;
    /* Dropped by an IOP reset: nothing of it can be playing. */
    if (!sfx_is_current(sfx))
        return 0;
    if (channel_owner[channel] == sfx)
        return sfx_channel_sounding(channel);
    /* Loops are only known through their owner; one-shots by the IOP. */
    return !sfx->loop && audsrv_is_adpcm_playing(channel, &sfx->adpcm) == 1;
}

int athena_sfx_stop(AthenaSfx *sfx, int channel) {
    if (!sfx || channel >= ATHENA_SOUND_CHANNELS)
        return ATHENA_SOUND_ERR_ARGS;
    if (!sfx_is_current(sfx))
        return ATHENA_SOUND_OK;
    for (int i = 0; i < ATHENA_SOUND_CHANNELS; i++) {
        if ((channel >= 0 && i != channel) || channel_owner[i] != sfx || channel_stopped[i])
            continue;
        sfx_silence_channel(i);
        channel_stopped[i] = true;
    }
    return ATHENA_SOUND_OK;
}

uint32_t athena_sfx_get_length(const AthenaSfx *sfx) {
    if (!sfx || sfx->rate <= 0)
        return 0;
    return (uint32_t)(((uint64_t)sfx->samples * 1000) / (uint64_t)sfx->rate);
}

int athena_sfx_get_rate(const AthenaSfx *sfx) {
    return sfx ? sfx->rate : 0;
}

bool athena_sfx_get_loop(const AthenaSfx *sfx) {
    return sfx && sfx->loop;
}

int athena_sfx_get_volume(const AthenaSfx *sfx) {
    return sfx ? sfx->volume : 0;
}

int athena_sfx_set_volume(AthenaSfx *sfx, int volume) {
    if (!sfx || volume < 0 || volume > ATHENA_SOUND_MAX_VOLUME)
        return ATHENA_SOUND_ERR_ARGS;
    sfx->volume = volume;
    return ATHENA_SOUND_OK;
}

int athena_sfx_get_pan(const AthenaSfx *sfx) {
    return sfx ? sfx->pan : 0;
}

int athena_sfx_set_pan(AthenaSfx *sfx, int pan) {
    if (!sfx || pan < ATHENA_SOUND_MIN_PAN || pan > ATHENA_SOUND_MAX_PAN)
        return ATHENA_SOUND_ERR_ARGS;
    sfx->pan = pan;
    return ATHENA_SOUND_OK;
}

int athena_sfx_set_master_volume(int volume) {
    if (volume < 0 || volume > ATHENA_SOUND_MAX_VOLUME)
        return ATHENA_SOUND_ERR_ARGS;
    master_volume = volume;
    if (!athena_sound_ready())
        return ATHENA_SOUND_OK;
    /* Voices still sounding follow at once; the others on their next play(). */
    for (int i = 0; i < ATHENA_SOUND_CHANNELS; i++) {
        if (sfx_channel_sounding(i))
            sfx_apply_levels(channel_owner[i], i);
    }
    return ATHENA_SOUND_OK;
}

int athena_sfx_get_master_volume(void) {
    return master_volume;
}

void athena_sfx_get_memory_stats(AthenaSoundMemoryStats *stats) {
    uint32_t used;

    if (!stats)
        return;
    used = sfx_next_address() - SPU_SAMPLE_BASE;
    stats->total = SPU_SAMPLE_END - SPU_SAMPLE_BASE;
    stats->used = used;
    stats->free = stats->total - used;
    stats->wasted = used - resident_bytes;
    stats->samples = resident_count;
}
