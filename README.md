# NukCPGDrop

Timed drop rig for HomeGymCon — 6 servo-controlled N52 magnets dropping Nuks cans
in random sequence, driven by ESP32-S3 with a Blazor WebAssembly captive portal UI.

## Quick Start

```bash
git clone https://github.com/YOUR_USER/NukCPGDrop.git
cd NukCPGDrop

# Setup pre-commit hooks
scripts/setup-hooks.ps1

# Build firmware
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py flash monitor

# Build UI (standalone dev)
cd ui/NukCPGDrop.Ui
dotnet run
```

## Architecture

```
ESP32-S3 (WiFi AP)
  ├── mDNS: nukcpgdrop.local
  ├── DNS:  captive portal (all → 192.168.4.1)
  ├── HTTP: serves Blazor WASM + REST API
  ├── I2C:  PCA9685 via DMA (GPIO8/9)
  └── GPIO: 6x SG90 servos + magnets
```

See [NUKCPGDROP_PLAN.md](NUKCPGDROP_PLAN.md) for full details.

## License

MIT
