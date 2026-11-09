// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "property/FreqWidgetPropertyAdapter.h"

#include "app/mainwindow.h"
#include "controllers/LayoutManager.h"
#include "widgets/freqwidget.h"

#include <QCheckBox>
#include <QSignalBlocker>

namespace raypm {

// Single shared instance for the registry
static FreqWidgetPropertyAdapter s_freqAdapterInstance;
static const bool s_freqAdapterRegistered = []() {
    registerPropertyAdapter(&s_freqAdapterInstance);
    return true;
}();

FreqWidgetPropertyAdapter::FreqWidgetPropertyAdapter(QObject *parent)
    : IWidgetPropertyAdapter(parent)
{}

QString FreqWidgetPropertyAdapter::typeId() const
{
    return QStringLiteral("FreqWidget");
}

// FreqWidgetPropertyAdapter.cpp

void FreqWidgetPropertyAdapter::onWidgetSelected(const WidgetPropertyContext &ctx)
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
    mw->setFreqPropertyRowsVisible(true);

    const bool useHamlib = lm->widgetFreqUseHamlib(ctx.widgetId);
    const bool showHz = lm->widgetFreqShowHz(ctx.widgetId);

    if (mw->m_propFreqUseHamlibCheck) {
        QSignalBlocker blocker(mw->m_propFreqUseHamlibCheck);
        mw->m_propFreqUseHamlibCheck->setChecked(useHamlib);
    }

    if (mw->m_propFreqShowHzCheck) {
        QSignalBlocker blocker(mw->m_propFreqShowHzCheck);
        mw->m_propFreqShowHzCheck->setChecked(showHz);
    }

    if (auto *fw = qobject_cast<FreqWidget *>(ctx.widget)) {
        fw->setUseHamlib(useHamlib);
        fw->setShowHertz(showHz);
    }
}

bool FreqWidgetPropertyAdapter::handlePropertyChange(const WidgetPropertyContext &ctx,
                                                     const QString &propertyKey,
                                                     const QVariant &newValue)
{
    if (propertyKey == QLatin1String("freq.useHamlib")) {
        MainWindow *mw = ctx.window;
        LayoutManager *lm = ctx.layoutManager;
        const bool useHamlib = newValue.toBool();

        if (lm) {
            lm->setWidgetFreqUseHamlib(ctx.widgetId, useHamlib);
        }

        if (auto *fw = qobject_cast<FreqWidget *>(ctx.widget)) {
            fw->setUseHamlib(useHamlib);
        }

        if (mw) {
            mw->updateRigFreqPollTimer();

            // Give the user an immediate update when Hamlib is first enabled,
            // instead of waiting for the next timer tick.
            if (useHamlib) {
                mw->pollRigFrequencyForFreqWidgets();
            }
        }

        return true;
    }

    if (propertyKey == QLatin1String("freq.showHz")) {
        LayoutManager *lm = ctx.layoutManager;
        const bool showHz = newValue.toBool();

        if (lm) {
            lm->setWidgetFreqShowHz(ctx.widgetId, showHz);
        }

        if (auto *fw = qobject_cast<FreqWidget *>(ctx.widget)) {
            fw->setShowHertz(showHz);
        }

        return true;
    }

    // Let the base class handle any generic "widget.*" properties.
    //return IWidgetPropertyAdapter::handlePropertyChange(ctx, propertyKey, newValue);
    return false;
}

} // namespace raypm
