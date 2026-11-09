// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#ifndef FREQUENCY_METER_H
#define FREQUENCY_METER_H

#include <Arduino.h>
#include "driver/pulse_cnt.h"

class FrequencyMeter
{
public:
    FrequencyMeter(uint8_t inputPin, float prescaler = 4.0f);
    void begin();
    void update(uint32_t nowMs);

    float getFrequencyHz() const { return _freqHz; }
    bool isValid() const { return _valid; }

private:
    static bool IRAM_ATTR on_reach_watchpoint_static(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx);

    uint8_t _inputPin;
    float _prescaler;
    pcnt_unit_handle_t _pcntUnit = nullptr;
    pcnt_channel_handle_t _pcntChan = nullptr;

    volatile int64_t _overflowCount = 0;

    uint32_t _lastGateMs = 0;
    uint64_t _lastMicros = 0;
    double _lastTotalPulses = 0;
    float _freqHz = 0.0f;
    bool _valid = false;

    static constexpr uint32_t GATE_TIME_MS = 100;
    static constexpr int16_t PCNT_LIMIT = 31000;
    const uint32_t VOX_HOLD_MS = 1500; // Hold frequency for 1.5 seconds of silence

    static constexpr int AVG_SAMPLES = 3;
    double _avgBuffer[AVG_SAMPLES] = {0};
    int _avgIndex = 0;
    uint32_t _lastValidSignalMs = 0;
    bool _bufferInitialized = false;
    double _peakFreqHz = 0;
};

#endif