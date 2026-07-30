#!/usr/bin/env python3
"""
NukCPGDrop — one-command development environment setup.

Usage:
    python scripts/setup.py          # full interactive setup
    python scripts/setup.py --check  # just check what's missing
"""

import argparse, os, platform, shutil, subprocess, sys, json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

def step(msg):
    print(f"\n  [{msg}]")

def ok(msg=""):
    print(f"    [OK] {msg}" if msg else "    [OK]")

def warn(msg):
    print(f"    [WARN] {msg}")

def fail(msg):
    print(f"    [FAIL] {msg}")
    return False

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def pip_install(pkg):
    r = run([sys.executable, "-m", "pip", "install", pkg])
    return r.returncode == 0

def main():
    parser = argparse.ArgumentParser(description="NukCPGDrop dev setup")
    parser.add_argument("--check", action="store_true", help="Only check, don't install")
    args = parser.parse_args()

    os.chdir(REPO_ROOT)
    print(f"\n{'='*60}")
    print(f"  NukCPGDrop Development Setup")
    print(f"  {REPO_ROOT}")
    print(f"{'='*60}\n")

    all_ok = True

    # ── 1. Python ──
    step("Python")
    if not shutil.which("python") and not shutil.which("python3"):
        all_ok = fail("Python not found. Install 3.11+ from python.org")
    else:
        py = shutil.which("python") or shutil.which("python3")
        v = run([py, "--version"]).stdout.strip()
        ok(f"{v}")
        if not args.check:
            for pkg in ["pre-commit", "pyserial", "esptool"]:
                if run([py, "-m", "pip", "show", pkg]).returncode != 0:
                    print(f"    Installing {pkg}...")
                    pip_install(pkg)
            if run([py, "-m", "pip", "show", "codespell"]).returncode != 0:
                print(f"    Installing codespell (optional)...")
                pip_install("codespell")

    # ── 2. .NET SDK ──
    step(".NET SDK")
    dn = shutil.which("dotnet")
    if not dn:
        all_ok = fail("Not found. Install from dotnet.microsoft.com/download")
    else:
        dv = run(["dotnet", "--version"]).stdout.strip()
        ok(f"v{dv}")

    # ── 3. ESP-IDF ──
    step("ESP-IDF")
    idf_path = os.environ.get("IDF_PATH", "")
    if not idf_path:
        # Search common install locations
        candidates = [
            Path(os.environ.get("USERPROFILE", "C:/")) / "source" / "repos" / "esp-idf",
            Path(os.environ.get("USERPROFILE", "C:/")) / "esp" / "esp-idf",
            Path.home() / "esp" / "esp-idf",
            Path("/opt/esp-idf"),
            Path("/usr/local/esp-idf"),
        ]
        for c in candidates:
            if (c / "tools" / "idf.py").exists():
                idf_path = str(c)
                os.environ["IDF_PATH"] = idf_path
                break
    if idf_path:
        idf_py = Path(idf_path) / "tools" / "idf.py"
        if idf_py.exists():
            ok(f"Found at {idf_path}")
            if not args.check:
                # Export environment for this shell
                export = Path(idf_path) / "export.ps1" if platform.system() == "Windows" else Path(idf_path) / "export.sh"
                if export.exists():
                    print(f"    Activate with: . {export}")
        else:
            all_ok = fail(f"IDF_PATH set but idf.py not found at {idf_py}")
    else:
        all_ok = fail("Not found. Install from docs.espressif.com")
        print("       Windows: git clone --recursive https://github.com/espressif/esp-idf.git")
        print("                cd esp-idf && install.ps1 esp32s3")
        print("       Linux:   See https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/")

    # ── 4. esptool ──
    step("esptool")
    if shutil.which("esptool.py") or shutil.which("esptool"):
        ok()
    else:
        if not args.check:
            print("    Installing...")
            pip_install("esptool")
        else:
            warn("Not installed (run setup without --check to install)")

    # ── 5. GitHub CLI ──
    step("GitHub CLI")
    gh = shutil.which("gh")
    if gh:
        auth = run(["gh", "auth", "status"]).stdout.strip()
        if "Logged in" in auth:
            ok("Authenticated")
        else:
            warn("Not authenticated — run: gh auth login")
    else:
        warn("Not installed. Install from cli.github.com (or: winget install GitHub.cli)")

    # ── 6. pre-commit hooks ──
    step("Pre-commit hooks")
    hooks_exist = (REPO_ROOT / ".git" / "hooks" / "pre-commit").exists()
    if hooks_exist:
        ok("Pre-commit hooks installed")
    else:
        if not args.check:
            print("    Installing...")
            r = run([sys.executable, "-m", "pre_commit", "install"])
            if r.returncode == 0:
                run([sys.executable, "-m", "pre_commit", "install", "--hook-type", "pre-push"])
                ok()
            else:
                warn("pre-commit install failed — run manually: pre-commit install")
        else:
            warn("Not installed (run setup without --check)")

    # ── 7. .env file ──
    step("Environment file")
    env_file = REPO_ROOT / ".env"
    env_example = REPO_ROOT / ".env.example"
    if env_file.exists():
        ok(".env exists")
    elif env_example.exists():
        if not args.check:
            import shutil
            shutil.copy2(env_example, env_file)
            ok("Created .env from .env.example — edit if needed")
        else:
            warn("Run setup without --check to create .env")

    # ── Summary ──
    print(f"\n{'='*60}")
    if all_ok:
        print(f"  All dependencies satisfied.")
    else:
        print(f"  Some dependencies need attention (see [FAIL] above).")
    print()
    print(f"  Next steps:")
    print(f"    python flash.py --board DisplayBoard    # build + flash")
    print(f"    python flash.py --identify              # list boards")
    print(f"    dotnet test tests/NukCPGDrop.Ui.Tests/  # run C# tests")
    print(f"    https://dylanrowe88.github.io/NukCPGDrop/flash/  # WebSerial flasher")
    print(f"{'='*60}\n")
    return 0 if all_ok else 1

if __name__ == "__main__":
    sys.exit(main())
