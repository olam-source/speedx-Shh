# 1 · Overview

[← Wiki home](README.md) · Next: [System Architecture →](02-architecture.md)

---

## The problem

Coaches want live heart rate and running stats for **every athlete at once** —
during track workouts, cross-country, or team runs. Existing options are bad for
a team:

- **GPS watches / chest straps** report to *each athlete's own phone/app*, not a
  single coach view, and pairing 30 of them is a nightmare.
- **Stadium telemetry systems** are expensive and assume infrastructure.
- **Wi-Fi / cellular** needs a router or SIM per device and dies the moment
  athletes spread out across a field or trail.

## The idea

Give each athlete a cheap ESP32 wearable that reads their vitals and **passes
them athlete-to-athlete** over ESP-NOW (a connectionless, router-free radio
protocol built into every ESP32). Frames hop across the team until they reach a
**coach hub** — an ESP32 on a USB cable plugged into the coach's laptop — which
shows everyone on one screen.

```
 (Athlete) <-> (Athlete) <-> (Athlete) <-> ...   gossip, no router
                     |
                [ Coach Hub ] --USB--> laptop --> one-screen dashboard
```

## Why this approach

- **No infrastructure.** No router, no SIM, no cloud, no per-device account.
- **Range grows with the team.** Every wearable is also a relay, so coverage
  extends as you add athletes instead of degrading.
- **Self-healing.** If an athlete moves out of range of one peer, their data
  finds another path. Lose a node and the mesh routes around it.
- **Cheap & buildable.** ~$20–25 per wearable from off-the-shelf modules; see
  [Hardware & Budget](06-hardware-and-budget.md).
- **One screen, zero install.** The coach opens an HTML file in Chrome/Edge — no
  app, no setup. See [Build, Flash & Dashboard](07-build-flash-and-dashboard.md).

## What it tracks

Heart rate, pace per mile, current & best mile split, distance, speed, cadence,
stride length, ground-contact time, vertical oscillation, elevation gain, skin
temperature, battery, and radio link quality. What's measured vs. estimated is
spelled out in [Sensors & Metrics](05-sensors-and-metrics.md).

## Who it's for

- **Track / XC / running coaches** who want real-time team telemetry.
- **Makers / students** building an end-to-end IoT mesh project.
- **Sports-tech tinkerers** who want a hackable, open platform instead of a
  closed wearable ecosystem.

## Current status

The firmware is complete and compiles to flashable ESP32 binaries (verified with
PlatformIO + the ESP32 toolchain). It runs today in **simulation mode** on a bare
ESP32, and the **real-sensor build** is validated against the actual vendor
libraries. Remaining work is physical: attach sensors, build enclosures, field
test. See [Manufacturing & Roadmap](08-manufacturing-and-roadmap.md).
