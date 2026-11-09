// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QWidget>

class QContextMenuEvent;

class WidgetBase : public QWidget
{
    Q_OBJECT

    Q_PROPERTY(int widgetId READ widgetId WRITE setWidgetId NOTIFY widgetIdChanged)
    Q_PROPERTY(QString widgetName READ widgetName WRITE setWidgetName NOTIFY widgetNameChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(double AppScaleFactor READ appScaleFactor WRITE setAppScaleFactor NOTIFY
                   appScaleFactorChanged)

public:
    explicit WidgetBase(QWidget *parent = nullptr);

    int widgetId() const { return m_widgetId; }
    void setWidgetId(int id);

    QString widgetName() const { return m_widgetName; }
    void setWidgetName(const QString &name);

    QString label() const { return m_label; }
    void setLabel(const QString &label);

    QString source() const { return m_source; }
    void setSource(const QString &source);

    virtual int layoutRowSpan() const { return 1; }
    virtual int layoutColSpan() const { return 1; }

    double appScaleFactor() const { return m_appScaleFactor; }
    virtual void setAppScaleFactor(double scale);

    using PropertiesDockVisibleFn = bool (*)();
    static void setPropertiesDockVisibleFn(PropertiesDockVisibleFn fn);

protected:
    virtual void onAppScaleFactorChanged(double /*scale*/) {}

    void contextMenuEvent(QContextMenuEvent *event) override;

signals:
    void widgetIdChanged(int id);
    void widgetNameChanged(const QString &name);
    void labelChanged(const QString &label);
    void sourceChanged(const QString &source);
    void appScaleFactorChanged(double scale);

    // Right-click → Properties…
    void propertiesRequested(WidgetBase *w);

private:
    int m_widgetId = -1;
    QString m_widgetName;
    QString m_label;
    QString m_source;
    double m_appScaleFactor = 1.0;

    static PropertiesDockVisibleFn s_isPropertiesDockVisible;
};
