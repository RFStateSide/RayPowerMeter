// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "property/AuxVoltageWidgetPropertyAdapter.h"

#include "app/mainwindow.h"
#include "controllers/LayoutManager.h"
#include "widgets/auxvoltagewidget.h"

#include <QVariant>

namespace raypm {

// ----- AuxVoltageWidgetPropertyAdapter -----

AuxVoltageWidgetPropertyAdapter::AuxVoltageWidgetPropertyAdapter(QObject *parent)
    : IWidgetPropertyAdapter(parent)
{}

QString AuxVoltageWidgetPropertyAdapter::typeId() const
{
    // Must match the widget type id used in LayoutManager / layout JSON
    // (the same string you test with QLatin1String("AuxVoltage")).
    return QStringLiteral("AuxVoltage");
}

void AuxVoltageWidgetPropertyAdapter::onWidgetSelected(const WidgetPropertyContext &ctx)
{
    MainWindow *mw = ctx.window;
    LayoutManager *lm = ctx.layoutManager;

    if (!mw || !lm)
        return;

    // Show only the AuxVoltageWidget row and hide the others that use the
    // shared property panel controls.
    mw->setPowerBarPropertyRowsVisible(false);
    mw->setGainPropertyRowsVisible(false);
    mw->setMiniScopePropertyRowsVisible(false);
    mw->setAuxSensorPropertyRowsVisible(false);
    mw->setFreqPropertyRowsVisible(false);
}

bool AuxVoltageWidgetPropertyAdapter::handlePropertyChange(const WidgetPropertyContext &ctx,
                                                           const QString &propertyKey,
                                                           const QVariant &newValue)
{
    if (!ctx.layoutManager || ctx.widgetId.isEmpty())
        return false;

    // Generic per-widget source binding
    if (propertyKey == QLatin1String("widget.source")) {
        const QString sourceKey = newValue.toString();
        if (sourceKey.isEmpty())
            return false;

        // This will update both the live widget (via WidgetBase::setSource)
        // and the layout model / JSON.
        ctx.layoutManager->setWidgetSource(ctx.widgetId, sourceKey);
        return true;
    }

    // Generic per-widget label
    if (propertyKey == QLatin1String("widget.label")) {
        const QString label = newValue.toString();

        // Update the actual AuxVoltageWidget label immediately, if present.
        if (auto *aux = ctx.as<AuxVoltageWidget>()) {
            aux->setLabel(label);
        }

        // Keep layout model / JSON in sync.
        ctx.layoutManager->setWidgetLabel(ctx.widgetId, label);
        return true;
    }

    return false;
}

} // namespace raypm

// ----- Static registration -----

namespace {

struct AuxVoltageAdapterRegistration
{
    AuxVoltageAdapterRegistration()
    {
        // Adapter lives for the duration of the process.
        // Registry keeps a raw pointer; no parent is strictly required.
        auto *adapter = new raypm::AuxVoltageWidgetPropertyAdapter(nullptr);
        raypm::registerPropertyAdapter(adapter);
    }
};

// Ensure registration runs at startup.
static AuxVoltageAdapterRegistration s_auxVoltageAdapterRegistration;

} // anonymous namespace
