#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <athena/spritebatch.h>

typedef struct {
	float w, h, u1, v1, u2, v2, ox, oy, sw, sh, ds, native;
} SbSprite;

struct AthenaSbAtlas {
	float ratio, screen_w, screen_h;
	int count;
	SbSprite *sprites;
	char *names;            /* NUL-separated copies */
	int *slots;             /* open-addressing table of sprite indexes, -1 empty */
	int nslots;
	const char **keys;      /* name of each sprite (points into names) */
};

static uint32_t sb_hash(const char *s)
{
	uint32_t h = 2166136261u;

	while (*s)
		h = (h ^ (uint8_t)*s++) * 16777619u;
	return h;
}

static float sb_round(float v) { return floorf(v + 0.5f); }

AthenaSbAtlas *athena_sb_atlas_create(const AthenaSbSpriteDef *defs, int count,
	float ratio, float screen_w, float screen_h)
{
	AthenaSbAtlas *atlas = calloc(1, sizeof(*atlas));
	size_t names_size = 0;
	char *name;

	if (!atlas || count < 0)
		goto fail;
	atlas->ratio = ratio;
	atlas->screen_w = screen_w;
	atlas->screen_h = screen_h;
	atlas->count = count;
	atlas->sprites = calloc(count ? count : 1, sizeof(SbSprite));
	atlas->keys = calloc(count ? count : 1, sizeof(char *));
	for (atlas->nslots = 16; atlas->nslots < count * 2; atlas->nslots *= 2)
		;
	atlas->slots = malloc(atlas->nslots * sizeof(int));
	for (int i = 0; i < count; i++)
		names_size += strlen(defs[i].name) + 1;
	atlas->names = malloc(names_size ? names_size : 1);
	if (!atlas->sprites || !atlas->keys || !atlas->slots || !atlas->names)
		goto fail;
	for (int i = 0; i < atlas->nslots; i++)
		atlas->slots[i] = -1;
	name = atlas->names;
	for (int i = 0; i < count; i++) {
		const AthenaSbSpriteDef *d = &defs[i];
		SbSprite *s = &atlas->sprites[i];
		uint32_t slot;

		s->w = d->w; s->h = d->h;
		s->u1 = d->u1; s->v1 = d->v1; s->u2 = d->u2; s->v2 = d->v2;
		s->ox = d->offset_x; s->oy = d->offset_y;
		s->sw = d->source_w; s->sh = d->source_h;
		s->ds = d->draw_scale; s->native = d->native_scale;
		strcpy(name, d->name);
		atlas->keys[i] = name;
		name += strlen(name) + 1;
		slot = sb_hash(atlas->keys[i]) & (atlas->nslots - 1);
		while (atlas->slots[slot] >= 0)
			slot = (slot + 1) & (atlas->nslots - 1);
		atlas->slots[slot] = i;
	}
	return atlas;
fail:
	athena_sb_atlas_destroy(atlas);
	return NULL;
}

void athena_sb_atlas_destroy(AthenaSbAtlas *atlas)
{
	if (!atlas)
		return;
	free(atlas->sprites);
	free(atlas->names);
	free(atlas->slots);
	free(atlas->keys);
	free(atlas);
}

int athena_sb_atlas_find(const AthenaSbAtlas *atlas, const char *name)
{
	uint32_t slot = sb_hash(name) & (atlas->nslots - 1);

	while (atlas->slots[slot] >= 0) {
		int i = atlas->slots[slot];

		if (strcmp(atlas->keys[i], name) == 0)
			return i;
		slot = (slot + 1) & (atlas->nslots - 1);
	}
	return -1;
}

void athena_sb_atlas_size(const AthenaSbAtlas *atlas, int id, float *w, float *h)
{
	const SbSprite *s = &atlas->sprites[id];

	*w = s->sw * s->ds / atlas->ratio;
	*h = s->sh * s->ds / atlas->ratio;
}

static void tint(const int *rgb, int alpha, uint32_t *color)
{
	int a = alpha;

	if (rgb)
		*color = athena_sb_color((rgb[0] + 1) >> 1, (rgb[1] + 1) >> 1, (rgb[2] + 1) >> 1, a);
	else
		*color = athena_sb_color(128, 128, 128, a);
}

void athena_sb_atlas_draw(const AthenaSbAtlas *atlas, AthenaImage *image, int id,
	float nx, float ny, float mul, float alpha, const int *rgb, bool flip)
{
	const SbSprite *s = &atlas->sprites[id];
	float ds = s->ds;
	float cx = nx * atlas->ratio + s->sw * ds / 2.0f;
	float cy = ny * atlas->ratio + s->sh * ds / 2.0f;
	float w = s->w * ds * mul, h = s->h * ds * mul;
	float ox = flip ? s->sw - s->ox - s->w : s->ox;
	float x = cx + (ox - s->sw / 2.0f) * ds * mul;
	float y = cy + (s->oy - s->sh / 2.0f) * ds * mul;
	uint32_t color;

	if (x >= atlas->screen_w || y >= atlas->screen_h || x + w <= 0.0f || y + h <= 0.0f ||
			alpha <= 0.0f)
		return;
	tint(rgb, (int)sb_round(128.0f * alpha), &color);
	athena_sb_sprite(image, x, y, w, h, flip ? s->u2 : s->u1, s->v1, flip ? s->u1 : s->u2, s->v2, color);
}

void athena_sb_atlas_draw_rect(const AthenaSbAtlas *atlas, AthenaImage *image, int id,
	float nx, float ny, float nw, float nh, float alpha, const int *rgb)
{
	const SbSprite *s = &atlas->sprites[id];
	float sx = nw * atlas->ratio / s->sw, sy = nh * atlas->ratio / s->sh;
	float x0 = sb_round(nx * atlas->ratio + s->ox * sx);
	float x1 = sb_round(nx * atlas->ratio + (s->ox + s->w) * sx);
	float y0 = sb_round(ny * atlas->ratio + s->oy * sy);
	float y1 = sb_round(ny * atlas->ratio + (s->oy + s->h) * sy);
	uint32_t color;

	if (x1 <= x0 || y1 <= y0 || x0 >= atlas->screen_w || y0 >= atlas->screen_h ||
			x1 <= 0.0f || y1 <= 0.0f || alpha <= 0.0f)
		return;
	tint(rgb, (int)sb_round(128.0f * alpha), &color);
	athena_sb_sprite(image, x0, y0, x1 - x0, y1 - y0, s->u1, s->v1, s->u2, s->v2, color);
}

void athena_sb_atlas_draw_nine(const AthenaSbAtlas *atlas, AthenaImage *image, int id,
	float nx, float ny, float nw, float nh, float corner, float alpha, const int *rgb)
{
	const SbSprite *s = &atlas->sprites[id];
	float x0 = sb_round(nx * atlas->ratio), y0 = sb_round(ny * atlas->ratio);
	float x1 = sb_round((nx + nw) * atlas->ratio), y1 = sb_round((ny + nh) * atlas->ratio);
	float w = x1 - x0, h = y1 - y0;
	float c = s->native > 0.0f ? corner / s->native : corner;   /* margin in texels */
	float mx = c * s->ds, my = mx;
	float xs[4], ys[4], us[4], vs[4];
	uint32_t color;

	if (w <= 0.0f || h <= 0.0f || x0 >= atlas->screen_w || y0 >= atlas->screen_h ||
			x1 <= 0.0f || y1 <= 0.0f || alpha <= 0.0f)
		return;
	if (mx * 2.0f > w) mx = w / 2.0f;
	if (my * 2.0f > h) my = h / 2.0f;
	xs[0] = x0; xs[1] = x0 + mx; xs[2] = x1 - mx; xs[3] = x1;
	ys[0] = y0; ys[1] = y0 + my; ys[2] = y1 - my; ys[3] = y1;
	us[0] = s->u1; us[1] = s->u1 + c; us[2] = s->u2 - c; us[3] = s->u2;
	vs[0] = s->v1; vs[1] = s->v1 + c; vs[2] = s->v2 - c; vs[3] = s->v2;
	tint(rgb, (int)sb_round(128.0f * alpha), &color);
	for (int j = 0; j < 3; j++)
		for (int i = 0; i < 3; i++) {
			float cw = xs[i + 1] - xs[i], ch = ys[j + 1] - ys[j];

			if (cw > 0.0f && ch > 0.0f)
				athena_sb_sprite(image, xs[i], ys[j], cw, ch, us[i], vs[j], us[i + 1], vs[j + 1], color);
		}
}
