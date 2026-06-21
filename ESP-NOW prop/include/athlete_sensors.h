#ifndef ATHLETE_SENSORS_H
#define ATHLETE_SENSORS_H

// ============================================================================
// AthleteSensors  -  hardware abstraction for a SpeedX wearable.
//
// Two interchangeable back ends, selected at build time:
//
//   * default (no flag) -> simulated metrics, so the mesh/dashboard can be
//     tested on a bare ESP32 with no sensors attached.
//   * -DUSE_REAL_SENSORS -> reads real hardware:
//        MAX30102  optical heart rate / SpO2 sensor (I2C)
//        MPU-6050  6-axis IMU for cadence/stride/dynamics (I2C)
//        u-blox NEO-M8N (or NEO-6M) GPS for distance/pace/splits (UART2)
//        battery   ADC voltage divider (e.g. LOLIN D32 onboard, GPIO35)
//
// main.cpp always calls the same three things:
//   sensors.begin();                // in setup()
//   sensors.update();               // every loop() iteration
//   sensors.fillMetrics(payload);   // when building a metrics frame
// plus onSessionStart() / markLap() / resetBest() for coach commands.
// ============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include "espnow_config.h"

// ============================================================================
// BleHrReader — pairs once with any standard BLE Heart Rate strap, stores the
// MAC in NVS, and auto-reconnects on every subsequent boot. Works in parallel
// with the athlete's Garmin/Coros watch: those receive the same strap's ANT+
// broadcast simultaneously — zero disruption to existing tracking workflows.
//
// Supported straps (anything advertising BLE Heart Rate Service 0x180D):
//   CooSpo H6 / H808S, Polar H9 / H10, Garmin HRM-Dual / HRM-Pro,
//   Wahoo TICKR, Magene H64, and most generic ANT+/BLE chest straps.
//
// First-time setup (one command, one time per athlete):
//   1. Flash the wearable.
//   2. Open serial monitor, type:  strap-reset
//   3. Put the HR strap on the athlete and power it on.
//   4. ESP32 finds it, bonds, prints "BLE HR strap bonded".  Done.
//      Every boot after this auto-connects with no user action.
// ============================================================================
#if defined(USE_BLE_HR)
#include <NimBLEDevice.h>

class BleHrReader : public NimBLEClientCallbacks {
public:
    // Call once from setup(), after Preferences::begin().
    void begin(Preferences& prefs) {
        prefs_ = &prefs;
        NimBLEDevice::init("");
        NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);

        String addr = prefs_->getString("strap_addr", "");
        if (addr.length() == 17) {
            storedAddr_ = addr.c_str();
            storedType_ = prefs_->getUChar("strap_atype", BLE_ADDR_PUBLIC);
            hasMac_ = true;
            connectDirect();
        } else {
            startScan();
        }
    }

    // Call every loop() — drives reconnect after drop-outs.
    void update() {
        if (connected_) return;

        // A scan result arrived on the NimBLE task; connect from this task.
        if (foundDevice_) {
            foundDevice_ = false;
            doConnect(foundAddress_);
            return;
        }

        if (scanning_) return;
        if (millis() - lastAttemptMs_ < RECONNECT_MS) return;
        hasMac_ ? connectDirect() : startScan();
    }

    uint16_t hr()        const { return hr_; }
    bool     connected() const { return connected_; }

    // Called by `strap-reset` serial command. Clears stored MAC and rescans.
    void resetPairing(Preferences& prefs) {
        prefs.remove("strap_addr");
        prefs.remove("strap_atype");
        hasMac_ = false;
        storedAddr_.clear();
        if (client_) client_->disconnect();
        startScan();
    }

    String statusString() const {
        if (connected_) return "BLE HR connected, HR=" + String(hr_) + " bpm";
        if (scanning_)  return "BLE HR scanning for strap...";
        if (hasMac_)    return "BLE HR bonded, reconnecting...";
        return "BLE HR no strap paired — type 'strap-reset' to pair";
    }

private:
    static constexpr uint32_t RECONNECT_MS    = 5000;
    static constexpr uint32_t SCAN_DURATION_S = 8;

    Preferences*   prefs_       = nullptr;
    NimBLEClient*  client_      = nullptr;
    std::string    storedAddr_;
    uint8_t        storedType_  = BLE_ADDR_PUBLIC;
    bool           hasMac_      = false;
    volatile bool  connected_   = false;
    volatile bool  scanning_    = false;
    volatile bool  foundDevice_ = false;   // set on NimBLE task, read on app task
    NimBLEAddress  foundAddress_;
    volatile uint16_t hr_       = 0;
    uint32_t       lastAttemptMs_ = 0;

    // ---- NimBLEClientCallbacks (runs on NimBLE task) ----
    void onDisconnect(NimBLEClient*, int) override {
        connected_    = false;
        hr_           = 0;
        lastAttemptMs_ = millis();
    }

    // ---- Connection helpers (run on app task via update()) ----
    void connectDirect() {
        lastAttemptMs_ = millis();
        doConnect(NimBLEAddress(storedAddr_, storedType_));
    }

    void doConnect(NimBLEAddress addr) {
        client_ = NimBLEDevice::createClient();
        client_->setClientCallbacks(this, false);

        if (!client_->connect(addr)) {
            NimBLEDevice::deleteClient(client_);
            client_      = nullptr;
            lastAttemptMs_ = millis();
            return;
        }

        auto* svc = client_->getService("180d");
        auto* chr = svc ? svc->getCharacteristic("2a37") : nullptr;
        if (!chr || !chr->canNotify()) {
            client_->disconnect();
            return;
        }

        // Lambda capture: BleHrReader outlives all BLE objects so the pointer
        // is valid for the session lifetime.
        BleHrReader* self = this;
        chr->subscribe(true,
            [self](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
                if (len < 2) return;
                // BLE HRS wire format: flags byte, then HR as uint8 or uint16
                self->hr_ = (data[0] & 0x01)
                    ? (uint16_t)(data[1] | ((uint16_t)data[2] << 8))
                    : data[1];
            });

        // Persist on first successful bond
        if (!hasMac_) {
            storedAddr_ = addr.toString();
            storedType_ = (uint8_t)addr.getType();
            prefs_->putString("strap_addr", String(storedAddr_.c_str()));
            prefs_->putUChar("strap_atype", storedType_);
            hasMac_ = true;
            Serial.println("BLE HR strap bonded — auto-connects from now on");
            Serial.println("Type 'strap-reset' if you ever need to pair a different strap");
        }
        connected_ = true;
    }

    void startScan() {
        if (scanning_) return;
        scanning_      = true;
        lastAttemptMs_ = millis();
        auto* scan = NimBLEDevice::getScan();
        scan->setAdvertisedDeviceCallbacks(new ScanCB(this), true);
        scan->setActiveScan(false);   // passive is enough to spot the HR service UUID
        scan->start(SCAN_DURATION_S, [](NimBLEScanResults r) {
            NimBLEDevice::getScan()->clearResults();
        }, false);
    }

    // Runs on NimBLE task: just record the address; connect on the app task.
    struct ScanCB : NimBLEAdvertisedDeviceCallbacks {
        BleHrReader* r;
        explicit ScanCB(BleHrReader* rr) : r(rr) {}
        void onResult(NimBLEAdvertisedDevice* dev) override {
            if (!dev->isAdvertisingService(NimBLEUUID("180d"))) return;
            NimBLEDevice::getScan()->stop();
            r->foundAddress_ = dev->getAddress();
            r->foundDevice_  = true;
            r->scanning_     = false;
        }
    };
};
#endif // USE_BLE_HR

// ---- Pin / config defaults (override with -D build flags if your wiring differs)
#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA 21          // MAX30102 + MPU-6050 shared SDA
#endif
#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL 22          // MAX30102 + MPU-6050 shared SCL
#endif
#ifndef PIN_GPS_RX
#define PIN_GPS_RX 16           // ESP32 RX2  <- GPS TX
#endif
#ifndef PIN_GPS_TX
#define PIN_GPS_TX 17           // ESP32 TX2  -> GPS RX
#endif
#ifndef GPS_BAUD
#define GPS_BAUD 9600           // NEO-6M/M8N default
#endif
#ifndef PIN_BATTERY_ADC
#define PIN_BATTERY_ADC 35      // LOLIN D32 onboard battery sense
#endif
#ifndef BATTERY_DIVIDER
#define BATTERY_DIVIDER 2.0f    // 100k/100k divider -> multiply reading by 2
#endif

constexpr float MILE_METERS = 1609.344f;

#if defined(USE_REAL_SENSORS)
#include <Wire.h>
#include <MAX30105.h>           // SparkFun MAX3010x library
#include "heartRate.h"          // checkForBeat() helper from the same library
#include <TinyGPS++.h>          // TinyGPSPlus
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#endif

class AthleteSensors {
public:
#if defined(USE_BLE_HR)
    // Call once from setup(), after Preferences::begin() and sensor begin().
    void beginBle(Preferences& prefs) { ble_.begin(prefs); }

    // Called from `strap-reset` serial command.
    void resetBleStrap(Preferences& prefs) { ble_.resetPairing(prefs); }

    String bleStatus() const { return ble_.statusString(); }
#endif

    bool begin() {
#if defined(USE_REAL_SENSORS)
        bool ok = true;
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

        // Heart rate (MAX30102)
        if (hr_.begin(Wire, I2C_SPEED_FAST)) {
            hr_.setup();                       // sane defaults for HR
            hr_.setPulseAmplitudeRed(0x0A);    // low red LED (presence indicator)
            hr_.setPulseAmplitudeIR(0x20);     // IR LED used for PPG
            hr_.enableDIETEMPRDY();            // allow die-temperature reads
            hrReady_ = true;
        } else {
            Serial.println("WARN: MAX30102 not found");
            ok = false;
        }

        // IMU (MPU-6050)
        if (mpu_.begin()) {
            mpu_.setAccelerometerRange(MPU6050_RANGE_8_G);
            mpu_.setGyroRange(MPU6050_RANGE_500_DEG);
            mpu_.setFilterBandwidth(MPU6050_BAND_21_HZ);
            imuReady_ = true;
        } else {
            Serial.println("WARN: MPU-6050 not found");
            ok = false;
        }

        // GPS (NEO-6M / NEO-M8N) on UART2
        Serial2.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

        analogReadResolution(12);
        onSessionStart();
        return ok;
#else
        sessionStartMs_ = millis();
        bestSplitSec_ = 0xFFFF;
        return true;
#endif
    }

    // Pump streaming sensors. Call as often as possible from loop().
    void update() {
#if defined(USE_REAL_SENSORS)
        uint32_t now = millis();
        updateHeartRate(now);
        updateImu(now);
        updateGps(now);
        if (now - lastBatteryMs_ >= 5000) {
            lastBatteryMs_ = now;
            updateBattery();
        }
#endif
#if defined(USE_BLE_HR)
        ble_.update();  // drives BLE reconnect state machine
#endif
    }

    // Populate the sensor-derived fields of a metrics frame. Identity fields
    // (athlete_id, team_id, sample_id, uptime_ms) are set by the caller.
    void fillMetrics(AthleteMetricsPayload& m) {
#if defined(USE_REAL_SENSORS)
        fillReal(m);
#else
        fillSimulated(m);
#endif
#if defined(USE_BLE_HR)
        // BLE chest strap overrides MAX30102 / simulated HR when connected.
        // Falls back to whatever fillReal/fillSimulated set when strap is absent.
        if (ble_.connected()) {
            m.heart_rate_bpm = ble_.hr();
            m.flags |= ATHLETE_FLAG_HEART_RATE_VALID;
        }
#endif
    }

    // ---- Coach-command hooks ----
    void onSessionStart() {
        sessionStartMs_ = millis();
        bestSplitSec_ = 0xFFFF;
#if defined(USE_REAL_SENSORS)
        distanceM_ = 0.0f;
        elevationGainM_ = 0.0f;
        lastMileCount_ = 0;
        lastMileMs_ = millis();
        currentSplitSec_ = 0;
        haveLastFix_ = false;
        stepCount_ = 0;
        windowStartSteps_ = 0;
        windowStartDistance_ = 0.0f;
        windowStartMs_ = millis();
#endif
    }
    void resetBest() { bestSplitSec_ = 0xFFFF; }
    void markLap()   { forceSplit_ = true; }

private:
#if defined(USE_BLE_HR)
    BleHrReader ble_;
#endif

    uint32_t sessionStartMs_ = 0;
    uint16_t bestSplitSec_ = 0xFFFF;
    bool forceSplit_ = false;

#if !defined(USE_REAL_SENSORS)
    // -------- Simulated back end (no hardware required) --------
    void fillSimulated(AthleteMetricsPayload& m) {
        uint32_t now = millis();
        uint32_t session_ms = now - sessionStartMs_;
        uint16_t distance_m = static_cast<uint16_t>((session_ms / 1000UL) * 4UL);
        uint16_t current_split = static_cast<uint16_t>(360 + random(-18, 19));
        if (current_split < bestSplitSec_) {
            bestSplitSec_ = current_split;
        }

        m.heart_rate_bpm = static_cast<uint16_t>(152 + random(-8, 9));
        m.pace_sec_per_mile = static_cast<uint16_t>(current_split + random(-5, 6));
        m.current_split_sec = current_split;
        m.best_split_sec = bestSplitSec_;
        m.distance_m = distance_m;
        m.speed_cm_s = static_cast<uint16_t>(430 + random(-25, 26));
        m.cadence_spm = static_cast<uint16_t>(176 + random(-6, 7));
        m.stride_length_cm = static_cast<uint16_t>(145 + random(-8, 9));
        m.ground_contact_ms = static_cast<uint16_t>(215 + random(-12, 13));
        m.vertical_oscillation_cm = static_cast<uint16_t>(8 + random(0, 4));
        m.elevation_gain_m = static_cast<int16_t>(random(-2, 5));
        m.skin_temp_c_x10 = static_cast<int16_t>(335 + random(-8, 9));
        m.battery_mv = static_cast<uint16_t>(3900 - min<uint32_t>(now / 60000UL, 500UL));
        m.signal_percent = 0;

        uint16_t flags = ATHLETE_FLAG_GPS_LOCK | ATHLETE_FLAG_HEART_RATE_VALID;
        if (distance_m > 0 && distance_m % 1609 < 8) {
            flags |= ATHLETE_FLAG_SPLIT_MARKER;
        }
        if (forceSplit_) {
            flags |= ATHLETE_FLAG_SPLIT_MARKER;
            forceSplit_ = false;
        }
        if (m.battery_mv < 3600) {
            flags |= ATHLETE_FLAG_LOW_BATTERY;
        }
        m.flags = flags;
    }
#endif

#if defined(USE_REAL_SENSORS)
    // -------- Real hardware back end --------
    MAX30105 hr_;
    Adafruit_MPU6050 mpu_;
    TinyGPSPlus gps_;
    bool hrReady_ = false, imuReady_ = false;

    // Heart rate state
    uint32_t lastBeatMs_ = 0;
    float bpmRates_[4] = {0, 0, 0, 0};
    uint8_t rateIndex_ = 0;
    uint16_t beatAvg_ = 0;
    float skinTempC_ = 0.0f;
    uint32_t lastTempMs_ = 0;

    // IMU / cadence state
    float accelHpf_ = 0.0f, accelPrev_ = 0.0f;
    bool stepArmed_ = true;
    uint32_t lastStepMs_ = 0;
    uint32_t stepCount_ = 0, windowStartSteps_ = 0;
    uint16_t cadenceSpm_ = 0, strideCm_ = 0, voscCm_ = 0;
    float voscPeak_ = 0.0f, voscTrough_ = 0.0f;
    uint32_t windowStartMs_ = 0;
    float windowStartDistance_ = 0.0f;

    // GPS / distance / split state
    bool gpsFix_ = false, haveLastFix_ = false;
    double lastLat_ = 0.0, lastLng_ = 0.0;
    float lastAlt_ = 0.0f;
    float distanceM_ = 0.0f, elevationGainM_ = 0.0f;
    float gpsSpeedMps_ = 0.0f;
    uint16_t lastMileCount_ = 0, currentSplitSec_ = 0;
    uint32_t lastMileMs_ = 0;
    bool splitJustMarked_ = false;

    // Battery
    uint16_t batteryMv_ = 4000;
    uint32_t lastBatteryMs_ = 0;

    void updateHeartRate(uint32_t now) {
        if (!hrReady_) return;
        long ir = hr_.getIR();
        if (ir < 50000) {              // finger/skin not in contact
            beatAvg_ = 0;
            return;
        }
        if (checkForBeat(ir)) {
            uint32_t delta = now - lastBeatMs_;
            lastBeatMs_ = now;
            float bpm = 60.0f / (delta / 1000.0f);
            if (bpm > 30 && bpm < 230) {
                bpmRates_[rateIndex_++ % 4] = bpm;
                float sum = 0; uint8_t n = 0;
                for (float r : bpmRates_) if (r > 0) { sum += r; n++; }
                if (n) beatAvg_ = static_cast<uint16_t>(sum / n);
            }
        }
        if (now - lastTempMs_ >= 3000) {
            lastTempMs_ = now;
            skinTempC_ = hr_.readTemperature();   // die temp ~ skin contact temp
        }
    }

    void updateImu(uint32_t now) {
        if (!imuReady_) return;
        sensors_event_t a, g, t;
        mpu_.getEvent(&a, &g, &t);

        // Acceleration magnitude, high-pass filtered to remove gravity.
        float mag = sqrtf(a.acceleration.x * a.acceleration.x +
                          a.acceleration.y * a.acceleration.y +
                          a.acceleration.z * a.acceleration.z);
        accelHpf_ = 0.9f * (accelHpf_ + mag - accelPrev_);
        accelPrev_ = mag;

        // Vertical oscillation estimate from vertical-accel peak-to-peak.
        float vert = a.acceleration.z - 9.81f;
        if (vert > voscPeak_) voscPeak_ = vert;
        if (vert < voscTrough_) voscTrough_ = vert;

        // Step detection: threshold crossing with refractory period.
        const float STEP_THRESHOLD = 2.5f;     // m/s^2 above the filtered baseline
        if (accelHpf_ > STEP_THRESHOLD && stepArmed_ && (now - lastStepMs_) > 220) {
            stepCount_++;
            lastStepMs_ = now;
            stepArmed_ = false;
        } else if (accelHpf_ < 0.5f) {
            stepArmed_ = true;
        }

        // Recompute cadence/stride over a rolling window.
        if (now - windowStartMs_ >= 4000) {
            uint32_t steps = stepCount_ - windowStartSteps_;
            float secs = (now - windowStartMs_) / 1000.0f;
            cadenceSpm_ = static_cast<uint16_t>((steps / secs) * 60.0f);

            float dist = distanceM_ - windowStartDistance_;
            strideCm_ = steps > 0 ? static_cast<uint16_t>((dist / steps) * 100.0f) : 0;

            float pp = voscPeak_ - voscTrough_;     // crude vertical oscillation proxy
            voscCm_ = static_cast<uint16_t>(constrain(pp * 0.6f, 0.0f, 40.0f));

            windowStartMs_ = now;
            windowStartSteps_ = stepCount_;
            windowStartDistance_ = distanceM_;
            voscPeak_ = 0.0f;
            voscTrough_ = 0.0f;
        }
    }

    void updateGps(uint32_t now) {
        while (Serial2.available() > 0) {
            gps_.encode(static_cast<char>(Serial2.read()));
        }

        if (gps_.speed.isValid()) {
            gpsSpeedMps_ = gps_.speed.mps();
        }
        if (!gps_.location.isUpdated() || !gps_.location.isValid()) {
            return;
        }

        gpsFix_ = true;
        double lat = gps_.location.lat();
        double lng = gps_.location.lng();
        float alt = gps_.altitude.isValid() ? gps_.altitude.meters() : lastAlt_;

        if (haveLastFix_) {
            double d = TinyGPSPlus::distanceBetween(lastLat_, lastLng_, lat, lng);
            if (d > 1.0 && d < 60.0) {          // ignore jitter and outliers
                distanceM_ += static_cast<float>(d);
            }
            if (alt > lastAlt_) {
                elevationGainM_ += (alt - lastAlt_);
            }

            // Mile-split detection.
            uint16_t miles = static_cast<uint16_t>(distanceM_ / MILE_METERS);
            if (miles > lastMileCount_) {
                currentSplitSec_ = static_cast<uint16_t>((now - lastMileMs_) / 1000UL);
                if (currentSplitSec_ < bestSplitSec_) bestSplitSec_ = currentSplitSec_;
                lastMileMs_ = now;
                lastMileCount_ = miles;
                splitJustMarked_ = true;
            }
        }

        lastLat_ = lat;
        lastLng_ = lng;
        lastAlt_ = alt;
        haveLastFix_ = true;
    }

    void updateBattery() {
        uint32_t mv = analogReadMilliVolts(PIN_BATTERY_ADC);
        batteryMv_ = static_cast<uint16_t>(mv * BATTERY_DIVIDER);
    }

    void fillReal(AthleteMetricsPayload& m) {
        m.heart_rate_bpm = beatAvg_;
        m.pace_sec_per_mile = gpsSpeedMps_ > 0.4f
            ? static_cast<uint16_t>(min<float>(MILE_METERS / gpsSpeedMps_, 65000.0f)) : 0;
        m.current_split_sec = currentSplitSec_;
        m.best_split_sec = bestSplitSec_;
        m.distance_m = static_cast<uint16_t>(min<float>(distanceM_, 65000.0f));
        m.speed_cm_s = static_cast<uint16_t>(gpsSpeedMps_ * 100.0f);
        m.cadence_spm = cadenceSpm_;
        m.stride_length_cm = strideCm_;
        m.ground_contact_ms = 0;     // not measurable from a torso/wrist IMU (needs a foot pod)
        m.vertical_oscillation_cm = voscCm_;
        m.elevation_gain_m = static_cast<int16_t>(elevationGainM_);
        m.skin_temp_c_x10 = static_cast<int16_t>(skinTempC_ * 10.0f);
        m.battery_mv = batteryMv_;
        m.signal_percent = 0;        // hub stamps this from the received RSSI

        uint16_t flags = 0;
        if (gpsFix_) flags |= ATHLETE_FLAG_GPS_LOCK;
        if (beatAvg_ > 0) flags |= ATHLETE_FLAG_HEART_RATE_VALID;
        if (splitJustMarked_) { flags |= ATHLETE_FLAG_SPLIT_MARKER; splitJustMarked_ = false; }
        if (forceSplit_) { flags |= ATHLETE_FLAG_SPLIT_MARKER; forceSplit_ = false; }
        if (batteryMv_ < 3600) flags |= ATHLETE_FLAG_LOW_BATTERY;
        m.flags = flags;
    }
#endif
};

#endif
