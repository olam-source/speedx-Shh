#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "espnow_config.h"
#include "espnow_utils.h"
#include "peer_manager.h"

// ===== Configuration =====
constexpr uint8_t NODE_ID = 1;
constexpr uint8_t TARGET_NODE_ID = 2;
constexpr uint32_t SEND_INTERVAL_MS = 5000;
constexpr uint32_t PEER_CHECK_INTERVAL_MS = 5000;

PeerManager peer_manager;
uint32_t last_send_time = 0;
uint32_t last_discovery_time = 0;
uint32_t last_peer_check = 0;
uint8_t sequence_number = 0;

// ===== Callback Functions =====
void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    Serial.printf("Send to %s: %s\n",
                  ESPNowUtils::macToString(mac_addr).c_str(),
                  status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failed");
}

void handleIncomingMessage(const uint8_t* mac, const ESPNowMessage& msg) {
    ESPNowUtils::ensurePeer(mac);
    peer_manager.registerPeer(mac, msg.sender_id);
    peer_manager.updatePeerActivity(mac, static_cast<int8_t>(msg.rssi), msg.sequence);

    Serial.printf("RX from %s | ", ESPNowUtils::macToString(mac).c_str());
    ESPNowUtils::printMessageSummary(msg);

    switch (msg.type) {
        case MessageType::ACK:
            Serial.printf("ACK received from Node %u for sequence %u\n", msg.sender_id, msg.sequence);
            break;
        case MessageType::PONG:
            Serial.printf("Discovery response from Node %u\n", msg.sender_id);
            break;
        case MessageType::MESH_BEACON:
            Serial.printf("Mesh beacon from Node %u with %u active peer(s)\n",
                          msg.sender_id,
                          msg.data_length >= 2 ? msg.payload[1] : 0);
            break;
        default:
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
        Serial.printf("Invalid message size: %d bytes\n", data_len);
        return;
    }

    if (msg.sender_id == NODE_ID) {
        return;
    }

    handleIncomingMessage(mac, msg);
}

// ===== Initialization Functions =====
void setupWiFi() {
    if (!ESPNowUtils::initWiFi("ESP32_Sender")) {
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

// ===== Sending Functions =====
void sendDiscoveryPing() {
    ESPNowMessage ping = ESPNowUtils::createPingMessage(NODE_ID, TARGET_NODE_ID, sequence_number++);
    bool success = ESPNowUtils::broadcastMessage(ping);
    Serial.printf("Discovery ping for Node %u: %s\n", TARGET_NODE_ID, success ? "queued" : "failed");
}

void sendSensorData() {
    SensorDataPayload sensor{};
    sensor.temperature_c = 25.5f + (random(-10, 10) / 10.0f);
    sensor.humidity_percent = static_cast<uint16_t>(50 + random(-5, 6));
    sensor.battery_mv = static_cast<uint16_t>(3700 + random(-40, 41));

    PeerInfo* receiver = peer_manager.getPeerById(TARGET_NODE_ID);
    const uint8_t* target_mac = receiver ? receiver->mac : ESPNowUtils::broadcastMac();

    ESPNowMessage msg = ESPNowUtils::createDataMessage(
        NODE_ID,
        TARGET_NODE_ID,
        sequence_number++,
        &sensor,
        sizeof(sensor)
    );

    bool success = ESPNowUtils::sendMessage(target_mac, msg);
    Serial.printf("Data to Node %u via %s - Temp: %.1f C, Humidity: %u%%, Battery: %umV (%s)\n",
                  TARGET_NODE_ID,
                  receiver ? "unicast" : "broadcast",
                  sensor.temperature_c,
                  sensor.humidity_percent,
                  sensor.battery_mv,
                  success ? "queued" : "failed");
}

// ===== Main Setup =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    randomSeed(static_cast<uint32_t>(micros()));

    Serial.println("\n\n=== ESP-NOW Sender Node ===");
    Serial.printf("Node ID: %u | Target Node: %u | Channel: %u\n", NODE_ID, TARGET_NODE_ID, ESPNOW_CHANNEL);

    setupWiFi();
    setupESPNow();

    sendDiscoveryPing();
    last_discovery_time = millis();
    Serial.println("Sender node initialized!");
}

// ===== Main Loop =====
void loop() {
    uint32_t current_time = millis();

    if (!peer_manager.getPeerById(TARGET_NODE_ID) &&
        current_time - last_discovery_time >= ESPNOW_DISCOVERY_INTERVAL_MS) {
        sendDiscoveryPing();
        last_discovery_time = current_time;
    }

    if (current_time - last_send_time >= SEND_INTERVAL_MS) {
        sendSensorData();
        last_send_time = current_time;
    }

    if (current_time - last_peer_check >= PEER_CHECK_INTERVAL_MS) {
        peer_manager.checkPeerTimeout(ESPNOW_DEFAULT_PEER_TIMEOUT_MS);
        peer_manager.printPeers();
        last_peer_check = current_time;
    }

    delay(50);
}
