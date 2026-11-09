// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QPair>
#include <QStringList>
#include <QVector>
#include <QWidget>

class AmplifierAnalyzerPlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AmplifierAnalyzerPlotWidget(QWidget *parent = nullptr);

    void clearPoints();
    void addPoint(double x, double y);
    int pointCount() const { return m_points.size(); }

    // Extra lines appended under the Min/Max/Avg/N box
    void setExtraMeasurements(const QStringList &lines)
    {
        m_extraMeasurements = lines;
        update();
    }
    void clearExtraMeasurements()
    {
        m_extraMeasurements.clear();
        update();
    }

    void setTitle(const QString &t)
    {
        m_title = t;
        update();
    }
    void setXLabel(const QString &t)
    {
        m_xLabel = t;
        update();
    }
    void setYLabel(const QString &t)
    {
        m_yLabel = t;
        update();
    }

    void setShowPoints(bool on)
    {
        m_showPoints = on;
        update();
    }
    void setShowMeasurements(bool on)
    {
        m_showMeasurements = on;
        update();
    }

    void setAutoScaleY(bool on)
    {
        m_autoScaleY = on;
        update();
    }
    bool autoScaleY() const { return m_autoScaleY; }

    void setFixedXRange(double minX, double maxX);
    void clearFixedXRange();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QPair<double, double>> m_points; // (x,y)

    QString m_title = "Amplifier Analyzer";
    QString m_xLabel = "X";
    QString m_yLabel = "Y";

    bool m_showPoints = false;
    bool m_showMeasurements = true;
    bool m_autoScaleY = true;

    bool m_useFixedX = false;
    double m_fixedMinX = 0.0;
    double m_fixedMaxX = 0.0;

    QStringList m_extraMeasurements;
};
