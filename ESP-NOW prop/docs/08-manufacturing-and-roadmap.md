# 8 · Manufacturing & Roadmap

[← Build, Flash & Dashboard](07-build-flash-and-dashboard.md) · [Wiki home](README.md)

---

## Getting units built

You don't have to assemble 30 wearables by hand. Two routes:

### Route A — 1 test unit + small runs (start here)
Hire a freelancer/maker to assemble the off-the-shelf modules, flash the
firmware, test, and ship. Validate before committing to a batch.
- **Fiverr / Upwork / Freelancer.com** — many ESP32 hardware-prototyping gigs.
- **PCBWay's gig marketplace** lists engineers for assembly + programming.
- Typical labor: **~$50–200** per assembled+flashed unit, plus parts.
- Uses the current breakout-module design **as-is** — no custom PCB.

### Route B — bulk / production (30+ and scaling)
Turnkey PCBA houses source parts, assemble, and can **flash your firmware** as a
programming/test step if you supply the `.bin`:
- **[JLCPCB](https://jlcpcb.com/pcb-assembly)** — cheapest small-batch turnkey.
- **PCBWay** — reliable with ESP32 modules in the BOM.
- **[Seeed Fusion](https://www.seeedstudio.com/pcb-assembly.html)** /
  **[Makerfabs](https://www.makerfabs.com/one-stop-pcb-assembly-service)** —
  more hand-holding / DFA review.

Two sub-options:
- **B1 (low upfront):** assembler integrates off-the-shelf modules into
  enclosures + flashes. Keeps the ~$800 budget; higher labor per unit.
- **B2 (proper, scalable):** commission a **custom PCB** integrating
  ESP32-WROOM + MAX30102 + MPU-6050 + GPS + LiPo charging. One-time NRE
  (~$300–1000 first run), lower per-unit after, the long-term standard.

## Manufacturing handoff package

Any of the above needs these to quote and build (firmware is the easy part —
it's done and compiles to a flashable binary):

1. **BOM** with manufacturer / LCSC part numbers.
2. **Wiring / pinout** (and a schematic for B2) — see [`../HARDWARE.md`](../HARDWARE.md).
3. **Firmware binaries** (`firmware.bin` for athlete + hub) + the `esptool`
   flash command and offsets, or a merged single-file image.
4. **Functional-test checklist**: powers on → prints MAC → joins mesh → shows on
   dashboard.

> Ask me to generate this package and I'll drop it into a `manufacturing/` folder
> with the built `.bin`s and flashing instructions.

## Honest status

| Area | State |
|------|-------|
| Mesh protocol + gossip | ✅ Complete |
| Coach hub + USB JSON stream | ✅ Complete |
| Browser dashboard (one screen, live, controls) | ✅ Complete |
| Coach desktop app + athlete roster management | ✅ Complete ([speedx-coach-app](https://github.com/olam-source/speedx-coach-app)) |
| Coach commands end-to-end | ✅ Complete |
| Runtime athlete-ID provisioning (one binary for all wearables) | ✅ Complete |
| Frame encryption (AES-256-GCM) | ✅ Complete |
| OTA firmware updates (SoftAP web upload) | ✅ Complete |
| Coach app: session recording + CSV export, low-batt/HR-zone alerts | ✅ Complete |
| Coach app: per-athlete history + charts, team filter, auto-update | ✅ Complete |
| Simulated metrics | ✅ Complete, runs on bare ESP32 |
| Real-sensor firmware | ✅ Compiles & links against vendor libs |
| **On-hardware validation** | ⬜ Needs physical boards + sensors |
| Enclosure / mechanical | ⬜ Not started |
| Field testing (range, battery life, HR accuracy) | ⬜ Not started |

## Roadmap / next steps

**Near term**
- Build 1 test unit (Route A); confirm live HR/GPS on real hardware.
- Measure ESP-NOW range and battery life in the field.
- Tune HR beat detection and cadence thresholds against ground truth.

**Medium term**
- `-DNO_GPS` build path (IMU/lap-based distance) for the ~$450 budget tier.
- Enclosure + armband design (3D-printable).
- Optional host-side logger (Python) to record sessions to CSV alongside the
  live dashboard.

**Longer term**
- Custom integrated PCB (Route B2) for a rugged, scalable wearable.
- BLE chest-strap HR option; foot-pod IMU for ground-contact time.
- Coach app: configurable alert thresholds and persistent session history
  across restarts (live per-athlete charts, team filter, alerts, CSV export, and
  auto-update already ship; installers are built by CI).

## Known limitations (carry these forward honestly)

- Optical HR is noisier at hard running effort; ground-contact time isn't
  measured without a foot pod (see [Sensors & Metrics](05-sensors-and-metrics.md)).
- ESP-NOW range is line-of-sight; very spread-out athletes rely on relays in
  between — plan team size/spacing accordingly.
- Lithium-battery shipping can stall a bulk order; source cells locally.
