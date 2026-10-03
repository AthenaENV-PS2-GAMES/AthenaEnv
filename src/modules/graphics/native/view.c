#include <math.h>
#include <stddef.h>

#include <athena/graphics/view.h>

/*
 * State and math of the 2D view (athena/graphics/view.h). No GS access here:
 * the clip rectangle lives in view_gs.c, so this file builds on a host.
 */

AthenaAffine2D athena_view_matrix = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
AthenaViewKind athena_view_current_kind = ATHENA_VIEW_IDENTITY;
uint32_t athena_view_culled;

static AthenaAffine2D view_world;
static bool view_world_set;

/* Matrix entries this close to 0 or 1 count as exact (float rounding). */
#define VIEW_EPSILON 1e-6f

void athena_affine_identity(AthenaAffine2D *m)
{
	m->xx = 1.0f;
	m->xy = 0.0f;
	m->yx = 0.0f;
	m->yy = 1.0f;
	m->tx = 0.0f;
	m->ty = 0.0f;
}

void athena_affine_multiply(const AthenaAffine2D *a, const AthenaAffine2D *b,
	AthenaAffine2D *out)
{
	AthenaAffine2D r;

	r.xx = a->xx * b->xx + a->xy * b->yx;
	r.xy = a->xx * b->xy + a->xy * b->yy;
	r.yx = a->yx * b->xx + a->yy * b->yx;
	r.yy = a->yx * b->xy + a->yy * b->yy;
	r.tx = a->xx * b->tx + a->xy * b->ty + a->tx;
	r.ty = a->yx * b->tx + a->yy * b->ty + a->ty;
	*out = r;
}

bool athena_affine_invert(const AthenaAffine2D *m, AthenaAffine2D *out)
{
	float det = m->xx * m->yy - m->xy * m->yx;
	AthenaAffine2D r;

	/* The EE has no infinities: a tiny determinant must be caught here. */
	if (fabsf(det) < 1e-12f)
		return false;
	r.xx = m->yy / det;
	r.xy = -m->xy / det;
	r.yx = -m->yx / det;
	r.yy = m->xx / det;
	r.tx = -(r.xx * m->tx + r.xy * m->ty);
	r.ty = -(r.yx * m->tx + r.yy * m->ty);
	*out = r;
	return true;
}

AthenaViewKind athena_affine_classify(const AthenaAffine2D *m)
{
	if (fabsf(m->xy) > VIEW_EPSILON || fabsf(m->yx) > VIEW_EPSILON)
		return ATHENA_VIEW_ROTATED;
	if (fabsf(m->xx - 1.0f) <= VIEW_EPSILON &&
		fabsf(m->yy - 1.0f) <= VIEW_EPSILON &&
		m->tx == 0.0f && m->ty == 0.0f)
		return ATHENA_VIEW_IDENTITY;
	return ATHENA_VIEW_AXIS;
}

void athena_affine_bounds(const AthenaAffine2D *m, const AthenaRect2D *rect,
	AthenaRect2D *out)
{
	float xs[4], ys[4];
	AthenaRect2D r;
	int i;

	athena_affine_apply(m, rect->x0, rect->y0, &xs[0], &ys[0]);
	athena_affine_apply(m, rect->x1, rect->y0, &xs[1], &ys[1]);
	athena_affine_apply(m, rect->x1, rect->y1, &xs[2], &ys[2]);
	athena_affine_apply(m, rect->x0, rect->y1, &xs[3], &ys[3]);
	r.x0 = r.x1 = xs[0];
	r.y0 = r.y1 = ys[0];
	for (i = 1; i < 4; i++) {
		if (xs[i] < r.x0) r.x0 = xs[i];
		if (xs[i] > r.x1) r.x1 = xs[i];
		if (ys[i] < r.y0) r.y0 = ys[i];
		if (ys[i] > r.y1) r.y1 = ys[i];
	}
	*out = r;
}

void athena_view_set(const AthenaAffine2D *m)
{
	if (!m) {
		athena_affine_identity(&athena_view_matrix);
		athena_view_current_kind = ATHENA_VIEW_IDENTITY;
		return;
	}
	athena_view_matrix = *m;
	athena_view_current_kind = athena_affine_classify(m);
	/* Snap near-exact entries so the fast paths see clean values. */
	if (athena_view_current_kind != ATHENA_VIEW_ROTATED) {
		athena_view_matrix.xy = 0.0f;
		athena_view_matrix.yx = 0.0f;
	}
	if (athena_view_current_kind == ATHENA_VIEW_IDENTITY)
		athena_affine_identity(&athena_view_matrix);
}

void athena_view_get(AthenaAffine2D *m)
{
	*m = athena_view_matrix;
}

float athena_view_scale(void)
{
	const AthenaAffine2D *m = &athena_view_matrix;

	return sqrtf(fabsf(m->xx * m->yy - m->xy * m->yx));
}

void athena_view_set_world(const AthenaAffine2D *m)
{
	view_world_set = m != NULL;
	if (m)
		view_world = *m;
}

bool athena_view_get_world(AthenaAffine2D *m)
{
	if (view_world_set && m)
		*m = view_world;
	return view_world_set;
}
