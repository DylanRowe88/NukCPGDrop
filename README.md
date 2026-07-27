# NukCPGDrop

Timed drop rig for HomeGymCon — 6 servo-controlled N52 magnets dropping Nuks cans
in random sequence, driven by ESP32-S3 with a Blazor WebAssembly captive portal UI.

## Quick Start

```bash
# Build UI + embed + flash
python flash.py

# Or step by step:
dotnet publish ui/NukCPGDrop.Ui/ -c Release
python scripts/embed-web.py publish/wwwroot/ firmware/main/include/web_assets.h
cd firmware && idf.py build && idf.py -p COM3 flash monitor
```

## Architecture

```
ESP32-S3 (WiFi AP) → serves Blazor WASM via HTTP + REST API
                  → PCA9685 I2C (GPIO8/9) → 6x SG90 servos + N52 magnets
                  → captive portal (DNS catch-all, mDNS: nukcpgdrop.local)
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for full design, CI/CD pipeline, and deployment.

## License

MIT
