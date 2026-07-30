# NukCPGDrop

[![CI](https://github.com/DylanRowe88/NukCPGDrop/actions/workflows/ci.yml/badge.svg)](https://github.com/DylanRowe88/NukCPGDrop/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/DylanRowe88/NukCPGDrop)](https://github.com/DylanRowe88/NukCPGDrop/releases)
[![WebSerial Flasher](https://img.shields.io/badge/flash-browser-blue)](https://dylanrowe88.github.io/NukCPGDrop/flash/)
[![Architecture](https://img.shields.io/badge/docs-architecture-purple)](ARCHITECTURE.md)

Timed drop rig for HomeGymCon — 16 servo-controlled N52 magnets dropping Nuks cans
in random sequence. ESP32-S3 with captive portal web UI + 2.8" touch display.

**[→ WebSerial Flasher](https://dylanrowe88.github.io/NukCPGDrop/flash/)** &nbsp;|&nbsp; **[→ Architecture Docs](ARCHITECTURE.md)**

---

## Quick Start — New Developer

```bash
# 1. Full automated setup (installs hooks, pip deps, .env)
python scripts/setup.py

# 2. Build + flash the DisplayBoard
python flash.py --board DisplayBoard
```

Or flash from the browser: [dylanrowe88.github.io/NukCPGDrop/flash/](https://dylanrowe88.github.io/NukCPGDrop/flash/)

## Dependencies

| Tool | Version | Install |
|------|---------|---------|
| Python | 3.11+ | [python.org](https://python.org) |
| ESP-IDF | v5.2 | `git clone --recursive https://github.com/espressif/esp-idf.git` |
| .NET SDK | 8.0 | [dotnet.microsoft.com](https://dotnet.microsoft.com/download) |
| esptool | latest | `pip install esptool` |
| gh CLI | latest | `winget install GitHub.cli` or [cli.github.com](https://cli.github.com) |

### Environment

```bash
# Activate ESP-IDF before building (every new terminal)
. C:/path/to/esp-idf/export.ps1          # Windows
source ~/esp/esp-idf/export.sh           # Linux/macOS
```

## Development Workflow

```bash
python flash.py --board DisplayBoard     # full pipeline + flash
python flash.py --identify               # list connected boards
dotnet test tests/NukCPGDrop.Ui.Tests/   # run C# tests
cd firmware && idf.py test               # run firmware tests
```

## Pre-commit & Pre-push Hooks

After `python scripts/setup.py`, every `git commit` automatically runs:

| Stage | What runs |
|-------|-----------|
| Pre-commit | Full pipeline (Blazor → embed → firmware) → C# tests → firmware tests → codespell → clang-format → lint |
| Pre-push   | Firmware bundle → GitHub release upload → E2E tests |

Skip with `--no-verify` if needed.

## Boards

| Board | Alias | Flash | PSRAM | I2C | LED |
|-------|-------|-------|-------|-----|-----|
| LCDWiki 2.8" Display (N16R8) | `DisplayBoard` | 16 MB | Octal 8 MB | 16/15 | 42 |
| ESP32-S3-DevKitC (N8R2) | `E2EBoard` | 8 MB | Quad 2 MB | 8/9 | 48 |

## Release Process

Pre-push hooks create a draft release automatically. To publish:

```bash
gh release edit v1.2.0 --draft=false
```

## License

MIT
