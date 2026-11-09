// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "widgets/widgetbase.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>

namespace raypm {

class AuxDigitalWidget : public WidgetBase
{
    Q_OBJECT
public:
    enum class Mode { Disabled = 0, Input = 1, Output = 2 };
    Q_ENUM(Mode)

    explicit AuxDigitalWidget(QWidget *parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    bool value() const { return m_value; }
    void setValue(bool value);

signals:
    void modeChanged(raypm::AuxDigitalWidget::Mode mode);
    void valueChanged(bool value);

    // Emitted when the user toggles the checkbox in Output mode.
    void outputValueChanged(bool value);

private slots:
    void onLabelChanged(const QString &label);
    void onCheckboxToggled(bool checked);

private:
    void updateVisuals();

    Mode m_mode = Mode::Disabled;
    bool m_value = false;

    QLabel *m_label = nullptr;
    QLabel *m_modeLabel = nullptr;
    QWidget *m_ledWidget = nullptr;
    QCheckBox *m_outputCheck = nullptr;
};

} // namespace raypm
