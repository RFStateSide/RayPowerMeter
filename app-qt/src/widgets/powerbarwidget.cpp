// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "powerbarwidget.h"
#include "common/Constants.h"

#include <QEvent>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>
#include <algorithm>

PowerBarWidget::PowerBarWidget(QWidget *parent)
    : WidgetBase(parent)
    , powerValue(0)
    , maxPower(1800)
    , labelText("Power")
    , yellowVal(1200)
    , redVal(1500)
    , greenColor(Qt::green)
    , yellowColor(Qt::yellow)
    , redColor(Qt::red)
    , valueTextColor(Qt::green)
    , showDynamicTextColor(false)
{
    setWidgetName(QStringLiteral("PowerBar"));

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    //setMinimumHeight(40);
    //setMaximumHeight(100);

    rightLabelText = "";
    rightValueText = "";

    peakValue = 0;
    peakHoldExpired = false;

    // Use configurable hold
    peakHoldTimer.setInterval(peakHoldMs);
    peakHoldTimer.setSingleShot(true);
    connect(&peakHoldTimer, &QTimer::timeout, this, [this]() {
        peakHoldExpired = true;
        peakDecayClock.restart(); // start timing decay window
        update();                 // allow decay after timeout
    });

    // Initialize label color from current theme/palette
    updateLabelColorFromPalette();

    // Capture an unscaled base font size ONCE at construction time.
    // This prevents "learning" the base while the app is temporarily scaled (e.g. 50%),
    // which causes wrong sizes when toggling back to 100%.
    if (m_basePointSize <= 0.0) {
        const double curPt = font().pointSizeF();
        const double denom = (m_scaleFactor > 0.0
                                  ? m_scaleFactor
                                  : 1.0); // whatever the widget currently thinks it is
        if (curPt > 0.0)
            m_basePointSize = curPt / denom;
        else
            m_basePointSize = BASE_UI_FONT_PT; // safe fallback from Constants.h
    }
}

QSize PowerBarWidget::sizeHint() const
{
    int baseWidth = 300;  // Adjust to your preferred default width
    int baseHeight = 100; // Normal height before scaling
    return QSize(static_cast<int>(baseWidth * m_scaleFactor),
                 static_cast<int>(baseHeight * m_scaleFactor));
}

// ---------------------------
// Meter mode control methods
// ---------------------------
void PowerBarWidget::setMeterMode(MeterMode m)
{
    mode_ = m;
    switch (mode_) {
    case MeterMode::Live:
        // Resume normal peak-hold behavior (standard profile)
        peakHoldTimer.setInterval(peakHoldMs);
        if (!peakHoldTimer.isActive())
            peakHoldTimer.start();
        break;
    case MeterMode::Peak:
        // Slower-decay profile: keep timer active so we still decay after a longer hold
        peakHoldTimer.setInterval(peakHoldMsPeak);
        if (!peakHoldTimer.isActive())
            peakHoldTimer.start();
        break;
    case MeterMode::Freeze:
        // Stop decaying and lock the current peak
        if (peakHoldTimer.isActive())
            peakHoldTimer.stop();
        peakValue = std::max(peakValue, displayedValue);
        break;
    case MeterMode::None:
        // No hold/decay; marker just follows the bar tip.
        if (peakHoldTimer.isActive())
            peakHoldTimer.stop();
        peakValue = displayedValue; // ensure marker sits at tip immediately
        break;
    }
    update();
}

void PowerBarWidget::clearPeak()
{
    // Reset peak marker/value to current displayed value.
    // Do NOT force a mode change; user may be in Freeze and just wants to clear the held tip.
    peakValue = displayedValue;
    peakHoldExpired = false;
    peakDecayClock.restart();

    // Restart the hold timer only for modes that decay
    if (mode_ == MeterMode::Live) {
        peakHoldTimer.start(peakHoldMs);
    } else if (mode_ == MeterMode::Peak) {
        peakHoldTimer.start(peakHoldMsPeak);
    } else {
        // Freeze/None => no timer
        if (peakHoldTimer.isActive())
            peakHoldTimer.stop();
    }

    update();
}

void PowerBarWidget::setPowerValue(double value)
{
    powerValue = value;

    // Helper to (re)start the hold timer according to current mode
    auto startHoldForMode = [this] {
        if (mode_ == MeterMode::Live) {
            peakHoldTimer.setInterval(peakHoldMs);
            peakHoldTimer.start();
        } else if (mode_ == MeterMode::Peak) {
            peakHoldTimer.setInterval(peakHoldMsPeak);
            peakHoldTimer.start();
        }
        // Freeze: no timer
    };

    if (std::isinf(value) && value < 0.0) {
        // Handle -INFINITY for dBm case
        peakValue = 0.0;
        peakHoldExpired = true;
        if (mode_ != MeterMode::None)
            startHoldForMode();
        peakDecayClock.restart();

        displayedValue = -INFINITY;
        displayUpdateTimer.restart();
        update();
        return;
    }

    if (value <= 0.0) {
        // This should only happen if you're working in Watts, not dBm
        peakValue = 0.0;
        peakHoldExpired = true;
        if (mode_ != MeterMode::None)
            startHoldForMode();
        peakDecayClock.restart();

        displayedValue = 0.0;
        displayUpdateTimer.restart();
        update();
        return;
    }

    if (value >= peakValue) {
        peakValue = value;
        peakHoldExpired = false;
        if (mode_ != MeterMode::None)
            startHoldForMode();
        // reset decay timing; new peak just captured
        peakDecayClock.invalidate();
    } else if (peakHoldExpired) {
        // Scale-aware decay toward current value (disabled in None):
        if (mode_ == MeterMode::None) {
            peakValue = displayedValue; // marker hugs the tip
        } else {
            const double range = static_cast<double>(maxPower - minPower);
            double dtSec = 0.0;
            if (!peakDecayClock.isValid()) {
                peakDecayClock.start();
                dtSec = 0.0;
            } else {
                dtSec = std::max(0.0, peakDecayClock.elapsed() / 1000.0);
                peakDecayClock.restart();
            }
            double frac = 0.0;
            if (mode_ == MeterMode::Live)
                frac = liveDecayFracPerSec;
            else if (mode_ == MeterMode::Peak)
                frac = peakDecayFracPerSec;
            // Freeze => frac remains 0

            if (frac > 0.0 && dtSec > 0.0) {
                const double step = range * frac * dtSec;
                peakValue = qMax(value, peakValue - step);
            }
        }
    }

    // --- Always refresh displayedValue on its cadence (independent of peak hold/decay) ---
    if (!displayUpdateTimer.isValid() || displayUpdateTimer.elapsed() >= displayUpdateIntervalMs) {
        displayedValue = value;
        displayUpdateTimer.restart();
    }

    // In None mode, keep the marker glued to the tip every update
    if (mode_ == MeterMode::None) {
        peakValue = displayedValue;
    }

    update();
}

void PowerBarWidget::setLabelText(const QString &text)
{
    labelText = text;
    update();
}

void PowerBarWidget::setLabelTextColor(const QColor &color)
{
    labelTextColor = color;
    update();
}

void PowerBarWidget::setThresholds(int yellow, int red)
{
    yellowVal = yellow;
    redVal = red;
    update();
}

void PowerBarWidget::setValueColor(const QColor &color)
{
    valueTextColor = color;
    update();
}

void PowerBarWidget::setRightLabelText(const QString &text)
{
    rightLabelText = text;
    update();
}

void PowerBarWidget::setRightValueText(const QString &text)
{
    rightValueText = text;
    update();
}

void PowerBarWidget::setRightValue(double value)
{
    rightValueRaw = value;

    if (!rightValueUpdateTimer.isValid()) {
        rightValueUpdateTimer.start();
        rightValueDisplayed = value;
        rightValueText = QString::number(rightValueDisplayed, 'f', rightValuePrecision)
                         + rightValueUnits;
        update();
        return;
    }

    if (rightValueUpdateTimer.elapsed() >= rightValueUpdateIntervalMs) {
        rightValueDisplayed = value;
        rightValueText = QString::number(rightValueDisplayed, 'f', rightValuePrecision)
                         + rightValueUnits;
        rightValueUpdateTimer.restart();
        update();
    }
}

void PowerBarWidget::setScaleFactor(qreal scale)
{
    if (scale <= 0.0)
        scale = 1.0;

    if (qFuzzyCompare(m_scaleFactor, scale))
        return;

    m_scaleFactor = scale;

    // ---- UI font (top/bottom text) ----
    QFont uiFont = font();
    if (uiFont.pointSizeF() > 0.0) {
        const double targetPt = std::max<double>(MIN_UI_FONT_PT, m_basePointSize * m_scaleFactor);
        uiFont.setPointSizeF(targetPt);
    } else if (uiFont.pixelSize() > 0) {
        const int px = int(std::lround(uiFont.pixelSize() * m_scaleFactor));
        uiFont.setPixelSize(std::max<int>(MIN_UI_FONT_PX, px));
    }
    uiFont.setBold(true);
    setFont(uiFont);

    // ---- Tick font (tick labels only) ----
    m_tickFont = uiFont;
    if (m_tickFont.pointSizeF() > 0.0) {
        const double tickPt = std::max<double>(MIN_TICK_FONT_PT, m_basePointSize * m_scaleFactor);
        m_tickFont.setPointSizeF(tickPt);
    }
    m_tickFont.setBold(false);

    updateGeometry();
    updateMinimumHeight();
    update();
}

void PowerBarWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event && event->button() == Qt::LeftButton) {
        clearPeak();
        event->accept();
        return;
    }
    WidgetBase::mouseDoubleClickEvent(event);
}

void PowerBarWidget::updateMinimumHeight()
{
    const int baseMinHeight = 90;  // base height to fit top text, bar, and bottom labels
    const int baseMaxHeight = 160; // a bit larger ceiling so 2.0 scale looks noticeably bigger

    const int minH = static_cast<int>(baseMinHeight * m_scaleFactor);
    const int maxH = static_cast<int>(baseMaxHeight * m_scaleFactor);

    setMinimumHeight(minH);
    setMaximumHeight(maxH);
}

// ----- New public APIs implementations -----

void PowerBarWidget::setTickConfig(double majorStep, double minorStep, bool labels)
{
    majorTickStep = std::max(0.0, majorStep);
    minorTickStep = std::max(0.0, minorStep);
    showTickLabels = labels;
    update();
}

void PowerBarWidget::setPeakStyle(int widthPx, qreal alpha)
{
    peakWidthPx = std::max(1, widthPx);
    peakAlpha = std::clamp(alpha, 0.0, 1.0);
    update();
}

void PowerBarWidget::setPeakHold(int holdMs, int decayMs, double decayStep)
{
    peakHoldMs = std::max(50, holdMs);
    peakDecayMs = std::max(50, decayMs);
    peakDecayStep = std::max(0.1, decayStep);
    peakHoldTimer.setInterval(peakHoldMs);
}

// Map a value to pixel X within the bar
int PowerBarWidget::valueToPixelX(double v, int margin, int totalWidth, double range) const
{
    v = qBound(static_cast<double>(minPower), v, static_cast<double>(maxPower));
    const double norm = (v - minPower) / range;
    return margin + static_cast<int>(norm * totalWidth);
}

void PowerBarWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int baseMargin = 10;
    const int baseSpacing = 5;

    int margin = static_cast<int>(baseMargin * m_scaleFactor);
    int spacing = std::max(static_cast<int>(baseSpacing * m_scaleFactor), 2);

    int totalWidth = width() - 2 * margin;
    int barHeight = static_cast<int>((height() / 3) * m_scaleFactor);

    // Use the widget font set by setScaleFactor()
    painter.setFont(font());
    QFontMetrics fm(painter.font());

    int topTextHeight = fm.height();
    int spacingAboveBar = spacing;
    int spacingBelowBar = spacing;

    int barY = margin + topTextHeight + spacingAboveBar;

    // ==== Top-left label + value ====
    QString leftLabel = QString("%1 ").arg(labelText);

    const bool showPeakNumber = (mode_ == MeterMode::Peak || mode_ == MeterMode::Freeze);
    const double numberForText = showPeakNumber ? peakValue : displayedValue;

    QString leftValue;
    if (std::isinf(numberForText) && numberForText < 0.0) {
        leftValue = "-∞" + powerValueUnits;
    } else {
        leftValue = QString::number(numberForText, 'f', powerValuePercision) + powerValueUnits;
    }

    int leftLabelWidth = fm.horizontalAdvance(leftLabel);
    int leftX = margin;
    int textY = margin + fm.ascent();

    painter.setPen(labelTextColor);
    painter.drawText(leftX, textY, leftLabel);

    if (showDynamicTextColor) {
        if (powerValue < yellowVal)
            painter.setPen(greenColor);
        else if (powerValue < redVal)
            painter.setPen(yellowColor);
        else
            painter.setPen(redColor);
    } else {
        painter.setPen(valueTextColor);
    }
    painter.drawText(leftX + leftLabelWidth, textY, leftValue);

    // ==== Top-right label + value ====
    if (!rightLabelText.isEmpty() && !rightValueText.isEmpty()) {
        QString rightStr = QString("%1 %2").arg(rightLabelText, rightValueText) + rightValueUnits;
        int rightWidth = fm.horizontalAdvance(rightStr);
        int rightX = width() - margin - rightWidth;

        painter.setPen(labelTextColor);
        painter.drawText(rightX, textY, rightLabelText);

        int labelOnlyWidth = fm.horizontalAdvance(rightLabelText + " ");
        painter.setPen(valueTextColor);
        painter.drawText(rightX + labelOnlyWidth, textY, rightValueText);
    }

    // ==== Bar background ====
    QRect barRect(margin, barY, totalWidth, barHeight);
    painter.setBrush(Qt::lightGray);
    painter.setPen(Qt::NoPen);
    painter.drawRect(barRect);

    // ==== Bar fill ====
    double clampedValue = qBound(static_cast<double>(minPower),
                                 displayedValue,
                                 static_cast<double>(maxPower));
    double range = static_cast<double>(maxPower - minPower);
    int fillWidth = 0;

    if (!(std::isinf(displayedValue) && displayedValue < 0.0)) {
        double normalized = (clampedValue - minPower) / range;
        fillWidth = static_cast<int>(normalized * totalWidth);
    }

    QLinearGradient gradient(margin, barY, margin + totalWidth, barY);
    qreal stop1 = (yellowVal - minPower) / range;
    qreal stop2 = (redVal - minPower) / range;
    qreal delta = 0.02;

    // Clamp stops into [0, 1] to avoid QGradient warnings
    stop1 = qBound<qreal>(0.0, stop1, 1.0);
    stop2 = qBound<qreal>(0.0, stop2, 1.0);

    gradient.setColorAt(0.0, greenColor);
    gradient.setColorAt(qMax<qreal>(0.0, stop1 - delta), greenColor);
    gradient.setColorAt(stop1, yellowColor);
    gradient.setColorAt(qMin<qreal>(1.0, stop1 + delta), yellowColor);
    gradient.setColorAt(qMax<qreal>(0.0, stop2 - delta), yellowColor);
    gradient.setColorAt(stop2, redColor);
    gradient.setColorAt(1.0, redColor);

    painter.setBrush(gradient);
    painter.drawRect(margin, barY, fillWidth, barHeight);

    // ==== Peak marker ====
    double adjustedPeakValue = std::isinf(peakValue) && peakValue < 0.0
                                   ? static_cast<double>(minPower)
                                   : qBound(static_cast<double>(minPower),
                                            peakValue,
                                            static_cast<double>(maxPower));

    double normalizedPeak = (adjustedPeakValue - minPower) / range;
    int peakX = margin + static_cast<int>(normalizedPeak * totalWidth);

    if (normalizedPeak >= 0.0 && normalizedPeak <= 1.0) {
        QColor pc = peakBarColor;
        pc.setAlphaF(peakAlpha);
        QPen peakPen(pc);
        peakPen.setStyle(Qt::SolidLine);
        peakPen.setWidthF(std::max(1.0, peakWidthPx * m_scaleFactor));
        painter.setPen(peakPen);
        int offset = qRound(2.0 * m_scaleFactor);
        painter.drawLine(QPointF(peakX, barY + offset), QPointF(peakX, barY + barHeight - offset));
    }

    // ==== Bottom scale: ticks + labels + threshold labels ====
    int bottomY = barY + barHeight + spacingBelowBar + fm.height() - fm.descent() / 2;
    QFontMetrics tickFm(m_tickFont);

    // Minor ticks (unchanged)
    if (minorTickStep > 0.0 && showTicks) {
        QColor minor = palette().mid().color();
        painter.setPen(QPen(minor, 1));
        for (double v = minPower; v <= maxPower + 1e-6; v += minorTickStep) {
            int x = valueToPixelX(v, margin, totalWidth, range);
            painter.drawLine(x,
                             barY + barHeight + 2,
                             x,
                             barY + barHeight + std::max(4, int(6 * m_scaleFactor)));
        }
    }

    // ---- Helper to build a clamped text rect centered on X ----
    auto makeCenteredTickTextRect = [&](int centerX, const QString &text) {
        const int w = tickFm.horizontalAdvance(text);
        const int h = tickFm.height();
        int left = std::clamp(centerX - w / 2, margin, width() - margin - w);
        return QRect(left, bottomY - h + tickFm.descent(), w, h);
    };

    // ---- Threshold rects with collision handling (prefer red over yellow) ----
    const QString yellowStr = QString::number(yellowVal);
    const QString redStr = QString::number(redVal);
    const int yellowX = margin + int(((yellowVal - minPower) / range) * totalWidth);
    const int redX = margin + int(((redVal - minPower) / range) * totalWidth);

    QRect yRect = makeCenteredTickTextRect(yellowX, yellowStr);
    QRect rRect = makeCenteredTickTextRect(redX, redStr);

    bool drawYellow = true, drawRed = true;
    const int minThresholdSpacingPx = std::max(4, int(8 * m_scaleFactor));
    if (yRect.intersects(rRect) || (rRect.left() - yRect.right()) < minThresholdSpacingPx) {
        drawYellow = false; // hide the first threshold if they collide
    }

    QVector<QRect> reservedRects;
    if (drawYellow)
        reservedRects << yRect;
    if (drawRed)
        reservedRects << rRect;

    // ---- Major ticks + labels with buffering (so we can drop '90' if '100' collides) ----
    if (majorTickStep > 0.0) {
        QColor major = palette().brightText().color();
        major.setAlpha(220);
        const int tickH = std::max(8, int(10 * m_scaleFactor));
        const int minSpacingPx = std::max(4, int(10 * m_scaleFactor));

        struct PendingLabel
        {
            QRect rect;
            QString text;
            QColor color;
            bool isLast;
        };
        QVector<PendingLabel> pending;

        for (double v = minPower; v <= maxPower + 1e-6; v += majorTickStep) {
            const int x = valueToPixelX(v, margin, totalWidth, range);

            if (showTicks) {
                painter.setPen(QPen(major, 1));
                painter.drawLine(x, barY + barHeight + 2, x, barY + barHeight + tickH);
            }
            if (!showTickLabels)
                continue;

            // label rect (clamped)
            QString lab = QString::number(int(v));
            const int w = tickFm.horizontalAdvance(lab);
            int left = std::clamp(x - w / 2, margin, width() - margin - w);
            QRect labRect(left, bottomY - tickFm.height() + tickFm.descent(), w, tickFm.height());

            // color by threshold
            QColor labColor;
            if (v < yellowVal)
                labColor = greenColor;
            else if (v < redVal)
                labColor = yellowColor;
            else
                labColor = redColor;

            const bool isLastTick = (v + 0.5 * majorTickStep >= maxPower);

            // Check collisions with thresholds
            bool collidesThreshold = false;
            for (const QRect &r : reservedRects) {
                if (labRect.intersects(r)) {
                    collidesThreshold = true;
                    break;
                }
            }
            if (collidesThreshold && !isLastTick)
                continue; // skip normal labels that collide with thresholds

            // Check against previous label (spacing)
            if (!pending.isEmpty() && !isLastTick) {
                if (labRect.left() <= (pending.constLast().rect.right() + minSpacingPx))
                    continue; // skip if too close to previous
            }

            pending.push_back({labRect, lab, labColor, isLastTick});
        }

        // Ensure last label always visible: if it collides with previous, drop previous.
        if (!pending.isEmpty()) {
            PendingLabel &last = pending.last();
            if (last.isLast && pending.size() >= 2) {
                const PendingLabel &prev = pending.at(pending.size() - 2);
                const bool collidesPrev = last.rect.left()
                                          <= (prev.rect.right()
                                              + std::max(4, int(10 * m_scaleFactor)));
                if (collidesPrev) {
                    pending.removeAt(pending.size() - 2); // drop '90' to keep '100'
                }
            }
        }

        // Draw buffered labels
        for (const auto &pl : pending) {
            painter.save();
            painter.setFont(m_tickFont);
            painter.setPen(pl.color);
            painter.drawText(pl.rect.left(), bottomY, pl.text);
            painter.restore();
        }
    }

    if (!showTresholdLabels)
        return;

    // ---- Draw threshold labels (only those allowed) ----
    if (drawYellow) {
        painter.save();
        painter.setFont(m_tickFont);
        painter.setPen(yellowColor);
        painter.drawText(yRect.left(), bottomY, yellowStr);
        painter.restore();
    }
    if (drawRed) {
        painter.save();
        painter.setFont(m_tickFont);
        painter.setPen(redColor);
        painter.drawText(rRect.left(), bottomY, redStr);
        painter.restore();
    }
}

void PowerBarWidget::setOkWarnCritColors(const QColor &ok, const QColor &warn, const QColor &crit)
{
    greenColor = ok;
    yellowColor = warn;
    redColor = crit;
    update();
}

void PowerBarWidget::changeEvent(QEvent *event)
{
    // Let base class handle generic stuff
    WidgetBase::changeEvent(event);

    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange) {
        // Global theme/palette changed (Helper::ApplyAppTheme was called)
        updateLabelColorFromPalette();
    }

    if (event->type() == QEvent::FontChange) {
        m_basePointSize = 0.0;

        const double curPt = font().pointSizeF();
        const double denom = (m_scaleFactor > 0.0 ? m_scaleFactor : 1.0);
        if (curPt > 0.0)
            m_basePointSize = curPt / denom;
        else
            m_basePointSize = BASE_UI_FONT_PT;

        // Re-apply current scale to rebuild uiFont + tickFont with the new base
        setScaleFactor(m_scaleFactor);
    }
}

void PowerBarWidget::updateLabelColorFromPalette()
{
    const QPalette pal = palette();

    // Label text (e.g. "Forward Power") tracks the theme text color
    labelTextColor = pal.color(QPalette::WindowText);

    // Decide dark vs light theme from the window background
    const QColor window = pal.color(QPalette::Window);
    const bool isDark = window.lightness() < 128;

    if (isDark) {
        // On dark background, bright yellow is fine
        yellowColor = Qt::yellow;
    } else {
        // Light theme: use DarkGoldenRod for better contrast
        yellowColor = QColor(QStringLiteral("#B8860B")); // Option A
    }

    update();
}
