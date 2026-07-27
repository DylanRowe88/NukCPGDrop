# NukCPGDrop — Architecture & CI/CD

## Overview

HomeGymCon timed drop rig: 6 SG90 servos with N52 magnets hold and release empty Nuks
cans in random sequence. ESP32-S3 creates a WiFi AP with captive portal, serves a
Blazor WebAssembly dashboard, and controls servos via PCA9685 I2C PWM driver.

---

## Hardware

| Component | Role |
|---|---|
| ESP32-S3-DevKit-C N8R2 | Main controller, WiFi AP, HTTP server |
| PCA9685 | 12-bit PWM driver, I2C (addr 0x40) |
| 6x SG90 micro servo | Rotates magnet to hold/release can |
| 6x N52 10x3mm disc magnet | Holds can via steel insert (zero-power) |
| USB power bank (5V @ 2A+) | System power |
| 1000µF 16V cap | 5V rail decoupling |

### Pinout

| Signal | GPIO |
|---|---|
| PCA9685 SDA | 8 |
| PCA9685 SCL | 9 |
| Servo outputs | PCA9685 CH0-5 |
| Status LED | 10 |

---

## Firmware (ESP-IDF 5.2)

### I2C / PCA9685 DMA Driver

Layered design using ESP-IDF I2C master with DMA descriptors:

```
servos.c              — High-level: drop_can(n), drop_batch(), hold_all()
pca9685.c             — Mid-level: write_channel(), write_batch() via I2C DMA
ESP-IDF i2c_master    — Low-level: i2c_master_transmit() with DMA chaining
```

- Batch writes combine multiple servo channels into one I2C transaction
- `pca9685_is_present()` probes bus for ACK at init

### FreeRTOS Task Layout

| Task | Stack | Pri | Function |
|---|---|---|---|
| main | 4K | 1 | Init, then idle |
| wifi | 4K | 3 | WiFi AP, mDNS |
| http | 8K | 2 | HTTP server, REST API |
| drop | 4K | 4 | Drop sequence execution |

### State Persistence

NVS namespace `nukcpgdrop` stores: difficulty, double-drop flag, drop count,
last sequence order. On boot, checks for incomplete sequence to offer resume.

### Captive Portal

ESP32 runs as WiFi AP (SSID: `NukCPGDrop-XXXX`), DNS server catches all UDP/53
queries, HTTP server on port 80. Multi-OS detection probe handling:

| Probe | OS | Action |
|---|---|---|
| `/hotspot-detect.html` | Apple iOS/macOS | 302 → `/` |
| `/generate_204` | Android | 302 → `/` |
| `/connecttest.txt` | Windows | 302 → `/` |
| `/check_network_status.txt` | Linux | 302 → `/` |

mDNS advertises at `nukcpgdrop.local`.

---

## Web UI (Blazor WebAssembly)

Standalone .NET 8 Blazor WASM app embedded into firmware via gzip-compressed C header.

### Build & Embed Pipeline

```
dotnet publish -c Release     →  publish/wwwroot/
scripts/embed-web.py           →  firmware/main/include/web_assets.h + .bin files
idf.py build                   →  firmware/build/NukCPGDrop.bin (~900KB)
```

The `web_server.c` wildcard handler serves files from `web_assets[]` array with
correct MIME types and Content-Encoding: gzip. Captive portal probes are
intercepted before file lookup.

### REST API

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | System state, held status, PCA9685 presence |
| POST | `/api/drop` | Start drop sequence (or `{"id":N}` for single) |
| POST | `/api/config` | Update difficulty / double-drop |

### UI Components

- **Dashboard**: 6 can slots with held/dropped/drop animation states
- **Difficulty selector**: Long (2s), Short (500ms), Random (300-2000ms)
- **Double-drop toggle**: Drops two cans simultaneously
- **Logo**: Placeholder at `/images/nuks-logo.png`
- **Progress bar**: Shows sequence completion
- **Responsive**: CSS clamp() + orientation media queries, touch-friendly

---

## CI/CD Pipeline

### Pre-commit (local, enforced on every commit)

```
1. clang-format           — Format C sources
2. dotnet format          — Format C# sources
3. dotnet restore         — NuGet restore
4. dotnet build           — .NET compilation
5. dotnet test            — 11 unit tests (bUnit + xUnit)
6. idf.py build           — ESP-IDF firmware compilation
7. trailing-whitespace    — Cleanup
8. end-of-file-fixer      — Cleanup
9. check-yaml             — YAML validation
10. check-added-large-files
11. check-merge-conflict
```

### Pre-push (in addition to above)

```
1. idf.py test            — QEMU firmware tests
```

### CI (GitHub Actions)

Seven job pipeline defined in `.github/workflows/ci.yml`:

```
ui-lint → ui-test → ui-mutation (Stryker 100%) → build-and-embed → firmware-test (QEMU)
```

### Deployment

```bash
python flash.py                     # Auto-detect port, build, flash, verify
python flash.py --port COM3         # Manual port
python flash.py --no-build          # Flash existing binary
python flash.py --monitor           # Flash then open serial
```

Port detection matches VID/PID for ESP32-S3 native USB (303A:1001) and common
UART bridges (CP210x, CH340, FTDI).

---

## Testing

| Layer | Framework | Method |
|---|---|---|
| Firmware unit | ESP-IDF Unity | `idf.py test` (QEMU esp32s3) |
| UI unit | xUnit + bUnit | `dotnet test` (11 tests) |
| UI mutation | Stryker.NET | `dotnet stryker` via CI (100% threshold) |

---

## Key Files

| File | Purpose |
|---|---|
| `firmware/main/pca9685.c` | DMA I2C driver for PCA9685 |
| `firmware/main/servos.c` | Servo abstraction, hold/release positions |
| `firmware/main/state.c` | NVS state persistence |
| `firmware/main/web_server.c` | HTTP server, REST API, captive portal, file serving |
| `firmware/main/dns_server.c` | Captive portal DNS |
| `ui/NukCPGDrop.Ui/Pages/Index.razor` | Main dashboard |
| `scripts/embed-web.py` | WASM → C header converter |
| `flash.py` | Build + flash + verify entry point |
| `.pre-commit-config.yaml` | Local hook enforcement |
| `.github/workflows/ci.yml` | GitHub Actions pipeline |
