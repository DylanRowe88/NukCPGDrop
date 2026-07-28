"""
HTTP integration tests for the NukCPGDrop firmware.

Usage:
    python tests/http_tests/test_http_server.py              # test against 192.168.4.1
    python tests/http_tests/test_http_server.py --ip 10.0.0.1  # custom IP (e.g. QEMU bridge)
    python tests/http_tests/test_http_server.py --mock         # test against a local mock server
"""

import argparse
import http.server
import io
import json
import os
import subprocess
import sys
import threading
import time
import urllib.request
import urllib.error

BASE = "http://192.168.4.1"

CAPTIVE_PROBES = [
    "/hotspot-detect.html",
    "/generate_204",
    "/connecttest.txt",
    "/ncsi.txt",
    "/fwlink/",
    "/fwlink",
    "/success.txt",
    "/canonical.html",
    "/gen_204",
    "/redirect",
    "/favicon.ico",
]

ASSETS = [
    "/",
    "/index.html",
    "/css/app.css",
    "/_framework/blazor.webassembly.js",
    "/_framework/dotnet.js",
    "/_framework/dotnet.runtime.js",
    "/_framework/dotnet.native.wasm",
    "/_framework/Microsoft.Extensions.Configuration.wasm",
    "/_framework/System.Private.CoreLib.wasm",
    "/images/Nuks_Logo_Final_White.png",
    "/images/nuks-possum-white.png",
    "/js/rangeSlider.js",
]

API_ENDPOINTS = [
    ("GET", "/api/status"),
]


def check_asset(url: str, expected_type: str = None) -> bool:
    """Fetch an asset URL and verify it returns 200 with the expected content type."""
    try:
        resp = urllib.request.urlopen(url, timeout=10)
        body = resp.read()
        ct = resp.headers.get("Content-Type", "")
        ok = resp.status == 200
        if expected_type and expected_type not in ct:
            print(f"  FAIL {url}: expected content-type '{expected_type}', got '{ct}'")
            return False
        if not ok:
            print(f"  FAIL {url}: status {resp.status}")
            return False
        return True
    except urllib.error.HTTPError as e:
        print(f"  FAIL {url}: HTTP {e.code}")
        return False
    except Exception as e:
        print(f"  FAIL {url}: {e}")
        return False


def check_redirect(url: str) -> bool:
    """Fetch a URL and verify it redirects (302) to the portal."""
    try:
        req = urllib.request.Request(url, method="GET")
        # Don't follow redirects
        from http.client import HTTPConnection
        parsed = urllib.parse.urlparse(url)
        conn = HTTPConnection(parsed.hostname, parsed.port or 80, timeout=10)
        conn.request("GET", parsed.path)
        resp = conn.getresponse()
        body = resp.read()
        status = resp.status
        loc = resp.getheader("Location", "")
        ok = status == 302 and BASE in loc
        if not ok:
            print(f"  FAIL {url}: status {status}, Location '{loc}'")
            return False
        conn.close()
        return True
    except Exception as e:
        print(f"  FAIL {url}: {e}")
        return False


def check_api(method: str, path: str) -> bool:
    """Call an API endpoint and verify JSON response."""
    url = BASE + path
    try:
        if method == "GET":
            resp = urllib.request.urlopen(url, timeout=10)
        else:
            data = json.dumps({"difficulty": 0}).encode()
            req = urllib.request.Request(url, data=data, method=method)
            req.add_header("Content-Type", "application/json")
            resp = urllib.request.urlopen(req, timeout=10)
        body = resp.read()
        json.loads(body)  # verify valid JSON
        return True
    except Exception as e:
        print(f"  FAIL {url}: {e}")
        return False


def test_assets():
    print("\n=== Static assets ===")
    for path in ASSETS:
        if check_asset(BASE + path):
            print(f"  OK   {path}")
        else:
            return False
    return True


def test_captive_probes():
    print("\n=== Captive portal probes ===")
    for path in CAPTIVE_PROBES:
        if check_redirect(BASE + path):
            print(f"  OK   {path} -> 302")
        else:
            return False
    return True


def test_api():
    print("\n=== API endpoints ===")
    for method, path in API_ENDPOINTS:
        if check_api(method, path):
            print(f"  OK   {method} {path}")
        else:
            return False
    return True


# ── Mock server for offline testing ──────────────────────────────

MOCK_ASSETS: dict = {}


def build_mock_assets():
    """Extract asset paths from the firmware's embedded header."""
    header_path = os.path.join(
        os.path.dirname(__file__),
        "..", "..", "firmware", "main", "include", "web_assets.h"
    )
    if not os.path.exists(header_path):
        print("WARN: web_assets.h not found, using minimal mock set")
        # Return a minimal set
        MOCK_ASSETS["/index.html"] = b"<html></html>"
        MOCK_ASSETS["/wwwroot/index.html"] = b"<html></html>"
        return
    with open(header_path, encoding="utf-8") as f:
        content = f.read()

    import re
    # Find lines like: { "/wwwroot/foo", "text/plain", 1000, 500, gz_var },
    pattern = re.compile(r'\{\s*"([^"]+)",\s*"([^"]+)",\s*(\d+),\s*(\d+),\s*(\w+)\s*\}')
    for m in pattern.finditer(content):
        path, mime, raw_len_str, len_str, var_name = m.groups()
        raw_len = int(raw_len_str)
        # We can't easily get the binary data, so just store the path
        MOCK_ASSETS[path] = (mime, raw_len)


def run_mock_server():
    """Simple HTTP server for offline testing."""

    class MockHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            path = self.path
            # Handle /
            if path == "/":
                path = "/wwwroot/index.html"

            # Check captive probes
            if path in CAPTIVE_PROBES or path in [f"/{p}" for p in CAPTIVE_PROBES]:
                self.send_response(302)
                self.send_header("Location", "http://192.168.4.1/")
                self.end_headers()
                return

            # Check assets
            if path in MOCK_ASSETS:
                self.send_response(200)
                self.send_header("Content-Type", MOCK_ASSETS[path][0] if isinstance(MOCK_ASSETS[path], tuple) else "text/html")
                self.end_headers()
                # Send a small placeholder body
                self.wfile.write(b"mock")
                return

            # Also check bare paths (without /wwwroot prefix)
            if path.startswith("/") and "/wwwroot" + path in MOCK_ASSETS:
                bare_path = "/wwwroot" + path
                self.send_response(200)
                self.send_header("Content-Type", MOCK_ASSETS[bare_path][0] if isinstance(MOCK_ASSETS[bare_path], tuple) else "text/html")
                self.end_headers()
                self.wfile.write(b"mock")
                return

            self.send_response(404)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(b"Not Found")

        def log_message(self, format, *args):
            pass  # silent

    server = http.server.HTTPServer(("127.0.0.1", 8080), MockHandler)
    print("Mock server listening on http://127.0.0.1:8080")
    server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description="Test NukCPGDrop HTTP server")
    parser.add_argument("--ip", default="192.168.4.1", help="Target IP address")
    parser.add_argument("--mock", action="store_true", help="Start a mock server for testing")
    args = parser.parse_args()

    global BASE
    BASE = f"http://{args.ip}"

    if args.mock:
        build_mock_assets()
        run_mock_server()
        return

    all_ok = True
    all_ok &= test_assets()
    all_ok &= test_captive_probes()
    all_ok &= test_api()

    print()
    if all_ok:
        print("ALL TESTS PASSED")
        sys.exit(0)
    else:
        print("SOME TESTS FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
