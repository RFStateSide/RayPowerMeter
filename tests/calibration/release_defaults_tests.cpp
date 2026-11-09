// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/ReleaseDefaults.h"

static_assert(!ReleaseDefaults::legacyOtaEnabled);
static_assert(!ReleaseDefaults::wifiEnabled(false));
static_assert(ReleaseDefaults::labNetworkEnabled == (EXPECTED_LAB_NETWORK == 1));
static_assert(ReleaseDefaults::wifiEnabled(true) == (EXPECTED_LAB_NETWORK == 1));

int main() {}
