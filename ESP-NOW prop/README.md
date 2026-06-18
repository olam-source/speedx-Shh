# ESP-NOW Network for ESP32

A comprehensive ESP-NOW network framework for ESP32 development. This workspace provides a complete development environment for multi-node wireless communication using the ESP-NOW protocol.

## Overview

ESP-NOW is a fast wireless communication protocol that supports low-power devices. It allows for:

- **Low Latency**: Minimal delay between nodes
- **Low Power Consumption**: Efficient for battery-operated devices
- **Wide Coverage**: Works across standard WiFi channels
- **Simple Pairing**: No complex connection setup

## Project Structure

```
SpeedX/
├── include/                    # Header files
│   ├── espnow_config.h        # Core configuration and message types
│   ├── espnow_utils.h         # Utility functions for ESP-NOW operations
│   └── peer_manager.h         # Peer tracking and management
├── src/
│   ├── node_sender/           # Example sender node
│   │   └── main.cpp
│   ├── node_receiver/         # Example receiver node
│   │   └── main.cpp
│   └── node_mesh/             # Example mesh node with beacons
│       └── main.cpp
├── lib/                        # External libraries
├── docs/                       # Documentation
├── config/                     # Configuration files
├── platformio.ini             # Build configuration
└── README.md                  # This file
```

## Features

### Core Components

1. **ESPNowUtils**: Static utility class for common ESP-NOW operations
   - WiFi initialization
   - Peer management
   - Message sending/broadcasting
   - RSSI strength calculation
   - Pre-built message creation

2. **PeerManager**: Track and manage connected peers
   - Register peers dynamically
   - Monitor signal strength (RSSI)
   - Timeout detection
   - Peer statistics

3. **Message Types**: Six predefined message types
   - `PING/PONG`: Connection testing
   - `DATA`: Sensor data transmission
   - `CONFIG`: Configuration changes
   - `ACK`: Acknowledgment messages
   - `MESH_BEACON`: Network discovery
   - `MESH_DATA`: Mesh routing

### Example Nodes

#### Sender Node

- Sends sensor data periodically
- Sends to a single receiver
- Demonstrates data transmission and callbacks

#### Receiver Node

- Receives and processes messages
- Manages peer connections
- Sends acknowledgments
- Tracks message history

#### Mesh Node

- Broadcasts periodic beacons
- Detects network topology
- Forwards messages
- Scalable for multiple nodes

## Getting Started

### Prerequisites

- ESP32 development board(s)
- USB cable for flashing
- VS Code with PlatformIO extension
- macOS/Linux/Windows

### Installation

1. **Install PlatformIO**
   - Open VS Code
   - Go to Extensions → Search "PlatformIO IDE"
   - Install the extension

2. **Update Board Definitions**
   - Open the project in VS Code
   - PlatformIO will auto-detect and install ESP32 board support

3. **Configure MAC Addresses**
   - Edit `src/node_sender/main.cpp` line 12
   - Replace `RECEIVER_MAC` with actual receiver ESP32 MAC address
   - To find MAC address, flash any node and check serial output

### Building & Flashing

#### Using PlatformIO CLI

```bash
# Build sender node
pio run -e esp32_node1 -t build

# Flash sender node
pio run -e esp32_node1 -t upload

# Build receiver node
pio run -e esp32_node2 -t build

# Flash receiver node
pio run -e esp32_node2 -t upload

# Monitor serial output
pio device monitor -e esp32_node1
```

#### Using VS Code

1. Open Command Palette (`Cmd+Shift+P`)
2. Type "PlatformIO: Build" or "PlatformIO: Upload"
3. Monitor output with "PlatformIO: Monitor"

### Finding ESP32 MAC Address

Each example node prints its MAC address on startup:

```
Device MAC: 34:85:18:12:34:56
```

Use this address in the receiver configuration.

## Configuration

### platformio.ini

The `platformio.ini` file contains build profiles for different nodes:

```ini
[env:esp32_node1]      # Sender
[env:esp32_node2]      # Receiver
[env:esp32_mesh]       # Mesh node
```

**Edit the COM ports** to match your system:

- **macOS**: `/dev/cu.SLAB_USBtoUART`, `/dev/cu.usbserial-*`
- **Linux**: `/dev/ttyUSB0`, `/dev/ttyACM0`
- **Windows**: `COM3`, `COM4`

### espnow_config.h

Core configuration parameters:

```cpp
#define ESPNOW_CHANNEL 1              // WiFi channel (1-13)
#define MAX_PEERS 20                  // Maximum peer connections
#define MAX_PAYLOAD_SIZE 250          // Message payload size
```

## Message Protocol

### Message Structure

```cpp
struct ESPNowMessage {
    MessageType type;           // Message type (PING, DATA, etc)
    uint8_t sender_id;         // Sending node ID
    uint8_t receiver_id;       // Receiving node ID (0xFF = broadcast)
    uint8_t sequence;          // Sequence number for ordering
    uint32_t timestamp;        // Milliseconds since startup
    uint8_t rssi;              // Signal strength (RSSI)
    uint16_t data_length;      // Payload size
    uint8_t payload[250];      // Message data
};
```

### Example: Sending Sensor Data

```cpp
ESPNowMessage msg{};
msg.type = MessageType::DATA;
msg.sender_id = NODE_ID;
msg.receiver_id = 2;
msg.sequence = sequence++;
msg.timestamp = millis();
msg.data_length = 8;

// Pack sensor data
float temperature = 25.5;
uint16_t humidity = 60;
memcpy(msg.payload, &temperature, 4);
memcpy(msg.payload + 4, &humidity, 2);

// Send
ESPNowUtils::sendMessage(receiver_mac, msg);
```

## Peer Management

The `PeerManager` class tracks connected nodes:

```cpp
PeerManager manager;

// Register a peer
manager.registerPeer(mac_address, peer_id);

// Get peer by ID
PeerInfo* peer = manager.getPeerById(node_id);

// Update signal strength
manager.updatePeerRssi(mac_address, rssi_value);

// Check for timeouts
manager.checkPeerTimeout(30000);  // 30 second timeout

// Get active peer count
int count = manager.getActivePeerCount();
```

## Callbacks

### Send Callback

```cpp
void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("Message sent successfully");
    }
}

esp_now_register_send_cb(onDataSent);
```

### Receive Callback

```cpp
void onDataReceived(const uint8_t* mac, const uint8_t* data, int data_len) {
    ESPNowMessage* msg = (ESPNowMessage*)data;
    // Process message
}

esp_now_register_recv_cb(onDataReceived);
```

## Troubleshooting

### Board Not Detected

- Check USB cable connection
- Install CH340 drivers (common ESP32 boards use this)
- Verify port in `platformio.ini`

### Messages Not Received

- Ensure both boards are on the same WiFi channel (default: 1)
- Verify MAC addresses are correct
- Add peers using `ESPNowUtils::addPeer()`

### Compilation Errors

- Ensure Arduino framework is installed
- Check `platformio.ini` has correct ESP32 board definition
- Clean build: `pio run -e esp32_node1 -t clean`

### Serial Output Issues

- Try different baud rates (default: 115200)
- Use `pio device monitor --baud 115200`

## Advanced Features

### Encryption

Enable encryption for messages:

```cpp
// Set Primary Master Key (PMK)
uint8_t pmk[16] = {0x08, 0x09, 0x0a, ...};
esp_now_set_pmk(pmk);

// Enable encryption on peer
esp_now_peer_info_t peer{};
peer.encrypt = true;  // Enable encryption
esp_now_add_peer(&peer);
```

### Mesh Routing

The mesh node example demonstrates basic routing:

1. Each node broadcasts periodic beacons
2. Beacons contain peer count and node status
3. Messages are forwarded to non-matching receivers
4. Topology can be discovered from beacon data

### Custom Data Types

Extend the message protocol for your needs:

```cpp
struct CustomData {
    int32_t temperature;  // x 100 (25.5°C = 2550)
    uint16_t humidity;
    uint8_t battery_level;
};

// In your message:
msg.data_length = sizeof(CustomData);
CustomData* data = (CustomData*)msg.payload;
data->temperature = 2550;
```

## Performance Metrics

- **Message Latency**: < 1ms average
- **Range**: ~250m line-of-sight (depends on obstacles)
- **Power Consumption**: ~80mA transmit, ~20mA receive
- **Bandwidth**: Up to 1Mbps (practical: 100-200 kbps)
- **Payload Size**: 250 bytes max

## Additional Resources

- [ESP-NOW Getting Started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)
- [ESP32 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [Arduino ESP32 Core](https://github.com/espressif/arduino-esp32)
- [PlatformIO Documentation](https://docs.platformio.org/)

## License

This project is provided as-is for educational and development purposes.

## Contributing

To extend this framework:

1. Add new message types to `MessageType` enum
2. Create handler functions in `onDataReceived`
3. Implement peer communication patterns
4. Document new features

---

**Last Updated**: June 2026  
**ESP32 Core Version**: 2.0.x+  
**Arduino Framework**: Latest
