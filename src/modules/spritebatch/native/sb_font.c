#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <athena/spritebatch.h>

#define FONT_CACHE_MAX 400
#define FONT_BUCKETS 1024

typedef struct CacheEntry {
	char *text;
	uint32_t hash;
	int nquads;
	float *quads;           /* per quad: dx dy w h u1 v1 u2 v2, native pixels / texels */
	float width;
	struct CacheEntry *next;
} CacheEntry;

struct AthenaSbFont {
	AthenaSbGlyph *glyphs;
	int nglyphs;
	int *slots;             /* codepoint -> glyph index, open addressing */
	int nslots;
	int default_glyph;
	float spacing, line_spacing, ratio, screen_w, screen_h;
	int color[3], shadow[3];
	float shadow_offset;
	CacheEntry *buckets[FONT_BUCKETS];
	int cached;
};

static float sb_round(float v) { return floorf(v + 0.5f); }

static uint32_t text_hash(const char *s)
{
	uint32_t h = 2166136261u;

	while (*s)
		h = (h ^ (uint8_t)*s++) * 16777619u;
	return h;
}

static uint32_t cp_hash(uint32_t cp) { return cp * 2654435761u; }

AthenaSbFont *athena_sb_font_create(const AthenaSbGlyph *glyphs, int count,
	uint32_t default_codepoint, float spacing, float line_spacing, float ratio,
	float screen_w, float screen_h, const int color[3], const int shadow[3],
	float shadow_offset)
{
	AthenaSbFont *font = calloc(1, sizeof(*font));

	if (!font || count <= 0)
		goto fail;
	font->glyphs = malloc(count * sizeof(AthenaSbGlyph));
	for (font->nslots = 64; font->nslots < count * 2; font->nslots *= 2)
		;
	font->slots = malloc(font->nslots * sizeof(int));
	if (!font->glyphs || !font->slots)
		goto fail;
	memcpy(font->glyphs, glyphs, count * sizeof(AthenaSbGlyph));
	font->nglyphs = count;
	for (int i = 0; i < font->nslots; i++)
		font->slots[i] = -1;
	font->default_glyph = 0;
	for (int i = 0; i < count; i++) {
		uint32_t slot = cp_hash(glyphs[i].codepoint) & (font->nslots - 1);

		while (font->slots[slot] >= 0)
			slot = (slot + 1) & (font->nslots - 1);
		font->slots[slot] = i;
		if (glyphs[i].codepoint == default_codepoint)
			font->default_glyph = i;
	}
	font->spacing = spacing;
	font->line_spacing = line_spacing;
	font->ratio = ratio;
	font->screen_w = screen_w;
	font->screen_h = screen_h;
	memcpy(font->color, color, sizeof(font->color));
	memcpy(font->shadow, shadow, sizeof(font->shadow));
	font->shadow_offset = shadow_offset;
	return font;
fail:
	athena_sb_font_destroy(font);
	return NULL;
}

static void cache_clear(AthenaSbFont *font)
{
	for (int i = 0; i < FONT_BUCKETS; i++) {
		CacheEntry *e = font->buckets[i];

		while (e) {
			CacheEntry *next = e->next;

			free(e->text);
			free(e->quads);
			free(e);
			e = next;
		}
		font->buckets[i] = NULL;
	}
	font->cached = 0;
}

void athena_sb_font_destroy(AthenaSbFont *font)
{
	if (!font)
		return;
	cache_clear(font);
	free(font->glyphs);
	free(font->slots);
	free(font);
}

static const AthenaSbGlyph *glyph_of(const AthenaSbFont *font, uint32_t cp)
{
	uint32_t slot = cp_hash(cp) & (font->nslots - 1);

	while (font->slots[slot] >= 0) {
		const AthenaSbGlyph *g = &font->glyphs[font->slots[slot]];

		if (g->codepoint == cp)
			return g;
		slot = (slot + 1) & (font->nslots - 1);
	}
	return &font->glyphs[font->default_glyph];
}

/* Next code point of a UTF-8 string (invalid bytes come back as themselves). */
static uint32_t next_cp(const unsigned char **p)
{
	const unsigned char *s = *p;
	uint32_t c = *s++;
	int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;

	if (extra) {
		c &= 0x3F >> extra;
		while (extra-- && (*s & 0xC0) == 0x80)
			c = (c << 6) | (*s++ & 0x3F);
	}
	*p = s;
	return c;
}

/* SpriteFont.DrawString: where each glyph goes, computed once per text. */
static CacheEntry *layout(AthenaSbFont *font, const char *text)
{
	uint32_t h = text_hash(text);
	CacheEntry *e;
	const unsigned char *p = (const unsigned char *)text;
	size_t len = strlen(text);
	float *q;
	int nq = 0;
	float best = 0.0f, x = 0.0f, y = 0.0f;
	bool first = true;

	for (e = font->buckets[h & (FONT_BUCKETS - 1)]; e; e = e->next)
		if (e->hash == h && strcmp(e->text, text) == 0)
			return e;

	if (font->cached >= FONT_CACHE_MAX)
		cache_clear(font);
	q = malloc((len + 1) * 8 * sizeof(float));      /* at most one quad per byte */
	e = calloc(1, sizeof(*e));
	if (!q || !e) {
		free(q);
		free(e);
		return NULL;
	}
	while (*p) {
		uint32_t cp = next_cp(&p);
		const AthenaSbGlyph *g;

		if (cp == '\n') {
			if (x > best) best = x;
			x = 0.0f;
			y += font->line_spacing;
			first = true;
			continue;
		}
		g = glyph_of(font, cp);
		x = first ? (g->left > 0.0f ? g->left : 0.0f) : x + font->spacing + g->left;
		first = false;
		if (g->w > 1.0f || g->h > 1.0f) {
			float *o = q + nq * 8;

			o[0] = x + g->crop_x; o[1] = y + g->crop_y; o[2] = g->w; o[3] = g->h;
			o[4] = g->x; o[5] = g->y; o[6] = g->x + g->w; o[7] = g->y + g->h;
			nq++;
		}
		x += g->advance + g->right;
	}
	e->text = strdup(text);
	e->hash = h;
	e->nquads = nq;
	e->quads = realloc(q, (nq ? nq : 1) * 8 * sizeof(float));
	if (!e->quads)
		e->quads = q;
	e->width = best > x ? best : x;
	e->next = font->buckets[h & (FONT_BUCKETS - 1)];
	font->buckets[h & (FONT_BUCKETS - 1)] = e;
	font->cached++;
	return e;
}

float athena_sb_font_measure(AthenaSbFont *font, const char *text)
{
	CacheEntry *e = layout(font, text);

	return e ? e->width : 0.0f;
}

float athena_sb_font_height(const AthenaSbFont *font, const char *text)
{
	int lines = 1;

	for (; *text; text++)
		if (*text == '\n')
			lines++;
	return font->line_spacing * (float)lines;
}

void athena_sb_font_draw(AthenaSbFont *font, AthenaImage *image, const char *text,
	float nx, float ny, const int *rgb, float alpha)
{
	CacheEntry *e = layout(font, text);
	const int *c = rgb ? rgb : font->color;
	uint32_t color;

	if (!e)
		return;
	color = athena_sb_color((c[0] + 1) >> 1, (c[1] + 1) >> 1, (c[2] + 1) >> 1,
		(int)sb_round(128.0f * alpha));
	for (int i = 0; i < e->nquads; i++) {
		const float *q = e->quads + i * 8;
		float sx = sb_round((nx + q[0]) * font->ratio), sy = sb_round((ny + q[1]) * font->ratio);

		if (sx >= font->screen_w || sy >= font->screen_h || sx + q[2] < 0.0f)
			continue;
		athena_sb_sprite(image, sx, sy, q[2] * font->ratio, q[3] * font->ratio,
			q[4], q[5], q[6], q[7], color);
	}
}

void athena_sb_font_draw_shadow(AthenaSbFont *font, AthenaImage *image, const char *text,
	float nx, float ny, const int *rgb, float alpha)
{
	float o = font->shadow_offset;

	athena_sb_font_draw(font, image, text, nx + o, ny + o, font->shadow, alpha);
	athena_sb_font_draw(font, image, text, nx, ny, rgb, alpha);
}

void athena_sb_font_draw_centered(AthenaSbFont *font, AthenaImage *image, const char *text,
	float cx, float ny, const int *rgb, float alpha, bool shadow)
{
	float x = cx - athena_sb_font_measure(font, text) / 2.0f;

	if (shadow)
		athena_sb_font_draw_shadow(font, image, text, x, ny, rgb, alpha);
	else
		athena_sb_font_draw(font, image, text, x, ny, rgb, alpha);
}
