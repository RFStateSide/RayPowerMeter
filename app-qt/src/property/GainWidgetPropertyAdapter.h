// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

// GainWidgetPropertyAdapter.h
#pragma once

#include "IWidgetPropertyAdapter.h"

namespace raypm {

/**
 * @brief Adapter for GainWidget source binding and property-panel behavior.
 */
class GainWidgetPropertyAdapter : public IWidgetPropertyAdapter
{
public:
    QString typeId() const override;
    void onWidgetSelected(const WidgetPropertyContext &ctx) override;
    bool handlePropertyChange(const WidgetPropertyContext &ctx,
                              const QString &propertyKey,
                              const QVariant &newValue) override;
};

} // namespace raypm
