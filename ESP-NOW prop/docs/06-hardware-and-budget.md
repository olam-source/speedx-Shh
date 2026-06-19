# 6 · Hardware & Budget

[← Sensors & Metrics](05-sensors-and-metrics.md) · [Wiki home](README.md) · Next: [Build, Flash & Dashboard →](07-build-flash-and-dashboard.md)

---

> Canonical wiring tables and pin-override flags live in
> [`../HARDWARE.md`](../HARDWARE.md). This page is the **purchasing & budget**
> view: where to buy and how the numbers scale to a team.

## Compatibility

The firmware runs on **any ESP32-family board** (it only uses ESP-NOW + USB
serial). The wearable's recommended board is the **LOLIN D32**, which integrates
LiPo charging, a low-dropout 3.3 V regulator suited to a single cell, and a
battery-sense divider on GPIO35 — so it removes the charger + regulator + divider
you'd otherwise add to a bare DevKit.

## Where to buy (per component)

| Part | Budget source | Genuine/fast source |
|------|---------------|---------------------|
| LOLIN D32 (ESP32) | [AliExpress](https://www.aliexpress.com/item/32883116057.html) | [Amazon](https://www.amazon.com/ESP-WROOM-32-Micropython-Compatible-Electronic-Performance/dp/B0F9KKFW8R) · [PMD Way](https://pmdway.com/products/lolin-d32-esp32-development-board) |
| MAX30102 (HR) | AliExpress bulk | [Amazon MakerFocus](https://www.amazon.com/MakerFocus-Heart-Rate-MAX30102-Compatible-Arduino/dp/B07GGSM8SP) |
| MPU-6050 (IMU) | AliExpress bulk | [Amazon HiLetgo](https://www.amazon.com/HiLetgo-MPU-6050-Accelerometer-Gyroscope-Converter/dp/B078SS8NQV) |
| GPS NEO-6M (budget) | AliExpress "NEO-6M GPS" | Amazon |
| GPS NEO-M8N (better) | [eBay](https://www.ebay.com/itm/205935080949) · [Maker&Coder](https://makerandcoder.com/product/u-blox-neo-m8n-gps-module-with-antenna-compass/) | [Mouser](https://www.mouser.com/ProductDetail/u-blox/NEO-M8N-0?qs=zW32dvEIR3unZhZI0KRbew%3D%3D) |
| Battery (18650 / 1S LiPo) | local (lithium air-ship limits) | Amazon |
| Coach hub: ESP32 DevKit V1 | AliExpress | Amazon |

> **Clones are fine.** The Amazon/AliExpress MAX30102, MPU-6050, and "NEO-M8N"
> are compatibles — exactly what these prices assume. First-party parts from
> SparkFun/Adafruit/Mouser cost 2–4× and are only worth it if you want
> guaranteed-authentic silicon.

## Budget build — 30 wearables under $800

Target: 30 athletes + 1 hub, production cost **< $800**. Achieved by using the
**NEO-6M** (the single biggest lever) and cheap battery/case, while keeping the
LOLIN D32 (avoids the bare-DevKit power problem).

| Part | Budget pick | ~Unit | ×30 |
|------|-------------|-------|-----|
| MCU + power | LOLIN D32 (standard) | $7.00 | $210 |
| Heart rate | MAX30102 | $2.00 | $60 |
| IMU | MPU-6050 | $1.20 | $36 |
| GPS | NEO-6M + antenna | $6.00 | $180 |
| Battery | 1S LiPo 1000 mAh / 18650 | $2.50 | $75 |
| Strap / case | armband or printed clip | $1.50 | $45 |
| Wires / JST / headers | — | $0.60 | $18 |
| **Per wearable** | | **$20.80** | **$624** |

**Coach hub:** ESP32 DevKit V1 ($4) + USB cable ($1) = **$5**

**Components ≈ $629** · +10% spares (~$62) · +batched shipping (~$30–60) →
**≈ $720–750 all-in. Under $800. ✅**

No firmware changes are needed for the NEO-6M — the UART driver and the
`GPS_BAUD=9600`, `PIN_BATTERY_ADC=35`, `BATTERY_DIVIDER=2.0` defaults already
match this build.

## Levers if you want more margin

| Lever | Effect |
|-------|--------|
| **Drop GPS** (derive distance from IMU steps / lap counting) | **−~$180** → ~$450 build; coarser splits. Needs a `-DNO_GPS` firmware path (not yet built). Viable since track teams run a measured 400 m oval. |
| **Upgrade to NEO-M8N** | +~$180; 10 Hz fixes, race-grade splits. Still fits if you trim elsewhere. |
| **Source batteries locally** | Avoids lithium air-ship delays/customs; ~+$15–75 across 30, still under budget. |

## Accuracy upgrades (optional)

- **BLE chest strap** (e.g. Polar H10) → far better HR while running. Needs BLE
  on the ESP32 (coexists with ESP-NOW, adds complexity); not wired yet.
- **Foot-pod IMU** → enables true ground-contact time (currently reported as 0).

## The one real budget risk

Lithium cells often can't air-ship cheaply overseas and can stall in customs. The
safe move: buy **everything except batteries** from AliExpress in bulk, and buy
**cells domestically**. Build that into your timeline.
