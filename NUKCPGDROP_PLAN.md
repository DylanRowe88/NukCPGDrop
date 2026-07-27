# NukCPGDrop — Complete Build Plan

## Overview

Timed drop rig for HomeGymCon: 6 electromagnet-adjacent drop stations controlled by an
ESP32-S3, served by a captive-portal Blazor WebAssembly dashboard. ESP32 creates its own
WiFi AP, runs mDNS + DNS captive portal, hosts the WASM UI, and controls 6x SG90 servos
with N52 magnets via a PCA9685 I2C PWM driver using the ESP's I2C DMA engine.

---

## 1. System Architecture

```
┌─────────────────────────────────────────────────────┐
│  USB Power Bank (5V @ 2A+)                          │
│    ├── ESP32-S3-DevKit-C N8R2 (USB)                  │
│    │     └── 3.3V LDO → ESP32-S3 + PSRAM             │
│    └── 5V rail ─→ PCA9685 V+ ─→ 6x SG90 servos      │
│                      VCC ← 3.3V from ESP             │
│                      GND ← common                    │
└──────────────────────┬──────────────────────────────┘
                       │ I2C (SDA/SCL via GPIO8/9)
                       ▼
┌─────────────────────────────────────────────────────┐
│  ESP32-S3 Firmware (ESP-IDF 5.x)                      │
│  ┌──────────┐ ┌──────────┐ ┌───────────────────┐    │
│  │ WiFi AP  │ │ mDNS     │ │ Captive Portal    │    │
│  │ 192.168.4.1 │ nukcpgdrop │ DNS→all→ESP      │    │
│  │          │ │ .local   │ │                   │    │
│  ├──────────┤ ├──────────┤ ├───────────────────┤    │
│  │ HTTP     │ │ REST API │ │ WebSocket         │    │
│  │ Server   │ │ /api/*   │ │ /ws (realtime)    │    │
│  ├──────────┤ ├──────────┤ ├───────────────────┤    │
│  │ PCA9685  │ │ State    │ │ Drop Sequence     │    │
│  │ DMA I2C  │ │ Manager  │ │ Scheduler         │    │
│  │ Driver   │ │ (NVS)    │ │ (FreeRTOS)        │    │
│  └──────────┘ └──────────┘ └───────────────────┘    │
└──────────────────────┬──────────────────────────────┘
                       │ HTTP (served from flash)
                       ▼
┌─────────────────────────────────────────────────────┐
│  Blazor WebAssembly UI (client-side .NET 8)          │
│  ┌─────────────────┐ ┌─────────────────────────┐    │
│  │ Dashboard       │ │ Difficulty Settings     │    │
│  │ - Drop Sequence │ │ - Long (1s interval)    │    │
│  │ - Status        │ │ - Short (500ms)         │    │
│  │ - Logo          │ │ - Random (varied)       │    │
│  │                 │ │ - Double-drop mode      │    │
│  ├─────────────────┤ ├─────────────────────────┤    │
│  │ Responsive      │ │ Screen scaling/zoom     │    │
│  │ CSS Grid/Flex   │ │ rotation via CSS        │    │
│  └─────────────────┘ └─────────────────────────┘    │
└─────────────────────────────────────────────────────┘
```

---

## 2. Project Structure

```
NukCPGDrop/
├── firmware/                            # ESP-IDF C project
│   ├── CMakeLists.txt                   # Top-level IDF project
│   ├── partitions.csv                   # NVS + Web partition
│   ├── sdkconfig                        # Default config
│   ├── main/
│   │   ├── CMakeLists.txt               # Main component
│   │   ├── include/
│   │   │   ├── pca9685.h
│   │   │   ├── servos.h
│   │   │   ├── state.h
│   │   │   ├── wifi_manager.h
│   │   │   ├── dns_server.h
│   │   │   └── web_server.h
│   │   ├── pca9685.c                    # DMA I2C driver
│   │   ├── servos.c                     # Servo abstraction
│   │   ├── state.c                      # NVS persistence
│   │   ├── wifi_manager.c               # WiFi AP setup
│   │   ├── dns_server.c                 # Captive portal DNS
│   │   ├── web_server.c                 # HTTP server + embeds
│   │   └── main.c                       # Entry, FreeRTOS tasks
│   ├── components/
│   │   ├── captive_portal/              # Reusable captive portal
│   │   └── esp_websocket/               # WebSocket server
│   └── test/                            # Unity tests
│       ├── CMakeLists.txt
│       ├── test_pca9685.c
│       ├── test_servos.c
│       └── test_state.c
├── ui/                                  # Blazor WASM
│   ├── NukCPGDrop.sln
│   └── NukCPGDrop.Ui/
│       ├── NukCPGDrop.Ui.csproj
│       ├── Program.cs
│       ├── _Imports.razor
│       ├── Pages/
│       │   ├── Index.razor              # Landing page
│       │   └── Dashboard.razor          # Main control panel
│       ├── Components/
│       │   ├── DropButton.razor
│       │   ├── DifficultySelector.razor
│       │   ├── DropStatus.razor
│       │   └── LogoDisplay.razor
│       ├── Services/
│       │   ├── ApiService.cs            # REST client
│       │   └── StateService.cs          # Local state
│       ├── Models/
│       │   ├── DropConfiguration.cs
│       │   └── DropResult.cs
│       └── wwwroot/
│           ├── index.html
│           ├── css/app.css
│           ├── js/app.js
│           └── images/
│               └── nuks-logo.png        # PLACEHOLDER
├── tests/
│   ├── NukCPGDrop.Ui.Tests/            # bUnit + xUnit
│   │   ├── NukCPGDrop.Ui.Tests.csproj
│   │   └── Components/
│   │       └── DropButtonTests.cs
│   └── NukCPGDrop.Firmware.Tests/       # QEMU test runner
│       └── README.md
├── scripts/
│   ├── embed-web.py                     # .wasm → C header
│   └── setup-hooks.ps1                  # Pre-commit setup
├── .github/workflows/
│   ├── ci.yml                           # Full CI pipeline
│   ├── firmware-tests.yml               # QEMU firmware tests
│   └── ui-tests.yml                     # .NET test suite
├── .gitignore
├── .pre-commit-config.yaml
├── .editorconfig
├── Directory.Build.props
├── NUKCPGDROP_PLAN.md
├── README.md
└── LICENSE
```

---

## 3. Hardware / Pinout

| Signal | ESP32-S3 GPIO | Notes |
|---|---|---|
| PCA9685 SDA | GPIO8 | I2C with DMA enabled |
| PCA9685 SCL | GPIO9 | I2C with DMA enabled |
| Servo 1-6 CH | PCA9685 OUT0-5 | 50Hz PWM, 0.5-2.5ms pulse |
| Start button | GPIO0 | BOOT button, internal pullup |
| Status LED | GPIO10 | Indicator |
| WiFi Antenna | onboard | Internal PCB trace |

**PCA9685 wiring:**
- VCC → ESP32 3.3V (logic level)
- V+ → 5V rail (servo power, direct from USB)
- GND → common
- OE → GND (always enabled)

---

## 4. DMA PCA9685 Driver Design

The PCA9685 is an I2C device, not a native DMA peripheral. The DMA is used at the
**I2C controller level** — ESP32-S3's I2C master supports DMA descriptors for
non-blocking multi-buffer transfers.

### Driver Layers

```
┌──────────────────────────────┐
│  servos.c                    │  High-level: drop_can(n), set_all()
│  FreeRTOS task notifications │
├──────────────────────────────┤
│  pca9685.c                   │  Mid-level: write_channel(), write_batch()
│  Queue-based batch updates   │
│  I2C DMA descriptors         │
├──────────────────────────────┤
│  ESP-IDF I2C driver          │  Low-level: i2c_master_cmd_begin()
│  CONFIG_I2C_USE_DMA=y        │  with DMA descriptor chaining
└──────────────────────────────┘
```

### Key design decisions:

- **Batch updates**: When dropping two cans simultaneously, both PWM registers are
  written in a single I2C transaction (multi-buffer DMA chain).
- **Non-blocking**: `i2c_master_cmd_begin()` with `portMAX_DELAY` from a dedicated
  I2C task, or use async with callback/interrupt.
- **Register caching**: Shadow register values in RAM, only write changed channels.
- **Frequency**: PCA9685 prescaler set for 50Hz (20ms period) standard servo.
- **Pulse range**: 0.5ms (0°) to 2.5ms (180°) mapped to 12-bit PCA9685 values.

### Critical sections:

```c
// I2C configuration with DMA
i2c_config_t conf = {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = GPIO_NUM_8,
    .scl_io_num = GPIO_NUM_9,
    .sda_pullup_en = GPIO_PULLUP_ENABLE,
    .scl_pullup_en = GPIO_PULLUP_ENABLE,
    .master.clk_speed = 400000,  // 400kHz fast mode
};
i2c_param_config(I2C_NUM_0, &conf);
i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, ESP_INTR_FLAG_LEVEL3);
// DMA enabled via sdkconfig: CONFIG_I2C_USE_DMA=y
```

```c
// Batch write to multiple servo channels
// Uses I2C DMA for non-blocking multi-register write
esp_err_t pca9685_write_batch(pca9685_t *dev, uint8_t *channels,
                               uint16_t *values, uint8_t count) {
    uint8_t data[2 + count * 2];
    data[0] = PCA9685_LED0_ON_L + channels[0] * 4;
    // Build contiguous register block
    for (int i = 0; i < count; i++) {
        data[2 + i * 2] = values[i] & 0xFF;
        data[2 + i * 2 + 1] = (values[i] >> 8) & 0xFF;
    }
    return i2c_master_write_to_device(I2C_NUM_0, dev->addr,
                                      data, 2 + count * 2,
                                      pdMS_TO_TICKS(10));
}
```

---

## 5. FreeRTOS Task Layout

| Task | Stack | Priority | Function |
|---|---|---|---|
| `main` | 4096 | 1 | init, then idle/power mgmt |
| `wifi_task` | 4096 | 3 | WiFi AP, mDNS, DNS server |
| `http_task` | 8192 | 2 | HTTP server, WebSocket |
| `drop_task` | 4096 | 4 | Drop sequence execution |
| `i2c_task` | 2048 | 3 | I2C DMA transactions |

**Timers used:**
- **FreeRTOS software timer**: Drop interval timer (difficulty-based)
- **ESP Timer** (`esp_timer`): High-precision drop timing (microsecond accuracy)
- **I2C timeout**: Hardware I2C timeout via `i2c_set_timeout()`

**Synchronization primitives:**
- Queue: `drop_queue` — commands from UI to drop task
- Mutex: `state_mutex` — protect NVS state
- Task notification: I2C completion notifies drop task

---

## 6. State Persistence (NVS)

All state stored in ESP-IDF Non-Volatile Storage under the `nukcpgdrop` namespace.

| Key | Type | Default | Description |
|---|---|---|---|
| `difficulty` | u8 | 1 | 0=Long, 1=Short, 2=Random |
| `double_drop` | bool | false | Drop two at a time |
| `drop_count` | u32 | 0 | Lifetime drop counter |
| `last_sequence` | blob | - | Last drop order for brownout recovery |

**Brownout recovery:**
1. On boot, check `last_sequence` in NVS
2. If incomplete sequence exists and `drop_count < 6`, offer resume option
3. Otherwise, start fresh

```c
typedef struct {
    uint8_t difficulty;        // 0, 1, 2
    bool     double_drop;
    uint32_t drop_count;
    uint8_t  last_sequence[6]; // shuffled order
    uint8_t  last_completed;   // index of last completed drop
} __attribute__((packed)) nukcpgdrop_state_t;
```

---

## 7. Web UI — Blazor WASM

### Build & Embed Pipeline

```
dotnet publish -c Release -o publish/
scripts/embed-web.py publish/wwwroot/  →  firmware/main/include/web_assets.h
```

`embed-web.py`:
- Compresses all files (gzip or brotli)
- Generates a C header with `{path, data, size, compressed}` tuples
- ESP HTTP server serves compressed files with proper Content-Encoding

```python
# scripts/embed-web.py (conceptual)
for path in walk(root):
    data = read(path)
    compressed = gzip.compress(data)
    mime = mimetypes.guess_type(path)
    emit(f"{{ \"{relative}\", {len(data)}, {len(compressed)},"
         f" \"{mime}\", gz_{varname} }}")
```

### UI Features

- **Dashboard**: Shows 6 can slots with status (Held/Dropped), next drop timer
- **Drop All**: Triggers full sequence
- **Manual drop**: Tap individual can to drop
- **Logo**: Placeholder `<img src="/images/nuks-logo.png">` for the Nuks logo
- **Difficulty selector**: Long / Short / Random tabs
- **Double-drop toggle**: If on, two cans drop simultaneously (pairs)
- **Responsive**: CSS Grid + `clamp()` for font sizing, `@media` orientation queries

### REST API Endpoints

| Method | Path | Description |
|---|---|---|
| GET | `/api/status` | Current system state |
| POST | `/api/drop` | Start drop sequence |
| POST | `/api/drop/{id}` | Drop specific can (1-6) |
| POST | `/api/config` | Update configuration |
| WS | `/ws` | Real-time status updates |

### Screen Scaling

- Use `viewport`, CSS `rem`/`clamp()` for all sizing
- CSS `@media (orientation: landscape)` / `(orientation: portrait)`
- Blazor's `IJSRuntime` to detect zoom via `window.devicePixelRatio`
- Touch events handled via standard pointer events

---

## 8. WiFi / Captive Portal

### Boot sequence:
1. ESP32 boots in AP mode (SSID: `NukCPGDrop-XXXX`, key from MAC)
2. mDNS responder: `nukcpgdrop.local`
3. DNS server catches all UDP/53 queries → responds with 192.168.4.1
4. HTTP server on :80 serves Blazor WASM
5. Any device that connects and opens browser sees the captive portal login
   window automatically (iOS/Android detect on connect, or they just browse)

### Implementation:
```c
// mDNS
esp_err_t start_mdns(void) {
    mdns_init();
    mdns_hostname_set("nukcpgdrop");
    mdns_instance_name_set("NukCPGDrop Rig");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}

// DNS: catch all A-record queries → ESP IP
static esp_err_t dns_catch_all(uint8_t *query, uint8_t *response, size_t *len) {
    // Respond with 192.168.4.1 to any A-record query
}
```

---

## 9. Testing Strategy

### Layer 1: Unit Tests (firmware)
- **Framework**: ESP-IDF Unity Test
- **Scope**: `test_pca9685.c`, `test_servos.c`, `test_state.c`
- **Run**: `idf.py test` (native on host via QEMU for ESP32-S3)
- **Coverage**: 100% branch + line enforced via gcov

### Layer 2: Unit Tests (UI)
- **Framework**: xUnit + bUnit
- **Scope**: Component rendering, API service mocking, state logic
- **Mutation testing**: Stryker.NET — enforce 100% mutation score
- **Run**: `dotnet test` in CI
- **Coverage**: `dotnet-cobertura` → report, enforced in PRs

### Layer 3: Integration
- **QEMU**: `esp32s3` machine target for full firmware tests
  - Test WiFi AP mode
  - Test DNS captive portal
  - Test I2C driver (loopback)
- **Playwright**: UI integration tests against the WASM app

### Pre-commit hooks:
```
pre-commit:
  - repo: local
    hooks:
      - id: clang-format     # C code formatting
      - id: dotnet-format     # C# code formatting
      - id: dotnet-test       # Run all .NET tests
      - id: idf-test          # Run firmware tests (if QEMU available)
      - id: stryker           # Mutation testing (staged, fast subset)
      - id: lint              # ESLint/quick lint for JS
```

### CI Pipeline (`.github/workflows/ci.yml`):
```
push/PR:
  - firmware-build (esp-idf)
  - firmware-lint (clang-tidy + clang-format)
  - firmware-test (QEMU + Unity + gcov → 100% coverage)
  - ui-build (dotnet build)
  - ui-test (dotnet test + bUnit)
  - ui-mutation (Stryker → 100% mutation score)
  - ui-lint (dotnet format --verify-no-changes)
  - publish-ui (dotnet publish)
  - embed-web (python embed-web.py)
  - firmware-build-with-ui (idf.py build, includes embedded web)
```

---

## 10. Bill of Materials

| Item | Qty | Est. Cost | Source |
|---|---|---|---|
| ESP32-S3-DevKit-C N8R2 | 1 | ~$12 | Amazon |
| PCA9685 16-ch PWM module | 1 | ~$5 | Amazon |
| SG90 9g micro servo | 6 | ~$12 | Amazon (6-pack) |
| N52 neodymium disc 10x3mm | 6 | ~$8 | Amazon |
| Tactile button | 1 | ~$1 | Amazon |
| Project box (printed/ABS) | 1 | ~$14 | Amazon/Printed |
| USB power bank 10000mAh | 1 | ~$15 | Already have |
| 1000µF 16V electrolytic cap | 1 | ~$1 | Amazon |
| **Total** | | **~$68** | |

---

## 11. Implementation Order

1. **Phase 0** — Repo setup, gitignore, pre-commit, CI scaffolding
2. **Phase 1** — ESP-IDF project skeleton: build, flash, UART hello
3. **Phase 2** — PCA9685 DMA driver: I2C init, single/batch write
4. **Phase 3** — Servo control: calibrate, drop/release moves
5. **Phase 4** — WiFi AP + mDNS + DNS captive portal
6. **Phase 5** — State persistence (NVS) with brownout recovery
7. **Phase 6** — Blazor WASM UI: dashboard, controls, logo
8. **Phase 7** — HTTP server + embed pipeline + serve WASM from ESP
9. **Phase 8** — Drop sequence scheduler with FreeRTOS timers
10. **Phase 9** — All tests: unit, integration, mutation
11. **Phase 10** — Physical build: mount servos, magnets, test drops
12. **Phase 11** — Polish: UI animations, captive portal UX, branding

---

## 12. Key Risks & Mitigations

| Risk | Mitigation |
|---|---|
| I2C DMA conflicts with other peripherals | Reserve I2C_NUM_0; check pin mux |
| Servo jitter from power rail noise | 1000µF cap on 5V rail; separate servo ground |
| Brownout during multi-drop (peak current) | Bulk capacitance; staggered servo move if needed |
| WASM files too large for flash | Gzip compression; use SPIFFS partition for assets |
| Touchscreen captive portal not appearing on all devices | Provide instruction card with URL |
| SG90 not strong enough to peel magnet off | Test with actual can weight; use MG90S if needed |
