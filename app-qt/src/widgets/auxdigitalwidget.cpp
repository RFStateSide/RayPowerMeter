// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "widgets/auxdigitalwidget.h"

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QStyle>

namespace raypm {

AuxDigitalWidget::AuxDigitalWidget(QWidget *parent)
    : WidgetBase(parent)
{
    setObjectName(QStringLiteral("AuxDigitalWidget"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(6);

    m_label = new QLabel(this);
    m_label->setText(tr("Aux Digital"));
    layout->addWidget(m_label, /*stretch=*/1);

    // NEW: mode text, e.g. "Disabled / Input / Output"
    m_modeLabel = new QLabel(this);
    m_modeLabel->setMinimumWidth(60);
    m_modeLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_modeLabel, /*stretch=*/0, Qt::AlignVCenter);

    // Small "LED" indicator for input/disabled
    m_ledWidget = new QWidget(this);
    m_ledWidget->setFixedSize(14, 14);
    m_ledWidget->setAutoFillBackground(true);
    layout->addWidget(m_ledWidget, /*stretch=*/0, Qt::AlignRight | Qt::AlignVCenter);

    // Checkbox for Output mode
    m_outputCheck = new QCheckBox(tr("On"), this);
    layout->addWidget(m_outputCheck, /*stretch=*/0, Qt::AlignRight | Qt::AlignVCenter);

    layout->addStretch(0);

    // Default to Disabled visuals
    updateVisuals();

    // Keep label in sync with WidgetBase's label property
    connect(this, &WidgetBase::labelChanged, this, &AuxDigitalWidget::onLabelChanged);

    connect(m_outputCheck, &QCheckBox::toggled, this, &AuxDigitalWidget::onCheckboxToggled);

    // Apply current base label and visuals
    onLabelChanged(label());
    updateVisuals();
}

void AuxDigitalWidget::setMode(AuxDigitalWidget::Mode mode)
{
    qDebug() << "[AuxDigitalWidget] setMode request" << int(mode) << "current" << int(m_mode)
             << "widgetId" << widgetId() << "widgetName" << widgetName() << "objectName"
             << objectName();

    if (m_mode == mode)
        return;

    m_mode = mode;
    updateVisuals();
    emit modeChanged(m_mode);
}

void AuxDigitalWidget::setValue(bool value)
{
    if (m_value == value)
        return;

    m_value = value;

    // Update checkbox without re-emitting
    {
        QSignalBlocker b(m_outputCheck);
        m_outputCheck->setChecked(m_value);
    }

    updateVisuals();
    emit valueChanged(m_value);
}

void AuxDigitalWidget::onLabelChanged(const QString &label)
{
    if (!m_label)
        return;

    if (label.isEmpty())
        m_label->setText(tr("Aux Digital"));
    else
        m_label->setText(label);
}

void AuxDigitalWidget::onCheckboxToggled(bool checked)
{
    if (m_mode != Mode::Output)
        return;

    if (m_value == checked)
        return;

    m_value = checked;
    updateVisuals();
    emit valueChanged(m_value);
    emit outputValueChanged(m_value);
}

void AuxDigitalWidget::updateVisuals()
{
    // Mode text
    if (m_modeLabel) {
        QString modeText;
        switch (m_mode) {
        case Mode::Disabled:
            modeText = tr("Disabled");
            break;
        case Mode::Input:
            modeText = tr("Input");
            break;
        case Mode::Output:
            modeText = tr("Output");
            break;
        }
        m_modeLabel->setText(modeText);
    }

    // LED color & visibility
    if (m_ledWidget) {
        QColor color;

        switch (m_mode) {
        case Mode::Disabled:
            color = QColor(80, 80, 80); // dark grey
            break;
        case Mode::Input:
            color = m_value ? QColor(0, 200, 0)   // green
                            : QColor(80, 80, 80); // grey
            break;
        case Mode::Output:
            // Output mode: we don't really need LED, but keep behaviour simple:
            color = m_value ? QColor(0, 200, 0) : QColor(80, 80, 80);
            break;
        }

        QPalette pal = m_ledWidget->palette();
        pal.setColor(QPalette::Window, color);
        m_ledWidget->setPalette(pal);

        // LED only for Disabled / Input; hide for Output
        m_ledWidget->setVisible(m_mode != Mode::Output);
    }

    // Checkbox visibility / enabled in Output mode only
    if (m_outputCheck) {
        const bool outputMode = (m_mode == Mode::Output);
        m_outputCheck->setVisible(outputMode);
        m_outputCheck->setEnabled(outputMode);
    }

    qDebug() << "[AuxDigitalWidget] updateVisuals mode" << int(m_mode) << "value" << m_value
             << "widgetId" << widgetId() << "widgetName" << widgetName() << "objectName"
             << objectName();
}

} // namespace raypm
