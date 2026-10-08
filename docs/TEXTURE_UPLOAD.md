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

`texture_manager_bind(tex, async = true)` on a **linear** texture (sprites,
atlases, fonts, tiles) no longer marks the texture for the interrupt. It
writes the upload into the VIF1 packet stream right away and returns
`GRAPHICS_BIND_RESIDENT`, so the caller draws in the same frame:

```
CNT  [FLUSHA]                       wait for PATH1/2/3 work queued before
-- CLUT, if pending, then pixels --
CNT  [DIRECT 5]  A+D: BITBLTBUF TRXPOS TRXREG TRXDIR
per chunk of up to 0x7fff quadwords:
CNT  [DIRECT 1]  GIFtag IMAGE (n)
REF  [DIRECT n]  -> tex->Mem / tex->Clut
--
CNT  [DIRECT 2]  A+D: TEXFLUSH
```

- **Ordering.** VIF1 processes the stream in order, and the pixels go to the
  GS through PATH2 like the draws do. Draws queued before the bind finish
  with the block's old texture, and draws queued after it see the new one.
- **FLUSHA** waits for work already queued that may still sample the block:
  VU1 programs (tilemap, PATH1) and PATH3 transfers.
- **TEXFLUSH** keeps the GS from sampling the old texture out of its texture
  cache.
- **One DIRECT per DMA tag.** The VIFcodes travel in the upper half of each
  tag (TTE is on for VIF1), the same pattern `unpack_list_append()` uses for
  UNPACK. No DIRECT spans two tags.
- **Size.** A 1024x1024 CT32 texture takes 37 quadwords of stream, well
  within the 1024-quadword half of the packet buffer.

The callers do not change. `owl_draw.c`, `fntsys.c`, `image_font.c` and
`tilemap.c` only emit `MARK` when the bind returns an id `>= 0`, which now
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

## Roadmap

### Phase 1: core (done on `fix/VRAM_SHARED_BLOCK`)

- [x] In-stream emitter and routing in `texture_manager_bind`.
- [x] Revert the draw skip of `6e4b170`.
- [x] Debug build (`docker compose run --rm debug`).

### Phase 2: validation (next)

- [ ] **Multi-atlas player** (the original bug): no cut of the previous atlas
      and no missing frame on atlas swaps, on PCSX2 and on hardware.
- [ ] **VRAM pressure:** a scene whose atlases do not all fit in VRAM. The
      result must be correct, possibly slower, and must not lock up.
- [ ] **Paletted textures** (T4/T8): CLUT and pixels uploaded together.
- [ ] **Tilemap (VU1/PATH1)** switching textures between spans: `FLUSHA`
      must not break the double-buffered batches.
- [ ] **Fonts** (`fntsys`, `image_font`) and a rotating view
      (`emit_tex_quad`, `draw_image_list_rotated`).
- [ ] **Video:** unchanged behaviour (no new flicker, no frame skips).
- [ ] **Large texture** (1024x1024 CT32): several IMAGE chunks.
- [ ] Hardware check of the one assumption PCSX2 may hide: a GIF packet whose
      tag and IMAGE data arrive in two consecutive DIRECTs.

### Phase 3: cleanup (after validation)

- [ ] Replace the five copies of the `MARK` block in `owl_draw.c` and those
      in `fntsys.c`, `image_font.c` and `tilemap.c` with one helper, since
      only video reaches it now.
- [ ] `image_font.c` tests `texture_id != -1`, so `GRAPHICS_BIND_ERROR` (-2)
      emits a `MARK` with an invalid id. Return early on error, as the other
      callers do.
- [ ] `athena_image_lock()`: its comment about the asynchronous bind no longer
      applies to linear textures.
- [ ] Debug counter of in-stream uploads per frame (bytes), next to the tile
      diagnostics, to find scenes that thrash VRAM.

### Phase 4: follow-ups (optional)

- [ ] **Synchronous uploads** (`graphics_surface_bind_sync`, `Image.lock`)
      still go over PATH3 outside the stream and may overwrite a block that a
      draw in flight still samples. Route them through the stream, followed
      by a flush and wait.
- [ ] **Eviction policy:** avoid evicting a block used earlier in the same
      frame when a free or older one exists. This reduces re-uploads under
      pressure, but correctness no longer depends on it.
- [ ] **Video:** if one-frame-late sampling becomes visible, build the
      macroblock chain in its own buffer and `CALL` it from the VIF1 stream,
      which keeps ordering at one quadword of stream.
