// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <Arduino.h>

namespace RayModulation
{
    // You can increase to 2048 if RAM allows
    constexpr uint16_t MOD_MAX_SAMPLES = 1024;
    constexpr uint16_t MOD_MIN_SAMPLES = 128;
    constexpr float MOD_DEFAULT_RATE = 10000.0f; // Hz

    struct CaptureConfig
    {
        uint8_t couplerIndex = 0;    // 0..3
        bool forward = true;         // true = FWD, false = REF
        uint16_t sampleCount = 1024; // clamped [MOD_MIN_SAMPLES, MOD_MAX_SAMPLES]
        float sampleRateHz = MOD_DEFAULT_RATE;
    };

    struct CaptureState
    {
        bool armed = false;
        bool busy = false;
        bool ready = false;
        uint32_t startedMs = 0;
        uint32_t completedMs = 0;
        CaptureConfig cfg;
        uint16_t validSamples = 0;
    };

    // Call once at startup
    void begin();

    // Arm a new capture (one-shot)
    void armCapture(const CaptureConfig &cfg);

    // Current state
    const CaptureState &state();

    // Convenience
    bool isReady();

    // Sample buffer access
    uint16_t sampleCount();
    const int16_t *samples();

    // TEMP: software-generated waveform so you can test
    // Replace with real ADC-based capture later.
    void debugFillSineWave();
} // namespace RayModulation
