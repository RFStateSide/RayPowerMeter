// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "property/IWidgetPropertyAdapter.h"

#include "app/mainwindow.h"
#include "controllers/LayoutManager.h"

#include <QFormLayout>
#include <QSignalBlocker>
#include <QVariant>

namespace raypm {

class SpacerWidgetPropertyAdapter : public IWidgetPropertyAdapter
{
public:
    using IWidgetPropertyAdapter::IWidgetPropertyAdapter;

    QString typeId() const override
    {
        // Matches LayoutManager's type string in JSON
        return QStringLiteral("Spacer");
    }

    void onWidgetSelected(const WidgetPropertyContext &ctx) override
    {
        if (!ctx.window || !ctx.layoutManager)
            return;

        MainWindow *win = ctx.window;
        LayoutManager *lm = ctx.layoutManager;

        if (!win->m_propFormLayout || !win->m_propMinHeightSpin)
            return;

        // Make the Min Height row visible for Spacer.
        if (QWidget *label = win->m_propFormLayout->labelForField(win->m_propMinHeightSpin)) {
            label->setVisible(true);
        }

        // Show only the FreqWidget-specific row and hide the others that use the
        // shared property panel controls.
        win->setPowerBarPropertyRowsVisible(false);
        win->setGainPropertyRowsVisible(false);
        win->setMiniScopePropertyRowsVisible(false);
        win->setAuxSensorPropertyRowsVisible(false);
        win->setFreqPropertyRowsVisible(false);

        win->m_propMinHeightSpin->setVisible(true);

        // Read current value from LayoutManager (default 0 if not set)
        int minHeight = 0;
        lm->widgetMinHeight(ctx.widgetId, minHeight);

        // Update spinbox without triggering change notifications
        QSignalBlocker blocker(win->m_propMinHeightSpin);
        win->m_propMinHeightSpin->setValue(minHeight);
    }

    bool handlePropertyChange(const WidgetPropertyContext &ctx,
                              const QString &propertyKey,
                              const QVariant &newValue) override
    {
        if (!ctx.layoutManager || ctx.widgetId.isEmpty())
            return false;

        // Spacer's only custom property in the panel: minimum height (px)
        if (propertyKey == QLatin1String("spacer.minHeight")) {
            const int px = newValue.toInt();
            ctx.layoutManager->setWidgetMinHeight(ctx.widgetId, px);
            return true;
        }

        return false;
    }
};

// Static registration with the adapter registry
namespace {
struct SpacerWidgetPropertyAdapterRegistration
{
    SpacerWidgetPropertyAdapterRegistration()
    {
        registerPropertyAdapter(new SpacerWidgetPropertyAdapter());
    }
};

static SpacerWidgetPropertyAdapterRegistration s_spacerWidgetPropertyAdapterRegistration;
} // namespace

} // namespace raypm
