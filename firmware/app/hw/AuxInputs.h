// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <Arduino.h>
#include "core/userprefs.h"

struct AuxInputConfig
{
    AuxInputType type = AuxInputType::None; // None / Voltage / Current / Temperature

    // Voltage/Current mapping: 0..ADC_VREF -> 0..maxRange
    float maxRange = 0.0f; // engineering units (e.g. V or A) for 3.3V full-scale

    // Temperature (thermistor) support
    // tempModel: 0=Linear, 1=ThermistorBeta (Option A)
    uint8_t tempModel = 1;
    float temperatureMaxRange = 150.0f;

    // Thermistor parameters (for tempModel=ThermistorBeta)
    // Topology: 3.3V -> Thermistor -> ADC node -> Rseries -> GND
    float thermSeriesOhms = 10000.0f; // series resistor to GND (ohms)
    float thermR0Ohms = 10000.0f;     // R@T0 (ohms)
    float thermBeta = 3950.0f;
    float thermT0C = 25.0f;

    // Optional linear calibration applied to final engineering value (all types)
    float cal_m = 1.0f;
    float cal_b = 0.0f;
};

struct AuxReading
{
    float rawVolts = 0.0f; // ADC → volts (0..3.3)
    float value = 0.0f;    // mapped engineering value (0..maxRange)
};

class AuxInputs
{
public:
    // Total AUX channels handled here
    static constexpr uint8_t kNumChannels = 3;

    // pin1 = AUX1 ADC pin (Voltage)
    // pin2 = AUX2 ADC pin (Current)
    // pin3 = AUX3 ADC pin (Temperature)
    AuxInputs(int pin1, int pin2, int pin3);

    void begin();
    void configureChannel(uint8_t index, const AuxInputConfig &cfg);
    void update();

    // Access readings by index (0..kNumChannels-1)
    AuxReading getChannel(uint8_t index) const;

    // Convenience accessors
    AuxReading getAux1() const { return _ch[0]; }
    AuxReading getAux2() const { return _ch[1]; }
    AuxReading getAux3() const { return _ch[2]; }

    AuxInputType getType(uint8_t index) const
    {
        if (index >= kNumChannels)
            return AuxInputType::None;
        return _cfg[index].type;
    }

    AuxInputConfig getChannelConfig(uint8_t index) const
    {
        if (index >= kNumChannels)
            return AuxInputConfig{};
        return _cfg[index];
    }

private:
    int _pins[kNumChannels];
    AuxInputConfig _cfg[kNumChannels];
    AuxReading _ch[kNumChannels];

    float samplePinToVolts(int pin) const;
    float mapVoltsToEng(const AuxInputConfig &cfg, float volts) const;
};
