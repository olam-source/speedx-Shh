#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "espnow_config.h"
#include "espnow_utils.h"
#include "peer_manager.h"

// ===== Configuration =====
constexpr uint8_t NODE_ID = 2;
constexpr uint32_t PEER_CHECK_INTERVAL_MS = 5000;

PeerManager peer_manager;
uint32_t last_peer_check = 0;

// ===== Callback Functions =====
void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    Serial.printf("Reply to %s: %s\n",
                  ESPNowUtils::macToString(mac_addr).c_str(),
                  status == ESP_NOW_SEND_SUCCESS ? "Sent" : "Failed");
}

void sendAckTo(const uint8_t* mac, uint8_t receiver_id, uint8_t sequence) {
    ESPNowMessage ack = ESPNowUtils::createAckMessage(NODE_ID, receiver_id, sequence);
    ESPNowUtils::sendMessage(mac, ack);
}

void sendPongTo(const uint8_t* mac, uint8_t receiver_id, uint8_t sequence) {
    ESPNowMessage pong = ESPNowUtils::createPongMessage(NODE_ID, receiver_id, sequence);
    ESPNowUtils::sendMessage(mac, pong);
}

void handleDataMessage(const uint8_t* mac, const ESPNowMessage& msg) {
    if (msg.data_length != sizeof(SensorDataPayload)) {
        Serial.printf("DATA payload length mismatch: expected %u, got %u\n",
                      static_cast<unsigned>(sizeof(SensorDataPayload)),
                      msg.data_length);
        sendAckTo(mac, msg.sender_id, msg.sequence);
        return;
    }

    SensorDataPayload sensor{};
    memcpy(&sensor, msg.payload, sizeof(sensor));
    Serial.printf("Temperature: %.1f C | Humidity: %u%% | Battery: %umV\n",
                  sensor.temperature_c,
                  sensor.humidity_percent,
                  sensor.battery_mv);
    sendAckTo(mac, msg.sender_id, msg.sequence);
}

void handleIncomingMessage(const uint8_t* mac, const ESPNowMessage& msg) {
    ESPNowUtils::ensurePeer(mac);
    peer_manager.registerPeer(mac, msg.sender_id);
    peer_manager.updatePeerActivity(mac, static_cast<int8_t>(msg.rssi), msg.sequence);

    Serial.printf("\n--- Message Received from %s ---\n", ESPNowUtils::macToString(mac).c_str());
    ESPNowUtils::printMessageSummary(msg);

    bool addressed_to_me = msg.receiver_id == NODE_ID || msg.receiver_id == ESPNOW_BROADCAST_ID;
    if (!addressed_to_me) {
        Serial.printf("Ignoring message for Node %u\n", msg.receiver_id);
        return;
    }

    switch (msg.type) {
        case MessageType::DATA:
        case MessageType::MESH_DATA:
            handleDataMessage(mac, msg);
            break;
        case MessageType::PING:
            Serial.println("PING received - sending PONG");
            sendPongTo(mac, msg.sender_id, msg.sequence);
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
            Serial.printf("Unhandled message type: %s\n", ESPNowUtils::messageTypeToString(msg.type));
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
    if (!ESPNowUtils::initWiFi("ESP32_Receiver")) {
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

    Serial.println("\n\n=== ESP-NOW Receiver Node ===");
    Serial.printf("Node ID: %u | Channel: %u\n", NODE_ID, ESPNOW_CHANNEL);

    setupWiFi();
    setupESPNow();

    Serial.println("Receiver node initialized!");
}

// ===== Main Loop =====
void loop() {
    uint32_t current_time = millis();

    if (current_time - last_peer_check >= PEER_CHECK_INTERVAL_MS) {
        peer_manager.checkPeerTimeout(ESPNOW_DEFAULT_PEER_TIMEOUT_MS);
        peer_manager.printPeers();
        last_peer_check = current_time;
    }

    delay(50);
}
