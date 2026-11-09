// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QDateTime>

namespace raypm {

/**
 * @brief Device-level configuration (non-coupler specific)
 *
 * This struct represents settings that apply to the device as a whole,
 * such as display brightness and system time.
 */
struct RayDeviceConfig
{
    // Display brightness (percentage, clamped by UI / firmware)
    int displayBrightness = 50; // 0–100 (UI enforces min 25)

    // UTC time to apply to the device
    QDateTime utcTime;

    // Convenience helper
    bool hasValidTime() const { return utcTime.isValid(); }
};

} // namespace raypm
