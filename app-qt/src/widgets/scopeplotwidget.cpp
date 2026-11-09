// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "scopeplotwidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <vector>

// Helpers
static double pow10floor(double x)
{
    if (x <= 0.0)
        return 1.0;
    return std::pow(10.0, std::floor(std::log10(x)));
}

double ScopePlotWidget::niceStep(double span, int targetTicks)
{
    if (span <= 0.0)
        return 1.0;
    if (targetTicks < 2)
        targetTicks = 2;

    const double raw = span / double(targetTicks - 1);
    const double base = pow10floor(raw);
    const double frac = raw / base;

    double niceFrac = 1.0;
    if (frac <= 1.0)
        niceFrac = 1.0;
    else if (frac <= 2.0)
        niceFrac = 2.0;
    else if (frac <= 5.0)
        niceFrac = 5.0;
    else
        niceFrac = 10.0;

    return niceFrac * base;
}

double ScopePlotWidget::niceFloor(double value, double step)
{
    if (step <= 0.0)
        return value;
    return std::floor(value / step) * step;
}

ScopePlotWidget::ScopePlotWidget(QWidget *parent)
    : QWidget(parent)
{
    m_scopeType = Telemetry; // default scope type

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(240, 140);

    // Default styling from palette
    setAutoFillBackground(true);
}

void ScopePlotWidget::setScopeType(ScopeType type)
{
    m_scopeType = type;
    update();
}

void ScopePlotWidget::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void ScopePlotWidget::setUnits(const QString &units)
{
    m_units = units;
    update();
}

void ScopePlotWidget::setShowMeasurements(bool on)
{
    if (m_showMeasurements == on)
        return;

    m_showMeasurements = on;

    // Compute immediately so first paint after enabling has values
    if (m_showMeasurements)
        recomputeStats();
    else
        m_statsValid = false;

    update();
}

void ScopePlotWidget::setSamples(const QVector<QPointF> &samples)
{
    m_rawSamples = samples;
    trimToWindow();
    trimToMaxSamples();
    rebuildSmoothedFromRaw();
    recomputeStats();
    update();
}

void ScopePlotWidget::recomputeStats()
{
    // If overlay is off, don't spend time computing
    if (!m_showMeasurements) {
        m_statsValid = false;
        return;
    }

    if (m_samples.isEmpty()) {
        m_statsValid = false;
        m_statMin = m_statMax = m_statRms = m_statPeak = 0.0;
        return;
    }

    double vMin = m_samples[0].y();
    double vMax = m_samples[0].y();
    double sumSq = 0.0;

    for (const auto &pt : m_samples) {
        const double y = pt.y();
        if (y < vMin)
            vMin = y;
        if (y > vMax)
            vMax = y;
        sumSq += (y * y);
    }

    m_statMin = vMin;
    m_statMax = vMax;
    m_statPeak = vMax;
    m_statRms = std::sqrt(sumSq / double(m_samples.size()));
    m_statsValid = true;
}

void ScopePlotWidget::setTimeWindowSeconds(double seconds)
{
    m_timeWindowSec = std::max(0.25, seconds);
    trimToWindow();
    rebuildSmoothedFromRaw();
    recomputeStats();
    update();
}

void ScopePlotWidget::setAutoScale(bool on)
{
    if (m_autoScale == on)
        return;

    m_autoScale = on;

    if (m_autoScale) {
        // True auto-scale: let paintEvent compute the dynamic range.
        m_useFixedRange = false;
    } else {
        // Auto-scale OFF: freeze a fixed Y range.
        double yMin = 0.0;
        double yMax = 1.0;

        if (!computeVisibleRange(yMin, yMax)) {
            // No data, nothing useful to freeze.
            m_useFixedRange = false;
        } else {
            // For telemetry scopes that clamp to zero (Forward, Aux V/I, mini scopes),
            // freeze a friendly 0-based range whose top is at a "nice" major tick.
            if ((m_scopeType == ScopeType::Telemetry || m_scopeType == ScopeType::Telemetry_Mini)
                && m_clampMinZero) {
                // Always start at 0 for these.
                yMin = 0.0;

                // Use the same tick logic as paintEvent.
                const int targetMajorTicks = (m_scopeType == ScopeType::Telemetry_Mini) ? 4 : 6;

                const double span = std::max(1e-9, yMax - yMin);
                const double step = niceStep(span, targetMajorTicks);

                double top = (step > 0.0) ? std::ceil(yMax / step) * step : yMax;

                // Safety: don't allow a degenerate range.
                if (top <= 0.0)
                    top = 1.0;

                setFixedYRange(yMin, top);
            } else {
                // All other scopes: just freeze the visible range as-is.
                setFixedYRange(yMin, yMax);
            }
        }

        qDebug() << "Auto-scale OFF: fixed Y range set to" << m_fixedYMin << "to" << m_fixedYMax;
    }

    update();
}

void ScopePlotWidget::setFixedYRange(double yMin, double yMax)
{
    if (yMax <= yMin) {
        yMax = yMin + 1.0;
    }
    m_fixedYMin = yMin;
    m_fixedYMax = yMax;
    m_useFixedRange = true;
    update();
}

void ScopePlotWidget::setUseFixedYRange(bool on)
{
    m_useFixedRange = on;
    update();
}

void ScopePlotWidget::setMaxSamples(int maxSamples)
{
    m_maxSamples = std::max(100, maxSamples);
    trimToMaxSamples();
    rebuildSmoothedFromRaw();
    recomputeStats();
    update();
}

void ScopePlotWidget::setClampMinZero(bool on)
{
    m_clampMinZero = on;
    update();
}

double ScopePlotWidget::currentYMax() const
{
    double yMin = 0.0;
    double yMax = 0.0;

    if (!computeVisibleRange(yMin, yMax))
        return 1.0; // safe fallback

    // Guard against degenerate ranges
    if (yMax <= 0.0)
        return 1.0;

    // Round UP to nearest whole number
    return std::ceil(yMax);
}

void ScopePlotWidget::clear()
{
    m_rawSamples.clear();
    m_samples.clear();
    m_statsValid = false;
    update();
}

void ScopePlotWidget::ensureTimerStarted()
{
    if (!m_timerStarted) {
        m_timer.start();
        m_timerStarted = true;
    }
}

void ScopePlotWidget::addSample(double y)
{
    ensureTimerStarted();
    const double x = m_timer.elapsed() / 1000.0;
    addSampleAt(x, y);
}

void ScopePlotWidget::addSampleAt(double xSeconds, double y)
{
    m_rawSamples.push_back(QPointF(xSeconds, y));
    trimToWindow();
    trimToMaxSamples();
    rebuildSmoothedFromRaw();
    recomputeStats();

    // If auto-scale is OFF but we couldn't lock a range at load time (no samples yet),
    // lock it now using the first real data we see.
    if (!m_autoScale && !m_useFixedRange) {
        double yMin = 0.0, yMax = 1.0;
        if (computeVisibleRange(yMin, yMax)) {
            if (m_scopeType == ScopeType::Telemetry_Mini && m_clampMinZero) {
                const double niceMax = std::ceil(yMax);
                setFixedYRange(0.0, (niceMax > 0.0 ? niceMax : 1.0));
            } else {
                setFixedYRange(yMin, yMax);
            }
        }
    }

    update();
}

void ScopePlotWidget::trimToWindow()
{
    if (m_rawSamples.isEmpty())
        return;

    const double newestX = m_rawSamples.last().x();
    const double cutoff = newestX - m_timeWindowSec;

    int firstKeep = 0;
    while (firstKeep < m_rawSamples.size() && m_rawSamples[firstKeep].x() < cutoff)
        ++firstKeep;

    if (firstKeep > 0)
        m_rawSamples.erase(m_rawSamples.begin(), m_rawSamples.begin() + firstKeep);
}

void ScopePlotWidget::trimToMaxSamples()
{
    if (m_maxSamples <= 0)
        return;

    if (m_rawSamples.size() <= m_maxSamples)
        return;

    const int drop = m_rawSamples.size() - m_maxSamples;
    m_rawSamples.erase(m_rawSamples.begin(), m_rawSamples.begin() + drop);
}

bool ScopePlotWidget::computeVisibleRange(double &yMin, double &yMax) const
{
    if (m_samples.isEmpty())
        return false;

    yMin = m_samples.first().y();
    yMax = m_samples.first().y();

    for (const auto &p : m_samples) {
        yMin = std::min(yMin, p.y());
        yMax = std::max(yMax, p.y());
    }

    // Clamp the visible minimum to zero (telemetry like power/voltage/current)
    if (m_clampMinZero) {
        if (yMin < 0.0)
            yMin = 0.0;
        if (yMax < yMin)
            yMax = yMin; // safety
    }

    if (qFuzzyCompare(yMin, yMax)) {
        // Give a tiny range so we can draw a line
        yMax = yMin + 1.0;
    }

    return true;
}

void ScopePlotWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();
    const bool isMini = (m_scopeType == ScopeType::Telemetry_Mini);

    // Layout constants
    int leftPad = 46; // will be recomputed after we know y-range / tick labels
    const int rightPad = 10;
    const int topPad = 22;
    const int bottomPad = 28; // a little extra for X tick labels

    // Background
    p.fillRect(r, palette().window());

    // Title + latest value
    p.setPen(palette().text().color());
    const QString title = m_title.isEmpty() ? QStringLiteral("Scope") : m_title;

    QString latestText;
    if (!m_samples.isEmpty()) {
        const double y = m_samples.last().y();
        if (!m_units.isEmpty())
            latestText = QStringLiteral("%1 %2").arg(y, 0, 'f', 2).arg(m_units);
        else
            latestText = QString::number(y, 'f', 2);
    } else {
        latestText = QStringLiteral("—");
    }

    p.drawText(r.adjusted(8, 2, -8, -2), Qt::AlignLeft | Qt::AlignTop, title);
    p.drawText(r.adjusted(8, 2, -8, -2), Qt::AlignRight | Qt::AlignTop, latestText);

    // If no data, stop here
    if (m_samples.size() < 2)
        return;

    // ---------------------------------------------------------------------
    // 1) Determine Y and X ranges
    // ---------------------------------------------------------------------
    double yMin = 0.0, yMax = 1.0;
    if (m_useFixedRange) {
        yMin = m_fixedYMin;
        yMax = m_fixedYMax;
        if (yMax <= yMin)
            yMax = yMin + 1.0;
    } else {
        if (!computeVisibleRange(yMin, yMax))
            return;

        if (m_autoScale) {
            const double span = (yMax - yMin);
            const double pad = span * 0.10;
            yMin -= pad;
            yMax += pad;

            if (m_clampMinZero && yMin < 0.0)
                yMin = 0.0;

            if (qFuzzyCompare(yMin, yMax))
                yMax = yMin + 1.0;
        }
    }

    // X range uses last timeWindowSec
    const double xMax = m_samples.last().x();
    const double xMin = xMax - m_timeWindowSec;

    // ---------------------------------------------------------------------
    // 2) Tick helpers and major tick generation
    // ---------------------------------------------------------------------
    auto pow10floor = [](double x) -> double {
        if (x <= 0.0)
            return 1.0;
        return std::pow(10.0, std::floor(std::log10(x)));
    };

    auto niceStep = [&](double span, int targetTicks) -> double {
        if (span <= 0.0)
            return 1.0;
        if (targetTicks < 2)
            targetTicks = 2;

        const double raw = span / double(targetTicks - 1);
        const double base = pow10floor(raw);
        const double frac = raw / base;

        double niceFrac = 1.0;
        if (frac <= 1.0)
            niceFrac = 1.0;
        else if (frac <= 2.0)
            niceFrac = 2.0;
        else if (frac <= 5.0)
            niceFrac = 5.0;
        else
            niceFrac = 10.0;

        return niceFrac * base;
    };

    auto niceFloor = [](double value, double step) -> double {
        if (step <= 0.0)
            return value;
        return std::floor(value / step) * step;
    };

    const int targetMajorTicks = isMini ? 4 : 6; // fewer labels in mini scope
    const int minorDiv = isMini ? 1 : 5;         // 1 => effectively disables minors
    const int tickLenMajor = isMini ? 4 : 6;
    const int tickLenMinor = isMini ? 0 : 3;

    const double ySpan = std::max(1e-9, (yMax - yMin));
    double yMajorStep = 1.0;
    double yFirst = yMin;

    QVector<double> yTicks; // explicit Y majors so yMax is guaranteed present

    // Mini telemetry scope with fixed 0-based engineering range: force 4 ticks.
    if (isMini && m_useFixedRange && m_clampMinZero) {
        // e.g. 0..60 => 0,20,40,60
        yFirst = yMin;            // typically 0
        yMajorStep = ySpan / 3.0; // span/3 => 4 ticks

        yTicks << yMin;
        yTicks << yMin + ySpan / 3.0;
        yTicks << yMin + 2.0 * ySpan / 3.0;
        yTicks << yMax; // top tick exactly at yMax
    } else {
        // All other scopes: normal "nice" ticks.
        yMajorStep = niceStep(ySpan, targetMajorTicks);
        yFirst = niceFloor(yMin, yMajorStep);

        for (double yy = yFirst; yy <= yMax + 0.5 * yMajorStep; yy += yMajorStep)
            yTicks << yy;
    }

    const double xSpanSec = std::max(1e-9, (xMax - xMin));
    const double xSpanMs = xSpanSec * 1000.0;
    const double xMajorStepMs = niceStep(std::max(1.0, xSpanMs), targetMajorTicks);
    const double xFirstMs = niceFloor(xMin * 1000.0, xMajorStepMs);

    QFontMetrics fm(font());

    // Determine decimals for Y labels based on major step size. We want to show enough decimals to distinguish adjacent ticks, but not more.
    auto decimalsForStep = [](double step) -> int {
        if (step <= 0.0)
            return 0;

        // Choose decimals from step size:
        //  step=10   -> 0
        //  step=1    -> 0
        //  step=0.5  -> 1
        //  step=0.1  -> 1
        //  step=0.05 -> 2
        const double d = -std::log10(step);
        int dec = int(std::ceil(d));
        dec = std::clamp(dec, 0, 3);

        // If step is an integer (or very close), 0 decimals is fine.
        if (step >= 1.0 && std::fabs(step - std::round(step)) < 1e-9)
            dec = 0;

        return dec;
    };

    int yDecimals = decimalsForStep(yMajorStep);

    // If we’re zoomed in (small span), force at least 1 decimal so labels don’t collapse.
    if (m_autoScale && ySpan < 5.0)
        yDecimals = std::max(yDecimals, 1);

    auto formatY = [&](double v) -> QString { return QString::number(v, 'f', yDecimals); };

    // Dynamic leftPad based on *widest* Y tick label
    QString widestY = formatY(yMin);
    for (double yy : yTicks) {
        const QString t = formatY(yy);
        if (fm.horizontalAdvance(t) > fm.horizontalAdvance(widestY))
            widestY = t;
    }
    leftPad = qBound(24, fm.horizontalAdvance(widestY) + 18, 110);

    const QRect plotRect = r.adjusted(leftPad, topPad, -rightPad, -bottomPad);

    // ---------------------------------------------------------------------
    // 3) Mapping functions
    // ---------------------------------------------------------------------
    p.fillRect(plotRect, palette().base());

    auto mapX = [&](double x) -> double {
        const double denom = (xMax - xMin);
        const double t = (denom <= 0.0) ? 1.0 : (x - xMin) / denom;
        return plotRect.left() + t * plotRect.width();
    };
    auto mapY = [&](double y) -> double {
        const double denom = (yMax - yMin);
        const double t = (denom <= 0.0) ? 0.0 : (y - yMin) / denom;
        return plotRect.bottom() - t * plotRect.height();
    };

    // ---------------------------------------------------------------------
    // 4) Grid and axes
    // ---------------------------------------------------------------------
    QPen gridPen(palette().mid().color());
    gridPen.setWidth(1);
    gridPen.setStyle(Qt::DotLine);
    gridPen.setColor(isMini ? gridPen.color().lighter(130) : gridPen.color());

    QPen axisPen(palette().text().color());
    axisPen.setWidth(1);
    axisPen.setStyle(Qt::SolidLine);

    const bool drawMinor = !isMini; // mini scopes: no minor grid/ticks
    const double yMinorStep = drawMinor ? (yMajorStep / double(minorDiv)) : 0.0;
    const double xMinorStepMs = drawMinor ? (xMajorStepMs / double(minorDiv)) : 0.0;

    p.setPen(gridPen);

    // Minor Y grid
    if (yMinorStep > 0.0) {
        for (double yy = yFirst; yy <= yMax + 0.5 * yMajorStep; yy += yMinorStep) {
            const int y = int(std::lround(mapY(yy)));
            if (y < plotRect.top() || y > plotRect.bottom())
                continue;
            p.drawLine(plotRect.left(), y, plotRect.right(), y);
        }
    }

    // Minor X grid
    if (xMinorStepMs > 0.0) {
        for (double xm = xFirstMs; xm <= (xMax * 1000.0) + 0.5 * xMajorStepMs; xm += xMinorStepMs) {
            const double xs = xm / 1000.0;
            const int x = int(std::lround(mapX(xs)));
            if (x < plotRect.left() || x > plotRect.right())
                continue;
            p.drawLine(x, plotRect.top(), x, plotRect.bottom());
        }
    }

    // Axes outline on top
    p.setPen(axisPen);
    p.drawRect(plotRect);

    // ---------------------------------------------------------------------
    // 5) Polyline (data)
    // ---------------------------------------------------------------------
    QPolygonF poly;
    poly.reserve(m_samples.size());
    for (const auto &pt : m_samples) {
        if (pt.x() < xMin)
            continue;
        poly.append(QPointF(mapX(pt.x()), mapY(pt.y())));
    }

    QPen plotPen(palette().highlight().color());
    plotPen.setWidth(2);
    plotPen.setStyle(Qt::SolidLine);
    p.setPen(plotPen);

    p.save();
    p.setClipRect(plotRect.adjusted(1, 1, -1, -1)); // keep inside axes border
    p.drawPolyline(poly);
    p.restore();

    // ---------------------------------------------------------------------
    // 6) Major ticks + labels
    // ---------------------------------------------------------------------
    p.setPen(palette().text().color());

    // Y ticks + labels (left), using explicit yTicks so yMax is always present
    for (double yy : yTicks) {
        int y = int(std::lround(mapY(yy)));
        y = qBound(plotRect.top(), y, plotRect.bottom());

        // Major tick mark
        p.drawLine(plotRect.left() - tickLenMajor, y, plotRect.left(), y);

        // Label (right-aligned into left margin)
        const QString label = formatY(yy);
        QRect tr(r.left() + 6,
                 y - fm.height() / 2,
                 (plotRect.left() - 8) - (r.left() + 6),
                 fm.height());
        p.drawText(tr, Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // Minor Y tick marks (no labels)
    if (yMinorStep > 0.0) {
        for (double yy = yFirst; yy <= yMax + 0.5 * yMajorStep; yy += yMinorStep) {
            const double mod = std::fmod(std::fabs(yy - yFirst), yMajorStep);
            if (mod < 1e-9 || std::fabs(mod - yMajorStep) < 1e-9)
                continue; // skip majors

            int y = int(std::lround(mapY(yy)));
            if (y < plotRect.top() || y > plotRect.bottom())
                continue;

            p.drawLine(plotRect.left() - tickLenMinor, y, plotRect.left(), y);
        }
    }

    // X ticks + labels (bottom) in ms (whole numbers)
    auto formatXms = [&](double ms) -> QString { return QString::number(int(std::lround(ms))); };
    int xLabelIndex = 0;

    for (double xm = xFirstMs; xm <= (xMax * 1000.0) + 0.5 * xMajorStepMs; xm += xMajorStepMs) {
        const double xs = xm / 1000.0;
        const int x = int(std::lround(mapX(xs)));
        if (x < plotRect.left() || x > plotRect.right())
            continue;

        // Major tick mark
        p.drawLine(x, plotRect.bottom(), x, plotRect.bottom() + tickLenMajor);

        // Label centered under tick
        if (!isMini || (xLabelIndex % 2 == 0)) {
            const QString label = formatXms(xm);
            QRect tr(x - 50, plotRect.bottom() + 6, 100, fm.height());
            p.drawText(tr, Qt::AlignHCenter | Qt::AlignTop, label);
        }
        ++xLabelIndex;
    }

    // Minor X tick marks (no labels)
    if (xMinorStepMs > 0.0) {
        for (double xm = xFirstMs; xm <= (xMax * 1000.0) + 0.5 * xMajorStepMs; xm += xMinorStepMs) {
            const double mod = std::fmod(std::fabs(xm - xFirstMs), xMajorStepMs);
            if (mod < 1e-6 || std::fabs(mod - xMajorStepMs) < 1e-6)
                continue;

            const double xs = xm / 1000.0;
            const int x = int(std::lround(mapX(xs)));
            if (x < plotRect.left() || x > plotRect.right())
                continue;

            p.drawLine(x, plotRect.bottom(), x, plotRect.bottom() + tickLenMinor);
        }
    }

    // ---------------------------------------------------------------------
    // 7) Measurements overlay (unchanged)
    // ---------------------------------------------------------------------
    if (m_showMeasurements && m_statsValid) {
        const QString units = m_units.isEmpty() ? QString() : (QStringLiteral(" ") + m_units);

        const QString text = tr("Peak: %1%5\nRMS:  %2%5\nMin:  %3%5\nMax:  %4%5")
                                 .arg(m_statPeak, 0, 'f', 2)
                                 .arg(m_statRms, 0, 'f', 2)
                                 .arg(m_statMin, 0, 'f', 2)
                                 .arg(m_statMax, 0, 'f', 2)
                                 .arg(units);

        p.save();

        QFont f = p.font();
        f.setPointSizeF(f.pointSizeF() * 0.90);
        p.setFont(f);

        const int pad = 6;
        QFontMetrics fm2(p.font());
        const QRect bound = fm2.boundingRect(QRect(0, 0, 10000, 10000), Qt::TextWordWrap, text);

        QRect box(plotRect.right() - bound.width() - (pad * 2) - 8,
                  plotRect.top() + 8,
                  bound.width() + (pad * 2),
                  bound.height() + (pad * 2));

        QColor bg = palette().base().color();
        bg.setAlpha(180);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(box, 6, 6);

        p.setPen(palette().text().color());
        p.drawText(box.adjusted(pad, pad, -pad, -pad), Qt::AlignLeft | Qt::AlignTop, text);

        p.restore();
    }
}

void ScopePlotWidget::setSmoothingMode(SmoothingMode mode)
{
    if (m_smoothing == mode)
        return;
    m_smoothing = mode;
    rebuildSmoothedFromRaw();
    recomputeStats();
    update();
}

void ScopePlotWidget::setSmaWindow(int window)
{
    window = std::clamp(window, 1, 2000);
    if (m_smaWindow == window)
        return;
    m_smaWindow = window;

    if (m_smoothing == SmoothingMode::Sma) {
        rebuildSmoothedFromRaw();
        recomputeStats();
        update();
    }
}

void ScopePlotWidget::setEmaAlpha(double alpha)
{
    alpha = std::clamp(alpha, 0.01, 1.0);
    if (qFuzzyCompare(m_emaAlpha, alpha))
        return;
    m_emaAlpha = alpha;

    if (m_smoothing == SmoothingMode::Ema) {
        rebuildSmoothedFromRaw();
        recomputeStats();
        update();
    }
}

void ScopePlotWidget::rebuildSmoothedFromRaw()
{
    m_samples.clear();
    m_samples.reserve(m_rawSamples.size());

    if (m_rawSamples.isEmpty())
        return;

    if (m_smoothing == SmoothingMode::None) {
        m_samples = m_rawSamples;
        return;
    }

    if (m_smoothing == SmoothingMode::Sma) {
        const int w = std::max(1, m_smaWindow);

        std::vector<double> ring(size_t(w), 0.0);
        int count = 0;
        int pos = 0;
        double sum = 0.0;

        for (const auto &pt : m_rawSamples) {
            const double y = pt.y();

            if (count < w) {
                ring[size_t(pos)] = y;
                sum += y;
                ++count;
            } else {
                sum -= ring[size_t(pos)];
                ring[size_t(pos)] = y;
                sum += y;
            }

            pos = (pos + 1) % w;
            const double ySma = sum / double(count);

            m_samples.push_back(QPointF(pt.x(), ySma));
        }
        return;
    }

    // EMA
    double ema = m_rawSamples.first().y();
    m_samples.push_back(QPointF(m_rawSamples.first().x(), ema));

    for (int i = 1; i < m_rawSamples.size(); ++i) {
        const double y = m_rawSamples[i].y();
        ema = m_emaAlpha * y + (1.0 - m_emaAlpha) * ema;
        m_samples.push_back(QPointF(m_rawSamples[i].x(), ema));
    }
}
