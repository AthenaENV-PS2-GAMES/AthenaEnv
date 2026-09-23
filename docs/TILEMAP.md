# TileMap Runtime

AthenaEnv ships with a tilemap renderer purpose-built for PlayStation 2
hardware. It streams sprite data to a custom VU1 microprogram in batches of
50, so you get thousands of quads per frame with minimal EE involvement.

## Concepts

| Component | Description |
| --- | --- |
| `TileMap.Descriptor` | Immutable render description: textures and materials (texture index, blend equation, sprite range). Create it once per tileset or level. |
| `TileMap.Instance` | A drawable that references a descriptor plus a sprite buffer. Multiple instances can share the same descriptor. |
| `TileMap.SpriteBuffer` | Creates native, DMA-aligned buffers: `create(count)` for zeroed memory or `fromObjects(array)` to convert structured JS. |
| `TileMap.layout` | `stride` and per-field `offsets` (`x`, `y`, `w`, ...) for editing buffers with `DataView`/typed arrays. |

The renderer batches sprites by material order, binds textures lazily, and
reads sprite memory directly by DMA. Keep sprite data contiguous and laid out
as `TileMap.layout` describes.

## Module layout

The module lives in `src/modules/tilemap`:

| Path | Role |
| --- | --- |
| `native/tilemap.c`, `native/tilemap.h` | Renderer: batching, texture binding, VIF/DMA packets. No QuickJS. |
| `vu1/draw_2D_tile_list.vsm` | Prebuilt VU1 program (source: `old/src/vu1/draw_2D_tile_list.vcl`). |
| `quickjs/ath_tilemap.c` | Bindings: validation and object lifetimes. |
| `tilemap.d.ts` | Public API documentation. |
| `../graphics/mpg_manager.*` | VU microprogram cache, shared by every VU renderer. |

The build compiles `.vsm` module sources with `dvp-as`. `vcl` is not in the
toolchain image, so edit the `.vcl` source elsewhere and commit the
regenerated `.vsm`.

## Usage

```js
const descriptor = new TileMap.Descriptor({
    textures: ["tiles.png"],            // paths or Image objects
    materials: [{ textureIndex: 0, endOffset: 23 }],
});
const map = new TileMap.Instance({
    descriptor,
    spriteBuffer: TileMap.SpriteBuffer.fromObjects(sprites),
});

const view = new DataView(map.getSpriteBuffer());
const { stride, offsets } = TileMap.layout;

while (true) {
    view.setFloat32(0 * stride + offsets.x, x, true);   // live edit
    Screen.clear();
    map.render(0, 0);
    Screen.flip();
}
```

Because you edit the buffer the renderer reads, moving tiles allocates
nothing on the C side. Writes made after `render()` and before
`Screen.flip()` may or may not appear in that frame, so update the buffer
before rendering.

## Replacing or streaming buffers

- `replaceSpriteBuffer(buffer)` switches to another buffer. It waits for
  queued draws that still read the previous one, so avoid calling it many
  times per frame.
- `updateSprites(dstOffset, source[, count])` copies a range of sprites from
  another buffer, for example to stream tiles in chunks. The source needs no
  alignment.

Use these for streaming tilemaps or levels loaded at runtime.

## Migrating from the old `TileMap`

| Old API | New API |
| --- | --- |
| `import TileMap from 'TileMap'` (default export) | Global `TileMap` namespace, or `import * as TileMap from 'TileMap'`. |
| `TileMap.init()` | Removed: the VU program is loaded on the first render. |
| `TileMap.begin()` | Removed: every `render()` configures VU1 itself. |
| Material `texture_index`, `blend_mode`, `end_offset` | `textureIndex`, `blendMode`, `endOffset`. |
| Materials as a binary `ArrayBuffer` | Removed; use an array of objects. |
| Omitted `blend_mode` meant equation 0 | Omitted `blendMode` keeps the current `Screen` equation. |
| Omitted `texture_index` was undefined | Defaults to 0 with textures, -1 without. |
| `fromObjects` colors defaulted to 0 (transparent) | Default to 128 (0x80, neutral). |
| `render(x, y[, z])` accepted missing values; `z` had no effect | `render(x, y)`: both required and finite. The VU program never used the origin z. |
| — | `TileMap.getCamera()`, `instance.spriteCount`, `instance.descriptor`, `descriptor.textures`. |

Behavior fixes made during the migration:

- Sprite memory is written back from the CPU cache before VU1 reads it by
  DMA. Previously, recent `DataView` writes could be missed.
- A material whose `endOffset` exceeds the buffer no longer reads past it.
- Buffers must be 16-byte aligned and a whole number of 64-byte records.
  `SpriteBuffer.create()`/`fromObjects()` allocate 64-byte aligned memory; the
  old ones used `js_malloc` with no alignment guarantee.
- Textures are resolved on every render. A freed `Image`, or one still
  loading through `ImageList`, skips its material instead of drawing from
  a dangling surface.
- A texture that fails to bind skips its material; the upload marker is
  emitted once per texture change instead of once per batch.
- `replaceSpriteBuffer()` and instance finalization wait for queued DMA before
  releasing the previous buffer.
- Invalid options throw `TypeError`/`RangeError` instead of being ignored.

## Hardware notes

These affect real consoles; PCSX2 does not reproduce VU/VIF/GIF concurrency
closely enough to show them.

- VU1 memory layout. A batch needs 453 quadwords: GIF tag, 200 input
  quadwords, output GIF tag and 250 output quadwords. The old double-buffer
  OFFSET of 452 made the two buffers share one quadword, so the last output
  vertex of a full batch could overwrite the next batch's GIF tag while the
  VIF was unpacking it. OFFSET is now 453, checked by a `_Static_assert`.
- Blend changes. `ALPHA` is written through PATH2 (VIF DIRECT), which can
  overtake sprites VU1 has not kicked through PATH1 yet. A VIF `FLUSH` now
  precedes every blend change after the first batch, and the equation is
  only written when it actually changes.
- Pipelining. Batches no longer end in `FLUSHA`, which made each batch wait
  for the GS to finish the previous one. The VIF already waits for the
  previous program before `MSCNT`, and the corrected layout keeps each unpack
  clear of the buffer VU1 or PATH1 may still be reading. VU1 transforms one
  batch while the GS draws the previous one. `TEX0`/`TEX1` (with their
  `FLUSHA`) are sent only when the texture changes, not for every batch.
  A batch is now 4 quadwords of packet instead of 5 (untextured) or 9
  (textured).
- Cache coherency. `SyncDCache` walks the 8 KB data cache by index, so
  its cost is constant, and it includes the line holding the end address.
  Buffers aligned to 16 bytes but not 64 are therefore written back in full.
- Per-sprite `zindex` reaches the GS as the raw bits of the float (the VU
  program adds it after `ftoi4`). It orders correctly only for non-negative
  values with a 24- or 32-bit Z buffer; with the default 16-bit Z buffer,
  depth-testing tiles is unreliable. Fixing it needs a VU program change and
  `vcl`.
- Freed textures. `texture_manager_free()` removes a surface from the
  upload queue, so freeing an `Image` after `render()` cannot make the upload
  interrupt read freed pixels. Sprites already queued with it may show stale
  VRAM for that frame.

## Test

`bin/tests/tilemap_test.js` validates the API and then draws an animated grid
with two translucent untextured markers.

`bin/tests/tilemap_stress.js` forces edge cases and then measures throughput
with 16x16 tiles from `tests/texture.png`.

## Measurements

PCSX2, NTSC, `tilemap_stress.js`. "render" is EE time spent in `render()`
calls per frame:

| Sprites | static | materials (switch every 256) | instances (1 per 1120) | animated (JS moves all) |
| --- | --- | --- | --- | --- |
| 1024 | 0.05 ms | 0.05 ms | 0.09 ms | 4.31 ms update, 60 fps |
| 4096 | 0.14 ms | 0.15 ms | 0.22 ms | 17.2 ms update, 30 fps |
| 16384 | 0.50 ms | 0.52 ms | 0.76 ms | 68.8 ms update, 12 fps |
| 65536 | 1.98 ms | 2.02 ms | 2.92 ms | - |

Findings:

- EE cost is about 0.03 ms per 1000 sprites. Material switches are
  nearly free. Each extra `render()` call adds about 0.016 ms.
- An earlier build spent about 0.2 ms per `render()` building a QuickJS
  exception to tell typed arrays from ArrayBuffers; 59 instances took
  14.16 ms. Caching the buffer kind brought that to 2.92 ms.
- Moving sprites from JS costs about 4.2 us per sprite: about 3840 moved
  sprites per frame at 60 fps. Move whole maps with `render(x, y)` or
  `TileMap.setCamera()` instead, and write only the sprites that change.
- Every scenario except `animated` held 60 fps up to the 65536-sprite test
  cap. PCSX2 does not model VU1 or GS timing, so this is not the real
  limit. 65536 16x16 tiles are about 16.8 Mpixels per frame, near the GS
  textured fill rate. Run the test on hardware to find the actual limit.
