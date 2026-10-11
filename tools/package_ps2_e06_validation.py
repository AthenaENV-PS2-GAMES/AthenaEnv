#!/usr/bin/env python3
"""Packages the E06 (Audio3D voices and occlusion) physical-PS2 validation into a new ZIP.
Usage: package_ps2_e06_validation.py OUT.zip --elf PATH   (OUT must not exist)"""
import argparse, hashlib, json, sys, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SAMPLES = {'loop.adp': 'tests/host/wav2adp/short.loop.adp', 'pop.adp': 'tests/host/wav2adp/short.adp', 'tone.adp': 'tests/host/wav2adp/rate16k.loop.adp'}

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('output', type=Path); ap.add_argument('--elf', type=Path, required=True)
    args = ap.parse_args()
    if not args.elf.is_file(): sys.exit(f'ELF not found: {args.elf}')
    base = 'athena-e06-ps2/'
    files = {base + name: (ROOT / src).read_bytes() for name, src in SAMPLES.items()}
    files[base + 'athena.elf'] = args.elf.read_bytes()
    files[base + 'athena.ini'] = b'default_script = ps2_e06_validation.js\n'
    files[base + 'ps2_e06_validation.js'] = (ROOT / 'bin/ps2_e06_validation.js').read_bytes()
    files[base + 'ps2_e06_config.quick.json'] = b'{ "quick": true }\n'
    files[base + 'LEIA-ME.md'] = (ROOT / 'docs/E06_PS2_VALIDATION.md').read_bytes()
    manifest = {'schema': 1, 'purpose': 'E06 Audio3D physical PS2 validation; results not yet collected',
        'files': {n: {'bytes': len(d), 'sha256': hashlib.sha256(d).hexdigest()} for n, d in sorted(files.items())}}
    files[base + 'manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    with zipfile.ZipFile(args.output, 'x', compression=zipfile.ZIP_DEFLATED) as z:
        for n, d in sorted(files.items()): z.writestr(n, d)
    print(f'{args.output}: {len(files)} files')

if __name__ == '__main__':
    main()
