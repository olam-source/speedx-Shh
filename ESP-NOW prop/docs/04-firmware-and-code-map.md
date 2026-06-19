# 4 · Firmware & Code Map

[← Mesh Protocol](03-mesh-protocol.md) · [Wiki home](README.md) · Next: [Sensors & Metrics →](05-sensors-and-metrics.md)

---

A guided tour of every file, so a new developer knows where to look.

## Shared library — `include/` (header-only)

### [`espnow_config.h`](../include/espnow_config.h)
The contract. Constants (channel, TTL, gossip tuning, node ids), the
`MessageType` enum, the packed `ESPNowMessage` frame, and the typed payloads
(`AthleteMetricsPayload`, `CoachCommandPayload`, `SensorDataPayload`). The
`static_assert`s here are the wire-format guardrails.

### [`espnow_utils.h`](../include/espnow_utils.h)
The `ESPNowUtils` helper class — all the boilerplate:
- `initWiFi()` / `initESPNow()` — bring up the radio in STA mode on the fixed channel.
- `addPeer()` / `ensurePeer()` / `addBroadcastPeer()` — peer table management.
- `parseMessage()` / `setPayload()` / `messageSize()` — safe (de)serialization with length checks.
- `createPingMessage()` / `createAthleteMetricsMessage()` / `createCoachCommandMessage()` / … — frame builders.
- `readAthleteMetrics()` / `readCoachCommand()` — typed payload extraction.
- `printAthleteMetricsJson()` — the **USB line format the dashboard parses**.

### [`peer_manager.h`](../include/peer_manager.h)
`PeerManager` — a fixed-size registry (`ESPNOW_MAX_PEERS = 20`) of who we've
heard, their MAC, id, RSSI, last-seen, message count. Handles registration,
lookup by id/MAC, activity updates, and timeout eviction. No dynamic allocation.

### [`athlete_sensors.h`](../include/athlete_sensors.h)
`AthleteSensors` — the hardware abstraction with two compile-time backends
(simulated vs. real MAX30102/MPU-6050/GPS/battery). Covered in detail in
[Sensors & Metrics](05-sensors-and-metrics.md).

## Node firmware — `src/`

### [`coach_hub/main.cpp`](../src/coach_hub/main.cpp) — the USB sink (Node 250)
- `handleAthleteMetrics()` — record into the roster, stamp link quality, **print
  JSON to USB**, ACK the sender.
- `findOrAllocateAthlete()` / roster — latest sample per athlete (up to 32).
- `sendBeacon()` — periodic discovery broadcast.
- `pollSerialConsole()` / `handleSerialCommand()` — accept coach commands typed
  on USB (or sent by the dashboard): `start`, `stop`, `lap`, `reset`, `identify`.
- `broadcastCoachCommand()` — flood a `COACH_COMMAND` to the team.
- `printRosterSnapshot()` — periodic `{"t":"hub",...}` heartbeat line.

### [`athlete_wearable/main.cpp`](../src/athlete_wearable/main.cpp) — a wearable
- `readAthleteMetrics()` — builds a sample; delegates sensor fields to `AthleteSensors`.
- `gossipMessage()` — the fan-out/unicast/dedupe logic from
  [Mesh Protocol](03-mesh-protocol.md).
- `sendOwnMetrics()` — sample + gossip every `ATHLETE_METRIC_INTERVAL_MS`.
- `handleAthleteMetrics()` — relay other athletes' frames toward the hub.
- `handleCoachCommand()` — apply start/stop/lap/reset/identify, then re-flood.
- `onDataReceived()` — the ESP-NOW receive callback (handles both Arduino-ESP32
  2.x and 3.x signatures).

`ATHLETE_NODE_ID` / `ATHLETE_TEAM_ID` come from build flags, so the *same source*
builds every athlete — see [`../platformio.ini`](../platformio.ini).

### `node_sender` / `node_receiver` / `node_mesh`
The original ESP-NOW framework demos (PING/PONG discovery, sensor DATA+ACK, TTL
mesh relay). Useful for radio bring-up; not part of the athlete system.

## Host — `dashboard/`

### [`coach_dashboard.html`](../dashboard/coach_dashboard.html)
Self-contained page (HTML + CSS + vanilla JS). Uses the **Web Serial API** to
read the hub's USB stream, parses each JSON line, and renders a live card per
athlete with HR zones, splits, pace, battery, link quality, and coach-control
buttons. No build step, no dependencies. See
[Build, Flash & Dashboard](07-build-flash-and-dashboard.md).

## Control flow cheat-sheet

```
setup():  initWiFi → initESPNow → register callbacks → addBroadcastPeer
          (+ athlete: sensors.begin();  + hub: first beacon)

loop():   athlete: sensors.update(); every 2s sendOwnMetrics(); periodic beacon-discovery; peer timeout
          hub:     pollSerialConsole(); periodic beacon; roster snapshot; peer timeout

on RX:    parseMessage → register/refresh peer → dedupe → dispatch by type
```

## Conventions

- **Header-only, no heap.** Fixed-size arrays everywhere; safe for a long-running
  embedded device.
- **One frame builder per message type** in `ESPNowUtils` — never hand-pack a frame.
- **Receive path is uniform** across nodes: parse → register peer → dedupe →
  switch on type. Adding a message type means adding an enum value, a builder, a
  reader, and a `case`.
