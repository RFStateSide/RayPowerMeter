// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/AuxDigital.h"

AuxDigital::AuxDigital(int pin1, int pin2, int pin3, int pin4)
{
    _pins[0] = pin1;
    _pins[1] = pin2;
    _pins[2] = pin3;
    _pins[3] = pin4;

#if defined(AUXDIGITAL_DEV_DISABLE_UART0)
    // Disable any channel using UART0 pins so Serial logging works reliably.
    for (uint8_t i = 0; i < kNumChannels; ++i)
    {
        if (isReservedForUart0_(_pins[i]))
        {
            _pins[i] = -1; // mark unavailable
        }
    }
#endif
}

bool AuxDigital::isReservedForUart0_(int pin)
{
    // Your board notes:
    //  GPIO43 = USB UART0 TX
    //  GPIO44 = USB UART0 RX
    return (pin == 43) || (pin == 44);
}

bool AuxDigital::isAvailable(uint8_t index) const
{
    if (index >= kNumChannels)
        return false;
    return _pins[index] >= 0;
}

void AuxDigital::begin()
{
    for (uint8_t i = 0; i < kNumChannels; ++i)
    {
        _state[i].mode = AuxDigitalMode::Disabled;
        _state[i].pull = AuxDigitalPull::None;
        _state[i].value = false;

        // If unavailable (UART0 conflict), do NOT touch pin muxing.
        if (!isAvailable(i))
            continue;

        pinMode(_pins[i], INPUT); // safe default
    }
}

void AuxDigital::applyPinConfig_(uint8_t index)
{
    if (index >= kNumChannels)
        return;

    if (!isAvailable(index))
        return; // Do not touch UART0 pins in dev mode

    const int pin = _pins[index];

    switch (_state[index].mode)
    {
    case AuxDigitalMode::Input:
        switch (_state[index].pull)
        {
        case AuxDigitalPull::PullUp:
            pinMode(pin, INPUT_PULLUP);
            break;
        case AuxDigitalPull::PullDown:
            pinMode(pin, INPUT_PULLDOWN);
            break;
        case AuxDigitalPull::None:
        default:
            pinMode(pin, INPUT);
            break;
        }
        break;

    case AuxDigitalMode::Output:
        pinMode(pin, OUTPUT);
        digitalWrite(pin, _state[index].value ? HIGH : LOW);
        break;

    case AuxDigitalMode::Disabled:
    default:
        // High-impedance input, no pull
        pinMode(pin, INPUT);
        break;
    }
}

void AuxDigital::configureChannel(uint8_t index, const AuxDigitalPreferences &cfg)
{
    if (index >= kNumChannels)
        return;

    // If the pin is unavailable in this build, force Disabled and ignore config.
    if (!isAvailable(index))
    {
        _state[index].mode = AuxDigitalMode::Disabled;
        _state[index].pull = AuxDigitalPull::None;
        _state[index].value = false;
        return;
    }

    // Map prefs to runtime state
    _state[index].mode = static_cast<AuxDigitalMode>(cfg.mode);
    _state[index].pull = static_cast<AuxDigitalPull>(cfg.pull);

    // For outputs, seed value from defaultState; for inputs just leave false.
    if (_state[index].mode == AuxDigitalMode::Output)
        _state[index].value = cfg.defaultState;

    applyPinConfig_(index);
}

void AuxDigital::update()
{
    for (uint8_t i = 0; i < kNumChannels; ++i)
    {
        if (!isAvailable(i))
            continue;

        if (_state[i].mode == AuxDigitalMode::Input)
        {
            _state[i].value = (digitalRead(_pins[i]) == HIGH);
        }
        // For Output and Disabled we keep the last written value.
    }
}

bool AuxDigital::read(uint8_t index) const
{
    if (index >= kNumChannels)
        return false;

    if (!isAvailable(index))
        return false;

    return _state[index].value;
}

void AuxDigital::write(uint8_t index, bool value)
{
    if (index >= kNumChannels)
        return;

    if (!isAvailable(index))
        return;

    if (_state[index].mode == AuxDigitalMode::Output)
    {
        _state[index].value = value;
        digitalWrite(_pins[index], value ? HIGH : LOW);
    }
}

AuxDigital::AuxDigitalMode AuxDigital::mode(uint8_t index) const
{
    if (index >= kNumChannels)
        return AuxDigitalMode::Disabled;

    if (!isAvailable(index))
        return AuxDigitalMode::Disabled;

    return _state[index].mode;
}

AuxDigital::AuxDigitalPull AuxDigital::pull(uint8_t index) const
{
    if (index >= kNumChannels)
        return AuxDigitalPull::None;

    if (!isAvailable(index))
        return AuxDigitalPull::None;

    return _state[index].pull;
}
