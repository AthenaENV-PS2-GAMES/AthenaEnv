#!/usr/bin/env python3
"""Packages the E04 (tint) physical-PS2 validation into a new ZIP.
Usage: package_ps2_e04_validation.py OUT.zip --elf PATH   (OUT must not exist)"""
import argparse, hashlib, json, sys, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODELS = ['arm.glb', 'bend.glb', 'morph.glb', 'textured_cube.glb', 'checker.png']

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('output', type=Path); ap.add_argument('--elf', type=Path, required=True)
    args = ap.parse_args()
    if not args.elf.is_file(): sys.exit(f'ELF not found: {args.elf}')
    base = 'athena-e04-ps2/'
    files = {base + 'models/' + m: (ROOT / 'bin/models' / m).read_bytes() for m in MODELS}
    files[base + 'athena.elf'] = args.elf.read_bytes()
    files[base + 'athena.ini'] = b'default_script = ps2_e04_validation.js\n'
    files[base + 'ps2_e04_validation.js'] = (ROOT / 'bin/ps2_e04_validation.js').read_bytes()
    files[base + 'ps2_e04_config.quick.json'] = b'{ "quick": true }\n'
    files[base + 'LEIA-ME.md'] = (ROOT / 'docs/E04_PS2_VALIDATION.md').read_bytes()
    manifest = {'schema': 1, 'purpose': 'E04 tint physical PS2 validation; results not yet collected',
        'files': {n: {'bytes': len(d), 'sha256': hashlib.sha256(d).hexdigest()} for n, d in sorted(files.items())}}
    files[base + 'manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    with zipfile.ZipFile(args.output, 'x', compression=zipfile.ZIP_DEFLATED) as z:
        for n, d in sorted(files.items()): z.writestr(n, d)
    print(f'{args.output}: {len(files)} files')

if __name__ == '__main__':
    main()
