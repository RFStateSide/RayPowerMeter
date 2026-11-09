// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "auxvoltagewidget.h"
#include "common/Constants.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace raypm {

AuxVoltageWidget::AuxVoltageWidget(QWidget *parent)
    : WidgetBase(parent)
{
    // --- Basic visual frame ---
    setObjectName(QStringLiteral("AuxVoltageWidget"));
    setAutoFillBackground(false);

    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(4, 1, 4, 1);
    rootLayout->setSpacing(4);

    // Title row (uses WidgetBase::label by default)
    m_titleLabel = new QLabel(this);
    //QFont titleFont = m_titleLabel->font();
    //titleFont.setBold(true);
    //m_titleLabel->setFont(titleFont);
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Value row
    m_valueLabel = new QLabel(this);
    QFont valueFont = m_valueLabel->font();
    valueFont.setPointSize(valueFont.pointSize() + 1);
    valueFont.setBold(true);
    m_valueLabel->setFont(valueFont);
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_unitsLabel = new QLabel(this);
    m_unitsLabel->setFont(valueFont); // same style as value
    m_unitsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_unitsLabel->setText(tr("V"));

    m_baseCaptionFont = m_titleLabel->font();
    m_baseValueFont = m_valueLabel->font();
    m_scaleInitialized = true;

    // Layout: title above, value below
    rootLayout->addWidget(m_titleLabel);
    rootLayout->addWidget(m_valueLabel);
    rootLayout->addWidget(m_unitsLabel);

    setLayout(rootLayout);

    // Reasonable defaults
    setLabel(tr("Voltage"));           // WidgetBase label
    setSource(QStringLiteral("Aux0")); // default source; user can override in Properties
    setVoltage(0.0);

    // Keep title synced with WidgetBase::label
    connect(this, &WidgetBase::labelChanged, this, &AuxVoltageWidget::onLabelChanged);

    updateTitleFromLabel();
}

void AuxVoltageWidget::onAppScaleFactorChanged(double scale)
{
    if (!m_scaleInitialized)
        return;

    if (scale <= 0.0)
        scale = 1.0;

    auto scaleFont = [scale](const QFont &base) {
        QFont f = base;
        if (base.pointSizeF() > 0)
            f.setPointSizeF(std::max(MIN_UI_FONT_PT, base.pointSizeF() * scale));
        else if (base.pixelSize() > 0)
            f.setPixelSize(std::max(MIN_UI_FONT_PX, base.pixelSize() * scale));
        return f;
    };

    m_titleLabel->setFont(scaleFont(m_baseCaptionFont));
    m_valueLabel->setFont(scaleFont(m_baseValueFont));
}

void AuxVoltageWidget::setVoltage(double volts)
{
    // Value only; units label handles “V”
    m_valueLabel->setText(tr("%1").arg(volts, 0, 'f', 2));

    if (m_unitsLabel)
        m_unitsLabel->setText(tr("V")); // keep explicit for future unit changes
}

void AuxVoltageWidget::onLabelChanged(const QString &label)
{
    Q_UNUSED(label);
    updateTitleFromLabel();
}

void AuxVoltageWidget::updateTitleFromLabel()
{
    if (!m_titleLabel)
        return;

    QString text = label();
    if (text.isEmpty())
        text = tr("Voltage");

    m_titleLabel->setText(text);
}

} // namespace raypm
