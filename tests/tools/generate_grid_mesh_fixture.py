#!/usr/bin/env python3
"""Writes a static, untextured GLB grid (default 105x105 vertices, 64,896 corners, with normals
and UVs) for the Model3D.Loader benchmark. Usage: generate_grid_mesh_fixture.py OUT.glb [N]"""
import json, struct, sys

n = int(sys.argv[2]) if len(sys.argv) > 2 else 105
pos, nor, uv = bytearray(), bytearray(), bytearray()
for y in range(n):
    for x in range(n):
        h = ((x * 7 + y * 13) % 11) / 40.0
        pos += struct.pack('<3f', x / (n - 1) * 2 - 1, h, y / (n - 1) * 2 - 1)
        nor += struct.pack('<3f', 0.0, 1.0, 0.0)
        uv += struct.pack('<2f', x / (n - 1), y / (n - 1))
idx = bytearray()
for y in range(n - 1):
    for x in range(n - 1):
        a = y * n + x; b = a + 1; c = a + n; d = c + 1
        idx += struct.pack('<6I', a, c, b, b, c, d)
parts = [('p', pos), ('n', nor), ('u', uv), ('i', idx)]
blob, views = bytearray(), []
for name, data in parts:
    views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data)}); blob += data
count = n * n
doc = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0, 'name': 'grid'}],
       'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2}, 'indices': 3, 'mode': 4}]}],
       'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': count, 'type': 'VEC3', 'min': [-1, 0, -1], 'max': [1, .25, 1]},
                     {'bufferView': 1, 'componentType': 5126, 'count': count, 'type': 'VEC3'},
                     {'bufferView': 2, 'componentType': 5126, 'count': count, 'type': 'VEC2'},
                     {'bufferView': 3, 'componentType': 5125, 'count': len(idx) // 4, 'type': 'SCALAR'}],
       'bufferViews': views, 'buffers': [{'byteLength': len(blob)}]}
js = json.dumps(doc, separators=(',', ':')).encode()
js += b' ' * (-len(js) % 4); blob += b'\0' * (-len(blob) % 4)
out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob)) + struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(blob), 0x004E4942) + blob
open(sys.argv[1], 'wb').write(out); print(len(out), 'bytes', count, 'vertices', len(idx) // 4, 'corners')
