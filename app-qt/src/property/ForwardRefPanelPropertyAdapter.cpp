// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "property/ForwardRefPanelPropertyAdapter.h"

#include "app/mainwindow.h"
#include "controllers/LayoutManager.h"
#include "property/IWidgetPropertyAdapter.h"
#include "widgets/widgetbase.h"

namespace raypm {

static ForwardRefPanelPropertyAdapter s_forwardRefPanelAdapter; // auto-register

ForwardRefPanelPropertyAdapter::ForwardRefPanelPropertyAdapter()
{
    // Register this adapter so adapterForWidgetType("ForwardRefPanel") finds us
    registerPropertyAdapter(this);
}

void ForwardRefPanelPropertyAdapter::onWidgetSelected(const WidgetPropertyContext &ctx)
{
    MainWindow *mw = ctx.window;
    LayoutManager *lm = ctx.layoutManager;

    if (!mw || !lm)
        return;

    // Show only the FreqWidget-specific row and hide the others that use the
    // shared property panel controls.
    mw->setPowerBarPropertyRowsVisible(false);
    mw->setGainPropertyRowsVisible(false);
    mw->setMiniScopePropertyRowsVisible(false);
    mw->setAuxSensorPropertyRowsVisible(false);
    mw->setFreqPropertyRowsVisible(false);
}

bool ForwardRefPanelPropertyAdapter::handlePropertyChange(const WidgetPropertyContext &ctx,
                                                          const QString &propertyKey,
                                                          const QVariant &newValue)
{
    if (!ctx.layoutManager || ctx.widgetId.isEmpty())
        return false;

    // ForwardRefPanel only cares about its source binding
    // (e.g. "Coupler0", "Coupler1", ...).
    if (propertyKey == QLatin1String("widget.source")) {
        ctx.layoutManager->setWidgetSource(ctx.widgetId, newValue.toString());
        return true; // tell MainWindow we handled it
    }

    // Let unhandled keys fall back to the legacy handlers
    return false;
}

} // namespace raypm
