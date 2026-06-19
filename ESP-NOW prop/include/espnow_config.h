#ifndef ESPNOW_CONFIG_H
#define ESPNOW_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include <cstring>

// ===== ESP-NOW Configuration =====
constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint8_t ESPNOW_BROADCAST_ID = 0xFF;
constexpr uint8_t ESPNOW_DEFAULT_TTL = 4;
constexpr uint8_t COACH_HUB_NODE_ID = 250;
constexpr uint8_t ESPNOW_MAX_PEERS = 20;
constexpr size_t ESPNOW_MAX_FRAME_SIZE = 250;
constexpr size_t ESPNOW_HEADER_SIZE = 14;
constexpr size_t MAX_PAYLOAD_SIZE = ESPNOW_MAX_FRAME_SIZE - ESPNOW_HEADER_SIZE;
constexpr uint32_t ESPNOW_DEFAULT_PEER_TIMEOUT_MS = 30000;
constexpr uint32_t ESPNOW_DISCOVERY_INTERVAL_MS = 10000;
constexpr uint32_t ATHLETE_METRIC_INTERVAL_MS = 2000;
constexpr uint32_t GOSSIP_RETRY_INTERVAL_MS = 750;
constexpr uint8_t GOSSIP_FANOUT = 3;
constexpr uint8_t GOSSIP_CACHE_SIZE = 32;

constexpr uint8_t ESPNOW_FLAG_ACK_REQUESTED = 0x01;
constexpr uint8_t ESPNOW_FLAG_FORWARDED = 0x02;
constexpr uint8_t ESPNOW_FLAG_GOSSIP = 0x04;
constexpr uint8_t ESPNOW_FLAG_URGENT = 0x08;

constexpr uint16_t ATHLETE_FLAG_GPS_LOCK = 0x0001;
constexpr uint16_t ATHLETE_FLAG_HEART_RATE_VALID = 0x0002;
constexpr uint16_t ATHLETE_FLAG_SPLIT_MARKER = 0x0004;
constexpr uint16_t ATHLETE_FLAG_LOW_BATTERY = 0x0008;

// ===== Coach Commands =====
// Broadcast from the coach hub to every athlete wearable through the mesh.
enum class CoachCommand : uint8_t {
    NONE = 0,
    START_SESSION = 1,   // zero distance/splits and begin a fresh run
    STOP_SESSION = 2,    // pause metric sampling
    MARK_LAP = 3,        // force a split marker on the next sample
    RESET_BEST = 4,      // clear the stored best split
    IDENTIFY = 5         // ask the wearable to flash/announce itself
};

constexpr uint8_t COACH_TARGET_ALL = 0xFF;

// ===== Message Types =====
enum class MessageType : uint8_t {
    PING = 0,
    PONG = 1,
    ACK = 4,
    MESH_BEACON = 6,
    ATHLETE_METRICS = 8,
    COACH_COMMAND = 10
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

struct __attribute__((packed)) AthleteMetricsPayload {
    uint8_t athlete_id;
    uint8_t team_id;
    uint16_t sample_id;
    uint32_t uptime_ms;
    uint16_t heart_rate_bpm;
    uint16_t pace_sec_per_mile;
    uint16_t current_split_sec;
    uint16_t best_split_sec;
    uint16_t distance_m;
    uint16_t speed_cm_s;
    uint16_t cadence_spm;
    uint16_t stride_length_cm;
    uint16_t ground_contact_ms;
    uint16_t vertical_oscillation_cm;
    int16_t elevation_gain_m;
    int16_t skin_temp_c_x10;
    uint16_t battery_mv;
    int8_t signal_percent;
    uint8_t reserved;
    uint16_t flags;
};

static_assert(sizeof(AthleteMetricsPayload) <= MAX_PAYLOAD_SIZE,
              "Athlete metrics payload must fit in one ESP-NOW frame");

struct __attribute__((packed)) CoachCommandPayload {
    uint8_t command;          // CoachCommand value
    uint8_t target_athlete;   // athlete_id, or COACH_TARGET_ALL for everyone
    uint16_t param;           // optional command argument (e.g. lap number)
    uint32_t issued_ms;       // hub uptime when the command was issued
};

static_assert(sizeof(CoachCommandPayload) <= MAX_PAYLOAD_SIZE,
              "Coach command payload must fit in one ESP-NOW frame");

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

#endif
