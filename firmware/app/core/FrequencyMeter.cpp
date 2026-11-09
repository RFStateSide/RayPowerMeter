// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/FrequencyMeter.h"
#include <string.h>

// Tuning constants
static constexpr float CALIBRATION_SCALER = 1.0f; // Adjust this: (Actual / Measured)
static constexpr uint32_t GLITCH_FILTER_NS = 12;

FrequencyMeter::FrequencyMeter(uint8_t inputPin, float prescaler)
    : _inputPin(inputPin), _prescaler(prescaler) {}

bool IRAM_ATTR FrequencyMeter::on_reach_watchpoint_static(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
    FrequencyMeter *instance = static_cast<FrequencyMeter *>(user_ctx);
    instance->_overflowCount++;
    return false;
}

void FrequencyMeter::begin()
{
    pcnt_unit_config_t unit_config = {};
    unit_config.low_limit = -31000;
    unit_config.high_limit = 31000;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &_pcntUnit));

    pcnt_chan_config_t chan_config = {};
    chan_config.edge_gpio_num = _inputPin;
    chan_config.level_gpio_num = -1;
    ESP_ERROR_CHECK(pcnt_new_channel(_pcntUnit, &chan_config, &_pcntChan));

    // Set to only count the rising edge
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(_pcntChan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));

    // Hardware Filter: This is your best defense against signal ringing
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = GLITCH_FILTER_NS,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(_pcntUnit, &filter_config));

    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(_pcntUnit, PCNT_LIMIT));

    pcnt_event_callbacks_t cbs = {};
    cbs.on_reach = on_reach_watchpoint_static; // Use the name your compiler liked

    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(_pcntUnit, &cbs, this));
    ESP_ERROR_CHECK(pcnt_unit_enable(_pcntUnit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(_pcntUnit));
    ESP_ERROR_CHECK(pcnt_unit_start(_pcntUnit));

    _lastGateMs = millis();
    _lastMicros = esp_timer_get_time(); // Add this line
}

void FrequencyMeter::update(uint32_t nowMs)
{
    if (nowMs - _lastGateMs >= GATE_TIME_MS)
    {
        uint64_t currentMicros = esp_timer_get_time();
        int pulseCount = 0;
        pcnt_unit_get_count(_pcntUnit, &pulseCount);

        noInterrupts();
        int64_t totalOverflows = _overflowCount;
        interrupts();

        uint64_t dtUs = currentMicros - _lastMicros;
        if (dtUs == 0)
            return;

        double currentTotalPulses = (static_cast<double>(totalOverflows) * 31000.0) + static_cast<double>(pulseCount);
        double pulsesInThisGate = currentTotalPulses - _lastTotalPulses;
        if (pulsesInThisGate < 0)
            pulsesInThisGate = 0;

        // Calculate Raw Frequency with your verified calibration
        double rawFreqHz = (pulsesInThisGate / static_cast<double>(dtUs)) * 1000000.0 * _prescaler * 1.00001263;

        if (rawFreqHz > 100000.0) // Signal detected
        {
            // --- PEAK-SYNC LOCK ---
            // If this is the start of a transmission, discard the first potentially partial gate
            // and wait for the next full 100ms window.
            if (!_valid)
            {
                _lastTotalPulses = currentTotalPulses;
                _lastMicros = currentMicros;
                _lastGateMs = nowMs;
                _lastValidSignalMs = nowMs;
                _valid = true;
                _bufferInitialized = false;
                _peakFreqHz = 0; // Reset peak for new transmission
                return;
            }

            _lastValidSignalMs = nowMs;

            // 1. Always track the absolute highest peak seen in this transmission
            if (rawFreqHz > _peakFreqHz)
            {
                _peakFreqHz = rawFreqHz;
            }

            // 2. SSB GATE: Only update the buffer if the signal is "High Quality"
            // If rawFreqHz is more than 2% below the peak, it's a voice 'valley'—ignore it.
            if (rawFreqHz > (_peakFreqHz * 0.98))
            {
                if (!_bufferInitialized)
                {
                    // Instant-Fill: Use the first high-quality gate to fill the entire buffer
                    for (int i = 0; i < AVG_SAMPLES; i++)
                        _avgBuffer[i] = rawFreqHz;
                    _avgIndex = 0;
                    _freqHz = (float)rawFreqHz;
                    _bufferInitialized = true;
                }
                else
                {
                    _avgBuffer[_avgIndex] = rawFreqHz;
                    _avgIndex = (_avgIndex + 1) % AVG_SAMPLES;

                    double sum = 0;
                    for (int i = 0; i < AVG_SAMPLES; i++)
                        sum += _avgBuffer[i];
                    _freqHz = (float)(sum / static_cast<double>(AVG_SAMPLES));
                }
            }
        }
        else // Silence
        {
            if (nowMs - _lastValidSignalMs > VOX_HOLD_MS)
            {
                _valid = false;
                _freqHz = 0;
                _peakFreqHz = 0;
                _bufferInitialized = false;
            }
        }

        _lastTotalPulses = currentTotalPulses;
        _lastMicros = currentMicros;
        _lastGateMs = nowMs;
    }
}