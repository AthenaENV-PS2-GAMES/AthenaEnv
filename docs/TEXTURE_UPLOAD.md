# Texture uploads

How a texture reaches VRAM when it is drawn, and why linear textures and
video frames take different paths.

## The problem

VRAM holds 4 MB. The texture manager (`texture_manager.c`) gives each bound
texture a block and, when VRAM is full, evicts the least used one. A player
animated from several atlases rebinds a different atlas almost every frame,
so blocks are reused all the time.

Asynchronous uploads used to run from the VIF1 `MARK` interrupt: the draw
emitted `MARK <id>`, the interrupt handler built a GIF (PATH3) packet with the
pixels and sent it, and the VIF1 stream went on with the draw. The two ran in
parallel, with no ordering between them:

| Version | Behaviour on an atlas swap |
| --- | --- |
| Before `6e4b170` | The draw sampled the block before the upload landed: a cut of the evicted atlas showed for one frame. |
| `6e4b170` | The frame that requested the upload skipped the draw: the player vanished for one frame. |
| Now | The upload is queued in the VIF1 stream, in order with the draws: the new atlas is drawn on the frame it is bound. |

## The design

`texture_manager_bind()` on a **linear** texture (sprites, atlases, fonts,
tiles) no longer marks the texture for the interrupt. It writes the upload
into the VIF1 packet stream right away. An asynchronous bind returns
`GRAPHICS_BIND_RESIDENT`, so the caller draws in the same frame:

```
CNT  [FLUSHA]                       wait for PATH1/2/3 work queued before
-- CLUT, if pending, then pixels --
CNT  [DIRECT 5]  A+D (EOP=0): BITBLTBUF TRXPOS TRXREG TRXDIR
per chunk of up to 0x7fff quadwords:
CNT  [DIRECT 1]  GIFtag IMAGE (n, EOP=0)
REF  [DIRECT n]  -> tex->Mem / tex->Clut
--
CNT  [DIRECT 2]  A+D (EOP=1): TEXFLUSH
```

A **synchronous** bind (`Image.lock()`, `ImageList` uploads, the render
targets) takes the same path, then sends the stream queued so far
(`owl_flush_packet()`) and waits for VIF1, so it returns once the pixels were
read and the caller may free them.

- **Ordering.** VIF1 processes the stream in order, and the pixels go to the
  GS through PATH2 like the draws do. Draws queued before the bind finish
  with the block's old texture, and draws queued after it see the new one.
- **FLUSHA** waits for work already queued that may still sample the block:
  VU1 programs (tilemap, PATH1) and PATH3 transfers.
- **TEXFLUSH** keeps the GS from sampling the old texture out of its texture
  cache.
- **One GIF packet.** Only TEXFLUSH sets EOP. PATH2 keeps the GIF from the
  registers to the last IMAGE quadword, so a video upload started by the
  MARK interrupt on PATH3 cannot set its own TRXDIR in between and receive
  these pixels.
- **One DIRECT per DMA tag.** The VIFcodes travel in the upper half of each
  tag (TTE is on for VIF1), the same pattern `unpack_list_append()` uses for
  UNPACK. No DIRECT spans two tags.
- **Size.** A 1024x1024 CT32 texture would take 37 quadwords of stream,
  well within the 1024-quadword half of the packet buffer.

`owl_draw.c`, `fntsys.c`, `image_font.c` and `tilemap.c` only emit `MARK`
(`texture_manager_add_mark()`) when the bind returns an id `>= 0`, which now
happens only for video.

### Video stays on the interrupt path

A video frame (`GSSURFACE.Macroblock`) replaces the whole texture on every
frame. On that path, letting the upload run in parallel with the VIF1 stream
is worth more than strict ordering, and the stream would have to carry 8
quadwords per 16x16 macroblock (about 9000 for 640x448), which does not fit
in a packet half. Macroblock textures keep the `MARK` + PATH3 upload, with
its known trade-off: the frame being uploaded may be sampled one frame
late.

## Implementation steps

1. **In-stream emitter** (`texture_manager.c`): `stream_send()` writes one
   transfer (registers, then IMAGE chunks by REF). `texture_stream_upload()`
   wraps the CLUT and pixel transfers in `FLUSHA` ... `TEXFLUSH` and queries
   the exact size from `owl_query_packet()`.
2. **Route the asynchronous bind** (`texture_manager_bind`):
   `stream = async && !tex->Macroblock`. On that path the transfer-request
   bits are never set, nothing goes to `texture_upload_queue`, and the
   result is `GRAPHICS_BIND_RESIDENT`. Synchronous binds and video are
   unchanged.
3. **Revert `texture_draw_ready()`** (`owl_draw.c`, from `6e4b170`). Sprites
   no longer need it, and it would drop video on every frame, since video
   invalidates its texture each frame.
4. **Synchronous binds in the stream**, followed by a flush and a VIF1 wait.
5. **Allocator lockup** (`_blockAlloc`): when a texture could not fit even
   after evicting every unlocked block, the eviction loop stepped `weight`
   by one up to `0xFFFFFFFF`, scanning the block list each time, and the EE
   hung (black screen). Each pass now jumps to the lowest weight left and
   the search stops once no unlocked texture is left; the bind then fails
   and the draw is skipped.

## Diagnostics

`Screen.getMemoryStats(Screen.VRAM_UPLOADED)` returns the bytes uploaded to
VRAM during the last frame (stream, synchronous and video uploads). A scene
that keeps it high every frame is thrashing VRAM. `tests/memory_stats.js`
shows it.

`tests/texture_upload_test.js` draws six 512x512 CT32 atlases (6 MB, more
than VRAM holds) twice per frame in opposite orders, a T8 texture and a
1024x256 CT32 texture (three IMAGE chunks). Every tile must show its own
colour, without stripes.

## Roadmap

### Phase 1: core (done on `fix/VRAM_SHARED_BLOCK`)

- [x] In-stream emitter and routing in `texture_manager_bind`.
- [x] Revert the draw skip of `6e4b170`.
- [x] Debug build (`docker compose run --rm debug`).

### Phase 2: validation

Checked on PCSX2 2.8.2 with the debug build:

- [x] **VRAM pressure** (`tests/texture_upload_test.js`): six 1 MB atlases
      re-uploaded every frame (about 13 MB per frame), drawn in two orders:
      every tile right, 60 fps. It first locked up, from the allocator loop
      (step 5 above), not from the stream.
- [x] **Paletted textures:** T8 (the test) and T4 (`bitWar` HUD): CLUT and
      pixels uploaded together.
- [x] **Large texture:** 1024x256 CT32, three IMAGE chunks. A 1024x1024
      CT32 texture is 4 MB, all of VRAM, so it can never bind; a 1024x512
      one does not fit next to the frame buffers and is now skipped instead
      of hanging.
- [x] **Tilemap (VU1/PATH1)**, sprites, HUD and fonts together: `bitWar`
      level 1.
- [x] **Fonts** (`fntsys`) and **video**
      (`tests/video_texture_example.js`, rotated quad included): unchanged.
- [x] **Synchronous binds mid-frame:** `Image.lock()` in `Game.enter()`.
- [ ] **Multi-atlas player** (the original bug), with input, on PCSX2 and on
      hardware. The pressure test covers the same ordering, harder.
- [ ] `image_font` (bitmap fonts) on screen.
- [ ] Hardware check of the one assumption PCSX2 may hide: a GIF packet that
      spans several DIRECTs (registers, IMAGE tag and IMAGE data in
      separate DIRECTs, EOP only at TEXFLUSH). If hardware rejects it, set
      EOP on the register and IMAGE tags: it works on PCSX2 too, and only
      reopens the window for video uploads described above.

### Phase 3: cleanup (done)

- [x] One helper, `texture_manager_add_mark()`, replaces the copies of the
      `MARK` block in `owl_draw.c`, `fntsys.c`, `image_font.c` and
      `tilemap.c`.
- [x] `image_font.c` returns early on `GRAPHICS_BIND_ERROR` and tests
      `texture_id >= 0`.
- [x] `athena_image_lock()`: comment updated.
- [x] `Screen.VRAM_UPLOADED`: bytes uploaded during the last frame.

### Phase 4: follow-ups

- [x] **Synchronous uploads** go through the stream, followed by a flush
      and a VIF1 wait. Video keeps its synchronous PATH3 upload.
- [ ] **Eviction policy:** avoid evicting a block used earlier in the same
      frame when a free or older one exists. This reduces re-uploads under
      pressure, but correctness no longer depends on it.
- [ ] **Video:** if one-frame-late sampling becomes visible, build the
      macroblock chain in its own buffer and `CALL` it from the VIF1 stream,
      which keeps ordering at one quadword of stream.
