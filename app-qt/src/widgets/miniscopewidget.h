// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "scopeplotwidget.h"
#include "widgetbase.h"

#include <QPointer>

class MiniScopeWidget : public WidgetBase
{
    Q_OBJECT
    Q_PROPERTY(double ScaleFactor READ scaleFactor WRITE setScaleFactor NOTIFY scaleFactorChanged)
    Q_PROPERTY(int ScopeSignal READ scopeSignal WRITE setScopeSignal NOTIFY scopeSignalChanged)
    Q_PROPERTY(double TimeWindowSeconds READ timeWindowSeconds WRITE setTimeWindowSeconds NOTIFY
                   timeWindowSecondsChanged)
    Q_PROPERTY(bool AutoScale READ autoScale WRITE setAutoScale NOTIFY autoScaleChanged)

public:
    enum class ScopeSignalType {
        ForwardWatts = 0,
        ReflectedWatts = 1,
        Swr = 2,
        AuxValue = 3,
        Aux2Value = 4,
        Aux3Value = 5
    };
    Q_ENUM(ScopeSignalType)

    explicit MiniScopeWidget(QWidget *parent = nullptr);

    void onAppScaleFactorChanged(double scale) override { setScaleFactor(scale); }

    int scopeSignal() const { return static_cast<int>(m_signal); }
    void setScopeSignal(int sig);

    double timeWindowSeconds() const { return m_timeWindowSeconds; }
    void setTimeWindowSeconds(double seconds);

    bool autoScale() const { return m_autoScale; }
    void setAutoScale(bool en);

    void setTitle(const QString &title);
    void setUnits(const QString &units);
    void clear();

    void applyManualYRangeHint(double yMax);

protected:
    double scaleFactor() const { return m_scaleFactor; }
    void setScaleFactor(double scale);

public slots:
    void addSample(double y);

signals:
    void scaleFactorChanged(double);
    void scopeSignalChanged(int);
    void timeWindowSecondsChanged(double);
    void autoScaleChanged(bool);

private:
    void applyScaleToFonts();

    QPointer<ScopePlotWidget> m_plot;
    double m_scaleFactor = 1.0;
    ScopeSignalType m_signal = ScopeSignalType::ForwardWatts;
    double m_timeWindowSeconds = 2.5;
    bool m_autoScale = true;

    double m_basePointSize = 0.0;
};
