// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/ModulationScope.h"
#include <math.h>

namespace RayModulation
{
    static CaptureState g_state;
    static int16_t      g_samples[MOD_MAX_SAMPLES];

    void begin()
    {
        g_state = CaptureState{};
        // Nothing else for now; real HW init can go here later.
    }

    void debugFillSineWave()
    {
        // Simple 1 kHz-ish sine for testing, scaled to 12-bit ADC range
        const uint16_t n  = g_state.cfg.sampleCount;
        const float    fs = g_state.cfg.sampleRateHz <= 0.0f
                                ? MOD_DEFAULT_RATE
                                : g_state.cfg.sampleRateHz;

        const float toneHz = 1000.0f;
        const float w      = 2.0f * PI * toneHz / fs;

        for (uint16_t i = 0; i < n; ++i)
        {
            float s = sinf(w * i);
            // Center around mid-scale ~2048, amplitude ~1500
            int16_t v = static_cast<int16_t>(2048 + 1500 * s);
            g_samples[i] = v;
        }

        g_state.validSamples = n;
    }

    void armCapture(const CaptureConfig &cfg)
    {
        g_state.armed       = true;
        g_state.busy        = true;
        g_state.ready       = false;
        g_state.startedMs   = millis();
        g_state.completedMs = 0;
        g_state.cfg         = cfg;

        // Clamp counts
        if (g_state.cfg.sampleCount < MOD_MIN_SAMPLES)
            g_state.cfg.sampleCount = MOD_MIN_SAMPLES;
        if (g_state.cfg.sampleCount > MOD_MAX_SAMPLES)
            g_state.cfg.sampleCount = MOD_MAX_SAMPLES;

        // --------------------------------------------------------------------
        // TEMP IMPLEMENTATION:
        //  - Generate synthetic waveform immediately.
        //  - Mark capture as completed.
        // Replace this block with real ADC sampling later.
        // --------------------------------------------------------------------
        debugFillSineWave();
        g_state.busy        = false;
        g_state.ready       = true;
        g_state.completedMs = millis();
    }

    const CaptureState &state()
    {
        return g_state;
    }

    bool isReady()
    {
        return g_state.ready;
    }

    uint16_t sampleCount()
    {
        return g_state.validSamples;
    }

    const int16_t *samples()
    {
        return g_samples;
    }

} // namespace RayModulation
