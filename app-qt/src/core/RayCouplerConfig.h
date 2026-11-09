// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace raypm {

// ---- Calibration support (multi-point, table-based) ----

struct RayCalPoint
{
    double volts = 0.0; // ADC voltage
    double watts = 0.0; // True RF power at this voltage
};

struct RayCalTable
{
    QString mode;                // "linear" or "table"
    int count = 0;               // number of points (up to MAX_CAL_POINTS)
    QVector<RayCalPoint> points; // calibration table points

    // Optional legacy linear coefficients (may still be sent by firmware)
    double m = 1.0;
    double b = 0.0;

    bool isTable() const { return mode == "table" && count >= 2; }
    bool isLinear() const { return mode == "linear"; }
};

struct RayCouplerConfig
{
    int index = 0;
    QString name;

    double fullScaleW = 0.0;   // "full_scale_w"
    double fwdBarRangeW = 0.0; // "fwd_bar_range_w"
    double refBarRangeW = 0.0; // "ref_bar_range_w"

    double highFwdTripW = 0.0; // "high_fwd_trip_w"
    double highRefTripW = 0.0; // "high_ref_trip_w"
    double highSWRTrip = 0.0;  // "high_swr_trip"

    quint8 tripMask = 0; // "trip_mask"
    quint8 typeId = 0;   // "type_id"
    QString typeName;    // "type_name"

    RayCalTable fwdCal;
    RayCalTable refCal;
};

} // namespace raypm
