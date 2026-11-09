// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "property/IWidgetPropertyAdapter.h"

#include "app/mainwindow.h"
#include "controllers/LayoutManager.h"
#include "widgets/miniscopewidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QVariant>

namespace raypm {

/**
 * Adapter for MiniScopeWidget properties in Layout Design mode.
 *
 * Manages the property panel rows specific to MiniScope widgets, including:
 *   - Signal type (Forward, Reflected, SWR, Aux)
 *   - Time window (seconds)
 *   - Auto-scale on/off
 */

class MiniScopeWidgetPropertyAdapter : public IWidgetPropertyAdapter
{
public:
    using IWidgetPropertyAdapter::IWidgetPropertyAdapter;

    QString typeId() const override
    {
        // Must match LayoutManager's type string for MiniScope widgets
        return QStringLiteral("MiniScope");
    }

    void onWidgetSelected(const WidgetPropertyContext &ctx) override
    {
        if (!ctx.window || !ctx.layoutManager)
            return;

        MainWindow *win = ctx.window;
        LayoutManager *lm = ctx.layoutManager;

        // Hide generic Source row – MiniScope uses its own "Signal" selector.
        if (win->m_propFormLayout && win->m_propSourceCombo) {
            if (QWidget *srcLabel = win->m_propFormLayout->labelForField(win->m_propSourceCombo)) {
                srcLabel->setVisible(false);
            }
            win->m_propSourceCombo->setVisible(false);
        }

        // Show MiniScope-specific rows (Signal, Window, Auto-scale)
        win->setMiniScopePropertyRowsVisible(true);

        // Read persisted values (or defaults) from LayoutManager
        int signalInt = static_cast<int>(MiniScopeWidget::ScopeSignalType::ForwardWatts);
        double windowSec = 10.0;
        bool autoScale = true;

        lm->widgetMiniScopeSignal(ctx.widgetId, signalInt);
        lm->widgetMiniScopeWindowSec(ctx.widgetId, windowSec);
        lm->widgetMiniScopeAutoScale(ctx.widgetId, autoScale);

        // Update property panel widgets, blocking signals to avoid feedback loops
        if (win->m_propScopeSignalCombo) {
            QSignalBlocker blocker(win->m_propScopeSignalCombo);
            const int idx = win->m_propScopeSignalCombo->findData(signalInt);
            if (idx >= 0)
                win->m_propScopeSignalCombo->setCurrentIndex(idx);
        }

        if (win->m_propScopeWindowSpin) {
            QSignalBlocker blocker(win->m_propScopeWindowSpin);
            win->m_propScopeWindowSpin->setValue(windowSec);
        }

        if (win->m_propScopeAutoScaleCheck) {
            QSignalBlocker blocker(win->m_propScopeAutoScaleCheck);
            win->m_propScopeAutoScaleCheck->setChecked(autoScale);
        }
    }

    bool handlePropertyChange(const WidgetPropertyContext &ctx,
                              const QString &propertyKey,
                              const QVariant &newValue) override
    {
        if (!ctx.layoutManager || ctx.widgetId.isEmpty())
            return false;

        // Signal type (Forward / Ref / SWR / Aux)
        if (propertyKey == QLatin1String("miniscope.signal")) {
            const int sig = newValue.toInt();
            ctx.layoutManager->setWidgetMiniScopeSignal(ctx.widgetId, sig);
            return true;
        }

        // Time window in seconds
        if (propertyKey == QLatin1String("miniscope.windowSec")) {
            const double sec = newValue.toDouble();
            ctx.layoutManager->setWidgetMiniScopeWindowSec(ctx.widgetId, sec);
            return true;
        }

        // Auto-scale on/off
        if (propertyKey == QLatin1String("miniscope.autoScale")) {
            const bool enabled = newValue.toBool();
            ctx.layoutManager->setWidgetMiniScopeAutoScale(ctx.widgetId, enabled);
            return true;
        }

        return false;
    }
};

// Static registration
namespace {
struct MiniScopeWidgetPropertyAdapterRegistration
{
    MiniScopeWidgetPropertyAdapterRegistration()
    {
        registerPropertyAdapter(new MiniScopeWidgetPropertyAdapter());
    }
};

static MiniScopeWidgetPropertyAdapterRegistration s_miniScopeWidgetPropertyAdapterRegistration;
} // namespace

} // namespace raypm
