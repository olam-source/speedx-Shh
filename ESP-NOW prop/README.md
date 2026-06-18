# SpeedX — Router-Free Athlete Tracking Mesh

SpeedX turns a pile of ESP32 boards into a self-healing **ESP-NOW mesh of
athlete wearables**. Each wearable tracks heart rate and track stats (mile pace,
splits, distance, cadence, stride, ground-contact time, and more) and gossips
those readings node-to-node across the team. There is **no central router** —
frames hop between athletes until they reach a single **coach hub** (a USB
ESP-NOW adapter) plugged into the coach's laptop, where every athlete's vitals
appear on one live screen.

```
   (Athlete 11) <-> (Athlete 12) <-> (Athlete 13) <-> ...any number
         \             |               /
          \         gossip            /   (relayed hop-by-hop,
           \          |              /      no Wi-Fi router)
                  [ Coach Hub ] --USB--> Coach laptop --> Web dashboard
                  (mesh sink)                             (one screen)
```

The mesh is built on a reusable ESP-NOW framework (one packet format, dynamic
peer discovery, ACK/PONG, TTL/hop forwarding, duplicate suppression, safe
payload sizing for ESP-NOW's 250-byte frame limit).

## Project Structure

```
ESP-NOW prop/
├── include/
│   ├── espnow_config.h        # Message types, packet + payload formats, constants
│   ├── espnow_utils.h         # WiFi, ESP-NOW, payload, send, and print helpers
│   └── peer_manager.h         # Dynamic peer registry and timeout tracking
├── src/
│   ├── athlete_wearable/      # Body-worn node: reads metrics, gossips them to the hub
│   ├── coach_hub/             # Central USB adapter: mesh sink + dashboard bridge
│   ├── node_sender/           # Generic demo node (optional)
│   ├── node_receiver/         # Generic demo node (optional)
│   └── node_mesh/             # Generic mesh relay demo (optional)
├── dashboard/
│   ├── coach_dashboard.html   # One-screen live dashboard (Web Serial, no install)
│   └── README.md
├── platformio.ini
└── README.md
```

## How the mesh works

1. **Wearables sample metrics** every 2 s into a compact `AthleteMetricsPayload`.
2. Each sample is sent as an `ATHLETE_METRICS` frame addressed to the coach hub,
   flagged for **gossip**.
3. **Gossip forwarding**: if a node knows a direct route to the hub it unicasts;
   otherwise it fans the frame out to a few random peers (`GOSSIP_FANOUT`) or
   broadcasts. Every node keeps a small cache to drop duplicates and a TTL/hop
   count to kill loops, so a frame ripples across the team until it reaches the
   hub — no access point required.
4. The **coach hub** (`COACH_HUB_NODE_ID = 250`) beacons so nearby wearables can
   discover it and unicast directly, records the latest sample per athlete,
   acknowledges frames, and streams everything to USB as line-delimited JSON.
5. The **dashboard** reads that USB stream in the browser and renders a live card
   per athlete. Coach buttons send `COACH_COMMAND` frames back down the same
   mesh (start/stop session, mark lap, reset best split).

Add athletes by flashing more wearables with unique IDs; the mesh discovers them
automatically. Range scales with the team because every athlete is also a relay.

## Node Roles

### Athlete Wearable (`athlete_wearable`)

- Samples heart rate + running dynamics every `ATHLETE_METRIC_INTERVAL_MS` (2 s).
  The sample function is the single place to wire in real sensors (HR strap,
  IMU, GPS) — it currently produces realistic simulated data.
- Gossips its own metrics toward the coach hub and **relays** other athletes'
  frames, so coverage grows with the squad.
- Tracks its own best split and session distance.
- Obeys coach commands relayed through the mesh (start/stop/lap/reset/identify)
  and re-floods them so the whole team stays in sync.

### Coach Hub (`coach_hub`)

- The **central USB ESP-NOW adapter** — the mesh sink. Plug it into the coach's
  computer; no router, no Wi-Fi join.
- Maintains a roster of every athlete's latest metrics.
- Emits one JSON line per metric for the dashboard, plus periodic hub-status
  lines (uptime, mesh peers, active athletes).
- Accepts coach commands typed on the USB console **or** pushed by the dashboard
  and broadcasts them to the team.

### Generic demo nodes (optional)

`node_sender`, `node_receiver`, and `node_mesh` are the original framework
examples (PING/PONG discovery, sensor DATA + ACK, and a TTL mesh relay). They
share the same headers and are handy for bring-up and protocol testing.

## Message Protocol

`ESPNowMessage` is a packed frame capped at one ESP-NOW v1 packet (250 bytes):

```cpp
struct ESPNowMessage {
    MessageType type;      // PING/PONG/DATA/ACK/MESH_*/ATHLETE_*/COACH_COMMAND
    uint8_t sender_id;
    uint8_t receiver_id;   // COACH_HUB_NODE_ID (250) or ESPNOW_BROADCAST_ID (255)
    uint8_t sequence;
    uint32_t timestamp;
    uint8_t rssi;
    uint16_t data_length;  // actual payload bytes sent (variable length)
    uint8_t ttl;           // hop budget for gossip/forwarding
    uint8_t hop_count;
    uint8_t flags;         // ACK_REQUESTED / FORWARDED / GOSSIP / URGENT
    uint8_t payload[MAX_PAYLOAD_SIZE];
};
```

Athlete telemetry payload:

```cpp
struct AthleteMetricsPayload {
    uint8_t  athlete_id, team_id;
    uint16_t sample_id;
    uint32_t uptime_ms;
    uint16_t heart_rate_bpm;
    uint16_t pace_sec_per_mile, current_split_sec, best_split_sec;
    uint16_t distance_m, speed_cm_s, cadence_spm, stride_length_cm;
    uint16_t ground_contact_ms, vertical_oscillation_cm;
    int16_t  elevation_gain_m, skin_temp_c_x10;
    uint16_t battery_mv;
    int8_t   signal_percent;
    uint8_t  reserved;
    uint16_t flags;        // GPS_LOCK / HEART_RATE_VALID / SPLIT_MARKER / LOW_BATTERY
};
```

Coach command payload:

```cpp
struct CoachCommandPayload {
    uint8_t  command;        // CoachCommand: START/STOP/MARK_LAP/RESET_BEST/IDENTIFY
    uint8_t  target_athlete; // athlete_id, or COACH_TARGET_ALL (0xFF)
    uint16_t param;
    uint32_t issued_ms;
};
```

Key constants (`espnow_config.h`):

```cpp
ESPNOW_CHANNEL = 1;            COACH_HUB_NODE_ID = 250;
ESPNOW_DEFAULT_TTL = 4;        ESPNOW_MAX_PEERS = 20;
ATHLETE_METRIC_INTERVAL_MS = 2000;
GOSSIP_FANOUT = 3;            GOSSIP_CACHE_SIZE = 32;
```

## Build & Flash

Environments are defined in `platformio.ini`. Give each athlete board a **unique
`ATHLETE_NODE_ID`** (set via build flags in the env) and update the
`upload_port` / `monitor_port` for your machine.

```bash
# Coach hub (the USB adapter on the coach's laptop)
pio run -e coach_hub -t upload

# One env per athlete board — duplicate the block to add more runners
pio run -e athlete1 -t upload     # ATHLETE_NODE_ID=11
pio run -e athlete2 -t upload     # ATHLETE_NODE_ID=12
pio run -e athlete3 -t upload     # ATHLETE_NODE_ID=13
```

Watch a node's serial output:

```bash
pio device monitor -e athlete1
```

Common ports: `/dev/cu.SLAB_USBtoUART`, `/dev/ttyUSB0`, `/dev/ttyACM0`, `COM3`.

## Quick Start

1. Flash one board with `coach_hub`; leave it plugged into the coach's laptop.
2. Flash each athlete board with `athlete1`, `athlete2`, … (unique IDs).
3. Power on the wearables — they self-discover and start gossiping.
4. Open `dashboard/coach_dashboard.html` in Chrome/Edge, click **Connect Hub**,
   and pick the hub's serial port. Athlete cards stream in live.
   *(Close any other serial monitor first — the port allows one reader.)*

See [`dashboard/README.md`](dashboard/README.md) for the dashboard details and
the JSON line format if you want to log or chart the data elsewhere.

## Coach Commands

From the dashboard buttons, or typed into the hub's USB serial console:

```
start            # zero distance/splits, begin a fresh session for everyone
stop             # pause metric sampling
lap [number]     # force a split marker on the next sample
reset            # clear each athlete's best split
identify [id]    # ask a wearable (or all) to announce itself
cmd <n> <t> <p>  # raw form: command number / target id / param
```

## Adding an Athlete

1. Pick a unique `ATHLETE_NODE_ID` (any value except 250 and 255).
2. Copy an `[env:athleteN]` block in `platformio.ini`, bump the ID, and set the
   board's ports.
3. Flash it. The hub and peers discover it automatically; a new card appears on
   the dashboard.

## Wiring Real Sensors

`readAthleteMetrics()` in `src/athlete_wearable/main.cpp` is the only place that
fabricates data. Replace the simulated fields with reads from your hardware
(e.g. a MAX30102/HR strap for `heart_rate_bpm`, an IMU for cadence/stride/GCT, a
GPS for distance/pace/splits). The frame format, mesh, hub, and dashboard need
no changes.

## Troubleshooting

- Every node must use the same `ESPNOW_CHANNEL`.
- Flash the correct PlatformIO env per board, and give each athlete a unique ID.
- If the dashboard won't connect, use Chrome/Edge and close other serial
  monitors holding the port.
- No athletes on screen? Confirm wearables print metric lines on their own
  serial, and that the hub prints `{"t":"athlete",...}` lines.
- Mesh traffic looping or noisy? Lower `ESPNOW_DEFAULT_TTL` or improve direct
  hub discovery (move the hub closer to at least one athlete).

## References

- [ESP-NOW Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)
- [PlatformIO ESP32 Platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)
- [Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)

## License

Provided as-is for educational and development purposes.
