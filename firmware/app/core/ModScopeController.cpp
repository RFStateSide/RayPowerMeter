// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/ModScopeController.h"

#include "hw/pinout.h"
#include "core/Constants.h"

// Global: true while ModScope is actively capturing samples.
// Used to freeze other ADC users (telemetry/protection) to avoid conflicts and improve capture rate.
volatile bool g_modScopeCaptureActive = false;

// ----------------------------------------------------------------------------
// Continuous ADC support - SAFE detection
//
// IMPORTANT:
// - The Arduino "analogContinuous*" API exists in Arduino-ESP32 3.x.
// - On Arduino-ESP32 2.x, esp32-hal-adc.h exists but those symbols do NOT.
//   Therefore, we must gate on ESP_ARDUINO_VERSION, not header presence.
// ----------------------------------------------------------------------------

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// Defaults if the macro/header isn't present
#ifndef ESP_ARDUINO_VERSION
#define ESP_ARDUINO_VERSION 0
#endif

#ifndef ESP_ARDUINO_VERSION_VAL
// If the header didn't provide this helper, make checks safely fail.
#define ESP_ARDUINO_VERSION_VAL(major, minor, patch) 0
#endif

// User overrides (optional): -D RAYPM_USE_ADC_CONTINUOUS=0 or =1
#ifndef RAYPM_USE_ADC_CONTINUOUS
#define RAYPM_USE_ADC_CONTINUOUS 1
#endif

#ifndef RAYPM_USE_ADC_CONTINUOUS_IDF
#define RAYPM_USE_ADC_CONTINUOUS_IDF 1
#endif

// Arduino analogContinuous* API: ONLY on Arduino-ESP32 3.x
#if (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)) && RAYPM_USE_ADC_CONTINUOUS
#include "esp32-hal-adc.h"
#define RAYPM_HAS_ADC_CONTINUOUS 1
#else
#define RAYPM_HAS_ADC_CONTINUOUS 0
#endif

// ESP-IDF adc_continuous driver headers can exist in multiple locations.
// We can compile this when headers exist AND the user hasn't disabled it.
#if defined(__has_include) && RAYPM_USE_ADC_CONTINUOUS_IDF
#if __has_include("esp_adc/adc_continuous.h")
#include "esp_adc/adc_continuous.h"
#define RAYPM_HAS_ADC_CONTINUOUS_IDF 1
#elif __has_include("driver/adc_continuous.h")
#include "driver/adc_continuous.h"
#define RAYPM_HAS_ADC_CONTINUOUS_IDF 1
#else
#define RAYPM_HAS_ADC_CONTINUOUS_IDF 0
#endif

#if __has_include("esp_adc/adc_oneshot.h")
#include "esp_adc/adc_oneshot.h"
#elif __has_include("driver/adc_oneshot.h")
#include "driver/adc_oneshot.h"
#endif

#if __has_include("esp_err.h")
#include "esp_err.h"
#endif
#else
#define RAYPM_HAS_ADC_CONTINUOUS_IDF 0
#endif

// Small helper for constraining values
template <typename T>
static T clampValue(T v, T lo, T hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

ModScopeController::ModScopeController()
    : _state(ModScopeState::Idle),
      _writeIndex(0),
      _samplePeriodUs(0),
      _lastSampleUs(0),
      _startMs(0),
      _activeCoupler(0),
      _capturedCount(0),
      _usedSampleRateHz(0),
      _firstSampleUsActual(0),
      _lastSampleUsActual(0),
      _targetSampleRateHz(0)
{
}

void ModScopeController::begin()
{
    // Make sure ADC is in 12-bit mode (0..4095)
    analogReadResolution(12);

#if RAYPM_HAS_ADC_CONTINUOUS && RAYPM_USE_ADC_CONTINUOUS
    // Configure defaults for continuous ADC too (12-bit, typical attenuation).
    analogContinuousSetWidth(12);
    analogContinuousSetAtten(ADC_11db);
#endif

    Serial.printf("[ModScope] build: ESP_ARDUINO_VERSION=%lu  ARD_CONT=%d  IDF_CONT=%d\n",
                  (unsigned long)ESP_ARDUINO_VERSION,
                  (int)RAYPM_HAS_ADC_CONTINUOUS,
                  (int)RAYPM_HAS_ADC_CONTINUOUS_IDF);
}

#if RAYPM_HAS_ADC_CONTINUOUS_IDF
static uint8_t s_adcRxBuf[4096]; // DMA drain buffer
#endif
void ModScopeController::update()
{
    // IMPORTANT: start adc_continuous ONLY from the loop() task context.
    // startCapture() may be called from the HTTP task, so we defer actual beginCapture_() here.
    if (_startPending)
    {
        _startPending = false;

        // Apply pending config and actually begin capture now (loop task context).
        _cfg = _pendingCfg;
        beginCapture_(); // sets g_modScopeCaptureActive + starts IDF/Arduino/analogRead path

        // If beginCapture_() failed it will set Error; either way, don't fall through this tick.
        return;
    }

    if (_state != ModScopeState::Capturing)
        return;

    // Start capture from loop task context (required for IDF adc_continuous stop/deinit symmetry)
    if (_startPending)
    {
        _startPending = false;
        _cfg = _pendingCfg;
        beginCapture_(); // This is where adc_continuous_start is allowed
        // beginCapture_() sets g_modScopeCaptureActive=true when it actually starts
    }

#if RAYPM_HAS_ADC_CONTINUOUS_IDF
    // Preferred path: ESP-IDF adc_continuous (DMA). Drain frames into the capture buffer.
    if (_useAdcContinuousIdf && _adcHandle)
    {
        const uint32_t remaining =
            (_cfg.sampleCount > _writeIndex) ? (_cfg.sampleCount - _writeIndex) : 0;

        if (remaining == 0)
        {
            // Capture already complete
            stopAdcContinuousIdf_(); // loop task context (safe)
            g_modScopeCaptureActive = false;
            _state = ModScopeState::Ready;
            return;
        }

        adc_continuous_handle_t handle =
            reinterpret_cast<adc_continuous_handle_t>(_adcHandle);

        uint32_t ret_num = 0;
        esp_err_t err = adc_continuous_read(handle,
                                            s_adcRxBuf,
                                            sizeof(s_adcRxBuf),
                                            &ret_num,
                                            0 /* no wait */);

        // Non-blocking: nothing ready yet
        if (err == ESP_ERR_TIMEOUT || ret_num == 0)
            return;

        if (err != ESP_OK)
        {
            // Stop/deinit ONLY here (loop task context)
            stopAdcContinuousIdf_();
            g_modScopeCaptureActive = false;
            _state = ModScopeState::Error;
            return;
        }

        const uint32_t bytesPerSample = sizeof(adc_digi_output_data_t);
        const uint32_t sampleCount = (bytesPerSample > 0) ? (ret_num / bytesPerSample) : 0;

        const adc_digi_output_data_t *out =
            reinterpret_cast<const adc_digi_output_data_t *>(s_adcRxBuf);

        for (uint32_t i = 0; i < sampleCount && _writeIndex < _cfg.sampleCount; ++i)
        {
            // TYPE2 packs 12-bit data + channel info. Mask to 12 bits.
            const uint16_t raw12 = static_cast<uint16_t>(out[i].type2.data) & 0x0FFF;

            // Filter: only accept samples from our configured channel.
            // (This avoids storing junk if the driver emits other channels/markers.)
            if (static_cast<int>(out[i].type2.channel) != _adcChannel)
                continue;

            _buffer[_writeIndex++] = raw12;
        }

        _capturedCount = _writeIndex;

        if (_writeIndex >= _cfg.sampleCount)
        {
            stopAdcContinuousIdf_();
            g_modScopeCaptureActive = false;

            // Use requested rate for FFT axis
            _usedSampleRateHz = _targetSampleRateHz;

            uint16_t mn = 0xFFFF, mx = 0;
            for (uint16_t i = 0; i < _cfg.sampleCount; ++i)
            {
                const uint16_t v = _buffer[i];
                if (v < mn)
                    mn = v;
                if (v > mx)
                    mx = v;
            }

            Serial.printf("[ModScope] capture done (IDF): n=%u min=%u max=%u first=%u,%u,%u,%u chan=%d\n",
                          (unsigned)_cfg.sampleCount,
                          (unsigned)mn, (unsigned)mx,
                          (unsigned)_buffer[0], (unsigned)_buffer[1],
                          (unsigned)_buffer[2], (unsigned)_buffer[3],
                          (int)_adcChannel);

            _state = ModScopeState::Ready;
        }

        return; // Don't run analogRead paced loop while IDF owns ADC
    }
#endif // RAYPM_HAS_ADC_CONTINUOUS_IDF

    // --- Fallback path: analogRead() paced by micros() ---
    if (_samplePeriodUs == 0)
    {
        g_modScopeCaptureActive = false;
        _state = ModScopeState::Error;
        return;
    }

    uint32_t nowUs = micros();

    uint16_t remaining = 0;
    if (_cfg.sampleCount > _writeIndex)
        remaining = static_cast<uint16_t>(_cfg.sampleCount - _writeIndex);

    uint16_t maxPerCall = remaining;
    if (maxPerCall > 4096)
        maxPerCall = 4096;

    uint16_t sinceYield = 0;

    while (maxPerCall-- > 0 &&
           _state == ModScopeState::Capturing &&
           (uint32_t)(nowUs - _lastSampleUs) >= _samplePeriodUs)
    {
        _lastSampleUs += _samplePeriodUs;
        sampleOnce_();

        nowUs = micros();

        if (++sinceYield >= 64)
        {
            sinceYield = 0;
            yield();
            nowUs = micros();
        }
    }
}

bool ModScopeController::startCapture(const ModScopeConfig &cfg)
{
    // Busy (either already capturing, or we already queued one)
    if (_state == ModScopeState::Capturing || _startPending)
        return false;

    ModScopeConfig c = cfg;

    // Clamp to buffer limits
    c.sampleCount = clampValue<uint16_t>(c.sampleCount,
                                         MODSCOPE_MIN_SAMPLES,
                                         MODSCOPE_MAX_SAMPLES);

    // Your hardware/driver limit (clamp to 48k)
    c.sampleRateHz = clampValue<uint32_t>(c.sampleRateHz, 2000, 48000);

#ifdef NUM_COUPLERS
    if (c.couplerIndex >= NUM_COUPLERS)
        c.couplerIndex = 0;
#endif

    // Queue request (do NOT start ADC here — HTTP task context)
    _pendingCfg = c;
    _startPending = true;

    // Make status show "busy" immediately
    _cfg = c;
    _activeCoupler = c.couplerIndex;
    _writeIndex = 0;
    _capturedCount = 0;
    _usedSampleRateHz = c.sampleRateHz;
    _startMs = millis();
    _state = ModScopeState::Capturing;

    return true;
}

void ModScopeController::reset()
{
#if RAYPM_HAS_ADC_CONTINUOUS_IDF
    if (_useAdcContinuousIdf)
        stopAdcContinuousIdf_();
#endif
#if RAYPM_HAS_ADC_CONTINUOUS && RAYPM_USE_ADC_CONTINUOUS
    if (_useAdcContinuous)
    {
        analogContinuousStop();
        analogContinuousDeinit();
        _useAdcContinuous = false;
        _adcContinuousPin = 0;
    }
#endif

    g_modScopeCaptureActive = false;
    _state = ModScopeState::Idle;
    _writeIndex = 0;
    _capturedCount = 0;
    _usedSampleRateHz = 0;
    _startPending = false;
}

ModScopeMeta ModScopeController::getMeta() const
{
    ModScopeMeta m;
    m.state = _state;
    m.coupler = _activeCoupler;

    // Report progress while capturing
    if (_state == ModScopeState::Capturing)
        m.sampleCount = _writeIndex;
    else
        m.sampleCount = _capturedCount;

    m.sampleRateHz = _usedSampleRateHz;
    m.startMs = _startMs;
    return m;
}

uint16_t ModScopeController::copySamples(uint16_t *dest, uint16_t maxCount) const
{
    if (!dest)
        return 0;

    if (_state != ModScopeState::Ready)
        return 0;

    uint16_t n = _capturedCount;
    if (n > maxCount)
        n = maxCount;

    for (uint16_t i = 0; i < n; ++i)
        dest[i] = _buffer[i];

    return n;
}

// -----------------------------------------------------------------------------
// ESP-IDF adc_continuous (DMA) sampler helpers (Option A2)
// -----------------------------------------------------------------------------
#if RAYPM_HAS_ADC_CONTINUOUS_IDF

// static uint8_t s_adcRxBuf[2048]; // 2 KB DMA drain buffer (static to avoid stack pressure)

bool ModScopeController::startAdcContinuousIdf_(int gpioPin)
{
    stopAdcContinuousIdf_();

    adc_unit_t unit;
    adc_channel_t chan;
    esp_err_t err = adc_oneshot_io_to_channel(static_cast<gpio_num_t>(gpioPin), &unit, &chan);
    if (err != ESP_OK)
    {
        Serial.printf("[ModScope] IDF map pin->channel failed: pin=%d err=%s\n", gpioPin, esp_err_to_name(err));
        return false;
    }

    _adcUnit = static_cast<int>(unit);
    _adcChannel = static_cast<int>(chan);

    Serial.printf("[ModScope] IDF pin map: pin=%d unit=%d chan=%d\n", gpioPin, (int)unit, (int)chan);

    // Handle configuration
    adc_continuous_handle_cfg_t handle_cfg = {};
    handle_cfg.max_store_buf_size = 4 * 1024; // internal DMA ring buffer (bytes)
    handle_cfg.conv_frame_size = 1024;        // bytes per frame (must be multiple of result size)

    adc_continuous_handle_t handle = nullptr;
    err = adc_continuous_new_handle(&handle_cfg, &handle);
    if (err != ESP_OK || handle == nullptr)
    {
        Serial.printf("[ModScope] IDF adc_continuous_new_handle failed: err=%s\n", esp_err_to_name(err));
        return false;
    }

    // Pattern configuration (single channel)
    adc_digi_pattern_config_t pattern[1] = {};
#if defined(ADC_ATTEN_DB_12)
    pattern[0].atten = ADC_ATTEN_DB_12; // preferred
#else
    pattern[0].atten = ADC_ATTEN_DB_11; // older cores
#endif
    pattern[0].channel = static_cast<adc_channel_t>(_adcChannel);
    pattern[0].unit = static_cast<adc_unit_t>(_adcUnit);
    pattern[0].bit_width = ADC_BITWIDTH_12;

    adc_continuous_config_t dig_cfg = {};
    dig_cfg.sample_freq_hz = _targetSampleRateHz;
    dig_cfg.conv_mode = (unit == ADC_UNIT_1) ? ADC_CONV_SINGLE_UNIT_1 : ADC_CONV_SINGLE_UNIT_2;
    // Force TYPE2 because your adc_digi_output_data_t only exposes .type2
    dig_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = pattern;

    err = adc_continuous_config(handle, &dig_cfg);
    if (err != ESP_OK)
    {
        Serial.printf("[ModScope] IDF adc_continuous_config failed: err=%s\n", esp_err_to_name(err));
        adc_continuous_deinit(handle);
        return false;
    }

    err = adc_continuous_start(handle);
    if (err != ESP_OK)
    {
        Serial.printf("[ModScope] IDF adc_continuous_start failed: err=%s\n", esp_err_to_name(err));
        adc_continuous_deinit(handle);
        return false;
    }

    _adcHandle = handle;
    _useAdcContinuousIdf = true;
    _captureStartUs = micros();
    _usedSampleRateHz = _targetSampleRateHz; // trust configured rate for FFT axis
    return true;
}

void ModScopeController::stopAdcContinuousIdf_()
{
#if RAYPM_HAS_ADC_CONTINUOUS_IDF
    adc_continuous_handle_t handle = nullptr;

    if (_adcHandle)
    {
        handle = reinterpret_cast<adc_continuous_handle_t>(_adcHandle);

        // Clear first to prevent double-stop if we re-enter via error/reset paths
        _adcHandle = nullptr;

        // Stop + deinit (MUST be called from the same task context as start)
        esp_err_t e1 = adc_continuous_stop(handle);
        esp_err_t e2 = adc_continuous_deinit(handle);

        if (e1 != ESP_OK || e2 != ESP_OK)
        {
            Serial.printf("[ModScope] stopAdcContinuousIdf_: stop=%d deinit=%d\n",
                          (int)e1, (int)e2);
        }
    }

    _useAdcContinuousIdf = false;

    // Never leave telemetry frozen if we stop/deinit
    g_modScopeCaptureActive = false;
#endif
}

#endif // RAYPM_HAS_ADC_CONTINUOUS_IDF

void ModScopeController::beginCapture_()
{
    Serial.printf("[ModScope] beginCapture: coupler=%u samples=%u rate=%lu\n",
                  (unsigned)_cfg.couplerIndex,
                  (unsigned)_cfg.sampleCount,
                  (unsigned long)_cfg.sampleRateHz);

    _writeIndex = 0;
    _capturedCount = 0;
    _activeCoupler = _cfg.couplerIndex;
    _firstSampleUsActual = 0;
    _lastSampleUsActual = 0;

    _targetSampleRateHz = _cfg.sampleRateHz;

    // While capturing, report the target (used for FFT axis; continuous drivers can be burst-drained).
    _usedSampleRateHz = _targetSampleRateHz;

    _samplePeriodUs = 1000000UL / (_targetSampleRateHz ? _targetSampleRateHz : 1);

    _lastSampleUs = micros() - _samplePeriodUs;
    _startMs = millis();

    // Sanity: check pin mapping
    int pin = couplerFwdPin_(_activeCoupler);
    if (pin < 0)
    {
        g_modScopeCaptureActive = false;
        _state = ModScopeState::Error;
        return;
    }

    // FIX: freeze telemetry as soon as capture is actually starting
    g_modScopeCaptureActive = true;

#if RAYPM_HAS_ADC_CONTINUOUS_IDF
    // Preferred: ESP-IDF adc_continuous (DMA) for true high-rate sampling.
    if (startAdcContinuousIdf_(pin))
    {
        Serial.printf("[ModScope] backend=IDF adc_continuous pin=%d target=%lu Hz\n", pin, (unsigned long)_targetSampleRateHz);
        _state = ModScopeState::Capturing;
        return;
    }
#endif

#if RAYPM_HAS_ADC_CONTINUOUS && RAYPM_USE_ADC_CONTINUOUS
    // Fallback: Arduino's analogContinuous wrapper. If this fails we fall back to analogRead().
    const uint8_t pins[] = {static_cast<uint8_t>(pin)};
    if (analogContinuous(pins, 1, 1, _targetSampleRateHz, nullptr))
    {
        analogContinuousStart();
        _useAdcContinuous = true;
        _adcContinuousPin = pins[0];
        Serial.printf("[ModScope] backend=Arduino analogContinuous pin=%d target=%lu Hz\n", pin, (unsigned long)_targetSampleRateHz);

        // In continuous mode, report the requested rate for the FFT axis.
        _usedSampleRateHz = _targetSampleRateHz;
    }
    else
    {
        Serial.printf("[ModScope] Arduino analogContinuous failed: pin=%d target=%lu Hz\n", pin, (unsigned long)_targetSampleRateHz);
        _useAdcContinuous = false;
        _adcContinuousPin = 0;
    }
#else
    _useAdcContinuous = false;
    _adcContinuousPin = 0;
#endif

    if (!_useAdcContinuousIdf && !_useAdcContinuous)
        Serial.printf("[ModScope] backend=analogRead pin=%d target=%lu Hz\n", pin, (unsigned long)_targetSampleRateHz);

    _state = ModScopeState::Capturing;
}

void ModScopeController::sampleOnce_()
{
    if (_writeIndex >= _cfg.sampleCount)
    {
        // Already done
        _capturedCount = _cfg.sampleCount;

        // FIX: ensure telemetry resumes even if we hit this path
        g_modScopeCaptureActive = false;

        _state = ModScopeState::Ready;
        return;
    }

    int pin = couplerFwdPin_(_activeCoupler);
    if (pin < 0)
    {
        g_modScopeCaptureActive = false;
        _state = ModScopeState::Error;
        return;
    }

    // Timestamp just before sampling (actual cadence measurement)
    const uint32_t tUs = micros();

    // Raw 12-bit ADC sample (0..4095)
    uint16_t raw = static_cast<uint16_t>(analogRead(pin));

    // Record first/last timestamps for measured sample rate
    if (_writeIndex == 0)
        _firstSampleUsActual = tUs;
    _lastSampleUsActual = tUs;

    _buffer[_writeIndex++] = raw;
    _capturedCount = _writeIndex; // Track progress

    if (_writeIndex >= _cfg.sampleCount)
    {
        _capturedCount = _cfg.sampleCount;

        // Compute measured average sample rate from timestamps
        if (_capturedCount > 1 && _firstSampleUsActual != 0)
        {
            const uint32_t deltaUs = static_cast<uint32_t>(_lastSampleUsActual - _firstSampleUsActual);
            if (deltaUs > 0)
            {
                const double measured =
                    (double(_capturedCount - 1) * 1000000.0) / double(deltaUs);

                // round to nearest uint32
                _usedSampleRateHz = static_cast<uint32_t>(measured + 0.5);
            }
            else
            {
                _usedSampleRateHz = _targetSampleRateHz;
            }
        }
        else
        {
            _usedSampleRateHz = _targetSampleRateHz;
        }

        // FIX: capture complete => allow telemetry ADC users to resume
        g_modScopeCaptureActive = false;

        // Tiny sanity debug: confirm we didn’t capture a flat/all-zero buffer
        uint16_t mn = 0xFFFF, mx = 0;
        for (uint16_t i = 0; i < _cfg.sampleCount; ++i)
        {
            const uint16_t v = _buffer[i];
            if (v < mn)
                mn = v;
            if (v > mx)
                mx = v;
        }
        Serial.printf("[ModScope] capture done (analogRead): n=%u min=%u max=%u first=%u,%u,%u,%u\n",
                      (unsigned)_cfg.sampleCount,
                      (unsigned)mn, (unsigned)mx,
                      (unsigned)_buffer[0], (unsigned)_buffer[1], (unsigned)_buffer[2], (unsigned)_buffer[3]);

        _state = ModScopeState::Ready;
    }
}

int ModScopeController::couplerFwdPin_(uint8_t coupler) const
{
    switch (coupler)
    {
    case 0:
        return COUPLER1_FWD_PIN;
    case 1:
        return COUPLER2_FWD_PIN;
    case 2:
        return COUPLER3_FWD_PIN;
    case 3:
        return COUPLER4_FWD_PIN;
    default:
        return -1;
    }
}
