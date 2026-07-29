---
name: nukcpgdrop
description: NukCPGDrop timed drop rig — ESP32-S3, PCA9685 servos, LVGL display, Blazor WASM captive portal
license: MIT
compatibility: opencode
metadata:
  board: dual-board
  framework: esp-idf
---

# NukCPGDrop — Agent Skill

## Boards

Two ESP32-S3 boards, same firmware with board-specific Kconfig:

| Board | Alias | Flash | PSRAM | Console | I2C | LED |
|-------|-------|-------|-------|---------|-----|-----|
| LCDWiki 2.8" Display (N16R8) | `DisplayBoard` | 16MB | Octal 8MB | USB-serial-JTAG (COM9) | SDA=16, SCL=15 | GPIO42 |
| ESP32-S3-DevKitC (N8R2) | `E2EBoard` | 8MB | Quad 2MB | USB-JTAG (COM8) | SDA=8, SCL=9 | GPIO48 |

Board selection via `CONFIG_BOARD_DISPLAY` / `CONFIG_BOARD_DEVKITC` in Kconfig (`firmware/main/Kconfig`).

## Key Files

| File | Purpose |
|------|---------|
| `flash.py` | Build, flash, verify — MAC-based board detection via `--board DisplayBoard` / `--board E2EBoard` |
| `scripts/board_config.py` | Alias ↔ MAC mapping in `.board_aliases.json` |
| `scripts/embed-web.py` | Converts Blazor WASM publish → C header `web_assets.h` |
| `firmware/main/main.c` | App init, task creation |
| `firmware/main/web_server.c` | HTTP/REST/captive portal/file serving + audio/SD test endpoints |
| `firmware/main/wifi_manager.c` | WiFi AP + STA mode (E2E test) |
| `firmware/main/servos.c` | Servo control via PCA9685, `servos_start_sequence()` for timed drops, uses `g_state.sv_start_pos/sv_stop_pos` (degrees→PWM) |
| `firmware/main/state.c` | NVS state persistence + battery ADC + global start/stop positions (0-180°) |
| `firmware/components/lvgl_porting/lv_port_disp.c` | ILI9341 full init (0x21 inversion, MADCTL=0x08) + FT6336G touch via `esp_lcd_touch_ft5x06` |
| `firmware/components/dashboard_ui/screen_main.c` | LVGL dashboard (scrollable, 1100px content), dynamic action button, 16-can 4-col grid, start/stop sliders, RSSI bars, mic level bar |
| `firmware/components/dashboard_ui/dashboard.c` | Dashboard update task, mic peak level via global `i2s_rx_handle` |
| `firmware/components/audio/es8311.c` | ES8311 full init per official driver (correct volume REG32, mic bias/gain) |
| `firmware/components/audio/i2s_audio.c` | I2S Philips stereo, TX+RX handles (global), MCLK_MULTIPLE_384, DIN=6 for mic |
| `firmware/components/audio/include/audio.h` | Extern `i2s_tx_handle`/`i2s_rx_handle` for shared access |
| `ui/NukCPGDrop.Ui/` | Blazor WASM captive portal dashboard (Index + Debug pages) |
| `ui/NukCPGDrop.Ui/Components/LogoDisplay.razor` | Scrolling art marquee with possum image + "Rest when you're dead" text |
| `ui/NukCPGDrop.Ui/Components/DropStatus.razor` | 16-can tap-to-toggle grid (4 columns) |
| `ui/NukCPGDrop.Ui/Components/RangeSlider.razor` | Dual-knob range slider for interval range |
| `tests/NukCPGDrop.Ui.Tests/Components/LogoDisplayTests.cs` | bUnit tests for LogoDisplay rendering |
| `firmware/partitions.csv` | 16MB partition table (DisplayBoard) |
| `firmware/partitions-8mb.csv` | 8MB partition table (DevKitC/E2E) |
| `firmware/sdkconfig.defaults` | DisplayBoard default config (LV_COLOR_16_SWAP=y) |
| `firmware/sdkconfig.defaults.e2e` | E2E test config (DevKitC + WiFi STA) |

## Build Pipeline

```bash
# Full pipeline (UI → embed → firmware → flash)
python flash.py --board DisplayBoard

# DevKitC E2E build (separate build dir)
python flash.py --board E2EBoard

# Identify connected boards
python flash.py --identify

# Register a new board
python flash.py --register-alias MyName

# flash.py now runs a pre-flight board detection check (esptool flash_id)
# before writing, and fails with a clear error if the board isn't reachable:

# Step by step (not recommended — use flash.py):
# dotnet publish ui/NukCPGDrop.Ui/ -c Release
# python scripts/embed-web.py publish/wwwroot/ firmware/main/include/web_assets.h
# cd firmware && idf.py build
# python -m esptool --chip esp32s3 --port COM9 --baud 921600 write-flash ...
```

## DisplayBoard Pinout (ILI9341 + FT6336G + ES8311)

| Signal | GPIO | Function |
|--------|------|----------|
| TFT MOSI | 11 | SPI2 data out |
| TFT MISO | 13 | SPI2 data in |
| TFT SCLK | 12 | SPI2 clock |
| TFT CS | 10 | Chip select |
| TFT DC | 46 | Data/Command |
| TFT BL | 45 | Backlight PWM |
| Touch SDA | 16 | I2C (shared with PCA9685) |
| Touch SCL | 15 | I2C |
| Touch RST | 18 | Reset |
| I2S MCLK | 4 | Audio codec |
| I2S BCK | 5 | Audio bit clock |
| I2S WS | 7 | Audio word select |
| I2S DOUT | 8 | Audio data out |
| I2S DIN | 6 | Audio data in |
| Amp EN | 1 | Amplifier enable |
| RGB LED | 42 | WS2812B NeoPixel |
| SDMMC CLK | 38 | SD card clock |
| SDMMC CMD | 40 | SD card command |
| SDMMC D0-3 | 39,41,48,47 | SD card data |
| Battery ADC | ADC1_CH8 | Voltage divider (2:1) |

## Display Fix (ILI9341)

Colors are inverted on this panel variant. The init sequence must include command `0x21` (Display Inversion ON). MADCTL = `0x08` (BGR=1, no flip). `LV_COLOR_16_SWAP=y` in sdkconfig for correct byte ordering.

## Touch (FT6336G)

I2C address `0x38`, compatible with FT5x06 driver (`esp_lcd_touch_ft5x06`). Connected via shared I2C bus (SDA=16, SCL=15). RST=GPIO18. The `lv_port_indev_init()` function initializes touch and registers with LVGL. Touch data is fed to LVGL via `touch_read_cb` callback.

## Audio (ES8311)

I2S codec on MCLK=4, BCK=5, WS=7, DOUT=8, DIN=6. I2C config at `0x18`. Amplifier enable on GPIO1. Uses Philips format, stereo, `MCLK_MULTIPLE_384`. Full init sequence: power up analog, enable HP drive, bypass EQ, set volume 85. Audio test via `POST /api/test/audio` (plays 1kHz sine, reads mic, returns peak/energy).

## E2E Test Firmware

The DevKitC can run as a WiFi STA to test the DisplayBoard HTTP server:

```bash
# Build E2E firmware, flash to DevKitC, press RST
python flash.py --board E2EBoard
```

On boot, the E2EBoard connects to `NukCPGDrop-D233BC` AP, sends HTTP GET to `/api/status` and `/`, logs responses via serial.

## Web Dashboard API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/status` | System state, held, PCA9685, WiFi, battery, servo config |
| POST | `/api/drop` | Start sequence or `{"id":N}` single |
| POST | `/api/hold` | `{"id":N}` hold one can |
| POST | `/api/reset` | Hold all cans |
| POST | `/api/config` | Difficulty, double-drop, interval, range, sound |
| POST | `/api/servo_config` | Per-servo direction and min/max |
| GET | `/api/audio/fft` | 8-bin spectrum from mic |
| POST | `/api/test/audio` | Audio loopback test |
| POST | `/api/test/sdcard` | SD card mount/write/read test |

## Common Tasks

### Fix display colors
Check `lv_port_disp.c` for MADCTL (`0x08`) and `0x21` inversion command. Ensure `LV_COLOR_16_SWAP=y` in sdkconfig.

### Add web UI features
Edit `.razor` files in `ui/NukCPGDrop.Ui/Pages/`, then rebuild: `dotnet publish` → `embed-web.py` → firmware build.

### Add LVGL dashboard features
Edit `firmware/components/dashboard_ui/screen_main.c` and `dashboard.c`. The dashboard is scrollable (CONTENT_H=900).

### Add new API endpoint
Add handler function and URI entry in `firmware/main/web_server.c` (api_uris array). Update C# `SystemStatus` model in `ui/NukCPGDrop.Ui/Models/DropConfiguration.cs`.

### Debug touch controller
Add `ESP_LOGI("touch", ...)` in `touch_read_cb()` in `lv_port_disp.c`. Check serial for `points=`, `x=`, `y=` values. Touch coordinates are polled by LVGL's indev timer (every ~33ms).

### Fix LVGL drop button (not starting sequence)
The LVGL action button in `screen_main.c` calls `servos_start_sequence()` which creates a `drop_seq` task. If it doesn't work, check that `servos.c` has `servos_start_sequence()` defined and that `screen_main.c` calls it when all cans are held. The web server drop endpoint calls `audio_play_prompt()` and creates `sequence_task`.

### Fix speaker clicks / no audio
Check `es8311.c` uses the official driver sequence (reset REG00=0x1F→0x00→0x80, clock dividers for 16kHz@MCLK=6.144MHz, HP drive at REG13=0x10, ADC/DAC power up, mic bias at REG0F=0x02, volume at REG20/REG21=0xD9). Check `i2s_audio.c` creates both TX+RX handles with Philips format, stereo mode, and `MCLK_MULTIPLE_384`. The amplifier enable GPIO1 must be set high.

### Fix mic not reading
Previously each consumer (dashboard, FFT handler, test handler) created throwaway I2S RX channels. The fix: `i2s_audio.c` creates both `i2s_tx_handle` and `i2s_rx_handle` once at init, exposed as `extern` globals via `audio.h`. All consumers read from `i2s_rx_handle` directly. Ensure `es8311.c` configures mic bias (REG0F=0x02) and mic gain (REG16).

### Add servo to state
Extend `nukcpgdrop_state_t` in `state.h`, add defaults in `state.c`, expose in API, save via `state_save()`.

## Pre-commit Hooks

Hooks are Python-based (no PowerShell). Defined in `.pre-commit-config.yaml`. Pre-push hooks include:
- ESP-IDF firmware build
- DevKitC E2E build
- QEMU E2E test (skips gracefully if QEMU not available)
- Hardware E2E test (skips if `BOARD_TYPE` not set)
