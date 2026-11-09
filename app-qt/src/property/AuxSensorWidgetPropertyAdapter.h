// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "IWidgetPropertyAdapter.h"

namespace raypm {

/**
 * @brief Property adapter for AuxSensorWidget.
 *
 * Responsibilities:
 *  - When an AuxSensor widget is selected in layout design mode:
 *      * Show Aux-specific rows (mode, temp units, label) in the property panel.
 *      * Populate those rows from the JSON-backed layout model.
 *  - When Aux properties change in the property panel:
 *      * Persist the changes via LayoutManager::setWidgetAuxSensorConfig().
 *      * Push updated values into the live AuxSensorWidget instance.
 *
 * This keeps MainWindow free of Aux-specific binding rules; it just delegates
 * to this adapter based on widget type.
 */
class AuxSensorWidgetPropertyAdapter : public IWidgetPropertyAdapter
{
public:
    /**
     * @brief Returns the widget "type" string this adapter handles.
     *
     * This must match the type string used by LayoutManager for AuxSensorWidget.
     * (Currently "AuxSensor".)
     */
    QString typeId() const override;

    /**
     * @brief Called when an AuxSensorWidget is selected in design mode.
     *
     * Implementations should:
     *  - make Aux rows visible in the property panel,
     *  - load mode / temp units / label from the layout model,
     *  - synchronize the generic widget label editor,
     *  - enable/disable the temp-units combo based on mode.
     */
    void onWidgetSelected(const WidgetPropertyContext &ctx) override;

    /**
     * @brief Handles AuxSensor-related property changes from the UI.
     *
     * Recognized property keys:
     *  - "aux.mode"            : AuxSensorWidget::Mode enum (stored as int)
     *  - "aux.tempUnitsIndex"  : index into the temp-units combo (int)
     *  - "aux.label"           : Aux label text (QString)
     *
     * For each handled property:
     *  - updates LayoutManager's JSON-backed layout model, and
     *  - updates the live AuxSensorWidget instance (if present).
     *
     * @return true if the property was handled, false otherwise.
     */
    bool handlePropertyChange(const WidgetPropertyContext &ctx,
                              const QString &propertyKey,
                              const QVariant &newValue) override;
};

} // namespace raypm
