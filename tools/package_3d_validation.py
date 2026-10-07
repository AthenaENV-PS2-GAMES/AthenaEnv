#!/usr/bin/env python3
"""Package existing, tested ELFs and assets for offline PS2 validation."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PRESETS = ("3d_indexed", "3d_features", "3d_geometry", "3d_palette",
           "3d_node_identity", "skin_texture_profile")


def package(root, output):
    files = {}
    for preset in PRESETS:
        folder = f"athena-3d/{preset}/"
        for name in ("athena_3d_js.elf", f"{preset}.js"):
            files[folder + name] = (root / "bin" / name).read_bytes()
        files[folder + "athena.ini"] = f"default_script={preset}.js\n".encode()
        for asset in sorted((root / "bin/models").rglob("*")):
            if asset.is_file():
                files[folder + "models/" + asset.relative_to(root / "bin/models").as_posix()] = asset.read_bytes()
    for name in ("athena_3d_header_before_native.elf", "athena_3d_header_after_native.elf"):
        files["athena-3d/benchmark/" + name] = (root / "bin" / name).read_bytes()
    report = "3d-indexed-constants-p11-2026-10-07.json"
    files["athena-3d/reference/" + report] = (root / "docs/benchmarks" / report).read_bytes()
    files["athena-3d/LEIA-ME.md"] = (root / "docs/3D_PS2_VALIDATION.md").read_bytes()
    manifest = {
        "schema": 1,
        "purpose": "PS2 physical validation; results not yet collected",
        "physicalValidation": "pending",
        "provenance": "Existing tested binaries; reference report identifies P11 sources and ELF hashes. This package does not assert a fresh build or reconstruct the P10 baseline.",
        "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                  for name, data in sorted(files.items())},
    }
    files["athena-3d/manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    # Refuse overwrite: retain previously generated evidence bundles.
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="New ZIP path (must not exist)")
    args = parser.parse_args()
    result = package(ROOT, args.output)
    print(f"{args.output}: {len(result['files'])} files; physical validation pending")
