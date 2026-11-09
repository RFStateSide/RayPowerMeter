// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once
#include <QString>

namespace raypm {

class DefaultLayoutProvider
{
public:
    static QString defaultLayoutJson(); // loads from :/layout/default_layout.json
};

} // namespace raypm
