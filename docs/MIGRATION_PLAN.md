# NukCPGDrop — Migration Plan: DevKitC → 2.8" ESP32-S3 Display

> Board: LCDWiki 2.8inch ESP32-S3 Display (ES3C28P / ES3N28P)
> Datasheets: `docs/2.8inch ESP32-S3 Display/`
> System-on-Chip: ESP32-S3 + Octal PSRAM + 16 MB flash

---

## Table of Contents

1. [Phase 0: Infrastructure & Build Config](#phase-0-infrastructure--build-config)
2. [Phase 1: Core GPIO Migration](#phase-1-core-gpio-migration)
3. [Phase 2: Display + Touch (LVGL Digital Twin)](#phase-2-display--touch-lvgl-digital-twin)
4. [Phase 3: Audio Subsystem](#phase-3-audio-subsystem)
5. [Phase 4: SD Card + Battery](#phase-4-sd-card--battery)
6. [Phase 5: Polish + E2E Tests](#phase-5-polish--e2e-tests)
7. [File-by-File Change List](#7-file-by-file-change-list)
8. [Digital Twin UI Spec](#8-digital-twin-ui-spec)
9. [Testing Strategy](#9-testing-strategy)
10. [Known Risks & Mitigations](#10-known-risks--mitigations)
11. [Appendices](#11-appendices)

---

## Phase 0: Infrastructure & Build Config

**Goal:** Project compiles for the new board with correct flash/PSRAM/USB settings.

### 0.1 Flash size — `sdkconfig.defaults`
```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
```

### 0.2 Octal PSRAM — `sdkconfig.defaults`
```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_TYPE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y
CONFIG_DEFAULT_PSRAM_CLK_IO=30
CONFIG_DEFAULT_PSRAM_CS_IO=26
```

### 0.3 Partition table — `partitions.csv` (16 MB layout)
```
nvs,      data, nvs,      0x9000,   0x4000,
otadata,  data, ota,      0xd000,   0x2000,
phy,      data, phy,      0xf000,   0x1000,
factory,  app,  factory,  0x10000,  14M,
```

### 0.4 USB-serial-JTAG console — `sdkconfig.defaults`
```
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

### 0.5 `flash.py` — port detection update

New board exposes only USB-serial-JTAG (303A:1001), no CH343. Update detection order:
```python
USB_VID_PID = [
    (0x303A, 0x1001),  # ESP32-S3 USB-serial-JTAG (new board)
    (1A86, 55D3),       # CH343 (DevKitC, fallback)
    (10C4, EA60),       # CP210x (fallback)
]
```

Boot-mode sequence changes: press BOOT + tap RESET → release RESET → release BOOT to enter download. `flash.py` must await the JTAG serial port to appear.

### 0.6 Remove QEMU hacks
- Drop `CONFIG_ESP_PHY_CALIBRATION_MODE_INIT_DATA` from `sdkconfig.defaults`
- Remove `esp_phy_store_cal_data_to_nvs()` call from `main.c`
- Remove `nvs_enable = false` from `wifi_manager.c`
- Revert `state.c` to direct `nvs_flash_init()` (no timeout)

### 0.7 Increase `main` task stack for LVGL
```
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```

### 0.8 Verification
- `idf.py build` succeeds
- `flash.py` detects the board and flashes
- Serial log shows "Ready. SSID: NukCPGDrop-*"

**Tests:**
| Test | Where | How |
|------|-------|-----|
| Partition table | Unit | `tests/` — check sizes match 16 MB |
| USB VID/PID | Unit | `tests/` — detection order |
| Build | pre-commit | `idf.py build` |
| Boot (QEMU) | pre-push | serial log "Ready. SSID:" |
| Boot (hardware) | Manual | connect to SSID, open http://192.168.4.1/ |

---

## Phase 1: Core GPIO Migration

**Goal:** PCA9685 servos, WS2812 LED, and I2C bus working on the new pins.

### 1.1 `servos.c` — change I2C pins
```c
pca9685_config_t cfg = {
    .addr = PCA9685_I2C_ADDR_BASE,
    .sda_gpio = 16,    // was 8
    .scl_gpio = 15,    // was 9
    .clk_speed = 100000,
};
```

### 1.2 `led.c` — change WS2812 pin
```c
#define LED_GPIO 42  // was 48
```

### 1.3 `pca9685.c` — no code changes needed

I2C bus now has three devices (PCA9685 @ 0x40, FT6336G @ 0x38, ES8311 @ 0x18/0x19). The I2C driver handles multiple devices on one bus. Verify `i2c_master_probe()` returns ESP_OK for all three addresses.

### 1.4 Verify no GPIO conflicts

GPIO15/16 are used by touch + audio + PCA9685. All three devices have distinct I2C addresses. No conflict.

### 1.5 Verification

**Tests:**
| Test | Where | How |
|------|-------|-----|
| LED color | Hardware | Visual check — green on boot |
| Servo actuation | Hardware | `/api/hold` and `/api/drop` |
| I2C probe | Unit | `test_pca9685.c` (update pin constants) |
| QEMU | pre-push | Skip (no I2C in QEMU) |

---

## Phase 2: Display + Touch (LVGL Digital Twin)

**Goal:** 2.8" ILI9341 display shows a real-time dashboard that mirrors the web UI. Touch input controls can drop/reset and adjust settings.

### 2.1 New components

```
firmware/components/
  lvgl/                        # LVGL v8.3.6 library
  lvgl_esp32_drivers/          # ILI9341 + FT6x36 drivers
    lvgl_tft/ili9341.c
    lvgl_touch/ft6x36.c
    lvgl_i2c/i2c_manager.c
  lvgl_porting/
    lv_port_disp.c             # SPI display init + flush callback
    lv_port_indev.c            # Touch init + read callback
  dashboard_ui/
    include/
      dashboard.h
      screen_main.h
      screen_debug.h
      widgets_can.h
    dashboard.c
    screen_main.c
    screen_debug.c
```

### 2.2 Display init parameters

| Parameter | Value |
|-----------|-------|
| Controller | ILI9341 |
| Interface | 4-wire SPI (SPI2_HOST) |
| SPI clock | 40 MHz |
| Color depth | 16-bit RGB565 |
| MADCTL | 0x08 (portrait, BGR) |
| COLMOD | 0x55 |
| Resolution | 240 × 320 |
| Backlight PWM | GPIO45, 2000 Hz, 8-bit (via LEDC) |
| TFT_RST | -1 (shared with EN) |
| Buffers | 2 × 320 × 40 × 2 = 50 KB (PSRAM) |

### 2.3 Touch init parameters

| Parameter | Value |
|-----------|-------|
| Controller | FT6336G |
| Interface | I2C (same bus as PCA9685) |
| I2C address | 0x38 |
| I2C speed | 400 kHz |
| INT pin | GPIO17 (polling for MVP) |
| RST pin | GPIO18 |
| Report rate | 100 Hz (polled) |

### 2.4 LVGL integration

```c
// main.c — added to app_main()
lv_init();
lv_port_disp_init();      // SPI + ILI9341
lv_port_indev_init();      // I2C + FT6336G

// Create 10 ms tick timer
esp_timer_create_args_t tick_timer = {
    .callback = &lv_tick_callback,
    .name = "lvgl_tick"
};
esp_timer_create(&tick_timer, &lvgl_tick_handle);
esp_timer_start_periodic(lvgl_tick_handle, 10000);  // 10 ms

// Create LVGL task on core 0
xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 0);

// lvgl_task runs every 10 ms:
void lvgl_task(void *arg) {
    while (1) {
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### 2.5 Shared state model

The LVGL UI reads/writes the same `g_state` struct and calls the same servo functions as the web API. No duplication:

```
Web API handler <--> g_state <--> LVGL dashboard
                         ↕
                    Servo driver
```

### 2.6 Dashboard screen layout (240 × 320 portrait)

```
┌──────────────────────────────┐
│  NukCPGDrop v1.0     📶 -45  │  Top bar: title, RSSI
├──────────────────────────────┤
│  ⬤   ⬤   ⬤                  │  Can indicators (3×2 grid)
│  ⬤   ⬤   ⬤                  │  green = held, red = dropped
├──────────────────────────────┤
│  [DROP ALL]    [RESET]       │  Action buttons (touch)
├──────────────────────────────┤
│  ██████░░░░  3/6            │  Sequence progress
├──────────────────────────────┤
│  Interval: [===●=====] 1500 │  Slider (touch-drag)
│  Double Drop: [ON]          │  Toggle (touch)
├──────────────────────────────┤
│  Drops: 42  Clients: 2       │  Status line
│  Battery: ████████░░ 82%   │  Battery level
└──────────────────────────────┘
```

### 2.7 Debug screen (swipe from main)

```
┌──────────────────────────────┐
│  ← Back                      │
├──────────────────────────────┤
│  1  HELD  [===●=====] [⚡]   │  Servo 1: state, slider, toggle
│  2  DROP  [===●=====] [⚡]   │  Servo 2
│  3  HELD  [===●=====] [⚡]   │  Servo 3
│  4  HELD  [===●=====] [⚡]   │  Servo 4
│  5  DROP  [===●=====] [⚡]   │  Servo 5
│  6  HELD  [===●=====] [⚡]   │  Servo 6
├──────────────────────────────┤
│  PCA9685: Detected           │  Status
│  WiFi: 2 clients             │
└──────────────────────────────┘
```

### 2.8 Touch event wiring

| Gesture | Action |
|---------|--------|
| Tap can indicator | Toggle that servo (hold ↔ release) |
| Tap DROP ALL | Call `start_sequence_task()` |
| Tap RESET | Call `servos_hold_all()` |
| Drag interval slider | Update `g_state.custom_interval` |
| Tap Double Drop toggle | Toggle `g_state.double_drop` |
| Swipe left | Switch to debug screen |
| Swipe right | Switch to main screen |
| Tap servo toggle | Call `servos_drop()` / `servos_set()` |

### 2.9 Backlight control

```c
ledc_timer_config_t bl_timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .timer_sel = LEDC_TIMER_0,
    .duty_resolution = LEDC_TIMER_8_BIT,
    .freq_hz = 2000,
};
ledc_timer_config(&bl_timer);
ledc_channel_config_t bl_chan = {
    .gpio_num = 45,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_0,
    .timer_sel = LEDC_TIMER_0,
    .duty = 255,  // 100% brightness
    .hpoint = 0,
};
ledc_channel_config(&bl_chan);
```

### 2.10 Verification

| Test | Where | How |
|------|-------|-----|
| Display init | Hardware | Splash screen (red → green → blue) |
| Touch response | Hardware | Tap can → state toggles |
| Drop via touch | Hardware | Tap DROP ALL → cans release |
| Slider drag | Hardware | Drag interval → value updates |
| Frame rate | Hardware | LVGL benchmark target: >30 FPS |
| Memory | Hardware | `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` |
| QEMU | pre-push | Skip (no display in QEMU) |

---

## Phase 3: Audio Subsystem

**Goal:** Voice prompts play through speaker on drop events.

### 3.1 New component: `components/audio/`

```
audio/
  include/audio.h
  es8311.c          # I2C init + register writes
  i2s_audio.c       # I2S driver init + write
  prompts.c         # Compressed PCM prompt data
  prompts.h
```

### 3.2 ES8311 register init sequence

From the Arduino `audio-tools` library and the `ES8311_DS.pdf` datasheet. Minimal init:

```c
// I2C writes to ES8311 (addr 0x18)
es8311_write_reg(0x01, 0x01);  // Reset
vTaskDelay(10);
es8311_write_reg(0x00, 0x3F);  // Set slave mode, I2S 16-bit
es8311_write_reg(0x10, 0x1E);  // DAC power up
es8311_write_reg(0x11, 0x7F);  // ADC power up
es8311_write_reg(0x12, 0x0C);  // MCLK = 384 × FS
es8311_write_reg(0x13, 0x00);  // Sample rate = 16 kHz
es8311_write_reg(0x14, 0x00);  // Volume control
```

### 3.3 I2S pins

| Signal | GPIO |
|--------|------|
| MCLK | 4 |
| BCK | 5 |
| WS | 7 |
| DOUT | 8 |
| DIN | 6 |

### 3.4 Amplifier enable

GPIO1 (AP_ENABLE) — set HIGH to power FM8002E amp:
```c
gpio_set_direction(1, GPIO_MODE_OUTPUT);
gpio_set_level(1, 1);
```

### 3.5 Audio prompts

Short PCM files compiled into `prompts.c`:
- `drop_1.wav` → "Can one released"
- `drop_all.wav` → "Drop sequence started"
- `reset.wav` → "All cans reset"
- `low_batt.wav` → "Low battery"

Played via `i2s_write()` on event triggers.

### 3.6 Verification

| Test | Where | How |
|------|-------|-----|
| I2C detect | Hardware | `i2c_master_probe(0x18)` == ESP_OK |
| I2S playback | Hardware | Connect speaker, trigger drop → audio |
| Mic capture | Hardware | (Post-MVP) echo test |

---

## Phase 4: SD Card + Battery

### 4.1 SD Card (post-MVP)

```c
sdmmc_host_t host = SDMMC_HOST_DEFAULT();
sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
slot.clk = 38;
slot.cmd = 40;
slot.d0  = 39;
slot.d1  = 41;
slot.d2  = 48;
slot.d3  = 47;
```

FATFS mounted. CSV log written on each drop:
```
timestamp,can_id,action,drop_count
2026-07-28T12:00:00Z,1,DROP,42
```

### 4.2 Battery ADC

ADC1_CH8 via 2:1 voltage divider:

```c
adc1_config_width(ADC_WIDTH_BIT_12);
adc1_config_channel_atten(ADC1_CHANNEL_8, ADC_ATTEN_DB_12);
int raw = adc1_get_raw(ADC1_CHANNEL_8);
int millivolts = esp_adc_cal_raw_to_voltage(raw, &adc_chars);
int battery_mv = millivolts * 2;     // 2:1 divider
int battery_pct = (battery_mv - 2500) * 100 / (4200 - 2500);
battery_pct = battery_pct < 0 ? 0 : (battery_pct > 100 ? 100 : battery_pct);
```

Add `battery` field to `/api/status` JSON:
```json
{ "battery": { "millivolts": 3800, "percent": 82, "charging": false } }
```

### 4.3 Verification

| Test | Where | How |
|------|-------|-----|
| Battery voltage | Hardware | Read `/api/status`, compare with multimeter |
| SD card | Hardware | Insert card, run drops, check CSV |
| Battery UI | Hardware | Dashboard shows correct % |

---

## Phase 5: Polish + E2E Tests

### 5.1 Pre-commit hooks update
- Add `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` validation
- Add LVGL component compilation check
- Keep existing clang-format, dotnet, firmware build hooks

### 5.2 Pre-push hooks update
- Keep QEMU serial-log tests
- Add hardware E2E step (when runner board is connected)
- Keep existing ESP-IDF test

### 5.3 Playwright E2E tests
- Update `tests/e2e/firmware.test.js`:
  - Test `/api/status` includes `battery` field
  - Test display-responsive layout (viewport 240×320)

### 5.4 Verification

| Test | Where | How |
|------|-------|-----|
| All pre-commit | pre-commit | `git commit` passes |
| All pre-push | pre-push | `git push` passes |
| 24-hour soak | Hardware | FW runs 24h with drop sequences |

---

## 7. File-by-File Change List

| File | Change | Phase |
|------|--------|-------|
| `firmware/sdkconfig.defaults` | 16 MB flash, Octal PSRAM, USB-serial-JTAG, no calibration hack | 0 |
| `firmware/partitions.csv` | 14 MB app partition | 0 |
| `scripts/flash.py` | USB-JTAG detection, boot sequence | 0 |
| `firmware/main/main.c` | Add LVGL init, remove QEMU injection | 0, 2 |
| `firmware/main/CMakeLists.txt` | Add lvgl, dashboard_ui components | 2 |
| `firmware/main/servos.c` | I2C pins 16/15 | 1 |
| `firmware/main/led.c` | WS2812 pin 42 | 1 |
| `firmware/main/wifi_manager.c` | Remove nvs_enable=false, QEMU IP override | 0 |
| `firmware/main/state.c` | Remove timeout, restore direct nvs_flash_init | 0 |
| `firmware/main/include/state.h` | Add battery fields | 4 |
| `firmware/main/web_server.c` | Add battery to /api/status | 4 |
| `firmware/components/lvgl/` | **New** — LVGL v8.3.6 | 2 |
| `firmware/components/lvgl_esp32_drivers/` | **New** — ILI9341 + FT6x36 | 2 |
| `firmware/components/lvgl_porting/` | **New** — disp + indev wrappers | 2 |
| `firmware/components/dashboard_ui/` | **New** — digital twin screens | 2 |
| `firmware/components/audio/` | **New** — ES8311 driver + prompts | 3 |
| `firmware/components/battery/` | **New** — ADC monitor | 4 |
| `tests/e2e/firmware.test.js` | Add battery, display checks | 5 |
| `.pre-commit-config.yaml` | Add 16 MB flash check | 5 |
| `docs/MIGRATION_PLAN.md` | This document | All |

---

## 8. Digital Twin UI Spec

### Screens

| Screen | Trigger | Elements |
|--------|---------|----------|
| Main | Boot, swipe right | Can indicators, drop/reset buttons, progress bar, interval slider, double-drop toggle, status, battery |
| Debug | Swipe left | 6 servo rows (state label, calibration slider, toggle button), back button, system status |
| Splash | Boot (3 s) | Logo "NukCPGDrop v1.0", loading spinner |

### Update rates

| Data | Interval | Source |
|------|----------|--------|
| Can states | 200 ms | `g_held[]` |
| Drop progress | 200 ms | `drop_count` |
| RSSI | 2 s | `wifi_ap_get_rssi()` |
| Battery | 10 s | ADC1_CH8 |
| WiFi clients | 2 s | `wifi_ap_get_sta_count()` |
| Total drops | 1 s | `g_state.drop_count` |

### Color palette

| Role | Hex | LVGL style |
|------|-----|------------|
| Background | `#0d0d0d` | `lv_color_hex(0x0d0d0d)` |
| Text | `#f0f0f0` | `lv_color_hex(0xf0f0f0)` |
| Muted | `#888888` | `lv_color_hex(0x888888)` |
| Accent | `#e63946` | `lv_color_hex(0xe63946)` |
| Held (green) | `#28a745` | `lv_color_hex(0x28a745)` |
| Dropped (red) | `#dc3545` | `lv_color_hex(0xdc3545)` |

---

## 9. Testing Strategy

### QEMU tests (pre-push, automated)

| # | Test | What it checks |
|---|------|----------------|
| 1 | Boot | Serial log contains "Ready. SSID:" |
| 2 | NVS | "state loaded" appears |
| 3 | Web server | "HTTP server running on :80" |
| 4 | DNS | "DNS server listening on port 53" |
| 5 | mDNS | "mDNS: nukcpgdrop.local" |

### Hardware tests (manual or CI runner)

| # | Test | Prerequisites |
|---|------|---------------|
| 1 | LED | Board powered |
| 2 | Servo | PCA9685 + 6 servos connected |
| 3 | Display | Phase 2 complete |
| 4 | Touch | Phase 2 complete |
| 5 | Audio | Phase 3 complete + speaker |
| 6 | SD card | Phase 4 complete + card inserted |
| 7 | Battery | Phase 4 complete + LiPo connected |
| 8 | Web UI | WiFi connected, browser open |
| 9 | Captive portal | Fresh connect to AP, no cache |

### CI pipeline

```
pre-commit:
  clang-format → dotnet format → dotnet restore → dotnet build →
  dotnet test → idf.py build → trailing-whitespace → end-of-file-fixer →
  check-yaml → check-added-large-files → check-merge-conflict

pre-push:
  (all pre-commit) +
  idf.py test (QEMU unit) +
  npm test:e2e (QEMU serial-log) +
  npm test:e2e:real (hardware, when runner connected)
```

---

## 10. Known Risks & Mitigations

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|-----------|--------|------------|
| 1 | USB-serial-JTAG not detected by Windows | Medium | Can't flash | Fall back to manual port; document `flash.py --port COMx` |
| 2 | I2C bus contention (3 devices) | Low | Missed touch events | 400 kHz touch + 100 kHz PCA9685; test with scope |
| 3 | LVGL double buffer exceeds PSRAM bandwidth | Low | Screen tear | Use DMA + 40 MHz SPI; benchmark with `lv_demo_benchmark` |
| 4 | ES8311 MCLK wrong frequency | Medium | No audio | Verify MCLK = 384 × sample_rate |
| 5 | LVGL + drop sequence CPU contention | Low | UI stutter | Core 0: LVGL; Core 1: drops |
| 6 | Display RST tied to EN — can't reset display independently | Low | Display stuck after crash | Power-cycle board to reset display |
| 7 | Factory calibration data missing | Low | WiFi recalibrates each boot | First boot calibrates, stores to NVS |
| 8 | Board rev v0.0 (QEMU) vs real silicon | Low | Rev detection | Hard-code revision for real hardware in `led.c` / `servos.c` |

---

## 11. Appendices

### A: Full GPIO Allocation

| GPIO | Function | Dir | Notes |
|------|----------|-----|-------|
| 0 | BOOT button | IN | Strapping, pull-up |
| 1 | AP_ENABLE | OUT | Amp power (active HIGH) |
| 4 | I2S_MCLK | OUT | ES8311 master clock |
| 5 | I2S_BCK | I/O | I2S bit clock |
| 6 | I2S_DIN | IN | Microphone |
| 7 | I2S_WS | I/O | I2S word select |
| 8 | I2S_DOUT | OUT | Speaker |
| 10 | TFT_CS | OUT | SPI CS |
| 11 | TFT_MOSI | OUT | SPI MOSI |
| 12 | TFT_SCLK | OUT | SPI clock |
| 13 | TFT_MISO | IN | SPI MISO |
| 15 | I2C_SCL | I/O | Touch + audio + PCA9685 |
| 16 | I2C_SDA | I/O | Touch + audio + PCA9685 |
| 17 | TOUCH_INT | IN | FT6336G (optional) |
| 18 | TOUCH_RST | OUT | Shared display/touch reset |
| 26 | PSRAM_CS | OUT | Octal PSRAM |
| 30 | PSRAM_CLK | OUT | Octal PSRAM |
| 38 | SD_CLK | OUT | SDMMC |
| 39 | SD_D0 | I/O | SDMMC |
| 40 | SD_CMD | I/O | SDMMC |
| 41 | SD_D1 | I/O | SDMMC |
| 42 | RGB_LED | OUT | WS2812B |
| 45 | TFT_BL | OUT | Backlight PWM |
| 46 | TFT_DC | OUT | Display data/command |
| 47 | SD_D3 | I/O | SDMMC |
| 48 | SD_D2 | I/O | SDMMC |

**Unused:** 2, 3, 14, 21, 33–37, 43, 44.

### B: Arduino Example Reference

| Example | Peripheral | Key code to port |
|---------|-----------|-----------------|
| 01_Simple_test | ILI9341 init | Fill/clear routines |
| 06_RGB_LED | WS2812 | NeoPixel → RMT |
| 08_LVGL_Demos | LVGL | UI structure |
| 13_Get_Battery_Voltage | ADC | Read + formula |
| 14_Backlight_PWM | LEDC | PWM config |
| 16_music | ES8311 | I2S + I2C init |
| 17_echo | Loopback | Full audio path |
| 19_WiFi_AP | AP mode | Reference only |
| 29_touch_pen | FT6336G | Touch read + rotation |
| 30_ai_chat | Audio AI | Full pipeline |

### C: Flash quick reference

```powershell
# Enter download mode (2.8" Display board):
#   1. Hold BOOT button
#   2. Tap RESET
#   3. Release RESET, then release BOOT

cd firmware
idf.py build
cd ..
python flash.py                    # auto-detect USB-JTAG
python flash.py --port COM5       # explicit

# Monitor (USB-serial-JTAG)
idf.py monitor
```

### D: Migration checklist

- [ ] Phase 0: Build config, partitions, flash.py, remove QEMU hacks
- [ ] Phase 0 test: Build + boot on QEMU + hardware
- [ ] Phase 1: GPIO remap (I2C, LED)
- [ ] Phase 1 test: LED blinks, servos move
- [ ] Phase 2: LVGL + ILI9341 + FT6336G
- [ ] Phase 2 test: Display shows UI, touch responds
- [ ] Phase 3: ES8311 I2S audio
- [ ] Phase 3 test: Speaker plays prompts
- [ ] Phase 4: SD card + battery ADC
- [ ] Phase 4 test: CSV logs, battery reads correctly
- [ ] Phase 5: E2E tests, CI hooks
- [ ] Phase 5 test: All pre-commit/pre-push pass
