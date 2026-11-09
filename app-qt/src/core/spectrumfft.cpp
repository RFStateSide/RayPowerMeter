// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "spectrumfft.h"

#include <QtMath>
#include <complex>

namespace raypm {

static inline double hannWindow(int n, int N)
{
    if (N <= 1)
        return 1.0;
    return 0.5 * (1.0 - qCos(2.0 * M_PI * double(n) / double(N - 1)));
}

QVector<QPointF> SpectrumFft::compute(const QVector<qint16> &samples, const Params &params)
{
    const int N = samples.size();
    if (N < 2 || params.sampleRateHz <= 0.0)
        return {};

    // ---- 1) Center around zero (remove DC) ----
    double mean = 0.0;
    for (qint16 v : samples)
        mean += double(v);
    mean /= double(N);

    // ---- 2) Build window and compute its energy (for amplitude normalization) ----
    QVector<double> win(N);
    double winEnergy = 0.0;

    for (int n = 0; n < N; ++n) {
        double w = 1.0;
        if (params.window == Window::Hann)
            w = hannWindow(n, N);
        win[n] = w;
        winEnergy += w * w;
    }

    // This factor keeps the amplitude roughly invariant when changing window.
    const double winScale = (winEnergy > 0.0) ? (2.0 / winEnergy) : 1.0;

    // ---- 3) Naive DFT (OK for N ≤ 4096) ----
    const int halfN = N / 2;
    const double fs = params.sampleRateHz;
    const double df = fs / double(N);
    const double nyquist = fs * 0.5;
    const double limitHz = qMin(params.maxHz, nyquist);
    const int maxBin = qMin(halfN, int(qFloor(limitHz / df)));

    QVector<double> mags(maxBin + 1);
    double maxMag = 0.0;

    for (int k = 0; k <= maxBin; ++k) {
        std::complex<double> acc(0.0, 0.0);

        for (int n = 0; n < N; ++n) {
            const double x = (double(samples[n]) - mean) * win[n];
            const double ang = -2.0 * M_PI * double(k) * double(n) / double(N);
            acc += std::complex<double>(qCos(ang), qSin(ang)) * x;
        }

        // Normalize by N and window energy.
        const double re = acc.real() * winScale / double(N);
        const double im = acc.imag() * winScale / double(N);
        const double mag = qSqrt(re * re + im * im);

        mags[k] = mag;
        if (mag > maxMag)
            maxMag = mag;
    }

    if (maxMag <= 0.0)
        maxMag = 1e-12;

    // ---- 4) Build output points (frequency, dB or linear) ----
    QVector<QPointF> out;
    out.reserve(maxBin + 1);

    for (int k = 0; k <= maxBin; ++k) {
        const double f = double(k) * df;
        const double mag = mags[k];

        double y;
        if (params.dbScale) {
            // 0 dB at the strongest bin, negative for everything else.
            const double ratio = mag / maxMag;
            const double safe = (ratio > 1e-12) ? ratio : 1e-12;
            y = 20.0 * qLn(safe) / qLn(10.0); // log10
        } else {
            // Linear 0..1, normalized to the strongest bin.
            y = mag / maxMag;
        }

        out.append(QPointF(f, y));
    }

    return out;
}

} // namespace raypm
