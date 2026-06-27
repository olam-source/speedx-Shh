# SpeedX — Router-Free Athlete Tracking Mesh

This repo holds the SpeedX ESP32 firmware, dashboard, and docs for the
router-free athlete mesh. The actual PlatformIO project lives in
`ESP-NOW prop/`.

Wearables relay athlete telemetry over ESP-NOW to a USB coach hub. The hub
streams one JSON object per line to either the browser dashboard in this repo or
the separate Electron coach app:

- Browser dashboard: `ESP-NOW prop/dashboard/coach_dashboard.html`
- Coach app: https://github.com/olam-source/speedx-coach-app

## What's in here

- `ESP-NOW prop/include/` — shared protocol, crypto, and sensor headers
- `ESP-NOW prop/src/` — athlete wearable and coach hub firmware
- `ESP-NOW prop/dashboard/` — zero-install browser dashboard
- `ESP-NOW prop/docs/` — project wiki / source-of-truth docs
- `ESP-NOW prop/HARDWARE.md` — parts list and wiring
- `ESP-NOW prop/platformio.ini` — build environments

## Current state

- Simulation build runs on a bare ESP32.
- Real-sensor build uses MAX30102, MPU-6050, GPS, and battery ADC.
- Wearables can be assigned IDs at runtime over USB (`id <hex>`, `team <n>`).
- The coach hub emits line-delimited JSON over USB for dashboards and loggers.

## Build

```bash
cd "ESP-NOW prop"
pio run -e coach_hub
pio run -e athlete1
pio run -e athlete1_hw
```

```bash
pio device monitor -e athlete1_hw
```

## Docs

- `ESP-NOW prop/docs/README.md`
- `ESP-NOW prop/dashboard/README.md`
- `ESP-NOW prop/HARDWARE.md`
