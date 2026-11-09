// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QElapsedTimer>
#include <QSize>
#include <QTimer>
#include <QWidget>

#include "widgets/powerbarwidget.h" // for PowerBarWidget::MeterMode
#include "widgets/widgetbase.h"

class AnalogPowerMeterWidget : public WidgetBase
{
    Q_OBJECT

public:
    explicit AnalogPowerMeterWidget(QWidget *parent = nullptr);

    // Suggested footprint: 2x2 tile
    int layoutRowSpan() const override { return 2; }
    int layoutColSpan() const override { return 2; }

    // Keep the widget square as the layout stretches
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }
    QSize sizeHint() const override { return QSize(260, 260); }
    QSize minimumSizeHint() const override { return QSize(160, 160); }

    // Scale factor (same pattern as PowerBarWidget)
    void setScaleFactor(qreal scale);
    qreal scaleFactor() const { return m_scaleFactor; }
    void onAppScaleFactorChanged(double scale) override
    {
        setScaleFactor(static_cast<qreal>(scale));
    }

    // --- “PowerBar-like” public API (so LayoutManager/MainWindow can treat it similarly) ---
    void setPowerValue(double value); // Forward (W)
    void setRightValue(double value); // Reflected (W)

    void setMaxPower(int maxW);
    void setMinPower(int minW);

    void setLabelText(const QString &text);

    void setThresholds(int yellowW, int redW);

    void setTickConfig(double majorStep, double minorStep, bool showLabels = true);

    void setShowTicks(bool show);
    void setShowTickLabels(bool show);
    void setShowThresholdLabels(bool show);

    bool showTicksEnabled() const { return m_showTicks; }
    bool showTickLabelsEnabled() const { return m_showTickLabels; }

    // Meter mode support (same enum as PowerBarWidget)
    PowerBarWidget::MeterMode meterMode() const { return m_mode; }
    void setMeterMode(PowerBarWidget::MeterMode mode);

signals:
    void meterModeChanged(int mode);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // App Scale
    qreal m_scaleFactor = 1.0;
    qreal m_defaultFontSize = 0.0;

    // Values
    double m_fwdW = 0.0;
    double m_refW = 0.0;

    // Peak tracking (simple + stable)
    double m_peakFwdW = 0.0;
    double m_peakRefW = 0.0;

    QElapsedTimer m_peakTimer;
    QTimer m_decayTimer;

    int m_peakHoldMsLive = 500;
    int m_peakHoldMsPeak = 1200;

    double m_decayStepLive = 10.0; // W per tick
    double m_decayStepPeak = 10.0; // W per tick

    // Scale
    int m_minPower = 0;
    int m_maxPower = 1800;

    int m_yellowW = 1200;
    int m_redW = 1500;

    // Ticks
    double m_majorStep = 200.0;
    double m_minorStep = 100.0;

    bool m_showTicks = true;
    bool m_showTickLabels = true;
    bool m_showThresholdLabels = true;

    QString m_label = QStringLiteral("Forward / Ref / SWR");

    PowerBarWidget::MeterMode m_mode = PowerBarWidget::MeterMode::Live;

private:
    double displayFwdW() const;
    double displayRefW() const;

    static double clamp01(double x);

    // Simple SWR computation from fwd/ref watts
    static double computeSWR(double fwdW, double refW);

    // Drawing helpers
    void drawFace(QPainter &p, const QRectF &r) const;
    void drawNeedles(QPainter &p, const QRectF &r, double fwdW, double refW) const;

    QPointF polarPoint(const QPointF &center, double radius, double degrees) const;

    // Map value -> angle for left/right scales (MFJ-like: two arcs meeting at the top)
    double valueToAngleLeft(double w) const;  // Forward (left needle)
    double valueToAngleRight(double w) const; // Reflected (right needle)

    void updatePeaks(double fwdW, double refW);
    void startDecayTimerIfNeeded();
};
