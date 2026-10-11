#!/usr/bin/env python3
"""Writes a deterministic 256x256 RGBA PNG (more than 256 colors: uploaded as CT32, 256 KiB of
pixels) for the texture-upload benchmark. Usage: generate_texture_fixture.py OUT.png [SIZE]"""
import struct, sys, zlib
size = int(sys.argv[2]) if len(sys.argv) > 2 else 256
rows = bytearray()
for y in range(size):
    rows.append(0)
    for x in range(size):
        rows += bytes(((x * 255) // (size - 1), (y * 255) // (size - 1), (x * 7 + y * 13) & 255, 255))
def chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(rows), 9)) + chunk(b'IEND', b'')
open(sys.argv[1], 'wb').write(png); print(len(png), 'bytes')
