#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "espnow_config.h"
#include "espnow_utils.h"
#include "peer_manager.h"
#include "athlete_sensors.h"

#ifndef ATHLETE_NODE_ID
#define ATHLETE_NODE_ID 11
#endif

#ifndef ATHLETE_TEAM_ID
#define ATHLETE_TEAM_ID 1
#endif

struct GossipRecord {
    uint8_t origin_id;
    uint8_t sequence;
    MessageType type;
    uint32_t seen_at;
};

PeerManager peer_manager;
AthleteSensors sensors;          // simulated, or real hardware with -DUSE_REAL_SENSORS
GossipRecord gossip_cache[GOSSIP_CACHE_SIZE] = {};
uint8_t cache_index = 0;
uint8_t sequence_number = 0;
uint16_t sample_id = 0;
uint32_t last_metric_time = 0;
uint32_t last_gossip_time = 0;
uint32_t last_discovery_time = 0;
uint32_t last_peer_check = 0;

// Session state, driven by coach commands relayed through the mesh.
bool session_paused = false;     // STOP_SESSION halts metric sampling

bool isCached(const ESPNowMessage& msg) {
    for (uint8_t i = 0; i < GOSSIP_CACHE_SIZE; i++) {
        if (gossip_cache[i].origin_id == msg.sender_id &&
            gossip_cache[i].sequence == msg.sequence &&
            gossip_cache[i].type == msg.type) {
            return true;
        }
    }
    return false;
}

void rememberMessage(const ESPNowMessage& msg) {
    gossip_cache[cache_index] = {msg.sender_id, msg.sequence, msg.type, millis()};
    cache_index = (cache_index + 1) % GOSSIP_CACHE_SIZE;
}

void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    Serial.printf("TX %s -> %s\n",
                  ESPNowUtils::macToString(mac_addr).c_str(),
                  status == ESP_NOW_SEND_SUCCESS ? "ok" : "failed");
}

AthleteMetricsPayload readAthleteMetrics() {
    AthleteMetricsPayload metrics{};
    metrics.athlete_id = ATHLETE_NODE_ID;
    metrics.team_id = ATHLETE_TEAM_ID;
    metrics.sample_id = sample_id++;
    metrics.uptime_ms = millis();

    // The sensor layer fills HR, pace, splits, distance, cadence, battery, etc.
    // (real hardware with -DUSE_REAL_SENSORS, otherwise simulated values).
    sensors.fillMetrics(metrics);
    return metrics;
}

void sendDiscoveryPing() {
    ESPNowMessage ping = ESPNowUtils::createPingMessage(ATHLETE_NODE_ID, COACH_HUB_NODE_ID, sequence_number++);
    ping.flags |= ESPNOW_FLAG_GOSSIP;
    ESPNowUtils::broadcastMessage(ping);
    Serial.println("Discovery ping broadcast for coach hub");
}

bool gossipMessage(ESPNowMessage msg) {
    if (msg.ttl == 0) {
        return false;
    }

    msg.ttl--;
    msg.hop_count++;
    msg.flags |= ESPNOW_FLAG_FORWARDED | ESPNOW_FLAG_GOSSIP;

    PeerInfo* hub = peer_manager.getPeerById(COACH_HUB_NODE_ID);
    if (hub) {
        return ESPNowUtils::sendMessage(hub->mac, msg);
    }

    uint8_t active_count = peer_manager.getActivePeerCount();
    if (active_count == 0) {
        return ESPNowUtils::broadcastMessage(msg);
    }

    bool sent = false;
    uint8_t attempts = min<uint8_t>(GOSSIP_FANOUT, active_count);
    uint8_t start = random(0, active_count);
    for (uint8_t i = 0; i < attempts; i++) {
        PeerInfo* peer = peer_manager.getActivePeerByIndex((start + i) % active_count);
        if (peer && peer->peer_id != msg.sender_id) {
            sent |= ESPNowUtils::sendMessage(peer->mac, msg);
        }
    }

    if (!sent) {
        sent = ESPNowUtils::broadcastMessage(msg);
    }
    return sent;
}

void sendOwnMetrics() {
    AthleteMetricsPayload metrics = readAthleteMetrics();
    ESPNowMessage msg = ESPNowUtils::createAthleteMetricsMessage(ATHLETE_NODE_ID, sequence_number++, metrics);
    rememberMessage(msg);
    bool queued = gossipMessage(msg);

    Serial.printf("Metric sample %u HR=%u pace=%u split=%u dist=%um gossip=%s\n",
                  metrics.sample_id,
                  metrics.heart_rate_bpm,
                  metrics.pace_sec_per_mile,
                  metrics.current_split_sec,
                  metrics.distance_m,
                  queued ? "queued" : "failed");
}

void handleAthleteMetrics(const uint8_t* mac, const ESPNowMessage& msg) {
    AthleteMetricsPayload metrics{};
    if (!ESPNowUtils::readAthleteMetrics(msg, metrics)) {
        Serial.println("Bad athlete metrics payload");
        return;
    }

    metrics.signal_percent = ESPNowUtils::rssiToPercentage(static_cast<int8_t>(msg.rssi));
    Serial.printf("Gossip RX athlete=%u sample=%u HR=%u pace=%u hops=%u ttl=%u\n",
                  metrics.athlete_id,
                  metrics.sample_id,
                  metrics.heart_rate_bpm,
                  metrics.pace_sec_per_mile,
                  msg.hop_count,
                  msg.ttl);

    if (msg.flags & ESPNOW_FLAG_ACK_REQUESTED) {
        ESPNowMessage ack = ESPNowUtils::createAckMessage(ATHLETE_NODE_ID, msg.sender_id, msg.sequence);
        ESPNowUtils::sendMessage(mac, ack);
    }

    uint32_t now = millis();
    if (now - last_gossip_time >= GOSSIP_RETRY_INTERVAL_MS) {
        gossipMessage(msg);
        last_gossip_time = now;
    }
}

void handleCoachCommand(const ESPNowMessage& msg) {
    CoachCommandPayload cmd{};
    if (!ESPNowUtils::readCoachCommand(msg, cmd)) {
        return;
    }

    bool for_me = cmd.target_athlete == COACH_TARGET_ALL || cmd.target_athlete == ATHLETE_NODE_ID;
    if (for_me) {
        switch (static_cast<CoachCommand>(cmd.command)) {
            case CoachCommand::START_SESSION:
                sensors.onSessionStart();
                session_paused = false;
                Serial.println("Coach: START session");
                break;
            case CoachCommand::STOP_SESSION:
                session_paused = true;
                Serial.println("Coach: STOP session");
                break;
            case CoachCommand::MARK_LAP:
                sensors.markLap();
                Serial.printf("Coach: MARK lap %u\n", cmd.param);
                break;
            case CoachCommand::RESET_BEST:
                sensors.resetBest();
                Serial.println("Coach: RESET best split");
                break;
            case CoachCommand::IDENTIFY:
                Serial.printf("Coach: IDENTIFY athlete %u\n", ATHLETE_NODE_ID);
                break;
            default:
                break;
        }
    }

    // Keep flooding the command outward so every wearable in the mesh gets it.
    gossipMessage(msg);
}

void handleIncomingMessage(const uint8_t* mac, const ESPNowMessage& msg) {
    ESPNowUtils::ensurePeer(mac);
    peer_manager.registerPeer(mac, msg.sender_id);
    peer_manager.updatePeerActivity(mac, static_cast<int8_t>(msg.rssi), msg.sequence);

    if (msg.sender_id == ATHLETE_NODE_ID || isCached(msg)) {
        return;
    }
    rememberMessage(msg);

    switch (msg.type) {
        case MessageType::ATHLETE_METRICS:
            handleAthleteMetrics(mac, msg);
            break;
        case MessageType::COACH_COMMAND:
            handleCoachCommand(msg);
            break;
        case MessageType::PONG:
        case MessageType::ACK:
            Serial.printf("Hub/peer response from Node %u for seq %u\n", msg.sender_id, msg.sequence);
            break;
        case MessageType::MESH_BEACON:
        case MessageType::PING:
            break;
        default:
            if (msg.flags & ESPNOW_FLAG_GOSSIP) {
                gossipMessage(msg);
            }
            break;
    }
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataReceived(const esp_now_recv_info_t* recv_info, const uint8_t* data, int data_len) {
    const uint8_t* mac = recv_info->src_addr;
#else
void onDataReceived(const uint8_t* mac, const uint8_t* data, int data_len) {
#endif
    ESPNowMessage msg{};
    if (!ESPNowUtils::parseMessage(data, data_len, msg)) {
        Serial.printf("Invalid frame: %d bytes\n", data_len);
        return;
    }

    handleIncomingMessage(mac, msg);
}

void setupWiFi() {
    if (!ESPNowUtils::initWiFi("Athlete_Wearable")) {
        while (true) delay(1000);
    }

    uint8_t mac[6];
    ESPNowUtils::getDeviceMac(mac);
    Serial.print("Wearable MAC: ");
    Serial.println(ESPNowUtils::macToString(mac));
}

void setupESPNow() {
    if (!ESPNowUtils::initESPNow()) {
        while (true) delay(1000);
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceived);
    ESPNowUtils::addBroadcastPeer();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    randomSeed(static_cast<uint32_t>(micros()) ^ ATHLETE_NODE_ID);

    Serial.println("\n\n=== Athlete Wearable Gossip Node ===");
    Serial.printf("Athlete Node: %u | Team: %u | Coach Hub: %u | Channel: %u\n",
                  ATHLETE_NODE_ID,
                  ATHLETE_TEAM_ID,
                  COACH_HUB_NODE_ID,
                  ESPNOW_CHANNEL);

    setupWiFi();
    setupESPNow();

    if (!sensors.begin()) {
        Serial.println("Sensor init incomplete - continuing with available data");
    }

    sendDiscoveryPing();
    last_discovery_time = millis();
}

void loop() {
    uint32_t now = millis();

    sensors.update();   // keep streaming sensors (HR beats, GPS bytes, IMU) fresh

    if (!session_paused && now - last_metric_time >= ATHLETE_METRIC_INTERVAL_MS) {
        sendOwnMetrics();
        last_metric_time = now;
    }

    if (now - last_discovery_time >= ESPNOW_DISCOVERY_INTERVAL_MS) {
        sendDiscoveryPing();
        last_discovery_time = now;
    }

    if (now - last_peer_check >= 5000) {
        peer_manager.checkPeerTimeout(ESPNOW_DEFAULT_PEER_TIMEOUT_MS);
        Serial.printf("Peers: %u\n", peer_manager.getActivePeerCount());
        last_peer_check = now;
    }

    delay(5);   // tight loop keeps HR beat-detection and GPS UART reads responsive
}
