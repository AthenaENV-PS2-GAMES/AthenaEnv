#ifndef ATHENA_IMAGE_NATIVE_H
#define ATHENA_IMAGE_NATIVE_H

#include <stdbool.h>
#include <stdint.h>

#include <athena/graphics.h>

typedef enum AthenaImageStatus {
	ATHENA_IMAGE_STATUS_QUEUED,
	ATHENA_IMAGE_STATUS_LOADING,
	ATHENA_IMAGE_STATUS_DECODED,
	ATHENA_IMAGE_STATUS_UPLOAD_PENDING,
	ATHENA_IMAGE_STATUS_READY,
	ATHENA_IMAGE_STATUS_FAILED,
	ATHENA_IMAGE_STATUS_CANCELLED,
} AthenaImageStatus;

typedef struct AthenaImage {
	char *path;
	GSSURFACE *surface;
	bool delayed;
	bool loaded;
	bool loading;
	bool failed;
	AthenaImageStatus status;
	AthenaImageLoadError error_code;
	char error_stage[16];
	char error_message[96];
	bool owns_surface;
	Color color;
	float width;
	float height;
	float startx;
	float starty;
	float endx;
	float endy;
	float angle;
} AthenaImage;

/*
 * CPU-side result of decoding an image file. Producing one touches only file
 * I/O and heap memory: no QuickJS, texture manager, VRAM or GS state. It is
 * therefore safe to build on a worker thread and hand to the main thread.
 */
typedef struct AthenaImageBuffer {
	uint32_t width;
	uint32_t height;
	uint32_t tbw;
	uint32_t filter;
	uint8_t psm;
	uint8_t clut_psm;
	uint8_t clut_storage_mode;
	uint32_t *mem;
	uint32_t *clut;
	uint32_t bytes;
	AthenaImageLoadError error;
} AthenaImageBuffer;

/* Thread-safe: decodes `path` into CPU memory owned by `buffer`. */
int athena_image_decode(const char *path, AthenaImageBuffer *buffer);
/* Thread-safe: frees CPU memory still owned by `buffer`. */
void athena_image_buffer_release(AthenaImageBuffer *buffer);
/*
 * Main thread only: moves a decoded buffer into `image`, replacing its
 * surface. Consumes the buffer on success and failure. A failed buffer marks
 * the image as failed without creating a partially initialized surface.
 */
int athena_image_apply_buffer(AthenaImage *image, const char *path,
	AthenaImageBuffer *buffer);
void athena_image_set_failed(AthenaImage *image, const char *path,
	AthenaImageLoadError code);
const char *athena_image_error_code_name(AthenaImageLoadError code);

AthenaImage *athena_image_create(const char *path, bool delayed);
AthenaImage *athena_image_create_empty(bool delayed);
int athena_image_load_path(AthenaImage *image, const char *path);
AthenaImage *athena_image_wrap(GSSURFACE *surface, bool delayed);
void athena_image_destroy(AthenaImage *image);
bool athena_image_is_loaded(const AthenaImage *image);
void athena_image_refresh_status(AthenaImage *image);
void athena_image_draw(AthenaImage *image, float x, float y, float width,
	float height, float startx, float starty, float endx, float endy,
	float angle, uint32_t color);
bool athena_image_lock(AthenaImage *image);
/*
 * Main thread only: makes a loaded image resident in VRAM now, and locks it
 * when `lock` is set. On failure the pixels are released and the image is
 * marked failed at the "upload" stage.
 */
int athena_image_upload(AthenaImage *image, bool lock);
bool athena_image_unlock(AthenaImage *image);
bool athena_image_locked(const AthenaImage *image);
bool athena_image_optimize(AthenaImage *image);
bool athena_image_copy_vram_block(AthenaImage *source, int source_x,
	int source_y, AthenaImage *destination, int destination_x, int destination_y);

#endif
