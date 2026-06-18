#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "espnow_config.h"
#include "espnow_utils.h"
#include "peer_manager.h"

// ===== Configuration =====
constexpr uint8_t NODE_ID = 3;
constexpr uint32_t BEACON_INTERVAL_MS = 10000;
constexpr uint32_t PEER_CHECK_INTERVAL_MS = 5000;
constexpr uint8_t MESSAGE_CACHE_SIZE = 16;

struct SeenMessage {
    uint8_t sender_id;
    uint8_t sequence;
    MessageType type;
    uint32_t seen_at;
};

PeerManager peer_manager;
SeenMessage seen_messages[MESSAGE_CACHE_SIZE] = {};
uint8_t seen_index = 0;
uint32_t last_beacon_time = 0;
uint32_t last_peer_check = 0;
uint8_t sequence_number = 0;

// ===== Callback Functions =====
void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    Serial.printf("Mesh send to %s: %s\n",
                  ESPNowUtils::macToString(mac_addr).c_str(),
                  status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failed");
}

bool hasSeenMessage(const ESPNowMessage& msg) {
    for (uint8_t i = 0; i < MESSAGE_CACHE_SIZE; i++) {
        if (seen_messages[i].sender_id == msg.sender_id &&
            seen_messages[i].sequence == msg.sequence &&
            seen_messages[i].type == msg.type) {
            return true;
        }
    }
    return false;
}

void rememberMessage(const ESPNowMessage& msg) {
    seen_messages[seen_index] = {msg.sender_id, msg.sequence, msg.type, millis()};
    seen_index = (seen_index + 1) % MESSAGE_CACHE_SIZE;
}

void sendAckTo(const uint8_t* mac, uint8_t receiver_id, uint8_t sequence) {
    ESPNowMessage ack = ESPNowUtils::createAckMessage(NODE_ID, receiver_id, sequence);
    ESPNowUtils::sendMessage(mac, ack);
}

void sendPongTo(const uint8_t* mac, uint8_t receiver_id, uint8_t sequence) {
    ESPNowMessage pong = ESPNowUtils::createPongMessage(NODE_ID, receiver_id, sequence);
    ESPNowUtils::sendMessage(mac, pong);
}

bool forwardMessage(ESPNowMessage msg) {
    if (msg.ttl == 0) {
        Serial.println("Dropping message: TTL expired");
        return false;
    }

    msg.ttl--;
    msg.hop_count++;
    msg.flags |= ESPNOW_FLAG_FORWARDED;

    PeerInfo* destination = peer_manager.getPeerById(msg.receiver_id);
    const uint8_t* next_hop = destination ? destination->mac : ESPNowUtils::broadcastMac();
    bool success = ESPNowUtils::sendMessage(next_hop, msg);

    Serial.printf("Forwarded %s for Node %u via %s (%s)\n",
                  ESPNowUtils::messageTypeToString(msg.type),
                  msg.receiver_id,
                  destination ? ESPNowUtils::macToString(next_hop).c_str() : "broadcast",
                  success ? "queued" : "failed");
    return success;
}

void handleLocalMessage(const uint8_t* mac, const ESPNowMessage& msg) {
    switch (msg.type) {
        case MessageType::PING:
            Serial.println("PING received - sending PONG");
            sendPongTo(mac, msg.sender_id, msg.sequence);
            break;
        case MessageType::DATA:
        case MessageType::MESH_DATA:
            Serial.printf("Mesh payload received from Node %u (%u bytes)\n", msg.sender_id, msg.data_length);
            if (msg.flags & ESPNOW_FLAG_ACK_REQUESTED) {
                sendAckTo(mac, msg.sender_id, msg.sequence);
            }
            break;
        case MessageType::ACK:
            Serial.printf("ACK received from Node %u for sequence %u\n", msg.sender_id, msg.sequence);
            break;
        case MessageType::MESH_BEACON:
            Serial.printf("Beacon from Node %u with %u active peer(s)\n",
                          msg.sender_id,
                          msg.data_length >= 2 ? msg.payload[1] : 0);
            break;
        default:
            Serial.printf("Unhandled mesh message type: %s\n", ESPNowUtils::messageTypeToString(msg.type));
            break;
    }
}

void handleIncomingMessage(const uint8_t* mac, const ESPNowMessage& msg) {
    ESPNowUtils::ensurePeer(mac);
    peer_manager.registerPeer(mac, msg.sender_id);
    peer_manager.updatePeerActivity(mac, static_cast<int8_t>(msg.rssi), msg.sequence);

    if (hasSeenMessage(msg)) {
        Serial.printf("Duplicate %s from Node %u seq %u ignored\n",
                      ESPNowUtils::messageTypeToString(msg.type),
                      msg.sender_id,
                      msg.sequence);
        return;
    }
    rememberMessage(msg);

    Serial.printf("\nMesh RX from %s | ", ESPNowUtils::macToString(mac).c_str());
    ESPNowUtils::printMessageSummary(msg);

    bool addressed_to_me = msg.receiver_id == NODE_ID || msg.receiver_id == ESPNOW_BROADCAST_ID;
    if (addressed_to_me) {
        handleLocalMessage(mac, msg);
        return;
    }

    forwardMessage(msg);
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataReceived(const esp_now_recv_info_t* recv_info, const uint8_t* data, int data_len) {
    const uint8_t* mac = recv_info->src_addr;
#else
void onDataReceived(const uint8_t* mac, const uint8_t* data, int data_len) {
#endif
    ESPNowMessage msg{};
    if (!ESPNowUtils::parseMessage(data, data_len, msg)) {
        Serial.printf("Invalid message size: %d bytes\n", data_len);
        return;
    }

    if (msg.sender_id == NODE_ID) {
        return;
    }

    handleIncomingMessage(mac, msg);
}

// ===== Beacon Functions =====
void sendBeacon() {
    ESPNowMessage beacon = ESPNowUtils::createBeaconMessage(
        NODE_ID,
        sequence_number++,
        peer_manager.getActivePeerCount()
    );

    bool success = ESPNowUtils::broadcastMessage(beacon);
    Serial.printf("Beacon %s (Seq: %u, Active peers: %u)\n",
                  success ? "queued" : "failed",
                  beacon.sequence,
                  peer_manager.getActivePeerCount());
}

// ===== Initialization Functions =====
void setupWiFi() {
    if (!ESPNowUtils::initWiFi("ESP32_Mesh_Node")) {
        Serial.println("WiFi setup failed");
        while (true) delay(1000);
    }

    uint8_t mac[6];
    ESPNowUtils::getDeviceMac(mac);
    Serial.print("Device MAC: ");
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

// ===== Main Setup =====
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n=== ESP-NOW Mesh Node ===");
    Serial.printf("Node ID: %u | Channel: %u\n", NODE_ID, ESPNOW_CHANNEL);

    setupWiFi();
    setupESPNow();

    sendBeacon();
    last_beacon_time = millis();
    Serial.println("Mesh node initialized!");
}

// ===== Main Loop =====
void loop() {
    uint32_t current_time = millis();

    if (current_time - last_beacon_time >= BEACON_INTERVAL_MS) {
        sendBeacon();
        last_beacon_time = current_time;
    }

    if (current_time - last_peer_check >= PEER_CHECK_INTERVAL_MS) {
        peer_manager.checkPeerTimeout(ESPNOW_DEFAULT_PEER_TIMEOUT_MS);
        Serial.printf("Network Status - Active peers: %u\n", peer_manager.getActivePeerCount());
        peer_manager.printPeers();
        last_peer_check = current_time;
    }

    delay(50);
}
