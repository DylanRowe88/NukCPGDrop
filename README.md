# NukCPGDrop

Timed drop rig for HomeGymCon — 6 servo-controlled N52 magnets dropping Nuks cans
in random sequence. ESP32-S3 with captive portal web UI + 2.8" touch display.

## Quick Start

```bash
# Flash DisplayBoard (auto-detects by MAC)
python flash.py --board DisplayBoard

# Flash DevKitC for E2E testing
python flash.py --board E2EBoard

# List connected boards
python flash.py --identify

# Register a new board
python flash.py --register-alias MyBoard
```

## Boards

| Board | Alias | Flash | PSRAM | Console | I2C | LED |
|-------|-------|-------|-------|---------|-----|-----|
| LCDWiki 2.8" Display (N16R8) | `DisplayBoard` | 16 MB | Octal 8 MB | USB-serial-JTAG | 16/15 | 42 |
| ESP32-S3-DevKitC (N8R2) | `E2EBoard` | 8 MB | Quad 2 MB | USB-JTAG + CH343 | 8/9 | 48 |

## Features

- **6 servos** via PCA9685 I2C PWM driver (shared bus with touch + audio)
- **Captive portal** — DNS catch-all + OS probe detection + mDNS
- **Blazor WebAssembly dashboard** — responsive UI with can indicators, debug page with per-servo direction/calibration, sound toggle, scrolling marquee, real-time mic FFT spectrum
- **2.8" ILI9341 display** — scrollable LVGL digital twin (240×320, 520px content height), touch-enabled via FT6336G (I2C 0x38)
- **ES8311 audio codec** — voice prompts on drop/reset events, mic loopback test, real-time audio peak bar on LVGL
- **Battery monitoring** — ADC voltage divider, percentage on dashboard + API
- **Servo direction toggles** — per-servo HI/LO setting for which end of range = HELD position
- **Real-time mic FFT** — 8-bin spectrum in web debug page + peak level bar on LVGL
- **E2E test firmware** — DevKitC connects as STA to DisplayBoard AP, runs HTTP tests, reports via serial
- **WebSerial browser flasher** — flash firmware directly from Chrome/Edge via [GitHub Pages](https://dylanrowe88.github.io/NukCPGDrop/flash/)
- **MAC-address-based board detection** — no COM port guessing

## Full Documentation

See [ARCHITECTURE.md](ARCHITECTURE.md) for pinouts, init sequences, API reference,
LVGL dashboard layout, E2E test infrastructure, and CI/CD.

## License

MIT
