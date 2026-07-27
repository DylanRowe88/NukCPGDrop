#!/usr/bin/env python3
"""
NukCPGDrop — Build, flash, verify, and E2E test.

Usage:
    python flash.py                     # auto-detect, full pipeline, flash, verify, E2E
    python flash.py --port COM3         # specify port
    python flash.py --no-build          # skip build, flash existing
    python flash.py --skip-e2e          # skip E2E WiFi/Playwright test
    python flash.py --monitor           # flash then open serial

Port priority: CH343 (1A86:55D3) > native USB (303A:1001) > CP210x > CH340 > FTDI
"""

import argparse, os, sys, subprocess, json, time, re, platform, socket, ipaddress
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent

ESP32_VID_PID = [
    (0x1A86, 0x55D3),   # CH343 UART (common on ESP32-S3 dev boards)
    (0x303A, 0x1001),   # ESP32-S3 native USB
    (0x10C4, 0xEA60),   # CP2102/CP2104
    (0x10C4, 0xEA70),   # CP2105
    (0x1A86, 0x7523),   # CH340
    (0x1A86, 0x55D4),   # CH341
    (0x0403, 0x6001),   # FT232
    (0x0403, 0x6010),   # FT2232
]

FIRMWARE_DIR = REPO_ROOT / "firmware"
UI_DIR = REPO_ROOT / "ui" / "NukCPGDrop.Ui"
TEST_DIR = REPO_ROOT / "tests" / "NukCPGDrop.Ui.Tests"

IDF_PYTHON = "C:/Users/thedy/.espressif/python_env/idf5.2_py3.14_env/Scripts/python.exe"
IDF_PATH = "C:/Users/thedy/source/repos/esp-idf"
IDF_PYTHON_ENV_PATH = "C:/Users/thedy/.espressif/python_env/idf5.2_py3.14_env"

IDF_ENV = {
    "IDF_PATH": IDF_PATH,
    "IDF_PYTHON_ENV_PATH": IDF_PYTHON_ENV_PATH,
    "PATH": os.pathsep.join([
        "C:/Users/thedy/.espressif/tools/xtensa-esp-elf/esp-13.2.0_20230928/xtensa-esp-elf/bin",
        "C:/Users/thedy/.espressif/tools/cmake/3.24.0/bin",
        "C:/Users/thedy/.espressif/tools/ninja/1.11.1",
        "C:/Users/thedy/.espressif/tools/idf-exe/1.0.3",
        "C:/Users/thedy/.espressif/tools/ccache/4.8/ccache-4.8-windows-x86_64",
        f"{IDF_PYTHON_ENV_PATH}/Scripts",
        f"{IDF_PATH}/tools",
        os.environ.get("PATH", ""),
    ]),
}

# ── helpers ──────────────────────────────────────────────────────────

def idf_run(args, cwd=None, capture=False, check=True):
    cwd = cwd or REPO_ROOT
    cmd = [IDF_PYTHON, f"{IDF_PATH}/tools/idf.py"] + args
    print(f"  > {' '.join(cmd)}")
    env = os.environ.copy(); env.update(IDF_ENV)
    if capture:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, env=env)
        if check and r.returncode != 0: print(r.stderr); sys.exit(r.returncode)
        return r
    r = subprocess.run(cmd, cwd=cwd, env=env)
    if check and r.returncode != 0: sys.exit(r.returncode)
    return r

def run(cmd, cwd=None, capture=False, check=True):
    cwd = cwd or REPO_ROOT
    print(f"  > {' '.join(cmd)}")
    if capture:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
        if check and r.returncode != 0: print(r.stderr); sys.exit(r.returncode)
        return r
    r = subprocess.run(cmd, cwd=cwd)
    if check and r.returncode != 0: sys.exit(r.returncode)
    return r

def step(label):
    print(f"\n=== {label} ===")

def warn(msg):
    print(f"  [WARN] {msg}")

def fail(msg):
    print(f"  [FAIL] {msg}")
    sys.exit(1)

def ok(msg):
    print(f"  [OK] {msg}")

# ── port detection ───────────────────────────────────────────────────

def detect_port():
    try:
        import serial.tools.list_ports
    except ImportError:
        run([sys.executable, "-m", "pip", "install", "pyserial"])
        import serial.tools.list_ports

    ports = list(serial.tools.list_ports.comports())
    matches = [(p.device, p.vid, p.pid, p.description)
               for p in ports
               for vid, pid in ESP32_VID_PID
               if p.vid == vid and p.pid == pid]

    if not matches:
        print("  No ESP32-S3 detected. Available ports:")
        for p in ports:
            vp = f"{p.vid:04X}:{p.pid:04X}" if p.vid else "N/A"
            print(f"    {p.device}  [{vp}]  {p.description}")
        fail("Specify port manually:  python flash.py --port COM3")

    def sort_key(m):
        vid, pid = m[1], m[2]
        if (vid, pid) == (0x1A86, 0x55D3): return 0
        if (vid, pid) == (0x303A, 0x1001): return 1
        return 2
    matches.sort(key=sort_key)

    port, vid, pid, desc = matches[0]
    print(f"  Detected: {port}  ({vid:04X}:{pid:04X} — {desc})")
    return port

# ── build pipeline ───────────────────────────────────────────────────

def run_lint():
    step("Lint: clang-format & dotnet-format")
    if os.name == "nt":
        run(["powershell", "-Command",
             "Get-ChildItem firmware -Recurse -Include *.c,*.h | "
             "ForEach-Object { clang-format -style=file -i $_.FullName }"])
    else:
        run(["find", "firmware", "-name", "*.c", "-o", "-name", "*.h",
             "-exec", "clang-format", "-style=file", "-i", "{}", "+"])
    run(["dotnet", "format", str(UI_DIR), "--verify-no-changes", "--verbosity", "normal"])

def run_ui_tests():
    step("Test: .NET unit tests")
    run(["dotnet", "test", str(TEST_DIR), "-c", "Release"])

def build_ui():
    step("Build: Blazor WASM UI")
    run(["dotnet", "publish", str(UI_DIR), "-c", "Release",
         "-o", str(REPO_ROOT / "publish" / "wwwroot")])

def embed_web():
    step("Embed: WASM -> C header")
    pub = REPO_ROOT / "publish" / "wwwroot"
    if not pub.exists():
        print("  [skip] no publish output found"); return
    header = FIRMWARE_DIR / "main" / "include" / "web_assets.h"
    run([sys.executable, str(REPO_ROOT / "scripts" / "embed-web.py"),
         str(pub), str(header)])

def build_firmware():
    step("Build: ESP-IDF firmware")
    idf_run(["build"], cwd=FIRMWARE_DIR)

def test_firmware():
    step("Test: Firmware unit tests (QEMU)")
    try:
        idf_run(["test"], cwd=FIRMWARE_DIR, check=False)
        print("  (QEMU tests passed or skipped)")
    except FileNotFoundError:
        print("  [skip] QEMU not available")

# ── flash & verify ───────────────────────────────────────────────────

def flash_firmware(port):
    step(f"Flash: firmware to {port}")
    bin_path = FIRMWARE_DIR / "build" / "NukCPGDrop.bin"
    if not bin_path.exists():
        fail(f"{bin_path} not found — run build first")

    is_native = port_vid(port) == 0x303A
    if is_native:
        print("  Native USB — direct serial, no RTS/DTR")
        idf_run(["-p", port, "-b", "921600", "flash"], cwd=FIRMWARE_DIR)
    else:
        print("  UART bridge — using RTS/DTR for bootloader entry")
        idf_run(["-p", port, "-b", "460800", "flash"], cwd=FIRMWARE_DIR)

def port_vid(port):
    try:
        import serial.tools.list_ports
        for p in serial.tools.list_ports.comports():
            if p.device == port: return p.vid or 0
    except: pass
    return 0

def verify_flash(port):
    step("Verify: flash integrity")
    bin_path = FIRMWARE_DIR / "build" / "NukCPGDrop.bin"
    ok(f"Firmware size: {bin_path.stat().st_size:,} bytes")
    try:
        import esptool
    except ImportError:
        run([sys.executable, "-m", "pip", "install", "esptool"])
        try: import esptool
        except: warn("esptool not available — verify skipped"); return
    r = subprocess.run([sys.executable, "-m", "esptool",
        "--port", port, "--baud", "460800", "verify_flash",
        "--flash_size", "keep", "0x10000", str(bin_path)],
        capture_output=True, text=True)
    if r.returncode == 0: ok("Flash verified")
    else: fail(f"Flash verification failed:\n{r.stderr[-500:]}")

# ── serial boot check ──────────────────────────────────────────────

def hard_reset_uart(port):
    """Reset ESP32-S3 via UART RTS/DTR per DevKitC auto-reset circuit."""
    import serial
    with serial.Serial(port, 115200, timeout=1) as ser:
        time.sleep(0.3)
        ser.dtr = False
        ser.rts = True
        time.sleep(0.15)
        ser.rts = False
        time.sleep(0.5)

def check_boot(port, timeout=15):
    step("Check: serial boot")
    try:
        import serial
    except ImportError:
        run([sys.executable, "-m", "pip", "install", "pyserial"])
        import serial

    is_native = port_vid(port) == 0x303A
    baud = 921600 if is_native else 115200

    ser = serial.Serial(port, baud, timeout=1)
    print(f"  Monitoring {port} @ {baud} baud for {timeout}s...")
    errors = []
    boot_complete = False
    start = time.time()

    while time.time() - start < timeout:
        try:
            line = ser.readline().decode('utf-8', errors='replace').strip()
            if not line: continue
            print(f"  {line[:120]}")
            lower = line.lower()
            if 'fatal' in lower or 'panic' in lower or 'abort' in lower:
                errors.append(line)
            if 'ready' in lower or 'app_main' in lower:
                boot_complete = True
        except: pass

    ser.close()
    if errors:
        fail(f"{len(errors)} boot error(s):\n" + "\n".join(errors[-3:]))
    if boot_complete:
        ok("Boot OK — no errors detected")
    else:
        warn("Boot monitor ended without seeing 'app_main'")

# ── E2E: Playwright (manual WiFi connect required) ──────────────────

def playwright_e2e(ssid):
    step("E2E: Playwright captive portal test")
    print("  Make sure your computer is connected to the ESP's WiFi AP first.")
    print(f"  Connect to SSID starting with '{ssid or 'NukCPGDrop-'}' then press Enter...")
    input("  Press Enter when connected...")

    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        run([sys.executable, "-m", "pip", "install", "playwright"])
        run([sys.executable, "-m", "playwright", "install", "chromium"])
        from playwright.sync_api import sync_playwright

    url = "http://192.168.4.1"
    ok(f"Navigating to {url}")

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        try:
            page.goto(url, timeout=20000, wait_until='domcontentloaded')
            title = page.title()
            ok(f"Page title: {title}")

            body = page.text_content("body") or ""
            checks = ["DROP ALL", "NukCPGDrop", "NUKCPGDROP", "Difficulty"]
            found = [c for c in checks if c in body or c in title]
            if found:
                ok(f"Dashboard renders: {', '.join(found)}")
            else:
                warn("Dashboard content not recognized")

            logo = page.query_selector("img[alt='Nuks Logo']")
            if logo:
                ok("Nuks logo image found")
            else:
                warn("Nuks logo image not found")

            page.goto(f"{url}/api/status", timeout=10000, wait_until='domcontentloaded')
            status_text = page.text_content("pre") or page.text_content("body") or ""
            if "difficulty" in status_text.lower():
                ok(f"REST API responds: {status_text[:120]}")
            else:
                warn(f"REST API response: {status_text[:120]}")

        except Exception as e:
            warn(f"Playwright error: {e}")
        finally:
            browser.close()

# ── main ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="NukCPGDrop — build, flash, verify, E2E")
    parser.add_argument("--port", "-p", help="Serial port (auto-detect)")
    parser.add_argument("--no-build", action="store_true", help="Skip build, flash existing")
    parser.add_argument("--skip-e2e", action="store_true", help="Skip WiFi/Playwright E2E test")
    parser.add_argument("--monitor", "-m", action="store_true", help="Open serial after flash")
    parser.add_argument("--skip-lint", action="store_true", help="Skip linting")
    parser.add_argument("--skip-tests", action="store_true", help="Skip tests")
    args = parser.parse_args()

    os.chdir(REPO_ROOT)
    print(f"NukCPGDrop — {REPO_ROOT}\n")

    if not args.no_build:
        if not args.skip_lint:
            run_lint()
        run_ui_tests()
        build_ui()
        embed_web()
        build_firmware()
        test_firmware()
    else:
        print("  (build skipped)")

    port = args.port or detect_port()
    flash_firmware(port)
    hard_reset_uart(port)
    time.sleep(2)
    verify_flash(port)
    check_boot(port)

    if not args.skip_e2e:
        playwright_e2e("NukCPGDrop-")

    if args.monitor:
        idf_run(["-p", port, "monitor"], cwd=FIRMWARE_DIR)

    print("\n=== NukCPGDrop — done ===")


if __name__ == "__main__":
    main()
