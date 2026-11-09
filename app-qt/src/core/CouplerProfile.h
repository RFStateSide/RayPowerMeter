// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QString>

namespace raypm {

struct CouplerProfile
{
    QString name;              // e.g. "RayCoupler-200W HF"
    double maxForwardWatts = 100.0;

    // Simple linear model for vpp → watts (for now)
    double scaleFwd   = 1.0;   // watts per volt (example)
    double scaleRef   = 1.0;

    // Future: polynomial coefficients, detector type, etc.
    // QVector<double> polyFwd;
    // QVector<double> polyRef;
};

} // namespace raypm
