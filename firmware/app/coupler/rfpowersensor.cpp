// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "rfpowersensor.h"
#include <cmath>
#include <cfloat>
#include <algorithm>

// Provided by ModScopeController.cpp; true while a capture is active.
extern volatile bool g_modScopeCaptureActive;

// ===== Local helpers / defaults =====
// Trimmed mean for uint16_t array, typical n 16, trim 2
static inline float trimmedMeanU16(uint16_t *v, int n, int trim)
{
    if (!v || n <= 0)
        return 0.0f;

    if (trim < 0)
        trim = 0;
    if (trim > (n - 1) / 2)
        trim = (n - 1) / 2;

    // Sort in-place
    std::sort(v, v + n);

    const int lo = trim;
    const int hi = n - trim; // exclusive

    uint32_t sum = 0;
    int count = 0;
    for (int i = lo; i < hi; ++i)
    {
        sum += v[i];
        ++count;
    }

    float avg = (count > 0) ? (static_cast<float>(sum) / static_cast<float>(count)) : 0.0f;
    /*
        // ---- NEW SAFETY NET FOR LOW POWER ----
        // If trimming wipes out the signal (avg -> 0) but we clearly saw non-zero samples,
        // fall back to a more "low-signal friendly" statistic.
        //
        // This prevents low-RF readings from disappearing due to aggressive trimming.
        if (avg <= 0.0f && v[n - 1] > 0)
        {
            // Option A: median (very stable for low-level signals)
            if (n % 2 == 1)
                return static_cast<float>(v[n / 2]);
            else
                return 0.5f * (static_cast<float>(v[n / 2 - 1]) + static_cast<float>(v[n / 2]));
        }
    */
    return avg;
}

// Global defaults; can be tweaked via the C-style setters at bottom
static PeakMode s_peakModeDefault = PeakMode::HoldThenDecay;
static uint16_t s_peakHoldMsDefault = 1000;  // ms
static float s_peakDecayWpsDefault = 400.0f; // watts per second

// Smooth the raw ADC voltage so bars don't flicker
static constexpr float EMA_ALPHA = 0.25f; // 0.15–0.30 feels good for UI

// ===================== Class =====================

RfPowerSensor::RfPowerSensor(const char *name,
                             uint8_t pinFwd,
                             uint8_t pinRef,
                             float maxWatts,
                             int interruptPin,
                             uint16_t peakHoldTime)
    : _name(name),
      _pinFwd(pinFwd),
      _pinRef(pinRef),
      _maxWatts(maxWatts),
      _interruptPin(interruptPin),
      _peakHoldTimeMs(peakHoldTime),
      _voltageFwd(0.0f),
      _voltageRef(0.0f),
      _emaFwd(0.0f),
      _emaRef(0.0f),
      _rfDetected(false),
      peakForwardWatts(0.0f),
      peakForwardTimestamp(0),
      peakReflectedWatts(0.0f),
      peakReflectedTimestamp(0)
{

    // Default linear map: W = (V / ADC_VREF) * _maxWatts
    setFwdCalLinear(_maxWatts / ADC_VREF, 0.0f);
    setRefCalLinear(_maxWatts / ADC_VREF, 0.0f);
}

void RfPowerSensor::begin()
{
    // Ensure pins are in a sane state
    pinMode(_pinFwd, INPUT);
    pinMode(_pinRef, INPUT);

    // Configure ADC resolution (ok even if called multiple times)
    analogReadResolution(ADC_BITS);

    // IMPORTANT (ESP32-S3 Arduino core):
    // analogSetPinAttenuation() can error if the ADC channel isn't configured yet.
    // Force channel config by doing a dummy read first.
    (void)analogRead(_pinFwd);
    (void)analogRead(_pinRef);

    // Now it is safe to set per-pin attenuation without "__analogChannelConfig" errors
    analogSetPinAttenuation(_pinFwd, ADC_11db);
    analogSetPinAttenuation(_pinRef, ADC_11db);

    // If you have an interrupt pin, leave as-is (if used elsewhere)
    if (_interruptPin >= 0)
    {
        pinMode(_interruptPin, INPUT);
        // If you attach interrupts elsewhere, you can omit this.
        // attachInterrupt(digitalPinToInterrupt(_interruptPin), isr, RISING);
    }
}

static inline float decayToward(float peak, float target, float decayWps, float dt_ms)
{
    if (decayWps <= 0.0f)
        return target;
    const float step = decayWps * (dt_ms / 1000.0f);
    if (peak > target)
        peak = fmaxf(target, peak - step);
    return peak;
}

void RfPowerSensor::update()
{
    // Freeze telemetry ADC reads while ModScope is capturing.
    if (g_modScopeCaptureActive)
        return;

    // If disabled, keep outputs quiet.
    if (_couplerType == CouplerType::NONE)
    {
        _voltageFwd = 0.0f;
        _voltageRef = 0.0f;
        _voltageFwdPeak = 0.0f;
        _voltageRefPeak = 0.0f;
        _emaFwd = 0.0f;
        _emaRef = 0.0f;
        _publishPeak = false;
        _publishPeakUntilMs = 0;
        _crestEma = 1.0f;
        return;
    }

    // --------------------------------------------------------------------
    // 1) Burst sample: trimmed-mean avg + max peak (counts)
    // --------------------------------------------------------------------
    uint16_t fSamp[ADC_SAMPLES_BURST];
    uint16_t rSamp[ADC_SAMPLES_BURST];

    uint16_t fMax = 0;
    uint16_t rMax = 0;

    for (uint8_t i = 0; i < ADC_SAMPLES_BURST; ++i)
    {
        const uint16_t f = static_cast<uint16_t>(analogRead(_pinFwd));
        const uint16_t r = static_cast<uint16_t>(analogRead(_pinRef));

        fSamp[i] = f;
        rSamp[i] = r;

        if (f > fMax)
            fMax = f;
        if (r > rMax)
            rMax = r;

        if (ADC_AVG_DELAY_US > 0)
            delayMicroseconds(ADC_AVG_DELAY_US);
    }

    const float fAvgCounts = trimmedMeanU16(fSamp, ADC_SAMPLES_BURST, ADC_TRIM_COUNT);
    const float rAvgCounts = trimmedMeanU16(rSamp, ADC_SAMPLES_BURST, ADC_TRIM_COUNT);

    const float vFwdAvg = (fAvgCounts / static_cast<float>(ADC_COUNTS_MAX)) * ADC_VREF;
    const float vRefAvg = (rAvgCounts / static_cast<float>(ADC_COUNTS_MAX)) * ADC_VREF;

    const float vFwdPeak = (static_cast<float>(fMax) / static_cast<float>(ADC_COUNTS_MAX)) * ADC_VREF;
    const float vRefPeak = (static_cast<float>(rMax) / static_cast<float>(ADC_COUNTS_MAX)) * ADC_VREF;

    _voltageFwdPeak = vFwdPeak;
    _voltageRefPeak = vRefPeak;

    // --------------------------------------------------------------------
    // 2) EMA smoothing based on AVG volts (keeps UI stable if you use smoothed getters)
    // --------------------------------------------------------------------
    _emaFwd = (_emaFwd == 0.0f) ? vFwdAvg : (EMA_ALPHA * vFwdAvg + (1.0f - EMA_ALPHA) * _emaFwd);
    _emaRef = (_emaRef == 0.0f) ? vRefAvg : (EMA_ALPHA * vRefAvg + (1.0f - EMA_ALPHA) * _emaRef);

    // --------------------------------------------------------------------
    // 3) Convert mean and peak voltages with the same calibration as live readings.
    // -------------------------------------------------------------------------
    const float fwdW_avg = calibratedWatts_(true, vFwdAvg);
    const float fwdW_peak = calibratedWatts_(true, vFwdPeak);
    const float refW_peak = calibratedWatts_(false, vRefPeak);

    // -------------------------------------------------------------------------
    // 4) Decide whether to PUBLISH avg volts or peak volts (SSB detection)
    // --------------------------------------------------------------------
    const uint32_t nowMs = millis();

    // Tuning knobs (good starting points)
    constexpr float kMinDetectW = 2.0f;  // below this, treat as idle/noise
    constexpr float kOnCrest = 1.25f;    // enter "publish peak" mode
    constexpr float kOffCrest = 1.10f;   // exit "publish peak" mode (hysteresis)
    constexpr uint32_t kHoldMs = 500;    // keep peak-publish active this long after detection
    constexpr float kCrestAlpha = 0.25f; // smooth crest to avoid chatter

    float crest = 1.0f;
    if (fwdW_avg > kMinDetectW)
        crest = (fwdW_peak > 0.0f) ? (fwdW_peak / fwdW_avg) : 1.0f;

    _crestEma = (_crestEma == 0.0f) ? crest : (kCrestAlpha * crest + (1.0f - kCrestAlpha) * _crestEma);

    if (fwdW_avg <= kMinDetectW)
    {
        _publishPeak = false;
        _publishPeakUntilMs = 0;
    }
    else
    {
        if (_crestEma >= kOnCrest)
        {
            _publishPeak = true;
            _publishPeakUntilMs = nowMs + kHoldMs;
        }
        else if (_publishPeak)
        {
            // stay in peak mode during hold; after hold, allow exit if crest low
            if ((int32_t)(nowMs - _publishPeakUntilMs) >= 0 && _crestEma <= kOffCrest)
                _publishPeak = false;
        }
    }

    // Publish selected volts (this directly affects getForwardPowerWatts())
    if (_publishPeak)
    {
        _voltageFwd = vFwdPeak;
        _voltageRef = vRefPeak;
    }
    else
    {
        _voltageFwd = vFwdAvg;
        _voltageRef = vRefAvg;
    }

    // --------------------------------------------------------------------
    // 5) Internal peak markers: make them meaningful too (use PEAK watts)
    // --------------------------------------------------------------------
    if (_useInternalPeak)
    {
        const float forwardWatts = fwdW_peak;
        const float reflectedWatts = refW_peak;

        const unsigned long now = nowMs;
        const float dt_ms = static_cast<float>(now - peakForwardTimestamp);

        const PeakMode mode = s_peakModeDefault;
        const uint16_t holdMs = (_peakHoldTimeMs > 0) ? _peakHoldTimeMs : s_peakHoldMsDefault;
        const float decayWps = s_peakDecayWpsDefault;

        switch (mode)
        {
        case PeakMode::HoldUntilHigher:
            if (forwardWatts > peakForwardWatts)
            {
                peakForwardWatts = forwardWatts;
                peakForwardTimestamp = now;
            }
            break;

        case PeakMode::HoldThenDecay:
            if (forwardWatts > peakForwardWatts)
            {
                peakForwardWatts = forwardWatts;
                peakForwardTimestamp = now;
            }
            else if (now - peakForwardTimestamp >= holdMs)
            {
                peakForwardWatts = decayToward(peakForwardWatts, forwardWatts, decayWps, dt_ms);
                peakForwardTimestamp = now;
            }
            break;

        case PeakMode::ContinuousDecay:
            if (forwardWatts > peakForwardWatts)
                peakForwardWatts = forwardWatts;
            else
                peakForwardWatts = decayToward(peakForwardWatts, forwardWatts, decayWps, dt_ms);

            peakForwardTimestamp = now;
            break;
        }

        const float dt_ms_ref = static_cast<float>(now - peakReflectedTimestamp);
        switch (mode)
        {
        case PeakMode::HoldUntilHigher:
            if (reflectedWatts > peakReflectedWatts)
            {
                peakReflectedWatts = reflectedWatts;
                peakReflectedTimestamp = now;
            }
            break;

        case PeakMode::HoldThenDecay:
            if (reflectedWatts > peakReflectedWatts)
            {
                peakReflectedWatts = reflectedWatts;
                peakReflectedTimestamp = now;
            }
            else if (now - peakReflectedTimestamp >= holdMs)
            {
                peakReflectedWatts = decayToward(peakReflectedWatts, reflectedWatts, decayWps, dt_ms_ref);
                peakReflectedTimestamp = now;
            }
            break;

        case PeakMode::ContinuousDecay:
            if (reflectedWatts > peakReflectedWatts)
                peakReflectedWatts = reflectedWatts;
            else
                peakReflectedWatts = decayToward(peakReflectedWatts, reflectedWatts, decayWps, dt_ms_ref);

            peakReflectedTimestamp = now;
            break;
        }
    }
}

float RfPowerSensor::readVoltage(uint8_t pin) const
{
    const int raw = analogRead(pin); // 0..ADC_COUNTS_MAX
    return (static_cast<float>(raw) / static_cast<float>(ADC_COUNTS_MAX)) * ADC_VREF;
}

float RfPowerSensor::getForwardVoltage() const
{
    return _voltageFwd;
}

float RfPowerSensor::getReflectedVoltage() const
{
    return _voltageRef;
}

float RfPowerSensor::getForwardPowerWattsSmoothed() const
{
    return calibratedWatts_(true, _emaFwd > 0.0f ? _emaFwd : _voltageFwd);
}

float RfPowerSensor::getReflectedPowerWattsSmoothed() const
{
    return calibratedWatts_(false, _emaRef > 0.0f ? _emaRef : _voltageRef);
}

float RfPowerSensor::getPeakForwardPowerWatts() const
{
    return peakForwardWatts;
}

float RfPowerSensor::getPeakReflectedPowerWatts() const
{
    return peakReflectedWatts;
}

const char *RfPowerSensor::getName() const
{
    return _name;
}

void RfPowerSensor::handleInterrupt()
{
    _rfDetected = true;
}

bool RfPowerSensor::isRfDetected() const
{
    return _rfDetected;
}

float RfPowerSensor::getSwr() const
{
    if (_couplerType == CouplerType::NONE)
        return 1.0f;

    const float fwd = getForwardPowerWatts();
    const float ref = getReflectedPowerWatts();

    if (fwd <= 0.25f)
    {
        return 1.0f; // no forward power; assume perfect match
    }
    const float ratio = sqrtf(ref / fwd);
    if (ratio >= 1.0f)
    {
        return VSWR_MAX; // total reflection; clamp to max
    }
    const float vswr = (1.0f + ratio) / (1.0f - ratio);
    return clipVswr(vswr); // from Constants.h
}

float RfPowerSensor::getSwrSmoothed() const
{
    if (_couplerType == CouplerType::NONE)
        return 1.0f;

    float fwd = getForwardPowerWattsSmoothed();
    float ref = getReflectedPowerWattsSmoothed();

    if (fwd <= 0.25f)
    {
        return 1.0f;
    }

    // Treat refFloor as a *noise floor* and subtract it smoothly.
    // This avoids snapping to 1.00 when ref hovers around the threshold.
    const float refFloor = fmaxf(RF_SWR_REF_FLOOR_W_ABS, RF_SWR_REF_FLOOR_W_REL * fwd);
    ref = fmaxf(0.0f, ref - refFloor);
    const float ratio = sqrtf(ref / fwd);
    if (ratio >= 1.0f)
    {
        return VSWR_MAX;
    }

    const float vswr = (1.0f + ratio) / (1.0f - ratio);
    return clipVswr(vswr);
}

// ===== Optional: global peak behavior overrides =====

extern "C" void RfPowerSensor_SetPeakMode(PeakMode m)
{
    s_peakModeDefault = m;
}

extern "C" void RfPowerSensor_SetPeakParams(uint16_t holdMs, float decayWps)
{
    if (holdMs)
        s_peakHoldMsDefault = holdMs;
    if (decayWps > 0.0f)
        s_peakDecayWpsDefault = decayWps;
}

// Utility: convert voltage to watts via an RfCal
static inline float dbm_to_w(float dbm)
{
    return powf(10.0f, (dbm - 30.0f) * 0.1f);
}

float voltsToWatts(const RfCal &c, float v)
{
    if (!c.valid)
        return 0.0f;
    if (c.type == CalType::LinearWatts)
    {
        return fmaxf(0.0f, c.m * v + c.b);
    }
    else
    {
        const float dbm = c.m * v + c.b;
        return dbm_to_w(dbm);
    }
}

void RfPowerSensor::setFullScaleWatts(float maxW)
{
    if (maxW <= 0.0f)
        maxW = 1.0f;

    _maxWatts = maxW;

    // IMPORTANT:
    // Do NOT overwrite calibration here. Full-scale is a range setting.
    // If no calibration exists at all, we keep a default linear mapping.
    if (!_calFwd.valid)
        setFwdCalLinear(_maxWatts / ADC_VREF, 0.0f);
    if (!_calRef.valid)
        setRefCalLinear(_maxWatts / ADC_VREF, 0.0f);
}

float RfPowerSensor::getForwardPowerWatts() const
{
    return calibratedWatts_(true, _voltageFwd);
}

float RfPowerSensor::getReflectedPowerWatts() const
{
    return calibratedWatts_(false, _voltageRef);
}

float RfPowerSensor::calibratedWatts_(bool isFwd, float voltage) const
{
    if (!isEnabled() || !isfinite(voltage))
        return 0.0f;

    const float tableWatts = v2wTable_(isFwd, voltage);
    if (isfinite(tableWatts))
        return tableWatts;

    const RfCal &cal = isFwd ? _calFwd : _calRef;
    const float watts = cal.valid ? voltsToWatts(cal, voltage) : (voltage / ADC_VREF) * _maxWatts;
    return isfinite(watts) ? fmaxf(0.0f, watts) : 0.0f;
}

void RfPowerSensor::setCalTable_(bool isFwd, const float *vPts, const float *wPts, uint8_t n)
{
    if (!vPts || !wPts || n < 2 || n > CAL_MAX_POINTS)
    {
        if (isFwd)
            _fwdTableCount = 0;
        else
            _refTableCount = 0;
        return;
    }

    if (isFwd)
    {
        _fwdTableCount = n;
        for (uint8_t i = 0; i < n; ++i)
        {
            _fwdTableV[i] = vPts[i];
            _fwdTableW[i] = wPts[i];
        }
    }
    else
    {
        _refTableCount = n;
        for (uint8_t i = 0; i < n; ++i)
        {
            _refTableV[i] = vPts[i];
            _refTableW[i] = wPts[i];
        }
    }
}

float RfPowerSensor::v2wTable_(bool isFwd, float V) const
{
    const uint8_t n = isFwd ? _fwdTableCount : _refTableCount;
    if (n < 2)
        return NAN;

    const float *v = isFwd ? _fwdTableV : _refTableV;
    const float *w = isFwd ? _fwdTableW : _refTableW;

    // Below the first knot there is no measured calibration.
    if (V < v[0])
        return 0.0f;

    // Clamp high end: above last point -> last watts
    if (V >= v[n - 1])
        return fmaxf(0.0f, w[n - 1]);

    // Find bracket interval
    for (uint8_t i = 0; i < (uint8_t)(n - 1); ++i)
    {
        const float v0 = v[i];
        const float v1 = v[i + 1];
        if (V >= v0 && V <= v1)
        {
            const float w0 = w[i];
            const float w1 = w[i + 1];
            const float dv = (v1 - v0);
            if (fabsf(dv) < 1e-6f)
                return fmaxf(0.0f, w0);

            const float t = (V - v0) / dv;
            const float out = w0 + t * (w1 - w0);
            return fmaxf(0.0f, out);
        }
    }

    // Should not reach here
    return fmaxf(0.0f, w[n - 1]);
}
