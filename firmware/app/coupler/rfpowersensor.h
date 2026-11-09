// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#ifndef RAYPOWERMETER_RFPOWERSENSOR_H
#define RAYPOWERMETER_RFPOWERSENSOR_H

#include <Arduino.h>
#include "core/Constants.h" // defines ADC_VREF and ADC_COUNTS_MAX

enum class PeakMode : uint8_t
{
    HoldUntilHigher = 0,
    HoldThenDecay = 1,
    ContinuousDecay = 2
};

enum class CalType : uint8_t
{
    LinearWatts,
    LogDbm,
    TableWatts
};
struct RfCal
{
    CalType type = CalType::LinearWatts;
    float m = 0.0f; // slope (W/V for Linear; dB/V for Log)
    float b = 0.0f; // intercept (W for Linear; dBm for Log)
    bool valid = false;
};

class RfPowerSensor
{
public:
    // Generic ADC-based sensor: FWD + REF + optional interrupt
    RfPowerSensor(const char *name,
                  uint8_t pinFwd,
                  uint8_t pinRef,
                  float maxWatts,
                  int interruptPin = -1,
                  uint16_t peakHoldTimeMs = 0);

    void begin();
    void update();

    // Instantaneous (last-sampled) volts
    float getForwardVoltage() const;
    float getReflectedVoltage() const;

    // UNSMOOTHED watts (uses last-sampled volts)
    float getForwardPowerWatts() const;
    float getReflectedPowerWatts() const;

    // Optional: smoothed watts (EMA) for UI-only use
    float getForwardPowerWattsSmoothed() const;
    float getReflectedPowerWattsSmoothed() const;

    // Peak markers (managed per PeakMode)
    float getPeakForwardPowerWatts() const;
    float getPeakReflectedPowerWatts() const;

    // SWR from forward/reflected watts
    float getSwr() const;
    float getSwrSmoothed() const;

    const char *getName() const;

    // Optional: external ISR can call this to flag RF activity
    void handleInterrupt();
    bool isRfDetected() const;

    // Optional: let the sensor manage peak logic internally (default: true)
    void setUseInternalPeak(bool enable) { _useInternalPeak = enable; }

    // Optional: configure peak behavior at runtime
    void setPeakMode(PeakMode mode) { _peakMode = mode; }

    void setPeakParams(uint16_t holdMs, float decayWps)
    {
        if (holdMs)
            _peakHoldTimeMs = holdMs;
        if (decayWps > 0.0f)
            _peakDecayWps = decayWps;
    }

    // --- Direct cal set ---
    void setFwdCalLinear(float m_W_per_V, float b_W)
    {
        _calFwd = {CalType::LinearWatts, m_W_per_V, b_W, true};
    }

    void setRefCalLinear(float m_W_per_V, float b_W)
    {
        _calRef = {CalType::LinearWatts, m_W_per_V, b_W, true};
    }

    void setFwdCalTable(const float *vPts, const float *wPts, uint8_t n)
    {
        setCalTable_(true, vPts, wPts, n);
    }

    void setRefCalTable(const float *vPts, const float *wPts, uint8_t n)
    {
        setCalTable_(false, vPts, wPts, n);
    }

    void clearFwdCalTable()
    {
        _fwdTableCount = 0;
        // keep legacy cal intact; runtime will fall back automatically
    }

    void clearRefCalTable()
    {
        _refTableCount = 0;
    }

    void setFwdCalLog(float m_dB_per_V, float b_dBm)
    {
        _calFwd = {CalType::LogDbm, m_dB_per_V, b_dBm, true};
    }

    void setRefCalLog(float m_dB_per_V, float b_dBm)
    {
        _calRef = {CalType::LogDbm, m_dB_per_V, b_dBm, true};
    }

    void clearCal() { _calFwd.valid = _calRef.valid = false; }

    void clearFwdCal()
    {
        _calFwd.valid = false;
    }

    void clearRefCal()
    {
        _calRef.valid = false;
    }

    // --- Solve from two points (handy for “calibrate from measurement”) ---
    static RfCal solveLinear2pt(float V1, float W1, float V2, float W2)
    {
        RfCal c;
        c.type = CalType::LinearWatts;
        c.m = (W2 - W1) / (V2 - V1);
        c.b = W1 - c.m * V1;
        c.valid = true;
        return c;
    }

    static RfCal solveLog2pt(float V1, float dBm1, float V2, float dBm2)
    {
        RfCal c;
        c.type = CalType::LogDbm;
        c.m = (dBm2 - dBm1) / (V2 - V1);
        c.b = dBm1 - c.m * V1;
        c.valid = true;
        return c;
    }

    // expose maxW for reset-to-default math
    float getMaxWatts() const { return _maxWatts; }

    // expose current calibration (for save/load later)
    void getFwdCal(RfCal &out) const { out = _calFwd; }
    void getRefCal(RfCal &out) const { out = _calRef; }

    // Change the full-scale range used for bar/percentage mapping
    // (used when applying per-coupler preferences).
    void setFullScaleWatts(float maxW);
    float getFullScaleWatts() const { return _maxWatts; }

    float getLastFwdVoltage() const { return _voltageFwd; }
    float getLastRefVoltage() const { return _voltageRef; }

    // Coupler type awareness: NONE / 200W / 2kW etc.
    void setCouplerType(CouplerType t) { _couplerType = t; }
    CouplerType couplerType() const { return _couplerType; }
    bool isEnabled() const { return _couplerType != CouplerType::NONE; }

private:
    friend struct CalibrationTestAccess;

    float readVoltage(uint8_t pin) const;

private:
    const char *_name;

    uint8_t _pinFwd;
    uint8_t _pinRef;

    float _maxWatts;
    int _interruptPin;

    // Peak behavior
    bool _useInternalPeak = true;
    PeakMode _peakMode = PeakMode::HoldThenDecay;
    uint16_t _peakHoldTimeMs = 1000; // ms
    float _peakDecayWps = 400.0f;    // watts per second

    // Last measured (unsmoothed) volts
    float _voltageFwd;
    float _voltageRef;

    // Smoothed volts (EMA)
    float _emaFwd;
    float _emaRef;

    // Coupler config (from UserPreferences)
    CouplerType _couplerType = CouplerType::NONE;

    // RF presence (optional interrupt)
    volatile bool _rfDetected;

    // Peak tracking
    float peakForwardWatts;
    unsigned long peakForwardTimestamp;
    float peakReflectedWatts;
    unsigned long peakReflectedTimestamp;

    // Calibration data
    RfCal _calFwd, _calRef;


    uint8_t _fwdTableCount = 0;
    uint8_t _refTableCount = 0;
    float _fwdTableV[CAL_MAX_POINTS]{0};
    float _fwdTableW[CAL_MAX_POINTS]{0};
    float _refTableV[CAL_MAX_POINTS]{0};
    float _refTableW[CAL_MAX_POINTS]{0};

    void setCalTable_(bool isFwd, const float *vPts, const float *wPts, uint8_t n);
    float v2wTable_(bool isFwd, float V) const;
    float calibratedWatts_(bool isFwd, float voltage) const;

    // Burst peak voltages (optional)
    float _voltageFwdPeak = 0.0f;
    float _voltageRefPeak = 0.0f;

    // Publish-mode selection (avg vs peak) based on crest factor
    bool _publishPeak = false;
    uint32_t _publishPeakUntilMs = 0;
    float _crestEma = 1.0f;
};

#endif // RAYPOWERMETER_RFPOWERSENSOR_H
