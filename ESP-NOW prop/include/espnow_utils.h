#ifndef ESPNOW_UTILS_H
#define ESPNOW_UTILS_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "espnow_config.h"

class ESPNowUtils {
public:
    // Initialize WiFi for ESP-NOW
    static bool initWiFi(const char* hostname) {
        WiFi.mode(WIFI_MODE_STA);
        WiFi.setHostname(hostname);
        WiFi.disconnect();
        delay(100);
        return true;
    }

    // Initialize ESP-NOW protocol
    static bool initESPNow() {
        if (esp_now_init() != ESP_OK) {
            Serial.println("Error initializing ESP-NOW");
            return false;
        }
        Serial.println("ESP-NOW initialized successfully");
        return true;
    }

    // Add a peer for communication
    static bool addPeer(const uint8_t* peer_mac, uint8_t channel = ESPNOW_CHANNEL) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, peer_mac, 6);
        peerInfo.channel = channel;
        peerInfo.encrypt = false;

        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
            Serial.println("Failed to add peer");
            return false;
        }
        Serial.println("Peer added successfully");
        return true;
    }

    // Send a message to a peer
    static bool sendMessage(const uint8_t* peer_mac, const ESPNowMessage& msg) {
        esp_err_t result = esp_now_send(peer_mac, (uint8_t*)&msg, sizeof(ESPNowMessage));
        return result == ESP_OK;
    }

    // Broadcast message to all peers
    static bool broadcastMessage(const ESPNowMessage& msg) {
        uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        return sendMessage(broadcastMac, msg);
    }

    // Convert MAC address to string
    static String macToString(const uint8_t* mac) {
        char buffer[18];
        snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return String(buffer);
    }

    // Get device's MAC address
    static void getDeviceMac(uint8_t* mac) {
        WiFi.macAddress(mac);
    }

    // Calculate RSSI strength percentage
    static uint8_t rssiToPercentage(int8_t rssi) {
        if (rssi <= -100) return 0;
        if (rssi >= -50) return 100;
        return (int8_t)(2 * (rssi + 100));
    }

    // Create a ping message
    static ESPNowMessage createPingMessage(uint8_t sender_id, uint8_t sequence) {
        ESPNowMessage msg{};
        msg.type = MessageType::PING;
        msg.sender_id = sender_id;
        msg.sequence = sequence;
        msg.timestamp = millis();
        msg.rssi = 0;
        msg.data_length = 0;
        return msg;
    }

    // Create an ACK message
    static ESPNowMessage createAckMessage(uint8_t sender_id, uint8_t ack_sequence) {
        ESPNowMessage msg{};
        msg.type = MessageType::ACK;
        msg.sender_id = sender_id;
        msg.sequence = ack_sequence;
        msg.timestamp = millis();
        msg.rssi = 0;
        msg.data_length = 0;
        return msg;
    }
};

#endif
