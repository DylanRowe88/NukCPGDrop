#!/usr/bin/env python3
"""
Convert compiled Blazor WASM wwwroot into a C header for ESP-IDF embedding.

Usage: python embed-web.py <wwwroot-dir> [output-header]

Output: C header with gzip-compressed file data and a web_asset_t index.
"""

import os, sys, gzip, mimetypes, struct

HEADER_TEMPLATE = """\
#pragma once
#include <stddef.h>

typedef struct {{ const char *path; const char *mime; size_t raw_len; size_t len; const unsigned char *data; }} web_asset_t;

{data}

static const web_asset_t web_assets[] = {{
{assets}
}};

static const size_t web_assets_count = {count};
"""

def make_c_bytes(data):
    parts = [" "]
    for i, b in enumerate(data):
        if i % 16 == 0 and i > 0:
            parts.append("\n ")
        parts.append(f"0x{b:02x},")
    return "".join(parts)

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "ui/NukCPGDrop.Ui/publish/wwwroot"
    out = sys.argv[2] if len(sys.argv) > 2 else "firmware/main/include/web_assets.h"

    assets = []
    data_sections = []
    seen_raw = set()

    for dirpath, _, filenames in os.walk(root):
        for fn in sorted(filenames):
            if fn.endswith(('.br', '.gz')):
                continue
            abspath = os.path.join(dirpath, fn)
            relpath = os.path.relpath(abspath, root).replace("\\", "/")
            with open(abspath, "rb") as f:
                raw = f.read()
            compressed = gzip.compress(raw, compresslevel=9)
            use_compressed = len(compressed) < len(raw)
            final = compressed if use_compressed else raw
            final_len = len(final)
            raw_len = len(raw)

            varname = f"gz_{relpath.replace('/', '_').replace('.', '_').replace('-', '_')}"
            mime = mimetypes.guess_type(relpath)[0] or "application/octet-stream"
            assets.append((f"/{relpath}", mime, raw_len, final_len, varname))
            data_sections.append(f"static const unsigned char {varname}[] = {{{make_c_bytes(final)}}};")

    assets_data = "\n".join(data_sections)
    assets_list = ",\n".join(
        f'    {{ "{path}", "{mime}", {raw_len}, {final_len}, {varname} }}'
        for path, mime, raw_len, final_len, varname in assets
    )

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write(HEADER_TEMPLATE.format(data=assets_data, assets=assets_list, count=len(assets)))

    print(f"Embedded {len(assets)} assets ({sum(a[3] for a in assets)} bytes) -> {out}")

if __name__ == "__main__":
    main()
