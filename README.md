# NukCPGDrop

Timed drop rig for HomeGymCon — 6 servo-controlled N52 magnets dropping Nuks cans
in random sequence. ESP32-S3 with captive portal web UI + 2.8" touch display.

## Quick Start

### Flash via browser (no install)

Open [dylanrowe88.github.io/NukCPGDrop/flash/](https://dylanrowe88.github.io/NukCPGDrop/flash/)
in Chrome or Edge, connect your ESP32-S3, select a firmware version, and flash.
Uses WebSerial + esptool-js. No Python, no ESP-IDF, no .NET needed.

### Flash via command line

```bash
# Flash to DisplayBoard (auto-detects by MAC address)
python flash.py --board DisplayBoard

# Flash to DevKitC for WiFi E2E testing
python flash.py --board E2EBoard

# List connected boards
python flash.py --identify

# Register a new board
python flash.py --register-alias MyBoard
```

## Supported Boards

| Board | Alias | Flash | PSRAM | Console |
|-------|-------|-------|-------|---------|
| LCDWiki 2.8" Display (N16R8) | `DisplayBoard` | 16 MB | Octal 8 MB | USB-serial-JTAG |
| ESP32-S3-DevKitC (N8R2) | `E2EBoard` | 8 MB | Quad 2 MB | CH343 UART / USB-JTAG |

Both boards run the same firmware. Board-specific pin mappings selected via Kconfig
(`CONFIG_BOARD_DISPLAY` / `CONFIG_BOARD_DEVKITC`).

See [ARCHITECTURE.md](ARCHITECTURE.md) for full design, pinout, and CI/CD pipeline.

## Features

- **6 servos** via PCA9685 I2C PWM driver
- **Captive portal** — DNS catch-all + probe detection for iOS/Android/Windows/Linux
- **Blazor WebAssembly dashboard** — responsive UI with can indicators, drop/reset controls
- **2.8" ILI9341 display** — LVGL digital twin dashboard (240×320, touch-enabled)
- **FT6336G capacitive touch** — control drops directly on the display
- **ES8311 audio codec** — voice prompts on drop events (I2S, mic + speaker)
- **Battery monitoring** — ADC voltage divider + percentage on dashboard and API
- **mDNS** — `nukcpgdrop.local`
- **REST API** — `/api/status`, `/api/drop`, `/api/config`
- **WiFi AP** — open network, SSID suffix derived from MAC

## Testing

```bash
# QEMU serial-log tests
npm run test:e2e

# Hardware E2E (requires PC connected to ESP AP)
BOARD_TYPE=E2EBoard npx mocha tests/e2e/firmware.test.js --timeout 60000

# All tests
npm run test:e2e:all
```

## License

MIT
