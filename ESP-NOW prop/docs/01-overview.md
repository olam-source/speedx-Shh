# 1 · Overview (Pi + Garmin Migration)

[← Wiki home](README.md) · Next: [System Architecture →](02-architecture.md)

---

## New target system

SpeedX is moving to a distributed ingest system built around Garmin watches and
Raspberry Pis:

```text
Garmin watches -> 4-6 Raspberry Pi edge nodes -> field router -> coach laptop app
```

## Why this redesign

1. **Use Garmin-native metrics directly.** Pull watch telemetry instead of
   rebuilding a full wearable stack.
2. **Scale in zones.** Multiple Pi nodes cover different parts of the field and
   collapse traffic into one router uplink.
3. **Coach-first workflow.** One app view for all athletes with effort, pace,
   cadence, splits, distance, GPS, and other available Garmin running stats.

## System goal

- Support **any number of Garmin watches**.
- Keep each sample tagged to an athlete/watch identity.
- Stream data to the coach app as line-delimited JSON for live rendering and
  recording/export.

## Transition note

The old ESP-NOW implementation remains in this repository as a legacy prototype.
New development should target the Pi + Garmin architecture described in this doc
set.
