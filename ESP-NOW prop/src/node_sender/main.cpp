#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "espnow_config.h"
#include "espnow_utils.h"

// ===== Configuration =====
#define NODE_ID 1
#define RECEIVER_MAC {0x34, 0x85, 0x18, 0x12, 0x34, 0x56}  // Replace with actual receiver MAC
#define SEND_INTERVAL 5000  // Send every 5 seconds

uint8_t receiver_mac[] = RECEIVER_MAC;
uint32_t last_send_time = 0;
uint8_t sequence_number = 0;

// ===== Callback Functions =====
void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    Serial.print("Send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failed");
}

void onDataReceived(const uint8_t* mac, const uint8_t* data, int data_len) {
    if (data_len != sizeof(ESPNowMessage)) {
        Serial.println("Invalid message size");
        return;
    }

    ESPNowMessage* msg = (ESPNowMessage*)data;
    
    Serial.printf("Message received from Node %d | Type: %d | RSSI: %d | Seq: %d\n",
                  msg->sender_id, (uint8_t)msg->type, msg->rssi, msg->sequence);
}

// ===== Initialization Functions =====
void setupWiFi() {
    ESPNowUtils::initWiFi("ESP32_Sender");
    
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
    
    // Add receiver as peer
    ESPNowUtils::addPeer(receiver_mac);
}

// ===== Sending Functions =====
void sendSensorData() {
    // Create a data message
    ESPNowMessage msg{};
    msg.type = MessageType::DATA;
    msg.sender_id = NODE_ID;
    msg.receiver_id = 2;  // Send to node 2
    msg.sequence = sequence_number++;
    msg.timestamp = millis();
    msg.rssi = 0;
    msg.data_length = 8;
    
    // Simulate sensor data (e.g., temperature and humidity)
    float temperature = 25.5 + (random(-10, 10) / 10.0);
    uint16_t humidity = 50 + random(-5, 5);
    memcpy(msg.payload, &temperature, 4);
    memcpy(msg.payload + 4, &humidity, 2);
    
    // Send message
    bool success = ESPNowUtils::sendMessage(receiver_mac, msg);
    
    if (success) {
        Serial.printf("Data sent - Temp: %.1f°C, Humidity: %d%%\n", temperature, humidity);
    } else {
        Serial.println("Failed to send data");
    }
}

// ===== Main Setup =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n=== ESP-NOW Sender Node ===");
    Serial.printf("Node ID: %d\n", NODE_ID);
    
    setupWiFi();
    setupESPNow();
    
    Serial.println("Sender node initialized!");
}

// ===== Main Loop =====
void loop() {
    uint32_t current_time = millis();
    
    if (current_time - last_send_time >= SEND_INTERVAL) {
        sendSensorData();
        last_send_time = current_time;
    }
    
    delay(100);
}
