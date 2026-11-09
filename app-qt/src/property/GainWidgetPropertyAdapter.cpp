// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

// GainWidgetPropertyAdapter.cpp
#include "GainWidgetPropertyAdapter.h"

#include "app/mainwindow.h"
#include "controllers/LayoutManager.h"
#include "widgets/gainwidget.h"

#include <QComboBox>
#include <QFormLayout>
#include <QVariant>

namespace raypm {

// Single shared instance for the registry
static GainWidgetPropertyAdapter s_gainAdapterInstance;
static const bool s_gainAdapterRegistered = []() {
    registerPropertyAdapter(&s_gainAdapterInstance);
    return true;
}();

QString GainWidgetPropertyAdapter::typeId() const
{
    return QStringLiteral("GainWidget");
}

void GainWidgetPropertyAdapter::onWidgetSelected(const WidgetPropertyContext &ctx)
{
    if (!ctx.window)
        return;

    // Show only the FreqWidget-specific row and hide the others that use the
    // shared property panel controls.
    ctx.window->setPowerBarPropertyRowsVisible(false);
    ctx.window->setFreqPropertyRowsVisible(false);
    ctx.window->setMiniScopePropertyRowsVisible(false);
    ctx.window->setAuxSensorPropertyRowsVisible(false);

    // Show Gain-specific rows
    ctx.window->setGainPropertyRowsVisible(true);

    // Populate the in/out source combos using MainWindow helper.
    // This reads + writes LayoutManager, sets current selections, etc.
    ctx.window->populateGainSourceCombos();

    // Hide the generic Source row – Gain uses in/out sources instead.
    if (ctx.window->m_propFormLayout && ctx.window->m_propSourceCombo) {
        if (QWidget *srcLabel = ctx.window->m_propFormLayout->labelForField(
                ctx.window->m_propSourceCombo)) {
            srcLabel->setVisible(false);
        }
        ctx.window->m_propSourceCombo->setVisible(false);
    }
}

bool GainWidgetPropertyAdapter::handlePropertyChange(const WidgetPropertyContext &ctx,
                                                     const QString &propertyKey,
                                                     const QVariant &newValue)
{
    if (!ctx.layoutManager || ctx.widgetId.isEmpty())
        return false;

    const QString value = newValue.toString();

    if (propertyKey == QLatin1String("gain.rfInSource")) {
        QString outSrc;
        if (ctx.window && ctx.window->m_propGainOutSourceCombo
            && ctx.window->m_propGainOutSourceCombo->currentIndex() >= 0) {
            outSrc = ctx.window->m_propGainOutSourceCombo->currentData().toString();
        }

        ctx.layoutManager->setWidgetGainSources(ctx.widgetId, value, outSrc);

        if (auto *gw = qobject_cast<GainWidget *>(ctx.layoutManager->widgetObject(ctx.widgetId))) {
            gw->setRfInSource(value);
            gw->setRfOutSource(outSrc);
        }
        return true;
    }

    if (propertyKey == QLatin1String("gain.rfOutSource")) {
        QString inSrc;
        if (ctx.window && ctx.window->m_propGainInSourceCombo
            && ctx.window->m_propGainInSourceCombo->currentIndex() >= 0) {
            inSrc = ctx.window->m_propGainInSourceCombo->currentData().toString();
        }

        ctx.layoutManager->setWidgetGainSources(ctx.widgetId, inSrc, value);

        if (auto *gw = qobject_cast<GainWidget *>(ctx.layoutManager->widgetObject(ctx.widgetId))) {
            gw->setRfInSource(inSrc);
            gw->setRfOutSource(value);
        }
        return true;
    }

    if (propertyKey == QLatin1String("gain.attnDb")) {
        const double attnDb = newValue.toDouble();
        ctx.layoutManager->setWidgetGainAttnDb(ctx.widgetId, attnDb);

        if (auto *gw = qobject_cast<GainWidget *>(ctx.layoutManager->widgetObject(ctx.widgetId))) {
            gw->setAttnDb(attnDb);
        }
        return true;
    }

    return false;
}

} // namespace raypm
