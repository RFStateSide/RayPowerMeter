// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "AuxSensorWidgetPropertyAdapter.h"

// Adjust these include paths to match your project structure.
#include "app/mainwindow.h"
#include "controllers/LayoutManager.h"
#include "widgets/auxsensorwidget.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSignalBlocker>

namespace raypm {

// Single shared instance for the registry
static AuxSensorWidgetPropertyAdapter s_auxAdapterInstance;
static const bool s_auxAdapterRegistered = []() {
    registerPropertyAdapter(&s_auxAdapterInstance);
    return true;
}();

// -----------------------------------------------------------------------------
// IWidgetPropertyAdapter implementation
// -----------------------------------------------------------------------------

/**
 * @brief Returns the widget "type" string this adapter handles.
 */
QString AuxSensorWidgetPropertyAdapter::typeId() const
{
    return QStringLiteral("AuxSensor");
}

/**
 * @brief Called when an AuxSensorWidget is selected in design mode.
 *
 * Shows Aux-specific rows (mode, temp units, label) and loads them from the
 * JSON-backed layout model via LayoutManager::widgetAuxSensorConfig().
 */
void AuxSensorWidgetPropertyAdapter::onWidgetSelected(const WidgetPropertyContext &ctx)
{
    if (!ctx.window || !ctx.layoutManager)
        return;

    // Show only the AuxSensor-specific rows and hide the others that use the
    // shared property panel controls.
    ctx.window->setPowerBarPropertyRowsVisible(false);
    ctx.window->setGainPropertyRowsVisible(false);
    ctx.window->setMiniScopePropertyRowsVisible(false);
    ctx.window->setFreqPropertyRowsVisible(false);

    // Ensure Aux rows are visible when an AuxSensor widget is selected.
    ctx.window->setAuxSensorPropertyRowsVisible(true);

    if (!ctx.window->m_propAuxSensorModeCombo || !ctx.window->m_propAuxSensorUnitsCombo
        || !ctx.window->m_propAuxSensorLabelEdit) {
        return;
    }

    AuxSensorWidget::Mode mode = AuxSensorWidget::Mode::Unknown;
    int tempUnitsIndex = 0;
    QString label;

    if (!ctx.layoutManager->widgetAuxSensorConfig(ctx.widgetId, mode, tempUnitsIndex, label)) {
        return;
    }

    QSignalBlocker b1(ctx.window->m_propAuxSensorModeCombo);
    QSignalBlocker b2(ctx.window->m_propAuxSensorUnitsCombo);
    QSignalBlocker b3(ctx.window->m_propAuxSensorLabelEdit);

    // Mode combo: find enum value stored in itemData.
    const int modeIdx = ctx.window->m_propAuxSensorModeCombo->findData(static_cast<int>(mode));
    ctx.window->m_propAuxSensorModeCombo->setCurrentIndex(modeIdx >= 0 ? modeIdx : 0);

    // Units combo: clamp index to valid range.
    if (ctx.window->m_propAuxSensorUnitsCombo->count() > 0) {
        if (tempUnitsIndex < 0 || tempUnitsIndex >= ctx.window->m_propAuxSensorUnitsCombo->count()) {
            tempUnitsIndex = 0;
        }
        ctx.window->m_propAuxSensorUnitsCombo->setCurrentIndex(tempUnitsIndex);
    }

    // Label.
    ctx.window->m_propAuxSensorLabelEdit->setText(label);

    // Keep the generic widget-label editor in sync.
    if (ctx.window->m_widgetLabelEdit) {
        QSignalBlocker bGeneric(ctx.window->m_widgetLabelEdit);
        ctx.window->m_widgetLabelEdit->setText(label);
    }

    // Units only make sense when mode == Temperature.
    const bool isTemp = (mode == AuxSensorWidget::Mode::Temperature);
    ctx.window->m_propAuxSensorUnitsCombo->setEnabled(isTemp);
}

/**
 * @brief Handles AuxSensor-related property changes from the property panel.
 *
 * The propertyKey dispatches to one of the three Aux properties:
 *  - "aux.mode"
 *  - "aux.tempUnitsIndex"
 *  - "aux.label"
 */
bool AuxSensorWidgetPropertyAdapter::handlePropertyChange(const WidgetPropertyContext &ctx,
                                                          const QString &propertyKey,
                                                          const QVariant &newValue)
{
    if (!ctx.layoutManager || ctx.widgetId.isEmpty())
        return false;

    // Helper to read current config before modifying a single field.
    auto loadConfig = [&](AuxSensorWidget::Mode &mode, int &tempUnitsIndex, QString &label) -> bool {
        return ctx.layoutManager->widgetAuxSensorConfig(ctx.widgetId, mode, tempUnitsIndex, label);
    };

    // --- Aux mode changed ----------------------------------------------------
    if (propertyKey == QLatin1String("aux.mode")) {
        AuxSensorWidget::Mode mode = AuxSensorWidget::Mode::Unknown;
        int tempUnitsIndex = 0;
        QString label;

        if (!loadConfig(mode, tempUnitsIndex, label))
            return false;

        const auto newMode = static_cast<AuxSensorWidget::Mode>(newValue.toInt());

        // Persist to model / JSON.
        ctx.layoutManager->setWidgetAuxSensorConfig(ctx.widgetId, newMode, tempUnitsIndex, label);

        // Update live widget.
        if (auto *sensor = qobject_cast<AuxSensorWidget *>(
                ctx.layoutManager->widgetObject(ctx.widgetId))) {
            sensor->setMode(newMode);
            sensor->setTempUnitsIndex(tempUnitsIndex);
            sensor->setLabel(label);
        }

        // Enable temp-units combo only for Temperature mode.
        if (ctx.window && ctx.window->m_propAuxSensorUnitsCombo) {
            ctx.window->m_propAuxSensorUnitsCombo->setEnabled(
                newMode == AuxSensorWidget::Mode::Temperature);
        }

        return true;
    }

    // --- Aux temperature units index changed --------------------------------
    if (propertyKey == QLatin1String("aux.tempUnitsIndex")) {
        AuxSensorWidget::Mode mode = AuxSensorWidget::Mode::Unknown;
        int tempUnitsIndex = 0;
        QString label;

        if (!loadConfig(mode, tempUnitsIndex, label))
            return false;

        tempUnitsIndex = newValue.toInt();

        // Persist to model / JSON.
        ctx.layoutManager->setWidgetAuxSensorConfig(ctx.widgetId, mode, tempUnitsIndex, label);

        // Update live widget.
        if (auto *sensor = qobject_cast<AuxSensorWidget *>(
                ctx.layoutManager->widgetObject(ctx.widgetId))) {
            sensor->setTempUnitsIndex(tempUnitsIndex);
        }

        return true;
    }

    // --- Aux label changed ---------------------------------------------------
    if (propertyKey == QLatin1String("aux.label")) {
        AuxSensorWidget::Mode mode = AuxSensorWidget::Mode::Unknown;
        int tempUnitsIndex = 0;
        QString label;

        if (!loadConfig(mode, tempUnitsIndex, label))
            return false;

        const QString newLabel = newValue.toString().trimmed();

        // Persist to model / JSON.
        ctx.layoutManager->setWidgetAuxSensorConfig(ctx.widgetId, mode, tempUnitsIndex, newLabel);

        // Update live widget.
        if (auto *sensor = qobject_cast<AuxSensorWidget *>(
                ctx.layoutManager->widgetObject(ctx.widgetId))) {
            sensor->setLabel(newLabel);
        }

        // Keep generic widget-label editor in sync.
        if (ctx.window && ctx.window->m_widgetLabelEdit) {
            QSignalBlocker b(ctx.window->m_widgetLabelEdit);
            ctx.window->m_widgetLabelEdit->setText(newLabel);
        }

        return true;
    }

    return false;
}

} // namespace raypm
