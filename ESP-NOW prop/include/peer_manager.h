#ifndef PEER_MANAGER_H
#define PEER_MANAGER_H

#include <Arduino.h>
#include "espnow_config.h"

class PeerManager {
private:
    PeerInfo peers[ESPNOW_MAX_PEERS];
    uint8_t peer_count;

    int findSlotByMac(const uint8_t* mac) const {
        for (int i = 0; i < peer_count; i++) {
            if (memcmp(peers[i].mac, mac, 6) == 0) {
                return i;
            }
        }
        return -1;
    }

    int findReusableSlot() const {
        for (int i = 0; i < peer_count; i++) {
            if (!peers[i].is_active) {
                return i;
            }
        }
        return peer_count < ESPNOW_MAX_PEERS ? peer_count : -1;
    }

public:
    PeerManager() : peer_count(0) {
        memset(peers, 0, sizeof(peers));
    }

    // Register a new peer or refresh an existing one.
    bool registerPeer(const uint8_t* mac, node_id_t peer_id) {
        if (mac == nullptr) {
            return false;
        }

        int existing = findSlotByMac(mac);
        if (existing >= 0) {
            peers[existing].peer_id = peer_id;
            peers[existing].is_active = true;
            peers[existing].last_seen = millis();
            return true;
        }

        int slot = findReusableSlot();
        if (slot < 0) {
            return false;
        }

        memcpy(peers[slot].mac, mac, 6);
        peers[slot].peer_id = peer_id;
        peers[slot].is_active = true;
        peers[slot].last_seen = millis();
        peers[slot].rssi = -100;
        peers[slot].message_count = 0;
        peers[slot].last_sequence = 0;

        if (slot == peer_count) {
            peer_count++;
        }
        return true;
    }

    // Get peer by ID.
    PeerInfo* getPeerById(node_id_t peer_id) {
        for (int i = 0; i < peer_count; i++) {
            if (peers[i].peer_id == peer_id && peers[i].is_active) {
                return &peers[i];
            }
        }
        return nullptr;
    }

    // Get peer by MAC address.
    PeerInfo* getPeerByMac(const uint8_t* mac) {
        int slot = findSlotByMac(mac);
        return slot >= 0 ? &peers[slot] : nullptr;
    }

    // Update peer telemetry after receiving a message.
    void updatePeerActivity(const uint8_t* mac, int8_t rssi, uint8_t sequence) {
        PeerInfo* peer = getPeerByMac(mac);
        if (peer) {
            peer->rssi = rssi;
            peer->last_seen = millis();
            peer->message_count++;
            peer->last_sequence = sequence;
        }
    }


    // Get all active peers.
    uint8_t getActivePeerCount() const {
        uint8_t count = 0;
        for (int i = 0; i < peer_count; i++) {
            if (peers[i].is_active) count++;
        }
        return count;
    }

    // Mark peers that have not sent a frame within timeout_ms as inactive.
    void checkPeerTimeout(uint32_t timeout_ms) {
        uint32_t current_time = millis();
        for (int i = 0; i < peer_count; i++) {
            if (peers[i].is_active && (current_time - peers[i].last_seen) > timeout_ms) {
                peers[i].is_active = false;
            }
        }
    }

    PeerInfo* getActivePeerByIndex(uint8_t active_index) {
        for (int i = 0, seen = 0; i < peer_count; i++) {
            if (peers[i].is_active && seen++ == active_index) return &peers[i];
        }
        return nullptr;
    }

    // Print all peers.
    void printPeers() {
        Serial.println("\n=== Active Peers ===");
        for (int i = 0; i < peer_count; i++) {
            if (peers[i].is_active) {
                Serial.printf("ID: %u | MAC: %02X:%02X:%02X:%02X:%02X:%02X | RSSI: %d | Messages: %u | Age: %lums\n",
                              peers[i].peer_id,
                              peers[i].mac[0], peers[i].mac[1], peers[i].mac[2],
                              peers[i].mac[3], peers[i].mac[4], peers[i].mac[5],
                              peers[i].rssi, peers[i].message_count,
                              static_cast<unsigned long>(millis() - peers[i].last_seen));
            }
        }
        Serial.println("===================\n");
    }
};

#endif
