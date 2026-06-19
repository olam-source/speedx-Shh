// ============================================================================
// SpeedX Coach Hub  -  central USB ESP-NOW adapter
//
// This node plugs into the coach's laptop over USB. It is the mesh "sink":
// athlete wearables gossip their metrics hop-by-hop across each other until a
// frame reaches this hub, which then streams every athlete's vitals to the
// host as one JSON object per line. A companion web dashboard
// (dashboard/coach_dashboard.html) reads that USB stream and renders every
// athlete on a single screen.
//
// The hub also:
//   - beacons so wearables can discover it and unicast directly when in range,
//   - keeps a roster of the latest sample from every athlete it has heard,
//   - acknowledges metric frames that request it,
//   - relays coach commands (typed on the host, or sent by the dashboard) out
//     to the whole team through the same gossip mesh.
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "espnow_config.h"
#include "espnow_utils.h"
#include "peer_manager.h"

#ifndef COACH_TEAM_ID
#define COACH_TEAM_ID 1
#endif

constexpr uint8_t NODE_ID = COACH_HUB_NODE_ID;
constexpr uint32_t BEACON_INTERVAL_MS = 5000;
constexpr uint32_t ROSTER_PRINT_INTERVAL_MS = 5000;
constexpr uint32_t PEER_CHECK_INTERVAL_MS = 5000;
constexpr uint32_t ATHLETE_STALE_MS = 15000;
constexpr uint8_t MAX_ATHLETES = 32;

// ----- Roster: latest known state for each athlete --------------------------
struct AthleteRecord {
    bool in_use;
    uint8_t athlete_id;
    uint32_t last_update_ms;   // for active-athlete counting
};

// ----- Gossip de-duplication cache ------------------------------------------
struct GossipRecord {
    uint8_t origin_id;
    uint8_t sequence;
    MessageType type;
    uint32_t seen_at;
};

PeerManager peer_manager;
AthleteRecord roster[MAX_ATHLETES] = {};
GossipRecord gossip_cache[GOSSIP_CACHE_SIZE] = {};
uint8_t cache_index = 0;
uint8_t sequence_number = 0;
uint32_t last_beacon_time = 0;
uint32_t last_roster_print = 0;
uint32_t last_peer_check = 0;

// USB serial command buffer (host -> hub).
String serial_line;

// ===== Gossip cache =========================================================
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

// ===== Roster ===============================================================
AthleteRecord* findOrAllocateAthlete(uint8_t athlete_id) {
    AthleteRecord* free_slot = nullptr;
    for (uint8_t i = 0; i < MAX_ATHLETES; i++) {
        if (roster[i].in_use && roster[i].athlete_id == athlete_id) {
            return &roster[i];
        }
        if (!roster[i].in_use && free_slot == nullptr) {
            free_slot = &roster[i];
        }
    }
    return free_slot;
}

uint8_t activeAthleteCount(uint32_t now) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_ATHLETES; i++) {
        if (roster[i].in_use && (now - roster[i].last_update_ms) <= ATHLETE_STALE_MS) {
            count++;
        }
    }
    return count;
}

void recordAthleteMetrics(const AthleteMetricsPayload& metrics) {
    AthleteRecord* record = findOrAllocateAthlete(metrics.athlete_id);
    if (!record) {
        Serial.println("{\"t\":\"warn\",\"msg\":\"roster full\"}");
        return;
    }
    record->in_use = true;
    record->athlete_id = metrics.athlete_id;
    record->last_update_ms = millis();
}

// ===== Callbacks ============================================================
void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.printf("{\"t\":\"tx\",\"to\":\"%s\",\"ok\":false}\n",
                      ESPNowUtils::macToString(mac_addr).c_str());
    }
}

void handleAthleteMetrics(const uint8_t* mac, const ESPNowMessage& msg) {
    AthleteMetricsPayload metrics{};
    if (!ESPNowUtils::readAthleteMetrics(msg, metrics)) {
        Serial.println("{\"t\":\"warn\",\"msg\":\"bad metrics payload\"}");
        return;
    }

    // Stamp the link quality of the last hop we heard it on.
    metrics.signal_percent = ESPNowUtils::rssiToPercentage(static_cast<int8_t>(msg.rssi));

    recordAthleteMetrics(metrics);
    ESPNowUtils::printAthleteMetricsJson(metrics, msg.hop_count, msg.ttl);

    // Acknowledge the immediate sender so wearables can prefer this hub route.
    if (msg.flags & ESPNOW_FLAG_ACK_REQUESTED) {
        ESPNowMessage ack = ESPNowUtils::createAckMessage(NODE_ID, msg.sender_id, msg.sequence);
        ESPNowUtils::sendMessage(mac, ack);
    }
}

void handleIncomingMessage(const uint8_t* mac, const ESPNowMessage& msg) {
    ESPNowUtils::ensurePeer(mac);
    peer_manager.registerPeer(mac, msg.sender_id);
    peer_manager.updatePeerActivity(mac, static_cast<int8_t>(msg.rssi), msg.sequence);

    if (isCached(msg)) {
        return;
    }
    rememberMessage(msg);

    switch (msg.type) {
        case MessageType::ATHLETE_METRICS:
            handleAthleteMetrics(mac, msg);
            break;
        case MessageType::PING:
            // A wearable is probing for the hub; answer so it learns our MAC.
            ESPNowUtils::sendMessage(mac, ESPNowUtils::createPongMessage(NODE_ID, msg.sender_id, msg.sequence));
            break;
        case MessageType::MESH_BEACON:
        case MessageType::ACK:
        case MessageType::PONG:
            break;
        default:
            Serial.printf("{\"t\":\"rx\",\"type\":\"%s\",\"from\":%u}\n",
                          ESPNowUtils::messageTypeToString(msg.type), msg.sender_id);
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
        return;
    }
    if (msg.sender_id == NODE_ID) {
        return;
    }
    handleIncomingMessage(mac, msg);
}

// ===== Outbound: beacons & coach commands ===================================
void sendBeacon() {
    ESPNowMessage beacon = ESPNowUtils::createBeaconMessage(
        NODE_ID, sequence_number++, peer_manager.getActivePeerCount());
    ESPNowUtils::broadcastMessage(beacon);
}

void broadcastCoachCommand(CoachCommand command, uint8_t target, uint16_t param) {
    ESPNowMessage msg = ESPNowUtils::createCoachCommandMessage(sequence_number++, command, target, param);
    rememberMessage(msg);  // don't re-handle our own echo
    ESPNowUtils::broadcastMessage(msg);
    Serial.printf("{\"t\":\"cmd_sent\",\"command\":%u,\"target\":%u,\"param\":%u}\n",
                  static_cast<unsigned>(command), target, param);
}

// ===== Host -> hub command console ==========================================
// Accepts simple verbs on the USB serial line, e.g.:
//   start            stop            reset
//   lap [number]     identify [athlete_id]
//   cmd <num> <target> <param>      (raw form)
void handleSerialCommand(String line) {
    line.trim();
    if (line.length() == 0) {
        return;
    }
    line.toLowerCase();

    int sep = line.indexOf(' ');
    String verb = sep < 0 ? line : line.substring(0, sep);
    String rest = sep < 0 ? "" : line.substring(sep + 1);
    rest.trim();

    if (verb == "start") {
        broadcastCoachCommand(CoachCommand::START_SESSION, COACH_TARGET_ALL, 0);
    } else if (verb == "stop") {
        broadcastCoachCommand(CoachCommand::STOP_SESSION, COACH_TARGET_ALL, 0);
    } else if (verb == "reset") {
        broadcastCoachCommand(CoachCommand::RESET_BEST, COACH_TARGET_ALL, 0);
    } else if (verb == "lap") {
        broadcastCoachCommand(CoachCommand::MARK_LAP, COACH_TARGET_ALL,
                              static_cast<uint16_t>(rest.toInt()));
    } else if (verb == "identify") {
        uint8_t target = rest.length() ? static_cast<uint8_t>(rest.toInt()) : COACH_TARGET_ALL;
        broadcastCoachCommand(CoachCommand::IDENTIFY, target, 0);
    } else if (verb == "cmd") {
        // raw: cmd <command> <target> <param>
        uint8_t parts[3] = {0, COACH_TARGET_ALL, 0};
        for (uint8_t i = 0; i < 3 && rest.length(); i++) {
            int s = rest.indexOf(' ');
            String tok = s < 0 ? rest : rest.substring(0, s);
            parts[i] = static_cast<uint8_t>(tok.toInt());
            rest = s < 0 ? "" : rest.substring(s + 1);
            rest.trim();
        }
        broadcastCoachCommand(static_cast<CoachCommand>(parts[0]), parts[1], parts[2]);
    } else {
        Serial.printf("{\"t\":\"warn\",\"msg\":\"unknown command\",\"got\":\"%s\"}\n", verb.c_str());
    }
}

void pollSerialConsole() {
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (serial_line.length() > 0) {
                handleSerialCommand(serial_line);
                serial_line = "";
            }
        } else if (serial_line.length() < 96) {
            serial_line += c;
        }
    }
}

// ===== Roster heartbeat to the dashboard ====================================
void printRosterSnapshot() {
    uint32_t now = millis();
    Serial.printf("{\"t\":\"hub\",\"node\":%u,\"team\":%u,\"uptime_ms\":%lu,"
                  "\"athletes\":%u,\"peers\":%u}\n",
                  NODE_ID, COACH_TEAM_ID, static_cast<unsigned long>(now),
                  activeAthleteCount(now), peer_manager.getActivePeerCount());
}

// ===== Init =================================================================
void setupWiFi() {
    if (!ESPNowUtils::initWiFi("SpeedX_Coach_Hub")) {
        while (true) delay(1000);
    }

    uint8_t mac[6];
    ESPNowUtils::getDeviceMac(mac);
    Serial.printf("{\"t\":\"boot\",\"role\":\"coach_hub\",\"node\":%u,\"mac\":\"%s\",\"channel\":%u}\n",
                  NODE_ID, ESPNowUtils::macToString(mac).c_str(), ESPNOW_CHANNEL);
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
    randomSeed(static_cast<uint32_t>(micros()) ^ NODE_ID);

    Serial.println("\n\n=== SpeedX Coach Hub (USB mesh adapter) ===");
    setupWiFi();
    setupESPNow();

    sendBeacon();
    last_beacon_time = millis();
    Serial.println("{\"t\":\"ready\",\"role\":\"coach_hub\"}");
}

void loop() {
    uint32_t now = millis();

    pollSerialConsole();

    if (now - last_beacon_time >= BEACON_INTERVAL_MS) {
        sendBeacon();
        last_beacon_time = now;
    }

    if (now - last_peer_check >= PEER_CHECK_INTERVAL_MS) {
        peer_manager.checkPeerTimeout(ESPNOW_DEFAULT_PEER_TIMEOUT_MS);
        last_peer_check = now;
    }

    if (now - last_roster_print >= ROSTER_PRINT_INTERVAL_MS) {
        printRosterSnapshot();
        last_roster_print = now;
    }

    delay(10);
}
