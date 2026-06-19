#ifndef OTA_PORTAL_H
#define OTA_PORTAL_H

// ============================================================================
// Over-the-air firmware updater (no router needed).
//
// When triggered (USB `ota` command), the node switches its radio into a Wi-Fi
// SoftAP and serves a one-page upload form. The coach joins the "SpeedX-OTA-..."
// network, opens http://192.168.4.1/, and uploads a new firmware.bin; the device
// flashes it with Update.h and reboots into the new build.
//
// Because OTA takes over the radio, the node leaves the ESP-NOW mesh while the
// portal is active (until it reboots). Requires an OTA-capable partition table
// (the default Arduino-ESP32 4 MB layout has app0/app1, so this works as-is).
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

class OtaPortal {
public:
    void begin(const String& apName, const String& password = "speedxota") {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apName.c_str(), password.c_str());
        IPAddress ip = WiFi.softAPIP();

        server_.on("/", HTTP_GET, [this]() {
            server_.send(200, "text/html", page());
        });
        server_.on("/update", HTTP_POST,
            [this]() {
                bool ok = !Update.hasError();
                server_.send(200, "text/html",
                             ok ? F("<h2>Update OK - rebooting...</h2>")
                                : F("<h2>Update FAILED</h2>"));
                delay(1500);
                if (ok) ESP.restart();
            },
            [this]() { handleUpload(); });
        server_.begin();
        active_ = true;

        Serial.printf("OTA portal up. Join Wi-Fi '%s' (pass '%s'), open http://%s/\n",
                      apName.c_str(), password.c_str(), ip.toString().c_str());
    }

    void handle() {
        if (active_) server_.handleClient();
    }

    bool active() const { return active_; }

private:
    WebServer server_{80};
    bool active_ = false;

    static String page() {
        return F("<!doctype html><html><head><meta name=viewport "
                 "content='width=device-width,initial-scale=1'><title>SpeedX OTA</title>"
                 "<style>body{font-family:-apple-system,sans-serif;background:#0c1018;color:#e8edf5;"
                 "display:flex;min-height:100vh;align-items:center;justify-content:center;margin:0}"
                 "form{background:#161c28;padding:28px;border-radius:14px;border:1px solid #2a3445;text-align:center}"
                 "h1{font-size:18px;letter-spacing:1px}h1 span{color:#36d399}"
                 "input[type=file]{margin:16px 0;color:#8a98ad;width:100%}"
                 "button{background:#36d399;color:#05140d;border:0;padding:10px 18px;border-radius:8px;"
                 "font-weight:600;cursor:pointer}</style></head><body>"
                 "<form method='POST' action='/update' enctype='multipart/form-data'>"
                 "<h1>SPEED<span>X</span> firmware update</h1>"
                 "<input type='file' name='firmware' accept='.bin' required><br>"
                 "<button type='submit'>Upload &amp; flash</button></form></body></html>");
    }

    void handleUpload() {
        HTTPUpload& up = server_.upload();
        if (up.status == UPLOAD_FILE_START) {
            Serial.printf("OTA: receiving %s\n", up.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        } else if (up.status == UPLOAD_FILE_WRITE) {
            if (Update.write(up.buf, up.currentSize) != up.currentSize) {
                Update.printError(Serial);
            }
        } else if (up.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("OTA: %u bytes written, OK\n", static_cast<unsigned>(up.totalSize));
            } else {
                Update.printError(Serial);
            }
        }
    }
};

#endif
