#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "espnow_config.h"
#include "espnow_utils.h"
#include "peer_manager.h"

// ===== Configuration =====
#define NODE_ID 2
#define PEER_TIMEOUT_MS 30000  // 30 seconds

PeerManager peer_manager;
uint32_t last_peer_check = 0;

// ===== Callback Functions =====
void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    Serial.print("ACK Status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sent" : "Failed");
}

void onDataReceived(const uint8_t* mac, const uint8_t* data, int data_len) {
    if (data_len != sizeof(ESPNowMessage)) {
        Serial.println("Invalid message size");
        return;
    }

    ESPNowMessage* msg = (ESPNowMessage*)data;
    
    Serial.printf("\n--- Message Received ---\n");
    Serial.printf("From Node: %d\n", msg->sender_id);
    Serial.printf("Type: %d | Sequence: %d\n", (uint8_t)msg->type, msg->sequence);
    Serial.printf("Payload Size: %d bytes\n", msg->data_length);
    
    switch (msg->type) {
        case MessageType::DATA: {
            // Parse sensor data
            float temperature = *(float*)msg->payload;
            uint16_t humidity = *(uint16_t*)(msg->payload + 4);
            Serial.printf("Temperature: %.1f°C | Humidity: %d%%\n", temperature, humidity);
            
            // Send ACK
            ESPNowMessage ack = ESPNowUtils::createAckMessage(NODE_ID, msg->sequence);
            ESPNowUtils::sendMessage(mac, ack);
            break;
        }
        case MessageType::PING: {
            Serial.println("PING received - sending PONG");
            ESPNowMessage pong = ESPNowUtils::createAckMessage(NODE_ID, msg->sequence);
            pong.type = MessageType::PONG;
            ESPNowUtils::sendMessage(mac, pong);
            break;
        }
        default:
            Serial.printf("Unknown message type: %d\n", (uint8_t)msg->type);
            break;
    }
    
    // Update peer information
    peer_manager.registerPeer(mac, msg->sender_id);
    peer_manager.updatePeerRssi(mac, msg->rssi);
}

// ===== Initialization Functions =====
void setupWiFi() {
    ESPNowUtils::initWiFi("ESP32_Receiver");
    
    uint8_t mac[6];
    ESPNowUtils::getDeviceMac(mac);
    Serial.print("Device MAC: ");
    Serial.println(ESPNowUtils::macToString(mac));
}

void setupESPNow() {
    ESPNowUtils::initESPNow();
    
    // Register send and receive callbacks
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceived);
}

// ===== Main Setup =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n=== ESP-NOW Receiver Node ===");
    Serial.printf("Node ID: %d\n", NODE_ID);
    
    setupWiFi();
    setupESPNow();
    
    Serial.println("Receiver node initialized!");
}

// ===== Main Loop =====
void loop() {
    uint32_t current_time = millis();
    
    // Check for peer timeouts every 5 seconds
    if (current_time - last_peer_check >= 5000) {
        peer_manager.checkPeerTimeout(PEER_TIMEOUT_MS);
        peer_manager.printPeers();
        last_peer_check = current_time;
    }
    
    delay(100);
}
