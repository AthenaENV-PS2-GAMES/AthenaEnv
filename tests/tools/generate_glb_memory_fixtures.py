"""Build deterministic GLB test containers from existing glTF/PNG fixtures."""
import base64
import copy
import json
from pathlib import Path
import struct
import argparse

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'tests/host/3d'

def glb(document, binary):
    text = json.dumps(document, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    binary += b'\0' * (-len(binary) % 4)
    return (struct.pack('<III', 0x46546c67, 2, 28+len(text)+len(binary))
            + struct.pack('<II', len(text), 0x4e4f534a) + text
            + struct.pack('<II', len(binary), 0x004e4942) + binary)

doc = json.loads((OUT/'shared_texture.gltf').read_text())
data = base64.b64decode(doc['buffers'][0].pop('uri').split(',')[1])
external_image = copy.deepcopy(doc)
image = (OUT/'checker.png').read_bytes()
doc['images'] = [{'bufferView': len(doc['bufferViews']), 'mimeType': 'image/png'}]
doc['bufferViews'].append({'buffer': 0, 'byteOffset': len(data), 'byteLength': len(image)})
doc['buffers'][0]['byteLength'] = len(data)+len(image)
embedded = glb(doc, data+image)
(OUT/'memory_shared.glb').write_bytes(embedded)
single = copy.deepcopy(doc)
single['meshes'][0]['primitives'] = single['meshes'][0]['primitives'][:1]
(OUT/'memory_textured.glb').write_bytes(glb(single, data+image))
(OUT/'memory_external_image.glb').write_bytes(glb(external_image, data))
external_buffer = copy.deepcopy(external_image)
external_buffer['buffers'][0]['uri'] = 'missing.bin'
(OUT/'memory_external_buffer.glb').write_bytes(glb(external_buffer, data))
print('Generated embedded/shared and rejected external-dependency GLB fixtures')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--read-output', type=Path, help='Also write arm GLB with 2MiB unused BIN padding for the read benchmark')
args = parser.parse_args()
if args.read_output:
    original = (ROOT/'bin/models/arm.glb').read_bytes()
    length = struct.unpack_from('<I', original, 12)[0]
    document = json.loads(original[20:20+length])
    offset = 20+length
    bin_length = struct.unpack_from('<I', original, offset)[0]
    binary = original[offset+8:offset+8+bin_length] + b'\0'*(2*1024*1024)
    document['buffers'][0]['byteLength'] = len(binary)
    args.read_output.write_bytes(glb(document, binary))
