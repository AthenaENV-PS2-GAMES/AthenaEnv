# Strict indexed PNG export

Run with Python 3.10+ and Pillow installed. This offline tool accepts standard noninterlaced 8-bit RGB/RGBA PNG, preserves every RGBA identity and never quantizes or resizes.

```sh
python3 tools/export_indexed_png.py \
  --input bin/COMZ2-PS2/hud.png \
  --output assets/generated/05b2/hud_t8.png \
  --manifest assets/generated/05b2/hud_t8.manifest.json \
  --format t8 \
  --expect-sha256 207fb489cced00536b42fa17e148735df3aa54a0798c35ee8ab81a33219a5326
```

The checked-in derivative already exists; this command therefore rejects the conflict. To reproduce, use a **fresh** directory such as `/tmp/hud-export-review/hud_t8.png`. Existing output or manifest paths are never overwritten. The PNG and JSON must share a directory separate from the input directory. `--manifest` defaults to the output filename with `.manifest.json`; `--version` prints the exporter version. Exit status is 0 on success, 2 on rejected input/publication errors.

T8 requires ≤256 distinct **RGBA** identities, not merely RGB colors. Explicit `--format t4` requires ≤16 identities and even width. Both modes reject any result whose rounded GS pixels plus CT32 CLUT would not be smaller than the original. Dimensions must be 1–1024, matching Image's PNG loader. Indexed/grayscale/16-bit/interlaced input, animation, color-profile/gamma/orientation metadata and malformed streams are outside the strict contract and rejected. RGB `tRNS` transparency is supported.

Palette entries are sorted by RGBA bytes. PNG output uses forced depth 8 or 4, filter None and fixed-Huffman zlib encoding. It contains **standard** PNG palette order and high-nibble-first T4 pixels; the engine handles its CSM1 permutation and nibble swap. RGB without transparency omits `tRNS` to preserve implicit GS opaque alpha `0x80`; explicit-alpha sources write all used palette alpha entries to preserve Image's `>>1` mapping, including explicit 255. Hidden RGB and distinct alpha identities remain exact.

PNG and manifest are fully built and independently round-trip-checked before publication. Staged complete files are fsynced, then published with exclusive hard links; JSON is the last commit marker. Ordinary errors roll back links created by this invocation. Publication requires a POSIX filesystem with hard links and directory fsync. An abrupt process/machine failure between links can leave a **complete orphan PNG**; consumers should require its verified manifest. Two independent filenames cannot be promised atomically visible as a pair. No existing file is replaced, even on a concurrent destination conflict.

The JSON records paths, hashes, dimensions, color count, formats/depth, exact pixel verification, calculated Image EE requests, GS allocations including CLUT, savings, alpha policy and Python/Pillow/zlib versions. Deterministic PNG bytes are tested with the recorded toolchain. Manifests also depend on explicit paths/tool versions; cross-directory manifests intentionally differ in those fields. Costs exclude allocator headers/resource handles and do not describe Texture3D canonical backing.

Validation:

```sh
python3 tests/tools/export_indexed_png_test.py
sh tests/host/run_indexed_png.sh
```

The second command compiles the production decoder with ASan/UBSan and uses the generated HUD fixture. LeakSanitizer may require execution outside tracing. See Phase 5B.2 reports for synthetic alpha/T4 cases, PCSX2 captures and matched benchmarks.

Adoption is explicit: stage the reviewed derivative in a **separate packaged game directory**, retaining its manifest and original hash. Existing HUD references in `main.js` and `native_hud.js` use 8×16 glyph cells, 16 columns, and lock the atlas; preserve those dimensions/crops. This phase does not alter those game scripts, original `hud.png`, global manifests, Makefiles or any engine API.
