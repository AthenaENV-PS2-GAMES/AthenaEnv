#ifndef ATHENA_SPRITE_H
#define ATHENA_SPRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <athena/graphics/view.h>
#include <athena/image.h>
#include <athena/tilemap.h>

/*
 * Sprites: frames of a texture (a spritesheet or atlas), clips that play
 * them in sequence, and the state of a clip being played. Everything but
 * athena_sprite_draw() is plain math on these structs, with no GS access,
 * so it runs and is tested on a host.
 *
 * A C game loop:
 *
 *     AthenaSpriteSheet sheet;
 *     AthenaSpriteGrid grid = { .frame_w = 32, .frame_h = 32,
 *         .texture_w = image->width, .texture_h = image->height };
 *     uint16_t run[8];
 *     float fps12 = 1.0f / 12.0f;
 *     AthenaSpriteAnim anim;
 *     AthenaSpriteDraw pose;
 *
 *     athena_sprite_sheet_init(&sheet);
 *     athena_sprite_sheet_add_grid(&sheet, &grid);
 *     athena_sprite_parse_frames("4-11", sheet.frame_count, run, 8);
 *     int clip = athena_sprite_sheet_add_clip(&sheet, "run", run, 8, &fps12, 1,
 *         ATHENA_SPRITE_LOOP, 0);
 *     athena_sprite_anim_play(&anim, &sheet, clip);
 *     athena_sprite_draw_init(&pose);
 *     pose.origin_x = 0.5f; pose.origin_y = 1.0f;
 *     for (;;) {
 *         athena_sprite_anim_advance(&anim, &sheet, dt, NULL, NULL);
 *         clearScreen(color);
 *         pose.x = player.x; pose.y = player.y;
 *         athena_sprite_draw(image, &sheet, athena_sprite_anim_frame(&anim, &sheet), &pose);
 *         flipScreen();
 *     }
 *
 * Units: frame rectangles in texels, positions in world units (the 2D view
 * of the graphics core applies the camera), durations and time in seconds,
 * rotation in radians (clockwise on screen, y pointing down).
 */

/* Longest frame or clip name kept, with its terminator. */
#define ATHENA_SPRITE_NAME_MAX 64
/* Most frames in a sheet and in a clip: frame indices are 16-bit. */
#define ATHENA_SPRITE_MAX_FRAMES 65535u
/* Shortest frame duration accepted, in seconds. */
#define ATHENA_SPRITE_MIN_DURATION 0.001f
/* Longest frame duration accepted, in seconds (a day). */
#define ATHENA_SPRITE_MAX_DURATION 86400.0f
/* Largest texel coordinate or frame size accepted. */
#define ATHENA_SPRITE_MAX_TEXELS 65536.0f

/* Error codes: negative results of the functions below. */
#define ATHENA_SPRITE_EINVAL (-1)   /* invalid argument */
#define ATHENA_SPRITE_ENOMEM (-2)
#define ATHENA_SPRITE_EEXIST (-3)   /* name already used in the sheet */
#define ATHENA_SPRITE_ERANGE (-4)   /* frame index or count out of range */

/*
 * One frame: a rectangle of the texture. Packers trim transparent borders;
 * the rectangle is then placed at (offset_x, offset_y) inside the untrimmed
 * frame of source_w x source_h, which is what origins and flips refer to,
 * so a trimmed frame draws where the untrimmed one would.
 */
typedef struct {
	float x, y, w, h;
	float offset_x, offset_y;
	float source_w, source_h;
	/* Duration from the sheet file in seconds; 0 when it gives none. */
	float duration;
	/*
	 * The texture holds the frame turned 90 degrees clockwise (TexturePacker
	 * "rotated"): x, y, w and h still describe the frame as shown, and it
	 * covers h x w texels from (x, y). Drawn as two triangles; TileMap
	 * animators cannot show it.
	 */
	bool rotated;
} AthenaSpriteFrame;

typedef enum {
	/* first..last, then again: `loops` cycles, or forever when 0. */
	ATHENA_SPRITE_LOOP,
	/* first..last..first, the ends not repeated: a cycle goes and returns. */
	ATHENA_SPRITE_PINGPONG
} AthenaSpriteMode;

typedef struct {
	char name[ATHENA_SPRITE_NAME_MAX];
	/* Sheet frame indices, in play order. */
	uint16_t *frames;
	/* Seconds each position of `frames` shows. */
	float *durations;
	uint32_t count;
	AthenaSpriteMode mode;
	/* Cycles before the clip ends; 0 plays forever. */
	uint32_t loops;
	/* Sum of `durations`: the length of one pass from first to last. */
	float length;
	/*
	 * Clip played when this one ends (athena_sprite_sheet_set_next()), or
	 * empty. It is looked up by name then, so it may be added later.
	 */
	char next[ATHENA_SPRITE_NAME_MAX];
} AthenaSpriteClip;

/* A rectangle, in texels of an untrimmed frame or in world units. */
typedef struct {
	float x, y, w, h;
} AthenaSpriteRect;

/*
 * A named rectangle per frame (Aseprite slices: hitboxes, attack boxes),
 * in the coordinates of the untrimmed frame. rects[i] belongs to frame i;
 * a width <= 0 means the frame has none.
 */
typedef struct {
	char name[ATHENA_SPRITE_NAME_MAX];
	AthenaSpriteRect *rects;
	uint32_t count;
} AthenaSpriteSlice;

typedef struct {
	AthenaSpriteFrame *frames;
	/* Frame names (packer file names, Aseprite keys); NULL when unnamed. */
	char **frame_names;
	uint32_t frame_count;
	uint32_t frame_capacity;
	AthenaSpriteClip *clips;
	uint32_t clip_count;
	uint32_t clip_capacity;
	AthenaSpriteSlice *slices;
	uint32_t slice_count;
	uint32_t slice_capacity;
	/*
	 * Texels trimmed from every side of each frame's texture rectangle when
	 * drawn: 0.5 keeps bilinear filtering and zoom from sampling the
	 * neighbor frames of an atlas packed without padding.
	 */
	float inset;
} AthenaSpriteSheet;

void athena_sprite_sheet_init(AthenaSpriteSheet *sheet);
/* Frees what the sheet owns; the sheet is empty and usable afterwards. */
void athena_sprite_sheet_clear(AthenaSpriteSheet *sheet);

/*
 * Checks a frame: finite, positive size, non-negative position, trim
 * inside the untrimmed size (a zero source size takes the rectangle's).
 * Normalizes it in place and returns 0, or ATHENA_SPRITE_EINVAL.
 */
int athena_sprite_frame_check(AthenaSpriteFrame *frame);

/*
 * Adds a frame (checked as above) named `name` (may be NULL) and returns its
 * index, or a negative error code.
 */
int athena_sprite_sheet_add_frame(AthenaSpriteSheet *sheet,
	const AthenaSpriteFrame *frame, const char *name);

/*
 * A grid of equal frames, left to right and top to bottom: `margin` texels
 * around the grid, `spacing` between cells. Columns and rows default (0) to
 * as many as fit in the texture; `count` (0 for all) stops early, and
 * `first` skips cells before the first frame. athena_sprite_sheet_add_grid()
 * writes back the columns and rows it used.
 */
typedef struct {
	float frame_w, frame_h;
	float margin, spacing;
	float texture_w, texture_h;
	uint32_t columns, rows;
	uint32_t first, count;
} AthenaSpriteGrid;

/* Adds the frames of a grid; returns how many, or a negative error code. */
int athena_sprite_sheet_add_grid(AthenaSpriteSheet *sheet,
	AthenaSpriteGrid *grid);

/* Index of the frame or clip called `name`, or -1. */
int athena_sprite_sheet_find_frame(const AthenaSpriteSheet *sheet,
	const char *name);
int athena_sprite_sheet_find_clip(const AthenaSpriteSheet *sheet,
	const char *name);

/*
 * Adds (or, with an existing name, replaces) a clip of `count` sheet frames
 * and returns its index, or a negative error code. `durations` holds either
 * one duration for every frame (duration_count 1) or one per frame
 * (duration_count == count); NULL uses each frame's own duration, which
 * must then be set. Durations are clamped to [ATHENA_SPRITE_MIN_DURATION,
 * ATHENA_SPRITE_MAX_DURATION]. Replacing a clip keeps its index, so
 * animations playing it continue from their position (clamped).
 */
int athena_sprite_sheet_add_clip(AthenaSpriteSheet *sheet, const char *name,
	const uint16_t *frames, uint32_t count, const float *durations,
	uint32_t duration_count, AthenaSpriteMode mode, uint32_t loops);

/*
 * Sets the clip played when clip `clip` ends; NULL or "" clears it.
 * Returns 0, or ATHENA_SPRITE_EINVAL.
 */
int athena_sprite_sheet_set_next(AthenaSpriteSheet *sheet, int32_t clip,
	const char *next);

/*
 * Sets the rectangle of slice `name` (created on first use) for frames
 * [first, first + count); a NULL rect or one of width <= 0 removes it.
 * Returns the slice index, or a negative error code.
 */
int athena_sprite_sheet_set_slice(AthenaSpriteSheet *sheet, const char *name,
	uint32_t first, uint32_t count, const AthenaSpriteRect *rect);

/* Index of the slice called `name`, or -1. */
int athena_sprite_sheet_find_slice(const AthenaSpriteSheet *sheet,
	const char *name);

/* The slice's rectangle in frame `frame` (untrimmed texels); false when none. */
bool athena_sprite_sheet_slice(const AthenaSpriteSheet *sheet, int slice,
	uint32_t frame, AthenaSpriteRect *rect);

/*
 * Parses a frame list: indices and inclusive ranges separated by commas,
 * such as "0-3,5,9-7" (a descending range plays backwards). Every index
 * must be below `limit`. Writes at most `max` indices and returns how many
 * the text holds (more than `max` when `out` is too short: call again with
 * a larger array), or ATHENA_SPRITE_EINVAL for bad syntax and
 * ATHENA_SPRITE_ERANGE for an index out of range.
 */
int athena_sprite_parse_frames(const char *text, uint32_t limit,
	uint16_t *out, uint32_t max);

/* ---------------------------------------------------------------------- */
/* Playback                                                               */

typedef enum {
	/* A clip position became current; `position` and `frame` tell which. */
	ATHENA_SPRITE_EVENT_FRAME,
	/* A cycle completed and another one starts. */
	ATHENA_SPRITE_EVENT_LOOP,
	/* The last cycle completed: the animation stops on its last frame. */
	ATHENA_SPRITE_EVENT_END
} AthenaSpriteEventType;

typedef void (*AthenaSpriteEventFunc)(void *opaque,
	AthenaSpriteEventType type, uint32_t position, uint32_t frame);

typedef struct {
	/* Clip index in the sheet, or -1 for a still frame. */
	int32_t clip;
	/* Position in the clip, or the sheet frame without a clip. */
	uint32_t position;
	/* Seconds spent in the current position. */
	float time;
	/* Time multiplier, >= 0. */
	float speed;
	/* Completed cycles. */
	uint32_t cycles;
	/* +1 or -1: the pingpong direction. */
	int8_t direction;
	bool playing;
	bool finished;
	/* The FRAME event of the current position was not reported yet. */
	bool pending_frame;
} AthenaSpriteAnim;

/* A still animation showing sheet frame 0, at speed 1. */
void athena_sprite_anim_init(AthenaSpriteAnim *anim);

/*
 * Starts clip `clip` of `sheet` from its first position. Returns false,
 * changing nothing, when the clip does not exist.
 */
bool athena_sprite_anim_play(AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet, int32_t clip);

/* Shows sheet frame `frame` without a clip; false when out of range. */
bool athena_sprite_anim_show(AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet, uint32_t frame);

/*
 * Jumps to `position` of the current clip, restarting its time; false
 * when out of range or without a clip. It is reported as a FRAME event on
 * the next advance, and a finished clip plays again from there.
 */
bool athena_sprite_anim_seek(AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet, uint32_t position);

/*
 * Advances a playing clip by `dt` seconds (times its speed), calling
 * `func` (may be NULL) for each event in order. Large steps move over
 * several frames and report each one; in a clip that plays forever, whole
 * cycles past the first are skipped at once, reporting one LOOP (a stall
 * does not replay every frame). Invalid or negative
 * steps do nothing. Returns true when the shown frame changed.
 */
bool athena_sprite_anim_advance(AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet, float dt, AthenaSpriteEventFunc func,
	void *opaque);

/* Sheet frame shown, always valid for a sheet with frames. */
uint32_t athena_sprite_anim_frame(const AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet);

/* Progress through the whole clip (all cycles when finite), 0..1. */
float athena_sprite_anim_progress(const AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet);

/* ---------------------------------------------------------------------- */
/* Drawing                                                                */

typedef struct {
	/* World position of the origin. */
	float x, y;
	/* Origin inside the untrimmed frame, 0..1: (0.5, 1) is bottom center. */
	float origin_x, origin_y;
	float scale_x, scale_y;
	/* Radians, about the origin. */
	float rotation;
	/* Mirror the frame inside its untrimmed box. */
	bool flip_x, flip_y;
	/* PS2 color: 0x80 per channel is the texture unchanged. */
	uint32_t color;
} AthenaSpriteDraw;

/* Origin (0, 0), scale 1, no rotation nor flip, neutral color. */
void athena_sprite_draw_init(AthenaSpriteDraw *draw);

/* Where a frame lands: what athena_image_draw() takes. */
typedef struct {
	float x, y, w, h;
	float u1, v1, u2, v2;
	/* About the rectangle's center, as athena_image_draw() rotates. */
	float angle;
} AthenaSpriteQuad;

/*
 * Places `frame` as `draw` asks, its texture rectangle shrunk by `inset`
 * texels on each side; false when there is nothing to draw.
 */
bool athena_sprite_quad(const AthenaSpriteFrame *frame,
	const AthenaSpriteDraw *draw, float inset, AthenaSpriteQuad *quad);

/*
 * The four corners of a frame drawn as `draw` asks, in the order top-left,
 * top-right, bottom-left, bottom-right of the frame as shown, with their
 * texture coordinates: flips, trim, origin, scale and rotation applied, and
 * frames turned in the atlas turned back. For drawing as triangles.
 */
typedef struct {
	float x[4], y[4];
	float u[4], v[4];
} AthenaSpriteCorners;

bool athena_sprite_corners(const AthenaSpriteFrame *frame,
	const AthenaSpriteDraw *draw, float inset, AthenaSpriteCorners *corners);

/*
 * World rectangle of `rect` (untrimmed frame texels, as slices are) for a
 * frame drawn as `draw` asks, ignoring rotation: flips mirror it inside
 * the frame, the origin and scale place it. false for a scale <= 0.
 */
bool athena_sprite_place_rect(const AthenaSpriteFrame *frame,
	const AthenaSpriteDraw *draw, const AthenaSpriteRect *rect,
	AthenaSpriteRect *out);

/*
 * Draws sheet frame `frame` of `image` through the current 2D view (the
 * camera). Frames past the image are clipped by the GS, not checked. A
 * frame turned in the atlas goes as two triangles.
 */
void athena_sprite_draw(AthenaImage *image, const AthenaSpriteSheet *sheet,
	uint32_t frame, const AthenaSpriteDraw *draw);

/*
 * Many sprites in few GS packets: consecutive sprites of one texture go out
 * together (the texture state once per chunk instead of once per sprite),
 * skipping those outside the current camera's viewport. Rotated sprites
 * are drawn one by one, in order. Main thread only.
 *
 *     AthenaSpriteBatch batch;
 *     athena_sprite_batch_begin(&batch);
 *     for (...) athena_sprite_batch_add(&batch, image, &sheet, frame, &pose);
 *     athena_sprite_batch_end(&batch);
 */
#define ATHENA_SPRITE_BATCH_CHUNK 128

typedef struct {
	AthenaImage *image;
	prim_tex_sprite chunk[ATHENA_SPRITE_BATCH_CHUNK];
	int queued;
	bool cull;
	AthenaViewCuller culler;
	/* Sprites sent and skipped outside the viewport since begin. */
	uint32_t drawn, culled;
} AthenaSpriteBatch;

void athena_sprite_batch_begin(AthenaSpriteBatch *batch);
void athena_sprite_batch_add(AthenaSpriteBatch *batch, AthenaImage *image,
	const AthenaSpriteSheet *sheet, uint32_t frame, const AthenaSpriteDraw *draw);
/* Sends what is queued; the batch may be added to again afterwards. */
void athena_sprite_batch_end(AthenaSpriteBatch *batch);

/*
 * Debug overlay of a frame drawn as `draw` asks, through the 2D view: its
 * outline (green), a cross on its origin (red) and the outlines of the
 * slices it has in that frame (yellow).
 */
void athena_sprite_draw_debug(const AthenaSpriteSheet *sheet, uint32_t frame,
	const AthenaSpriteDraw *draw);

/* ---------------------------------------------------------------------- */
/* Batches over TileMap sprite buffers                                    */

/*
 * One animated TileMap sprite. The animator writes only its texture
 * coordinates (u1, v1, u2, v2): position, size, color and depth stay the
 * application's. Trim offsets are not applied, and frames turned in the
 * atlas cannot be shown (a TileMap sprite has no rotated texture).
 */
typedef struct {
	AthenaSpriteAnim anim;
	/* Sprite index in the buffer. */
	uint32_t sprite;
	/* Sheet frame written last; UINT32_MAX forces the next write. */
	uint32_t shown;
	bool flip_x, flip_y;
} AthenaSpriteAnimatorEntry;

#define ATHENA_SPRITE_NOT_SHOWN UINT32_MAX

/*
 * Advances every entry by `dt` (entry speeds apply), without events, and
 * writes the texture coordinates of the entries whose frame or flip changed
 * into `sprites` (NULL only advances). Entries past `sprite_count` are
 * skipped. Returns how many sprites were written.
 */
uint32_t athena_sprite_animator_update(AthenaSpriteAnimatorEntry *entries,
	uint32_t count, const AthenaSpriteSheet *sheet, float dt,
	AthenaTileSprite *sprites, uint32_t sprite_count);

/* ---------------------------------------------------------------------- */
/* Sheet files and background loading                                     */

/*
 * `name` relative to the directory of `file` (malloc'd), as sheet files
 * name their texture; absolute names and devices ("mass:") are kept.
 */
char *athena_sprite_path_join(const char *file, const char *name);

/*
 * The texture name of an Aseprite or TexturePacker file (meta.image) found
 * by scanning its text, without parsing JSON (malloc'd), or NULL when it
 * is not a plain string. The parsed file stays the reference.
 */
char *athena_sprite_find_meta_image(const char *text, size_t length);

/* Largest sheet file read, in bytes. */
#define ATHENA_SPRITE_LOAD_MAX_BYTES (2u * 1024u * 1024u)

/* Results of a loading job. */
#define ATHENA_SPRITE_LOAD_OK 0
#define ATHENA_SPRITE_LOAD_OPEN (-1)       /* the sheet file cannot be opened */
#define ATHENA_SPRITE_LOAD_TOO_LARGE (-2)
#define ATHENA_SPRITE_LOAD_READ (-3)
#define ATHENA_SPRITE_LOAD_IMAGE (-4)      /* the texture: see image.error */
#define ATHENA_SPRITE_LOAD_NOMEM (-5)
#define ATHENA_SPRITE_LOAD_CANCELLED (-6)

/*
 * What a loading job produces (athena_job_data()). The script thread takes
 * `text` and `image` (setting them to NULL / releasing them) to build the
 * sheet; what it leaves is freed with the job.
 */
typedef struct {
	char *json_path;
	/* Given, or found in the file with image_from_json. */
	char *image_path;
	bool image_from_json;
	char *text;
	size_t length;
	AthenaImageBuffer image;
	bool has_image;
} AthenaSpriteLoad;

struct AthenaJob;

/*
 * Reads `json_path` (may be NULL) and decodes `image_path` (may be NULL;
 * with `image_from_json`, the file's meta.image) on a worker of the job
 * pool (thread module). NULL when it cannot be queued.
 */
struct AthenaJob *athena_sprite_load_submit(const char *json_path,
	const char *image_path, bool image_from_json);

#endif
