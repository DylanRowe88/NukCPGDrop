#!/usr/bin/env python3
"""
NukCPGDrop — one-command development environment setup.

Usage:
    python scripts/setup.py          # interactive
    python scripts/setup.py --check  # just check what's missing
"""

import argparse, os, platform, shutil, subprocess, sys, json
from pathlib import Path

REQUIRED_PYTHON_PACKAGES = ["pyserial", "esptool"]
OPTIONAL_PACKAGES = ["codespell", "playwright"]

def check(description, condition, hint=""):
    mark = "[OK]" if condition else "[MISS]"
    print(f"  {mark} {description}")
    if not condition and hint:
        print(f"       {hint}")
    return condition

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def main():
    parser = argparse.ArgumentParser(description="NukCPGDrop dev setup")
    parser.add_argument("--check", action="store_true", help="Only check, don't install")
    parser.add_argument("--fix", action="store_true", help="Attempt to auto-fix issues")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    os.chdir(repo_root)

    print(f"\n=== NukCPGDrop Dev Setup ===  ({repo_root})\n")
    print("Checking dependencies...\n")

    all_ok = True

    # ── Git ──
    git_ok = check("git installed", shutil.which("git"))
    if git_ok:
        hook_ok = os.path.exists(".git/hooks/pre-commit")
        check("pre-commit hooks installed", hook_ok, "Run: pip install pre-commit && pre-commit install")

    # ── Python ──
    py = shutil.which("python") or shutil.which("python3")
    py_ok = check("Python 3.11+", py is not None, "Install Python 3.11+ from python.org")
    if py_ok:
        v = run([py, "--version"]).stdout.strip()
        check(f"Python version: {v}", "3." in v)

    # ── .NET SDK ──
    dotnet = shutil.which("dotnet")
    dn_ok = check(".NET SDK 8.0+", dotnet is not None, "Install from dotnet.microsoft.com/download")
    if dn_ok:
        dv = run(["dotnet", "--version"]).stdout.strip()
        check(f".NET version: {dv}", dv.startswith("8"))

    # ── ESP-IDF ──
    idf_path = os.environ.get("IDF_PATH", "")
    idf_ok = check("IDF_PATH set", bool(idf_path), "Set IDF_PATH or run export.ps1 / export.sh")
    if idf_ok:
        idf_py = Path(idf_path) / "tools" / "idf.py"
        check("idf.py exists", idf_py.exists(), f"ESP-IDF not found at {idf_path}")
        esp_target = run([sys.executable or "python", str(idf_py), "--version"],
                        cwd=repo_root / "firmware").stdout.strip()
        check("ESP-IDF version", bool(esp_target), esp_target[:80])

    # ── esptool ──
    check("esptool (pip)", shutil.which("esptool.py") or shutil.which("esptool"),
          "Run: pip install esptool")

    # ── gh CLI ──
    gh_ok = check("GitHub CLI (gh)", shutil.which("gh"), "Install from cli.github.com")
    if gh_ok:
        auth = run(["gh", "auth", "status"]).stdout.strip()
        check("gh authenticated", "Logged in" in auth,
              "Run: gh auth login")

    # ── ccache ──
    check("ccache (optional, faster builds)", shutil.which("ccache"))

    print(f"\n=== Summary ===")
    print(f"  Repo: {repo_root}")
    print(f"  Branch: {run(['git','rev-parse','--abbrev-ref','HEAD']).stdout.strip()}")
    print(f"  Last tag: {run(['git','describe','--tags','--always']).stdout.strip()}")

    if not args.check:
        print("\n=== Quick Start ===")
        print(f"  1. Activate ESP-IDF: . esp-idf/export.ps1 (or export.sh)")
        print(f"  2. Install hooks:     pip install pre-commit && pre-commit install")
        print(f"  3. Build:            python flash.py --board DisplayBoard")
        print(f"  4. Flash:            Connect ESP via USB, then:")
        print(f"                       python flash.py --board DisplayBoard")
        print(f"\n  Or use the WebSerial flasher:")
        print(f"    https://dylanrowe88.github.io/NukCPGDrop/flash/")
        print()

    return 0 if all_ok else 1

if __name__ == "__main__":
    sys.exit(main())
