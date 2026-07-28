# NukCPGDrop E2E Tests

Tests the firmware's boot sequence, HTTP API, captive portal, static assets, and Web UI
via Playwright — in **QEMU** (serial log checks) or on **real hardware**.

## Supported boards

| Board | Chips | Connectivity | Tests |
|-------|-------|-------------|-------|
| **QEMU** (virtual) | ESP32-S3 | Simulated | Boot, NVS, WiFi init, HTTP server, DNS, mDNS |
| **E2EBoard** (DevKitC N8R2) | ESP32-S3 + 8MB flash + 2MB PSRAM | CH343 UART or USB-JTAG | Full Web UI, captive portal, API, assets, controls |
| **DisplayBoard** (N16R8) | ESP32-S3 + 16MB flash + 8MB PSRAM | USB-serial-JTAG | Boot, API, battery, display, touch, audio |

## Prerequisites

```powershell
# Playwright browsers (first time only)
npx playwright install chromium

# Python HTTP tests (optional)
pip install requests
```

## Running tests

### QEMU mode (default)

```powershell
# Requires QEMU ESP32-S3 binary in .qemu/
npm run test:e2e
```

Validates boot serial log: `app_main()`, NVS, WiFi init, HTTP/DNS/mDNS starting.

### E2EBoard (DevKitC — WiFi AP)

```powershell
# 1. Flash the board (MAC-based alias, no COM port needed)
python flash.py --board E2EBoard

# 2. Connect your PC to the ESP AP (SSID: NukCPGDrop-XXXX)
#    or run with TARGET_URL pointing at the board

# 3. Run E2E tests
$env:BOARD_TYPE="E2EBoard"
$env:TARGET_URL="http://192.168.4.1"
npm run test:e2e:e2eboard
```

Tests all Web UI functionality: dashboard, captive portal, API endpoints,
static assets, .wasm loading, servo controls, can toggles.

### DisplayBoard (USB-JTAG)

```powershell
# 1. Flash the board (MAC-based alias, no COM port needed)
python flash.py --board DisplayBoard

# 2. Run E2E tests (serial boot check + HTTP API)
$env:BOARD_TYPE="DisplayBoard"
$env:TARGET_URL="http://192.168.4.1"
npm run test:e2e:display
```

Verifies boot via serial (if SERIAL_PORT set) and checks HTTP API for
battery, display, touch, and audio fields.

### All modes

```powershell
npm run test:e2e:all
```

## Advanced options

| Env var | Default | Description |
|---------|---------|-------------|
| `BOARD_TYPE` | `qemu` | `qemu`, `E2EBoard`, or `DisplayBoard` |
| `TARGET_URL` | `http://192.168.4.1` (HW) / `http://localhost:8080` (QEMU) | Board HTTP URL |
| `SERIAL_PORT` | (none) | COM port for hardware serial boot check |

## Python HTTP tests

```powershell
# Against DevKitC AP
python tests/http_tests/test_http_server.py

# Against mock server (offline)
python tests/http_tests/test_http_server.py --mock
```

## Test output

Performance metrics are written to `.e2e-perf.json` after each run:

```json
[
  { "label": "Page load (full)", "value": 3420, "unit": "ms", "board": "devkitc" },
  { "label": "Asset /_framework/dotnet.native.wasm", "value": 1048576, "unit": "bytes", "board": "devkitc" }
]
```

## CI integration

```yaml
pre-commit:  clang-format → dotnet → firmware build → lint
pre-push:    (pre-commit) + idf.py test + npm run test:e2e
```
