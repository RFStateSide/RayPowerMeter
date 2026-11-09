// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "widgets/analogpowermeterwidget.h"
#include "common//Constants.h"

#include <QFontMetricsF>
#include <QPaintEvent>
#include <QPainter>
#include <QtMath>

static inline double clampd(double v, double lo, double hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

AnalogPowerMeterWidget::AnalogPowerMeterWidget(QWidget *parent)
    : WidgetBase(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    m_peakTimer.start();

    m_decayTimer.setInterval(50);
    m_decayTimer.setSingleShot(false);

    connect(&m_decayTimer, &QTimer::timeout, this, [this]() {
        // Decay peaks toward live values
        const double decayStep = (m_mode == PowerBarWidget::MeterMode::Peak) ? m_decayStepPeak
                                                                             : m_decayStepLive;

        if (m_peakFwdW > m_fwdW)
            m_peakFwdW = qMax(m_fwdW, m_peakFwdW - decayStep);
        else
            m_peakFwdW = m_fwdW;

        if (m_peakRefW > m_refW)
            m_peakRefW = qMax(m_refW, m_peakRefW - decayStep);
        else
            m_peakRefW = m_refW;

        update();
    });
}

void AnalogPowerMeterWidget::setScaleFactor(qreal scale)
{
    if (scale <= 0.0)
        scale = 1.0;

    if (qFuzzyCompare(m_scaleFactor, scale))
        return;

    if (m_defaultFontSize <= 0.0) {
        const double curPt = font().pointSizeF();
        if (curPt > 0.0)
            m_defaultFontSize = curPt / scale;
        else
            m_defaultFontSize = 10.0;
    }

    m_scaleFactor = scale;

    QFont f = font();
    if (m_defaultFontSize > 0.0)
        f.setPointSizeF(std::max<double>(MIN_UI_FONT_PT, m_defaultFontSize * m_scaleFactor));

    setFont(f);

    updateGeometry();
    update();
}

void AnalogPowerMeterWidget::setPowerValue(double value)
{
    const double v = qMax(0.0, value);
    if (qFuzzyCompare(m_fwdW, v))
        return;

    m_fwdW = v;
    updatePeaks(m_fwdW, m_refW);
    update();
}

void AnalogPowerMeterWidget::setRightValue(double value)
{
    const double v = qMax(0.0, value);
    if (qFuzzyCompare(m_refW, v))
        return;

    m_refW = v;
    updatePeaks(m_fwdW, m_refW);
    update();
}

void AnalogPowerMeterWidget::setMaxPower(int maxW)
{
    maxW = qMax(1, maxW);
    if (m_maxPower == maxW)
        return;

    m_maxPower = maxW;
    update();
}

void AnalogPowerMeterWidget::setMinPower(int minW)
{
    minW = qMax(0, minW);
    if (m_minPower == minW)
        return;

    m_minPower = minW;
    update();
}

void AnalogPowerMeterWidget::setLabelText(const QString &text)
{
    if (m_label == text)
        return;

    m_label = text;
    update();
}

void AnalogPowerMeterWidget::setThresholds(int yellowW, int redW)
{
    yellowW = qMax(0, yellowW);
    redW = qMax(0, redW);

    m_yellowW = yellowW;
    m_redW = redW;
    update();
}

void AnalogPowerMeterWidget::setTickConfig(double majorStep, double minorStep, bool showLabels)
{
    if (majorStep > 0.0)
        m_majorStep = majorStep;
    if (minorStep > 0.0)
        m_minorStep = minorStep;

    m_showTickLabels = showLabels;
    update();
}

void AnalogPowerMeterWidget::setShowTicks(bool show)
{
    if (m_showTicks == show)
        return;
    m_showTicks = show;
    update();
}

void AnalogPowerMeterWidget::setShowTickLabels(bool show)
{
    if (m_showTickLabels == show)
        return;
    m_showTickLabels = show;
    update();
}

void AnalogPowerMeterWidget::setShowThresholdLabels(bool show)
{
    if (m_showThresholdLabels == show)
        return;
    m_showThresholdLabels = show;
    update();
}

void AnalogPowerMeterWidget::setMeterMode(PowerBarWidget::MeterMode mode)
{
    if (m_mode == mode)
        return;

    m_mode = mode;
    emit meterModeChanged(static_cast<int>(mode));

    // Refresh peaks / decay policy
    updatePeaks(m_fwdW, m_refW);
    update();
}

double AnalogPowerMeterWidget::displayFwdW() const
{
    if (m_mode == PowerBarWidget::MeterMode::Peak)
        return qMax(m_fwdW, m_peakFwdW);
    return m_fwdW;
}

double AnalogPowerMeterWidget::displayRefW() const
{
    if (m_mode == PowerBarWidget::MeterMode::Peak)
        return qMax(m_refW, m_peakRefW);
    return m_refW;
}

double AnalogPowerMeterWidget::clamp01(double x)
{
    if (x < 0.0)
        return 0.0;
    if (x > 1.0)
        return 1.0;
    return x;
}

double AnalogPowerMeterWidget::computeSWR(double fwdW, double refW)
{
    // Approx using power ratio: gamma = sqrt(Pr/Pf)
    if (fwdW <= 0.0)
        return 1.0;

    const double gamma = qSqrt(qMax(0.0, refW) / qMax(1e-9, fwdW));
    const double num = 1.0 + gamma;
    const double den = 1.0 - gamma;

    if (den <= 1e-6)
        return 99.9;

    const double swr = num / den;
    return clampd(swr, 1.0, 99.9);
}

QPointF AnalogPowerMeterWidget::polarPoint(const QPointF &center,
                                           double radius,
                                           double degrees) const
{
    const double rad = qDegreesToRadians(degrees);
    return QPointF(center.x() + radius * qCos(rad), center.y() - radius * qSin(rad));
}

// MFJ-like mapping:
// - Forward (left) needle sweeps from bottom-left (225°) up to top (90°)
// - Reflected (right) needle sweeps from bottom-right (-45°) up to top (90°)
double AnalogPowerMeterWidget::valueToAngleLeft(double w) const
{
    const double t = clamp01((w - m_minPower) / qMax(1.0, double(m_maxPower - m_minPower)));
    const double a0 = 225.0;
    const double a1 = 90.0;
    return a0 + (a1 - a0) * t;
}

double AnalogPowerMeterWidget::valueToAngleRight(double w) const
{
    const double t = clamp01((w - m_minPower) / qMax(1.0, double(m_maxPower - m_minPower)));
    const double a0 = -45.0;
    const double a1 = 90.0;
    return a0 + (a1 - a0) * t;
}

void AnalogPowerMeterWidget::updatePeaks(double fwdW, double refW)
{
    const qint64 elapsed = m_peakTimer.elapsed();

    // Update peaks immediately if rising
    if (fwdW >= m_peakFwdW)
        m_peakFwdW = fwdW;
    if (refW >= m_peakRefW)
        m_peakRefW = refW;

    // Hold peaks for a bit, then decay
    const int holdMs = (m_mode == PowerBarWidget::MeterMode::Peak) ? m_peakHoldMsPeak
                                                                   : m_peakHoldMsLive;

    if (elapsed < holdMs) {
        // Still holding, keep timer running but don't decay yet
        if (m_mode == PowerBarWidget::MeterMode::Peak)
            startDecayTimerIfNeeded();
        return;
    }

    startDecayTimerIfNeeded();
}

void AnalogPowerMeterWidget::startDecayTimerIfNeeded()
{
    // Only needed when peak mode is active, but harmless otherwise
    if (!m_decayTimer.isActive())
        m_decayTimer.start();
}

void AnalogPowerMeterWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Force a centered square drawing area
    const QRectF full = rect();
    const qreal side = qMin(full.width(), full.height());
    const QRectF square(full.center().x() - side / 2.0, full.center().y() - side / 2.0, side, side);

    // Background
    p.fillRect(full, palette().window());

    drawFace(p, square);
    drawNeedles(p, square, displayFwdW(), displayRefW());
}

void AnalogPowerMeterWidget::drawFace(QPainter &p, const QRectF &r) const
{
    // Meter geometry (MFJ-ish):
    // pivot near bottom center, arc above
    const QPointF pivot(r.center().x(), r.bottom() - r.height() * 0.12);
    const qreal radius = r.width() * 0.44;

    // Face border
    p.save();
    p.setPen(QPen(palette().mid().color(), 1.0));
    p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 6, 6);
    p.restore();

    // Title label (top)
    p.save();
    p.setPen(palette().text().color());
    QFont tf = p.font();
    tf.setBold(true);
    tf.setPointSizeF(qMax(8.0, tf.pointSizeF() * 0.95));
    p.setFont(tf);

    const QRectF titleRect(r.left(), r.top() + r.height() * 0.04, r.width(), r.height() * 0.10);
    p.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter, m_label);
    p.restore();

    if (!m_showTicks)
        return;

    // Tick drawing helper
    auto drawTick = [&](double deg, qreal len, qreal w) {
        const QPointF a = polarPoint(pivot, radius, deg);
        const QPointF b = polarPoint(pivot, radius - len, deg);
        QPen pen(palette().text().color(), w);
        p.setPen(pen);
        p.drawLine(a, b);
    };

    // Label drawing helper
    auto drawLabel = [&](double deg, const QString &txt, qreal off) {
        const QPointF pt = polarPoint(pivot, radius - off, deg);
        const QRectF tr(pt.x() - 30, pt.y() - 10, 60, 20);
        p.drawText(tr, Qt::AlignCenter, txt);
    };

    // Label style
    p.save();
    p.setPen(palette().text().color());

    QFont f = p.font();
    f.setPointSizeF(qMax(7.0, f.pointSizeF() * 0.85));
    p.setFont(f);

    // Left arc ticks (Forward)
    const double maxW = double(m_maxPower);
    const double minW = double(m_minPower);

    const double major = qMax(1.0, m_majorStep);
    const double minor = qMax(1.0, m_minorStep);

    // Minor ticks
    for (double w = minW; w <= maxW + 0.5; w += minor) {
        const bool isMajor = (qFuzzyCompare(std::fmod(w - minW, major), 0.0)
                              || qFuzzyCompare(std::fmod(w - minW, major), major));

        const qreal len = isMajor ? (radius * 0.08) : (radius * 0.045);
        const qreal penW = isMajor ? 1.4 : 1.0;

        // Forward (left)
        const double dL = valueToAngleLeft(w);
        drawTick(dL, len, penW);

        // Reflected (right)
        const double dR = valueToAngleRight(w);
        drawTick(dR, len, penW);

        if (isMajor && m_showTickLabels) {
            const QString txt = QString::number(int(w));
            // Put labels slightly inward
            drawLabel(dL, txt, radius * 0.16);
            drawLabel(dR, txt, radius * 0.16);
        }
    }

    // Threshold labels (yellow/red) on both arcs (optional)
    if (m_showThresholdLabels) {
        auto drawThresh = [&](int thrW, const QString &txt) {
            const double wl = clampd(double(thrW), minW, maxW);
            const double dl = valueToAngleLeft(wl);
            const double dr = valueToAngleRight(wl);

            QFont lf = p.font();
            lf.setBold(true);
            p.setFont(lf);

            drawLabel(dl, txt, radius * 0.24);
            drawLabel(dr, txt, radius * 0.24);

            // restore
            lf.setBold(false);
            p.setFont(lf);
        };

        if (m_yellowW > 0)
            drawThresh(m_yellowW, QStringLiteral("Y"));
        if (m_redW > 0)
            drawThresh(m_redW, QStringLiteral("R"));
    }

    p.restore();

    // Center readout: SWR and small labels like MFJ
    p.save();
    p.setPen(palette().text().color());

    const double swr = computeSWR(displayFwdW(), displayRefW());
    QFont rf = p.font();
    rf.setBold(true);
    rf.setPointSizeF(qMax(9.0, rf.pointSizeF() * 1.05));
    p.setFont(rf);

    const QRectF readRect(r.left(),
                          r.center().y() + r.height() * 0.05,
                          r.width(),
                          r.height() * 0.22);
    p.drawText(readRect,
               Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("SWR %1").arg(swr, 0, 'f', 2));

    QFont sf = p.font();
    sf.setBold(false);
    sf.setPointSizeF(qMax(8.0, sf.pointSizeF() * 0.85));
    p.setFont(sf);

    p.drawText(readRect, Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("FWD    /    REF"));

    p.restore();
}

void AnalogPowerMeterWidget::drawNeedles(QPainter &p, const QRectF &r, double fwdW, double refW) const
{
    const QPointF pivot(r.center().x(), r.bottom() - r.height() * 0.12);
    const qreal radius = r.width() * 0.42;

    // Clamp values to scale
    const double f = clampd(fwdW, double(m_minPower), double(m_maxPower));
    const double rr = clampd(refW, double(m_minPower), double(m_maxPower));

    const double angF = valueToAngleLeft(f);
    const double angR = valueToAngleRight(rr);

    // Needle helper
    auto drawNeedle = [&](double deg, const QColor &color) {
        const QPointF tip = polarPoint(pivot, radius, deg);
        const QPointF mid = polarPoint(pivot, radius * 0.70, deg);

        QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(pivot, mid);

        QPen pen2(color, 1.4, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen2);
        p.drawLine(mid, tip);
    };

    // Colors (use palette-friendly choices)
    const QColor fwdColor = QColor(0, 170, 255); // forward
    const QColor refColor = QColor(255, 90, 90); // reflected

    p.save();

    // Draw needles
    drawNeedle(angF, fwdColor);
    drawNeedle(angR, refColor);

    // Pivot knob
    p.setPen(Qt::NoPen);
    p.setBrush(palette().mid().color());
    p.drawEllipse(pivot, r.width() * 0.015, r.width() * 0.015);

    p.setBrush(palette().windowText().color());
    p.drawEllipse(pivot, r.width() * 0.006, r.width() * 0.006);

    p.restore();
}
