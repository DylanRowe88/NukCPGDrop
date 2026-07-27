# Firmware QEMU Tests

ESP32-S3 firmware tests run under QEMU using the `esp32s3` machine target.

## Prerequisites

- QEMU with ESP32-S3 support (`qemu-system-xtensa` or `qemu-system-esp32`)
- ESP-IDF v5.x with Unity test framework
- `idf.py` in PATH

## Run Tests

```bash
cd firmware
idf.py test
```

## Test Coverage

- `test_pca9685.c` — I2C register read/write, batch mode, frequency calc
- `test_servos.c` — state transitions, batch drops, boundary checks
- `test_state.c` — NVS save/load, sequence storage, interval calc

Coverage (gcov) enforced at 100% branch + line in CI.
