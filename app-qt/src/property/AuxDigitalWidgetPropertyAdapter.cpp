// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "AuxDigitalWidgetPropertyAdapter.h"

#include "app/mainwindow.h"
#include "controllers/LayoutManager.h"
#include "widgets/auxdigitalwidget.h"

#include <QComboBox>
#include <QFormLayout>
#include <QSignalBlocker>

namespace raypm {

// Single shared instance, auto-registered
static AuxDigitalWidgetPropertyAdapter s_auxDigitalAdapterInstance;

AuxDigitalWidgetPropertyAdapter::AuxDigitalWidgetPropertyAdapter()
{
    registerPropertyAdapter(this);
}

void AuxDigitalWidgetPropertyAdapter::onWidgetSelected(const WidgetPropertyContext &ctx)
{
    if (!ctx.window || !ctx.layoutManager)
        return;

    // Show only the AuxDigital property rows
    ctx.window->setPowerBarPropertyRowsVisible(false);
    ctx.window->setMiniScopePropertyRowsVisible(false);
    ctx.window->setFreqPropertyRowsVisible(false);
    ctx.window->setAuxSensorPropertyRowsVisible(false);
    ctx.window->setAuxDigitalPropertyRowsVisible(true);

    if (!ctx.window->m_propAuxDigitalModeCombo)
        return;

    // Load persisted mode for this widget (default Disabled if not set)
    AuxDigitalWidget::Mode mode = AuxDigitalWidget::Mode::Disabled;
    ctx.layoutManager->widgetAuxDigitalMode(ctx.widgetId, mode);

    {
        QSignalBlocker b(ctx.window->m_propAuxDigitalModeCombo);
        const int desired = static_cast<int>(mode);
        const int idx = ctx.window->m_propAuxDigitalModeCombo->findData(desired);
        ctx.window->m_propAuxDigitalModeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    // push the mode into the live widget
    if (WidgetBase *wb = ctx.layoutManager->widgetObject(ctx.widgetId)) {
        if (auto *dig = qobject_cast<AuxDigitalWidget *>(wb)) {
            dig->setMode(mode);
        }
    }
}

bool AuxDigitalWidgetPropertyAdapter::handlePropertyChange(const WidgetPropertyContext &ctx,
                                                           const QString &propertyKey,
                                                           const QVariant &value)
{
    // 1) Digital mode change
    if (propertyKey == QStringLiteral("auxdig.mode")) {
        const int modeInt = value.toInt();
        const auto mode = static_cast<AuxDigitalWidget::Mode>(modeInt);

        ctx.layoutManager->setWidgetAuxDigitalMode(ctx.widgetId, mode);

        if (WidgetBase *wb = ctx.layoutManager->widgetObject(ctx.widgetId)) {
            if (auto *dig = qobject_cast<AuxDigitalWidget *>(wb)) {
                dig->setMode(mode);
            }
        }
        return true;
    }

    // 2) Widget label change (same pattern as AuxSensor adapter)
    if (propertyKey == QStringLiteral("widget.label")) {
        const QString newLabel = value.toString().trimmed();

        // Persist label into model/JSON
        ctx.layoutManager->setWidgetLabel(ctx.widgetId, newLabel);

        // Keep the label line-edit in sync
        if (ctx.window && ctx.window->m_widgetLabelEdit) {
            QSignalBlocker blocker(ctx.window->m_widgetLabelEdit);
            ctx.window->m_widgetLabelEdit->setText(newLabel);
        }

        // Update live widget
        if (WidgetBase *wb = ctx.layoutManager->widgetObject(ctx.widgetId)) {
            if (auto *dig = qobject_cast<AuxDigitalWidget *>(wb)) {
                dig->setLabel(newLabel);
            }
        }

        return true;
    }

    return false;
}

} // namespace raypm
