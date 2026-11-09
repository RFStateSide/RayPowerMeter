// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>
#include "Constants.h"
#include "hw/pinout.h"
#include "coupler/rfpowersensor.h"
#include "core/SystemStatus.h"
#include "core/TripTypes.h"

/*
// What tripped?
enum class RayTripType : uint8_t
{
    None = 0,
    HighForwardPower,
    HighReflectedPower,
    HighSWR,
    ExternalInterrupt1,
    ExternalInterrupt2
};

// Where did it come from?
enum class RayTripSource : uint8_t
{
    None = 0,
    Coupler1,
    Coupler2,
    Coupler3,
    Coupler4,
    Interrupt1,
    Interrupt2
};
*/

struct RayTripEvent
{
    RayTripType type = RayTripType::None;
    RayTripSource source = RayTripSource::None;
    float threshold = 0.0f; // configured threshold
    float value = 0.0f;     // measured value
    uint32_t timestamp = 0; // millis() when trip latched
};

// Simple per-coupler thresholds
struct RayProtectionConfig
{
    // Forward power limits (W). 0 or <0 means "disabled".
    float fwdMaxW[NUM_COUPLERS] = {0, 0, 0, 0};
    // Reflected power limits (W)
    float reflMaxW[NUM_COUPLERS] = {0, 0, 0, 0};
    // SWR limits
    float swrMax[NUM_COUPLERS] = {0, 0, 0, 0};

    // Ignore trips during boot time (ms). 0 => use BOOT_IGNORED_MS default.
    uint32_t bootIgnoreMs = BOOT_IGNORED_MS;

    // If true, only enforce power/SWR thresholds when txActive == true in update()
    bool gateOnTx = true;
};

class RayProtectionController
{
public:
    using TripCallback = std::function<void(const RayTripEvent &)>;

    RayProtectionController(const std::vector<RfPowerSensor *> &sensors,
                            const RayProtectionConfig &config);

    void begin();
    void update(bool txActive);
    void reset();

    bool isTripped() const { return _tripped; }
    RayTripEvent lastTrip() const { return _lastTrip; }
    void setAlarmRelay(bool on);

    void setTripCallback(TripCallback cb) { _callback = cb; }

    // NEW: runtime config access
    void setConfig(const RayProtectionConfig &cfg) { _config = cfg; }
    RayProtectionConfig getConfig() const { return _config; }

    void bindSystemStatus(SystemStatus *status) { _status = status; }

private:
    void attachInterrupts_();
    void checkInterrupts_();
    void checkCouplerLimits_(bool txActive);

    // Bookkeeping
    const std::vector<RfPowerSensor *> &_sensors;
    RayProtectionConfig _config;

    bool _tripped = false;
    RayTripEvent _lastTrip{};
    uint32_t _bootTs = 0;

    // Require SWR limit for N consecutive update() cycles before tripping
    static constexpr uint8_t kSWRTripConsecutiveCycles = 2;
    uint8_t _swrHighCount[NUM_COUPLERS] = {0};

    // Interrupt flags set from ISRs
    volatile bool _int1Flag = false;
    volatile bool _int2Flag = false;

    TripCallback _callback = nullptr;

    // Singleton pointer so the C-style ISR lambdas can poke the instance
    static RayProtectionController *s_instance;

    SystemStatus *_status = nullptr;

        // Timing for internal sampling
    uint32_t m_nextRfSampleUs = 0;
    static constexpr uint32_t RF_SAMPLE_PERIOD_US = 2000; // 2ms (500 Hz). Try 5000 if needed.
};
