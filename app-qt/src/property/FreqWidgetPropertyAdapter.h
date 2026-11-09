// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "property/IWidgetPropertyAdapter.h"

namespace raypm {

/**
 * Adapter for FreqWidget properties in Layout Design mode.
 *
 * Right now it reuses the generic properties:
 *   - "widget.source" (optional, if you want to bind to a specific source key)
 *   - "widget.label"
 */
class FreqWidgetPropertyAdapter : public IWidgetPropertyAdapter
{
public:
    explicit FreqWidgetPropertyAdapter(QObject *parent = nullptr);

    QString typeId() const override;

    void onWidgetSelected(const WidgetPropertyContext &ctx) override;
    bool handlePropertyChange(const WidgetPropertyContext &ctx,
                              const QString &propertyKey,
                              const QVariant &newValue) override;
};

} // namespace raypm
