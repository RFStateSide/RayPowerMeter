// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

// IWidgetPropertyAdapter.cpp
#include "property/IWidgetPropertyAdapter.h"

#include "app/mainwindow.h"
#include "controllers/LayoutManager.h"
#include "widgets/widgetbase.h"

#include <QMap>

namespace raypm {

static QMap<QString, IWidgetPropertyAdapter *> &registry()
{
    static QMap<QString, IWidgetPropertyAdapter *> s_registry;
    return s_registry;
}

void registerPropertyAdapter(IWidgetPropertyAdapter *adapter)
{
    if (!adapter) {
        return;
    }
    registry().insert(adapter->typeId(), adapter);
}

IWidgetPropertyAdapter *adapterForWidgetType(const QString &typeId)
{
    return registry().value(typeId, nullptr);
}

WidgetPropertyContext makeWidgetPropertyContext(MainWindow *mainWindow,
                                                LayoutManager *layoutManager,
                                                const QString &groupId,
                                                const QString &widgetId)
{
    WidgetPropertyContext ctx;
    ctx.window = mainWindow;
    ctx.layoutManager = layoutManager;
    ctx.groupId = groupId;
    ctx.widgetId = widgetId;
    ctx.widget = layoutManager ? layoutManager->widgetObject(widgetId) : nullptr;
    return ctx;
}

} // namespace raypm
