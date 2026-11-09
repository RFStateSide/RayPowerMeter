// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "property/IWidgetPropertyAdapter.h"

namespace raypm {

class ForwardRefPanelPropertyAdapter : public IWidgetPropertyAdapter
{
public:
    ForwardRefPanelPropertyAdapter();
    ~ForwardRefPanelPropertyAdapter() override = default;

    QString typeId() const override
    {
        // Matches LayoutManager::LayoutWidget::type for this widget
        return QStringLiteral("ForwardRefPanel");
    }

    void onWidgetSelected(const WidgetPropertyContext &ctx) override;
    bool handlePropertyChange(const WidgetPropertyContext &ctx,
                              const QString &propertyKey,
                              const QVariant &newValue) override;
};

} // namespace raypm
