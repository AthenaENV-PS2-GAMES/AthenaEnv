/* Host stub: audsrv EE API; the tests simulate the IOP side. */
#pragma once
struct audsrv_fmt_t { int freq; int bits; int channels; };
typedef struct { int size; void *buffer; int pitch; int loop; int channels; } audsrv_adpcm_t;
#define AUDSRV_ERR_NOERROR 0
#define AUDSRV_ERR_OUT_OF_MEMORY 0x0004
#define AUDSRV_ERR_NO_MORE_CHANNELS 0x0007
/* Streams (sound_stream.c). */
int audsrv_set_format(struct audsrv_fmt_t *fmt);
int audsrv_available(void);
int audsrv_queued(void);
int audsrv_play_audio(const char *buf, int len);
int audsrv_stop_audio(void);
int audsrv_set_volume(int v);
/* ADPCM voices (sound_sfx.c). */
int audsrv_load_adpcm(audsrv_adpcm_t *adpcm, void *buffer, int size);
int audsrv_free_adpcm(audsrv_adpcm_t *adpcm);
int audsrv_ch_play_adpcm(int ch, audsrv_adpcm_t *adpcm);
int audsrv_is_adpcm_playing(int ch, audsrv_adpcm_t *adpcm);
int audsrv_adpcm_set_volume_and_pan(int ch, int volume, int pan);
