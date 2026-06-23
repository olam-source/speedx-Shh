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
//        MAX30102 optical HR sensor — IR PPG, I2C (0x57), wristband/armband form factor
//        MPU-6050  6-axis IMU for cadence/stride/dynamics (I2C)
//        u-blox NEO-6M (or NEO-M8N) GPS for distance/pace/splits (UART2)
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

#if defined(USE_BLE_HR)
#include <NimBLEDevice.h>
#endif

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
#include <TinyGPS++.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <MAX30105.h>      // SparkFun MAX3010x lib — drives the MAX30102 (addr 0x57)
#include "heartRate.h"     // checkForBeat() beat detector
#endif

// SpeedX armband IS the BLE HR sensor — advertises standard Heart Rate Service
// (0x180D) so Garmin/Coros/Apple Watch pair once and auto-reconnect each session.
// BLE server callbacks live directly on AthleteSensors when USE_BLE_HR is set.
class AthleteSensors
#if defined(USE_BLE_HR)
    : public NimBLEServerCallbacks
#endif
{
public:
#if defined(USE_BLE_HR)
    void beginBle() {
        NimBLEDevice::init("SpeedX");
        auto* srv = NimBLEDevice::createServer();
        srv->setCallbacks(this);
        auto* svc   = srv->createService("180d");
        bleHrChar_  = svc->createCharacteristic("2a37",
                          NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        auto* loc   = svc->createCharacteristic("2a38", NIMBLE_PROPERTY::READ);
        uint8_t wrist = 2;   // body sensor location: wrist
        loc->setValue(&wrist, 1);
        svc->start();

        // Running Speed & Cadence service (0x1814): the wearable also presents
        // as a "footpod" so watches pick up pace, cadence and distance.
        auto* rsc   = srv->createService("1814");
        bleRscChar_ = rsc->createCharacteristic("2a53",
                          NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        rsc->start();

        NimBLEDevice::getAdvertising()->addServiceUUID("180d");
        NimBLEDevice::getAdvertising()->addServiceUUID("1814");
        NimBLEDevice::getAdvertising()->start();
    }
    String bleStatus() const {
        return bleConnected_
            ? "BLE HR: " + String(bleClients_) + " client(s) connected"
            : "BLE HR: advertising as 'SpeedX' — pair in watch sensor settings";
    }
#endif

    bool begin() {
#if defined(USE_REAL_SENSORS)
        bool ok = true;
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

        // MAX30102 optical HR (I2C 0x57, IR PPG, wristband/armband)
        if (ppg_.begin(Wire, I2C_SPEED_FAST)) {
            ppg_.setup();                       // SparkFun defaults: 50mA, 411us, 4-avg
            ppg_.setPulseAmplitudeRed(0x0A);    // dim red — IR drives beat detection
            ppg_.setPulseAmplitudeIR(0x24);     // ponytail: ~7mA IR; raise if signal is weak
            ppg_.setPulseAmplitudeGreen(0);     // MAX30102 has no green LED
            Serial.println("MAX30102 optical HR ready");
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
        updatePpg();
        updateImu(now);
        updateGps(now);
        if (now - lastBatteryMs_ >= 5000) {
            lastBatteryMs_ = now;
            updateBattery();
        }
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
        bleNotify(m.heart_rate_bpm);   // push HR to paired Garmin / Coros / Apple Watch
        bleNotifyRsc(m);               // push pace / cadence / distance (footpod profile)
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
    NimBLECharacteristic* bleHrChar_  = nullptr;
    NimBLECharacteristic* bleRscChar_ = nullptr;
    volatile bool    bleConnected_   = false;
    volatile uint8_t bleClients_     = 0;

    void bleNotify(uint16_t bpm) {
        if (!bleConnected_ || !bleHrChar_) return;
        uint8_t data[2] = {0x00, (uint8_t)min<uint16_t>(bpm, 255)};
        bleHrChar_->setValue(data, 2);
        bleHrChar_->notify();
    }

    // RSC Measurement (0x2A53): flags, instantaneous speed (u16, 1/256 m/s),
    // cadence (u8, steps/min), stride length (u16, 1/100 m), total distance
    // (u32, 1/10 m). Watches read this as a footpod for pace/cadence/distance.
    void bleNotifyRsc(const AthleteMetricsPayload& m) {
        if (!bleConnected_ || !bleRscChar_) return;
        uint8_t buf[10];
        buf[0] = 0x07;   // bit0 stride present | bit1 distance present | bit2 running
        uint16_t speed = static_cast<uint16_t>(m.speed_cm_s * 2.56f);   // cm/s -> (m/s)*256
        buf[1] = speed & 0xFF; buf[2] = speed >> 8;
        // ponytail: RSC cadence is steps/min; halve it here if a watch shows 2x (strides).
        buf[3] = static_cast<uint8_t>(min<uint16_t>(m.cadence_spm, 255));
        uint16_t stride = m.stride_length_cm;                           // already 1/100 m
        buf[4] = stride & 0xFF; buf[5] = stride >> 8;
        uint32_t dist = static_cast<uint32_t>(m.distance_m) * 10u;      // m -> 1/10 m
        buf[6] = dist & 0xFF; buf[7] = (dist >> 8) & 0xFF;
        buf[8] = (dist >> 16) & 0xFF; buf[9] = (dist >> 24) & 0xFF;
        bleRscChar_->setValue(buf, sizeof(buf));
        bleRscChar_->notify();
    }
    void onConnect(NimBLEServer*) override {
        bleClients_++; bleConnected_ = true;
        NimBLEDevice::getAdvertising()->start();
    }
    void onDisconnect(NimBLEServer*) override {
        if (bleClients_ > 0) bleClients_--;
        bleConnected_ = bleClients_ > 0;
    }
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

        // Simulated GPS: trace a ~400m oval (≈90s/lap) so the dashboard route
        // map has something to draw on the bench. Each athlete is phase-offset.
        const double base_lat = 51.5558, base_lon = -0.2796;   // a real running track
        double ang = (session_ms / 90000.0) * TWO_PI + (m.athlete_id % 12) * 0.52;
        double rlat = 50.0 / 111320.0;                          // ~100m N-S
        double rlon = 80.0 / (111320.0 * cos(base_lat * PI / 180.0));  // ~160m E-W
        m.lat_e7 = static_cast<int32_t>((base_lat + rlat * sin(ang)) * 1e7);
        m.lon_e7 = static_cast<int32_t>((base_lon + rlon * cos(ang)) * 1e7);
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
    Adafruit_MPU6050 mpu_;
    TinyGPSPlus gps_;
    bool imuReady_ = false;

    // MAX30102 optical HR state
    MAX30105 ppg_;
    uint16_t bpmPpg_         = 0;
    uint32_t ppgLastBeatMs_  = 0;
    float    ppgRates_[4]    = {};
    uint8_t  ppgIdx_         = 0;

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

    void updatePpg() {
        // SparkFun lib pulls the FIFO internally; getIR() returns the latest sample.
        long ir = ppg_.getIR();
        if (ir < 50000) {            // ponytail: no skin contact; tune per wrist/arm coupling
            bpmPpg_ = 0;
            return;
        }
        if (checkForBeat(ir)) {
            uint32_t now = millis();
            uint32_t delta = now - ppgLastBeatMs_;
            ppgLastBeatMs_ = now;
            float bpm = 60000.0f / delta;
            if (bpm >= 30.0f && bpm <= 230.0f) {
                ppgRates_[ppgIdx_++ % 4] = bpm;
                float sum = 0.0f; uint8_t n = 0;
                for (float r : ppgRates_) if (r > 0.0f) { sum += r; n++; }
                if (n) bpmPpg_ = static_cast<uint16_t>(sum / n);
            }
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
        m.heart_rate_bpm = bpmPpg_;
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
        m.skin_temp_c_x10 = 0;   // ponytail: add NTC thermistor if skin temp needed
        m.battery_mv = batteryMv_;
        m.lat_e7 = gpsFix_ ? static_cast<int32_t>(lastLat_ * 1e7) : 0;
        m.lon_e7 = gpsFix_ ? static_cast<int32_t>(lastLng_ * 1e7) : 0;
        m.signal_percent = 0;        // hub stamps this from the received RSSI

        uint16_t flags = 0;
        if (gpsFix_) flags |= ATHLETE_FLAG_GPS_LOCK;
        if (bpmPpg_ > 0) flags |= ATHLETE_FLAG_HEART_RATE_VALID;
        if (splitJustMarked_) { flags |= ATHLETE_FLAG_SPLIT_MARKER; splitJustMarked_ = false; }
        if (forceSplit_) { flags |= ATHLETE_FLAG_SPLIT_MARKER; forceSplit_ = false; }
        if (batteryMv_ < 3600) flags |= ATHLETE_FLAG_LOW_BATTERY;
        m.flags = flags;
    }
#endif
};

#endif
