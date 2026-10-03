#include <stdlib.h>

#include <athena/sound.h>

void athena_sound_set_volume(int volume)
{
    sound_setvolume(volume);
}

int athena_sound_find_channel(void)
{
    return sound_sfx_find_channel();
}

AthenaSound *athena_sound_load(const char *path)
{
    AthenaSound *sound;

    if (!path)
        return NULL;

    sound = calloc(1, sizeof(*sound));
    if (!sound)
        return NULL;

    sound->stream = sound_load(path);
    if (!sound->stream) {
        free(sound);
        return NULL;
    }

    return sound;
}

void athena_sound_free(AthenaSound *sound)
{
    if (!sound)
        return;

    if (sound->stream)
        sound_free(sound->stream);

    free(sound);
}

void athena_sound_play(AthenaSound *sound)
{
    if (sound && sound->stream)
        sound_play(sound->stream);
}

void athena_sound_pause(void)
{
    sound_pause();
}

void athena_sound_rewind(AthenaSound *sound)
{
    if (sound && sound->stream)
        sound_rewind(sound->stream);
}

bool athena_sound_is_playing(const AthenaSound *sound)
{
    return sound && sound->stream && is_sound_playing(sound->stream);
}

int athena_sound_get_duration(const AthenaSound *sound)
{
    return (sound && sound->stream) ? sound_get_duration(sound->stream) : 0;
}

void athena_sound_set_position(AthenaSound *sound, uint32_t position_ms)
{
    if (sound && sound->stream)
        sound_set_position(sound->stream, (int)position_ms);
}

int athena_sound_get_position(const AthenaSound *sound)
{
    return (sound && sound->stream) ? sound_get_position(sound->stream) : 0;
}

void athena_sound_set_loop(AthenaSound *sound, bool loop)
{
    if (sound && sound->stream)
        sound->stream->loop = loop;
}

bool athena_sound_get_loop(const AthenaSound *sound)
{
    return sound && sound->stream && sound->stream->loop;
}

AthenaSfx *athena_sfx_load(const char *path)
{
    AthenaSfx *sfx;

    if (!path)
        return NULL;

    sfx = calloc(1, sizeof(*sfx));
    if (!sfx)
        return NULL;

    sfx->sfx = sound_sfx_load(path);
    if (!sfx->sfx) {
        free(sfx);
        return NULL;
    }

    return sfx;
}

void athena_sfx_free(AthenaSfx *sfx)
{
    if (!sfx)
        return;

    if (sfx->sfx)
        sound_sfx_free(sfx->sfx);

    free(sfx);
}

int athena_sfx_play(AthenaSfx *sfx, int channel)
{
    return (sfx && sfx->sfx) ? sound_sfx_play(channel, sfx->sfx) : -1;
}

bool athena_sfx_is_playing(const AthenaSfx *sfx, int channel)
{
    return sfx && sfx->sfx && sound_sfx_is_playing(sfx->sfx, channel);
}

int athena_sfx_get_length(const AthenaSfx *sfx)
{
    return (sfx && sfx->sfx) ? sound_sfx_length(sfx->sfx) : 0;
}

uint32_t athena_sfx_get_volume(const AthenaSfx *sfx)
{
    return (sfx && sfx->sfx) ? sfx->sfx->volume : 0;
}

void athena_sfx_set_volume(AthenaSfx *sfx, uint32_t volume)
{
    if (sfx && sfx->sfx)
        sfx->sfx->volume = volume;
}

uint32_t athena_sfx_get_pan(const AthenaSfx *sfx)
{
    return (sfx && sfx->sfx) ? sfx->sfx->pan : 0;
}

void athena_sfx_set_pan(AthenaSfx *sfx, uint32_t pan)
{
    if (sfx && sfx->sfx)
        sfx->sfx->pan = pan;
}

bool athena_sfx_get_loop(const AthenaSfx *sfx)
{
    return sfx && sfx->sfx && sfx->sfx->sound.loop;
}

void athena_sfx_set_loop(AthenaSfx *sfx, bool loop)
{
    if (sfx && sfx->sfx)
        sfx->sfx->sound.loop = loop;
}

int athena_sfx_get_pitch(const AthenaSfx *sfx)
{
    return (sfx && sfx->sfx) ? sound_sfx_get_pitch(sfx->sfx) : 0;
}

void athena_sfx_set_pitch(AthenaSfx *sfx, int pitch)
{
    if (sfx && sfx->sfx)
        sound_sfx_set_pitch(sfx->sfx, pitch);
}
