// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <functional>

#include <qspinbox.h>

#include "app/customsplashscreen.h"
#include "app/dialogviewlogfile.h"
#include "comm/RayCommManager.h"
#include "core/AppSettings.h"
#include "core/RayAuxConfig.h"
#include "core/RayCouplerConfig.h"
#include "core/RuntimeDataModel.h"
#include "core/Updater.h"
#include "core/rigctldclient.h" // for RigCtldClient::CommandId
#include "dialoghardwareconfig.h"
#include "dialogprotectionwarning.h"
#include "dialogsetup.h"
#include "widgets/amplifieranalyzerwidget.h"
#include "widgets/analogpowermeterwidget.h"
#include "widgets/auxdigitalwidget.h"
#include "widgets/forwardrefpanel.h"
#include "widgets/freqwidget.h"
#include "widgets/miniscopewidget.h"
#include "widgets/powerbarwidget.h"
#include "widgets/scopeplotwidget.h"
#include "widgets/spectrumplotwidget.h"
#include "widgets/swrvsfreqplotwidget.h"

using SourceType = raypm::FreqWidget::SourceType;

// Forward declarations
class QDockWidget;
class QSpinBox;
class QCheckBox;
class QLabel;
class QFormLayout;
class QComboBox;
class QToolButton;
class QTabWidget;
class QMetaObject;
class QTimer;
class QProcess;

namespace raypm {
class RigCtldClient;
}

namespace Ui {
class MainWindow;
}

namespace raypm {

class ForwardRefPanel; // from src/widgets/ForwardRefPanel.h
class LayoutManager;   // from src/controllers/LayoutManager.h

class IWidgetPropertyAdapter;
class GainWidgetPropertyAdapter;
class AuxSensorWidgetPropertyAdapter;
class AuxVoltageWidgetPropertyAdapter;
class AuxDigitalWidgetPropertyAdapter;
class ForwardRefPanelPropertyAdapter;
class PowerBarWidgetPropertyAdapter;
class PowerBarVertWidgetPropertyAdapter;
class MiniScopeWidgetPropertyAdapter;
class SpacerWidgetPropertyAdapter;
class FreqWidgetPropertyAdapter;
class AnalogPowerMeterPropertyAdapter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(CustomSplashScreen *splash, QWidget *parent = nullptr);
    ~MainWindow() override;

    void resizeEvent(QResizeEvent *event) override;

    bool isWidgetPropertiesDockVisible() const;

private slots:
    void onConnectedChanged(bool connected);
    void onTelemetryReceived(const raypm::RayTelemetry &telemetry);
    void onCommError(const QString &message);
    void openSetupDialog();
    void openHardwareConfig();
    void onScaleFactorChanged(double scale);
    void onCouplerConfigReceived(const raypm::RayCouplerConfig &cfg);
    void onHardwareReloadRequested();
    void onHardwareCouplerApplyRequested(const raypm::RayCouplerConfig &cfg);
    void onHardwareDeviceApplyRequested(int brightness, qint64 utcEpochSeconds);
    void onHardwareCalibrationApplyRequested(int couplerIndex,
                                             bool forwardPath,
                                             double vLow,
                                             double wLowTrue,
                                             double vHigh,
                                             double wHighTrue);
    void onHardwareCalibrationTableApplyRequested(int couplerIndex,
                                                  bool forwardPath,
                                                  const QVector<raypm::RayCalPoint> &points);
    void onHardwareCalibrationCopyRequested(int index, bool fromForward, bool toForward);
    void onAuxConfigReceived(const raypm::RayAuxConfig &cfg);
    void onHardwareAuxApplyRequested(const raypm::RayAuxConfig &cfg);

    // Design mode & Layout slots
    void onLayoutWidgetClicked(const QString &groupId, const QString &widgetId);
    void onWidgetPropertiesRequested(const QString &groupId, const QString &widgetId);
    void onPropertyMarginChanged(int value);
    void onPropertyMinHeightChanged(int px);
    void onPropertyVisibleChanged(bool checked);
    void onWidgetLabelEdited(const QString &text);
    void onPropertyShowTicksChanged(bool checked);
    void onPropertyShowLabelsChanged(bool checked);
    void onPropertyShowThresholdLabelsChanged(bool checked);
    void onPropertyMeterModeChanged(int index);
    //void onAddPowerBarToGroup();
    //void onAddForwardRefToGroup();
    void onAddSelectedWidget();
    void onRemoveSelectedWidget();
    void onMoveSelectedWidgetUp();
    void onMoveSelectedWidgetDown();
    void onAddGroupAbove();
    void onAddGroupBelow();
    void onAddGroupLeft();
    void onAddGroupRight();
    void onRemoveSelectedGroup();
    void onMoveSelectedGroup();
    void onAddSelectedGroup();
    void onGroupNameChanged(const QString &text);
    void onGroupCouplerIndexChanged(int value);
    void onPropertySourceChanged(int index);
    void updateTelemetryAgeLabel();
    void onPropertyGainInSourceChanged(int index);
    void onPropertyGainOutSourceChanged(int index);
    void onPropertyScopeSignalChanged(int index);
    void onPropertyScopeWindowChanged(double seconds);
    void onPropertyScopeAutoScaleChanged(bool checked);
    void onProtectionResetRequested();
    void onPropertyAuxSensorUnitsChanged(int index);
    void onPropertyAuxSensorModeChanged(int index);
    void onPropertyAuxSensorLabelEdited(const QString &text);
    void onPropertyAuxDigitalModeChanged(int index);
    void onPropertyFreqUseHamlibChanged(bool checked);
    void onPropertyFreqShowHzChanged(bool checked);

    // Menu actions
    void on_actionOpen_Setup_triggered();
    void on_actionOpen_HW_Config_triggered();
    void on_actionReset_to_Default_Layout_triggered();
    void on_actionLoad_Layout_triggered();
    void on_actionSave_Layout_As_triggered();
    void on_actionDesign_Mode_triggered(bool checked);
    void on_actionExit_triggered();
    void on_actionTelemetryHistory_triggered();
    void on_actionScopeTelemetry_triggered();
    void on_actionScopeModulation_triggered();
    void on_actionScopeTelemetry_triggered(bool checked);
    void on_actionScopeModulation_triggered(bool checked);
    void on_actionScopeAnalyzer_triggered(bool checked);
    void on_actionScopeSWR_vs_Frequency_triggered(bool checked);
    void onRigCommandFinished(raypm::RigCtldClient::CommandId id,
                              bool ok,
                              const QStringList &lines,
                              int rprt);
    void on_actionScopeSpectrum_triggered(bool checked);

    void on_actionCheck_for_Updates_triggered();

    void on_actionNew_Layout_triggered();

    void on_actionAbout_triggered();

    void on_actionLog_File_triggered();

    void on_actionAmplifier_Analyzer_triggered();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    CustomSplashScreen *splash;
    DialogViewLogFile *dialogViewLogFile;
    void setupUi();
    void setupConnections();
    void styleGui(); // apply theme + stylesheet + scaling

    QSize m_baseWindowSize; // window size normalized to scale=1.0
    bool m_inScaleResize = false;

    bool m_firstShow = true;

    // Layout menu actions
    QAction *m_actLayoutReset = nullptr;
    QAction *m_actLayoutLoad = nullptr;
    QAction *m_actLayoutSaveAs = nullptr;

    // Dynamic layout actions (created in code)
    QAction *m_actAddPowerBarToGroup = nullptr;
    QAction *m_actAddForwardRefToGroup = nullptr;
    QAction *m_actRemoveWidget = nullptr;
    QAction *m_actMoveWidgetUp = nullptr;
    QAction *m_actMoveWidgetDown = nullptr;
    QAction *m_actAddGroupAbove = nullptr;
    QAction *m_actAddGroupBelow = nullptr;
    QAction *m_actRemoveGroup = nullptr;

    QComboBox *m_addWidgetCombo = nullptr;

    QComboBox *m_widgetTypeCombo = nullptr;
    QComboBox *m_groupMoveDirectionCombo = nullptr;
    QComboBox *m_groupAddDirectionCombo = nullptr;

    QSpinBox *m_propMinHeightSpin = nullptr;

    // Keep all coupler configs we've seen so we can
    // reapply them after rebuilding the layout.
    QMap<int, RayCouplerConfig> m_couplerConfigs;

    struct AuxSourceInfo
    {
        int index = -1; // from JSON "index"
        QString type;   // from JSON "type" (voltage/current/temperature)
    };

    QVector<AuxSourceInfo> m_auxSources;

    AppSettings m_settings;
    RayCommManager m_comm;

    // Status bar + telemetry age tracking
    QLabel *m_connectionStatusLabel = nullptr;
    QLabel *m_telemetryAgeLabel = nullptr;
    QTimer m_telemetryAgeTimer;
    QDateTime m_lastTelemetryTime;
    bool m_hasTelemetry = false;
    QLabel *m_statusProtectionLabel = nullptr;

    Ui::MainWindow *ui = nullptr;  // Designer UI
    QTimer *m_fakeTimer = nullptr; // if you still have fake data
    raypm::ForwardRefPanel *m_forwardRefPanel = nullptr;
    PowerBarWidget *m_powerBar = nullptr;

    DialogSetup *m_setupDialog = nullptr;
    DialogHardwareConfig *m_hwDialog = nullptr;

    DialogProtectionWarning *m_protectionDialog = nullptr;
    bool m_lastProtectionActive = false;
    QString tripTypeToSummary(quint8 type) const;
    QString tripSourceToString(quint8 source) const;
    QString tripDetailLine(const raypm::RayTripInfo &trip) const;

    double m_scaleFactor = 1.0;
    bool m_isDarkTheme = true;

    RayCouplerConfig m_lastCouplerConfig;
    bool m_hasCouplerConfig = false;

    raypm::RayAuxConfig m_lastAuxConfig;
    bool m_hasAuxConfig = false;
    QMap<int, raypm::RayAuxConfig> m_auxConfigs;

    RuntimeDataModel *m_runtimeModel = nullptr;
    LayoutManager *m_layoutManager = nullptr;

    // Design mode state
    bool m_designModeEnabled = false;
    QAction *m_actDesignMode = nullptr;

    QAction *m_actAddGroup = nullptr;  // Add group Above/Below/Left/Right
    QAction *m_actMoveGroup = nullptr; // Move group Up/Down/Left/Right

    // Current selection
    QString m_selectedGroupId;
    QString m_selectedWidgetId;

    // --- Group properties --- //
    QLabel *m_groupPropIdLabel = nullptr;
    QLineEdit *m_groupNameEdit = nullptr;
    QSpinBox *m_groupCouplerSpin = nullptr;

    // Properties dock
    QDockWidget *m_propertiesDock = nullptr;
    QLabel *m_propHintLabel = nullptr;
    QLabel *m_propSelectedLabel = nullptr;
    QSpinBox *m_propMarginSpin = nullptr;
    QCheckBox *m_propVisibleCheck = nullptr;
    QComboBox *m_propMeterModeCombo = nullptr;
    QComboBox *m_propSourceCombo = nullptr;
    QLineEdit *m_widgetLabelEdit = nullptr;

    // PowerBar-specific controls
    QFormLayout *m_propFormLayout = nullptr;
    QCheckBox *m_propShowTicksCheck = nullptr;
    QCheckBox *m_propShowLabelsCheck = nullptr;
    QCheckBox *m_propShowThresholdLabelsCheck = nullptr;

    // GainWidget-specific property controls
    QComboBox *m_propGainInSourceCombo = nullptr;
    QComboBox *m_propGainOutSourceCombo = nullptr;
    QDoubleSpinBox *m_propGainAttnDbSpin = nullptr;
    void onPropertyGainAttnDbChanged(double value);

    // AuxSensorWidget-specific property controls
    QComboBox *m_propAuxSensorUnitsCombo = nullptr;
    QComboBox *m_propAuxSensorModeCombo = nullptr;
    QLineEdit *m_propAuxSensorLabelEdit = nullptr;

    // AuxDigital properties
    QComboBox *m_propAuxDigitalModeCombo = nullptr;

    // FreqWidget-specific property controls
    QCheckBox *m_propFreqUseHamlibCheck = nullptr;
    QCheckBox *m_propFreqShowHzCheck = nullptr;

    // Widget properties runtime mode (vs design mode)
    void populatePropertiesDockForSelection(const QString &groupId,
                                            const QString &widgetId,
                                            bool fromDesignMode);
    void setWidgetPropertiesRuntimeMode(bool enabled);
    bool m_widgetPropertiesModeEnabled = false;
    bool canEditWidgetProperties() const;

    // Helpers to show/hide GainWidget property rows
    void setGainPropertyRowsVisible(bool visible);
    void populateGainSourceCombos();

    // Helpers for Aux → MiniScope range handling
    double auxMaxRangeForConfig(const raypm::RayAuxConfig &cfg) const;
    void applyAuxRangeToMiniScopes(int auxIndex);
    void reapplyAuxRangesToMiniScopes();

    // Helper toshow/hide AuxSensorWidget property rows
    void setAuxSensorPropertyRowsVisible(bool visible);
    // Helper toshow/hide AuxDigitalWidget property rows
    void setAuxDigitalPropertyRowsVisible(bool visible);

    // Helper to show/hide Coupler Index property row on widgets that support coupler binding
    bool widgetTypeUsesCoupler(const QString &type);

    QDockWidget *m_layoutToolsDock = nullptr;

    QByteArray m_preDesignGeometryData;
    bool m_hasPreDesignGeometryData = false;

    // Helpers to show/hide PowerBar property rows
    void setPowerBarPropertyRowsVisible(bool visible);
    void setGroupCouplerRowVisible(bool visible);

    // Helpers to show/hide FreqWidget property rows
    void setFreqPropertyRowsVisible(bool visible);

    // Enable/disable design-mode layout actions
    void updateDesignActionsEnabled();

    // Helper to reapply coupler config bindings after layout rebuild
    void reapplyCouplerLayoutBindings();

    void populateSourceCombo();

    void initializeLayoutFromSettings();

    QString layoutConfigDirectory() const;
    QString defaultLayoutFilePath() const;
    QString promptForNewGroupTitle(const QString &defaultTitle = QString());

    // Telemetry History dock
    QDockWidget *m_teleHistDock = nullptr;
    QTableWidget *m_teleHistTable = nullptr;
    QTimer m_teleHistTimer;
    bool m_teleHistRunning = false;
    bool m_teleHistPaused = false;

    // Telemetry History UI controls
    QComboBox *m_teleHistSignalCombo = nullptr;
    QComboBox *m_teleHistCouplerCombo = nullptr;
    QComboBox *m_teleHistAuxCombo = nullptr;   // optional filter for auxIndex
    QSpinBox *m_teleHistMaxRowsSpin = nullptr; // ring buffer size

    // Telemetry History data export path
    QString m_lastTeleHistExportDir;

    // ------------------------
    // Scopes (M3)
    // ------------------------
    QDockWidget *m_scopeTelemetryDock = nullptr;
    QTabWidget *m_scopeTelemetryTabs = nullptr;

    // Plot widgets (one per tab)
    ScopePlotWidget *m_scopePlotForwardW = nullptr;
    ScopePlotWidget *m_scopePlotReflectedW = nullptr;
    ScopePlotWidget *m_scopePlotSwr = nullptr;
    ScopePlotWidget *m_scopePlotAuxVolt = nullptr;
    ScopePlotWidget *m_scopePlotAuxCurr = nullptr;
    ScopePlotWidget *m_scopePlotTempC = nullptr; // optional extra tab
    // (future) QWidget *m_scopeTabTbd = nullptr;
    // mainwindow.h (private:)
    QComboBox *m_scopeTelemetrySourceCombo = nullptr;  // (used as Coupler selector)
    QComboBox *m_scopeTelemetryAuxCombo = nullptr;     // Aux selector (Aux 1..4)
    QComboBox *m_scopeTelemetryCouplerCombo = nullptr; // Coupler selector (Coupler 1..4)

    int m_scopeSelectedCouplerIndex = 0; // 0-based (Coupler 1 => 0)

    void applyScopeTelemetrySettings(); // uses checkbox/window/maxpts + selected coupler
    double scopeCouplerMaxW() const; // returns fwd_bar_range_w for selected coupler (fallback safe)

    QDockWidget *m_scopeModulationDock = nullptr;
    QDockWidget *m_scopeSwrFreqDock = nullptr;

    // Connections we create only while dock is alive/visible
    QMetaObject::Connection m_scopeTelemetryConn;
    QMetaObject::Connection m_scopeModulationConn;
    QMetaObject::Connection m_scopeSwrFreqConn;

    // --- Scope Telemetry controls/state ---
    bool m_scopeTelemetryRunning = false;
    bool m_scopeTelemetryPaused = false;

    QPushButton *m_scopeBtnStartStop = nullptr;
    QCheckBox *m_modLiveCheck = nullptr;
    QPushButton *m_scopeBtnPause = nullptr;
    QPushButton *m_scopeBtnClear = nullptr;
    QCheckBox *m_scopeChkAutoScale = nullptr;
    QComboBox *m_scopeComboWindow = nullptr;
    QSpinBox *m_scopeSpinMaxPts = nullptr;
    QPushButton *m_scopeBtnExport = nullptr;

    QComboBox *m_scopeTelemetrySmoothingCombo = nullptr;
    QComboBox *m_modSmoothingCombo = nullptr;

    // --- MiniScope property controls/state ---
    QComboBox *m_propScopeSignalCombo = nullptr;
    QDoubleSpinBox *m_propScopeWindowSpin = nullptr;
    QCheckBox *m_propScopeAutoScaleCheck = nullptr;
    void setMiniScopePropertyRowsVisible(bool visible);

    void exportActiveTelemetryScopePlotPng();
    QString m_lastTeleScopeExportDir = QString();

    // --- Scope Modulation controls/state ---
    bool m_scopeModulationRunning = false;
    bool m_modDataRequested = false;
    int m_modPollAttempts = 0;
    QTimer *m_modPollTimer = nullptr;

    QPushButton *m_modBtnStartStop = nullptr;
    QPushButton *m_modBtnClear = nullptr;
    QPushButton *m_modBtnExport = nullptr;

    QComboBox *m_modCouplerCombo = nullptr;  // Coupler 1..4 (0..3 data)
    QComboBox *m_modPathCombo = nullptr;     // "forward" / "reflected"
    QSpinBox *m_modSamplesSpin = nullptr;    // number of samples requested
    QSpinBox *m_modRateSpin = nullptr;       // sample rate Hz
    QComboBox *m_modTimebaseCombo = nullptr; // Auto, 10/25/50/100/250/500 ms

    ScopePlotWidget *m_scopePlotModulation = nullptr;

    QString m_lastModScopeExportDir;
    void exportModulationScopePlotPng();

    QLabel *m_modAxisLabel = nullptr;
    QLabel *m_modStatusLabel = nullptr;

    // Scope measurements overlay
    bool m_showScopeMeasurements = false;
    QCheckBox *m_scopeChkMeasurements = nullptr; // Telemetry dock
    QCheckBox *m_modChkMeasurements = nullptr;   // Modulation dock

    void applyScopeMeasurementsOverlay(bool on);

    // --- SWR vs Frequency (manual capture state) ---
    QTimer *m_swrManualCaptureTimer = nullptr;
    QDialog *m_swrManualDialog = nullptr;
    QLabel *m_swrManualStatusLabel = nullptr;
    QPushButton *m_swrManualCaptureBtn = nullptr;

    int m_swrManualRequiredSamples = 9;   // samples per freq step
    int m_swrTrimCount = 3;               // samples to trim from each end
    int m_swrManualCouplerIndex = 0;      // selected coupler index
    double m_swrManualTargetFreqHz = 0.0; // target capture freq
    QVector<double> m_swrManualSwrBuf;    // rolling buffer, size <= requiredSamples
    bool m_swrManualArmed = false;        // true while dialog open

    void startSwrManualDialog(double targetFreqHz);
    void stopSwrManualDialog();
    void onSwrManualCaptureTimerTick();
    void onSwrManualCaptureNow();
    void appendSwrManualSampleIfArmed(const RayTelemetry &t);
    void updateSwrManualDialogText();
    double trimmedMean(const QVector<double> &values, int trimCount) const;
    bool getRigctldEndpoint(QString &host, int &port) const;

    // --- Scope SWR vs Frequency controls/state ---
    SwrVsFreqPlotWidget *m_swrVsFreqPlot = nullptr;

    QDoubleSpinBox *m_swrFreqCurrentMHz = nullptr;
    QDoubleSpinBox *m_swrFreqStartMHz = nullptr;
    QDoubleSpinBox *m_swrFreqEndMHz = nullptr;
    QSpinBox *m_swrFreqNumPts = nullptr;
    QCheckBox *m_swrFreqChkMeasurements = nullptr;
    QCheckBox *m_swrFreqChkAutoScale = nullptr;

    QComboBox *m_swrFreqCouplerCombo = nullptr;
    QComboBox *m_swrFreqUnitsCombo = nullptr;
    QComboBox *m_swrFreqSmoothingCombo = nullptr;
    QComboBox *m_swrFreqModeCombo = nullptr;

    QPushButton *m_swrFreqBtnManualCapture = nullptr;
    QPushButton *m_swrFreqBtnClear = nullptr;
    QPushButton *m_swrFreqBtnExport = nullptr;

    QString m_lastSwrFreqExportDir;
    QLabel *m_swrManualInstructionLabel = nullptr;

    // ---- SWR vs Frequency (Auto via rigctld TCP) ----
    QPushButton *m_swrFreqBtnAutoStart = nullptr;
    QPushButton *m_swrFreqBtnAutoStop = nullptr;

    QSpinBox *m_swrFreqAutoSettleMs = nullptr;  // optional UI control
    QSpinBox *m_swrFreqAutoTimeoutMs = nullptr; // optional UI control

    raypm::RigCtldClient *m_rigCtld = nullptr;
    QTimer *m_rigFreqPollTimer = nullptr;

    QHash<raypm::RigCtldClient::CommandId, std::function<void(bool, const QStringList &, int)>>
        m_rigPending;

    bool m_swrAutoActive = false;

    // Plot frequencies (X axis): start..end (inclusive)
    QVector<double> m_swrAutoStepFreqHz;

    // Rig command frequencies: (start+delta)..(end-delta)
    QVector<double> m_swrAutoRigStepFreqHz;

    int m_swrAutoStepIndex = -1;

    double m_swrAutoOriginalFreqHz = std::numeric_limits<double>::quiet_NaN();

    QTimer *m_swrAutoCaptureTimer = nullptr;
    QElapsedTimer m_swrAutoWaitTimer;

    // Used to ignore “too-early” samples right after a rig frequency change
    QElapsedTimer m_swrAutoArmTimer;

    // Updater
    Updater *m_updater = nullptr;

    // Auto sweep tuning knobs (keep simple, no UI required)
    int m_swrAutoMinSettleMs = 20;        // ignore samples for this long after freq set
    int m_swrAutoMinSamples = 3;          // earliest we can accept a point
    double m_swrAutoStableSpan = 0.05;    // SWR stable if (max-min) <= this
    double m_swrAutoEdgeDeltaHz = 5000.0; // 5 kHz inside band edges

    // helpers
    void startSwrAutoSweep();
    void stopSwrAutoSweep(bool restoreFreq, const QString &reason = QString());
    void swrAutoSetNextFrequency();
    void swrAutoArmCapture(double targetHz);
    void onSwrAutoCapturePoll();
    double swrVsFreqUnitsYFromSwr(double swr) const;
    quint64 rigSend(const QString &cmd, std::function<void(bool, const QStringList &, int)> cb);
    quint64 rigSend(const QString &cmd,
                    int timeoutMs,
                    std::function<void(bool, const QStringList &, int)> cb);
    void ensureRigCtldClient();

    // Manual sweep session state
    QVector<double> m_swrManualStepFreqHz; // size = Num Points
    int m_swrManualStepIndex = -1;         // -1 = not sweeping
    void cleanupSwrManualDialogUiOnly();

    // Auto-started rigctld (Hamlib) process
    QProcess *m_rigctldProcess = nullptr;
    bool m_rigctldStartedByApp = false;
    bool m_swrAutoStartPromptShown = false;

    // Hamlib / rigctld helpers (MainWindow-side prompt)
    QString hamlibExePath(const QString &exeName) const;
    bool startRigctldForSerialFromSettings(QString *errorOut);
    bool startRigctldFromSettings(QString *errorOut);
    bool isLocalHostName(const QString &host) const;

    // --- Auto (Hamlib) frequency verify (read-back) ---
    QTimer *m_swrAutoVerifyTimer = nullptr;
    bool m_swrAutoVerifyInFlight = false;

    double m_swrAutoVerifyTargetHz = 0.0;
    double m_swrAutoVerifyPlotHz = 0.0;
    int m_swrAutoVerifyElapsedMs = 0;

    // knobs (simple constants)
    int m_swrAutoVerifyPollMs = 20;     // how often to poll "f"
    int m_swrAutoVerifyMaxMs = 400;     // total verify time budget
    double m_swrAutoVerifyTolHz = 50.0; // acceptable error from target

    void swrAutoBeginVerifyThenArm(double plotHz, double targetHz);
    void swrAutoVerifyPoll();
    static bool parseRigHzFromLines(const QStringList &lines, double *outHz);

    // FreqWidget Hamlib integration
    void updateRigFreqPollTimer();
    void pollRigFrequencyForFreqWidgets();
    void applyRigFrequencyToFreqWidgets(double hz, bool valid);

    // --- Scope Spectrum controls/state ---
    QDockWidget *m_scopeSpectrumDock = nullptr;

    bool m_scopeSpectrumRunning = false;
    bool m_specDataRequested = false;
    int m_specPollAttempts = 0;
    QTimer *m_specPollTimer = nullptr;

    int m_specReqCoupler = 0;
    QString m_specReqPath; // "fwd" / "ref"
    int m_specReqSamples = 0;
    int m_specReqRateHz = 0;

    QPushButton *m_specBtnStartStop = nullptr;
    QPushButton *m_specBtnClear = nullptr;
    QPushButton *m_specBtnExport = nullptr;

    QComboBox *m_specCouplerCombo = nullptr;
    QComboBox *m_specPathCombo = nullptr;
    QComboBox *m_specSamplesCombo = nullptr; // power-of-two list
    QComboBox *m_specRateCombo = nullptr;    // 24000/48000/96000
    QComboBox *m_specWindowCombo = nullptr;  // Rect/Hann (for next task)
    QComboBox *m_specScaleCombo = nullptr;   // Linear/dB

    SpectrumPlotWidget *m_scopePlotSpectrum = nullptr;
    QLabel *m_specStatusLabel = nullptr;

    QString m_lastSpecScopeExportDir;
    void exportSpectrumScopePlotPng();

    QDockWidget *m_ampAnalyzerDock = nullptr;
    AmplifierAnalyzerWidget *m_ampAnalyzerWidget = nullptr;

    // AuxAnalogWidget helpers
    int auxAnalogIndexFromSource(const QString &source) const;
    void applyAuxAnalogConfigFromLayout();
    bool m_applyLayoutAuxAnalogPending = false;
    bool m_forceApplyAuxConfigOnce = false;
    QSet<int> m_applyLayoutAuxAnalogReceived;
    bool auxAnalogConfigReady() const;
    void tryApplyAuxAnalogConfigFromLayout();

    // AuxDigitalWidget helpers
    void connectAuxDigitalWidgets();
    int auxDigitalIndexFromSource(const QString &source) const;
    void applyAuxDigitalConfigFromLayout();

    // Internal layout-design property adapters need to drive the property panel
    // (e.g. GainWidgetPropertyAdapter uses m_propFormLayout and the gain combos).
    // Property adapters need access to the property-panel controls
    friend class GainWidgetPropertyAdapter;
    friend class AuxSensorWidgetPropertyAdapter;
    friend class AuxVoltageWidgetPropertyAdapter;
    friend class AuxDigitalWidgetPropertyAdapter;
    friend class ForwardRefPanelPropertyAdapter;
    friend class PowerBarWidgetPropertyAdapter;
    friend class PowerBarVertWidgetPropertyAdapter;
    friend class MiniScopeWidgetPropertyAdapter;
    friend class SpacerWidgetPropertyAdapter;
    friend class FreqWidgetPropertyAdapter;
    friend class AnalogPowerMeterPropertyAdapter;
};

} // namespace raypm
