// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "widgets/amplifieranalyzerplotwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

AmplifierAnalyzerPlotWidget::AmplifierAnalyzerPlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void AmplifierAnalyzerPlotWidget::clearPoints()
{
    m_points.clear();
    update();
}

void AmplifierAnalyzerPlotWidget::addPoint(double x, double y)
{
    m_points.append({x, y});
    update();
}

void AmplifierAnalyzerPlotWidget::setFixedXRange(double minX, double maxX)
{
    m_useFixedX = true;
    m_fixedMinX = minX;
    m_fixedMaxX = maxX;
    update();
}

void AmplifierAnalyzerPlotWidget::clearFixedXRange()
{
    m_useFixedX = false;
    update();
}

static void computeMinMax(const QVector<QPair<double, double>> &pts,
                          double &minX,
                          double &maxX,
                          double &minY,
                          double &maxY)
{
    if (pts.isEmpty()) {
        minX = 0;
        maxX = 1;
        minY = 0;
        maxY = 1;
        return;
    }

    minX = maxX = pts.first().first;
    minY = maxY = pts.first().second;

    for (const auto &p : pts) {
        minX = qMin(minX, p.first);
        maxX = qMax(maxX, p.first);
        minY = qMin(minY, p.second);
        maxY = qMax(maxY, p.second);
    }

    if (qFuzzyCompare(minX, maxX)) {
        minX -= 1.0;
        maxX += 1.0;
    }
    if (qFuzzyCompare(minY, maxY)) {
        minY -= 1.0;
        maxY += 1.0;
    }
}

void AmplifierAnalyzerPlotWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();

    // ---- Layout constants ----
    int leftPad = 52; // will be recomputed after we know Y tick widths
    const int rightPad = 10;
    const int topPad = 26;    // room for title
    const int bottomPad = 34; // room for X tick labels + x label

    // Background
    p.fillRect(r, palette().window());

    // ------------------------------------------------------------
    // Helpers for ticks
    // ------------------------------------------------------------
    auto pow10floor = [](double x) -> double {
        if (x <= 0.0)
            return 1.0;
        return std::pow(10.0, std::floor(std::log10(x)));
    };

    auto niceStep = [&](double span, int targetTicks) -> double {
        if (span <= 0.0)
            return 1.0;
        targetTicks = qMax(2, targetTicks);

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

    // ------------------------------------------------------------
    // Determine ranges
    // ------------------------------------------------------------
    double minX = 0.0, maxX = 1.0, minY = 0.0, maxY = 1.0;

    auto computeMinMax = [&](const QVector<QPair<double, double>> &pts,
                             double &outMinX,
                             double &outMaxX,
                             double &outMinY,
                             double &outMaxY) {
        if (pts.isEmpty()) {
            outMinX = 0.0;
            outMaxX = 1.0;
            outMinY = 0.0;
            outMaxY = 1.0;
            return;
        }
        outMinX = outMaxX = pts.first().first;
        outMinY = outMaxY = pts.first().second;

        for (const auto &pt : pts) {
            outMinX = qMin(outMinX, pt.first);
            outMaxX = qMax(outMaxX, pt.first);
            outMinY = qMin(outMinY, pt.second);
            outMaxY = qMax(outMaxY, pt.second);
        }
    };

    computeMinMax(m_points, minX, maxX, minY, maxY);

    if (m_useFixedX) {
        minX = m_fixedMinX;
        maxX = m_fixedMaxX;
        if (qFuzzyCompare(minX, maxX)) {
            minX -= 1.0;
            maxX += 1.0;
        }
    }

    // Defaults if empty
    if (m_points.isEmpty()) {
        if (m_yLabel.contains(QStringLiteral("dB"), Qt::CaseInsensitive)) {
            minY = 0.0;
            maxY = 30.0;
        } else {
            minY = 0.0;
            maxY = 1.0;
        }
        if (!m_useFixedX) {
            minX = 0.0;
            maxX = 1.0;
        }
    }

    auto niceCeil = [](double value, double step) -> double {
        if (step <= 0.0)
            return value;
        return std::ceil(value / step) * step;
    };

    // If auto-scale is OFF: keep a stable axis, but choose a "nice" maxY from data
    if (!m_autoScaleY) {
        const bool isDb = m_yLabel.contains(QStringLiteral("dB"), Qt::CaseInsensitive);

        const double dataMinY = minY;
        const double dataMaxY = maxY;

        // For gain plots, we typically want the baseline anchored at 0 dB
        double desiredMinY = isDb ? 0.0 : dataMinY;

        // Guard against odd cases (all points below 0, or empty-ish span)
        if (isDb && dataMaxY < desiredMinY) {
            // If everything is below 0, allow the axis to include it
            desiredMinY = dataMinY;
        }

        const int targetMajorTicks = 6;

        // Build a "nice" step based on the data span from desiredMinY -> dataMaxY
        double spanForStep = qMax(1e-12, (dataMaxY - desiredMinY));
        double step = niceStep(spanForStep, targetMajorTicks);
        if (step <= 0.0)
            step = 1.0;

        // Snap min/max to nice boundaries
        minY = isDb ? desiredMinY : niceFloor(desiredMinY, step);

        // Add a tiny headroom before ceiling so the trace doesn’t sit on the top grid line
        const double headroom = spanForStep * 0.02;
        maxY = niceCeil(dataMaxY + headroom, step);

        // Ensure we have a non-degenerate range
        if (!(maxY > minY)) {
            maxY = minY + step * (targetMajorTicks - 1);
        }
    }

    // Pad Y range slightly (more pad when auto-scale)
    {
        double span = (maxY - minY);
        if (span <= 0.0)
            span = 1.0;

        const double pad = span * (m_autoScaleY ? 0.08 : 0.05);
        minY -= pad;
        maxY += pad;

        if (qFuzzyCompare(minY, maxY)) {
            minY -= 1.0;
            maxY += 1.0;
        }
    }

    // ------------------------------------------------------------
    // Build ticks
    // ------------------------------------------------------------
    const double xSpan = qMax(1e-12, (maxX - minX));
    const double ySpan = qMax(1e-12, (maxY - minY));

    const int targetMajorTicks = 6;

    // Y ticks
    const double yMajorStep = niceStep(ySpan, targetMajorTicks);
    const double yFirst = niceFloor(minY, yMajorStep);

    QVector<double> yTicks;
    for (double yy = yFirst; yy <= maxY + 0.5 * yMajorStep; yy += yMajorStep)
        yTicks << yy;

    // X ticks
    const double xMajorStep = niceStep(xSpan, targetMajorTicks);
    const double xFirst = niceFloor(minX, xMajorStep);

    QVector<double> xTicks;
    for (double xx = xFirst; xx <= maxX + 0.5 * xMajorStep; xx += xMajorStep)
        xTicks << xx;

    // Tick formatting: smart decimals based on step
    auto decimalsForStep = [](double step) -> int {
        if (step >= 10.0)
            return 0;
        if (step >= 1.0)
            return 0;
        if (step >= 0.1)
            return 1;
        if (step >= 0.01)
            return 2;
        return 3;
    };

    const int yDec = decimalsForStep(yMajorStep);
    const int xDec = decimalsForStep(xMajorStep);

    QFontMetrics fm(font());

    auto formatY = [&](double v) -> QString { return QString::number(v, 'f', yDec); };
    auto formatX = [&](double v) -> QString { return QString::number(v, 'f', xDec); };

    // Dynamic leftPad: based on widest Y tick label, plus room for Y axis label
    QString widestY = formatY(minY);
    for (double yy : yTicks) {
        const QString t = formatY(yy);
        if (fm.horizontalAdvance(t) > fm.horizontalAdvance(widestY))
            widestY = t;
    }

    const int yTickTextW = fm.horizontalAdvance(widestY);
    const int yLabelExtra = 18; // extra room for rotated "Gain (dB)"
    leftPad = qBound(48, yTickTextW + 18 + yLabelExtra, 140);

    const QRect plotRect = r.adjusted(leftPad, topPad, -rightPad, -bottomPad);
    const QRect plotInner = plotRect.adjusted(1, 1, -1, -1); // <-- keeps grid/curve inside border

    // ------------------------------------------------------------
    // Title
    // ------------------------------------------------------------
    p.setPen(palette().text().color());
    p.drawText(r.adjusted(8, 2, -8, -2), Qt::AlignHCenter | Qt::AlignTop, m_title);

    // ------------------------------------------------------------
    // Mapping functions (use plotInner)
    // ------------------------------------------------------------
    auto mapX = [&](double x) -> int {
        const double t = (x - minX) / xSpan;
        return plotInner.left() + int(t * plotInner.width());
    };
    auto mapY = [&](double y) -> int {
        const double t = (y - minY) / ySpan;
        return plotInner.bottom() - int(t * plotInner.height());
    };

    // ------------------------------------------------------------
    // Plot background + border
    // ------------------------------------------------------------
    p.fillRect(plotRect, palette().base());
    p.setPen(QPen(QColor(140, 140, 140), 1));
    p.drawRect(plotRect);

    // ------------------------------------------------------------
    // Grid + tick labels
    // ------------------------------------------------------------
    const QColor gridMajor(130, 130, 130);
    const QColor axisText = palette().text().color();

    // Y grid + labels
    p.setPen(QPen(gridMajor, 1));
    for (double yy : yTicks) {
        const int y = mapY(yy);
        if (y < plotInner.top() || y > plotInner.bottom())
            continue;

        p.drawLine(plotInner.left(), y, plotInner.right(), y);

        // tick label (right-aligned just left of plotRect)
        const QString t = formatY(yy) + (m_yLabel.contains("dB", Qt::CaseInsensitive) ? " dB" : "");
        p.setPen(axisText);
        p.drawText(QRect(r.left(), y - fm.height() / 2, plotRect.left() - 6, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter,
                   t);
        p.setPen(QPen(gridMajor, 1));
    }

    // X grid + labels
    p.setPen(QPen(gridMajor, 1));
    for (double xx : xTicks) {
        const int x = mapX(xx);
        if (x < plotInner.left() || x > plotInner.right())
            continue;

        p.drawLine(x, plotInner.top(), x, plotInner.bottom());

        const QString t = formatX(xx);
        p.setPen(axisText);
        p.drawText(QRect(x - 40, plotRect.bottom() + 2, 80, fm.height()),
                   Qt::AlignHCenter | Qt::AlignTop,
                   t);
        p.setPen(QPen(gridMajor, 1));
    }

    // Axis labels
    p.setPen(axisText);

    // X label centered under axis
    p.drawText(QRect(plotRect.left(), r.bottom() - fm.height() - 2, plotRect.width(), fm.height()),
               Qt::AlignHCenter | Qt::AlignVCenter,
               m_xLabel);

    // Y label rotated, placed in left margin *away from tick labels*
    // (this fixes overlap)
    if (!m_yLabel.isEmpty()) {
        p.save();
        // Left margin rectangle: from r.left to plotRect.left
        const QRect leftMarginRect(r.left(),
                                   plotRect.top(),
                                   plotRect.left() - r.left(),
                                   plotRect.height());
        // Rotate around a point a bit left of the tick label area
        const int cx = leftMarginRect.left() + 12; // <-- pushes label away from tick text
        const int cy = leftMarginRect.center().y();

        p.translate(cx, cy);
        p.rotate(-90);
        p.drawText(QRect(-leftMarginRect.height() / 2,
                         -fm.height() / 2,
                         leftMarginRect.height(),
                         fm.height()),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   m_yLabel);
        p.restore();
    }

    // ------------------------------------------------------------
    // Curve + points
    // ------------------------------------------------------------
    if (m_points.size() >= 2) {
        QPainterPath path;
        path.moveTo(mapX(m_points[0].first), mapY(m_points[0].second));
        for (int i = 1; i < m_points.size(); ++i)
            path.lineTo(mapX(m_points[i].first), mapY(m_points[i].second));

        p.setPen(QPen(QColor(0, 160, 255), 2));
        p.drawPath(path);

        if (m_showPoints) {
            p.setBrush(QColor(0, 160, 255));
            p.setPen(Qt::NoPen);
            for (const auto &pt : m_points)
                p.drawEllipse(QPoint(mapX(pt.first), mapY(pt.second)), 3, 3);
        }
    }

    // ------------------------------------------------------------
    // Measurements box (top-right)
    // ------------------------------------------------------------
    if (m_showMeasurements) {
        double mn = 0.0, mx = 0.0, avg = 0.0;
        if (!m_points.isEmpty()) {
            mn = mx = m_points.first().second;
            double sum = 0.0;
            for (const auto &pt : m_points) {
                mn = qMin(mn, pt.second);
                mx = qMax(mx, pt.second);
                sum += pt.second;
            }
            avg = sum / double(m_points.size());
        }

        QStringList lines;
        lines << QString("Min: %1").arg(mn, 0, 'f', 2) << QString("Max: %1").arg(mx, 0, 'f', 2)
              << QString("Avg: %1").arg(avg, 0, 'f', 2) << QString("N=%1").arg(m_points.size());

        if (!m_extraMeasurements.isEmpty()) {
            lines << ""; // spacer line
            lines.append(m_extraMeasurements);
        }

        const QString measText = lines.join("\n");

        const int pad = 6;
        const int lineCount = lines.size();
        const int boxW = 210; // widened a bit for P1dB lines
        const int boxH = fm.height() * lineCount + pad * 2;

        const QRect box = QRect(plotRect.right() - boxW - 10, plotRect.top() + 10, boxW, boxH);

        p.setPen(QPen(QColor(180, 180, 180), 1));
        p.setBrush(QColor(30, 30, 30, 190));
        p.drawRoundedRect(box, 6, 6);

        p.setPen(Qt::white);
        p.drawText(box.adjusted(pad, pad, -pad, -pad), Qt::AlignLeft | Qt::AlignTop, measText);
    }
}
