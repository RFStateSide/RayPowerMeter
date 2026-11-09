// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <Arduino.h>
#include "core/Constants.h"
#include "core/userprefs.h"

// AuxDigital manages NUM_AUX_DIGITAL channels (stable indexing: 0..NUM_AUX_DIGITAL-1).
// In development builds we can disable channels that collide with UART0 pins so Serial works.
//
// Build flag:
//   -D AUXDIGITAL_DEV_DISABLE_UART0
// Behavior:
//   Any channel mapped to GPIO43 or GPIO44 is treated as unavailable:
//     - never pinMode()'d
//     - forced Disabled
//     - read() returns false
//     - write() ignored
//     - configureChannel() ignored
class AuxDigital
{
public:
    static constexpr uint8_t kNumChannels = NUM_AUX_DIGITAL;

    enum class AuxDigitalMode : uint8_t
    {
        Disabled = 0,
        Input    = 1,
        Output   = 2
    };

    enum class AuxDigitalPull : uint8_t
    {
        None     = 0,
        PullUp   = 1,
        PullDown = 2
    };

    struct ChannelState
    {
        AuxDigitalMode mode = AuxDigitalMode::Disabled;
        AuxDigitalPull pull = AuxDigitalPull::None;
        bool value          = false;
    };

    AuxDigital(int pin1, int pin2, int pin3, int pin4);

    void begin();

    void configureChannel(uint8_t index, const AuxDigitalPreferences &cfg);
    void update(); // reads all INPUT channels into _state[].value

    bool read(uint8_t index) const;
    void write(uint8_t index, bool value);

    AuxDigitalMode mode(uint8_t index) const;
    AuxDigitalPull pull(uint8_t index) const;

    // True if the channel is physically usable in this build (pin valid and not reserved).
    bool isAvailable(uint8_t index) const;

private:
    int _pins[kNumChannels];
    ChannelState _state[kNumChannels];

    void applyPinConfig_(uint8_t index);

    static bool isReservedForUart0_(int pin);
};
