#ifndef ATHENA_SOUND_FACADE_H
#define ATHENA_SOUND_FACADE_H

#include <stdbool.h>
#include <stdint.h>

#include <sound.h>

typedef struct AthenaSound {
    SoundStream *stream;
} AthenaSound;

typedef struct AthenaSfx {
    Sfx *sfx;
} AthenaSfx;

void athena_sound_set_volume(int volume);
int athena_sound_find_channel(void);

AthenaSound *athena_sound_load(const char *path);
void athena_sound_free(AthenaSound *sound);
void athena_sound_play(AthenaSound *sound);
void athena_sound_pause(void);
void athena_sound_rewind(AthenaSound *sound);
bool athena_sound_is_playing(const AthenaSound *sound);
int athena_sound_get_duration(const AthenaSound *sound);
void athena_sound_set_position(AthenaSound *sound, uint32_t position_ms);
int athena_sound_get_position(const AthenaSound *sound);
void athena_sound_set_loop(AthenaSound *sound, bool loop);
bool athena_sound_get_loop(const AthenaSound *sound);

AthenaSfx *athena_sfx_load(const char *path);
void athena_sfx_free(AthenaSfx *sfx);
int athena_sfx_play(AthenaSfx *sfx, int channel);
bool athena_sfx_is_playing(const AthenaSfx *sfx, int channel);
int athena_sfx_get_length(const AthenaSfx *sfx);
uint32_t athena_sfx_get_volume(const AthenaSfx *sfx);
void athena_sfx_set_volume(AthenaSfx *sfx, uint32_t volume);
uint32_t athena_sfx_get_pan(const AthenaSfx *sfx);
void athena_sfx_set_pan(AthenaSfx *sfx, uint32_t pan);
bool athena_sfx_get_loop(const AthenaSfx *sfx);
void athena_sfx_set_loop(AthenaSfx *sfx, bool loop);
int athena_sfx_get_pitch(const AthenaSfx *sfx);
void athena_sfx_set_pitch(AthenaSfx *sfx, int pitch);

#endif /* ATHENA_SOUND_FACADE_H */
