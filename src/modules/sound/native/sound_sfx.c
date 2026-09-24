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
/* audsrv places samples from 0x5010 up to the end of the 2 MiB SPU2 RAM. */
#define ADPCM_MAX_SIZE (2 * 1024 * 1024 - 0x5010)
/* Not yet sent to the channel: forces the first volume/pan update. */
#define CHANNEL_LEVEL_UNKNOWN INT_MIN

struct AthenaSfx {
    /* Its address is the sample id on the IOP side: it must not move. */
    audsrv_adpcm_t adpcm;
    /* audsrv session the sample was uploaded to. */
    uint32_t generation;
    uint32_t samples;
    int rate;
    int volume;
    int pan;
    bool loop;
};

static AthenaSfx *channel_owner[ATHENA_SOUND_CHANNELS];
static int channel_volume[ATHENA_SOUND_CHANNELS];
static int channel_pan[ATHENA_SOUND_CHANNELS];

static void sfx_forget_channel_levels(void) {
    for (int i = 0; i < ATHENA_SOUND_CHANNELS; i++) {
        channel_volume[i] = CHANNEL_LEVEL_UNKNOWN;
        channel_pan[i] = CHANNEL_LEVEL_UNKNOWN;
    }
}

void sound_sfx_audsrv_started(void) {
    memset(channel_owner, 0, sizeof(channel_owner));
    sfx_forget_channel_levels();
}

static bool sfx_is_current(const AthenaSfx *sfx) {
    return athena_sound_ready() && sfx->generation == sound_iop_generation();
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
    if (size > ADPCM_MAX_SIZE) {
        fclose(file);
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

AthenaSfx *athena_sfx_load(const char *path, int *result) {
    AdpcmHeader header;
    AthenaSfx *sfx;
    uint8_t *buffer = NULL;
    int size = 0;
    int status;

    if (!path || !path[0]) {
        status = ATHENA_SOUND_ERR_ARGS;
        goto fail;
    }
    status = athena_sound_ensure();
    if (status < 0)
        goto fail;
    status = sfx_read_file(path, &buffer, &size);
    if (status < 0)
        goto fail;
    memcpy(&header, buffer, sizeof(header));
    if (memcmp(header.magic, "APCM", 4) != 0 || header.pitch == 0) {
        free(buffer);
        status = ATHENA_SOUND_ERR_FORMAT;
        goto fail;
    }
    sfx = calloc(1, sizeof(*sfx));
    if (!sfx) {
        free(buffer);
        status = ATHENA_SOUND_ERR_MEMORY;
        goto fail;
    }

    status = audsrv_load_adpcm(&sfx->adpcm, buffer, size);
    /* The IOP keeps its own copy in SPU2 RAM; the EE buffer is not needed. */
    free(buffer);
    sfx->adpcm.buffer = NULL;
    if (status != AUDSRV_ERR_NOERROR) {
        dbgprintf("[Sound] audsrv_load_adpcm(%s) failed: %d\n", path, status);
        free(sfx);
        status = status == -AUDSRV_ERR_OUT_OF_MEMORY ?
            ATHENA_SOUND_ERR_SPU_MEMORY : ATHENA_SOUND_ERR_IOP;
        goto fail;
    }

    sfx->generation = sound_iop_generation();
    sfx->samples = header.samples;
    sfx->rate = (int)(((uint64_t)header.pitch * 48000) / 4096);
    sfx->loop = header.loop != 0;
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

void athena_sfx_destroy(AthenaSfx *sfx) {
    if (!sfx)
        return;
    for (int i = 0; i < ATHENA_SOUND_CHANNELS; i++) {
        if (channel_owner[i] == sfx)
            channel_owner[i] = NULL;
    }
    /* The IOP matches samples by address: a later allocation may reuse it. */
    if (sfx_is_current(sfx))
        audsrv_free_adpcm(&sfx->adpcm);
    free(sfx);
}

static bool sfx_channel_idle(int channel) {
    AthenaSfx *owner = channel_owner[channel];

    if (!owner)
        return true;
    if (audsrv_is_adpcm_playing(channel, &owner->adpcm) == 1)
        return false;
    channel_owner[channel] = NULL;
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

/* Returns the channel, -1 when it is busy, or an AthenaSoundResult. */
static int sfx_start(AthenaSfx *sfx, int channel) {
    int result;

    if (channel_volume[channel] != sfx->volume || channel_pan[channel] != sfx->pan) {
        audsrv_adpcm_set_volume_and_pan(channel, sfx->volume, sfx->pan);
        channel_volume[channel] = sfx->volume;
        channel_pan[channel] = sfx->pan;
    }
    result = audsrv_ch_play_adpcm(channel, &sfx->adpcm);
    if (result == channel) {
        channel_owner[channel] = sfx;
        return channel;
    }
    if (result == -AUDSRV_ERR_NO_MORE_CHANNELS)
        return -1;
    dbgprintf("[Sound] audsrv_ch_play_adpcm(%d) failed: %d\n", channel, result);
    return ATHENA_SOUND_ERR_IOP;
}

int athena_sfx_play(AthenaSfx *sfx, int channel) {
    if (!sfx || channel >= ATHENA_SOUND_CHANNELS)
        return ATHENA_SOUND_ERR_ARGS;
    if (!sfx_is_current(sfx))
        return ATHENA_SOUND_ERR_STALE;
    if (channel >= 0)
        return sfx_start(sfx, channel);

    /*
     * A channel may still be busy with a sample that was freed while it
     * played, so a refused channel is not the end of the search.
     */
    for (int i = 0; i < ATHENA_SOUND_CHANNELS; i++) {
        int result;

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
    if (!sfx_is_current(sfx))
        return ATHENA_SOUND_ERR_STALE;
    return audsrv_is_adpcm_playing(channel, &sfx->adpcm) == 1;
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
