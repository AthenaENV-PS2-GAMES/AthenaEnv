// 
// A Texture manager for Athena
// 
// ----------------------------------------------------------------------
// Original authors of the code took from gsKit:
// Copyright 2017 - Rick "Maximus32" Gaiser <rgaiser@gmail.com>
// Copyright 2004 - Chris "Neovanglist" Gilbert <Neovanglist@LainOS.org>
// Licenced under Academic Free License version 2.0
// 
// Changes to work with Athena multi-path DMA manager, cache lock system and memory tracking:
// 2025 - Daniel Santos
//


#ifndef __TEXTURE_MANAGER_H__
#define __TEXTURE_MANAGER_H__

#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRANSFER_REQUEST_MASK GRAPHICS_TRANSFER_REQUEST_MASK

/// Quadwords texture_manager_add_mark() writes.
#define TEXTURE_MARK_QWC 4

void texture_upload(GSCONTEXT *gsGlobal, GSSURFACE *Texture);

/// Initialize the texture manager
void texture_manager_init(GSCONTEXT *gsGlobal);

/// Bind a texture to VRAM, will automatically transfer the texture.
int texture_manager_bind(GSCONTEXT *gsGlobal, GSSURFACE *tex, bool async);

/// Invalidate a texture, will automatically transfer the texture on next bind call.
void texture_manager_invalidate(GSSURFACE *tex);

/// Free the texture, this is mainly a performance optimization.
/// The texture will be automatically freed if not used.
void texture_manager_free(GSSURFACE *tex);

/// When starting a next frame (on vsync/swap), call this function.
/// It updates texture usage statistics.
void texture_manager_nextFrame(GSCONTEXT *gsGlobal);

int texture_manager_push(GSSURFACE *tex);

int texture_manager_lock(GSSURFACE *tex);

int texture_manager_unlock(GSSURFACE *tex);

int texture_manager_is_locked(GSSURFACE *tex);

int texture_manager_lock_and_bind(GSCONTEXT *gsGlobal, GSSURFACE *tex, bool async);

unsigned int texture_manager_used_memory();

unsigned int texture_manager_get_locked_memory();

unsigned int texture_manager_get_unlocked_memory();

/// Bytes uploaded to VRAM during the last frame.
unsigned int texture_manager_uploaded_memory();

/// Emits the MARK that uploads `texture_id`, the id an asynchronous bind of
/// a video texture returns. Its TEXTURE_MARK_QWC quadwords go inside a CNT
/// tag the caller counts.
void texture_manager_add_mark(owl_packet *packet, int texture_id);

#ifdef __cplusplus
};
#endif


#endif /* __TEXTURE_MANAGER_H__ */
