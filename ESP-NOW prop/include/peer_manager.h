#ifndef PEER_MANAGER_H
#define PEER_MANAGER_H

#include <Arduino.h>
#include "espnow_config.h"

class PeerManager {
private:
    PeerInfo peers[MAX_PEERS];
    uint8_t peer_count;
    
public:
    PeerManager() : peer_count(0) {
        memset(peers, 0, sizeof(peers));
    }

    // Register a new peer
    bool registerPeer(const uint8_t* mac, uint8_t peer_id) {
        if (peer_count >= MAX_PEERS) {
            return false;
        }
        
        for (int i = 0; i < peer_count; i++) {
            if (memcmp(peers[i].mac, mac, 6) == 0) {
                return true;  // Peer already registered
            }
        }

        memcpy(peers[peer_count].mac, mac, 6);
        peers[peer_count].peer_id = peer_id;
        peers[peer_count].is_active = true;
        peers[peer_count].last_seen = millis();
        peers[peer_count].rssi = -100;
        peers[peer_count].message_count = 0;
        
        peer_count++;
        return true;
    }

    // Get peer by ID
    PeerInfo* getPeerById(uint8_t peer_id) {
        for (int i = 0; i < peer_count; i++) {
            if (peers[i].peer_id == peer_id && peers[i].is_active) {
                return &peers[i];
            }
        }
        return nullptr;
    }

    // Get peer by MAC address
    PeerInfo* getPeerByMac(const uint8_t* mac) {
        for (int i = 0; i < peer_count; i++) {
            if (memcmp(peers[i].mac, mac, 6) == 0) {
                return &peers[i];
            }
        }
        return nullptr;
    }

    // Update peer RSSI
    void updatePeerRssi(const uint8_t* mac, int8_t rssi) {
        PeerInfo* peer = getPeerByMac(mac);
        if (peer) {
            peer->rssi = rssi;
            peer->last_seen = millis();
            peer->message_count++;
        }
    }

    // Remove peer
    bool removePeer(uint8_t peer_id) {
        for (int i = 0; i < peer_count; i++) {
            if (peers[i].peer_id == peer_id) {
                peers[i].is_active = false;
                return true;
            }
        }
        return false;
    }

    // Get all active peers
    uint8_t getActivePeerCount() const {
        uint8_t count = 0;
        for (int i = 0; i < peer_count; i++) {
            if (peers[i].is_active) count++;
        }
        return count;
    }

    // Check if peer is still alive (timeout)
    bool checkPeerTimeout(uint32_t timeout_ms) {
        uint32_t current_time = millis();
        for (int i = 0; i < peer_count; i++) {
            if (peers[i].is_active) {
                if ((current_time - peers[i].last_seen) > timeout_ms) {
                    peers[i].is_active = false;
                }
            }
        }
        return true;
    }

    // Print all peers
    void printPeers() {
        Serial.println("\n=== Active Peers ===");
        for (int i = 0; i < peer_count; i++) {
            if (peers[i].is_active) {
                Serial.printf("ID: %d | RSSI: %d | Messages: %d\n", 
                            peers[i].peer_id, peers[i].rssi, peers[i].message_count);
            }
        }
        Serial.println("===================\n");
    }
};

#endif
