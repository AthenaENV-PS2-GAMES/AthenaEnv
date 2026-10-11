#!/usr/bin/env python3
"""Splits GLBs into .gltf + .bin for the external-dependency (resolver) tests.
Usage: generate_external_gltf_fixtures.py   (writes tests/host/3d/ext_*.gltf|bin)"""
import json, struct
from pathlib import Path

root = Path(__file__).resolve().parents[2]
out = root / 'tests/host/3d'

def split(glb_path, name, bin_uri):
    b = glb_path.read_bytes()
    json_len = struct.unpack('<I', b[12:16])[0]
    doc = json.loads(b[20:20 + json_len])
    bin_len = struct.unpack('<I', b[20 + json_len:24 + json_len])[0]
    bin_data = b[28 + json_len:28 + json_len + bin_len]
    doc['buffers'][0]['uri'] = bin_uri
    doc['buffers'][0]['byteLength'] = len(bin_data)
    (out / f'{name}.gltf').write_text(json.dumps(doc, indent=1))
    (out / bin_uri).write_bytes(bin_data)

split(root / 'bin/models/textured_cube.glb', 'ext_textured', 'ext_textured.bin')   # image uri checker.png
split(root / 'bin/models/bend.glb', 'ext_bend', 'ext bend.bin')                      # skin + animation; space in the file name
doc = json.loads((out / 'ext_bend.gltf').read_text())
doc['buffers'][0]['uri'] = 'ext%20bend.bin'                                          # percent-encoded as glTF requires
(out / 'ext_bend.gltf').write_text(json.dumps(doc, indent=1))
print('ok')
