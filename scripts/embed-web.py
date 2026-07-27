#!/usr/bin/env python3
"""
Convert compiled Blazor WASM wwwroot into a C header for ESP-IDF embedding.

Usage: python embed-web.py <wwwroot-dir> [output-header]

Output: C header with gzip-compressed file blobs and a file index.
"""

import os, sys, gzip, mimetypes, struct

HEADER_TEMPLATE = """\
#pragma once
#include <stddef.h>

typedef struct {{ const char *path; const char *mime; const size_t len; const size_t compressed_len; const unsigned char *data; }} web_asset_t;

{externs}

static const web_asset_t web_assets[] = {{
{assets}
}};

static const size_t web_assets_count = {count};
"""

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "ui/NukCPGDrop.Ui/publish/wwwroot"
    out = sys.argv[2] if len(sys.argv) > 2 else "firmware/main/include/web_assets.h"

    assets = []
    for dirpath, _, filenames in os.walk(root):
        for fn in filenames:
            abspath = os.path.join(dirpath, fn)
            relpath = os.path.relpath(abspath, root).replace("\\", "/")
            with open(abspath, "rb") as f:
                raw = f.read()
            compressed = gzip.compress(raw, compresslevel=9)
            varname = f"gz_{relpath.replace('/', '_').replace('.', '_')}"
            mime = mimetypes.guess_type(relpath)[0] or "application/octet-stream"
            assets.append((relpath, mime, len(raw), len(compressed), varname, compressed))

    externs = "\n".join(f'extern const unsigned char {varname}[];' for *_, varname, _ in assets)
    entries = []
    for relpath, mime, raw_len, comp_len, varname, _ in assets:
        entries.append(f'    {{ "{relpath}", "{mime}", {raw_len}, {comp_len}, {varname} }},')

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write(HEADER_TEMPLATE.format(externs=externs, assets="\n".join(entries), count=len(assets)))

    # Write binary data as .incbin files
    for relpath, mime, raw_len, comp_len, varname, data in assets:
        inc_path = os.path.join(os.path.dirname(out), f"{varname}.bin")
        with open(inc_path, "wb") as f:
            f.write(data)

    print(f"Embedded {len(assets)} assets -> {out}")

if __name__ == "__main__":
    main()
