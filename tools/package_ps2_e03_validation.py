#!/usr/bin/env python3
"""Packages the E03 physical-PS2 validation (script, ELF, fixtures, instructions) into a new ZIP.

The log files to bring back are written by bin/ps2_e03_validation.js next to the ELF.
Usage: package_ps2_e03_validation.py OUT.zip [--elf PATH]   (OUT must not exist)"""
import argparse, hashlib, json, subprocess, sys, tempfile, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = {  # name in models/ -> source relative to the repo
    'lit_cube.glb': 'bin/models/lit_cube.glb', 'arm.glb': 'bin/models/arm.glb', 'bend_hi.glb': 'bin/models/bend_hi.glb',
    'checker.png': 'tests/host/3d/checker.png', 'ext_bend.gltf': 'tests/host/3d/ext_bend.gltf', 'ext bend.bin': 'tests/host/3d/ext bend.bin',
    'ext_textured.gltf': 'tests/host/3d/ext_textured.gltf', 'ext_textured.bin': 'tests/host/3d/ext_textured.bin',
    'memory_external_image.glb': 'tests/host/3d/memory_external_image.glb',
}
DEFAULT_ELF = ROOT / '.validation/e03-final-build/bin/athena_e03_final7.elf'

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('output', type=Path); ap.add_argument('--elf', type=Path, default=DEFAULT_ELF)
    args = ap.parse_args()
    if not args.elf.is_file(): sys.exit(f'ELF not found: {args.elf}')
    files = {}
    base = 'athena-e03-ps2/'
    with tempfile.TemporaryDirectory() as tmp:
        arm_read, grid = Path(tmp) / 'arm_read.glb', Path(tmp) / 'grid_mesh.glb'
        subprocess.check_call([sys.executable, str(ROOT / 'tests/tools/generate_glb_memory_fixtures.py'), '--read-output', str(arm_read)], stdout=subprocess.DEVNULL)
        subprocess.check_call([sys.executable, str(ROOT / 'tests/tools/generate_grid_mesh_fixture.py'), str(grid)], stdout=subprocess.DEVNULL)
        texture = Path(tmp) / 'texture_256.png'
        subprocess.check_call([sys.executable, str(ROOT / 'tests/tools/generate_texture_fixture.py'), str(texture)], stdout=subprocess.DEVNULL)
        files[base + 'models/texture_256.png'] = texture.read_bytes()
        files[base + 'models/arm_read.glb'] = arm_read.read_bytes(); files[base + 'models/grid_mesh.glb'] = grid.read_bytes()
    for name, src in FIXTURES.items(): files[base + 'models/' + name] = (ROOT / src).read_bytes()
    files[base + 'athena.elf'] = args.elf.read_bytes()
    files[base + 'athena.ini'] = b'default_script = ps2_e03_validation.js\n'
    files[base + 'ps2_e03_validation.js'] = (ROOT / 'bin/ps2_e03_validation.js').read_bytes()
    files[base + 'ps2_e03_config.quick.json'] = b'{ "quick": true }\n'
    files[base + 'LEIA-ME.md'] = (ROOT / 'docs/E03_PS2_VALIDATION.md').read_bytes()
    manifest = {'schema': 1, 'purpose': 'E03 physical PS2 validation; results not yet collected',
        'elf': str(args.elf.relative_to(ROOT)) if args.elf.is_relative_to(ROOT) else args.elf.name,
        'files': {n: {'bytes': len(d), 'sha256': hashlib.sha256(d).hexdigest()} for n, d in sorted(files.items())}}
    files[base + 'manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    with zipfile.ZipFile(args.output, 'x', compression=zipfile.ZIP_DEFLATED) as z:
        for n, d in sorted(files.items()): z.writestr(n, d)
    print(f'{args.output}: {len(files)} files')

if __name__ == '__main__':
    main()
