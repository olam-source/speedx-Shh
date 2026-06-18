#ifndef ESPNOW_CONFIG_H
#define ESPNOW_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include <cstring>

// ===== ESP-NOW Configuration =====
constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint8_t ESPNOW_BROADCAST_ID = 0xFF;
constexpr uint8_t ESPNOW_DEFAULT_TTL = 4;
constexpr uint8_t ESPNOW_MAX_PEERS = 20;
constexpr size_t ESPNOW_MAX_FRAME_SIZE = 250;
constexpr size_t ESPNOW_HEADER_SIZE = 14;
constexpr size_t MAX_PAYLOAD_SIZE = ESPNOW_MAX_FRAME_SIZE - ESPNOW_HEADER_SIZE;
constexpr uint32_t ESPNOW_DEFAULT_PEER_TIMEOUT_MS = 30000;
constexpr uint32_t ESPNOW_DISCOVERY_INTERVAL_MS = 10000;

constexpr uint8_t ESPNOW_FLAG_ACK_REQUESTED = 0x01;
constexpr uint8_t ESPNOW_FLAG_FORWARDED = 0x02;

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
struct __attribute__((packed)) ESPNowMessage {
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

static_assert(offsetof(ESPNowMessage, payload) == ESPNOW_HEADER_SIZE,
              "ESP-NOW message header size changed; update ESPNOW_HEADER_SIZE");
static_assert(sizeof(ESPNowMessage) == ESPNOW_MAX_FRAME_SIZE,
              "ESP-NOW message must fit in one ESP-NOW v1 frame");

struct __attribute__((packed)) SensorDataPayload {
    float temperature_c;
    uint16_t humidity_percent;
    uint16_t battery_mv;
};

// ===== Peer Information Structure =====
struct PeerInfo {
    uint8_t mac[6];
    uint8_t peer_id;
    int8_t rssi;
    uint32_t last_seen;
    uint16_t message_count;
    bool is_active;
    uint8_t last_sequence;
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
