# NukCPGDrop — Architecture & CI/CD

HomeGymCon timed drop rig: 16 SG90 servos with N52 magnets hold and release empty
Nuks cans in random sequence. ESP32-S3 creates a WiFi AP with captive portal,
serves a Blazor WebAssembly dashboard, drives a 2.8" touch display, and plays
audio prompts.

---

## Hardware

### Display Board (LCDWiki 2.8" ESP32-S3, N16R8)

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3 dual-core LX7 @ 240 MHz |
| Flash | 16 MB QIO |
| PSRAM | 8 MB Octal SPI |
| Display | 2.8" ILI9341 SPI (240×320), 40MHz, 16-bit RGB565 |
| Touch | FT6336G capacitive (I2C addr 0x38) |
| Audio | ES8311 I2S codec + FM8002E amp + MEMS mic |
| RGB LED | WS2812B NeoPixel on GPIO42 |
| USB | USB-serial-JTAG only (Type-C) |
| Battery | TP4054 charger + ADC monitor (ADC1_CH8, 2:1 divider) |

### DevKitC (ESP32-S3-DevKitC N8R2)

Used for WiFi E2E testing. Same firmware with `CONFIG_BOARD_DEVKITC=y`.

### Pinout

| Signal | Display Board | DevKitC |
|--------|--------------|---------|
| PCA9685 SDA | GPIO16 | GPIO8 |
| PCA9685 SCL | GPIO15 | GPIO9 |
| WS2812 LED | GPIO42 | GPIO48 |
| TFT SPI MOSI | GPIO11 | — |
| TFT SPI MISO | GPIO13 | — |
| TFT SPI SCLK | GPIO12 | — |
| TFT CS | GPIO10 | — |
| TFT DC | GPIO46 | — |
| TFT BL | GPIO45 | — |
| Touch I2C SDA | GPIO16 | — |
| Touch I2C SCL | GPIO15 | — |
| Touch RST | GPIO18 | — |
| I2S MCLK | GPIO4 | — |
| I2S BCK | GPIO5 | — |
| I2S WS | GPIO7 | — |
| I2S DOUT | GPIO8 | — |
| I2S DIN | GPIO6 | — |
| Amp EN | GPIO1 | — |
| SDMMC CLK | GPIO38 | — |
| SDMMC CMD | GPIO40 | — |
| SDMMC D0-D3 | GPIO39,41,48,47 | — |
| BOOT button | GPIO0 | GPIO0 |

Board selection via Kconfig: `CONFIG_BOARD_DISPLAY` or `CONFIG_BOARD_DEVKITC`.

### ILI9341 Display Init

This panel requires:
- **0x21** (Display Inversion ON) — the panel ships with inverted color polarity
- **MADCTL = 0x08** — BGR=1, no row/column flip
- **COLMOD = 0x55** — 16-bit pixel format
- **LV_COLOR_16_SWAP=y** in sdkconfig — corrects byte ordering on little-endian ESP32
- Full extended register init (power control, gamma curves) per manufacturer spec

---

## Firmware (ESP-IDF 5.2)

### Component Diagram

```
main/
  servos.c            — Servo drop/hold API, uses g_state.active_servos + sv_start/sv_stop
  pca9685.c           — I2C PCA9685 PWM driver (16 channels)
  state.c             — NVS persistence + battery ADC + Nuks count + positions
  web_server.c        — HTTP/REST/captive portal/file serving + audio/SD test endpoints
  wifi_manager.c      — WiFi AP + STA mode (E2E test) + per-client list API
  dns_server.c        — DNS captive portal
components/
  lvgl/               — LVGL v8.3.6 library
  lvgl_porting/       — ILI9341 SPI (direct mode) + FT6336G touch (esp_lcd_touch_ft5x06)
  dashboard_ui/       — Scrollable LVGL digital twin (720px), dynamic action button,
                        interval range sliders, held/dropped position sliders,
                        8-bin FFT spectrum, Nuks +/- count, sound toggle, battery
  audio/              — ES8311 I2S codec driver + actual audio prompts (sine/sweep tones)
  i2c_shared/         — Shared I2C bus singleton (touch 0x38 + PCA9685 0x40 + ES8311 0x18)
```

### FreeRTOS Task Layout

| Task | Stack | Pri | Core | Function |
|------|-------|-----|------|----------|
| main | 8K | 1 | 0 | Init only |
| lvgl | 4K | 5 | 0 | `lv_task_handler()` every 5ms |
| wifi | 4K | 3 | 1 | WiFi AP, mDNS |
| httpd | 8K | 2 | 1 | HTTP server (`lwip_select` on port 80) |
| drop_seq | 4K | 4 | 1 | Drop sequence execution (respects active_servos) |
| dashboard_update | 3K | 4 | 1 | Updates LVGL widgets, 8-bin FFT spectrum, servo state |

### Captive Portal

ESP32 runs as WiFi AP (SSID: `NukCPGDrop-XXXX`), DNS server catches all UDP/53 queries.

| Probe | OS | Action |
|-------|----|--------|
| `/hotspot-detect.html` | Apple | 302 → `http://192.168.4.1/` |
| `/generate_204` | Android | 302 → `http://192.168.4.1/` |
| `/connecttest.txt` | Windows | 302 → `http://192.168.4.1/` |
| `/check_network_status.txt` | Linux | 302 → `http://192.168.4.1/` |

mDNS: `nukcpgdrop.local`

### State Persistence

Full state blob in NVS (`nukcpgdrop` namespace): difficulty, drop count, active_servos (Nuks count),
interval range (min/max), last sequence, battery voltage, sv_start_pos (held angle),
sv_stop_pos (dropped angle), sound_enabled. Version auto-detected from git via `PROJECT_VER`.

### Digital Twin Dashboard (LVGL) — Current Layout

Scrollable screen (240×320 visible, 720px content height):

```
┌──────────────────────────────────┐
│  NukCPGDrop           RSSI:-45   │  Top bar
├──────────────────────────────────┤
│  Cans (16 max, active_servos=N)  │
│  [1] [2] [3] [4] [5] [6]... [N] │  N tap-to-toggle buttons, 4-col grid
├──────────────────────────────────┤
│  [DROP ALL / RESET / RUNNING...] │  Dynamic action button
├──────────────────────────────────┤
│  Interval Range                  │
│  Min: [===o==========] 500 ms    │  Always visible (always random mode)
│  Max: [=====o========] 2000 ms   │
├──────────────────────────────────┤
│  Servo Positions                 │
│  Held: [===o==========] 45°      │  Changing moves all held servos
│  Dropped: [====o=======] 135°    │  Changing moves all dropped servos
├──────────────────────────────────┤
│  Sound [ON/OFF]                  │  Toggle switch
├──────────────────────────────────┤
│  ██ ██ ██ ██ ██ ██ ██ ██        │  8-bin FFT spectrum (green→yellow→red)
├──────────────────────────────────┤
│  Drops: 42  Clients: 2           │  Status line
│  Battery: ████████░ 93%          │
│  Nuks [-] 16 [+]  v1.2.0        │  Auto-version from git tag
└──────────────────────────────────┘
```

### Display + Touch Driver

- **ILI9341**: Direct SPI on SPI2_HOST (MOSI=11, MISO=13, SCLK=12, CS=10, DC=46, BL=45). No hardware reset pin. 40MHz clock, HALF DUPLEX.
- **FT6336G**: I2C addr 0x38, compatible with `esp_lcd_touch_ft5x06` driver. RST=GPIO18. LVGL indev registered with `touch_read_cb` callback.

### Audio

- **ES8311 codec**: I2C addr 0x18, I2S Philips format, stereo, MCLK_MULTIPLE_384
- **Prompts**: Actual sine/sweep tones generated on-the-fly: 440Hz (single drop), rising 200→800Hz (drop all), falling (reset), pulsed 120Hz (low battery)
- **Mic**: MEMS mic via DIN=6, read via shared I2S RX handle, crude FFT binned into 8 frequency bars

---

## Web UI (Blazor WebAssembly)

Standalone .NET 8 Blazor WASM app gzip-compressed and embedded as C header. Single-page
(simplified from previous debug/home split).

### Build Pipeline

```
dotnet publish -c Release              →  publish/wwwroot/
scripts/embed-web.py ... "vX.Y.Z"      →  firmware/main/include/web_assets.h
idf.py build                           →  firmware/build/NukCPGDrop.bin (~4 MB)
```

`embed-web.py` also writes `wwwroot/version.json` with the build version for runtime
consistency checks against the API.

### Pages

Single page at `/` (`Index.razor`): interval range sliders (always random),
held/dropped position sliders (real-time servo update), sound toggle, can grid
(active_servos determines count), Nuks +/- counter at bottom, FFT spectrum canvas,
version from API.

### REST API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/status` | System state, held, PCA9685, WiFi + per-client list, battery, versions |
| POST | `/api/drop` | Start sequence or `{"id":N}` single |
| POST | `/api/hold` | `{"id":N}` hold one can |
| POST | `/api/reset` | Hold all cans |
| POST | `/api/config` | Difficulty, range, sound, active_servos, held/dropped positions |
| POST | `/api/servo_config` | Held/dropped positions (with real-time servo update) |
| GET | `/api/audio/fft` | 8-bin audio spectrum from mic |
| POST | `/api/test/audio` | Audio loopback test |
| POST | `/api/test/sdcard` | SD card mount/write/read |

### Version Consistency

- Firmware version auto-detected from git tag via ESP-IDF's `PROJECT_VER` → `FW_VERSION` compile define
- `/api/status` returns `wifi.version` = `"NukCPGDrop vX.Y.Z"`
- Blazor displays version from API response (`@(_status?.Wifi?.Version ?? "")`)
- Embedded `wwwroot/version.json` allows Blazor to compare its build version against the firmware

---

## WebSerial Flasher

The `docs/flash/` directory contains a standalone HTML/JS page using
[esptool-js](https://github.com/espressif/esptool-js) and WebSerial to flash
firmware directly from the browser. Features:
- MAC-based board identification (reads chip registers)
- Release version selection with MD5-based installed-firmware detection
- Auto-reconnect to previously authorized serial ports
- Dynamic marquee (possum + "Rest when you're dead") that scales to viewport
- Download from `raw.githubusercontent.com` (CORS-enabled)
- Baud rate auto-detect: 921600 → 460800 → 230400 → 115200 with retry
- per-file progress bars, total progress, ETA countdown
- Flash verification via MD5 checksum
- USB-JTAG hard reset after flash (DTR/RTS via WebSerial API)
- Wi-Fi QR code on completion (dynamic SSID from MAC)
- TRACE log suppression

Served at: https://dylanrowe88.github.io/NukCPGDrop/flash/

### CLI flasher (development)

```bash
python flash.py --board DisplayBoard   # MAC-based detection
python flash.py --identify               # list boards
```

---

## E2E Test Infrastructure

### DevKitC STA HTTP Test

A special firmware variant (`CONFIG_E2E_TEST=y`) that connects as STA to
`NukCPGDrop-D233BC` and runs HTTP tests, logging via serial.

### QEMU Test

Located in `tests/e2e/`. Boots the firmware in QEMU and checks serial logs.
HTTP tests skipped (no RF emulation). Skipped gracefully if QEMU not installed.

### Hardware Playwright Tests

Playwright + Mocha tests in `tests/e2e/firmware.test.js`. Runs against
`http://192.168.4.1/` when `BOARD_TYPE` is set.

---

## Pre-commit / Pre-push Hooks

All Python-based. Defined in `.pre-commit-config.yaml`.

**Pre-commit (every commit) — ~20s:**

```
dotnet test (8.5s) → firmware unit tests (5.8s) → clang-format →
dotnet format → codespell → trailing-whitespace → YAML check →
large-file check → merge-conflict check
```

**Pre-push (incremental) — ~50s:**

```
Full pipeline: dotnet publish (11.5s) → embed-web.py (3s) →
  incremental idf.py build (5.8s, ccache)
DevKitC E2E build → firmware bundle → GitHub release upload →
version consistency → QEMU E2E → hardware E2E
```

Clean build (no ccache) adds ~50s for the first `idf.py build`.


---

## Versioning

Version auto-detected from the nearest `git` tag by ESP-IDF's build system.
Available as `FW_VERSION` compile definition in both web_server and dashboard_ui
components. Displayed in:
- LVGL bottom bar: `v1.2.0`
- Blazor bottom bar: from API `wifi.version`
- WebSerial flasher: installed firmware detection via MD5 manifest matching

---

## CI/CD

GitHub Actions (`ci.yml`) on push/PR to master:
- `ui-lint`: dotnet format verification
- `ui-test`: all C# tests
- `draft-release` (master push only): creates a draft release with instructions
  to build locally and upload via `gh release upload`

No firmware build on GitHub runners (minutes are expensive). Build locally with
`python flash.py --board DisplayBoard`.

---

## Key Files

| File | Purpose |
|------|---------|
| `firmware/main/main.c` | App init, task bootstrap, E2E test mode |
| `firmware/main/servos.c` | PCA9685 servo control, uses `active_servos` + global held/dropped positions |
| `firmware/main/state.c` | NVS persistence: Nuks count, interval range, positions, battery |
| `firmware/main/web_server.c` | HTTP + REST + captive portal (individual URI handlers, no wildcards) |
| `firmware/main/wifi_manager.c` | WiFi AP + STA + per-client list |
| `firmware/components/lvgl_porting/lv_port_disp.c` | ILI9341 full init + FT6336G touch |
| `firmware/components/dashboard_ui/screen_main.c` | LVGL digital twin (720px, all controls) |
| `firmware/components/dashboard_ui/dashboard.c` | 200ms update task, 256-sample crude FFT → 8 bins |
| `firmware/components/audio/es8311.c` | ES8311 init per official driver sequence |
| `firmware/components/audio/i2s_audio.c` | I2S TX+RX handles (global, Philips stereo) |
| `firmware/components/audio/prompts.c` | Real sine/sweep tone generation (was stub) |
| `ui/NukCPGDrop.Ui/Pages/Index.razor` | Single-page Blazor dashboard |
| `docs/flash/` | WebSerial browser flasher (GitHub Pages) |
| `flash.py` | MAC-based CLI build + flash |
| `scripts/setup.py` | One-command dev environment setup |
| `scripts/embed-web.py` | WASM → C header (also writes version.json) |
| `scripts/create-firmware-bundle.py` | Release zip with manifest.json |
| `.pre-commit-config.yaml` | All hook definitions |
| `.github/workflows/ci.yml` | GitHub Actions: lint, test, draft release |
| `.opencode/skills/nukcpgdrop/SKILL.md` | Agent skill for AI coding assistants |
