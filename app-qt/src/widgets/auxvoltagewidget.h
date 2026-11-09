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

class AuxVoltageWidget : public WidgetBase
{
    Q_OBJECT
public:
    explicit AuxVoltageWidget(QWidget *parent = nullptr);

    int layoutRowSpan() const override { return 1; }
    int layoutColSpan() const override { return 1; }

    void onAppScaleFactorChanged(double scale) override;

    // Call this from telemetry routing
    void setVoltage(double volts);

private slots:
    // Keep title in sync if WidgetBase::label changes
    void onLabelChanged(const QString &label);

private:
    void updateTitleFromLabel();

    QLabel *m_titleLabel = nullptr;
    QLabel *m_valueLabel = nullptr;
    QLabel *m_unitsLabel = nullptr;

    QFont m_baseCaptionFont;
    QFont m_baseValueFont;
    bool m_scaleInitialized = false;
};

} // namespace raypm
