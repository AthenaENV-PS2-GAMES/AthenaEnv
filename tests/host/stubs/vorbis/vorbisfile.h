/* Host stub: Ogg Vorbis is not decoded on the host (every open fails). */
#pragma once
#include <stdio.h>
typedef long long ogg_int64_t;
typedef struct { int dummy; } OggVorbis_File;
typedef struct { int channels; long rate; } vorbis_info;
typedef struct { int dummy; } ov_callbacks;
static ov_callbacks OV_CALLBACKS_DEFAULT;
#define OV_HOLE (-3)
static inline int ov_open_callbacks(void *f, OggVorbis_File *v, const char *i, long n, ov_callbacks c) { (void)f;(void)v;(void)i;(void)n;(void)c; return -1; }
static inline vorbis_info *ov_info(OggVorbis_File *v, int l) { (void)v;(void)l; return 0; }
static inline ogg_int64_t ov_pcm_total(OggVorbis_File *v, int l) { (void)v;(void)l; return 0; }
static inline int ov_pcm_seek(OggVorbis_File *v, ogg_int64_t p) { (void)v;(void)p; return -1; }
static inline ogg_int64_t ov_pcm_tell(OggVorbis_File *v) { (void)v; return 0; }
static inline long ov_read(OggVorbis_File *v, char *b, int n, int e, int w, int s, int *sec) { (void)v;(void)b;(void)n;(void)e;(void)w;(void)s;(void)sec; return 0; }
static inline int ov_clear(OggVorbis_File *v) { (void)v; return 0; }
