// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QColor>
#include <QElapsedTimer>
#include "powerbarwidget.h"
#include "widgetbase.h"

class PowerBarVertWidget : public WidgetBase
{
    Q_OBJECT

public:
    explicit PowerBarVertWidget(QWidget *parent = nullptr);

    // Deconstructor
    ~PowerBarVertWidget() override;

    int layoutRowSpan() const override { return 2; } // 2 rows tall
    int layoutColSpan() const override { return 1; } // 1 column wide

    void onAppScaleFactorChanged(double scale) override;

    // Reuse the same meter mode as PowerBarWidget
    using MeterMode = PowerBarWidget::MeterMode;

    // --- Core values / limits ---
    void setPowerValue(double value);
    void setMaxPower(int max)
    {
        m_maxPower = max;
        update();
    }
    void setMinPower(int min)
    {
        m_minPower = min;
        update();
    }

    // --- Text / labels ---
    void setLabelText(const QString &text);
    void setLabelTextColor(const QColor &color);
    void setValueColor(const QColor &color);
    void setPowerValueUnits(const QString &units) { m_powerUnits = units; }
    void setPowerValuePercision(int p) { m_powerPrecision = p; }

    void setRightLabelText(const QString &text);
    void setRightValueText(const QString &text);
    void setRightValue(double value);
    void setRightValuePrecision(int p) { m_rightPrecision = p; }
    void setRightTextUnits(const QString &units) { m_rightUnits = units; }

    // --- Thresholds / scale ---
    void setThresholds(int yellow, int red);
    void setTickConfig(double majorStep, double minorStep, bool labels);
    void setShowTicks(bool show);
    void setShowTickLabels(bool labels);
    void setShowThresholdLabels(bool show);

    // --- Scaling / theme-ish helpers ---
    void setScaleFactor(qreal scale);
    double m_basePointSize = 0.0;
    QFont m_tickFont;
    void setPeakHoldProfileScaled(int liveHoldMs,
                                  double liveDecayFracPerSec,
                                  int peakHoldMs,
                                  double freezeDecayFracPerSec);
    void setOkWarnCritColors(const QColor &ok, const QColor &warn, const QColor &crit);

    // --- Meter mode control (matches PowerBarWidget) ---
    void setMeterMode(MeterMode m);
    MeterMode meterMode() const { return m_mode; }
    void clearPeak();

    // Cadence API – mostly no-op but kept for compatibility
    void setDisplayCadenceMs(int ms) { m_displayIntervalMs = qMax(10, ms); }
    void setRightValueCadenceMs(int ms) { m_rightIntervalMs = qMax(10, ms); }

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    void changeEvent(QEvent *event) override;

    // Double-click to reset peak (matches PowerBarWidget behavior)
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    // Scale / geometry
    qreal m_scaleFactor = 1.0;

    // Core values
    double m_powerValue = 0.0;
    double m_peakValue = 0.0;
    int m_minPower = 0;
    int m_maxPower = 1800;

    // Peak-hold profile
    int m_peakHoldLiveHoldMs = 2000;
    double m_peakHoldLiveDecayFracPerSec = 0.025;
    int m_peakHoldHoldMs = 2500;
    double m_peakHoldFreezeDecayFracPerSec = 0.012;

    // Peak hold/decay state (to match PowerBarWidget behavior)
    QTimer m_peakHoldTimer;
    bool m_peakHoldExpired = false;
    QElapsedTimer m_peakDecayClock;

    // Text + formatting
    QString m_labelText = tr("Forward Power");
    QColor m_labelTextColor;
    QColor m_valueTextColor;
    QString m_powerUnits = QStringLiteral(" W");
    int m_powerPrecision = 1;

    QString m_rightLabelText = tr("Ref (W):");
    QString m_rightUnits = QStringLiteral(" W");
    int m_rightPrecision = 1;
    double m_rightRaw = 0.0;
    double m_rightDisplayed = 0.0;
    QString m_rightValueText;

    // Thresholds & colors
    int m_yellowVal = 1200;
    int m_redVal = 1500;
    QColor m_greenColor;
    QColor m_yellowColor; // DarkGoldenRod style
    QColor m_redColor;

    const QColor kPeakTipColor = QColor::fromRgb(248, 0, 248); // purple tip

    // Ticks / labels
    double m_majorTickStep = 200.0;
    double m_minorTickStep = 100.0;
    bool m_showTicks = true;
    bool m_showTickLabels = true;
    bool m_showThresholdLabels = true;

    // Simple cadence (we keep it mostly for right value)
    int m_displayIntervalMs = 200;
    int m_rightIntervalMs = 200;
    QElapsedTimer m_displayTimer;
    QElapsedTimer m_rightTimer;

    // Meter mode
    MeterMode m_mode = MeterMode::Live;

    // Helpers
    void updateFontForScale();
    int valueToPixelY(double v, int barTop, int barBottom, double range) const;
    void updateMinimumHeight();

    // Sync label text color with the current theme/palette
    void updateLabelColorFromPalette();
};
