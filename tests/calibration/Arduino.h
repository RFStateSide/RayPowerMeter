// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using std::isfinite;

constexpr int INPUT = 0;
constexpr int ADC_11db = 0;
inline int adcCounts[2]{};
inline int analogRead(uint8_t pin) { return adcCounts[pin]; }
inline void pinMode(uint8_t, int) {}
inline void analogReadResolution(int) {}
inline void analogSetPinAttenuation(uint8_t, int) {}
inline void delayMicroseconds(uint16_t) {}
inline unsigned long millis() { return 100; }

#define F(text) text
class Stream
{
public:
    void println(const char *) {}
    template <typename... Args> void printf(const char *, Args...) {}
};
inline Stream Serial;

inline size_t strlcpy(char *destination, const char *source, size_t capacity)
{
    const size_t length = std::strlen(source);
    if (capacity)
    {
        const size_t copied = length < capacity ? length : capacity - 1;
        std::memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}
