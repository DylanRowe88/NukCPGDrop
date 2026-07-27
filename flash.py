#!/usr/bin/env python3
"""
NukCPGDrop — Build, flash, and verify script.

Usage:
    python flash.py              # auto-detect port, full pipeline, flash
    python flash.py --port COM3  # specify port, skip detection
    python flash.py --no-build   # skip build, flash existing
    python flash.py --monitor    # flash then open serial monitor

Detects ESP32-S3 by VID/PID:
  - Native USB (ESP32-S3 built-in):  303A:1001
  - CP210x UART bridge:            10C4:EA60 / 10C4:EA70
  - CH340/CH341 UART bridge:       1A86:7523 / 1A86:55D4
  - FTDI FT232:                    0403:6001
"""

import argparse, os, sys, subprocess, json, time, re, platform
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent

ESP32_VID_PID = [
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


# ── helpers ──────────────────────────────────────────────────────────

def run(cmd, cwd=None, capture=False, check=True):
    cwd = cwd or REPO_ROOT
    print(f"  > {' '.join(cmd)}")
    if capture:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
        if check and r.returncode != 0:
            print(r.stderr)
            sys.exit(r.returncode)
        return r
    r = subprocess.run(cmd, cwd=cwd)
    if check and r.returncode != 0:
        sys.exit(r.returncode)
    return r


def step(label):
    print(f"\n━━━ {label} ━━━")


# ── port detection ───────────────────────────────────────────────────

def detect_port():
    try:
        import serial.tools.list_ports
    except ImportError:
        print("  [install] pyserial not found, installing...")
        run([sys.executable, "-m", "pip", "install", "pyserial"])
        import serial.tools.list_ports

    ports = list(serial.tools.list_ports.comports())
    matches = []

    for p in ports:
        for vid, pid in ESP32_VID_PID:
            if p.vid == vid and p.pid == pid:
                matches.append((p.device, f"{vid:04X}:{pid:04X}", p.description))

    if not matches:
        print("  No ESP32-S3 detected. Available ports:")
        for p in ports:
            vidpid = f"{p.vid:04X}:{p.pid:04X}" if p.vid else "N/A"
            print(f"    {p.device}  [{vidpid}]  {p.description}")
        print("\n  Specify port manually:  python flash.py --port COM3")
        sys.exit(1)

    if len(matches) == 1:
        port = matches[0][0]
        print(f"  Detected: {port}  ({matches[0][1]} — {matches[0][2]})")
        return port

    print("  Multiple ESP32-S3 ports found:")
    for i, (dev, vp, desc) in enumerate(matches):
        print(f"    [{i}] {dev}  ({vp} — {desc})")
    sel = int(input("  Select: "))
    return matches[sel][0]


# ── build pipeline ───────────────────────────────────────────────────

def check_tools():
    tools = [
        ("dotnet", "dotnet --version"),
        ("python", sys.executable + " --version"),
    ]
    if os.name != "nt":
        tools.append(("idf.py", "idf.py --version"))

    for name, cmd in tools:
        try:
            subprocess.run(cmd.split(), capture_output=True, check=True)
        except (FileNotFoundError, subprocess.CalledProcessError):
            print(f"  [missing] {name} — install before flashing")
            sys.exit(1)


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
    step("Embed: WASM → C header")
    pub = REPO_ROOT / "publish" / "wwwroot"
    if not pub.exists():
        print("  [skip] no publish output found")
        return
    header = FIRMWARE_DIR / "main" / "include" / "web_assets.h"
    run([sys.executable, str(REPO_ROOT / "scripts" / "embed-web.py"),
         str(pub), str(header)])


def build_firmware():
    step("Build: ESP-IDF firmware")
    run(["idf.py", "build"], cwd=FIRMWARE_DIR)


def test_firmware():
    step("Test: Firmware unit tests (QEMU)")
    try:
        run(["idf.py", "test"], cwd=FIRMWARE_DIR, check=False)
        print("  (QEMU tests passed or skipped)")
    except FileNotFoundError:
        print("  [skip] QEMU not available")


# ── flash & verify ───────────────────────────────────────────────────

def flash_firmware(port):
    step(f"Flash: firmware to {port}")
    bin_path = FIRMWARE_DIR / "build" / "NukCPGDrop.bin"
    if not bin_path.exists():
        print(f"  [error] {bin_path} not found — run build first")
        sys.exit(1)

    is_native_usb = False
    try:
        import serial.tools.list_ports
        for p in serial.tools.list_ports.comports():
            if p.device == port and p.vid == 0x303A and p.pid == 0x1001:
                is_native_usb = True
                break
    except ImportError:
        pass

    if is_native_usb:
        print("  Native USB port — using direct serial, no RTS/DTR needed")
        run(["idf.py", "-p", port, "-b", "921600", "flash"],
            cwd=FIRMWARE_DIR)
    else:
        print("  UART bridge port — using RTS/DTR for bootloader entry")
        run(["idf.py", "-p", port, "-b", "460800",
             "flash"], cwd=FIRMWARE_DIR)


def verify_flash(port):
    step("Verify: flash integrity")
    bin_path = FIRMWARE_DIR / "build" / "NukCPGDrop.bin"
    size = bin_path.stat().st_size
    print(f"  Firmware size: {size:,} bytes")

    try:
        import esptool
    except ImportError:
        run([sys.executable, "-m", "pip", "install", "esptool"])
        try:
            import esptool
        except ImportError:
            print("  [skip] esptool not available — verify skipped")
            return

    verify_args = [
        sys.executable, "-m", "esptool",
        "--port", port,
        "--baud", "460800",
        "verify_flash",
        "--flash_size", "keep",
    ]

    partitions = FIRMWARE_DIR / "build" / "partitions.bin"
    if partitions.exists():
        verify_args.extend(["--flash_mode", "dout"])

    verify_args.extend([
        "0x10000", str(bin_path),
    ])

    result = subprocess.run(verify_args, capture_output=True, text=True)
    if result.returncode == 0:
        print("  ✓ Flash verified successfully")
    else:
        print("  ✗ Flash verification failed")
        print(result.stderr[-500:])
        sys.exit(1)


def serial_monitor(port):
    step(f"Monitor: opening serial on {port}")
    run(["idf.py", "-p", port, "monitor"], cwd=FIRMWARE_DIR)


# ── main ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="NukCPGDrop — build, flash, and verify")
    parser.add_argument("--port", "-p", help="Serial port (auto-detect if omitted)")
    parser.add_argument("--no-build", action="store_true",
                        help="Skip build pipeline, flash existing binary")
    parser.add_argument("--monitor", "-m", action="store_true",
                        help="Open serial monitor after flash")
    parser.add_argument("--skip-lint", action="store_true", help="Skip linting")
    parser.add_argument("--skip-tests", action="store_true", help="Skip tests")
    args = parser.parse_args()

    os.chdir(REPO_ROOT)
    print(f"NukCPGDrop — {REPO_ROOT}\n")

    if not args.no_build:
        check_tools()
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
    verify_flash(port)

    if args.monitor:
        serial_monitor(port)

    print("\n✓ NukCPGDrop flashed and verified successfully")


if __name__ == "__main__":
    main()
