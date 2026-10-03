/*
 * Host test of src/modules/video/native/video.c: the real playback state
 * machine against a fake libmpeg that follows the vendored decoder's
 * contract (native/libmpeg/README.md):
 *
 * - data comes only through the data callback, which sends it with
 *   dmaKit_send(); every picture here takes PICTURE_BYTES of it;
 * - the init callback runs before the first picture of a sequence and may
 *   refuse it by returning NULL, which fails the picture;
 * - once the data callback reports the end, it is not called again.
 *
 * The decoder worker runs as a real pthread (host_runtime.h). clock() is
 * driven by the test, and every step waits for the worker to go idle
 * first, so timing checks stay exact.
 * Run with tests/host/run.sh from the repository root.
 */
#define _GNU_SOURCE
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Shared with the worker thread, hence the atomics. */
static uint64_t fake_ms;
/* When set, every clock() read advances time (lets bounded waits expire). */
static int clock_autostep;
static clock_t host_clock(void)
{
	uint64_t ms = __atomic_load_n(&clock_autostep, __ATOMIC_SEQ_CST) ?
		__atomic_add_fetch(&fake_ms, 1, __ATOMIC_SEQ_CST) :
		__atomic_load_n(&fake_ms, __ATOMIC_SEQ_CST);
	return (clock_t)(ms * (CLOCKS_PER_SEC / 1000));
}
#define clock host_clock
#include "video.c"
#undef clock
#include "video_probe.c"
#include "host_runtime.h"

/* --- Fake libmpeg --------------------------------------------------------- */

#define PICTURE_BYTES 4096

static struct {
	int (*data_cb)(void *);
	void *(*init_cb)(void *, MPEGSequenceInfo *);
	s64 *pts;
	/* Sequence the next stream announces (display size, before padding). */
	int width, height, chroma;
	float rate;
	int fail_alloc;
	int alloc_failed;
	int started;
	int eof;
	size_t received;
	int pictures;
	int init_calls, destroy_calls;
	/* When set, the IPU never finishes a DMA transfer. */
	int stall;
	/*
	 * When set, the stream ends with a sequence end code after this many
	 * pictures: like libmpeg, the decoder reads a little past it (which
	 * makes a looping video wrap the file) and then fails the picture.
	 */
	int end_code_after;
	int run_pictures;
} dec;

static void fake_reset_sequence(void)
{
	dec.width = 640;
	dec.height = 360;
	dec.chroma = MPEG_CHROMA_FORMAT_420;
	dec.rate = 60.0f;
	dec.fail_alloc = 0;
	dec.stall = 0;
	dec.end_code_after = 0;
	host_d4_chcr = 0;
}

void MPEG_Initialize(int (*data_cb)(void *), void *data_param,
	void *(*init_cb)(void *, MPEGSequenceInfo *), void *init_param, s64 *pts)
{
	dec.data_cb = data_cb;
	dec.init_cb = init_cb;
	dec.pts = pts;
	dec.started = 0;
	dec.eof = 0;
	dec.received = 0;
	dec.alloc_failed = 0;
	dec.run_pictures = 0;
	dec.init_calls++;
}

void MPEG_Destroy(void)
{
	dec.started = 0;
	dec.destroy_calls++;
}

int MPEG_AllocFailed(void)
{
	return dec.alloc_failed;
}

static int fake_picture(void *data, s64 *pts)
{
	if (dec.end_code_after && dec.run_pictures >= dec.end_code_after) {
		if (!dec.eof && !dec.data_cb(NULL))
			dec.eof = 1;
		return 0;
	}
	dec.run_pictures++;
	while (dec.received < PICTURE_BYTES) {
		if (dec.eof || !dec.data_cb(NULL)) {
			dec.eof = 1;
			return 0;
		}
	}
	dec.received -= PICTURE_BYTES;

	if (!dec.started) {
		MPEGSequenceInfo si;

		memset(&si, 0, sizeof(si));
		si.m_DisplayWidth = dec.width;
		si.m_DisplayHeight = dec.height;
		si.m_Width = (dec.width + 15) & ~15;
		si.m_Height = (dec.height + 15) & ~15;
		si.m_ChromaFmt = dec.chroma;
		si.m_FrameRate = dec.rate;
		si.m_MSPerFrame = dec.rate > 0 ? (int)(1000.0f / dec.rate + 0.5f) : 0;
		data = dec.init_cb(NULL, &si);
		if (!data)
			return 0;
		if (dec.fail_alloc) {
			dec.alloc_failed = 1;
			return 0;
		}
		dec.started = 1;
		/* The decoder writes whole macroblock frames into the buffer. */
		CHECK(malloc_usable_size(data) >= (size_t)si.m_Width * si.m_Height * 4,
			"frame buffer smaller than the coded frame");
	}
	*pts = ++dec.pictures;
	return 1;
}

int (*MPEG_Picture)(void *, s64 *) = fake_picture;

/* --- Fake dmaKit / graphics ------------------------------------------------ */

int dmaKit_chan_init(unsigned int channel) { return 0; }

int dmaKit_send(unsigned int channel, void *data, unsigned int qwc)
{
	CHECK(channel == DMA_CHANNEL_TOIPU, "DMA sent to channel %u", channel);
	CHECK(!(host_d4_chcr & DMA_CHCR_STR), "DMA sent while the channel is busy");
	dec.received += (size_t)qwc * 16;
	if (dec.stall)
		host_d4_chcr |= DMA_CHCR_STR;
	return 0;
}

static int surface_releases, surface_invalidates;
static struct { float w, h, startx, starty, endx, endy, angle; Color color; int calls; } last_draw;

int graphics_surface_init(GSSURFACE *surface)
{
	memset(surface, 0, sizeof(*surface));
	return 0;
}
void graphics_surface_release(GSSURFACE *surface) { surface->Vram = 0; surface_releases++; }
void graphics_surface_invalidate(GSSURFACE *surface) { surface->Vram = 0; surface_invalidates++; }
void athena_calculate_tbw(GSSURFACE *surface) { surface->TBW = (surface->Width + 63) / 64; }
void draw_image(GSSURFACE *source, float x, float y, float width, float height,
	float startx, float starty, float endx, float endy, Color color)
{
	last_draw.w = width;
	last_draw.h = height;
	last_draw.startx = startx;
	last_draw.starty = starty;
	last_draw.endx = endx;
	last_draw.endy = endy;
	last_draw.angle = 0;
	last_draw.color = color;
	last_draw.calls++;
}
void draw_image_rotate(GSSURFACE *source, float x, float y, float width,
	float height, float startx, float starty, float endx, float endy,
	float angle, Color color)
{
	draw_image(source, x, y, width, height, startx, starty, endx, endy, color);
	last_draw.angle = angle;
}

/* --- Helpers ------------------------------------------------------------------ */

static char dir[256];

/* Writes a stream of `pictures` fake pictures and returns its path.
 * The path stays valid for the whole run. */
static const char *stream(const char *name, int pictures)
{
	char path[512];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	file = fopen(path, "wb");
	for (int i = 0; i < pictures * PICTURE_BYTES; i++)
		fputc(i & 0xFF, file);
	fclose(file);
	return strdup(path); /* leaked: a few short strings per run */
}

static AthenaVideo *open_ok(const char *path)
{
	AthenaVideoError error;
	AthenaVideo *video = athena_video_create(path, &error);

	CHECK(video && error == ATHENA_VIDEO_OK, "create(%s): %s", path,
		athena_video_error_name(error));
	return video;
}

static AthenaVideoError open_error(const char *path)
{
	AthenaVideoError error;
	AthenaVideo *video = athena_video_create(path, &error);

	CHECK(!video, "create(%s) succeeded", path);
	athena_video_destroy(video);
	return error;
}

/* Advances the fake EE clock; the worker reads it too. */
static void advance(int ms)
{
	__atomic_add_fetch(&fake_ms, ms, __ATOMIC_SEQ_CST);
}

/* Waits (real time) until the worker has nothing left to do. */
static void settle(AthenaVideo *video)
{
	double deadline = now_ms() + 2000;
	bool idle = false;

	while (!idle && now_ms() < deadline) {
		video_lock(video);
		/* Idle: no restart pending, nothing decoding, and nothing it could decode. */
		idle = !video->restart_requested;
		for (int i = 0; idle && i < video->slot_count; i++) {
			if (video->slots[i].state == SLOT_DECODING ||
				(video->slots[i].state == SLOT_FREE && !video->end_queued))
				idle = false;
		}
		video_unlock(video);
		if (!idle)
			sleep_ms(1);
	}
	CHECK(idle, "decoder worker still busy after 2 s");
}

/* Advances time by `ms` and runs update(); returns frames advanced. */
static int step(AthenaVideo *video, int ms)
{
	int before = athena_video_get_current_frame(video);

	settle(video);
	__atomic_add_fetch(&fake_ms, ms, __ATOMIC_SEQ_CST);
	athena_video_update(video);
	return athena_video_get_current_frame(video) - before;
}

/* Frames decoded ahead and not shown yet. */
static int queued(AthenaVideo *video)
{
	int count;

	settle(video);
	video_lock(video);
	count = video->queue_count;
	video_unlock(video);
	return count;
}

/* --- Tests -------------------------------------------------------------------- */

static void test_errors(void)
{
	const char *ten = stream("ten.m2v", 10);
	AthenaVideo *video;

	printf("errors\n");
	CHECK(open_error(NULL) == ATHENA_VIDEO_ERROR_ARGUMENT, "NULL path");
	CHECK(open_error("") == ATHENA_VIDEO_ERROR_ARGUMENT, "empty path");
	CHECK(open_error("/nonexistent/video.m2v") == ATHENA_VIDEO_ERROR_OPEN, "missing file");
	CHECK(open_error(stream("empty.m2v", 0)) == ATHENA_VIDEO_ERROR_FORMAT, "empty file");

	fake_reset_sequence();
	dec.width = 1280;
	CHECK(open_error(ten) == ATHENA_VIDEO_ERROR_UNSUPPORTED, "1280 wide");
	fake_reset_sequence();
	dec.height = 1030; /* coded 1040 */
	CHECK(open_error(ten) == ATHENA_VIDEO_ERROR_UNSUPPORTED, "1030 tall");
	fake_reset_sequence();
	dec.width = 1024;
	dec.height = 1024;
	video = open_ok(ten);
	athena_video_destroy(video);
	fake_reset_sequence();
	dec.chroma = MPEG_CHROMA_FORMAT_422;
	CHECK(open_error(ten) == ATHENA_VIDEO_ERROR_UNSUPPORTED, "4:2:2");
	fake_reset_sequence();
	dec.rate = 0.0f;
	CHECK(open_error(ten) == ATHENA_VIDEO_ERROR_UNSUPPORTED, "frame rate 0");
	fake_reset_sequence();
	dec.fail_alloc = 1;
	CHECK(open_error(ten) == ATHENA_VIDEO_ERROR_MEMORY, "arena allocation");
	fake_reset_sequence();

	video = open_ok(ten);
	CHECK(open_error(ten) == ATHENA_VIDEO_ERROR_BUSY, "second video");
	athena_video_destroy(video);
	video = open_ok(ten);
	athena_video_destroy(video);

	for (int e = ATHENA_VIDEO_OK; e <= ATHENA_VIDEO_ERROR_UNSUPPORTED; e++) {
		CHECK(strcmp(athena_video_error_name(e), "unknown") != 0, "name of %d", e);
		CHECK(strcmp(athena_video_error_message(e), "unknown error") != 0, "message of %d", e);
	}
}

static void test_open(void)
{
	AthenaVideo *video;
	GSSURFACE *texture;
	int destroys;

	printf("open, draw, destroy\n");
	fake_reset_sequence();
	video = open_ok(stream("ten.m2v", 10));
	CHECK(athena_video_is_ready(video), "ready");
	CHECK(athena_video_get_width(video) == 640 && athena_video_get_height(video) == 360,
		"display %dx%d", athena_video_get_width(video), athena_video_get_height(video));
	CHECK(athena_video_get_coded_width(video) == 640 && athena_video_get_coded_height(video) == 368,
		"coded %dx%d", athena_video_get_coded_width(video), athena_video_get_coded_height(video));
	CHECK(athena_video_get_fps(video) == 60.0f, "fps %f", athena_video_get_fps(video));
	CHECK(athena_video_get_state(video) == ATHENA_VIDEO_STOPPED, "stopped");
	CHECK(athena_video_get_current_frame(video) == 0, "current frame");

	texture = athena_video_get_texture(video);
	CHECK(texture && texture->Width == 640 && texture->Height == 368, "texture size");
	CHECK(texture && texture->Macroblock && texture->Mem, "texture macroblock data");

	athena_video_draw(video, 0, 0, 0, 0);
	CHECK(last_draw.w == 640 && last_draw.h == 360, "default draw size %fx%f", last_draw.w, last_draw.h);
	CHECK(last_draw.endx == 640 && last_draw.endy == 360, "padding drawn: %fx%f", last_draw.endx, last_draw.endy);
	athena_video_draw(video, 0, 0, 320, 180);
	CHECK(last_draw.w == 320 && last_draw.h == 180, "scaled draw");

	surface_releases = 0;
	destroys = dec.destroy_calls;
	athena_video_destroy(video);
	CHECK(surface_releases == 1, "texture released %d times", surface_releases);
	CHECK(dec.destroy_calls == destroys + 1, "decoder not destroyed");

	/* The shutdown hook releases a video left open. */
	video = open_ok(stream("ten.m2v", 10));
	destroys = dec.destroy_calls;
	athena_video_module_shutdown();
	CHECK(dec.destroy_calls == destroys + 1, "shutdown did not destroy the decoder");
	athena_video_module_shutdown();
	video = open_ok(stream("ten.m2v", 10));
	athena_video_destroy(video);
}

static void test_timing(void)
{
	AthenaVideo *video;
	int frames = 0;

	printf("timing\n");
	fake_reset_sequence();
	video = open_ok(stream("long.m2v", 400));

	CHECK(step(video, 100) == 0, "decoded while stopped");
	athena_video_play(video);
	CHECK(step(video, 16) == 0, "frame before its time");
	CHECK(step(video, 1) == 1, "frame at 16.67 ms");

	/* 60 Hz vsync is 16.67 ms: 17, 17, 16 is 50 ms per 3 flips. */
	for (int i = 0; i < 60; i++)
		frames += step(video, i % 3 == 2 ? 16 : 17);
	CHECK(frames >= 59 && frames <= 60, "60 fps at vsync rate: %d frames in 1 s", frames);

	/* A 30 fps render loop still plays at 60 fps. */
	frames = 0;
	for (int i = 0; i < 30; i++)
		frames += step(video, i % 3 == 2 ? 34 : 33);
	CHECK(frames >= 59 && frames <= 60, "60 fps at 30 Hz render: %d frames in 1 s", frames);

	/*
	 * A long stall shows at most VIDEO_MAX_CATCH_UP frames, then plays on
	 * normally. Two are decoded ahead: the third comes on the next call.
	 */
	{
		int burst = step(video, 500);
		int late = step(video, 1);

		CHECK(burst == video->slot_count - 2, "stall burst %d", burst);
		CHECK(burst + late == VIDEO_MAX_CATCH_UP, "catch-up %d + %d", burst, late);
		CHECK(step(video, 1) == 0, "fast-forward after a stall");
	}

	surface_invalidates = 0;
	step(video, 50);
	CHECK(surface_invalidates == 1, "one upload per update, got %d", surface_invalidates);

	athena_video_pause(video);
	CHECK(step(video, 1000) == 0, "decoded while paused");
	athena_video_play(video);
	CHECK(step(video, 10) == 0, "pause time counted after resume");
	CHECK(step(video, 7) == 1, "resume");
	athena_video_destroy(video);
}

static void test_worker(void)
{
	AthenaVideo *video;
	int pictures;

	printf("decoder worker\n");
	fake_reset_sequence();
	video = open_ok(stream("long.m2v", 400));
	CHECK(video->slot_count == VIDEO_MAX_SLOTS, "%d frame buffers for 640x368", video->slot_count);

	/* Stopped, the worker fills every free buffer ahead, then idles. */
	CHECK(queued(video) == video->slot_count - 1, "decoded ahead: %d", queued(video));
	pictures = dec.pictures;
	sleep_ms(20);
	CHECK(dec.pictures == pictures, "worker decoded with no free buffer");

	/* Frames never overwrite the shown one or the one shown before it. */
	athena_video_play(video);
	for (int i = 0; i < 20; i++) {
		step(video, 17);
		video_lock(video);
		CHECK(video->slots[video->shown].state == SLOT_SHOWN, "shown slot state");
		CHECK(video->retired < 0 || video->slots[video->retired].state == SLOT_RETIRED,
			"retired slot state");
		CHECK(video->texture.Mem == (uint32_t *)video->slots[video->shown].data,
			"texture not on the shown frame");
		video_unlock(video);
	}

	/* stop() drops what was decoded ahead; the next frame is the first. */
	athena_video_stop(video);
	athena_video_play(video);
	settle(video);
	advance(17);
	CHECK(athena_video_update(video) && athena_video_get_current_frame(video) == 0,
		"first picture after stop: %d", athena_video_get_current_frame(video));
	CHECK(step(video, 17) == 1, "second picture after stop");
	athena_video_destroy(video);

	/* Large frames get only the minimum number of buffers. */
	fake_reset_sequence();
	dec.width = 1024;
	dec.height = 1024;
	video = open_ok(stream("big.m2v", 5));
	CHECK(video->slot_count == VIDEO_MIN_SLOTS, "%d frame buffers for 1024x1024", video->slot_count);
	athena_video_destroy(video);
	fake_reset_sequence();
}

static void test_end_and_loop(void)
{
	AthenaVideo *video;
	int destroys, inits;

	printf("end, loop, stop\n");
	fake_reset_sequence();
	video = open_ok(stream("ten.m2v", 10));
	athena_video_play(video);
	for (int i = 0; i < 40 && !athena_video_is_ended(video); i++)
		step(video, 17);
	CHECK(athena_video_is_ended(video), "not ended");
	/* The first picture was decoded on open. */
	CHECK(athena_video_get_current_frame(video) == 9, "frames %d", athena_video_get_current_frame(video));
	CHECK(!athena_video_update(video), "update after the end");

	destroys = dec.destroy_calls;
	inits = dec.init_calls;
	athena_video_play(video);
	CHECK(athena_video_is_playing(video), "play after the end");
	CHECK(athena_video_get_current_frame(video) == 0, "not rewound");
	/* The worker restarts the decoder. */
	settle(video);
	CHECK(dec.destroy_calls == destroys + 1 && dec.init_calls == inits + 1, "decoder not restarted");
	/* Pictures are numbered from 0 in every pass. */
	advance(17);
	CHECK(athena_video_update(video) && athena_video_get_current_frame(video) == 0,
		"first picture after restart: %d", athena_video_get_current_frame(video));

	athena_video_stop(video);
	CHECK(athena_video_get_state(video) == ATHENA_VIDEO_STOPPED, "stop");
	CHECK(athena_video_get_current_frame(video) == 0, "stop rewinds");

	athena_video_set_loop(video, true);
	athena_video_play(video);
	for (int i = 0; i < 35; i++)
		step(video, 17);
	CHECK(athena_video_is_playing(video), "loop ended");
	CHECK(athena_video_get_current_frame(video) < 10, "loop did not rewind: %d",
		athena_video_get_current_frame(video));
	athena_video_destroy(video);
}

static void test_stall(void)
{
	AthenaVideo *video;

	printf("IPU stall\n");
	fake_reset_sequence();
	video = open_ok(stream("long.m2v", 400));
	athena_video_play(video);
	dec.stall = 1;
	__atomic_store_n(&clock_autostep, 1, __ATOMIC_SEQ_CST);
	for (int i = 0; i < 10 && !athena_video_is_ended(video); i++)
		step(video, 17);
	__atomic_store_n(&clock_autostep, 0, __ATOMIC_SEQ_CST);
	CHECK(athena_video_is_ended(video), "stalled IPU did not end playback");
	athena_video_destroy(video);
	fake_reset_sequence();
}

/* update_synced() once the worker is idle; returns the picture shown. */
static int sync_at(AthenaVideo *video, uint32_t clock_ms, bool clock_ended)
{
	settle(video);
	athena_video_update_synced(video, clock_ms, clock_ended);
	return athena_video_get_current_frame(video);
}

static void test_sync(void)
{
	AthenaVideo *video;
	int inits, frame;

	printf("sync to an external clock\n");
	fake_reset_sequence();
	video = open_ok(stream("long.m2v", 400));
	athena_video_set_synced(video, true);
	CHECK(athena_video_is_synced(video), "synced");
	athena_video_play(video);

	/* The clock picks the picture: n from n periods (16.67 ms) on. */
	CHECK(sync_at(video, 0, false) == 0, "picture at 0 ms");
	CHECK(sync_at(video, 16, false) == 0, "picture at 16 ms");
	CHECK(sync_at(video, 17, false) == 1, "picture at 17 ms");
	CHECK(sync_at(video, 50, false) == 3, "picture at 50 ms (one dropped)");

	/* EE time does not matter; a clock that stands still holds the picture. */
	advance(1000);
	CHECK(sync_at(video, 50, false) == 3, "EE time moved the video");
	/* A clock a little behind (not a rewind): the video waits. */
	CHECK(sync_at(video, 20, false) == 3, "video went back");

	/* Behind the clock, it catches up as fast as the worker decodes. */
	for (int i = 0; i < 200 && athena_video_get_current_frame(video) < 120; i++)
		sync_at(video, 2000, false);
	CHECK(athena_video_get_current_frame(video) == 120, "caught up to %d",
		athena_video_get_current_frame(video));

	/* The clock moved back: the video restarts, a loop only when looping. */
	athena_video_take_events(video);
	for (int i = 0; i < 20; i++)
		CHECK(sync_at(video, 100, false) <= 6, "ahead of the clock after the rewind: %d",
			athena_video_get_current_frame(video));
	CHECK(athena_video_get_current_frame(video) == 6, "picture at 100 ms after the rewind: %d",
		athena_video_get_current_frame(video));
	CHECK(athena_video_get_loop_count(video) == 0 && athena_video_take_events(video) == 0,
		"rewind counted as a loop without loop");

	athena_video_set_loop(video, true);
	sync_at(video, 1500, false);
	for (int i = 0; i < 200 && athena_video_get_current_frame(video) < 90; i++)
		sync_at(video, 1500, false);
	sync_at(video, 30, false);
	CHECK(athena_video_get_loop_count(video) == 1, "loop count %d", athena_video_get_loop_count(video));
	CHECK(athena_video_take_events(video) == ATHENA_VIDEO_EVENT_LOOP, "loop event");
	CHECK(sync_at(video, 30, false) == 1, "picture at 30 ms after the loop: %d",
		athena_video_get_current_frame(video));
	athena_video_destroy(video);

	/* Without loop the video ends when its pictures are done. */
	fake_reset_sequence();
	video = open_ok(stream("ten.m2v", 10));
	athena_video_set_synced(video, true);
	athena_video_play(video);
	for (int i = 0; i < 50 && !athena_video_is_ended(video); i++)
		sync_at(video, 1000, false);
	CHECK(athena_video_is_ended(video) && athena_video_get_current_frame(video) == 9,
		"ended at %d", athena_video_get_current_frame(video));
	CHECK(athena_video_take_events(video) & ATHENA_VIDEO_EVENT_END, "end event");
	athena_video_destroy(video);

	/* Looping, a video shorter than its clock holds its last picture. */
	fake_reset_sequence();
	video = open_ok(stream("ten.m2v", 10));
	athena_video_set_synced(video, true);
	athena_video_set_loop(video, true);
	inits = dec.init_calls;
	athena_video_play(video);
	for (int i = 0; i < 50; i++)
		sync_at(video, 1000, false);
	CHECK(athena_video_is_playing(video) && athena_video_get_current_frame(video) == 9,
		"holding at %d", athena_video_get_current_frame(video));
	CHECK(dec.init_calls == inits, "the decoder looped by itself");
	frame = sync_at(video, 10, false);
	CHECK(frame == 0 && athena_video_get_loop_count(video) == 1,
		"after the clock wrapped: picture %d, loops %d", frame, athena_video_get_loop_count(video));
	athena_video_destroy(video);

	/* Once the clock ended, the rest plays on the EE clock. */
	fake_reset_sequence();
	video = open_ok(stream("ten.m2v", 10));
	athena_video_set_synced(video, true);
	athena_video_play(video);
	CHECK(sync_at(video, 50, false) == 3, "synced before the clock ends");
	for (int i = 0; i < 40 && !athena_video_is_ended(video); i++) {
		advance(17);
		sync_at(video, 50, true);
	}
	CHECK(athena_video_is_ended(video) && athena_video_get_current_frame(video) == 9,
		"EE clock after the end: %d", athena_video_get_current_frame(video));
	athena_video_destroy(video);

	/* Not synced: update_synced() is update(). */
	fake_reset_sequence();
	video = open_ok(stream("ten.m2v", 10));
	athena_video_play(video);
	settle(video);
	advance(17);
	CHECK(athena_video_update_synced(video, 5000, false) &&
		athena_video_get_current_frame(video) == 1, "unsynced update_synced");
	athena_video_destroy(video);
	fake_reset_sequence();
}

static void test_draw_options(void)
{
	AthenaVideoDrawOptions options;
	AthenaVideo *video;

	printf("draw options\n");
	fake_reset_sequence();
	video = open_ok(stream("ten.m2v", 10));

	athena_video_draw_options_init(&options);
	CHECK(athena_video_draw_ex(video, 0, 0, &options), "defaults");
	CHECK(last_draw.w == 640 && last_draw.h == 360 && last_draw.endy == 360 &&
		last_draw.color == 0x80808080 && last_draw.angle == 0, "defaults drawn");

	/* A source rectangle defaults the destination to its own size. */
	options.startx = 100;
	options.starty = 50;
	options.endx = 300;
	options.endy = 150;
	options.color = 0x40808080;
	options.angle = 0.5f;
	CHECK(athena_video_draw_ex(video, 0, 0, &options), "region");
	CHECK(last_draw.w == 200 && last_draw.h == 100, "region size %fx%f", last_draw.w, last_draw.h);
	CHECK(last_draw.startx == 100 && last_draw.endy == 150, "region source");
	CHECK(last_draw.angle == 0.5f && last_draw.color == 0x40808080, "angle and tint");

	options.width = 64;
	options.height = 32;
	CHECK(athena_video_draw_ex(video, 0, 0, &options) && last_draw.w == 64 && last_draw.h == 32,
		"region scaled");

	/* Padding rows and empty rectangles are refused, and nothing is drawn. */
	last_draw.calls = 0;
	athena_video_draw_options_init(&options);
	options.endy = 368;
	CHECK(!athena_video_draw_ex(video, 0, 0, &options), "padding drawn");
	athena_video_draw_options_init(&options);
	options.startx = 300;
	options.endx = 300;
	CHECK(!athena_video_draw_ex(video, 0, 0, &options), "empty rectangle");
	athena_video_draw_options_init(&options);
	options.startx = -1;
	CHECK(!athena_video_draw_ex(video, 0, 0, &options), "negative start");
	CHECK(last_draw.calls == 0, "refused rectangle drawn");
	athena_video_destroy(video);
}

static void test_events(void)
{
	AthenaVideo *video;
	unsigned int events = 0;

	printf("events and loop count\n");
	fake_reset_sequence();
	video = open_ok(stream("ten.m2v", 10));
	athena_video_play(video);
	for (int i = 0; i < 40 && !athena_video_is_ended(video); i++) {
		step(video, 17);
		events |= athena_video_take_events(video);
	}
	CHECK(events == ATHENA_VIDEO_EVENT_END, "events %#x at the end", events);
	CHECK(athena_video_take_events(video) == 0, "events not cleared");
	CHECK(athena_video_get_loop_count(video) == 0, "loops without loop");

	/* Seamless loop: the data wraps, one LOOP per pass. */
	athena_video_stop(video);
	athena_video_set_loop(video, true);
	athena_video_play(video);
	events = 0;
	for (int i = 0; i < 30; i++) {
		step(video, 17);
		events |= athena_video_take_events(video);
	}
	CHECK(events == ATHENA_VIDEO_EVENT_LOOP, "events %#x while looping", events);
	/* stop() rewound: pictures 1-30 play, the data wraps before 11 and 21. */
	CHECK(athena_video_get_loop_count(video) == 2, "loop count %d after 30 frames of 10",
		athena_video_get_loop_count(video));
	athena_video_destroy(video);

	/* With an end code the data wraps and the decoder restarts: still one loop. */
	fake_reset_sequence();
	dec.end_code_after = 10;
	video = open_ok(stream("ten.m2v", 10));
	athena_video_set_loop(video, true);
	athena_video_play(video);
	for (int i = 0; i < 30; i++)
		step(video, 17);
	CHECK(athena_video_get_loop_count(video) == 3, "end code loop count %d",
		athena_video_get_loop_count(video));
	CHECK(athena_video_is_playing(video), "end code loop ended");

	athena_video_stop(video);
	CHECK(athena_video_get_loop_count(video) == 0 && athena_video_take_events(video) == 0,
		"stop keeps the loop count");
	athena_video_destroy(video);
	fake_reset_sequence();
}

static void test_probe(void)
{
	const char *fixtures = "bin/tests/video/";
	AthenaVideoInfo info;
	char path[256];

#define PROBE(name) (snprintf(path, sizeof(path), "%s%s", fixtures, name), \
	athena_video_probe(path, &info))

	printf("probe\n");
	CHECK(PROBE("short.m2v") == ATHENA_VIDEO_OK, "short.m2v");
	CHECK(info.width == 640 && info.height == 360, "size %dx%d", info.width, info.height);
	CHECK(info.coded_width == 640 && info.coded_height == 368, "coded %dx%d",
		info.coded_width, info.coded_height);
	CHECK(info.fps == 60.0f && info.frames == 60, "fps %f, frames %d", info.fps, info.frames);
	CHECK(info.duration > 0.999f && info.duration < 1.001f, "duration %f", info.duration);
	CHECK(info.mpeg2 && info.progressive && info.chroma_format == 1 && info.supported,
		"stream flags");

	CHECK(PROBE("noend.m2v") == ATHENA_VIDEO_OK && info.frames == 60, "noend frames %d", info.frames);
	CHECK(PROBE("truncated.m2v") == ATHENA_VIDEO_OK && info.frames == 30,
		"truncated frames %d", info.frames);
	CHECK(PROBE("wide.m2v") == ATHENA_VIDEO_OK && info.width == 1280 && !info.supported,
		"wide: %d, supported %d", info.width, info.supported);
	CHECK(PROBE("chroma422.m2v") == ATHENA_VIDEO_OK && info.chroma_format == 2 && !info.supported,
		"4:2:2");
	CHECK(PROBE("badrate.m2v") == ATHENA_VIDEO_OK && info.fps == 0.0f && !info.supported &&
		info.duration == 0.0f, "forbidden frame rate");
	CHECK(PROBE("garbage.bin") == ATHENA_VIDEO_ERROR_FORMAT, "garbage");
	CHECK(PROBE("empty.m2v") == ATHENA_VIDEO_ERROR_FORMAT, "empty");
	CHECK(PROBE("missing.m2v") == ATHENA_VIDEO_ERROR_OPEN, "missing");
	CHECK(athena_video_probe(NULL, &info) == ATHENA_VIDEO_ERROR_ARGUMENT, "NULL path");
#undef PROBE
}

int main(void)
{
	snprintf(dir, sizeof(dir), "%s/athena-video-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}

	test_errors();
	test_open();
	test_timing();
	test_end_and_loop();
	test_stall();
	test_worker();
	test_sync();
	test_draw_options();
	test_events();
	test_probe();

	CHECK(__atomic_load_n(&threads_alive, __ATOMIC_SEQ_CST) == 0,
		"%d decoder threads left running", threads_alive);
	printf("video: %d checks, %d failures\n", checks, failures);
	return failures != 0;
}
