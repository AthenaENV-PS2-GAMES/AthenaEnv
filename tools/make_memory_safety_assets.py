#!/usr/bin/env python3
"""Deterministic decoder/bitmap-font fixtures; PNG/BMP use stdlib; JPEG requires Pillow."""
import struct
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / 'bin/tests/memory_safety'
OUT.mkdir(parents=True, exist_ok=True)


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def png(name, width, height, depth, color_type, pixel, palette=None, interlace=False):
    passes = [(0, 0, 1, 1)] if not interlace else [
        (0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
        (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
    data = bytearray()
    for sx, sy, dx, dy in passes:
        if sx >= width or sy >= height:
            continue
        for y in range(sy, height, dy):
            values = [pixel(x, y) for x in range(sx, width, dx)]
            if color_type == 3 and depth == 4:
                row = bytes((values[i] << 4) | (values[i+1] if i+1 < len(values) else 0)
                            for i in range(0, len(values), 2))
            elif color_type == 3:
                row = bytes(values)
            else:
                row = bytes(v for p in values for v in p)
            data += b'\0' + row
    encoded = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB',
        width, height, depth, color_type, 0, 0, int(interlace)))
    if palette:
        encoded += chunk(b'PLTE', bytes(v for p in palette for v in p))
    encoded += chunk(b'IDAT', zlib.compress(data)) + chunk(b'IEND', b'')
    (OUT / name).write_bytes(encoded)


colors = [(240, 40, 40), (40, 220, 60), (40, 80, 240), (240, 240, 240)]
def rgb(x, y):
    return colors[(x >= 32) + 2 * (y >= 32)]


png('rgb.png', 64, 64, 8, 2, rgb)
png('rgba_adam7.png', 64, 64, 8, 6,
    lambda x, y: (*rgb(x, y), 0 if ((x // 8 + y // 8) & 1) else 255), interlace=True)
pal4 = [(0, 0, 0)] * 16
pal8 = [(0, 0, 0)] * 256
for i, color in enumerate(colors):
    pal4[8+i] = color
    pal8[8+i] = color
index = lambda x, y: 8 + (x >= 32) + 2 * (y >= 32)
png('palette4.png', 64, 64, 4, 3, index, pal4)
png('palette8_adam7.png', 64, 64, 8, 3, index, pal8, interlace=True)

# 63*3=189 bytes + 3 bytes of row padding. Both orientations must look alike.
def bmp(name, top_down):
    width, height = 63, 64
    stride = (width * 3 + 3) & ~3
    pixels = bytearray()
    for row in range(height):
        y = row if top_down else height-row-1
        pixels += bytes(v for x in range(width) for v in reversed(rgb(x, y)))
        pixels += b'\xcc' * (stride-width*3)
    header = struct.pack('<2sIHHI', b'BM', 54+len(pixels), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, width, -height if top_down else height,
                       1, 24, 0, len(pixels), 0, 0, 0, 0)
    (OUT / name).write_bytes(header+info+pixels)


bmp('padded.bmp', False)
bmp('top_down.bmp', True)
(OUT / 'truncated.bmp').write_bytes((OUT / 'padded.bmp').read_bytes()[:-1])
(OUT / 'truncated.png').write_bytes((OUT / 'rgba_adam7.png').read_bytes()[:-8])
# White "A" glyph in every cell. Font's 16x16 atlas convention is exercised.
letter = ['01110', '10001', '10001', '11111', '10001', '10001', '10001']
def glyph(x, y):
    gx, gy = ((x % 16)-3)//2, ((y % 16)-1)//2
    opaque = 0 <= gx < 5 and 0 <= gy < 7 and letter[gy][gx] == '1'
    return (255, 255, 255, 255 if opaque else 0)


png('bitmap_font.png', 256, 256, 8, 6, glyph)
print(f'Memory safety fixtures written to {OUT}')

# JPEG visual fixtures require Pillow; PNG/BMP above remain stdlib-only.
from PIL import Image
im = Image.open(OUT / 'rgb.png')
im.save(OUT / 'rgb.jpg', quality=100, subsampling=0)
im.convert('L').save(OUT / 'gray.jpg', quality=100)
Image.new('RGB', (1, 1), (240, 40, 40)).save(OUT / 'tiny.jpg', quality=100)
im.convert('CMYK').save(OUT / 'cmyk.jpg', quality=100)
Image.new('RGB', (1025, 1), (240, 40, 40)).save(OUT / 'oversized.jpg')
(OUT / 'truncated.jpg').write_bytes((OUT / 'rgb.jpg').read_bytes()[:-2])
