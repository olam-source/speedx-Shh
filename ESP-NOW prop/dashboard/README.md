# SpeedX Coach Dashboard

A single-screen, zero-install dashboard for live athlete telemetry. It reads the
coach hub's USB serial stream directly in the browser using the
[Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)
and renders one live card per athlete.

## Run it

1. Flash an ESP32 with the `coach_hub` environment and plug it into your laptop.
2. Open `coach_dashboard.html` in **Chrome or Edge** (Web Serial is not in Safari/Firefox).
   - Open from `file://` or serve locally — both work.
3. Click **Connect Hub** and pick the hub's serial port (115200 baud).
4. Athlete cards appear as soon as wearables start gossiping metrics to the hub.

> Close any other serial monitor (e.g. `pio device monitor`) first — only one
> program can hold the port at a time.

## What you see per athlete

- Heart rate with live training-zone coloring (Z1–Z5)
- Pace per mile, current split, best split, total distance
- Cadence, stride length, ground-contact time, speed, skin temp
- Link quality (RSSI %), hop count to the hub, battery level
- Badges for GPS lock, lap markers, and low battery
- Cards dim automatically when an athlete goes stale (no data for ~12 s)

## Coach controls

The buttons send commands back down the mesh to every wearable:

| Button       | Effect on wearables                                  |
|--------------|------------------------------------------------------|
| ▶ Start      | Zero distance/splits and begin a fresh session       |
| ⏸ Stop       | Pause metric sampling                                 |
| ⚑ Mark Lap   | Force a split marker on the next sample               |
| ↺ Reset Best | Clear each athlete's stored best split                |

## Data format

The hub emits one JSON object per line over USB. The athlete record looks like:

```json
{"t":"athlete","id":11,"team":1,"sample":42,"uptime_ms":84000,"hr":154,
 "pace_s":362,"split_s":361,"best_split_s":349,"dist_m":336,"speed_cms":430,
 "cadence":176,"stride_cm":145,"gct_ms":215,"vosc_cm":9,"elev_m":1,
 "skin_c10":335,"batt_mv":3870,"signal":78,"flags":3,"hops":1,"ttl":3}
```

Periodic `{"t":"hub",...}` lines carry hub uptime, mesh peer count, and active
athlete count. Because it is plain line-delimited JSON, you can pipe the same
stream into Python, a CSV logger, Grafana, etc. instead of (or alongside) this
page.
