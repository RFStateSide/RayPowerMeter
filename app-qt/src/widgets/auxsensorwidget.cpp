// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "auxsensorwidget.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace raypm {

AuxSensorWidget::AuxSensorWidget(QWidget *parent)
    : WidgetBase(parent)
{
    setObjectName(QStringLiteral("AuxSensorWidget"));
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 1, 4, 1);
    outer->setSpacing(0);

    // Single horizontal row for: name | value | units
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("AuxSensorTitleLabel"));
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_valueLabel = new QLabel(this);
    QFont valueFont = m_valueLabel->font();
    valueFont.setPointSize(valueFont.pointSize() + 1);
    valueFont.setBold(true);
    m_valueLabel->setFont(valueFont);
    m_valueLabel->setObjectName(QStringLiteral("AuxSensorValueLabel"));
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_unitsLabel = new QLabel(this);
    m_unitsLabel->setFont(valueFont);
    m_unitsLabel->setObjectName(QStringLiteral("AuxSensorUnitsLabel"));
    m_unitsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    row->addWidget(m_titleLabel);
    row->addWidget(m_valueLabel);
    row->addWidget(m_unitsLabel);

    outer->addLayout(row);

    // React to label/appScaleFactor from WidgetBase
    connect(this, &WidgetBase::labelChanged, this, &AuxSensorWidget::onLabelChanged);

    /*   connect(this, &AuxSensorWidget::appScaleFactorChanged, this, [this](double scale) {
        QFont base = font();
        base.setPointSizeF(base.pointSizeF() * scale);

        // Title: slightly smaller
        QFont titleFont = base;
        titleFont.setPointSizeF(titleFont.pointSizeF() - 1.0);

        // Value: larger + bold
        QFont valueFont = base;
        valueFont.setPointSizeF(valueFont.pointSizeF() + 2.0);
        valueFont.setBold(true);

        // Units: a little larger + bold
        QFont unitsFont = base;
        unitsFont.setPointSizeF(unitsFont.pointSizeF() + 1.0);
        unitsFont.setBold(true);

        m_titleLabel->setFont(titleFont);
        m_valueLabel->setFont(valueFont);
        m_unitsLabel->setFont(unitsFont);
    });
*/
    updateTitle();
    updateUnitsLabel();

    setValue(std::numeric_limits<double>::quiet_NaN());
}

void AuxSensorWidget::setMode(Mode m)
{
    if (m_mode == m)
        return;

    m_mode = m;
    updateTitle();
    updateUnitsLabel();
}

void AuxSensorWidget::setTempUnitsIndex(int index)
{
    if (index < 0)
        index = 0;
    if (index > 1)
        index = 1;

    if (m_tempUnitsIndex == index)
        return;

    m_tempUnitsIndex = index;
    updateUnitsLabel();
    emit tempUnitsIndexChanged(index);
}

void AuxSensorWidget::setValue(double value)
{
    double displayValue = value;

    // Temperature is stored in °C; convert if user prefers °F
    if (m_mode == Mode::Temperature && !std::isnan(value)) {
        if (m_tempUnitsIndex == 1) { // Fahrenheit
            displayValue = value * 9.0 / 5.0 + 32.0;
        }
    }

    if (std::isnan(displayValue)) {
        m_valueLabel->setText(QStringLiteral("---"));
    } else {
        // Basic formatting; we can refine later per type if you want
        int decimals = (m_mode == Mode::Temperature) ? 1 : 2;
        m_valueLabel->setText(QString::number(displayValue, 'f', decimals));
    }
}

void AuxSensorWidget::onLabelChanged(const QString &)
{
    updateTitle();
}

void AuxSensorWidget::updateTitle()
{
    if (!m_titleLabel)
        return;

    QString text = label().trimmed();
    if (text.isEmpty()) {
        switch (m_mode) {
        case Mode::Voltage:
            text = tr("Voltage");
            break;
        case Mode::Current:
            text = tr("Current");
            break;
        case Mode::Temperature:
            text = tr("Temperature");
            break;
        default:
            text = tr("Aux Sensor");
            break;
        }
    }

    m_titleLabel->setText(text);
}

void AuxSensorWidget::updateUnitsLabel()
{
    if (!m_unitsLabel)
        return;

    QString u;

    switch (m_mode) {
    case Mode::Voltage:
        u = QStringLiteral("V");
        break;
    case Mode::Current:
        u = QStringLiteral("A");
        break;
    case Mode::Temperature:
        u = (m_tempUnitsIndex == 1) ? QStringLiteral("°F") : QStringLiteral("°C");
        break;
    default:
        u.clear();
        break;
    }

    m_unitsLabel->setText(u);
}

} // namespace raypm
