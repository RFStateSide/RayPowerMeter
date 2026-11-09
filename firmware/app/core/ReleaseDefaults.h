// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#ifndef RAYPM_ENABLE_LAB_NETWORK
#define RAYPM_ENABLE_LAB_NETWORK 0
#endif

#ifndef RAYPM_ENABLE_LEGACY_OTA
#define RAYPM_ENABLE_LEGACY_OTA 0
#endif

namespace ReleaseDefaults
{
constexpr bool labNetworkEnabled = RAYPM_ENABLE_LAB_NETWORK == 1;
constexpr bool legacyOtaEnabled = RAYPM_ENABLE_LEGACY_OTA == 1;
constexpr char otaDisabledMessage[] = "Firmware OTA is disabled in this build";

constexpr bool wifiEnabled(bool savedEnabled)
{
    return labNetworkEnabled && savedEnabled;
}
}
