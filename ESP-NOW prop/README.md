# ESP-NOW Network for ESP32

A complete PlatformIO/Arduino ESP-NOW framework for ESP32 nodes. The project includes a sender, receiver, and mesh relay example that share one packet format, dynamic peer discovery, ACK/PONG replies, peer timeout tracking, and safe payload sizing for ESP-NOW's 250-byte frame limit.

## Project Structure

```
ESP-NOW prop/
├── include/
│   ├── espnow_config.h        # Message types, packet format, constants
│   ├── espnow_utils.h         # WiFi, ESP-NOW, payload, and send helpers
│   └── peer_manager.h         # Dynamic peer registry and timeout tracking
├── src/
│   ├── node_sender/main.cpp   # Node 1: discovers Node 2 and sends sensor data
│   ├── node_receiver/main.cpp # Node 2: receives data and replies with ACK/PONG
│   └── node_mesh/main.cpp     # Node 3: beacons, tracks peers, forwards traffic
├── platformio.ini
└── README.md
```

## What Is Finished

- Packet size is capped at one ESP-NOW frame.
- Messages are sent with variable length instead of always sending a padded struct.
- Broadcast peer setup is automatic.
- Nodes add peers dynamically from inbound packets before replying.
- Sender discovery no longer requires a hard-coded receiver MAC.
- Receiver validates payload sizes and acknowledges DATA/MESH_DATA.
- Mesh node sends beacons, suppresses duplicates, and forwards messages with TTL/hop tracking.
- PlatformIO environments build one node entry point at a time.

## Node Roles

### Node 1: Sender

- Broadcasts discovery PING frames for Node 2.
- Learns Node 2's MAC from PONG/ACK replies.
- Sends `SensorDataPayload` to Node 2 every 5 seconds.
- Uses unicast after discovery and broadcast while the target is unknown.
- Prints peer status every 5 seconds.

### Node 2: Receiver

- Listens for PING, DATA, MESH_DATA, ACK, and beacon frames.
- Dynamically registers the sender or mesh peer.
- Responds to PING with PONG.
- Validates sensor payloads and replies to DATA/MESH_DATA with ACK.
- Ignores messages addressed to other node IDs.

### Node 3: Mesh

- Broadcasts mesh beacons every 10 seconds.
- Registers peers seen through any valid message.
- Handles messages addressed to Node 3 locally.
- Forwards messages for other node IDs to a known destination MAC or by broadcast.
- Uses TTL, hop count, and a small duplicate cache to reduce loops.

## Message Protocol

`ESPNowMessage` is a packed 250-byte maximum frame:

```cpp
struct ESPNowMessage {
    MessageType type;
    uint8_t sender_id;
    uint8_t receiver_id;
    uint8_t sequence;
    uint32_t timestamp;
    uint8_t rssi;
    uint16_t data_length;
    uint8_t ttl;
    uint8_t hop_count;
    uint8_t flags;
    uint8_t payload[MAX_PAYLOAD_SIZE];
};
```

Important constants:

```cpp
constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint8_t ESPNOW_BROADCAST_ID = 0xFF;
constexpr uint8_t ESPNOW_DEFAULT_TTL = 4;
constexpr size_t ESPNOW_MAX_FRAME_SIZE = 250;
constexpr size_t MAX_PAYLOAD_SIZE = 236;
```

Sensor data uses a typed payload:

```cpp
struct SensorDataPayload {
    float temperature_c;
    uint16_t humidity_percent;
    uint16_t battery_mv;
};
```

## Build Environments

The PlatformIO file exposes one environment per node:

```ini
[env:esp32_node1]  # sender
[env:esp32_node2]  # receiver
[env:esp32_mesh]   # mesh relay
```

Build them from the project directory:

```bash
pio run -e esp32_node1
pio run -e esp32_node2
pio run -e esp32_mesh
```

Upload examples:

```bash
pio run -e esp32_node1 -t upload
pio run -e esp32_node2 -t upload
pio run -e esp32_mesh -t upload
```

Monitor serial output:

```bash
pio device monitor -e esp32_node1
```

Update the `monitor_port` and `upload_port` entries in `platformio.ini` for your machine. Common port names are `/dev/cu.SLAB_USBtoUART`, `/dev/ttyUSB0`, `/dev/ttyACM0`, and `COM3`.

## Startup Flow

1. Flash Node 2 with `esp32_node2`.
2. Flash Node 1 with `esp32_node1`.
3. Node 1 broadcasts a PING addressed to Node 2.
4. Node 2 registers Node 1 and replies with PONG.
5. Node 1 sends future sensor packets to Node 2 by unicast.
6. Optional mesh nodes can be flashed with `esp32_mesh`; they beacon and forward traffic for other IDs.

All nodes print their MAC address on boot:

```text
Device MAC: 34:85:18:12:34:56
```

The MAC is useful for diagnostics, but the sender/receiver path does not require copying it into source code.

## Extending The Network

To add another node:

1. Pick a unique `NODE_ID`.
2. Add a PlatformIO environment with a source filter for the new node directory.
3. Use `ESPNowUtils::createDataMessage()` or `createMeshDataMessage()` for payloads.
4. Call `ESPNowUtils::parseMessage()` in receive callbacks.
5. Register inbound peers with `PeerManager::registerPeer()` and `updatePeerActivity()`.

## Troubleshooting

- Confirm every node uses the same `ESPNOW_CHANNEL`.
- Build the correct PlatformIO environment for the node role.
- If replies fail, check that inbound peers are being printed in the serial monitor.
- If a node is quiet, verify the board and upload ports in `platformio.ini`.
- If mesh traffic loops, lower `ESPNOW_DEFAULT_TTL` or increase direct discovery between nodes.

## References

- [ESP-NOW Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)
- [PlatformIO ESP32 Platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)

## License

This project is provided as-is for educational and development purposes.
