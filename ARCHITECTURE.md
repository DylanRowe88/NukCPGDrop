# NukCPGDrop — Architecture & CI/CD

HomeGymCon timed drop rig: 6 SG90 servos with N52 magnets hold and release empty
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
  led.c               — WS2812 RMT driver
  servos.c            — Servo drop/batch/hold API, reads g_state.servo_dir[]
  pca9685.c           — I2C PCA9685 PWM driver
  state.c             — NVS persistence + battery ADC
  web_server.c        — HTTP/REST/captive portal/file serving + audio/SD test endpoints
  wifi_manager.c      — WiFi AP + STA mode (E2E test) + per-client list API
  dns_server.c        — DNS captive portal
components/
  lvgl/               — LVGL v8.3.6 library
  lvgl_porting/       — ILI9341 SPI (direct mode) + FT6336G touch (esp_lcd_touch_ft5x06)
  dashboard_ui/       — Scrollable LVGL digital twin (520px content), dynamic action button,
                        RSSI bars, servo direction toggles, sound toggle, mic level bar
  audio/              — ES8311 I2S codec driver + prompts
  i2c_shared/         — Shared I2C bus singleton (touch 0x38 + PCA9685 0x40 + ES8311 0x18)
```

### FreeRTOS Task Layout

| Task | Stack | Pri | Core | Function |
|------|-------|-----|------|----------|
| main | 8K | 1 | 0 | Init only |
| lvgl | 4K | 5 | 0 | `lv_task_handler()` every 5ms |
| wifi | 4K | 3 | 1 | WiFi AP, mDNS |
| httpd | 8K | 2 | 1 | HTTP server (`lwip_select` on port 80) |
| drop_seq | 4K | 4 | 1 | Drop sequence execution |
| dashboard_update | 3K | 4 | 1 | Updates LVGL widgets, mic peak, servo state |

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

Full state blob in NVS (`nukcpgdrop` namespace): difficulty, double-drop flag,
drop count, custom interval, range (min/max), last sequence, battery voltage,
servo direction[6], sv_min/max[6], sound_enabled.

### Digital Twin Dashboard (LVGL)

Scrollable screen (240×320 visible, 900px content height):

```
┌──────────────────────────────────┐
│  NukCPGDrop           RSSI:-45   │  Top bar
├──────────────────────────────────┤
│  Cans                            │
│  [1 HELD] [2 HELD] [3 HELD]      │  6 tap-to-toggle can buttons
│  [4 HELD] [5 HELD] [6 HELD]      │  green=held, red=dropped, yellow=anim
├──────────────────────────────────┤
│  Actions                         │
│  [DROP ALL / RESET / RUNNING...]  │  Single dynamic button
├──────────────────────────────────┤
│  Progress            ▓▓▓░░░  3/6 │  Adjusts for double-drop mode
├──────────────────────────────────┤
│  Drop Interval                   │
│  Interval: [=======o====] 2000ms │
│  Double Drop: [ON/OFF]          │
├──────────────────────────────────┤
│  Difficulty                      │
│  [Long] [Short] [Random]         │
│  Min: [==o===]  Max: [===o==]   │  (hidden unless Random)
├──────────────────────────────────┤
│  System                          │
│  PCA9685: OK  Clients: 2         │
│  LED: rgb(0,255,0)  ▂▃▄▅  bars  │  RSSI signal bars
├──────────────────────────────────┤
│  Servo Direction                 │
│  [1 LO] [2 HI] [3 LO]           │  Per-servo toggle
│  [4 HI] [5 LO] [6 HI]           │
├──────────────────────────────────┤
│  Sound                           │
│  Enable: [ON/OFF]               │
├──────────────────────────────────┤
│  Mic Level                       │
│  [========o===========]          │  Real-time audio peak
├──────────────────────────────────┤
│  Drops: 42  Clients: 2           │  Status line
│  Battery: ████████░ 93%          │
│  NukCPGDrop v1.0                 │
└──────────────────────────────────┘
```

### Display + Touch Driver

- **ILI9341**: Direct SPI on SPI2_HOST (MOSI=11, MISO=13, SCLK=12, CS=10, DC=46, BL=45). No hardware reset pin. 40MHz clock, HALF DUPLEX.
- **FT6336G**: I2C addr 0x38, compatible with `esp_lcd_touch_ft5x06` driver. RST=GPIO18. LVGL indev registered with `touch_read_cb` callback.

---

## Web UI (Blazor WebAssembly)

Standalone .NET 8 Blazor WASM app gzip-compressed and embedded as C header.

### Build Pipeline

```
dotnet publish -c Release             →  publish/wwwroot/
scripts/embed-web.py                   →  firmware/main/include/web_assets.h
idf.py build                           →  firmware/build/NukCPGDrop.bin (~4.8 MB)
```

### Pages

| Route | File | Content |
|-------|------|---------|
| `/` | `Index.razor` | Main dashboard: can grid, drop/reset button, difficulty, interval, double-drop, progress bar, scrolling art marquee |
| `/debug` | `Debug.razor` | Full debug: servo calibration + direction, sound toggle, mic FFT spectrum, WiFi client list, system stats |
| `/dashboard` | `Dashboard.razor` | (placeholder) |

### REST API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/status` | System state, held, PCA9685, LED, WiFi + per-client list, battery, servo config, sound |
| POST | `/api/drop` | Start sequence or `{"id":N}` single |
| POST | `/api/hold` | `{"id":N}` hold one can |
| POST | `/api/reset` | Hold all cans |
| POST | `/api/config` | Difficulty, double-drop, interval, range, sound_enabled |
| POST | `/api/servo_config` | Per-servo direction + min/max angles |
| GET | `/api/audio/fft` | 8-bin audio spectrum from mic |
| POST | `/api/test/audio` | Audio loopback test (1kHz sine → speaker → mic) |
| POST | `/api/test/sdcard` | SD card mount/write/read/verify |

---

## Flashing

### WebSerial browser flasher (end users)

The `docs/flash/` directory contains a standalone HTML/JS page using
[esptool-js](https://github.com/espressif/esptool-js) and WebSerial to flash
firmware directly from the browser. Served at
[dylanrowe88.github.io/NukCPGDrop/flash/](https://dylanrowe88.github.io/NukCPGDrop/flash/).

### CLI flasher (development)

MAC-address-based board detection via `flash.py`:

```bash
python flash.py --board DisplayBoard   # 16MB flash, CONFIG_BOARD_DISPLAY
python flash.py --board E2EBoard       # 8MB flash, CONFIG_BOARD_DEVKITC, E2E test
```

### Board Configs

```python
BOARD_CONFIGS = {
    "DisplayBoard": { "build_dir": "build", "flash_size": "16MB", "partitions": "partitions.csv" },
    "E2EBoard":     { "build_dir": "build_devkitc", "flash_size": "8MB", "partitions": "partitions-8mb.csv" },
}
```

---

## E2E Test Infrastructure

### DevKitC STA HTTP Test

A special firmware variant (`CONFIG_E2E_TEST=y`) that:
1. Initializes NVS + WiFi
2. Connects as STA to `NukCPGDrop-D233BC`
3. Runs HTTP GET to `/api/status` and `/`
4. Logs responses via serial
5. Idles (vTaskSuspend)

Build: `python flash.py --board E2EBoard`

### QEMU Test

Located in `tests/e2e/`. Uses Espressif's `qemu-system-xtensa` fork to boot the
firmware and check serial logs. Partition table uses 16MB flash to match the
DisplayBoard firmware. HTTP tests are skipped (QEMU has no RF emulation).

### Hardware Playwright Tests

Board-specific Playwright + Mocha tests in `tests/e2e/firmware.test.js`. Runs
against `http://192.168.4.1/` when `BOARD_TYPE` is set. Covers boot, API
endpoints, static assets, captive portal, dashboard rendering, control ops.

---

## Pre-commit / Pre-push Hooks

All hooks are Python-based (no PowerShell). Defined in `.pre-commit-config.yaml`.

**Pre-commit (every commit):**

```bash
clang-format → dotnet format → dotnet restore → dotnet build →
dotnet test → idf.py build → trailing-whitespace → end-of-file-fixer →
check-yaml → check-added-large-files → check-merge-conflict
```

**Pre-push (added):**

```bash
idf.py build (DevKitC E2E) → create-firmware-bundle → QEMU E2E test (graceful skip) →
hardware E2E test (graceful skip without BOARD_TYPE)
```

---

## Key Files

| File | Purpose |
|------|---------|
| `firmware/main/main.c` | App init, E2E test mode, task bootstrap |
| `firmware/main/pca9685.c` | I2C PCA9685 driver |
| `firmware/main/servos.c` | Servo abstraction, direction-aware via `g_state.servo_dir[]` |
| `firmware/main/state.c` | NVS persistence + battery + servo config |
| `firmware/main/web_server.c` | HTTP + REST + captive portal + file serving + audio/SD test endpoints |
| `firmware/main/wifi_manager.c` | WiFi AP + STA mode + per-client list API |
| `firmware/components/lvgl_porting/lv_port_disp.c` | ILI9341 SPI (full init) + FT6336G touch (esp_lcd_touch_ft5x06) |
| `firmware/components/dashboard_ui/screen_main.c` | Scrollable LVGL digital twin (900px content, all widgets) |
| `firmware/components/dashboard_ui/dashboard.c` | Dashboard update task (200ms tick, mic level read) |
| `firmware/components/audio/` | ES8311 I2S + speaker prompts |
| `firmware/partitions.csv` | 16MB partition table (DisplayBoard) |
| `firmware/partitions-8mb.csv` | 8MB partition table (DevKitC/E2E) |
| `firmware/sdkconfig.defaults` | DisplayBoard config |
| `firmware/sdkconfig.defaults.e2e` | E2E test config |
| `ui/NukCPGDrop.Ui/` | Blazor WASM dashboard (Index + Debug pages) |
| `ui/NukCPGDrop.Ui/Components/LogoDisplay.razor` | Scrolling art marquee with possum + text |
| `ui/NukCPGDrop.Ui/Components/RangeSlider.razor` | Dual-knob range slider for servo calibration |
| `ui/NukCPGDrop.Ui/Components/DropStatus.razor` | Can grid with tap-to-toggle |
| `flash.py` | MAC-based build + flash + verify |
| `scripts/embed-web.py` | WASM → C header |
| `scripts/board_config.py` | MAC alias registry |
| `scripts/create-firmware-bundle.py` | Release zip builder |
| `docs/flash/` | WebSerial browser flasher (GitHub Pages) |
| `tests/e2e/firmware.test.js` | Playwright + Mocha E2E suite |
| `tests/e2e/qemu.js` | QEMU process manager |
| `.opencode/skills/nukcpgdrop/SKILL.md` | Agent skill for AI coding assistants |
| `docs/index.html` | GitHub Pages root (redirects to repo) |
