// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QFrame>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>
#include "widgets/auxdigitalwidget.h"
#include "widgets/auxsensorwidget.h"
#include "widgets/freqwidget.h"
#include "widgets/gainwidget.h"
#include "widgets/miniscopewidget.h"
#include "widgets/powerbarwidget.h"

class QWidget;
class QGroupBox;
class PowerBarWidget;
class WidgetBase;

namespace raypm {

class RuntimeDataModel;
class ForwardRefPanel;
struct RayCouplerConfig;

// Builds the main-page layout from JSON and tracks
// group <-> coupler bindings so group titles can be updated
// when coupler configs arrive.
class LayoutManager : public QObject
{
    Q_OBJECT

public:
    explicit LayoutManager(RuntimeDataModel *model, QObject *parent = nullptr);

    // Return all widgets whose WidgetBase::source() matches the given string,
    // e.g. "Coupler0", "Coupler1", etc.
    QVector<WidgetBase *> widgetsForSource(const QString &source) const;

    // Convenience: return widgets bound to the given coupler index.
    // This just calls widgetsForSource("Coupler%1").
    QVector<WidgetBase *> widgetsForCoupler(int couplerIndex) const;

    // Convenience: all widget ids currently in the layout
    QStringList allWidgetIds() const;

    // Build layout into centralWidget. Returns the primary power bar +
    // forward/ref panel (for legacy usage in MainWindow).
    void buildInitialLayout(QWidget *centralWidget,
                            PowerBarWidget *&outPrimaryPowerBar,
                            ForwardRefPanel *&outPrimaryForwardRefPanel);

    // Reset to built-in default JSON layout
    void resetToDefaultLayout(QWidget *centralWidget,
                              PowerBarWidget *&outPrimaryPowerBar,
                              ForwardRefPanel *&outPrimaryForwardRefPanel);

    // Load layout JSON from file and rebuild UI
    bool loadLayoutFromFile(const QString &filePath,
                            QWidget *centralWidget,
                            PowerBarWidget *&outPrimaryPowerBar,
                            ForwardRefPanel *&outPrimaryForwardRefPanel);

    // Save current layout JSON to file
    bool saveLayoutToFile(const QString &filePath) const;

    // Create an empty layout with the requested column count (1–4).
    // Clears the model and rebuilds the UI grid with no groups.
    void createEmptyLayout(int cols,
                           QWidget *centralWidget,
                           PowerBarWidget *&outPrimaryPowerBar,
                           ForwardRefPanel *&outPrimaryForwardRefPanel);

    // Called when a coupler config arrives so we can update group titles, etc.
    void applyCouplerConfig(const RayCouplerConfig &cfg);

    PowerBarWidget *primaryPowerBarForCoupler(int couplerIndex) const;

    // Design-mode group selection highlight
    void setSelectedGroup(const QString &groupId);
    QString selectedGroupId() const { return m_selectedGroupId; }

    // Group-level properties (for the Group Properties panel)
    bool groupBinding(const QString &groupId, QString &outTitle, int &outCouplerIndex) const;
    bool groupBindingType(const QString &groupId, QString &outBindingType) const;
    void setGroupTitle(const QString &groupId, const QString &title);
    void setGroupCouplerIndex(const QString &groupId, int couplerIndex);

    // Design mode control
    void setDesignModeEnabled(bool enabled);
    bool designModeEnabled() const { return m_designModeEnabled; }

    // Query / update properties for a given widget tile by widgetId
    bool widgetMargin(const QString &widgetId, int &outMargin) const;
    void setWidgetMargin(const QString &widgetId, int margin);

    bool setWidgetMinHeight(const QString &widgetId, int minHeight);
    bool widgetMinHeight(const QString &widgetId, int &outMinHeight) const;

    // Widget-level source override
    bool widgetSource(const QString &widgetId, QString &outSource) const;
    void setWidgetSource(const QString &widgetId, const QString &source);

    // Widget-level label override
    bool widgetLabel(const QString &widgetId, QString &outLabel) const;
    void setWidgetLabel(const QString &widgetId, const QString &label);

    bool widgetVisible(const QString &widgetId) const;
    void setWidgetVisible(const QString &widgetId, bool visible);

    // NEW: PowerBar-specific properties
    bool widgetShowTicks(const QString &widgetId, bool &outShowTicks) const;
    void setWidgetShowTicks(const QString &widgetId, bool show);

    bool widgetShowTickLabels(const QString &widgetId, bool &outShowLabels) const;
    void setWidgetShowTickLabels(const QString &widgetId, bool show);

    bool widgetShowThresholdLabels(const QString &widgetId, bool &outShow) const;
    void setWidgetShowThresholdLabels(const QString &widgetId, bool show);

    bool widgetMeterMode(const QString &widgetId, PowerBarWidget::MeterMode &outMode) const;
    void setWidgetMeterMode(const QString &widgetId, PowerBarWidget::MeterMode mode);

    // --- GainWidget helpers ---
    bool widgetGainSources(const QString &widgetId, QString &rfInSource, QString &rfOutSource) const;
    void setWidgetGainSources(const QString &widgetId,
                              const QString &rfInSource,
                              const QString &rfOutSource);
    bool widgetGainAttnDb(const QString &widgetId, double &outAttnDb) const;
    void setWidgetGainAttnDb(const QString &widgetId, double attnDb);

    // --- AuxSensorWidget helpers ---

    // Summary: Returns the persisted AuxSensorWidget config (mode, temp units, label)
    // for the given widget ID. Returns false if the widget is not an AuxSensor.
    bool widgetAuxSensorConfig(const QString &widgetId,
                               AuxSensorWidget::Mode &outMode,
                               int &outTempUnitsIndex,
                               QString &outLabel) const;

    // Summary: Updates the persisted AuxSensorWidget config (mode, temp units, label)
    // for the given widget ID and rebuilds the current layout JSON.
    void setWidgetAuxSensorConfig(const QString &widgetId,
                                  AuxSensorWidget::Mode mode,
                                  int tempUnitsIndex,
                                  const QString &label);

    // --- AuxDigitalWidget helpers ---
    bool widgetAuxDigitalMode(const QString &widgetId, AuxDigitalWidget::Mode &outMode) const;
    void setWidgetAuxDigitalMode(const QString &widgetId, AuxDigitalWidget::Mode mode);

    // Access the underlying WidgetBase* by id (used by property panel)
    WidgetBase *widgetObject(const QString &widgetId) const;
    QString widgetIdForObject(QWidget *w) const;

    // All GainWidgets currently in the layout (used by telemetry)
    QVector<GainWidget *> gainWidgets() const;

    // NEW: returns the JSON "type" (e.g. "PowerBar", "ForwardRefPanel")
    QString widgetType(const QString &widgetId) const;

    // Direction for moving widgets inside a group
    enum class MoveDirection { Up, Down };

    // Direction for moving whole groups in the top-level grid
    enum class GroupMoveDirection { Up, Down, Left, Right };

    // Where to insert a new group relative to an existing one
    enum class GroupInsertPos { Above, Below, Left, Right };

    // Add a widget of the given JSON "type" (e.g. "PowerBar", "ForwardRefPanel")
    // into the given group. Rebuilds the layout and returns the new widgetId,
    // or an empty string on failure.
    QString addWidgetToGroup(const QString &groupId,
                             const QString &type,
                             QWidget *centralWidget,
                             PowerBarWidget *&outPrimaryPowerBar,
                             ForwardRefPanel *&outPrimaryForwardRefPanel);

    // Remove a widget (and its container) from the layout; returns true on success.
    bool removeWidget(const QString &widgetId,
                      QWidget *centralWidget,
                      PowerBarWidget *&outPrimaryPowerBar,
                      ForwardRefPanel *&outPrimaryForwardRefPanel);

    // Move an entire group in the top-level grid (Up/Down/Left/Right).
    bool moveGroup(const QString &groupId,
                   GroupMoveDirection dir,
                   QWidget *centralWidget,
                   PowerBarWidget *&outPrimaryPowerBar,
                   ForwardRefPanel *&outPrimaryForwardRefPanel);

    // After a rebuild, highlight a widget by its id (if present).
    bool highlightWidget(const QString &widgetId);

    // Move a widget up or down within its group's widget list.
    bool moveWidget(const QString &widgetId,
                    MoveDirection dir,
                    QWidget *centralWidget,
                    PowerBarWidget *&outPrimaryPowerBar,
                    ForwardRefPanel *&outPrimaryForwardRefPanel);

    // Add a new group above or below the given base group.
    // Returns the new groupId or an empty string on failure.
    QString addGroupRelativeTo(const QString &baseGroupId,
                               GroupInsertPos pos,
                               QWidget *centralWidget,
                               PowerBarWidget *&outPrimaryPowerBar,
                               ForwardRefPanel *&outPrimaryForwardRefPanel);

    // Remove an entire group (and all its widgets).
    bool removeGroup(const QString &groupId,
                     QWidget *centralWidget,
                     PowerBarWidget *&outPrimaryPowerBar,
                     ForwardRefPanel *&outPrimaryForwardRefPanel);

    void setGroupGridColumns(int cols);
    int groupGridColumns() const;

    // MiniScope-specific persisted properties
    bool widgetMiniScopeSignal(const QString &widgetId, int &outSignal) const;
    void setWidgetMiniScopeSignal(const QString &widgetId, int signal);

    bool widgetMiniScopeWindowSec(const QString &widgetId, double &outSec) const;
    void setWidgetMiniScopeWindowSec(const QString &widgetId, double sec);

    bool widgetMiniScopeAutoScale(const QString &widgetId, bool &outAuto) const;
    void setWidgetMiniScopeAutoScale(const QString &widgetId, bool enabled);

    // convenience
    MiniScopeWidget *miniScopeForId(const QString &widgetId) const;

    // FreqWidget helpers (Hamlib integration)
    bool widgetFreqUseHamlib(const QString &widgetId) const;
    void setWidgetFreqUseHamlib(const QString &widgetId, bool useHamlib);
    bool widgetFreqShowHz(const QString &widgetId) const;
    void setWidgetFreqShowHz(const QString &widgetId, bool showHz);
    bool anyFreqWidgetUsesHamlib() const;

signals:
    // Emitted when the user clicks a widget in Design Mode
    void widgetClicked(const QString &groupId, const QString &widgetId);

    // Emitted when a widget requests Properties... (runtime)
    void widgetPropertiesRequested(const QString &groupId, const QString &widgetId);

protected:
    // For hit-testing clicks in design mode
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Data-only description of the current layout, mirrored from JSON.
    struct WidgetLayout
    {
        QString id;     // JSON "id" (e.g. "c0_powerBar")
        QString type;   // JSON "type" ("PowerBar", "ForwardRefPanel", ...)
        QString name;   // Human-friendly name (often same as id for now)
        QString label;  // Display label ("Forward Power", "Forward / Ref / SWR", ...)
        QString source; // Data source (e.g. "Coupler0", "Coupler1")

        int row = 0;
        int column = 0;
        int rowSpan = 1;
        int colSpan = 1;

        int margin = 0;      // cell margin (px)
        bool visible = true; // visible flag

        QJsonObject rawProperties; // full "properties" JSON for future use
    };

    struct GroupLayout
    {
        QString id;          // JSON group id ("coupler0_panel", ...)
        QString title;       // JSON "title"
        QString bindingType; // binding.type (e.g. "coupler")
        int couplerIndex = 0;

        // Explicit grid position in the outer group grid
        int row = 0;
        int column = 0;

        int columns = 1;               // desired width in outer grid (columns)
        int rows = 1;                  // desired height in outer grid (rows)
        QVector<WidgetLayout> widgets; // widgets in JSON order
    };

    QVector<GroupLayout> m_layoutModel; // mirrored layout model
    int m_groupGridColumns = 2;         // number of columns for the top-level group grid

    struct WidgetRef
    {
        QString groupId;
        QString widgetId;
        QString type;                // "PowerBar", "ForwardRefPanel", etc.
        QFrame *container = nullptr; // our QFrame tile
        QWidget *inner = nullptr;    // the actual widget (PowerBar, ForwardRef, etc.)
    };

    QHash<QString, WidgetRef> m_widgetMap; // key = widgetId
    QHash<QWidget *, QString> m_widgetIds; // already there: container -> widgetId

    RuntimeDataModel *m_model = nullptr;
    QPointer<QWidget> m_centralWidget; // remember where we build the layout

    struct GroupInfo
    {
        QString id; // <--- Group id from JSON ("coupler0_panel", etc.)
        int couplerIndex = 0;
        QGroupBox *groupBox = nullptr;
        QString baseTitle; // title from JSON (fallback)

        QVector<PowerBarWidget *> powerBars; // All PowerBars in this group
    };

    QVector<GroupInfo> m_groups;

    QString m_currentLayoutJson;

    // Design mode flag
    bool m_designModeEnabled = false;
    QPointer<QWidget> m_selectedWidget;

    QString m_selectedGroupId;

    // Shared helper for all builders
    void buildLayoutFromJson(const QString &jsonText,
                             QWidget *centralWidget,
                             PowerBarWidget *&outPowerBar,
                             ForwardRefPanel *&outForwardRefPanel);

    void clearSelectionHighlight();
    void applySelectionHighlight(QWidget *w);

    // Layout-model helpers
    QString buildJsonFromModel() const;
    GroupLayout *findGroupLayout(const QString &groupId);
    const GroupLayout *findGroupLayout(const QString &groupId) const;
    WidgetLayout *findWidgetLayout(const QString &widgetId, GroupLayout **outGroup = nullptr);
    const WidgetLayout *findWidgetLayout(const QString &widgetId,
                                         const GroupLayout **outGroup = nullptr) const;

    bool rectsOverlap(int r1, int c1, int rs1, int cs1, int r2, int c2, int rs2, int cs2) const;

    bool canPlaceAt(
        const QList<WidgetLayout> &widgets, int row, int col, int rowSpan, int colSpan) const;
};

} // namespace raypm
