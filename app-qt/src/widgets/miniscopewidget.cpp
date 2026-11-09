// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "miniscopewidget.h"
#include "common//Constants.h"

#include <QVBoxLayout>
#include <QtMath>

MiniScopeWidget::MiniScopeWidget(QWidget *parent)
    : WidgetBase(parent)
{
    setWidgetName(QStringLiteral("MiniScope"));

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(24, 24);

    m_plot = new ScopePlotWidget(this);
    m_plot->setTitle(QStringLiteral("Mini Scope"));
    m_plot->setScopeType(ScopePlotWidget::ScopeType::Telemetry_Mini);
    m_plot->setUnits(QString());
    m_plot->setTimeWindowSeconds(m_timeWindowSeconds);
    m_plot->setMaxSamples(250);
    m_plot->setAutoScale(m_autoScale);
    m_plot->setUseFixedYRange(false);
    m_plot->setClampMinZero(true);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_plot);

    // Don’t “lock” a base font here, because the app might already be scaled.
    // We’ll compute an unscaled base the first time setScaleFactor() is called.
    m_basePointSize = 0.0;
}

void MiniScopeWidget::setScaleFactor(double scale)
{
    if (scale <= 0.0)
        scale = 1.0;

    if (qFuzzyCompare(m_scaleFactor, scale))
        return;

    // Robust base-capture:
    // If we were created after app scaling was already applied, the current font()
    // is already scaled. Recover the unscaled base by dividing by the incoming scale.
    if (m_basePointSize <= 0.0) {
        const double curPt = font().pointSizeF();
        if (curPt > 0.0)
            m_basePointSize = curPt / scale;
        else
            m_basePointSize = 10.0; // fallback
    }

    m_scaleFactor = scale;
    applyScaleToFonts();
    emit scaleFactorChanged(m_scaleFactor);
}

void MiniScopeWidget::applyScaleToFonts()
{
    const double targetPt = std::max<double>(MIN_UI_FONT_PT, m_basePointSize * m_scaleFactor);

    QFont f = font();
    if (f.pointSizeF() > 0.0) {
        f.setPointSizeF(targetPt);
    } else if (f.pixelSize() > 0) {
        // If your app ever runs pixel fonts, keep behavior consistent
        f.setPixelSize(
            std::max<int>(MIN_UI_FONT_PX, int(std::lround(f.pixelSize() * m_scaleFactor))));
    }

    setFont(f);

    if (m_plot)
        m_plot->setFont(f);

    updateGeometry();
    update();
}

void MiniScopeWidget::setScopeSignal(int sig)
{
    const int clamped = qBound(0, sig, 5);
    const auto newSig = static_cast<ScopeSignalType>(clamped);

    if (m_signal == newSig)
        return;

    m_signal = newSig;
    emit scopeSignalChanged(static_cast<int>(m_signal));
}

void MiniScopeWidget::setTimeWindowSeconds(double seconds)
{
    if (seconds < 0.1)
        seconds = 0.1;
    if (qFuzzyCompare(m_timeWindowSeconds, seconds))
        return;

    m_timeWindowSeconds = seconds;
    if (m_plot)
        m_plot->setTimeWindowSeconds(m_timeWindowSeconds);

    emit timeWindowSecondsChanged(m_timeWindowSeconds);
}

void MiniScopeWidget::setAutoScale(bool en)
{
    if (m_autoScale == en)
        return;

    m_autoScale = en;
    if (m_plot)
        m_plot->setAutoScale(m_autoScale);

    emit autoScaleChanged(m_autoScale);
}

void MiniScopeWidget::setTitle(const QString &title)
{
    if (m_plot)
        m_plot->setTitle(title);
}

void MiniScopeWidget::setUnits(const QString &units)
{
    if (m_plot)
        m_plot->setUnits(units);
}

void MiniScopeWidget::clear()
{
    if (m_plot)
        m_plot->clear();
}

void MiniScopeWidget::applyManualYRangeHint(double yMax)
{
    if (!m_plot)
        return;

    if (yMax <= 0.0)
        return;

    // Only make sense when auto-scale is OFF. Otherwise auto-scale should control the range.
    if (!m_autoScale) {
        // Aux scopes are always 0-based.
        m_plot->setClampMinZero(true);
        m_plot->setFixedYRange(0.0, yMax);
    }
}

void MiniScopeWidget::addSample(double y)
{
    if (m_plot)
        m_plot->addSample(y);
}
