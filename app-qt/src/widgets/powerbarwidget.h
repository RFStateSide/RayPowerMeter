// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include "widgetbase.h"

class PowerBarWidget : public WidgetBase
{
    Q_OBJECT

public:
    explicit PowerBarWidget(QWidget *parent = nullptr);

    int layoutRowSpan() const override { return 1; } // 1 row tall
    int layoutColSpan() const override { return 2; } // 2 columns wide

    void onAppScaleFactorChanged(double scale) override { setScaleFactor(scale); }

    void setPowerValue(double value);
    void setMaxPower(int max)
    {
        maxPower = max;
        update();
    }
    void setMinPower(int min)
    {
        minPower = min;
        update();
    }
    void setLabelText(const QString &text);
    void setLabelTextColor(const QColor &color);
    void setThresholds(int yellow, int red);
    void setValueColor(const QColor &color);
    void setRightLabelText(const QString &text);
    void setRightValueText(const QString &text);
    void setRightValue(double value);
    void setPowerValuePercision(int p) { powerValuePercision = p; }
    void setRightValuePrecision(int p) { rightValuePrecision = p; }
    void setPowerValueUnits(const QString &units) { powerValueUnits = units; }
    void setRightTextUnits(const QString &units) { rightValueUnits = units; }
    void setPeakBarColor(const QColor &color) { peakBarColor = color; };

    void setDynamicTextDisplay(bool show) { showDynamicTextColor = show; }

    // ---- New: ticks + labels on the scale ----
    void setTickConfig(double majorStep, double minorStep, bool showLabels = true);
    void setShowTickLabels(bool show)
    {
        showTickLabels = show;
        update();
    }
    void setShowTicks(bool show)
    {
        showTicks = show;
        update();
    }
    void setShowThresholdLabels(bool show)
    {
        showTresholdLabels = show;
        update();
    }

    bool showTicksEnabled() const { return showTicks; }
    bool showTickLabelsEnabled() const { return showTickLabels; }

    // ---- New: peak marker style and behavior ----
    void setPeakStyle(int widthPx, qreal alpha); // e.g. width=3, alpha=0.9
    void setPeakHold(int holdMs, int decayMs, double decayStep = 10.0);
    // Configure both Live and Peak profiles at once
    void setPeakHoldProfile(int liveHoldMs,
                            double liveDecayStep,
                            int peakHoldMs_forPeak,
                            double peakDecayStep_forPeak)
    {
        peakHoldMs = std::max(50, liveHoldMs);
        peakDecayStep = std::max(0.1, liveDecayStep);
        peakHoldMsPeak = std::max(50, peakHoldMs_forPeak);
        peakDecayStepPeak = std::max(0.1, peakDecayStep_forPeak);
        // sync currently running timer to mode
        if (mode_ == MeterMode::Live)
            peakHoldTimer.setInterval(peakHoldMs);
        if (mode_ == MeterMode::Peak)
            peakHoldTimer.setInterval(peakHoldMsPeak);
    }

    // Configure Live/Peak decay as FRACTION of scale per second (scale-aware)
    // Example: liveFrac=0.07 => decays 7% of (max-min) per second after hold.
    void setPeakHoldProfileScaled(int liveHoldMs,
                                  double liveFracPerSec,
                                  int peakHoldMs_forPeak,
                                  double peakFracPerSec)
    {
        peakHoldMs = std::max(50, liveHoldMs);
        liveDecayFracPerSec = std::max(0.0, liveFracPerSec);
        peakHoldMsPeak = std::max(50, peakHoldMs_forPeak);
        peakDecayFracPerSec = std::max(0.0, peakFracPerSec);
        if (mode_ == MeterMode::Live)
            peakHoldTimer.setInterval(peakHoldMs);
        if (mode_ == MeterMode::Peak)
            peakHoldTimer.setInterval(peakHoldMsPeak);
    }

    // ---- New: UI cadence throttling (ms) ----
    void setDisplayCadenceMs(int ms) { displayUpdateIntervalMs = qMax(10, ms); }
    void setRightValueCadenceMs(int ms) { rightValueUpdateIntervalMs = qMax(10, ms); }

    // in PowerBarWidget.h (public)
    void setOkWarnCritColors(const QColor &ok, const QColor &warn, const QColor &crit);

    // ---- Meter mode control ----
    enum class MeterMode { Live, Peak, Freeze, None };
    void setMeterMode(MeterMode m);
    MeterMode meterMode() const { return mode_; }
    void clearPeak(); // reset peak marker/value (and return to Live if frozen)

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;

    // React to palette/theme changes
    void changeEvent(QEvent *event) override;
    void setScaleFactor(qreal scale);

    // Double-click to reset peak
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    double powerValue;
    double peakValue;
    QTimer peakHoldTimer;
    bool peakHoldExpired;
    double displayedValue = 0.0;
    QElapsedTimer displayUpdateTimer;
    QElapsedTimer rightValueUpdateTimer;
    int displayUpdateIntervalMs = 200;
    int rightValueUpdateIntervalMs = 200;
    double rightValueRaw = 0.0;
    double rightValueDisplayed = 0.0;

    int maxPower;
    int minPower = 0; // Minimum power value, default is 0

    QString labelText;
    QString rightLabelText;
    QString rightValueText;
    QString powerValueUnits;
    QString rightValueUnits;

    int yellowVal;
    int redVal;

    QColor greenColor;
    QColor yellowColor;
    QColor redColor;
    QColor valueTextColor = Qt::green;
    QColor labelTextColor = Qt::white;
    //QColor peakBarColor = QColor::fromRgb(0, 96, 96);
    QColor peakBarColor = QColor::fromRgb(248, 0, 248);

    bool showDynamicTextColor; // Flag to control dynamic text display
    int powerValuePercision = 0; // Number of decimal places for power value
    int rightValuePrecision = 1;

    qreal m_scaleFactor = 1.0;
    double m_basePointSize = 0.0;
    QFont m_tickFont;

    bool showPeakMarker = true;

    // ---- New: ticks + labels ----
    double majorTickStep = 0.0; // 0 disables ticks
    double minorTickStep = 0.0; // 0 disables minor ticks
    bool showTickLabels = true;
    bool showTicks = true;

    bool showTresholdLabels = true;

    // ---- New: peak styling + behavior ----
    int peakWidthPx = 3;
    qreal peakAlpha = 0.90;      // 0..1
    int peakHoldMs = 2000;       // was 1000; hold longer before decay (Live)
    int peakDecayMs = 800;       // used by external cadence decay, here used as step shaping
    double peakDecayStep = 10.0; // legacy (kept for backward compat if used elsewhere)
    // Separate, slower profile for Peak mode
    int peakHoldMsPeak = 2500;      // was 1500; hold longer (Peak)
    double peakDecayStepPeak = 4.0; // legacy; superseded by frac below

    // ---- Scale-aware decay (fraction of (max-min) per second) ----
    double liveDecayFracPerSec = 0.025; // was 0.07; slower decay for SSB UX
    double peakDecayFracPerSec = 0.012; // was 0.025; slower in Peak mode
    QElapsedTimer peakDecayClock;       // measures elapsed time for scale-aware ste

    MeterMode mode_ = MeterMode::Live;

    void updateMinimumHeight();

    int valueToPixelX(double v, int margin, int totalWidth, double range) const;

    // Sync label text color with the current theme/palette
    void updateLabelColorFromPalette();
};

