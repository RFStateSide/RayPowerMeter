// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <array>
#include <cstring>

class TestEEPROM
{
public:
    bool failCommit = false;
    bool begin(size_t size) { return size <= staged.size(); }

    template <typename T> void get(int address, T &value)
    {
        std::memcpy(&value, staged.data() + address, sizeof(value));
    }

    template <typename T> void put(int address, const T &value)
    {
        std::memcpy(staged.data() + address, &value, sizeof(value));
    }

    bool commit()
    {
        if (failCommit)
            return false;
        saved = staged;
        return true;
    }

    void restart() { staged = saved; }

private:
    std::array<unsigned char, 4096> staged{};
    std::array<unsigned char, 4096> saved{};
};
inline TestEEPROM EEPROM;
