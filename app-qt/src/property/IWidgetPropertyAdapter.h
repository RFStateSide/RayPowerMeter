// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

// IWidgetPropertyAdapter.h
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

class WidgetBase;

namespace raypm {

class MainWindow;
class LayoutManager;

/**
 * @brief Lightweight context describing the widget whose properties
 *        are being edited in the property panel.
 */
struct WidgetPropertyContext
{
    MainWindow *window = nullptr; // owning MainWindow (for access to UI controls)
    LayoutManager *layoutManager = nullptr;
    QString groupId;
    QString widgetId;
    WidgetBase *widget = nullptr; // actual widget object in the layout

    template<typename T>
    T *as() const
    {
        return qobject_cast<T *>(widget);
    }
};

/**
 * @brief Base interface for widget property adapters.
 *
 * Adapters know how to:
 *   - Expose which widget type they handle (typeId)
 *   - React when a widget of that type is selected
 *   - Apply property changes coming from the property panel
 */
class IWidgetPropertyAdapter : public QObject
{
public:
    explicit IWidgetPropertyAdapter(QObject *parent = nullptr)
        : QObject(parent)
    {}

    ~IWidgetPropertyAdapter() override = default;

    /// Returns the widget type id this adapter is for, e.g. "GainWidget".
    virtual QString typeId() const = 0;

    /// Called whenever a widget of this adapter's type is selected.
    virtual void onWidgetSelected(const WidgetPropertyContext &ctx) = 0;

    /// Called when a property in the panel changes. Return true if handled.
    virtual bool handlePropertyChange(const WidgetPropertyContext &ctx,
                                      const QString &propertyKey,
                                      const QVariant &newValue)
        = 0;
};

/// Registry / factory helpers
IWidgetPropertyAdapter *adapterForWidgetType(const QString &typeId);
void registerPropertyAdapter(IWidgetPropertyAdapter *adapter);

/// Convenience helper to build a WidgetPropertyContext for a given widget id.
WidgetPropertyContext makeWidgetPropertyContext(MainWindow *mainWindow,
                                                LayoutManager *layoutManager,
                                                const QString &groupId,
                                                const QString &widgetId);

} // namespace raypm

/*
==============================================================================
Guide: Adding widget properties & data binding via the property adapter pattern
==============================================================================

Goal:
    Make a widget-specific property editable in the Layout Design panel and
    keep it in sync between:
        - the live widget instance
        - the LayoutManager model (rawProperties / JSON layouts)
        - the property panel controls (check boxes, combos, etc.)

Typical examples: FreqWidget.useHamlib, FreqWidget.showHertz, etc.

-------------------------------------------------------------------------------
1) Widget class: define the property
-------------------------------------------------------------------------------
    - Add a Q_PROPERTY to the widget:

        Q_PROPERTY(bool myProp READ myProp WRITE setMyProp
                   NOTIFY myPropChanged)

    - Implement:
        bool  myProp() const;
        void  setMyProp(bool);
    - In the setter:
        * early-return if the value didn’t change
        * update internal state
        * update visuals (e.g. call updateDisplay())
        * emit myPropChanged()

-------------------------------------------------------------------------------
2) LayoutManager: store & restore the property in the layout model
-------------------------------------------------------------------------------
    - In LayoutManager::createWidgetForLayout(...):
        * After constructing the widget and before returning it, read the
          property from wl->rawProperties and apply it to the widget.
          Provide a sane default for older layouts that don’t have it yet.

            const bool myProp =
                props.value("myProp").toBool('default' true);
            widget->setMyProp(myProp);

    - Add convenience helpers in LayoutManager.{h,cpp}:

        bool widgetXxxMyProp(const QString &widgetId) const;
        void setWidgetXxxMyProp(const QString &widgetId, bool value);

      These should:
        * use findWidgetLayout(widgetId, &grp) to locate the WidgetLayout
        * read/write wl->rawProperties["myProp"]
        * after a write, refresh m_currentLayoutJson = buildJsonFromModel()
          so save/load stays consistent.

-------------------------------------------------------------------------------
3) MainWindow: add controls to the property panel
-------------------------------------------------------------------------------
    - In MainWindow::createPropertyPanel():
        * Create the control (e.g. QCheckBox* m_propXxxMyPropCheck)
        * Add it to the form layout
        * Store the pointer in a MainWindow member

    - Add a visibility helper:

        void setXxxPropertyRowsVisible(bool visible);

      This should show/hide both the form label and the control.

    - Connect the control’s signal to a slot:

        connect(m_propXxxMyPropCheck, &QCheckBox::toggled,
                this, &MainWindow::onPropertyXxxMyPropChanged);

    - In the slot:
        * bail if not in design mode or no selected widget
        * get the adapter for the selected widget type
        * build a WidgetPropertyContext
        * call adapter->handlePropertyChange(ctx, "xxx.myProp", value);

-------------------------------------------------------------------------------
4) Concrete adapter: sync between panel, layout, and widget
-------------------------------------------------------------------------------
    - For each widget type, implement or extend a *PropertyAdapter subclass
      (e.g. FreqWidgetPropertyAdapter).

    - onWidgetSelected(const WidgetPropertyContext &ctx):
        * Show the relevant property rows via MainWindow helper(s)
          and hide others (gain, aux, etc.).
        * Read the current value from LayoutManager::widgetXxxMyProp().
        * Push it into the property control using QSignalBlocker to avoid
          recursive signals.
        * Apply it to the actual widget instance (qobject_cast<T*>(ctx.widget))
          so the panel and widget are in sync.

    - handlePropertyChange(...):
        * Check propertyKey (e.g. "xxx.myProp").
        * Update LayoutManager via setWidgetXxxMyProp().
        * Update the widget instance (call setMyProp()).
        * Perform any side effects (timers, polls, etc.).
        * Return true if the property was handled; otherwise return false so
          other handlers can be tried if needed.

-------------------------------------------------------------------------------
5) Adapter registration & selection
-------------------------------------------------------------------------------
    - In MainWindow::initializePropertyAdapters():
        * Register the adapter under the widget’s type string, e.g.:

            m_propertyAdapters.emplace("FreqWidget",
                std::make_unique<FreqWidgetPropertyAdapter>());

    - Ensure the selection flow (when clicking a widget in design mode) builds
      a WidgetPropertyContext and calls adapter->onWidgetSelected(ctx) for the
      selected widget type.

-------------------------------------------------------------------------------
6) Persistence
-------------------------------------------------------------------------------
    - Because the property is stored in WidgetLayout::rawProperties and
      buildJsonFromModel(), it will automatically be saved into the layout
      JSON and restored via createWidgetForLayout() on the next load.

    - For new properties, choose sensible defaults in both:
        * LayoutManager::createWidgetForLayout(...)
        * LayoutManager::widgetXxxMyProp(...)
      so older layouts behave correctly without modification.

End of guide.
==============================================================================
*/
