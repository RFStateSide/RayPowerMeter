// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "property/IWidgetPropertyAdapter.h"

namespace raypm {

class AuxDigitalWidgetPropertyAdapter : public IWidgetPropertyAdapter
{
public:
    AuxDigitalWidgetPropertyAdapter();

    QString typeId() const override { return QStringLiteral("AuxDigital"); }

    void onWidgetSelected(const WidgetPropertyContext &ctx) override;

    bool handlePropertyChange(const WidgetPropertyContext &ctx,
                              const QString &propertyKey,
                              const QVariant &newValue) override;
};

} // namespace raypm
