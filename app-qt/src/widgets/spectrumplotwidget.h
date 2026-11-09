// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QPointF>
#include <QVector>
#include <QWidget>

class SpectrumPlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SpectrumPlotWidget(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setDbScale(bool on);
    bool dbScale() const { return m_dbScale; }

    // If > 0, forces x-range [0..maxHz]. If 0, uses data range.
    void setMaxFrequencyHz(double maxHz);
    double maxFrequencyHz() const { return m_maxHz; }

    // Replace full spectrum (x = Hz, y = magnitude (linear or dB))
    void setSpectrum(const QVector<QPointF> &points);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static double niceStep(double span, int targetTicks);
    static QString formatFreq(double hz);

    QVector<QPointF> m_pts;
    QString m_title = QStringLiteral("Spectrum");
    bool m_dbScale = true;
    double m_maxHz = 20000.0;

    // Cached peak info
    double m_peakHz = 0.0;
    double m_peakY = 0.0;
};
