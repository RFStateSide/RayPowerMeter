// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "hw/AuxInputs.h"
#include "core/Constants.h"
#include <algorithm>
#include <math.h>

// Provided by ModScopeController.cpp; true while a capture is active.
extern volatile bool g_modScopeCaptureActive;

// If you want, you can also include pinout.h here and use named macros
// like AUX1_ADC_PIN / AUX2_ADC_PIN instead of raw pin numbers.

static inline float trimmedMeanU16(uint16_t *v, int n, int trim)
{
    if (!v || n <= 0)
        return 0.0f;

    if (trim < 0)
        trim = 0;
    if (trim > (n - 1) / 2)
        trim = (n - 1) / 2;

    // Sort in-place (n is small: typically 8/16/32)
    std::sort(v, v + n);

    const int lo = trim;
    const int hi = n - trim; // exclusive
    uint32_t sum = 0;
    for (int i = lo; i < hi; ++i)
        sum += static_cast<uint32_t>(v[i]);

    const int denom = (hi - lo);
    return (denom > 0) ? (static_cast<float>(sum) / static_cast<float>(denom)) : 0.0f;
}

AuxInputs::AuxInputs(int pin1, int pin2, int pin3)
{
    _pins[0] = pin1;
    _pins[1] = pin2;
    _pins[2] = pin3;

    // Default configs and readings: disabled / zero
    for (uint8_t i = 0; i < kNumChannels; ++i)
    {
        _cfg[i] = AuxInputConfig{};
        _ch[i] = AuxReading{};
    }
}

void AuxInputs::begin()
{
    // Configure pins as analog inputs
    for (uint8_t i = 0; i < kNumChannels; ++i)
    {
        if (_pins[i] >= 0)
        {
            pinMode(_pins[i], INPUT);
        }
    }

    // Match whatever you’re using elsewhere (12-bit here)
    analogReadResolution(12);
}

void AuxInputs::configureChannel(uint8_t index, const AuxInputConfig &cfg)
{
    if (index >= kNumChannels)
        return;

    _cfg[index] = cfg;
}

void AuxInputs::update()
{
    // Freeze telemetry ADC reads while ModScope is capturing.
    // This avoids ADC resource conflicts (continuous mode) and improves capture rate (analogRead backend).
    if (g_modScopeCaptureActive)
    {
        return;
    }

    for (uint8_t i = 0; i < kNumChannels; ++i)
    {
        const AuxInputConfig &cfg = _cfg[i];

        if (cfg.type == AuxInputType::None || _pins[i] < 0)
        {
            _ch[i].rawVolts = 0.0f;
            _ch[i].value = 0.0f;
            continue;
        }

        // Trimmed-mean ADC sampling (same constants as RfPowerSensor)
        uint16_t samples[ADC_SAMPLES_BURST];

        for (int s = 0; s < ADC_SAMPLES_BURST; ++s)
        {
            int raw = analogRead(_pins[i]);
            if (raw < 0)
                raw = 0;
            if (raw > ADC_MAX)
                raw = ADC_MAX;

            samples[s] = static_cast<uint16_t>(raw);
        }

        // Convert ADC counts -> volts using trimmed mean
        const float tm = trimmedMeanU16(samples, ADC_SAMPLES_BURST, ADC_TRIM_COUNT);
        const float v = (static_cast<float>(tm) / ADC_MAX) * ADC_VREF;

        _ch[i].rawVolts = v;
        _ch[i].value = mapVoltsToEng(cfg, v);
    }
}

AuxReading AuxInputs::getChannel(uint8_t index) const
{
    if (index >= kNumChannels)
        return AuxReading{};

    return _ch[index];
}

float AuxInputs::samplePinToVolts(int pin) const
{
    int raw = analogRead(pin);
    if (raw < 0)
        raw = 0;

    return (static_cast<float>(raw) / ADC_MAX) * ADC_VREF;
}

// -----------------------------------------------------------------------------
// Thermistor divider topology selection
//
// true  = 3.3V -> Rs (pull-up) -> ADC -> NTC -> GND   (MOST COMMON)
// false = 3.3V -> NTC -> ADC -> Rs (pull-down) -> GND
// -----------------------------------------------------------------------------
static constexpr bool THERMISTOR_PULLUP_TO_VREF = true;
float AuxInputs::mapVoltsToEng(const AuxInputConfig &cfg, float volts) const
{
    if (cfg.type == AuxInputType::None)
        return 0.0f;

    // Clamp input voltage to sane limits
    if (volts < 0.0f)
        volts = 0.0f;
    if (volts > ADC_VREF)
        volts = ADC_VREF;

    float eng = 0.0f;

    if (cfg.type == AuxInputType::Temperature)
    {
        // Option A (recommended): Thermistor Beta model.
        // Topology: Vref -> Thermistor -> ADC -> Rseries -> GND
        // V = Vref * Rseries / (Rseries + Rtherm)
        // => Rtherm = Rseries * (Vref - V) / V
        if (cfg.tempModel == 1)
        {
            const float V = volts;
            const float Rs = (cfg.thermSeriesOhms > 1.0f) ? cfg.thermSeriesOhms : 10000.0f;
            const float R0 = (cfg.thermR0Ohms > 1.0f) ? cfg.thermR0Ohms : 10000.0f;
            const float B = (cfg.thermBeta > 10.0f) ? cfg.thermBeta : 3950.0f;
            const float T0K = cfg.thermT0C + 273.15f;

            // Avoid divide-by-zero if V is extremely small
            if (V <= 0.001f)
            {
                eng = cfg.temperatureMaxRange; // effectively "very hot" in this topology
            }
            else
            {
                float Rtherm = 0.0f;

                if constexpr (THERMISTOR_PULLUP_TO_VREF)
                {
                    // 3.3V -> Rs -> ADC -> NTC -> GND
                    // V = Vref * (Rntc / (Rs + Rntc))
                    // => Rntc = Rs * V / (Vref - V)
                    if (V < (ADC_VREF - 0.001f))
                        Rtherm = Rs * (V / (ADC_VREF - V));
                    else
                        Rtherm = Rs; // fallback
                }
                else
                {
                    // 3.3V -> NTC -> ADC -> Rs -> GND
                    // V = Vref * (Rs / (Rs + Rntc))
                    // => Rntc = Rs * (Vref - V) / V
                    if (V > 0.001f)
                        Rtherm = Rs * (ADC_VREF - V) / V;
                    else
                        Rtherm = Rs; // fallback
                }

                // 1/T = 1/T0 + (1/B) * ln(R/R0)
                const float invT = (1.0f / T0K) + (1.0f / B) * logf(Rtherm / R0);
                const float TK = (invT != 0.0f) ? (1.0f / invT) : T0K;
                eng = TK - 273.15f; // degC
            }
        }
        else
        {
            // Linear fallback: volts -> 0..temperatureMaxRange
            eng = (volts / ADC_VREF) * cfg.temperatureMaxRange;
        }

        // Clamp temperature to a sensible range (allow a bit below zero)
        const float tMax = (cfg.temperatureMaxRange > 1.0f) ? cfg.temperatureMaxRange : 150.0f;
        if (eng < -40.0f)
            eng = -40.0f;
        if (eng > tMax)
            eng = tMax;
    }
    else
    {
        // Voltage / Current: map 0..ADC_VREF to 0..maxRange
        eng = (volts / ADC_VREF) * cfg.maxRange;
    }

    // Apply final linear calibration to engineering value
    eng = (eng * cfg.cal_m) + cfg.cal_b;
    return eng;
}
