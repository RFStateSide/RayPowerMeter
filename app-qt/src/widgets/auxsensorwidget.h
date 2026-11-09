// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "widgetbase.h"

class QLabel;
class QVBoxLayout;
class QHBoxLayout;

namespace raypm {

// Generic Aux sensor widget: Voltage / Current / Temperature
class AuxSensorWidget : public WidgetBase
{
    Q_OBJECT

    // 0 = °C, 1 = °F  (matches AppSettings::unitsTempIndex)
    Q_PROPERTY(
        int tempUnitsIndex READ tempUnitsIndex WRITE setTempUnitsIndex NOTIFY tempUnitsIndexChanged)

public:
    enum class Mode { Unknown, Voltage, Current, Temperature };
    Q_ENUM(Mode)

    explicit AuxSensorWidget(QWidget *parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode m);

    int tempUnitsIndex() const { return m_tempUnitsIndex; }
    void setTempUnitsIndex(int index);

public slots:
    // Value is always in base units:
    //  - Voltage:  Volts
    //  - Current:  Amps
    //  - Temperature: Celsius (we convert to °F if needed)
    void setValue(double value);

    void onLabelChanged(const QString &label);

signals:
    void tempUnitsIndexChanged(int index);

private:
    void updateTitle();
    void updateUnitsLabel();

    QLabel *m_titleLabel = nullptr;
    QLabel *m_valueLabel = nullptr;
    QLabel *m_unitsLabel = nullptr;

    QFont m_baseCaptionFont;
    QFont m_baseValueFont;
    bool m_scaleInitialized = false;

    Mode m_mode = Mode::Unknown;
    int m_tempUnitsIndex = 0; // 0 = °C, 1 = °F
};

} // namespace raypm
