#include <stdlib.h>
#include <string.h>

#include <athena/graphics.h>

#include <athena/image.h>

static GSSURFACE *image_alloc_surface(bool delayed)
{
	GSSURFACE *surface = malloc(sizeof(*surface));
	if (!surface)
		return NULL;

	if (graphics_surface_init(surface) < 0) {
		free(surface);
		return NULL;
	}

	surface->Delayed = delayed;
	return surface;
}

static void image_release_surface(GSSURFACE *surface)
{
	if (!surface)
		return;

	graphics_surface_release(surface);
	free(surface->Mem);
	free(surface->Clut);
	free(surface);
}

static void image_set_error(AthenaImage *image, AthenaImageLoadError code)
{
	const char *stage = "decode";
	const char *message = "image decoding failed";

	if (!image)
		return;
	switch (code) {
	case ATHENA_IMAGE_LOAD_OPEN:
		stage = "open";
		message = "image file could not be opened";
		break;
	case ATHENA_IMAGE_LOAD_FORMAT:
		stage = "decode";
		message = "unsupported image format";
		break;
	case ATHENA_IMAGE_LOAD_SURFACE:
		stage = "surface";
		message = "image surface could not be created";
		break;
	case ATHENA_IMAGE_LOAD_UPLOAD:
		stage = "upload";
		message = "texture could not be uploaded to VRAM";
		break;
	case ATHENA_IMAGE_LOAD_DECODE:
	default:
		break;
	}
	image->error_code = code;
	strncpy(image->error_stage, stage, sizeof(image->error_stage) - 1);
	image->error_stage[sizeof(image->error_stage) - 1] = '\0';
	strncpy(image->error_message, message, sizeof(image->error_message) - 1);
	image->error_message[sizeof(image->error_message) - 1] = '\0';
}

AthenaImage *athena_image_create_empty(bool delayed)
{
	AthenaImage *image = calloc(1, sizeof(*image));
	if (!image)
		return NULL;

	image->surface = image_alloc_surface(delayed);
	if (!image->surface) {
		free(image);
		return NULL;
	}

	image->owns_surface = true;
	image->delayed = delayed;
	image->status = ATHENA_IMAGE_STATUS_QUEUED;
	image->color = 0x80808080;
	return image;
}

AthenaImage *athena_image_create(const char *path, bool delayed)
{
	AthenaImage *image = athena_image_create_empty(delayed);
	if (!image)
		return NULL;

	if (path) {
		image->path = strdup(path);
		AthenaImageLoadError error = ATHENA_IMAGE_LOAD_OK;
		if (!image->path ||
			load_image_ex(image->surface, path, delayed, &error) < 0) {
			image_set_error(image, error);
			athena_image_destroy(image);
			return NULL;
		}

		image->loaded = true;
		image->status = ATHENA_IMAGE_STATUS_DECODED;
		image->width = (float)image->surface->Width;
		image->height = (float)image->surface->Height;
		image->endx = image->width;
		image->endy = image->height;
	}

	return image;
}

const char *athena_image_error_code_name(AthenaImageLoadError code)
{
	switch (code) {
	case ATHENA_IMAGE_LOAD_OPEN:
		return "open_failed";
	case ATHENA_IMAGE_LOAD_FORMAT:
		return "unsupported_format";
	case ATHENA_IMAGE_LOAD_SURFACE:
		return "surface_failed";
	case ATHENA_IMAGE_LOAD_UPLOAD:
		return "upload_failed";
	case ATHENA_IMAGE_LOAD_DECODE:
	default:
		return "decode_failed";
	}
}

void athena_image_set_failed(AthenaImage *image, const char *path,
	AthenaImageLoadError code)
{
	if (!image)
		return;
	if (path) {
		free(image->path);
		image->path = strdup(path);
	}
	image->loaded = false;
	image->loading = false;
	image->failed = true;
	image->status = ATHENA_IMAGE_STATUS_FAILED;
	image_set_error(image, code == ATHENA_IMAGE_LOAD_OK ?
		ATHENA_IMAGE_LOAD_DECODE : code);
}

static uint32_t image_clut_size(uint8_t psm)
{
	if (psm == GS_PSM_T4)
		return athena_surface_size(8, 2, GS_PSM_CT32);
	if (psm == GS_PSM_T8)
		return athena_surface_size(16, 16, GS_PSM_CT32);
	return 0;
}

void athena_image_buffer_release(AthenaImageBuffer *buffer)
{
	if (!buffer)
		return;
	free(buffer->mem);
	free(buffer->clut);
	buffer->mem = NULL;
	buffer->clut = NULL;
	buffer->bytes = 0;
}

int athena_image_decode(const char *path, AthenaImageBuffer *buffer)
{
	/*
	 * The format loaders fill a GSSURFACE, but this one is a private
	 * parameter block: it is never registered with the texture manager and
	 * only its CPU fields are read back.
	 */
	GSSURFACE scratch;
	uint32_t size;

	if (!buffer)
		return -1;
	memset(buffer, 0, sizeof(*buffer));
	if (!path) {
		buffer->error = ATHENA_IMAGE_LOAD_OPEN;
		return -1;
	}
	memset(&scratch, 0, sizeof(scratch));
	scratch.PSM = GS_PSM_CT32;
	scratch.ClutPSM = GS_PSM_CT32;
	scratch.Filter = GS_FILTER_NEAREST;
	if (load_image_ex(&scratch, path, true, &buffer->error) < 0) {
		free(scratch.Mem);
		free(scratch.Clut);
		if (buffer->error == ATHENA_IMAGE_LOAD_OK)
			buffer->error = ATHENA_IMAGE_LOAD_DECODE;
		return -1;
	}
	size = athena_surface_size(scratch.Width, scratch.Height, scratch.PSM);
	if (!scratch.Mem || scratch.Width == 0 || scratch.Height == 0 ||
		size == UINT32_MAX ||
		((scratch.PSM == GS_PSM_T4 || scratch.PSM == GS_PSM_T8) &&
			!scratch.Clut)) {
		free(scratch.Mem);
		free(scratch.Clut);
		buffer->error = ATHENA_IMAGE_LOAD_DECODE;
		return -1;
	}
	buffer->width = scratch.Width;
	buffer->height = scratch.Height;
	buffer->tbw = scratch.TBW;
	buffer->filter = scratch.Filter;
	buffer->psm = scratch.PSM;
	buffer->clut_psm = scratch.ClutPSM;
	buffer->clut_storage_mode = scratch.ClutStorageMode;
	buffer->mem = scratch.Mem;
	buffer->clut = scratch.Clut;
	buffer->bytes = size + (scratch.Clut ? image_clut_size(scratch.PSM) : 0);
	buffer->error = ATHENA_IMAGE_LOAD_OK;
	return 0;
}

int athena_image_apply_buffer(AthenaImage *image, const char *path,
	AthenaImageBuffer *buffer)
{
	GSSURFACE *surface;
	char *new_path = NULL;

	if (!image || !buffer) {
		athena_image_buffer_release(buffer);
		return -1;
	}
	if (buffer->error != ATHENA_IMAGE_LOAD_OK || !buffer->mem) {
		AthenaImageLoadError code = buffer->error;
		athena_image_buffer_release(buffer);
		athena_image_set_failed(image, path, code);
		return -1;
	}
	if (image->surface && graphics_surface_is_locked(image->surface)) {
		athena_image_buffer_release(buffer);
		athena_image_set_failed(image, path, ATHENA_IMAGE_LOAD_SURFACE);
		return -1;
	}
	surface = image_alloc_surface(image->delayed);
	if (path)
		new_path = strdup(path);
	if (!surface || (path && !new_path)) {
		free(surface);
		free(new_path);
		athena_image_buffer_release(buffer);
		athena_image_set_failed(image, path, ATHENA_IMAGE_LOAD_SURFACE);
		return -1;
	}
	surface->Width = buffer->width;
	surface->Height = buffer->height;
	surface->TBW = buffer->tbw;
	surface->Filter = buffer->filter;
	surface->PSM = buffer->psm;
	surface->ClutPSM = buffer->clut_psm;
	surface->ClutStorageMode = buffer->clut_storage_mode;
	surface->Mem = buffer->mem;
	surface->Clut = buffer->clut;
	buffer->mem = NULL;
	buffer->clut = NULL;
	buffer->bytes = 0;

	if (image->owns_surface)
		image_release_surface(image->surface);
	image->surface = surface;
	image->owns_surface = true;
	free(image->path);
	image->path = new_path;
	image->loaded = true;
	image->loading = false;
	image->failed = false;
	image->status = ATHENA_IMAGE_STATUS_DECODED;
	image->error_code = ATHENA_IMAGE_LOAD_OK;
	image->error_stage[0] = '\0';
	image->error_message[0] = '\0';
	image->width = (float)surface->Width;
	image->height = (float)surface->Height;
	image->startx = 0.0f;
	image->starty = 0.0f;
	image->endx = image->width;
	image->endy = image->height;
	image->angle = 0.0f;
	image->color = 0x80808080;
	return 0;
}

int athena_image_load_path(AthenaImage *image, const char *path)
{
	AthenaImageBuffer buffer;

	if (!image || !image->surface || !path ||
		graphics_surface_is_locked(image->surface))
		return -1;
	athena_image_decode(path, &buffer);
	return athena_image_apply_buffer(image, path, &buffer);
}

AthenaImage *athena_image_wrap(GSSURFACE *surface, bool delayed)
{
	AthenaImage *image;

	if (!surface)
		return NULL;

	image = calloc(1, sizeof(*image));
	if (!image)
		return NULL;

	image->surface = surface;
	image->owns_surface = false;
	image->delayed = delayed;
	image->loaded = true;
	image->width = (float)surface->Width;
	image->height = (float)surface->Height;
	image->endx = image->width;
	image->endy = image->height;
	image->color = 0x80808080;
	return image;
}

void athena_image_destroy(AthenaImage *image)
{
	if (!image)
		return;

	if (image->owns_surface)
		image_release_surface(image->surface);
	free((void *)image->path);
	free(image);
}

bool athena_image_is_loaded(const AthenaImage *image)
{
	return image && image->loaded && image->surface;
}

void athena_image_refresh_status(AthenaImage *image)
{
	if (!image || !image->surface || !image->loaded)
		return;
	if (image->surface->Vram & GRAPHICS_TRANSFER_REQUEST_MASK ||
		image->surface->VramClut & GRAPHICS_TRANSFER_REQUEST_MASK) {
		image->status = ATHENA_IMAGE_STATUS_UPLOAD_PENDING;
	} else if (image->status == ATHENA_IMAGE_STATUS_UPLOAD_PENDING ||
		(image->status == ATHENA_IMAGE_STATUS_DECODED &&
			image->surface->Vram != 0)) {
		image->status = ATHENA_IMAGE_STATUS_READY;
	}
}

void athena_image_draw(AthenaImage *image, float x, float y, float width,
	float height, float startx, float starty, float endx, float endy,
	float angle, uint32_t color)
{
	if (!athena_image_is_loaded(image))
		return;
	if (image->delayed && image->status == ATHENA_IMAGE_STATUS_DECODED)
		image->status = ATHENA_IMAGE_STATUS_UPLOAD_PENDING;

	if (angle != 0.0f) {
		draw_image_rotate(image->surface, x, y, width, height,
			startx, starty, endx, endy, angle, color);
	} else {
		draw_image(image->surface, x, y, width, height,
			startx, starty, endx, endy, color);
	}
}

bool athena_image_lock(AthenaImage *image)
{
	int result;

	if (!athena_image_is_loaded(image))
		return false;
	if (graphics_surface_is_locked(image->surface))
		return true;

	/*
	 * Locking must leave the texture resident. An asynchronous bind only
	 * queues an upload; its VIF marker is normally emitted by draw_image().
	 * Since a locked texture is already treated as resident by subsequent
	 * draws, that marker would never be emitted and the upload could remain
	 * pending forever.
	 */
	result = graphics_surface_bind_sync(image->surface);
	if (result == GRAPHICS_BIND_ERROR)
		return false;

	result = graphics_surface_lock(image->surface);
	if (result != 0)
		image->status = ATHENA_IMAGE_STATUS_READY;
	return result != 0;
}

int athena_image_upload(AthenaImage *image, bool lock)
{
	GSSURFACE *surface;

	if (!athena_image_is_loaded(image))
		return -1;
	if (lock ? athena_image_lock(image) :
		graphics_surface_bind_sync(image->surface) != GRAPHICS_BIND_ERROR) {
		image->status = ATHENA_IMAGE_STATUS_READY;
		return 0;
	}
	/*
	 * VRAM could not hold the texture. The failed image keeps no pixels, so
	 * a large texture does not also stay pinned in CPU memory.
	 */
	surface = image->surface;
	if (image->owns_surface && !graphics_surface_is_locked(surface)) {
		graphics_surface_release(surface);
		free(surface->Mem);
		free(surface->Clut);
		surface->Mem = NULL;
		surface->Clut = NULL;
	}
	athena_image_set_failed(image, NULL, ATHENA_IMAGE_LOAD_UPLOAD);
	return -1;
}

bool athena_image_unlock(AthenaImage *image)
{
	if (!athena_image_is_loaded(image))
		return false;
	return graphics_surface_unlock(image->surface) != 0;
}

bool athena_image_locked(const AthenaImage *image)
{
	return athena_image_is_loaded(image) &&
		graphics_surface_is_locked(image->surface);
}

bool athena_image_optimize(AthenaImage *image)
{
	if (!athena_image_is_loaded(image) || !image->owns_surface ||
		image->surface->PSM != GS_PSM_CT24)
		return false;
	if (graphics_surface_is_locked(image->surface))
		return false;

	graphics_surface_release(image->surface);
	athena_texture_optimize(image->surface);
	return true;
}

bool athena_image_copy_vram_block(AthenaImage *source, int source_x,
	int source_y, AthenaImage *destination, int destination_x, int destination_y)
{
	if (!athena_image_is_loaded(source) || !athena_image_is_loaded(destination))
		return false;

	return graphics_surface_copy_block(source->surface, source_x, source_y,
		destination->surface, destination_x, destination_y) == 0;
}

void athena_image_set_dimensions(AthenaImage *image, float width, float height)
{
	if (!image)
		return;
	image->width = width;
	image->height = height;
}

void athena_image_set_src_rect(AthenaImage *image, float startx, float starty,
	float endx, float endy)
{
	if (!image)
		return;
	image->startx = startx;
	image->starty = starty;
	image->endx = endx;
	image->endy = endy;
}

void athena_image_set_angle(AthenaImage *image, float angle)
{
	if (image)
		image->angle = angle;
}

void athena_image_set_color(AthenaImage *image, Color color)
{
	if (image)
		image->color = color;
}

void athena_image_set_filter(AthenaImage *image, uint32_t filter)
{
	if (athena_image_is_loaded(image))
		image->surface->Filter = filter;
}
