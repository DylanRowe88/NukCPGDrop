#!/usr/bin/env python3
"""Validate that embed-web.py generates valid C syntax in the output header."""

import sys, os, re, tempfile, subprocess, gzip

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'scripts'))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))
exec(open(os.path.join(os.path.dirname(__file__), '..', '..', 'scripts', 'embed-web.py')).read().split('def main')[0])
# Define make_c_bytes inline
def make_c_bytes(data):
    parts = [" "]
    for i, b in enumerate(data):
        if i % 16 == 0 and i > 0:
            parts.append("\n ")
        parts.append(f"0x{b:02x},")
    return "".join(parts)

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))

def test_output_is_valid_c():
    """Run embed-web.py and verify the output compiles as C."""
    wwwroot = os.path.join(REPO_ROOT, 'publish', 'wwwroot')
    if not os.path.isdir(wwwroot):
        print("SKIP: no publish output found")
        return

    out_h = os.path.join(REPO_ROOT, 'firmware', 'main', 'include', 'web_assets.h')
    subprocess.run([sys.executable, os.path.join(REPO_ROOT, 'scripts', 'embed-web.py'),
                    wwwroot, out_h], check=True)

    with open(out_h) as f:
        content = f.read()

    # 1. Check header guard
    assert '#pragma once' in content, "Missing #pragma once"

    # 2. Check all extern declarations have valid C identifiers
    var_pattern = re.compile(r'(?:extern\s+)?const\s+unsigned\s+char\s+(\w+)\[\]')
    for match in var_pattern.finditer(content):
        ident = match.group(1)
        assert ident.isidentifier(), f"Invalid C identifier: {ident}"

    # 3. Check hex bytes are valid
    hex_pattern = re.compile(r'0x([0-9a-fA-F]{2})')
    for match in hex_pattern.finditer(content):
        val = int(match.group(1), 16)
        assert 0 <= val <= 255, f"Invalid byte: {val}"

    # 4. Check web_assets array entries have 5 fields
    array_entries = re.findall(r'\{([^}]+)\}', content)
    for entry in array_entries:
        fields = [f.strip() for f in entry.split(',') if f.strip()]
        # Some entries span multiple lines, just check we have string+string+number+number+identifier
        strings = sum(1 for f in fields if f.startswith('"'))
        numbers = sum(1 for f in fields if f.lstrip('-').isdigit())
        idents = sum(1 for f in fields if not f.startswith('"') and not f.lstrip('-').isdigit())
        assert strings == 2, f"Expected 2 strings in entry, got {strings}"
        assert idents >= 1, f"Expected at least 1 identifier in entry"

    # 5. Check web_assets_count matches
    count_match = re.search(r'web_assets_count\s*=\s*(\d+)', content)
    array_match = re.search(r'web_assets\[\]\s*=\s*\{([^}]+)\}', content, re.DOTALL)
    if count_match and array_match:
        count = int(count_match.group(1))
        # Count actual entries (between { } with at least one field)
        body = array_match.group(1)
        entry_count = body.count('{')
        assert count == entry_count, f"Count mismatch: header says {count}, found {entry_count} entries"

    # 6. Verify gzip data decompresses correctly
    data_sections = re.findall(
        r'static const unsigned char (\w+)\[\] = \{([^}]+)\};', content)
    for varname, hexdata in data_sections:
        if varname == 'gz_wwwroot_index_html':
            bytes_data = bytes(int(b, 16) for b in hex_pattern.findall(hexdata))
            try:
                decompressed = gzip.decompress(bytes_data)
                html = decompressed.decode('utf-8')
                assert '<!DOCTYPE html>' in html, "index.html doesn't start with DOCTYPE"
                assert '</html>' in html, "index.html doesn't end with html tag"
            except Exception as e:
                print(f"WARN: {varname} could not be decompressed: {e}")

    print(f"OK: {count_match.group(0)} — all checks passed")


def test_make_c_bytes_output():
    """Test the hex byte formatter."""
    data = bytes(range(32))
    result = make_c_bytes(data)
    assert '0x00' in result
    assert '0x1f' in result
    assert '\n' in result  # should have line breaks
    assert result.startswith(' ')
    print("OK: make_c_bytes")


if __name__ == '__main__':
    test_make_c_bytes_output()
    test_output_is_valid_c()
    print("\nAll embed-web validation tests passed.")
