# 7 · Build, Flash & Dashboard

[← Hardware & Budget](06-hardware-and-budget.md) · [Wiki home](README.md) · Next: [Manufacturing & Roadmap →](08-manufacturing-and-roadmap.md)

---

## Toolchain

Built with **PlatformIO** (Arduino framework on the Espressif32 platform). Verified
with PlatformIO Core 6.1.19 + espressif32 7.0.1 (Arduino-ESP32 3.x).

```bash
# Install PlatformIO (any of these)
pip install platformio          # into a venv is cleanest
# or: brew install platformio
# or use the PlatformIO IDE extension in VS Code
```

First build downloads the ESP32 toolchain (~hundreds of MB), then it's cached.

## Build environments

Defined in [`../platformio.ini`](../platformio.ini):

| Env | Builds | Notes |
|-----|--------|-------|
| `coach_hub` | the USB hub | default env |
| `athlete1` | simulated wearable | no sensors needed; set the real id over USB with `id <n>` |
| `athlete1_hw` | **real-sensor wearable** | `-DUSE_REAL_SENSORS` + `lib_deps` (MAX3010x, TinyGPSPlus, MPU6050) |

> **Board id:** `esp32doit-devkit-v1`. (The original `esp32doit-devkit1` was
> invalid and broke every build — fixed.)

## Build & flash

```bash
# Compile
pio run -e coach_hub
pio run -e athlete1            # simulated
pio run -e athlete1_hw         # real sensors

# Flash over USB (board connected)
pio run -e coach_hub   -t upload
pio run -e athlete1_hw -t upload

# Serial monitor
pio device monitor -e athlete1_hw
```

Set `upload_port` / `monitor_port` per board in `platformio.ini`. Common names:
`/dev/cu.SLAB_USBtoUART`, `/dev/ttyUSB0`, `COM3`.

### Adding athletes — one binary for the whole team ⭐

Athlete IDs are set **at runtime over USB** and stored in NVS, so you flash the
*same* binary to every wearable and assign identities afterward — no per-athlete
compile. After flashing, open a serial monitor (115200) and type:

```
id 12        # set this wearable's athlete id (1–249); saves + reboots
team 1       # set its team (0–255)
info         # print current id / team / MAC
reset-id     # revert to the build-flag default
```

The build-flag `ATHLETE_NODE_ID` is now just the *default* if nothing is stored.
The legacy per-athlete envs (`athlete2`, `athlete3`, …) still work, but you no
longer need them — flash `athlete1` (or `athlete1_hw`) to every board and run
`id N` once each. The mesh discovers new IDs automatically.

### Resource use (reference build)

`coach_hub`: Flash ~56%, RAM ~14%. `athlete1_hw`: Flash ~60%, RAM ~14%. Plenty
of headroom on a 4 MB ESP32.

### Over-the-air firmware updates (no router)

A flashed wearable can update itself wirelessly — no re-cabling. In the USB
console (or any time at the bench) type `ota`. The node ([`ota_portal.h`](../include/ota_portal.h)):

1. switches its radio into a Wi-Fi access point named `SpeedX-OTA-<id>`
   (password `speedxota`),
2. serves an upload page at `http://192.168.4.1/`.

Join that Wi-Fi from a phone/laptop, open the page, upload a new `firmware.bin`
(from `.pio/build/<env>/firmware.bin`), and the board flashes it and reboots.
While the OTA portal is up the node leaves the mesh until it restarts.

### Encryption passphrase

Frames are AES-256-GCM encrypted by default. **Set your team's passphrase** in
`platformio.ini` (`[env]` → `-DESPNOW_PSK_PASSPHRASE="..."`) before flashing the
fleet — every node must share it. See [Mesh Protocol → Security](03-mesh-protocol.md#security--payload-encryption).

## Two ways for the coach to view the team

| Client | Best for | Athlete names? |
|--------|----------|----------------|
| **SpeedX Coach desktop app** (recommended) | Day-to-day coaching; an installable app | ✅ roster: node id → name + team, saved to disk |
| **Browser dashboard** (`dashboard/coach_dashboard.html`) | Quick check / no install / any laptop | ❌ shows athletes by node id only |

Both read the **same** USB JSON stream from the hub — pick whichever fits.

## SpeedX Coach desktop app ⭐

A cross-platform **Electron** app in its own repo:
**[github.com/olam-source/speedx-coach-app](https://github.com/olam-source/speedx-coach-app)**.
It bundles Chromium, so the USB serial reading works the same on macOS, Windows,
and Linux — no "open it in Chrome" caveat — and it adds **athlete roster
management**.

### Run it

```bash
git clone https://github.com/olam-source/speedx-coach-app
cd speedx-coach-app
npm install
npm start            # or: npm run dist  -> .dmg / .exe / .AppImage
```

Click **Connect Hub**, choose the hub's port from the in-app chooser.

### Athlete management (the headline feature)

- **Add / edit / remove athletes** in the sidebar, mapping a **node ID** (1–249)
  to a **name** and **team**. Validates uniqueness and reserved ids (250/255).
- **Persisted** to `userData/roster.json`, so the roster survives restarts.
- **Pre-register** athletes before a session — they appear as *offline* cards
  until their wearable reports.
- Live node IDs not on the roster show as "Node N · Unassigned" with a one-click
  **+ add to roster**.
- Same coach controls (Start / Stop / Mark Lap / Reset Best) as the browser
  dashboard.

> The wearable firmware already keys everything by **node ID** (set per athlete
> via the `ATHLETE_NODE_ID` build flag). The app's roster is the friendly-name
> layer on top — no firmware change is needed to name or add athletes.

## The browser dashboard (lightweight alternative)

[`../dashboard/coach_dashboard.html`](../dashboard/coach_dashboard.html) — a
single self-contained page. Full notes in
[`../dashboard/README.md`](../dashboard/README.md).

### Run it

1. Flash `coach_hub`, plug it into the laptop.
2. Open `coach_dashboard.html` in **Chrome or Edge** (Web Serial isn't in
   Safari/Firefox). `file://` works.
3. Click **Connect Hub**, pick the serial port (115200 baud).
4. Athlete cards stream in live.

> Close any other serial monitor first — only one program can hold the port.

### What it shows

Per athlete: HR with training-zone color (Z1–Z5), pace/mi, current & best split,
distance, cadence, stride, ground-contact, speed, skin temp, link %, hop count,
battery, and GPS/lap/low-battery badges. Cards dim when an athlete goes stale
(~12 s no data).

### Coach controls

Buttons send commands back down the mesh: **Start** (zero session), **Stop**,
**Mark Lap**, **Reset Best**. Same as typing `start` / `stop` / `lap` / `reset`
into the hub's USB console.

## The USB data format

The hub emits one JSON object per line — easy to parse anywhere (browser, Python,
Grafana, a CSV logger):

```json
{"t":"athlete","id":11,"team":1,"sample":42,"uptime_ms":84000,"hr":154,
 "pace_s":362,"split_s":361,"best_split_s":349,"dist_m":336,"speed_cms":430,
 "cadence":176,"stride_cm":145,"gct_ms":0,"vosc_cm":9,"elev_m":1,
 "skin_c10":335,"batt_mv":3870,"signal":78,"flags":3,"hops":1,"ttl":3}
```

Plus periodic `{"t":"hub",...}` heartbeat lines (uptime, mesh peers, active
athletes).
