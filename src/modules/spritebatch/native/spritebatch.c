#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <athena/graphics.h>
#include <athena/graphics/view.h>
#include <athena/spritebatch.h>

#define SB_QUEUE 512
#define SB_MAX_MARKS 16

enum { STEP_SPRITES, STEP_RECT, STEP_QUAD };

typedef struct {
	uint8_t kind;
	AthenaImage *image;
	uint32_t first, count;      /* sprites of the pool */
	float f[8];                 /* rect: x y w h; quad: x1 y1 x2 y2 x3 y3 x4 y4 */
	uint32_t c[4];
} SbStep;

typedef struct {
	int id;
	uint32_t index;             /* sprite of the pool */
} SbMark;

struct AthenaSbLayer {
	SbStep *steps;
	uint32_t nsteps;
	prim_tex_sprite *pool;
	uint32_t npool;
	SbMark marks[SB_MAX_MARKS];
	uint32_t nmarks;
};

/* Live queue: sprites of one image waiting for the next flush. */
static prim_tex_sprite queue[SB_QUEUE];
static prim_tex_sprite scratch[SB_QUEUE];
static uint32_t queue_count;
static AthenaImage *queue_image;

/* Recorder. */
static struct {
	bool active;
	SbStep *steps;
	uint32_t nsteps, cap_steps;
	prim_tex_sprite *pool;
	uint32_t npool, cap_pool;
	SbMark marks[SB_MAX_MARKS];
	uint32_t nmarks;
	int pending_mark;
	bool failed;
} rec;

static uint32_t stat_sprites, stat_batches;

void athena_sb_stats(uint32_t *sprites, uint32_t *batches)
{
	*sprites = stat_sprites;
	*batches = stat_batches;
	stat_sprites = stat_batches = 0;
}

static void sb_submit(AthenaImage *image, prim_tex_sprite *list, uint32_t count)
{
	AthenaViewCuller culler;

	if (!image || count == 0)
		return;
	stat_sprites += count;
	stat_batches++;
	/* Under a camera, sprites outside its viewport never reach the GS. */
	if (!athena_view_culler_init(&culler)) {
		athena_image_draw_list(image, 0.0f, 0.0f, list, (int)count);
		return;
	}
	for (uint32_t done = 0; done < count; ) {
		uint32_t n = 0, chunk = count - done > SB_QUEUE ? SB_QUEUE : count - done;

		for (uint32_t i = 0; i < chunk; i++) {
			const prim_tex_sprite *s = &list[done + i];

			if (athena_view_culler_visible(&culler, s->x, s->y, s->w, s->h))
				scratch[n++] = *s;
			else
				athena_view_count_culled(1);
		}
		if (n)
			athena_image_draw_list(image, 0.0f, 0.0f, scratch, (int)n);
		done += chunk;
	}
}

void athena_sb_flush(void)
{
	if (queue_count)
		sb_submit(queue_image, queue, queue_count);
	queue_count = 0;
	queue_image = NULL;
}

static void sb_draw_rect_now(float x, float y, float w, float h, uint32_t color)
{
	if (athena_view_kind() != ATHENA_VIEW_IDENTITY) {
		if (w > 0.0f && h > 0.0f)
			draw_rect_f(x, y, w, h, color);
		return;
	}
	if (w < 1.0f || h < 1.0f || w > 1e9f || h > 1e9f)
		return;
	draw_sprite(x, y, (int)w, (int)h, color);
}

static void sb_draw_quad_now(const float xy[8], const uint32_t c[4])
{
	draw_quad_gouraud(xy[0], xy[1], xy[2], xy[3], xy[4], xy[5], xy[6], xy[7],
		c[0], c[1], c[2], c[3]);
}

/* ---- recorder ----------------------------------------------------------- */

static SbStep *rec_new_step(uint8_t kind)
{
	if (rec.nsteps == rec.cap_steps) {
		uint32_t cap = rec.cap_steps ? rec.cap_steps * 2 : 8;
		SbStep *steps = realloc(rec.steps, cap * sizeof(SbStep));

		if (!steps) {
			rec.failed = true;
			return NULL;
		}
		rec.steps = steps;
		rec.cap_steps = cap;
	}
	SbStep *step = &rec.steps[rec.nsteps++];

	memset(step, 0, sizeof(*step));
	step->kind = kind;
	return step;
}

static void rec_sprite(AthenaImage *image, const prim_tex_sprite *s)
{
	SbStep *step = rec.nsteps ? &rec.steps[rec.nsteps - 1] : NULL;

	if (!step || step->kind != STEP_SPRITES || step->image != image) {
		step = rec_new_step(STEP_SPRITES);
		if (!step)
			return;
		step->image = image;
		step->first = rec.npool;
	}
	if (rec.npool == rec.cap_pool) {
		uint32_t cap = rec.cap_pool ? rec.cap_pool * 2 : 256;
		prim_tex_sprite *pool = realloc(rec.pool, cap * sizeof(prim_tex_sprite));

		if (!pool) {
			rec.failed = true;
			return;
		}
		rec.pool = pool;
		rec.cap_pool = cap;
	}
	if (rec.pending_mark > 0) {
		if (rec.nmarks < SB_MAX_MARKS) {
			rec.marks[rec.nmarks].id = rec.pending_mark;
			rec.marks[rec.nmarks].index = rec.npool;
			rec.nmarks++;
		}
		rec.pending_mark = 0;
	}
	rec.pool[rec.npool++] = *s;
	step->count++;
}

void athena_sb_sprite(AthenaImage *image, float x, float y, float w, float h,
	float u1, float v1, float u2, float v2, uint32_t color)
{
	prim_tex_sprite *s;
	prim_tex_sprite local;

	if (!image || w == 0.0f || h == 0.0f)
		return;
	if (rec.active) {
		s = &local;
	} else {
		if (image != queue_image)
			athena_sb_flush();
		else if (queue_count == SB_QUEUE) {
			sb_submit(queue_image, queue, queue_count);
			queue_count = 0;
		}
		queue_image = image;
		s = &queue[queue_count++];
	}
	s->x = x; s->y = y; s->w = w; s->h = h;
	s->u1 = u1; s->v1 = v1; s->u2 = u2; s->v2 = v2;
	s->rgba = color;
	if (rec.active)
		rec_sprite(image, s);
}

void athena_sb_rect(float x, float y, float w, float h, uint32_t color)
{
	if (rec.active) {
		SbStep *step = rec_new_step(STEP_RECT);

		if (step) {
			step->f[0] = x; step->f[1] = y; step->f[2] = w; step->f[3] = h;
			step->c[0] = color;
		}
		return;
	}
	athena_sb_flush();
	sb_draw_rect_now(x, y, w, h, color);
}

void athena_sb_quad(const float xy[8], const uint32_t color[4])
{
	if (rec.active) {
		SbStep *step = rec_new_step(STEP_QUAD);

		if (step) {
			memcpy(step->f, xy, sizeof(step->f));
			memcpy(step->c, color, sizeof(step->c));
		}
		return;
	}
	athena_sb_flush();
	sb_draw_quad_now(xy, color);
}

bool athena_sb_capturing(void) { return rec.active; }

bool athena_sb_capture_begin(void)
{
	if (rec.active)
		return false;
	athena_sb_flush();
	memset(&rec, 0, sizeof(rec));
	rec.active = true;
	return true;
}

void athena_sb_capture_abort(void)
{
	free(rec.steps);
	free(rec.pool);
	memset(&rec, 0, sizeof(rec));
}

AthenaSbLayer *athena_sb_capture_end(void)
{
	AthenaSbLayer *layer;

	if (!rec.active)
		return NULL;
	if (rec.failed) {
		athena_sb_capture_abort();
		return NULL;
	}
	layer = calloc(1, sizeof(*layer));
	if (layer) {
		/* Trim to size: layers live as long as a scene (an empty capture is a valid, empty layer). */
		layer->steps = rec.nsteps ? realloc(rec.steps, rec.nsteps * sizeof(SbStep)) : NULL;
		layer->pool = rec.npool ? realloc(rec.pool, rec.npool * sizeof(prim_tex_sprite)) : NULL;
		if (!layer->steps && rec.nsteps)
			layer->steps = rec.steps;
		if (!layer->pool && rec.npool)
			layer->pool = rec.pool;
		if (!rec.nsteps) {
			free(rec.steps);
			free(rec.pool);
		}
		layer->nsteps = rec.nsteps;
		layer->npool = rec.npool;
		layer->nmarks = rec.nmarks;
		memcpy(layer->marks, rec.marks, sizeof(layer->marks));
		memset(&rec, 0, sizeof(rec));
	} else {
		athena_sb_capture_abort();
	}
	return layer;
}

void athena_sb_mark(int id)
{
	if (rec.active)
		rec.pending_mark = id > 0 ? id : 0;
}

/* ---- layers --------------------------------------------------------------- */

void athena_sb_layer_destroy(AthenaSbLayer *layer)
{
	if (!layer)
		return;
	free(layer->steps);
	free(layer->pool);
	free(layer);
}

void athena_sb_layer_draw(const AthenaSbLayer *layer, AthenaSbValidFn valid, void *user)
{
	athena_sb_flush();
	for (uint32_t i = 0; i < layer->nsteps; i++) {
		const SbStep *s = &layer->steps[i];

		if (s->kind == STEP_SPRITES) {
			if (valid && !valid(user, s->image))
				continue;
			sb_submit(s->image, layer->pool + s->first, s->count);
		} else if (s->kind == STEP_RECT) {
			sb_draw_rect_now(s->f[0], s->f[1], s->f[2], s->f[3], s->c[0]);
		} else {
			sb_draw_quad_now(s->f, s->c);
		}
	}
}

bool athena_sb_layer_tint(AthenaSbLayer *layer, int id, int r, int g, int b)
{
	for (uint32_t i = 0; i < layer->nmarks; i++) {
		if (layer->marks[i].id == id) {
			prim_tex_sprite *s = &layer->pool[layer->marks[i].index];
			uint32_t alpha = ((uint32_t)s->rgba >> 24) & 0xff;

			s->rgba = athena_sb_color(r, g, b, (int)alpha);
			return true;
		}
	}
	return false;
}

uint32_t athena_sb_layer_steps(const AthenaSbLayer *layer) { return layer->nsteps; }

AthenaImage *athena_sb_layer_step_image(const AthenaSbLayer *layer, uint32_t step)
{
	return step < layer->nsteps && layer->steps[step].kind == STEP_SPRITES ?
		layer->steps[step].image : NULL;
}

uint32_t athena_sb_layer_step_count(const AthenaSbLayer *layer, uint32_t step)
{
	return step < layer->nsteps && layer->steps[step].kind == STEP_SPRITES ?
		layer->steps[step].count : 0;
}

void athena_sb_layer_step_record(const AthenaSbLayer *layer, uint32_t step,
	uint32_t index, float out[16])
{
	const prim_tex_sprite *s = &layer->pool[layer->steps[step].first + index];
	uint32_t c = (uint32_t)s->rgba, bits[4] = { c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, c >> 24 };

	out[0] = s->x; out[1] = s->y; out[2] = s->w; out[3] = s->h;
	out[4] = s->u1; out[5] = s->v1; out[6] = s->u2; out[7] = s->v2;
	memcpy(out + 8, bits, sizeof(bits));
	out[12] = out[13] = out[14] = out[15] = 0.0f;
}
