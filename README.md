# NukCPGDrop

Timed drop rig for HomeGymCon — 16 servo-controlled N52 magnets dropping Nuks cans
in random sequence. ESP32-S3 with captive portal web UI + 2.8" touch display.

## Quick Start — New Developer

```bash
# 1. Check what's installed
python scripts/setup.py

# 2. Install pre-commit hooks (runs on every commit automatically)
pip install pre-commit
pre-commit install
pre-commit install --hook-type pre-push

# 3. Build + flash the DisplayBoard
python flash.py --board DisplayBoard
```

Or flash from the browser: https://dylanrowe88.github.io/NukCPGDrop/flash/

## Dependencies

| Tool | Version | Required for |
|------|---------|-------------|
| Python | 3.11+ | Build scripts, flash tool |
| ESP-IDF | v5.2 | Firmware compilation (`install.ps1 esp32s3`) |
| .NET SDK | 8.0 | Blazor WebAssembly UI (`dotnet install`) |
| esptool | latest | Flashing (`pip install esptool`) |
| gh CLI | latest | Release uploads (`winget install GitHub.cli`) |
| ccache | — | Faster rebuilds (optional, `winget install ccache`) |

### Environment Setup

```bash
# Windows — activate ESP-IDF before building
. C:/path/to/esp-idf/export.ps1

# Linux/macOS
source ~/esp/esp-idf/export.sh
```

## Development Workflow

```bash
# Full build pipeline (UI → embed → firmware → flash)
python flash.py --board DisplayBoard

# Build & run C# tests
dotnet test tests/NukCPGDrop.Ui.Tests/

# Build firmware tests
cd firmware && idf.py build --target test

# Flash via CLI (auto-detects board by MAC)
python flash.py --board DisplayBoard

# E2E test: flash DevKitC with test firmware
python flash.py --board E2EBoard --monitor
```

## Pre-commit Hooks (automatic)

Every commit runs: full pipeline (dotnet publish → embed → firmware build) → C# tests → firmware tests → codespell → linting.

Every push runs: firmware bundle → release upload → E2E tests.

If a hook blocks you:
```bash
git commit --no-verify   # bypass pre-commit
git push --no-verify     # bypass pre-push
```

## Boards

| Board | Alias | Flash | PSRAM | I2C | LED |
|-------|-------|-------|-------|-----|-----|
| LCDWiki 2.8" Display (N16R8) | `DisplayBoard` | 16 MB | Octal 8 MB | 16/15 | 42 |
| ESP32-S3-DevKitC (N8R2) | `E2EBoard` | 8 MB | Quad 2 MB | 8/9 | 48 |

## Release Process

```bash
# Pre-push hooks create a draft release automatically.
# To publish:
gh release edit v1.2.0 --draft=false
```

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for pinouts, API reference, LVGL layout, E2E infra.

## License

MIT
