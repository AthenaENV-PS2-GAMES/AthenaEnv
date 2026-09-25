#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include <kernel.h>
#include <delaythread.h>
#include <ee_regs.h>
#include <dmaKit.h>

#include <athena/debug.h>
#include <athena/graphics.h>
#include <athena/mutex.h>
#include <athena/thread.h>
#include <athena/video.h>

#include "libmpeg/libmpeg.h"

/* File data is read in 64 KiB blocks and sent to the IPU in 2 KiB DMA chunks. */
#define VIDEO_FILE_BUFFER_SIZE (64 * 1024)
#define VIDEO_DMA_CHUNK_SIZE 2048
/* A chunk normally drains at once; this long means the IPU stopped reading. */
#define VIDEO_DMA_TIMEOUT_MS 500
/* Frames update() may show to catch up; beyond that, late time is dropped. */
#define VIDEO_MAX_CATCH_UP 3

/*
 * Frame buffers: the one shown, the one shown before it (its upload is a
 * DMA that reads the buffer until the next flip), and 1-2 decoded ahead.
 * The fourth is only allocated within the budget.
 */
#define VIDEO_MIN_SLOTS 3
#define VIDEO_MAX_SLOTS 4
#define VIDEO_SLOT_BUDGET (8 * 1024 * 1024)

#define VIDEO_STACK_SIZE (16 * 1024)
#define VIDEO_IDLE_US 2000
#define VIDEO_WAIT_STEP_US 1000
/* A synced clock moving back by more than this restarted (looped or rewound). */
#define VIDEO_SYNC_REWIND_MS 500

#define DMA_CHCR_STR 0x100

typedef enum VideoSlotState {
	SLOT_FREE,
	SLOT_DECODING,
	SLOT_READY,
	SLOT_SHOWN,
	SLOT_RETIRED,
} VideoSlotState;

typedef struct VideoSlot {
	/* Decoded RGBA32 frame in 16x16 macroblock order. */
	uint8_t *data;
	VideoSlotState state;
	/* currentFrame once shown, and whether a loop happened just before. */
	int frame;
	bool looped;
} VideoSlot;

/*
 * Threads: the drawing thread calls the public API; the "Video decoder"
 * worker owns libmpeg, the file and the IPU once the video is open (the
 * creating thread uses them to decode the first frame, before the worker
 * starts, and after it stops). Fields shared by both are under `lock`.
 */
struct AthenaVideo {
	/* Set by the first sequence, fixed afterwards. */
	int width;
	int height;
	int coded_width;
	int coded_height;
	float fps;
	float frame_period_ms;
	size_t frame_size;
	bool ready;

	/* Drawing thread. */
	AthenaVideoState state;
	int current_frame;
	int loop_count;
	unsigned int events;
	uint32_t last_tick_ms;
	float pending_ms;
	GSSURFACE texture;
	int shown;
	int retired;

	/* Shared, under `lock`. */
	AthenaMutex *lock;
	VideoSlot slots[VIDEO_MAX_SLOTS];
	int slot_count;
	/* Decoded slots waiting to be shown, oldest first. */
	int queue[VIDEO_MAX_SLOTS];
	int queue_head;
	int queue_count;
	/* Bumped by a restart request: frames decoded before it are dropped. */
	uint32_t generation;
	bool restart_requested;
	/* The decoder reached the end: playback ends once the queue drains. */
	bool end_queued;
	bool stop_worker;
	/* Read by the decoder while it runs, written by the drawing thread. */
	volatile bool loop;
	/*
	 * Synced to an external clock (an audio track): its position picks the
	 * frame, and its rewinds, not the decoder, restart the video.
	 */
	volatile bool synced;
	/* Drawing thread: the clock position seen by the previous update. */
	uint32_t sync_last_ms;

	/* Decoder side. */
	AthenaThread *worker;
	FILE *file;
	uint8_t *file_buffer;
	size_t file_buffer_size;
	size_t file_buffer_pos;
	uint8_t *decode_target;
	/* Why the init callback refused a sequence. */
	AthenaVideoError sequence_error;
	/* The IPU stopped taking data: the stream cannot continue. */
	volatile bool stalled;
	/*
	 * The data callback already wrapped to the start in this decoder run
	 * and flagged the loop; a sequence end code decoded right after must
	 * not flag it again.
	 */
	bool wrapped;
	bool next_looped;
	/* Frames decoded in this pass. */
	int decode_frame;
	s64 pts;
};

/*
 * libmpeg keeps one global decoder and calls back without per-call context,
 * so the open video is tracked here and a second one is refused.
 */
static AthenaVideo *active_video;

static uint32_t video_now_ms(void)
{
	return (uint32_t)(((uint64_t)clock() * 1000) / CLOCKS_PER_SEC);
}

static void video_lock(AthenaVideo *video)
{
	athena_mutex_core_lock(video->lock);
}

static void video_unlock(AthenaVideo *video)
{
	athena_mutex_core_unlock(video->lock);
}

/* --- Decoder side ----------------------------------------------------- */

static size_t video_fill_buffer(AthenaVideo *video)
{
	video->file_buffer_size = fread(video->file_buffer, 1,
		VIDEO_FILE_BUFFER_SIZE, video->file);
	video->file_buffer_pos = 0;
	return video->file_buffer_size;
}

static void video_rewind(AthenaVideo *video)
{
	fseek(video->file, 0, SEEK_SET);
	video_fill_buffer(video);
	/* The next picture decoded is picture 0: currentFrame is its index. */
	video->decode_frame = -1;
}

/* Whether the decoder itself restarts at the end (not when synced). */
static bool video_decoder_loops(const AthenaVideo *video)
{
	return video->loop && !video->synced;
}

/* Waits for the previous chunk to reach the IPU, with a time limit. */
static bool video_wait_to_ipu(void)
{
	uint32_t start = video_now_ms();

	while (*R_EE_D4_CHCR & DMA_CHCR_STR) {
		if (video_now_ms() - start > VIDEO_DMA_TIMEOUT_MS)
			return false;
	}
	return true;
}

/* libmpeg data callback: feeds the next chunk to the IPU, 0 at end of data. */
static int video_data_callback(void *user_data)
{
	AthenaVideo *video = active_video;
	size_t remaining, chunk;

	(void)user_data;
	if (!video || !video->file || video->stalled)
		return 0;

	if (video->file_buffer_pos >= video->file_buffer_size) {
		if (video_fill_buffer(video) == 0) {
			if (!video_decoder_loops(video))
				return 0; /* libmpeg injects a sequence end code itself. */
			/*
			 * Seamless loop: the decoder keeps going into the start of the
			 * file. The few pictures it still holds from the end are counted
			 * in the new pass.
			 */
			video_rewind(video);
			if (video->file_buffer_size == 0)
				return 0;
			video->wrapped = true;
			video->next_looped = true;
		}
	}

	if (!video_wait_to_ipu()) {
		dbgprintf("video: IPU stopped reading data, ending playback\n");
		video->stalled = true;
		return 0;
	}

	/*
	 * Fixed-size chunks as in the ps2sdk sample: the buffer is a full
	 * allocation, so rounding the tail up to a quadword stays inside it.
	 */
	remaining = video->file_buffer_size - video->file_buffer_pos;
	chunk = remaining < VIDEO_DMA_CHUNK_SIZE ? remaining : VIDEO_DMA_CHUNK_SIZE;
	dmaKit_send(DMA_CHANNEL_TOIPU, video->file_buffer + video->file_buffer_pos,
		(chunk + 15) >> 4);
	video->file_buffer_pos += chunk;
	return 1;
}

static AthenaVideoError video_check_sequence(const MPEGSequenceInfo *info)
{
	if (info->m_DisplayWidth <= 0 || info->m_DisplayHeight <= 0)
		return ATHENA_VIDEO_ERROR_FORMAT;
	if (info->m_Width > ATHENA_VIDEO_MAX_SIZE ||
		info->m_Height > ATHENA_VIDEO_MAX_SIZE)
		return ATHENA_VIDEO_ERROR_UNSUPPORTED;
	if (info->m_ChromaFmt != MPEG_CHROMA_FORMAT_420)
		return ATHENA_VIDEO_ERROR_UNSUPPORTED;
	if (!(info->m_FrameRate > 0.0f))
		return ATHENA_VIDEO_ERROR_UNSUPPORTED;
	return ATHENA_VIDEO_OK;
}

static void video_free_slots(AthenaVideo *video)
{
	for (int i = 0; i < VIDEO_MAX_SLOTS; i++) {
		free(video->slots[i].data);
		video->slots[i].data = NULL;
	}
	video->slot_count = 0;
}

static bool video_alloc_slots(AthenaVideo *video, size_t frame_size)
{
	int wanted = frame_size * VIDEO_MAX_SLOTS <= VIDEO_SLOT_BUDGET ?
		VIDEO_MAX_SLOTS : VIDEO_MIN_SLOTS;

	for (int i = 0; i < wanted; i++) {
		video->slots[i].data = memalign(64, frame_size);
		if (!video->slots[i].data) {
			if (i >= VIDEO_MIN_SLOTS)
				break;
			video_free_slots(video);
			return false;
		}
		video->slots[i].state = SLOT_FREE;
		video->slot_count = i + 1;
	}
	return true;
}

/*
 * libmpeg sequence callback: called before a sequence starts decoding.
 * Returns the buffer libmpeg writes the picture into, or NULL to refuse the
 * sequence (libmpeg then fails the picture without decoding it).
 *
 * The first sequence sets the size and allocates the frame buffers. Later
 * ones (a restart, a loop, a new sequence in the file) must keep the same
 * size: the drawing thread uses the buffers meanwhile.
 */
static void *video_init_callback(void *user_data, MPEGSequenceInfo *info)
{
	AthenaVideo *video = active_video;
	AthenaVideoError check;

	(void)user_data;
	if (!video)
		return NULL;

	check = video_check_sequence(info);
	if (check == ATHENA_VIDEO_OK && video->ready &&
		(info->m_Width != video->coded_width ||
		 info->m_Height != video->coded_height))
		check = ATHENA_VIDEO_ERROR_UNSUPPORTED;
	/* Restarts replay the same sequence: log only what is new. */
	if (!video->ready || check != ATHENA_VIDEO_OK)
		dbgprintf("video: sequence %dx%d (coded %dx%d), chroma %d, %d.%02d fps%s\n",
			info->m_DisplayWidth, info->m_DisplayHeight, info->m_Width,
			info->m_Height, info->m_ChromaFmt, (int)info->m_FrameRate,
			(int)(info->m_FrameRate * 100.0f) % 100,
			check != ATHENA_VIDEO_OK ? ": refused" : "");
	if (check != ATHENA_VIDEO_OK) {
		video->sequence_error = check;
		return NULL;
	}
	if (video->ready)
		return video->decode_target;

	video->frame_size = (size_t)info->m_Width * (size_t)info->m_Height * 4;
	if (!video_alloc_slots(video, video->frame_size)) {
		video->sequence_error = ATHENA_VIDEO_ERROR_MEMORY;
		return NULL;
	}
	video->width = info->m_DisplayWidth;
	video->height = info->m_DisplayHeight;
	video->coded_width = info->m_Width;
	video->coded_height = info->m_Height;
	video->fps = info->m_FrameRate;
	video->frame_period_ms = 1000.0f / info->m_FrameRate;
	video->decode_target = video->slots[0].data;
	video->ready = true;
	return video->decode_target;
}

static void video_start_decoder(AthenaVideo *video)
{
	video->stalled = false;
	/* next_looped survives: a loop flagged before a restart still counts. */
	video->wrapped = false;
	MPEG_Initialize(video_data_callback, NULL, video_init_callback, NULL,
		&video->pts);
}

static void video_restart_decoder(AthenaVideo *video)
{
	MPEG_Destroy();
	video_rewind(video);
	video_start_decoder(video);
}

/*
 * Decodes the next picture into `target`, restarting the stream when it
 * ends and loop is on. Returns false at the end of playback.
 */
static bool video_decode_into(AthenaVideo *video, uint8_t *target,
	int *frame, bool *looped)
{
	video->decode_target = target;
	if (!MPEG_Picture(target, &video->pts)) {
		bool counted = video->wrapped;

		/*
		 * libmpeg does not tell end of stream from a decode error; either
		 * way nothing is left to decode, so restart when looping or end.
		 */
		if (!video_decoder_loops(video) || video->stalled ||
			video->sequence_error != ATHENA_VIDEO_OK)
			return false;
		video_restart_decoder(video);
		if (!MPEG_Picture(target, &video->pts))
			return false;
		if (!counted)
			video->next_looped = true;
	}
	*frame = ++video->decode_frame;
	*looped = video->next_looped;
	video->next_looped = false;
	return true;
}

static int video_take_free_slot(AthenaVideo *video)
{
	for (int i = 0; i < video->slot_count; i++) {
		if (video->slots[i].state == SLOT_FREE)
			return i;
	}
	return -1;
}

/* Drops decoded frames not shown yet. Called under the lock. */
static void video_flush_queue(AthenaVideo *video)
{
	while (video->queue_count > 0) {
		video->slots[video->queue[video->queue_head]].state = SLOT_FREE;
		video->queue_head = (video->queue_head + 1) % VIDEO_MAX_SLOTS;
		video->queue_count--;
	}
}

static void video_worker(void *arg)
{
	AthenaVideo *video = arg;

	for (;;) {
		bool restart = false, ok, looped = false;
		uint32_t generation;
		int index = -1, frame = 0;

		video_lock(video);
		if (video->stop_worker || athena_thread_core_stop_requested()) {
			video_unlock(video);
			break;
		}
		if (video->restart_requested) {
			video->restart_requested = false;
			restart = true;
		} else if (!video->end_queued) {
			index = video_take_free_slot(video);
			if (index >= 0)
				video->slots[index].state = SLOT_DECODING;
		}
		generation = video->generation;
		video_unlock(video);

		if (restart) {
			/* stop() or play() after the end: a new playback, not a loop. */
			video->next_looped = false;
			video_restart_decoder(video);
			continue;
		}
		if (index < 0) {
			DelayThread(VIDEO_IDLE_US);
			continue;
		}

		ok = video_decode_into(video, video->slots[index].data, &frame, &looped);

		video_lock(video);
		if (generation != video->generation) {
			/* A restart was requested meanwhile: this frame is stale. */
			video->slots[index].state = SLOT_FREE;
		} else if (ok) {
			VideoSlot *slot = &video->slots[index];

			slot->state = SLOT_READY;
			slot->frame = frame;
			slot->looped = looped;
			video->queue[(video->queue_head + video->queue_count) % VIDEO_MAX_SLOTS] = index;
			video->queue_count++;
		} else {
			video->slots[index].state = SLOT_FREE;
			video->end_queued = true;
		}
		video_unlock(video);
	}
	athena_thread_core_worker_finished(video->worker);
	ExitThread();
}

static void video_stop_worker(AthenaVideo *video)
{
	if (!video->worker)
		return;
	video_lock(video);
	video->stop_worker = true;
	video_unlock(video);
	athena_thread_core_stop(video->worker);
	athena_thread_core_wait(video->worker);
	/*
	 * worker_finished() signals just before ExitThread(); give the worker
	 * time to become dormant before its stack is released.
	 */
	for (int attempts = 0; attempts < 100 &&
		athena_thread_core_get_status(video->worker) != THS_DORMANT; attempts++)
		DelayThread(100);
	athena_thread_core_finalize(video->worker);
	video->worker = NULL;
}

/* --- Drawing thread --------------------------------------------------- */

const char *athena_video_error_message(AthenaVideoError error)
{
	switch (error) {
	case ATHENA_VIDEO_OK:
		return "no error";
	case ATHENA_VIDEO_ERROR_ARGUMENT:
		return "invalid argument";
	case ATHENA_VIDEO_ERROR_BUSY:
		return "another Video is already open";
	case ATHENA_VIDEO_ERROR_OPEN:
		return "unable to open file";
	case ATHENA_VIDEO_ERROR_MEMORY:
		return "out of memory";
	case ATHENA_VIDEO_ERROR_FORMAT:
		return "not a decodable MPEG-1/2 elementary video stream";
	case ATHENA_VIDEO_ERROR_UNSUPPORTED:
		return "unsupported stream (needs 4:2:0 chroma, a valid frame rate and at most 1024x1024)";
	case ATHENA_VIDEO_ERROR_THREAD:
		return "unable to start the decoder thread";
	}
	return "unknown error";
}

const char *athena_video_error_name(AthenaVideoError error)
{
	switch (error) {
	case ATHENA_VIDEO_OK:
		return "ok";
	case ATHENA_VIDEO_ERROR_ARGUMENT:
		return "invalid_argument";
	case ATHENA_VIDEO_ERROR_BUSY:
		return "busy";
	case ATHENA_VIDEO_ERROR_OPEN:
		return "open_failed";
	case ATHENA_VIDEO_ERROR_MEMORY:
		return "out_of_memory";
	case ATHENA_VIDEO_ERROR_FORMAT:
		return "invalid_format";
	case ATHENA_VIDEO_ERROR_UNSUPPORTED:
		return "unsupported_format";
	case ATHENA_VIDEO_ERROR_THREAD:
		return "thread_failed";
	}
	return "unknown";
}

static AthenaVideo *video_fail(AthenaVideo *video, AthenaVideoError code,
	AthenaVideoError *error)
{
	athena_video_destroy(video);
	if (error)
		*error = code;
	return NULL;
}

AthenaVideo *athena_video_create(const char *path, AthenaVideoError *error)
{
	AthenaVideo *video;

	if (error)
		*error = ATHENA_VIDEO_OK;
	if (!path || !*path)
		return video_fail(NULL, ATHENA_VIDEO_ERROR_ARGUMENT, error);
	if (active_video)
		return video_fail(NULL, ATHENA_VIDEO_ERROR_BUSY, error);

	video = calloc(1, sizeof(*video));
	if (!video)
		return video_fail(NULL, ATHENA_VIDEO_ERROR_MEMORY, error);
	graphics_surface_init(&video->texture);
	video->state = ATHENA_VIDEO_STOPPED;
	video->shown = -1;
	video->retired = -1;
	video->lock = athena_mutex_core_create();
	if (!video->lock)
		return video_fail(video, ATHENA_VIDEO_ERROR_MEMORY, error);

	video->file = fopen(path, "rb");
	if (!video->file)
		return video_fail(video, ATHENA_VIDEO_ERROR_OPEN, error);

	video->file_buffer = memalign(64, VIDEO_FILE_BUFFER_SIZE);
	if (!video->file_buffer)
		return video_fail(video, ATHENA_VIDEO_ERROR_MEMORY, error);
	video_fill_buffer(video);

	/* dmaKit_init already ran in the graphics module. */
	dmaKit_chan_init(DMA_CHANNEL_TOIPU);

	active_video = video;
	video_start_decoder(video);

	/* Decode the first frame here, so the size, rate and texture are known. */
	if (!MPEG_Picture(NULL, &video->pts) || !video->ready) {
		AthenaVideoError code = video->sequence_error;

		if (code == ATHENA_VIDEO_OK)
			code = MPEG_AllocFailed() ? ATHENA_VIDEO_ERROR_MEMORY :
				ATHENA_VIDEO_ERROR_FORMAT;
		return video_fail(video, code, error);
	}
	/* The first frame is shown before playback starts, and not counted. */
	video->slots[0].state = SLOT_SHOWN;
	video->shown = 0;
	video->decode_frame = 0;

	video->texture.Width = video->coded_width;
	video->texture.Height = video->coded_height;
	video->texture.PSM = GS_PSM_CT32;
	video->texture.Mem = (uint32_t *)video->slots[0].data;
	video->texture.Filter = GS_FILTER_LINEAR;
	video->texture.Delayed = 0;
	video->texture.Macroblock = 1;
	athena_calculate_tbw(&video->texture);

	video->worker = athena_thread_core_create("Video decoder", video_worker,
		video, VIDEO_STACK_SIZE, ATHENA_THREAD_DEFAULT_PRIORITY + 1);
	if (!video->worker)
		return video_fail(video, ATHENA_VIDEO_ERROR_THREAD, error);
	if (athena_thread_core_start(video->worker) < 0) {
		/* Never started, so destroy() also finalizes the thread. */
		athena_thread_core_destroy(video->worker);
		video->worker = NULL;
		return video_fail(video, ATHENA_VIDEO_ERROR_THREAD, error);
	}
	return video;
}

void athena_video_destroy(AthenaVideo *video)
{
	if (!video)
		return;

	/* The worker finishes its picture: libmpeg is idle afterwards. */
	video_stop_worker(video);
	if (video == active_video) {
		MPEG_Destroy();
		active_video = NULL;
	}
	if (video->file)
		fclose(video->file);
	free(video->file_buffer);
	/* Drop the texture manager's reference before the surface goes away. */
	graphics_surface_release(&video->texture);
	video_free_slots(video);
	if (video->lock)
		athena_mutex_core_destroy(video->lock);
	free(video);
}

/* Asks the worker to decode from the start again; frames queued are dropped. */
static void video_request_restart(AthenaVideo *video)
{
	video_lock(video);
	video->generation++;
	video->restart_requested = true;
	video->end_queued = false;
	video_flush_queue(video);
	video_unlock(video);
	video->current_frame = 0;
	video->pending_ms = 0.0f;
	video->sync_last_ms = 0;
}

static void video_ended(AthenaVideo *video)
{
	video->state = ATHENA_VIDEO_ENDED;
	video->events |= ATHENA_VIDEO_EVENT_END;
}

void athena_video_play(AthenaVideo *video)
{
	if (!video || !video->ready || video->state == ATHENA_VIDEO_PLAYING)
		return;

	if (video->state == ATHENA_VIDEO_ENDED) {
		video_request_restart(video);
		video->loop_count = 0;
	}

	video->state = ATHENA_VIDEO_PLAYING;
	video->last_tick_ms = video_now_ms();
	video->pending_ms = 0.0f;
}

void athena_video_pause(AthenaVideo *video)
{
	if (video && video->state == ATHENA_VIDEO_PLAYING)
		video->state = ATHENA_VIDEO_PAUSED;
}

void athena_video_stop(AthenaVideo *video)
{
	if (!video)
		return;

	video->state = ATHENA_VIDEO_STOPPED;
	video->loop_count = 0;
	video->events = 0;
	if (video->ready)
		video_request_restart(video);
}

/*
 * Takes up to `due` decoded frames off the queue; all but the newest are
 * dropped unseen. Returns the newest slot, or -1 when none was decoded yet.
 */
static int video_take_frames(AthenaVideo *video, int due, int *taken)
{
	int newest = -1;

	*taken = 0;
	video_lock(video);
	while (*taken < due && video->queue_count > 0) {
		VideoSlot *slot = &video->slots[video->queue[video->queue_head]];

		video->queue_head = (video->queue_head + 1) % VIDEO_MAX_SLOTS;
		video->queue_count--;
		if (newest >= 0)
			video->slots[newest].state = SLOT_FREE;
		newest = (int)(slot - video->slots);
		video->current_frame = slot->frame;
		if (slot->looped) {
			video->loop_count++;
			video->events |= ATHENA_VIDEO_EVENT_LOOP;
		}
		(*taken)++;
	}
	video_unlock(video);
	return newest;
}

/* Whether the decoder is done and every decoded frame was shown. */
static bool video_drained(AthenaVideo *video)
{
	bool drained;

	video_lock(video);
	drained = video->end_queued && video->queue_count == 0;
	video_unlock(video);
	return drained;
}

static void video_show(AthenaVideo *video, int index)
{
	video_lock(video);
	if (video->retired >= 0)
		video->slots[video->retired].state = SLOT_FREE;
	video->retired = video->shown;
	if (video->retired >= 0)
		video->slots[video->retired].state = SLOT_RETIRED;
	video->shown = index;
	video->slots[index].state = SLOT_SHOWN;
	video_unlock(video);

	/* New pixels: flush them to RAM and have the next draw re-upload. */
	video->texture.Mem = (uint32_t *)video->slots[index].data;
	SyncDCache(video->slots[index].data,
		video->slots[index].data + video->frame_size);
	graphics_surface_invalidate(&video->texture);
}

/*
 * Nothing decoded in time: the worker only runs while this thread waits.
 * Lends it up to one frame period, so a loop that never idles still plays
 * (as slowly as decoding allows) instead of freezing. Returns whether a
 * frame is queued.
 */
static bool video_wait_for_frame(AthenaVideo *video)
{
	for (int waited = 0; waited < (int)(video->frame_period_ms * 1000.0f);
		waited += VIDEO_WAIT_STEP_US) {
		bool queued, finished;

		DelayThread(VIDEO_WAIT_STEP_US);
		video_lock(video);
		queued = video->queue_count > 0;
		finished = video->end_queued;
		video_unlock(video);
		if (queued || finished)
			return queued;
	}
	return false;
}

bool athena_video_update(AthenaVideo *video)
{
	float max_pending;
	uint32_t now;
	int due, taken, newest;

	if (!video || video->state != ATHENA_VIDEO_PLAYING)
		return false;

	now = video_now_ms();
	video->pending_ms += (float)(now - video->last_tick_ms);
	video->last_tick_ms = now;
	if (video->pending_ms < video->frame_period_ms)
		return false;

	max_pending = VIDEO_MAX_CATCH_UP * video->frame_period_ms;
	if (video->pending_ms > max_pending)
		video->pending_ms = max_pending; /* long stall: resync, no fast-forward */
	due = (int)(video->pending_ms / video->frame_period_ms);

	newest = video_take_frames(video, due, &taken);
	if (taken == 0) {
		if (video_drained(video)) {
			video_ended(video);
			video->pending_ms = 0.0f;
			return false;
		}
		if (!video_wait_for_frame(video))
			return false;
		newest = video_take_frames(video, 1, &taken);
		if (taken == 0)
			return false;
	}

	video->pending_ms -= (float)taken * video->frame_period_ms;
	if (video->pending_ms < 0.0f)
		video->pending_ms = 0.0f;
	video_show(video, newest);
	return true;
}

/*
 * Takes the queued frames whose index is at most `target`; all but the
 * newest are dropped unseen. Returns the newest slot, or -1.
 */
static int video_take_frames_until(AthenaVideo *video, int target, int *taken)
{
	int newest = -1;

	*taken = 0;
	video_lock(video);
	while (video->queue_count > 0 &&
		video->slots[video->queue[video->queue_head]].frame <= target) {
		VideoSlot *slot = &video->slots[video->queue[video->queue_head]];

		video->queue_head = (video->queue_head + 1) % VIDEO_MAX_SLOTS;
		video->queue_count--;
		if (newest >= 0)
			video->slots[newest].state = SLOT_FREE;
		newest = (int)(slot - video->slots);
		video->current_frame = slot->frame;
		(*taken)++;
	}
	video_unlock(video);
	return newest;
}

void athena_video_set_synced(AthenaVideo *video, bool synced)
{
	if (!video)
		return;
	video->synced = synced;
	video->sync_last_ms = 0;
	/* Leaving sync: the EE clock takes over from now. */
	video->last_tick_ms = video_now_ms();
	video->pending_ms = 0.0f;
}

bool athena_video_is_synced(const AthenaVideo *video)
{
	return video && video->synced;
}

bool athena_video_update_synced(AthenaVideo *video, uint32_t clock_ms,
	bool clock_ended)
{
	int target, taken, newest;

	if (!video || !video->synced || clock_ended)
		/* No clock, or it stopped for good: what is left plays on the EE clock. */
		return athena_video_update(video);
	if (video->state != ATHENA_VIDEO_PLAYING)
		return false;

	/* Keep the EE clock current, for when the external one ends. */
	video->last_tick_ms = video_now_ms();
	video->pending_ms = 0.0f;

	if (clock_ms + VIDEO_SYNC_REWIND_MS < video->sync_last_ms) {
		/* The clock went back: its loop wrapped, or it was rewound. */
		video_request_restart(video);
		if (video->loop) {
			video->loop_count++;
			video->events |= ATHENA_VIDEO_EVENT_LOOP;
		}
	}
	video->sync_last_ms = clock_ms;

	/* Picture n is shown from n periods after the start. */
	target = (int)((float)clock_ms / video->frame_period_ms);
	newest = video_take_frames_until(video, target, &taken);
	if (taken == 0) {
		if (video_drained(video)) {
			/* Looping: hold the last picture until the clock wraps. */
			if (!video->loop)
				video_ended(video);
			return false;
		}
		video_lock(video);
		bool behind = video->queue_count == 0;
		video_unlock(video);
		/* Frames queued but not due yet: the video is ahead, just wait. */
		if (!behind || !video_wait_for_frame(video))
			return false;
		newest = video_take_frames_until(video, target, &taken);
		if (taken == 0)
			return false;
	}
	video_show(video, newest);
	return true;
}

void athena_video_draw_options_init(AthenaVideoDrawOptions *options)
{
	memset(options, 0, sizeof(*options));
	options->color = 0x80808080;
}

bool athena_video_draw_ex(AthenaVideo *video, float x, float y,
	const AthenaVideoDrawOptions *options)
{
	float startx, starty, endx, endy, width, height;

	if (!video || !video->ready || !options)
		return false;

	startx = options->startx;
	starty = options->starty;
	endx = options->endx > 0 ? options->endx : (float)video->width;
	endy = options->endy > 0 ? options->endy : (float)video->height;
	/* Only the picture: the macroblock padding holds no image. */
	if (startx < 0 || starty < 0 || endx <= startx || endy <= starty ||
		endx > (float)video->width || endy > (float)video->height)
		return false;

	width = options->width > 0 ? options->width : endx - startx;
	height = options->height > 0 ? options->height : endy - starty;
	if (options->angle != 0.0f)
		draw_image_rotate(&video->texture, x, y, width, height, startx, starty,
			endx, endy, options->angle, options->color);
	else
		draw_image(&video->texture, x, y, width, height, startx, starty,
			endx, endy, options->color);
	return true;
}

void athena_video_draw(AthenaVideo *video, float x, float y, float width,
	float height)
{
	AthenaVideoDrawOptions options;

	athena_video_draw_options_init(&options);
	options.width = width;
	options.height = height;
	athena_video_draw_ex(video, x, y, &options);
}

unsigned int athena_video_take_events(AthenaVideo *video)
{
	unsigned int events;

	if (!video)
		return 0;
	events = video->events;
	video->events = 0;
	return events;
}

int athena_video_get_loop_count(const AthenaVideo *video)
{
	return video ? video->loop_count : 0;
}

int athena_video_get_width(const AthenaVideo *video)
{
	return video ? video->width : 0;
}

int athena_video_get_height(const AthenaVideo *video)
{
	return video ? video->height : 0;
}

int athena_video_get_coded_width(const AthenaVideo *video)
{
	return video ? video->coded_width : 0;
}

int athena_video_get_coded_height(const AthenaVideo *video)
{
	return video ? video->coded_height : 0;
}

float athena_video_get_fps(const AthenaVideo *video)
{
	return video ? video->fps : 0.0f;
}

AthenaVideoState athena_video_get_state(const AthenaVideo *video)
{
	return video ? video->state : ATHENA_VIDEO_STOPPED;
}

bool athena_video_is_ready(const AthenaVideo *video)
{
	return video && video->ready;
}

bool athena_video_is_ended(const AthenaVideo *video)
{
	return !video || video->state == ATHENA_VIDEO_ENDED;
}

bool athena_video_is_playing(const AthenaVideo *video)
{
	return video && video->state == ATHENA_VIDEO_PLAYING;
}

bool athena_video_get_loop(const AthenaVideo *video)
{
	return video && video->loop;
}

void athena_video_set_loop(AthenaVideo *video, bool loop)
{
	if (video)
		video->loop = loop;
}

int athena_video_get_current_frame(const AthenaVideo *video)
{
	return video ? video->current_frame : 0;
}

GSSURFACE *athena_video_get_texture(AthenaVideo *video)
{
	return video && video->ready ? &video->texture : NULL;
}

/*
 * native.shutdown hook: releases a video the application left open, so the
 * IPU interrupt handlers, the DMA channel and the decoder thread do not
 * outlive it. The QuickJS runtime is destroyed (finalizers included) before
 * shutdown hooks run.
 */
void athena_video_module_shutdown(void)
{
	athena_video_destroy(active_video);
}
