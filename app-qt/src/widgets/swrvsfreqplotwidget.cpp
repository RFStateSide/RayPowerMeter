// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "swrvsfreqplotwidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>

SwrVsFreqPlotWidget::SwrVsFreqPlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_title = tr("SWR vs Frequency");
}

void SwrVsFreqPlotWidget::clearPoints()
{
    m_points.clear();
    update();
}

void SwrVsFreqPlotWidget::addPoint(double freqHz, double yValue)
{
    // Replace existing point at same frequency (within 1 Hz) or append
    for (auto &pt : m_points) {
        if (qAbs(pt.first - freqHz) <= 5.0) {
            pt.second = yValue;
            update();
            return;
        }
    }
    m_points.append(qMakePair(freqHz, yValue));

    // Keep sorted by frequency
    std::sort(m_points.begin(), m_points.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

    update();
}

void SwrVsFreqPlotWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();

    // Layout
    const int leftPad = 56;
    const int rightPad = 12;
    const int topPad = 22;
    const int bottomPad = 30;

    // Background
    p.fillRect(r, palette().window());

    // Title
    p.setPen(palette().text().color());
    p.drawText(QRect(r.left() + 8, r.top() + 2, r.width() - 16, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               m_title);

    // Plot area
    const QRect plotRect(r.left() + leftPad,
                         r.top() + topPad,
                         r.width() - leftPad - rightPad,
                         r.height() - topPad - bottomPad);

    // Frame
    p.setPen(palette().mid().color());
    p.drawRect(plotRect.adjusted(0, 0, -1, -1));

    if (m_points.isEmpty()) {
        p.setPen(palette().mid().color());
        p.drawText(plotRect, Qt::AlignCenter, tr("No data yet"));
        return;
    }

    // Convert raw SWR -> display Y based on current units
    auto swrToY = [&](double swr) -> double {
        if (swr < 1.0)
            swr = 1.0;
        if (m_units == UnitsMode::Vswr)
            return swr;

        const double gamma = (swr - 1.0) / (swr + 1.0);
        if (gamma <= 0.0)
            return SWR_VS_FREQ_RL_DB_FLOOR;

        const double rlDb = 20.0 * std::log10(gamma); // negative
        return std::max(SWR_VS_FREQ_RL_DB_FLOOR, rlDb);
    };

    // Build smoothed points for plotting (same X, smoothed Y)
    QVector<QPair<double, double>> pts;
    pts.reserve(m_points.size());

    if (m_smoothing == SmoothingMode::None) {
        for (const auto &pt : m_points)
            pts.append({pt.first, swrToY(pt.second)});
    } else if (m_smoothing == SmoothingMode::Sma) {
        const int w = std::max(1, m_smaWindow);
        double sum = 0.0;
        QVector<double> buf;
        buf.reserve(w);

        for (int i = 0; i < m_points.size(); ++i) {
            const double y = swrToY(m_points[i].second);
            buf.append(y);
            sum += y;
            if (buf.size() > w) {
                sum -= buf.front();
                buf.pop_front();
            }
            const double ySma = sum / double(buf.size());
            pts.append({m_points[i].first, ySma});
        }
    } else { // EMA
        double ema = swrToY(m_points.first().second);
        pts.append({m_points.first().first, ema});
        for (int i = 1; i < m_points.size(); ++i) {
            const double y = swrToY(m_points[i].second);
            ema = m_emaAlpha * y + (1.0 - m_emaAlpha) * ema;
            pts.append({m_points[i].first, ema});
        }
    }

    // Determine X range
    double minF, maxF;
    if (m_useFixedX) {
        minF = m_fixedMinXHz;
        maxF = m_fixedMaxXHz;
    } else {
        minF = pts.first().first;
        maxF = pts.last().first;
    }

    // Determine Y range from display points
    double minY = pts.first().second;
    double maxY = pts.first().second;
    for (const auto &pt : pts) {
        minY = std::min(minY, pt.second);
        maxY = std::max(maxY, pt.second);
    }

    // Expand X a little if degenerate
    if (qAbs(maxF - minF) < 1.0) {
        minF -= 5000.0;
        maxF += 5000.0;
    }

    // Y scaling
    if (m_autoScaleY) {
        if (m_units == UnitsMode::Vswr) {
            minY = std::min(1.0, minY);
            maxY = std::max(1.0, maxY);
            const double span = (maxY - minY);
            maxY = maxY + 0.10 * (span + 1e-9);
        } else {
            // Return Loss dB (negative): keep within [floor..0] and add a little pad
            minY = std::max(SWR_VS_FREQ_RL_DB_FLOOR, minY);
            maxY = std::min(0.0, maxY);

            if (maxY <= minY) {
                minY = SWR_VS_FREQ_FIXED_RL_DB_MIN; // e.g. -30
                maxY = SWR_VS_FREQ_FIXED_RL_DB_MAX; // 0
            } else {
                const double span = (maxY - minY);
                const double pad = 0.10 * span;
                minY = std::max(SWR_VS_FREQ_RL_DB_FLOOR, minY - pad);
                maxY = std::min(0.0, maxY + pad);
            }
        }
    } else {
        if (m_units == UnitsMode::Vswr) {
            minY = 1.0;
            maxY = SWR_VS_FREQ_FIXED_SWR_MAX; // 3.0
        } else {
            minY = SWR_VS_FREQ_FIXED_RL_DB_MIN; // -30
            maxY = SWR_VS_FREQ_FIXED_RL_DB_MAX; // 0
        }
    }

    // Final safety
    if (qAbs(maxY - minY) < 1e-6) {
        if (m_units == UnitsMode::Vswr) {
            minY = 1.0;
            maxY = SWR_VS_FREQ_FIXED_SWR_MAX;
        } else {
            minY = SWR_VS_FREQ_FIXED_RL_DB_MIN;
            maxY = SWR_VS_FREQ_FIXED_RL_DB_MAX;
        }
    }
    if (maxY <= minY) {
        if (m_units == UnitsMode::ReturnLossDb) {
            minY = SWR_VS_FREQ_FIXED_RL_DB_MIN;
            maxY = SWR_VS_FREQ_FIXED_RL_DB_MAX;
        } else {
            minY = 1.0;
            maxY = SWR_VS_FREQ_FIXED_SWR_MAX;
        }
    }

    auto mapX = [&](double fHz) -> int {
        const double t = (fHz - minF) / (maxF - minF);
        return plotRect.left() + int(t * plotRect.width());
    };
    auto mapY = [&](double y) -> int {
        const double t = (y - minY) / (maxY - minY);
        return plotRect.bottom() - int(t * plotRect.height());
    };

    // Grid + ticks
    p.setPen(palette().midlight().color());
    const int xTicks = 5;
    const int yTicks = 5;

    for (int i = 0; i <= xTicks; i++) {
        const int x = plotRect.left() + (plotRect.width() * i) / xTicks;
        p.drawLine(x, plotRect.top(), x, plotRect.bottom());

        const double f = minF + (maxF - minF) * (double(i) / xTicks);
        const QString lbl = QString::number(f / 1e6, 'f', 3);
        p.setPen(palette().text().color());
        p.drawText(QRect(x - 40, plotRect.bottom() + 4, 80, 20),
                   Qt::AlignHCenter | Qt::AlignTop,
                   lbl);
        p.setPen(palette().midlight().color());
    }

    for (int i = 0; i <= yTicks; i++) {
        const int y = plotRect.bottom() - (plotRect.height() * i) / yTicks;
        p.drawLine(plotRect.left(), y, plotRect.right(), y);

        const double v = minY + (maxY - minY) * (double(i) / yTicks);
        const QString lbl = (m_units == UnitsMode::Vswr) ? QString::number(v, 'f', 2)
                                                         : QString::number(v, 'f', 1);

        p.setPen(palette().text().color());
        p.drawText(QRect(r.left() + 2, y - 10, leftPad - 6, 20),
                   Qt::AlignRight | Qt::AlignVCenter,
                   lbl);
        p.setPen(palette().midlight().color());
    }

    // Axis labels
    p.setPen(palette().text().color());
    p.drawText(QRect(plotRect.left(), r.bottom() - 24, plotRect.width(), 20),
               Qt::AlignHCenter | Qt::AlignVCenter,
               tr("Frequency (MHz)"));

    // Data line
    QPainterPath path;
    bool first = true;
    for (const auto &pt : pts) {
        const int x = mapX(pt.first);
        const int y = mapY(pt.second);
        if (first) {
            path.moveTo(x, y);
            first = false;
        } else {
            path.lineTo(x, y);
        }
    }

    p.setPen(palette().highlight().color());
    p.drawPath(path);

    // Points (optional)
    if (m_showPoints) {
        p.setBrush(palette().highlight().color());
        p.setPen(Qt::NoPen);
        for (const auto &pt : pts) {
            p.drawEllipse(QPoint(mapX(pt.first), mapY(pt.second)), 3, 3);
        }
    }

    // Measurements overlay
    if (m_showMeasurements && !pts.isEmpty()) {
        double vMin = pts[0].second, vMax = pts[0].second, sum = 0.0;
        for (const auto &pt : pts) {
            vMin = std::min(vMin, pt.second);
            vMax = std::max(vMax, pt.second);
            sum += pt.second;
        }
        const double avg = sum / double(pts.size());
        const QString units = (m_units == UnitsMode::ReturnLossDb) ? tr(" dB") : QString();

        const QString text = tr("Min: %1%4\nMax: %2%4\nAvg: %3%4")
                                 .arg(vMin, 0, 'f', 2)
                                 .arg(vMax, 0, 'f', 2)
                                 .arg(avg, 0, 'f', 2)
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

void SwrVsFreqPlotWidget::setFixedXRangeHz(double minHz, double maxHz)
{
    m_useFixedX = true;
    m_fixedMinXHz = minHz;
    m_fixedMaxXHz = maxHz;
    update();
}

void SwrVsFreqPlotWidget::clearFixedXRange()
{
    m_useFixedX = false;
    update();
}

void SwrVsFreqPlotWidget::setSmoothingMode(SmoothingMode mode)
{
    m_smoothing = mode;
    update();
}

void SwrVsFreqPlotWidget::setSmaWindow(int window)
{
    // keep it sane; tweak bounds as you like
    window = std::clamp(window, 1, 200);
    if (m_smaWindow == window)
        return;
    m_smaWindow = window;
    update();
}

void SwrVsFreqPlotWidget::setEmaAlpha(double alpha)
{
    // alpha in (0..1]
    alpha = std::clamp(alpha, 0.01, 1.0);
    if (qFuzzyCompare(m_emaAlpha, alpha))
        return;
    m_emaAlpha = alpha;
    update();
}
