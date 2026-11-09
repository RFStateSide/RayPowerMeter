// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QElapsedTimer>
#include <QPointF>
#include <QVector>
#include <QWidget>

class ScopePlotWidget : public QWidget
{
    Q_OBJECT

public:
    enum ScopeType { Telemetry, Telemetry_Mini, Modulation, SWR };
    enum class SmoothingMode { None = 0, Sma = 1, Ema = 2 };

    explicit ScopePlotWidget(QWidget *parent = nullptr);

    // ScopeType
    void setScopeType(ScopeType type);
    ScopeType scopeType() const { return m_scopeType; }

    // Display
    void setTitle(const QString &title);
    void setUnits(const QString &units);

    // Measurements overlay (Peak / RMS / Min / Max)
    void setShowMeasurements(bool on);
    bool showMeasurements() const { return m_showMeasurements; }

    // Smoothing
    void setSmoothingMode(SmoothingMode mode);
    SmoothingMode smoothingMode() const { return m_smoothing; }

    void setSmaWindow(int window);
    int smaWindow() const { return m_smaWindow; }

    void setEmaAlpha(double alpha);
    double emaAlpha() const { return m_emaAlpha; }

    // Data
    void clear();

    // Replaces all samples at once (preferred for burst captures)
    void setSamples(const QVector<QPointF> &samples);

    // Adds a sample at "now" using this widget's monotonic timer.
    void addSample(double y);

    // Adds a sample at explicit time (seconds).
    void addSampleAt(double xSeconds, double y);

    // Window / scaling
    void setTimeWindowSeconds(double seconds); // e.g. 10.0 means show last 10 seconds
    double timeWindowSeconds() const { return m_timeWindowSec; }

    void setAutoScale(bool on);
    bool autoScale() const { return m_autoScale; }

    void setFixedYRange(double yMin, double yMax); // also enables fixed range
    void setUseFixedYRange(bool on);
    bool useFixedYRange() const { return m_useFixedRange; }

    void setMaxSamples(int maxSamples); // safety cap
    int maxSamples() const { return m_maxSamples; }

    void setClampMinZero(bool on);
    bool clampMinZero() const { return m_clampMinZero; }
    double currentYMax() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void ensureTimerStarted();
    void trimToWindow();     // trims samples outside time window
    void trimToMaxSamples(); // trims to maxSamples
    void recomputeStats();
    bool computeVisibleRange(double &yMin, double &yMax) const;

    static double niceStep(double span, int targetTicks);
    static double niceFloor(double value, double step);

    void rebuildSmoothedFromRaw();

private:
    ScopeType m_scopeType = Telemetry;

    QString m_title;
    QString m_units;

    QVector<QPointF> m_rawSamples; // x=seconds, y=raw value (unsmoothed)
    QVector<QPointF> m_samples;    // x=seconds, y=plotted value (smoothed)

    QElapsedTimer m_timer;
    bool m_timerStarted = false;

    double m_timeWindowSec = 10.0;

    bool m_autoScale = true;
    bool m_useFixedRange = false;
    double m_fixedYMin = 0.0;
    double m_fixedYMax = 1.0;

    int m_maxSamples = 5000;
    bool m_clampMinZero = false;

    // Measurements (cached; computed when samples change)
    bool m_showMeasurements = false;
    bool m_statsValid = false;
    double m_statMin = 0.0;
    double m_statMax = 0.0;
    double m_statRms = 0.0;
    double m_statPeak = 0.0;

    // Smoothing
    SmoothingMode m_smoothing = SmoothingMode::Ema;
    int m_smaWindow = 5;
    double m_emaAlpha = 0.33;
};
