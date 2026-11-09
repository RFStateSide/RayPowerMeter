// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "powerbarvertwidget.h"
#include "common/Constants.h"
#include <algorithm>

#include <QEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QStyleOption>
#include <QtMath>

PowerBarVertWidget::PowerBarVertWidget(QWidget *parent)
    : WidgetBase(parent)
{
    setWidgetName(QStringLiteral("PowerBarVert"));

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    QPalette pal = this->palette();
    m_labelTextColor = pal.text().color();
    m_valueTextColor = pal.text().color();

    // TODO: Make isDarkTheme-aware and use correct theme colors as PowerBarWidget does.
    m_greenColor = Qt::green;
    m_yellowColor = Qt::yellow;
    m_redColor = Qt::red;

    updateFontForScale();
    updateMinimumHeight();

    // Peak hold timer setup to match PowerBarWidget behavior
    m_peakHoldTimer.setSingleShot(true);
    m_peakHoldTimer.setInterval(m_peakHoldLiveHoldMs);

    connect(&m_peakHoldTimer, &QTimer::timeout, this, [this]() {
        m_peakHoldExpired = true;
        m_peakDecayClock.restart();
        update();
    });
}

PowerBarVertWidget::~PowerBarVertWidget()
{
    qDebug() << "[PowerBarVertWidget] destroyed:" << this << "objectName=" << objectName();
}

void PowerBarVertWidget::onAppScaleFactorChanged(double scale)
{
    setScaleFactor(scale);
}

void PowerBarVertWidget::setPowerValue(double value)
{
    // Clamp negatives
    m_powerValue = qMax(0.0, value);

    // Keep peak glued to tip in None
    if (m_mode == MeterMode::None) {
        m_peakValue = m_powerValue;
        update();
        return;
    }

    // Live and Peak: capture new peaks, then hold then decay
    if (m_mode == MeterMode::Live || m_mode == MeterMode::Peak) {
        auto startHoldForMode = [this]() {
            if (m_mode == MeterMode::Live) {
                m_peakHoldTimer.setInterval(m_peakHoldLiveHoldMs);
                m_peakHoldTimer.start();
            } else if (m_mode == MeterMode::Peak) {
                m_peakHoldTimer.setInterval(m_peakHoldHoldMs);
                m_peakHoldTimer.start();
            }
        };

        if (m_powerValue >= m_peakValue) {
            m_peakValue = m_powerValue;
            m_peakHoldExpired = false;
            startHoldForMode();
            m_peakDecayClock.invalidate(); // new peak; decay timing restarts after hold
        } else if (m_peakHoldExpired) {
            // Scale-aware decay after hold expires
            const double range = qMax(1.0, double(m_maxPower - m_minPower));
            const double dtSec = (m_peakDecayClock.isValid() ? (m_peakDecayClock.elapsed() / 1000.0)
                                                             : 0.0);

            double fracPerSec = 0.0;
            if (m_mode == MeterMode::Live)
                fracPerSec = m_peakHoldLiveDecayFracPerSec;
            else                                              // Peak mode
                fracPerSec = m_peakHoldFreezeDecayFracPerSec; // name kept, now used for Peak decay

            if (dtSec > 0.0 && fracPerSec > 0.0) {
                const double step = fracPerSec * range * dtSec;
                m_peakValue = qMax(m_powerValue, m_peakValue - step);
                m_peakDecayClock.restart();
            }
        }

        update();
        return;
    }

    // Freeze: hold only; no decay
    if (m_mode == MeterMode::Freeze) {
        if (m_powerValue > m_peakValue)
            m_peakValue = m_powerValue;
        update();
        return;
    }

    update();
}

void PowerBarVertWidget::setLabelText(const QString &text)
{
    m_labelText = text;
    update();
}

void PowerBarVertWidget::setLabelTextColor(const QColor &color)
{
    m_labelTextColor = color;
    update();
}

void PowerBarVertWidget::setValueColor(const QColor &color)
{
    m_valueTextColor = color;
    update();
}

void PowerBarVertWidget::setRightLabelText(const QString &text)
{
    m_rightLabelText = text;
    update();
}

void PowerBarVertWidget::setRightValueText(const QString &text)
{
    m_rightValueText = text;
    update();
}

void PowerBarVertWidget::setRightValue(double value)
{
    m_rightRaw = value;

    if (!m_rightTimer.isValid()) {
        m_rightTimer.start();
        m_rightDisplayed = value;
        m_rightValueText = QString::number(m_rightDisplayed, 'f', m_rightPrecision);
        update();
        return;
    }

    if (m_rightTimer.elapsed() >= m_rightIntervalMs) {
        m_rightTimer.restart();
        m_rightDisplayed = value;
        m_rightValueText = QString::number(m_rightDisplayed, 'f', m_rightPrecision);
        update();
    }
}

void PowerBarVertWidget::setThresholds(int yellow, int red)
{
    m_yellowVal = yellow;
    m_redVal = red;
    update();
}

void PowerBarVertWidget::setTickConfig(double majorStep, double minorStep, bool labels)
{
    m_majorTickStep = qMax(0.0, majorStep);
    m_minorTickStep = qMax(0.0, minorStep);
    m_showTickLabels = labels;
    update();
}

void PowerBarVertWidget::setShowTicks(bool show)
{
    m_showTicks = show;
    update();
}

void PowerBarVertWidget::setShowTickLabels(bool labels)
{
    m_showTickLabels = labels;
    update();
}

void PowerBarVertWidget::setShowThresholdLabels(bool show)
{
    m_showThresholdLabels = show;
    update();
}

void PowerBarVertWidget::setScaleFactor(qreal scale)
{
    m_scaleFactor = qMax(0.5, scale);
    updateFontForScale();
    updateMinimumHeight();
    updateGeometry();
    update();
}

void PowerBarVertWidget::setOkWarnCritColors(const QColor &ok,
                                             const QColor &warn,
                                             const QColor &crit)
{
    m_greenColor = ok;
    m_yellowColor = warn;
    m_redColor = crit;
    update();
}

void PowerBarVertWidget::changeEvent(QEvent *event)
{
    // Let base class handle generic stuff
    WidgetBase::changeEvent(event);

    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::StyleChange) {
        // Global theme/palette changed (Helper::ApplyAppTheme was called)
        updateLabelColorFromPalette();

        updateFontForScale();
        update();
    }
}

void PowerBarVertWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event && event->button() == Qt::LeftButton) {
        clearPeak();
        event->accept();
        return;
    }
    WidgetBase::mouseDoubleClickEvent(event);
}

// ---------------------------
// Meter mode control methods
// ---------------------------
void PowerBarVertWidget::setMeterMode(MeterMode mode)
{
    if (m_mode == mode)
        return;

    m_mode = mode;

    // Mode transitions: keep semantics consistent with PowerBarWidget
    switch (m_mode) {
    case MeterMode::Live:
        m_peakHoldExpired = false;
        m_peakHoldTimer.setInterval(m_peakHoldLiveHoldMs);
        if (!m_peakHoldTimer.isActive())
            m_peakHoldTimer.start();
        break;

    case MeterMode::Peak:
        m_peakHoldExpired = false;
        m_peakHoldTimer.setInterval(m_peakHoldHoldMs); // "Peak" hold
        if (!m_peakHoldTimer.isActive())
            m_peakHoldTimer.start();
        break;

    case MeterMode::Freeze:
        if (m_peakHoldTimer.isActive())
            m_peakHoldTimer.stop();
        // Freeze holds current peak until cleared
        m_peakValue = qMax(m_peakValue, m_powerValue);
        m_peakHoldExpired = false;
        break;

    case MeterMode::None:
        if (m_peakHoldTimer.isActive())
            m_peakHoldTimer.stop();
        m_peakValue = m_powerValue; // marker hugs tip
        m_peakHoldExpired = false;
        break;
    }

    update();
}

void PowerBarVertWidget::clearPeak()
{
    m_peakValue = m_powerValue;
    m_peakHoldExpired = false;
    m_peakDecayClock.restart();

    if (m_mode == MeterMode::Live) {
        m_peakHoldTimer.setInterval(m_peakHoldLiveHoldMs);
        m_peakHoldTimer.start();
    } else if (m_mode == MeterMode::Peak) {
        m_peakHoldTimer.setInterval(m_peakHoldHoldMs);
        m_peakHoldTimer.start();
    } else {
        if (m_peakHoldTimer.isActive())
            m_peakHoldTimer.stop();
    }

    update();
}

// ---------------------------
// Painting / geometry helpers
// ---------------------------

void PowerBarVertWidget::updateFontForScale()
{
    // Robust base capture (recover unscaled base even if widget created after scaling is active)
    if (m_basePointSize <= 0.0) {
        const double curPt = font().pointSizeF();
        if (curPt > 0.0)
            m_basePointSize = curPt / m_scaleFactor;
        else
            m_basePointSize = BASE_UI_FONT_PT; // fallback
    }

    // ---- UI font (top label/value + bottom label/value) ----
    QFont uiFont = font();
    if (uiFont.pointSizeF() > 0.0) {
        const double targetPt = std::max<double>(MIN_UI_FONT_PT, m_basePointSize * m_scaleFactor);
        uiFont.setPointSizeF(targetPt);
    } else if (uiFont.pixelSize() > 0) {
        const int px = int(std::lround(uiFont.pixelSize() * m_scaleFactor));
        uiFont.setPixelSize(std::max<int>(int(MIN_UI_FONT_PX), px));
    }

    // Keep your UI look
    uiFont.setBold(true);
    setFont(uiFont);

    // ---- Tick font (tick labels ONLY) ----
    m_tickFont = uiFont;
    if (m_tickFont.pointSizeF() > 0.0) {
        const double tickPt = std::max<double>(MIN_TICK_FONT_PT, m_basePointSize * m_scaleFactor);
        m_tickFont.setPointSizeF(tickPt);
    } else if (m_tickFont.pixelSize() > 0) {
        const int px = int(std::lround(m_tickFont.pixelSize() * m_scaleFactor));
        // allow smaller than MIN_UI_FONT_PX for tick labels
        m_tickFont.setPixelSize(std::max<int>(9, px));
    }

    // Bold makes tiny ticks look oversized
    m_tickFont.setBold(false);
}

int PowerBarVertWidget::valueToPixelY(double v, int barTop, int barBottom, double range) const
{
    v = qBound(static_cast<double>(m_minPower), v, static_cast<double>(m_maxPower));
    if (range <= 0.0)
        return barBottom;

    const double norm = (v - m_minPower) / range; // 0..1
    // 0 -> bottom, 1 -> top
    const double y = barBottom - norm * (barBottom - barTop);
    return qRound(y);
}

QSize PowerBarVertWidget::sizeHint() const
{
    const int baseW = 40;
    const int baseH = 140; // Was 160; 320 gives the taller 2-row feel
    return QSize(qRound(baseW * m_scaleFactor), qRound(baseH * m_scaleFactor));
}

void PowerBarVertWidget::updateMinimumHeight()
{
    const int baseH = 140; // keep in sync with sizeHint()
    const int minH = qRound(baseH * m_scaleFactor);
    setMinimumHeight(minH);
}

void PowerBarVertWidget::updateLabelColorFromPalette()
{
    const QPalette pal = palette();

    // Label text (e.g. "Forward Power") tracks the theme text color
    m_labelTextColor = pal.color(QPalette::WindowText);

    // Decide dark vs light theme from the window background
    const QColor window = pal.color(QPalette::Window);
    const bool isDark = window.lightness() < 128;

    if (isDark) {
        // On dark background, bright yellow is fine
        m_yellowColor = Qt::yellow;
    } else {
        // Light theme: use DarkGoldenRod for better contrast
        m_yellowColor = QColor(QStringLiteral("#B8860B")); // Option A
    }

    update();
}

void PowerBarVertWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Let style draw background if needed
    QStyleOption opt;
    opt.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    const int w = width();
    const int h = height();

    if (w <= 10 || h <= 10)
        return;

    const int margin = qRound(8 * m_scaleFactor);
    const int innerLeft = margin;
    const int innerRight = w - margin;
    const int innerTop = margin;
    const int innerBottom = h - margin;

    QFontMetrics fm(font());
    QFontMetrics tickFm(m_tickFont);

    // Top label + numeric value line
    QString topLabel = m_labelText;
    QString topValue = QString::number(m_powerValue, 'f', m_powerPrecision) + m_powerUnits;

    // Choose dynamic color by thresholds
    QColor dynamicColor = m_valueTextColor;
    if (m_powerValue < m_yellowVal)
        dynamicColor = m_greenColor;
    else if (m_powerValue < m_redVal)
        dynamicColor = m_yellowColor;
    else
        dynamicColor = m_redColor;

    const QString topText = QStringLiteral("%1  %2").arg(topLabel, topValue);
    const int topTextWidth = fm.horizontalAdvance(topText);
    const int topTextX = innerLeft + (innerRight - innerLeft - topTextWidth) / 2;
    const int topTextY = innerTop + fm.ascent();

    // draw top label/value
    p.setPen(m_labelTextColor);
    p.drawText(topTextX, topTextY, topLabel);

    // value aligned just after label
    const int labelOnlyWidth = fm.horizontalAdvance(topLabel + "  ");
    p.setPen(dynamicColor);
    p.drawText(topTextX + labelOnlyWidth, topTextY, topValue);

    const int topAreaBottom = topTextY + fm.descent() + qRound(4 * m_scaleFactor);

    // Bottom area for right label/value + threshold labels
    // Only reserve ONE text line (Ref line) + small padding, so the bar can extend lower.
    const int bottomPad = qRound(2 * m_scaleFactor);
    const int bottomAreaHeight = fm.height() + bottomPad;
    const int bottomAreaTop = innerBottom - bottomAreaHeight;

    // Bar geometry: make bar narrower and centered
    const int availableWidth = innerRight - innerLeft;
    int barWidth = qMax(10, qRound(availableWidth * 0.45)); // ~45% of inner width
    if (barWidth > availableWidth)
        barWidth = availableWidth - qRound(4 * m_scaleFactor);

    const int barLeft = innerLeft + (availableWidth - barWidth) / 2;
    const int barRight = barLeft + barWidth;

    const int barTop = topAreaBottom + qRound(12 * m_scaleFactor);
    const int barBottom = bottomAreaTop - qRound(2 * m_scaleFactor);
    const int barHeight = barBottom - barTop;

    if (barHeight <= 0)
        return;

    const double range = static_cast<double>(m_maxPower - m_minPower);
    if (range <= 0.0)
        return;

    // ---- Draw bar track ----
    QRect barRect(barLeft, barTop, barWidth, barHeight);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::lightGray); // ← PERFECT MATCH to horizontal bar
    p.drawRect(barRect);

    // ---- Bar fill ----
    double clampedValue = qBound(static_cast<double>(m_minPower),
                                 m_powerValue,
                                 static_cast<double>(m_maxPower));
    int fillTopY = valueToPixelY(clampedValue, barTop, barBottom, range);

    QLinearGradient grad(barLeft, barBottom, barLeft, barTop);
    qreal stop1 = (m_yellowVal - m_minPower) / range;
    qreal stop2 = (m_redVal - m_minPower) / range;
    stop1 = qBound<qreal>(0.0, stop1, 1.0);
    stop2 = qBound<qreal>(0.0, stop2, 1.0);

    grad.setColorAt(0.0, m_greenColor);
    grad.setColorAt(stop1, m_greenColor);
    grad.setColorAt(stop1 + 0.02, m_yellowColor);
    grad.setColorAt(stop2, m_yellowColor);
    grad.setColorAt(stop2 + 0.02, m_redColor);
    grad.setColorAt(1.0, m_redColor);

    p.setBrush(grad);
    QRect fillRect(barLeft, fillTopY, barWidth, barBottom - fillTopY);
    p.drawRect(fillRect);

    // ---- Peak marker tip (purple “peak hold” line) ----
    {
        // Decide which value the tip should represent:
        //  - Live / None   -> current power value
        //  - Peak / Freeze -> held peak value
        double tipValue = m_powerValue;
        if (m_mode == MeterMode::Peak || m_mode == MeterMode::Freeze)
            tipValue = m_peakValue;

        double peakClamped = qBound(static_cast<double>(m_minPower),
                                    tipValue,
                                    static_cast<double>(m_maxPower));
        int peakY = valueToPixelY(peakClamped, barTop, barBottom, range);

        const int tipThickness = qMax(1, qRound(3 * m_scaleFactor));
        const QColor peakBarColor = QColor::fromRgb(248, 0, 248);

        p.setPen(QPen(peakBarColor, tipThickness));
        p.drawLine(barLeft + 2, peakY, barRight - 2, peakY);
    }

    // ---- Ticks + numeric labels on left side ----
    if (m_showTicks && m_majorTickStep > 0.0) {
        p.setPen(palette().mid().color());
        const int majorTickLen = qRound(6 * m_scaleFactor);
        const int minorTickLen = qRound(3 * m_scaleFactor);

        auto drawTick = [&](double v, bool major) {
            int y = valueToPixelY(v, barTop, barBottom, range);
            int len = major ? majorTickLen : minorTickLen;
            p.drawLine(barLeft - len, y, barLeft, y);
        };

        // Decide stepping:
        //  - If minor step > 0: use it, and every Nth tick is major
        //  - Else: only majors
        double step = (m_minorTickStep > 0.0 ? m_minorTickStep : m_majorTickStep);

        int minorPerMajor = 1;
        if (m_minorTickStep > 0.0 && m_majorTickStep > 0.0) {
            // e.g. major=50, minor=10 -> 5 minor ticks per major
            double ratio = m_majorTickStep / m_minorTickStep;
            minorPerMajor = qMax(1, qRound(ratio));
        }

        int tickIndex = 0;
        for (double v = m_minPower; v <= m_maxPower + 0.0001; v += step, ++tickIndex) {
            bool isMajor = (tickIndex % minorPerMajor == 0);

            drawTick(v, isMajor);

            // Numeric labels only on major ticks if requested
            if (isMajor && m_showTickLabels) {
                const QString lab = QString::number(int(v));

                const bool isYellowTick = qFuzzyCompare(v, m_yellowVal);
                const bool isRedTick = qFuzzyCompare(v, m_redVal);

                // Optionally hide the threshold *values* themselves
                if (!m_showThresholdLabels && (isYellowTick || isRedTick))
                    continue;

                // Use tick font ONLY for tick labels
                p.save();
                p.setFont(m_tickFont);

                // Recompute metrics for tick font
                QFontMetrics tfm(p.font());

                const int th = tfm.height();
                const int yCenter = valueToPixelY(v, barTop, barBottom, range);
                const int y = yCenter + th / 2 - tfm.descent();

                QRect textRect(innerLeft, y - th, barLeft - majorTickLen - innerLeft - 2, th);

                QColor c;
                if (v < m_yellowVal)
                    c = m_greenColor;
                else if (v < m_redVal)
                    c = m_yellowColor;
                else
                    c = m_redColor;

                p.setPen(c);
                p.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, lab);

                p.restore(); // restores UI font + pen
            }
        }
    }

    /*    // ---- Threshold labels ("Yel", "Red") near bar right side ----
    const bool drawYel = (m_yellowVal > m_minPower && m_yellowVal < m_maxPower);
    const bool drawRed = (m_redVal > m_minPower && m_redVal < m_maxPower);

    if (drawYel || drawRed) {
        const int labelX = barRight + qRound(4 * m_scaleFactor);
        if (drawYel) {
            int y = valueToPixelY(m_yellowVal, barTop, barBottom, range);
            QString ys = QStringLiteral("Yel");
            QRect r(labelX, y - fm.height(), fm.horizontalAdvance(ys), fm.height());
            p.setPen(m_yellowColor);
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, ys);
        }
        if (drawRed) {
            int y = valueToPixelY(m_redVal, barTop, barBottom, range);
            QString rs = QStringLiteral("Red");
            QRect r(labelX, y - fm.height(), fm.horizontalAdvance(rs), fm.height());
            p.setPen(m_redColor);
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, rs);
        }
    }
*/
    // ---- Bottom: right label + value (e.g., Ref (W)) ----
    if (!m_rightLabelText.isEmpty() && !m_rightValueText.isEmpty()) {
        QString bottomStr = QStringLiteral("%1 %2").arg(m_rightLabelText, m_rightValueText)
                            + m_rightUnits;

        int textWidth = fm.horizontalAdvance(bottomStr);
        int x = innerLeft + (innerRight - innerLeft - textWidth) / 2;
        int y = innerBottom - fm.descent(); // keep text a bit above the edge

        p.setPen(m_labelTextColor);
        p.drawText(x, y, m_rightLabelText);

        int labelWidth = fm.horizontalAdvance(m_rightLabelText + " ");
        // Use the same dynamic (green / yellow / red) color as the top value
        p.setPen(dynamicColor);
        p.drawText(x + labelWidth, y, m_rightValueText + m_rightUnits);
    }
}

void PowerBarVertWidget::setPeakHoldProfileScaled(int liveHoldMs,
                                                  double liveDecayFracPerSec,
                                                  int peakHoldMs,
                                                  double freezeDecayFracPerSec)
{
    m_peakHoldLiveHoldMs = liveHoldMs;
    m_peakHoldLiveDecayFracPerSec = liveDecayFracPerSec;
    m_peakHoldHoldMs = peakHoldMs;
    m_peakHoldFreezeDecayFracPerSec = freezeDecayFracPerSec;
}
