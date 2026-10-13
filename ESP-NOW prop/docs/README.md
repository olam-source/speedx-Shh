# SpeedX System Docs

Primary architecture target:

```text
Garmin watches -> 4-6 Raspberry Pi edge nodes -> router -> coach laptop app
```

This folder now documents the migration from the original ESP32/ESP-NOW prototype
to the Pi + Garmin stack.

## Core pages

| # | Page | Purpose |
|---|---|---|
| 1 | [Overview](01-overview.md) | Product goals and migration direction |
| 2 | [System Architecture](02-architecture.md) | Pi node topology, router stream, packet contract |

## Legacy pages

Pages `03` through `08` still describe the previous ESP-NOW firmware baseline and
are retained for reference during migration.
