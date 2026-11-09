// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

// /src/app/core/AppState.h
#pragma once
#include <Arduino.h>

enum class AppMode : uint8_t {
    Idle = 0,      // no TX, just measuring
    TxActive,      // TX key down, protection enforced
    FaultLatched   // tripped; require manual clear
};
