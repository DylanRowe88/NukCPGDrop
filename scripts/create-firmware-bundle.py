#!/usr/bin/env python3
"""
Package built firmware binaries into a release bundle zip.

Usage:
    python scripts/create-firmware-bundle.py firmware/build NukCPGDrop-Display-v1.0.0.zip
"""

import argparse, hashlib, json, os, sys, zipfile
from pathlib import Path

BINARY_MANIFEST = [
    ("bootloader.bin",             "bootloader/bootloader.bin",      0x0000),
    ("partition-table.bin",        "partition_table/partition-table.bin", 0x8000),
    ("NukCPGDrop.bin",             "NukCPGDrop.bin",                 0x10000),
    ("ota_data_initial.bin",       "ota_data_initial.bin",           0xD000),
]


def main():
    parser = argparse.ArgumentParser(description="Create firmware release bundle")
    parser.add_argument("build_dir", help="Path to firmware build directory (e.g. firmware/build)")
    parser.add_argument("output", help="Output zip file path")
    parser.add_argument("--board", default="DisplayBoard", help="Board name")
    parser.add_argument("--version", default="", help="Version string")
    args = parser.parse_args()

    build_dir = Path(args.build_dir)
    if not build_dir.exists():
        print(f"Build directory not found: {build_dir}")
        sys.exit(1)

    files = []
    for name, rel_path, offset in BINARY_MANIFEST:
        src = build_dir / rel_path
        if not src.exists():
            print(f"  [WARN] {src} not found — skipping")
            continue
        data = src.read_bytes()
        files.append({
            "name": name,
            "offset": f"0x{offset:X}",
            "size": len(data),
            "md5": hashlib.md5(data).hexdigest(),
        })

    manifest = {
        "board": args.board,
        "version": args.version or "unknown",
        "files": files,
    }

    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("manifest.json", json.dumps(manifest, indent=2))
        for entry in files:
            src = build_dir / BINARY_MANIFEST[[m["name"] for m in BINARY_MANIFEST].index(entry["name"])][1]
            zf.write(src, entry["name"])

    print(f"  Created {args.output} ({os.path.getsize(args.output) / 1024:.0f} KB)")
    print(f"  Board: {args.board}, Version: {manifest['version']}")
    for f in manifest["files"]:
        print(f"    {f['name']:25s}  {f['offset']:8s}  {f['size']:>8,d} B  MD5:{f['md5']}")


if __name__ == "__main__":
    main()
