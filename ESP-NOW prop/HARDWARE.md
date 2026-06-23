# SpeedX Hardware Guide

Cheap, off-the-shelf, fully buildable parts. Every wearable is identical, so the
system scales by simply building and flashing more of them — the mesh discovers
new athletes automatically. Approximate prices are USD from AliExpress / generic
sellers (2024-ish); brand-name versions cost more.

## Compatibility

The SpeedX firmware runs on **any ESP32-family board** (classic ESP32, ESP32-S2,
S3, or C3) because the mesh only uses ESP-NOW + USB serial. The sensor layer
(`include/athlete_sensors.h`) targets the parts below; change the pin `-D` flags
if you wire them differently. If you already own an ESP32 board, you can reuse
it — you only need to add the sensors.

---

## Per-Athlete Wearable

| Part | Suggested model | Why | ~Price |
|------|-----------------|-----|--------|
| MCU + battery mgmt | **LOLIN D32** (ESP32 + onboard LiPo charger + battery ADC on GPIO35) | One board does compute, charging, and battery sensing — fewer parts to wire | $6–9 |
| Heart rate | **MAX30102** optical PPG module (I2C) | Cheapest buildable HR; reads via SparkFun MAX3010x lib | $2–4 |
| Motion / cadence | **MPU-6050** 6-axis IMU (I2C) | Cadence, stride estimate, vertical oscillation | $1–3 |
| GPS | **u-blox NEO-6M** w/ antenna (UART) | Distance, pace, mile splits; 1 Hz fixes | $6–9 |
| Battery | **1S LiPo** (3.7 V, 500–1000 mAh) or 18650 Li-ion | Powers the wearable for hours | $3–6 |
| Strap / case | Velcro armband or 3D-printed clip + elastic | Mount on upper arm or waistband | $1–5 |

**Wearable subtotal: ~$25–45** depending on GPS choice and battery.

### Options
- **Upgrade to NEO-M8N** (~$15) for 10 Hz fixes → finer pace and split timing.
  Same wiring and `-DGPS_BAUD=9600` (default); no firmware change.
- Replace the LOLIN D32 with a plain **ESP32 DevKit V1** (~$5) plus a **TP4056
  USB-C charger** (~$1) and a 2×100 kΩ divider on a spare ADC pin for battery
  sensing (set `-DPIN_BATTERY_ADC=<pin>`).

### Accuracy upgrade (optional)
- **Polar H10 / any BLE chest strap** gives far better HR while running than a
  wrist/arm optical sensor. It needs BLE on the ESP32 (coexists with ESP-NOW but
  adds complexity); not wired in this firmware yet — optical MAX30102 is the
  default for buildability.
- **Foot-pod IMU** (a second MPU-6050 on the shoe) is required for true
  **ground-contact time**. With only an arm/torso IMU, `ground_contact_ms` is
  reported as 0 and stride/vertical-oscillation are estimates.

---

## Coach Hub (one per team)

| Part | Suggested model | Why | ~Price |
|------|-----------------|-----|--------|
| MCU | **ESP32 DevKit V1** (or any ESP32) | Plugs into the coach's laptop over USB; no sensors | $5–8 |
| Cable | USB micro/USB-C data cable | Power + serial to the dashboard | $1 |

**Hub subtotal: ~$6–9.** No battery or sensors needed.

---

## Wiring (per wearable)

Defaults assume a LOLIN D32 / classic ESP32. All values are overridable with
build flags (see `include/athlete_sensors.h`).

### I2C bus — MAX30102 and MPU-6050 share it
| Signal | ESP32 pin | Flag |
|--------|-----------|------|
| SDA | GPIO21 | `-DPIN_I2C_SDA=` |
| SCL | GPIO22 | `-DPIN_I2C_SCL=` |
| VCC | 3V3 | — |
| GND | GND | — |

(MAX30102 = I2C addr `0x57`, MPU-6050 = `0x68`; no conflict.)

### GPS — UART2
| Signal | Connection | Flag |
|--------|-----------|------|
| GPS TX → ESP32 RX2 | GPIO16 | `-DPIN_GPS_RX=` |
| GPS RX ← ESP32 TX2 | GPIO17 | `-DPIN_GPS_TX=` |
| VCC | 3V3 (or 5V per module) | — |
| GND | GND | — |

### Battery sense
| Signal | ESP32 pin | Flag |
|--------|-----------|------|
| Battery via 2×100 kΩ divider | GPIO35 | `-DPIN_BATTERY_ADC=`, `-DBATTERY_DIVIDER=` |

> The LOLIN D32 already has this divider to GPIO35, so no external parts are
> needed on that board.

---

## Build & flash

```bash
# Coach hub (USB on the laptop)
pio run -e coach_hub -t upload

# Real-hardware wearable (pulls in sensor libraries automatically)
pio run -e athlete1_hw -t upload      # ATHLETE_NODE_ID=11

# Bench-test with no sensors attached (simulated metrics)
pio run -e athlete1 -t upload
```

To add athletes, copy the `[env:athlete1_hw]` block in `platformio.ini`, give it a
**unique `ATHLETE_NODE_ID`** (anything except 250 = hub and 255 = broadcast), set
its ports, and flash. The `lib_deps` are fetched by PlatformIO on first build.

## What each metric comes from

| Metric | Source | Quality |
|--------|--------|---------|
| Heart rate | MAX30102 PPG + beat detection | good at rest, noisier at hard effort |
| Distance / pace / speed | NEO-6M GPS | good outdoors with sky view |
| Mile splits / best split | derived from GPS distance + timer | good |
| Elevation gain | GPS altitude deltas | rough (GPS altitude is noisy) |
| Cadence | IMU step detection | good |
| Stride length | distance ÷ steps | estimate |
| Vertical oscillation | IMU vertical-accel amplitude | estimate |
| Ground-contact time | needs foot-mounted IMU | not measured (reports 0) |
| Skin temp | MAX30102 die temperature | approximate |
| Battery | ADC voltage divider | good |
| Link quality | ESP-NOW RSSI (added by the hub) | good |
