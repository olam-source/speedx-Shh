#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "espnow_config.h"
#include "espnow_utils.h"
#include "peer_manager.h"

// ===== Configuration =====
#define NODE_ID 3
#define BEACON_INTERVAL 10000  // Send beacon every 10 seconds
#define PEER_TIMEOUT_MS 30000  // 30 seconds

PeerManager peer_manager;
uint32_t last_beacon_time = 0;
uint32_t last_peer_check = 0;
uint8_t beacon_sequence = 0;

// ===== Callback Functions =====
void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    // Uncomment for debugging
    // Serial.printf("Send to %s: %s\n", ESPNowUtils::macToString(mac_addr).c_str(),
    //              status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failed");
}

void onDataReceived(const uint8_t* mac, const uint8_t* data, int data_len) {
    if (data_len != sizeof(ESPNowMessage)) {
        Serial.println("Invalid message size");
        return;
    }

    ESPNowMessage* msg = (ESPNowMessage*)data;
    uint8_t rssi_percent = ESPNowUtils::rssiToPercentage(msg->rssi);
    
    Serial.printf("\nMesh Message: Node %d -> Node %d | Type: %d | RSSI: %d%%\n",
                  msg->sender_id, msg->receiver_id, (uint8_t)msg->type, rssi_percent);

    // Update peer information
    peer_manager.registerPeer(mac, msg->sender_id);
    peer_manager.updatePeerRssi(mac, msg->rssi);

    // Forward message to next hop if not destined for this node
    if (msg->receiver_id != NODE_ID && msg->receiver_id != 0xFF) {
        // Message forwarding logic can be implemented here
        Serial.println("Forwarding message...");
    }

    // Send ACK if this is the destination
    if (msg->receiver_id == NODE_ID) {
        ESPNowMessage ack = ESPNowUtils::createAckMessage(NODE_ID, msg->sequence);
        ESPNowUtils::sendMessage(mac, ack);
    }
}

// ===== Beacon Functions =====
void sendBeacon() {
    ESPNowMessage beacon{};
    beacon.type = MessageType::MESH_BEACON;
    beacon.sender_id = NODE_ID;
    beacon.receiver_id = 0xFF;  // Broadcast
    beacon.sequence = beacon_sequence++;
    beacon.timestamp = millis();
    beacon.rssi = 0;
    beacon.data_length = 2;
    
    // Payload: node status (active = 1)
    beacon.payload[0] = 1;
    beacon.payload[1] = peer_manager.getActivePeerCount();
    
    // Broadcast beacon
    bool success = ESPNowUtils::broadcastMessage(beacon);
    
    if (success) {
        Serial.printf("Beacon sent (Seq: %d, Active peers: %d)\n", 
                     beacon.sequence, peer_manager.getActivePeerCount());
    }
}

// ===== Initialization Functions =====
void setupWiFi() {
    ESPNowUtils::initWiFi("ESP32_Mesh_Node");
    
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
    
    // Add broadcast peer (not strictly necessary but recommended)
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ESPNowUtils::addPeer(broadcast_mac);
}

// ===== Main Setup =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n=== ESP-NOW Mesh Node ===");
    Serial.printf("Node ID: %d\n", NODE_ID);
    
    setupWiFi();
    setupESPNow();
    
    Serial.println("Mesh node initialized!");
}

// ===== Main Loop =====
void loop() {
    uint32_t current_time = millis();
    
    // Send beacon periodically
    if (current_time - last_beacon_time >= BEACON_INTERVAL) {
        sendBeacon();
        last_beacon_time = current_time;
    }
    
    // Check for peer timeouts every 5 seconds
    if (current_time - last_peer_check >= 5000) {
        peer_manager.checkPeerTimeout(PEER_TIMEOUT_MS);
        Serial.printf("Network Status - Active peers: %d\n", peer_manager.getActivePeerCount());
        last_peer_check = current_time;
    }
    
    delay(100);
}
