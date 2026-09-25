/*
 * Video.probe(): reads an MPEG-1/2 elementary stream's headers on the CPU,
 * without libmpeg or the IPU. Start codes are found by a byte-at-a-time
 * state machine, so file reads may split headers anywhere.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <athena/video.h>

#include "libmpeg/libmpeg.h"

#define PROBE_READ_SIZE (32 * 1024)
/* Longest header prefix parsed: the sequence extension needs 6 bytes. */
#define PROBE_HEADER_BYTES 8

#define CODE_PICTURE 0x00
#define CODE_SEQUENCE_HEADER 0xB3
#define CODE_EXTENSION 0xB5

#define EXT_SEQUENCE 1
#define EXT_PICTURE_CODING 8
#define PICTURE_STRUCTURE_FRAME 3

static const float probe_frame_rates[16] = {
	0.0f, 24000.0f / 1001.0f, 24.0f, 25.0f, 30000.0f / 1001.0f, 30.0f, 50.0f,
	60000.0f / 1001.0f, 60.0f, 0, 0, 0, 0, 0, 0, 0,
};

typedef struct ProbeState {
	uint32_t window;      /* last bytes seen, to spot 00 00 01 xx */
	int code;             /* start code whose header is being collected, or -1 */
	uint8_t header[PROBE_HEADER_BYTES];
	int header_length;

	bool have_sequence;
	bool have_sequence_ext;
	int width, height, frame_rate_code, rate_n, rate_d;
	bool progressive;
	int chroma_format;

	bool picture_open;    /* picture header seen, coding extension not yet */
	int frames;
	int fields;
} ProbeState;

static void probe_close_picture(ProbeState *s, bool frame)
{
	if (!s->picture_open)
		return;
	s->picture_open = false;
	if (frame)
		s->frames++;
	else
		s->fields++;
}

static void probe_header(ProbeState *s)
{
	const uint8_t *b = s->header;

	if (s->code == CODE_SEQUENCE_HEADER && !s->have_sequence) {
		s->have_sequence = true;
		s->width = (b[0] << 4) | (b[1] >> 4);
		s->height = ((b[1] & 0x0F) << 8) | b[2];
		s->frame_rate_code = b[3] & 0x0F;
	} else if (s->code == CODE_EXTENSION) {
		int id = b[0] >> 4;

		if (id == EXT_SEQUENCE && s->have_sequence && !s->have_sequence_ext) {
			s->have_sequence_ext = true;
			s->progressive = (b[1] >> 3) & 1;
			s->chroma_format = (b[1] >> 1) & 3;
			s->width |= (((b[1] & 1) << 1) | (b[2] >> 7)) << 12;
			s->height |= ((b[2] >> 5) & 3) << 12;
			s->rate_n = (b[5] >> 5) & 3;
			s->rate_d = b[5] & 0x1F;
		} else if (id == EXT_PICTURE_CODING) {
			probe_close_picture(s, (b[2] & 3) == PICTURE_STRUCTURE_FRAME);
		}
	}
}

static void probe_byte(ProbeState *s, uint8_t byte)
{
	if (s->code >= 0) {
		s->header[s->header_length++] = byte;
		if (s->header_length == PROBE_HEADER_BYTES) {
			probe_header(s);
			s->code = -1;
		}
	}

	s->window = (s->window << 8) | byte;
	if ((s->window & 0xFFFFFF00u) != 0x00000100u)
		return;

	/* A start code ends any header still being collected. */
	if (s->code >= 0 && s->header_length >= 6)
		probe_header(s);
	s->code = -1;

	switch (byte) {
	case CODE_PICTURE:
		/* MPEG-1 pictures have no coding extension: always frames. */
		probe_close_picture(s, true);
		s->picture_open = true;
		break;
	case CODE_SEQUENCE_HEADER:
	case CODE_EXTENSION:
		s->code = byte;
		s->header_length = 0;
		break;
	}
}

static int probe_round(int value, int multiple)
{
	return (value + multiple - 1) / multiple * multiple;
}

AthenaVideoError athena_video_probe(const char *path, AthenaVideoInfo *info)
{
	ProbeState state;
	uint8_t *buffer;
	FILE *file;
	size_t length;

	if (!path || !*path || !info)
		return ATHENA_VIDEO_ERROR_ARGUMENT;
	memset(info, 0, sizeof(*info));

	file = fopen(path, "rb");
	if (!file)
		return ATHENA_VIDEO_ERROR_OPEN;
	buffer = malloc(PROBE_READ_SIZE);
	if (!buffer) {
		fclose(file);
		return ATHENA_VIDEO_ERROR_MEMORY;
	}

	memset(&state, 0, sizeof(state));
	state.code = -1;
	state.progressive = true;
	state.chroma_format = MPEG_CHROMA_FORMAT_420;
	state.window = 0xFFFFFFFFu;
	while ((length = fread(buffer, 1, PROBE_READ_SIZE, file)) > 0) {
		for (size_t i = 0; i < length; i++)
			probe_byte(&state, buffer[i]);
	}
	if (state.code >= 0 && state.header_length >= 6)
		probe_header(&state);
	/* A last picture without its coding extension (cut file) is a frame. */
	probe_close_picture(&state, true);
	free(buffer);
	fclose(file);

	if (!state.have_sequence || state.width == 0 || state.height == 0)
		return ATHENA_VIDEO_ERROR_FORMAT;

	info->mpeg2 = state.have_sequence_ext;
	info->progressive = !info->mpeg2 || state.progressive;
	info->chroma_format = state.chroma_format;
	info->width = state.width;
	info->height = state.height;
	info->coded_width = probe_round(state.width, 16);
	info->coded_height = probe_round(state.height, info->progressive ? 16 : 32);
	info->fps = probe_frame_rates[state.frame_rate_code] *
		((state.rate_n + 1.0f) / (state.rate_d + 1.0f));
	info->frames = state.frames + state.fields / 2;
	info->duration = info->fps > 0.0f ? info->frames / info->fps : 0.0f;
	info->supported = info->coded_width <= ATHENA_VIDEO_MAX_SIZE &&
		info->coded_height <= ATHENA_VIDEO_MAX_SIZE &&
		info->chroma_format == MPEG_CHROMA_FORMAT_420 && info->fps > 0.0f;
	return ATHENA_VIDEO_OK;
}
