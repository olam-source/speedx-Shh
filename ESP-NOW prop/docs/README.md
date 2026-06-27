# SpeedX Firmware Docs

> Router-free athlete-tracking mesh. ESP32 wearables read heart rate and running
> stats, relay them node-to-node, and hand them to a USB coach hub for live
> display.

This folder is the firmware project’s developer knowledge base. It is written as
linked pages so it reads well on GitHub and imports cleanly into Notion
(*Import → Markdown & CSV*, drop the whole `docs/` folder).

---

## 📚 Pages

| # | Page | What's in it |
|---|------|--------------|
| 1 | [Overview](01-overview.md) | The problem, the one-screen solution, who it's for |
| 2 | [System Architecture](02-architecture.md) | Node roles, data flow, the big picture |
| 3 | [Mesh Protocol](03-mesh-protocol.md) | ESP-NOW frame format, gossip, TTL/dedup, discovery |
| 4 | [Firmware & Code Map](04-firmware-and-code-map.md) | Every file, key functions, how the code is organized |
| 5 | [Sensors & Metrics](05-sensors-and-metrics.md) | Sim vs real hardware, what each metric comes from |
| 6 | [Hardware & Budget](06-hardware-and-budget.md) | BOM, where to buy, the 30-unit budget build |
| 7 | [Build, Flash & Dashboard](07-build-flash-and-dashboard.md) | Toolchain, `pio` commands, the coach dashboard |
| 8 | [Manufacturing & Roadmap](08-manufacturing-and-roadmap.md) | Producing it in bulk, status, what's next |

---

## ⚡ At a glance

| | |
|---|---|
| **What** | Wearable athlete telemetry over a self-healing ESP-NOW mesh |
| **No** | Wi-Fi router, cell connection, or paid infrastructure |
| **Scales to** | Any number of athletes — each wearable is also a relay |
| **Coach view** | Desktop app (with athlete roster) **or** a zero-install browser screen |
| **Status** | Firmware complete & compiles to flashable ESP32 binaries; runs in simulation today, real-sensor build validated against vendor libraries |
| **Firmware repo** | [github.com/olam-source/speedx-Shh](https://github.com/olam-source/speedx-Shh) |
| **Coach app repo** | [github.com/olam-source/speedx-coach-app](https://github.com/olam-source/speedx-coach-app) |

## 🔗 Source-of-truth files (outside this wiki)

- [`../README.md`](../README.md) — repo landing page
- [`../HARDWARE.md`](../HARDWARE.md) — canonical bill of materials & wiring
- [`../platformio.ini`](../platformio.ini) — build environments
- [`../dashboard/coach_dashboard.html`](../dashboard/coach_dashboard.html) — the dashboard

> **Keep one source of truth.** Code facts live in the source files; this docs
> set explains and links to them. When firmware changes, update the page that
> points at it rather than copying code in.
