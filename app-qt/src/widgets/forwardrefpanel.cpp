// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "forwardrefpanel.h"
#include "common/Constants.h"

#include <QGridLayout>
#include <QLabel>
#include <algorithm>

namespace raypm {

ForwardRefPanel::ForwardRefPanel(QWidget *parent)
    : WidgetBase(parent)
{
    setObjectName(QStringLiteral("ForwardRefPanel"));

    auto *layout = new QGridLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(0);

    m_lblForwardLabel = new QLabel(tr("Forward (W):"), this);
    m_lblRefLabel = new QLabel(tr("Reflected (W):"), this);
    m_lblSwrLabel = new QLabel(tr("SWR:"), this);

    auto *lblForward = m_lblForwardLabel;
    auto *lblRef = m_lblRefLabel;
    auto *lblSwr = m_lblSwrLabel;

    m_lblForwardValue = new QLabel(QStringLiteral("0.0"), this);
    m_lblRefValue = new QLabel(QStringLiteral("0.0"), this);
    m_lblSwrValue = new QLabel(QStringLiteral("1.00"), this);

    // Make values stand out a bit
    QFont valueFont = m_lblForwardValue->font();
    valueFont.setPointSize(valueFont.pointSize() + 1);
    valueFont.setBold(true);

    m_lblForwardValue->setFont(valueFont);
    m_lblRefValue->setFont(valueFont);
    m_lblSwrValue->setFont(valueFont);

    layout->addWidget(lblForward, 0, 0);
    layout->addWidget(m_lblForwardValue, 0, 1);
    layout->addWidget(lblRef, 1, 0);
    layout->addWidget(m_lblRefValue, 1, 1);
    layout->addWidget(lblSwr, 2, 0);
    layout->addWidget(m_lblSwrValue, 2, 1);

    setLayout(layout);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // ---- scaling baseline + keepFont flags ----
    m_baseLabelFont = lblForward->font();
    m_baseValueFont = valueFont;

    // Opt all panel labels out of Helper::ApplyUserScaleFactor;
    // we’ll scale them manually in setScaleFactor().
    lblForward->setProperty("keepFont", true);
    lblRef->setProperty("keepFont", true);
    lblSwr->setProperty("keepFont", true);
    m_lblForwardValue->setProperty("keepFont", true);
    m_lblRefValue->setProperty("keepFont", true);
    m_lblSwrValue->setProperty("keepFont", true);
}

int ForwardRefPanel::compactHeightForFonts(const QFont &labelFont, const QFont &valueFont) const
{
    const QFontMetrics fmLabel(labelFont);
    const QFontMetrics fmValue(valueFont);

    const int rowH = qMax(fmLabel.height(), fmValue.height());
    constexpr int rows = 3;

    int vSpacing = 0;
    QMargins margins;

    if (auto *grid = qobject_cast<QGridLayout *>(layout())) {
        vSpacing = qMax(0, grid->verticalSpacing());
        margins = grid->contentsMargins();
    }

    // Small safety pad so descenders don’t clip depending on font/AA/hinting
    const int safetyPad = 2;

    return margins.top() + margins.bottom() + (rows * rowH) + ((rows - 1) * vSpacing) + safetyPad;
}

QSize ForwardRefPanel::sizeHint() const
{
    const QFont labelFont = m_lblForwardLabel ? m_lblForwardLabel->font() : font();
    const QFont valueFont = m_lblForwardValue ? m_lblForwardValue->font() : font();

    const int h = compactHeightForFonts(labelFont, valueFont);
    return QSize(140, h);
}

QSize ForwardRefPanel::minimumSizeHint() const
{
    const QFont labelFont = m_lblForwardLabel ? m_lblForwardLabel->font() : font();
    const QFont valueFont = m_lblForwardValue ? m_lblForwardValue->font() : font();

    const int h = compactHeightForFonts(labelFont, valueFont);
    return QSize(80, h);
}

void raypm::ForwardRefPanel::onAppScaleFactorChanged(double scale)
{
    setScaleFactor(scale);
}

void ForwardRefPanel::setValues(double forwardW, double reflectedW, double swr)
{
    m_lblForwardValue->setText(QString::number(forwardW, 'f', 1));
    m_lblRefValue->setText(QString::number(reflectedW, 'f', 1));
    m_lblSwrValue->setText(QString::number(swr, 'f', 2));

    // Optional SWR color cue
    if (swr > 3.0) {
        m_lblSwrValue->setStyleSheet("color: red;");
    } else if (swr > 2.0) {
        m_lblSwrValue->setStyleSheet("color: orange;");
    } else {
        m_lblSwrValue->setStyleSheet(""); // default palette
    }
}

void ForwardRefPanel::setScaleFactor(double scale)
{
    if (scale <= 0.0)
        scale = 1.0;

    const double minPt = MIN_UI_FONT_PT;
    const int minPx = MIN_UI_FONT_PX;

    // Scale caption labels
    QFont labelFont = m_baseLabelFont;
    if (labelFont.pointSizeF() > 0) {
        labelFont.setPointSizeF(std::max(minPt, m_baseLabelFont.pointSizeF() * scale));
    } else if (labelFont.pixelSize() > 0) {
        labelFont.setPixelSize(std::max(minPx, int(m_baseLabelFont.pixelSize() * scale)));
    }

    // Scale numeric values a little more aggressively so they pop
    QFont valueFont = m_baseValueFont;
    const double valueScale = scale * 1.1;
    if (valueFont.pointSizeF() > 0) {
        valueFont.setPointSizeF(std::max(minPt, m_baseValueFont.pointSizeF() * valueScale));
    } else if (valueFont.pixelSize() > 0) {
        valueFont.setPixelSize(std::max(minPx, int(m_baseValueFont.pixelSize() * valueScale)));
    }

    if (m_lblForwardLabel)
        m_lblForwardLabel->setFont(labelFont);
    if (m_lblRefLabel)
        m_lblRefLabel->setFont(labelFont);
    if (m_lblSwrLabel)
        m_lblSwrLabel->setFont(labelFont);

    m_lblForwardValue->setFont(valueFont);
    m_lblRefValue->setFont(valueFont);
    m_lblSwrValue->setFont(valueFont);

    // Scale margins/spacing (keep rows tighter than default)
    auto *grid = qobject_cast<QGridLayout *>(layout());
    if (grid) {
        // Keep the original style: small left/right padding, no top/bottom padding
        const int lr = std::clamp(int(std::lround(4.0 * scale)), 2, 12);
        grid->setContentsMargins(lr, 0, lr, 0);

        // Column gap can be modest so labels/values breathe horizontally
        const int hsp = std::clamp(int(std::lround(6.0 * scale)), 2, 16);
        grid->setHorizontalSpacing(hsp);

        // Row gap should be small (this is the one you care about)
        const int vsp = std::clamp(int(std::lround(1.0 * scale)), 0, 8);
        grid->setVerticalSpacing(vsp);
    }

    updateGeometry();
    update();
}

} // namespace raypm
