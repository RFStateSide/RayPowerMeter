// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "property/IWidgetPropertyAdapter.h"

#include "app/mainwindow.h"
#include "controllers/LayoutManager.h"
#include "widgets/powerbarwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QVariant>

namespace raypm {

// Small helper so both horiz/vert adapters share logic for property changes
static bool handlePowerBarPropertyChange(LayoutManager *layout,
                                         const QString &widgetId,
                                         const QString &propertyKey,
                                         const QVariant &newValue)
{
    if (!layout || widgetId.isEmpty())
        return false;

    if (propertyKey == QLatin1String("powerbar.meterMode")) {
        const int index = newValue.toInt();

        PowerBarWidget::MeterMode mode = PowerBarWidget::MeterMode::Live;
        switch (index) {
        case 1:
            mode = PowerBarWidget::MeterMode::Peak;
            break;
        case 2:
            mode = PowerBarWidget::MeterMode::Freeze;
            break;
        case 3:
            mode = PowerBarWidget::MeterMode::None;
            break;
        default:
            mode = PowerBarWidget::MeterMode::Live;
            break;
        }

        layout->setWidgetMeterMode(widgetId, mode);
        return true;
    }

    if (propertyKey == QLatin1String("powerbar.showTicks")) {
        layout->setWidgetShowTicks(widgetId, newValue.toBool());
        return true;
    }

    if (propertyKey == QLatin1String("powerbar.showLabels")) {
        layout->setWidgetShowTickLabels(widgetId, newValue.toBool());
        return true;
    }

    if (propertyKey == QLatin1String("powerbar.showThresholdLabels")) {
        layout->setWidgetShowThresholdLabels(widgetId, newValue.toBool());
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------------
// Horizontal PowerBar
// -----------------------------------------------------------------------------
class PowerBarWidgetPropertyAdapter : public IWidgetPropertyAdapter
{
public:
    using IWidgetPropertyAdapter::IWidgetPropertyAdapter;

    QString typeId() const override { return QStringLiteral("PowerBar"); }

    void onWidgetSelected(const WidgetPropertyContext &ctx) override
    {
        MainWindow *window = ctx.window;
        LayoutManager *layout = ctx.layoutManager;

        if (!window || !layout)
            return;

        if (!window->m_propShowTicksCheck || !window->m_propShowLabelsCheck
            || !window->m_propMeterModeCombo) {
            return;
        }

        // Show only the FreqWidget-specific row and hide the others that use the
        // shared property panel controls.
        window->setGainPropertyRowsVisible(false);
        window->setMiniScopePropertyRowsVisible(false);
        window->setAuxSensorPropertyRowsVisible(false);
        window->setFreqPropertyRowsVisible(false);

        window->setPowerBarPropertyRowsVisible(true);

        bool showTicks = true;
        bool showLabels = true;
        bool showThresholdLabels = true;
        layout->widgetShowTicks(ctx.widgetId, showTicks);
        layout->widgetShowTickLabels(ctx.widgetId, showLabels);
        layout->widgetShowThresholdLabels(ctx.widgetId, showThresholdLabels);

        PowerBarWidget::MeterMode mode = PowerBarWidget::MeterMode::Live;
        const bool hasMode = layout->widgetMeterMode(ctx.widgetId, mode);

        QSignalBlocker b0(static_cast<QObject *>(window->m_propShowThresholdLabelsCheck));
        QSignalBlocker b1(static_cast<QObject *>(window->m_propShowTicksCheck));
        QSignalBlocker b2(static_cast<QObject *>(window->m_propShowLabelsCheck));
        QSignalBlocker b3(static_cast<QObject *>(window->m_propMeterModeCombo));

        window->m_propShowTicksCheck->setChecked(showTicks);
        window->m_propShowLabelsCheck->setChecked(showLabels);
        window->m_propShowThresholdLabelsCheck->setChecked(showThresholdLabels);

        if (!hasMode) {
            window->m_propMeterModeCombo->setEnabled(false);
            return;
        }

        window->m_propMeterModeCombo->setEnabled(true);

        int idx = 0;
        switch (mode) {
        case PowerBarWidget::MeterMode::Live:
            idx = 0;
            break;
        case PowerBarWidget::MeterMode::Peak:
            idx = 1;
            break;
        case PowerBarWidget::MeterMode::Freeze:
            idx = 2;
            break;
        case PowerBarWidget::MeterMode::None:
            idx = 3;
            break;
        }

        window->m_propMeterModeCombo->setCurrentIndex(idx);
    }

    bool handlePropertyChange(const WidgetPropertyContext &ctx,
                              const QString &propertyKey,
                              const QVariant &newValue) override
    {
        return handlePowerBarPropertyChange(ctx.layoutManager, ctx.widgetId, propertyKey, newValue);
    }
};

// -----------------------------------------------------------------------------
// Vertical PowerBar
// -----------------------------------------------------------------------------
class PowerBarVertWidgetPropertyAdapter : public IWidgetPropertyAdapter
{
public:
    using IWidgetPropertyAdapter::IWidgetPropertyAdapter;

    QString typeId() const override { return QStringLiteral("PowerBarVert"); }

    void onWidgetSelected(const WidgetPropertyContext &ctx) override
    {
        MainWindow *window = ctx.window;
        LayoutManager *layout = ctx.layoutManager;

        if (!window || !layout)
            return;

        if (!window->m_propShowTicksCheck || !window->m_propShowLabelsCheck
            || !window->m_propMeterModeCombo) {
            return;
        }

        window->setPowerBarPropertyRowsVisible(true);

        bool showTicks = true;
        bool showLabels = true;
        layout->widgetShowTicks(ctx.widgetId, showTicks);
        layout->widgetShowTickLabels(ctx.widgetId, showLabels);

        PowerBarWidget::MeterMode mode = PowerBarWidget::MeterMode::Live;
        const bool hasMode = layout->widgetMeterMode(ctx.widgetId, mode);

        QSignalBlocker b1(static_cast<QObject *>(window->m_propShowTicksCheck));
        QSignalBlocker b2(static_cast<QObject *>(window->m_propShowLabelsCheck));
        QSignalBlocker b3(static_cast<QObject *>(window->m_propMeterModeCombo));

        window->m_propShowTicksCheck->setChecked(showTicks);
        window->m_propShowLabelsCheck->setChecked(showLabels);

        if (!hasMode) {
            window->m_propMeterModeCombo->setEnabled(false);
            return;
        }

        window->m_propMeterModeCombo->setEnabled(true);

        int idx = 0;
        switch (mode) {
        case PowerBarWidget::MeterMode::Live:
            idx = 0;
            break;
        case PowerBarWidget::MeterMode::Peak:
            idx = 1;
            break;
        case PowerBarWidget::MeterMode::Freeze:
            idx = 2;
            break;
        case PowerBarWidget::MeterMode::None:
            idx = 3;
            break;
        }

        window->m_propMeterModeCombo->setCurrentIndex(idx);
    }

    bool handlePropertyChange(const WidgetPropertyContext &ctx,
                              const QString &propertyKey,
                              const QVariant &newValue) override
    {
        return handlePowerBarPropertyChange(ctx.layoutManager, ctx.widgetId, propertyKey, newValue);
    }
};

// Static registration for both adapters
namespace {
struct PowerBarPropertyAdapterRegistration
{
    PowerBarPropertyAdapterRegistration()
    {
        registerPropertyAdapter(new PowerBarWidgetPropertyAdapter());
        registerPropertyAdapter(new PowerBarVertWidgetPropertyAdapter());
    }
};

static PowerBarPropertyAdapterRegistration s_powerBarPropertyAdapterRegistration;
} // namespace

} // namespace raypm
