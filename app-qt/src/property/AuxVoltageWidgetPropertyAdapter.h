// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "property/IWidgetPropertyAdapter.h"

namespace raypm {

/**
 * Adapter for AuxVoltageWidget properties in Layout Design mode.
 *
 * Right now it only handles the two generic properties:
 *   - "widget.source"
 *   - "widget.label"
 */
class AuxVoltageWidgetPropertyAdapter : public IWidgetPropertyAdapter
{
public:
    explicit AuxVoltageWidgetPropertyAdapter(QObject *parent = nullptr);

    QString typeId() const override;

    void onWidgetSelected(const WidgetPropertyContext &ctx) override;
    bool handlePropertyChange(const WidgetPropertyContext &ctx,
                              const QString &propertyKey,
                              const QVariant &newValue) override;
};

} // namespace raypm
