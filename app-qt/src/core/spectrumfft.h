// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QPointF>
#include <QVector>

namespace raypm {

class SpectrumFft
{
public:
    enum class Window { Hann, Rectangular };

    struct Params
    {
        double sampleRateHz = 48000.0;
        double maxHz = 20000.0;
        Window window = Window::Hann;
        bool dbScale = true; // true = dBFS-like, false = linear 0..1
    };

    // Compute single-sided spectrum (0 .. maxHz).
    // - Input samples are assumed to be 12-bit ADC counts in [0, 4095].
    // - We subtract the mean to remove DC.
    // - We apply the requested window.
    // - We normalize magnitudes so the strongest bin in this capture is 0 dB
    //   (or 1.0 in linear mode). This makes HTTP and COMM visually comparable.
    static QVector<QPointF> compute(const QVector<qint16> &samples, const Params &params);
};

} // namespace raypm
