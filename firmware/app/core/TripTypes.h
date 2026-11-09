// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once
#include <stdint.h>

// What tripped?
enum class RayTripType : uint8_t
{
    None = 0,
    HighForwardPower,
    HighReflectedPower,
    HighSWR,
    ExternalInterrupt1,
    ExternalInterrupt2
};

// Where did it come from?
enum class RayTripSource : uint8_t
{
    None = 0,
    Coupler1,
    Coupler2,
    Coupler3,
    Coupler4,
    Interrupt1,
    Interrupt2
};
