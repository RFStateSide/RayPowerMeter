// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QPair>
#include <QVector>
#include <QWidget>
#include "common/Constants.h"

class SwrVsFreqPlotWidget : public QWidget
{
    Q_OBJECT
public:
    enum class UnitsMode { Vswr = 0, ReturnLossDb = 1 };
    enum class SmoothingMode { None = 0, Sma = 1, Ema = 2 };

    void setSmoothingMode(SmoothingMode mode);
    SmoothingMode smoothingMode() const { return m_smoothing; }

    void setSmaWindow(int window);
    int smaWindow() const { return m_smaWindow; }

    void setEmaAlpha(double alpha);
    double emaAlpha() const { return m_emaAlpha; }

    explicit SwrVsFreqPlotWidget(QWidget *parent = nullptr);

    void clearPoints();
    void addPoint(double freqHz, double yValue);
    int pointCount() const { return m_points.size(); }

    void setUnitsMode(UnitsMode m)
    {
        m_units = m;
        update();
    }
    UnitsMode unitsMode() const { return m_units; }

    void setTitle(const QString &t)
    {
        m_title = t;
        update();
    }
    QString title() const { return m_title; }

    void setFixedXRangeHz(double minHz, double maxHz);
    void clearFixedXRange();

    void setShowPoints(bool on)
    {
        m_showPoints = on;
        update();
    }
    bool showPoints() const { return m_showPoints; }

    // Measurements overlay (Min / Max / Avg)
    void setShowMeasurements(bool on)
    {
        if (m_showMeasurements == on)
            return;
        m_showMeasurements = on;
        update();
    }
    bool showMeasurements() const { return m_showMeasurements; }

    // Y-axis scaling
    void setAutoScaleY(bool on)
    {
        if (m_autoScaleY == on)
            return;
        m_autoScaleY = on;
        update();
    }
    bool autoScaleY() const { return m_autoScaleY; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QPair<double, double>> m_points; // (freqHz, y)
    UnitsMode m_units = UnitsMode::Vswr;
    QString m_title;

    bool m_useFixedX = false;
    double m_fixedMinXHz = 0.0;
    double m_fixedMaxXHz = 0.0;
    bool m_showPoints = false;
    bool m_showMeasurements = false;
    bool m_autoScaleY = true;

    SmoothingMode m_smoothing = SmoothingMode::Ema;
    int m_smaWindow = 5;
    double m_emaAlpha = 0.33;
};
