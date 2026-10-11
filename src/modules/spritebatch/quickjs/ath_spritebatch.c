#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <ath_env.h>
#include <athena/spritebatch.h>
#include <athena/js/image.h>

#include "ath_spritebatch.h"

#define SB_KEEP 16          /* images a single layer can use */
#define SB_MARK_NAMES 16

static JSClassID layer_class_id, atlas_class_id, font_class_id;

/* ---- images kept alive while recording --------------------------------- */

typedef struct {
	AthenaSbLayer *core;
	JSValue keep[SB_KEEP];
	AthenaImage *ptr[SB_KEEP];
	int nkeep;
} LayerObj;

static JSValue cap_keep[SB_KEEP];
static AthenaImage *cap_ptr[SB_KEEP];
static int cap_n;

static void cap_release(JSContext *ctx)
{
	for (int i = 0; i < cap_n; i++)
		JS_FreeValue(ctx, cap_keep[i]);
	cap_n = 0;
}

/* A layer references images by pointer: hold their JS objects until it is gone. */
static int note_image(JSContext *ctx, JSValueConst value, AthenaImage *image)
{
	if (!athena_sb_capturing())
		return 0;
	for (int i = 0; i < cap_n; i++)
		if (cap_ptr[i] == image)
			return 0;
	if (cap_n == SB_KEEP) {
		JS_ThrowRangeError(ctx, "SpriteBatch.capture: more than %d images in one layer", SB_KEEP);
		return -1;
	}
	cap_ptr[cap_n] = image;
	cap_keep[cap_n++] = JS_DupValue(ctx, value);
	return 0;
}

/* ---- argument helpers ---------------------------------------------------- */

static int arg_float(JSContext *ctx, JSValueConst v, float *out, float fallback)
{
	if (JS_IsUndefined(v)) {
		*out = fallback;
		return 0;
	}
	return JS_ToFloat32(ctx, out, v);
}

/* [r, g, b] (0..255), or null/undefined for none. Returns -1 on error, 0 for none, 1 when set. */
static int arg_rgb(JSContext *ctx, JSValueConst v, int out[3])
{
	if (JS_IsUndefined(v) || JS_IsNull(v))
		return 0;
	for (uint32_t i = 0; i < 3; i++) {
		JSValue e = JS_GetPropertyUint32(ctx, v, i);
		int r = JS_ToInt32(ctx, &out[i], e);

		JS_FreeValue(ctx, e);
		if (r)
			return -1;
	}
	return 1;
}

/* ---- queue functions ------------------------------------------------------- */

static JSValue js_sprite(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	AthenaImage *image;
	float f[8];
	int c[4];

	if (argc < 12)
		return JS_ThrowTypeError(ctx, "SpriteBatch.sprite expects 12 arguments");
	image = athena_image_from_value(ctx, argv[0]);
	if (!image || note_image(ctx, argv[0], image))
		return JS_EXCEPTION;
	for (int i = 0; i < 8; i++)
		if (JS_ToFloat32(ctx, &f[i], argv[1 + i]))
			return JS_EXCEPTION;
	for (int i = 0; i < 4; i++)
		if (JS_ToInt32(ctx, &c[i], argv[9 + i]))
			return JS_EXCEPTION;
	athena_sb_sprite(image, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7],
		athena_sb_color(c[0], c[1], c[2], c[3]));
	return JS_UNDEFINED;
}

static JSValue js_flush(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	athena_sb_flush();
	return JS_UNDEFINED;
}

static JSValue js_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	float f[4];
	uint32_t color;

	if (argc < 5)
		return JS_ThrowTypeError(ctx, "SpriteBatch.rect expects 5 arguments");
	for (int i = 0; i < 4; i++)
		if (JS_ToFloat32(ctx, &f[i], argv[i]))
			return JS_EXCEPTION;
	if (JS_ToUint32(ctx, &color, argv[4]))
		return JS_EXCEPTION;
	athena_sb_rect(f[0], f[1], f[2], f[3], color);
	return JS_UNDEFINED;
}

/* quad(x1, y1, c1, x2, y2, c2, x3, y3, c3, x4, y4, c4): the argument order of Draw.quadGouraud. */
static JSValue js_quad(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	float xy[8];
	uint32_t c[4];

	if (argc < 12)
		return JS_ThrowTypeError(ctx, "SpriteBatch.quad expects 12 arguments");
	for (int i = 0; i < 4; i++) {
		if (JS_ToFloat32(ctx, &xy[i * 2], argv[i * 3]) ||
				JS_ToFloat32(ctx, &xy[i * 2 + 1], argv[i * 3 + 1]) ||
				JS_ToUint32(ctx, &c[i], argv[i * 3 + 2]))
			return JS_EXCEPTION;
	}
	athena_sb_quad(xy, c);
	return JS_UNDEFINED;
}

/* ---- marks ------------------------------------------------------------------ */

static char mark_names[SB_MARK_NAMES][32];
static int mark_count;

static int mark_id(JSContext *ctx, JSValueConst v, bool create)
{
	const char *name = JS_ToCString(ctx, v);
	int id = -1;

	if (!name)
		return -1;
	for (int i = 0; i < mark_count; i++)
		if (strcmp(mark_names[i], name) == 0) {
			id = i + 1;
			break;
		}
	if (id < 0 && create) {
		if (mark_count < SB_MARK_NAMES && strlen(name) < sizeof(mark_names[0])) {
			strcpy(mark_names[mark_count], name);
			id = ++mark_count;
		} else {
			JS_ThrowRangeError(ctx, "SpriteBatch.mark: too many or too long mark names");
		}
	}
	JS_FreeCString(ctx, name);
	return id;
}

static JSValue js_mark(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int id;

	if (argc < 1)
		return JS_ThrowTypeError(ctx, "SpriteBatch.mark expects a name");
	if (!athena_sb_capturing())
		return JS_UNDEFINED;
	id = mark_id(ctx, argv[0], true);
	if (id < 0)
		return JS_EXCEPTION;
	athena_sb_mark(id);
	return JS_UNDEFINED;
}

static JSValue js_unmark(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	athena_sb_mark(0);
	return JS_UNDEFINED;
}

/* stats(): [sprites, batches] sent since the last call (a frame, when called once per frame). */
static JSValue js_stats(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	uint32_t sprites, batches;
	JSValue array = JS_NewArray(ctx);

	athena_sb_stats(&sprites, &batches);
	JS_SetPropertyUint32(ctx, array, 0, JS_NewUint32(ctx, sprites));
	JS_SetPropertyUint32(ctx, array, 1, JS_NewUint32(ctx, batches));
	return array;
}

static JSValue js_capturing(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return JS_NewBool(ctx, athena_sb_capturing());
}

/* ---- layers ------------------------------------------------------------------- */

static void layer_finalizer(JSRuntime *rt, JSValue value)
{
	LayerObj *l = JS_GetOpaque(value, layer_class_id);

	if (!l)
		return;
	athena_sb_layer_destroy(l->core);
	for (int i = 0; i < l->nkeep; i++)
		JS_FreeValueRT(rt, l->keep[i]);
	free(l);
}

static JSClassDef layer_class = { "SpriteBatchLayer", .finalizer = layer_finalizer };

static bool layer_image_valid(void *user, const AthenaImage *image)
{
	const LayerObj *l = user;

	for (int i = 0; i < l->nkeep; i++)
		if (l->ptr[i] == image)
			return athena_image_peek(l->keep[i]) == image;
	return false;
}

static JSValue js_capture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	JSValue result, object;
	AthenaSbLayer *core;
	LayerObj *l;

	if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
		return JS_ThrowTypeError(ctx, "SpriteBatch.capture expects a function");
	if (!athena_sb_capture_begin())
		return JS_ThrowInternalError(ctx, "SpriteBatch.capture cannot be nested");
	result = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
	if (JS_IsException(result)) {
		athena_sb_capture_abort();
		cap_release(ctx);
		return result;
	}
	JS_FreeValue(ctx, result);
	core = athena_sb_capture_end();
	if (!core) {
		cap_release(ctx);
		return JS_ThrowOutOfMemory(ctx);
	}
	l = calloc(1, sizeof(*l));
	object = l ? JS_NewObjectClass(ctx, layer_class_id) : JS_EXCEPTION;
	if (!l || JS_IsException(object)) {
		athena_sb_layer_destroy(core);
		free(l);
		cap_release(ctx);
		return JS_IsException(object) ? object : JS_ThrowOutOfMemory(ctx);
	}
	l->core = core;
	l->nkeep = cap_n;
	for (int i = 0; i < cap_n; i++) {
		l->keep[i] = cap_keep[i];
		l->ptr[i] = cap_ptr[i];
	}
	cap_n = 0;                                  /* ownership moved to the layer */
	JS_SetOpaque(object, l);
	return object;
}

static LayerObj *layer_this(JSContext *ctx, JSValueConst v)
{
	LayerObj *l = JS_GetOpaque2(ctx, v, layer_class_id);

	if (l && !l->core) {
		JS_ThrowInternalError(ctx, "layer was freed");
		return NULL;
	}
	return l;
}

static JSValue layer_draw(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	LayerObj *l = layer_this(ctx, this_val);

	if (!l)
		return JS_EXCEPTION;
	athena_sb_layer_draw(l->core, layer_image_valid, l);
	return JS_UNDEFINED;
}

/* tint(name, r, g, b): channels 0..128 (128 neutral); false when no sprite has that mark. */
static JSValue layer_tint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	LayerObj *l = layer_this(ctx, this_val);
	int c[3], id;

	if (!l)
		return JS_EXCEPTION;
	if (argc < 4)
		return JS_ThrowTypeError(ctx, "Layer.tint expects (name, r, g, b)");
	for (int i = 0; i < 3; i++)
		if (JS_ToInt32(ctx, &c[i], argv[1 + i]))
			return JS_EXCEPTION;
	id = mark_id(ctx, argv[0], false);
	if (id < 0)
		return JS_FALSE;
	return JS_NewBool(ctx, athena_sb_layer_tint(l->core, id, c[0], c[1], c[2]));
}

static JSValue layer_free(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	LayerObj *l = JS_GetOpaque2(ctx, this_val, layer_class_id);

	if (!l)
		return JS_EXCEPTION;
	athena_sb_layer_destroy(l->core);
	l->core = NULL;
	for (int i = 0; i < l->nkeep; i++)
		JS_FreeValue(ctx, l->keep[i]);
	l->nkeep = 0;
	return JS_UNDEFINED;
}

/* dump(): [{ image, count, data: ArrayBuffer }] with the sprites as Image.drawList records (tests, tools). */
static JSValue layer_dump(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	LayerObj *l = layer_this(ctx, this_val);
	JSValue array;
	uint32_t out = 0;

	if (!l)
		return JS_EXCEPTION;
	array = JS_NewArray(ctx);
	for (uint32_t s = 0; s < athena_sb_layer_steps(l->core); s++) {
		uint32_t n = athena_sb_layer_step_count(l->core, s);
		AthenaImage *image = athena_sb_layer_step_image(l->core, s);
		JSValue item, buffer = JS_UNDEFINED, imageValue = JS_UNDEFINED;
		float *data;

		if (!image)
			continue;
		data = malloc(n * 16 * sizeof(float));
		if (!data)
			return JS_ThrowOutOfMemory(ctx);
		for (uint32_t i = 0; i < n; i++)
			athena_sb_layer_step_record(l->core, s, i, data + i * 16);
		buffer = JS_NewArrayBufferCopy(ctx, (const uint8_t *)data, n * 16 * sizeof(float));
		free(data);
		for (int k = 0; k < l->nkeep; k++)
			if (l->ptr[k] == image)
				imageValue = JS_DupValue(ctx, l->keep[k]);
		item = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, item, "image", imageValue);
		JS_SetPropertyStr(ctx, item, "count", JS_NewUint32(ctx, n));
		JS_SetPropertyStr(ctx, item, "data", buffer);
		JS_SetPropertyUint32(ctx, array, out++, item);
	}
	return array;
}

static const JSCFunctionListEntry layer_proto[] = {
	JS_CFUNC_DEF("draw", 0, layer_draw),
	JS_CFUNC_DEF("tint", 4, layer_tint),
	JS_CFUNC_DEF("dump", 0, layer_dump),
	JS_CFUNC_DEF("free", 0, layer_free),
};

/* ---- atlas ----------------------------------------------------------------------- */

typedef struct {
	AthenaSbAtlas *core;
	JSValue image;          /* borrowed: also the object's read-only "image" property */
} AtlasObj;

static void atlas_finalizer(JSRuntime *rt, JSValue value)
{
	AtlasObj *a = JS_GetOpaque(value, atlas_class_id);

	if (a) {
		athena_sb_atlas_destroy(a->core);
		free(a);
	}
}

static JSClassDef atlas_class = { "SpriteBatchAtlas", .finalizer = atlas_finalizer };

static int get_num(JSContext *ctx, JSValueConst obj, uint32_t index, float *out)
{
	JSValue v = JS_GetPropertyUint32(ctx, obj, index);
	int r = JS_ToFloat32(ctx, out, v);

	JS_FreeValue(ctx, v);
	return r;
}

static int get_named(JSContext *ctx, JSValueConst obj, const char *name, float *out, float fallback)
{
	JSValue v = JS_GetPropertyStr(ctx, obj, name);
	int r = 0;

	if (JS_IsUndefined(v))
		*out = fallback;
	else
		r = JS_ToFloat32(ctx, out, v);
	JS_FreeValue(ctx, v);
	return r;
}

/* new Atlas(image, info, screenW, screenH, ratio): info.sprites = { name: { uv, offset, source_size, draw_scale, w, h } } */
static JSValue atlas_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
	AthenaSbSpriteDef *defs = NULL;
	JSPropertyEnum *tab = NULL;
	uint32_t n = 0;
	JSValue sprites = JS_UNDEFINED, proto, object = JS_EXCEPTION;
	AtlasObj *a = NULL;
	float sw, sh, ratio;
	AthenaImage *image;

	if (argc < 5)
		return JS_ThrowTypeError(ctx, "new Atlas(image, info, screenWidth, screenHeight, ratio)");
	image = athena_image_from_value(ctx, argv[0]);
	if (!image || JS_ToFloat32(ctx, &sw, argv[2]) || JS_ToFloat32(ctx, &sh, argv[3]) ||
			JS_ToFloat32(ctx, &ratio, argv[4]))
		return JS_EXCEPTION;
	sprites = JS_GetPropertyStr(ctx, argv[1], "sprites");
	if (!JS_IsObject(sprites)) {
		JS_ThrowTypeError(ctx, "Atlas info needs a sprites object");
		goto fail;
	}
	if (JS_GetOwnPropertyNames(ctx, &tab, &n, sprites, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
		goto fail;
	defs = calloc(n ? n : 1, sizeof(*defs));
	if (!defs) {
		JS_ThrowOutOfMemory(ctx);
		goto fail;
	}
	for (uint32_t i = 0; i < n; i++) {
		JSValue s = JS_GetProperty(ctx, sprites, tab[i].atom), uv, off, src;
		AthenaSbSpriteDef *d = &defs[i];
		float ds;
		int bad;

		defs[i].name = JS_AtomToCString(ctx, tab[i].atom);
		uv = JS_GetPropertyStr(ctx, s, "uv");
		off = JS_GetPropertyStr(ctx, s, "offset");
		src = JS_GetPropertyStr(ctx, s, "source_size");
		bad = get_named(ctx, s, "w", &d->w, 0) || get_named(ctx, s, "h", &d->h, 0) ||
			get_named(ctx, s, "draw_scale", &ds, 1) ||
			get_num(ctx, uv, 0, &d->u1) || get_num(ctx, uv, 1, &d->v1) ||
			get_num(ctx, uv, 2, &d->u2) || get_num(ctx, uv, 3, &d->v2) ||
			get_num(ctx, off, 0, &d->offset_x) || get_num(ctx, off, 1, &d->offset_y) ||
			get_num(ctx, src, 0, &d->source_w) || get_num(ctx, src, 1, &d->source_h) ||
			get_named(ctx, s, "native_scale", &d->native_scale, ratio > 0 ? ds / ratio : 1);
		d->draw_scale = ds;
		JS_FreeValue(ctx, uv);
		JS_FreeValue(ctx, off);
		JS_FreeValue(ctx, src);
		JS_FreeValue(ctx, s);
		if (bad)
			goto fail;                   /* a conversion threw */
		if (!isfinite(d->w) || !isfinite(d->h) || !isfinite(d->u1) || !isfinite(d->v1) ||
				!isfinite(d->u2) || !isfinite(d->v2) || !isfinite(d->offset_x) ||
				!isfinite(d->offset_y) || !isfinite(d->source_w) || !isfinite(d->source_h) ||
				!isfinite(ds) || !isfinite(d->native_scale)) {
			JS_ThrowTypeError(ctx, "Atlas sprite \"%s\" is malformed", d->name);
			goto fail;
		}
	}
	a = calloc(1, sizeof(*a));
	if (!a) {
		JS_ThrowOutOfMemory(ctx);
		goto fail;
	}
	a->core = athena_sb_atlas_create(defs, (int)n, ratio, sw, sh);
	if (!a->core) {
		JS_ThrowOutOfMemory(ctx);
		goto fail;
	}
	proto = JS_GetPropertyStr(ctx, new_target, "prototype");
	object = JS_NewObjectProtoClass(ctx, proto, atlas_class_id);
	JS_FreeValue(ctx, proto);
	if (JS_IsException(object))
		goto fail;
	a->image = argv[0];
	JS_DefinePropertyValueStr(ctx, object, "image", JS_DupValue(ctx, argv[0]), JS_PROP_ENUMERABLE);
	JS_DefinePropertyValueStr(ctx, object, "sprites", JS_DupValue(ctx, sprites), JS_PROP_ENUMERABLE);
	JS_SetOpaque(object, a);
	a = NULL;
	goto done;
fail:
	if (a) {
		athena_sb_atlas_destroy(a->core);
		free(a);
	}
	object = JS_EXCEPTION;
done:
	for (uint32_t i = 0; defs && i < n; i++)
		JS_FreeCString(ctx, defs[i].name);
	free(defs);
	for (uint32_t i = 0; tab && i < n; i++)
		JS_FreeAtom(ctx, tab[i].atom);
	if (tab)
		js_free(ctx, tab);
	JS_FreeValue(ctx, sprites);
	return object;
}

typedef struct { AtlasObj *a; AthenaImage *image; int id; } AtlasCall;

/* Resolves `this`, the image and the sprite (a name or an index from id()). */
static int atlas_call(JSContext *ctx, JSValueConst this_val, JSValueConst which, AtlasCall *c)
{
	c->a = JS_GetOpaque2(ctx, this_val, atlas_class_id);
	if (!c->a)
		return -1;
	c->image = athena_image_peek(c->a->image);
	if (!c->image) {
		JS_ThrowInternalError(ctx, "the atlas image was freed");
		return -1;
	}
	if (note_image(ctx, c->a->image, c->image))
		return -1;
	if (JS_IsNumber(which)) {
		if (JS_ToInt32(ctx, &c->id, which))
			return -1;
		if (c->id < 0)
			c->id = -1;
	} else {
		const char *name = JS_ToCString(ctx, which);

		if (!name)
			return -1;
		c->id = athena_sb_atlas_find(c->a->core, name);
		if (c->id < 0)
			JS_ThrowReferenceError(ctx, "sprite \"%s\" is not in the atlas", name);
		JS_FreeCString(ctx, name);
		if (c->id < 0)
			return -1;
	}
	if (c->id < 0) {
		JS_ThrowRangeError(ctx, "invalid sprite index");
		return -1;
	}
	return 0;
}

/* draw(name | id, nx, ny, mul = 1, alpha = 1, rgb = null, flip = false) */
static JSValue atlas_draw(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	AtlasCall c;
	float nx, ny, mul = 1, alpha = 1;
	int rgb[3], has;

	if (argc < 3)
		return JS_ThrowTypeError(ctx, "Atlas.draw expects (name, x, y, ...)");
	if (atlas_call(ctx, this_val, argv[0], &c))
		return JS_EXCEPTION;
	if (JS_ToFloat32(ctx, &nx, argv[1]) || JS_ToFloat32(ctx, &ny, argv[2]) ||
			(argc > 3 && arg_float(ctx, argv[3], &mul, 1)) ||
			(argc > 4 && arg_float(ctx, argv[4], &alpha, 1)))
		return JS_EXCEPTION;
	has = argc > 5 ? arg_rgb(ctx, argv[5], rgb) : 0;
	if (has < 0)
		return JS_EXCEPTION;
	athena_sb_atlas_draw(c.a->core, c.image, c.id, nx, ny, mul, alpha, has ? rgb : NULL,
		argc > 6 && JS_ToBool(ctx, argv[6]));
	return JS_UNDEFINED;
}

/* drawRect(name | id, nx, ny, nw, nh, alpha = 1, rgb = null) */
static JSValue atlas_draw_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	AtlasCall c;
	float f[4], alpha = 1;
	int rgb[3], has;

	if (argc < 5)
		return JS_ThrowTypeError(ctx, "Atlas.drawRect expects (name, x, y, width, height, ...)");
	if (atlas_call(ctx, this_val, argv[0], &c))
		return JS_EXCEPTION;
	for (int i = 0; i < 4; i++)
		if (JS_ToFloat32(ctx, &f[i], argv[1 + i]))
			return JS_EXCEPTION;
	if (argc > 5 && arg_float(ctx, argv[5], &alpha, 1))
		return JS_EXCEPTION;
	has = argc > 6 ? arg_rgb(ctx, argv[6], rgb) : 0;
	if (has < 0)
		return JS_EXCEPTION;
	athena_sb_atlas_draw_rect(c.a->core, c.image, c.id, f[0], f[1], f[2], f[3], alpha, has ? rgb : NULL);
	return JS_UNDEFINED;
}

/* drawNinePatch(name | id, nx, ny, nw, nh, corner, alpha = 1, rgb = null) */
static JSValue atlas_draw_nine(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	AtlasCall c;
	float f[5], alpha = 1;
	int rgb[3], has;

	if (argc < 6)
		return JS_ThrowTypeError(ctx, "Atlas.drawNinePatch expects (name, x, y, width, height, corner, ...)");
	if (atlas_call(ctx, this_val, argv[0], &c))
		return JS_EXCEPTION;
	for (int i = 0; i < 5; i++)
		if (JS_ToFloat32(ctx, &f[i], argv[1 + i]))
			return JS_EXCEPTION;
	if (argc > 6 && arg_float(ctx, argv[6], &alpha, 1))
		return JS_EXCEPTION;
	has = argc > 7 ? arg_rgb(ctx, argv[7], rgb) : 0;
	if (has < 0)
		return JS_EXCEPTION;
	athena_sb_atlas_draw_nine(c.a->core, c.image, c.id, f[0], f[1], f[2], f[3], f[4], alpha, has ? rgb : NULL);
	return JS_UNDEFINED;
}

static JSValue atlas_size(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	AtlasCall c;
	float w, h;
	JSValue array;

	if (argc < 1)
		return JS_ThrowTypeError(ctx, "Atlas.size expects a sprite");
	c.a = JS_GetOpaque2(ctx, this_val, atlas_class_id);
	if (!c.a)
		return JS_EXCEPTION;
	{
		const char *name = JS_ToCString(ctx, argv[0]);

		if (!name)
			return JS_EXCEPTION;
		c.id = athena_sb_atlas_find(c.a->core, name);
		if (c.id < 0) {
			JS_ThrowReferenceError(ctx, "sprite \"%s\" is not in the atlas", name);
			JS_FreeCString(ctx, name);
			return JS_EXCEPTION;
		}
		JS_FreeCString(ctx, name);
	}
	athena_sb_atlas_size(c.a->core, c.id, &w, &h);
	array = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, array, 0, JS_NewFloat64(ctx, w));
	JS_SetPropertyUint32(ctx, array, 1, JS_NewFloat64(ctx, h));
	return array;
}

/* id(name): index for the faster draw(id, ...) form, or -1. */
static JSValue atlas_id(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	AtlasObj *a = JS_GetOpaque2(ctx, this_val, atlas_class_id);
	const char *name;
	int id;

	if (!a)
		return JS_EXCEPTION;
	if (argc < 1 || !(name = JS_ToCString(ctx, argv[0])))
		return JS_EXCEPTION;
	id = athena_sb_atlas_find(a->core, name);
	JS_FreeCString(ctx, name);
	return JS_NewInt32(ctx, id);
}

static const JSCFunctionListEntry atlas_proto[] = {
	JS_CFUNC_DEF("draw", 3, atlas_draw),
	JS_CFUNC_DEF("drawRect", 5, atlas_draw_rect),
	JS_CFUNC_DEF("drawNinePatch", 6, atlas_draw_nine),
	JS_CFUNC_DEF("size", 1, atlas_size),
	JS_CFUNC_DEF("id", 1, atlas_id),
};

/* ---- bitmap font --------------------------------------------------------------------- */

typedef struct {
	AthenaSbFont *core;
	JSValue image;          /* borrowed, see AtlasObj */
} FontObj;

static void font_finalizer(JSRuntime *rt, JSValue value)
{
	FontObj *f = JS_GetOpaque(value, font_class_id);

	if (f) {
		athena_sb_font_destroy(f->core);
		free(f);
	}
}

static JSClassDef font_class = { "SpriteBatchFont", .finalizer = font_finalizer };

static int arg_rgb3(JSContext *ctx, JSValueConst obj, const char *key, int out[3], const int def[3])
{
	JSValue v = JS_GetPropertyStr(ctx, obj, key);
	int r = arg_rgb(ctx, v, out);

	JS_FreeValue(ctx, v);
	if (r == 0)
		memcpy(out, def, 3 * sizeof(int));
	return r < 0 ? -1 : 0;
}

/*
 * new BitmapFont(image, info, screenWidth, screenHeight, ratio, text)
 * info: SpriteFont metrics { glyphs: { char: [x, y, w, h, cropX, cropY, left, advance, right] }, default, spacing, line_spacing }
 * text: { color: [r, g, b], shadow: [r, g, b], shadowOffset }
 */
static JSValue font_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
	AthenaSbGlyph *glyphs = NULL;
	JSPropertyEnum *tab = NULL;
	uint32_t n = 0, count = 0;
	JSValue glyphsObj = JS_UNDEFINED, proto, object = JS_EXCEPTION, defv;
	FontObj *f = NULL;
	float sw, sh, ratio, spacing, line, offset;
	int color[3], shadow[3];
	const int white[3] = { 34, 17, 34 }, sh_def[3] = { 206, 156, 95 };
	AthenaImage *image;
	const char *def;
	uint32_t def_cp = '*';

	if (argc < 6)
		return JS_ThrowTypeError(ctx, "new BitmapFont(image, info, screenWidth, screenHeight, ratio, text)");
	image = athena_image_from_value(ctx, argv[0]);
	if (!image || JS_ToFloat32(ctx, &sw, argv[2]) || JS_ToFloat32(ctx, &sh, argv[3]) ||
			JS_ToFloat32(ctx, &ratio, argv[4]) ||
			get_named(ctx, argv[1], "spacing", &spacing, 0) ||
			get_named(ctx, argv[1], "line_spacing", &line, 0) ||
			get_named(ctx, argv[5], "shadowOffset", &offset, 2) ||
			arg_rgb3(ctx, argv[5], "color", color, white) ||
			arg_rgb3(ctx, argv[5], "shadow", shadow, sh_def))
		return JS_EXCEPTION;
	glyphsObj = JS_GetPropertyStr(ctx, argv[1], "glyphs");
	if (!JS_IsObject(glyphsObj)) {
		JS_ThrowTypeError(ctx, "BitmapFont info needs a glyphs object");
		goto fail;
	}
	defv = JS_GetPropertyStr(ctx, argv[1], "default");
	def = JS_IsString(defv) ? JS_ToCString(ctx, defv) : NULL;
	if (def) {
		const unsigned char *p = (const unsigned char *)def;

		def_cp = *p;                    /* the default glyph is a single ASCII key ('*') */
		JS_FreeCString(ctx, def);
	}
	JS_FreeValue(ctx, defv);
	if (JS_GetOwnPropertyNames(ctx, &tab, &n, glyphsObj, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
		goto fail;
	glyphs = calloc(n ? n : 1, sizeof(*glyphs));
	if (!glyphs) {
		JS_ThrowOutOfMemory(ctx);
		goto fail;
	}
	for (uint32_t i = 0; i < n; i++) {
		JSValue g = JS_GetProperty(ctx, glyphsObj, tab[i].atom);
		const char *key = JS_AtomToCString(ctx, tab[i].atom);
		AthenaSbGlyph *o = &glyphs[count];
		float v[9];
		int bad = !key;

		for (uint32_t k = 0; !bad && k < 9; k++)
			bad = get_num(ctx, g, k, &v[k]);
		JS_FreeValue(ctx, g);
		for (uint32_t k = 0; !bad && k < 9; k++)
			if (!isfinite(v[k]))
				bad = 2;
		if (bad) {
			if (key)
				JS_FreeCString(ctx, key);
			if (bad == 2)
				JS_ThrowTypeError(ctx, "BitmapFont glyph is malformed");
			goto fail;
		}
		{   /* the key is one character: decode it as UTF-8 */
			const unsigned char *p = (const unsigned char *)key;
			uint32_t cp = *p++;
			int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;

			if (extra) {
				cp &= 0x3F >> extra;
				while (extra-- && (*p & 0xC0) == 0x80)
					cp = (cp << 6) | (*p++ & 0x3F);
			}
			o->codepoint = cp;
		}
		JS_FreeCString(ctx, key);
		o->x = v[0]; o->y = v[1]; o->w = v[2]; o->h = v[3];
		o->crop_x = v[4]; o->crop_y = v[5];
		o->left = v[6]; o->advance = v[7]; o->right = v[8];
		count++;
	}
	f = calloc(1, sizeof(*f));
	if (!f) {
		JS_ThrowOutOfMemory(ctx);
		goto fail;
	}
	f->core = athena_sb_font_create(glyphs, (int)count, def_cp, spacing, line, ratio, sw, sh,
		color, shadow, offset);
	if (!f->core) {
		JS_ThrowInternalError(ctx, "BitmapFont needs at least one glyph");
		goto fail;
	}
	proto = JS_GetPropertyStr(ctx, new_target, "prototype");
	object = JS_NewObjectProtoClass(ctx, proto, font_class_id);
	JS_FreeValue(ctx, proto);
	if (JS_IsException(object))
		goto fail;
	f->image = argv[0];
	JS_DefinePropertyValueStr(ctx, object, "image", JS_DupValue(ctx, argv[0]), JS_PROP_ENUMERABLE);
	JS_SetOpaque(object, f);
	f = NULL;
	goto done;
fail:
	if (f) {
		athena_sb_font_destroy(f->core);
		free(f);
	}
	object = JS_EXCEPTION;
done:
	free(glyphs);
	for (uint32_t i = 0; tab && i < n; i++)
		JS_FreeAtom(ctx, tab[i].atom);
	if (tab)
		js_free(ctx, tab);
	JS_FreeValue(ctx, glyphsObj);
	return object;
}

typedef struct { FontObj *f; AthenaImage *image; const char *text; } FontCall;

static int font_call(JSContext *ctx, JSValueConst this_val, JSValueConst text, FontCall *c, bool need_image)
{
	c->f = JS_GetOpaque2(ctx, this_val, font_class_id);
	if (!c->f)
		return -1;
	c->image = NULL;
	if (need_image) {
		c->image = athena_image_peek(c->f->image);
		if (!c->image) {
			JS_ThrowInternalError(ctx, "the font image was freed");
			return -1;
		}
		if (note_image(ctx, c->f->image, c->image))
			return -1;
	}
	c->text = JS_ToCString(ctx, text);
	return c->text ? 0 : -1;
}

static JSValue font_measure(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	FontCall c;
	float w;

	if (argc < 1)
		return JS_ThrowTypeError(ctx, "BitmapFont.measure expects a string");
	if (font_call(ctx, this_val, argv[0], &c, false))
		return JS_EXCEPTION;
	w = athena_sb_font_measure(c.f->core, c.text);
	JS_FreeCString(ctx, c.text);
	return JS_NewFloat64(ctx, w);
}

static JSValue font_height(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	FontCall c;
	float h;

	if (argc < 1)
		return JS_ThrowTypeError(ctx, "BitmapFont.height expects a string");
	if (font_call(ctx, this_val, argv[0], &c, false))
		return JS_EXCEPTION;
	h = athena_sb_font_height(c.f->core, c.text);
	JS_FreeCString(ctx, c.text);
	return JS_NewFloat64(ctx, h);
}

/* draw / drawShadow (text, nx, ny, rgb = default, alpha = 1) */
static JSValue font_draw_impl(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int mode)
{
	FontCall c;
	float nx, ny, alpha = 1;
	int rgb[3], has;

	if (argc < 3)
		return JS_ThrowTypeError(ctx, "BitmapFont.draw expects (text, x, y, ...)");
	if (JS_ToFloat32(ctx, &nx, argv[1]) || JS_ToFloat32(ctx, &ny, argv[2]) ||
			(argc > 4 && arg_float(ctx, argv[4], &alpha, 1)))
		return JS_EXCEPTION;
	has = argc > 3 ? arg_rgb(ctx, argv[3], rgb) : 0;
	if (has < 0 || font_call(ctx, this_val, argv[0], &c, true))
		return JS_EXCEPTION;
	if (mode)
		athena_sb_font_draw_shadow(c.f->core, c.image, c.text, nx, ny, has ? rgb : NULL, alpha);
	else
		athena_sb_font_draw(c.f->core, c.image, c.text, nx, ny, has ? rgb : NULL, alpha);
	JS_FreeCString(ctx, c.text);
	return JS_UNDEFINED;
}

static JSValue font_draw(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return font_draw_impl(ctx, this_val, argc, argv, 0);
}

static JSValue font_draw_shadow(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return font_draw_impl(ctx, this_val, argc, argv, 1);
}

/* drawCentered(text, cx, ny, rgb = default, alpha = 1, shadow = true) */
static JSValue font_draw_centered(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	FontCall c;
	float cx, ny, alpha = 1;
	int rgb[3], has;
	bool shadow = true;

	if (argc < 3)
		return JS_ThrowTypeError(ctx, "BitmapFont.drawCentered expects (text, centerX, y, ...)");
	if (JS_ToFloat32(ctx, &cx, argv[1]) || JS_ToFloat32(ctx, &ny, argv[2]) ||
			(argc > 4 && arg_float(ctx, argv[4], &alpha, 1)))
		return JS_EXCEPTION;
	has = argc > 3 ? arg_rgb(ctx, argv[3], rgb) : 0;
	if (argc > 5 && !JS_IsUndefined(argv[5]))
		shadow = JS_ToBool(ctx, argv[5]);
	if (has < 0 || font_call(ctx, this_val, argv[0], &c, true))
		return JS_EXCEPTION;
	athena_sb_font_draw_centered(c.f->core, c.image, c.text, cx, ny, has ? rgb : NULL, alpha, shadow);
	JS_FreeCString(ctx, c.text);
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry font_proto[] = {
	JS_CFUNC_DEF("measure", 1, font_measure),
	JS_CFUNC_DEF("height", 1, font_height),
	JS_CFUNC_DEF("draw", 3, font_draw),
	JS_CFUNC_DEF("drawShadow", 3, font_draw_shadow),
	JS_CFUNC_DEF("drawCentered", 3, font_draw_centered),
};

/* ---- module ------------------------------------------------------------------------------ */

static const JSCFunctionListEntry spritebatch_funcs[] = {
	JS_CFUNC_DEF("sprite", 12, js_sprite),
	JS_CFUNC_DEF("flush", 0, js_flush),
	JS_CFUNC_DEF("rect", 5, js_rect),
	JS_CFUNC_DEF("quad", 12, js_quad),
	JS_CFUNC_DEF("capture", 1, js_capture),
	JS_CFUNC_DEF("capturing", 0, js_capturing),
	JS_CFUNC_DEF("stats", 0, js_stats),
	JS_CFUNC_DEF("mark", 1, js_mark),
	JS_CFUNC_DEF("unmark", 0, js_unmark),
};

static JSValue make_class(JSContext *ctx, JSClassID id, const char *name, JSCFunction *ctor, int length,
	const JSCFunctionListEntry *proto_funcs, int nproto)
{
	JSValue proto = JS_NewObject(ctx), constructor;

	JS_SetPropertyFunctionList(ctx, proto, proto_funcs, nproto);
	JS_SetClassProto(ctx, id, JS_DupValue(ctx, proto));
	if (!ctor) {
		JS_FreeValue(ctx, proto);
		return JS_UNDEFINED;
	}
	constructor = JS_NewCFunction2(ctx, ctor, name, length, JS_CFUNC_constructor, 0);
	JS_SetConstructor(ctx, constructor, proto);
	JS_FreeValue(ctx, proto);
	return constructor;
}

static int spritebatch_module_init(JSContext *ctx, JSModuleDef *m)
{
	if (athena_register_class(ctx, &layer_class_id, &layer_class) < 0 ||
			athena_register_class(ctx, &atlas_class_id, &atlas_class) < 0 ||
			athena_register_class(ctx, &font_class_id, &font_class) < 0)
		return -1;
	make_class(ctx, layer_class_id, "Layer", NULL, 0, layer_proto, countof(layer_proto));
	JS_SetModuleExportList(ctx, m, spritebatch_funcs, countof(spritebatch_funcs));
	JS_SetModuleExport(ctx, m, "Atlas",
		make_class(ctx, atlas_class_id, "Atlas", atlas_ctor, 5, atlas_proto, countof(atlas_proto)));
	JS_SetModuleExport(ctx, m, "BitmapFont",
		make_class(ctx, font_class_id, "BitmapFont", font_ctor, 6, font_proto, countof(font_proto)));
	return 0;
}

JSModuleDef *athena_spritebatch_init(JSContext *ctx)
{
	JSModuleDef *m = athena_push_module(ctx, spritebatch_module_init, spritebatch_funcs,
		countof(spritebatch_funcs), "SpriteBatch");

	if (!m)
		return NULL;
	JS_AddModuleExport(ctx, m, "Atlas");
	JS_AddModuleExport(ctx, m, "BitmapFont");
	return m;
}
