#ifndef ESPNOW_CONFIG_H
#define ESPNOW_CONFIG_H

#include <stdint.h>
#include <cstring>

// ===== ESP-NOW Configuration =====
#define ESPNOW_CHANNEL 1
#define ESPNOW_WIFI_MODE WIFI_STA
#define MAX_PEERS 20
#define MAX_PAYLOAD_SIZE 250

// ===== Message Types =====
enum class MessageType : uint8_t {
    PING = 0,
    PONG = 1,
    DATA = 2,
    CONFIG = 3,
    ACK = 4,
    ERROR = 5,
    MESH_BEACON = 6,
    MESH_DATA = 7
};

// ===== Message Structure =====
struct ESPNowMessage {
    MessageType type;
    uint8_t sender_id;
    uint8_t receiver_id;
    uint8_t sequence;
    uint32_t timestamp;
    uint8_t rssi;
    uint16_t data_length;
    uint8_t payload[MAX_PAYLOAD_SIZE];
};

// ===== Peer Information Structure =====
struct PeerInfo {
    uint8_t mac[6];
    uint8_t peer_id;
    int8_t rssi;
    uint32_t last_seen;
    uint16_t message_count;
    bool is_active;
};

// ===== Global Configuration =====
struct Config {
    uint8_t node_id;
    uint8_t channel;
    bool use_encryption;
    uint8_t pmk[16];  // Primary Master Key
    uint8_t lmk[16];  // Local Master Key
};

#endif
