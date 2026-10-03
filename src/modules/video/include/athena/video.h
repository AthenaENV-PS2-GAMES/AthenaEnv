#ifndef ATHENA_VIDEO_H
#define ATHENA_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include <athena/graphics.h>

/*
 * MPEG-1/2 elementary video streams decoded by the IPU through libmpeg.
 *
 * libmpeg keeps its decoder state in globals, so only one video can be open
 * at a time: athena_video_create() fails with ATHENA_VIDEO_ERROR_BUSY while
 * another one exists. All functions must run on the thread that draws.
 *
 * Frames are decoded ahead by a "Video decoder" thread one priority below
 * the default, which runs while the drawing thread waits (vsync). While a
 * video is open, the IPU, DMA channels 3/4 and the scratchpad belong to it.
 */

typedef struct AthenaVideo AthenaVideo;

/* Largest frame the GS can sample as one texture. */
#define ATHENA_VIDEO_MAX_SIZE 1024

typedef enum AthenaVideoError {
	ATHENA_VIDEO_OK = 0,
	ATHENA_VIDEO_ERROR_ARGUMENT,
	ATHENA_VIDEO_ERROR_BUSY,
	ATHENA_VIDEO_ERROR_OPEN,
	ATHENA_VIDEO_ERROR_MEMORY,
	/* Not an MPEG-1/2 elementary video stream, or no decodable picture. */
	ATHENA_VIDEO_ERROR_FORMAT,
	/* Valid stream the decoder cannot play: size, chroma or frame rate. */
	ATHENA_VIDEO_ERROR_UNSUPPORTED,
	/* The decoder thread could not be created. */
	ATHENA_VIDEO_ERROR_THREAD,
} AthenaVideoError;

typedef enum AthenaVideoState {
	ATHENA_VIDEO_STOPPED = 0,
	ATHENA_VIDEO_PLAYING,
	ATHENA_VIDEO_PAUSED,
	ATHENA_VIDEO_ENDED,
} AthenaVideoState;

/*
 * Opens `path` and decodes its first frame. Returns NULL on failure and, when
 * `error` is not NULL, stores the reason there.
 */
AthenaVideo *athena_video_create(const char *path, AthenaVideoError *error);
void athena_video_destroy(AthenaVideo *video);
/* Human-readable description of `error`. */
const char *athena_video_error_message(AthenaVideoError error);
/* Stable identifier of `error`, e.g. "unsupported_format". */
const char *athena_video_error_name(AthenaVideoError error);

void athena_video_play(AthenaVideo *video);
void athena_video_pause(AthenaVideo *video);
/* Stops playback and rewinds to the beginning of the stream. */
void athena_video_stop(AthenaVideo *video);
/*
 * Advances playback by the time elapsed since the previous call, decoding
 * the frames that became due (late frames are decoded and dropped, up to a
 * small limit). Returns true when the displayed frame changed. Call once per
 * rendered frame.
 */
bool athena_video_update(AthenaVideo *video);
/*
 * Draws the current frame. A width or height <= 0 uses the display size.
 * The macroblock padding below/right of the picture is never drawn.
 */
void athena_video_draw(AthenaVideo *video, float x, float y, float width,
	float height);

typedef struct AthenaVideoDrawOptions {
	/* Destination size; <= 0 uses the size of the source rectangle. */
	float width;
	float height;
	/* Source rectangle in picture pixels; end <= 0 means the picture edge. */
	float startx;
	float starty;
	float endx;
	float endy;
	/* Rotation in radians around the destination's center. */
	float angle;
	/* Tint multiplied with the texture; 0x80808080 draws it unchanged. */
	uint32_t color;
} AthenaVideoDrawOptions;

/* Fills `options` with the defaults: whole picture, unscaled, untinted. */
void athena_video_draw_options_init(AthenaVideoDrawOptions *options);
/*
 * Draws the current frame with `options`. Returns false without drawing
 * when the source rectangle is empty or outside the picture.
 */
bool athena_video_draw_ex(AthenaVideo *video, float x, float y,
	const AthenaVideoDrawOptions *options);

/*
 * Syncing to an external clock, normally an audio track (Sound stream):
 *
 *   athena_video_set_synced(video, true);
 *   athena_sound_stream_play(audio, 0);
 *   athena_video_play(video);
 *   // every frame:
 *   athena_video_update_synced(video,
 *       athena_sound_stream_get_position(audio),
 *       athena_sound_stream_ended(audio));
 *
 * The clock's position picks the picture (picture n from n frame periods).
 * When it moves back (a loop, a rewind) the video restarts from the start,
 * counting a loop when loop is set; the decoder no longer loops by itself.
 * With loop set, a video shorter than its clock holds its last picture
 * until the clock wraps. Once the clock has ended, the remaining pictures
 * play on the EE clock. The caller keeps play/pause/stop of both in step.
 */
void athena_video_set_synced(AthenaVideo *video, bool synced);
bool athena_video_is_synced(const AthenaVideo *video);
/* athena_video_update() driven by `clock_ms`; like it when not synced. */
bool athena_video_update_synced(AthenaVideo *video, uint32_t clock_ms,
	bool clock_ended);

/* Events raised by athena_video_update(), read with athena_video_take_events(). */
#define ATHENA_VIDEO_EVENT_LOOP 0x1u /* playback restarted from the beginning */
#define ATHENA_VIDEO_EVENT_END  0x2u /* playback reached the end (not looping) */

/* Returns the events raised since the previous call and clears them. */
unsigned int athena_video_take_events(AthenaVideo *video);
/* Times playback looped since it was opened, stopped or restarted. */
int athena_video_get_loop_count(const AthenaVideo *video);

/* Picture size from the sequence header. */
int athena_video_get_width(const AthenaVideo *video);
int athena_video_get_height(const AthenaVideo *video);
/* Decoded frame size: the picture rounded up to whole macroblocks. */
int athena_video_get_coded_width(const AthenaVideo *video);
int athena_video_get_coded_height(const AthenaVideo *video);
float athena_video_get_fps(const AthenaVideo *video);
AthenaVideoState athena_video_get_state(const AthenaVideo *video);
bool athena_video_is_ready(const AthenaVideo *video);
bool athena_video_is_ended(const AthenaVideo *video);
bool athena_video_is_playing(const AthenaVideo *video);
bool athena_video_get_loop(const AthenaVideo *video);
void athena_video_set_loop(AthenaVideo *video, bool loop);
/* Index of the picture shown: 0 is the first, again after a rewind or loop. */
int athena_video_get_current_frame(const AthenaVideo *video);
/*
 * Surface holding the current frame (coded size, 16x16 macroblock layout),
 * owned by the video and valid until athena_video_destroy(). NULL before
 * the first frame.
 */
GSSURFACE *athena_video_get_texture(AthenaVideo *video);

typedef struct AthenaVideoInfo {
	/* Picture size and its macroblock-aligned decoded size. */
	int width;
	int height;
	int coded_width;
	int coded_height;
	float fps;
	/* Whole frames in the file (two field pictures make one frame). */
	int frames;
	float duration;
	bool mpeg2;
	bool progressive;
	/* MPEG_CHROMA_FORMAT_* value: 1 is 4:2:0. */
	int chroma_format;
	/* Whether athena_video_create() accepts the stream (see its errors). */
	bool supported;
} AthenaVideoInfo;

/*
 * Reads the stream's headers without the decoder, so it works while another
 * video is open. Reads the whole file to count the frames: slow on disc.
 * Returns ATHENA_VIDEO_OK, _ERROR_ARGUMENT, _ERROR_OPEN, _ERROR_MEMORY, or
 * _ERROR_FORMAT when no sequence header is found.
 */
AthenaVideoError athena_video_probe(const char *path, AthenaVideoInfo *info);

#endif
