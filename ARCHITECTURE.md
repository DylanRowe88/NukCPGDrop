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
| Display | 2.8" ILI9341 SPI (240×320) |
| Touch | FT6336G capacitive (I2C addr 0x38) |
| Audio | ES8311 I2S codec + FM8002E amp + MEMS mic |
| RGB LED | WS2812B NeoPixel on GPIO42 |
| USB | USB-serial-JTAG only (Type-C) |
| Battery | TP4054 charger + ADC monitor (ADC1_CH8) |

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
| SDMMC | GPIO38-41,47,48 | — |
| BOOT button | GPIO0 | GPIO0 |

Board selection via Kconfig: `CONFIG_BOARD_DISPLAY` or `CONFIG_BOARD_DEVKITC`.

---

## Firmware (ESP-IDF 5.2)

### Component Diagram

```
main/
  led.c               — WS2812 RMT driver
  servos.c            — Servo drop/batch/hold API
  pca9685.c           — I2C PCA9685 PWM driver
  state.c             — NVS persistence
  web_server.c        — HTTP/REST/captive portal/file serving
  wifi_manager.c      — WiFi AP + netif
  dns_server.c        — DNS captive portal
components/
  lvgl/               — LVGL v8.3.6 library
  lvgl_porting/       — ILI9341 SPI driver + FT6336G touch
  dashboard_ui/       — Digital twin LVGL screens
  audio/              — ES8311 I2S codec driver
  i2c_shared/         — Shared I2C bus (touch + audio + PCA9685)
  battery/            — ADC + voltage monitor
```

### FreeRTOS Task Layout

| Task | Stack | Pri | Core | Function |
|------|-------|-----|------|----------|
| main | 8K | 1 | 0 | Init, LVGL handler |
| lvgl | 4K | 5 | 0 | LVGL task handler (5ms tick) |
| wifi | 4K | 3 | 1 | WiFi AP, mDNS |
| http | 8K | 2 | 1 | HTTP server, REST API |
| drop | 4K | 4 | 1 | Drop sequence execution |

### Captive Portal

ESP32 runs as WiFi AP (SSID: `NukCPGDrop-XXXX`), DNS server catches all UDP/53 queries.

| Probe | OS | Action |
|-------|----|--------|
| `/hotspot-detect.html` | Apple | 302 → `http://192.168.4.1/` |
| `/generate_204` | Android | 302 → `http://192.168.4.1/` |
| `/connecttest.txt` | Windows | 302 → `http://192.168.4.1/` |
| `/check_network_status.txt` | Linux | 302 → `http://192.168.4.1/` |

mDNS advertises at `nukcpgdrop.local`.

### State Persistence

NVS namespace stores: difficulty, double-drop flag, drop count, custom interval,
range, last sequence. Battery voltage read every 10 seconds.

### Digital Twin Dashboard (LVGL)

The 2.8" display shows the same information as the web UI:

```
┌──────────────────────────────┐
│  NukCPGDrop v1.0     📶 -45  │
├──────────────────────────────┤
│  ⬤   ⬤   ⬤                  │  6 can indicators (3×2)
│  ⬤   ⬤   ⬤                  │  green=held, red=dropped
├──────────────────────────────┤
│  [DROP ALL]    [RESET]       │  Touch buttons
├──────────────────────────────┤
│  ██████░░░░  3/6            │  Progress bar
├──────────────────────────────┤
│  Drops: 42  Clients: 2       │  Status line
│  Battery: ████████░░ 82%   │
└──────────────────────────────┘
```

---

## Web UI (Blazor WebAssembly)

Standalone .NET 8 Blazor WASM app gzip-compressed and embedded as C header.

### Build Pipeline

```
dotnet publish -c Release     →  publish/wwwroot/
scripts/embed-web.py           →  firmware/main/include/web_assets.h
idf.py build                   →  firmware/build/NukCPGDrop.bin (~4.7 MB)
```

### REST API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/status` | System state, held, PCA9685, LED, WiFi, battery |
| POST | `/api/drop` | Start sequence or `{"id":N}` single |
| POST | `/api/hold` | `{"id":N}` hold one can |
| POST | `/api/reset` | Hold all cans |
| POST | `/api/config` | Difficulty, double-drop, interval, range |

---

## Flashing

### WebSerial browser flasher (end users)

The `docs/flash/` directory contains a standalone HTML/JS page that uses
[esptool-js](https://github.com/espressif/esptool-js) and WebSerial to flash
firmware directly from the browser. Served via GitHub Pages at
[dylanrowe88.github.io/NukCPGDrop/flash/](https://dylanrowe88.github.io/NukCPGDrop/flash/).
No Python, ESP-IDF, or .NET required — just Chrome or Edge.

The page fetches firmware bundles from GitHub Releases, displays available
versions, and guides the user through connect → select → flash → verify.

### CLI flasher (development)

MAC-address-based board detection — no COM port guessing:

```bash
# Register a board once
python flash.py --register-alias MyBoard   # probes all ports
python flash.py --identify                 # list known boards

# Flash by alias
python flash.py --board DisplayBoard       # matches MAC 14:C1:9F:D2:33:BC
python flash.py --board E2EBoard           # matches MAC D8:3B:DA:76:23:28
```

Aliases stored in `.board_aliases.json`. Script verifies the MAC matches before
flashing, preventing accidental cross-flashing.

### Publishing a release

The pre-push hook automatically creates a firmware bundle zip after every push.
To publish a new release:

```bash
git tag v1.1.0
git push origin v1.1.0
gh release create v1.1.0 ./NukCPGDrop-Display-v1.1.0.zip --title "v1.1.0"
```

The bundle zip includes `bootloader.bin`, `partition-table.bin`, `NukCPGDrop.bin`,
`ota_data_initial.bin`, and a `manifest.json` with MD5 checksums. The WebSerial
page fetches these releases via the GitHub API and handles download + extraction
client-side.

---

## Testing

### QEMU (ESP32-S3, pre-push)

| Test | What it checks |
|------|----------------|
| Boot | Serial log "Ready. SSID:" |
| NVS | "state loaded" |
| Web server | "HTTP server running" |
| DNS | "DNS server listening" |
| mDNS | "mDNS: nukcpgdrop.local" |

Limitations: ESP32-S3 PHY init hangs in QEMU (no RF emulation), so HTTP is
unreachable. All non-network tests pass.

### Hardware (E2EBoard — DevKitC)

Full Playwright test suite against `http://192.168.4.1/`:

- Page load, asset serving, WASM loading
- Captive portal redirect detection
- API endpoint response time (<500ms target)
- Can toggle via API
- Performance metrics (page weight, asset sizes)

### Pre-commit hooks

```
clang-format → dotnet format → dotnet restore → dotnet build →
dotnet test → idf.py build → trailing-whitespace → end-of-file-fixer →
check-yaml → check-added-large-files → check-merge-conflict
```

### Pre-push hooks (adds)

```
idf.py test (QEMU) → npm run test:e2e → create-firmware-bundle
```

---

## Key Files

| File | Purpose |
|------|---------|
| `firmware/main/main.c` | App init, LVGL + WiFi + web server bootstrap |
| `firmware/main/pca9685.c` | DMA I2C driver for PCA9685 |
| `firmware/main/servos.c` | Servo abstraction, hold/release |
| `firmware/main/state.c` | NVS persistence + battery |
| `firmware/main/web_server.c` | HTTP + REST + captive portal + file serving |
| `firmware/main/wifi_manager.c` | WiFi AP + Kconfig board selection |
| `firmware/main/dns_server.c` | Captive portal DNS |
| `firmware/components/lvgl_porting/` | ILI9341 SPI + FT6336G touch drivers |
| `firmware/components/dashboard_ui/` | LVGL digital twin screens |
| `firmware/components/audio/` | ES8311 I2S codec |
| `ui/NukCPGDrop.Ui/` | Blazor WebAssembly dashboard |
| `scripts/embed-web.py` | WASM → C header converter |
| `scripts/board_config.py` | MAC alias registry |
| `flash.py` | Build + flash + verify + E2E |
| `docs/flash/` | WebSerial browser flasher (GitHub Pages) |
| `scripts/create-firmware-bundle.py` | Package build output into release zip |
| `tests/e2e/firmware.test.js` | Playwright + Mocha E2E tests |
| `tests/e2e/qemu.js` | QEMU process manager |
| `docs/MIGRATION_PLAN.md` | Board migration plan |
