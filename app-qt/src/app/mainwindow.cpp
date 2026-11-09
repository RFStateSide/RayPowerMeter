// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "app/dialogsetup.h"
#include "common/Constants.h"
#include "controllers/LayoutManager.h"
#include "core/AppSettings.h"
#include "core/RayDeviceConfig.h"
#include "core/rigctldclient.h"
#include "core/RuntimeDataModel.h"
#include "core/helper.h"
#include "widgets/forwardrefpanel.h"
#include "widgets/auxvoltagewidget.h"
#include "widgets/auxdigitalwidget.h"
#include "widgets/gainwidget.h"
#include "widgets/miniscopewidget.h"
#include "widgets/powerbarvertwidget.h"
#include "widgets/auxsensorwidget.h"
#include "core/spectrumfft.h"
#include "property/IWidgetPropertyAdapter.h"
#include "widgets/freqwidget.h"
#include "widgets/analogpowermeterwidget.h"
#include "app/customsplashscreen.h"
#include "app/dialogabout.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>
#include <QMessageBox>
#include <QProcess>
#include <QThread>
#include <QVariant>
#include <functional>
#include <memory>
#include <QSet>

using namespace raypm;
static MainWindow *g_mainWindowForWidgetDockCheck = nullptr;

namespace raypm
{

    MainWindow::MainWindow(CustomSplashScreen *splash, QWidget *parent)
        : QMainWindow(parent)
        , splash(splash)
        , dialogViewLogFile(new DialogViewLogFile())
    {
        g_mainWindowForWidgetDockCheck = this;

        WidgetBase::setPropertiesDockVisibleFn([]() -> bool {
            if (!g_mainWindowForWidgetDockCheck)
                return false;
            return g_mainWindowForWidgetDockCheck->isWidgetPropertiesDockVisible();
        });

        // Load persisted settings
        m_settings = SettingsManager::load();

        // Create runtime model + layout manager (setupConnections() depends on these)
        m_runtimeModel = new RuntimeDataModel(this);
        m_layoutManager = new LayoutManager(m_runtimeModel, this);

        // Build UI (creates m_protectionDialog)
        setupUi();

        // Setup API connections (now m_protectionDialog and m_runtimeModel exist)
        setupConnections();

        // Tell comm manager about settings and start it (after signals are connected)
        m_comm.setSettings(m_settings);
        m_comm.start();

        // --- Telemetry age timer ---
        connect(&m_telemetryAgeTimer, &QTimer::timeout, this, &MainWindow::updateTelemetryAgeLabel);
        m_telemetryAgeTimer.setInterval(250);
        m_telemetryAgeTimer.start();

        // Design-mode click notifications
        if (m_layoutManager && ui && ui->centralwidget)
        {
            connect(m_layoutManager,
                    &LayoutManager::widgetClicked,
                    this,
                    &MainWindow::onLayoutWidgetClicked);
        }

        // Properties requests (e.g. from right-click context menu)
        if (m_layoutManager)
        {
            connect(m_layoutManager,
                    &LayoutManager::widgetPropertiesRequested,
                    this,
                    &MainWindow::onWidgetPropertiesRequested);
        }

        // Timer to poll rig frequency for FreqWidget(s) that use Hamlib
        m_rigFreqPollTimer = new QTimer(this);
        m_rigFreqPollTimer->setInterval(1000);
        connect(m_rigFreqPollTimer, &QTimer::timeout,
                this, &MainWindow::pollRigFrequencyForFreqWidgets);

        splash->setLoadingMessage("Loading Layout...", 50);
        splash->setProgress(50);
        initializeLayoutFromSettings();
        m_layoutManager->setGroupGridColumns(m_settings.layoutGridColumns);

        m_scaleFactor = m_settings.appScale;
        m_isDarkTheme = (m_settings.theme == raypm::AppTheme::Dark);

        splash->setLoadingMessage("Initilizing GUI...", 75);
        splash->setProgress(50);
        styleGui();

        // Updater
        m_updater = new Updater(&m_comm, &m_settings, this);
    }

    MainWindow::~MainWindow()
    {
        delete ui;
    }

    void MainWindow::resizeEvent(QResizeEvent *event)
    {
        QMainWindow::resizeEvent(event);

        if (m_inScaleResize)
            return;

        if (isMaximized() || isFullScreen())
            return;

        // Keep base size in "scale=1.0 units"
        if (m_scaleFactor > 0.0)
        {
            m_baseWindowSize = QSize(int(width() / m_scaleFactor), int(height() / m_scaleFactor));
        }
    }

    bool MainWindow::isWidgetPropertiesDockVisible() const
    {
        return m_propertiesDock && m_propertiesDock->isVisible();
    }

    void MainWindow::showEvent(QShowEvent *event)
    {
        QMainWindow::showEvent(event);

        if (!m_firstShow)
            return;
        m_firstShow = false;

        // Let Qt compute correct size hints now that the UI is actually realized
        if (centralWidget())
            centralWidget()->layout()->activate();

        // Target = whatever the layout says it wants, but clamp to screen
        QSize target = sizeHint().expandedTo(minimumSizeHint());

        QScreen *scr = screen();
        if (!scr)
            scr = QGuiApplication::primaryScreen();

        if (scr)
        {
            const QRect avail = scr->availableGeometry();

            // Don’t exceed the screen (leave a little breathing room)
            target.setWidth(qMin(target.width(), avail.width() - 40));
            target.setHeight(qMin(target.height(), avail.height() - 80));
        }

        // If your .ui default is *smaller* than needed, bump it up once
        if (width() < target.width() || height() < target.height())
            resize(target);

        // Final splash update + close
        if (splash) {
            splash->setLoadingMessage("Initilizing completed...", 500);
            splash->setProgress(500);
            splash->finish(this);
        }
    }

    void MainWindow::closeEvent(QCloseEvent *event)
    {
        if (event->isAccepted()) {
            if (dialogViewLogFile && dialogViewLogFile->isVisible())
                dialogViewLogFile->close();

            if (m_setupDialog && m_setupDialog->isVisible())
                m_setupDialog->close();

            if (m_hwDialog && m_hwDialog->isVisible())
                m_hwDialog->close();

            if (m_protectionDialog && m_protectionDialog->isVisible())
                m_protectionDialog->close();
        }

        QMainWindow::closeEvent(event);
    }

    void MainWindow::setupUi()
    {
        ui = new Ui::MainWindow();
        ui->setupUi(this);

        // --- Status bar: permanent connection + telemetry labels --- //
        m_connectionStatusLabel = new QLabel(this);
        m_connectionStatusLabel->setText(tr("Idle"));
        // const int baseMinW = 220; // at scale 1.0
        // m_connectionStatusLabel->setMinimumWidth(int(baseMinW * m_scaleFactor));
        m_connectionStatusLabel->setMinimumWidth(0);
        m_connectionStatusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        m_telemetryAgeLabel = new QLabel(this);
        m_telemetryAgeLabel->setText(tr("No data yet"));

        // Protection status
        m_statusProtectionLabel = new QLabel(this);
        m_statusProtectionLabel->setText(tr("Protection: OK"));
        m_statusProtectionLabel->setToolTip(tr("Amplifier protection status"));
        m_statusProtectionLabel->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);

        QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        m_telemetryAgeLabel->setFont(fixedFont);
        m_telemetryAgeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        statusBar()->addPermanentWidget(m_connectionStatusLabel, 1);
        statusBar()->addPermanentWidget(m_statusProtectionLabel, 1);
        statusBar()->addPermanentWidget(m_telemetryAgeLabel, 0);

        statusBar()->showMessage(tr("Starting RayPowerMeter..."));

        // Locate the "Layout" menu so we can add only global layout actions
        QMenu *layoutMenu = nullptr;
        if (menuBar())
        {
            const QList<QAction *> topActs = menuBar()->actions();
            for (QAction *act : topActs)
            {
                if (QMenu *menu = act->menu())
                {
                    if (menu->title().contains(tr("Layout")))
                    {
                        layoutMenu = menu;
                        break;
                    }
                }
            }
        }

        // Remove old actions from menu (now handled by Layout Tools dock)
        if (layoutMenu)
        {
            bool passedDesignMode = false;
            const QList<QAction *> actions = layoutMenu->actions();
            for (QAction *act : actions)
            {
                if (act == m_actDesignMode)
                {
                    passedDesignMode = true;
                    continue;
                }

                if (passedDesignMode)
                {
                    layoutMenu->removeAction(act);
                    act->deleteLater();
                }
            }

            if (!m_actRemoveWidget)
            {
                m_actRemoveWidget = new QAction(tr("Remove Widget"), this);
                connect(m_actRemoveWidget,
                        &QAction::triggered,
                        this,
                        &MainWindow::onRemoveSelectedWidget);
            }

            if (!m_actMoveWidgetUp)
            {
                m_actMoveWidgetUp = new QAction(tr("Move Up"), this);
                connect(m_actMoveWidgetUp,
                        &QAction::triggered,
                        this,
                        &MainWindow::onMoveSelectedWidgetUp);
            }

            if (!m_actMoveWidgetDown)
            {
                m_actMoveWidgetDown = new QAction(tr("Move Down"), this);
                connect(m_actMoveWidgetDown,
                        &QAction::triggered,
                        this,
                        &MainWindow::onMoveSelectedWidgetDown);
            }

            if (!m_actAddGroupAbove)
            {
                m_actAddGroupAbove = new QAction(tr("Add Group Above"), this);
                connect(m_actAddGroupAbove, &QAction::triggered, this, &MainWindow::onAddGroupAbove);
            }

            if (!m_actAddGroupBelow)
            {
                m_actAddGroupBelow = new QAction(tr("Add Group Below"), this);
                connect(m_actAddGroupBelow, &QAction::triggered, this, &MainWindow::onAddGroupBelow);
            }

            if (!m_actRemoveGroup)
            {
                m_actRemoveGroup = new QAction(tr("Remove Group"), this);
                connect(m_actRemoveGroup, &QAction::triggered, this, &MainWindow::onRemoveSelectedGroup);
            }

            if (!m_actMoveGroup)
            {
                m_actMoveGroup = new QAction(tr("Move Group"), this);
                connect(m_actMoveGroup, &QAction::triggered, this, &MainWindow::onMoveSelectedGroup);
            }

            if (!m_actAddGroup)
            {
                m_actAddGroup = new QAction(tr("Add Group"), this);
                connect(m_actAddGroup,
                        &QAction::triggered,
                        this,
                        &::raypm::MainWindow::onAddSelectedGroup);
            }

            updateDesignActionsEnabled();
        }

        // --- Properties Dock --- //
        m_propertiesDock = new QDockWidget(tr("Widget Properties"), this);
        m_propertiesDock->setObjectName(QStringLiteral("WidgetPropertiesDock"));
        m_propertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        auto *panel = new QWidget(m_propertiesDock);
        m_propFormLayout = new QFormLayout(panel);

        // Hint label at the top of the properties panel
        m_propHintLabel = new QLabel(this);
        m_propHintLabel->setText(tr("(Right-click a widget to change selection)"));
        m_propHintLabel->setWordWrap(true);
        m_propHintLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        // Optional: make it visually “hint-like”
        QFont f = m_propHintLabel->font();
        f.setPointSizeF(f.pointSizeF() * 0.95);
        m_propHintLabel->setFont(f);
        m_propHintLabel->setStyleSheet("opacity: 0.85;");

        //
        // --- Group section (above widget section) --- //
        //
        m_groupPropIdLabel = new QLabel(tr("<no group>"), panel);

        m_groupNameEdit = new QLineEdit(panel);
        m_groupNameEdit->setPlaceholderText(tr("Group title"));

        m_groupCouplerSpin = new QSpinBox(panel);
        m_groupCouplerSpin->setRange(0, 3); // adjust upper bound as needed
        m_groupCouplerSpin->setReadOnly(true);
        m_groupCouplerSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);   // optional: removes arrows
        m_groupCouplerSpin->setFocusPolicy(Qt::NoFocus);                    // optional: avoid cursor focus
        m_groupCouplerSpin->setEnabled(false);  // read-only via disabled

        if (!m_designModeEnabled)
            m_propFormLayout->addRow(m_propHintLabel);

        m_propFormLayout->addRow(tr("Group id:"), m_groupPropIdLabel);
        m_propFormLayout->addRow(tr("Group name:"), m_groupNameEdit);
        m_propFormLayout->addRow(tr("Coupler index:"), m_groupCouplerSpin);

        //
        // --- Widget section --- //
        //
        m_propSelectedLabel = new QLabel(tr("<none>"), panel);

        // UX: avoid widening the dock for long widget ids
        m_propSelectedLabel->setTextFormat(Qt::PlainText);
        m_propSelectedLabel->setWordWrap(true);
        m_propSelectedLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_propSelectedLabel->setMinimumWidth(0);

        // NEW: editable widget label
        m_widgetLabelEdit = new QLineEdit(panel);
        m_widgetLabelEdit->setPlaceholderText(tr("Widget label"));

        m_propMarginSpin = new QSpinBox(panel);
        m_propMarginSpin->setRange(0, 50);
        m_propMarginSpin->setSingleStep(1);

        m_propMinHeightSpin = new QSpinBox(this);
        m_propMinHeightSpin->setRange(0, 2000);
        m_propMinHeightSpin->setSingleStep(5);
        m_propMinHeightSpin->setToolTip(tr("Spacer minimum height in pixels (0 = default)"));

        m_propVisibleCheck = new QCheckBox(tr("Visible"), panel);
        m_propVisibleCheck->setChecked(true);

        m_propSourceCombo = new QComboBox(panel);
        m_propSourceCombo->setEnabled(false);

        // PowerBar-specific controls
        m_propMeterModeCombo = new QComboBox(panel);
        m_propMeterModeCombo->addItem(tr("Live"), static_cast<int>(PowerBarWidget::MeterMode::Live));
        m_propMeterModeCombo->addItem(tr("Peak"), static_cast<int>(PowerBarWidget::MeterMode::Peak));
        m_propMeterModeCombo->addItem(tr("Freeze"), static_cast<int>(PowerBarWidget::MeterMode::Freeze));
        m_propMeterModeCombo->addItem(tr("None"), static_cast<int>(PowerBarWidget::MeterMode::None));

        m_propShowTicksCheck = new QCheckBox(tr("Show ticks"), panel);
        m_propShowTicksCheck->setChecked(true);

        m_propShowLabelsCheck = new QCheckBox(tr("Show tick labels"), panel);
        m_propShowLabelsCheck->setChecked(true);

        m_propShowThresholdLabelsCheck =
            new QCheckBox(tr("Show threshold labels"), m_layoutToolsDock);
        m_propShowThresholdLabelsCheck->setChecked(true);

        // Row construction order
        m_propFormLayout->addRow(tr("Selected widget:"), m_propSelectedLabel);
        m_propFormLayout->addRow(tr("Widget label:"), m_widgetLabelEdit);
        m_propFormLayout->addRow(tr("Margin (px):"), m_propMarginSpin);
        // m_propFormLayout->addRow(tr("Min height (px):"), m_propMinHeightSpin);
        m_propFormLayout->addRow(tr("Min height (px):"), m_propMinHeightSpin);
        m_propFormLayout->addRow(QString(), m_propVisibleCheck);
        m_propFormLayout->addRow(tr("Source:"), m_propSourceCombo);

        // GainWidget-specific sources (RF In / RF Out)
        m_propGainInSourceCombo = new QComboBox(panel);
        m_propGainOutSourceCombo = new QComboBox(panel);
        m_propFormLayout->addRow(tr("RF In source:"), m_propGainInSourceCombo);
        m_propFormLayout->addRow(tr("RF Out source:"), m_propGainOutSourceCombo);
        m_propGainAttnDbSpin = new QDoubleSpinBox(panel);
        m_propGainAttnDbSpin->setRange(-120.0, 120.0);
        m_propGainAttnDbSpin->setSingleStep(0.5);
        m_propGainAttnDbSpin->setDecimals(1);
        m_propGainAttnDbSpin->setSuffix(tr(" dB"));
        m_propGainAttnDbSpin->setToolTip(tr("Built-in attenuator compensation added to the computed gain."));
        m_propFormLayout->addRow(tr("Attenuator:"), m_propGainAttnDbSpin);


        m_propFormLayout->addRow(tr("Meter mode:"), m_propMeterModeCombo);
        m_propFormLayout->addRow(tr("Show ticks:"), m_propShowTicksCheck);
        m_propFormLayout->addRow(tr("Show tick labels:"), m_propShowLabelsCheck);
        m_propFormLayout->addRow(tr("Show threshold labels:"), m_propShowThresholdLabelsCheck);

        // --- MiniScope-specific controls ---
        m_propScopeSignalCombo = new QComboBox(panel);
        m_propScopeSignalCombo->addItem(tr("Forward (W)"),
                                        (int)MiniScopeWidget::ScopeSignalType::ForwardWatts);
        m_propScopeSignalCombo->addItem(tr("Reflected (W)"),
                                        (int)MiniScopeWidget::ScopeSignalType::ReflectedWatts);
        m_propScopeSignalCombo->addItem(tr("SWR"), (int)MiniScopeWidget::ScopeSignalType::Swr);
        // AUX channels – one entry per index
        m_propScopeSignalCombo->addItem(tr("Aux 1"),
                                        static_cast<int>(MiniScopeWidget::ScopeSignalType::AuxValue));
        m_propScopeSignalCombo->addItem(tr("Aux 2"),
                                        static_cast<int>(MiniScopeWidget::ScopeSignalType::Aux2Value));
        m_propScopeSignalCombo->addItem(tr("Aux 3"),
                                        static_cast<int>(MiniScopeWidget::ScopeSignalType::Aux3Value));
        m_propFormLayout->addRow(tr("Signal:"), m_propScopeSignalCombo);

        m_propScopeWindowSpin = new QDoubleSpinBox(panel);
        m_propScopeWindowSpin->setRange(0.5, 60.0);
        m_propScopeWindowSpin->setSingleStep(0.5);
        m_propScopeWindowSpin->setSuffix(tr(" s"));
        m_propFormLayout->addRow(tr("Time window:"), m_propScopeWindowSpin);

        m_propScopeAutoScaleCheck = new QCheckBox(tr("Auto-scale"), panel);
        m_propFormLayout->addRow(QString(), m_propScopeAutoScaleCheck);

        // -- AuxSensor-specific properties -- //
        m_propAuxSensorModeCombo = new QComboBox(panel);
        // Use AuxSensorWidget::Mode enum values as item data
        m_propAuxSensorModeCombo->addItem(tr("Unknown"),
                                          static_cast<int>(AuxSensorWidget::Mode::Unknown));
        m_propAuxSensorModeCombo->addItem(tr("Voltage"),
                                          static_cast<int>(AuxSensorWidget::Mode::Voltage));
        m_propAuxSensorModeCombo->addItem(tr("Current"),
                                          static_cast<int>(AuxSensorWidget::Mode::Current));
        m_propAuxSensorModeCombo->addItem(tr("Temperature"),
                                          static_cast<int>(AuxSensorWidget::Mode::Temperature));
        m_propFormLayout->addRow(tr("Aux mode:"), m_propAuxSensorModeCombo);

        m_propAuxSensorUnitsCombo = new QComboBox(panel);
        // Index mapping matches AuxSensorWidget::tempUnitsIndex() (0 = °C, 1 = °F)
        m_propAuxSensorUnitsCombo->addItem(tr("°C"));
        m_propAuxSensorUnitsCombo->addItem(tr("°F"));
        m_propFormLayout->addRow(tr("Temp units:"), m_propAuxSensorUnitsCombo);

        m_propAuxSensorLabelEdit = new QLineEdit(panel);
        m_propAuxSensorLabelEdit->setPlaceholderText(tr("e.g. Amplifier"));
        m_propFormLayout->addRow(tr("Aux label:"), m_propAuxSensorLabelEdit);

        // --- AuxDigital-specific properties -- //
        m_propAuxDigitalModeCombo = new QComboBox(panel);
        m_propAuxDigitalModeCombo->addItem(tr("Disabled"),
                                           static_cast<int>(AuxDigitalWidget::Mode::Disabled));
        m_propAuxDigitalModeCombo->addItem(tr("Input (read-only)"),
                                           static_cast<int>(AuxDigitalWidget::Mode::Input));
        m_propAuxDigitalModeCombo->addItem(tr("Output (controllable)"),
                                           static_cast<int>(AuxDigitalWidget::Mode::Output));
        m_propFormLayout->addRow(tr("Digital mode:"), m_propAuxDigitalModeCombo);

        // --- FreqWidget-specific properties ---
        m_propFreqUseHamlibCheck = new QCheckBox(panel);
        m_propFreqUseHamlibCheck->setChecked(false);
        m_propFormLayout->addRow(tr("Use Hamlib (rig frequency):"),
                                 m_propFreqUseHamlibCheck);

        m_propFreqShowHzCheck = new QCheckBox(panel);
        m_propFreqShowHzCheck->setChecked(true);
        m_propFormLayout->addRow(tr("Show Hz tail:"), m_propFreqShowHzCheck);

        panel->setLayout(m_propFormLayout);
        m_propertiesDock->setWidget(panel);
        addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);

        // Start hidden; show when in Design Mode and a group/widget is active
        m_propertiesDock->hide();

        // PowerBar-specific rows hidden until a PowerBar is selected
        setPowerBarPropertyRowsVisible(false);

        // FreqWidget-specific row hidden until a FreqWidget is selected
        setFreqPropertyRowsVisible(false);

        // Coupler row initially visible; we’ll hide it for non-coupler groups
        setGroupCouplerRowVisible(true);

        // --- Wire up property change signals --- //

        // Group properties
        connect(m_groupNameEdit, &QLineEdit::textEdited, this, &MainWindow::onGroupNameChanged);

        connect(m_groupCouplerSpin,
                qOverload<int>(&QSpinBox::valueChanged),
                this,
                &MainWindow::onGroupCouplerIndexChanged);

        // Widget properties
        connect(m_widgetLabelEdit, &QLineEdit::textEdited, this, &MainWindow::onWidgetLabelEdited);

        connect(m_propMarginSpin,
                qOverload<int>(&QSpinBox::valueChanged),
                this,
                &MainWindow::onPropertyMarginChanged);

        connect(m_propMinHeightSpin,
                qOverload<int>(&QSpinBox::valueChanged),
                this,
                &MainWindow::onPropertyMinHeightChanged);

        connect(m_propVisibleCheck, &QCheckBox::toggled, this, &MainWindow::onPropertyVisibleChanged);

        connect(m_propMeterModeCombo,
                qOverload<int>(&QComboBox::currentIndexChanged),
                this,
                &MainWindow::onPropertyMeterModeChanged);

        connect(m_propShowTicksCheck,
                &QCheckBox::toggled,
                this,
                &MainWindow::onPropertyShowTicksChanged);

        connect(m_propShowLabelsCheck,
                &QCheckBox::toggled,
                this,
                &MainWindow::onPropertyShowLabelsChanged);

        connect(m_propShowThresholdLabelsCheck,
                &QCheckBox::toggled,
                this,
                &MainWindow::onPropertyShowThresholdLabelsChanged);

        connect(m_propSourceCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                &MainWindow::onPropertySourceChanged);

        connect(m_propGainInSourceCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                &MainWindow::onPropertyGainInSourceChanged);

        connect(m_propGainOutSourceCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                &MainWindow::onPropertyGainOutSourceChanged);

        connect(m_propGainAttnDbSpin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                &MainWindow::onPropertyGainAttnDbChanged);

        // MiniScope-specific connections
        connect(m_propScopeSignalCombo,
                &QComboBox::currentIndexChanged,
                this,
                &MainWindow::onPropertyScopeSignalChanged);
        connect(m_propScopeWindowSpin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                &MainWindow::onPropertyScopeWindowChanged);
        connect(m_propScopeAutoScaleCheck,
                &QCheckBox::toggled,
                this,
                &MainWindow::onPropertyScopeAutoScaleChanged);

        // AuxSensor-specific connections
        connect(m_propAuxSensorUnitsCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onPropertyAuxSensorUnitsChanged);

        connect(m_propAuxSensorModeCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onPropertyAuxSensorModeChanged);

        connect(m_propAuxSensorLabelEdit,
                &QLineEdit::textEdited,
                this,
                &MainWindow::onPropertyAuxSensorLabelEdited);

        connect(m_propAuxDigitalModeCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                &MainWindow::onPropertyAuxDigitalModeChanged);

        connect(m_propFreqUseHamlibCheck,
                &QCheckBox::toggled,
                this,
                &MainWindow::onPropertyFreqUseHamlibChanged);

        connect(m_propFreqShowHzCheck,
                &QCheckBox::toggled,
                this,
                &MainWindow::onPropertyFreqShowHzChanged);

        // --- Layout Tools Dock --- //
        m_layoutToolsDock = new QDockWidget(tr("Layout Tools"), this);
        m_layoutToolsDock->setObjectName(QStringLiteral("LayoutToolsDock"));
        m_layoutToolsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        auto *ltPanel = new QWidget(m_layoutToolsDock);
        auto *ltLayout = new QVBoxLayout(ltPanel);

        // Groups section
        auto *groupsBox = new QGroupBox(tr("Groups"), ltPanel);
        auto *groupButtonsLayout = new QVBoxLayout(groupsBox);

        auto *rowAdd = new QHBoxLayout;
        auto *btnAddAbove = new QToolButton(groupsBox);
        btnAddAbove->setText(tr("Add Above"));
        if (m_actAddGroupAbove)
            btnAddAbove->setDefaultAction(m_actAddGroupAbove);
        rowAdd->addWidget(btnAddAbove);

        auto *btnAddBelow = new QToolButton(groupsBox);
        btnAddBelow->setText(tr("Add Below"));
        if (m_actAddGroupBelow)
            btnAddBelow->setDefaultAction(m_actAddGroupBelow);
        rowAdd->addWidget(btnAddBelow);
        groupButtonsLayout->addLayout(rowAdd);

        auto *rowRemove = new QHBoxLayout;
        auto *btnRemoveGroup = new QToolButton(groupsBox);
        btnRemoveGroup->setText(tr("Remove Group"));
        if (m_actRemoveGroup)
            btnRemoveGroup->setDefaultAction(m_actRemoveGroup);
        rowRemove->addWidget(btnRemoveGroup);
        groupButtonsLayout->addLayout(rowRemove);

        auto *rowMove = new QHBoxLayout;
        auto *moveLabel = new QLabel(tr("Move:"), groupsBox);
        rowMove->addWidget(moveLabel);

        m_groupMoveDirectionCombo = new QComboBox(groupsBox);
        m_groupMoveDirectionCombo->addItem(tr("Up"),
                                           static_cast<int>(LayoutManager::GroupMoveDirection::Up));
        m_groupMoveDirectionCombo->addItem(tr("Down"),
                                           static_cast<int>(LayoutManager::GroupMoveDirection::Down));
        m_groupMoveDirectionCombo->addItem(tr("Left"),
                                           static_cast<int>(LayoutManager::GroupMoveDirection::Left));
        m_groupMoveDirectionCombo->addItem(tr("Right"),
                                           static_cast<int>(LayoutManager::GroupMoveDirection::Right));
        rowMove->addWidget(m_groupMoveDirectionCombo, 1);

        auto *btnMoveGroup = new QToolButton(groupsBox);
        btnMoveGroup->setText(tr("Go"));
        connect(btnMoveGroup, &QToolButton::clicked, this, &MainWindow::onMoveSelectedGroup);
        rowMove->addWidget(btnMoveGroup);

        // I added this code but it is not adding the row to the layout like the Move row above (Fix)
        auto *rowAddCombo = new QHBoxLayout;
        auto *addLabel = new QLabel(tr("Add group:"), groupsBox);
        rowAddCombo->addWidget(addLabel);

        m_groupAddDirectionCombo = new QComboBox(groupsBox);

        m_groupAddDirectionCombo->addItem(tr("Above"),
                                          static_cast<int>(LayoutManager::GroupInsertPos::Above));
        m_groupAddDirectionCombo->addItem(tr("Below"),
                                          static_cast<int>(LayoutManager::GroupInsertPos::Below));
        m_groupAddDirectionCombo->addItem(tr("Left"),
                                          static_cast<int>(LayoutManager::GroupInsertPos::Left));
        m_groupAddDirectionCombo->addItem(tr("Right"),
                                          static_cast<int>(LayoutManager::GroupInsertPos::Right));

        rowAddCombo->addWidget(m_groupAddDirectionCombo, 1);

        auto *btnAddGroup = new QToolButton(groupsBox);
        btnAddGroup->setText(tr("Add"));
        connect(btnAddGroup, &QToolButton::clicked, this, &MainWindow::onAddSelectedGroup);
        rowAddCombo->addWidget(btnAddGroup);

        groupButtonsLayout->addLayout(rowMove);
        groupButtonsLayout->addLayout(rowAddCombo);
        groupButtonsLayout->addStretch(1);
        groupsBox->setLayout(groupButtonsLayout);

        // Widgets section
        auto *widgetsBox = new QGroupBox(tr("Widgets"), ltPanel);
        auto *widgetsLayout = new QVBoxLayout(widgetsBox);

        auto *widgetRow = new QHBoxLayout();
        widgetRow->setContentsMargins(0, 0, 0, 0);

        m_widgetTypeCombo = new QComboBox(widgetsBox);
        for (int i = 0; i < WIDGET_TYPE_COUNT; ++i)
            m_widgetTypeCombo->addItem(QString::fromLatin1(WIDGET_TYPE_LIST[i]));
        widgetRow->addWidget(m_widgetTypeCombo, 1);

        auto *btnAddWidget = new QToolButton(widgetsBox);
        btnAddWidget->setText(tr("Add Widget"));
        btnAddWidget->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        connect(btnAddWidget, &QToolButton::clicked, this, &MainWindow::onAddSelectedWidget);
        widgetRow->addWidget(btnAddWidget);

        widgetsLayout->addLayout(widgetRow);

        auto *btnRemoveWidget = new QToolButton(widgetsBox);
        btnRemoveWidget->setText(tr("Remove Widget"));
        if (m_actRemoveWidget)
            btnRemoveWidget->setDefaultAction(m_actRemoveWidget);

        auto *btnMoveUp = new QToolButton(widgetsBox);
        btnMoveUp->setText(tr("Move Up"));
        if (m_actMoveWidgetUp)
            btnMoveUp->setDefaultAction(m_actMoveWidgetUp);

        auto *btnMoveDown = new QToolButton(widgetsBox);
        btnMoveDown->setText(tr("Move Down"));
        if (m_actMoveWidgetDown)
            btnMoveDown->setDefaultAction(m_actMoveWidgetDown);

        widgetsLayout->addWidget(btnRemoveWidget);
        widgetsLayout->addWidget(btnMoveUp);
        widgetsLayout->addWidget(btnMoveDown);
        widgetsLayout->addStretch(1);
        widgetsBox->setLayout(widgetsLayout);

        ltLayout->addWidget(groupsBox);
        ltLayout->addWidget(widgetsBox);
        ltLayout->addStretch(1);

        ltPanel->setLayout(ltLayout);
        m_layoutToolsDock->setWidget(ltPanel);
        addDockWidget(Qt::LeftDockWidgetArea, m_layoutToolsDock);

        // Start hidden; show only in Design Mode
        m_layoutToolsDock->hide();

        // --- Protection warning dialog --- //
        m_protectionDialog = new raypm::DialogProtectionWarning(this);
    }

    void MainWindow::setupConnections()
    {
        // Connection state still comes directly from comm manager
        connect(&m_comm, &RayCommManager::connectedChanged, this, &MainWindow::onConnectedChanged);

        // NEW: route telemetry and coupler config through RuntimeDataModel
        if (m_runtimeModel)
        {
            connect(&m_comm,
                    &RayCommManager::telemetryReceived,
                    m_runtimeModel,
                    &RuntimeDataModel::setTelemetry);

            connect(m_runtimeModel,
                    &RuntimeDataModel::telemetryChanged,
                    this,
                    &MainWindow::onTelemetryReceived);

            connect(&m_comm,
                    &RayCommManager::couplerConfigReceived,
                    m_runtimeModel,
                    &RuntimeDataModel::setCouplerConfig);

            connect(m_runtimeModel,
                    &RuntimeDataModel::couplerConfigChanged,
                    this,
                    &MainWindow::onCouplerConfigReceived);

            connect(&m_comm,
                    &raypm::RayCommManager::telemetryReceived,
                    this,
                    [this](const raypm::RayTelemetry &t)
                    {
                        if (!m_hwDialog)
                            return;

                        // Only update when Cal tab is visible (optional, but nice)
                        // if (m_hwDialog->currentTabIndex() != 3) return;  // if you add a helper

                        const int wantIdx = m_hwDialog->selectedCouplerIndex();
                        const bool fwd = m_hwDialog->isCalForwardPath();

                        // Find the selected coupler sample
                        for (const auto &cs : t.couplers)
                        {
                            if (cs.index == wantIdx)
                            {
                                const double liveW = fwd ? cs.forwardWatts : cs.reflectedWatts;

                                // Raw voltage
                                const double raw = fwd ? cs.forwardVolts : cs.reflectedVolts;

                                m_hwDialog->setCalibrationLive(raw, liveW);
                                break;
                            }
                        }

                        // We have t.auxChannels filled by RayHttpCommWorker from "aux" JSON
                        for (const auto &ax : t.auxChannels)
                        {
                            // ax.index and ax.rawVolts come from the firmware status JSON
                            m_hwDialog->setAuxCalibrationLive(ax.index, ax.rawVolts);
                        }
                    });
        }
        else
        {
            // Fallback (should not happen) – direct wiring as before
            connect(&m_comm, &RayCommManager::telemetryReceived, this, &MainWindow::onTelemetryReceived);

            connect(&m_comm,
                    &RayCommManager::couplerConfigReceived,
                    this,
                    &MainWindow::onCouplerConfigReceived);
        }

        connect(&m_comm, &RayCommManager::errorOccurred, this, &MainWindow::onCommError);

        connect(m_protectionDialog,
                &raypm::DialogProtectionWarning::resetProtectionRequested,
                this,
                &raypm::MainWindow::onProtectionResetRequested);

        connect(&m_comm,
                &RayCommManager::auxConfigReceived,
                this,
                &MainWindow::onAuxConfigReceived);

    }

    void MainWindow::onConnectedChanged(bool connected)
    {
        // Derive a human-readable mode from settings
        QString modeText;
        QString pathText;

        const int modeIndex = m_settings.http.connectionModeIndex;

        // Interpret connectionModeIndex:
        // 0 = "serial/COMM" (HTTP disabled)
        // 1 = "HTTP (LAN)"
        // 2 = "HTTP (Access Point)"
        switch (modeIndex)
        {
        case 1:
            modeText = tr("HTTP (LAN)");
            break;
        case 2:
            modeText = tr("HTTP (Access Point)");
            break;
        default:
            modeText = tr("COMM (Serial)");
            break;
        }

        if (modeIndex == 0)
        {
            pathText = tr("Port %1").arg(m_settings.serial.portName);
        }
        else
        {
            const auto &http = m_settings.http;
            const QString host = http.host.isEmpty() ? QStringLiteral("raypowermeter.local") : http.host;
            const int port = (http.port > 0) ? http.port : 80;
            pathText = tr("%1:%2").arg(host).arg(port);
        }

        const QString connDesc = tr("%1 via %2").arg(modeText, pathText);
        const QString stateText = connected ? tr("Connected") : tr("Disconnected");

        if (m_connectionStatusLabel)
        {
            m_connectionStatusLabel->setText(tr("%1 — %2").arg(connDesc, stateText));
        }

        if (!connected)
        {
            m_hasTelemetry = false;
            updateTelemetryAgeLabel();
            return;
        }

        // Connected
        m_applyLayoutAuxAnalogPending = true;
        m_applyLayoutAuxAnalogReceived.clear();

        // ✅ One-shot: we need at least one aux_config_set after connect
        // so firmware status reflects the configured aux types (not "none").
        m_forceApplyAuxConfigOnce = true;

        // Trigger GET /api/aux/config (OK even if CommManager also requests; we can de-dupe later)
        m_comm.requestAuxConfig();

        // Digital config can apply immediately
        applyAuxDigitalConfigFromLayout();
    }

    void MainWindow::updateTelemetryAgeLabel()
    {
        if (!m_telemetryAgeLabel)
        {
            return;
        }

        if (!m_hasTelemetry || !m_lastTelemetryTime.isValid())
        {
            m_telemetryAgeLabel->setText(tr("No data yet"));
            return;
        }

        const qint64 msRaw = m_lastTelemetryTime.msecsTo(QDateTime::currentDateTime());
        if (msRaw < 0)
        {
            m_telemetryAgeLabel->setText(tr("No data yet"));
            return;
        }

        qint64 ms = msRaw;

        if (ms < 1200)
        {
            // Show in ms with fixed 3-char field, never 0
            if (ms == 0)
                ms = 1;

            QString msStr = QString::number(ms);
            msStr = msStr.rightJustified(3, QLatin1Char(' ')); // "  1", " 12", "111"

            m_telemetryAgeLabel->setText(tr("Last update: %1 ms ago").arg(msStr));
        }
        else
        {
            // Show in seconds with 1 decimal, fixed width field
            double seconds = ms / 1000.0;
            if (seconds > 999.9)
                seconds = 999.9;

            QString secStr = QString::number(seconds, 'f', 1);   // e.g. "0.2", "12.3"
            secStr = secStr.rightJustified(5, QLatin1Char(' ')); // "  0.2", " 12.3"

            m_telemetryAgeLabel->setText(tr("Last update: %1 s ago").arg(secStr));
        }
    }

    void MainWindow::onTelemetryReceived(const raypm::RayTelemetry &telemetry)
    {
        // --- Telemetry timestamp for "last update" age ---
        m_lastTelemetryTime = QDateTime::currentDateTime();
        m_hasTelemetry = true;
        updateTelemetryAgeLabel();

        appendSwrManualSampleIfArmed(telemetry); // Auto mode also uses this method, method name is misleading

        // --- Protection / trip status + dialog handling ---
        {
            const bool protectionActive = telemetry.trip.tripped;
            QPalette appPal = QApplication::palette();

            // ----- Status bar label -----
            if (m_statusProtectionLabel)
            {
                if (protectionActive)
                {
                    const QString summary = tripTypeToSummary(telemetry.trip.type);
                    const QString details = tripDetailLine(telemetry.trip);

                    m_statusProtectionLabel->setText(tr("PROTECTION TRIPPED: %1").arg(summary));

                    QPalette pal = m_statusProtectionLabel->palette();
                    pal.setColor(QPalette::WindowText, appPal.color(QPalette::BrightText));
                    m_statusProtectionLabel->setPalette(pal);

                    QFont f = m_statusProtectionLabel->font();
                    f.setBold(true);
                    m_statusProtectionLabel->setFont(f);

                    m_statusProtectionLabel->setToolTip(
                        tr("Protection has tripped on the hardware.\n\n%1").arg(details));
                }
                else
                {
                    m_statusProtectionLabel->setText(tr("Protection: OK"));

                    QPalette pal = m_statusProtectionLabel->palette();
                    pal.setColor(QPalette::WindowText, appPal.color(QPalette::Highlight));
                    m_statusProtectionLabel->setPalette(pal);

                    QFont f = m_statusProtectionLabel->font();
                    f.setBold(true);
                    m_statusProtectionLabel->setFont(f);

                    m_statusProtectionLabel->setToolTip(
                        tr("Amplifier protection is not currently tripped."));
                }
            }

            // ----- Rising edge: show dialog -----
            if (!m_lastProtectionActive && protectionActive && m_protectionDialog)
            {
                if (m_protectionDialog->shouldShowOnNextTrip())
                {
                    const QString summary = tripTypeToSummary(telemetry.trip.type);
                    const QString details = tripDetailLine(telemetry.trip);

                    m_protectionDialog->setSummaryText(summary);
                    m_protectionDialog->setDetailsText(details);

                    m_protectionDialog->show();
                    m_protectionDialog->raise();
                    m_protectionDialog->activateWindow();
                }
            }

            // ----- Auto-close dialog when alarms are cleared -----
            if (m_protectionDialog && m_protectionDialog->isVisible())
            {
                m_protectionDialog->autoCloseIfSafe(protectionActive);
            }

            // Remember last state for next packet
            m_lastProtectionActive = protectionActive;
        }

        // --- Update widgets based on telemetry ---
        // New, scalable path: multi-coupler + multi-AUX fan-out based on
        // logical source keys like "Coupler0", "Aux0", etc.
        if (m_layoutManager)
        {
            // ---------------------------------------------------------------------
            // 1) AUX channels (voltage / current / temperature) via auxChannels[]
            // ---------------------------------------------------------------------
            if (!telemetry.auxChannels.isEmpty())
            {
                // First: drive AuxSensorWidget and AuxVoltageWidget using source keys
                for (const RayAuxSample &ax : telemetry.auxChannels)
                {
                    if (ax.index < 0)
                        continue;

                    const QString auxSourceKey = QStringLiteral("Aux%1").arg(ax.index);
                    const auto auxWidgets = m_layoutManager->widgetsForSource(auxSourceKey);

                    for (WidgetBase *wb : auxWidgets)
                    {
                        if (!wb)
                            continue;

                        // Generic Aux sensor widget
                        if (auto *sensor = qobject_cast<AuxSensorWidget *>(wb))
                        {
                            AuxSensorWidget::Mode mode = AuxSensorWidget::Mode::Unknown;
                            switch (ax.type)
                            {
                            case RayAuxType::Voltage:
                                mode = AuxSensorWidget::Mode::Voltage;
                                break;
                            case RayAuxType::Current:
                                mode = AuxSensorWidget::Mode::Current;
                                break;
                            case RayAuxType::Temperature:
                                mode = AuxSensorWidget::Mode::Temperature;
                                break;
                            default:
                                mode = AuxSensorWidget::Mode::Unknown;
                                break;
                            }

                            sensor->setMode(mode);
                            sensor->setValue(ax.value);
                        }

                        // Legacy voltage-only widget
                        else if (auto *v = qobject_cast<AuxVoltageWidget *>(wb))
                        {
                            const double volts =
                                (ax.type == RayAuxType::Voltage) ? ax.value : 0.0;
                            v->setVoltage(volts);
                        }
                    }
                }

                // --- Aux Digital telemetry ---
                for (const auto &dig : telemetry.auxDigitalChannels)
                {
                    if (dig.index < 0)
                        continue;

                    // Firmware index is 0..3; layout/source keys are "AuxD1".."AuxD4"
                    const int oneBased = dig.index + 1;
                    const QString sourceKey = QStringLiteral("AuxD%1").arg(oneBased);

                    const auto widgets = m_layoutManager->widgetsForSource(sourceKey);
                    for (WidgetBase *wb : widgets)
                    {
                        if (!wb)
                            continue;

                        if (auto *w = qobject_cast<AuxDigitalWidget *>(wb))
                        {
                            w->setValue(dig.value);
                            // Mode is NOT touched here; it stays whatever the layout says (Input/Output/Disabled).
                        }
                    }
                }

                // Second: drive MiniScope widgets from AUX channels based on signal
                const QStringList allWidgetIds = m_layoutManager->allWidgetIds();

                for (const QString &wid : allWidgetIds)
                {
                    MiniScopeWidget *ms = m_layoutManager->miniScopeForId(wid);
                    if (!ms)
                        continue;

                    using Sig = MiniScopeWidget::ScopeSignalType;
                    const Sig sig = static_cast<Sig>(ms->scopeSignal());

                    // Only handle Aux modes here; RF modes are handled in the coupler block.
                    int auxIndex = -1;
                    switch (sig)
                    {
                    case Sig::AuxValue: // Aux 1
                        auxIndex = 0;
                        break;
                    case Sig::Aux2Value: // Aux 2
                        auxIndex = 1;
                        break;
                    case Sig::Aux3Value: // Aux 3
                        auxIndex = 2;
                        break;
                    default:
                        continue; // Forward/Ref/SWR – skip here
                    }

                    const RayAuxSample *sample = nullptr;
                    for (const RayAuxSample &ax : telemetry.auxChannels)
                    {
                        if (ax.index == auxIndex)
                        {
                            sample = &ax;
                            break;
                        }
                    }
                    if (!sample)
                        continue;

                    const RayAuxSample &ax = *sample;
                    QString title;
                    QString units;

                    switch (ax.type)
                    {
                    case RayAuxType::Voltage:
                        title = tr("Voltage Aux %1").arg(auxIndex + 1);
                        units = QStringLiteral("V");
                        break;
                    case RayAuxType::Current:
                        title = tr("Current Aux %1").arg(auxIndex + 1);
                        units = QStringLiteral("A");
                        break;
                    case RayAuxType::Temperature:
                        title = tr("Temp Aux %1").arg(auxIndex + 1);
                        units = (m_settings.unitsTempIndex == 1)
                                    ? QStringLiteral("°F")
                                    : QStringLiteral("°C");
                        break;
                    default:
                        title = tr("Aux %1").arg(auxIndex + 1);
                        units.clear();
                        break;
                    }

                    ms->setTitle(title);
                    ms->setUnits(units);
                    ms->addSample(ax.value);
                }
            }

            // Fallback: legacy single AUX index (older/fake sources)
            else if (telemetry.auxIndex >= 0)
            {
                const QString auxSourceKey = QStringLiteral("Aux%1").arg(telemetry.auxIndex);
                const auto auxWidgets = m_layoutManager->widgetsForSource(auxSourceKey);

                for (WidgetBase *wb : auxWidgets)
                {
                    if (!wb)
                        continue;

                    if (auto *sensor = qobject_cast<AuxSensorWidget *>(wb))
                    {
                        sensor->setMode(AuxSensorWidget::Mode::Voltage);
                        sensor->setValue(telemetry.voltage);
                    }
                    else if (auto *v = qobject_cast<AuxVoltageWidget *>(wb))
                    {
                        v->setVoltage(telemetry.voltage);
                    }
                }
            }

            // ---------------------------------------------------------------------
            // 2) RF couplers via couplers[]  (AND build forwardByIndex for GainWidget)
            // ---------------------------------------------------------------------
            QHash<int, double> forwardByIndex;

            if (!telemetry.couplers.isEmpty())
            {
                for (const RayCouplerSample &cs : telemetry.couplers)
                {
                    if (cs.index < 0)
                        continue;

                    // NEW: remember forward power for this coupler
                    forwardByIndex.insert(cs.index, cs.forwardWatts);

                    const QString sourceKey = QStringLiteral("Coupler%1").arg(cs.index);
                    const auto widgets = m_layoutManager->widgetsForSource(sourceKey);

                    for (WidgetBase *wb : widgets)
                    {
                        if (!wb)
                            continue;

                        if (auto *panel = qobject_cast<ForwardRefPanel *>(wb))
                        {
                            panel->setValues(cs.forwardWatts, cs.reflectedWatts, cs.swr);
                        }
                        // Horizontal bar
                        else if (auto *bar = qobject_cast<PowerBarWidget *>(wb))
                        {
                            bar->setPowerValue(cs.forwardWatts);
                            bar->setRightLabelText(tr("Ref (W):"));
                            bar->setRightValue(cs.reflectedWatts);
                        }
                        // Vertical bar – same behavior
                        else if (auto *barV = qobject_cast<PowerBarVertWidget *>(wb))
                        {
                            barV->setPowerValue(cs.forwardWatts);
                            barV->setRightLabelText(tr("Ref (W):"));
                            barV->setRightValue(cs.reflectedWatts);
                        }
                        // Analog meter
                        else if (auto *am = qobject_cast<AnalogPowerMeterWidget *>(wb))
                        {
                            am->setPowerValue(cs.forwardWatts);
                            am->setRightValue(cs.reflectedWatts);
                        }
                        else if (auto *freq = qobject_cast<FreqWidget *>(wb))
                        {
                            // When Hamlib is selected, do NOT override it with RF telemetry.
                            if (freq->useHamlib())
                                continue;

                            // Only show "live" when we have forward power AND firmware
                            // says the RF frequency is valid.
                            constexpr double MIN_FWD_W_FOR_LIVE_FREQ = 0.05;

                            const bool hasPower =
                                (cs.forwardWatts >= MIN_FWD_W_FOR_LIVE_FREQ);

                            const double hz = telemetry.rfFrequencyHz;
                            const bool live = telemetry.rfFrequencyValid /* && hasPower*/;

                            // FreqWidget remembers the last valid value internally.
                            // When live == false, it will grey out and show "Last: ...".
                            freq->setFrequency(hz, live, SourceType::RF);
                        }

                        else if (auto *ms = qobject_cast<MiniScopeWidget *>(wb))
                        {
                            using Sig = MiniScopeWidget::ScopeSignalType;
                            const Sig sig = static_cast<Sig>(ms->scopeSignal());

                            // If this MiniScope is in an Aux mode, let the AUX block
                            // drive it; do not overwrite with coupler data.
                            if (sig == Sig::AuxValue ||
                                sig == Sig::Aux2Value ||
                                sig == Sig::Aux3Value)
                            {
                                continue;
                            }

                            auto signalSuffix = [&](Sig s) -> QString
                            {
                                switch (s)
                                {
                                case Sig::ForwardWatts:
                                    return tr("Forward (W)");
                                case Sig::ReflectedWatts:
                                    return tr("Reflected (W)");
                                case Sig::Swr:
                                    return tr("SWR");
                                default:
                                    return QString();
                                }
                            };

                            const QString base = tr("RF Coupler %1").arg(cs.index + 1);
                            const QString suffix = signalSuffix(sig);
                            ms->setTitle(suffix.isEmpty()
                                             ? base
                                             : (base + QLatin1Char(' ') + suffix));

                            double y = 0.0;
                            QString units;

                            switch (sig)
                            {
                            case Sig::ForwardWatts:
                                y = cs.forwardWatts;
                                units = QStringLiteral("W");
                                break;
                            case Sig::ReflectedWatts:
                                y = cs.reflectedWatts;
                                units = QStringLiteral("W");
                                break;
                            case Sig::Swr:
                                y = cs.swr;
                                units.clear(); // or "SWR"
                                break;
                            default:
                                break;
                            }

                            ms->setUnits(units);
                            ms->addSample(y);
                        }

                        // future widgets ...
                    }
                }
            }

            // ---------------------------------------------------------------------
            // 2b) Calibration live feed: push selected coupler/path volts+watts
            // ---------------------------------------------------------------------
            if (m_hwDialog && m_hwDialog->isVisible() && !telemetry.couplers.isEmpty())
            {
                // If you haven’t added these helpers on the dialog yet, see note below.
                const int selIdx = m_hwDialog->calibrationSelectedCouplerIndex();
                const bool fwd = m_hwDialog->calibrationSelectedForwardPath();

                for (const RayCouplerSample &cs : telemetry.couplers)
                {
                    if (cs.index != selIdx)
                        continue;

                    // NOTE: these require RayTelemetry parsing of fwd_v/ref_v into forwardVolts/reflectedVolts
                    const double v = fwd ? cs.forwardVolts : cs.reflectedVolts;
                    const double w = fwd ? cs.forwardWatts : cs.reflectedWatts;

                    m_hwDialog->setCalibrationLive(v, w);
                    break;
                }
            }

            // Fallback: legacy single coupler index (older sources with no couplers[])
            if (telemetry.couplers.isEmpty() && telemetry.couplerIndex >= 0)
            {
                const int idx = telemetry.couplerIndex;
                forwardByIndex.insert(idx, telemetry.forwardWatts); // NEW: feed map

                const QString sourceKey = QStringLiteral("Coupler%1").arg(idx);
                const auto widgets = m_layoutManager->widgetsForSource(sourceKey);

                for (WidgetBase *wb : widgets)
                {
                    if (!wb)
                        continue;

                    if (auto *panel = qobject_cast<ForwardRefPanel *>(wb))
                    {
                        panel->setValues(telemetry.forwardWatts,
                                         telemetry.reflectedWatts,
                                         telemetry.swrLegacy);
                    }
                    else if (auto *bar = qobject_cast<PowerBarWidget *>(wb))
                    {
                        bar->setPowerValue(telemetry.forwardWatts);
                        bar->setRightLabelText(tr("Ref (W):"));
                        bar->setRightValue(telemetry.reflectedWatts);
                    }
                }
            }

            // ---------------------------------------------------------------------
            // 3) GainWidgets: gain = 10*log10(Pout / Pin) using FORWARD watts only
            // ---------------------------------------------------------------------
            const auto gainWidgets = m_layoutManager->gainWidgets();
            if (!gainWidgets.isEmpty())
            {
                auto extractCouplerIndex = [](const QString &src) -> int
                {
                    static const QString prefix = QStringLiteral("Coupler");
                    if (!src.startsWith(prefix))
                        return -1;
                    bool ok = false;
                    const int idx = src.mid(prefix.size()).toInt(&ok);
                    return ok ? idx : -1;
                };

                for (GainWidget *gw : gainWidgets)
                {
                    if (!gw)
                        continue;

                    const int inIdx = extractCouplerIndex(gw->rfInSource());
                    const int outIdx = extractCouplerIndex(gw->rfOutSource());
                    if (inIdx < 0 || outIdx < 0)
                        continue;

                    if (!forwardByIndex.contains(inIdx) || !forwardByIndex.contains(outIdx))
                        continue;

                    const double inW = forwardByIndex.value(inIdx);
                    const double outW = forwardByIndex.value(outIdx);

                    gw->setGainFromWatts(inW, outW);
                }
            }

            // We’re done – don’t fall through to legacy single-widget path
            return;
        }

        // -------------------------------------------------------------------------
        // Legacy fallback (no LayoutManager): single power bar + single fwd/ref panel
        // -------------------------------------------------------------------------
        double fwd = telemetry.forwardWatts;
        double ref = telemetry.reflectedWatts;
        double swr = telemetry.swrLegacy;

        // If vectors exist, prefer coupler0 data for legacy widgets
        if (!telemetry.couplers.isEmpty())
        {
            const auto &c0 = telemetry.couplers.first();
            fwd = c0.forwardWatts;
            ref = c0.reflectedWatts;
            swr = c0.swr;
        }

        if (m_forwardRefPanel)
        {
            m_forwardRefPanel->setValues(fwd, ref, swr);
        }

        if (m_powerBar)
        {
            m_powerBar->setPowerValue(fwd);
            m_powerBar->setRightLabelText(tr("Ref (W):"));
            m_powerBar->setRightValue(ref);
        }
    }

    void MainWindow::onCommError(const QString &message)
    {
        statusBar()->showMessage(tr("Error: %1").arg(message));
    }

    void MainWindow::openSetupDialog()
    {
        if (!m_setupDialog)
        {
            m_setupDialog = new DialogSetup(this);

            // Connect signals
            connect(m_setupDialog, &DialogSetup::ReloadSettings, this, [this]()
                    {
            // 1) Reload full settings from disk
            m_settings = raypm::SettingsManager::load();

            // 2) Update cached UI prefs
            m_scaleFactor = m_settings.appScale;
            m_isDarkTheme = (m_settings.theme == raypm::AppTheme::Dark);

            // 3) Reconfigure the comm manager with the new transport/mode
            m_comm.setSettings(m_settings);
            m_comm.start(); // will pick Serial vs HTTP based on m_settings

            // 4) Restyle GUI (what you already had)
            styleGui();

            // 5) Immediately refresh the status bar text to reflect the *new* mode
            //    We pass 'false' so it shows "Disconnected (...)" until a connection
            //    is established and RayCommManager emits connectedChanged(true).
            onConnectedChanged(false); });

            // Push updated settings into any live panels that cached a copy
            if (m_ampAnalyzerWidget)
                m_ampAnalyzerWidget->setSettings(m_settings);

            connect(m_setupDialog,
                    &DialogSetup::ShowStatusBarMessage,
                    this,
                    [this](const QString &msg, int timeoutMs)
                    {
                        // Also reflect the test result in the connection status label
                        if (m_connectionStatusLabel)
                        {
                            m_connectionStatusLabel->setText(msg);
                        }

                        if (statusBar())
                        {
                            statusBar()->showMessage(msg, timeoutMs);
                        }
                    });

            connect(m_setupDialog,
                    &DialogSetup::scaleFactorChanged,
                    this,
                    &MainWindow::onScaleFactorChanged);
        }

        m_setupDialog->show();
        m_setupDialog->raise();
        m_setupDialog->activateWindow();
    }

    void MainWindow::on_actionOpen_Setup_triggered()
    {
        openSetupDialog();
    }

    void MainWindow::openHardwareConfig()
    {
        if (!m_hwDialog)
        {
            m_hwDialog = new DialogHardwareConfig(this);

            connect(m_hwDialog,
                    &DialogHardwareConfig::reloadRequested,
                    this,
                    &MainWindow::onHardwareReloadRequested);

            connect(m_hwDialog,
                    &DialogHardwareConfig::applyCouplerRequested,
                    this,
                    &MainWindow::onHardwareCouplerApplyRequested);

            connect(m_hwDialog,
                    &DialogHardwareConfig::applyAuxRequested,
                    this,
                    &MainWindow::onHardwareAuxApplyRequested);

            connect(m_hwDialog,
                    &DialogHardwareConfig::applyDeviceRequested,
                    this,
                    &MainWindow::onHardwareDeviceApplyRequested);

            connect(m_hwDialog,
                    &DialogHardwareConfig::applyCalibrationRequested,
                    this,
                    &MainWindow::onHardwareCalibrationApplyRequested);

            connect(m_hwDialog,
                    &DialogHardwareConfig::applyCalibrationTableRequested,
                    this,
                    &MainWindow::onHardwareCalibrationTableApplyRequested);

            connect(&m_comm,
                    &raypm::RayCommManager::couplerCalibrationApplied,
                    this,
                    [this](int idx, bool ok, const QString &msg)
                    {
                        statusBar()->showMessage(ok ? tr("Coupler %1: %2").arg(idx + 1).arg(msg)
                                                    : tr("Coupler %1: Calibration failed: %2")
                                                          .arg(idx + 1)
                                                          .arg(msg),
                                                 3000);
                    });

            connect(&m_comm,
                    &raypm::RayCommManager::auxConfigApplied,
                    this,
                    [this](int idx, bool ok, const QString &msg)
                    {
                        statusBar()->showMessage(
                            ok ? tr("Aux %1: %2").arg(idx + 1).arg(msg)
                               : tr("Aux %1: Apply failed: %2").arg(idx + 1).arg(msg),
                            3000);
                    });

            connect(&m_comm,
                    &raypm::RayCommManager::auxCalibrationApplied,
                    this,
                    [this](int idx, bool ok, const QString &msg)
                    {
                        statusBar()->showMessage(
                            ok ? tr("Aux %1: Calibration updated.").arg(idx + 1)
                               : tr("Aux %1: Calibration failed: %2").arg(idx + 1).arg(msg),
                            3000);
                    });

            connect(&m_comm,
                    &raypm::RayCommManager::deviceConfigReceived,
                    m_hwDialog,
                    &DialogHardwareConfig::setDeviceConfig);

            connect(m_hwDialog,
                    &DialogHardwareConfig::ShowStatusBarMessage,
                    this,
                    [this](const QString &msg, int ms)
                    { statusBar()->showMessage(msg, ms); });

            connect(m_hwDialog,
                    &DialogHardwareConfig::copyCalibrationRequested,
                    this,
                    &MainWindow::onHardwareCalibrationCopyRequested);

            connect(m_hwDialog,
                    &DialogHardwareConfig::resetCalibrationRequested,
                    this,
                    [this](int couplerIndex, const QString &path, const QString &calSource)
                    {
                        statusBar()->showMessage(
                            tr("Sending calibration reset for Coupler %1 (%2, %3)...")
                                .arg(couplerIndex + 1)
                                .arg(path.isEmpty() ? tr("both") : path)
                                .arg(calSource.isEmpty() ? tr("FactoryTable") : calSource),
                            2500);

                        m_comm.resetCouplerCalibration(couplerIndex, path, calSource);
                    });
        }

        if (m_hasCouplerConfig)
        {
            // Push all known coupler configs into the dialog
            for (auto it = m_couplerConfigs.cbegin(); it != m_couplerConfigs.cend(); ++it)
            {
                m_hwDialog->setCouplerConfig(it.value());
            }

            // Let the dialog choose the first enabled coupler as the initial selection
            m_hwDialog->selectInitialCoupler();
        }

        if (m_hasAuxConfig)
        {
            for (auto it = m_auxConfigs.cbegin(); it != m_auxConfigs.cend(); ++it)
            {
                m_hwDialog->setAuxConfig(it.value());
            }
        }

        m_hwDialog->show();
        m_hwDialog->raise();
        m_hwDialog->activateWindow();

        // Ask hardware for the latest device config (brightness + unix_time)
        m_comm.requestDeviceConfig();
        m_comm.requestAuxConfig();
    }

    void MainWindow::on_actionOpen_HW_Config_triggered()
    {
        openHardwareConfig();
    }

    void MainWindow::styleGui()
    {
        QApplication::setStyle("Fusion");

        Helper::ApplyAppTheme(m_isDarkTheme);
        qApp->setStyleSheet(Helper::GlobalStyleSheet(m_isDarkTheme));

        // Apply user scale factor to central widget
        if (QWidget *cw = centralWidget())
        {
            Helper::ApplyUserScaleFactor(cw, m_scaleFactor);
        }

        // Apply scaling to dock widgets + their contents
        const auto docks = findChildren<QDockWidget *>();
        for (QDockWidget *dock : docks)
        {
            Helper::ApplyUserScaleFactor(dock, m_scaleFactor); // title bar + dock chrome font
            if (QWidget *w = dock->widget())
            {
                Helper::ApplyUserScaleFactor(w, m_scaleFactor); // the actual dock contents
            }
        }

        // After Helper scaling to central + docks...
        const auto widgets = findChildren<WidgetBase *>();
        for (WidgetBase *w : widgets)
        {
            w->setAppScaleFactor(m_scaleFactor);
        }

        // Existing: custom widgets that self-scale
        if (m_powerBar)
        {
            // m_powerBar->setScaleFactor(m_scaleFactor);
        }

        if (m_forwardRefPanel)
        {
            m_forwardRefPanel->setScaleFactor(m_scaleFactor);
        }

        if (m_protectionDialog)
        {
            m_protectionDialog->setIsDarkTheme(m_isDarkTheme);
        }

        // DEBUG ONLY, TODO: remove
        auto dumpClamp = [](QWidget *w, const char *tag)
        {
            if (!w)
                return;
            qDebug() << tag << w->metaObject()->className() << w->objectName()
                     << "minW=" << w->minimumWidth() << "minSize=" << w->minimumSize()
                     << "sizeHint=" << w->sizeHint() << "policy=" << w->sizePolicy();
        };

        dumpClamp(this, "MainWindow");
        dumpClamp(centralWidget(), "Central");

        for (QDockWidget *dock : findChildren<QDockWidget *>())
        {
            dumpClamp(dock, "Dock");
            dumpClamp(dock->widget(), "DockContent");
        }

        // Status bar widgets (common clamp source)
        if (statusBar())
        {
            const auto kids = statusBar()->findChildren<QWidget *>();
            for (QWidget *k : kids)
                dumpClamp(k, "StatusChild");
        }

        adjustSize();
    }

    void MainWindow::onScaleFactorChanged(double scale)
    {
        const double oldScale = m_scaleFactor;

        m_scaleFactor = scale;
        m_settings.appScale = scale;
        raypm::SettingsManager::save(m_settings);

        styleGui();

        if (isMaximized() || isFullScreen())
            return;

        // Initialize base size if we don't have one yet
        if (!m_baseWindowSize.isValid() && oldScale > 0.0)
        {
            m_baseWindowSize = QSize(int(width() / oldScale), int(height() / oldScale));
        }

        m_inScaleResize = true;

        QSize target(int(m_baseWindowSize.width() * m_scaleFactor),
                     int(m_baseWindowSize.height() * m_scaleFactor));

        // Optional: clamp to available screen area (recommended)
        if (QScreen *scr = screen())
        {
            const QRect avail = scr->availableGeometry();
            target.setWidth(qMin(target.width(), avail.width()));
            target.setHeight(qMin(target.height(), avail.height()));
        }

        resize(target);

        m_inScaleResize = false;
    }

    void MainWindow::onCouplerConfigReceived(const RayCouplerConfig &cfg)
    {
        // Cache last config
        m_lastCouplerConfig = cfg;
        m_hasCouplerConfig = true;

        // Remember all configs by index so we can reapply after layout changes
        m_couplerConfigs[cfg.index] = cfg;

        // Helper: apply scale + ticks + thresholds to *any* power bar widget
        auto applyScaleToBar = [&](auto *bar)
        {
            if (!bar)
                return;

            const double fullScaleW = cfg.fullScaleW;
            const double fwdRange = cfg.fwdBarRangeW;

            // Same policy as before: prefer fwdBarRangeW, then fullScaleW, then 1800 W fallback
            double maxRange = (fwdRange > 0.0) ? fwdRange : (fullScaleW > 0.0 ? fullScaleW : 1800.0);

            bar->setMaxPower(static_cast<int>(std::ceil(maxRange)));

            double major;
            if (maxRange <= 100.0)
                major = 10.0;
            else if (maxRange <= 500.0)
                major = 50.0;
            else if (maxRange <= 1000.0)
                major = 100.0;
            else
                major = 200.0;

            const double minor = major / 2.0;
            bar->setTickConfig(major, minor, true);

            const double yellowW = maxRange * 0.70;
            const double redW = maxRange * 0.90;

            bar->setThresholds(static_cast<int>(std::round(yellowW)),
                               static_cast<int>(std::round(redW)));
        };

        // 1) Apply to all widgets bound to this coupler (horizontal AND vertical bars)
        if (m_layoutManager)
        {
            const QString sourceKey = QStringLiteral("Coupler%1").arg(cfg.index);
            const auto widgets = m_layoutManager->widgetsForSource(sourceKey);

            for (WidgetBase *wb : widgets)
            {
                if (!wb)
                    continue;

                if (auto *barH = qobject_cast<PowerBarWidget *>(wb))
                {
                    applyScaleToBar(barH);
                }
                else if (auto *barV = qobject_cast<PowerBarVertWidget *>(wb))
                {
                    applyScaleToBar(barV);
                }
            }
        }

        // 2) Legacy fallback: main summary bar for coupler 0 if it isn't in the layout mapping
        if (cfg.index == 0 && m_powerBar)
        {
            applyScaleToBar(m_powerBar);
        }

        // 3) Update group titles via LayoutManager (all indices)
        if (m_layoutManager)
        {
            m_layoutManager->applyCouplerConfig(cfg);
        }

        // 4) Push into HW config dialog if open
        if (m_hwDialog)
        {
            m_hwDialog->setCouplerConfig(cfg);
        }
    }

    void MainWindow::onAuxConfigReceived(const raypm::RayAuxConfig &cfg)
    {
        m_lastAuxConfig = cfg;
        m_hasAuxConfig = true;
        m_auxConfigs[cfg.index] = cfg;

        // Feed into HW dialog if open
        if (m_hwDialog)
        {
            m_hwDialog->setAuxConfig(cfg);
        }

        // --- NEW: push Aux name into any widgets bound to this Aux channel ---
        if (m_layoutManager)
        {
            // Source key used throughout the app for Aux widgets, e.g. "Aux0", "Aux1"
            const QString auxSourceKey = QStringLiteral("Aux%1").arg(cfg.index);

            const auto widgets = m_layoutManager->widgetsForSource(auxSourceKey);

            // Decide what label text to use
            QString label = cfg.name.trimmed();

            if (label.isEmpty())
            {
                const QString typeKey = cfg.typeName.toLower();
                if (typeKey == QLatin1String("voltage"))
                {
                    label = tr("Aux %1 Voltage").arg(cfg.index + 1);
                }
                else if (typeKey == QLatin1String("current"))
                {
                    label = tr("Aux %1 Current").arg(cfg.index + 1);
                }
                else if (typeKey == QLatin1String("temperature"))
                {
                    label = tr("Aux %1 Temp").arg(cfg.index + 1);
                }
                else
                {
                    label = tr("Aux %1").arg(cfg.index + 1);
                }
            }

            for (WidgetBase *wb : widgets)
            {
                if (!wb)
                    continue;

                // This drives AuxVoltageWidget::onLabelChanged(...) via the
                // WidgetBase::labelChanged signal, which in turn calls updateTitleFromLabel().
                wb->setLabel(label);
            }
        }
        // ---------------------------------------------------------------------

        // Apply updated engineering range to any MiniScopes bound to this Aux channel.
        // We only adjust fixed ranges for scopes where auto-scale is currently OFF.
        applyAuxRangeToMiniScopes(cfg.index);

        // Apply AuxAnalog config from layout if pending after receiving all 3 channels
        if (m_applyLayoutAuxAnalogPending)
        {
            m_applyLayoutAuxAnalogReceived.insert(cfg.index);
            tryApplyAuxAnalogConfigFromLayout();
        }
    }

    void MainWindow::onHardwareAuxApplyRequested(const raypm::RayAuxConfig &cfg)
    {
        // Cache locally
        m_lastAuxConfig = cfg;
        m_hasAuxConfig = true;
        m_auxConfigs[cfg.index] = cfg;

        statusBar()->showMessage(
            tr("Aux %1 configuration updated (not yet pushed to hardware).")
                .arg(cfg.index + 1),
            3000);

        // Push to hardware through comm layer
        m_comm.applyAuxConfig(cfg);
    }

    void MainWindow::onHardwareReloadRequested()
    {
        statusBar()->showMessage(tr("Reloading hardware configuration..."), 2000);

        // Simple approach for now: force the comm manager to request it again.
        // We'll add this helper on RayCommManager in a later step.
        m_comm.requestCouplerConfig();
        m_comm.requestDeviceConfig();
        m_comm.requestAuxConfig();
    }

    void MainWindow::onHardwareCouplerApplyRequested(const raypm::RayCouplerConfig &cfg)
    {
        // Cache locally
        m_lastCouplerConfig = cfg;
        m_hasCouplerConfig = true;

        statusBar()->showMessage(tr("Coupler configuration updated (not yet pushed to hardware)."),
                                 3000);

        // Push to hardware through comm layer
        m_comm.applyCouplerConfig(cfg);
    }

    void MainWindow::onHardwareDeviceApplyRequested(int brightness, qint64 utcEpochSeconds)
    {
        statusBar()->showMessage(tr("Device configuration updated (not yet pushed to hardware)."), 3000);

        // Push to hardware through comm layer
        m_comm.applyDeviceConfig(brightness, utcEpochSeconds);
    }

    void MainWindow::onHardwareCalibrationApplyRequested(
        int couplerIndex, bool forwardPath, double vLow, double wLowTrue, double vHigh, double wHighTrue)
    {
        m_comm.applyCouplerCalibration(couplerIndex, forwardPath, vLow, wLowTrue, vHigh, wHighTrue);
    }

    void MainWindow::onHardwareCalibrationTableApplyRequested(int index,
                                                              bool isForward,
                                                              const QList<raypm::RayCalPoint> &points)
    {
        // Convert to QVector if your comm layer uses QVector
        QVector<raypm::RayCalPoint> vec;
        vec.reserve(points.size());
        for (const auto &p : points)
            vec.push_back(p);

        // RayCommManager is a value member in your codebase (m_comm), not a pointer.
        m_comm.applyCouplerCalibrationTable(index, isForward, vec);

    }

    void MainWindow::onHardwareCalibrationCopyRequested(int couplerIndex,
                                                        bool fromForward,
                                                        bool toForward)
    {
        m_comm.copyCouplerCalibration(couplerIndex, fromForward, toForward);
    }

    void MainWindow::on_actionReset_to_Default_Layout_triggered()
    {
        if (!m_layoutManager || !ui)
            return;

        m_layoutManager->resetToDefaultLayout(ui->centralwidget, m_powerBar, m_forwardRefPanel);

        reapplyCouplerLayoutBindings();

        statusBar()->showMessage(tr("Layout reset to default."), 5000);
    }

    void MainWindow::on_actionLoad_Layout_triggered()
    {
        if (!m_layoutManager || !ui)
            return;

        const QString startDir = layoutConfigDirectory();

        const QString filePath = QFileDialog::getOpenFileName(this,
                                                              tr("Load Layout"),
                                                              startDir,
                                                              tr("Layout JSON (*.json);;All Files (*.*)"));

        if (filePath.isEmpty())
            return;

        if (!m_layoutManager->loadLayoutFromFile(filePath,
                                                 ui->centralwidget,
                                                 m_powerBar,
                                                 m_forwardRefPanel))
        {
            statusBar()->showMessage(tr("Failed to load layout from %1").arg(filePath), 5000);
            return;
        }

        // Reapply coupler config bindings after layout change
        reapplyCouplerLayoutBindings();

        // Reapply aux channel range settings to MiniScopes after layout change
        reapplyAuxRangesToMiniScopes();

        // Layout may contain FreqWidgets with Use Hamlib enabled.
        // Refresh Hamlib polling state based on the new layout.
        updateRigFreqPollTimer();

        // Layout may contain AuxDigital widgets.
        connectAuxDigitalWidgets();

        // Reapply AuxDigital config for newly loaded widgets.
        applyAuxDigitalConfigFromLayout();

        // Reapply AuxAnalog config for newly loaded widgets.
        m_applyLayoutAuxAnalogPending = true;
        m_applyLayoutAuxAnalogReceived.clear();
        tryApplyAuxAnalogConfigFromLayout();


        statusBar()->showMessage(tr("Loaded layout from %1").arg(filePath), 5000);
    }

    void MainWindow::on_actionSave_Layout_As_triggered()
    {
        if (!m_layoutManager)
            return;

        const QString startDir = layoutConfigDirectory();

        const QString filePath = QFileDialog::getSaveFileName(this,
                                                              tr("Save Layout As"),
                                                              defaultLayoutFilePath(),
                                                              tr("Layout JSON (*.json);All Files (*.*)"));

        if (filePath.isEmpty())
            return;

        if (!m_layoutManager->saveLayoutToFile(filePath))
        {
            statusBar()->showMessage(tr("Failed to save layout to %1").arg(filePath), 5000);
            return;
        }

        statusBar()->showMessage(tr("Saved layout to %1").arg(filePath), 5000);
    }

    // TODO(ray): In Design Mode the central area shrinks slightly when the
    // properties dock is shown (esp. with non-1.0 scaling). Geometry is
    // restored correctly on exit, but we should refine the width compensation
    // logic later.
    void MainWindow::on_actionDesign_Mode_triggered(bool enabled)
    {
        m_designModeEnabled = enabled;

        if (m_layoutManager)
            m_layoutManager->setDesignModeEnabled(enabled);

        if (enabled)
        {
            // --- ENTER DESIGN MODE ---

            // Save full window geometry (size + position)
            m_preDesignGeometryData = saveGeometry();
            m_hasPreDesignGeometryData = true;

            // Measure central width BEFORE showing the dock
            int cwBefore = 0;
            if (QWidget *cw = centralWidget())
                cwBefore = cw->width();

            // Show properties dock
            if (m_propertiesDock)
            {
                m_propertiesDock->show();
                m_propertiesDock->raise();
            }

            // Show layout tools dock
            if (m_layoutToolsDock)
            {
                m_layoutToolsDock->show();
                m_layoutToolsDock->raise();
            }

            // Let QMainWindow recompute layout
            qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

            // Iteratively adjust until central width matches original
            for (int i = 0; i < 3; ++i)
            {
                QWidget *cw = centralWidget();
                if (!cw)
                    break;

                int cwAfter = cw->width();
                int delta = cwBefore - cwAfter; // >0 => central area shrank

                if (qAbs(delta) <= 1)
                    break; // close enough

                QRect g = geometry();
                g.setWidth(g.width() + delta);
                setGeometry(g);

                qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
            }

            // Reset properties panel state
            m_selectedWidgetId.clear();

            if (m_propSelectedLabel)
                m_propSelectedLabel->setText(tr("<no widget selected>"));

            {
                // Reset property controls, blocking signals to avoid recursion
                QSignalBlocker b1(m_propMarginSpin);
                QSignalBlocker b2(m_propVisibleCheck);
                QSignalBlocker b3(m_propShowTicksCheck);
                QSignalBlocker b4(m_propShowLabelsCheck);

                if (m_propMarginSpin)
                    m_propMarginSpin->setValue(0);

                if (m_propVisibleCheck)
                    m_propVisibleCheck->setChecked(true);

                if (m_propShowTicksCheck)
                    m_propShowTicksCheck->setChecked(true);

                if (m_propShowLabelsCheck)
                    m_propShowLabelsCheck->setChecked(true);
            }

            // Tag central widget as being in design mode
            if (QWidget *cw = centralWidget())
                cw->setProperty("raypmDesignMode", true);

            // No widget selected yet → hide PowerBar-only rows
            setPowerBarPropertyRowsVisible(false);
            setGainPropertyRowsVisible(false);
            setMiniScopePropertyRowsVisible(false);

            statusBar()->showMessage(tr("Design Mode: click widgets to inspect/edit properties"), 5000);
        }
        else
        {
            // --- LEAVE DESIGN MODE ---

            if (m_propertiesDock)
                m_propertiesDock->hide();

            if (m_layoutToolsDock)
                m_layoutToolsDock->hide();

            if (m_hasPreDesignGeometryData)
            {
                restoreGeometry(m_preDesignGeometryData);
            }

            m_selectedGroupId.clear();
            m_selectedWidgetId.clear();

            statusBar()->showMessage(tr("Design Mode disabled"), 3000);

            if (QWidget *cw = centralWidget())
            {
                cw->setProperty("raypmDesignMode", false); // Tag as non-design mode
                if (QLayout *lay = cw->layout())
                    lay->activate();
                cw->update();
            }
        }

        updateDesignActionsEnabled();
    }

    void MainWindow::onLayoutWidgetClicked(const QString &groupId, const QString &widgetId)
    {
        if (!m_designModeEnabled)
            return;

        populatePropertiesDockForSelection(groupId, widgetId, /*fromDesignMode=*/true);
    }

    void MainWindow::populatePropertiesDockForSelection(const QString &groupId,
                                                        const QString &widgetId,
                                                        bool fromDesignMode)
    {
        m_selectedGroupId = groupId;

        const QString g = groupId.isEmpty() ? tr("<none>") : groupId;
        const QString w = widgetId.isEmpty() ? tr("<group>") : widgetId;

        if (fromDesignMode)
        {
            statusBar()->showMessage(tr("Design: group=%1, widget=%2").arg(g, w), 8000);

            // Update LayoutManager group selection highlight (only meaningful in design mode)
            if (m_layoutManager)
                m_layoutManager->setSelectedGroup(groupId);
        }
        else
        {
            // Runtime properties mode: lock down layout editing controls
            setWidgetPropertiesRuntimeMode(true);
        }

        // --- Group properties ---
        if (m_groupPropIdLabel)
            m_groupPropIdLabel->setText(g);

        bool groupHasCouplerBinding = false;

        if (m_layoutManager && !groupId.isEmpty())
        {
            QString groupTitle;
            int couplerIndex = 0;

            if (m_layoutManager->groupBinding(groupId, groupTitle, couplerIndex))
            {
                QSignalBlocker b1(m_groupNameEdit);
                QSignalBlocker b2(m_groupCouplerSpin);
                if (m_groupPropIdLabel)
                    m_groupPropIdLabel->setText(groupId);
                if (m_groupNameEdit)
                    m_groupNameEdit->setText(groupTitle);
                if (m_groupCouplerSpin)
                    m_groupCouplerSpin->setValue(couplerIndex);
            }

            QString bindingType;
            if (m_layoutManager->groupBindingType(groupId, bindingType))
                groupHasCouplerBinding = (bindingType == QLatin1String("coupler"));
        }

        //setGroupCouplerRowVisible(groupHasCouplerBinding);
        bool showCouplerRow = false;

        if (groupHasCouplerBinding && m_layoutManager && !widgetId.isEmpty())
        {
            const QString type = m_layoutManager->widgetType(widgetId);
            showCouplerRow = widgetTypeUsesCoupler(type);
        }

        setGroupCouplerRowVisible(showCouplerRow);

        // If click is on the group only (no widget), clear widget selection but KEEP dock visible
        if (widgetId.isEmpty() || !m_layoutManager)
        {
            m_selectedWidgetId.clear();

            if (m_propSelectedLabel)
                m_propSelectedLabel->setText(tr("<group only>"));

            setPowerBarPropertyRowsVisible(false);
            setGainPropertyRowsVisible(false);
            setMiniScopePropertyRowsVisible(false);
            setAuxSensorPropertyRowsVisible(false);
            setAuxDigitalPropertyRowsVisible(false);
            setFreqPropertyRowsVisible(false);

            if (m_propMinHeightSpin)
            {
                m_propMinHeightSpin->setVisible(false);
                if (m_propFormLayout)
                {
                    if (QWidget *label = m_propFormLayout->labelForField(m_propMinHeightSpin))
                        label->setVisible(false);
                }
            }

            if (m_propFormLayout && m_propSourceCombo)
            {
                if (QWidget *srcLabel = m_propFormLayout->labelForField(m_propSourceCombo))
                    srcLabel->setVisible(false);
                m_propSourceCombo->setVisible(false);
            }

            if (m_propertiesDock)
                m_propertiesDock->show();

            return;
        }

        // --- Reset all widget-specific rows; adapter will re-enable as needed ---
        setPowerBarPropertyRowsVisible(false);
        setGainPropertyRowsVisible(false);
        setMiniScopePropertyRowsVisible(false);
        setAuxSensorPropertyRowsVisible(false);
        setAuxDigitalPropertyRowsVisible(false);

        if (m_propMinHeightSpin)
        {
            m_propMinHeightSpin->setVisible(false);
            if (m_propFormLayout)
            {
                if (QWidget *label = m_propFormLayout->labelForField(m_propMinHeightSpin))
                    label->setVisible(false);
            }
        }

        // --- Widget selection ---
        m_selectedWidgetId = widgetId;

        // Load widget label (for all WidgetBase types)
        if (m_widgetLabelEdit && m_layoutManager)
        {
            QString label;
            QSignalBlocker b(m_widgetLabelEdit);
            if (m_layoutManager->widgetLabel(widgetId, label))
                m_widgetLabelEdit->setText(label);
            else
                m_widgetLabelEdit->clear();
        }

        int margin = 0;
        bool visible = true;
        m_layoutManager->widgetMargin(widgetId, margin);
        visible = m_layoutManager->widgetVisible(widgetId);

        populateSourceCombo();
        if (m_propSourceCombo)
        {
            m_propSourceCombo->setEnabled(true);

            if (m_propFormLayout)
            {
                if (QWidget *srcLabel = m_propFormLayout->labelForField(m_propSourceCombo))
                    srcLabel->setVisible(true);
            }
            m_propSourceCombo->setVisible(true);
        }

        if (m_propSelectedLabel)
        {
            const QString fullPath = QStringLiteral("%1 / %2").arg(g, w);
            m_propSelectedLabel->setToolTip(fullPath);
            m_propSelectedLabel->setText(QStringLiteral("%1\n%2").arg(g, w));
        }

        if (m_propMarginSpin)
        {
            QSignalBlocker b(static_cast<QObject *>(m_propMarginSpin));
            m_propMarginSpin->setValue(margin);
        }
        if (m_propVisibleCheck)
        {
            QSignalBlocker b(static_cast<QObject *>(m_propVisibleCheck));
            m_propVisibleCheck->setChecked(visible);
        }

        // --- Delegate to adapter for widget-type-specific properties ---
        const QString type = m_layoutManager->widgetType(widgetId);
        if (!type.isEmpty())
        {
            if (IWidgetPropertyAdapter *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this, m_layoutManager, groupId, widgetId);
                adapter->onWidgetSelected(ctx);
            }
        }

        if (m_propertiesDock)
            m_propertiesDock->show();
    }

    void MainWindow::setWidgetPropertiesRuntimeMode(bool enabled)
    {
        // Runtime "Widget Properties" mode:
        // We allow editing common layout/widget fields (name/label/margin/visible/source)
        // because the user explicitly opened Properties from the widget context menu.
        //
        // Design mode still exists for full layout tools; runtime mode is just a fast access path.

        m_widgetPropertiesModeEnabled = enabled;

        // Enable/disable common fields based on runtime mode
        const bool allowEdits = enabled || m_designModeEnabled;

        if (m_groupNameEdit)     m_groupNameEdit->setEnabled(allowEdits);
        if (m_groupCouplerSpin)  m_groupCouplerSpin->setEnabled(allowEdits);

        if (m_widgetLabelEdit)   m_widgetLabelEdit->setEnabled(allowEdits);
        if (m_propMarginSpin)    m_propMarginSpin->setEnabled(allowEdits);
        if (m_propMinHeightSpin) m_propMinHeightSpin->setEnabled(allowEdits);
        if (m_propVisibleCheck)  m_propVisibleCheck->setEnabled(allowEdits);
        if (m_propSourceCombo)   m_propSourceCombo->setEnabled(allowEdits);

        // Widget-specific UX controls (e.g., MeterMode) should also be editable in runtime mode
        if (m_propMeterModeCombo)
            m_propMeterModeCombo->setEnabled(allowEdits);
    }

    bool MainWindow::canEditWidgetProperties() const
    {
        return (m_designModeEnabled || m_widgetPropertiesModeEnabled);
    }

    void MainWindow::onWidgetPropertiesRequested(const QString &groupId, const QString &widgetId)
    {
        // Enter "Widget Properties" runtime mode (not full Design Mode)
        m_widgetPropertiesModeEnabled = true;

        if (m_propertiesDock)
        {
            m_propertiesDock->setWindowTitle(tr("Widget Properties"));
            m_propertiesDock->show();
            m_propertiesDock->raise();
        }

        // Populate the same UI, but with design-only behaviors disabled
        populatePropertiesDockForSelection(groupId, widgetId, /*fromDesignMode=*/false);
    }

    void MainWindow::onPropertyMarginChanged(int value)
    {
        const bool allow = (m_designModeEnabled || m_widgetPropertiesModeEnabled);
        if (!allow || m_selectedWidgetId.isEmpty() || !m_layoutManager)
            return;

        m_layoutManager->setWidgetMargin(m_selectedWidgetId, value);
    }

    void MainWindow::onPropertyMinHeightChanged(int px)
    {
        if (!m_layoutManager || m_selectedWidgetId.isEmpty())
            return;

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);

        // Only meaningful for Spacer
        if (type != QLatin1String("Spacer"))
            return;

        qDebug() << "[UI] MinHeight changed:"
                 << "widgetId=" << m_selectedWidgetId << "value=" << px;

        // Adapter first
        if (auto *adapter = adapterForWidgetType(type))
        {
            const WidgetPropertyContext ctx =
                makeWidgetPropertyContext(this,
                                          m_layoutManager,
                                          m_selectedGroupId,
                                          m_selectedWidgetId);

            if (adapter->handlePropertyChange(
                    ctx,
                    QStringLiteral("spacer.minHeight"),
                    QVariant::fromValue(px)))
            {
                return; // Adapter handled it
            }
        }

        // Fallback: legacy path (safe until we fully delete old code)
        m_layoutManager->setWidgetMinHeight(m_selectedWidgetId, px);
    }

    void MainWindow::onPropertyVisibleChanged(bool checked)
    {
        if (!canEditWidgetProperties() || m_selectedWidgetId.isEmpty() || !m_layoutManager)
            return;

        m_layoutManager->setWidgetVisible(m_selectedWidgetId, checked);
    }

    void MainWindow::onPropertyShowTicksChanged(bool checked)
    {
        if (!canEditWidgetProperties() || m_selectedWidgetId.isEmpty() || !m_layoutManager)
            return;

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);
        if (!type.isEmpty())
        {
            if (auto *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this,
                                              m_layoutManager,
                                              m_selectedGroupId,
                                              m_selectedWidgetId);

                if (adapter->handlePropertyChange(ctx,
                                                  QStringLiteral("powerbar.showTicks"),
                                                  QVariant::fromValue(checked)))
                {
                    return;
                }
            }
        }

        // Safety fallback (your LayoutManager supports this and updates the live widget)
        m_layoutManager->setWidgetShowTicks(m_selectedWidgetId, checked);
    }

    void MainWindow::onPropertyShowLabelsChanged(bool checked)
    {
        if (!canEditWidgetProperties() || m_selectedWidgetId.isEmpty() || !m_layoutManager)
            return;

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);
        if (!type.isEmpty())
        {
            if (auto *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this,
                                              m_layoutManager,
                                              m_selectedGroupId,
                                              m_selectedWidgetId);

                if (adapter->handlePropertyChange(ctx,
                                                  QStringLiteral("powerbar.showLabels"),
                                                  QVariant::fromValue(checked)))
                {
                    return;
                }
            }
        }

        m_layoutManager->setWidgetShowTickLabels(m_selectedWidgetId, checked);
    }

    void MainWindow::onPropertyShowThresholdLabelsChanged(bool checked)
    {
        if (!canEditWidgetProperties() || m_selectedWidgetId.isEmpty() || !m_layoutManager)
            return;

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);
        if (!type.isEmpty())
        {
            if (auto *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this,
                                              m_layoutManager,
                                              m_selectedGroupId,
                                              m_selectedWidgetId);

                if (adapter->handlePropertyChange(ctx,
                                                  QStringLiteral("powerbar.showThresholdLabels"),
                                                  QVariant::fromValue(checked)))
                {
                    return;
                }
            }
        }

        m_layoutManager->setWidgetShowThresholdLabels(m_selectedWidgetId, checked);
    }

    void MainWindow::onPropertyMeterModeChanged(int index)
    {
        const bool allow = m_designModeEnabled || m_widgetPropertiesModeEnabled;

        if (!allow || m_selectedWidgetId.isEmpty() || !m_layoutManager)
            return;

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);
        if (!type.isEmpty())
        {
            if (auto *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this,
                                              m_layoutManager,
                                              m_selectedGroupId,
                                              m_selectedWidgetId);

                if (adapter->handlePropertyChange(
                        ctx,
                        QStringLiteral("powerbar.meterMode"),
                        QVariant::fromValue(index)))
                {
                    return;
                }
            }
        }
    }

    void MainWindow::onPropertyFreqUseHamlibChanged(bool checked)
    {
        if (!canEditWidgetProperties() || !m_layoutManager || m_selectedWidgetId.isEmpty())
            return;

        // If Hamlib is not configured / tested, refuse the change and guide the user.
        if (checked)
        {
            if (!m_settings.hamlib.enabled || !m_settings.hamlib.lastTestOk)
            {
                QMessageBox::warning(
                    this,
                    tr("Hamlib not configured"),
                    tr("Hamlib must be enabled and have a successful Test in Setup \u2192 Hamlib\n"
                       "before it can be used as the Frequency source."));

                if (m_propFreqUseHamlibCheck)
                {
                    QSignalBlocker blocker(m_propFreqUseHamlibCheck);
                    m_propFreqUseHamlibCheck->setChecked(false);
                }
                return;
            }
        }

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);

        if (!type.isEmpty())
        {
            if (auto *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this,
                                              m_layoutManager,
                                              m_selectedGroupId,
                                              m_selectedWidgetId);

                adapter->handlePropertyChange(ctx,
                                              QStringLiteral("freq.useHamlib"),
                                              QVariant(checked));
            }
        }

        // Always ensure layout model is updated (and works even if adapter is missing)
        m_layoutManager->setWidgetFreqUseHamlib(m_selectedWidgetId, checked);

        updateRigFreqPollTimer();
    }

    void MainWindow::onPropertyFreqShowHzChanged(bool checked)
    {
        if (!canEditWidgetProperties() || !m_layoutManager || m_selectedWidgetId.isEmpty())
            return;

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);

        if (!type.isEmpty())
        {
            if (auto *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this,
                                              m_layoutManager,
                                              m_selectedGroupId,
                                              m_selectedWidgetId);

                adapter->handlePropertyChange(ctx,
                                              QStringLiteral("freq.showHz"),
                                              QVariant(checked));
            }
        }

        m_layoutManager->setWidgetFreqShowHz(m_selectedWidgetId, checked);
    }

    void MainWindow::onPropertySourceChanged(int index)
    {
        if (!m_layoutManager || !m_propSourceCombo)
            return;

        if (m_selectedWidgetId.isEmpty())
            return;

        const QString sourceKey = m_propSourceCombo->itemData(index).toString();
        // index 0 ("Use group default") has empty data => allow that to pass through
        // because it means "clear widget source"
        // (Your existing code returned early on empty; keep current behavior if you prefer)
        // We'll treat empty as "clear override"
        const QString newSource = sourceKey; // may be empty

        // If this group is a coupler-bound group, and the user picked CouplerN,
        // update the GROUP coupler index (so Coupler index spin stays correct).
        QString bindingType;
        const bool hasBindingType = m_layoutManager->groupBindingType(m_selectedGroupId, bindingType);
        const bool isCouplerBoundGroup = (hasBindingType && bindingType == QLatin1String("coupler"));

        if (isCouplerBoundGroup && newSource.startsWith(QLatin1String("Coupler")))
        {
            bool ok = false;
            const int couplerIdx = newSource.mid(QStringLiteral("Coupler").size()).toInt(&ok);
            if (ok)
            {
                // This updates RF widgets in the group AND should be reflected in the UI.
                m_layoutManager->setGroupCouplerIndex(m_selectedGroupId, couplerIdx);

                // Keep the Coupler index spin in sync immediately (avoid stale display).
                if (m_groupCouplerSpin)
                {
                    QSignalBlocker b(static_cast<QObject *>(m_groupCouplerSpin));
                    m_groupCouplerSpin->setValue(couplerIdx);
                }

                return; // done
            }
        }

        // Otherwise keep existing behavior: try adapter first, then set widget source.
        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);

        if (!type.isEmpty())
        {
            if (auto *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this,
                                              m_layoutManager,
                                              m_selectedGroupId,
                                              m_selectedWidgetId);

                // Generic "widget.source" key for all source-bound widgets.
                if (adapter->handlePropertyChange(ctx,
                                                  QStringLiteral("widget.source"),
                                                  QVariant::fromValue(newSource)))
                {
                    return;
                }
            }
        }

        // Apply widget-level override
        m_layoutManager->setWidgetSource(m_selectedWidgetId, newSource);
    }

    void MainWindow::onAddSelectedWidget()
    {
        // Must be in design mode, with a valid layout manager, UI, and selected group
        if (!m_designModeEnabled || !m_layoutManager || !ui || m_selectedGroupId.isEmpty())
            return;

        if (!m_widgetTypeCombo)
        {
            qWarning() << "[MainWindow] onAddSelectedWidget() - widgetTypeCombo is null";
            return;
        }

        const QString type = m_widgetTypeCombo->currentText();
        if (type.isEmpty())
        {
            qWarning() << "[MainWindow] onAddSelectedWidget() - empty widget type";
            return;
        }

        qDebug() << "[MainWindow] Adding widget of type" << type << "to group" << m_selectedGroupId;

        const QString newId = m_layoutManager->addWidgetToGroup(m_selectedGroupId,
                                                                type,
                                                                ui->centralwidget,
                                                                m_powerBar,
                                                                m_forwardRefPanel);

        if (newId.isEmpty())
        {
            qWarning() << "[MainWindow] addWidgetToGroup failed for type" << type;
            statusBar()->showMessage(tr("Failed to add widget to group %1").arg(m_selectedGroupId),
                                     3000);
            return;
        }

        // For AUX widgets, give a sensible default source instead of inheriting RF Coupler
        if (type == QLatin1String("AuxVoltage"))
        {
            // Bind to Aux0 by default; you can change to Aux1, Aux2... via the Source combo.
            m_layoutManager->setWidgetSource(newId, QStringLiteral("Aux0"));
        }

        // Make the newly-added widget the *actual* selected widget in the UI:
        // this updates m_selectedWidgetId, the "Selected widget:" label, Source combo,
        // and hides PowerBar-only rows for non-PowerBar widgets.
        onLayoutWidgetClicked(m_selectedGroupId, newId);

        // Re-apply coupler-specific bindings after the layout rebuild so PowerBars
        // keep their proper full-scale ranges.
        reapplyCouplerLayoutBindings();

        statusBar()->showMessage(tr("Added %1 to group %2").arg(type, m_selectedGroupId), 2000);
    }
    /*
    void MainWindow::onAddPowerBarToGroup()
    {
        if (!m_designModeEnabled || !m_layoutManager || !ui || m_selectedGroupId.isEmpty())
            return;

        const QString newId = m_layoutManager->addWidgetToGroup(m_selectedGroupId,
                                                                QStringLiteral("PowerBar"),
                                                                ui->centralwidget,
                                                                m_powerBar,
                                                                m_forwardRefPanel);

        if (newId.isEmpty()) {
            statusBar()->showMessage(tr("Failed to add Power Bar to group %1").arg(m_selectedGroupId),
                                     4000);
            return;
        }

        m_selectedWidgetId = newId;
        statusBar()->showMessage(tr("Added Power Bar to group %1").arg(m_selectedGroupId), 3000);
    }

    void MainWindow::onAddForwardRefToGroup()
    {
        if (!m_designModeEnabled || !m_layoutManager || !ui || m_selectedGroupId.isEmpty())
            return;

        const QString newId = m_layoutManager->addWidgetToGroup(m_selectedGroupId,
                                                                QStringLiteral("ForwardRefPanel"),
                                                                ui->centralwidget,
                                                                m_powerBar,
                                                                m_forwardRefPanel);

        if (newId.isEmpty()) {
            statusBar()
                ->showMessage(tr("Failed to add Forward/Ref panel to group %1").arg(m_selectedGroupId),
                              4000);
            return;
        }

        m_selectedWidgetId = newId;

        reapplyCouplerLayoutBindings();
        statusBar()->showMessage(tr("Added Forward/Ref panel to group %1").arg(m_selectedGroupId), 3000);
    }
    */
    void MainWindow::onRemoveSelectedWidget()
    {
        // Only valid in design mode with a selected widget
        if (!m_designModeEnabled || !m_layoutManager || !ui || m_selectedWidgetId.isEmpty())
        {
            qWarning() << "[MainWindow] onRemoveSelectedWidget() ignored -"
                       << "designMode=" << m_designModeEnabled
                       << "layoutManager=" << (m_layoutManager != nullptr) << "ui=" << (ui != nullptr)
                       << "selectedId=" << m_selectedWidgetId;
            return;
        }

        if (!ui->centralwidget)
        {
            qWarning() << "[MainWindow] onRemoveSelectedWidget() - centralwidget is null";
            return;
        }

        qDebug() << "[MainWindow] Removing widget id" << m_selectedWidgetId;

        // Ask LayoutManager to remove it from the JSON + rebuild the central layout
        if (!m_layoutManager->removeWidget(m_selectedWidgetId,
                                           ui->centralwidget,
                                           m_powerBar,
                                           m_forwardRefPanel))
        {
            statusBar()->showMessage(tr("Failed to remove widget from layout."), 3000);
            qWarning() << "[MainWindow] removeWidget() returned false for id" << m_selectedWidgetId;
            return;
        }

        statusBar()->showMessage(tr("Removed widget from layout."), 2000);

        // Clear current widget selection
        m_selectedWidgetId.clear();
        if (m_propSelectedLabel)
            m_propSelectedLabel->setText(tr("<no widget selected>"));

        // Disable widget-level property controls
        if (m_propMarginSpin)
        {
            m_propMarginSpin->setEnabled(false);
            m_propMarginSpin->setValue(0);
        }

        if (m_propVisibleCheck)
        {
            m_propVisibleCheck->setEnabled(false);
            m_propVisibleCheck->setChecked(true);
        }

        if (m_propMeterModeCombo)
        {
            m_propMeterModeCombo->setEnabled(false);
            m_propMeterModeCombo->setCurrentIndex(0);
        }

        if (m_propShowTicksCheck)
        {
            m_propShowTicksCheck->setEnabled(false);
            m_propShowTicksCheck->setChecked(true);
        }

        if (m_propShowLabelsCheck)
        {
            m_propShowLabelsCheck->setEnabled(false);
            m_propShowLabelsCheck->setChecked(true);
        }

        if (m_propSourceCombo)
        {
            m_propSourceCombo->setEnabled(false);
            m_propSourceCombo->clear();
        }

        // Keep dock visible so the user sees "no widget selected" in design mode
        if (m_propertiesDock && m_designModeEnabled)
            m_propertiesDock->show();

        // Re-apply any coupler → widget bindings after the layout rebuild
        reapplyCouplerLayoutBindings();

        qDebug() << "[MainWindow] onRemoveSelectedWidget() finished.";
    }

    void MainWindow::onMoveSelectedWidgetUp()
    {
        if (!m_designModeEnabled || !m_layoutManager || !ui || m_selectedWidgetId.isEmpty())
            return;

        if (!m_layoutManager->moveWidget(m_selectedWidgetId,
                                         LayoutManager::MoveDirection::Up,
                                         ui->centralwidget,
                                         m_powerBar,
                                         m_forwardRefPanel))
        {
            statusBar()->showMessage(tr("Cannot move widget further up"), 3000);
            return;
        }

        reapplyCouplerLayoutBindings();
    }

    void MainWindow::onMoveSelectedWidgetDown()
    {
        if (!m_designModeEnabled || !m_layoutManager || !ui || m_selectedWidgetId.isEmpty())
            return;

        if (!m_layoutManager->moveWidget(m_selectedWidgetId,
                                         LayoutManager::MoveDirection::Down,
                                         ui->centralwidget,
                                         m_powerBar,
                                         m_forwardRefPanel))
        {
            statusBar()->showMessage(tr("Cannot move widget further down"), 3000);
            return;
        }

        reapplyCouplerLayoutBindings();
    }

    void MainWindow::onAddGroupAbove()
    {
        if (!m_designModeEnabled || !m_layoutManager || !ui || m_selectedGroupId.isEmpty())
            return;

        const QString newGroupId = m_layoutManager
                                       ->addGroupRelativeTo(m_selectedGroupId,
                                                            LayoutManager::GroupInsertPos::Above,
                                                            ui->centralwidget,
                                                            m_powerBar,
                                                            m_forwardRefPanel);
        if (newGroupId.isEmpty())
        {
            statusBar()->showMessage(tr("Failed to add group above %1").arg(m_selectedGroupId), 4000);
            return;
        }

        // Ask the user for a title and apply it
        const QString title = promptForNewGroupTitle(tr("New Group"));
        if (!title.isEmpty())
            m_layoutManager->setGroupTitle(newGroupId, title);

        // Select the newly created group
        m_selectedGroupId = newGroupId;
        m_selectedWidgetId.clear();

        if (m_layoutManager)
            m_layoutManager->setSelectedGroup(newGroupId);

        // Update the properties dock as if the group was clicked
        onLayoutWidgetClicked(m_selectedGroupId, QString());

        if (m_propertiesDock && m_designModeEnabled)
            m_propertiesDock->show();

        // Re-apply bindings (titles, coupler index, etc.)
        reapplyCouplerLayoutBindings();

        statusBar()->showMessage(tr("Group added"), 3000);
    }

    void MainWindow::onAddGroupBelow()
    {
        if (!m_designModeEnabled || !m_layoutManager || !ui || m_selectedGroupId.isEmpty())
            return;

        const QString newGroupId = m_layoutManager
                                       ->addGroupRelativeTo(m_selectedGroupId,
                                                            LayoutManager::GroupInsertPos::Below,
                                                            ui->centralwidget,
                                                            m_powerBar,
                                                            m_forwardRefPanel);
        if (newGroupId.isEmpty())
        {
            statusBar()->showMessage(tr("Failed to add group below %1").arg(m_selectedGroupId), 4000);
            return;
        }

        // Ask the user for a title and apply it
        const QString title = promptForNewGroupTitle(tr("New Group"));
        if (!title.isEmpty())
            m_layoutManager->setGroupTitle(newGroupId, title);

        // Select the newly created group
        m_selectedGroupId = newGroupId;
        m_selectedWidgetId.clear();

        if (m_layoutManager)
            m_layoutManager->setSelectedGroup(newGroupId);

        // Update the properties dock as if the group was clicked
        onLayoutWidgetClicked(m_selectedGroupId, QString());

        if (m_propertiesDock && m_designModeEnabled)
            m_propertiesDock->show();

        reapplyCouplerLayoutBindings();

        statusBar()->showMessage(tr("Group added"), 3000);
    }

    void MainWindow::onAddGroupLeft() {}

    void MainWindow::onAddGroupRight() {}

    void MainWindow::onRemoveSelectedGroup()
    {
        if (!m_designModeEnabled || !m_layoutManager || !ui || m_selectedGroupId.isEmpty())
            return;

        if (!m_layoutManager->removeGroup(m_selectedGroupId,
                                          ui->centralwidget,
                                          m_powerBar,
                                          m_forwardRefPanel))
        {
            statusBar()->showMessage(tr("Failed to remove group %1").arg(m_selectedGroupId), 4000);
            return;
        }

        m_selectedGroupId.clear();
        m_selectedWidgetId.clear();
        if (m_propertiesDock)
            m_propertiesDock->hide();

        statusBar()->showMessage(tr("Group removed"), 3000);
    }

    void MainWindow::onMoveSelectedGroup()
    {
        if (!m_designModeEnabled || !m_layoutManager || !ui || m_selectedGroupId.isEmpty())
            return;

        if (!m_groupMoveDirectionCombo)
            return;

        const QVariant data = m_groupMoveDirectionCombo->currentData();
        if (!data.isValid())
            return;

        auto dir = static_cast<LayoutManager::GroupMoveDirection>(data.toInt());

        if (!m_layoutManager
                 ->moveGroup(m_selectedGroupId, dir, ui->centralwidget, m_powerBar, m_forwardRefPanel))
        {
            statusBar()->showMessage(tr("Cannot move group %1 in that direction").arg(m_selectedGroupId),
                                     4000);
            return;
        }

        // Re-apply coupler bindings so titles/scales stay correct
        reapplyCouplerLayoutBindings();

        // Keep the properties dock and selection consistent
        if (m_layoutManager)
            m_layoutManager->setSelectedGroup(m_selectedGroupId);

        if (m_propertiesDock && m_designModeEnabled)
            m_propertiesDock->show();

        // Refresh properties panel text without faking a click
        onLayoutWidgetClicked(m_selectedGroupId, QString());

        statusBar()
            ->showMessage(tr("Group moved %1").arg(m_groupMoveDirectionCombo->currentText().toLower()),
                          2000);
    }

    void MainWindow::onAddSelectedGroup()
    {
        if (!m_designModeEnabled || !m_layoutManager || !ui)
            return;

        /*    // This assumes your combo stores GroupInsertPos in itemData()
        const auto pos = static_cast<raypm::LayoutManager::GroupInsertPos>(
            m_groupAddDirectionCombo->currentData().toInt());
    */
        if (!m_groupAddDirectionCombo)
            return;

        QVariant data = m_groupAddDirectionCombo->currentData();
        LayoutManager::GroupInsertPos pos = LayoutManager::GroupInsertPos::Below;

        if (data.isValid())
        {
            pos = static_cast<LayoutManager::GroupInsertPos>(data.toInt());
        }
        else
        {
            const QString t = m_groupAddDirectionCombo->currentText();
            if (t.contains("Above", Qt::CaseInsensitive))
                pos = LayoutManager::GroupInsertPos::Above;
            else if (t.contains("Below", Qt::CaseInsensitive))
                pos = LayoutManager::GroupInsertPos::Below;
            else if (t.contains("Left", Qt::CaseInsensitive))
                pos = LayoutManager::GroupInsertPos::Left;
            else if (t.contains("Right", Qt::CaseInsensitive))
                pos = LayoutManager::GroupInsertPos::Right;
        }

        QString newGroupId = m_layoutManager->addGroupRelativeTo(m_selectedGroupId,
                                                                 pos,
                                                                 ui->centralwidget,
                                                                 m_powerBar,
                                                                 m_forwardRefPanel);

        if (newGroupId.isEmpty())
        {
            statusBar()->showMessage(tr("Failed to add group"), 4000);
            return;
        }

        const QString title = promptForNewGroupTitle(tr("New Group"));
        if (!title.isEmpty())
            m_layoutManager->setGroupTitle(newGroupId, title);

        // Select the new group + refresh properties
        m_selectedGroupId = newGroupId;
        m_selectedWidgetId.clear();

        m_layoutManager->setSelectedGroup(newGroupId);
        onLayoutWidgetClicked(m_selectedGroupId, QString());

        if (m_propertiesDock && m_designModeEnabled)
            m_propertiesDock->show();

        reapplyCouplerLayoutBindings();
        statusBar()->showMessage(tr("Group added"), 3000);
    }

    void MainWindow::onGroupNameChanged(const QString &text)
    {
        const bool allow = (m_designModeEnabled || m_widgetPropertiesModeEnabled);
        if (!allow || !m_layoutManager || m_selectedGroupId.isEmpty())
            return;

        // m_layoutManager->setGroupTitle(m_selectedGroupId, text);
        const QString trimmed = text.trimmed();
        m_layoutManager->setGroupTitle(m_layoutManager->selectedGroupId(), trimmed);
    }

    void MainWindow::onGroupCouplerIndexChanged(int value)
    {
        const bool allow = (m_designModeEnabled || m_widgetPropertiesModeEnabled);
        if (!allow || !m_layoutManager || m_selectedGroupId.isEmpty())
            return;

        m_layoutManager->setGroupCouplerIndex(m_selectedGroupId, value);

        // Optional: immediately re-apply coupler config if we have it cached
        auto it = m_couplerConfigs.constFind(value);
        if (it != m_couplerConfigs.constEnd())
        {
            m_layoutManager->applyCouplerConfig(it.value());
        }
    }

    void MainWindow::onWidgetLabelEdited(const QString &text)
    {
        if (!m_layoutManager || m_selectedWidgetId.isEmpty())
            return;

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);

        if (!type.isEmpty())
        {
            if (auto *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this,
                                              m_layoutManager,
                                              m_selectedGroupId,
                                              m_selectedWidgetId);

                // Let the adapter handle "widget.label" if it wants to.
                if (adapter->handlePropertyChange(ctx,
                                                  QStringLiteral("widget.label"),
                                                  QVariant::fromValue(text)))
                {
                    return; // Adapter took care of it.
                }
            }
        }

        // Fallback: legacy behavior
        m_layoutManager->setWidgetLabel(m_selectedWidgetId, text);
    }

    // Helper to show/hide PowerBar-specific property rows
    void MainWindow::setPowerBarPropertyRowsVisible(bool visible)
    {
        if (!m_propFormLayout)
            return;

        auto updateRow = [this, visible](QWidget *field)
        {
            if (!field)
                return;
            if (QWidget *label = m_propFormLayout->labelForField(field))
                label->setVisible(visible);
            field->setVisible(visible);
        };

        updateRow(m_propMeterModeCombo);
        updateRow(m_propShowTicksCheck);
        updateRow(m_propShowLabelsCheck);
        updateRow(m_propShowThresholdLabelsCheck);
    }

    void MainWindow::setGainPropertyRowsVisible(bool visible)
    {
        if (!m_propFormLayout)
            return;

        auto toggleRow = [this, visible](QWidget *field)
        {
            if (!field)
                return;
            field->setVisible(visible);
            if (QLabel *lbl = qobject_cast<QLabel *>(m_propFormLayout->labelForField(field)))
            {
                lbl->setVisible(visible);
            }
        };

        toggleRow(m_propGainInSourceCombo);
        toggleRow(m_propGainOutSourceCombo);
        toggleRow(m_propGainAttnDbSpin);
    }

    void MainWindow::setFreqPropertyRowsVisible(bool visible)
    {
        if (!m_propFormLayout || !m_propFreqUseHamlibCheck || !m_propFreqShowHzCheck)
            return;

        if (QWidget *label = m_propFormLayout->labelForField(m_propFreqUseHamlibCheck))
            label->setVisible(visible);
        m_propFreqUseHamlibCheck->setVisible(visible);

        if (QWidget *label = m_propFormLayout->labelForField(m_propFreqShowHzCheck))
            label->setVisible(visible);
        m_propFreqShowHzCheck->setVisible(visible);
    }

    void MainWindow::populateGainSourceCombos()
    {
        if (!m_layoutManager || !m_propGainInSourceCombo || !m_propGainOutSourceCombo)
            return;

        m_propGainInSourceCombo->blockSignals(true);
        m_propGainOutSourceCombo->blockSignals(true);

        m_propGainInSourceCombo->clear();
        m_propGainOutSourceCombo->clear();

        // Only RF couplers (Coupler0..CouplerN) make sense for gain
        for (int i = 0; i < MAX_COUPLERS; ++i)
        {
            const QString key = QStringLiteral("Coupler%1").arg(i);
            QString label = tr("Coupler %1").arg(i + 1);

            auto cfgIt = m_couplerConfigs.constFind(i);
            if (cfgIt != m_couplerConfigs.constEnd() && !cfgIt->name.isEmpty())
            {
                label += QStringLiteral(" (%1)").arg(cfgIt->name);
            }

            m_propGainInSourceCombo->addItem(label, key);
            m_propGainOutSourceCombo->addItem(label, key);
        }

        // Select current values from the layout model, if present
        if (!m_selectedWidgetId.isEmpty())
        {
            QString inSrc, outSrc;
            if (m_layoutManager->widgetGainSources(m_selectedWidgetId, inSrc, outSrc))
            {
                auto pick = [](QComboBox *combo, const QString &value)
                {
                    if (!combo)
                        return;
                    int idxToSelect = -1;
                    for (int i = 0; i < combo->count(); ++i)
                    {
                        if (combo->itemData(i).toString() == value)
                        {
                            idxToSelect = i;
                            break;
                        }
                    }
                    if (idxToSelect >= 0)
                        combo->setCurrentIndex(idxToSelect);
                };

                pick(m_propGainInSourceCombo, inSrc);
                pick(m_propGainOutSourceCombo, outSrc);
            }
        }

        m_propGainInSourceCombo->blockSignals(false);
        m_propGainOutSourceCombo->blockSignals(false);

        double attnDb = 0.0;
        if (m_propGainAttnDbSpin
            && m_layoutManager->widgetGainAttnDb(m_selectedWidgetId, attnDb))
        {
            m_propGainAttnDbSpin->setValue(attnDb);
        }
    }

    // Pick the engineering max based on the Aux type
    double MainWindow::auxMaxRangeForConfig(const raypm::RayAuxConfig &cfg) const
    {
        const QString type = cfg.typeName.toLower();

        if (type == QLatin1String("voltage"))
        {
            // Volts
            return (cfg.voltageMaxRange > 0.0) ? cfg.voltageMaxRange : AUX_VOLTAGE_MAX_RANGE_DEFAULT;
        }
        else if (type == QLatin1String("current"))
        {
            // Amps
            return (cfg.currentMaxRange > 0.0) ? cfg.currentMaxRange : AUX_CURRENT_MAX_RANGE_DEFAULT;
        }
        else if (type == QLatin1String("temperature"))
        {
            // °C
            return (cfg.temperatureMaxRange > 0.0) ? cfg.temperatureMaxRange : AUX_TEMPERATURE_MAX_RANGE_DEFAULT;
        }

        return 0.0;
    }

    // For a given Aux index, push that range into all MiniScopes bound to it
    void MainWindow::applyAuxRangeToMiniScopes(int auxIndex)
    {
        if (!m_layoutManager)
            return;

        const auto it = m_auxConfigs.constFind(auxIndex);
        if (it == m_auxConfigs.constEnd())
            return;

        const double maxRange = auxMaxRangeForConfig(it.value());
        if (maxRange <= 0.0)
            return;

        const QString auxSourceKey = QStringLiteral("Aux%1").arg(auxIndex);
        const auto auxWidgets = m_layoutManager->widgetsForSource(auxSourceKey);

        for (WidgetBase *wb : auxWidgets)
        {
            if (!wb)
                continue;

            if (auto *ms = qobject_cast<MiniScopeWidget *>(wb))
            {
                // Only force a fixed range when auto-scale is OFF
                if (!ms->autoScale())
                    ms->applyManualYRangeHint(maxRange);
            }
        }
    }

    void MainWindow::reapplyAuxRangesToMiniScopes()
    {
        if (!m_layoutManager)
            return;

        // m_auxConfigs is keyed by Aux index (0,1,2)
        for (auto it = m_auxConfigs.cbegin(); it != m_auxConfigs.cend(); ++it)
        {
            const int auxIndex = it.key();
            applyAuxRangeToMiniScopes(auxIndex);
        }
    }

    /**
     * @brief Show or hide all AuxSensor property rows in the Layout Properties panel.
     *
     * This controls the three Aux-related fields:
     *  - Aux mode
     *  - Temp units
     *  - Aux label
     *
     * When @p visible is false both the field widgets *and* their form labels
     * are hidden, so the rows disappear entirely for non-AuxSensor widgets.
     */
    void MainWindow::setAuxSensorPropertyRowsVisible(bool visible)
    {
        if (!m_propFormLayout)
            return;

        auto toggleRow = [&](QWidget *field)
        {
            if (!field)
                return;
            if (QWidget *label = m_propFormLayout->labelForField(field))
                label->setVisible(visible);
            field->setVisible(visible);
        };

        toggleRow(m_propAuxSensorModeCombo);
        toggleRow(m_propAuxSensorUnitsCombo);
        toggleRow(m_propAuxSensorLabelEdit);
    }

    void MainWindow::setAuxDigitalPropertyRowsVisible(bool visible)
    {
        if (!m_propFormLayout || !m_propAuxDigitalModeCombo)
            return;

        if (QWidget *label = m_propFormLayout->labelForField(m_propAuxDigitalModeCombo))
        {
            label->setVisible(visible);
        }
        m_propAuxDigitalModeCombo->setVisible(visible);
    }

    bool MainWindow::widgetTypeUsesCoupler(const QString &type)
    {
        return type == "PowerBar"
            || type == "PowerBarVert"
            || type == "ForwardRefPanel"
            || type == "AnalogPowerMeter";
    }

    void MainWindow::setGroupCouplerRowVisible(bool visible)
    {
        if (!m_propFormLayout || !m_groupCouplerSpin)
            return;

        if (QWidget *label = m_propFormLayout->labelForField(m_groupCouplerSpin))
            label->setVisible(visible);

        m_groupCouplerSpin->setVisible(visible);
    }

    void raypm::MainWindow::onProtectionResetRequested()
    {
        // Forward the request to the hardware via RayCommManager.
        // You’ll fill in the internals of requestProtectionReset().
        m_comm.requestProtectionReset();
    }

    // Summary: User changed AuxSensor "temperature units" (°C / °F).
    // Persist to LayoutManager and update the live AuxSensorWidget.
    void MainWindow::onPropertyAuxSensorUnitsChanged(int index)
    {
        if (!canEditWidgetProperties() || !m_layoutManager || m_selectedWidgetId.isEmpty() || !m_propAuxSensorUnitsCombo)
            return;

        AuxSensorWidget::Mode mode = AuxSensorWidget::Mode::Unknown;
        int tempUnitsIndex = 0;
        QString label;

        if (!m_layoutManager->widgetAuxSensorConfig(m_selectedWidgetId, mode, tempUnitsIndex, label))
            return;

        tempUnitsIndex = index;

        // Persist to model / JSON (we will also make LayoutManager push to live widget; see below)
        m_layoutManager->setWidgetAuxSensorConfig(m_selectedWidgetId, mode, tempUnitsIndex, label);
    }

    // Summary: User changed AuxSensor "mode" (Voltage / Current / Temperature).
    // Persist to LayoutManager and update the live AuxSensorWidget.
    void MainWindow::onPropertyAuxSensorModeChanged(int index)
    {
        if (!canEditWidgetProperties() || !m_layoutManager || m_selectedWidgetId.isEmpty() || !m_propAuxSensorModeCombo)
            return;

        const QVariant data = m_propAuxSensorModeCombo->itemData(index);
        if (!data.isValid())
            return;

        const auto newMode = static_cast<AuxSensorWidget::Mode>(data.toInt());

        AuxSensorWidget::Mode currentMode = AuxSensorWidget::Mode::Unknown;
        int tempUnitsIndex = 0;
        QString label;

        if (!m_layoutManager->widgetAuxSensorConfig(m_selectedWidgetId, currentMode, tempUnitsIndex, label))
            return;

        m_layoutManager->setWidgetAuxSensorConfig(m_selectedWidgetId, newMode, tempUnitsIndex, label);

        if (m_propAuxSensorUnitsCombo)
            m_propAuxSensorUnitsCombo->setEnabled(newMode == AuxSensorWidget::Mode::Temperature);
    }

    // Summary: User edited the AuxSensor label in the property panel.
    // Persist to LayoutManager, update the live widget, and keep the
    // generic widget-label editor in sync.
    void MainWindow::onPropertyAuxSensorLabelEdited(const QString &text)
    {
        if (!canEditWidgetProperties() || !m_layoutManager || m_selectedWidgetId.isEmpty())
            return;

        AuxSensorWidget::Mode mode = AuxSensorWidget::Mode::Unknown;
        int tempUnitsIndex = 0;
        QString currentLabel;

        if (!m_layoutManager->widgetAuxSensorConfig(m_selectedWidgetId, mode, tempUnitsIndex, currentLabel))
            return;

        const QString newLabel = text.trimmed();

        m_layoutManager->setWidgetAuxSensorConfig(m_selectedWidgetId, mode, tempUnitsIndex, newLabel);

        if (m_widgetLabelEdit)
        {
            QSignalBlocker blocker(m_widgetLabelEdit);
            m_widgetLabelEdit->setText(newLabel);
        }
    }

    void MainWindow::onPropertyAuxDigitalModeChanged(int index)
    {
        if (!canEditWidgetProperties() || !m_layoutManager || m_selectedWidgetId.isEmpty())
            return;

        if (!m_propAuxDigitalModeCombo)
            return;

        const int modeInt = m_propAuxDigitalModeCombo->itemData(index).toInt();

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);
        auto *adapter = adapterForWidgetType(type);
        if (!adapter)
            return;

        WidgetPropertyContext ctx;
        ctx.window = this;
        ctx.layoutManager = m_layoutManager;
        ctx.widgetId = m_selectedWidgetId;

        // Update layout + live widget (adapter) + also persist via LayoutManager (below)
        adapter->handlePropertyChange(ctx, QStringLiteral("auxdig.mode"), modeInt);

        // Persist (and we will also make LayoutManager push to live widget; see below)
        m_layoutManager->setWidgetAuxDigitalMode(m_selectedWidgetId, static_cast<AuxDigitalWidget::Mode>(modeInt));

        // Also push the mode to firmware via /api/aux_digital/config
        QString source;
        if (!m_layoutManager->widgetSource(m_selectedWidgetId, source))
            return;

        const int auxIndex = auxDigitalIndexFromSource(source);
        if (auxIndex < 0)
            return;

        m_comm.applyAuxDigitalConfig(auxIndex, modeInt);
    }

    // Helper to enable/disable design-mode layout actions
    void MainWindow::updateDesignActionsEnabled()
    {
        const bool enabled = m_designModeEnabled;

        auto setAct = [enabled](QAction *act)
        {
            if (act)
                act->setEnabled(enabled);
        };

        setAct(m_actAddPowerBarToGroup);
        setAct(m_actAddForwardRefToGroup);
        setAct(m_actRemoveWidget);
        setAct(m_actMoveWidgetUp);
        setAct(m_actMoveWidgetDown);
        setAct(m_actAddGroupAbove);
        setAct(m_actAddGroupBelow);
        setAct(m_actRemoveGroup);
        setAct(m_actAddGroup);
        setAct(m_actMoveGroup);

        if (m_groupMoveDirectionCombo)
            m_groupMoveDirectionCombo->setEnabled(enabled);

        if (m_groupAddDirectionCombo)
            m_groupAddDirectionCombo->setEnabled(enabled);
    }

    void MainWindow::onPropertyGainInSourceChanged(int index)
    {
        if (!canEditWidgetProperties() || !m_layoutManager || m_selectedWidgetId.isEmpty())
            return;
        if (!m_propGainInSourceCombo || !m_propGainOutSourceCombo)
            return;
        if (index < 0 || index >= m_propGainInSourceCombo->count())
            return;

        const QString inSrcKey = m_propGainInSourceCombo->itemData(index).toString();
        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);

        if (IWidgetPropertyAdapter *adapter = adapterForWidgetType(type))
        {
            const WidgetPropertyContext ctx =
                makeWidgetPropertyContext(this, m_layoutManager, m_selectedGroupId, m_selectedWidgetId);

            adapter->handlePropertyChange(ctx, QStringLiteral("gain.rfInSource"), QVariant(inSrcKey));
            return;
        }

        QString outSrcKey;
        if (m_propGainOutSourceCombo->currentIndex() >= 0)
            outSrcKey = m_propGainOutSourceCombo->currentData().toString();

        m_layoutManager->setWidgetGainSources(m_selectedWidgetId, inSrcKey, outSrcKey);
    }

    void MainWindow::onPropertyGainOutSourceChanged(int index)
    {
        if (!canEditWidgetProperties() || !m_layoutManager || m_selectedWidgetId.isEmpty())
            return;
        if (!m_propGainInSourceCombo || !m_propGainOutSourceCombo)
            return;
        if (index < 0 || index >= m_propGainOutSourceCombo->count())
            return;

        const QString outSrcKey = m_propGainOutSourceCombo->itemData(index).toString();
        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);

        if (IWidgetPropertyAdapter *adapter = adapterForWidgetType(type))
        {
            const WidgetPropertyContext ctx =
                makeWidgetPropertyContext(this, m_layoutManager, m_selectedGroupId, m_selectedWidgetId);

            adapter->handlePropertyChange(ctx, QStringLiteral("gain.rfOutSource"), QVariant(outSrcKey));
            return;
        }

        QString inSrcKey;
        if (m_propGainInSourceCombo->currentIndex() >= 0)
            inSrcKey = m_propGainInSourceCombo->currentData().toString();

        m_layoutManager->setWidgetGainSources(m_selectedWidgetId, inSrcKey, outSrcKey);
    }

    // Helper to reapply coupler config bindings after layout changes
    void MainWindow::reapplyCouplerLayoutBindings()
    {
        if (!m_layoutManager)
            return;

        for (auto it = m_couplerConfigs.cbegin(); it != m_couplerConfigs.cend(); ++it)
        {
            const RayCouplerConfig &cfg = it.value();

            // 1) Update group titles
            m_layoutManager->applyCouplerConfig(cfg);

            // 2) Apply bar scaling to all widgets bound to this coupler
            auto applyScaleToBar = [&](auto *bar)
            {
                if (!bar)
                    return;

                const double fullScaleW = cfg.fullScaleW;
                const double fwdRange = cfg.fwdBarRangeW;
                double maxRange = (fwdRange > 0.0) ? fwdRange
                                                   : (fullScaleW > 0.0 ? fullScaleW : 1800.0);

                bar->setMaxPower(static_cast<int>(std::ceil(maxRange)));

                double major;
                if (maxRange <= 100.0)
                    major = 10.0;
                else if (maxRange <= 500.0)
                    major = 50.0;
                else if (maxRange <= 1000.0)
                    major = 100.0;
                else
                    major = 200.0;

                const double minor = major / 2.0;
                bar->setTickConfig(major, minor, true);

                const double yellowW = maxRange * 0.70;
                const double redW = maxRange * 0.90;

                bar->setThresholds(static_cast<int>(std::round(yellowW)),
                                   static_cast<int>(std::round(redW)));
            };

            const QString sourceKey = QStringLiteral("Coupler%1").arg(cfg.index);
            const auto widgets = m_layoutManager->widgetsForSource(sourceKey);

            for (WidgetBase *wb : widgets)
            {
                if (!wb)
                    continue;

                if (auto *barH = qobject_cast<PowerBarWidget *>(wb))
                {
                    applyScaleToBar(barH);
                }
                else if (auto *barV = qobject_cast<PowerBarVertWidget *>(wb))
                {
                    applyScaleToBar(barV);
                }
            }

            // Legacy summary bar for coupler 0
            if (cfg.index == 0 && m_powerBar)
            {
                applyScaleToBar(m_powerBar);
            }
        }
    }

    void MainWindow::initializeLayoutFromSettings()
    {
        if (!m_layoutManager || !ui || !ui->centralwidget)
            return;

        bool loadedFromFile = false;

        if (!m_settings.startupLayoutName.isEmpty())
        {
            const QString layoutsDir = layoutConfigDirectory();
            const QDir dir(layoutsDir);
            const QString candidate = dir.filePath(m_settings.startupLayoutName);

            if (QFileInfo::exists(candidate))
            {
                loadedFromFile = m_layoutManager->loadLayoutFromFile(candidate,
                                                                     ui->centralwidget,
                                                                     m_powerBar,
                                                                     m_forwardRefPanel);
            }
        }

        if (!loadedFromFile)
        {
            // Fall back to built-in default JSON
            m_layoutManager->buildInitialLayout(ui->centralwidget, m_powerBar, m_forwardRefPanel);
        }

        // Design-mode click notifications (connect only once)
        connect(m_layoutManager,
                &LayoutManager::widgetClicked,
                this,
                &MainWindow::onLayoutWidgetClicked);

        // Tell LayoutManager about number of grig columns
        m_layoutManager->setGroupGridColumns(m_settings.layoutGridColumns);

        // Start with the rig frequency polling timer state per settings
        updateRigFreqPollTimer();

        // Re-apply aux input ranges to MiniScopes
        reapplyAuxRangesToMiniScopes();

        // Wire AuxDigital widgets to the setAuxDigitalValue() API
        connectAuxDigitalWidgets();

        // Reapply AuxDigital config for newly loaded widgets.
        //applyAuxDigitalConfigFromLayout();

        // Reapply AuxAnalog config (Aux0..Aux2) for newly loaded widgets.
        //applyAuxAnalogConfigFromLayout();
    }

    QString MainWindow::layoutConfigDirectory() const
    {
        // AppConfigLocation is already ...\<OrgName>\<AppName>
        QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        if (baseDir.isEmpty())
            baseDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

        QDir dir(baseDir);

        // Just add a single "Layouts" subfolder
        const QString layoutsDirName = QStringLiteral("Layouts");
        if (!dir.exists(layoutsDirName))
            dir.mkdir(layoutsDirName);
        dir.cd(layoutsDirName);

        return dir.absolutePath();
    }

    QString MainWindow::defaultLayoutFilePath() const
    {
        return layoutConfigDirectory() + QStringLiteral("/RayPowerMeterLayout.json");
    }

    QString MainWindow::promptForNewGroupTitle(const QString &defaultTitle)
    {
        bool ok = false;
        const QString title = QInputDialog::getText(this,
                                                    tr("New Group"),
                                                    tr("Group title:"),
                                                    QLineEdit::Normal,
                                                    defaultTitle,
                                                    &ok);

        if (!ok || title.trimmed().isEmpty())
            return QString();

        return title.trimmed();
    }

    void MainWindow::populateSourceCombo()
    {
        if (!m_layoutManager || !m_propSourceCombo)
        {
            return;
        }

        m_propSourceCombo->blockSignals(true);
        m_propSourceCombo->clear();

        // Entry 0: group default
        m_propSourceCombo->addItem(tr("Use group default"), QVariant()); // empty data

        // RF couplers (0..MAX_COUPLERS-1)
        for (int i = 0; i < MAX_COUPLERS; ++i)
        {
            const QString key = QStringLiteral("Coupler%1").arg(i);
            QString label = tr("RF Coupler %1").arg(i + 1);

            // If we have a user-friendly name from m_couplerConfigs, append it
            auto cfgIt = m_couplerConfigs.constFind(i);
            if (cfgIt != m_couplerConfigs.constEnd() && !cfgIt->name.isEmpty())
            {
                label += QStringLiteral(" (%1)").arg(cfgIt->name);
            }

            m_propSourceCombo->addItem(label, key);
        }

        // AUX channels – for now, expose a fixed set based on MAX_AUX_CHANNELS.
        // Internal keys are "Aux0", "Aux1", ...; labels are "Voltage Aux 1", etc.
        // Later, when we have per-AUX type info (voltage/current/temperature)
        // coming through RuntimeDataModel, we can make these labels smarter.
        for (int i = 0; i < MAX_AUX_CHANNELS; ++i)
        {
            const QString key = QStringLiteral("Aux%1").arg(i);
            const QString label = tr("Aux %1").arg(i + 1);
            m_propSourceCombo->addItem(label, key);
        }

        // Aux Digital channels – map to firmware names "AuxD1".."AuxD4".
        for (int i = 0; i < MAX_AUX_DIGITAL_CHANNELS; ++i)
        {
            const int oneBased = i + 1;
            const QString key = QStringLiteral("AuxD%1").arg(oneBased); // data
            const QString label = tr("Aux Digital %1").arg(oneBased);   // visible text
            m_propSourceCombo->addItem(label, key);
        }

        // Select the current value if present
        QString currentSource;
        if (!m_selectedWidgetId.isEmpty() && m_layoutManager->widgetSource(m_selectedWidgetId, currentSource))
        {
            int idxToSelect = 0; // default (group default)

            if (!currentSource.isEmpty())
            {
                for (int i = 0; i < m_propSourceCombo->count(); ++i)
                {
                    if (m_propSourceCombo->itemData(i).toString() == currentSource)
                    {
                        idxToSelect = i;
                        break;
                    }
                }
            }

            m_propSourceCombo->setCurrentIndex(idxToSelect);
        }
        else
        {
            m_propSourceCombo->setCurrentIndex(0);
        }

        m_propSourceCombo->blockSignals(false);
    }

    QString MainWindow::tripTypeToSummary(quint8 type) const
    {
        // RayTripType mapping from firmware:
        //   0 = None
        //   1 = HighForwardPower
        //   2 = HighReflectedPower
        //   3 = HighSWR
        //   4 = ExternalInterrupt1
        //   5 = ExternalInterrupt2
        switch (type)
        {
        case 0:
            return tr("No trip");
        case 1:
            return tr("High forward power");
        case 2:
            return tr("High reflected power");
        case 3:
            return tr("High SWR");
        case 4:
            return tr("External interrupt 1");
        case 5:
            return tr("External interrupt 2");
        default:
            return tr("Protection trip (type %1)").arg(type);
        }
    }

    QString MainWindow::tripSourceToString(quint8 source) const
    {
        // RayTripSource mapping from firmware:
        //   0 = None
        //   1 = Coupler1
        //   2 = Coupler2
        //   3 = Coupler3
        //   4 = Coupler4
        //   5 = Interrupt1
        //   6 = Interrupt2
        switch (source)
        {
        case 0:
            return tr("None");
        case 1:
            return tr("Coupler 1");
        case 2:
            return tr("Coupler 2");
        case 3:
            return tr("Coupler 3");
        case 4:
            return tr("Coupler 4");
        case 5:
            return tr("External interrupt 1");
        case 6:
            return tr("External interrupt 2");
        default:
            return tr("Source %1").arg(source);
        }
    }

    QString MainWindow::tripDetailLine(const raypm::RayTripInfo &trip) const
    {
        if (!trip.tripped)
        {
            return tr("No detailed trip information available.");
        }

        const QString typeText = tripTypeToSummary(trip.type);
        const QString sourceText = tripSourceToString(trip.source);

        return tr("%1\nSource: %2\nValue: %3 (threshold %4)")
            .arg(typeText)
            .arg(sourceText)
            .arg(trip.value, 0, 'f', 2)
            .arg(trip.threshold, 0, 'f', 2);
    }

    void MainWindow::onPropertyGainAttnDbChanged(double value)
    {
        if (!canEditWidgetProperties() || !m_layoutManager || m_selectedWidgetId.isEmpty())
            return;

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);
        if (IWidgetPropertyAdapter *adapter = adapterForWidgetType(type))
        {
            const WidgetPropertyContext ctx =
                makeWidgetPropertyContext(this, m_layoutManager, m_selectedGroupId, m_selectedWidgetId);

            adapter->handlePropertyChange(ctx, QStringLiteral("gain.attnDb"), QVariant(value));
            return;
        }

        m_layoutManager->setWidgetGainAttnDb(m_selectedWidgetId, value);
    }

    void MainWindow::on_actionExit_triggered()
    {
        // Close the app
        close();
    }

    void MainWindow::on_actionTelemetryHistory_triggered()
    {
        if (!m_teleHistDock)
        {
            m_teleHistDock = new QDockWidget(tr("Telemetry History"), this);
            m_teleHistDock->setObjectName(QStringLiteral("TelemetryHistoryDock"));
            m_teleHistDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

            auto *panel = new QWidget(m_teleHistDock);
            auto *v = new QVBoxLayout(panel);

            // --- simple controls rows (2-row layout) ---
            auto *row1 = new QHBoxLayout();
            auto *row2 = new QHBoxLayout();

            auto *btnStartStop = new QPushButton(tr("Start"), panel);
            auto *btnPause = new QPushButton(tr("Pause"), panel);
            btnPause->setEnabled(false); // only enabled once started
            auto *btnClear = new QPushButton(tr("Clear"), panel);
            auto *btnExport = new QPushButton(tr("Export as CSV"), panel);

            auto *spInterval = new QSpinBox(panel);
            spInterval->setRange(50, 5000);
            spInterval->setSingleStep(50);
            spInterval->setValue(250);
            spInterval->setSuffix(tr(" ms"));

            m_teleHistCouplerCombo = new QComboBox(panel);
            m_teleHistCouplerCombo->clear();
            m_teleHistCouplerCombo->addItem(tr("N/A"), -1);
            m_teleHistCouplerCombo->addItem(tr("Coupler 1"), 0);
            m_teleHistCouplerCombo->addItem(tr("Coupler 2"), 1);
            m_teleHistCouplerCombo->addItem(tr("Coupler 3"), 2);
            m_teleHistCouplerCombo->addItem(tr("Coupler 4"), 3);

            m_teleHistSignalCombo = new QComboBox(panel);
            m_teleHistSignalCombo->clear();
            m_teleHistSignalCombo->addItem(tr("Forward Watts"), 0);
            m_teleHistSignalCombo->addItem(tr("Reflected Watts"), 1);
            m_teleHistSignalCombo->addItem(tr("SWR"), 2);
            m_teleHistSignalCombo->addItem(tr("Raw Fwd"), 3);
            m_teleHistSignalCombo->addItem(tr("Raw Ref"), 4);
            m_teleHistSignalCombo->addItem(tr("Aux Voltage"), 5);
            m_teleHistSignalCombo->addItem(tr("Aux Current"), 6);
            m_teleHistSignalCombo->addItem(tr("Temperature (C)"), 7);

            m_teleHistAuxCombo = new QComboBox(panel);
            m_teleHistAuxCombo->clear();
            m_teleHistAuxCombo->addItem(tr("N/A"), -1);
            m_teleHistAuxCombo->addItem(tr("Aux 1"), 0);
            m_teleHistAuxCombo->addItem(tr("Aux 2"), 1);
            m_teleHistAuxCombo->addItem(tr("Aux 3"), 2);
            m_teleHistAuxCombo->addItem(tr("Aux 4"), 3);

            m_teleHistMaxRowsSpin = new QSpinBox(panel);
            m_teleHistMaxRowsSpin->setRange(50, 20000);
            m_teleHistMaxRowsSpin->setSingleStep(50);
            m_teleHistMaxRowsSpin->setValue(1000);

            auto updateSourceEnablement = [this]()
            {
                if (!m_teleHistSignalCombo)
                    return;

                const int sig = m_teleHistSignalCombo->currentData().toInt();

                const bool isCouplerSignal = (sig >= 0 && sig <= 4); // Forward..Raw Ref
                const bool isAuxSignal = (sig >= 5);                 // Aux / Temp

                // ---- Coupler selector ----
                if (m_teleHistCouplerCombo)
                {
                    const bool wasEnabled = m_teleHistCouplerCombo->isEnabled();
                    m_teleHistCouplerCombo->setEnabled(isCouplerSignal);

                    if (!isCouplerSignal)
                    {
                        // Leaving coupler mode → force N/A
                        if (m_teleHistCouplerCombo->currentIndex() != 0)
                            m_teleHistCouplerCombo->setCurrentIndex(0);
                    }
                    else if (!wasEnabled)
                    {
                        // Entering coupler mode → if N/A, default to Coupler 1
                        if (m_teleHistCouplerCombo->currentIndex() == 0 && m_teleHistCouplerCombo->count() > 1)
                            m_teleHistCouplerCombo->setCurrentIndex(1);
                    }
                }

                // ---- Aux selector ----
                if (m_teleHistAuxCombo)
                {
                    const bool wasEnabled = m_teleHistAuxCombo->isEnabled();
                    m_teleHistAuxCombo->setEnabled(isAuxSignal);

                    if (!isAuxSignal)
                    {
                        // Leaving aux mode → force N/A
                        if (m_teleHistAuxCombo->currentIndex() != 0)
                            m_teleHistAuxCombo->setCurrentIndex(0);
                    }
                    else if (!wasEnabled)
                    {
                        // Entering aux mode → if N/A, default to Aux 1
                        if (m_teleHistAuxCombo->currentIndex() == 0 && m_teleHistAuxCombo->count() > 1)
                            m_teleHistAuxCombo->setCurrentIndex(1);
                    }
                }
            };

            connect(m_teleHistSignalCombo,
                    qOverload<int>(&QComboBox::currentIndexChanged),
                    this,
                    [updateSourceEnablement]()
                    { updateSourceEnablement(); });

            // initialize state
            updateSourceEnablement();

            // Row 1: Start/Pause + Interval + Max + right-side buttons
            row1->addWidget(btnStartStop);
            row1->addSpacing(6);
            row1->addWidget(btnPause);

            row1->addSpacing(10);
            row1->addWidget(new QLabel(tr("Sample:"), panel));
            row1->addWidget(spInterval);

            row1->addSpacing(10);
            row1->addWidget(new QLabel(tr("Max:"), panel));
            row1->addWidget(m_teleHistMaxRowsSpin);
            row1->addSpacing(10);
            row1->addWidget(btnClear);

            row1->addStretch(1);
            row1->addWidget(btnExport);

            // Row 2: Source selectors (Coupler/Signal/Aux)
            row2->addWidget(new QLabel(tr("Coupler:"), panel));
            row2->addWidget(m_teleHistCouplerCombo);

            row2->addSpacing(16);
            row2->addWidget(new QLabel(tr("Signal:"), panel));
            row2->addWidget(m_teleHistSignalCombo);

            row2->addSpacing(16);
            row2->addWidget(new QLabel(tr("Aux:"), panel));
            row2->addWidget(m_teleHistAuxCombo);

            row2->addStretch(1);

            v->addLayout(row1);

            connect(m_modChkMeasurements, &QCheckBox::toggled, this, [this](bool on)
                    { applyScopeMeasurementsOverlay(on); });

            connect(m_scopeChkMeasurements, &QCheckBox::toggled, this, [this](bool on)
                    { applyScopeMeasurementsOverlay(on); });
            v->addLayout(row2);

            // --- table ---
            m_teleHistTable = new QTableWidget(panel);
            m_teleHistTable->setColumnCount(4);
            m_teleHistTable->setHorizontalHeaderLabels(
                {tr("Time"), tr("Source"), tr("Signal"), tr("Value")});
            m_teleHistTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
            m_teleHistTable->setSelectionBehavior(QAbstractItemView::SelectRows);
            m_teleHistTable->setSelectionMode(QAbstractItemView::SingleSelection);
            m_teleHistTable->horizontalHeader()->setStretchLastSection(false);

            // Keep "Signal" stretchy, keep "Value" reasonable
            m_teleHistTable->horizontalHeader()
                ->setSectionResizeMode(0, QHeaderView::ResizeToContents); // Time
            m_teleHistTable->horizontalHeader()
                ->setSectionResizeMode(1, QHeaderView::ResizeToContents);                       // Source
            m_teleHistTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch); // Signal
            m_teleHistTable->horizontalHeader()
                ->setSectionResizeMode(3, QHeaderView::ResizeToContents); // Value

            m_teleHistTable->setColumnWidth(3, 110); // small sanity cap

            v->addWidget(m_teleHistTable, 1);
            panel->setLayout(v);

            m_teleHistDock->setWidget(panel);
            addDockWidget(Qt::RightDockWidgetArea, m_teleHistDock);

            // timer “clock event”
            m_teleHistTimer.setInterval(spInterval->value());
            connect(&m_teleHistTimer, &QTimer::timeout, this, [this]()
                    {
            if (!m_runtimeModel || !m_teleHistTable || !m_teleHistSignalCombo)
                return;

            if (!m_teleHistRunning || m_teleHistPaused)
                return;

            const RayTelemetry t = m_runtimeModel->telemetry();

            const int sig = m_teleHistSignalCombo ? m_teleHistSignalCombo->currentData().toInt()
                                                  : 0;
            const QString sigName = m_teleHistSignalCombo ? m_teleHistSignalCombo->currentText()
                                                          : QString();

            const bool isCouplerSignal = (sig >= 0 && sig <= 4); // Forward..Raw Ref
            const bool isAuxSignal = (sig >= 5);                 // Aux / Temp

            double value = 0.0;
            QString sourceText = tr("N/A");

            // ---------- COUPLER MODE ----------
            if (isCouplerSignal) {
                const int selCoupler = m_teleHistCouplerCombo
                                           ? m_teleHistCouplerCombo->currentData().toInt()
                                           : -1;
                if (selCoupler < 0)
                    return;

                sourceText = tr("Coupler %1").arg(selCoupler + 1);

                // Prefer multi-coupler samples when present
                const RayCouplerSample *cs = nullptr;
                for (const auto &c : t.couplers) {
                    if (c.index == selCoupler) {
                        cs = &c;
                        break;
                    }
                }

                if (cs) {
                    switch (sig) {
                    case 0:
                        value = cs->forwardWatts;
                        break;
                    case 1:
                        value = cs->reflectedWatts;
                        break;
                    case 2:
                        value = cs->swr;
                        break;

                    // Your UI says "Raw", but multi-coupler gives volts per coupler.
                    // Use forwardVolts/reflectedVolts here so Coupler 2–4 work.
                    case 3:
                        value = cs->forwardVolts;
                        break; // "Raw Fwd" -> fwd_v
                    case 4:
                        value = cs->reflectedVolts;
                        break; // "Raw Ref" -> ref_v
                    default:
                        value = 0.0;
                        break;
                    }
                } else {
                    // Fallback to legacy fields (single active coupler)
                    // Only log if the legacy coupler matches selection.
                    if (t.couplerIndex != selCoupler)
                        return;

                    switch (sig) {
                    case 0:
                        value = t.forwardWatts;
                        break;
                    case 1:
                        value = t.reflectedWatts;
                        break;
                    case 2:
                        value = t.swrLegacy;
                        break;
                    case 3:
                        value = t.rawFwd;
                        break;
                    case 4:
                        value = t.rawRef;
                        break;
                    default:
                        value = 0.0;
                        break;
                    }
                }
            }

            // ---------- AUX MODE ----------
            else if (isAuxSignal) {
                const int selAux = m_teleHistAuxCombo ? m_teleHistAuxCombo->currentData().toInt()
                                                      : -1;
                if (selAux < 0)
                    return;

                sourceText = tr("Aux %1").arg(selAux + 1);

                RayAuxType wantedType = RayAuxType::None;
                if (sig == 5)
                    wantedType = RayAuxType::Voltage;
                else if (sig == 6)
                    wantedType = RayAuxType::Current;
                else if (sig == 7)
                    wantedType = RayAuxType::Temperature;

                // Prefer auxChannels when present
                const RayAuxSample *ax = nullptr;
                for (const auto &a : t.auxChannels) {
                    if (a.index == selAux && a.type == wantedType) {
                        ax = &a;
                        break;
                    }
                }

                if (ax) {
                    value = ax->value;
                } else {
                    // Fallback to legacy fields (single aux)
                    if (t.auxIndex != selAux)
                        return;

                    switch (sig) {
                    case 5:
                        value = t.voltage;
                        break;
                    case 6:
                        value = t.current;
                        break;
                    case 7:
                        value = t.temperatureC;
                        break;
                    default:
                        value = 0.0;
                        break;
                    }
                }
            } else {
                return;
            }

            // ---- Insert row ----
            const int r = m_teleHistTable->rowCount();
            m_teleHistTable->insertRow(r);

            m_teleHistTable->setItem(r,
                                     0,
                                     new QTableWidgetItem(
                                         QTime::currentTime().toString("HH:mm:ss.zzz")));
            m_teleHistTable->setItem(r, 1, new QTableWidgetItem(sourceText));
            m_teleHistTable->setItem(r, 2, new QTableWidgetItem(sigName));
            m_teleHistTable->setItem(r, 3, new QTableWidgetItem(QString::number(value, 'f', 3)));

            // Ring buffer trim (keep your existing)
            const int maxRows = m_teleHistMaxRowsSpin ? m_teleHistMaxRowsSpin->value() : 1000;
            while (m_teleHistTable->rowCount() > maxRows) {
                m_teleHistTable->removeRow(0);
            }

            m_teleHistTable->scrollToBottom(); });

            connect(spInterval, qOverload<int>(&QSpinBox::valueChanged), this, [this](int ms)
                    {
            m_teleHistTimer.setInterval(ms);
            if (statusBar())
                statusBar()->showMessage(tr("Telemetry History sample interval: %1 ms").arg(ms),
                                         2000); });

            connect(btnStartStop, &QPushButton::clicked, this, [this, btnStartStop, btnPause]()
                    {
            m_teleHistRunning = !m_teleHistRunning;

            if (m_teleHistRunning) {
                m_teleHistPaused = false;
                btnPause->setEnabled(true);
                btnPause->setText(tr("Pause"));

                m_teleHistTimer.start();
                btnStartStop->setText(tr("Stop"));

                if (statusBar())
                    statusBar()->showMessage(tr("Telemetry History started"), 2000);
            } else {
                m_teleHistTimer.stop();
                btnStartStop->setText(tr("Start"));

                m_teleHistPaused = false;
                btnPause->setEnabled(false);
                btnPause->setText(tr("Pause"));

                if (statusBar())
                    statusBar()->showMessage(tr("Telemetry History stopped"), 2000);
            } });

            connect(btnPause, &QPushButton::clicked, this, [this, btnPause]()
                    {
            if (!m_teleHistRunning)
                return;

            m_teleHistPaused = !m_teleHistPaused;
            btnPause->setText(m_teleHistPaused ? tr("Resume") : tr("Pause"));

            if (statusBar()) {
                statusBar()->showMessage(m_teleHistPaused ? tr("Telemetry History paused")
                                                          : tr("Telemetry History resumed"),
                                         2000);
            } });

            connect(btnClear, &QPushButton::clicked, this, [this]()
                    {
            if (!m_teleHistTable)
                return;
            m_teleHistTable->setRowCount(0);
            if (statusBar())
                statusBar()->showMessage(tr("Telemetry History cleared"), 2000); });

            connect(btnExport, &QPushButton::clicked, this, [this]()
                    {
            if (!m_teleHistTable || m_teleHistTable->rowCount() == 0) {
                if (statusBar())
                    statusBar()->showMessage(tr("Telemetry History: nothing to export"), 2000);
                return;
            }

            const QString defaultDir = m_lastTeleHistExportDir.isEmpty()
                                           ? QStandardPaths::writableLocation(
                                                 QStandardPaths::DocumentsLocation)
                                           : m_lastTeleHistExportDir;

            const QString defaultName = QString("TelemetryHistory_%1.csv")
                                            .arg(QDateTime::currentDateTime().toString(
                                                "yyyyMMdd_HHmmss"));

            const QString filePath
                = QFileDialog::getSaveFileName(this,
                                               tr("Export Telemetry History"),
                                               QDir(defaultDir).filePath(defaultName),
                                               tr("CSV (*.csv)"));

            if (filePath.isEmpty())
                return;

            QFile f(filePath);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                if (statusBar())
                    statusBar()->showMessage(tr("Export failed: can't open file"), 3000);
                return;
            }

            m_lastTeleHistExportDir = QFileInfo(filePath).absolutePath();

            QTextStream out(&f);

            // Header row
            out << "Time,Source,Signal,Value\n";

            // Data rows
            for (int r = 0; r < m_teleHistTable->rowCount(); ++r) {
                auto cell = [this, r](int c) -> QString {
                    QTableWidgetItem *it = m_teleHistTable->item(r, c);
                    QString s = it ? it->text() : QString();
                    // CSV escaping: wrap in quotes if needed, escape internal quotes
                    if (s.contains('"'))
                        s.replace("\"", "\"\"");
                    if (s.contains(',') || s.contains('"') || s.contains('\n'))
                        s = "\"" + s + "\"";
                    return s;
                };

                out << cell(0) << "," << cell(1) << "," << cell(2) << "," << cell(3) << "\n";
            }

            f.close();

            if (statusBar())
                statusBar()->showMessage(tr("Telemetry History exported: %1")
                                             .arg(QFileInfo(filePath).fileName()),
                                         3000); });
        }

        m_teleHistDock->setVisible(!m_teleHistDock->isVisible());
        if (m_teleHistDock->isVisible())
            m_teleHistDock->raise();
    }

    void MainWindow::on_actionScopeTelemetry_triggered() {}

    void MainWindow::on_actionScopeModulation_triggered() {}

    // -----------------------------------------------------------------------------
    // Scope Telemetry settings helpers
    // -----------------------------------------------------------------------------
    double raypm::MainWindow::scopeCouplerMaxW() const
    {
        constexpr double kDefaultMaxW = 100.0;

        int couplerIdx = 0;

        // We reuse m_scopeTelemetrySourceCombo as the COUPLER selector.
        // Fall back to m_scopeTelemetryCouplerCombo if it ever exists in older layouts.
        QComboBox *cb = m_scopeTelemetrySourceCombo ? m_scopeTelemetrySourceCombo
                                                    : m_scopeTelemetryCouplerCombo;

        if (cb)
        {
            const int v = cb->currentData().toInt();
            if (v >= 0)
                couplerIdx = v;
        }

        const auto it = m_couplerConfigs.constFind(couplerIdx);
        if (it == m_couplerConfigs.cend())
            return kDefaultMaxW;

        const auto &cfg = it.value();

        if (cfg.fwdBarRangeW > 0.0)
            return cfg.fwdBarRangeW;
        if (cfg.fullScaleW > 0.0)
            return cfg.fullScaleW;

        return kDefaultMaxW;
    }

    void raypm::MainWindow::setMiniScopePropertyRowsVisible(bool visible)
    {
        if (!m_propFormLayout)
            return;

        auto toggleRow = [this, visible](QWidget *field)
        {
            if (!field)
                return;
            if (QWidget *lbl = m_propFormLayout->labelForField(field))
                lbl->setVisible(visible);
            field->setVisible(visible);
        };

        toggleRow(m_propScopeSignalCombo);
        toggleRow(m_propScopeWindowSpin);
        toggleRow(m_propScopeAutoScaleCheck);
    }

    void raypm::MainWindow::applyScopeTelemetrySettings()
    {
        const bool autoScale = m_scopeChkAutoScale ? m_scopeChkAutoScale->isChecked() : true;
        const double winS = m_scopeComboWindow ? m_scopeComboWindow->currentData().toDouble() : 10.0;
        const int maxPts = m_scopeSpinMaxPts ? m_scopeSpinMaxPts->value() : 2000;

        auto applyCommon = [&](ScopePlotWidget *w)
        {
            if (!w)
                return;
            w->setTimeWindowSeconds(winS);
            w->setMaxSamples(maxPts);
            w->setAutoScale(autoScale);
        };

        applyCommon(m_scopePlotForwardW);
        applyCommon(m_scopePlotReflectedW);
        applyCommon(m_scopePlotSwr);
        applyCommon(m_scopePlotAuxVolt);
        applyCommon(m_scopePlotAuxCurr);
        applyCommon(m_scopePlotTempC);

        // When Auto-scale is OFF, force fixed ranges for power plots
        // (otherwise the widget will still “track” min/max as new data arrives).
        if (!autoScale)
        {
            // Forward max comes from selected coupler's fwd_bar_range_w (or fullScaleW fallback)
            const double fwdMax = std::max(1.0, scopeCouplerMaxW());

            // Reflected max: use selected coupler config if available, else 20% of forward
            double refMax = fwdMax * 0.20;

            QComboBox *cb = m_scopeTelemetrySourceCombo ? m_scopeTelemetrySourceCombo
                                                        : m_scopeTelemetryCouplerCombo;

            if (cb)
            {
                const int idx = cb->currentData().toInt();
                if (idx >= 0)
                {
                    const auto it = m_couplerConfigs.constFind(idx);
                    if (it != m_couplerConfigs.cend() && it.value().refBarRangeW > 0.0)
                        refMax = it.value().refBarRangeW;
                }
            }

            refMax = std::max(1.0, refMax);

            if (m_scopePlotForwardW)
            {
                m_scopePlotForwardW->setFixedYRange(0.0, fwdMax);
                m_scopePlotForwardW->setUseFixedYRange(true);
            }
            if (m_scopePlotReflectedW)
            {
                m_scopePlotReflectedW->setFixedYRange(0.0, refMax);
                m_scopePlotReflectedW->setUseFixedYRange(true);
            }

            if (m_scopePlotSwr)
            {
                m_scopePlotSwr->setFixedYRange(1.0, m_scopePlotSwr->currentYMax());
                m_scopePlotSwr->setUseFixedYRange(true);
            }

            if (m_scopePlotAuxVolt)
            {
                m_scopePlotAuxVolt->setFixedYRange(0.0, m_scopePlotAuxVolt->currentYMax());
                m_scopePlotAuxVolt->setUseFixedYRange(true);
            }

            if (m_scopePlotAuxCurr)
            {
                m_scopePlotAuxCurr->setFixedYRange(0.0, m_scopePlotAuxCurr->currentYMax());
                m_scopePlotAuxCurr->setUseFixedYRange(true);
            }

            // Temperature always plots in auto scale mode for now, TODO: consider fixed range option
            if (m_scopePlotTempC)
                m_scopePlotTempC->setUseFixedYRange(false);
        }
        else
        {
            // Auto-scale ON => ensure fixed range is off
            if (m_scopePlotForwardW)
                m_scopePlotForwardW->setUseFixedYRange(false);
            if (m_scopePlotReflectedW)
                m_scopePlotReflectedW->setUseFixedYRange(false);
            if (m_scopePlotSwr)
                m_scopePlotSwr->setUseFixedYRange(false);
            if (m_scopePlotAuxVolt)
                m_scopePlotAuxVolt->setUseFixedYRange(false);
            if (m_scopePlotAuxCurr)
                m_scopePlotAuxCurr->setUseFixedYRange(false);
            if (m_scopePlotTempC)
                m_scopePlotTempC->setUseFixedYRange(false);
        }

        // repaint
        if (m_scopePlotForwardW)
            m_scopePlotForwardW->update();
        if (m_scopePlotReflectedW)
            m_scopePlotReflectedW->update();
        if (m_scopePlotSwr)
            m_scopePlotSwr->update();
        if (m_scopePlotAuxVolt)
            m_scopePlotAuxVolt->update();
        if (m_scopePlotAuxCurr)
            m_scopePlotAuxCurr->update();
        if (m_scopePlotTempC)
            m_scopePlotTempC->update();
    }

    void MainWindow::on_actionScopeTelemetry_triggered(bool checked)
    {
        // User is closing it via menu
        if (!checked)
        {
            if (m_scopeTelemetryDock)
            {
                // This will emit visibilityChanged(false),
                // and that handler will perform all cleanup + deleteLater().
                m_scopeTelemetryDock->close();
            }
            return;
        }

        // Lazy-create on first open
        if (!m_scopeTelemetryDock)
        {
            m_scopeTelemetryDock = new QDockWidget(tr("Scopes - Telemetry"), this);
            m_scopeTelemetryDock->setObjectName(QStringLiteral("ScopesTelemetryDock"));
            m_scopeTelemetryDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

            auto *panel = new QWidget(m_scopeTelemetryDock);
            auto *v = new QVBoxLayout(panel);
            v->setContentsMargins(6, 6, 6, 6);
            v->setSpacing(6);

            // ----------------------------
            // Top controls row #1 (buttons + autoscale)
            // ----------------------------
            auto *row1 = new QHBoxLayout();
            row1->setContentsMargins(0, 0, 0, 0);
            row1->setSpacing(6);

            m_scopeBtnStartStop = new QPushButton(tr("Start"), panel);
            m_scopeBtnPause = new QPushButton(tr("Pause"), panel);
            m_scopeBtnPause->setEnabled(false);
            m_scopeBtnClear = new QPushButton(tr("Clear"), panel);

            m_scopeChkAutoScale = new QCheckBox(tr("Auto-scale"), panel);
            m_scopeChkAutoScale->setChecked(true);
            m_scopeChkMeasurements = new QCheckBox(tr("Measures"), panel);
            m_scopeChkMeasurements->setChecked(m_showScopeMeasurements);
            m_scopeBtnExport = new QPushButton(tr("Export as PNG"), panel);

            row1->addWidget(m_scopeBtnStartStop);
            row1->addWidget(m_scopeBtnPause);
            row1->addWidget(m_scopeBtnClear);
            row1->addSpacing(14);
            row1->addWidget(m_scopeChkAutoScale);
            row1->addWidget(m_scopeChkMeasurements);
            row1->addStretch(1);
            row1->addWidget(m_scopeBtnExport);

            v->addLayout(row1);

            connect(m_modChkMeasurements, &QCheckBox::toggled, this, [this](bool on)
                    { applyScopeMeasurementsOverlay(on); });

            connect(m_scopeChkMeasurements, &QCheckBox::toggled, this, [this](bool on)
                    { applyScopeMeasurementsOverlay(on); });

            // ----------------------------
            // Top controls row #2 (Coupler/Aux + Window + Max)
            // ----------------------------
            auto *row2 = new QHBoxLayout();
            row2->setContentsMargins(0, 0, 0, 0);
            row2->setSpacing(6);

            // Reuse m_scopeTelemetrySourceCombo as the COUPLER selector (to avoid renaming churn)
            m_scopeTelemetrySourceCombo = new QComboBox(panel);
            m_scopeTelemetrySourceCombo->clear();
            m_scopeTelemetrySourceCombo->addItem(tr("N/A"), -1);
            m_scopeTelemetrySourceCombo->addItem(tr("Coupler 1"), 0);
            m_scopeTelemetrySourceCombo->addItem(tr("Coupler 2"), 1);
            m_scopeTelemetrySourceCombo->addItem(tr("Coupler 3"), 2);
            m_scopeTelemetrySourceCombo->addItem(tr("Coupler 4"), 3);
            m_scopeTelemetrySourceCombo->setCurrentIndex(1); // Coupler 1 by default

            m_scopeTelemetryAuxCombo = new QComboBox(panel);
            m_scopeTelemetryAuxCombo->clear();
            m_scopeTelemetryAuxCombo->addItem(tr("N/A"), -1);
            m_scopeTelemetryAuxCombo->addItem(tr("Aux 1"), 0);
            m_scopeTelemetryAuxCombo->addItem(tr("Aux 2"), 1);
            m_scopeTelemetryAuxCombo->addItem(tr("Aux 3"), 2);
            m_scopeTelemetryAuxCombo->addItem(tr("Aux 4"), 3);
            m_scopeTelemetryAuxCombo->setCurrentIndex(1); // Aux 1 by default

            m_scopeComboWindow = new QComboBox(panel);
            m_scopeComboWindow->addItem(tr("2 s"), 2.0);
            m_scopeComboWindow->addItem(tr("5 s"), 5.0);
            m_scopeComboWindow->addItem(tr("10 s"), 10.0);
            m_scopeComboWindow->addItem(tr("20 s"), 20.0);
            m_scopeComboWindow->setCurrentIndex(2); // 10 s default

            m_scopeTelemetrySmoothingCombo = new QComboBox(panel);
            m_scopeTelemetrySmoothingCombo->addItem(tr("None"), QStringLiteral("none"));
            m_scopeTelemetrySmoothingCombo->addItem(tr("SMA (3)"), QStringLiteral("sma:3"));
            m_scopeTelemetrySmoothingCombo->addItem(tr("SMA (5)"), QStringLiteral("sma:5"));
            m_scopeTelemetrySmoothingCombo->addItem(tr("SMA (9)"), QStringLiteral("sma:9"));
            m_scopeTelemetrySmoothingCombo->addItem(tr("EMA (Fast)"), QStringLiteral("ema:0.50"));
            m_scopeTelemetrySmoothingCombo->addItem(tr("EMA (Medium)"), QStringLiteral("ema:0.33"));
            m_scopeTelemetrySmoothingCombo->addItem(tr("EMA (Slow)"), QStringLiteral("ema:0.15"));
            m_scopeTelemetrySmoothingCombo->setCurrentIndex(
                m_scopeTelemetrySmoothingCombo->findData(QStringLiteral("ema:0.33")));

            m_scopeSpinMaxPts = new QSpinBox(panel);
            m_scopeSpinMaxPts->setRange(200, 20000);
            m_scopeSpinMaxPts->setSingleStep(200);
            m_scopeSpinMaxPts->setValue(2000);

            row2->addWidget(new QLabel(tr("Coupler:"), panel));
            row2->addWidget(m_scopeTelemetrySourceCombo);
            row2->addSpacing(10);

            row2->addWidget(new QLabel(tr("Aux:"), panel));
            row2->addWidget(m_scopeTelemetryAuxCombo);
            row2->addSpacing(10);

            row2->addWidget(new QLabel(tr("Window:"), panel));
            row2->addWidget(m_scopeComboWindow);
            row2->addSpacing(10);

            row2->addWidget(new QLabel(tr("Smoothing:"), panel));
            row2->addWidget(m_scopeTelemetrySmoothingCombo);
            row2->addSpacing(10);

            row2->addWidget(new QLabel(tr("Max:"), panel));
            row2->addWidget(m_scopeSpinMaxPts);
            row2->addStretch(1);

            v->addLayout(row2);

            // ----------------------------
            // Tabs (plots)
            // ----------------------------
            m_scopeTelemetryTabs = new QTabWidget(panel);

            auto makePlotTab = [panel](ScopePlotWidget *&outPlot,
                                       const QString &title,
                                       const QString &yLabel) -> QWidget *
            {
                auto *w = new QWidget(panel);
                auto *l = new QVBoxLayout(w);
                l->setContentsMargins(0, 0, 0, 0);
                l->setSpacing(0);

                outPlot = new ScopePlotWidget(w);
                outPlot->setTitle(title);
                outPlot->setScopeType(ScopePlotWidget::ScopeType::Telemetry);

                // Defaults; actual settings applied by applyScopeTelemetrySettings()
                outPlot->setAutoScale(true);
                outPlot->setMaxSamples(2000);
                outPlot->setTimeWindowSeconds(10.0);

                Q_UNUSED(yLabel);

                l->addWidget(outPlot, 1);
                w->setLayout(l);
                return w;
            };

            QWidget *tabFwd = makePlotTab(m_scopePlotForwardW, tr("Forward Power"), tr("W"));
            QWidget *tabRef = makePlotTab(m_scopePlotReflectedW, tr("Reflected Power"), tr("W"));
            QWidget *tabSwr = makePlotTab(m_scopePlotSwr, tr("SWR"), QString());
            QWidget *tabAuxV = makePlotTab(m_scopePlotAuxVolt, tr("Aux Voltage"), tr("V"));
            QWidget *tabAuxI = makePlotTab(m_scopePlotAuxCurr, tr("Aux Current"), tr("A"));
            QWidget *tabTemp = makePlotTab(m_scopePlotTempC, tr("Temperature"), tr("°C"));

            // Clamp some plots to 0 minimum:
            if (m_scopePlotForwardW)
                m_scopePlotForwardW->setClampMinZero(true);
            if (m_scopePlotReflectedW)
                m_scopePlotReflectedW->setClampMinZero(true);
            if (m_scopePlotAuxVolt)
                m_scopePlotAuxVolt->setClampMinZero(true);
            if (m_scopePlotAuxCurr)
                m_scopePlotAuxCurr->setClampMinZero(true);

            // SWR / Temp: (no clamping to 0)
            if (m_scopePlotSwr)
                m_scopePlotSwr->setClampMinZero(false);
            if (m_scopePlotTempC)
                m_scopePlotTempC->setClampMinZero(false);

            m_scopeTelemetryTabs->addTab(tabFwd, tr("Forward"));
            m_scopeTelemetryTabs->addTab(tabRef, tr("Reflected"));
            m_scopeTelemetryTabs->addTab(tabSwr, tr("SWR"));
            m_scopeTelemetryTabs->addTab(tabAuxV, tr("Aux V"));
            m_scopeTelemetryTabs->addTab(tabAuxI, tr("Aux I"));
            m_scopeTelemetryTabs->addTab(tabTemp, tr("Temp"));

            v->addWidget(m_scopeTelemetryTabs, 1);

            panel->setLayout(v);
            m_scopeTelemetryDock->setWidget(panel);
            addDockWidget(Qt::RightDockWidgetArea, m_scopeTelemetryDock);

            // ----------------------------
            // Smart enable/disable like Telemetry History
            // ----------------------------
            auto updateSelectorEnablement = [this]()
            {
                if (!m_scopeTelemetryTabs)
                    return;

                const int tab = m_scopeTelemetryTabs->currentIndex();
                const bool isCouplerTab = (tab == 0 || tab == 1 || tab == 2); // Fwd/Ref/SWR
                const bool isAuxTab = (tab == 3 || tab == 4 || tab == 5);     // AuxV/AuxI/Temp

                if (m_scopeTelemetrySourceCombo)
                    m_scopeTelemetrySourceCombo->setEnabled(isCouplerTab);
                if (m_scopeTelemetryAuxCombo)
                    m_scopeTelemetryAuxCombo->setEnabled(isAuxTab);
            };

            connect(m_scopeTelemetryTabs,
                    &QTabWidget::currentChanged,
                    this,
                    [updateSelectorEnablement](int)
                    { updateSelectorEnablement(); });

            // ----------------------------
            // Apply settings (autoscale/window/max + selected coupler)
            // IMPORTANT: this is what makes "Auto-scale unchecked" truly stop autoscaling.
            // ----------------------------
            connect(m_scopeChkAutoScale, &QCheckBox::toggled, this, [this](bool)
                    { applyScopeTelemetrySettings(); });

            connect(m_scopeComboWindow,
                    qOverload<int>(&QComboBox::currentIndexChanged),
                    this,
                    [this](int)
                    { applyScopeTelemetrySettings(); });

            connect(m_scopeSpinMaxPts, qOverload<int>(&QSpinBox::valueChanged), this, [this](int)
                    { applyScopeTelemetrySettings(); });

            connect(m_scopeTelemetrySourceCombo,
                    qOverload<int>(&QComboBox::currentIndexChanged),
                    this,
                    [this](int)
                    {
                        // selected coupler impacts fixed Y max when auto-scale is off
                        applyScopeTelemetrySettings();
                    });

            connect(m_scopeTelemetryAuxCombo,
                    qOverload<int>(&QComboBox::currentIndexChanged),
                    this,
                    [this](int)
                    {
                        // affects which aux index gets plotted
                        // no need to rescale, but harmless
                        applyScopeTelemetrySettings();
                    });

            // Initialize once
            updateSelectorEnablement();
            applyScopeTelemetrySettings();

            // If user clicks the X on the dock, keep the menu item in sync
            connect(m_scopeTelemetryDock, &QDockWidget::visibilityChanged, this, [this](bool vis)
                    {
            // Keep the menu action in sync with the dock visibility
            if (ui && ui->actionScopeTelemetry) {
                QSignalBlocker b(ui->actionScopeTelemetry);
                ui->actionScopeTelemetry->setChecked(vis);
            }

            // Only do teardown when becoming hidden/closed
            if (vis)
                return;

            // Stop feeding telemetry into a scope that’s going away
            if (m_scopeTelemetryConn) {
                disconnect(m_scopeTelemetryConn);
                m_scopeTelemetryConn = {};
            }

            // Schedule the dock for deletion and clear our pointer
            if (m_scopeTelemetryDock) {
                m_scopeTelemetryDock->deleteLater();
                m_scopeTelemetryDock = nullptr;
            }

            // Clear child pointers and flags so a future open starts clean
            m_scopeTelemetryTabs = nullptr;

            m_scopePlotForwardW = nullptr;
            m_scopePlotReflectedW = nullptr;
            m_scopePlotSwr = nullptr;
            m_scopePlotAuxVolt = nullptr;
            m_scopePlotAuxCurr = nullptr;
            m_scopePlotTempC = nullptr;

            m_scopeBtnStartStop = nullptr;
            m_scopeBtnPause     = nullptr;
            m_scopeBtnClear     = nullptr;
            m_scopeBtnExport    = nullptr;
            m_scopeChkAutoScale = nullptr;
            m_scopeComboWindow  = nullptr;
            m_scopeSpinMaxPts   = nullptr;

            m_scopeTelemetrySourceCombo = nullptr;
            m_scopeTelemetryAuxCombo    = nullptr;

            m_scopeTelemetryRunning = false;
            m_scopeTelemetryPaused  = false; });

            // ----------------------------
            // Telemetry hookup (plot selected sources, NOT "follow active")
            // ----------------------------
            m_scopeTelemetryConn = connect(&m_comm,
                                           &RayCommManager::telemetryReceived,
                                           this,
                                           [this](const raypm::RayTelemetry &t)
                                           {
                                               if (!m_scopeTelemetryDock)
                                                   return;

                                               if (!m_scopeTelemetryRunning || m_scopeTelemetryPaused)
                                                   return;

                                               // --- Selected Coupler (for Forward/Reflected/SWR tabs) ---
                                               int wantCoupler = 0;
                                               if (m_scopeTelemetrySourceCombo)
                                               {
                                                   const int v = m_scopeTelemetrySourceCombo->currentData().toInt();
                                                   if (v >= 0)
                                                       wantCoupler = v;
                                               }

                                               const raypm::RayCouplerSample *cs = nullptr;
                                               for (const auto &c : t.couplers)
                                               {
                                                   if (c.index == wantCoupler)
                                                   {
                                                       cs = &c;
                                                       break;
                                                   }
                                               }

                                               const double fwdW = cs ? cs->forwardWatts : t.forwardWatts;
                                               const double refW = cs ? cs->reflectedWatts : t.reflectedWatts;
                                               const double swr = cs ? cs->swr : t.swrLegacy;

                                               if (m_scopePlotForwardW)
                                                   m_scopePlotForwardW->addSample(fwdW);
                                               if (m_scopePlotReflectedW)
                                                   m_scopePlotReflectedW->addSample(refW);
                                               if (m_scopePlotSwr)
                                                   m_scopePlotSwr->addSample(swr);

                                               // --- Selected Aux (for AuxV/AuxI/Temp tabs) ---
                                               int wantAux = 0;
                                               if (m_scopeTelemetryAuxCombo)
                                               {
                                                   const int v = m_scopeTelemetryAuxCombo->currentData().toInt();
                                                   if (v >= 0)
                                                       wantAux = v;
                                               }

                                               auto findAux = [&](raypm::RayAuxType type,
                                                                  double legacyFallback) -> double
                                               {
                                                   for (const auto &a : t.auxChannels)
                                                   {
                                                       if (a.index == wantAux && a.type == type)
                                                           return a.value;
                                                   }
                                                   return legacyFallback;
                                               };

                                               const double v = findAux(raypm::RayAuxType::Voltage, t.voltage);
                                               const double i = findAux(raypm::RayAuxType::Current, t.current);
                                               const double tc = findAux(raypm::RayAuxType::Temperature, t.temperatureC);

                                               if (m_scopePlotAuxVolt)
                                                   m_scopePlotAuxVolt->addSample(v);
                                               if (m_scopePlotAuxCurr)
                                                   m_scopePlotAuxCurr->addSample(i);
                                               if (m_scopePlotTempC)
                                                   m_scopePlotTempC->addSample(tc);
                                           });

            // ----------------------------
            // Buttons
            // ----------------------------
            connect(m_scopeBtnStartStop, &QPushButton::clicked, this, [this]()
                    {
            m_scopeTelemetryRunning = !m_scopeTelemetryRunning;

            if (m_scopeTelemetryRunning) {
                m_scopeTelemetryPaused = false;
                if (m_scopeBtnPause) {
                    m_scopeBtnPause->setEnabled(true);
                    m_scopeBtnPause->setText(tr("Pause"));
                }
                if (m_scopeBtnStartStop)
                    m_scopeBtnStartStop->setText(tr("Stop"));
                if (statusBar())
                    statusBar()->showMessage(tr("Telemetry scope started"), 1500);
            } else {
                m_scopeTelemetryPaused = false;
                if (m_scopeBtnPause) {
                    m_scopeBtnPause->setEnabled(false);
                    m_scopeBtnPause->setText(tr("Pause"));
                }
                if (m_scopeBtnStartStop)
                    m_scopeBtnStartStop->setText(tr("Start"));
                if (statusBar())
                    statusBar()->showMessage(tr("Telemetry scope stopped"), 1500);
            } });

            connect(m_scopeBtnPause, &QPushButton::clicked, this, [this]()
                    {
            if (!m_scopeTelemetryRunning)
                return;

            m_scopeTelemetryPaused = !m_scopeTelemetryPaused;
            if (m_scopeBtnPause)
                m_scopeBtnPause->setText(m_scopeTelemetryPaused ? tr("Resume") : tr("Pause"));

            if (statusBar())
                statusBar()->showMessage(m_scopeTelemetryPaused ? tr("Telemetry scope paused")
                                                                : tr("Telemetry scope resumed"),
                                         1500); });

            connect(m_scopeBtnClear, &QPushButton::clicked, this, [this]()
                    {
            auto clear = [&](ScopePlotWidget *w) {
                if (w)
                    w->clear();
            };
            clear(m_scopePlotForwardW);
            clear(m_scopePlotReflectedW);
            clear(m_scopePlotSwr);
            clear(m_scopePlotAuxVolt);
            clear(m_scopePlotAuxCurr);
            clear(m_scopePlotTempC);

            if (statusBar())
                statusBar()->showMessage(tr("Telemetry scope cleared"), 1500); });

            connect(m_scopeBtnExport, &QPushButton::clicked, this, [this]()
                    { exportActiveTelemetryScopePlotPng(); });

            // Smoothing change handler
            auto applyTelemetrySmoothing = [this]()
            {
                const QString key = m_scopeTelemetrySmoothingCombo
                                        ? m_scopeTelemetrySmoothingCombo->currentData().toString()
                                        : QStringLiteral("ema:0.33");

                auto applyOne = [&](ScopePlotWidget *w)
                {
                    if (!w)
                        return;

                    if (key == QLatin1String("none"))
                    {
                        w->setSmoothingMode(ScopePlotWidget::SmoothingMode::None);
                    }
                    else if (key.startsWith(QLatin1String("sma:")))
                    {
                        w->setSmoothingMode(ScopePlotWidget::SmoothingMode::Sma);
                        w->setSmaWindow(key.mid(4).toInt());
                    }
                    else if (key.startsWith(QLatin1String("ema:")))
                    {
                        w->setSmoothingMode(ScopePlotWidget::SmoothingMode::Ema);
                        w->setEmaAlpha(key.mid(4).toDouble());
                    }
                    else
                    {
                        w->setSmoothingMode(ScopePlotWidget::SmoothingMode::Ema);
                        w->setEmaAlpha(0.33);
                    }
                };

                applyOne(m_scopePlotForwardW);
                applyOne(m_scopePlotReflectedW);
                applyOne(m_scopePlotSwr);
                applyOne(m_scopePlotAuxVolt);
                applyOne(m_scopePlotAuxCurr);
                applyOne(m_scopePlotTempC);
            };

            // Connect ONCE (during dock creation)
            connect(m_scopeTelemetrySmoothingCombo, &QComboBox::currentIndexChanged,
                    this, [applyTelemetrySmoothing](int)
                    { applyTelemetrySmoothing(); });

            // Apply ONCE initially
            applyTelemetrySmoothing();
        }

        m_scopeTelemetryDock->show();
        m_scopeTelemetryDock->raise();
    }

    void MainWindow::on_actionScopeModulation_triggered(bool checked)
    {
        // Unchecked => hide + cleanup
        if (!checked)
        {
            if (m_scopeModulationDock)
            {
                m_scopeModulationDock->hide();
                m_scopeModulationDock->deleteLater();
                m_scopeModulationDock = nullptr;
                m_modSmoothingCombo = nullptr;
                m_scopeTelemetrySmoothingCombo = nullptr;
            }

            m_scopeModulationRunning = false;
            m_modDataRequested = false;
            m_modPollAttempts = 0;

            if (m_modPollTimer)
                m_modPollTimer->stop();

            return;
        }

        // Already created => just show/raise
        if (m_scopeModulationDock)
        {
            m_scopeModulationDock->show();
            m_scopeModulationDock->raise();
            return;
        }

        // -----------------------------
        // Create dock + UI
        // -----------------------------
        m_scopeModulationDock = new QDockWidget(tr("Modulation"), this);
        m_scopeModulationDock->setObjectName(QStringLiteral("ScopeModulationDock"));
        m_scopeModulationDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        auto *panel = new QWidget(m_scopeModulationDock);
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(6, 6, 6, 6);
        v->setSpacing(6);

        // Row 1: buttons
        auto *row1 = new QHBoxLayout;
        row1->setContentsMargins(0, 0, 0, 0);

        m_modBtnStartStop = new QPushButton(tr("Start"), panel);
        // Live checkbox (auto-rearm)
        m_modLiveCheck = new QCheckBox(tr("Live"), this);
        m_modLiveCheck->setChecked(false);
        m_modChkMeasurements = new QCheckBox(tr("Measures"), panel);
        m_modChkMeasurements->setChecked(m_showScopeMeasurements);
        m_modBtnClear = new QPushButton(tr("Clear"), panel);
        m_modBtnExport = new QPushButton(tr("Export as PNG"), panel);

        row1->addWidget(m_modBtnStartStop);
        row1->addWidget(m_modLiveCheck);
        row1->addWidget(m_modChkMeasurements);
        row1->addWidget(m_modBtnClear);
        row1->addStretch(1);
        row1->addWidget(m_modBtnExport);
        v->addLayout(row1);

        connect(m_modChkMeasurements, &QCheckBox::toggled, this, [this](bool on)
                { applyScopeMeasurementsOverlay(on); });

        // Row 2: parameters
        auto *row2 = new QHBoxLayout;
        row2->setContentsMargins(0, 0, 0, 0);

        m_modCouplerCombo = new QComboBox(panel);
        m_modCouplerCombo->addItem(tr("Coupler 1"), 0);
        m_modCouplerCombo->addItem(tr("Coupler 2"), 1);
        m_modCouplerCombo->addItem(tr("Coupler 3"), 2);
        m_modCouplerCombo->addItem(tr("Coupler 4"), 3);

        m_modPathCombo = new QComboBox(panel);
        m_modPathCombo->addItem(tr("Forward"), QStringLiteral("fwd"));
        m_modPathCombo->addItem(tr("Reflected"), QStringLiteral("ref"));

        m_modSamplesSpin = new QSpinBox(panel);
        m_modSamplesSpin->setRange(256, 262144);
        m_modSamplesSpin->setSingleStep(256);
        m_modSamplesSpin->setValue(1024);

        m_modRateSpin = new QSpinBox(panel);
        m_modRateSpin->setRange(8000, 192000);
        m_modRateSpin->setSingleStep(1000);
        m_modRateSpin->setValue(24000);

        m_modSmoothingCombo = new QComboBox(panel);
        m_modSmoothingCombo->addItem(tr("None"), QStringLiteral("none"));
        m_modSmoothingCombo->addItem(tr("SMA (3)"), QStringLiteral("sma:3"));
        m_modSmoothingCombo->addItem(tr("SMA (5)"), QStringLiteral("sma:5"));
        m_modSmoothingCombo->addItem(tr("SMA (9)"), QStringLiteral("sma:9"));
        m_modSmoothingCombo->addItem(tr("EMA (Fast)"), QStringLiteral("ema:0.50"));
        m_modSmoothingCombo->addItem(tr("EMA (Medium)"), QStringLiteral("ema:0.33"));
        m_modSmoothingCombo->addItem(tr("EMA (Slow)"), QStringLiteral("ema:0.15"));
        m_modSmoothingCombo->setCurrentIndex(m_modSmoothingCombo->findData(QStringLiteral("ema:0.33")));

        row2->addWidget(new QLabel(tr("Coupler:"), panel));
        row2->addWidget(m_modCouplerCombo);
        row2->addSpacing(10);

        row2->addWidget(new QLabel(tr("Path:"), panel));
        row2->addWidget(m_modPathCombo);
        row2->addSpacing(10);

        row2->addWidget(new QLabel(tr("Samples:"), panel));
        row2->addWidget(m_modSamplesSpin);
        row2->addSpacing(10);

        row2->addWidget(new QLabel(tr("Rate (Hz):"), panel));
        row2->addWidget(m_modRateSpin);

        row2->addSpacing(10);
        row2->addWidget(new QLabel(tr("Timebase:"), panel));

        row2->addSpacing(10);
        row2->addWidget(new QLabel(tr("Smoothing:"), panel));
        row2->addWidget(m_modSmoothingCombo);

        m_modTimebaseCombo = new QComboBox(panel);
        m_modTimebaseCombo->addItem(tr("Auto"), 0);
        m_modTimebaseCombo->addItem(tr("10 ms"), 10);
        m_modTimebaseCombo->addItem(tr("25 ms"), 25);
        m_modTimebaseCombo->addItem(tr("50 ms"), 50);
        m_modTimebaseCombo->addItem(tr("100 ms"), 100);
        m_modTimebaseCombo->addItem(tr("250 ms"), 250);
        m_modTimebaseCombo->addItem(tr("500 ms"), 500);
        m_modTimebaseCombo->setCurrentIndex(5); // default 250 ms (nice “snapshot”)
        row2->addWidget(m_modTimebaseCombo);
        row2->addStretch(1);

        v->addLayout(row2);

        // Plot (ONLY ONCE)
        m_scopePlotModulation = new ScopePlotWidget(panel);
        m_scopePlotModulation->setTitle(tr("Modulation"));
        m_scopePlotModulation->setScopeType(ScopePlotWidget::ScopeType::Modulation);
        m_scopePlotModulation->setShowMeasurements(m_showScopeMeasurements);
        v->addWidget(m_scopePlotModulation, 1);

        // Axis labels on one line (centered)
        m_modAxisLabel = new QLabel(tr("RF Volts (Vpp @ 50Ω)    |    Time (ms)"), panel);
        m_modAxisLabel->setAlignment(Qt::AlignHCenter);
        v->addWidget(m_modAxisLabel);

        // Status row (bottom)
        auto *rowInfo = new QHBoxLayout();
        rowInfo->setContentsMargins(0, 4, 0, 0);

        m_modStatusLabel = new QLabel(tr("Idle"), panel); // <-- if you have a member
        m_modStatusLabel->setMinimumWidth(160);

        rowInfo->addWidget(m_modStatusLabel);
        rowInfo->addStretch(1);

        v->addLayout(rowInfo);

        panel->setLayout(v);
        m_scopeModulationDock->setWidget(panel);
        addDockWidget(Qt::RightDockWidgetArea, m_scopeModulationDock);

        // Dock close => uncheck menu item
        connect(m_scopeModulationDock, &QDockWidget::visibilityChanged, this, [this](bool vis)
                {
        if (ui && ui->actionScopeModulation) {
            QSignalBlocker b(ui->actionScopeModulation);
            ui->actionScopeModulation->setChecked(vis);
        }
        if (!vis) {
            m_scopeModulationRunning = false;
        } });

        // Cleanup pointers when dock is destroyed (prevents stale pointers)
        connect(m_scopeModulationDock, &QObject::destroyed, this, [this]()
                {
        // Stop polling timer if it exists
        if (m_scopeModulationDock) {
            if (auto *t = m_scopeModulationDock->findChild<QTimer *>(
                    QStringLiteral("modPollTimer"))) {
                t->stop();
            }
        }

        m_scopeModulationDock = nullptr;
        m_modBtnStartStop = nullptr;
        m_modLiveCheck = nullptr;
        m_modBtnClear = nullptr;
        m_modBtnExport = nullptr;
        m_modCouplerCombo = nullptr;
        m_modPathCombo = nullptr;
        m_modSamplesSpin = nullptr;
        m_modRateSpin = nullptr;
        m_scopePlotModulation = nullptr;
        m_scopeModulationRunning = false; });

        // -----------------------------
        // Wire buttons
        // -----------------------------
        connect(m_modBtnClear, &QPushButton::clicked, this, [this]()
                {
        if (m_scopePlotModulation)
            m_scopePlotModulation->clear(); });

        connect(m_modBtnExport, &QPushButton::clicked, this, &MainWindow::exportModulationScopePlotPng);

        connect(m_modBtnStartStop, &QPushButton::clicked, this, [this]()
                {
        if (!m_modCouplerCombo || !m_modPathCombo || !m_modSamplesSpin || !m_modRateSpin)
            return;

        // Toggle: Stop
        if (m_scopeModulationRunning) {
            m_scopeModulationRunning = false;

            if (m_modPollTimer)
                m_modPollTimer->stop();

            if (m_modBtnStartStop) {
                m_modBtnStartStop->setText(tr("Start"));
                m_modBtnStartStop->setEnabled(true);
            }

            if (statusBar())
                statusBar()->showMessage(tr("Modulation capture stopped."), 2000);

            return;
        }

        const int coupler = m_modCouplerCombo->currentData().toInt();
        const QString path = m_modPathCombo->currentData().toString();
        const int samples = m_modSamplesSpin->value();
        const int rateHz = m_modRateSpin->value();

        // Snapshot capture
        m_scopeModulationRunning = true;
        m_modDataRequested = false;
        m_modPollAttempts = 0;

        // Ensure poll timer exists (polls /api/modulation/status until ready)
        // Create once
        if (!m_modPollTimer) {
            m_modPollTimer = new QTimer(this);
            m_modPollTimer->setInterval(100); // 50–100ms is fine
            connect(m_modPollTimer, &QTimer::timeout, this, [this]() {
                if (!m_scopeModulationRunning || !m_scopeModulationDock) {
                    m_modPollTimer->stop();
                    return;
                }

                // Timeout protection (uses your existing counter)
                if (++m_modPollAttempts > 50) { // 50 * 75ms = 3.75s
                    m_modPollTimer->stop();
                    m_scopeModulationRunning = false;

                    if (m_modBtnStartStop) {
                        m_modBtnStartStop->setEnabled(true);
                        m_modBtnStartStop->setText(tr("Start"));
                    }

                    if (statusBar())
                        statusBar()->showMessage(tr("Modulation capture timed out."), 4000);

                    return;
                }

                m_comm.requestModulationStatus();
            });
        }

        if (m_modBtnStartStop) {
            m_modBtnStartStop->setEnabled(false);
            m_modBtnStartStop->setText(tr("Requesting..."));
        }

        if (m_scopePlotModulation)
            m_scopePlotModulation->clear();

        m_comm.requestModulationStart(coupler, path, samples, rateHz); });

        // -----------------------------
        // Modulation communication handlers
        // -----------------------------
        connect(&m_comm,
                &raypm::RayCommManager::modulationStartAck,
                this,
                [this](bool ok,
                       const QString &status,
                       int coupler,
                       const QString &path,
                       int samples,
                       int sampleRateHz)
                {
                    Q_UNUSED(coupler);
                    Q_UNUSED(path);

                    qDebug() << "Modulation start ack received: ok=" << ok << "status=" << status
                             << "samples=" << samples << "rateHz=" << sampleRateHz;

                    // --- NEW: status label helper (no header change) ---
                    auto setModStatus = [this](const QString &s)
                    {
                        if (!m_scopeModulationDock)
                            return;
                        if (auto *lbl = m_scopeModulationDock->findChild<QLabel *>(
                                QStringLiteral("modStatusLabel")))
                            lbl->setText(s);
                    };

                    if (!ok)
                    {
                        m_scopeModulationRunning = false;
                        if (m_modPollTimer)
                            m_modPollTimer->stop();

                        if (m_modBtnStartStop)
                        {
                            m_modBtnStartStop->setEnabled(true);
                            m_modBtnStartStop->setText(tr("Start"));
                        }

                        setModStatus(tr("Idle"));

                        if (statusBar())
                            statusBar()->showMessage(tr("Modulation start failed: %1").arg(status),
                                                     4000);
                        return;
                    }

                    // Reflect accepted values
                    if (m_modSamplesSpin)
                        m_modSamplesSpin->setValue(samples);
                    if (m_modRateSpin)
                        m_modRateSpin->setValue(sampleRateHz);

                    // --- NEW: axis labels based on current settings ---
                    if (m_scopeModulationDock)
                    {
                        if (auto *xLbl = m_scopeModulationDock->findChild<QLabel *>(
                                QStringLiteral("modXAxisLabel")))
                            xLbl->setText(tr("X: Time (ms)"));
                        if (auto *yLbl = m_scopeModulationDock->findChild<QLabel *>(
                                QStringLiteral("modYAxisLabel")))
                            yLbl->setText(tr("Y: ADC counts (centered)"));
                    }

                    if (statusBar())
                        statusBar()->showMessage(tr("Modulation capture armed (%1)").arg(status), 2500);

                    // Keep as "Stop" while we're waiting for ready/data
                    if (m_modBtnStartStop)
                    {
                        m_modBtnStartStop->setEnabled(true);
                        m_modBtnStartStop->setText(tr("Stop"));
                    }

                    setModStatus(tr("Armed"));

                    // Reset poll state
                    m_modPollAttempts = 0;
                    m_modDataRequested = false;

                    // Ensure poll timer exists
                    if (!m_modPollTimer)
                    {
                        QObject *parentObj = m_scopeModulationDock
                                                 ? static_cast<QObject *>(m_scopeModulationDock)
                                                 : static_cast<QObject *>(this);

                        m_modPollTimer = new QTimer(parentObj);
                        m_modPollTimer->setInterval(100); // <-- changed from 50

                        connect(m_modPollTimer, &QTimer::timeout, this, [this]()
                                {
                        if (!m_scopeModulationDock || !m_scopeModulationRunning) {
                            m_modPollTimer->stop();
                            return;
                        }

                        if (++m_modPollAttempts > 50) { // 50 * 100ms = 5s
                            m_modPollTimer->stop();
                            m_scopeModulationRunning = false;

                            if (m_modBtnStartStop) {
                                m_modBtnStartStop->setEnabled(true);
                                m_modBtnStartStop->setText(tr("Start"));
                            }

                            if (auto *lbl = m_scopeModulationDock
                                                ? m_scopeModulationDock->findChild<QLabel *>(
                                                      QStringLiteral("modStatusLabel"))
                                                : nullptr) {
                                lbl->setText(tr("Idle"));
                            }

                            if (statusBar())
                                statusBar()->showMessage(
                                    tr("Modulation capture timed out (never became ready)."), 4000);
                            return;
                        }

                        // Optional: show “Capturing” once we’re polling
                        if (auto *lbl = m_scopeModulationDock
                                            ? m_scopeModulationDock->findChild<QLabel *>(
                                                  QStringLiteral("modStatusLabel"))
                                            : nullptr) {
                            if (lbl->text() == tr("Armed"))
                                lbl->setText(tr("Capturing"));
                        }

                        m_comm.requestModulationStatus(); });
                    }

                    // Store requested sample count for later fallback
                    m_modPollTimer->setProperty("requestSamples", samples);

                    // Start polling
                    m_modPollTimer->start();
                    m_comm.requestModulationStatus();
                });

        // Modulation data received handler
        connect(&m_comm,
                &raypm::RayCommManager::modulationStatusReceived,
                this,
                [this](const QString &state,
                       int coupler,
                       const QString &path,
                       int samples,
                       int sampleRateHz)
                {
                    qDebug() << "Mod status:" << state << "samples=" << samples
                             << "rate=" << sampleRateHz;

                    Q_UNUSED(coupler);
                    Q_UNUSED(path);
                    Q_UNUSED(sampleRateHz);

                    if (!m_scopeModulationDock || !m_scopeModulationRunning)
                        return;

                    auto setModStatus = [this](const QString &s)
                    {
                        if (!m_scopeModulationDock)
                            return;
                        if (auto *lbl = m_scopeModulationDock->findChild<QLabel *>(
                                QStringLiteral("modStatusLabel")))
                            lbl->setText(s);
                    };

                    if (state == QLatin1String("ready"))
                    {
                        if (m_modPollTimer)
                            m_modPollTimer->stop();

                        setModStatus(tr("Ready"));

                        if (!m_modDataRequested)
                        {
                            m_modDataRequested = true;

                            int requestSamples = samples;
                            if (requestSamples <= 0 && m_modPollTimer)
                            {
                                const QVariant v = m_modPollTimer->property("requestSamples");
                                if (v.isValid())
                                    requestSamples = v.toInt();
                            }
                            if (requestSamples <= 0 && m_modSamplesSpin)
                                requestSamples = m_modSamplesSpin->value();

                            setModStatus(tr("Plotting"));
                            m_comm.requestModulationData(requestSamples);
                        }

                        return;
                    }

                    if (state == QLatin1String("error"))
                    {
                        if (m_modPollTimer)
                            m_modPollTimer->stop();

                        m_scopeModulationRunning = false;

                        if (m_modBtnStartStop)
                        {
                            m_modBtnStartStop->setEnabled(true);
                            m_modBtnStartStop->setText(tr("Start"));
                        }

                        setModStatus(tr("Idle"));
                    }
                });

        connect(
            &m_comm,
            &raypm::RayCommManager::modulationDataReceived,
            this,
            [this](int coupler, const QString &path, int sampleRateHz, const QVector<qint16> &samples)
            {
                Q_UNUSED(path);

                if (!m_scopePlotModulation || !m_scopeModulationDock->isVisible())
                    return;

                const bool liveNow = (m_modLiveCheck && m_modLiveCheck->isChecked());

                if (sampleRateHz <= 0 || samples.size() < 2)
                {
                    // In Live mode we "hold last valid capture" during quiet/invalid frames.
                    if (!liveNow)
                        m_scopePlotModulation->clear();
                    return;
                }

                // Prefer the coupler index reported by the capture
                int couplerIndex = coupler;
                if (couplerIndex < 0 || couplerIndex > 3)
                {
                    // fallback to UI if signal value is unexpected
                    couplerIndex = m_modCouplerCombo ? m_modCouplerCombo->currentIndex() : 0;
                }

                // Pull full scale watts from cached coupler config (linear pre-calibration for now).
                double fullScaleW = 0.0;
                auto it = m_couplerConfigs.find(couplerIndex);
                if (it != m_couplerConfigs.end())
                    fullScaleW = it.value().fullScaleW;

                // Fallback if config isn’t available yet
                if (fullScaleW <= 0.0)
                    fullScaleW = 100.0;

                auto clamp01 = [](double v) -> double
                {
                    if (v < 0.0)
                        return 0.0;
                    if (v > 1.0)
                        return 1.0;
                    return v;
                };

                auto rfVppFromWatts50 = [](double watts) -> double
                {
                    if (watts <= 0.0)
                        return 0.0;
                    // Vrms = sqrt(P*R), Vpp = 2*sqrt(2)*Vrms
                    const double vrms = std::sqrt(watts * 50.0);
                    return 2.0 * std::sqrt(2.0) * vrms;
                };

                const double fs = double(sampleRateHz);
                const qsizetype N = samples.size();
                const double durationSec = (N > 1) ? (double(N - 1) / fs) : 0.0;

                // -----------------------------
                // Timebase (last-window policy)
                // -----------------------------
                double windowMs = 0.0; // 0 == Auto
                if (m_modTimebaseCombo)
                {
                    windowMs = m_modTimebaseCombo->currentData().toDouble();
                }
                const double windowSec = (windowMs > 0.0) ? (windowMs / 1000.0) : durationSec;

                // How many samples fit in the selected window?
                qsizetype winN = N;
                if (windowSec > 0.0 && fs > 0.0)
                {
                    winN = std::min<qsizetype>(N,
                                               std::max<qsizetype>(1,
                                                                   qsizetype(
                                                                       std::ceil(windowSec * fs))));
                }
                const qsizetype startIdx = std::max<qsizetype>(0, N - winN);

                // IMPORTANT: clear after maxSamples/timeWindow are set
                // m_scopePlotModulation->setTimeWindowSeconds(std::max(0.01, windowSec));
                // m_scopePlotModulation->setUseFixedYRange(false);
                // m_scopePlotModulation->setMaxSamples(int(winN));
                // m_scopePlotModulation->clear();

                // const bool liveNow = (m_modLiveCheck && m_modLiveCheck->isChecked());

                // Precompute windowed points so we can apply a "quiet" policy in Live mode
                QVector<double> xs;
                QVector<double> ys;
                xs.reserve(int(winN));
                ys.reserve(int(winN));

                double yMax = 0.0;
                int aboveThresh = 0;

                // denom for x normalization across the WINDOW so it always fills [-1..0]
                const double denom = (winN > 1) ? double(winN - 1) : 1.0;

                for (qsizetype i = startIdx; i < N; ++i)
                {
                    const double adc = double(samples[i]);
                    const double frac = clamp01(adc / 4095.0);

                    // Linear watts (pre-calibration): 3.3V => fullScaleW
                    const double watts = frac * fullScaleW;

                    // Default display: RF-equivalent voltage, Vpp @ 50Ω
                    const double y = rfVppFromWatts50(watts);

                    // i=N-1 => 0, i=startIdx => -1
                    const double xNorm = (double(i) - double(N - 1)) / denom;

                    xs.push_back(xNorm);
                    ys.push_back(y);

                    yMax = std::max(yMax, y);
                    if (y >= MODULATION_QUIET_VPP_THRESHOLD)
                        ++aboveThresh;
                }

                const int minAbove = int(std::ceil(MODULATION_QUIET_ABOVE_FRACTION * double(winN)));
                const bool isQuietLive = (liveNow && winN > 0 && aboveThresh < minAbove);

                if (!isQuietLive)
                {
                    // Only touch the plot when not quiet (so we "hold last valid capture" during quiet)
                    m_scopePlotModulation->setTimeWindowSeconds(std::max(0.01, windowSec));
                    m_scopePlotModulation->setUseFixedYRange(false);
                    m_scopePlotModulation->setMaxSamples(int(winN));

                    QVector<QPointF> pts;
                    pts.reserve(xs.size());
                    for (qsizetype k = 0; k < xs.size(); ++k)
                        pts.push_back(QPointF(xs[k], ys[k]));

                    m_scopePlotModulation->setSamples(pts);

                    // Y: fixed min=0, max computed
                    const double paddedMax = (yMax > 0.0) ? (yMax * 1.05) : 1.0;
                    m_scopePlotModulation->setFixedYRange(0.0, paddedMax);
                    m_scopePlotModulation->setUseFixedYRange(true);

                    m_scopePlotModulation->update();
                }

                m_scopePlotModulation->update();

                // Stop polling now that we have the data
                if (m_modPollTimer)
                    m_modPollTimer->stop();

                const bool live = (m_modLiveCheck && m_modLiveCheck->isChecked());

                if (live)
                {
                    m_modDataRequested = false;
                    m_modPollAttempts = 0;

                    if (m_modBtnStartStop)
                    {
                        m_modBtnStartStop->setEnabled(true);
                        m_modBtnStartStop->setText(isQuietLive ? tr("Standby") : tr("Stop"));
                    }

                    // setModStatus(isQuietLive ? tr("Standby") : tr("Live"));

                    if (statusBar())
                    {
                        statusBar()->showMessage(isQuietLive
                                                     ? tr("Modulation quiet — holding last capture.")
                                                     : tr("Modulation updated."),
                                                 1500);
                    }

                    // Defer to avoid re-entrancy in the signal handler.
                    QTimer::singleShot(0, this, [this]()
                                       {
                    if (!m_scopeModulationRunning)
                        return;
                    if (!m_modLiveCheck || !m_modLiveCheck->isChecked())
                        return;

                    if (!m_modCouplerCombo || !m_modPathCombo || !m_modSamplesSpin || !m_modRateSpin)
                        return;

                    const int coupler = m_modCouplerCombo->currentData().toInt();
                    const QString path = m_modPathCombo->currentData().toString();
                    const int samples = m_modSamplesSpin->value();
                    const int rateHz = m_modRateSpin->value();

                    // Reset poll state for the next capture cycle
                    m_modDataRequested = false;
                    m_modPollAttempts = 0;

                    // Kick off the next capture
                    m_comm.requestModulationStart(coupler, path, samples, rateHz); });
                }
                else
                {
                    // capture-once UX reset (existing behavior)
                    m_scopeModulationRunning = false;
                    m_modDataRequested = false;
                    m_modPollAttempts = 0;

                    if (m_modBtnStartStop)
                    {
                        m_modBtnStartStop->setEnabled(true);
                        m_modBtnStartStop->setText(tr("Start"));
                    }

                    if (statusBar())
                        statusBar()->showMessage(tr("Modulation data received: %1 samples at %2 Hz")
                                                     .arg(samples.size())
                                                     .arg(sampleRateHz),
                                                 4000);

                    qDebug() << "Modulation data received:" << samples.size() << "samples at"
                             << sampleRateHz << "Hz";
                }
            });

        // Smoothing change handler
        auto applyModSmoothing = [this]()
        {
            if (!m_scopePlotModulation)
                return;

            const QString key = m_modSmoothingCombo ? m_modSmoothingCombo->currentData().toString()
                                                    : QStringLiteral("ema:0.33");

            if (key == QLatin1String("none"))
            {
                m_scopePlotModulation->setSmoothingMode(ScopePlotWidget::SmoothingMode::None);
                return;
            }
            if (key.startsWith(QLatin1String("sma:")))
            {
                m_scopePlotModulation->setSmoothingMode(ScopePlotWidget::SmoothingMode::Sma);
                m_scopePlotModulation->setSmaWindow(key.mid(4).toInt());
                return;
            }
            if (key.startsWith(QLatin1String("ema:")))
            {
                m_scopePlotModulation->setSmoothingMode(ScopePlotWidget::SmoothingMode::Ema);
                m_scopePlotModulation->setEmaAlpha(key.mid(4).toDouble());
                return;
            }

            m_scopePlotModulation->setSmoothingMode(ScopePlotWidget::SmoothingMode::Ema);
            m_scopePlotModulation->setEmaAlpha(0.33);
        };

        connect(m_modSmoothingCombo, &QComboBox::currentIndexChanged, this,
                [applyModSmoothing](int)
                { applyModSmoothing(); });

        applyModSmoothing();

        // Show dock now
        m_scopeModulationDock->show();
        m_scopeModulationDock->raise();

        // Show dock now
        m_scopeModulationDock->show();
        m_scopeModulationDock->raise();
    }

    void MainWindow::exportActiveTelemetryScopePlotPng()
    {
        if (!m_scopeTelemetryDock || !m_scopeTelemetryTabs)
            return;

        // Find the plot widget in the currently selected tab
        QWidget *tab = m_scopeTelemetryTabs->currentWidget();
        if (!tab)
            return;

        auto *plot = tab->findChild<ScopePlotWidget *>();
        if (!plot)
            return;

        // Build a smart default filename
        QString tabName = m_scopeTelemetryTabs->tabText(m_scopeTelemetryTabs->currentIndex());
        tabName.replace(QRegularExpression(R"([^A-Za-z0-9_\-]+)"), "_");

        QString sourcePart;

        // Use the Scope Telemetry selectors (NOT History selectors)
        if (m_scopeTelemetrySourceCombo && m_scopeTelemetrySourceCombo->isEnabled())
        {
            sourcePart = m_scopeTelemetrySourceCombo->currentText();
        }
        else if (m_scopeTelemetryAuxCombo && m_scopeTelemetryAuxCombo->isEnabled())
        {
            sourcePart = m_scopeTelemetryAuxCombo->currentText();
        }

        sourcePart = sourcePart.trimmed();
        if (!sourcePart.isEmpty())
            sourcePart.replace(QRegularExpression(R"([^A-Za-z0-9_\-]+)"), "_");

        const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString base = QString("Scope_%1").arg(tabName);
        if (!sourcePart.isEmpty())
            base += "_" + sourcePart;
        base += "_" + ts + ".png";

        const QString startDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        // Use last export dir if we have one
        const QString startDirFinal = m_lastTeleScopeExportDir.isEmpty() ? startDir
                                                                         : m_lastTeleScopeExportDir;

        const QString defaultPath = QDir(startDirFinal).filePath(base);

        const QString filePath = QFileDialog::getSaveFileName(this,
                                                              tr("Export Scope Image"),
                                                              defaultPath,
                                                              tr("PNG Image (*.png)"));
        if (filePath.isEmpty())
            return;

        QPixmap pm = plot->grab();

        // Theme-aware watermark (dark logo on light theme, white logo on dark theme)
        const QString wmPath = m_isDarkTheme
                                   ? QStringLiteral(":/assets/icons/RfssLogo1024_659_white.png")
                                   : QStringLiteral(":/assets/icons/RfssLogo1024_659.png");

        QPixmap mark(wmPath);
        if (!mark.isNull())
        {
            const int side = qBound(48, int(qMin(pm.width(), pm.height()) * 0.18), 180);
            mark = mark.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);

            QPainter painter(&pm);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            painter.setOpacity(m_isDarkTheme ? 0.12 : 0.12);

            const int padX = 15;
            const int padY = 30;
            const int x = qMax(0, pm.width() - mark.width() - padX);
            const int y = qMax(0, pm.height() - mark.height() - padY);
            painter.drawPixmap(x, y, mark);
        }

        pm.save(filePath, "PNG");

        m_lastTeleScopeExportDir = QFileInfo(filePath).absolutePath();
    }

    void MainWindow::applyScopeMeasurementsOverlay(bool on)
    {
        m_showScopeMeasurements = on;

        if (m_scopePlotModulation)
            m_scopePlotModulation->setShowMeasurements(on);

        if (m_scopePlotForwardW)
            m_scopePlotForwardW->setShowMeasurements(on);

        if (m_scopePlotReflectedW)
            m_scopePlotReflectedW->setShowMeasurements(on);

        if (m_scopePlotSwr)
            m_scopePlotSwr->setShowMeasurements(on);

        if (m_scopePlotAuxVolt)
            m_scopePlotAuxVolt->setShowMeasurements(on);

        if (m_scopePlotAuxCurr)
            m_scopePlotAuxCurr->setShowMeasurements(on);

        if (m_scopePlotTempC)
            m_scopePlotTempC->setShowMeasurements(on);

        if (m_swrVsFreqPlot)
            m_swrVsFreqPlot->setShowMeasurements(on);

        // Sync checkboxes
        if (m_scopeChkMeasurements && m_scopeChkMeasurements->isChecked() != on)
            m_scopeChkMeasurements->setChecked(on);

        if (m_modChkMeasurements && m_modChkMeasurements->isChecked() != on)
            m_modChkMeasurements->setChecked(on);

        if (m_swrFreqChkMeasurements && m_swrFreqChkMeasurements->isChecked() != on)
            m_swrFreqChkMeasurements->setChecked(on);
    }

    void MainWindow::exportModulationScopePlotPng()
    {
        if (!m_scopeModulationDock || !m_scopePlotModulation)
            return;

        // Build a smart default filename
        QString tabName = QStringLiteral("Modulation");

        QString sourcePart;
        if (m_modCouplerCombo)
        {
            sourcePart = m_modCouplerCombo->currentText();
        }
        if (m_modPathCombo)
        {
            const QString p = m_modPathCombo->currentText();
            if (!p.isEmpty())
                sourcePart += "_" + p;
        }

        sourcePart = sourcePart.trimmed();
        if (!sourcePart.isEmpty())
            sourcePart.replace(QRegularExpression(R"([^A-Za-z0-9_\-]+)"), "_");

        const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString base = QString("Scope_%1").arg(tabName);
        if (!sourcePart.isEmpty())
            base += "_" + sourcePart;
        base += "_" + ts + ".png";

        const QString startDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString startDirFinal = m_lastModScopeExportDir.isEmpty() ? startDir
                                                                        : m_lastModScopeExportDir;
        const QString defaultPath = QDir(startDirFinal).filePath(base);

        const QString filePath = QFileDialog::getSaveFileName(this,
                                                              tr("Export Scope Image"),
                                                              defaultPath,
                                                              tr("PNG Image (*.png)"));
        if (filePath.isEmpty())
            return;

        QPixmap pm = m_scopePlotModulation->grab();

        // Theme-aware watermark (dark logo on light theme, white logo on dark theme)
        const QString wmPath = m_isDarkTheme
                                   ? QStringLiteral(":/assets/icons/RfssLogo1024_659_white.png")
                                   : QStringLiteral(":/assets/icons/RfssLogo1024_659.png");

        QPixmap mark(wmPath);
        if (!mark.isNull())
        {
            const int side = qBound(48, int(qMin(pm.width(), pm.height()) * 0.18), 180);
            mark = mark.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);

            QPainter painter(&pm);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            painter.setOpacity(m_isDarkTheme ? 0.12 : 0.12);

            const int padX = 15;
            const int padY = 30;
            const int x = qMax(0, pm.width() - mark.width() - padX);
            const int y = qMax(0, pm.height() - mark.height() - padY);
            painter.drawPixmap(x, y, mark);
        }

        pm.save(filePath, "PNG");

        m_lastModScopeExportDir = QFileInfo(filePath).absolutePath();
    }

    void MainWindow::exportSpectrumScopePlotPng()
    {
        if (!m_scopeSpectrumDock || !m_scopePlotSpectrum)
            return;

        QString tabName = QStringLiteral("Spectrum");

        QString sourcePart;
        if (m_specCouplerCombo)
            sourcePart = m_specCouplerCombo->currentText();
        if (m_specPathCombo)
        {
            const QString p = m_specPathCombo->currentText();
            if (!p.isEmpty())
                sourcePart += "_" + p;
        }

        sourcePart = sourcePart.trimmed();
        if (!sourcePart.isEmpty())
            sourcePart.replace(QRegularExpression(R"([^A-Za-z0-9_\-]+)"), "_");

        const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString base = QString("Scope_%1").arg(tabName);
        if (!sourcePart.isEmpty())
            base += "_" + sourcePart;
        base += "_" + ts + ".png";

        const QString startDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString startDirFinal = m_lastSpecScopeExportDir.isEmpty() ? startDir : m_lastSpecScopeExportDir;
        const QString defaultPath = QDir(startDirFinal).filePath(base);

        const QString filePath = QFileDialog::getSaveFileName(this,
                                                              tr("Export Scope Image"),
                                                              defaultPath,
                                                              tr("PNG Image (*.png)"));
        if (filePath.isEmpty())
            return;

        QPixmap pm = m_scopePlotSpectrum->grab();

        const QString wmPath = m_isDarkTheme
                                   ? QStringLiteral(":/assets/icons/RfssLogo1024_659_white.png")
                                   : QStringLiteral(":/assets/icons/RfssLogo1024_659.png");

        QPixmap mark(wmPath);
        if (!mark.isNull())
        {
            const int side = qBound(48, int(qMin(pm.width(), pm.height()) * 0.18), 180);
            mark = mark.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);

            QPainter painter(&pm);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.setOpacity(0.12);

            const int padX = 15;
            const int padY = 30;
            const int x = qMax(0, pm.width() - mark.width() - padX);
            const int y = qMax(0, pm.height() - mark.height() - padY);
            painter.drawPixmap(x, y, mark);
        }

        pm.save(filePath, "PNG");
        m_lastSpecScopeExportDir = QFileInfo(filePath).absolutePath();
    }

    int MainWindow::auxAnalogIndexFromSource(const QString &source) const
    {
        // Expect layout sources: "Aux0", "Aux1", "Aux2"
        if (!source.startsWith(QStringLiteral("Aux")))
            return -1;

        // Reject AuxDigital ("AuxD#")
        if (source.startsWith(QStringLiteral("AuxD")))
            return -1;

        bool ok = false;
        const int idx = source.mid(3).toInt(&ok); // after "Aux"
        if (!ok)
            return -1;

        // Firmware shows 3 analog aux channels in your JSON
        if (idx < 0 || idx > 2)
            return -1;

        return idx; // 0..2
    }

    void MainWindow::applyAuxAnalogConfigFromLayout()
    {
        if (!m_layoutManager)
            return;

        struct DesiredAux
        {
            QString typeName; // "voltage" | "current" | "temperature"
            QString name;
        };

        QMap<int, DesiredAux> desired;

        const QStringList widgetIds = m_layoutManager->allWidgetIds();

        for (const QString &widgetId : widgetIds)
        {
            QString source;
            if (!m_layoutManager->widgetSource(widgetId, source))
                continue;

            const int auxIndex = auxAnalogIndexFromSource(source);
            if (auxIndex < 0)
                continue;

            const QString type = m_layoutManager->widgetType(widgetId);

            QString label;
            m_layoutManager->widgetLabel(widgetId, label);

            if (type == QLatin1String("AuxVoltage"))
            {
                DesiredAux d;
                d.typeName = QStringLiteral("voltage");
                d.name = label.isEmpty() ? QStringLiteral("Voltage") : label;

                if (!desired.contains(auxIndex))
                    desired.insert(auxIndex, d);

                continue;
            }

            if (type == QLatin1String("AuxSensor"))
            {
                AuxSensorWidget::Mode mode = AuxSensorWidget::Mode::Unknown;
                int tempUnitsIndex = 0;
                QString sensorLabel;

                if (!m_layoutManager->widgetAuxSensorConfig(widgetId, mode, tempUnitsIndex, sensorLabel))
                    continue;

                QString typeName;
                switch (mode)
                {
                case AuxSensorWidget::Mode::Voltage:
                    typeName = QStringLiteral("voltage");
                    break;
                case AuxSensorWidget::Mode::Current:
                    typeName = QStringLiteral("current");
                    break;
                case AuxSensorWidget::Mode::Temperature:
                    typeName = QStringLiteral("temperature");
                    break;
                default:
                    break;
                }

                if (typeName.isEmpty())
                    continue;

                DesiredAux d;
                d.typeName = typeName;

                const QString finalLabel = !sensorLabel.isEmpty() ? sensorLabel : label;
                d.name = finalLabel;

                if (!desired.contains(auxIndex))
                    desired.insert(auxIndex, d);

                continue;
            }
        }

        if (desired.isEmpty())
            return;

        bool didAnyPost = false;

        for (auto it = desired.constBegin(); it != desired.constEnd(); ++it)
        {
            const int idx = it.key();
            const DesiredAux &d = it.value();

            if (!m_auxConfigs.contains(idx))
            {
                qDebug() << "[Layout] Skipping applyAuxConfig for Aux" << idx
                         << "(device config not loaded yet)";
                continue;
            }

            const raypm::RayAuxConfig deviceCfg = m_auxConfigs[idx];

            if (deviceCfg.voltageMaxRange <= 0 ||
                deviceCfg.currentMaxRange <= 0 ||
                deviceCfg.temperatureMaxRange <= 0 ||
                deviceCfg.thermBeta <= 0)
            {
                qDebug() << "[Layout] Skipping applyAuxConfig for Aux" << idx
                         << "(ranges/beta not loaded yet)";
                continue;
            }

            const QString wantType = d.typeName.trimmed().toLower();
            const QString haveType = deviceCfg.typeName.trimmed().toLower();

            QString wantName = d.name.trimmed();
            const QString haveName = deviceCfg.name.trimmed();

            // Never let layout clear the device name accidentally.
            // If the layout label is empty, keep the device's current name.
            if (wantName.isEmpty())
                wantName = haveName;

            const bool same = (wantType == haveType && wantName == haveName);

            // ✅ IMPORTANT:
            // Even if nothing changed, firmware status may still show type="none"
            // until it processes at least one aux_config_set after boot/connect.
            if (same && !m_forceApplyAuxConfigOnce)
            {
                continue;
            }

            raypm::RayAuxConfig cfg = deviceCfg;
            cfg.index = idx;
            cfg.typeName = wantType;
            cfg.name = wantName;

            // Keep local cache consistent
            m_auxConfigs[idx] = cfg;

            m_comm.applyAuxConfig(cfg);
            didAnyPost = true;
        }

        // Consume the one-shot force flag once we have posted at least once.
        if (m_forceApplyAuxConfigOnce && didAnyPost)
        {
            m_forceApplyAuxConfigOnce = false;
        }
    }

    bool MainWindow::auxAnalogConfigReady() const
    {
        // We consider "ready" only when we have all 3 aux configs AND they contain sane ranges.
        if (m_auxConfigs.size() < MAX_AUX_CHANNELS)
            return false;

        for (int i = 0; i < MAX_AUX_CHANNELS; ++i)
        {
            if (!m_auxConfigs.contains(i))
                return false;

            const auto &cfg = m_auxConfigs[i];
            if (cfg.voltageMaxRange <= 0 || cfg.currentMaxRange <= 0 || cfg.temperatureMaxRange <= 0 || cfg.thermBeta <= 0)
                return false;
        }

        return true;
    }

    void MainWindow::tryApplyAuxAnalogConfigFromLayout()
    {
        if (!m_applyLayoutAuxAnalogPending)
            return;

        if (!auxAnalogConfigReady())
            return;

        m_applyLayoutAuxAnalogPending = false;
        m_applyLayoutAuxAnalogReceived.clear();

        applyAuxAnalogConfigFromLayout();
    }

    int MainWindow::auxDigitalIndexFromSource(const QString &source) const
    {
        // Expect firmware JSON names "AuxD1".."AuxD4"
        if (!source.startsWith(QStringLiteral("AuxD")))
            return -1;

        bool ok = false;
        const int oneBased = source.mid(4).toInt(&ok);
        if (!ok || oneBased <= 0)
            return -1;

        return oneBased - 1; // index 0..3
    }

    void MainWindow::applyAuxDigitalConfigFromLayout()
    {
        if (!m_layoutManager)
            return;

        // We iterate over layout items, not just live widgets, so we match
        // the JSON (auxDigMode + source) exactly.
        const QStringList widgetIds = m_layoutManager->allWidgetIds();

        for (const QString &widgetId : widgetIds)
        {
            // Only AuxDigital widgets will return true here.
            AuxDigitalWidget::Mode mode;
            if (!m_layoutManager->widgetAuxDigitalMode(widgetId, mode))
                continue;

            // Map layout "source" (AuxD1..AuxD4) -> firmware index.
            QString source;
            if (!m_layoutManager->widgetSource(widgetId, source))
                continue;

            const int auxIndex = auxDigitalIndexFromSource(source);
            if (auxIndex < 0)
                continue;

            const int modeId = static_cast<int>(mode);

            // Push config to the active comm worker (HTTP or Serial).
            // This matches what we already do in the layout property handler.
            m_comm.applyAuxDigitalConfig(auxIndex, modeId);
        }
    }

    void MainWindow::connectAuxDigitalWidgets()
    {
        if (!m_layoutManager)
            return;

        const QStringList ids = m_layoutManager->allWidgetIds();
        for (const QString &id : ids)
        {
            WidgetBase *wb = m_layoutManager->widgetObject(id);
            auto *dig = qobject_cast<AuxDigitalWidget *>(wb);
            if (!dig)
                continue;

            // Determine Aux Digital index from the widget's source string.
            QString source;
            if (!m_layoutManager->widgetSource(id, source))
                continue;

            const int index = auxDigitalIndexFromSource(source);
            if (index < 0)
                continue;

            // Look up the configured mode from the layout.
            AuxDigitalWidget::Mode mode = AuxDigitalWidget::Mode::Disabled;
            m_layoutManager->widgetAuxDigitalMode(id, mode);

            // Always push the configuration to the firmware so it knows
            // how this Aux Digital channel is configured.
            //
            // This will call through RayCommManager to:
            //   - RaySerialCommWorker::applyAuxDigitalConfig(...)  (COMM)
            //   - RayHttpCommWorker::applyAuxDigitalConfig(...)    (HTTP)
            //
            m_comm.applyAuxDigitalConfig(index, static_cast<int>(mode));

            // Only wire up output changes when the channel is configured as OUTPUT.
            if (mode != AuxDigitalWidget::Mode::Output)
                continue;

            connect(dig,
                    &AuxDigitalWidget::outputValueChanged,
                    this,
                    [this, index](bool value)
                    {
                        // We already resolved the index once; no need to
                        // look up the source string again here.
                        m_comm.setAuxDigitalValue(index, value);
                    });
        }
    }

    void MainWindow::onPropertyScopeSignalChanged(int index)
    {
        if (!canEditWidgetProperties() || m_selectedWidgetId.isEmpty() || !m_layoutManager || !m_propScopeSignalCombo)
            return;

        const int sig = m_propScopeSignalCombo->itemData(index).toInt();

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);
        if (!type.isEmpty())
        {
            if (auto *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this, m_layoutManager, m_selectedGroupId, m_selectedWidgetId);

                if (adapter->handlePropertyChange(ctx,
                                                  QStringLiteral("miniscope.signal"),
                                                  QVariant::fromValue(sig)))
                {
                    return;
                }
            }
        }

        m_layoutManager->setWidgetMiniScopeSignal(m_selectedWidgetId, sig);
    }

    void MainWindow::onPropertyScopeWindowChanged(double seconds)
    {
        if (!canEditWidgetProperties() || m_selectedWidgetId.isEmpty() || !m_layoutManager)
            return;

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);
        if (!type.isEmpty())
        {
            if (auto *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this, m_layoutManager, m_selectedGroupId, m_selectedWidgetId);

                if (adapter->handlePropertyChange(ctx,
                                                  QStringLiteral("miniscope.windowSec"),
                                                  QVariant::fromValue(seconds)))
                {
                    return;
                }
            }
        }

        m_layoutManager->setWidgetMiniScopeWindowSec(m_selectedWidgetId, seconds);
    }

    void MainWindow::onPropertyScopeAutoScaleChanged(bool checked)
    {
        if (!canEditWidgetProperties() || m_selectedWidgetId.isEmpty() || !m_layoutManager)
            return;

        const QString type = m_layoutManager->widgetType(m_selectedWidgetId);

        if (!type.isEmpty())
        {
            if (auto *adapter = adapterForWidgetType(type))
            {
                const WidgetPropertyContext ctx =
                    makeWidgetPropertyContext(this, m_layoutManager, m_selectedGroupId, m_selectedWidgetId);

                adapter->handlePropertyChange(ctx,
                                              QStringLiteral("miniscope.autoScale"),
                                              QVariant::fromValue(checked));
            }
        }

        m_layoutManager->setWidgetMiniScopeAutoScale(m_selectedWidgetId, checked);

        if (checked)
            return;

        QString sourceKey;
        if (!m_layoutManager->widgetSource(m_selectedWidgetId, sourceKey))
            return;

        if (!sourceKey.startsWith(QLatin1String("Aux")))
            return;

        bool ok = false;
        const int auxIndex = sourceKey.mid(3).toInt(&ok);
        if (!ok || auxIndex < 0)
            return;

        applyAuxRangeToMiniScopes(auxIndex);
    }

    void MainWindow::on_actionScopeAnalyzer_triggered(bool checked)
    {
        // TODO: implement analyzer scope UI
    }

    void MainWindow::on_actionScopeSWR_vs_Frequency_triggered(bool checked)
    {
        if (!checked)
        {
            if (m_scopeSwrFreqDock)
            {
                if (m_scopeSwrFreqConn)
                    disconnect(m_scopeSwrFreqConn);

                m_scopeSwrFreqDock->hide();
                m_scopeSwrFreqDock->deleteLater();
            }

            m_scopeSwrFreqDock = nullptr;
            m_swrVsFreqPlot = nullptr;
            m_swrFreqCurrentMHz = nullptr;
            m_swrFreqStartMHz = nullptr;
            m_swrFreqEndMHz = nullptr;
            m_swrFreqNumPts = nullptr;
            m_swrFreqCouplerCombo = nullptr;
            m_swrFreqUnitsCombo = nullptr;
            m_swrFreqModeCombo = nullptr;
            m_swrFreqBtnManualCapture = nullptr;
            m_swrFreqBtnClear = nullptr;
            m_swrFreqBtnExport = nullptr;
            m_swrFreqChkMeasurements = nullptr;
            return;
        }

        if (m_scopeSwrFreqDock)
            return;

        m_scopeSwrFreqDock = new QDockWidget(tr("Scope - SWR vs Frequency"), this);
        m_scopeSwrFreqDock->setObjectName(QStringLiteral("ScopeSwrVsFreqDock"));
        m_scopeSwrFreqDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        auto *root = new QWidget(m_scopeSwrFreqDock);
        auto *v = new QVBoxLayout(root);
        v->setContentsMargins(6, 6, 6, 6);
        v->setSpacing(6);

        // --- Controls row ---
        auto *row = new QHBoxLayout();
        row->setSpacing(8);

        // Current / Start / End (MHz)
        m_swrFreqCurrentMHz = new QDoubleSpinBox(root);
        m_swrFreqCurrentMHz->setRange(0.001, 1000.0);
        m_swrFreqCurrentMHz->setDecimals(3);
        m_swrFreqCurrentMHz->setSingleStep(0.001);
        m_swrFreqCurrentMHz->setSuffix(tr(" MHz"));
        m_swrFreqCurrentMHz->setValue(14.200);

        m_swrFreqStartMHz = new QDoubleSpinBox(root);
        m_swrFreqStartMHz->setRange(0.001, 1000.0);
        m_swrFreqStartMHz->setDecimals(3);
        m_swrFreqStartMHz->setSingleStep(0.001);
        m_swrFreqStartMHz->setSuffix(tr(" MHz"));
        m_swrFreqStartMHz->setValue(14.000);

        m_swrFreqEndMHz = new QDoubleSpinBox(root);
        m_swrFreqEndMHz->setRange(0.001, 1000.0);
        m_swrFreqEndMHz->setDecimals(3);
        m_swrFreqEndMHz->setSingleStep(0.001);
        m_swrFreqEndMHz->setSuffix(tr(" MHz"));
        m_swrFreqEndMHz->setValue(14.350);

        m_swrFreqNumPts = new QSpinBox(root);
        m_swrFreqNumPts->setRange(2, 1000);
        m_swrFreqNumPts->setValue(25);

        // Coupler selector
        m_swrFreqCouplerCombo = new QComboBox(root);
        // Populate typical Coupler 1..4 (you can enhance later using your model/config)
        m_swrFreqCouplerCombo->addItem(tr("Coupler 1"), 0);
        m_swrFreqCouplerCombo->addItem(tr("Coupler 2"), 1);
        m_swrFreqCouplerCombo->addItem(tr("Coupler 3"), 2);
        m_swrFreqCouplerCombo->addItem(tr("Coupler 4"), 3);
        m_swrFreqCouplerCombo->setCurrentIndex(0);

        // Units
        m_swrFreqUnitsCombo = new QComboBox(root);
        m_swrFreqUnitsCombo->addItem(tr("VSWR"), int(SwrVsFreqPlotWidget::UnitsMode::Vswr));
        m_swrFreqUnitsCombo->addItem(tr("Return Loss (dB)"),
                                     int(SwrVsFreqPlotWidget::UnitsMode::ReturnLossDb));
        m_swrFreqUnitsCombo->setCurrentIndex(0);

        // Smoothing
        m_swrFreqSmoothingCombo = new QComboBox(root);

        // Encode both "type" and parameter in the item data string
        m_swrFreqSmoothingCombo->addItem(tr("None"), QStringLiteral("none"));

        m_swrFreqSmoothingCombo->addItem(tr("SMA (3)"), QStringLiteral("sma:3"));
        m_swrFreqSmoothingCombo->addItem(tr("SMA (5)"), QStringLiteral("sma:5"));
        m_swrFreqSmoothingCombo->addItem(tr("SMA (9)"), QStringLiteral("sma:9"));

        m_swrFreqSmoothingCombo->addItem(tr("EMA (Fast)"), QStringLiteral("ema:0.50"));
        m_swrFreqSmoothingCombo->addItem(tr("EMA (Medium)"), QStringLiteral("ema:0.33"));
        m_swrFreqSmoothingCombo->addItem(tr("EMA (Slow)"), QStringLiteral("ema:0.15"));

        m_swrFreqSmoothingCombo->setCurrentIndex(m_swrFreqSmoothingCombo->findData(QStringLiteral("ema:0.33")));

        // Mode (Auto disabled in Phase 1)
        m_swrFreqModeCombo = new QComboBox(root);
        m_swrFreqModeCombo->addItem(tr("Manual"), 0);
        m_swrFreqModeCombo->addItem(tr("Auto (Hamlib)"), 1);
        m_swrFreqModeCombo->setCurrentIndex(0);

        // Buttons
        m_swrFreqBtnManualCapture = nullptr;                        // no longer used (single Start button)
        m_swrFreqBtnAutoStart = new QPushButton(tr("Start"), root); // single Start button (label updated by mode)
        m_swrFreqBtnAutoStop = new QPushButton(tr("Stop"), root);
        m_swrFreqBtnAutoStop->setEnabled(false);
        m_swrFreqBtnClear = new QPushButton(tr("Clear"), root);
        m_swrFreqBtnExport = new QPushButton(tr("Export as PNG"), root);

        m_swrFreqChkAutoScale = new QCheckBox(tr("Auto-scale"), root);
        m_swrFreqChkAutoScale->setChecked(true);

        // Row 1: actions (like other scopes)
        auto *row1 = new QHBoxLayout;
        row1->setContentsMargins(0, 0, 0, 0);

        m_swrFreqChkMeasurements = new QCheckBox(tr("Measures"), root);
        m_swrFreqChkMeasurements->setChecked(m_showScopeMeasurements);

        row1->addWidget(new QLabel(tr("Current:"), root));
        row1->addWidget(m_swrFreqCurrentMHz);

        row1->addSpacing(10);
        row1->addWidget(new QLabel(tr("Mode:"), root));
        row1->addWidget(m_swrFreqModeCombo);

        row1->addWidget(m_swrFreqBtnAutoStart); // single Start button
        row1->addWidget(m_swrFreqBtnAutoStop);  // Stop is only meaningful for Auto
        row1->addWidget(m_swrFreqBtnClear);

        row1->addSpacing(10);
        row1->addWidget(m_swrFreqChkMeasurements);
        row1->addSpacing(10);
        row1->addWidget(m_swrFreqChkAutoScale);

        row1->addSpacing(10);
        row1->addWidget(new QLabel(tr("Scale:"), root));
        row1->addWidget(m_swrFreqUnitsCombo);

        row1->addStretch(1);
        row1->addWidget(m_swrFreqBtnExport);

        v->addLayout(row1);

        // Row 2: parameters
        auto *row2 = new QHBoxLayout;
        row2->setContentsMargins(0, 0, 0, 0);

        row2->addWidget(new QLabel(tr("Start:"), root));
        row2->addWidget(m_swrFreqStartMHz);
        row2->addWidget(new QLabel(tr("End:"), root));
        row2->addWidget(m_swrFreqEndMHz);
        row2->addWidget(new QLabel(tr("Points:"), root));
        row2->addWidget(m_swrFreqNumPts);

        row2->addSpacing(10);
        row2->addWidget(new QLabel(tr("Coupler:"), root));
        row2->addWidget(m_swrFreqCouplerCombo);

        row2->addSpacing(10);
        row2->addWidget(new QLabel(tr("Smoothing:"), root));
        row2->addWidget(m_swrFreqSmoothingCombo);

        row2->addSpacing(10);
        row2->addWidget(new QLabel(tr("Settle (ms):"), root));
        m_swrFreqAutoSettleMs = new QSpinBox(root);
        m_swrFreqAutoSettleMs->setRange(10, 500);
        m_swrFreqAutoSettleMs->setValue(100);
        m_swrFreqAutoSettleMs->setSingleStep(10);
        row2->addWidget(m_swrFreqAutoSettleMs);

        row2->addWidget(new QLabel(tr("Timeout (ms):"), root));
        m_swrFreqAutoTimeoutMs = new QSpinBox(root);
        m_swrFreqAutoTimeoutMs->setRange(500, 10000);
        m_swrFreqAutoTimeoutMs->setValue(2000);
        m_swrFreqAutoTimeoutMs->setSingleStep(250);
        row2->addWidget(m_swrFreqAutoTimeoutMs);

        row2->addStretch(1);

        v->addLayout(row2);

        // --- Plot ---
        m_swrVsFreqPlot = new SwrVsFreqPlotWidget(root);
        m_swrVsFreqPlot->setTitle(tr("SWR vs Frequency"));
        // Match other scopes: measurements toggle is global
        m_swrVsFreqPlot->setShowMeasurements(m_showScopeMeasurements);

        // Default line-only (no dots)
        m_swrVsFreqPlot->setShowPoints(false);
        m_swrVsFreqPlot->setFixedXRangeHz(m_swrFreqStartMHz->value() * 1e6,
                                          m_swrFreqEndMHz->value() * 1e6);
        v->addWidget(m_swrVsFreqPlot, 1);

        root->setLayout(v);
        m_scopeSwrFreqDock->setWidget(root);
        addDockWidget(Qt::RightDockWidgetArea, m_scopeSwrFreqDock);
        m_scopeSwrFreqDock->show();

        // Disable Auto for now (Phase 1)
        m_swrFreqModeCombo->setToolTip(tr("Auto sweep requires Hamlib (Phase 2)."));
        // Keep it selectable, but prevent using it
        // We'll just gate the capture button:
        auto updateModeUi = [this]()
        {
        const bool isManual = (m_swrFreqModeCombo && m_swrFreqModeCombo->currentIndex() == 0);
        const bool isAuto = !isManual;

        // Single Start button: enabled whenever we're not actively sweeping
        if (m_swrFreqBtnAutoStart) {
            m_swrFreqBtnAutoStart->setEnabled(!m_swrAutoActive);
            m_swrFreqBtnAutoStart->setText(isManual ? tr("Manual Capture…") : tr("Start Auto"));
        }

        // Stop only applies to Auto sweeps
        if (m_swrFreqBtnAutoStop)
            m_swrFreqBtnAutoStop->setEnabled(isAuto && m_swrAutoActive);

        // Don't let user flip Mode mid-sweep
        if (m_swrFreqModeCombo)
            m_swrFreqModeCombo->setEnabled(!m_swrAutoActive);

        // prevent changing sweep params mid-sweep
        const bool lock = m_swrAutoActive;
        if (m_swrFreqStartMHz)
            m_swrFreqStartMHz->setEnabled(!lock);
        if (m_swrFreqEndMHz)
            m_swrFreqEndMHz->setEnabled(!lock);
        if (m_swrFreqNumPts)
            m_swrFreqNumPts->setEnabled(!lock);
        if (m_swrFreqCouplerCombo)
            m_swrFreqCouplerCombo->setEnabled(!lock); };

        updateModeUi();

        // Auto sweep timer (for read-back frequency verify loop)
        if (!m_swrAutoVerifyTimer)
        {
            m_swrAutoVerifyTimer = new QTimer(this);
            m_swrAutoVerifyTimer->setSingleShot(true);
            connect(m_swrAutoVerifyTimer, &QTimer::timeout, this, &MainWindow::swrAutoVerifyPoll);
        }

        connect(m_swrFreqModeCombo, &QComboBox::currentIndexChanged, this, [updateModeUi](int)
                { updateModeUi(); });

        // Units affect how we plot new points (and could repaint)
        connect(m_swrFreqUnitsCombo, &QComboBox::currentIndexChanged, this, [this](int)
                {
        if (!m_swrVsFreqPlot)
            return;
        auto mode = SwrVsFreqPlotWidget::UnitsMode(m_swrFreqUnitsCombo->currentData().toInt());
        m_swrVsFreqPlot->setUnitsMode(mode); });

        // Auto-scale
        connect(m_swrFreqChkAutoScale, &QCheckBox::toggled, this, [this](bool on)
                {
        if (!m_swrVsFreqPlot)
            return;
        m_swrVsFreqPlot->setAutoScaleY(on); });

        // Clear
        connect(m_swrFreqBtnClear, &QPushButton::clicked, this, [this]()
                {
        if (m_swrVsFreqPlot)
            m_swrVsFreqPlot->clearPoints(); });

        // Export PNG (same pattern as your other scopes)
        connect(m_swrFreqBtnExport, &QPushButton::clicked, this, [this]()
                {
        if (!m_swrVsFreqPlot)
            return;

        const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString base = QString("Scope_SWR_vs_Freq_%1.png").arg(ts);

        const QString startDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString startDirFinal = m_lastSwrFreqExportDir.isEmpty() ? startDir
                                                                       : m_lastSwrFreqExportDir;
        const QString defaultPath = QDir(startDirFinal).filePath(base);

        const QString filePath = QFileDialog::getSaveFileName(this,
                                                              tr("Export Scope Image"),
                                                              defaultPath,
                                                              tr("PNG Image (*.png)"));
        if (filePath.isEmpty())
            return;

        // Grab the widget and add a subtle icon watermark
        QPixmap pm = m_swrVsFreqPlot->grab();

        // Theme-aware watermark (dark logo on light theme, white logo on dark theme)
        const QString wmPath = m_isDarkTheme
                                   ? QStringLiteral(":/assets/icons/RfssLogo1024_659_white.png")
                                   : QStringLiteral(":/assets/icons/RfssLogo1024_659.png");

        QPixmap mark(wmPath);
        if (!mark.isNull()) {
            const int side = qBound(48, int(qMin(pm.width(), pm.height()) * 0.18), 180);
            mark = mark.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);

            QPainter painter(&pm);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            // Slightly different opacity feels better per theme (optional; tweak as you like)
            painter.setOpacity(m_isDarkTheme ? 0.12 : 0.12);

            const int padX = 15;
            const int padY = 30;
            const int x = qMax(0, pm.width() - mark.width() - padX);
            const int y = qMax(0, pm.height() - mark.height() - padY);
            painter.drawPixmap(x, y, mark);
        }

        pm.save(filePath, "PNG");

        m_lastSwrFreqExportDir = QFileInfo(filePath).absolutePath(); });

        // Manual Capture button is now single Start button for both modes
        auto startManualSweepFromUi = [this]()
        {
            if (!m_swrFreqStartMHz || !m_swrFreqEndMHz || !m_swrFreqNumPts)
                return;

            m_swrManualCouplerIndex = m_swrFreqCouplerCombo
                                          ? m_swrFreqCouplerCombo->currentData().toInt()
                                          : 0;

            const double startHz = m_swrFreqStartMHz->value() * 1e6;
            const double endHz = m_swrFreqEndMHz->value() * 1e6;
            const int n = m_swrFreqNumPts->value();

            if (n < 2)
                return;

            // Build evenly spaced steps (inclusive)
            m_swrManualStepFreqHz.clear();
            m_swrManualStepFreqHz.reserve(n);

            const double step = (endHz - startHz) / double(n - 1);
            for (int i = 0; i < n; ++i)
                m_swrManualStepFreqHz.append(startHz + step * double(i));

            m_swrManualStepIndex = 0;
            if (m_swrVsFreqPlot)
                m_swrVsFreqPlot->clearPoints();

            startSwrManualDialog(m_swrManualStepFreqHz[m_swrManualStepIndex]);
        };

        // Keep dock + action in sync when user closes the dock
        connect(m_scopeSwrFreqDock, &QDockWidget::visibilityChanged, this, [this](bool vis)
                {
        if (!vis) {
            // find your QAction and uncheck it (pattern similar to your other docks)
            if (ui && ui->actionScopeSWR_vs_Frequency)
                ui->actionScopeSWR_vs_Frequency->setChecked(false);
        } });

        auto updateXRange = [this]()
        {
            if (!m_swrVsFreqPlot || !m_swrFreqStartMHz || !m_swrFreqEndMHz)
                return;
            m_swrVsFreqPlot->setFixedXRangeHz(m_swrFreqStartMHz->value() * 1e6,
                                              m_swrFreqEndMHz->value() * 1e6);
        };

        connect(m_swrFreqStartMHz,
                QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this,
                [updateXRange](double)
                { updateXRange(); });
        connect(m_swrFreqEndMHz,
                QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this,
                [updateXRange](double)
                { updateXRange(); });

        connect(m_swrFreqChkMeasurements, &QCheckBox::toggled, this, [this](bool on)
                { applyScopeMeasurementsOverlay(on); });

        // Single Start button handles both modes
        connect(m_swrFreqBtnAutoStart, &QPushButton::clicked, this, [this, startManualSweepFromUi]()
                {
        const bool isManual = (m_swrFreqModeCombo && m_swrFreqModeCombo->currentIndex() == 0);

        if (isManual) {
            startManualSweepFromUi();
        } else {
            startSwrAutoSweep();
        } });

        connect(m_swrFreqBtnAutoStop, &QPushButton::clicked, this, [this]()
                { stopSwrAutoSweep(true, tr("Stopped by user")); });

        connect(m_scopeSwrFreqDock, &QDockWidget::visibilityChanged, this, [this](bool vis)
                {
        if (!vis && m_swrAutoActive) {
            stopSwrAutoSweep(true, tr("Scope closed"));
        } });

        // RigCtld client for SWR vs Freq Auto mode (shared with FreqWidgets)
        ensureRigCtldClient();

        // Smoothing
        /*    connect(m_swrFreqSmoothingCombo, &QComboBox::currentIndexChanged, this, [this](int) {
                if (!m_swrVsFreqPlot || !m_swrFreqSmoothingCombo)
                    return;
                auto mode = SwrVsFreqPlotWidget::SmoothingMode(m_swrFreqSmoothingCombo->currentData().toInt());
                m_swrVsFreqPlot->setSmoothingMode(mode);
            });

            // apply default immediately
            if (m_swrVsFreqPlot && m_swrFreqSmoothingCombo) {
                auto mode = SwrVsFreqPlotWidget::SmoothingMode(m_swrFreqSmoothingCombo->currentData().toInt());
                m_swrVsFreqPlot->setSmoothingMode(mode);
            }
        */

        auto applySwrSmoothingFromUi = [this]()
        {
            if (!m_swrVsFreqPlot || !m_swrFreqSmoothingCombo)
                return;

            const QString key = m_swrFreqSmoothingCombo->currentData().toString();

            if (key == QLatin1String("none"))
            {
                m_swrVsFreqPlot->setSmoothingMode(SwrVsFreqPlotWidget::SmoothingMode::None);
                return;
            }

            if (key.startsWith(QLatin1String("sma:")))
            {
                const int w = key.mid(4).toInt();
                m_swrVsFreqPlot->setSmoothingMode(SwrVsFreqPlotWidget::SmoothingMode::Sma);
                m_swrVsFreqPlot->setSmaWindow(w);
                return;
            }

            if (key.startsWith(QLatin1String("ema:")))
            {
                const double a = key.mid(4).toDouble();
                m_swrVsFreqPlot->setSmoothingMode(SwrVsFreqPlotWidget::SmoothingMode::Ema);
                m_swrVsFreqPlot->setEmaAlpha(a);
                return;
            }

            // Fallback
            m_swrVsFreqPlot->setSmoothingMode(SwrVsFreqPlotWidget::SmoothingMode::Ema);
        };

        connect(m_swrFreqSmoothingCombo, &QComboBox::currentIndexChanged, this, [applySwrSmoothingFromUi](int)
                { applySwrSmoothingFromUi(); });

        // Apply default immediately
        applySwrSmoothingFromUi();

        // (Optional) if you want to update "Current:" from telemetry later, keep a connection handle.
        // For now, we only need telemetry for the manual buffer in your appendSwrManualSampleIfArmed()
    }

    void MainWindow::appendSwrManualSampleIfArmed(const RayTelemetry &t)
    {
        if (!m_swrManualArmed)
            return;

        // Auto-sweep-only gating knobs
        constexpr int kAutoIgnoreFirstMs = 30; // flush stale telemetry after arm
        constexpr double kAutoMinFwdW = 0.5;   // require real RF before accepting SWR

        // During Auto sweep, ignore the first few ms after arming capture
        // to avoid mixing in stale frames from the previous step.
        if (m_swrAutoActive)
        {
            if (m_swrAutoWaitTimer.isValid() && m_swrAutoWaitTimer.elapsed() < kAutoIgnoreFirstMs)
                return;
        }

        // Find selected coupler sample in telemetry
        double swr = 0.0;
        double fwdW = 0.0;
        bool found = false;

        for (const auto &c : t.couplers)
        {
            if (c.index == m_swrManualCouplerIndex)
            {
                swr = c.swr;
                fwdW = c.forwardWatts;
                found = true;
                break;
            }
        }
        if (!found)
            return;

        // Auto sweep: reject frames where forward power isn't meaningful/stable
        if (m_swrAutoActive)
        {
            if (!std::isfinite(fwdW) || fwdW < kAutoMinFwdW)
                return;
        }

        // Basic sanity: SWR should be >= 1.0
        if (!(swr >= 1.0) || !std::isfinite(swr))
            return;

        // Rolling buffer of last N samples (N = m_swrManualRequiredSamples)
        m_swrManualSwrBuf.append(swr);
        while (m_swrManualSwrBuf.size() > m_swrManualRequiredSamples)
            m_swrManualSwrBuf.removeFirst();
    }

    void MainWindow::startSwrManualDialog(double targetFreqHz)
    {
        cleanupSwrManualDialogUiOnly(); // safety (don't wipe sweep state)

        m_swrManualTargetFreqHz = targetFreqHz;
        m_swrManualSwrBuf.clear();
        m_swrManualArmed = true;

        m_swrManualDialog = new QDialog(this);
        m_swrManualDialog->setWindowTitle(tr("Manual SWR Capture"));
        m_swrManualDialog->setModal(true);

        auto *layout = new QVBoxLayout(m_swrManualDialog);

        m_swrManualInstructionLabel = new QLabel(m_swrManualDialog);
        m_swrManualInstructionLabel->setWordWrap(true);
        layout->addWidget(m_swrManualInstructionLabel);

        // Samples label (this was missing -> caused Capture never to enable)
        m_swrManualStatusLabel = new QLabel(tr("Samples: 0/%1").arg(m_swrManualRequiredSamples),
                                            m_swrManualDialog);
        layout->addWidget(m_swrManualStatusLabel);

        updateSwrManualDialogText();

        auto *btnRow = new QHBoxLayout();
        btnRow->addStretch();

        m_swrManualCaptureBtn = new QPushButton(tr("Capture"), m_swrManualDialog);
        m_swrManualCaptureBtn->setEnabled(false);
        btnRow->addWidget(m_swrManualCaptureBtn);

        auto *abortBtn = new QPushButton(tr("Abort"), m_swrManualDialog);
        btnRow->addWidget(abortBtn);

        layout->addLayout(btnRow);

        connect(m_swrManualCaptureBtn, &QPushButton::clicked, this, &MainWindow::onSwrManualCaptureNow);
        connect(abortBtn, &QPushButton::clicked, m_swrManualDialog, &QDialog::reject);

        // Timer that enables Capture only when we have 3 samples
        m_swrManualCaptureTimer = new QTimer(this);
        m_swrManualCaptureTimer->setInterval(100);
        connect(m_swrManualCaptureTimer,
                &QTimer::timeout,
                this,
                &MainWindow::onSwrManualCaptureTimerTick);
        m_swrManualCaptureTimer->start();

        // When dialog closes, cleanup
        connect(m_swrManualDialog, &QDialog::finished, this, [this](int)
                { stopSwrManualDialog(); });

        m_swrManualDialog->show();
    }

    void MainWindow::onSwrManualCaptureTimerTick()
    {
        if (!m_swrManualArmed || !m_swrManualCaptureBtn)
            return;

        const int have = m_swrManualSwrBuf.size();
        if (m_swrManualStatusLabel)
            m_swrManualStatusLabel->setText(
                tr("Samples: %1/%2").arg(have).arg(m_swrManualRequiredSamples));

        const bool ready = (have >= m_swrManualRequiredSamples);
        m_swrManualCaptureBtn->setEnabled(ready);

        if (ready)
        {
            m_swrManualStatusLabel->setText(
                tr("Samples: %1/%2 (ready)").arg(have).arg(m_swrManualRequiredSamples));
        }
    }

    void MainWindow::stopSwrManualDialog()
    {
        m_swrManualArmed = false;
        m_swrManualSwrBuf.clear();

        m_swrManualStepFreqHz.clear();
        m_swrManualStepIndex = -1;
        m_swrManualInstructionLabel = nullptr;

        if (m_swrManualCaptureTimer)
        {
            m_swrManualCaptureTimer->stop();
            m_swrManualCaptureTimer->deleteLater();
            m_swrManualCaptureTimer = nullptr;
        }

        // dialog deletes itself by parent ownership; just null our pointers
        m_swrManualDialog = nullptr;
        m_swrManualStatusLabel = nullptr;
        m_swrManualCaptureBtn = nullptr;
    }

    void MainWindow::onSwrManualCaptureNow()
    {
        if (m_swrManualSwrBuf.size() < m_swrManualRequiredSamples)
            return;

        // Compute robust SWR using trimmed mean to reduce jitter/outliers
        const int n = m_swrManualSwrBuf.size();

        // Use configured trim (e.g. 12 samples, trim 3)
        int trim = m_swrTrimCount;
        if (trim < 0)
            trim = 0;
        if (trim > (n - 1) / 2)
            trim = (n - 1) / 2;

        const double avgSwr = trimmedMean(m_swrManualSwrBuf, trim);

        // Convert if needed
        double y = avgSwr;

        if (m_swrFreqUnitsCombo)
        {
            const auto mode = SwrVsFreqPlotWidget::UnitsMode(m_swrFreqUnitsCombo->currentData().toInt());
            if (mode == SwrVsFreqPlotWidget::UnitsMode::ReturnLossDb)
            {
                // |Γ| = (SWR-1)/(SWR+1), RL(dB) = -20 log10(|Γ|)
                const double swr = std::max(1.0, avgSwr);
                const double gamma = (swr - 1.0) / (swr + 1.0);
                const double g = std::max(1e-9, std::abs(gamma));
                y = -20.0 * std::log10(g);
            }
        }

        if (m_swrVsFreqPlot)
        {
            m_swrVsFreqPlot->addPoint(m_swrManualTargetFreqHz, y);
        }

        // Manual sweep session: advance to next frequency step
        if (m_swrManualStepIndex >= 0 && !m_swrManualStepFreqHz.isEmpty())
        {
            m_swrManualStepIndex++;

            if (m_swrManualStepIndex < m_swrManualStepFreqHz.size())
            {
                // next target
                m_swrManualTargetFreqHz = m_swrManualStepFreqHz[m_swrManualStepIndex];

                // clear buffer so we require 3 *fresh* samples for next point
                m_swrManualSwrBuf.clear();

                // update UI
                updateSwrManualDialogText();
                if (m_swrManualCaptureBtn)
                    m_swrManualCaptureBtn->setEnabled(false);
                if (m_swrManualStatusLabel)
                    m_swrManualStatusLabel->setText(tr("Samples: 0/%1").arg(m_swrManualRequiredSamples));

                return; // keep dialog open
            }

            // done
            if (m_swrManualDialog)
                m_swrManualDialog->accept();
            return;
        }

        // fallback (single-shot)
        if (m_swrManualDialog)
            m_swrManualDialog->accept();
    }

    void MainWindow::updateSwrManualDialogText()
    {
        if (!m_swrManualInstructionLabel)
            return;

        const double mhz = m_swrManualTargetFreqHz / 1e6;

        QString stepInfo;
        if (m_swrManualStepIndex >= 0 && !m_swrManualStepFreqHz.isEmpty())
        {
            stepInfo = tr(" (%1 of %2)").arg(m_swrManualStepIndex + 1).arg(m_swrManualStepFreqHz.size());
        }

        m_swrManualInstructionLabel->setText(
            tr("Tune to <b>%1 MHz</b>%2 and transmit a carrier.\nWaiting for telemetry…")
                .arg(QString::number(mhz, 'f', 3))
                .arg(stepInfo));
    }

    double MainWindow::trimmedMean(const QVector<double> &values, int trimCount) const
    {
        const int n = values.size();
        if (n == 0)
            return 0.0;

        int trim = trimCount;
        if (trim < 0)
            trim = 0;
        if (trim > (n - 1) / 2)
            trim = (n - 1) / 2;

        // If trimming removes everything (or no trimming), fall back to simple mean.
        if (trim == 0 || (n - 2 * trim) <= 0)
        {
            double sum = 0.0;
            for (double v : values)
                sum += v;
            return sum / double(n);
        }

        QVector<double> sorted = values;
        std::sort(sorted.begin(), sorted.end());

        double sum = 0.0;
        for (int i = trim; i < (n - trim); ++i)
            sum += sorted[i];

        return sum / double(n - 2 * trim);
    }

    bool MainWindow::getRigctldEndpoint(QString &host, int &port) const
    {
        // 0 = Serial (local rigctld using CAT port settings)
        // 1 = Network (connect to an already-running rigctld at host:port)
        if (m_settings.hamlib.connectionModeIndex == 0)
        {
            host = QStringLiteral("127.0.0.1");
            port = m_settings.hamlib.serial.tcpPort;
        }
        else
        {
            host = m_settings.hamlib.network.host;
            port = m_settings.hamlib.network.port;
        }

        host = host.trimmed();
        if (host.isEmpty())
            host = QStringLiteral("127.0.0.1");

        if (port <= 0)
            port = 4532;

        return true;
    }

    void MainWindow::cleanupSwrManualDialogUiOnly()
    {
        m_swrManualArmed = false;
        m_swrManualSwrBuf.clear();

        // IMPORTANT: do NOT clear m_swrManualStepFreqHz / m_swrManualStepIndex here

        m_swrManualInstructionLabel = nullptr;

        if (m_swrManualCaptureTimer)
        {
            m_swrManualCaptureTimer->stop();
            m_swrManualCaptureTimer->deleteLater();
            m_swrManualCaptureTimer = nullptr;
        }

        m_swrManualDialog = nullptr;
        m_swrManualStatusLabel = nullptr;
        m_swrManualCaptureBtn = nullptr;
    }

    double MainWindow::swrVsFreqUnitsYFromSwr(double swr) const
    {
        if (swr < 1.0)
            swr = 1.0;

        // Gamma = (SWR-1)/(SWR+1)
        const double gamma = (swr - 1.0) / (swr + 1.0);

        const auto mode = SwrVsFreqPlotWidget::UnitsMode(m_swrFreqUnitsCombo->currentData().toInt());
        if (mode == SwrVsFreqPlotWidget::UnitsMode::Vswr)
            return swr;

        // Professional Return Loss (negative): RL_dB = 20*log10(|Γ|)
        // SWR=1 => Γ=0 => RL=-inf (floor it)
        if (gamma <= 0.0)
            return SWR_VS_FREQ_RL_DB_FLOOR;

        double rlDb = 20.0 * std::log10(gamma); // negative (since 0<gamma<1)

        // Clamp to sane bounds: [floor .. 0]
        if (rlDb < SWR_VS_FREQ_RL_DB_FLOOR)
            rlDb = SWR_VS_FREQ_RL_DB_FLOOR;
        if (rlDb > 0.0)
            rlDb = 0.0;

        return rlDb;
    }

    quint64 MainWindow::rigSend(const QString &cmd, std::function<void(bool, const QStringList &, int)> cb)
    {
        const quint64 cmdId = m_rigCtld->sendCommand(cmd);
        m_rigPending.insert(cmdId, std::move(cb));
        return cmdId;
    }

    quint64 MainWindow::rigSend(const QString &cmd, int timeoutMs, std::function<void(bool, const QStringList &, int)> cb)
    {
        const quint64 cmdId = m_rigCtld->sendCommand(cmd, timeoutMs);
        m_rigPending.insert(cmdId, std::move(cb));
        return cmdId;
    }

    void MainWindow::ensureRigCtldClient()
    {
        // Create client once
        if (!m_rigCtld)
        {
            // Shared rigctld client (used by SWR-vs-Freq, FreqWidgets, AmplifierAnalyzer)
            m_rigCtld = new raypm::RigCtldClient(this);

            connect(m_rigCtld, &raypm::RigCtldClient::commandFinished,
                    this, &MainWindow::onRigCommandFinished);

            connect(m_rigCtld, &raypm::RigCtldClient::errorOccurred, this,
                    [this](const QString &msg)
                    {
                        qWarning() << "[rigctld] error:" << msg;

                        // During Auto-SWR STARTUP (preflight), rigctld may not be running yet.
                        // Let the preflight logic decide what to do.
                        const bool isPreflight =
                            m_swrAutoActive &&
                            (m_swrAutoStepIndex < 0) &&
                            (!m_swrAutoCaptureTimer || !m_swrAutoCaptureTimer->isActive());

                        if (isPreflight)
                            return;

                        if (m_swrAutoActive) {
                            stopSwrAutoSweep(false, tr("rigctld error: %1").arg(msg));
                        }
                    });

            connect(m_rigCtld, &raypm::RigCtldClient::connectedChanged, this,
                    [this](bool ok)
                    {
                        if (ok)
                            return;

                        const bool isPreflight =
                            m_swrAutoActive &&
                            (m_swrAutoStepIndex < 0) &&
                            (!m_swrAutoCaptureTimer || !m_swrAutoCaptureTimer->isActive());

                        if (isPreflight)
                            return;

                        if (m_swrAutoActive) {
                            stopSwrAutoSweep(false, tr("rigctld disconnected"));
                        }
                    });
        }

        // ---- Auto-start rigctld if enabled (this is the missing piece for Amplifier Analyzer) ----
        const bool autoStartEnabled =
            m_settings.hamlib.enabled &&
            m_settings.hamlib.lastTestOk &&
            m_settings.hamlib.serial.autoStartRigctld;

        if (autoStartEnabled)
        {
            // Only auto-start rigctld when we will be connecting locally (Serial mode or localhost Network)
            QString host;
            int port = 0;
            getRigctldEndpoint(host, port);

            const bool localHost = isLocalHostName(host);

            if (localHost)
            {
                QString err;
                if (!startRigctldFromSettings(&err))
                {
                    if (!err.isEmpty())
                        qWarning() << "[Hamlib] auto-start rigctld failed:" << err;
                }
            }
        }

        // ---- Ensure the client is pointed at the correct endpoint ----
        if (m_settings.hamlib.enabled && m_settings.hamlib.lastTestOk && m_rigCtld && !m_rigCtld->isConnected())
        {
            QString host;
            int port = 0;
            getRigctldEndpoint(host, port);

            qInfo() << "[Hamlib] ensureRigCtldClient connectToHost" << host << port;
            m_rigCtld->connectToHost(host, port);
        }
    }

    QString MainWindow::hamlibExePath(const QString &exeName) const
    {
        const QString bin = m_settings.hamlib.binPath.trimmed();
        if (!bin.isEmpty())
        {
            const QString candidate = QDir(bin).filePath(exeName);
            if (QFileInfo::exists(candidate))
            {
                return QDir::toNativeSeparators(candidate);
            }
        }
        return QStandardPaths::findExecutable(exeName);
    }

    bool MainWindow::startRigctldForSerialFromSettings(QString *errorOut)
    {
#if defined(Q_OS_WIN)
        const QString rigctld = hamlibExePath(QStringLiteral("rigctld.exe"));
#else
        const QString rigctld = hamlibExePath(QStringLiteral("rigctld"));
#endif
        if (rigctld.isEmpty())
        {
            if (errorOut)
                *errorOut = tr("rigctld not found. Install Hamlib or set Setup → Hamlib → Bin Path.");
            return false;
        }

        const int rigId = m_settings.hamlib.serial.rigModelId;
        if (rigId < 0)
        {
            if (errorOut)
                *errorOut = tr("Rig model not selected. Configure Setup → Hamlib → Serial and run Test.");
            return false;
        }

        const QString port = m_settings.hamlib.serial.portName.trimmed();
        if (port.isEmpty())
        {
            if (errorOut)
                *errorOut = tr("Serial port not selected. Configure Setup → Hamlib → Serial and run Test.");
            return false;
        }

        const int baud = m_settings.hamlib.serial.baudRate;
        const int tcpPort = (m_settings.hamlib.serial.tcpPort > 0) ? m_settings.hamlib.serial.tcpPort : 4532;

        if (!m_rigctldProcess)
        {
            m_rigctldProcess = new QProcess(this);
            connect(m_rigctldProcess, &QProcess::finished, this, [this](int, QProcess::ExitStatus)
                    { qInfo() << "[Hamlib] rigctld exited"; });
        }

        if (m_rigctldProcess->state() != QProcess::NotRunning)
        {
            return true; // already running
        }

        QStringList args;
        args << "-m" << QString::number(rigId)
             << "-r" << port
             << "-s" << QString::number(baud)
             << "-t" << QString::number(tcpPort);

        m_rigctldStartedByApp = true;
        m_rigctldProcess->start(rigctld, args);

        if (!m_rigctldProcess->waitForStarted(1200))
        {
            if (errorOut)
                *errorOut = tr("Failed to start rigctld: %1").arg(m_rigctldProcess->errorString());
            return false;
        }

        QThread::msleep(150); // let it bind/listen
        return true;
    }

    bool MainWindow::isLocalHostName(const QString &host) const
    {
        const QString h = host.trimmed().toLower();
        return h.isEmpty() || h == QStringLiteral("127.0.0.1") || h == QStringLiteral("localhost") || h == QStringLiteral("::1");
    }

    bool MainWindow::startRigctldFromSettings(QString *errorOut)
    {
    #if defined(Q_OS_WIN)
        const QString rigctldExe = hamlibExePath(QStringLiteral("rigctld.exe"));
    #else
        const QString rigctldExe = hamlibExePath(QStringLiteral("rigctld"));
    #endif

        if (rigctldExe.isEmpty())
        {
            if (errorOut)
                *errorOut = tr("rigctld not found. Install Hamlib and/or set Setup → Hamlib → Bin Path.");
            return false;
        }

        const int rigId = m_settings.hamlib.serial.rigModelId;
        if (rigId < 0)
        {
            if (errorOut)
                *errorOut = tr("Rig model not selected. Configure Setup → Hamlib → Serial and run Test.");
            return false;
        }

        const QString serialPort = m_settings.hamlib.serial.portName.trimmed();
        if (serialPort.isEmpty())
        {
            if (errorOut)
                *errorOut = tr("Serial port not selected. Configure Setup → Hamlib → Serial and run Test.");
            return false;
        }

        const int baud = m_settings.hamlib.serial.baudRate;
        const int tcpPort = (m_settings.hamlib.serial.tcpPort > 0) ? m_settings.hamlib.serial.tcpPort : 4532;

        if (!m_rigctldProcess)
        {
            m_rigctldProcess = new QProcess(this);
            connect(m_rigctldProcess,
                    qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                    this,
                    [this](int, QProcess::ExitStatus)
                    {
                        if (m_rigctldStartedByApp)
                            qInfo() << "[Hamlib] rigctld exited";
                    });
        }

        // IMPORTANT: don’t relaunch if already running
        if (m_rigctldProcess->state() != QProcess::NotRunning)
            return true;

        QStringList args;
        args << "-m" << QString::number(rigId)
             << "-r" << serialPort
             << "-s" << QString::number(baud)
             << "-t" << QString::number(tcpPort);

        m_rigctldStartedByApp = true;
        m_rigctldProcess->start(rigctldExe, args);

        if (!m_rigctldProcess->waitForStarted(1200))
        {
            if (errorOut)
                *errorOut = tr("Failed to start rigctld: %1").arg(m_rigctldProcess->errorString());
            return false;
        }

        QThread::msleep(150);
        return true;
    }

    void MainWindow::startSwrAutoSweep()
    {
        if (!m_swrFreqStartMHz || !m_swrFreqEndMHz || !m_swrFreqNumPts)
            return;

        if (m_swrAutoActive)
        {
            // Ignore double-clicks; user can Stop first.
            return;
        }

        // Gate on setup (but do NOT require opening Setup again)
        if (!m_settings.hamlib.enabled || !m_settings.hamlib.lastTestOk)
        {
            QMessageBox::warning(
                this,
                tr("Hamlib not configured"),
                tr("Auto sweep requires a successful Hamlib Test in Setup → Hamlib.\n\n"
                   "Open Setup and press Test first."));
            return;
        }

        if (!m_rigCtld)
            return;

        // Resolve endpoint based on persisted connectionModeIndex (0=Serial, 1=Network)
        QString host;
        int port = 0;
        getRigctldEndpoint(host, port);

        qInfo() << "[Hamlib] Using rigctld endpoint" << host << port
                << "modeIndex=" << m_settings.hamlib.connectionModeIndex
                << "autoStart=" << m_settings.hamlib.serial.autoStartRigctld
                << "serialTcpPort=" << m_settings.hamlib.serial.tcpPort
                << "network=" << m_settings.hamlib.network.host << m_settings.hamlib.network.port;

        const int n = m_swrFreqNumPts->value();
        if (n < 2)
            return;

        // Build evenly spaced steps (inclusive)
        const double startHz = m_swrFreqStartMHz->value() * 1e6;
        const double endHz = m_swrFreqEndMHz->value() * 1e6;

        // Plot steps: keep the true band edges for the X axis
        m_swrAutoStepFreqHz.clear();
        m_swrAutoStepFreqHz.reserve(n);

        const double plotStep = (endHz - startHz) / double(n - 1);
        for (int i = 0; i < n; ++i)
            m_swrAutoStepFreqHz.append(startHz + plotStep * double(i));

        // Rig steps: avoid band edges by shifting start/end inward by delta
        double edgeDeltaHz = m_swrAutoEdgeDeltaHz;
        const double spanHz = std::fabs(endHz - startHz);
        if (spanHz <= 2.0 * edgeDeltaHz)
        {
            // If the requested span is tiny, shrink delta to 2% of span (still “inside”)
            edgeDeltaHz = std::max(0.0, spanHz * 0.02);
        }

        const double rigStartHz = startHz + edgeDeltaHz;
        const double rigEndHz = endHz - edgeDeltaHz;

        m_swrAutoRigStepFreqHz.clear();
        m_swrAutoRigStepFreqHz.reserve(n);

        const double rigStep = (rigEndHz - rigStartHz) / double(n - 1);
        for (int i = 0; i < n; ++i)
            m_swrAutoRigStepFreqHz.append(rigStartHz + rigStep * double(i));

        m_swrManualCouplerIndex = m_swrFreqCouplerCombo ? m_swrFreqCouplerCombo->currentData().toInt() : 0;

        m_swrAutoStepIndex = -1;
        m_swrAutoOriginalFreqHz = std::numeric_limits<double>::quiet_NaN();

        if (m_swrVsFreqPlot)
            m_swrVsFreqPlot->clearPoints();

        if (!m_swrAutoCaptureTimer)
        {
            m_swrAutoCaptureTimer = new QTimer(this);
            m_swrAutoCaptureTimer->setInterval(50);
            connect(m_swrAutoCaptureTimer, &QTimer::timeout, this, &MainWindow::onSwrAutoCapturePoll);
        }

        // Mark active immediately so Stop works and retries can be canceled.
        m_swrAutoActive = true;

        // Helper: start rigctld detached using Serial settings (same minimal args as Setup Start)
        auto startRigctldDetachedForSerial = [&](QString *errorOut) -> bool
        {
#ifdef Q_OS_WIN
            const QString rigctldExe = hamlibExePath(QStringLiteral("rigctld.exe"));
#else
            const QString rigctldExe = hamlibExePath(QStringLiteral("rigctld"));
#endif
            if (rigctldExe.isEmpty())
            {
                if (errorOut)
                    *errorOut = tr("rigctld not found. Set Hamlib bin folder or add it to PATH.");
                return false;
            }

            const int rigId = m_settings.hamlib.serial.rigModelId;
            if (rigId < 0)
            {
                if (errorOut)
                    *errorOut = tr("Hamlib Serial settings are missing Rig Model ID. Open Setup → Hamlib → Serial and select a rig model.");
                return false;
            }

            const QString portName = m_settings.hamlib.serial.portName.trimmed();
            if (portName.isEmpty())
            {
                if (errorOut)
                    *errorOut = tr("Hamlib Serial settings are missing the serial port (COMx). Open Setup → Hamlib → Serial.");
                return false;
            }

            const int baud = m_settings.hamlib.serial.baudRate;
            const int tcpPort = (m_settings.hamlib.serial.tcpPort > 0) ? m_settings.hamlib.serial.tcpPort : 4532;

            QStringList args;
            args << "-m" << QString::number(rigId)
                 << "-r" << portName
                 << "-s" << QString::number(baud)
                 << "-t" << QString::number(tcpPort);

            const bool ok = QProcess::startDetached(rigctldExe, args);
            if (!ok)
            {
                if (errorOut)
                    *errorOut = tr("Failed to start rigctld.");
                return false;
            }
            return true;
        };

        // Connection/prompt/retry loop (important: don’t enqueue rig commands until connected)
        auto prompted = std::make_shared<bool>(false);
        auto deadlineMs = std::make_shared<qint64>(QDateTime::currentMSecsSinceEpoch() + 2000);
        auto tryConnect = std::make_shared<std::function<void()>>();

        *tryConnect = [this, host, port, prompted, deadlineMs, tryConnect]() mutable
        {
            if (!m_swrAutoActive)
                return;

            if (!m_rigCtld)
                return;

            if (m_rigCtld->isConnected())
            {
                // Now safe to query current frequency for restore
                rigSend(QStringLiteral("f"), [this, host, port](bool ok, const QStringList &lines, int /*rprt*/)
                        {
                if (!m_swrAutoActive)
                    return;

                if (!ok || lines.isEmpty()) {
                    stopSwrAutoSweep(false, tr("rigctld connected but did not return a frequency."));
                    return;
                }

                bool parseOk = false;
                const double hz = lines.first().trimmed().toDouble(&parseOk);
                if (parseOk && hz > 0.0) {
                    m_swrAutoOriginalFreqHz = hz;
                    if (m_swrFreqCurrentMHz) {
                        QSignalBlocker b(*m_swrFreqCurrentMHz);
                        m_swrFreqCurrentMHz->setValue(hz / 1e6);
                    }
                }

                // Start stepping
                m_swrAutoStepIndex = -1;
                swrAutoSetNextFrequency();

                statusBar()->showMessage(tr("Auto SWR sweep started (rigctld %1:%2).").arg(host).arg(port)); });
                return;
            }

            // Not connected yet: attempt connect + retry until deadline
            m_rigCtld->connectToHost(host, port);

            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now >= *deadlineMs)
            {
                const bool localHost = isLocalHostName(host);

                // If we're connecting to localhost, we CAN help the user by starting rigctld,
                // even if they selected "Network" mode.
                if (localHost && !*prompted)
                {
                    *prompted = true;

                    // Auto-start path (no prompt)
                    if (m_settings.hamlib.serial.autoStartRigctld)
                    {
                        QString err;
                        if (!startRigctldFromSettings(&err))
                        {
                            stopSwrAutoSweep(false, err);
                            return;
                        }

                        *deadlineMs = QDateTime::currentMSecsSinceEpoch() + 3500;
                        QTimer::singleShot(250, this, [tryConnect]()
                                           { (*tryConnect)(); });
                        return;
                    }

                    // Prompt path
                    QMessageBox mb(this);
                    mb.setIcon(QMessageBox::Question);
                    mb.setWindowTitle(tr("Hamlib rigctld not running"));
                    mb.setText(tr("Auto sweep requires rigctld to be running. Start it now?"));
                    mb.setInformativeText(tr("This will start rigctld using your Setup → Hamlib → Serial settings."));
                    QPushButton *btnStart = mb.addButton(tr("Start rigctld"), QMessageBox::AcceptRole);
                    mb.addButton(QMessageBox::Cancel);
                    mb.exec();

                    if (mb.clickedButton() != btnStart)
                    {
                        stopSwrAutoSweep(false, tr("Canceled by user."));
                        return;
                    }

                    QString err;
                    if (!startRigctldFromSettings(&err))
                    {
                        stopSwrAutoSweep(false, err);
                        return;
                    }

                    *deadlineMs = QDateTime::currentMSecsSinceEpoch() + 3500;
                    QTimer::singleShot(250, this, [tryConnect]()
                                       { (*tryConnect)(); });
                    return;
                }

                // Remote host or already prompted once: stop with a clear error
                stopSwrAutoSweep(false,
                                 tr("Could not connect to rigctld at %1:%2.").arg(host).arg(port));
                return;
            }
            // Retry after delay
            QTimer::singleShot(200, this, [tryConnect]()
                               { (*tryConnect)(); });
        };

        // Kick off connect loop
        (*tryConnect)();

        // Force Auto mode selection if needed and refresh gating UI
        if (m_swrFreqModeCombo)
        {
            if (m_swrFreqModeCombo->currentIndex() == 0)
                m_swrFreqModeCombo->setCurrentIndex(1);
        }
        if (m_swrFreqModeCombo)
            emit m_swrFreqModeCombo->currentIndexChanged(m_swrFreqModeCombo->currentIndex());
    }

    void MainWindow::swrAutoSetNextFrequency()
    {
        if (!m_swrAutoActive || !m_rigCtld)
            return;

        ++m_swrAutoStepIndex;

        if (m_swrAutoStepIndex >= m_swrAutoStepFreqHz.size())
        {
            stopSwrAutoSweep(true, tr("Auto sweep complete"));
            return;
        }

        const double plotHz = m_swrAutoStepFreqHz.at(m_swrAutoStepIndex);
        const double rigHz = (m_swrAutoRigStepFreqHz.size() == m_swrAutoStepFreqHz.size())
                                 ? m_swrAutoRigStepFreqHz.at(m_swrAutoStepIndex)
                                 : plotHz;

        // Update UI “current MHz” to what we actually command the rig to
        if (m_swrFreqCurrentMHz)
        {
            QSignalBlocker b(*m_swrFreqCurrentMHz);
            m_swrFreqCurrentMHz->setValue(rigHz / 1e6);
        }

        // Send rigctld set frequency
        const QString cmd = QStringLiteral("F %1").arg(QString::number((quint64)std::llround(rigHz)));

        rigSend(cmd, [this, plotHz, rigHz](bool ok, const QStringList &, int)
                {
        if (!m_swrAutoActive)
            return;

        if (!ok) {
            stopSwrAutoSweep(true, tr("Failed to set rig frequency."));
            return;
        }

        // NEW: verify the rig actually reached rigHz before arming capture.
        // This prevents plot-vs-rig drift when rigctld/rig lags.
        swrAutoBeginVerifyThenArm(plotHz, rigHz); });
    }

    void MainWindow::swrAutoArmCapture(double targetHz)
    {
        if (!m_swrAutoActive)
            return;

        m_swrManualTargetFreqHz = targetHz;
        m_swrManualSwrBuf.clear();
        m_swrManualArmed = true;

        m_swrAutoArmTimer.restart();
        m_swrAutoWaitTimer.restart();
        m_swrAutoCaptureTimer->start();
    }

    void MainWindow::onSwrAutoCapturePoll()
    {
        if (!m_swrAutoActive)
            return;

        const int maxNeed = m_swrManualRequiredSamples;

        // IMPORTANT: If smoothing is None, do NOT early-accept.
        // With None, user expects stable per-step values, so require full sample count.
        const bool isNone = m_swrFreqSmoothingCombo->currentIndex() ? 0 : 1; // <-- wire this to your UI selection
        const int minNeed = isNone
                                ? maxNeed
                                : std::max(1, std::min(m_swrAutoMinSamples, maxNeed));

        if (m_swrManualSwrBuf.size() < minNeed)
            return;

        // If we haven’t reached maxNeed, only accept early if stable (and smoothing isn't None)
        if (!isNone && m_swrManualSwrBuf.size() < maxNeed)
        {
            double mn = m_swrManualSwrBuf[0];
            double mx = m_swrManualSwrBuf[0];
            for (double v : m_swrManualSwrBuf)
            {
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
            const double span = mx - mn;
            if (span > m_swrAutoStableSpan)
                return;
        }

        // We have enough samples (or stable early) for this step
        m_swrManualArmed = false;
        m_swrAutoCaptureTimer->stop();

        // Robust average with configured trim
        const int n = m_swrManualSwrBuf.size();
        int trim = m_swrTrimCount;
        if (trim < 0)
            trim = 0;
        if (trim > (n - 1) / 2)
            trim = (n - 1) / 2;

        const double avgSwr = trimmedMean(m_swrManualSwrBuf, trim);
        const double y = swrVsFreqUnitsYFromSwr(avgSwr);

        if (m_swrVsFreqPlot)
            m_swrVsFreqPlot->addPoint(m_swrManualTargetFreqHz, y);

        // Next step
        swrAutoSetNextFrequency();
    }

    void MainWindow::stopSwrAutoSweep(bool restoreFreq, const QString &reason)
    {
        if (!m_swrAutoActive)
            return;

        m_swrAutoActive = false;
        m_swrManualArmed = false;

        if (m_swrAutoCaptureTimer)
            m_swrAutoCaptureTimer->stop();

        if (m_swrAutoVerifyTimer)
            m_swrAutoVerifyTimer->stop();
        m_swrAutoVerifyInFlight = false;

        m_rigPending.clear();

        // Restore frequency if we successfully read it
        if (restoreFreq && m_rigCtld && m_rigCtld->isConnected() && std::isfinite(m_swrAutoOriginalFreqHz))
        {
            const QString cmd = QStringLiteral("F %1").arg(
                QString::number((qint64)std::llround(m_swrAutoOriginalFreqHz)));
            m_rigCtld->sendCommand(cmd, 1500);
        }

        if (!reason.isEmpty())
        {
            qWarning() << "[Hamlib][AutoSWR]" << reason;
            statusBar()->showMessage(reason);
        }

        // Re-enable UI via your mode UI gating
        if (m_swrFreqModeCombo)
            emit m_swrFreqModeCombo->currentIndexChanged(m_swrFreqModeCombo->currentIndex());
    }

    void MainWindow::onRigCommandFinished(raypm::RigCtldClient::CommandId id,
                                          bool ok,
                                          const QStringList &lines,
                                          int rprt)
    {
        auto it = m_rigPending.find(id);
        if (it == m_rigPending.end())
            return;

        auto fn = it.value();
        m_rigPending.erase(it);

        if (fn)
            fn(ok, lines, rprt);
    }

    void MainWindow::on_actionScopeSpectrum_triggered(bool checked)
    {
        // Unchecked => hide + cleanup
        if (!checked)
        {
            if (m_scopeSpectrumDock)
            {
                m_scopeSpectrumDock->hide();
                m_scopeSpectrumDock->deleteLater();
                m_scopeSpectrumDock = nullptr;
            }

            m_scopeSpectrumRunning = false;
            m_specDataRequested = false;
            m_specPollAttempts = 0;
            if (m_specPollTimer)
                m_specPollTimer->stop();

            m_specBtnStartStop = nullptr;
            m_specBtnClear = nullptr;
            m_specBtnExport = nullptr;
            m_specCouplerCombo = nullptr;
            m_specPathCombo = nullptr;
            m_specSamplesCombo = nullptr;
            m_specRateCombo = nullptr;
            m_specWindowCombo = nullptr;
            m_specScaleCombo = nullptr;
            m_scopePlotSpectrum = nullptr;
            m_specStatusLabel = nullptr;

            return;
        }

        // Already created => just show/raise
        if (m_scopeSpectrumDock)
        {
            m_scopeSpectrumDock->show();
            m_scopeSpectrumDock->raise();
            return;
        }

        // -----------------------------
        // Create dock + UI
        // -----------------------------
        m_scopeSpectrumDock = new QDockWidget(tr("Spectrum"), this);
        m_scopeSpectrumDock->setObjectName(QStringLiteral("ScopeSpectrumDock"));
        m_scopeSpectrumDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        auto *panel = new QWidget(m_scopeSpectrumDock);
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(6, 6, 6, 6);
        v->setSpacing(6);

        // Row 1: buttons
        auto *row1 = new QHBoxLayout;
        row1->setContentsMargins(0, 0, 0, 0);

        m_specBtnStartStop = new QPushButton(tr("Start"), panel);
        m_specBtnClear = new QPushButton(tr("Clear"), panel);
        m_specBtnExport = new QPushButton(tr("Export as PNG"), panel);

        row1->addWidget(m_specBtnStartStop);
        row1->addWidget(m_specBtnClear);
        row1->addStretch(1);
        row1->addWidget(m_specBtnExport);
        v->addLayout(row1);

        // Row 2: parameters
        auto *row2 = new QHBoxLayout;
        row2->setContentsMargins(0, 0, 0, 0);

        m_specCouplerCombo = new QComboBox(panel);
        m_specCouplerCombo->addItem(tr("Coupler 1"), 0);
        m_specCouplerCombo->addItem(tr("Coupler 2"), 1);
        m_specCouplerCombo->addItem(tr("Coupler 3"), 2);
        m_specCouplerCombo->addItem(tr("Coupler 4"), 3);

        m_specPathCombo = new QComboBox(panel);
        m_specPathCombo->addItem(tr("Forward"), QStringLiteral("fwd"));
        m_specPathCombo->addItem(tr("Reflected"), QStringLiteral("ref"));

        m_specSamplesCombo = new QComboBox(panel);
        m_specSamplesCombo->addItem(QStringLiteral("512"), 512);
        m_specSamplesCombo->addItem(QStringLiteral("1024"), 1024);
        m_specSamplesCombo->addItem(QStringLiteral("2048"), 2048);
        m_specSamplesCombo->addItem(QStringLiteral("4096"), 4096);
        m_specSamplesCombo->setCurrentIndex(2);

        m_specRateCombo = new QComboBox(panel);
        m_specRateCombo->addItem(QStringLiteral("12000"), 12000);
        m_specRateCombo->addItem(QStringLiteral("24000"), 24000);
        m_specRateCombo->addItem(QStringLiteral("48000"), 48000);
        m_specRateCombo->setCurrentIndex(1); // 48k default

        m_specWindowCombo = new QComboBox(panel);
        m_specWindowCombo->addItem(tr("Hann"), QStringLiteral("hann"));
        m_specWindowCombo->addItem(tr("Rectangular"), QStringLiteral("rect"));
        m_specWindowCombo->setCurrentIndex(0);

        m_specScaleCombo = new QComboBox(panel);
        m_specScaleCombo->addItem(tr("dB"), QStringLiteral("db"));
        m_specScaleCombo->addItem(tr("Linear"), QStringLiteral("lin"));
        m_specScaleCombo->setCurrentIndex(0);

        row2->addWidget(new QLabel(tr("Coupler:"), panel));
        row2->addWidget(m_specCouplerCombo);
        row2->addSpacing(10);

        row2->addWidget(new QLabel(tr("Path:"), panel));
        row2->addWidget(m_specPathCombo);
        row2->addSpacing(10);

        row2->addWidget(new QLabel(tr("Samples:"), panel));
        row2->addWidget(m_specSamplesCombo);
        row2->addSpacing(10);

        row2->addWidget(new QLabel(tr("Rate (Hz):"), panel));
        row2->addWidget(m_specRateCombo);
        row2->addSpacing(10);

        row2->addWidget(new QLabel(tr("Window:"), panel));
        row2->addWidget(m_specWindowCombo);
        row2->addSpacing(10);

        row2->addWidget(new QLabel(tr("Scale:"), panel));
        row2->addWidget(m_specScaleCombo);

        row2->addStretch(1);
        v->addLayout(row2);

        // Plot
        m_scopePlotSpectrum = new SpectrumPlotWidget(panel);
        m_scopePlotSpectrum->setTitle(tr("Spectrum"));
        v->addWidget(m_scopePlotSpectrum, 1);

        // Status row
        auto *rowInfo = new QHBoxLayout();
        rowInfo->setContentsMargins(0, 4, 0, 0);

        m_specStatusLabel = new QLabel(tr("Idle"), panel);
        m_specStatusLabel->setMinimumWidth(160);

        rowInfo->addWidget(m_specStatusLabel);
        rowInfo->addStretch(1);
        v->addLayout(rowInfo);

        panel->setLayout(v);
        m_scopeSpectrumDock->setWidget(panel);
        addDockWidget(Qt::RightDockWidgetArea, m_scopeSpectrumDock);

        // Dock close => uncheck menu item
        connect(m_scopeSpectrumDock, &QDockWidget::visibilityChanged, this, [this](bool vis)
                {
        if (ui && ui->actionScopeSpectrum) {
            QSignalBlocker b(ui->actionScopeSpectrum);
            ui->actionScopeSpectrum->setChecked(vis);
        }
        if (!vis) {
            m_scopeSpectrumRunning = false;
            m_specDataRequested = false;
            m_specPollAttempts = 0;
            if (m_specPollTimer)
                m_specPollTimer->stop();
        } });

        // Buttons
        connect(m_specBtnClear, &QPushButton::clicked, this, [this]()
                {
        if (m_scopePlotSpectrum)
            m_scopePlotSpectrum->clear();
        if (m_specStatusLabel)
            m_specStatusLabel->setText(tr("Idle")); });

        connect(m_specBtnExport, &QPushButton::clicked, this, &MainWindow::exportSpectrumScopePlotPng);

        // Start/Stop (Task 1: real capture wiring; FFT comes in Task 2)
        connect(m_specBtnStartStop, &QPushButton::clicked, this, [this]()
                {
        if (!m_scopePlotSpectrum || !m_specStatusLabel || !m_specCouplerCombo || !m_specPathCombo
            || !m_specSamplesCombo || !m_specRateCombo) {
            return;
        }

        // If already running, treat click as "Stop/Cancel"
        if (m_scopeSpectrumRunning) {
            m_scopeSpectrumRunning = false;
            m_specDataRequested = false;
            m_specPollAttempts = 0;
            if (m_specPollTimer)
                m_specPollTimer->stop();

            if (m_specBtnStartStop) {
                m_specBtnStartStop->setEnabled(true);
                m_specBtnStartStop->setText(tr("Start"));
            }

            m_specStatusLabel->setText(tr("Idle"));
            return;
        }

        // If modulation scope is actively polling/capturing, stop its local poll loop
        // (Capture endpoint is shared; starting Spectrum will override the server-side session.)
        if (m_scopeModulationRunning) {
            m_scopeModulationRunning = false;
            m_modDataRequested = false;
            m_modPollAttempts = 0;
            if (m_modPollTimer)
                m_modPollTimer->stop();

            if (m_modBtnStartStop) {
                m_modBtnStartStop->setEnabled(true);
                m_modBtnStartStop->setText(tr("Start"));
            }
            if (m_modStatusLabel)
                m_modStatusLabel->setText(tr("Idle"));
        }

        const int coupler = m_specCouplerCombo->currentData().toInt();
        const QString path = m_specPathCombo->currentData().toString();
        const int samples = m_specSamplesCombo->currentData().toInt();
        const int rateHz = m_specRateCombo->currentData().toInt();

        // Store request so we can ignore unrelated modulation capture signals
        m_specReqCoupler = coupler;
        m_specReqPath = path;
        m_specReqSamples = samples;
        m_specReqRateHz = rateHz;

        m_scopeSpectrumRunning = true;
        m_specDataRequested = false;
        m_specPollAttempts = 0;

        // Configure plot metadata (even though FFT is Task 2)
        const double nyquist = double(rateHz) * 0.5;
        const double fMax = qMin(20000.0, nyquist);
        m_scopePlotSpectrum->setMaxFrequencyHz(fMax);

        const bool db = (m_specScaleCombo && m_specScaleCombo->currentData().toString() == QLatin1String("db"));
        m_scopePlotSpectrum->setDbScale(db);

        const QString title = tr("Spectrum - %1 %2 (%3 @ %4 Hz)")
                                  .arg(m_specCouplerCombo->currentText(),
                                       m_specPathCombo->currentText())
                                  .arg(samples)
                                  .arg(rateHz);
        m_scopePlotSpectrum->setTitle(title);
        m_scopePlotSpectrum->clear();

        m_specStatusLabel->setText(tr("Requesting..."));

        if (m_specBtnStartStop) {
            m_specBtnStartStop->setEnabled(false);
            m_specBtnStartStop->setText(tr("Requesting..."));
        }

        // Kick off capture (reuses the modulation capture endpoints)
        m_comm.requestModulationStart(coupler, path, samples, rateHz); });

        // -----------------------------
        // Spectrum communication handlers (reuses modulation capture signals)
        // -----------------------------
        connect(&m_comm,
                &raypm::RayCommManager::modulationStartAck,
                this,
                [this](bool ok,
                       const QString &status,
                       int coupler,
                       const QString &path,
                       int samples,
                       int sampleRateHz)
                {
                    if (!m_scopeSpectrumDock || !m_scopeSpectrumRunning)
                        return;

                    // Only handle the session we requested from the Spectrum UI
                    if (coupler != m_specReqCoupler || path != m_specReqPath)
                        return;

                    if (!ok)
                    {
                        m_scopeSpectrumRunning = false;

                        if (m_specBtnStartStop)
                        {
                            m_specBtnStartStop->setEnabled(true);
                            m_specBtnStartStop->setText(tr("Start"));
                        }

                        if (m_specStatusLabel)
                            m_specStatusLabel->setText(tr("Idle"));

                        if (statusBar())
                            statusBar()->showMessage(tr("Spectrum start failed: %1").arg(status), 4000);

                        return;
                    }

                    if (m_specBtnStartStop)
                    {
                        m_specBtnStartStop->setEnabled(true);
                        m_specBtnStartStop->setText(tr("Stop"));
                    }

                    if (m_specStatusLabel)
                        m_specStatusLabel->setText(tr("Armed"));

                    m_specPollAttempts = 0;
                    m_specDataRequested = false;

                    // Ensure poll timer exists (polls /api/modulation/status until ready)
                    if (!m_specPollTimer)
                    {
                        QObject *parentObj =
                            m_scopeSpectrumDock ? static_cast<QObject *>(m_scopeSpectrumDock) : static_cast<QObject *>(this);

                        m_specPollTimer = new QTimer(parentObj);
                        m_specPollTimer->setInterval(100);

                        connect(m_specPollTimer, &QTimer::timeout, this, [this]()
                                {
                        if (!m_scopeSpectrumDock || !m_scopeSpectrumRunning) {
                            m_specPollTimer->stop();
                            return;
                        }

                        if (++m_specPollAttempts > 50) { // 50 * 100ms = 5s
                            m_specPollTimer->stop();
                            m_scopeSpectrumRunning = false;

                            if (m_specBtnStartStop) {
                                m_specBtnStartStop->setEnabled(true);
                                m_specBtnStartStop->setText(tr("Start"));
                            }

                            if (m_specStatusLabel)
                                m_specStatusLabel->setText(tr("Idle"));

                            if (statusBar())
                                statusBar()->showMessage(tr("Spectrum capture timed out."), 4000);

                            return;
                        }

                        m_comm.requestModulationStatus(); });
                    }

                    // Store accepted sample count for later fallback
                    m_specPollTimer->setProperty("requestSamples", samples);
                    Q_UNUSED(sampleRateHz);

                    // Start polling
                    m_specPollTimer->start();
                    m_comm.requestModulationStatus();
                });

        connect(&m_comm,
                &raypm::RayCommManager::modulationStatusReceived,
                this,
                [this](const QString &state,
                       int coupler,
                       const QString &path,
                       int samples,
                       int sampleRateHz)
                {
                    Q_UNUSED(sampleRateHz);

                    if (!m_scopeSpectrumDock || !m_scopeSpectrumRunning)
                        return;

                    if (coupler != m_specReqCoupler || path != m_specReqPath)
                        return;

                    if (state == QLatin1String("ready"))
                    {
                        if (m_specPollTimer)
                            m_specPollTimer->stop();

                        if (m_specStatusLabel)
                            m_specStatusLabel->setText(tr("Ready"));

                        if (!m_specDataRequested)
                        {
                            m_specDataRequested = true;

                            int requestSamples = samples;
                            if (requestSamples <= 0 && m_specPollTimer)
                                requestSamples = m_specPollTimer->property("requestSamples").toInt();
                            if (requestSamples <= 0)
                                requestSamples = m_specReqSamples;

                            if (m_specStatusLabel)
                                m_specStatusLabel->setText(tr("Capturing..."));

                            m_comm.requestModulationData(requestSamples);
                        }

                        return;
                    }

                    if (state == QLatin1String("error"))
                    {
                        if (m_specPollTimer)
                            m_specPollTimer->stop();

                        m_scopeSpectrumRunning = false;

                        if (m_specBtnStartStop)
                        {
                            m_specBtnStartStop->setEnabled(true);
                            m_specBtnStartStop->setText(tr("Start"));
                        }

                        if (m_specStatusLabel)
                            m_specStatusLabel->setText(tr("Idle"));

                        if (statusBar())
                            statusBar()->showMessage(tr("Spectrum capture error."), 4000);
                    }
                });

        connect(&m_comm,
                &raypm::RayCommManager::modulationDataReceived,
                this,
                [this](int coupler, const QString &path, int sampleRateHz, const QVector<qint16> &samples)
                {
                    if (!m_scopeSpectrumDock || !m_scopePlotSpectrum)
                        return;

                    qDebug() << "[Spectrum] sampleRateHz(actual)=" << sampleRateHz
                             << "samples=" << samples.size();

                    // Only handle the session we requested from the Spectrum UI
                    if (coupler != m_specReqCoupler || path != m_specReqPath)
                        return;

                    // One-shot complete
                    m_scopeSpectrumRunning = false;

                    if (m_specBtnStartStop)
                    {
                        m_specBtnStartStop->setEnabled(true);
                        m_specBtnStartStop->setText(tr("Start"));
                    }

                    if (m_specPollTimer)
                        m_specPollTimer->stop();

                    if (sampleRateHz <= 0 || samples.size() < 2)
                    {
                        if (m_specStatusLabel)
                            m_specStatusLabel->setText(tr("Idle"));
                        m_scopePlotSpectrum->clear();
                        return;
                    }

                    /*                // -------- DEBUG: raw sample stats for Spectrum capture --------
                                    auto logSamples = [](const char *tag, const QVector<qint16> &s) {
                                        if (s.isEmpty()) {
                                            qDebug() << "[ModDebug]" << tag << "EMPTY";
                                            return;
                                        }

                                        qint16 min = std::numeric_limits<qint16>::max();
                                        qint16 max = std::numeric_limits<qint16>::min();
                                        double sum  = 0.0;
                                        double sum2 = 0.0;

                                        for (qint16 v : s) {
                                            if (v < min) min = v;
                                            if (v > max) max = v;
                                            sum  += v;
                                            sum2 += double(v) * double(v);
                                        }

                                        const int N = s.size();
                                        const double mean = sum / N;
                                        const double rms  = std::sqrt(sum2 / N);

                                        qDebug() << "[ModDebug]" << tag
                                                 << "N="   << N
                                                 << "min=" << min
                                                 << "max=" << max
                                                 << "mean="<< mean
                                                 << "rms=" << rms;
                                    };

                                    logSamples("SpectrumCapture", samples);
                                    // -------- END DEBUG ------------------------------------------
                    */
                    const double fs = double(sampleRateHz);
                    const double fMax = qMin(20000.0, fs * 0.5);

                    SpectrumFft::Params fp;
                    fp.sampleRateHz = sampleRateHz;
                    fp.maxHz = fMax;

                    // Window from UI ("hann" / "rect")
                    const QString win = m_specWindowCombo
                                            ? m_specWindowCombo->currentData().toString()
                                            : QStringLiteral("hann");
                    fp.window = (win == QLatin1String("rect"))
                                    ? SpectrumFft::Window::Rectangular
                                    : SpectrumFft::Window::Hann;

                    // Scale from UI ("db" / "lin")
                    const QString scale = m_specScaleCombo
                                              ? m_specScaleCombo->currentData().toString()
                                              : QStringLiteral("db");
                    fp.dbScale = (scale == QLatin1String("db"));

                    m_scopePlotSpectrum->setMaxFrequencyHz(fMax);
                    m_scopePlotSpectrum->setDbScale(fp.dbScale);

                    const QVector<QPointF> pts = SpectrumFft::compute(samples, fp);
                    m_scopePlotSpectrum->setSpectrum(pts);

                    if (m_specStatusLabel)
                    {
                        m_specStatusLabel->setText(
                            tr("Captured %1 samples at %2 Hz")
                                .arg(samples.size())
                                .arg(sampleRateHz));
                    }
                });

        // Show dock now
        m_scopeSpectrumDock->show();
        m_scopeSpectrumDock->raise();
    }

    bool MainWindow::parseRigHzFromLines(const QStringList &lines, double *outHz)
    {
        if (!outHz)
            return false;
        if (lines.isEmpty())
            return false;

        // rigctld "f" typically returns a single line: "14325000"
        bool ok = false;
        const QString s = lines.first().trimmed();

        // Some rigs may return float MHz in some contexts; handle both.
        // Prefer integer Hz if it parses cleanly.
        const qint64 asInt = s.toLongLong(&ok);
        if (ok)
        {
            *outHz = double(asInt);
            return true;
        }

        const double asDbl = s.toDouble(&ok);
        if (ok)
        {
            // Heuristic: if it's small, assume MHz; if huge, assume Hz.
            *outHz = (asDbl < 1e6) ? (asDbl * 1e6) : asDbl;
            return true;
        }

        return false;
    }

    void MainWindow::swrAutoBeginVerifyThenArm(double plotHz, double targetHz)
    {
        if (!m_swrAutoActive || !m_rigCtld)
            return;

        m_swrAutoVerifyPlotHz = plotHz;
        m_swrAutoVerifyTargetHz = targetHz;
        m_swrAutoVerifyElapsedMs = 0;

        m_swrAutoVerifyInFlight = false;

        if (m_swrAutoVerifyTimer)
            m_swrAutoVerifyTimer->start(0);
    }

    void MainWindow::swrAutoVerifyPoll()
    {
        if (!m_swrAutoActive || !m_rigCtld)
            return;

        // Don’t stack multiple "f" commands if one is still in flight
        if (m_swrAutoVerifyInFlight)
            return;

        // Stop if we’ve exceeded the max verify time budget
        if (m_swrAutoVerifyElapsedMs >= m_swrAutoVerifyMaxMs)
        {
            // Proceed anyway (best effort), but this time you know verify failed.
            // You could also stopSwrAutoSweep(...) if you prefer hard correctness.
            const int settleMs = (m_swrFreqAutoSettleMs ? m_swrFreqAutoSettleMs->value() : 0);
            QTimer::singleShot(settleMs, this, [this]()
                               {
            if (!m_swrAutoActive)
                return;
            swrAutoArmCapture(m_swrAutoVerifyPlotHz); });
            return;
        }

        m_swrAutoVerifyInFlight = true;

        rigSend(QStringLiteral("f"), 800, [this](bool ok, const QStringList &lines, int)
                {
        m_swrAutoVerifyInFlight = false;

        if (!m_swrAutoActive)
            return;

        double actualHz = 0.0;
        if (ok && parseRigHzFromLines(lines, &actualHz)) {
            const double err = std::fabs(actualHz - m_swrAutoVerifyTargetHz);

            if (err <= m_swrAutoVerifyTolHz) {
                // Verified: rig is where we think it is. Now do settle + arm capture.
                const int settleMs = (m_swrFreqAutoSettleMs ? m_swrFreqAutoSettleMs->value() : 0);
                QTimer::singleShot(settleMs, this, [this]() {
                    if (!m_swrAutoActive)
                        return;
                    swrAutoArmCapture(m_swrAutoVerifyPlotHz);
                });
                return;
            }
        }

        // Not verified yet; poll again
        m_swrAutoVerifyElapsedMs += m_swrAutoVerifyPollMs;
        if (m_swrAutoVerifyTimer)
            m_swrAutoVerifyTimer->start(m_swrAutoVerifyPollMs); });
    }

    void MainWindow::updateRigFreqPollTimer()
    {
        if (!m_rigFreqPollTimer)
            return;

        if (!m_layoutManager)
        {
            m_rigFreqPollTimer->stop();
            return;
        }

        const bool any = m_layoutManager->anyFreqWidgetUsesHamlib();
        const bool timerActiveBefore = m_rigFreqPollTimer->isActive();

        qInfo() << "[Hamlib][Freq] updateRigFreqPollTimer:"
                << "anyFreqWidgetUsesHamlib =" << any
                << "timerActiveBefore =" << timerActiveBefore;

        if (any)
        {
            if (!timerActiveBefore)
            {
                // If the user enabled auto-start and we have a known-good config,
                // try to start rigctld once for the FreqWidgets.
                const auto &s = m_settings;
                const bool autoStart =
                    s.hamlib.enabled &&
                    s.hamlib.lastTestOk &&
                    s.hamlib.serial.autoStartRigctld;

                if (autoStart)
                {
                    qInfo() << "[Hamlib][Freq] auto-start rigctld because FreqWidgets use Hamlib";

                    // We don't need an onReady callback here; the poll timer will
                    // simply start polling, and RigCtldClient will connect when ready.
                    QString err;
                    startRigctldFromSettings(&err);
                    qWarning() << "[Hamlib][Freq] auto-start rigctld failed:" << err;
                }

                m_rigFreqPollTimer->start();
                qInfo() << "[Hamlib][Freq] starting poll timer";
            }
        }
        else
        {
            if (timerActiveBefore)
            {
                qInfo() << "[Hamlib][Freq] stopping poll timer (no FreqWidgets using Hamlib)";
            }
            m_rigFreqPollTimer->stop();
        }
    }

    void MainWindow::pollRigFrequencyForFreqWidgets()
    {
        constexpr bool DEBUG_HAMLIB_FREQ = true;

        const bool enabled = m_settings.hamlib.enabled;
        const bool lastOk = m_settings.hamlib.lastTestOk;

        if (!enabled || !lastOk)
        {
            if (DEBUG_HAMLIB_FREQ)
            {
                qInfo() << "[Hamlib][Freq] poll skipped:"
                        << "enabled=" << enabled
                        << "lastTestOk=" << lastOk;
            }
            return;
        }

        // Make sure we actually have a RigCtldClient instance
        ensureRigCtldClient();

        if (!m_rigCtld)
        {
            if (DEBUG_HAMLIB_FREQ)
            {
                qWarning() << "[Hamlib][Freq] poll aborted: m_rigCtld still null after ensureRigCtldClient()";
            }
            return;
        }

        // Ensure we’re connected to the right endpoint before sending “f”
        if (!m_rigCtld->isConnected())
        {
            QString host;
            int port = 0;
            if (!getRigctldEndpoint(host, port))
            {
                if (DEBUG_HAMLIB_FREQ)
                {
                    qWarning() << "[Hamlib][Freq] poll aborted: getRigctldEndpoint() failed";
                }
                return;
            }

            if (DEBUG_HAMLIB_FREQ)
            {
                qInfo() << "[Hamlib][Freq] connecting rigctld for FreqWidgets to"
                        << host << port;
            }

            m_rigCtld->connectToHost(host, port);

            // Let the next timer tick actually send the command once connected
            return;
        }

        if (DEBUG_HAMLIB_FREQ)
        {
            qInfo() << "[Hamlib][Freq] sending 'f' for FreqWidgets";
        }

        rigSend(QStringLiteral("f"),
                [this](bool ok, const QStringList &lines, int /*rprt*/)
                {
                    double hz = 0.0;
                    const bool parsed = parseRigHzFromLines(lines, &hz);
                    const bool valid = ok && parsed && (hz > 0.0);

                    if (DEBUG_HAMLIB_FREQ)
                    {
                        qInfo() << "[Hamlib][Freq] reply ok=" << ok
                                << "parsed=" << parsed
                                << "hz=" << hz
                                << "valid=" << valid;
                    }

                    applyRigFrequencyToFreqWidgets(hz, valid);
                });
    }

    void MainWindow::applyRigFrequencyToFreqWidgets(double hz, bool valid)
    {
        if (!m_layoutManager)
            return;

        const QStringList widgetIds = m_layoutManager->allWidgetIds();

        if (DEBUG_HAMLIB_FREQ)
        {
            qDebug() << "[Hamlib][Freq] applyRigFrequencyToFreqWidgets: hz=" << hz
                     << "valid=" << valid
                     << "widgets=" << widgetIds;
        }

        for (const QString &id : widgetIds)
        {
            WidgetBase *w = m_layoutManager->widgetObject(id);
            auto *freq = qobject_cast<FreqWidget *>(w);
            if (!freq)
                continue;

            if (DEBUG_HAMLIB_FREQ)
            {
                qDebug() << "   widget" << id
                         << "useHamlib()=" << freq->useHamlib();
            }

            if (!freq->useHamlib())
                continue; // this widget still uses RF telemetry

            if (valid)
            {
                if (DEBUG_HAMLIB_FREQ)
                {
                    qDebug() << "   --> setting Hamlib freq on" << id << "to" << hz;
                }
                freq->setFrequency(hz, /*live*/ true, SourceType::HAMLIB);
            }
            else
            {
                if (DEBUG_HAMLIB_FREQ)
                {
                    qDebug() << "   --> marking freq invalid on" << id;
                }
                freq->setFrequencyValid(false);
            }
        }
    }

    void MainWindow::on_actionCheck_for_Updates_triggered()
    {
        if (!m_updater)
            m_updater = new Updater(&m_comm, &m_settings, this);

        m_updater->checkForUpdatesInteractive(this);
    }

    void MainWindow::on_actionNew_Layout_triggered()
    {
        if (!m_layoutManager || !ui)
            return;

        // Warn: creating a new layout clears the current one
        const bool hasExistingLayout = !m_layoutManager->allWidgetIds().isEmpty();
        if (hasExistingLayout)
        {
            const auto rc = QMessageBox::warning(
                this,
                tr("Create New Layout"),
                tr("This will clear the current layout and create a new empty layout.\n\nContinue?"),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel);

            if (rc != QMessageBox::Yes)
                return;
        }

        bool ok = false;
        const int cols = QInputDialog::getInt(this,
                                              tr("Create New Layout"),
                                              tr("Columns (1–4):"),
                                              qBound(1, m_settings.layoutGridColumns, 4),
                                              1,
                                              4,
                                              1,
                                              &ok);
        if (!ok)
            return;

        // Persist the user's preferred default columns for next time.
        m_settings.layoutGridColumns = cols;
        SettingsManager::save(m_settings);

        // Clear selection state before rebuild
        m_selectedGroupId.clear();
        m_selectedWidgetId.clear();
        m_layoutManager->setSelectedGroup(QString());

        // Create the new empty layout
        m_layoutManager->createEmptyLayout(cols, ui->centralwidget, m_powerBar, m_forwardRefPanel);

        // Auto-enter design mode after creation
        if (ui->actionDesign_Mode && !ui->actionDesign_Mode->isChecked())
        {
            // Keep UI state consistent
            ui->actionDesign_Mode->setChecked(true);
        }
        on_actionDesign_Mode_triggered(true);

        // Reapply various bindings / timers based on the new layout.
        reapplyCouplerLayoutBindings();
        reapplyAuxRangesToMiniScopes();
        updateRigFreqPollTimer();
        connectAuxDigitalWidgets();
        applyAuxDigitalConfigFromLayout();

        m_applyLayoutAuxAnalogPending = true;
        m_applyLayoutAuxAnalogReceived.clear();
        tryApplyAuxAnalogConfigFromLayout();

        statusBar()->showMessage(tr("New empty layout created."), 5000);
    }

    void MainWindow::on_actionAbout_triggered()
    {
        QMap<DialogAbout::AboutSection, QString> info;
        info[DialogAbout::AboutSection::LOGO] = ":/assets/icons/icon1024.png";
        info[DialogAbout::AboutSection::TITLE] = "RayPowerMeter";
        info[DialogAbout::AboutSection::VERSION] = QApplication::applicationVersion();
        info[DialogAbout::AboutSection::REVISION] = APP_RELEASE;
        info[DialogAbout::AboutSection::DATE] = APP_RELEASE_DATE;
        info[DialogAbout::AboutSection::SHORT_INFO] =
            "Multi-channel RF power, SWR, and signal monitoring for amateur radio systems.";
        info[DialogAbout::AboutSection::DESCRIPTION] =
            "RayPowerMeter is a real-time RF measurement and monitoring application designed for "
            "amateur radio operators and RF developers. It provides live forward and reflected "
            "power readings, SWR calculation, logging, and device control using external couplers "
            "and sensors. The application supports multiple channels and is intended for both "
            "operational monitoring and test/measurement workflows.";
        info[DialogAbout::AboutSection::AUTHOR] = APP_COMPANY;
        info[DialogAbout::AboutSection::LICENCE] = "GNU Lesser General Public License v3.0 only (LGPL-3.0-only).";

        DialogAbout dlg(this);
        dlg.setStyleSheet(this->styleSheet());
        dlg.populate(info);
        dlg.exec();
    }

    void MainWindow::on_actionLog_File_triggered()
    {
        dialogViewLogFile->setStyleSheet(this->styleSheet());
        dialogViewLogFile->setFont(this->font());
        dialogViewLogFile->show();
    }

    void MainWindow::on_actionAmplifier_Analyzer_triggered()
    {
        // Reuse existing rig client (MainWindow already owns/initializes it)
        ensureRigCtldClient();

        if (m_ampAnalyzerDock) {
            m_ampAnalyzerDock->raise();
            m_ampAnalyzerDock->activateWindow();
            m_ampAnalyzerDock->show();
            return;
        }

        m_ampAnalyzerDock = new QDockWidget(tr("Amplifier Analyzer"), this);
        m_ampAnalyzerDock->setObjectName(QStringLiteral("AmplifierAnalyzerDock"));
        m_ampAnalyzerDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        m_ampAnalyzerWidget = new AmplifierAnalyzerWidget(m_runtimeModel,
                                                          m_rigCtld,
                                                          m_settings,
                                                          m_isDarkTheme,
                                                          m_ampAnalyzerDock);

        // Keep style consistent
        m_ampAnalyzerWidget->setStyleSheet(this->styleSheet());
        m_ampAnalyzerWidget->setFont(this->font());

        m_ampAnalyzerDock->setWidget(m_ampAnalyzerWidget);
        addDockWidget(Qt::RightDockWidgetArea, m_ampAnalyzerDock);
        m_ampAnalyzerDock->show();

        connect(m_ampAnalyzerDock, &QDockWidget::visibilityChanged, this, [this](bool vis){
            if (!vis) {
                // optional: if you want it to recreate fresh each time
                // (matching your SWR dock pattern), uncomment below:
                //
                // m_ampAnalyzerDock->deleteLater();
                // m_ampAnalyzerDock = nullptr;
                // m_ampAnalyzerWidget = nullptr;
            }
        });
    }







} // namespace raypm
