#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <athena/sprite.h>

/*
 * Sheets, clips and playback (athena/sprite.h): math on the structs only,
 * no GS access, so tests/host runs it as is. sprite_gs.c draws.
 */

/* Frames a single advance may step over before the rest is dropped. */
#define SPRITE_MAX_STEPS 4096
/* Clips a single advance may go through by `next` (a cycle of short clips). */
#define SPRITE_MAX_CHAIN 16

/*
 * Exponent all ones: Infinity or NaN. The EE's FPU has neither, so values
 * that came from doubles are checked by their bits, not by comparing.
 */
static bool finite_bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return (bits & 0x7F800000u) != 0x7F800000u;
}

static bool in_range(float value, float low, float high)
{
	return finite_bits(value) && value >= low && value <= high;
}

static bool name_valid(const char *name)
{
	return name && name[0] && strlen(name) < ATHENA_SPRITE_NAME_MAX;
}

/* ---------------------------------------------------------------------- */
/* Sheets                                                                 */

void athena_sprite_sheet_init(AthenaSpriteSheet *sheet)
{
	memset(sheet, 0, sizeof(*sheet));
}

static void clip_free(AthenaSpriteClip *clip)
{
	free(clip->frames);
	free(clip->durations);
	clip->frames = NULL;
	clip->durations = NULL;
}

void athena_sprite_sheet_clear(AthenaSpriteSheet *sheet)
{
	uint32_t i;

	if (sheet->frame_names) {
		for (i = 0; i < sheet->frame_count; i++)
			free(sheet->frame_names[i]);
	}
	for (i = 0; i < sheet->clip_count; i++)
		clip_free(&sheet->clips[i]);
	for (i = 0; i < sheet->slice_count; i++)
		free(sheet->slices[i].rects);
	free(sheet->frame_names);
	free(sheet->frames);
	free(sheet->clips);
	free(sheet->slices);
	athena_sprite_sheet_init(sheet);
}

int athena_sprite_frame_check(AthenaSpriteFrame *frame)
{
	const float max = ATHENA_SPRITE_MAX_TEXELS;

	if (!in_range(frame->x, 0.0f, max) || !in_range(frame->y, 0.0f, max) ||
		!in_range(frame->w, 0.0f, max) || !in_range(frame->h, 0.0f, max) ||
		!(frame->w > 0.0f) || !(frame->h > 0.0f) ||
		!in_range(frame->offset_x, 0.0f, max) ||
		!in_range(frame->offset_y, 0.0f, max) ||
		!in_range(frame->source_w, 0.0f, max) ||
		!in_range(frame->source_h, 0.0f, max) ||
		!in_range(frame->duration, 0.0f, ATHENA_SPRITE_MAX_DURATION))
		return ATHENA_SPRITE_EINVAL;
	if (frame->source_w == 0.0f)
		frame->source_w = frame->offset_x + frame->w;
	if (frame->source_h == 0.0f)
		frame->source_h = frame->offset_y + frame->h;
	if (frame->offset_x + frame->w > frame->source_w ||
		frame->offset_y + frame->h > frame->source_h)
		return ATHENA_SPRITE_EINVAL;
	if (frame->duration > 0.0f && frame->duration < ATHENA_SPRITE_MIN_DURATION)
		frame->duration = ATHENA_SPRITE_MIN_DURATION;
	return 0;
}

/* Room for `extra` more frames. */
static int reserve_frames(AthenaSpriteSheet *sheet, uint32_t extra)
{
	uint32_t needed, capacity;
	AthenaSpriteFrame *frames;
	char **names;

	if (extra > ATHENA_SPRITE_MAX_FRAMES - sheet->frame_count)
		return ATHENA_SPRITE_ERANGE;
	needed = sheet->frame_count + extra;
	if (needed <= sheet->frame_capacity)
		return 0;
	capacity = sheet->frame_capacity ? sheet->frame_capacity : 16;
	while (capacity < needed)
		capacity *= 2;
	frames = realloc(sheet->frames, capacity * sizeof(*frames));
	if (!frames)
		return ATHENA_SPRITE_ENOMEM;
	sheet->frames = frames;
	if (sheet->frame_names) {
		names = realloc(sheet->frame_names, capacity * sizeof(*names));
		if (!names)
			return ATHENA_SPRITE_ENOMEM;
		sheet->frame_names = names;
	}
	sheet->frame_capacity = capacity;
	return 0;
}

int athena_sprite_sheet_add_frame(AthenaSpriteSheet *sheet,
	const AthenaSpriteFrame *frame, const char *name)
{
	AthenaSpriteFrame checked = *frame;
	char *copy = NULL;
	int result;

	if (athena_sprite_frame_check(&checked) < 0)
		return ATHENA_SPRITE_EINVAL;
	if (name) {
		if (!name_valid(name))
			return ATHENA_SPRITE_EINVAL;
		if (athena_sprite_sheet_find_frame(sheet, name) >= 0)
			return ATHENA_SPRITE_EEXIST;
	}
	result = reserve_frames(sheet, 1);
	if (result < 0)
		return result;
	if (name) {
		copy = malloc(strlen(name) + 1);
		if (!copy)
			return ATHENA_SPRITE_ENOMEM;
		strcpy(copy, name);
		/* The first named frame gives the sheet its name table. */
		if (!sheet->frame_names) {
			sheet->frame_names = calloc(sheet->frame_capacity,
				sizeof(*sheet->frame_names));
			if (!sheet->frame_names) {
				free(copy);
				return ATHENA_SPRITE_ENOMEM;
			}
		}
	}
	if (sheet->frame_names)
		sheet->frame_names[sheet->frame_count] = copy;
	sheet->frames[sheet->frame_count] = checked;
	return (int)sheet->frame_count++;
}

/* Cells of `size` (plus spacing) that fit in `extent` after the margins. */
static uint32_t grid_fit(float extent, float margin, float spacing, float size)
{
	float room = extent - 2.0f * margin + spacing;

	if (!(room >= size + spacing))
		return 0;
	return (uint32_t)floorf(room / (size + spacing));
}

int athena_sprite_sheet_add_grid(AthenaSpriteSheet *sheet,
	AthenaSpriteGrid *grid)
{
	const float max = ATHENA_SPRITE_MAX_TEXELS;
	uint32_t columns = grid->columns, rows = grid->rows, total, count, i;
	int result;

	if (!in_range(grid->frame_w, 1.0f, max) || !in_range(grid->frame_h, 1.0f, max) ||
		!in_range(grid->margin, 0.0f, max) || !in_range(grid->spacing, 0.0f, max) ||
		!in_range(grid->texture_w, 0.0f, max) || !in_range(grid->texture_h, 0.0f, max))
		return ATHENA_SPRITE_EINVAL;
	if (columns == 0)
		columns = grid_fit(grid->texture_w, grid->margin, grid->spacing, grid->frame_w);
	if (rows == 0)
		rows = grid_fit(grid->texture_h, grid->margin, grid->spacing, grid->frame_h);
	if (columns == 0 || rows == 0)
		return ATHENA_SPRITE_EINVAL;
	if (columns > ATHENA_SPRITE_MAX_FRAMES || rows > ATHENA_SPRITE_MAX_FRAMES ||
		(uint64_t)columns * rows > ATHENA_SPRITE_MAX_FRAMES)
		return ATHENA_SPRITE_ERANGE;
	total = columns * rows;
	if (grid->first >= total)
		return ATHENA_SPRITE_ERANGE;
	count = grid->count ? grid->count : total - grid->first;
	if (count > total - grid->first)
		return ATHENA_SPRITE_ERANGE;
	result = reserve_frames(sheet, count);
	if (result < 0)
		return result;
	grid->columns = columns;
	grid->rows = rows;
	for (i = 0; i < count; i++) {
		uint32_t cell = grid->first + i;
		AthenaSpriteFrame *frame = &sheet->frames[sheet->frame_count];

		memset(frame, 0, sizeof(*frame));
		frame->x = grid->margin + (float)(cell % columns) * (grid->frame_w + grid->spacing);
		frame->y = grid->margin + (float)(cell / columns) * (grid->frame_h + grid->spacing);
		frame->w = frame->source_w = grid->frame_w;
		frame->h = frame->source_h = grid->frame_h;
		if (sheet->frame_names)
			sheet->frame_names[sheet->frame_count] = NULL;
		sheet->frame_count++;
	}
	return (int)count;
}

int athena_sprite_sheet_find_frame(const AthenaSpriteSheet *sheet,
	const char *name)
{
	uint32_t i;

	if (!name || !sheet->frame_names)
		return -1;
	for (i = 0; i < sheet->frame_count; i++)
		if (sheet->frame_names[i] && strcmp(sheet->frame_names[i], name) == 0)
			return (int)i;
	return -1;
}

int athena_sprite_sheet_find_clip(const AthenaSpriteSheet *sheet,
	const char *name)
{
	uint32_t i;

	if (!name)
		return -1;
	for (i = 0; i < sheet->clip_count; i++)
		if (strcmp(sheet->clips[i].name, name) == 0)
			return (int)i;
	return -1;
}

int athena_sprite_sheet_add_clip(AthenaSpriteSheet *sheet, const char *name,
	const uint16_t *frames, uint32_t count, const float *durations,
	uint32_t duration_count, AthenaSpriteMode mode, uint32_t loops)
{
	AthenaSpriteClip clip;
	int index;
	uint32_t i;

	if (!name_valid(name) || !frames || count == 0 ||
		count > ATHENA_SPRITE_MAX_FRAMES ||
		(mode != ATHENA_SPRITE_LOOP && mode != ATHENA_SPRITE_PINGPONG))
		return ATHENA_SPRITE_EINVAL;
	if (durations && duration_count != 1 && duration_count != count)
		return ATHENA_SPRITE_EINVAL;
	for (i = 0; i < count; i++) {
		if (frames[i] >= sheet->frame_count)
			return ATHENA_SPRITE_ERANGE;
		if (durations) {
			if (!finite_bits(durations[duration_count == 1 ? 0 : i]) ||
				!(durations[duration_count == 1 ? 0 : i] > 0.0f))
				return ATHENA_SPRITE_EINVAL;
		} else if (!(sheet->frames[frames[i]].duration > 0.0f)) {
			return ATHENA_SPRITE_EINVAL;
		}
	}

	memset(&clip, 0, sizeof(clip));
	strcpy(clip.name, name);
	clip.frames = malloc(count * sizeof(*clip.frames));
	clip.durations = malloc(count * sizeof(*clip.durations));
	if (!clip.frames || !clip.durations) {
		clip_free(&clip);
		return ATHENA_SPRITE_ENOMEM;
	}
	memcpy(clip.frames, frames, count * sizeof(*clip.frames));
	for (i = 0; i < count; i++) {
		float d = durations ? durations[duration_count == 1 ? 0 : i] :
			sheet->frames[frames[i]].duration;

		if (d < ATHENA_SPRITE_MIN_DURATION)
			d = ATHENA_SPRITE_MIN_DURATION;
		if (d > ATHENA_SPRITE_MAX_DURATION)
			d = ATHENA_SPRITE_MAX_DURATION;
		clip.durations[i] = d;
		clip.length += d;
	}
	clip.count = count;
	clip.mode = mode;
	clip.loops = loops;

	index = athena_sprite_sheet_find_clip(sheet, name);
	if (index >= 0) {
		clip_free(&sheet->clips[index]);
		sheet->clips[index] = clip;
		return index;
	}
	if (sheet->clip_count == sheet->clip_capacity) {
		uint32_t capacity = sheet->clip_capacity ? sheet->clip_capacity * 2 : 8;
		AthenaSpriteClip *clips = realloc(sheet->clips, capacity * sizeof(*clips));

		if (!clips) {
			clip_free(&clip);
			return ATHENA_SPRITE_ENOMEM;
		}
		sheet->clips = clips;
		sheet->clip_capacity = capacity;
	}
	sheet->clips[sheet->clip_count] = clip;
	return (int)sheet->clip_count++;
}

int athena_sprite_sheet_set_next(AthenaSpriteSheet *sheet, int32_t clip,
	const char *next)
{
	if (clip < 0 || (uint32_t)clip >= sheet->clip_count ||
		(next && next[0] && !name_valid(next)))
		return ATHENA_SPRITE_EINVAL;
	if (next)
		strcpy(sheet->clips[clip].next, next);
	else
		sheet->clips[clip].next[0] = 0;
	return 0;
}

int athena_sprite_sheet_find_slice(const AthenaSpriteSheet *sheet,
	const char *name)
{
	uint32_t i;

	if (!name)
		return -1;
	for (i = 0; i < sheet->slice_count; i++)
		if (strcmp(sheet->slices[i].name, name) == 0)
			return (int)i;
	return -1;
}

int athena_sprite_sheet_set_slice(AthenaSpriteSheet *sheet, const char *name,
	uint32_t first, uint32_t count, const AthenaSpriteRect *rect)
{
	const float max = ATHENA_SPRITE_MAX_TEXELS;
	AthenaSpriteSlice *slice;
	int index;
	uint32_t i;

	if (!name_valid(name))
		return ATHENA_SPRITE_EINVAL;
	if (first > sheet->frame_count || count > sheet->frame_count - first)
		return ATHENA_SPRITE_ERANGE;
	if (rect && (!in_range(rect->x, -max, max) || !in_range(rect->y, -max, max) ||
		!in_range(rect->w, -max, max) || !in_range(rect->h, -max, max)))
		return ATHENA_SPRITE_EINVAL;
	index = athena_sprite_sheet_find_slice(sheet, name);
	if (index < 0) {
		if (sheet->slice_count == sheet->slice_capacity) {
			uint32_t capacity = sheet->slice_capacity ? sheet->slice_capacity * 2 : 4;
			AthenaSpriteSlice *slices = realloc(sheet->slices, capacity * sizeof(*slices));

			if (!slices)
				return ATHENA_SPRITE_ENOMEM;
			sheet->slices = slices;
			sheet->slice_capacity = capacity;
		}
		slice = &sheet->slices[sheet->slice_count];
		memset(slice, 0, sizeof(*slice));
		strcpy(slice->name, name);
		index = (int)sheet->slice_count++;
	}
	slice = &sheet->slices[index];
	/* One rectangle per frame of the sheet, frames added since included. */
	if (slice->count < sheet->frame_count) {
		AthenaSpriteRect *rects = realloc(slice->rects,
			sheet->frame_count * sizeof(*rects));

		if (!rects)
			return ATHENA_SPRITE_ENOMEM;
		memset(rects + slice->count, 0,
			(sheet->frame_count - slice->count) * sizeof(*rects));
		slice->rects = rects;
		slice->count = sheet->frame_count;
	}
	for (i = first; i < first + count; i++) {
		if (rect && rect->w > 0.0f && rect->h > 0.0f)
			slice->rects[i] = *rect;
		else
			memset(&slice->rects[i], 0, sizeof(slice->rects[i]));
	}
	return index;
}

bool athena_sprite_sheet_slice(const AthenaSpriteSheet *sheet, int slice,
	uint32_t frame, AthenaSpriteRect *rect)
{
	const AthenaSpriteSlice *s;

	if (slice < 0 || (uint32_t)slice >= sheet->slice_count)
		return false;
	s = &sheet->slices[slice];
	if (frame >= s->count || !(s->rects[frame].w > 0.0f) || !(s->rects[frame].h > 0.0f))
		return false;
	*rect = s->rects[frame];
	return true;
}

/* A decimal index; advances *text past it. */
static bool parse_index(const char **text, uint32_t *out)
{
	const char *p = *text;
	uint32_t value = 0;

	while (*p == ' ')
		p++;
	if (*p < '0' || *p > '9')
		return false;
	while (*p >= '0' && *p <= '9') {
		value = value * 10u + (uint32_t)(*p - '0');
		if (value > ATHENA_SPRITE_MAX_FRAMES)
			return false;
		p++;
	}
	while (*p == ' ')
		p++;
	*text = p;
	*out = value;
	return true;
}

int athena_sprite_parse_frames(const char *text, uint32_t limit,
	uint16_t *out, uint32_t max)
{
	uint64_t total = 0;

	if (!text)
		return ATHENA_SPRITE_EINVAL;
	for (;;) {
		uint32_t from, to, index;
		int step;

		if (!parse_index(&text, &from))
			return ATHENA_SPRITE_EINVAL;
		to = from;
		if (*text == '-') {
			text++;
			if (!parse_index(&text, &to))
				return ATHENA_SPRITE_EINVAL;
		}
		if (from >= limit || to >= limit)
			return ATHENA_SPRITE_ERANGE;
		step = to >= from ? 1 : -1;
		for (index = from;; index += (uint32_t)step) {
			if (total < max)
				out[total] = (uint16_t)index;
			if (++total > ATHENA_SPRITE_MAX_FRAMES)
				return ATHENA_SPRITE_ERANGE;
			if (index == to)
				break;
		}
		if (*text == '\0')
			return (int)total;
		if (*text != ',')
			return ATHENA_SPRITE_EINVAL;
		text++;
	}
}

/* ---------------------------------------------------------------------- */
/* Playback                                                               */

void athena_sprite_anim_init(AthenaSpriteAnim *anim)
{
	memset(anim, 0, sizeof(*anim));
	anim->clip = -1;
	anim->speed = 1.0f;
	anim->direction = 1;
}

static const AthenaSpriteClip *anim_clip(const AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet)
{
	if (anim->clip < 0 || (uint32_t)anim->clip >= sheet->clip_count)
		return NULL;
	return &sheet->clips[anim->clip];
}

bool athena_sprite_anim_play(AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet, int32_t clip)
{
	if (clip < 0 || (uint32_t)clip >= sheet->clip_count)
		return false;
	anim->clip = clip;
	anim->position = 0;
	anim->time = 0.0f;
	anim->cycles = 0;
	anim->direction = 1;
	anim->playing = true;
	anim->finished = false;
	anim->pending_frame = true;
	return true;
}

bool athena_sprite_anim_show(AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet, uint32_t frame)
{
	if (frame >= sheet->frame_count)
		return false;
	anim->clip = -1;
	anim->position = frame;
	anim->time = 0.0f;
	anim->cycles = 0;
	anim->direction = 1;
	anim->playing = false;
	anim->finished = false;
	anim->pending_frame = false;
	return true;
}

bool athena_sprite_anim_seek(AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet, uint32_t position)
{
	const AthenaSpriteClip *clip = anim_clip(anim, sheet);

	if (!clip || position >= clip->count)
		return false;
	anim->position = position;
	anim->time = 0.0f;
	anim->pending_frame = true;
	if (anim->finished) {
		anim->finished = false;
		anim->playing = true;
		anim->cycles = 0;
	}
	/* The pingpong ends only turn one way. */
	if (clip->mode == ATHENA_SPRITE_PINGPONG) {
		if (position == 0)
			anim->direction = 1;
		else if (position == clip->count - 1)
			anim->direction = -1;
	}
	return true;
}

/* Time of one cycle: first to last, and back for pingpong. */
static float clip_cycle(const AthenaSpriteClip *clip)
{
	if (clip->mode == ATHENA_SPRITE_PINGPONG && clip->count > 1)
		return 2.0f * clip->length - clip->durations[0] -
			clip->durations[clip->count - 1];
	return clip->length;
}

static void emit(AthenaSpriteEventFunc func, void *opaque,
	AthenaSpriteEventType type, const AthenaSpriteClip *clip, uint32_t position)
{
	if (func)
		func(opaque, type, position, clip->frames[position]);
}

/* Completes a cycle; true when it was the last one. */
static bool anim_cycle_done(AthenaSpriteAnim *anim, const AthenaSpriteClip *clip)
{
	anim->cycles++;
	return clip->loops != 0 && anim->cycles >= clip->loops;
}

/* Ends the clip; returns the time left past its end, for a next clip. */
static float anim_finish(AthenaSpriteAnim *anim, AthenaSpriteEventFunc func,
	void *opaque, const AthenaSpriteClip *clip)
{
	float left = anim->time;

	anim->time = 0.0f;
	anim->playing = false;
	anim->finished = true;
	emit(func, opaque, ATHENA_SPRITE_EVENT_END, clip, anim->position);
	return left;
}

/*
 * Plays the time accumulated in anim->time through `clip`. Returns true
 * when the clip ended, with the time left past its end in *left.
 */
static bool anim_run(AthenaSpriteAnim *anim, const AthenaSpriteClip *clip,
	AthenaSpriteEventFunc func, void *opaque, uint32_t *steps, float *left)
{
	float cycle;

	/*
	 * A clip that plays forever skips whole cycles past the first one (the
	 * same state after each), so a stall costs one LOOP, not every frame.
	 */
	cycle = clip_cycle(clip);
	if (clip->loops == 0 &&
		anim->time >= 2.0f * cycle + clip->durations[anim->position]) {
		float skipped = floorf((anim->time - clip->durations[anim->position]) /
			cycle) - 1.0f;

		if (skipped >= 1.0f) {
			anim->time -= skipped * cycle;
			if (anim->time < 0.0f)
				anim->time = 0.0f;
			anim->cycles += skipped < 4.0e9f ? (uint32_t)skipped : 0u;
			emit(func, opaque, ATHENA_SPRITE_EVENT_LOOP, clip, anim->position);
		}
	}

	while (anim->time >= clip->durations[anim->position]) {
		anim->time -= clip->durations[anim->position];
		if (clip->mode == ATHENA_SPRITE_LOOP || clip->count == 1) {
			if (anim->position + 1 < clip->count) {
				anim->position++;
			} else if (anim_cycle_done(anim, clip)) {
				*left = anim_finish(anim, func, opaque, clip);
				return true;
			} else {
				anim->position = 0;
				emit(func, opaque, ATHENA_SPRITE_EVENT_LOOP, clip, 0);
			}
		} else if (anim->direction > 0) {
			if (anim->position + 1 < clip->count) {
				anim->position++;
			} else {
				anim->direction = -1;
				anim->position--;
			}
		} else {
			anim->position--;
			if (anim->position == 0) {
				anim->direction = 1;
				if (anim_cycle_done(anim, clip)) {
					emit(func, opaque, ATHENA_SPRITE_EVENT_FRAME, clip, 0);
					*left = anim_finish(anim, func, opaque, clip);
					return true;
				}
				emit(func, opaque, ATHENA_SPRITE_EVENT_LOOP, clip, 0);
			}
		}
		emit(func, opaque, ATHENA_SPRITE_EVENT_FRAME, clip, anim->position);
		if (++*steps >= SPRITE_MAX_STEPS) {
			anim->time = 0.0f;
			break;
		}
	}
	return false;
}

bool athena_sprite_anim_advance(AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet, float dt, AthenaSpriteEventFunc func,
	void *opaque)
{
	const AthenaSpriteClip *clip = anim_clip(anim, sheet);
	uint32_t start, steps = 0, chain;
	float left;

	if (!clip || !anim->playing)
		return false;
	if (anim->position >= clip->count)
		anim->position = clip->count - 1;
	start = clip->frames[anim->position];
	if (anim->pending_frame) {
		anim->pending_frame = false;
		emit(func, opaque, ATHENA_SPRITE_EVENT_FRAME, clip, anim->position);
	}
	if (!finite_bits(dt) || !finite_bits(anim->speed) || !(dt > 0.0f) ||
		!(anim->speed > 0.0f))
		return false;
	dt *= anim->speed;
	if (!finite_bits(dt))
		return false;
	anim->time += dt;

	/* A clip that ends goes on to its next one with the time left over. */
	for (chain = 0; chain < SPRITE_MAX_CHAIN; chain++) {
		int next;

		if (!anim_run(anim, clip, func, opaque, &steps, &left) || !clip->next[0])
			break;
		next = athena_sprite_sheet_find_clip(sheet, clip->next);
		if (next < 0)
			break;
		athena_sprite_anim_play(anim, sheet, next);
		clip = &sheet->clips[next];
		anim->pending_frame = false;
		emit(func, opaque, ATHENA_SPRITE_EVENT_FRAME, clip, 0);
		anim->time = left;
	}
	return athena_sprite_anim_frame(anim, sheet) != start;
}

uint32_t athena_sprite_anim_frame(const AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet)
{
	const AthenaSpriteClip *clip = anim_clip(anim, sheet);

	if (clip)
		return clip->frames[anim->position < clip->count ?
			anim->position : clip->count - 1];
	return anim->position < sheet->frame_count ? anim->position : 0;
}

float athena_sprite_anim_progress(const AthenaSpriteAnim *anim,
	const AthenaSpriteSheet *sheet)
{
	const AthenaSpriteClip *clip = anim_clip(anim, sheet);
	uint32_t position, i;
	float elapsed = 0.0f, cycle, total;

	if (!clip)
		return 0.0f;
	if (anim->finished)
		return 1.0f;
	position = anim->position < clip->count ? anim->position : clip->count - 1;
	if (anim->direction > 0) {
		for (i = 0; i < position; i++)
			elapsed += clip->durations[i];
	} else {
		elapsed = clip->length;
		for (i = position + 1; i + 1 < clip->count; i++)
			elapsed += clip->durations[i];
	}
	elapsed += anim->time;
	cycle = clip_cycle(clip);
	if (clip->loops == 0) {
		total = cycle;
	} else {
		total = cycle * (float)clip->loops;
		elapsed += cycle * (float)anim->cycles;
	}
	if (!(total > 0.0f))
		return 0.0f;
	elapsed /= total;
	return elapsed < 0.0f ? 0.0f : (elapsed > 1.0f ? 1.0f : elapsed);
}

/* ---------------------------------------------------------------------- */
/* Drawing                                                                */

void athena_sprite_draw_init(AthenaSpriteDraw *draw)
{
	memset(draw, 0, sizeof(*draw));
	draw->scale_x = draw->scale_y = 1.0f;
	draw->color = 0x80808080u;
}

/* Texture rectangle of a frame, flipped, shrunk by `inset` on each side. */
static void frame_uv(const AthenaSpriteFrame *frame, bool flip_x, bool flip_y,
	float inset, float *u1, float *v1, float *u2, float *v2)
{
	float ix = inset, iy = inset;

	if (!(ix > 0.0f))
		ix = iy = 0.0f;
	if (ix > frame->w * 0.5f)
		ix = frame->w * 0.5f;
	if (iy > frame->h * 0.5f)
		iy = frame->h * 0.5f;
	*u1 = flip_x ? frame->x + frame->w - ix : frame->x + ix;
	*u2 = flip_x ? frame->x + ix : frame->x + frame->w - ix;
	*v1 = flip_y ? frame->y + frame->h - iy : frame->y + iy;
	*v2 = flip_y ? frame->y + iy : frame->y + frame->h - iy;
}

bool athena_sprite_place_rect(const AthenaSpriteFrame *frame,
	const AthenaSpriteDraw *draw, const AthenaSpriteRect *rect,
	AthenaSpriteRect *out)
{
	float left, top;

	if (!finite_bits(draw->scale_x) || !finite_bits(draw->scale_y) ||
		!(draw->scale_x > 0.0f) || !(draw->scale_y > 0.0f))
		return false;
	left = draw->flip_x ? frame->source_w - rect->x - rect->w : rect->x;
	top = draw->flip_y ? frame->source_h - rect->y - rect->h : rect->y;
	out->x = draw->x + (left - draw->origin_x * frame->source_w) * draw->scale_x;
	out->y = draw->y + (top - draw->origin_y * frame->source_h) * draw->scale_y;
	out->w = rect->w * draw->scale_x;
	out->h = rect->h * draw->scale_y;
	return true;
}

bool athena_sprite_corners(const AthenaSpriteFrame *frame,
	const AthenaSpriteDraw *draw, float inset, AthenaSpriteCorners *corners)
{
	float left, top, w, h, ix, iy, c = 1.0f, sn = 0.0f;
	int i;

	if (!finite_bits(draw->scale_x) || !finite_bits(draw->scale_y) ||
		!(draw->scale_x > 0.0f) || !(draw->scale_y > 0.0f))
		return false;
	left = draw->flip_x ? frame->source_w - frame->offset_x - frame->w : frame->offset_x;
	top = draw->flip_y ? frame->source_h - frame->offset_y - frame->h : frame->offset_y;
	left = (left - draw->origin_x * frame->source_w) * draw->scale_x;
	top = (top - draw->origin_y * frame->source_h) * draw->scale_y;
	w = frame->w * draw->scale_x;
	h = frame->h * draw->scale_y;
	if (draw->rotation != 0.0f) {
		c = cosf(draw->rotation);
		sn = sinf(draw->rotation);
	}
	ix = inset > 0.0f ? (inset < frame->w * 0.5f ? inset : frame->w * 0.5f) : 0.0f;
	iy = inset > 0.0f ? (inset < frame->h * 0.5f ? inset : frame->h * 0.5f) : 0.0f;
	for (i = 0; i < 4; i++) {
		bool right = (i & 1) != 0, bottom = (i & 2) != 0;
		float px = left + (right ? w : 0.0f), py = top + (bottom ? h : 0.0f);
		/* The texel of the frame this corner shows, before the atlas turn. */
		float s = (right != draw->flip_x) ? frame->w - ix : ix;
		float t = (bottom != draw->flip_y) ? frame->h - iy : iy;

		/* Turned about the origin, clockwise on screen (y down). */
		corners->x[i] = draw->x + px * c - py * sn;
		corners->y[i] = draw->y + py * c + px * sn;
		if (frame->rotated) {
			/* Stored turned 90 degrees clockwise: (s, t) is at (h - t, s). */
			corners->u[i] = frame->x + (frame->h - t);
			corners->v[i] = frame->y + s;
		} else {
			corners->u[i] = frame->x + s;
			corners->v[i] = frame->y + t;
		}
	}
	return true;
}

bool athena_sprite_quad(const AthenaSpriteFrame *frame,
	const AthenaSpriteDraw *draw, float inset, AthenaSpriteQuad *quad)
{
	float left, top, w, h;

	if (!finite_bits(draw->scale_x) || !finite_bits(draw->scale_y) ||
		!(draw->scale_x > 0.0f) || !(draw->scale_y > 0.0f))
		return false;
	/* A flip mirrors the frame inside its untrimmed box, trim included. */
	left = draw->flip_x ? frame->source_w - frame->offset_x - frame->w : frame->offset_x;
	top = draw->flip_y ? frame->source_h - frame->offset_y - frame->h : frame->offset_y;
	left = (left - draw->origin_x * frame->source_w) * draw->scale_x;
	top = (top - draw->origin_y * frame->source_h) * draw->scale_y;
	w = frame->w * draw->scale_x;
	h = frame->h * draw->scale_y;

	quad->w = w;
	quad->h = h;
	frame_uv(frame, draw->flip_x, draw->flip_y, inset, &quad->u1, &quad->v1,
		&quad->u2, &quad->v2);
	quad->angle = draw->rotation;
	if (draw->rotation == 0.0f) {
		quad->x = draw->x + left;
		quad->y = draw->y + top;
	} else {
		/*
		 * The image is turned about its center: turn that center about
		 * the origin, the same way (clockwise on screen, y down).
		 */
		float c = cosf(draw->rotation), s = sinf(draw->rotation);
		float cx = left + w * 0.5f, cy = top + h * 0.5f;

		quad->x = draw->x + cx * c - cy * s - w * 0.5f;
		quad->y = draw->y + cy * c + cx * s - h * 0.5f;
	}
	return true;
}

/* ---------------------------------------------------------------------- */
/* Batches                                                                */

uint32_t athena_sprite_animator_update(AthenaSpriteAnimatorEntry *entries,
	uint32_t count, const AthenaSpriteSheet *sheet, float dt,
	AthenaTileSprite *sprites, uint32_t sprite_count)
{
	uint32_t i, written = 0;

	if (sheet->frame_count == 0)
		return 0;
	for (i = 0; i < count; i++) {
		AthenaSpriteAnimatorEntry *entry = &entries[i];
		const AthenaSpriteFrame *frame;
		AthenaTileSprite *sprite;
		uint32_t shown;

		if (entry->anim.playing)
			athena_sprite_anim_advance(&entry->anim, sheet, dt, NULL, NULL);
		if (!sprites || entry->sprite >= sprite_count)
			continue;
		shown = athena_sprite_anim_frame(&entry->anim, sheet);
		if (shown == entry->shown)
			continue;
		entry->shown = shown;
		frame = &sheet->frames[shown];
		sprite = &sprites[entry->sprite];
		/* Turned frames cannot be TileMap sprites; the bindings refuse them. */
		frame_uv(frame, entry->flip_x, entry->flip_y, sheet->inset, &sprite->u1,
			&sprite->v1, &sprite->u2, &sprite->v2);
		written++;
	}
	return written;
}

/* ---------------------------------------------------------------------- */
/* Sheet files                                                            */

char *athena_sprite_path_join(const char *file, const char *name)
{
	const char *slash = NULL, *p;
	size_t dir = 0;
	char *full;

	if (!name)
		return NULL;
	/* Absolute paths and devices ("mass:", "host:") are used as they are. */
	if (file && name[0] != '/' && !strchr(name, ':')) {
		for (p = file; *p; p++)
			if (*p == '/' || *p == '\\')
				slash = p;
		if (!slash)
			slash = strrchr(file, ':');
		if (slash)
			dir = (size_t)(slash - file) + 1;
	}
	full = malloc(dir + strlen(name) + 1);
	if (!full)
		return NULL;
	if (dir)
		memcpy(full, file, dir);
	strcpy(full + dir, name);
	return full;
}

static const char *skip_space(const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
		p++;
	return p;
}

char *athena_sprite_find_meta_image(const char *text, size_t length)
{
	const char *end = text + length, *meta = NULL, *p;
	static const char key[] = "\"image\"";

	/* Aseprite and TexturePacker write "meta" last; take the last one. */
	for (p = text; p + 6 <= end; p++)
		if (memcmp(p, "\"meta\"", 6) == 0)
			meta = p + 6;
	if (!meta)
		return NULL;
	for (p = meta; p + sizeof(key) - 1 <= end; p++) {
		const char *value, *close;
		char *name;

		if (memcmp(p, key, sizeof(key) - 1) != 0)
			continue;
		value = skip_space(p + sizeof(key) - 1, end);
		if (value >= end || *value != ':')
			continue;
		value = skip_space(value + 1, end);
		if (value >= end || *value != '"')
			continue;
		value++;
		for (close = value; close < end && *close != '"'; close++)
			if (*close == '\\')
				return NULL;   /* escapes: left to the parser on the script thread */
		if (close >= end || close == value)
			return NULL;
		name = malloc((size_t)(close - value) + 1);
		if (!name)
			return NULL;
		memcpy(name, value, (size_t)(close - value));
		name[close - value] = '\0';
		return name;
	}
	return NULL;
}
