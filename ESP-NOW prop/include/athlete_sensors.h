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
//        MAXM86161 optical HR sensor — green LED PPG, I2C, wristband/armband form factor
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
// BleHrServer — SpeedX IS the chest strap. Advertises standard BLE Heart Rate
// Service (0x180D) so any Garmin, Coros, Apple Watch, or Wahoo can pair to it
// once in their sensor settings and auto-reconnect every session from then on.
//
// No external strap needed. HR comes from the onboard MAXM86161 optical sensor.
// Multiple clients can connect simultaneously (e.g. coach's tablet + athlete's watch).
// ============================================================================
#if defined(USE_BLE_HR)
#include <NimBLEDevice.h>

class BleHrServer : public NimBLEServerCallbacks {
public:
    void begin() {
        NimBLEDevice::init("SpeedX");
        auto* srv = NimBLEDevice::createServer();
        srv->setCallbacks(this);

        auto* svc   = srv->createService("180d");
        hrChar_     = svc->createCharacteristic("2a37",
                          NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        auto* loc   = svc->createCharacteristic("2a38", NIMBLE_PROPERTY::READ);
        uint8_t chest = 1;
        loc->setValue(&chest, 1);   // body sensor location: chest
        svc->start();

        NimBLEDevice::getAdvertising()->addServiceUUID("180d");
        NimBLEDevice::getAdvertising()->start();
    }

    // Push a new HR value to all connected watches/apps.
    void notify(uint16_t bpm) {
        if (!connected_ || !hrChar_) return;
        uint8_t data[2] = {0x00, (uint8_t)min<uint16_t>(bpm, 255)};
        hrChar_->setValue(data, 2);
        hrChar_->notify();
    }

    bool    connected()   const { return connected_; }
    uint8_t clientCount() const { return clients_; }

    String statusString() const {
        return connected_
            ? "BLE HR: " + String(clients_) + " client(s) connected"
            : "BLE HR: advertising as 'SpeedX' — pair in watch sensor settings";
    }

private:
    NimBLECharacteristic* hrChar_ = nullptr;
    volatile bool    connected_ = false;
    volatile uint8_t clients_   = 0;

    void onConnect(NimBLEServer*) override {
        clients_++;
        connected_ = true;
        NimBLEDevice::getAdvertising()->start();  // stay visible for a second client
    }
    void onDisconnect(NimBLEServer*) override {
        if (clients_ > 0) clients_--;
        connected_ = clients_ > 0;
    }
};
#endif // USE_BLE_HR

// ---- Pin / config defaults (override with -D build flags if your wiring differs)
#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA 21          // MAXM86161 + MPU-6050 shared SDA
#endif
#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL 22          // MAXM86161 + MPU-6050 shared SCL
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

// MAXM86161 I2C register map (7-bit address 0x62).
// ponytail: green LED only — best for wrist/armband; add IR slot if SpO2 needed later.
namespace Ppg {
    constexpr uint8_t ADDR     = 0x62;
    constexpr uint8_t INT_EN1  = 0x02;
    constexpr uint8_t FIFO_WR  = 0x04;
    constexpr uint8_t FIFO_RD  = 0x06;
    constexpr uint8_t FIFO_DAT = 0x07;
    constexpr uint8_t FIFO_CF1 = 0x08;
    constexpr uint8_t FIFO_CF2 = 0x09;
    constexpr uint8_t SYS_CTRL = 0x0A;
    constexpr uint8_t PPG_CF1  = 0x0B;   // [7:5] sample rate, [4:3] ADC range
    constexpr uint8_t PPG_CF2  = 0x0C;   // [7:6] LED range, [3:0] pulse width
    constexpr uint8_t LED_SEQ1 = 0x12;   // [3:0] slot1 LED: 0=off, 1=green, 2=IR, 3=red
    constexpr uint8_t LED1_PA  = 0x23;   // green pulse amplitude (0x00–0xFF, LSB=0.12mA)
}
#endif

class AthleteSensors {
public:
#if defined(USE_BLE_HR)
    void beginBle() { ble_.begin(); }
    String bleStatus() const { return ble_.statusString(); }
#endif

    bool begin() {
#if defined(USE_REAL_SENSORS)
        bool ok = true;
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

        // MAXM86161 optical HR (I2C, green LED, wristband/armband)
        ppgWrite(Ppg::SYS_CTRL,  0x01);         // software reset
        delay(100);
        ppgWrite(Ppg::FIFO_CF1,  0x0F);         // no averaging, A_FULL at 15 samples
        ppgWrite(Ppg::FIFO_CF2,  0x02);         // clear FIFO stat pointer on read
        ppgWrite(Ppg::PPG_CF1,   0x50);         // ponytail: 100 sps, ADC 16384nA; drop to 0x00 if saturation
        ppgWrite(Ppg::PPG_CF2,   0x00);         // LED range 31mA, shortest pulse width
        ppgWrite(Ppg::LED_SEQ1,  0x01);         // slot 1 = LED1 (green)
        ppgWrite(Ppg::LED1_PA,   0x20);         // ponytail: ~3.84mA; raise to 0x40 if signal is weak
        ppgWrite(Ppg::INT_EN1,   0x80);         // enable A_FULL interrupt
        Serial.println("MAXM86161 optical HR ready");

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
        ble_.notify(m.heart_rate_bpm);   // push ECG HR to paired Garmin / Coros / Apple Watch
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
    BleHrServer ble_;
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
    Adafruit_MPU6050 mpu_;
    TinyGPSPlus gps_;
    bool imuReady_ = false;

    // MAXM86161 optical HR state
    uint16_t bpmPpg_         = 0;
    float    ppgBaseline_    = 0.0f;
    float    ppgPeak_        = 0.0f;
    float    ppgValley_      = 0.0f;
    bool     ppgArmed_       = true;
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

    void ppgWrite(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(Ppg::ADDR);
        Wire.write(reg); Wire.write(val);
        Wire.endTransmission();
    }

    uint8_t ppgRead(uint8_t reg) {
        Wire.beginTransmission(Ppg::ADDR);
        Wire.write(reg);
        Wire.endTransmission(false);
        Wire.requestFrom(Ppg::ADDR, (uint8_t)1);
        return Wire.available() ? Wire.read() : 0;
    }

    void detectBeat(uint32_t sample, uint32_t now) {
        // DC baseline removal (slow IIR, ~2s time constant at 100 sps)
        ppgBaseline_ += (static_cast<float>(sample) - ppgBaseline_) * 0.02f;
        float sig = static_cast<float>(sample) - ppgBaseline_;

        // Auto-scale peak/valley to handle varying skin tone and LED coupling
        if (sig > ppgPeak_)   ppgPeak_   = sig; else ppgPeak_   *= 0.998f;
        if (sig < ppgValley_) ppgValley_ = sig; else ppgValley_ *= 0.998f;
        float amp = ppgPeak_ - ppgValley_;
        if (amp < 300) return;   // ponytail: no contact or too much motion noise; tune 300 per skin tone

        bool above = sig > ppgValley_ + amp * 0.5f;
        if (above && ppgArmed_ && (now - ppgLastBeatMs_) > 300) {
            uint32_t delta = now - ppgLastBeatMs_;
            ppgLastBeatMs_ = now;
            ppgArmed_ = false;
            float bpm = 60000.0f / delta;
            if (bpm >= 30.0f && bpm <= 230.0f) {
                ppgRates_[ppgIdx_++ % 4] = bpm;
                float sum = 0.0f; uint8_t n = 0;
                for (float r : ppgRates_) if (r > 0.0f) { sum += r; n++; }
                if (n) bpmPpg_ = static_cast<uint16_t>(sum / n);
            }
        } else if (!above) {
            ppgArmed_ = true;
        }
    }

    void updatePpg() {
        uint8_t wr    = ppgRead(Ppg::FIFO_WR);
        uint8_t rd    = ppgRead(Ppg::FIFO_RD);
        uint8_t avail = (wr - rd) & 0x1F;     // FIFO is 32 samples deep
        uint32_t now  = millis();
        while (avail-- > 0) {
            Wire.beginTransmission(Ppg::ADDR);
            Wire.write(Ppg::FIFO_DAT);
            Wire.endTransmission(false);
            Wire.requestFrom(Ppg::ADDR, (uint8_t)3);
            if (Wire.available() < 3) break;
            uint32_t raw = ((uint32_t)Wire.read() << 16)
                         | ((uint32_t)Wire.read() <<  8)
                         |  (uint32_t)Wire.read();
            // MAXM86161 FIFO: bits [2:0] = LED tag (1=green,2=IR,3=red), [23:3] = 19-bit sample
            if ((raw & 0x07) != 1) continue;   // keep only green channel
            detectBeat(raw >> 3, now);
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
