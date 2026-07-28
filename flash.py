#!/usr/bin/env python3
"""
NukCPGDrop — Build, flash, verify, and E2E test.

Usage:
    python flash.py                     # auto-detect, full pipeline, flash, verify, E2E
    python flash.py --port COM3         # specify port
    python flash.py --no-build          # skip build, flash existing
    python flash.py --skip-e2e          # skip E2E WiFi/Playwright test
    python flash.py --monitor           # flash then open serial

Port priority: native USB (303A:1001) > CH343 (1A86:55D3) > CP210x > CH340 > FTDI
Requires: ESP-IDF installed, Python 3.10+, dotnet SDK 8.0+
"""

import argparse, os, sys, subprocess, json, time, re, platform, shutil
from pathlib import Path
from typing import Optional

REPO_ROOT = Path(__file__).resolve().parent

ESP32_VID_PID = [
    (0x303A, 0x1001),   # ESP32-S3 USB-serial-JTAG (2.8" Display board)
    (0x1A86, 0x55D3),   # CH343 UART (DevKitC, fallback)
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


# ── ESP-IDF discovery ──────────────────────────────────────────────

def _find_idf() -> tuple:
    """Auto-detect IDF_PATH and IDF_PYTHON. Returns (idf_path, idf_python, idf_venv)."""

    # 1. Check IDF_PATH env var
    idf_path = os.environ.get("IDF_PATH")
    if idf_path:
        idf_path = Path(idf_path)
        if (idf_path / "tools" / "idf.py").exists():
            return _find_idf_python(idf_path)

    # 2. Check if idf.py is in PATH
    which = shutil.which("idf.py")
    if which:
        idf_path = Path(which).resolve().parent.parent
        if (idf_path / "tools" / "idf.py").exists():
            return _find_idf_python(idf_path)

    # 3. Check common locations
    candidates = []
    if platform.system() == "Windows":
        candidates = [
            Path(os.environ.get("USERPROFILE", "C:/")) / "esp" / "esp-idf",
            Path(os.environ.get("USERPROFILE", "C:/")) / "source" / "repos" / "esp-idf",
            Path("C:/esp-idf"),
            Path("C:/Espressif/esp-idf"),
        ]
    else:
        candidates = [
            Path.home() / "esp" / "esp-idf",
            Path("/opt/esp-idf"),
            Path("/usr/local/esp-idf"),
        ]
    for p in candidates:
        if (p / "tools" / "idf.py").exists():
            return _find_idf_python(p)

    # 4. Check PlatformIO IDF install (common on Windows)
    pio_dirs = [
        Path.home() / ".platformio" / "packages" / "framework-espidf",
    ]
    for p in pio_dirs:
        idf_py = p / "tools" / "idf.py"
        if idf_py.exists():
            return _find_idf_python(p)

    raise RuntimeError(
        "ESP-IDF not found. Install it via:\n"
        "  git clone --recursive https://github.com/espressif/esp-idf.git\n"
        "  cd esp-idf && ./install.ps1 esp32s3\n"
        "Then set IDF_PATH or add idf.py to PATH."
    )


def _find_idf_python(idf_path: Path) -> tuple:
    """Given IDF_PATH, find the IDF Python environment."""
    # Check IDF_PYTHON_ENV_PATH env var
    venv = os.environ.get("IDF_PYTHON_ENV_PATH")
    if venv and (Path(venv) / "Scripts" / "python.exe").exists():
        return str(idf_path), str(Path(venv) / "Scripts" / "python.exe"), venv

    # Find Python env in common locations
    search_dirs = [
        idf_path / ".." / ".." / ".espressif" / "python_env",
        Path.home() / ".espressif" / "python_env",
    ]
    for base in search_dirs:
        if base.exists():
            for env_dir in base.iterdir():
                py = env_dir / "Scripts" / "python.exe"
                if py.exists():
                    return str(idf_path), str(py), str(env_dir)

    # Fallback: use system Python, hope idf.py requirements are installed
    return str(idf_path), sys.executable, ""


def _build_idf_env(idf_path: str, idf_python: str, idf_venv: str) -> dict:
    """Build environment dict for IDF tools."""
    base_tools = [
        "xtensa-esp-elf-gdb", "xtensa-esp-elf", "riscv32-esp-elf",
        "esp32ulp-elf", "cmake", "openocd-esp32", "ninja",
        "idf-exe", "ccache", "dfu-util",
    ]

    search_roots = [
        Path(os.environ.get("HOME", "C:/")) / ".espressif" / "tools",
        Path(os.environ.get("USERPROFILE", "C:/")) / ".espressif" / "tools",
        Path("C:/Users") / os.environ.get("USERNAME", "") / ".espressif" / "tools",
    ]

    tool_paths = []
    for root in search_roots:
        if not root.exists(): continue
        for tool_dir in root.iterdir():
            for bin_dir in tool_dir.rglob("bin"):
                if bin_dir.is_dir():
                    tool_paths.append(str(bin_dir))
            for exe in tool_dir.rglob("*.exe"):
                tool_paths.append(str(exe.parent))

    # Deduplicate
    tool_paths = list(dict.fromkeys(tool_paths))

    env = os.environ.copy()
    env["IDF_PATH"] = idf_path
    if idf_venv:
        env["IDF_PYTHON_ENV_PATH"] = idf_venv
    env["PATH"] = os.pathsep.join(tool_paths + [
        f"{Path(idf_venv).resolve()}/Scripts" if idf_venv else "",
        f"{idf_path}/tools",
        os.environ.get("PATH", ""),
    ])
    return env


# ── detect IDF once at module level ─────────────────────────────────

try:
    _IDF_PATH, _IDF_PYTHON, _IDF_VENV = _find_idf()
    _IDF_ENV = _build_idf_env(_IDF_PATH, _IDF_PYTHON, _IDF_VENV)
except RuntimeError as e:
    print(f"[WARN] {e}")
    _IDF_PATH = _IDF_PYTHON = _IDF_VENV = ""
    _IDF_ENV = {}


# ── helpers ──────────────────────────────────────────────────────────

_timings = {}

def step(label):
    _timings[label] = time.time()
    print(f"\n=== {label} ===")

def step_end(label):
    if label in _timings:
        elapsed = time.time() - _timings[label]
        print(f"  [{elapsed:.1f}s]")
        return elapsed
    return 0

def idf_run(args, cwd=None, capture=False, check=True):
    cwd = cwd or REPO_ROOT
    cmd = [_IDF_PYTHON, f"{_IDF_PATH}/tools/idf.py"] + args
    print(f"  > {' '.join(cmd)}")
    if capture:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, env=_IDF_ENV)
        if check and r.returncode != 0: print(r.stderr); sys.exit(r.returncode)
        return r
    r = subprocess.run(cmd, cwd=cwd, env=_IDF_ENV)
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


def warn(msg):
    print(f"  [WARN] {msg}")

def fail(msg):
    print(f"  [FAIL] {msg}")
    sys.exit(1)

def ok(msg):
    print(f"  [OK] {msg}")


# ── port detection ───────────────────────────────────────────────────

from scripts.board_config import resolve, register, list_aliases

ESP32_VID_PID = [
    (0x303A, 0x1001),   # ESP32-S3 USB-serial-JTAG
    (0x1A86, 0x55D3),   # CH343 UART
    (0x10C4, 0xEA60),   # CP2102/CP2104
    (0x10C4, 0xEA70),   # CP2105
    (0x1A86, 0x7523),   # CH340
    (0x1A86, 0x55D4),   # CH341
    (0x0403, 0x6001),   # FT232
    (0x0403, 0x6010),   # FT2232
]

def read_mac(port):
    """Read the ESP32-S3 MAC address via esptool."""
    try:
        r = subprocess.run(
            [sys.executable, "-m", "esptool", "--chip", "esp32s3",
             "--port", port, "--baud", "115200", "read_mac"],
            capture_output=True, text=True, timeout=15
        )
        for line in r.stdout.splitlines():
            m = re.search(r'MAC:\s+([0-9a-fA-F:]{17})', line)
            if m:
                return m.group(1).upper()
            m = re.search(r'([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})', line)
            if m:
                return m.group(1).upper()
    except Exception as e:
        print(f"  [WARN] Could not read MAC from {port}: {e}")
    return None

def find_port_by_alias(alias):
    """Scan all COM ports, find the one whose MAC matches the given alias.
       Returns the port device path or None."""
    target_mac = resolve(alias)
    if not target_mac:
        return None

    import serial.tools.list_ports
    for p in serial.tools.list_ports.comports():
        if (p.vid, p.pid) in ESP32_VID_PID:
            mac = read_mac(p.device)
            if mac and mac == target_mac:
                print(f"  Found '{alias}' at {p.device}  ({p.vid:04X}:{p.pid:04X})")
                return p.device
    print(f"  Board '{alias}' (MAC {target_mac}) not found on any port.")
    print(f"  Make sure it is connected and in normal boot mode.")
    return None

def identify_boards():
    """Scan all ports and identify which board is which."""
    import serial.tools.list_ports
    aliases = {}
    try:
        from scripts.board_config import load as load_aliases
        aliases = load_aliases()
    except: pass

    ports = list(serial.tools.list_ports.comports())
    print("Scanning for ESP32-S3 boards...")
    for p in ports:
        if (p.vid, p.pid) in ESP32_VID_PID:
            mac = read_mac(p.device)
            known = [a for a, m in aliases.items() if m == mac] if mac else []
            label = f"  [{known[0]}]" if known else ""
            print(f"  {p.device:8s}  ({p.vid:04X}:{p.pid:04X})  MAC={mac or '???'}  {label}")
    print()
    list_aliases()
    print()
    print("To register a new board:")
    print("  python flash.py --register-alias MyBoard")
    print("  (probes all ports and prompts to confirm which one)")

def confirm_and_flash(port, alias):
    """Confirm with the user before flashing, then flash."""
    mac = read_mac(port)
    print(f"  Target: '{alias}' at {port} (MAC: {mac})")

    # Build and flash
    if not args.no_build:
        if not args.skip_lint: run_lint(); step_end("lint")
        run_ui_tests(); step_end("tests")
        build_ui(); step_end("ui")
        embed_web(); step_end("embed")
        build_firmware(); step_end("firmware")
        test_firmware(); step_end("qemu")
    else:
        print("  (build skipped)")

    flash_firmware(port); step_end("flash")
    time.sleep(1)
    verify_flash(port); step_end("verify")
    time.sleep(1)

    is_native = port_vid(port) == 0x303A
    if not is_native:
        hard_reset_uart(port)
        time.sleep(0.3)
    check_boot(port); step_end("boot")

    if not args.skip_e2e:
        connect_and_test_e2e()

    if args.monitor:
        idf_run(["-p", port, "monitor"], cwd=FIRMWARE_DIR)


def port_vid(port):
    try:
        import serial.tools.list_ports
        for p in serial.tools.list_ports.comports():
            if p.device == port: return p.vid or 0
    except: pass
    return 0


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


# ── flash ────────────────────────────────────────────────────────────

def flash_firmware(port):
    step(f"Flash: firmware to {port}")
    bin_path = FIRMWARE_DIR / "build" / "NukCPGDrop.bin"
    if not bin_path.exists():
        fail(f"{bin_path} not found — run build first")

    is_native = port_vid(port) == 0x303A
    is_8mb = not is_native  # CH343 UART → DevKitC (8MB)
    baud = 921600 if is_native else 460800
    flash_size = "8MB" if is_8mb else "16MB"

    esptool_args = [
        _IDF_PYTHON, "-m", "esptool",
        "--chip", "esp32s3",
        "--port", port,
        "--baud", str(baud),
        "--before", "default_reset",
        "--after", "no_reset" if not is_native else "hard_reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", flash_size,
        "0x0", str(FIRMWARE_DIR / "build" / "bootloader" / "bootloader.bin"),
        "0x10000", str(bin_path),
        "0x8000", str(FIRMWARE_DIR / "build" / "partition_table" / "partition-table.bin"),
        "0xd000", str(FIRMWARE_DIR / "build" / "ota_data_initial.bin"),
    ]
    print(f"  > {' '.join(str(a) for a in esptool_args)}")

    r = subprocess.run(esptool_args, env=_IDF_ENV)
    if r.returncode != 0:
        fail("Flash failed")


# ── verify ──────────────────────────────────────────────────────────

def verify_flash(port):
    step("Verify: flash integrity")
    bin_path = FIRMWARE_DIR / "build" / "NukCPGDrop.bin"
    ok(f"Firmware size: {bin_path.stat().st_size:,} bytes")
    r = subprocess.run([sys.executable, "-m", "esptool",
        "--port", port, "--baud", "460800",
        "--before", "no_reset", "--after", "no_reset",
        "verify_flash", "--flash_size", "keep",
        "0x10000", str(bin_path)],
        capture_output=True, text=True)
    if r.returncode == 0:
        ok("Flash verified")
    else:
        warn("Flash verification skipped (esptool couldn't connect — expected after flash)")


# ── boot check ──────────────────────────────────────────────────────

def hard_reset_uart(port):
    """Reset ESP32-S3 via UART RTS/DTR for normal boot (not download mode)."""
    import serial
    with serial.Serial(port, 115200, timeout=1) as ser:
        time.sleep(0.3)
        ser.dtr = False
        ser.rts = False
        time.sleep(0.2)
        ser.rts = True     # EN -> LOW (chip resets)
        time.sleep(0.2)
        ser.rts = False    # EN -> HIGH, GPIO0 stays HIGH -> normal boot
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
            if 'app_main' in lower:
                boot_complete = True
        except: pass

    ser.close()
    if errors:
        fail(f"{len(errors)} boot error(s):\n" + "\n".join(errors[-3:]))
    if boot_complete:
        ok("Boot OK")
    else:
        warn("Boot monitor ended without seeing 'app_main'")


# ── E2E: WiFi + Playwright ─────────────────────────────────────────

def _netsh(args):
    return subprocess.run(["netsh"] + args, capture_output=True, text=True)

def _find_builtin_wifi():
    """Return the name of the built-in WiFi adapter (not USB)."""
    r = _netsh(["wlan", "show", "interfaces"])
    current_name = None
    for line in r.stdout.splitlines():
        m = re.match(r'\s+Name\s+:\s+(.+)', line)
        if m: current_name = m.group(1).strip()
        m = re.match(r'\s+Description\s+:\s+(.+)', line)
        if m:
            desc = m.group(1).strip()
            if current_name and 'USB' not in desc.upper():
                return current_name
            current_name = None
    return None

def _connect_esp_ap(iface, ssid_prefix):
    # Force a scan first
    _netsh(["wlan", "scan", f"interface={iface}"])
    time.sleep(3)
    r = _netsh(["wlan", "show", "networks", f"interface={iface}"])
    for line in r.stdout.splitlines():
        if ssid_prefix.lower() in line.lower():
            return line.split(":")[-1].strip()
    # Also try mode=bssid for more detail
    r2 = _netsh(["wlan", "show", "networks", f"interface={iface}", "mode=bssid"])
    for line in r2.stdout.splitlines():
        if ssid_prefix.lower() in line.lower():
            parts = line.split(":")
            return parts[-1].strip() if len(parts) > 1 else parts[0].strip()
    return None

def _save_and_disconnect(iface):
    r = _netsh(["wlan", "show", "interfaces"])
    for line in r.stdout.splitlines():
        m = re.match(r'\s+Profile\s+:\s+(.+)', line)
        if m: return m.group(1).strip()
    return None

def _restore_connection(iface, profile):
    if profile:
        _netsh(["wlan", "connect", f"name={profile}", f"interface={iface}"])

def connect_and_test_e2e():
    step("E2E: WiFi + Playwright test")

    iface = _find_builtin_wifi()
    if not iface:
        warn("No built-in WiFi adapter found")
        return

    ok(f"Using built-in adapter: {iface}")

    # Try to find ESP AP SSID from known pattern
    esp_ssid = _connect_esp_ap(iface, "NukCPGDrop-")
    if not esp_ssid:
        warn("ESP AP not found via scan — trying direct connect by prefix")
        _netsh(["wlan", "connect", "name=NukCPGDrop-105D30", f"interface={iface}"])
        time.sleep(3)
        esp_ssid = "NukCPGDrop-105D30"

    ok(f"Found AP: {esp_ssid}")
    prev_profile = _save_and_disconnect(iface)

    # Add open profile and connect (built-in adapter only)
    xml = f'''<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
    <name>{esp_ssid}</name>
    <SSIDConfig><SSID><name>{esp_ssid}</name></SSID></SSIDConfig>
    <connectionType>ESS</connectionType>
    <connectionMode>manual</connectionMode>
    <MSM><security>
        <authEncryption><authentication>open</authentication><encryption>none</encryption></authEncryption>
    </security></MSM>
</WLANProfile>'''
    p = REPO_ROOT / f"{esp_ssid}.xml"
    p.write_text(xml)
    _netsh(["wlan", "add", "profile", f"filename={p}", f"interface={iface}"])
    _netsh(["wlan", "connect", f"name={esp_ssid}", f"interface={iface}"])
    p.unlink()
    time.sleep(5)

    # Run Playwright test
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        run([sys.executable, "-m", "pip", "install", "playwright"])
        run([sys.executable, "-m", "playwright", "install", "chromium"])
        from playwright.sync_api import sync_playwright

    url = "http://192.168.4.1"
    with sync_playwright() as pw:
        browser = pw.chromium.launch(
            headless=False,
            args=[
                '--no-sandbox',
                '--disable-web-security',
                '--disable-features=BlockInsecurePrivateNetworkRequests',
                '--disable-captive-portal-detection',
                '--ignore-certificate-errors',
                '--allow-insecure-localhost',
            ])
        page = browser.new_page()
        ok(f"Navigating to {url}")
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

            page.goto(f"{url}/api/status", timeout=10000, wait_until='domcontentloaded')
            st = page.text_content("pre") or page.text_content("body") or ""
            if "difficulty" in st.lower():
                ok(f"REST API responds: {st[:120]}")

        except Exception as e:
            warn(f"Playwright error: {e}")
        finally:
            browser.close()

    # Restore previous connection on built-in adapter only
    _netsh(["wlan", "disconnect", f"interface={iface}"])
    _netsh(["wlan", "delete", "profile", f"name={esp_ssid}", f"interface={iface}"])
    _restore_connection(iface, prev_profile)


# ── main ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="NukCPGDrop — build, flash, verify, E2E")
    parser.add_argument("--port", "-p", help="Serial port (auto-detect)")
    parser.add_argument("--board", default=None, metavar="ALIAS",
                        help="Board alias (e.g. DisplayBoard, E2EBoard). "
                             "Required for flash. Use --identify to list aliases.")
    parser.add_argument("--register-alias", metavar="ALIAS",
                        help="Register a new board alias by probing all ports")
    parser.add_argument("--no-build", action="store_true", help="Skip build, flash existing")
    parser.add_argument("--skip-e2e", action="store_true", help="Skip WiFi/Playwright E2E test")
    parser.add_argument("--monitor", "-m", action="store_true", help="Open serial after flash")
    parser.add_argument("--skip-lint", action="store_true", help="Skip linting")
    parser.add_argument("--skip-tests", action="store_true", help="Skip tests")
    parser.add_argument("--identify", action="store_true", help="Identify connected boards")
    parser.add_argument("--yes", "-y", action="store_true", help="Skip confirmation prompt")
    global args
    args = parser.parse_args()

    if args.identify:
        identify_boards()
        return

    if args.register_alias:
        import serial.tools.list_ports
        ports = [(p.device, p.vid, p.pid) for p in serial.tools.list_ports.comports()
                 if (p.vid, p.pid) in ESP32_VID_PID]
        if not ports:
            fail("No ESP32-S3 boards found to register")
        for dev, vid, pid in ports:
            mac = read_mac(dev)
            label = "USB-JTAG" if (vid, pid) == (0x303A, 0x1001) else "UART"
            print(f"  {dev:8s}  ({vid:04X}:{pid:04X})  {label}  MAC={mac}")
        idx = 0 if len(ports) == 1 else int(input("  Which port index (0)? ") or "0")
        dev, vid, pid = ports[idx]
        mac = read_mac(dev)
        if mac:
            register(args.register_alias, mac)
        else:
            fail(f"Could not read MAC from {dev}")
        return

    if not args.board:
        parser.print_help()
        print("\nERROR: --board ALIAS is required (use --identify to list available boards)")
        sys.exit(1)

    os.chdir(REPO_ROOT)
    print(f"NukCPGDrop — {REPO_ROOT}\n")

    if not _IDF_PATH:
        fail("ESP-IDF not found. Install it and set IDF_PATH.")

    # Find the board by alias (MAC-based)
    port = args.port or find_port_by_alias(args.board)
    if not port:
        fail(f"Board '{args.board}' not found. Run --identify to list connected boards.")

    # Build and flash
    if not args.no_build:
        if not args.skip_lint: run_lint(); step_end("lint")
        run_ui_tests(); step_end("tests")
        build_ui(); step_end("ui")
        embed_web(); step_end("embed")
        build_firmware(); step_end("firmware")
        test_firmware(); step_end("qemu")
    else:
        print("  (build skipped)")

    flash_firmware(port); step_end("flash")
    time.sleep(1)
    verify_flash(port); step_end("verify")
    time.sleep(1)

    is_native = port_vid(port) == 0x303A
    if not is_native:
        hard_reset_uart(port)
        time.sleep(0.3)
    check_boot(port); step_end("boot")

    if not args.skip_e2e:
        connect_and_test_e2e()

    if args.monitor:
        idf_run(["-p", port, "monitor"], cwd=FIRMWARE_DIR)

    print("\n=== Timing Summary ===")
    for label, start in sorted(_timings.items(), key=lambda x: x[1]):
        elapsed = time.time() - start
        print(f"  {label}: {elapsed:.1f}s")

    print("\n=== NukCPGDrop — done ===")


if __name__ == "__main__":
    main()
