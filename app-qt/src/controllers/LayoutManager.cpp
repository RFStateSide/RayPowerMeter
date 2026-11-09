// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "controllers/LayoutManager.h"
#include "core/defaultlayoutprovider.h"

#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QWidget>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <QDebug>

#include <QFile>
#include <QTextStream>

#include <QEvent>
#include <QHash>
#include <QMouseEvent>

#include "core/RayCouplerConfig.h"
#include "core/RuntimeDataModel.h"
#include "widgets/analogpowermeterwidget.h"
#include "widgets/auxsensorwidget.h"
#include "widgets/auxvoltagewidget.h"
#include "widgets/forwardrefpanel.h"
#include "widgets/freqwidget.h"
#include "widgets/gainwidget.h"
#include "widgets/miniscopewidget.h"
#include "widgets/powerbarvertwidget.h"
#include "widgets/powerbarwidget.h"
#include "widgets/spacerwidget.h"
#include "widgets/widgetbase.h"

namespace {

// Map enum -> string for JSON
QString meterModeToString(PowerBarWidget::MeterMode m)
{
    switch (m) {
    case PowerBarWidget::MeterMode::Live:
        return QStringLiteral("Live");
    case PowerBarWidget::MeterMode::Peak:
        return QStringLiteral("Peak");
    case PowerBarWidget::MeterMode::Freeze:
        return QStringLiteral("Freeze");
    case PowerBarWidget::MeterMode::None:
        return QStringLiteral("None");
    }
    return QStringLiteral("Live");
}

// Map string -> enum for restoring from JSON
PowerBarWidget::MeterMode meterModeFromString(const QString &s)
{
    const QString t = s.trimmed().toLower();
    if (t == QLatin1String("peak"))
        return PowerBarWidget::MeterMode::Peak;
    if (t == QLatin1String("freeze"))
        return PowerBarWidget::MeterMode::Freeze;
    if (t == QLatin1String("none"))
        return PowerBarWidget::MeterMode::None;
    return PowerBarWidget::MeterMode::Live;
}

// Default layout JSON with a single group bound to coupler index 0.
// Later we can move this to a file and/or add additional groups.
static const char kDefaultLayoutJson[] = R"JSON(
{
  "schemaVersion": 1,
  "groupGridColumns": 2,
  "groups": [
    {
      "id": "coupler0_panel",
      "title": "Live Power Meter",
      "binding": {
        "type": "coupler",
        "couplerIndex": 0
      },
      "layout": {
        "columns": 1
      },
      "widgets": [
        {
          "id": "c0_powerBar",
          "type": "PowerBar",
          "row": 0,
          "column": 0,
          "rowSpan": 1,
          "colSpan": 1
        },
        {
          "id": "c0_forwardRef",
          "type": "ForwardRefPanel",
          "row": 1,
          "column": 0,
          "rowSpan": 1,
          "colSpan": 1
        }
      ]
    },
    {
      "id": "coupler1_panel",
      "title": "Coupler 2",
      "binding": {
        "type": "coupler",
        "couplerIndex": 1
      },
      "layout": {
        "columns": 1
      },
      "widgets": [
        {
          "id": "c1_powerBar",
          "type": "PowerBar",
          "row": 0,
          "column": 0,
          "rowSpan": 1,
          "colSpan": 1
        }
      ]
    }
  ]
}
)JSON";

} // anonymous namespace

namespace raypm {

//---- Helper to install event filter recursively on all child widgets ----
static void installDesignFiltersRecursive(QWidget *root, QObject *filter)
{
    if (!root)
        return;
    root->installEventFilter(filter);
    const auto kids = root->findChildren<QWidget *>();
    for (QWidget *w : kids)
        w->installEventFilter(filter);
}

LayoutManager::LayoutManager(RuntimeDataModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
    m_currentLayoutJson = QString::fromUtf8(kDefaultLayoutJson);

    // Hard fallback so you always boot even if the resource is missing
    if (m_currentLayoutJson.trimmed().isEmpty()) {
        m_currentLayoutJson = QStringLiteral(
            "{\"schemaVersion\":1,\"groupGridColumns\":2,\"groups\":[]}");
    }
}

LayoutManager::GroupLayout *LayoutManager::findGroupLayout(const QString &groupId)
{
    for (GroupLayout &g : m_layoutModel) {
        if (g.id == groupId)
            return &g;
    }
    return nullptr;
}

const LayoutManager::GroupLayout *LayoutManager::findGroupLayout(const QString &groupId) const
{
    for (const GroupLayout &g : m_layoutModel) {
        if (g.id == groupId)
            return &g;
    }
    return nullptr;
}

LayoutManager::WidgetLayout *LayoutManager::findWidgetLayout(const QString &widgetId,
                                                             GroupLayout **outGroup)
{
    for (GroupLayout &g : m_layoutModel) {
        for (WidgetLayout &w : g.widgets) {
            if (w.id == widgetId) {
                if (outGroup)
                    *outGroup = &g;
                return &w;
            }
        }
    }
    if (outGroup)
        *outGroup = nullptr;
    return nullptr;
}

const LayoutManager::WidgetLayout *LayoutManager::findWidgetLayout(
    const QString &widgetId, const GroupLayout **outGroup) const
{
    for (const GroupLayout &g : m_layoutModel) {
        for (const WidgetLayout &w : g.widgets) {
            if (w.id == widgetId) {
                if (outGroup)
                    *outGroup = &g;
                return &w;
            }
        }
    }
    if (outGroup)
        *outGroup = nullptr;
    return nullptr;
}

bool LayoutManager::widgetGainSources(const QString &widgetId,
                                      QString &rfInSource,
                                      QString &rfOutSource) const
{
    const WidgetLayout *w = findWidgetLayout(widgetId, nullptr);
    if (!w)
        return false;

    const QJsonObject &props = w->rawProperties;
    rfInSource = props.value(QStringLiteral("rfInSource")).toString();
    rfOutSource = props.value(QStringLiteral("rfOutSource")).toString();
    return true;
}

void LayoutManager::setWidgetGainSources(const QString &widgetId,
                                         const QString &rfInSource,
                                         const QString &rfOutSource)
{
    WidgetLayout *w = findWidgetLayout(widgetId, nullptr);
    if (!w)
        return;

    QJsonObject props = w->rawProperties;
    props.insert(QStringLiteral("rfInSource"), rfInSource);
    props.insert(QStringLiteral("rfOutSource"), rfOutSource);
    w->rawProperties = props;

    if (!rfOutSource.isEmpty())
        w->source = rfOutSource;

    // Update live widget instance
    if (auto *gw = qobject_cast<GainWidget *>(widgetObject(widgetId))) {
        gw->setRfInSource(rfInSource);
        gw->setRfOutSource(rfOutSource);
    }

    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::widgetGainAttnDb(const QString &widgetId, double &outAttnDb) const
{
    const WidgetLayout *w = findWidgetLayout(widgetId, nullptr);
    if (!w)
        return false;

    if (w->type != QLatin1String("GainWidget"))
        return false;

    const QJsonObject &props = w->rawProperties;
    outAttnDb = props.value(QStringLiteral("attnDb")).toDouble(0.0);
    return true;
}

void LayoutManager::setWidgetGainAttnDb(const QString &widgetId, double attnDb)
{
    WidgetLayout *w = findWidgetLayout(widgetId, nullptr);
    if (!w)
        return;

    if (w->type != QLatin1String("GainWidget"))
        return;

    QJsonObject props = w->rawProperties;
    props.insert(QStringLiteral("attnDb"), attnDb);
    w->rawProperties = props;

    // Update live widget instance
    if (auto *gw = qobject_cast<GainWidget *>(widgetObject(widgetId)))
        gw->setAttnDb(attnDb);

    m_currentLayoutJson = buildJsonFromModel();
}

// --- AuxSensorWidget helpers ----------------------------------------------

// Summary: Returns the persisted AuxSensorWidget config (mode, temp units, label)
// for the given widget ID. Returns false if the widget is not an AuxSensor.
bool LayoutManager::widgetAuxSensorConfig(const QString &widgetId,
                                          AuxSensorWidget::Mode &outMode,
                                          int &outTempUnitsIndex,
                                          QString &outLabel) const
{
    const WidgetLayout *w = findWidgetLayout(widgetId, nullptr);
    if (!w)
        return false;

    if (w->type != QLatin1String("AuxSensor"))
        return false;

    const QJsonObject &props = w->rawProperties;

    const int modeInt = props.value(QStringLiteral("auxMode"))
                            .toInt(static_cast<int>(AuxSensorWidget::Mode::Unknown));
    outMode = static_cast<AuxSensorWidget::Mode>(modeInt);

    outTempUnitsIndex = props.value(QStringLiteral("auxTempUnitsIndex")).toInt(0);
    outLabel = w->label;

    return true;
}

// Summary: Updates the persisted AuxSensorWidget config (mode, temp units, label)
// for the given widget ID and rebuilds the current layout JSON.
void LayoutManager::setWidgetAuxSensorConfig(const QString &widgetId,
                                             AuxSensorWidget::Mode mode,
                                             int tempUnitsIndex,
                                             const QString &label)
{
    WidgetLayout *w = findWidgetLayout(widgetId, nullptr);
    if (!w)
        return;

    if (w->type != QLatin1String("AuxSensor"))
        return;

    QJsonObject props = w->rawProperties;
    props.insert(QStringLiteral("auxMode"), static_cast<int>(mode));
    props.insert(QStringLiteral("auxTempUnitsIndex"), tempUnitsIndex);
    props.insert(QStringLiteral("label"), label);

    w->rawProperties = props;
    w->label = label;

    // Update live widget instance
    if (auto *sensor = qobject_cast<AuxSensorWidget *>(widgetObject(widgetId))) {
        sensor->setMode(mode);
        sensor->setTempUnitsIndex(tempUnitsIndex);
        sensor->setLabel(label);
    }

    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::widgetAuxDigitalMode(const QString &widgetId,
                                         AuxDigitalWidget::Mode &outMode) const
{
    const WidgetLayout *w = findWidgetLayout(widgetId, nullptr);
    if (!w)
        return false;

    if (w->type != QLatin1String("AuxDigital"))
        return false;

    const QJsonObject &props = w->rawProperties;
    const int modeInt = props.value(QStringLiteral("auxDigMode"))
                            .toInt(static_cast<int>(AuxDigitalWidget::Mode::Disabled));
    outMode = static_cast<AuxDigitalWidget::Mode>(modeInt);
    return true;
}

void LayoutManager::setWidgetAuxDigitalMode(const QString &widgetId, AuxDigitalWidget::Mode mode)
{
    WidgetLayout *w = findWidgetLayout(widgetId, nullptr);
    if (!w)
        return;

    if (w->type != QLatin1String("AuxDigital"))
        return;

    QJsonObject props = w->rawProperties;
    props.insert(QStringLiteral("auxDigMode"), static_cast<int>(mode));
    w->rawProperties = props;

    // Update live widget instance
    if (auto *dig = qobject_cast<AuxDigitalWidget *>(widgetObject(widgetId)))
        dig->setMode(mode);

    m_currentLayoutJson = buildJsonFromModel();
}

WidgetBase *LayoutManager::widgetObject(const QString &widgetId) const
{
    auto it = m_widgetMap.constFind(widgetId);
    if (it == m_widgetMap.constEnd())
        return nullptr;

    const WidgetRef &ref = it.value();
    if (!ref.inner)
        return nullptr;

    return qobject_cast<WidgetBase *>(ref.inner);
}

QString LayoutManager::widgetIdForObject(QWidget *w) const
{
    auto it = m_widgetIds.constFind(w);
    if (it == m_widgetIds.constEnd())
        return {};
    return it.value();
}

QVector<GainWidget *> LayoutManager::gainWidgets() const
{
    QVector<GainWidget *> result;
    const auto refs = m_widgetMap.values();
    for (const WidgetRef &ref : refs) {
        if (!ref.inner)
            continue;
        if (auto *gw = qobject_cast<GainWidget *>(ref.inner))
            result.append(gw);
    }
    return result;
}

QString LayoutManager::buildJsonFromModel() const
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 2);
    root.insert(QStringLiteral("groupGridColumns"), m_groupGridColumns);

    QJsonArray groupsArr;

    for (const GroupLayout &g : m_layoutModel) {
        QJsonObject gObj;
        QJsonObject gl;
        gl.insert(QStringLiteral("row"), g.row);
        gl.insert(QStringLiteral("column"), g.column);
        gl.insert(QStringLiteral("columns"), g.columns);
        gl.insert(QStringLiteral("rows"), g.rows);
        gObj.insert(QStringLiteral("layout"), gl);
        gObj.insert(QStringLiteral("id"), g.id);
        gObj.insert(QStringLiteral("title"), g.title);

        QJsonObject binding;
        if (!g.bindingType.isEmpty())
            binding.insert(QStringLiteral("type"), g.bindingType);
        binding.insert(QStringLiteral("couplerIndex"), g.couplerIndex);
        gObj.insert(QStringLiteral("binding"), binding);

        QJsonObject layoutObj;
        layoutObj.insert(QStringLiteral("row"), g.row);
        layoutObj.insert(QStringLiteral("column"), g.column);
        layoutObj.insert(QStringLiteral("columns"), g.columns);
        layoutObj.insert(QStringLiteral("rows"), g.rows);
        gObj.insert(QStringLiteral("layout"), layoutObj);

        QJsonArray ws;
        for (const WidgetLayout &w : g.widgets) {
            QJsonObject wObj;
            wObj.insert(QStringLiteral("id"), w.id);
            wObj.insert(QStringLiteral("type"), w.type);
            wObj.insert(QStringLiteral("row"), w.row);
            wObj.insert(QStringLiteral("column"), w.column);
            wObj.insert(QStringLiteral("rowSpan"), w.rowSpan);
            wObj.insert(QStringLiteral("colSpan"), w.colSpan);

            QJsonObject props = w.rawProperties;

            // Keep canonical fields in sync with the struct
            props.insert(QStringLiteral("margin"), w.margin);
            props.insert(QStringLiteral("visible"), w.visible);
            if (!w.source.isEmpty())
                props.insert(QStringLiteral("source"), w.source);

            // Keep widget label in sync (critical for AuxSensor labels surviving rebuilds)
            if (w.label.isEmpty())
                props.remove(QStringLiteral("label"));
            else
                props.insert(QStringLiteral("label"), w.label);

            // For GainWidget, rely on the layout model's rawProperties.
            // They are kept up to date by setWidgetGainSources() called from MainWindow.
            // (Optional debug if you want to verify what is being written:)
            if (w.type == QLatin1String("GainWidget")) {
                const QString inSrc = props.value(QStringLiteral("rfInSource")).toString();
                const QString outSrc = props.value(QStringLiteral("rfOutSource")).toString();
                qDebug() << "[LayoutManager] Saving GainWidget from model" << w.id
                         << "sources:" << inSrc << "/" << outSrc;
            }

            wObj.insert(QStringLiteral("properties"), props);
            ws.append(wObj);
        }

        gObj.insert(QStringLiteral("widgets"), ws);
        groupsArr.append(gObj);
    }

    root.insert(QStringLiteral("groups"), groupsArr);

    QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

void LayoutManager::applyCouplerConfig(const RayCouplerConfig &cfg)
{
    if (m_groups.isEmpty())
        return;

    // Only update the primary coupler panel:
    //   coupler0_panel, coupler1_panel, coupler2_panel, coupler3_panel, ...
    const QString expectedPanelId = QStringLiteral("coupler%1_panel").arg(cfg.index);

    for (GroupInfo &g : m_groups) {
        if (!g.groupBox)
            continue;

        // This ensures we do NOT touch extra groups like "coupler1_panel_group"
        if (g.id != expectedPanelId)
            continue;

        // Base title for this coupler: use configured name if present,
        // otherwise fall back to whatever we already had.
        QString baseTitle = cfg.name.trimmed();
        if (baseTitle.isEmpty()) {
            baseTitle = g.baseTitle.isEmpty() ? g.groupBox->title() : g.baseTitle;
        }

        g.baseTitle = baseTitle;

        // Visible title can include scale, e.g. "Coupler 1 (100 W)"
        QString visibleTitle = baseTitle;
        if (cfg.fullScaleW > 0.0) {
            visibleTitle = QStringLiteral("%1 (%2 W)").arg(baseTitle).arg(cfg.fullScaleW, 0, 'f', 0);
        }

        g.groupBox->setTitle(visibleTitle);
        break; // only the primary panel for this coupler index
    }
}

PowerBarWidget *LayoutManager::primaryPowerBarForCoupler(int couplerIndex) const
{
    for (const GroupInfo &g : m_groups) {
        if (g.couplerIndex == couplerIndex && !g.powerBars.isEmpty()) {
            return g.powerBars.first();
        }
    }
    return nullptr;
}

void LayoutManager::buildLayoutFromJson(const QString &jsonText,
                                        QWidget *centralWidget,
                                        PowerBarWidget *&outPowerBar,
                                        ForwardRefPanel *&outForwardRefPanel)
{
    outPowerBar = nullptr;
    outForwardRefPanel = nullptr;

    if (!centralWidget)
        return;

    // Remember the central widget so dynamic operations can rebuild.
    m_centralWidget = centralWidget;

    // Get or create top-level grid layout
    QLayout *existingLayout = centralWidget->layout();
    QGridLayout *grid = qobject_cast<QGridLayout *>(existingLayout);

    if (!grid) {
        // If there was some other layout, clear and delete it
        if (existingLayout) {
            while (QLayoutItem *item = existingLayout->takeAt(0)) {
                if (QWidget *w = item->widget())
                    w->deleteLater();
                delete item;
            }
            delete existingLayout;
        }

        // Compact tuning (was 20 margins / 12 spacing)
        static constexpr int kOuterMargin = 10;
        static constexpr int kOuterSpacing = 8;

        grid = new QGridLayout(centralWidget);
        grid->setContentsMargins(kOuterMargin, kOuterMargin, kOuterMargin, kOuterMargin);
        grid->setSpacing(kOuterSpacing);
        centralWidget->setLayout(grid);
    } else {
        // Reuse existing grid: remove old items/widgets
        while (QLayoutItem *item = grid->takeAt(0)) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
    }

    // Clear runtime bookkeeping
    m_groups.clear();      // Clear previous group tracking
    m_widgetIds.clear();   // Clear previous widget ID tracking
    m_layoutModel.clear(); // Clear previous in-memory layout model
    m_widgetMap.clear();   // Clear widgetId -> WidgetRef map (avoid stale pointers)

    // Parse JSON
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[LayoutManager] Failed to parse layout JSON:" << perr.errorString();
        return;
    }

    const QJsonObject root = doc.object();

    // Group grid column count from JSON (default 2 if missing)
    m_groupGridColumns = root.value(QStringLiteral("groupGridColumns")).toInt(2);

    const QJsonArray groups = root.value(QStringLiteral("groups")).toArray();
    if (groups.isEmpty()) {
        // Empty layouts are valid (Phase A: "New Layout").
        // We already cleared the UI grid and internal bookkeeping above,
        // and we already applied groupGridColumns from JSON.
        // Nothing else to build.
        if (m_designModeEnabled) {
            // Re-apply design mode styling/filtering so the editor stays consistent.
            setDesignModeEnabled(true);
        }
        return;
    }

    // --- Fallback packer state for layouts that omit group row/column ---
    // We do NOT use this for groups that explicitly specify row/column.
    int packRow = 0;
    int packCol = 0;
    int packRowSpanMax = 1;

    auto advancePackRow = [&]() {
        packRow += packRowSpanMax;
        packCol = 0;
        packRowSpanMax = 1;
    };

    // Build groups/widgets
    for (const QJsonValue &gVal : groups) {
        const QJsonObject gObj = gVal.toObject();
        const QString groupId = gObj.value(QStringLiteral("id")).toString();
        const QString title = gObj.value(QStringLiteral("title")).toString(QObject::tr("Group"));

        const QJsonObject binding = gObj.value(QStringLiteral("binding")).toObject();
        const int couplerIndex = binding.value(QStringLiteral("couplerIndex")).toInt(0);

        auto *groupBox = new QGroupBox(title, centralWidget);
        auto *gLayout = new QGridLayout(groupBox);
        // Compact tuning (was 12 margins / 8 spacing)
        static constexpr int kGroupMargin = 3;
        static constexpr int kGroupSpacing = 2;

        gLayout->setContentsMargins(kGroupMargin, kGroupMargin, kGroupMargin, kGroupMargin);
        gLayout->setSpacing(kGroupSpacing);

        GroupInfo info;
        info.id = groupId;
        info.couplerIndex = couplerIndex;
        info.groupBox = groupBox;
        info.baseTitle = title;

        // Build GroupLayout entry for the in-memory model
        GroupLayout groupLayout;
        groupLayout.id = groupId;
        groupLayout.title = title;
        groupLayout.bindingType = binding.value(QStringLiteral("type")).toString();
        groupLayout.couplerIndex = couplerIndex;

        const QJsonObject layoutObj = gObj.value(QStringLiteral("layout")).toObject();
        groupLayout.columns = layoutObj.value(QStringLiteral("columns")).toInt(1);
        groupLayout.rows = layoutObj.value(QStringLiteral("rows")).toInt(1); // default 1 if missing
        const bool hasRow = layoutObj.contains(QStringLiteral("row"));
        const bool hasCol = layoutObj.contains(QStringLiteral("column"));

        // Explicit group position (row/column) if present, else mark for fallback pack.
        if (hasRow && hasCol) {
            groupLayout.row = layoutObj.value(QStringLiteral("row")).toInt(0);
            groupLayout.column = layoutObj.value(QStringLiteral("column")).toInt(0);
        } else {
            groupLayout.row = -1;
            groupLayout.column = -1;
        }

        m_groups.push_back(info);
        GroupInfo &storedInfo = m_groups.back();

        const QJsonArray widgets = gObj.value(QStringLiteral("widgets")).toArray();
        for (const QJsonValue &wVal : widgets) {
            const QJsonObject wObj = wVal.toObject();
            const QString type = wObj.value(QStringLiteral("type")).toString();
            const QString widgetId = wObj.value(QStringLiteral("id")).toString();

            const int row = wObj.value(QStringLiteral("row")).toInt(0);
            const int col = wObj.value(QStringLiteral("column")).toInt(0);
            const int rowSpan = wObj.value(QStringLiteral("rowSpan")).toInt(1);
            const int colSpan = wObj.value(QStringLiteral("colSpan")).toInt(1);

            // Read optional properties
            const QJsonObject props = wObj.value(QStringLiteral("properties")).toObject();
            const int marginFromJson = props.value(QStringLiteral("margin")).toInt(0);
            const bool visibleFromJson = props.value(QStringLiteral("visible")).toBool(true);
            const int minHeightFromJson = props.value(QStringLiteral("minHeight")).toInt(0);
            const QString sourceFromJson = props.value(QStringLiteral("source"))
                                               .toString(); // widget-level source

            // PowerBar-specific properties
            const QString meterModeStr = props.value(QStringLiteral("meterMode")).toString();
            const bool showTicksFromJson = props.value(QStringLiteral("showTicks")).toBool(true);
            const bool showLabelsFromJson = props.value(QStringLiteral("showTickLabels"))
                                                .toBool(true);
            const bool showThresholdLabelsFromJson
                = props.value(QStringLiteral("showThresholdLabels")).toBool(true);

            // Create a container "tile" for this widget --------------------
            auto *cell = new QFrame(groupBox);
            cell->setFrameShape(QFrame::NoFrame);
            cell->setLineWidth(0);
            cell->setObjectName(widgetId + QStringLiteral("_container"));

            // Layout inside the cell so the inner widget fills it completely
            auto *cellLayout = new QVBoxLayout(cell);
            cellLayout->setContentsMargins(0, 0, 0, 0);
            cellLayout->setSpacing(0);

            // Use margin from JSON (default 0)
            const int m = qMax(0, marginFromJson);
            cellLayout->setContentsMargins(m, m, m, m);

            QWidget *innerWidget = nullptr;

            if (type == QLatin1String("PowerBar")) {
                auto *powerBar = new PowerBarWidget(cell);

                powerBar->setProperty("keepFont", true);
                powerBar->setLabelText(QObject::tr("Forward Power"));
                powerBar->setPowerValueUnits(" W");
                powerBar->setMaxPower(1800);
                powerBar->setThresholds(1200, 1500);
                powerBar->setTickConfig(200, 100, true);
                powerBar->setShowTicks(showTicksFromJson);
                powerBar->setShowTickLabels(showLabelsFromJson);
                powerBar->setShowThresholdLabels(showThresholdLabelsFromJson);
                powerBar->setPeakHoldProfileScaled(500, 0.07, 1200, 0.025);

                PowerBarWidget::MeterMode mm = PowerBarWidget::MeterMode::Live;
                if (!meterModeStr.isEmpty())
                    mm = meterModeFromString(meterModeStr);
                powerBar->setMeterMode(mm);
                powerBar->setRightLabelText(QObject::tr("Ref (W):"));
                powerBar->setRightValue(0.0);

                // Track as a "power bar" for coupler scaling
                storedInfo.powerBars.append(powerBar);
                innerWidget = powerBar;

            } else if (type == QLatin1String("PowerBarVert")) {
                auto *powerBar = new PowerBarVertWidget(cell);

                powerBar->setProperty("keepFont", true);
                powerBar->setLabelText(QObject::tr("Forward Power"));
                powerBar->setPowerValueUnits(" W");
                powerBar->setMaxPower(1800);
                powerBar->setThresholds(1200, 1500);
                powerBar->setTickConfig(200, 100, true);
                powerBar->setShowTicks(showTicksFromJson);
                powerBar->setShowTickLabels(showLabelsFromJson);
                powerBar->setShowThresholdLabels(showThresholdLabelsFromJson);
                powerBar->setPeakHoldProfileScaled(500, 0.07, 1200, 0.025);

                PowerBarWidget::MeterMode mm = PowerBarWidget::MeterMode::Live;
                if (!meterModeStr.isEmpty())
                    mm = meterModeFromString(meterModeStr);
                powerBar->setMeterMode(mm);
                powerBar->setRightLabelText(QObject::tr("Ref (W):"));
                powerBar->setRightValue(0.0);

                // Treat vertical bars as power bars for scaling and coupler config
                innerWidget = powerBar;

                // NEW: if this vertical bar spans multiple rows, hint that the group wants extra height
                if (rowSpan > 1 && groupBox) {
                    const int factor = qMax(1, rowSpan);

                    int baseMin = groupBox->minimumSizeHint().height();
                    if (baseMin <= 0)
                        baseMin = groupBox->sizeHint().height();
                    if (baseMin <= 0)
                        baseMin = 140; // sensible fallback

                    const int targetMinHeight = baseMin * factor;

                    groupBox->setMinimumHeight(targetMinHeight);
                    groupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

                    qDebug() << "[LayoutManager] PowerBarVert group" << groupId
                             << "rowSpan=" << rowSpan << "minHeight=" << targetMinHeight;
                }

            } else if (type == QLatin1String("AnalogPowerMeter")) {
                auto *m = new AnalogPowerMeterWidget(cell);

                m->setProperty("keepFont", true);
                m->setMaxPower(1800);
                m->setThresholds(1200, 1500);

                // reuse same persisted properties
                m->setShowTicks(showTicksFromJson);
                m->setShowTickLabels(showLabelsFromJson);
                m->setShowThresholdLabels(showThresholdLabelsFromJson);

                PowerBarWidget::MeterMode mm = PowerBarWidget::MeterMode::Live;
                if (!meterModeStr.isEmpty())
                    mm = meterModeFromString(meterModeStr);
                m->setMeterMode(mm);

                innerWidget = m;
            } else if (type == QLatin1String("ForwardRefPanel")) {
                auto *panel = new ForwardRefPanel(cell);
                innerWidget = panel;

                if (!outForwardRefPanel)
                    outForwardRefPanel = panel;

            } else if (type == QLatin1String("AuxSensor")) {
                auto *sensor = new AuxSensorWidget(cell);
                innerWidget = sensor;

                // Restore persisted AuxSensor config (if present)
                const int modeInt = props.value(QStringLiteral("auxMode"))
                                        .toInt(static_cast<int>(AuxSensorWidget::Mode::Unknown));
                sensor->setMode(static_cast<AuxSensorWidget::Mode>(modeInt));

                const int tempUnitsIndex = props.value(QStringLiteral("auxTempUnitsIndex")).toInt(0);
                sensor->setTempUnitsIndex(tempUnitsIndex);

                const QString label = props.value(QStringLiteral("label")).toString();
                if (!label.isEmpty())
                    sensor->setLabel(label);

            } else if (type == QLatin1String("AuxVoltage")) {
                // Legacy / backwards-compat widget
                auto *aux = new AuxVoltageWidget(cell);
                innerWidget = aux;

            } else if (type == QLatin1String("AuxDigital")) {
                auto *dig = new AuxDigitalWidget(cell);
                innerWidget = dig;

                // Apply persisted mode directly from JSON properties.
                // NOTE: m_layoutModel is not populated yet here, so we must NOT call
                // widgetAuxDigitalMode(), which reads from the in-memory model.
                const int modeInt = props.value(QStringLiteral("auxDigMode"))
                                        .toInt(static_cast<int>(AuxDigitalWidget::Mode::Disabled));
                const AuxDigitalWidget::Mode mode = static_cast<AuxDigitalWidget::Mode>(modeInt);
                dig->setMode(mode);

                // Label is managed by the generic label property / WidgetBase
            } else if (type == QLatin1String("FreqWidget")) {
                auto *fw = new FreqWidget(cell);
                innerWidget = fw;

                // Restore Hamlib usage from persisted properties (default: false).
                const bool useHamlib = props.value(QStringLiteral("useHamlib")).toBool(false);
                fw->setUseHamlib(useHamlib);

                // Restore Hz tail display (default: true).
                const bool showHz = props.value(QStringLiteral("showHz")).toBool(true);
                fw->setShowHertz(showHz);

            } else if (type == QLatin1String("GainWidget")) {
                auto *gw = new GainWidget(cell);
                innerWidget = gw;

                // Read optional per-widget sources from JSON properties
                QString inSrc = props.value(QStringLiteral("rfInSource")).toString();
                QString outSrc = props.value(QStringLiteral("rfOutSource")).toString();

                qDebug() << "[LayoutManager] GainWidget" << widgetId
                         << "sources from JSON before defaults:" << inSrc << "/" << outSrc;

                // Default to this group's coupler ("CouplerX") if not specified
                const QString defaultSrc = QStringLiteral("Coupler%1").arg(couplerIndex);
                if (inSrc.isEmpty())
                    inSrc = defaultSrc;
                if (outSrc.isEmpty())
                    outSrc = defaultSrc;

                qDebug() << "[LayoutManager] GainWidget" << widgetId
                         << "sources from JSON/properties:" << inSrc << "/" << outSrc;

                gw->setRfInSource(inSrc);
                gw->setRfOutSource(outSrc);

                // Optional built-in attenuator to be added to computed gain (dB)
                const double attnDb = props.value(QStringLiteral("attnDb")).toDouble(0.0);
                gw->setAttnDb(attnDb);

            } else if (type == QLatin1String("MiniScope")) {
                auto *ms = new MiniScopeWidget(cell);
                innerWidget = ms;

                // Read MiniScope properties once (and be tolerant of legacy keys)
                const int sig = props.value(QStringLiteral("scopeSignal"))
                                    .toInt((int) MiniScopeWidget::ScopeSignalType::ForwardWatts);

                const double win = props.contains(QStringLiteral("windowSec"))
                                       ? props.value(QStringLiteral("windowSec")).toDouble(10.0)
                                       : props.value(QStringLiteral("timeWindowSec"))
                                             .toDouble(10.0); // legacy support

                // Only default to ms->autoScale() if the key is missing
                const bool autos = props.contains(QStringLiteral("autoScale"))
                                       ? props.value(QStringLiteral("autoScale")).toBool()
                                       : ms->autoScale();

                ms->setScopeSignal(sig);
                ms->setTimeWindowSeconds(win);
                ms->setAutoScale(autos);
            } else if (type == QLatin1String("Spacer")) {
                auto *spacer = new SpacerWidget(cell);

                const int minH = qMax(0, minHeightFromJson);
                spacer->setSpacerMinHeightPx(minH);

                // IMPORTANT: give the container a minimum too (helps QGridLayout sizing)
                cell->setMinimumHeight(minH + (2 * m)); // include margins

                innerWidget = spacer;
                spacer->applyProperties(props);
            } else {
                qWarning() << "[LayoutManager] Unknown widget type in layout JSON:" << type;
            }

            // Precompute friendly metadata used both by WidgetBase and WidgetLayout
            QString friendlyName = !widgetId.isEmpty() ? widgetId : type;

            // Prefer explicit label from JSON if present
            const QString labelFromJson = props.value(QStringLiteral("label")).toString();

            QString friendlyLabel;
            if (type == QLatin1String("PowerBar") || type == QLatin1String("PowerBarVert"))
                friendlyLabel = QObject::tr("Forward Power");
            else if (type == QLatin1String("ForwardRefPanel"))
                friendlyLabel = QObject::tr("Forward / Ref / SWR");
            else if (type == QLatin1String("AnalogPowerMeter"))
                friendlyLabel = QObject::tr("Cross-Needle Power / SWR");
            else if (type == QLatin1String("AuxSensor"))
                friendlyLabel = QObject::tr("Aux Sensor");
            else if (type == QLatin1String("AuxVoltage"))
                friendlyLabel = QObject::tr("Aux Voltage");
            else if (type == QLatin1String("AuxDigital"))
                friendlyLabel = QObject::tr("Aux Digital");
            else if (type == QLatin1String("GainWidget"))
                friendlyLabel = QObject::tr("Gain");
            else if (type == QLatin1String("FreqWidget"))
                friendlyLabel = QObject::tr("Frequency");
            else if (type == QLatin1String("MiniScope"))
                friendlyLabel = QObject::tr("Mini Scope");
            else if (type == QLatin1String("Spacer"))
                friendlyLabel = QObject::tr("Spacer");
            else
                friendlyLabel = type;

            // Prefer persisted label from JSON when present (prevents labels "forgetting" on rebuild)
            const QString effectiveLabel = !labelFromJson.trimmed().isEmpty()
                                               ? labelFromJson.trimmed()
                                               : friendlyLabel;

            // Source: from JSON, or default per coupler index for RF widgets
            QString resolvedSource = sourceFromJson;
            if (resolvedSource.isEmpty()) {
                if (type == QLatin1String("PowerBar") || type == QLatin1String("PowerBarVert")
                    || type == QLatin1String("ForwardRefPanel")
                    || type == QLatin1String("AnalogPowerMeter")) {
                    resolvedSource = QStringLiteral("Coupler%1").arg(couplerIndex);
                } else {
                    resolvedSource.clear();
                }
            }

            // --- populate WidgetBase metadata (name / label / source) ---
            if (innerWidget) {
                if (auto *wb = qobject_cast<WidgetBase *>(innerWidget)) {
                    wb->setWidgetName(friendlyName);
                    wb->setLabel(effectiveLabel);
                    wb->setSource(resolvedSource);
                }
            }
            // -----------------------------------------------------------------

            // --- Mirror into in-memory layout model (WidgetLayout) ---
            {
                WidgetLayout wLayout;
                wLayout.id = widgetId;
                wLayout.type = type;
                wLayout.name = friendlyName;
                wLayout.label = effectiveLabel;
                wLayout.source = resolvedSource;

                wLayout.row = row;
                wLayout.column = col;
                wLayout.rowSpan = rowSpan;
                wLayout.colSpan = colSpan;

                wLayout.margin = m;
                wLayout.visible = visibleFromJson;
                wLayout.rawProperties = props;

                groupLayout.widgets.append(wLayout);
            }
            // ---------------------------------------------------------

            if (innerWidget) {
                cellLayout->addWidget(innerWidget);
                gLayout->addWidget(cell, row, col, rowSpan, colSpan);

                if (!widgetId.isEmpty()) {
                    // Container
                    m_widgetIds.insert(cell, widgetId);

                    // Inner widget + all children (so clicks on ScopePlotWidget, labels, etc. are captured)
                    m_widgetIds.insert(innerWidget, widgetId);

                    const auto kids = innerWidget->findChildren<QWidget *>();
                    for (QWidget *k : kids)
                        m_widgetIds.insert(k, widgetId);
                }

                if (!widgetId.isEmpty()) {
                    WidgetRef ref;
                    ref.groupId = groupId;
                    ref.widgetId = widgetId;
                    ref.type = type;
                    ref.container = cell;
                    ref.inner = innerWidget;
                    m_widgetMap.insert(widgetId, ref);
                }

                if (m_designModeEnabled) {
                    cell->installEventFilter(this);

                    innerWidget->installEventFilter(this);
                    const auto kids = innerWidget->findChildren<QWidget *>();
                    for (QWidget *k : kids)
                        k->installEventFilter(this);
                }

                if (type == QLatin1String("Spacer"))
                    gLayout->setRowStretch(row, 1);

                cell->setVisible(visibleFromJson);

                // debug output for id, type and props
                qDebug() << "[Build] Created widget" << widgetId << "type:" << type << "row:" << row
                         << "col:" << col << "rowSpan:" << rowSpan << "colSpan:" << colSpan
                         << "margin:" << m << "visible:" << visibleFromJson
                         << "source:" << resolvedSource << "minHeight:" << minHeightFromJson;
            } else {
                cell->deleteLater();
            }

            // Connect propertiesRequested from WidgetBase to LayoutManager's widgetPropertiesRequested signal,
            // so MainWindow can open the properties editor when a widget is right-clicked in design mode.
            if (auto *wb = qobject_cast<WidgetBase *>(innerWidget)) {
                connect(wb,
                        &WidgetBase::propertiesRequested,
                        this,
                        [this, groupId, widgetId](WidgetBase *) {
                            emit widgetPropertiesRequested(groupId, widgetId);
                        });
            }
        } // end for widgets

        // --------- Derive group.columns and group.rows from widgets ----------
        int maxRightColumn = 0;
        int maxBottomRow = 0;

        for (const WidgetLayout &w : groupLayout.widgets) {
            const int right = w.column + w.colSpan; // first free column after this widget
            const int bottom = w.row + w.rowSpan;   // first free row after this widget

            if (right > maxRightColumn)
                maxRightColumn = right;
            if (bottom > maxBottomRow)
                maxBottomRow = bottom;
        }

        if (maxRightColumn > 0 && groupLayout.columns < maxRightColumn)
            groupLayout.columns = maxRightColumn;
        if (groupLayout.columns < 1)
            groupLayout.columns = 1;

        if (maxBottomRow > 0 && groupLayout.rows < maxBottomRow)
            groupLayout.rows = maxBottomRow;
        if (groupLayout.rows < 1)
            groupLayout.rows = 1;
        // ---------------------------------------------------------------------
        qDebug() << "[LayoutManager] group" << groupLayout.id << "columns=" << groupLayout.columns
                 << "rows=" << groupLayout.rows;

        // If this group is just a single widget that has rowSpan > 1,
        // treat that as "this whole group should be taller".
        if (groupLayout.widgets.size() == 1 && groupBox) {
            const WidgetLayout &w0 = groupLayout.widgets.constFirst();
            if (w0.rowSpan > 1) {
                const int factor = qMax(1, w0.rowSpan);

                int baseMin = groupBox->minimumSizeHint().height();
                if (baseMin <= 0)
                    baseMin = groupBox->sizeHint().height();
                if (baseMin <= 0)
                    baseMin = 120; // sensible fallback

                // Make the group significantly taller than the 1x1 groups
                const int targetMinHeight = baseMin * factor;

                groupBox->setMinimumHeight(targetMinHeight);
                groupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

                qDebug() << "[LayoutManager] Taller group" << groupLayout.id
                         << "rows=" << groupLayout.rows << "rowSpanHint=" << w0.rowSpan
                         << "minHeight=" << targetMinHeight;
            }
        }

        // Ensure groupLayout.columns is at least as wide as the widgets we saw.
        int maxRightColumn2 = 0;
        for (const WidgetLayout &w : groupLayout.widgets) {
            const int right = w.column + w.colSpan;
            if (right > maxRightColumn2)
                maxRightColumn2 = right;
        }

        if (maxRightColumn2 > 0 && groupLayout.columns < maxRightColumn2)
            groupLayout.columns = maxRightColumn2;
        if (groupLayout.columns < 1)
            groupLayout.columns = 1;

        // Store this group's layout in the in-memory model
        m_layoutModel.append(groupLayout);

    } // end build groups/widgets

    // --- Lay out groups in a grid (m_groupGridColumns columns by default) ---
    // --- Lay out groups in an explicit grid using groupLayout.row/column ---
    if (grid) {
        const int cols = qMax(1, m_groupGridColumns);

        // All columns share extra horizontal space
        for (int c = 0; c < cols; ++c)
            grid->setColumnStretch(c, 1);

        // 1) Fallback-pack any groups that are missing row/column (row < 0).
        //    We do this AFTER spans were derived from widgets (your existing logic),
        //    so the packer uses the final row/col spans.
        for (GroupLayout &gl : m_layoutModel) {
            int groupColSpan = qBound(1, gl.columns, cols);
            int groupRowSpan = qMax(1, gl.rows);

            if (gl.row >= 0 && gl.column >= 0)
                continue; // already explicitly placed by JSON

            // Old behavior (flow pack) but stored as explicit coordinates
            if (packCol + groupColSpan > cols) {
                advancePackRow();
            }

            gl.row = packRow;
            gl.column = packCol;

            packCol += groupColSpan;
            packRowSpanMax = qMax(packRowSpanMax, groupRowSpan);
        }

        // 2) Place group boxes explicitly.
        int maxUsedRow = -1;

        for (const GroupInfo &g : m_groups) {
            if (!g.groupBox)
                continue;

            const GroupLayout *gl = findGroupLayout(g.id);
            if (!gl) {
                // Should not happen, but avoid crashes
                continue;
            }

            int groupColSpan = qBound(1, gl->columns, cols);
            int groupRowSpan = qMax(1, gl->rows);

            int r = qMax(0, gl->row);
            int c = qBound(0, gl->column, cols - 1);

            // If span would exceed right edge, clamp span (keep left edge)
            if (c + groupColSpan > cols)
                groupColSpan = cols - c;

            grid->addWidget(g.groupBox, r, c, groupRowSpan, groupColSpan, Qt::AlignTop);

            const int bottomRow = r + groupRowSpan - 1;
            if (bottomRow > maxUsedRow)
                maxUsedRow = bottomRow;
        }

        // 3) Apply row stretch so content hugs the top; last used row stretches
        if (maxUsedRow >= 0) {
            for (int r = 0; r <= maxUsedRow; ++r)
                grid->setRowStretch(r, 0);
            grid->setRowStretch(maxUsedRow, 1);
        }
    }

    // If we were already in Design Mode, re-apply group styling and filters
    // so new group boxes get the dashed border and hit-testing.
    if (m_designModeEnabled) {
        setDesignModeEnabled(true);
    }

    // After building groups, pick primary PowerBar for coupler 0 (legacy binding)
    PowerBarWidget *primary0 = primaryPowerBarForCoupler(0);
    if (primary0)
        outPowerBar = primary0;

    // Install event filters recursively on all widgets if in design mode
    if (m_designModeEnabled && m_centralWidget) {
        installDesignFiltersRecursive(m_centralWidget, this);
    }
}

void LayoutManager::buildInitialLayout(QWidget *centralWidget,
                                       PowerBarWidget *&outPowerBar,
                                       ForwardRefPanel *&outForwardRefPanel)
{
    /*    // Start from the built-in default JSON layout and reuse the generic builder.
    m_currentLayoutJson = QString::fromUtf8(kDefaultLayoutJson);
    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);
*/
    // Start from the embedded default JSON (resource)
    m_currentLayoutJson = DefaultLayoutProvider::defaultLayoutJson();

    // Hard fallback so the app still boots even if the resource is missing
    if (m_currentLayoutJson.trimmed().isEmpty()) {
        m_currentLayoutJson = QStringLiteral(
            "{\"schemaVersion\":1,\"groupGridColumns\":2,\"groups\":[]}");
    }

    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);
}

void LayoutManager::resetToDefaultLayout(QWidget *centralWidget,
                                         PowerBarWidget *&outPowerBar,
                                         ForwardRefPanel *&outForwardRefPanel)
{
    /*    m_currentLayoutJson = QString::fromUtf8(kDefaultLayoutJson);
    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);
*/
    // Always reload from resource so “Reset” is truly the built-in preset
    m_currentLayoutJson = DefaultLayoutProvider::defaultLayoutJson();

    if (m_currentLayoutJson.trimmed().isEmpty()) {
        m_currentLayoutJson = QStringLiteral(
            "{\"schemaVersion\":1,\"groupGridColumns\":2,\"groups\":[]}");
    }

    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);
}

bool LayoutManager::loadLayoutFromFile(const QString &filePath,
                                       QWidget *centralWidget,
                                       PowerBarWidget *&outPowerBar,
                                       ForwardRefPanel *&outForwardRefPanel)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[LayoutManager] Failed to open layout file" << filePath << ":"
                   << f.errorString();
        return false;
    }

    const QString jsonText = QString::fromUtf8(f.readAll());
    f.close();

    m_currentLayoutJson = jsonText;
    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);
    return true;
}

bool LayoutManager::saveLayoutToFile(const QString &filePath) const
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[LayoutManager] Failed to save layout file" << filePath << ":"
                   << f.errorString();
        return false;
    }

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8); // Qt 6 compatible

    out << m_currentLayoutJson;
    return true;
}

void LayoutManager::createEmptyLayout(int cols,
                                      QWidget *centralWidget,
                                      PowerBarWidget *&outPowerBar,
                                      ForwardRefPanel *&outForwardRefPanel)
{
    // Phase A: create a brand-new empty layout with 1–4 columns.
    const int clampedCols = qBound(1, cols, 4);

    // Clear any selection state so we start clean.
    clearSelectionHighlight();
    m_selectedGroupId.clear();
    m_selectedWidget = nullptr;

    // Build a minimal valid JSON layout.
    m_currentLayoutJson = QStringLiteral(
                              "{\"schemaVersion\":1,\"groupGridColumns\":%1,\"groups\":[]}")
                              .arg(clampedCols);

    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);
}

void LayoutManager::setDesignModeEnabled(bool enabled)
{
    // Remember what was selected so we can restore it after re-styling.
    const QString previouslySelectedGroup = m_selectedGroupId;

    m_designModeEnabled = enabled;

    if (!enabled) {
        clearSelectionHighlight();
        m_selectedGroupId.clear();
    }

    // Re-apply or remove design-mode styling and event filters
    for (GroupInfo &g : m_groups) {
        if (!g.groupBox)
            continue;

        if (enabled) {
            // Base dashed border for design mode
            g.groupBox->setStyleSheet(
                "QGroupBox { border: 2px dashed #888888; border-radius: 4px; }");
            g.groupBox->installEventFilter(this);

            const QList<QWidget *> children = g.groupBox->findChildren<QWidget *>();
            for (QWidget *w : children)
                w->installEventFilter(this);
        } else {
            g.groupBox->setStyleSheet(QString());
            g.groupBox->removeEventFilter(this);

            const QList<QWidget *> children = g.groupBox->findChildren<QWidget *>();
            for (QWidget *w : children)
                w->removeEventFilter(this);
        }
    }

    // Tell SpacerWidget instances whether we are in design mode
    for (auto it = m_widgetMap.begin(); it != m_widgetMap.end(); ++it) {
        const WidgetRef &ref = it.value();
        if (!ref.inner)
            continue;

        if (auto *sp = qobject_cast<SpacerWidget *>(ref.inner)) {
            sp->setProperty("raypmDesignMode", enabled);
            sp->update();
        }
    }

    // If we are in design mode, restore highlight for the previously
    // selected group (if it still exists)
    if (enabled && !previouslySelectedGroup.isEmpty()) {
        setSelectedGroup(previouslySelectedGroup);
    }
}

void LayoutManager::clearSelectionHighlight()
{
    if (m_selectedWidget) {
        // Remove any selection border we set
        m_selectedWidget->setStyleSheet(QString());
        m_selectedWidget.clear();
    }
}

void LayoutManager::applySelectionHighlight(QWidget *w)
{
    // Clear previous
    clearSelectionHighlight();

    if (!w)
        return;

    m_selectedWidget = w;

    // Local style: only this widget gets the border, not all its children
    m_selectedWidget->setStyleSheet("border: 2px solid #00ffff;"
                                    "border-radius: 4px;");
}

void LayoutManager::setSelectedGroup(const QString &groupId)
{
    if (!m_designModeEnabled)
        return;

    m_selectedGroupId = groupId;

    // Base style: all groups are "designable"
    for (GroupInfo &g : m_groups) {
        if (!g.groupBox)
            continue;

        if (!g.id.isEmpty() && g.id == m_selectedGroupId) {
            // Selected group: thicker / brighter border
            g.groupBox->setStyleSheet(
                "QGroupBox { border: 3px solid #ffaa00; border-radius: 4px; }");
        } else {
            // Unselected but designable
            g.groupBox->setStyleSheet(
                "QGroupBox { border: 2px dashed #888888; border-radius: 4px; }");
        }
    }
}

bool LayoutManager::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_designModeEnabled)
        return QObject::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton)
            return QObject::eventFilter(watched, event);

        QWidget *clicked = qobject_cast<QWidget *>(watched);
        if (!clicked)
            return QObject::eventFilter(watched, event);

        // 1) Find which group this click belongs to
        QString groupId;
        QGroupBox *groupBoxForClick = nullptr;
        for (const GroupInfo &g : m_groups) {
            if (!g.groupBox)
                continue;
            if (clicked == g.groupBox || g.groupBox->isAncestorOf(clicked)) {
                groupId = g.id;
                groupBoxForClick = g.groupBox;
                break;
            }
        }

        // If the click is not inside any design-mode group, ignore it.
        // This prevents clicks on status bar / central background / other docks
        // from clearing the current selection.
        if (!groupBoxForClick)
            return QObject::eventFilter(watched, event);

        // 2) Hit-test inside the group to find the deepest child under the mouse
        QWidget *hitChild = nullptr;
        if (groupBoxForClick) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPoint posInGroup = groupBoxForClick->mapFromGlobal(
                mouse->globalPosition().toPoint());
#else
            const QPoint posInGroup = groupBoxForClick->mapFromGlobal(mouse->globalPos());
#endif
            hitChild = groupBoxForClick->childAt(posInGroup);
        }

        // Start the ancestor walk from the child under the cursor if we found one;
        // otherwise fall back to the originally clicked object.
        QWidget *root = hitChild ? hitChild : clicked;

        // 3) Walk up to the nearest ancestor that has a widgetId
        QString widgetId;
        while (root && root != groupBoxForClick) {
            if (m_widgetIds.contains(root)) {
                widgetId = m_widgetIds.value(root);
                break;
            }
            root = root->parentWidget();
        }

        // 4) Decide what to highlight
        QWidget *targetForSelection = nullptr;

        if (widgetId.isEmpty()) {
            // Clicked on group background: clear widget highlight, keep group selection.
            targetForSelection = nullptr;
        } else {
            // Prefer the QFrame "tile" container for this widget
            auto it = m_widgetMap.constFind(widgetId);
            if (it != m_widgetMap.constEnd()) {
                const WidgetRef &ref = it.value();
                if (ref.container)
                    targetForSelection = ref.container;
                else if (ref.inner)
                    targetForSelection = ref.inner;
            }

            // Fallback to whatever ancestor we hit if we couldn't map it
            if (!targetForSelection && root)
                targetForSelection = root;
        }

        applySelectionHighlight(targetForSelection);

        //qDebug() << "[DesignMode] Clicked group=" << groupId << "widget=" << widgetId
        //         << "objName=" << (root ? root->objectName() : QStringLiteral("<group>"));

        if (WidgetBase *wb = widgetObject(widgetId)) {
            qDebug() << "[DesignMode] Clicked group=" << groupId << "widget=" << widgetId
                     << "objName=" << wb->objectName();
        } else {
            qDebug() << "[DesignMode] Clicked group=" << groupId << "widget=" << widgetId
                     << "objName=<none>";
        }

        emit widgetClicked(groupId, widgetId);
        return false; // don't consume the event
    }

    return QObject::eventFilter(watched, event);
}

bool LayoutManager::groupBinding(const QString &groupId,
                                 QString &outTitle,
                                 int &outCouplerIndex) const
{
    outTitle.clear();
    outCouplerIndex = 0;

    if (groupId.isEmpty())
        return false;

    const GroupLayout *g = findGroupLayout(groupId);
    if (!g)
        return false;

    outTitle = g->title;
    outCouplerIndex = g->couplerIndex;
    return true;
}

bool LayoutManager::groupBindingType(const QString &groupId, QString &outBindingType) const
{
    outBindingType.clear();

    if (groupId.isEmpty())
        return false;

    const GroupLayout *g = findGroupLayout(groupId);
    if (!g)
        return false;

    outBindingType = g->bindingType;
    return true;
}

void LayoutManager::setGroupTitle(const QString &groupId, const QString &title)
{
    if (groupId.isEmpty())
        return;

    GroupLayout *gl = findGroupLayout(groupId);
    if (!gl)
        return;

    gl->title = title;

    // Keep JSON in sync
    m_currentLayoutJson = buildJsonFromModel();

    // Update live group box
    for (GroupInfo &g : m_groups) {
        if (g.id == groupId && g.groupBox) {
            g.baseTitle = title;
            g.groupBox->setTitle(title);
            break;
        }
    }
}

void LayoutManager::setGroupCouplerIndex(const QString &groupId, int couplerIndex)
{
    if (groupId.isEmpty())
        return;

    couplerIndex = qMax(0, couplerIndex);

    GroupLayout *gl = findGroupLayout(groupId);
    if (!gl)
        return;

    gl->couplerIndex = couplerIndex;

    const QString newSource = QStringLiteral("Coupler%1").arg(couplerIndex);

    // Update widget sources in the model – **RF widgets only**
    for (WidgetLayout &wl : gl->widgets) {
        const bool isRfWidget = (wl.type == QLatin1String("PowerBar")
                                 || wl.type == QLatin1String("PowerBarVert")
                                 || wl.type == QLatin1String("ForwardRefPanel")
                                 || wl.type == QLatin1String("AnalogpowerMeter"));

        if (!isRfWidget)
            continue;

        wl.source = newSource;
        wl.rawProperties.insert(QStringLiteral("source"), newSource);
    }

    // Update in-memory GroupInfo
    for (GroupInfo &g : m_groups) {
        if (g.id == groupId) {
            g.couplerIndex = couplerIndex;
            break;
        }
    }

    // Update live WidgetBase::source() – **same RF-only filter**
    auto it = m_widgetMap.begin();
    while (it != m_widgetMap.end()) {
        const WidgetRef &ref = it.value();
        if (ref.groupId == groupId && ref.inner) {
            const bool isRfWidget = (ref.type == QLatin1String("PowerBar")
                                     || ref.type == QLatin1String("PowerBarVert")
                                     || ref.type == QLatin1String("ForwardRefPanel")
                                     || ref.type == QLatin1String("AnalogpowerMeter"));

            if (isRfWidget) {
                if (auto *wb = qobject_cast<WidgetBase *>(ref.inner)) {
                    wb->setSource(newSource);
                }
            }
        }
        ++it;
    }

    // Keep JSON in sync
    m_currentLayoutJson = buildJsonFromModel();
}

QString LayoutManager::widgetType(const QString &widgetId) const
{
    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end())
        return QString();
    return it.value().type;
}

bool LayoutManager::widgetMargin(const QString &widgetId, int &outMargin) const
{
    outMargin = 0;

    const GroupLayout *grp = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl)
        return false;

    outMargin = wl->margin;
    return true;
}

void LayoutManager::setWidgetMargin(const QString &widgetId, int margin)
{
    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end() || !it.value().container)
        return;

    margin = qMax(0, margin);

    if (QLayout *lay = it.value().container->layout())
        lay->setContentsMargins(margin, margin, margin, margin);

    GroupLayout *g = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl)
        return;

    wl->margin = margin;
    wl->rawProperties.insert(QStringLiteral("margin"), margin);

    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::setWidgetMinHeight(const QString &widgetId, int minHeight)
{
    minHeight = qMax(0, minHeight);

    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end() || !it.value().inner)
        return false;

    qDebug() << "[LayoutManager] setWidgetMinHeight" << widgetId << "->" << minHeight;

    QWidget *inner = it.value().inner;
    QFrame *container = it.value().container;

    // SpacerWidget needs its own setter so sizeHint/minimumSizeHint update
    if (auto *sp = qobject_cast<SpacerWidget *>(inner)) {
        sp->setSpacerMinHeightPx(minHeight);
    } else {
        // Shouldn't really be called for non-spacer widgets, but safe fallback
        inner->setMinimumHeight(minHeight);
    }

    // Also set the container min height so the grid row is forced to expand
    if (container)
        container->setMinimumHeight(minHeight);

    // Force relayout
    if (container && container->layout())
        container->layout()->invalidate();

    if (QWidget *pw = container ? container->parentWidget() : nullptr) {
        if (pw->layout()) {
            pw->layout()->invalidate();
            pw->layout()->activate();
        }
    }

    // Persist into in-memory model
    GroupLayout *g = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl)
        return false;

    wl->rawProperties.insert(QStringLiteral("minHeight"), minHeight);
    m_currentLayoutJson = buildJsonFromModel();
    return true;
}

bool LayoutManager::widgetMinHeight(const QString &widgetId, int &outMinHeight) const
{
    outMinHeight = 0;

    const GroupLayout *g = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl)
        return false;

    outMinHeight = wl->rawProperties.value(QStringLiteral("minHeight")).toInt(0);
    return true;
}

bool LayoutManager::widgetVisible(const QString &widgetId) const
{
    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end() || !it.value().container)
        return true;
    return it.value().container->isVisible();
}

bool LayoutManager::widgetSource(const QString &widgetId, QString &outSource) const
{
    outSource.clear();

    const GroupLayout *grp = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl)
        return false;

    outSource = wl->source;
    return true;
}

void LayoutManager::setWidgetSource(const QString &widgetId, const QString &source)
{
    // For coupler-bound groups, RF widgets share the group's coupler index.
    // If the user changes source to CouplerN, keep the group's couplerIndex in sync
    // and fan the source out to all RF widgets in that group.
    GroupLayout *g = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (wl && g && g->bindingType == QLatin1String("coupler")) {
        const bool isRfWidget = (wl->type == QLatin1String("PowerBar")
                                 || wl->type == QLatin1String("PowerBarVert")
                                 || wl->type == QLatin1String("ForwardRefPanel")
                                 || wl->type == QLatin1String("AnalogpowerMeter"));

        if (isRfWidget) {
            static const QString prefix = QStringLiteral("Coupler");
            if (source.startsWith(prefix)) {
                bool ok = false;
                const int idx = source.mid(prefix.size()).toInt(&ok);
                if (ok && idx >= 0) {
                    setGroupCouplerIndex(g->id, idx);
                    return;
                }
            }
        }
    }

    // Normal per-widget source update (non-coupler groups, Aux sources, etc.)
    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end() || !it.value().inner)
        return;

    if (auto *wb = qobject_cast<WidgetBase *>(it.value().inner)) {
        wb->setSource(source);
    }

    if (!wl)
        return;

    wl->source = source;
    wl->rawProperties.insert(QStringLiteral("source"), source);

    m_currentLayoutJson = buildJsonFromModel();
}

void LayoutManager::setWidgetVisible(const QString &widgetId, bool visible)
{
    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end() || !it.value().container)
        return;

    it.value().container->setVisible(visible);

    GroupLayout *g = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl)
        return;

    wl->visible = visible;
    wl->rawProperties.insert(QStringLiteral("visible"), visible);

    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::widgetLabel(const QString &widgetId, QString &outLabel) const
{
    const GroupLayout *g = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl) {
        outLabel.clear();
        return false;
    }

    // If we have an explicit label stored, use it.
    if (!wl->label.isEmpty()) {
        outLabel = wl->label;
        return true;
    }

    // Fall back to the same defaults used when building the layout
    if (wl->type == QLatin1String("PowerBar") || wl->type == QLatin1String("PowerBarVert"))
        outLabel = QObject::tr("Forward Power");
    else if (wl->type == QLatin1String("ForwardRefPanel"))
        outLabel = QObject::tr("Forward / Ref / SWR");
    else if (wl->type == QLatin1String("AnalogPowerMeter"))
        outLabel = QObject::tr("Cross-Needle Power / SWR");
    else
        outLabel = wl->type;

    return true;
}

void LayoutManager::setWidgetLabel(const QString &widgetId, const QString &label)
{
    // Update the live widget instance (if present)
    auto it = m_widgetMap.find(widgetId);
    if (it != m_widgetMap.end() && it.value().inner) {
        if (auto *pb = qobject_cast<PowerBarWidget *>(it.value().inner)) {
            pb->setLabelText(label);
        } else if (auto *pv = qobject_cast<PowerBarVertWidget *>(it.value().inner)) {
            pv->setLabelText(label);
        }
    }

    // Update the in-memory model
    GroupLayout *g = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl)
        return;

    wl->label = label;
    wl->rawProperties.insert(QStringLiteral("label"), label);

    // Rebuild JSON so Save Layout / startup layout see the new label
    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::widgetMeterMode(const QString &widgetId,
                                    PowerBarWidget::MeterMode &outMode) const
{
    outMode = PowerBarWidget::MeterMode::Live;

    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end())
        return false;

    QWidget *inner = it->inner;
    if (!inner)
        return false;

    if (auto *pb = qobject_cast<PowerBarWidget *>(inner)) {
        outMode = pb->meterMode();
        return true;
    }
    if (auto *pv = qobject_cast<PowerBarVertWidget *>(inner)) {
        outMode = pv->meterMode();
        return true;
    }
    if (auto *am = qobject_cast<AnalogPowerMeterWidget *>(inner)) {
        outMode = am->meterMode();
        return true;
    }

    return false;
}

void LayoutManager::setWidgetMeterMode(const QString &widgetId, PowerBarWidget::MeterMode mode)
{
    // Live widget update
    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end())
        return;

    QWidget *inner = it->inner;
    if (!inner)
        return;

    if (auto *pb = qobject_cast<PowerBarWidget *>(inner)) {
        pb->setMeterMode(mode);
    } else if (auto *pv = qobject_cast<PowerBarVertWidget *>(inner)) {
        pv->setMeterMode(mode);
    } else if (auto *am = qobject_cast<AnalogPowerMeterWidget *>(inner)) {
        am->setMeterMode(mode);
    } else {
        return; // not a meter widget
    }

    // Persist into in-memory model
    const QString modeStr = meterModeToString(mode);

    GroupLayout *g = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl)
        return;

    wl->rawProperties.insert(QStringLiteral("meterMode"), modeStr);

    // Rebuild JSON from model so Save/Load pick it up
    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::widgetShowTicks(const QString &widgetId, bool &outShowTicks) const
{
    outShowTicks = true; // default

    const GroupLayout *grp = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl)
        return false;

    if (wl->type != QLatin1String("PowerBar") && wl->type != QLatin1String("PowerBarVert")
        && wl->type != QLatin1String("AnalogPowerMeter") && wl) {
        return false;
    }

    if (wl->rawProperties.contains(QStringLiteral("showTicks"))) {
        outShowTicks = wl->rawProperties.value(QStringLiteral("showTicks")).toBool(true);
    } else {
        outShowTicks = true;
    }

    return true;
}

void LayoutManager::setWidgetShowTicks(const QString &widgetId, bool show)
{
    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end())
        return;

    QWidget *inner = it->inner;
    if (!inner)
        return;

    if (auto *pb = qobject_cast<PowerBarWidget *>(inner)) {
        pb->setShowTicks(show);
    } else if (auto *pv = qobject_cast<PowerBarVertWidget *>(inner)) {
        pv->setShowTicks(show);
    } else if (auto *am = qobject_cast<AnalogPowerMeterWidget *>(inner)) {
        am->setShowTicks(show);
    } else {
        return; // not a bar widget
    }

    GroupLayout *g = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl)
        return;

    wl->rawProperties.insert(QStringLiteral("showTicks"), show);
    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::widgetShowTickLabels(const QString &widgetId, bool &outShowLabels) const
{
    outShowLabels = true; // default

    const GroupLayout *grp = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl)
        return false;

    if (wl->type != QLatin1String("PowerBar") && wl->type != QLatin1String("PowerBarVert")
        && wl->type != QLatin1String("AnalogPowerMeter")) {
        return false;
    }

    if (wl->rawProperties.contains(QStringLiteral("showTickLabels"))) {
        outShowLabels = wl->rawProperties.value(QStringLiteral("showTickLabels")).toBool(true);
    } else {
        outShowLabels = true;
    }

    return true;
}

void LayoutManager::setWidgetShowTickLabels(const QString &widgetId, bool show)
{
    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end())
        return;

    QWidget *inner = it->inner;
    if (!inner)
        return;

    if (auto *pb = qobject_cast<PowerBarWidget *>(inner)) {
        pb->setShowTickLabels(show);
    } else if (auto *pv = qobject_cast<PowerBarVertWidget *>(inner)) {
        pv->setShowTickLabels(show);
    } else if (auto *am = qobject_cast<AnalogPowerMeterWidget *>(inner)) {
        am->setShowTickLabels(show);
    } else {
        return;
    }

    GroupLayout *g = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl)
        return;

    wl->rawProperties.insert(QStringLiteral("showTickLabels"), show);
    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::widgetShowThresholdLabels(const QString &widgetId, bool &outShow) const
{
    outShow = true; // default

    const GroupLayout *grp = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl)
        return false;

    if (wl->type != QLatin1String("PowerBar") && wl->type != QLatin1String("PowerBarVert")
        && wl->type != QLatin1String("AnalogPowerMeter")) {
        return false;
    }

    if (wl->rawProperties.contains(QStringLiteral("showThresholdLabels"))) {
        outShow = wl->rawProperties.value(QStringLiteral("showThresholdLabels")).toBool(true);
    } else {
        outShow = true;
    }

    return true;
}

void LayoutManager::setWidgetShowThresholdLabels(const QString &widgetId, bool show)
{
    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end())
        return;

    QWidget *inner = it->inner;
    if (!inner)
        return;

    if (auto *pb = qobject_cast<PowerBarWidget *>(inner)) {
        pb->setShowThresholdLabels(show);
    } else if (auto *pv = qobject_cast<PowerBarVertWidget *>(inner)) {
        pv->setShowThresholdLabels(show);
    } else if (auto *am = qobject_cast<AnalogPowerMeterWidget *>(inner)) {
        am->setShowThresholdLabels(show);
    } else {
        return;
    }

    GroupLayout *g = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl)
        return;

    wl->rawProperties.insert(QStringLiteral("showThresholdLabels"), show);
    m_currentLayoutJson = buildJsonFromModel();
}

QString LayoutManager::addWidgetToGroup(const QString &groupId,
                                        const QString &type,
                                        QWidget *centralWidget,
                                        PowerBarWidget *&outPowerBar,
                                        ForwardRefPanel *&outForwardRefPanel)
{
    if (groupId.isEmpty() || type.isEmpty() || !centralWidget)
        return QString();

    GroupLayout *gl = findGroupLayout(groupId);
    if (!gl)
        return QString();

    // Decide where to place the new widget
    int newRow = 0;
    int newCol = 0;

    if (gl->widgets.isEmpty()) {
        newRow = 0;
        newCol = 0;
    } else {
        // Most widgets are 1x1 in the group grid
        const int wantRowSpan = 1;
        const int wantColSpan = 1;

        // 1) Prefer filling an existing empty cell inside the current group grid
        // Determine current grid bounds based on existing widgets (and current gl->columns if set)
        int maxRow = 0;
        int maxCol = 0;
        for (const WidgetLayout &w : gl->widgets) {
            maxRow = qMax(maxRow, w.row + qMax(1, w.rowSpan) - 1);
            maxCol = qMax(maxCol, w.column + qMax(1, w.colSpan) - 1);
        }

        // If your model already tracks group columns/rows, honor it as the "visible grid"
        const int gridCols = qMax(1, gl->columns); // 2 for “wide groups”
        const int gridRows = qMax(1, gl->rows);    // 2 for “tall groups”
        maxCol = qMax(maxCol, gridCols - 1);
        maxRow = qMax(maxRow, gridRows - 1);

        bool found = false;
        for (int r = 0; r <= maxRow && !found; ++r) {
            for (int c = 0; c <= maxCol && !found; ++c) {
                if (canPlaceAt(gl->widgets, r, c, wantRowSpan, wantColSpan)) {
                    newRow = r;
                    newCol = c;
                    found = true;
                }
            }
        }

        if (!found) {
            // 2) Otherwise, fall back to your existing behavior: stack below the lowest widget
            int maxBottomRow = -1;
            for (const WidgetLayout &w : gl->widgets) {
                const int bottom = w.row + qMax(1, w.rowSpan) - 1;
                if (bottom > maxBottomRow)
                    maxBottomRow = bottom;
            }
            newRow = maxBottomRow + 1;
            newCol = 0;

            // 3) Keep your original special case for single tall/wide widget
            if (gl->widgets.size() == 1) {
                const WidgetLayout &existing = gl->widgets.first();
                const bool isTall = (existing.rowSpan > 1 && existing.colSpan == 1);
                const bool isWide = (existing.colSpan > 1 && existing.rowSpan == 1);

                if (isTall) {
                    newRow = existing.row;
                    newCol = existing.column + existing.colSpan;
                } else if (isWide) {
                    newRow = existing.row + existing.rowSpan;
                    newCol = existing.column;
                }
            }
        }
    }

    // Generate unique widget id
    const QString baseId = QStringLiteral("%1_%2").arg(groupId, type);
    QString newId = baseId;
    int suffix = 1;
    auto idExists = [gl](const QString &candidate) -> bool {
        for (const WidgetLayout &w : gl->widgets) {
            if (w.id == candidate)
                return true;
        }
        return false;
    };
    while (idExists(newId)) {
        newId = baseId + QStringLiteral("_%1").arg(suffix++);
    }

    // Default properties
    QJsonObject props;
    props.insert(QStringLiteral("visible"), true);
    props.insert(QStringLiteral("margin"), 0);

    if (type == QLatin1String("PowerBar") || type == QLatin1String("PowerBarVert")
        || type == QLatin1String("AnalogPowerMeter")) {
        props.insert(QStringLiteral("meterMode"),
                     meterModeToString(PowerBarWidget::MeterMode::Live));
        props.insert(QStringLiteral("showTicks"), true);
        props.insert(QStringLiteral("showTickLabels"), true);
        props.insert(QStringLiteral("showThresholdLabels"), true);
    }

    // Spacer-specific default property
    if (type == QLatin1String("Spacer")) {
        // Sensible default so the properties panel and JSON are never "0" unless user sets it.
        props.insert(QStringLiteral("minHeight"), 30);
    }

    // Friendly metadata
    QString friendlyName = newId;
    QString friendlyLabel;
    if (type == QLatin1String("PowerBar") || type == QLatin1String("PowerBarVert")
        || type == QLatin1String("AnalogPowerMeter"))
        friendlyLabel = QObject::tr("Forward Power");
    else if (type == QLatin1String("ForwardRefPanel"))
        friendlyLabel = QObject::tr("Forward / Ref / SWR");
    else if (type == QLatin1String("AnalogPowerMeter"))
        friendlyLabel = QObject::tr("Cross-Needle Power / SWR");
    else
        friendlyLabel = type;

    // If JSON provides an explicit label, prefer that
    const QString labelFromJson = props.value(QStringLiteral("label")).toString();
    if (!labelFromJson.isEmpty())
        friendlyLabel = labelFromJson;

    QString source;
    if (type == QLatin1String("PowerBar") || type == QLatin1String("PowerBarVert")
        || type == QLatin1String("ForwardRefPanel") || type == QLatin1String("AnalogPowerMeter")) {
        source = QStringLiteral("Coupler%1").arg(gl->couplerIndex);
        props.insert(QStringLiteral("source"), source);
    }

    // Default row/column span hints for newly created widgets
    int defaultRowSpan = 1;
    int defaultColSpan = 1;

    if (type == QLatin1String("PowerBarVert")) {
        defaultRowSpan = 2;
        defaultColSpan = 1;
    } else if (type == QLatin1String("PowerBar")) {
        defaultRowSpan = 1;
        defaultColSpan = 2;
    } else if (type == QLatin1String("AnalogPowerMeter")) {
        defaultRowSpan = 2;
        defaultColSpan = 2;
    }

    WidgetLayout wLayout;
    wLayout.id = newId;
    wLayout.type = type;
    wLayout.name = friendlyName;
    wLayout.label = friendlyLabel;
    wLayout.source = source;
    wLayout.row = newRow;
    wLayout.column = newCol;
    wLayout.rowSpan = defaultRowSpan;
    wLayout.colSpan = defaultColSpan;
    wLayout.margin = 0;
    wLayout.visible = true;
    wLayout.rawProperties = props;

    gl->widgets.append(wLayout);

    // Sync JSON & rebuild UI
    m_currentLayoutJson = buildJsonFromModel();
    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);

    if (m_designModeEnabled) {
        highlightWidget(newId);
    }

    return newId;
}

bool LayoutManager::removeWidget(const QString &widgetId,
                                 QWidget *centralWidget,
                                 PowerBarWidget *&outPowerBar,
                                 ForwardRefPanel *&outForwardRefPanel)
{
    if (widgetId.isEmpty() || !centralWidget)
        return false;

    bool removed = false;
    for (GroupLayout &g : m_layoutModel) {
        for (int i = 0; i < g.widgets.size(); ++i) {
            if (g.widgets[i].id == widgetId) {
                g.widgets.removeAt(i);
                removed = true;
                break;
            }
        }
        if (removed)
            break;
    }

    if (!removed)
        return false;

    m_currentLayoutJson = buildJsonFromModel();
    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);
    return true;
}

bool LayoutManager::moveWidget(const QString &widgetId,
                               MoveDirection dir,
                               QWidget *centralWidget,
                               PowerBarWidget *&outPowerBar,
                               ForwardRefPanel *&outForwardRefPanel)
{
    if (widgetId.isEmpty() || !centralWidget)
        return false;

    GroupLayout *gl = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &gl);
    if (!gl || !wl)
        return false;

    if (gl->widgets.size() <= 1)
        return false;

    int idx = -1;
    for (int i = 0; i < gl->widgets.size(); ++i) {
        if (gl->widgets[i].id == widgetId) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return false;

    int swapIndex = (dir == MoveDirection::Up) ? idx - 1 : idx + 1;
    if (swapIndex < 0 || swapIndex >= gl->widgets.size())
        return false;

    qSwap(gl->widgets[idx], gl->widgets[swapIndex]);

    // Reassign row indices sequentially
    for (int i = 0; i < gl->widgets.size(); ++i) {
        gl->widgets[i].row = i;
    }

    m_currentLayoutJson = buildJsonFromModel();
    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);

    if (m_designModeEnabled) {
        highlightWidget(widgetId);
    }

    return true;
}

// Helpers for collision detection
static bool columnsOverlap(int aCol, int aSpan, int bCol, int bSpan)
{
    const int a0 = aCol;
    const int a1 = aCol + aSpan - 1;
    const int b0 = bCol;
    const int b1 = bCol + bSpan - 1;
    return !(a1 < b0 || b1 < a0);
}

// Helper to detect rectangle overlap in grid coordinates
static bool rectOverlap(
    int aRow, int aCol, int aRowSpan, int aColSpan, int bRow, int bCol, int bRowSpan, int bColSpan)
{
    const int ar0 = aRow;
    const int ar1 = aRow + aRowSpan - 1;
    const int ac0 = aCol;
    const int ac1 = aCol + aColSpan - 1;

    const int br0 = bRow;
    const int br1 = bRow + bRowSpan - 1;
    const int bc0 = bCol;
    const int bc1 = bCol + bColSpan - 1;

    const bool rowHit = !(ar1 < br0 || br1 < ar0);
    const bool colHit = !(ac1 < bc0 || bc1 < ac0);
    return rowHit && colHit;
}

// Add a new group relative to the given base group.
// Returns the new groupId or an empty string on failure.
// NOTE: If baseGroupId is empty (or there are no groups yet), this creates the
// first group at (row=0, col=0) regardless of pos.
QString LayoutManager::addGroupRelativeTo(const QString &baseGroupId,
                                          GroupInsertPos pos,
                                          QWidget *centralWidget,
                                          PowerBarWidget *&outPowerBar,
                                          ForwardRefPanel *&outForwardRefPanel)
{
    if (!centralWidget)
        return QString();

    // No selection + no groups yet => drop first group at (0,0)
    if (baseGroupId.isEmpty()) {
        if (!m_layoutModel.isEmpty())
            return QString(); // keep existing rule: selection required once there are groups

        // Generate unique group id
        QString newGroupId = QStringLiteral("group0");
        int suffix = 1;
        auto groupIdExists = [this](const QString &candidate) -> bool {
            for (const GroupLayout &g : m_layoutModel) {
                if (g.id == candidate)
                    return true;
            }
            return false;
        };
        while (groupIdExists(newGroupId)) {
            newGroupId = QStringLiteral("group%1").arg(suffix++);
        }

        GroupLayout newGroup;
        newGroup.id = newGroupId;
        newGroup.title = QObject::tr("New Group");
        newGroup.bindingType = QStringLiteral("coupler");
        newGroup.couplerIndex = 0;

        newGroup.columns = 1;
        newGroup.rows = 1;

        newGroup.row = 0;
        newGroup.column = 0;

        m_layoutModel.append(newGroup);

        m_currentLayoutJson = buildJsonFromModel();
        buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);

        return newGroupId;
    }

    int baseIndex = -1;
    for (int i = 0; i < m_layoutModel.size(); ++i) {
        if (m_layoutModel[i].id == baseGroupId) {
            baseIndex = i;
            break;
        }
    }
    if (baseIndex < 0)
        return QString();

    const GroupLayout &base = m_layoutModel[baseIndex];

    qDebug() << "[addGroupRelativeTo] base=" << baseGroupId << "pos=" << (int) pos
             << "base.row/col=" << base.row << base.column << "base.rows/cols=" << base.rows
             << base.columns;

    // Generate unique group id
    const QString baseId = QStringLiteral("%1_group").arg(baseGroupId);
    QString newGroupId = baseId;
    int suffix = 1;
    auto groupIdExists = [this](const QString &candidate) -> bool {
        for (const GroupLayout &g : m_layoutModel) {
            if (g.id == candidate)
                return true;
        }
        return false;
    };
    while (groupIdExists(newGroupId)) {
        newGroupId = baseId + QStringLiteral("_%1").arg(suffix++);
    }

    GroupLayout newGroup;
    newGroup.id = newGroupId;
    newGroup.title = QObject::tr("New Group");
    newGroup.bindingType = base.bindingType;
    newGroup.couplerIndex = base.couplerIndex;

    // Default group span (you can change later via UI)
    newGroup.columns = 1; // colSpan
    newGroup.rows = 1;    // rowSpan

    // --- Grid column count ---
    int cols = m_groupGridColumns;
    if (cols < 1)
        cols = 1;

    // Clamp new group span to grid width
    newGroup.columns = qBound(1, newGroup.columns, cols);
    newGroup.rows = qMax(1, newGroup.rows);

    // === shift-down helper ===
    auto shiftDownBand =
        [&](int startRow, int bandCol, int bandColSpan, int deltaRows, const QString &skipId) {
            for (GroupLayout &g : m_layoutModel) {
                if (!skipId.isEmpty() && g.id == skipId)
                    continue;

                if (g.row >= startRow && columnsOverlap(g.column, g.columns, bandCol, bandColSpan)) {
                    g.row += deltaRows;
                }
            }
        };
    // ===================================================

    // Compute target row/col based on the BASE GROUP's explicit position
    int targetRow = base.row;
    int targetCol = base.column;

    switch (pos) {
    case GroupInsertPos::Above:
        // Spreadsheet insert: insert at base.row, pushing base (and below) down
        targetRow = qMax(0, base.row);
        targetCol = base.column;
        break;

    case GroupInsertPos::Below:
        // Insert immediately after the base group's vertical span
        targetRow = base.row + qMax(1, base.rows);
        targetCol = base.column;
        break;

    case GroupInsertPos::Left:
        targetRow = base.row;
        targetCol = qMax(0, base.column - newGroup.columns);
        break;

    case GroupInsertPos::Right:
    default:
        targetRow = base.row;
        targetCol = qMin(cols - 1, base.column + qMax(1, base.columns));
        break;
    }

    qDebug() << "[addGroupRelativeTo] targetRow/Col=" << targetRow << targetCol
             << "newGroup.rows/cols=" << newGroup.rows << newGroup.columns;

    targetCol = qBound(0, targetCol, cols - 1);
    // Ensure the new group fits horizontally
    if (targetCol + newGroup.columns > cols)
        targetCol = qMax(0, cols - newGroup.columns);

    // Spreadsheet-like insertion: shift down only the affected column band
    // Band columns = [targetCol, targetCol + newGroup.columns - 1]
    shiftDownBand(targetRow, targetCol, newGroup.columns, newGroup.rows, QString() /*skipId*/);

    // Safety: if there are still overlaps due to spans, keep shifting until clear
    auto rectOverlap = [&](const GroupLayout &a, const GroupLayout &b) -> bool {
        const int ar0 = a.row;
        const int ar1 = a.row + qMax(1, a.rows) - 1;
        const int ac0 = a.column;
        const int ac1 = a.column + qMax(1, a.columns) - 1;

        const int br0 = b.row;
        const int br1 = b.row + qMax(1, b.rows) - 1;
        const int bc0 = b.column;
        const int bc1 = b.column + qMax(1, b.columns) - 1;

        const bool rowHit = !(ar1 < br0 || br1 < ar0);
        const bool colHit = !(ac1 < bc0 || bc1 < ac0);
        return rowHit && colHit;
    };

    // Place the new group at target and resolve any remaining collisions
    newGroup.row = targetRow;
    newGroup.column = targetCol;

    bool collision = true;
    while (collision) {
        collision = false;

        for (const GroupLayout &g : std::as_const(m_layoutModel)) {
            if (g.id == newGroup.id)
                continue;

            if (rectOverlap(newGroup, g)) {
                // shift down the same band by newGroup.rows and try again
                shiftDownBand(newGroup.row,
                              newGroup.column,
                              newGroup.columns,
                              newGroup.rows,
                              QString());
                newGroup.row += newGroup.rows;
                collision = true;
                break;
            }
        }
    }

    // Insert into model (order no longer matters when layout is explicit grid)
    m_layoutModel.append(newGroup);

    // Rebuild UI from model
    m_currentLayoutJson = buildJsonFromModel();
    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);

    return newGroupId;
}

bool LayoutManager::removeGroup(const QString &groupId,
                                QWidget *centralWidget,
                                PowerBarWidget *&outPowerBar,
                                ForwardRefPanel *&outForwardRefPanel)
{
    if (groupId.isEmpty() || !centralWidget)
        return false;

    int removeIndex = -1;
    for (int i = 0; i < m_layoutModel.size(); ++i) {
        if (m_layoutModel[i].id == groupId) {
            removeIndex = i;
            break;
        }
    }
    if (removeIndex < 0)
        return false;

    m_layoutModel.removeAt(removeIndex);

    m_currentLayoutJson = buildJsonFromModel();
    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);
    return true;
}

void LayoutManager::setGroupGridColumns(int cols)
{
    cols = qBound(1, cols, 4);

    if (m_groupGridColumns == cols)
        return;

    m_groupGridColumns = cols;

    qDebug() << "[LayoutManager] setGroupGridColumns to" << m_groupGridColumns;
}

int LayoutManager::groupGridColumns() const
{
    return m_groupGridColumns;
}

bool LayoutManager::moveGroup(const QString &groupId,
                              GroupMoveDirection dir,
                              QWidget *centralWidget,
                              PowerBarWidget *&outPowerBar,
                              ForwardRefPanel *&outForwardRefPanel)
{
    if (groupId.isEmpty() || !centralWidget)
        return false;

    if (m_layoutModel.size() <= 1)
        return false;

    int idx = -1;
    for (int i = 0; i < m_layoutModel.size(); ++i) {
        if (m_layoutModel[i].id == groupId) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return false;

    int cols = m_groupGridColumns;
    if (cols < 1)
        cols = 1;

    GroupLayout &moving = m_layoutModel[idx];

    // Helpers
    auto rectOverlap = [&](const GroupLayout &a, const GroupLayout &b) -> bool {
        const int ar0 = a.row;
        const int ar1 = a.row + qMax(1, a.rows) - 1;
        const int ac0 = a.column;
        const int ac1 = a.column + qMax(1, a.columns) - 1;

        const int br0 = b.row;
        const int br1 = b.row + qMax(1, b.rows) - 1;
        const int bc0 = b.column;
        const int bc1 = b.column + qMax(1, b.columns) - 1;

        const bool rowHit = !(ar1 < br0 || br1 < ar0);
        const bool colHit = !(ac1 < bc0 || bc1 < ac0);
        return rowHit && colHit;
    };

    auto findGroupAtCell = [&](int row, int col) -> int {
        for (int i = 0; i < m_layoutModel.size(); ++i) {
            const GroupLayout &g = m_layoutModel[i];
            const int r0 = g.row;
            const int r1 = g.row + qMax(1, g.rows) - 1;
            const int c0 = g.column;
            const int c1 = g.column + qMax(1, g.columns) - 1;
            if (row >= r0 && row <= r1 && col >= c0 && col <= c1)
                return i;
        }
        return -1;
    };

    auto shiftDownBand =
        [&](int startRow, int bandCol, int bandColSpan, int deltaRows, const QString &skipId) {
            for (GroupLayout &g : m_layoutModel) {
                if (!skipId.isEmpty() && g.id == skipId)
                    continue;

                if (g.row >= startRow && columnsOverlap(g.column, g.columns, bandCol, bandColSpan)) {
                    g.row += deltaRows;
                }
            }
        };

    // Compute target row/col
    int targetRow = moving.row;
    int targetCol = moving.column;

    const int movingRowSpan = qMax(1, moving.rows);
    const int movingColSpan = qBound(1, moving.columns, cols);

    // Ensure moving span is valid vs grid width
    moving.columns = movingColSpan;
    moving.rows = movingRowSpan;
    if (moving.column + moving.columns > cols)
        moving.column = qMax(0, cols - moving.columns);

    switch (dir) {
    case GroupMoveDirection::Up:
        targetRow = qMax(0, moving.row - 1);
        targetCol = moving.column;
        break;

    case GroupMoveDirection::Down:
        targetRow = moving.row + 1;
        targetCol = moving.column;
        break;

    case GroupMoveDirection::Left:
        targetRow = moving.row;
        targetCol = qMax(0, moving.column - 1);
        break;

    case GroupMoveDirection::Right:
        targetRow = moving.row;
        targetCol = qMin(cols - 1, moving.column + 1);
        break;
    }

    // Ensure target fits horizontally
    if (targetCol + moving.columns > cols)
        targetCol = qMax(0, cols - moving.columns);

    // LEFT/RIGHT: swap if occupied (feels like a nudge)
    if (dir == GroupMoveDirection::Left || dir == GroupMoveDirection::Right) {
        const int occupantIdx = findGroupAtCell(targetRow, targetCol);
        if (occupantIdx >= 0 && occupantIdx != idx) {
            // Swap positions (top-left). Keep spans unchanged.
            GroupLayout &occ = m_layoutModel[occupantIdx];

            const int oldRow = moving.row;
            const int oldCol = moving.column;

            moving.row = occ.row;
            moving.column = occ.column;

            occ.row = oldRow;
            occ.column = oldCol;
        } else {
            // Empty: just move
            moving.row = targetRow;
            moving.column = targetCol;
        }
    } else {
        // UP/DOWN: move-with-insert (shift-down band at target)
        // Remove “self overlap” by shifting others (skip moving id)
        shiftDownBand(targetRow, targetCol, moving.columns, moving.rows, moving.id);

        moving.row = targetRow;
        moving.column = targetCol;

        // Resolve any remaining collisions (spans can cause rare overlaps)
        bool collision = true;
        while (collision) {
            collision = false;
            for (const GroupLayout &g : std::as_const(m_layoutModel)) {
                if (g.id == moving.id)
                    continue;
                if (rectOverlap(moving, g)) {
                    shiftDownBand(moving.row, moving.column, moving.columns, moving.rows, moving.id);
                    moving.row += moving.rows;
                    collision = true;
                    break;
                }
            }
        }
    }

    m_currentLayoutJson = buildJsonFromModel();
    buildLayoutFromJson(m_currentLayoutJson, centralWidget, outPowerBar, outForwardRefPanel);

    if (m_designModeEnabled)
        setSelectedGroup(groupId);

    return true;
}

bool LayoutManager::highlightWidget(const QString &widgetId)
{
    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end() || !it.value().container)
        return false;

    applySelectionHighlight(it.value().container);
    return true;
}

QVector<WidgetBase *> LayoutManager::widgetsForSource(const QString &source) const
{
    QVector<WidgetBase *> result;

    // m_widgetMap is keyed by widgetId; we only care about WidgetBase + source()
    const auto refs = m_widgetMap.values();
    result.reserve(refs.size());

    for (const WidgetRef &ref : refs) {
        if (!ref.inner)
            continue;

        if (auto *wb = qobject_cast<WidgetBase *>(ref.inner)) {
            if (wb->source() == source) {
                result.push_back(wb);
            }
        }
    }

    return result;
}

QVector<WidgetBase *> LayoutManager::widgetsForCoupler(int couplerIndex) const
{
    const QString src = QStringLiteral("Coupler%1").arg(couplerIndex);
    return widgetsForSource(src);
}

QStringList LayoutManager::allWidgetIds() const
{
    return m_widgetMap.keys();
}

bool LayoutManager::rectsOverlap(
    int r1, int c1, int rs1, int cs1, int r2, int c2, int rs2, int cs2) const
{
    const int r1b = r1 + qMax(1, rs1) - 1;
    const int c1b = c1 + qMax(1, cs1) - 1;
    const int r2b = r2 + qMax(1, rs2) - 1;
    const int c2b = c2 + qMax(1, cs2) - 1;

    const bool rowOverlap = !(r1b < r2 || r2b < r1);
    const bool colOverlap = !(c1b < c2 || c2b < c1);
    return rowOverlap && colOverlap;
}

bool LayoutManager::canPlaceAt(
    const QList<WidgetLayout> &widgets, int row, int col, int rowSpan, int colSpan) const
{
    for (const WidgetLayout &w : widgets) {
        if (rectsOverlap(row, col, rowSpan, colSpan, w.row, w.column, w.rowSpan, w.colSpan))
            return false;
    }
    return true;
}

MiniScopeWidget *LayoutManager::miniScopeForId(const QString &widgetId) const
{
    auto it = m_widgetMap.find(widgetId);
    if (it == m_widgetMap.end())
        return nullptr;

    return qobject_cast<MiniScopeWidget *>(it.value().inner);
}

bool LayoutManager::widgetMiniScopeSignal(const QString &widgetId, int &outSignal) const
{
    // default
    outSignal = (int) MiniScopeWidget::ScopeSignalType::ForwardWatts;

    const GroupLayout *grp = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl || wl->type != QLatin1String("MiniScope"))
        return false;

    if (wl->rawProperties.contains(QStringLiteral("scopeSignal")))
        outSignal = wl->rawProperties.value(QStringLiteral("scopeSignal")).toInt(outSignal);

    return true;
}

void LayoutManager::setWidgetMiniScopeSignal(const QString &widgetId, int signal)
{
    // Update the live widget instance
    if (auto *ms = miniScopeForId(widgetId)) {
        ms->setScopeSignal(signal);
    }

    GroupLayout *grp = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl || wl->type != QLatin1String("MiniScope"))
        return;

    wl->rawProperties.insert(QStringLiteral("scopeSignal"), signal);

    // Keep the logical data source in sync with the chosen signal.
    QString newSource;
    const auto sig = static_cast<MiniScopeWidget::ScopeSignalType>(signal);

    switch (sig) {
    // RF-coupler driven modes -> bind to CouplerN if this group is coupler-bound
    case MiniScopeWidget::ScopeSignalType::ForwardWatts:
    case MiniScopeWidget::ScopeSignalType::ReflectedWatts:
    case MiniScopeWidget::ScopeSignalType::Swr:
        if (grp && grp->bindingType == QLatin1String("coupler")) {
            newSource = QStringLiteral("Coupler%1").arg(grp->couplerIndex);
        }
        break;

    // AUX-driven modes -> bind to Aux 1 / Aux 2 / Aux 3
    case MiniScopeWidget::ScopeSignalType::AuxValue:
        newSource = QStringLiteral("Aux0"); // Aux 1
        break;
    case MiniScopeWidget::ScopeSignalType::Aux2Value:
        newSource = QStringLiteral("Aux1"); // Aux 2
        break;
    case MiniScopeWidget::ScopeSignalType::Aux3Value:
        newSource = QStringLiteral("Aux2"); // Aux 3
        break;

    default:
        break;
    }

    if (!newSource.isEmpty()) {
        // This updates WidgetLayout::source and persists back to JSON.
        setWidgetSource(widgetId, newSource);
    }

    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::widgetMiniScopeWindowSec(const QString &widgetId, double &outSec) const
{
    outSec = 10.0;

    const GroupLayout *grp = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl || wl->type != QLatin1String("MiniScope"))
        return false;

    if (wl->rawProperties.contains(QStringLiteral("windowSec")))
        outSec = wl->rawProperties.value(QStringLiteral("windowSec")).toDouble(outSec);

    return true;
}

void LayoutManager::setWidgetMiniScopeWindowSec(const QString &widgetId, double sec)
{
    if (auto *ms = miniScopeForId(widgetId)) {
        ms->setTimeWindowSeconds(sec);
    }

    GroupLayout *g = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl || wl->type != QLatin1String("MiniScope"))
        return;

    wl->rawProperties.insert(QStringLiteral("windowSec"), sec);
    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::widgetMiniScopeAutoScale(const QString &widgetId, bool &outAuto) const
{
    outAuto = true;

    const GroupLayout *grp = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl || wl->type != QLatin1String("MiniScope"))
        return false;

    if (wl->rawProperties.contains(QStringLiteral("autoScale")))
        outAuto = wl->rawProperties.value(QStringLiteral("autoScale")).toBool(outAuto);

    return true;
}

void LayoutManager::setWidgetMiniScopeAutoScale(const QString &widgetId, bool enabled)
{
    if (auto *ms = miniScopeForId(widgetId)) {
        ms->setAutoScale(enabled);
    }

    GroupLayout *g = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &g);
    if (!wl || wl->type != QLatin1String("MiniScope"))
        return;

    wl->rawProperties.insert(QStringLiteral("autoScale"), enabled);
    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::widgetFreqUseHamlib(const QString &widgetId) const
{
    const GroupLayout *grp = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl || wl->type != QLatin1String("FreqWidget"))
        return false;

    if (!wl->rawProperties.contains(QStringLiteral("useHamlib")))
        return false;

    return wl->rawProperties.value(QStringLiteral("useHamlib")).toBool(false);
}

void LayoutManager::setWidgetFreqUseHamlib(const QString &widgetId, bool useHamlib)
{
    GroupLayout *grp = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl || wl->type != QLatin1String("FreqWidget"))
        return;

    wl->rawProperties.insert(QStringLiteral("useHamlib"), useHamlib);

    // Update live widget instance
    if (auto *fw = qobject_cast<FreqWidget *>(widgetObject(widgetId)))
        fw->setUseHamlib(useHamlib);

    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::widgetFreqShowHz(const QString &widgetId) const
{
    const GroupLayout *grp = nullptr;
    const WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl || wl->type != QLatin1String("FreqWidget"))
        return true; // default: show Hz tail

    // For older layouts that don’t have the property yet, default to true.
    if (!wl->rawProperties.contains(QStringLiteral("showHz")))
        return true;

    return wl->rawProperties.value(QStringLiteral("showHz")).toBool(true);
}

void LayoutManager::setWidgetFreqShowHz(const QString &widgetId, bool showHz)
{
    GroupLayout *grp = nullptr;
    WidgetLayout *wl = findWidgetLayout(widgetId, &grp);
    if (!wl || wl->type != QLatin1String("FreqWidget"))
        return;

    wl->rawProperties.insert(QStringLiteral("showHz"), showHz);

    // Update live widget instance
    if (auto *fw = qobject_cast<FreqWidget *>(widgetObject(widgetId)))
        fw->setShowHertz(showHz);

    m_currentLayoutJson = buildJsonFromModel();
}

bool LayoutManager::anyFreqWidgetUsesHamlib() const
{
    for (auto it = m_widgetMap.constBegin(); it != m_widgetMap.constEnd(); ++it) {
        const WidgetRef &ref = it.value();
        if (!ref.inner)
            continue;

        if (auto *fw = qobject_cast<FreqWidget *>(ref.inner)) {
            if (fw->useHamlib())
                return true;
        }
    }
    return false;
}

} // namespace raypm
