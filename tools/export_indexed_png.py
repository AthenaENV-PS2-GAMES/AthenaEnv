#!/usr/bin/env python3
"""Strict offline RGB/RGBA PNG to exact AthenaENV T8/T4 PNG; no quantization.

Requires Pillow for independent PNG decode/round-trip checking. Output PNG bytes
are encoded here with fixed scanline filters and fixed-Huffman zlib compression.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import struct
import sys
import tempfile
import zlib

from PIL import Image, __version__ as PILLOW_VERSION

VERSION = "1.0.0"
SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAX_DIMENSION = 1024


class ExportError(ValueError):
    """Rejected input or unsafe destination; no committed output."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def inspect_png(data: bytes) -> tuple[int, int, int, int, list[tuple[bytes, bytes]]]:
    """Validate framing/CRC/order before allowing the bounded Pillow decoder."""
    if not data.startswith(SIGNATURE):
        raise ExportError("input must be a PNG")
    chunks = []
    offset = 8
    idat_ended = False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ExportError("truncated PNG chunk")
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset+4:offset+8]
        end = offset + 12 + length
        if end > len(data) or any(not (65 <= c <= 90 or 97 <= c <= 122) for c in kind):
            raise ExportError("invalid PNG chunk")
        if kind[2] & 32:
            raise ExportError("invalid PNG chunk reserved bit")
        payload = data[offset+8:end-4]
        if zlib.crc32(kind + payload) != struct.unpack_from(">I", data, end-4)[0]:
            raise ExportError("PNG CRC mismatch")
        if not chunks and kind != b"IHDR":
            raise ExportError("IHDR must be first")
        if kind in (b"IHDR", b"PLTE", b"tRNS", b"IEND") and any(k == kind for k, _ in chunks):
            raise ExportError("duplicate PNG structural chunk")
        if kind in (b"acTL", b"fcTL", b"fdAT", b"iCCP", b"sRGB", b"gAMA", b"cHRM", b"eXIf"):
            raise ExportError("animation/color-profile/orientation metadata is outside the strict input contract")
        if kind not in (b"IHDR", b"PLTE", b"tRNS", b"IDAT", b"IEND") and not kind[0] & 32:
            raise ExportError("unsupported critical PNG chunk")
        seen_idat = any(k == b"IDAT" for k, _ in chunks)
        if seen_idat and kind in (b"PLTE", b"tRNS"):
            raise ExportError("palette/transparency must precede IDAT")
        if kind == b"IDAT" and idat_ended:
            raise ExportError("IDAT chunks must be consecutive")
        if seen_idat and kind != b"IDAT":
            idat_ended = True
        chunks.append((kind, payload))
        offset = end
        if kind == b"IEND":
            if payload or offset != len(data):
                raise ExportError("invalid IEND or trailing bytes")
            break
    if not chunks or chunks[-1][0] != b"IEND" or not any(k == b"IDAT" for k, _ in chunks):
        raise ExportError("PNG requires IDAT and IEND")
    if len(chunks[0][1]) != 13:
        raise ExportError("invalid IHDR")
    w, h, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", chunks[0][1])
    if not (1 <= w <= MAX_DIMENSION and 1 <= h <= MAX_DIMENSION):
        raise ExportError("dimensions must be 1..1024, matching the Image PNG loader")
    if color not in (2, 6) or depth != 8:
        raise ExportError("input must be 8-bit RGB or RGBA; indexed/grayscale/16-bit inputs are unsupported")
    if compression or filtering or interlace:
        raise ExportError("only standard noninterlaced PNG input is supported")
    palette = next((p for k, p in chunks if k == b"PLTE"), None)
    if palette is not None and (not palette or len(palette) % 3 or len(palette) > 768):
        raise ExportError("invalid suggested palette")
    transparency = next((p for k, p in chunks if k == b"tRNS"), None)
    if transparency is not None:
        if color != 2 or len(transparency) != 6 or any(x > 255 for x in struct.unpack(">HHH", transparency)):
            raise ExportError("invalid RGB transparency")
    # Bound expansion and reject trailing/incomplete zlib streams explicitly.
    packed = b"".join(p for k, p in chunks if k == b"IDAT")
    expected = h * (1 + w * (3 if color == 2 else 4))
    stream = zlib.decompressobj()
    try:
        raw = stream.decompress(packed, expected + 1)
    except zlib.error as error:
        raise ExportError("invalid PNG compression") from error
    if len(raw) != expected or not stream.eof or stream.unused_data or stream.unconsumed_tail:
        raise ExportError("invalid PNG decompressed size or trailing compressed data")
    stride = expected // h
    if any(raw[y*stride] > 4 for y in range(h)):
        raise ExportError("invalid PNG scanline filter")
    return w, h, depth, color, chunks


def gs_size(width: int, height: int, psm: str) -> int:
    """Ordinary GS allocation, port of Athena's block/small-page rounding.

    Contract is checked against the exact production C functions in tests.
    CLUT and optional page-start alignment are accounted separately.
    """
    if not (1 <= width <= MAX_DIMENSION and 1 <= height <= MAX_DIMENSION):
        raise ExportError("invalid sizing dimensions")
    if psm in ("PSMCT32", "PSMCT24"):
        bw, bh = (width+7)//8, (height+7)//8
    elif psm == "PSMT8":
        bw, bh = (width+15)//16, (height+15)//16
    elif psm == "PSMT4":
        bw, bh = (width+31)//32, (height+15)//16
    else:
        raise ExportError("unsupported sizing format")
    if psm != "PSMT4":
        if bw <= 2 and bh <= 1: aw, ah = 1, 1
        elif bw <= 4 and bh <= 2: aw, ah = 2, 2
        elif bw <= 8 and bh <= 4: aw, ah = 4, 4
        else: aw, ah = 8, 4
    else:
        if bw <= 1 and bh <= 2: aw, ah = 1, 1
        elif bw <= 2 and bh <= 4: aw, ah = 2, 2
        elif bw <= 4 and bh <= 8: aw, ah = 4, 4
        else: aw, ah = 4, 8
    return ((bw+aw-1)//aw*aw) * ((bh+ah-1)//ah*ah) * 256


def costs(width: int, height: int, psm: str) -> dict:
    pixels = width * height
    payload = {"PSMCT32":pixels*4, "PSMCT24":pixels*3, "PSMT8":pixels, "PSMT4":(pixels+1)//2}[psm]
    clut_cpu = 1024 if psm == "PSMT8" else 64 if psm == "PSMT4" else 0
    clut_gs = gs_size(16,16,"PSMCT32") if psm == "PSMT8" else gs_size(8,2,"PSMCT32") if psm == "PSMT4" else 0
    return {"pixel_payload_bytes":payload, "ee_pixel_backing_request_bytes":(payload+15)&~15,
            "ee_clut_request_bytes":clut_cpu, "ee_total_backing_request_bytes":((payload+15)&~15)+clut_cpu,
            "gs_pixel_allocation_bytes":gs_size(width,height,psm), "gs_clut_allocation_bytes":clut_gs,
            "gs_total_allocation_bytes":gs_size(width,height,psm)+clut_gs}


def encode_indexed(width: int, height: int, rgba: bytes, palette: list[bytes], mode: str,
                   explicit_alpha: bool) -> bytes:
    lookup = {c:i for i,c in enumerate(palette)}
    rows = bytearray()
    for y in range(height):
        indices = [lookup[rgba[(y*width+x)*4:(y*width+x+1)*4]] for x in range(width)]
        rows.append(0)  # fixed PNG filter None
        if mode == "t8":
            rows.extend(indices)
        else:
            # Standard PNG packs the left pixel in the HIGH nibble. The loader swaps it.
            rows.extend((indices[x]<<4)|indices[x+1] for x in range(0,width,2))
    compressor = zlib.compressobj(9, zlib.DEFLATED, 15, 8, zlib.Z_FIXED)
    packed = compressor.compress(bytes(rows)) + compressor.flush()
    result = SIGNATURE + chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,8 if mode=="t8" else 4,3,0,0,0))
    result += chunk(b"PLTE", b"".join(c[:3] for c in palette))
    # RGB input without tRNS uses implicit opaque GS alpha 0x80. RGBA/RGB+tRNS
    # decode explicit alpha with >>1, even for 255. Keep that distinction exact.
    if explicit_alpha:
        result += chunk(b"tRNS", bytes(c[3] for c in palette))
    result += chunk(b"IDAT",packed) + chunk(b"IEND",b"")
    return result


def build_export(source: bytes, mode: str = "t8", expected_sha256: str | None = None) -> tuple[bytes, dict]:
    if mode not in ("t8", "t4"):
        raise ExportError("format must be t8 or t4")
    if expected_sha256 is not None and sha256(source) != expected_sha256.lower():
        raise ExportError("source SHA-256 mismatch; nothing exported")
    width,height,_,color,chunks = inspect_png(source)
    if mode == "t4" and width & 1:
        raise ExportError("T4 requires even width in the existing PNG loader")
    try:
        with Image.open(io.BytesIO(source)) as image:
            image.load()
            rgba = image.convert("RGBA").tobytes()
    except (OSError, ValueError) as error:
        raise ExportError("PNG decode failed") from error
    identities = {rgba[i:i+4] for i in range(0,len(rgba),4)}
    limit = 256 if mode == "t8" else 16
    if len(identities) > limit:
        raise ExportError(f"{len(identities)} distinct RGBA identities exceed {mode.upper()} limit {limit}; quantization is forbidden")
    palette = sorted(identities)  # bytewise R,G,B,A; no GS CSM1 permutation
    explicit_alpha = color == 6 or any(k == b"tRNS" for k,_ in chunks)
    original_psm = "PSMCT32" if explicit_alpha else "PSMCT24"
    exported_psm = "PSMT8" if mode == "t8" else "PSMT4"
    original,exported = costs(width,height,original_psm),costs(width,height,exported_psm)
    if exported["gs_total_allocation_bytes"] >= original["gs_total_allocation_bytes"]:
        raise ExportError("combined GS pixels + CLUT do not save VRAM")
    png = encode_indexed(width,height,rgba,palette,mode,explicit_alpha)
    with Image.open(io.BytesIO(png)) as roundtrip:
        roundtrip.load()
        exact = roundtrip.size == (width,height) and roundtrip.convert("RGBA").tobytes() == rgba
    if not exact:
        raise ExportError("internal pixel-by-pixel round-trip check failed")
    savings = {}
    for domain,key in [("ee","ee_total_backing_request_bytes"),("gs","gs_total_allocation_bytes")]:
        delta = original[key]-exported[key]
        savings[domain] = {"absolute_bytes":delta,"percentage":100*delta/original[key]}
    manifest = {"schema_version":1,"exporter":{"name":"AthenaENV strict indexed PNG exporter","version":VERSION},
        "tools":{"python":platform.python_version(),"pillow":PILLOW_VERSION,"zlib_compile":zlib.ZLIB_VERSION,"zlib_runtime":zlib.ZLIB_RUNTIME_VERSION},
        "dimensions":{"width":width,"height":height},"distinct_rgba_identities":len(palette),
        "original":{"sha256":sha256(source),"format":original_psm,"png_color_type":color,"png_bit_depth":8,"file_bytes":len(source),"costs":original},
        "exported":{"sha256":sha256(png),"format":exported_psm,"png_color_type":3,"png_bit_depth":8 if mode=="t8" else 4,"file_bytes":len(png),"costs":exported},
        "round_trip":{"verified":True,"compared_pixels":width*height,"rgba_sha256":sha256(rgba),"dimensions_unchanged":True},
        "savings":savings,"gs_alpha_policy":"explicit source alpha preserved with full tRNS, GS >>1" if explicit_alpha else "opaque RGB: omit tRNS, preserving implicit GS alpha 0x80",
        "encoding":{"palette_order":"lexicographic RGBA bytes","scanline_filter":0,"compression":"zlib level 9, memLevel 8, Z_FIXED","gs_csm1_permutation":False,"gs_nibble_conversion":False},
        "stripped_ancillary_chunks":[k.decode("ascii") for k,_ in chunks if k not in (b"IHDR",b"PLTE",b"tRNS",b"IDAT",b"IEND")],
        "accounting":"Calculated ordinary Image backing requests and GS pixel+CT32 CLUT allocation; excludes allocator headers, handles and page-aligned start gaps. Texture3D canonical backing differs."}
    return png,manifest


def export(source: Path, output: Path, manifest_path: Path | None = None, mode: str = "t8",
           expected_sha256: str | None = None) -> dict:
    if not hasattr(os,"O_DIRECTORY"):
        raise ExportError("publication requires a POSIX filesystem with directory fsync and hard links")
    source = source.resolve(strict=True)
    output = Path(os.path.abspath(output))
    manifest_path = Path(os.path.abspath(manifest_path or output.with_suffix(".manifest.json")))
    if output.suffix.lower() != ".png" or manifest_path.suffix.lower() != ".json":
        raise ExportError("output must be .png and manifest must be .json")
    if output.parent.resolve() == source.parent or manifest_path.parent.resolve() != output.parent.resolve():
        raise ExportError("PNG and manifest must share a separate output directory")
    if output.resolve() == source or manifest_path.resolve() in (source,output.resolve()):
        raise ExportError("source/output/manifest paths must be distinct")
    for p in (output,manifest_path):
        if p.exists() or p.is_symlink():
            raise ExportError(f"destination already exists: {p}")
    original_bytes = source.read_bytes()
    png,report = build_export(original_bytes,mode,expected_sha256)
    report["original"]["path"] = str(source)
    report["exported"]["path"] = str(output)
    report["manifest_path"] = str(manifest_path)
    encoded_report = (json.dumps(report,sort_keys=True,indent=2,ensure_ascii=True)+"\n").encode("utf-8")
    output.parent.mkdir(parents=True,exist_ok=True)
    committed = []
    # Hard links publish already-complete files with no-clobber semantics. The
    # manifest is the last commit marker; normal failures roll back our links.
    with tempfile.TemporaryDirectory(prefix=".indexed-png-",dir=output.parent) as stage:
        staged = [Path(stage)/"image.png",Path(stage)/"manifest.json"]
        try:
            for p,data in zip(staged,(png,encoded_report)):
                with p.open("xb") as f:
                    f.write(data);f.flush();os.fsync(f.fileno())
            if sha256(source.read_bytes()) != report["original"]["sha256"]:
                raise ExportError("source changed during export")
            for temp,destination in zip(staged,(output,manifest_path)):
                os.link(temp,destination)
                committed.append((temp,destination))
            directory = os.open(output.parent,os.O_RDONLY | os.O_DIRECTORY)
            try: os.fsync(directory)
            finally: os.close(directory)
        except BaseException:
            for temp,destination in reversed(committed):
                if destination.exists() and os.path.samestat(temp.stat(),destination.stat()):
                    destination.unlink()
            raise
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--manifest",type=Path)
    parser.add_argument("--format",choices=("t8","t4"),default="t8")
    parser.add_argument("--expect-sha256",help="optional reviewed source hash; mismatch aborts before writing")
    parser.add_argument("--version",action="version",version=VERSION)
    args = parser.parse_args()
    try:
        report = export(args.input,args.output,args.manifest,args.format,args.expect_sha256)
    except (ExportError,OSError) as error:
        print(f"export rejected: {error}",file=sys.stderr)
        return 2
    print(json.dumps({"output":report["exported"]["path"],"manifest":report["manifest_path"],"format":report["exported"]["format"],"gs_saved_bytes":report["savings"]["gs"]["absolute_bytes"]},sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
