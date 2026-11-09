// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/RayProtectionController.h"
#include "core/userprefs.h"

extern UserPreferences gPrefs;

RayProtectionController *RayProtectionController::s_instance = nullptr;

RayProtectionController::RayProtectionController(const std::vector<RfPowerSensor *> &sensors,
                                                 const RayProtectionConfig &config)
    : _sensors(sensors),
      _config(config) {}

void RayProtectionController::begin()
{
    s_instance = this;
    _bootTs = millis();
    _tripped = false;
    _int1Flag = _int2Flag = false;

    // Clear SWR trip counters
    for (size_t i = 0; i < NUM_COUPLERS; ++i)
        _swrHighCount[i] = 0;

    // Configure interrupt pins (active-low or high depends on your comparator logic)
    pinMode(INT_PROTECT_1_PIN, INPUT_PULLUP);
    pinMode(INT_PROTECT_2_PIN, INPUT_PULLUP);

    attachInterrupts_();

    // Clear any already-asserted lines so we don’t immediately trip on boot
    if (digitalRead(INT_PROTECT_1_PIN) == LOW)
        _int1Flag = true;
    if (digitalRead(INT_PROTECT_2_PIN) == LOW)
        _int2Flag = true;

    // Initialize timing for RF sampling
    m_nextRfSampleUs = micros();
}

void RayProtectionController::setAlarmRelay(bool on)
{
    pinMode(ALARM_RELAY_PIN, OUTPUT);

    if (on)
        digitalWrite(ALARM_RELAY_PIN, RELAY_ON);
    else
        digitalWrite(ALARM_RELAY_PIN, RELAY_OFF);
}

void RayProtectionController::attachInterrupts_()
{
    attachInterrupt(
        digitalPinToInterrupt(INT_PROTECT_1_PIN),
        []
        {
            if (RayProtectionController::s_instance)
            {
                RayProtectionController::s_instance->_int1Flag = true;
            }
        },
        FALLING);

    attachInterrupt(
        digitalPinToInterrupt(INT_PROTECT_2_PIN),
        []
        {
            if (RayProtectionController::s_instance)
            {
                RayProtectionController::s_instance->_int2Flag = true;
            }
        },
        FALLING);
}

void RayProtectionController::reset()
{
    _tripped = false;
    _lastTrip = RayTripEvent{};
    _int1Flag = _int2Flag = false;

    // Clear SWR trip counters
    for (size_t i = 0; i < NUM_COUPLERS; ++i)
        _swrHighCount[i] = 0;

    setAlarmRelay(false);
}

void RayProtectionController::update(bool txActive)
{
    const uint32_t now = millis();
    const uint32_t now2 = millis();

    // Determine effective boot ignore period:
    const uint32_t ignoreMs = _config.bootIgnoreMs ? _config.bootIgnoreMs : BOOT_IGNORED_MS;

    // Ignore everything during boot ignore window (prevents false trips on startup / floating inputs)
    if (now - _bootTs < ignoreMs)
    {
        _int1Flag = _int2Flag = false;

        // Clear SWR trip counters
        for (size_t i = 0; i < NUM_COUPLERS; ++i)
            _swrHighCount[i] = 0;

        // Ensure relay stays off during boot ignore window
        if (gPrefs.protectionRelayEnabled)
            setAlarmRelay(false);

        return;
    }

    /*    // ---- DEBUG: measure ProtectionController update() loop timing (prints once per second)
        static uint32_t s_lastCallMs = 0;
        static uint32_t s_windowStartMs = 0;
        static uint32_t s_count = 0;
        static uint32_t s_sumDtMs = 0;
        static uint32_t s_minDtMs = 0xFFFFFFFFu;
        static uint32_t s_maxDtMs = 0;

        // Determine effective boot ignore period:
        const uint32_t ignoreMs = _config.bootIgnoreMs ? _config.bootIgnoreMs : BOOT_IGNORED_MS;

        // Ignore everything during boot ignore window
        if (now - _bootTs < ignoreMs)
        {
            _int1Flag = _int2Flag = false;

            // Clear SWR trip counters
            for (size_t i = 0; i < NUM_COUPLERS; ++i)
                _swrHighCount[i] = 0;

            return;
        }

        if (s_lastCallMs != 0)
        {
            const uint32_t dt = now2 - s_lastCallMs;
            s_sumDtMs += dt;
            ++s_count;
            if (dt < s_minDtMs)
                s_minDtMs = dt;
            if (dt > s_maxDtMs)
                s_maxDtMs = dt;
        }
        s_lastCallMs = now2;

        if (s_windowStartMs == 0)
            s_windowStartMs = now2;

        if ((now2 - s_windowStartMs) >= 1000)
        {
            if (s_count > 0 && Serial)
            {
                const uint32_t avgDt = s_sumDtMs / s_count;
                Serial.printf("[Protect] update dt(ms): avg=%lu min=%lu max=%lu samples=%lu tx=%d\n",
                              (unsigned long)avgDt,
                              (unsigned long)s_minDtMs,
                              (unsigned long)s_maxDtMs,
                              (unsigned long)s_count,
                              txActive ? 1 : 0);
            }

            // reset window stats
            s_windowStartMs = now2;
            s_count = 0;
            s_sumDtMs = 0;
            s_minDtMs = 0xFFFFFFFFu;
            s_maxDtMs = 0;
        } // ---- END DEBUG
    */

    // Sample sensors at a fixed interval (e.g., 500 Hz) to ensure stable timing for peak detection, regardless of protection logic execution time
    const uint32_t nowUs = micros();
    if ((int32_t)(nowUs - m_nextRfSampleUs) >= 0)
    {
        // keep phase stable even after stalls
        do
        {
            m_nextRfSampleUs += RF_SAMPLE_PERIOD_US;
        } while ((int32_t)(nowUs - m_nextRfSampleUs) >= 0);

        for (size_t i = 0; i < NUM_COUPLERS; ++i)
        {
            if (_sensors[i])
                _sensors[i]->update(); // <-- this is the critical part for SSB peaks
        }
    }

    if (_status)
    {
        _status->lastUpdateMs = millis();
        _status->mode = txActive ? AppMode::TxActive : AppMode::Idle;

        for (size_t i = 0; i < NUM_COUPLERS; ++i)
        {
            auto *s = _sensors[i]; // or however you store RfPowerSensor*
            auto &cs = _status->couplers[i];

            if (!s)
            {
                cs = CouplerStatus{}; // zero/reset if missing
                continue;
            }

            cs.fwdVolts = s->getForwardVoltage();
            cs.refVolts = s->getReflectedVoltage();
            cs.fwdWatts = s->getForwardPowerWatts();
            cs.refWatts = s->getReflectedPowerWatts();
            // cs.swr = s->getSwrSmoothed();
            cs.swr = s->getSwr();
            cs.fwdPeakW = s->getPeakForwardPowerWatts();
            cs.refPeakW = s->getPeakReflectedPowerWatts();
        }

        // primary view: e.g. coupler 3 ⇒ index 2
        constexpr size_t kPrimaryIndex = 2;
        if (kPrimaryIndex < NUM_COUPLERS)
        {
            const auto &cs = _status->couplers[kPrimaryIndex];
            _status->primaryFwdWatts = cs.fwdWatts;
            _status->primaryRefWatts = cs.refWatts;
            _status->primarySwr = cs.swr;
        }
    }

    // If already tripped, we just keep the latched state until reset()
    if (_tripped)
    {
        return;
    }

    // 1) Hardware interrupts (highest priority)
    checkInterrupts_();
    if (_tripped)
        return;

    // 2) Coupler-based limits (power / SWR)
    checkCouplerLimits_(_config.gateOnTx ? txActive : true);
    setAlarmRelay(_tripped && gPrefs.protectionRelayEnabled);
}

void RayProtectionController::checkInterrupts_()
{
    const uint32_t now = millis();

    if (_int1Flag)
    {
        _int1Flag = false;

        if (gPrefs.extInt1Enabled)
        { // <--- gate by prefs
            _tripped = true;
            _lastTrip.type = RayTripType::ExternalInterrupt1;
            _lastTrip.source = RayTripSource::Interrupt1;
            _lastTrip.threshold = 0.0f;
            _lastTrip.value = 0.0f;
            _lastTrip.timestamp = now;

            if (_callback)
            {
                _callback(_lastTrip);
            }
            return;
        }
    }

    if (_int2Flag)
    {
        _int2Flag = false;

        if (gPrefs.extInt2Enabled)
        { // <--- gate by prefs
            _tripped = true;
            _lastTrip.type = RayTripType::ExternalInterrupt2;
            _lastTrip.source = RayTripSource::Interrupt2;
            _lastTrip.threshold = 0.0f;
            _lastTrip.value = 0.0f;
            _lastTrip.timestamp = now;

            if (_callback)
            {
                _callback(_lastTrip);
            }
            return;
        }
    }
}

void RayProtectionController::checkCouplerLimits_(bool txActive)
{
    if (!txActive)
    {
        return;
    }

    const uint32_t now = millis();

    auto indexToSource = [](size_t i) -> RayTripSource
    {
        switch (i)
        {
        case 0:
            return RayTripSource::Coupler1;
        case 1:
            return RayTripSource::Coupler2;
        case 2:
            return RayTripSource::Coupler3;
        case 3:
            return RayTripSource::Coupler4;
        default:
            return RayTripSource::None;
        }
    };

    // Gating thresholds to prevent false trips from floating / near-zero forward ---
    // If forward power is below this, ignore SWR (and optionally ignore reflected power trips).
    constexpr float kMinFwdForSwr_W = 2.0f;  // tweak as you like (2–5W typical)
    constexpr float kMinFwdForRefl_W = 2.0f; // keep same unless you want different behavior

    const size_t count = _sensors.size() < NUM_COUPLERS ? _sensors.size() : NUM_COUPLERS;

    for (size_t i = 0; i < count; ++i)
    {
        // Skip couplers that are logically disabled
        if (i >= gPrefs.numCouplers)
            continue;

        const CouplerPreferences &cp = gPrefs.couplers[i];
        if (cp.couplerType == CouplerType::NONE)
            continue;

        RfPowerSensor *s = _sensors[i];
        if (!s)
            continue;

        const float fwd = s->getForwardPowerWatts();
        const float ref = s->getReflectedPowerWatts();
        const float swr = clipVswr(s->getSwr()); // keep SWR sane

        const float fwdLimit = _config.fwdMaxW[i];
        const float reflLimit = _config.reflMaxW[i];
        const float swrLimit = _config.swrMax[i];

        // Forward power limit (no gating; forward is meaningful by itself)
        if (fwdLimit > 0.0f && fwd > fwdLimit)
        {
            _tripped = true;
            if (gPrefs.protectionRelayEnabled)
                setAlarmRelay(true);

            _lastTrip.type = RayTripType::HighForwardPower;
            _lastTrip.source = indexToSource(i);
            _lastTrip.threshold = fwdLimit;
            _lastTrip.value = fwd;
            _lastTrip.timestamp = now;
            if (_callback)
                _callback(_lastTrip);
            return;
        }

        // If forward is too low, ignore reflected/SWR (prevents floating input trips) ---
        const bool fwdIsValidForRefl = (fwd >= kMinFwdForRefl_W);
        const bool fwdIsValidForSwr = (fwd >= kMinFwdForSwr_W);

        // Reflected power limit (gate on forward)
        if (reflLimit > 0.0f && fwdIsValidForRefl && ref > reflLimit)
        {
            _tripped = true;
            if (gPrefs.protectionRelayEnabled)
                setAlarmRelay(true);

            _lastTrip.type = RayTripType::HighReflectedPower;
            _lastTrip.source = indexToSource(i);
            _lastTrip.threshold = reflLimit;
            _lastTrip.value = ref;
            _lastTrip.timestamp = now;
            if (_callback)
                _callback(_lastTrip);
            return;
        }

        // SWR limit (gate on forward + require N consecutive cycles)
        if (swrLimit > 0.0f)
        {
            if (!fwdIsValidForSwr)
            {
                // Forward too low => SWR meaningless
                _swrHighCount[i] = 0;
                continue;
            }

            if (swr > swrLimit)
            {
                if (_swrHighCount[i] < 255)
                    ++_swrHighCount[i];

                if (_swrHighCount[i] >= kSWRTripConsecutiveCycles)
                {
                    _tripped = true;
                    if (gPrefs.protectionRelayEnabled)
                        setAlarmRelay(true);

                    _lastTrip.type = RayTripType::HighSWR;
                    _lastTrip.source = indexToSource(i);
                    _lastTrip.threshold = swrLimit;
                    _lastTrip.value = swr;
                    _lastTrip.timestamp = now;
                    if (_callback)
                        _callback(_lastTrip);
                    return;
                }
            }
            else
            {
                _swrHighCount[i] = 0;
            }
        }
    }
}
