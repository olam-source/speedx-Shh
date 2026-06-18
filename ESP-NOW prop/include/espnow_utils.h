#ifndef ESPNOW_UTILS_H
#define ESPNOW_UTILS_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "espnow_config.h"

class ESPNowUtils {
public:
    static const uint8_t* broadcastMac() {
        static const uint8_t mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        return mac;
    }

    static bool isBroadcastMac(const uint8_t* mac) {
        return mac && memcmp(mac, broadcastMac(), 6) == 0;
    }

    // Initialize WiFi for ESP-NOW.
    static bool initWiFi(const char* hostname, uint8_t channel = ESPNOW_CHANNEL) {
        WiFi.mode(WIFI_MODE_STA);
        WiFi.setSleep(false);
        WiFi.setHostname(hostname);
        WiFi.disconnect();
        delay(100);

        esp_err_t channel_result = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        if (channel_result != ESP_OK) {
            Serial.printf("Failed to set WiFi channel %u: %d\n", channel, channel_result);
            return false;
        }

        return true;
    }

    // Initialize ESP-NOW protocol.
    static bool initESPNow() {
        esp_err_t result = esp_now_init();
        if (result != ESP_OK) {
            Serial.printf("Error initializing ESP-NOW: %d\n", result);
            return false;
        }
        Serial.println("ESP-NOW initialized successfully");
        return true;
    }

    // Add a peer for communication.
    static bool addPeer(const uint8_t* peer_mac, uint8_t channel = ESPNOW_CHANNEL, bool encrypt = false, const uint8_t* lmk = nullptr) {
        if (peer_mac == nullptr) {
            return false;
        }

        if (esp_now_is_peer_exist(peer_mac)) {
            return true;
        }

        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, peer_mac, 6);
        peerInfo.channel = channel;
        peerInfo.ifidx = WIFI_IF_STA;
        peerInfo.encrypt = encrypt;

        if (encrypt && lmk != nullptr) {
            memcpy(peerInfo.lmk, lmk, sizeof(peerInfo.lmk));
        }

        esp_err_t result = esp_now_add_peer(&peerInfo);
        if (result != ESP_OK && result != ESP_ERR_ESPNOW_EXIST) {
            Serial.printf("Failed to add peer %s: %d\n", macToString(peer_mac).c_str(), result);
            return false;
        }

        Serial.printf("Peer ready: %s\n", macToString(peer_mac).c_str());
        return true;
    }

    static bool addBroadcastPeer(uint8_t channel = ESPNOW_CHANNEL) {
        return addPeer(broadcastMac(), channel);
    }

    static bool ensurePeer(const uint8_t* peer_mac, uint8_t channel = ESPNOW_CHANNEL) {
        if (isBroadcastMac(peer_mac)) {
            return addBroadcastPeer(channel);
        }
        return addPeer(peer_mac, channel);
    }

    static size_t messageSize(const ESPNowMessage& msg) {
        uint16_t payload_length = msg.data_length;
        if (payload_length > MAX_PAYLOAD_SIZE) {
            payload_length = MAX_PAYLOAD_SIZE;
        }
        return ESPNOW_HEADER_SIZE + payload_length;
    }

    static bool parseMessage(const uint8_t* data, int data_len, ESPNowMessage& msg) {
        if (data == nullptr || data_len < static_cast<int>(ESPNOW_HEADER_SIZE) || data_len > static_cast<int>(ESPNOW_MAX_FRAME_SIZE)) {
            return false;
        }

        memset(&msg, 0, sizeof(msg));
        memcpy(&msg, data, data_len);

        if (msg.data_length > MAX_PAYLOAD_SIZE) {
            return false;
        }

        return data_len >= static_cast<int>(ESPNOW_HEADER_SIZE + msg.data_length);
    }

    static bool setPayload(ESPNowMessage& msg, const void* payload, uint16_t payload_length) {
        if (payload_length > MAX_PAYLOAD_SIZE || (payload_length > 0 && payload == nullptr)) {
            return false;
        }

        msg.data_length = payload_length;
        memset(msg.payload, 0, sizeof(msg.payload));
        if (payload_length > 0) {
            memcpy(msg.payload, payload, payload_length);
        }
        return true;
    }

    // Send a message to a peer.
    static bool sendMessage(const uint8_t* peer_mac, ESPNowMessage msg) {
        if (peer_mac == nullptr || !ensurePeer(peer_mac)) {
            return false;
        }

        if (msg.data_length > MAX_PAYLOAD_SIZE) {
            msg.data_length = MAX_PAYLOAD_SIZE;
        }

        esp_err_t result = esp_now_send(peer_mac, reinterpret_cast<uint8_t*>(&msg), messageSize(msg));
        if (result != ESP_OK) {
            Serial.printf("Send failed to %s: %d\n", macToString(peer_mac).c_str(), result);
            return false;
        }
        return true;
    }

    // Broadcast message to all peers.
    static bool broadcastMessage(ESPNowMessage msg) {
        msg.receiver_id = msg.receiver_id == 0 ? ESPNOW_BROADCAST_ID : msg.receiver_id;
        return sendMessage(broadcastMac(), msg);
    }

    // Convert MAC address to string.
    static String macToString(const uint8_t* mac) {
        if (mac == nullptr) {
            return String("<null>");
        }

        char buffer[18];
        snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return String(buffer);
    }

    // Get device's MAC address.
    static void getDeviceMac(uint8_t* mac) {
        WiFi.macAddress(mac);
    }

    // Calculate RSSI strength percentage.
    static uint8_t rssiToPercentage(int8_t rssi) {
        if (rssi <= -100) return 0;
        if (rssi >= -50) return 100;
        return static_cast<uint8_t>(2 * (rssi + 100));
    }

    static const char* messageTypeToString(MessageType type) {
        switch (type) {
            case MessageType::PING: return "PING";
            case MessageType::PONG: return "PONG";
            case MessageType::DATA: return "DATA";
            case MessageType::CONFIG: return "CONFIG";
            case MessageType::ACK: return "ACK";
            case MessageType::ERROR: return "ERROR";
            case MessageType::MESH_BEACON: return "MESH_BEACON";
            case MessageType::MESH_DATA: return "MESH_DATA";
            default: return "UNKNOWN";
        }
    }

    static ESPNowMessage createMessage(MessageType type, uint8_t sender_id, uint8_t receiver_id, uint8_t sequence) {
        ESPNowMessage msg{};
        msg.type = type;
        msg.sender_id = sender_id;
        msg.receiver_id = receiver_id;
        msg.sequence = sequence;
        msg.timestamp = millis();
        msg.rssi = 0;
        msg.data_length = 0;
        msg.ttl = ESPNOW_DEFAULT_TTL;
        msg.hop_count = 0;
        msg.flags = 0;
        return msg;
    }

    // Create a ping message.
    static ESPNowMessage createPingMessage(uint8_t sender_id, uint8_t receiver_id, uint8_t sequence) {
        ESPNowMessage msg = createMessage(MessageType::PING, sender_id, receiver_id, sequence);
        msg.flags = ESPNOW_FLAG_ACK_REQUESTED;
        return msg;
    }

    static ESPNowMessage createPongMessage(uint8_t sender_id, uint8_t receiver_id, uint8_t sequence) {
        ESPNowMessage msg = createMessage(MessageType::PONG, sender_id, receiver_id, sequence);
        return msg;
    }

    // Create an ACK message.
    static ESPNowMessage createAckMessage(uint8_t sender_id, uint8_t receiver_id, uint8_t ack_sequence) {
        ESPNowMessage msg = createMessage(MessageType::ACK, sender_id, receiver_id, ack_sequence);
        return msg;
    }

    static ESPNowMessage createDataMessage(uint8_t sender_id, uint8_t receiver_id, uint8_t sequence, const void* payload, uint16_t payload_length) {
        ESPNowMessage msg = createMessage(MessageType::DATA, sender_id, receiver_id, sequence);
        msg.flags = ESPNOW_FLAG_ACK_REQUESTED;
        setPayload(msg, payload, payload_length);
        return msg;
    }

    static ESPNowMessage createMeshDataMessage(uint8_t sender_id, uint8_t receiver_id, uint8_t sequence, const void* payload, uint16_t payload_length) {
        ESPNowMessage msg = createMessage(MessageType::MESH_DATA, sender_id, receiver_id, sequence);
        msg.flags = ESPNOW_FLAG_ACK_REQUESTED;
        setPayload(msg, payload, payload_length);
        return msg;
    }

    static ESPNowMessage createBeaconMessage(uint8_t sender_id, uint8_t sequence, uint8_t active_peer_count) {
        uint8_t payload[2] = {1, active_peer_count};
        ESPNowMessage msg = createMessage(MessageType::MESH_BEACON, sender_id, ESPNOW_BROADCAST_ID, sequence);
        setPayload(msg, payload, sizeof(payload));
        return msg;
    }

    static void printMessageSummary(const ESPNowMessage& msg) {
        Serial.printf("%s Node %u -> Node %u | Seq: %u | Len: %u | TTL: %u | Hops: %u\n",
                      messageTypeToString(msg.type),
                      msg.sender_id,
                      msg.receiver_id,
                      msg.sequence,
                      msg.data_length,
                      msg.ttl,
                      msg.hop_count);
    }
};

#endif
