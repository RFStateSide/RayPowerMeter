// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace raypm {

// --- AUX channel kind -------------------------------------------------------
enum class RayAuxType : quint8 { None = 0, Voltage = 1, Current = 2, Temperature = 3 };

// --- AUX digital I/O mode ----------------------------------------------------
enum class RayAuxDigitalMode : quint8 {
    Disabled = 0,
    Input = 1,
    Output = 2,
};

struct RayAuxDigitalStatus
{
    int index = -1;
    QString name;

    RayAuxDigitalMode mode = RayAuxDigitalMode::Disabled;
    QString modeName; // raw string from firmware ("disabled", "input", "output")

    bool value = false; // current logic level (as reported by firmware)
};

// --- Per-coupler RF sample --------------------------------------------------
struct RayCouplerSample
{
    int index = -1;
    QString name; // from status rf.couplers[].name (if present)

    double forwardVolts = 0.0;   // fwd_v
    double reflectedVolts = 0.0; // ref_v
    double forwardWatts = 0.0;   // fwd_w
    double reflectedWatts = 0.0; // ref_w
    double swr = 1.0;

    // Peaks (from status only; NOT in streaming telemetry)
    double peakForwardWatts = 0.0;   // peak_f
    double peakReflectedWatts = 0.0; // peak_r
};

// --- Per-AUX sample ---------------------------------------------------------
struct RayAuxSample
{
    int index = -1;
    RayAuxType type = RayAuxType::None;
    QString typeName;      // "voltage" / "current" / "temperature" / "none"
    double rawVolts = 0.0; // raw_volts (0..3.3V)
    double value = 0.0;    // engineering units (V, A, °C, ...)
};

// --- Protection/trip info ---------------------------------------------------
struct RayTripInfo
{
    bool tripped = false;
    quint8 type = 0;   // RayTripType enum value from firmware
    quint8 source = 0; // RayTripSource enum value
    double threshold = 0.0;
    double value = 0.0;
};

// --- Network status (HTTP only) ---------------------------------------------
struct RayNetStatus
{
    int state = 0; // NetManager::State
    QString ip;
    int rssiDbm = 0; // optional; 0 if unknown
    QString hostname;
};

// --- Top-level telemetry snapshot -------------------------------------------
struct RayTelemetry
{
    QDateTime timestamp;
    quint32 tsMs = 0; // For streaming telemetry (ts_ms), 0 for status

    // Device info
    QString deviceAppName;
    QString deviceAppVersion;

    // RF high-level state (from rf.*)
    quint8 rfMode = 0; // AppMode from firmware
    bool protectionOn = true;

    // RF frequency from RayPowerMeter prescaler ---
    double rfFrequencyHz = 0.0;
    bool rfFrequencyValid = false;

    RayTripInfo trip;

    // Multi-coupler + AUX data
    QVector<RayCouplerSample> couplers;
    QVector<RayAuxSample> auxChannels;
    QVector<RayAuxDigitalStatus> auxDigitalChannels;

    RayNetStatus net; // Populated only for HTTP /api/status

    // Optional: “current primary coupler” (from coupler config, not status)
    int activeCouplerIndex = -1;

    // ------------------------------------------------------------------------
    // Legacy flat fields (for backward compatibility with older code).
    // New code should prefer couplers[] / auxChannels[] above.
    // ------------------------------------------------------------------------
    int couplerIndex = -1;
    int auxIndex = -1;

    double forwardWatts = 0.0;
    double reflectedWatts = 0.0;
    double swrLegacy = 1.0; // kept separate to avoid confusion

    double voltage = 0.0;      // AUX voltage (legacy)
    double current = 0.0;      // AUX current (legacy)
    double temperatureC = 0.0; // temperature (legacy)

    double rawFwd = 0.0;
    double rawRef = 0.0;
};

} // namespace raypm

Q_DECLARE_METATYPE(raypm::RayTelemetry)
