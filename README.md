# SpeedX — Raspberry Pi + Garmin Telemetry System

SpeedX is being migrated to a field architecture where **4-6 Raspberry Pi edge nodes** collect Garmin watch telemetry, tag samples by athlete/watch, and stream them to a router that forwards to the coach laptop app.

```text
Garmin watches -> Pi edge nodes (4-6) -> router -> SpeedX Coach app
```

## Current repository role

This repo currently contains the original ESP32/ESP-NOW prototype plus docs.
The new system architecture and migration direction are documented in:

- `ESP-NOW prop/docs/01-overview.md`
- `ESP-NOW prop/docs/02-architecture.md`
- `ESP-NOW prop/docs/README.md`
- `pi-gateway/README.md` (starter router + Pi edge-node services)

## Coach app

The coach app now supports a **router TCP stream** for the new architecture:

- https://github.com/olam-source/speedx-coach-app

## Notes

- Existing ESP-NOW firmware remains in this repo as a legacy baseline while Pi/Garmin services are rolled out.
- Telemetry format is line-delimited JSON, including Garmin-style payload support in the app.
