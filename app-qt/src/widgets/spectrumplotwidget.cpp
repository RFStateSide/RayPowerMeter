// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "spectrumplotwidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QtMath>
#include <algorithm>

SpectrumPlotWidget::SpectrumPlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
}

void SpectrumPlotWidget::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void SpectrumPlotWidget::setDbScale(bool on)
{
    m_dbScale = on;
    update();
}

void SpectrumPlotWidget::setMaxFrequencyHz(double maxHz)
{
    m_maxHz = std::max(0.0, maxHz);
    update();
}

void SpectrumPlotWidget::setSpectrum(const QVector<QPointF> &points)
{
    m_pts = points;

    // find peak (max y)
    m_peakHz = 0.0;
    m_peakY = 0.0;
    if (!m_pts.isEmpty()) {
        auto it = std::max_element(m_pts.begin(),
                                   m_pts.end(),
                                   [](const QPointF &a, const QPointF &b) { return a.y() < b.y(); });
        m_peakHz = it->x();
        m_peakY = it->y();
    }

    update();
}

void SpectrumPlotWidget::clear()
{
    m_pts.clear();
    m_peakHz = 0.0;
    m_peakY = 0.0;
    update();
}

double SpectrumPlotWidget::niceStep(double span, int targetTicks)
{
    if (!(span > 0.0) || targetTicks <= 0)
        return 1.0;

    const double raw = span / double(targetTicks);
    const double p10 = qPow(10.0, qFloor(qLn(raw) / qLn(10.0)));
    const double n = raw / p10;

    double step = 1.0;
    if (n < 1.5)
        step = 1.0;
    else if (n < 3.0)
        step = 2.0;
    else if (n < 7.0)
        step = 5.0;
    else
        step = 10.0;

    return step * p10;
}

QString SpectrumPlotWidget::formatFreq(double hz)
{
    if (hz >= 1000.0)
        return QString::number(hz / 1000.0, 'f', 1) + QStringLiteral(" kHz");
    return QString::number(hz, 'f', 0) + QStringLiteral(" Hz");
}

void SpectrumPlotWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();
    p.fillRect(r, palette().window());

    // Layout
    int leftPad = 54;
    const int rightPad = 10;
    const int topPad = 22;
    const int bottomPad = 28;

    const QRect plotR(leftPad,
                      topPad,
                      r.width() - leftPad - rightPad,
                      r.height() - topPad - bottomPad);
    if (plotR.width() <= 10 || plotR.height() <= 10)
        return;

    // Title + peak readout
    p.setPen(palette().text().color());
    QString headline = m_title;
    if (!m_pts.isEmpty()) {
        headline += QStringLiteral("   |   Peak ");
        headline += formatFreq(m_peakHz);
        headline += QStringLiteral(" @ ");
        headline += (m_dbScale ? QString::number(m_peakY, 'f', 1) + " dB"
                               : QString::number(m_peakY, 'f', 3));
    }
    p.drawText(QRect(leftPad, 2, r.width() - leftPad - rightPad, topPad - 2),
               Qt::AlignLeft | Qt::AlignVCenter,
               headline);

    // Determine X range
    double xMin = 0.0;
    double xMax = (m_maxHz > 0.0) ? m_maxHz : 0.0;
    if (xMax <= 0.0 && !m_pts.isEmpty())
        xMax = std::max(1.0, m_pts.last().x());

    // Determine Y range from data
    double yMin = 0.0, yMax = 1.0;
    if (!m_pts.isEmpty()) {
        yMin = m_pts.first().y();
        yMax = m_pts.first().y();
        for (const auto &pt : m_pts) {
            yMin = std::min(yMin, pt.y());
            yMax = std::max(yMax, pt.y());
        }
        if (qFuzzyCompare(yMin, yMax)) {
            yMin -= 1.0;
            yMax += 1.0;
        } else {
            const double pad = (yMax - yMin) * 0.08;
            yMin -= pad;
            yMax += pad;
        }
    } else {
        // reasonable defaults for empty
        if (m_dbScale) {
            yMin = -100.0;
            yMax = 0.0;
        } else {
            yMin = 0.0;
            yMax = 1.0;
        }
    }

    // Grid + ticks
    p.setPen(palette().mid().color());
    const int xTicks = 5;
    const int yTicks = 5;

    for (int i = 0; i <= xTicks; ++i) {
        const int x = plotR.left() + (plotR.width() * i) / xTicks;
        p.drawLine(x, plotR.top(), x, plotR.bottom());
    }
    for (int i = 0; i <= yTicks; ++i) {
        const int y = plotR.top() + (plotR.height() * i) / yTicks;
        p.drawLine(plotR.left(), y, plotR.right(), y);
    }

    // Axis labels (X)
    p.setPen(palette().text().color());
    const double xSpan = std::max(1.0, xMax - xMin);
    const double xStep = niceStep(xSpan, xTicks);
    const double xStart = qFloor(xMin / xStep) * xStep;

    for (double xv = xStart; xv <= xMax + 0.5 * xStep; xv += xStep) {
        if (xv < xMin - 0.5 * xStep)
            continue;
        const double t = (xv - xMin) / xSpan;
        const int px = plotR.left() + int(t * plotR.width());
        const QString s = formatFreq(xv);

        const int labelW = 100;
        int xLeft = px - labelW / 2;
        xLeft = qBound(0, xLeft, r.width() - labelW);

        p.drawText(QRect(xLeft, plotR.bottom() + 4, labelW, bottomPad - 4),
                   Qt::AlignHCenter | Qt::AlignTop,
                   s);
    }

    // Axis labels (Y)
    const double ySpan = std::max(1e-9, yMax - yMin);
    const double yStep = niceStep(ySpan, yTicks);
    const double yStart = qFloor(yMin / yStep) * yStep;

    for (double yv = yStart; yv <= yMax + 0.5 * yStep; yv += yStep) {
        if (yv < yMin - 0.5 * yStep)
            continue;
        const double t = (yv - yMin) / ySpan;
        const int py = plotR.bottom() - int(t * plotR.height());
        const QString s = m_dbScale ? (QString::number(yv, 'f', 0) + " dB")
                                    : QString::number(yv, 'f', 2);
        p.drawText(QRect(0, py - 10, leftPad - 6, 20), Qt::AlignRight | Qt::AlignVCenter, s);
    }

    // Spectrum polyline
    if (m_pts.size() >= 2) {
        QPolygonF poly;
        poly.reserve(m_pts.size());

        for (const auto &pt : m_pts) {
            const double x = pt.x();
            const double y = pt.y();

            if (x < xMin || x > xMax)
                continue;

            const double tx = (x - xMin) / xSpan;
            const double ty = (y - yMin) / ySpan;

            const double px = plotR.left() + tx * plotR.width();
            const double py = plotR.bottom() - ty * plotR.height();
            poly << QPointF(px, py);
        }

        p.setPen(QPen(palette().highlight().color(), 1.5));
        p.drawPolyline(poly);
    }
}
