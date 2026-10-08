// 
// A Texture manager for Athena
// 
// ----------------------------------------------------------------------
// Original authors of the code took from gsKit:
// Copyright 2017 - Rick "Maximus32" Gaiser <rgaiser@gmail.com>
// Copyright 2004 - Chris "Neovanglist" Gilbert <Neovanglist@LainOS.org>
// Licenced under Academic Free License version 2.0
// 
// Changes to work with Athena multi-path DMA manager:
// 2025 - Daniel Santos
//

#include <stdio.h>
#include <malloc.h>
#include <kernel.h>
#include <string.h>

#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>

#include <debug.h>

#include <athena/graphics/texture_manager.h>

#define VIF1_MARK_CLEAN 65535

#define TEXTURE_UPLOAD_QUEUE_SIZE 2048

//#define MAX_TEXTURE_PACKET_SIZE 640
#define MAX_TEXTURE_PACKET_SIZE 36864

volatile uint32_t *VIF1_MARK = (volatile uint32_t *)(0x10003C30);

struct SVramBlock {
	unsigned int iStart;
	unsigned int iSize;
	unsigned int iUseCount;
	unsigned int iUseCountPrev;
	unsigned int bLocked;

	GSSURFACE * tex;
	struct SVramBlock * pNext;
	struct SVramBlock * pPrev;
};

static struct SVramBlock * __head = NULL;
static int texture_upload_callback_id = -1;

static int texture_upload_queue_top = 0;

/* Bytes sent to VRAM during the current frame and during the last one. */
static volatile unsigned int uploaded_bytes = 0;
static unsigned int uploaded_bytes_last = 0;

GSSURFACE *texture_upload_queue[TEXTURE_UPLOAD_QUEUE_SIZE] = { NULL };

owl_packet* async_upload_packet = NULL;

owl_qword async_upload_packet_buffer[owl_packet_size(MAX_TEXTURE_PACKET_SIZE)] qw_aligned;

static inline unsigned int align_to_page(unsigned int addr) {
	return (addr + 8191) & ~8191;
}

void texture_send(uint32_t *mem, int width, int height, uint32_t tbp,
	int psm, uint32_t tbw, uint8_t clut)
{
	//int packet_size;
	int packets;
	int remain;
	int qwc;

	uint32_t tex_size = athena_surface_size(width, height, psm);

	qwc = (tex_size / 16) + (tex_size % 16? 1 : 0);

	packets = qwc / GS_GIF_BLOCKSIZE;
	remain  = qwc % GS_GIF_BLOCKSIZE;

	//if(clut == GS_CLUT_TEXTURE)
	//	packet_size = (6+(packets * 3)+remain);
	//else
	//	packet_size = (9+(packets * 3)+remain);

	//if(remain > 0)
	//	packet_size += 3;

	owl_add_cnt_tag_fill(async_upload_packet, 5);

	owl_add_tag(async_upload_packet, GIF_AD, GIFTAG(4, 0, 0, 0, 0, 1));

	owl_add_tag(async_upload_packet, GS_BITBLTBUF, GS_SETREG_BITBLTBUF(0, 0, 0, tbp/256, tbw, psm));
	owl_add_tag(async_upload_packet, GS_TRXPOS, GS_SETREG_TRXPOS(0, 0, 0, 0, 0));
	owl_add_tag(async_upload_packet, GS_TRXREG, GS_SETREG_TRXREG(width, height));
	owl_add_tag(async_upload_packet, GS_TRXDIR, GS_SETREG_TRXDIR(0));

	while (packets-- > 0)
	{
		owl_add_tag(async_upload_packet, 0, DMA_TAG( 1, 0, DMA_CNT, 0, 0, 0));
		owl_add_tag(async_upload_packet, 0, GIFTAG( GS_GIF_BLOCKSIZE, 0, 0, 0, GSKIT_GIF_FLG_IMAGE, 0 ));
		owl_add_tag(async_upload_packet, 0, DMA_TAG( GS_GIF_BLOCKSIZE, 0, DMA_REF, 0, (u32)mem, 0 ));

		mem += (GS_GIF_BLOCKSIZE * 4);
	}

	if (remain > 0) {
		owl_add_tag(async_upload_packet, 0, DMA_TAG( 1, 0, DMA_CNT, 0, 0, 0));
		owl_add_tag(async_upload_packet, 0, GIFTAG( remain, 0, 0, 0, GSKIT_GIF_FLG_IMAGE, 0 ));
		owl_add_tag(async_upload_packet, 0, DMA_TAG( remain, 0, DMA_REF, 0, (u32)mem, 0 ));
	}

	//if(clut != GS_CLUT_TEXTURE)
	//{
	//	owl_add_end_tag(custom_packet, 2);
	//	owl_add_tag(custom_packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));
	//	owl_add_tag(custom_packet, GS_TEXFLUSH, 0);
	//} else {
	//	owl_add_end_tag(custom_packet, 0);
	//}
}

/**
 * @brief Upload macroblock-formatted texture data to VRAM
 * Data is in 16x16 pixel block format (used by MPEG decoder)
 */
void texture_send_macroblock(uint32_t *mem, int width, int height,
	uint32_t tbp, int psm, uint32_t tbw)
{
	int mb_width = width >> 4;   // Number of macroblocks in width
	int mb_height = height >> 4; // Number of macroblocks in height
	u8* frame_ptr = (u8*)mem;
	
	// Process each macroblock
	for (int mb_y = 0; mb_y < mb_height; mb_y++) {
		for (int mb_x = 0; mb_x < mb_width; mb_x++) {
			int dest_x = mb_x * 16;
			int dest_y = mb_y * 16;
			
			// Setup transfer: CNT tag with 5 qwords of register data
			owl_add_cnt_tag_fill(async_upload_packet, 5);
			
			// GIF AD tag: 4 registers to set
			owl_add_tag(async_upload_packet, GIF_AD, GIFTAG(4, 0, 0, 0, 0, 1));
			
			// Set destination buffer (VRAM address, buffer width, pixel format)
			owl_add_tag(async_upload_packet, GS_BITBLTBUF, 
				GS_SETREG_BITBLTBUF(0, 0, 0, tbp / 256, tbw, psm));
			
			// Set transfer position in destination (x, y in VRAM)
			owl_add_tag(async_upload_packet, GS_TRXPOS, 
				GS_SETREG_TRXPOS(0, 0, dest_x, dest_y, 0));
			
			// Set transfer size (16x16 pixels)
			owl_add_tag(async_upload_packet, GS_TRXREG, 
				GS_SETREG_TRXREG(16, 16));
			
			// Set transfer direction (host to local = upload)
			owl_add_tag(async_upload_packet, GS_TRXDIR, 
				GS_SETREG_TRXDIR(0));
			
			// Send the actual pixel data for this macroblock
			// Each macroblock is 16x16 = 256 pixels * 4 bytes = 1024 bytes = 64 quadwords
			owl_add_tag(async_upload_packet, 0, DMA_TAG(1, 0, DMA_CNT, 0, 0, 0));
			owl_add_tag(async_upload_packet, 0, GIFTAG(64, 0, 0, 0, GSKIT_GIF_FLG_IMAGE, 0));
			owl_add_tag(async_upload_packet, 0, DMA_TAG(64, 0, DMA_REF, 0, (u32)frame_ptr, 0));
			
			// Move to next macroblock in source buffer
			frame_ptr += 1024;  // 16x16 * 4 bytes
		}
	}
}

/*
 * Uploads in the VIF1 stream, for linear textures (sprites, atlases, fonts,
 * tiles). The pixels reach the GS through PATH2, in order with the draws
 * queued around them: after the last draw that sampled the block's previous
 * texture, and before the first draw that samples this one. Macroblock
 * (video) textures keep the MARK upload on PATH3, which runs in parallel
 * with the draws.
 *
 * Every DIRECT is contained in one DMA tag: the VIFcodes travel in the upper
 * half of the tag (TTE) and the pixels come from a REF to the texture memory,
 * as unpack_list_append() does for UNPACK.
 *
 * The whole upload is one GIF packet: only TEXFLUSH, at its end, sets EOP.
 * PATH2 keeps the GIF from the registers to the last IMAGE quadword, so a
 * PATH3 transfer (video, started by the MARK interrupt) cannot set its own
 * TRXDIR between them and receive these pixels.
 */

/* Quadwords of `size` bytes, as texture_send() rounds them. */
static int stream_qwords(uint32_t size)
{
	return (size / 16) + (size % 16 ? 1 : 0);
}

/* Quadwords stream_send() emits for a texture of `qwc` quadwords. */
static int stream_send_size(int qwc)
{
	int chunks = (qwc + GS_GIF_BLOCKSIZE - 1) / GS_GIF_BLOCKSIZE;

	/* Transfer registers 6; per chunk, the IMAGE tag 2 and the REF 1. */
	return 6 + chunks * 3;
}

static void stream_direct_cnt(owl_packet *packet, int qwc)
{
	owl_add_cnt_tag(packet, qwc, owl_vif_code_double(
		VIF_CODE(qwc, 0, VIF_DIRECT, 0), VIF_CODE(0, 0, VIF_NOP, 0)));
}

static void stream_send(owl_packet *packet, uint32_t *mem, int width,
	int height, uint32_t tbp, int psm, uint32_t tbw)
{
	int qwc = stream_qwords(athena_surface_size(width, height, psm));

	uploaded_bytes += qwc * 16;

	stream_direct_cnt(packet, 5);
	owl_add_tag(packet, GIF_AD, GIFTAG(4, 0, 0, 0, 0, 1));
	owl_add_tag(packet, GS_BITBLTBUF, GS_SETREG_BITBLTBUF(0, 0, 0, tbp/256, tbw, psm));
	owl_add_tag(packet, GS_TRXPOS, GS_SETREG_TRXPOS(0, 0, 0, 0, 0));
	owl_add_tag(packet, GS_TRXREG, GS_SETREG_TRXREG(width, height));
	owl_add_tag(packet, GS_TRXDIR, GS_SETREG_TRXDIR(0));

	while (qwc > 0) {
		int chunk = qwc < GS_GIF_BLOCKSIZE ? qwc : GS_GIF_BLOCKSIZE;

		stream_direct_cnt(packet, 1);
		owl_add_tag(packet, 0, GIFTAG(chunk, 0, 0, 0, GSKIT_GIF_FLG_IMAGE, 0));
		owl_add_tag(packet, owl_vif_code_double(
			VIF_CODE(chunk, 0, VIF_DIRECT, 0), VIF_CODE(0, 0, VIF_NOP, 0)),
			DMA_TAG(chunk, 0, DMA_REF, 0, (u32)mem, 0));

		mem += chunk * 4;
		qwc -= chunk;
	}
}

/*
 * Queues the upload of `tex` (its CLUT and/or pixels) in the VIF1 stream.
 * FLUSHA first: draws already queued, on PATH1 (VU1) or PATH2, and PATH3
 * uploads end before the block is overwritten. TEXFLUSH last, so the GS
 * does not sample the block's previous texture from its texture cache.
 */
static void texture_stream_upload(GSSURFACE *tex, bool texture, bool clut,
	int cwidth, int cheight)
{
	owl_packet *packet;
	/* FLUSHA 1, TEXFLUSH 3. */
	int size = 1 + 3;

	if (clut)
		size += stream_send_size(stream_qwords(
			athena_surface_size(cwidth, cheight, tex->ClutPSM)));
	if (texture)
		size += stream_send_size(stream_qwords(
			athena_surface_size(tex->Width, tex->Height, tex->PSM)));

	packet = owl_query_packet(CHANNEL_VIF1, size);

	owl_add_cnt_tag(packet, 0, owl_vif_code_double(
		VIF_CODE(0, 0, VIF_NOP, 0), VIF_CODE(0, 0, VIF_FLUSHA, 0)));

	if (clut)
		stream_send(packet, tex->Clut, cwidth, cheight, tex->VramClut,
			tex->ClutPSM, 1);
	if (texture)
		stream_send(packet, tex->Mem, tex->Width, tex->Height, tex->Vram,
			tex->PSM, tex->TBW);

	stream_direct_cnt(packet, 2);
	owl_add_tag(packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));
	owl_add_tag(packet, GS_TEXFLUSH, 0);
}

int upload_texture_handler(int cause) {
	DIntr();

	if (cause == INTC_VIF1) {
		uint32_t tex_id = *VIF1_MARK;

		*VIF1_MARK = VIF1_MARK_CLEAN;

		if (tex_id < TEXTURE_UPLOAD_QUEUE_SIZE &&
			texture_upload_queue[tex_id]) {
			GSSURFACE *tex = texture_upload_queue[tex_id];
			texture_upload_queue[tex_id] = NULL;

			

			if ((tex->VramClut & TRANSFER_REQUEST_MASK) == TRANSFER_REQUEST_MASK) {
				tex->VramClut &= ~TRANSFER_REQUEST_MASK;

				uploaded_bytes += athena_surface_size(
					tex->PSM == GS_PSM_T8 ? 16 : 8,
					tex->PSM == GS_PSM_T8 ? 16 : 2, tex->ClutPSM);

				if (tex->PSM == GS_PSM_T8) {
					texture_send(tex->Clut, 16, 16, tex->VramClut, tex->ClutPSM, 1, GS_CLUT_PALLETE);

				} else if (tex->PSM == GS_PSM_T4) {
					texture_send(tex->Clut, 8,  2, tex->VramClut, tex->ClutPSM, 1, GS_CLUT_PALLETE);
				}
			}

			if ((tex->Vram & TRANSFER_REQUEST_MASK) == TRANSFER_REQUEST_MASK) {
				tex->Vram &= ~TRANSFER_REQUEST_MASK;

				athena_calculate_tbw(tex);
				uploaded_bytes += athena_surface_size(tex->Width,
					tex->Height, tex->PSM);

				// Select upload function based on data format
				if (tex->Macroblock) {
					texture_send_macroblock(tex->Mem, tex->Width, tex->Height, tex->Vram, tex->PSM, tex->TBW);
				} else {
					texture_send(tex->Mem, tex->Width, tex->Height, tex->Vram, tex->PSM, tex->TBW, (tex->Clut ? GS_CLUT_TEXTURE : GS_CLUT_NONE));
				}
			}

			owl_add_end_tag(async_upload_packet, 2);
			owl_add_tag(async_upload_packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));
			owl_add_tag(async_upload_packet, GS_TEXFLUSH, 0);

			owl_send_packet(async_upload_packet);
		}

		*(volatile uint32_t *)(0x10003C10) = 8;
	}

	ExitHandler();	
	return 0;
}

#define VIF0_ERR        ((volatile uint32_t *)(0x10003820))
#define VIF1_ERR        ((volatile uint32_t *)(0x10003c20))
#define VIF0_ERR_ME0_M      (0x01<<1)
#define VIF1_ERR_ME0_M      (0x01<<1)

void add_texture_upload_handler(int (*upload_callback)(int))
{
	DIntr();

	texture_upload_callback_id = AddIntcHandler(INTC_VIF1, upload_callback, -1);
	*VIF0_ERR = VIF0_ERR_ME0_M;
	*VIF1_ERR = VIF1_ERR_ME0_M;
	EnableIntc(INTC_VIF1);

	EIntr();
}

void remove_texture_upload_handler()
{
	DIntr();

	DisableIntc(INTC_VIF1);

	RemoveIntcHandler(INTC_VIF1, texture_upload_callback_id);

	EIntr();
}



void texture_upload(GSCONTEXT *gsGlobal, GSSURFACE *Texture)
{
	athena_calculate_tbw(Texture);

	if (Texture->PSM == GS_PSM_T8)
	{
		//texture_send(Texture->Mem, Texture->Width, Texture->Height, Texture->Vram, Texture->PSM, Texture->TBW, GS_CLUT_TEXTURE, NULL);
		//texture_send(Texture->Clut, 16, 16, Texture->VramClut, Texture->ClutPSM, 1, GS_CLUT_PALLETE, NULL);

	}
	else if (Texture->PSM == GS_PSM_T4)
	{
		//texture_send(Texture->Mem, Texture->Width, Texture->Height, Texture->Vram, Texture->PSM, Texture->TBW, GS_CLUT_TEXTURE, NULL);
		//texture_send(Texture->Clut, 8,  2, Texture->VramClut, Texture->ClutPSM, 1, GS_CLUT_PALLETE, NULL);
	}
	else
	{
		//texture_send(Texture->Mem, Texture->Width, Texture->Height, Texture->Vram, Texture->PSM, Texture->TBW, GS_CLUT_NONE, NULL);
	}
}

//---------------------------------------------------------------------------
// Private functions

//---------------------------------------------------------------------------
static inline struct SVramBlock *
_blockCreate(unsigned int start, unsigned int size)
{
	struct SVramBlock * block;


	block = malloc(sizeof(struct SVramBlock));
	
	block->iStart = start;
	block->iSize = size;
	block->iUseCount = 0;
	block->iUseCountPrev = 0;
	block->bLocked = 0;

	block->tex = NULL;
	block->pNext = NULL;
	block->pPrev = NULL;

	return block;
}

//---------------------------------------------------------------------------
static inline void
_blockInsertAfter(struct SVramBlock * block, struct SVramBlock * next)
{
	next->pNext = block->pNext;
	next->pPrev = block;
	if(block->pNext != NULL) {
		block->pNext->pPrev = next;
	}
		
	block->pNext = next;
}

static inline struct SVramBlock *
_blockSplitFreeAligned(struct SVramBlock * block, unsigned int size, unsigned int aligned_start)
{
	struct SVramBlock * pNewBlock;
	struct SVramBlock * pAlignmentBlock = NULL;

	if (aligned_start > block->iStart) {
		unsigned int padding = aligned_start - block->iStart;
		pAlignmentBlock = _blockCreate(block->iStart, padding);
		pAlignmentBlock->pPrev = block->pPrev;
		pAlignmentBlock->pNext = block;
		if (block->pPrev != NULL)
			block->pPrev->pNext = pAlignmentBlock;
		else
			__head = pAlignmentBlock;
		block->pPrev = pAlignmentBlock;
		block->iStart = aligned_start;
		block->iSize -= padding;
	}

	if (block->iSize > size) {
		pNewBlock = _blockCreate(block->iStart + size, block->iSize - size);
		block->iSize = size;
		_blockInsertAfter(block, pNewBlock);
	}

	return block;
}

//---------------------------------------------------------------------------
static inline struct SVramBlock *
_blockRemove(struct SVramBlock * block)
{
	struct SVramBlock * next = block->pNext;

	if(block->pPrev != NULL)
		block->pPrev->pNext = block->pNext;
	if(block->pNext != NULL)
		block->pNext->pPrev = block->pPrev;

	free(block);

	return next;
}

//---------------------------------------------------------------------------
static inline struct SVramBlock *
_blockSplitFree(struct SVramBlock * block, unsigned int size)
{
	struct SVramBlock * pNewBlock;

	// Create second block with leftover size
	pNewBlock = _blockCreate(block->iStart + size, block->iSize - size);
	// Shrink first block
	block->iSize = size;
	// Insert second block after first block
	_blockInsertAfter(block, pNewBlock);

	return block;
}

//---------------------------------------------------------------------------
static inline struct SVramBlock *
_blockMergeFree(struct SVramBlock * block)
{
	// Search backwards to the first free block
	while ((block->pPrev != NULL) && (block->pPrev->tex == NULL))
		block = block->pPrev;

	// Merge free blocks
	while((block->pNext != NULL) && (block->pNext->tex == NULL)) {
		block->iSize += block->pNext->iSize;
		_blockRemove(block->pNext);
	}

	return block;
}

//---------------------------------------------------------------------------
// How often is this vram block used, mainly based on predictions
static inline unsigned int
_blockGetWeight(struct SVramBlock * block)
{
	unsigned int weight = 0;

	if ((block != NULL) && (block->tex != NULL)) {
		if (block->bLocked) {
			return 0xFFFFFFFF;
		}

		if(block->iUseCount == block->iUseCountPrev) {
			// Prediction:
			// - This frame: done
			// - Next frame: needed
			weight += block->iUseCountPrev;
		}
		else if(block->iUseCount < block->iUseCountPrev) {
			// Prediction:
			// - This frame: needed
			weight += block->iUseCountPrev - block->iUseCount;
			// - Next frame: needed
			weight += block->iUseCountPrev;
		}
		else {
			// Prediction:
			// - This frame: unsure
			weight += 1;
			// - Next frame: needed
			weight += block->iUseCount;
		}
	}

	return weight;
}

//---------------------------------------------------------------------------
// Simple block allocator
static inline struct SVramBlock *
_blockAlloc(unsigned int size, bool page_aligned)
{
	struct SVramBlock * block = NULL;
	unsigned int weight = 0;
	unsigned int aligned_start;
	unsigned int required_size;

	for (block = __head; block != NULL; block = block->pNext) {
		if (block->tex == NULL) {
			if (page_aligned) {
				aligned_start = align_to_page(block->iStart);
				required_size = size + (aligned_start - block->iStart);
				if (block->iSize >= required_size) {
					break;
				}
			} else if (block->iSize >= size) {
				break;
			}
		}
	}

	/*
	 * Evict the least used textures first. Each pass jumps to the lowest
	 * weight left, and the search ends once no unlocked texture is left:
	 * stepping the weight by one up to 0xFFFFFFFF locked up the EE when the
	 * texture could not fit at all.
	 */
	while (block == NULL) {
		unsigned int next = 0xFFFFFFFF;

		for (block = __head; block != NULL; block = block->pNext) {
			if ((block->tex != NULL) && !block->bLocked &&
				(_blockGetWeight(block) > weight)) {
				if (_blockGetWeight(block) < next)
					next = _blockGetWeight(block);
				continue;
			}
			if ((block->tex != NULL) && !block->bLocked) {
				block->tex = NULL;
				block = _blockMergeFree(block);
				
				if (page_aligned) {
					aligned_start = align_to_page(block->iStart);
					required_size = size + (aligned_start - block->iStart);
					if (block->iSize >= required_size) {
						break;
					}
				} else if (block->iSize >= size) {
					break;
				}
			}
		}
		if (block == NULL && next == 0xFFFFFFFF)
			break;
		weight = next;
	}

	if (block != NULL) {
		if (page_aligned) {
			aligned_start = align_to_page(block->iStart);
			block = _blockSplitFreeAligned(block, size, aligned_start);
		} else {
			block = _blockSplitFree(block, size);
		}
	}

	return block;
}

//---------------------------------------------------------------------------
// Public functions

//---------------------------------------------------------------------------
void texture_manager_init(GSCONTEXT *gsGlobal)
{
	struct SVramBlock * block = __head;

	// Delete all blocks (if present)
	while(block != NULL)
		block = _blockRemove(block);

	// Allocate the initial free block
	__head = _blockCreate(0, 4*1024*1024);
	texture_upload_queue_top = 0;
	memset(texture_upload_queue, 0, sizeof(texture_upload_queue));

	*VIF1_MARK = VIF1_MARK_CLEAN;

	if (texture_upload_callback_id == -1) {
		add_texture_upload_handler(upload_texture_handler);
	}

	async_upload_packet = owl_create_packet(CHANNEL_GIF, owl_packet_size(MAX_TEXTURE_PACKET_SIZE), async_upload_packet_buffer);
}

//---------------------------------------------------------------------------

int texture_manager_push(GSSURFACE *tex) {
	int id = texture_upload_queue_top;
	if (!tex || texture_upload_queue[id] != NULL)
		return GRAPHICS_BIND_ERROR;

	texture_upload_queue[id] = tex;

	texture_upload_queue_top = ( texture_upload_queue_top + 1 ) & (TEXTURE_UPLOAD_QUEUE_SIZE-1);

	return id;
}


int texture_manager_bind(GSCONTEXT *gsGlobal, GSSURFACE *tex, bool async) {
	struct SVramBlock * block;
	bool block_created = false;
	unsigned int ttransfer = 0;
	unsigned int ctransfer = 0;
	unsigned int tsize;
	unsigned int csize = 0;
	unsigned int cwidth = 16;
	unsigned int cheight = 16;
	/*
	 * A linear texture uploads in the VIF1 stream, so draws queued after
	 * the bind see the new pixels and draws queued before it the old ones.
	 * An asynchronous bind reports it resident. Video (macroblock) textures
	 * upload on PATH3: from the MARK interrupt, or now if synchronous.
	 */
	bool stream = !tex->Macroblock;

	for (block = __head; block != NULL; block = block->pNext) {
		if(block->tex == tex)
			break;
	}

	tsize = athena_vram_surface_size(tex->Width, tex->Height, tex->PSM);
	if (tex->Clut != NULL) {
		cwidth  = (tex->PSM == GS_PSM_T8) ? 16 : 8;
		cheight = (tex->PSM == GS_PSM_T8) ? 16 : 2;
		csize   = athena_vram_surface_size(cwidth, cheight, tex->ClutPSM);
	}
	if (tsize == UINT32_MAX || csize == UINT32_MAX)
		return GRAPHICS_BIND_ERROR;

	if (block == NULL) {
		block = _blockAlloc(tsize + csize, tex->PageAligned);
		if (block == NULL)
			return GRAPHICS_BIND_ERROR;
		block_created = true;
		block->tex = tex;
		block->iUseCount = 0;
		block->iUseCountPrev = 1;
		block->bLocked = 0;

		tex->Vram = 0;
		tex->VramClut = 0;
	}

	if (tex->Vram == 0)
		ttransfer = 1;

	if (tex->Clut != NULL && tex->VramClut == 0)
		ctransfer = 1;

	if (ttransfer) {
		if (tex->PageAligned) {
			tex->Vram = align_to_page(block->iStart);
		} else {
			tex->Vram = block->iStart;
		}
		
		athena_calculate_tbw(tex);

		if (tex->Mem) {
			SyncDCache(tex->Mem,
				(u8 *)(tex->Mem) + athena_surface_size(
					tex->Width, tex->Height, tex->PSM));

			if (stream) {
				/* Sent below, after the CLUT address is known. */
			} else if (!async) {
				uploaded_bytes += athena_surface_size(tex->Width,
					tex->Height, tex->PSM);
				texture_send_macroblock(tex->Mem, tex->Width, tex->Height, tex->Vram, tex->PSM, tex->TBW);
			} else {
				tex->Vram |= TRANSFER_REQUEST_MASK;
			}
		} else {
			ttransfer = 0;
		}
	}

	if (ctransfer) {
		if (tex->PageAligned) {
			tex->VramClut = align_to_page(block->iStart) + tsize;
		} else {
			tex->VramClut = block->iStart + tsize;
		}

		if (tex->Clut) {
			SyncDCache(tex->Clut, (u8 *)(tex->Clut) + csize);

			if (stream) {
				/* Sent below, with the pixels. */
			} else if (!async) {
				uploaded_bytes += athena_surface_size(cwidth, cheight,
					tex->ClutPSM);
				texture_send(tex->Clut, cwidth, cheight, tex->VramClut,
					tex->ClutPSM, 1, GS_CLUT_PALLETE);
			} else {
				tex->VramClut |= TRANSFER_REQUEST_MASK;
			}
		} else {
			ctransfer = 0;
		}
	}

	block->iUseCount++;

	if (stream) {
		if (!(ttransfer || ctransfer))
			return async ? GRAPHICS_BIND_RESIDENT : 0;

		texture_stream_upload(tex, ttransfer, ctransfer, cwidth, cheight);
		if (async)
			return GRAPHICS_BIND_RESIDENT;

		/*
		 * A synchronous bind returns once the pixels were read, so the
		 * caller may free them: send the stream so far and wait for it.
		 */
		owl_flush_packet();
		dmaKit_wait(DMA_CHANNEL_VIF1, 0);
		return ttransfer | ctransfer;
	}

	if (!async && (ttransfer || ctransfer)) {
		owl_add_end_tag(async_upload_packet, 2);
		owl_add_tag(async_upload_packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));
		owl_add_tag(async_upload_packet, GS_TEXFLUSH, 0);
		owl_send_packet(async_upload_packet);
	}

	if (async) {
		int upload_id;
		if (!(ttransfer || ctransfer))
			return GRAPHICS_BIND_RESIDENT;

		upload_id = texture_manager_push(tex);
		if (upload_id == GRAPHICS_BIND_ERROR) {
			if (ttransfer)
				tex->Vram = 0;
			if (ctransfer)
				tex->VramClut = 0;
			if (block_created)
				texture_manager_free(tex);
			return GRAPHICS_BIND_ERROR;
		}
		return upload_id;
	}

	return (ttransfer|ctransfer);
}

//---------------------------------------------------------------------------
int texture_manager_lock(GSSURFACE *tex)
{
	struct SVramBlock * block;

	for (block = __head; block != NULL; block = block->pNext) {
		if(block->tex == tex) {
			block->bLocked = 1;
			return 1;  
		}
	}
	
	return 0;  
}

//---------------------------------------------------------------------------
int texture_manager_unlock(GSSURFACE *tex)
{
	struct SVramBlock * block;

	for (block = __head; block != NULL; block = block->pNext) {
		if(block->tex == tex) {
			block->bLocked = 0;
			return 1;
		}
	}
	
	return 0;  
}

int texture_manager_is_locked(GSSURFACE *tex)
{
	struct SVramBlock * block;

	for (block = __head; block != NULL; block = block->pNext) {
		if(block->tex == tex) {
			return block->bLocked;
		}
	}
	
	return 0;  
}

int texture_manager_lock_and_bind(GSCONTEXT *gsGlobal, GSSURFACE *tex, bool async)
{
	int result = texture_manager_bind(gsGlobal, tex, async);
	
	if (result >= 0 || result == GRAPHICS_BIND_RESIDENT) {
		texture_manager_lock(tex);
	}
	
	return result;
}

int texture_manager_get_locked_count()
{
	struct SVramBlock * block;
	int count = 0;

	for (block = __head; block != NULL; block = block->pNext) {
		if ((block->tex != NULL) && block->bLocked) {
			count++;
		}
	}
	
	return count;
}

unsigned int texture_manager_get_locked_memory()
{
	struct SVramBlock * block;
	unsigned int total = 0;

	for (block = __head; block != NULL; block = block->pNext) {
		if ((block->tex != NULL) && block->bLocked) {
			total += block->iSize;
		}
	}
	
	return total;
}

unsigned int texture_manager_get_unlocked_memory()
{
	struct SVramBlock * block;
	unsigned int total = 0;

	for (block = __head; block != NULL; block = block->pNext) {
		if ((block->tex != NULL) && !block->bLocked) {
			total += block->iSize;
		}
	}
	
	return total;
}

unsigned int texture_manager_used_memory()
{
	struct SVramBlock * block;
	unsigned int total = 0;

	for (block = __head; block != NULL; block = block->pNext) {
		if (block->tex != NULL) {
			total += block->iSize;
		}
	}
	
	return total;
}

void texture_manager_invalidate(GSSURFACE *tex)
{
	tex->Vram = 0;
	tex->VramClut = 0;
}

//---------------------------------------------------------------------------
void texture_manager_free(GSSURFACE * tex)
{
	struct SVramBlock * block;

	// Locate texture
	for (block = __head; block != NULL; block = block->pNext) {
		if(block->tex == tex) {
			for (int i = 0; i < TEXTURE_UPLOAD_QUEUE_SIZE; i++) {
				if (texture_upload_queue[i] == tex)
					texture_upload_queue[i] = NULL;
			}
			// Free block
			block->tex = NULL;
			block->bLocked = 0;
			_blockMergeFree(block);
			tex->Vram = 0;
			tex->VramClut = 0;
			break;
		}
	}
}

//---------------------------------------------------------------------------
unsigned int texture_manager_uploaded_memory()
{
	return uploaded_bytes_last;
}

/*
 * The VIF MARK that makes the interrupt upload a video texture: the 4
 * quadwords go inside a CNT tag the caller counts (TEXTURE_MARK_QWC).
 */
void texture_manager_add_mark(owl_packet *packet, int texture_id)
{
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_FLUSH, 0));
	owl_add_uint(packet, VIF_CODE(2, 0, VIF_DIRECT, 0));

	owl_add_tag(packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));
	owl_add_tag(packet, GIF_NOP, 0);

	owl_add_uint(packet, VIF_CODE(0, 0, VIF_FLUSHA, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(texture_id, 0, VIF_MARK, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 1));
}

void texture_manager_nextFrame(GSCONTEXT * gsGlobal)
{
	struct SVramBlock * block;

	uploaded_bytes_last = uploaded_bytes;
	uploaded_bytes = 0;

	// Register use count
	for(block = __head; block != NULL; block = block->pNext) {
		if(block->tex != NULL) {
			block->iUseCountPrev = block->iUseCount;
			block->iUseCount = 0;
		}
	}
}
