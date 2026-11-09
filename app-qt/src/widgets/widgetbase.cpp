// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "widgetbase.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>

WidgetBase::WidgetBase(QWidget *parent)
    : QWidget(parent)
{}

void WidgetBase::contextMenuEvent(QContextMenuEvent *event)
{
    // If the properties dock is already visible, treat right-click as “retarget selection”
    if (s_isPropertiesDockVisible && s_isPropertiesDockVisible()) {
        emit propertiesRequested(this);
        event->accept();
        return;
    }

    QMenu menu(this);
    QAction *actProps = menu.addAction(tr("Properties..."));

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == actProps)
        emit propertiesRequested(this);

    event->accept();
}

void WidgetBase::setWidgetId(int id)
{
    if (m_widgetId == id)
        return;
    m_widgetId = id;
    emit widgetIdChanged(m_widgetId);
}

void WidgetBase::setWidgetName(const QString &name)
{
    if (m_widgetName == name)
        return;
    m_widgetName = name;
    emit widgetNameChanged(m_widgetName);
}

void WidgetBase::setLabel(const QString &label)
{
    if (m_label == label)
        return;
    m_label = label;
    emit labelChanged(m_label);
}

void WidgetBase::setSource(const QString &source)
{
    if (m_source == source)
        return;
    m_source = source;
    emit sourceChanged(m_source);
}

void WidgetBase::setAppScaleFactor(double scale)
{
    if (scale <= 0.0)
        scale = 1.0;

    if (qFuzzyCompare(m_appScaleFactor, scale))
        return;

    m_appScaleFactor = scale;

    onAppScaleFactorChanged(m_appScaleFactor);
    emit appScaleFactorChanged(m_appScaleFactor);
}

WidgetBase::PropertiesDockVisibleFn WidgetBase::s_isPropertiesDockVisible = nullptr;

void WidgetBase::setPropertiesDockVisibleFn(PropertiesDockVisibleFn fn)
{
    s_isPropertiesDockVisible = fn;
}
