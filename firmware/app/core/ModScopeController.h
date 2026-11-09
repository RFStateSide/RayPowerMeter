// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <Arduino.h>

// How many samples we support per capture.
// You said 512–2048; we’ll default to 1024 and allow up to this max.
// You can bump this later if RAM allows.
constexpr uint16_t MODSCOPE_MIN_SAMPLES = 512;
constexpr uint16_t MODSCOPE_MAX_SAMPLES = 4096;

// Simple state machine for the scope
enum class ModScopeState : uint8_t
{
    Idle = 0,
    Capturing,
    Ready,
    Error
};

// Capture configuration (per run)
struct ModScopeConfig
{
    uint8_t couplerIndex = 0;      // 0..(NUM_COUPLERS-1)
    uint16_t sampleCount = 1024;   // 512..MODSCOPE_MAX_SAMPLES
    uint32_t sampleRateHz = 20000; // e.g. 10–20 kHz for envelope
};

// Lightweight metadata for the last capture
struct ModScopeMeta
{
    ModScopeState state = ModScopeState::Idle;
    uint8_t coupler = 0;
    uint16_t sampleCount = 0;
    uint32_t sampleRateHz = 0;
    uint32_t startMs = 0;
};

class ModScopeController
{
public:
    ModScopeController();

    void begin();

    // Call frequently from loop()
    void update();

    // Start a capture. Returns false if already capturing.
    bool startCapture(const ModScopeConfig &cfg);

    // Reset/abort capture and clear state.
    void reset();

    // State queries
    ModScopeState state() const { return _state; }
    bool isIdle() const { return _state == ModScopeState::Idle; }
    bool isCapturing() const { return _state == ModScopeState::Capturing; }
    bool isReady() const { return _state == ModScopeState::Ready; }

    // Metadata for last capture (valid when state() == Ready)
    ModScopeMeta getMeta() const;

    // Copy out up to maxCount samples into dest.
    // Returns actual number copied.
    uint16_t copySamples(uint16_t *dest, uint16_t maxCount) const;

private:
    // Internal helpers
    void beginCapture_();
    void sampleOnce_();
    int couplerFwdPin_(uint8_t coupler) const;

#if defined(ESP_ARDUINO_VERSION)
    // ESP-IDF adc_continuous (Option A2) support
    bool startAdcContinuousIdf_(int gpioPin);
    void stopAdcContinuousIdf_();
#endif

private:
    volatile ModScopeState _state;

    ModScopeConfig _cfg;
    uint16_t _buffer[MODSCOPE_MAX_SAMPLES];

    uint16_t _writeIndex;
    uint32_t _samplePeriodUs;
    uint32_t _lastSampleUs;
    uint32_t _startMs;

    // Cached for convenience when reporting
    uint8_t _activeCoupler;
    uint16_t _capturedCount;
    uint32_t _usedSampleRateHz;

    uint32_t _firstSampleUsActual;
    uint32_t _lastSampleUsActual;
    uint32_t _targetSampleRateHz;

    // Arduino-ESP32 continuous ADC (Option A1) support.
    // When enabled, sampling is hardware-timed and we just drain results.
    bool _useAdcContinuous = false;
    uint8_t _adcContinuousPin = 0;

    // ESP-IDF adc_continuous (DMA) support (Option A2).
    // Stored as generic types so the header does not depend on IDF headers.
    bool _useAdcContinuousIdf = false;
    void *_adcHandle = nullptr; // adc_continuous_handle_t
    int _adcUnit = -1;          // adc_unit_t
    int _adcChannel = -1;       // adc_channel_t

    uint32_t _captureStartUs = 0; // capture timing (IDF adc_continuous)

    volatile bool _startPending = false;
    ModScopeConfig _pendingCfg;
};
