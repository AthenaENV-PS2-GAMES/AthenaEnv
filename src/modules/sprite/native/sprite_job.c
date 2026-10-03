#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <athena/job.h>
#include <athena/sprite.h>

/*
 * Background loading of a sheet (athena/sprite.h): the worker reads the
 * sheet file and decodes the texture (athena_image_decode() touches only
 * files and heap memory), so the script thread only parses and uploads.
 */

static int load_read(AthenaSpriteLoad *load)
{
	FILE *file = fopen(load->json_path, "rb");
	long size;

	if (!file)
		return ATHENA_SPRITE_LOAD_OPEN;
	if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
		fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return ATHENA_SPRITE_LOAD_READ;
	}
	if ((unsigned long)size > ATHENA_SPRITE_LOAD_MAX_BYTES) {
		fclose(file);
		return ATHENA_SPRITE_LOAD_TOO_LARGE;
	}
	load->text = malloc((size_t)size + 1);
	if (!load->text) {
		fclose(file);
		return ATHENA_SPRITE_LOAD_NOMEM;
	}
	if (fread(load->text, 1, (size_t)size, file) != (size_t)size) {
		fclose(file);
		return ATHENA_SPRITE_LOAD_READ;
	}
	fclose(file);
	load->text[size] = '\0';
	load->length = (size_t)size;
	return ATHENA_SPRITE_LOAD_OK;
}

static int load_run(AthenaJob *job, void *data)
{
	AthenaSpriteLoad *load = data;
	int result;

	if (load->json_path) {
		result = load_read(load);
		if (result < 0)
			return result;
		if (load->image_from_json && !load->image_path) {
			char *name = athena_sprite_find_meta_image(load->text, load->length);

			/* Not found here: the script thread loads what the parser finds. */
			if (name) {
				load->image_path = athena_sprite_path_join(load->json_path, name);
				free(name);
			}
		}
	}
	if (athena_job_should_stop(job))
		return ATHENA_SPRITE_LOAD_CANCELLED;
	if (load->image_path) {
		if (athena_image_decode(load->image_path, &load->image) < 0)
			return ATHENA_SPRITE_LOAD_IMAGE;
		load->has_image = true;
	}
	return ATHENA_SPRITE_LOAD_OK;
}

static void load_release(void *data)
{
	AthenaSpriteLoad *load = data;

	free(load->json_path);
	free(load->image_path);
	free(load->text);
	athena_image_buffer_release(&load->image);
	free(load);
}

static const AthenaJobType load_type = {
	"Sprite sheet", load_run, load_release, ATHENA_SPRITE_LOAD_CANCELLED,
	ATHENA_JOB_PRIORITY_IO,
};

static char *copy(const char *text)
{
	char *out;

	if (!text)
		return NULL;
	out = malloc(strlen(text) + 1);
	if (out)
		strcpy(out, text);
	return out;
}

struct AthenaJob *athena_sprite_load_submit(const char *json_path,
	const char *image_path, bool image_from_json)
{
	AthenaSpriteLoad *load = calloc(1, sizeof(*load));

	if (!load)
		return NULL;
	load->json_path = copy(json_path);
	load->image_path = copy(image_path);
	load->image_from_json = image_from_json;
	if ((json_path && !load->json_path) || (image_path && !load->image_path)) {
		load_release(load);
		return NULL;
	}
	return athena_job_submit(&load_type, load);
}
