#ifndef ESPNOW_CRYPTO_H
#define ESPNOW_CRYPTO_H

// ============================================================================
// Application-layer encryption for SpeedX frames.
//
// ESP-NOW's built-in encryption is per-peer/unicast only (and broadcast can't be
// encrypted at all), which doesn't fit a broadcast gossip mesh. So instead we
// authenticate-and-encrypt the *payload* of every frame with AES-256-GCM, using
// a shared network key derived from a passphrase. The frame header stays in the
// clear so relays can route/dedupe without decrypting; the immutable header
// fields are bound in as GCM additional-authenticated-data (AAD), so they can't
// be tampered with either.
//
// On the wire the payload becomes:  [12-byte IV][ciphertext][16-byte GCM tag]
// A frame that fails authentication is dropped — which also isolates the mesh
// from foreign/unkeyed traffic.
//
// Enable/disable fleet-wide with -DESPNOW_ENCRYPT=1/0 (default ON). Set your own
// key with -DESPNOW_PSK_PASSPHRASE="your team secret" in platformio.ini [env].
// ============================================================================

#include <Arduino.h>
#include <string.h>
#include "espnow_config.h"

#ifndef ESPNOW_ENCRYPT
#define ESPNOW_ENCRYPT 1
#endif

#ifndef ESPNOW_PSK_PASSPHRASE
#define ESPNOW_PSK_PASSPHRASE "speedx-default-change-me"
#endif

#if ESPNOW_ENCRYPT
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>
#endif

class EspNowCrypto {
public:
    static constexpr size_t IV_LEN = 12;
    static constexpr size_t TAG_LEN = 16;
    static constexpr size_t OVERHEAD = IV_LEN + TAG_LEN;   // 28 bytes per frame

    static bool enabled() {
#if ESPNOW_ENCRYPT
        return true;
#else
        return false;
#endif
    }

#if ESPNOW_ENCRYPT
    // Derive the 256-bit key from the passphrase (once).
    static void init() {
        if (keyReady()) return;
        const char* pass = ESPNOW_PSK_PASSPHRASE;
        mbedtls_sha256(reinterpret_cast<const unsigned char*>(pass), strlen(pass), key(), 0);
        keyReady() = true;
    }

    // Encrypt msg.payload[0..data_length) in place -> IV || ciphertext || tag.
    static bool encrypt(ESPNowMessage& msg) {
        init();
        uint16_t pt_len = msg.data_length;
        if (pt_len > MAX_PAYLOAD_SIZE - OVERHEAD) return false;

        uint8_t iv[IV_LEN];
        esp_fill_random(iv, IV_LEN);

        uint8_t pt[MAX_PAYLOAD_SIZE];
        memcpy(pt, msg.payload, pt_len);

        uint8_t aad[6] = {static_cast<uint8_t>(msg.type),
                          static_cast<uint8_t>(msg.sender_id & 0xFF), static_cast<uint8_t>(msg.sender_id >> 8),
                          static_cast<uint8_t>(msg.receiver_id & 0xFF), static_cast<uint8_t>(msg.receiver_id >> 8),
                          msg.sequence};
        uint8_t ct[MAX_PAYLOAD_SIZE];
        uint8_t tag[TAG_LEN];

        mbedtls_gcm_context g;
        mbedtls_gcm_init(&g);
        int rc = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key(), 256);
        if (rc == 0) {
            rc = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, pt_len,
                                           iv, IV_LEN, aad, sizeof(aad),
                                           pt, ct, TAG_LEN, tag);
        }
        mbedtls_gcm_free(&g);
        if (rc != 0) return false;

        memcpy(msg.payload, iv, IV_LEN);
        memcpy(msg.payload + IV_LEN, ct, pt_len);
        memcpy(msg.payload + IV_LEN + pt_len, tag, TAG_LEN);
        msg.data_length = IV_LEN + pt_len + TAG_LEN;
        return true;
    }

    // Verify + decrypt in place. On success rewrites payload to plaintext and
    // restores data_length; returns false (drop) if authentication fails.
    static bool decrypt(ESPNowMessage& msg) {
        init();
        uint16_t wire = msg.data_length;
        if (wire < OVERHEAD || wire > MAX_PAYLOAD_SIZE) return false;
        uint16_t pt_len = wire - OVERHEAD;

        uint8_t iv[IV_LEN];
        memcpy(iv, msg.payload, IV_LEN);
        uint8_t ct[MAX_PAYLOAD_SIZE];
        memcpy(ct, msg.payload + IV_LEN, pt_len);
        uint8_t tag[TAG_LEN];
        memcpy(tag, msg.payload + IV_LEN + pt_len, TAG_LEN);

        uint8_t aad[6] = {static_cast<uint8_t>(msg.type),
                          static_cast<uint8_t>(msg.sender_id & 0xFF), static_cast<uint8_t>(msg.sender_id >> 8),
                          static_cast<uint8_t>(msg.receiver_id & 0xFF), static_cast<uint8_t>(msg.receiver_id >> 8),
                          msg.sequence};
        uint8_t pt[MAX_PAYLOAD_SIZE];

        mbedtls_gcm_context g;
        mbedtls_gcm_init(&g);
        int rc = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key(), 256);
        if (rc == 0) {
            rc = mbedtls_gcm_auth_decrypt(&g, pt_len, iv, IV_LEN, aad, sizeof(aad),
                                          tag, TAG_LEN, ct, pt);
        }
        mbedtls_gcm_free(&g);
        if (rc != 0) return false;

        memcpy(msg.payload, pt, pt_len);
        memset(msg.payload + pt_len, 0, sizeof(msg.payload) - pt_len);
        msg.data_length = pt_len;
        return true;
    }

private:
    // Function-local statics keep this ODR-safe even if the header is ever
    // included in more than one translation unit.
    static uint8_t* key() { static uint8_t k[32] = {0}; return k; }
    static bool& keyReady() { static bool r = false; return r; }
#endif
};

#endif
