# 5 · Sensors & Metrics

[← Firmware & Code Map](04-firmware-and-code-map.md) · [Wiki home](README.md) · Next: [Hardware & Budget →](06-hardware-and-budget.md)

---

All sensor logic lives in one place:
[`../include/athlete_sensors.h`](../include/athlete_sensors.h). The wearable
firmware never talks to hardware directly — it calls three methods:

```cpp
sensors.begin();                 // setup()
sensors.update();                // every loop() iteration
sensors.fillMetrics(payload);    // when building a frame
// + onSessionStart() / markLap() / resetBest() for coach commands
```

## Two backends, chosen at build time

| Build | Flag | Behavior |
|-------|------|----------|
| **Simulated** (default) | *(none)* | Generates realistic fake metrics. Runs on a bare ESP32 with **no sensors** — perfect for testing the mesh and dashboard. |
| **Real hardware** | `-DUSE_REAL_SENSORS` | Reads MAX30102 + MPU-6050 + GPS + battery ADC. Pulls vendor libraries via `lib_deps`. |

The frame format, mesh, hub, and dashboard are **identical** in both modes — only
where the numbers come from changes. This means you develop and demo the entire
system in simulation, then flip one flag for hardware.

## The sensors (real mode)

| Sensor | Interface | Provides |
|--------|-----------|----------|
| **MAX30102** | I2C (`0x57`) | Heart rate (PPG beat detection), skin temperature |
| **MPU-6050** | I2C (`0x68`) | Cadence (step detection), stride estimate, vertical oscillation |
| **u-blox NEO-M8N / 6M** | UART2 | Distance, pace, speed, mile splits, elevation |
| **Battery** | ADC (GPIO35) | Pack voltage via divider |

Pins are `#define`d with defaults and overridable by build flag
(`-DPIN_I2C_SDA=`, `-DPIN_GPS_RX=`, `-DPIN_BATTERY_ADC=`, …). Wiring tables are in
[Hardware & Budget](06-hardware-and-budget.md).

## How each metric is derived

| Metric | Source | Quality |
|--------|--------|---------|
| Heart rate | MAX30102 PPG + `checkForBeat()` rolling average | good at rest, noisier at hard effort |
| Distance / pace / speed | GPS fixes + `distanceBetween()` | good outdoors with sky view |
| Route / position (`lat_e7`, `lon_e7`) | raw GPS lat/lon ×1e7; dashboard draws the path | good outdoors; 0/0 when no fix |
| Mile splits / best split | GPS distance crossing mile marks + timer | good |
| Elevation gain | sum of positive GPS-altitude deltas | rough (GPS altitude is noisy) |
| Cadence | IMU accel-magnitude peak detection | good |
| Stride length | distance ÷ steps over a window | estimate |
| Vertical oscillation | IMU vertical-accel peak-to-peak | estimate |
| **Ground-contact time** | needs a **foot-mounted** IMU | **not measured — reports 0** |
| Skin temp | MAX30102 die temperature | approximate |
| Battery | ADC × divider ratio | good |
| Link quality (signal %) | ESP-NOW RSSI, stamped by the hub | good |

> **Be honest with users about this table.** Optical wrist/arm HR and a torso
> IMU have real limits. The two upgrade paths — a BLE chest strap for HR and a
> foot-pod IMU for ground-contact time — are noted in
> [Hardware & Budget](06-hardware-and-budget.md).

## Session state & coach commands

The sensor layer owns session-relative state so coach commands behave correctly:

- `onSessionStart()` — zero distance/splits, clear best split (START_SESSION).
- `markLap()` — force a split marker on the next sample (MARK_LAP).
- `resetBest()` — clear the stored best split (RESET_BEST).
- `STOP_SESSION` pauses sampling in the wearable's `loop()`.

## Adding or swapping a sensor

1. Add the read in `update()` (real path) and a value in `fillReal()`.
2. If it's a new metric, add a field to `AthleteMetricsPayload`
   ([config](../include/espnow_config.h)) and to `printAthleteMetricsJson()`
   ([utils](../include/espnow_utils.h)).
3. Add the field to the dashboard card.
4. Mirror a plausible value in `fillSimulated()` so sim mode still demos it.
