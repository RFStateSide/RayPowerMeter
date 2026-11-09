// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QDockWidget>
#include <QElapsedTimer>
#include <QMap>
#include <QPainter>
#include <QTimer>

#include "core/AppSettings.h"
#include "core/RuntimeDataModel.h"
#include "core/rigctldclient.h"
#include "core/tonegenerator.h"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QTableView;

class AmplifierAnalyzerPlotWidget;
class AmplifierAnalyzerTableModel;
class ToneGenerator;

class AmplifierAnalyzerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AmplifierAnalyzerWidget(raypm::RuntimeDataModel *runtimeModel,
                                     raypm::RigCtldClient *rigCtld,
                                     const raypm::AppSettings &settings,
                                     bool isDarkTheme,
                                     QWidget *parent = nullptr);

    void setSettings(const raypm::AppSettings &s) { m_settings = s; }
    void setDarkTheme(bool on) { m_isDarkTheme = on; }

signals:
    void requestCloseMe(); // used by dock to uncheck/cleanup if desired

private slots:
    void onTelemetryChanged(const raypm::RayTelemetry &t);

    void onStartClicked();
    void onStopClicked();
    void onCaptureClicked();
    void onClearClicked();
    void onExportCsvClicked();
    void onExportPngClicked();

    void onRigCommandFinished(raypm::RigCtldClient::CommandId id,
                              bool ok,
                              const QStringList &lines,
                              int rprt);

    void onSweepTick();

private:
    enum class Mode { Manual = 0, GainFlatness = 1, Linearity = 2, Compression1dB = 3 };

    enum class SweepState {
        Idle = 0,

        ReadOrigMode,
        SetCwMode,

        SetAudioMode,
        StartAudioTone,

        PreTxOn,
        SetFreq,
        SetPower,
        Settling,
        Capture,
        NextStep,

        TxOff,

        RestoreOrigMode,

        Done,
        Aborted
    };

    enum class TxMethod {
        MoxPtt = 0,        // Use Rig TX ON/OFF templates (default T 1 / T 0)
        CarrierTuneCw = 1, // Save mode -> M CW 0 -> TX ON -> ... -> TX OFF -> restore mode
        AudioToneSsb
        = 2 // Save mode -> M USB 2400 -> start audio tone -> TX ON -> ... -> TX OFF -> stop tone -> restore
    };

    struct SweepStep
    {
        double freqHz = 0.0;
        double drivePct = -1.0; // 0..100 (%), -1 means unchanged
    };

    // ---- 1 dB Compression state ----
    struct P1dBState
    {
        bool haveRef = false;
        bool found = false;

        int refN = 3;             // use first 3 valid points
        double gainRefDb = 0.0;   // average of first refN gains
        double thresholdDb = 0.0; // gainRefDb - 1.0

        // Interpolated results:
        double drivePct = 0.0;
        double pinDutW = 0.0;
        double poutW = 0.0;
        double gainDb = 0.0; // should be thresholdDb

        QString statusText; // "Running", "Found", "Not found", etc.
    };

    void resetP1dB();
    void updatePlotMeasurementsOverlay() const;
    void processP1dBAfterPoint(); // called after a point is added (Capture)

    // State
    mutable P1dBState m_p1db;

    void buildUi();
    void rebuildSweepPlan();
    void beginSweep();
    void abortSweep(const QString &why);
    void finishSweep(bool ok, const QString &msg);

    bool hamlibReadyOrWarn() const;
    bool ensureRigConnectedOrWarn();
    QString resolveRigHost() const;
    int resolveRigPort() const;

    // Telemetry sampling helpers
    double readCouplerForwardW(const raypm::RayTelemetry &t, int couplerIndex) const;
    double readAuxValue(const raypm::RayTelemetry &t, int auxIndex) const;
    double readTelemetryFreqMHz(const raypm::RayTelemetry &t) const;

    // Calculations
    static double db10(double ratio);
    static double attnCorrectPin(double pinRigW, double attnDb);
    static double efficiencyPct(double poutW, double volts, double amps);

    bool checkSafetyStops(const raypm::RayTelemetry &t,
                          double pinDutW,
                          double poutW,
                          double amps,
                          double tempC,
                          QString *reasonOut) const;

    // Rig command helpers (template-based)
    raypm::RigCtldClient::CommandId sendRig(const QString &cmd, int timeoutMs = 2500);
    QString expandTemplate(const QString &tmpl, double drivePct) const;

private:
    raypm::RuntimeDataModel *m_runtime = nullptr;
    raypm::RigCtldClient *m_rig = nullptr;
    raypm::AppSettings m_settings{};
    bool m_isDarkTheme = true;

    raypm::RayTelemetry m_lastTelemetry{};
    bool m_hasTelemetry = false;

    // UI
    QComboBox *m_modeCombo = nullptr;

    QComboBox *m_inCouplerCombo = nullptr;
    QComboBox *m_outCouplerCombo = nullptr;

    QComboBox *m_auxVCombo = nullptr;
    QComboBox *m_auxICombo = nullptr;
    QComboBox *m_auxTCombo = nullptr;

    QDoubleSpinBox *m_attnDb = nullptr;

    QComboBox *m_txMethodCombo = nullptr;
    // Carrier-tune (CW) support
    bool m_restoreModeAfterSweep = false;
    QString m_savedMode;     // e.g. "USB"
    int m_savedPassband = 0; // e.g. 2400

    // Gain flatness sweep inputs
    QDoubleSpinBox *m_startMHz = nullptr;
    QDoubleSpinBox *m_endMHz = nullptr;
    QSpinBox *m_points = nullptr;

    // Linearity sweep inputs
    QDoubleSpinBox *m_fixedMHz = nullptr;
    QDoubleSpinBox *m_driveLow = nullptr; // user units (0..100 default)
    QDoubleSpinBox *m_driveHigh = nullptr;
    QSpinBox *m_driveSteps = nullptr;

    // Safety
    QDoubleSpinBox *m_maxPinW = nullptr;
    QDoubleSpinBox *m_maxPoutW = nullptr;
    QDoubleSpinBox *m_maxAmps = nullptr;
    QDoubleSpinBox *m_maxTempC = nullptr;

    // Timing
    QSpinBox *m_settleMs = nullptr;
    QSpinBox *m_timeoutMs = nullptr;

    // Rig command templates
    QLineEdit *m_cmdTxOn = nullptr;
    QLineEdit *m_cmdTxOff = nullptr;
    QLineEdit *m_cmdSetPowerTemplate = nullptr; // uses {p}

    QPushButton *m_btnStart = nullptr;
    QPushButton *m_btnStop = nullptr;
    QPushButton *m_btnCapture = nullptr;
    QPushButton *m_btnClear = nullptr;
    QPushButton *m_btnExportCsv = nullptr;
    QPushButton *m_btnExportPng = nullptr;

    QCheckBox *m_chkAutoScale = nullptr;
    QCheckBox *m_chkShowPoints = nullptr;
    QCheckBox *m_chkMeasurements = nullptr;

    AmplifierAnalyzerPlotWidget *m_plot = nullptr;
    QTableView *m_table = nullptr;
    AmplifierAnalyzerTableModel *m_model = nullptr;

    // Sweep engine
    QTimer *m_sweepTimer = nullptr;
    QVector<SweepStep> m_steps;
    int m_stepIndex = -1;
    SweepState m_state = SweepState::Idle;

    // Telemetry sequence counter and require it to advance before recording a point
    quint64 m_telemetrySeq = 0;    // increments on every telemetryChanged
    quint64 m_captureStartSeq = 0; // seq when we begin waiting for a fresh sample

    QElapsedTimer m_settleTimer;
    QElapsedTimer m_captureTimer; // Used to wait for RF before plotting
    // Audio Tone TX: first point needs extra time for audio/ALC/TX chain to stabilize
    bool m_initialToneWarmupPending = false;
    int m_initialToneWarmupMs = 1000;

    QMap<raypm::RigCtldClient::CommandId, QString> m_pendingCmdTags;

    TxMethod txMethod() const;
    void requestTxOn();
    void requestTxOff();

    // Tone generator (for AudioToneSsb)
    ToneGenerator *m_toneGen = nullptr;

    // UI: tone controls (used when TxMethod::AudioToneSsb)
    QComboBox *m_toneDeviceCombo = nullptr;
    QDoubleSpinBox *m_toneLevelSpin = nullptr;
    QComboBox *m_toneCountCombo = nullptr;        // 1-tone / 2-tone
    QDoubleSpinBox *m_toneFreqHz = nullptr;       // base frequency
    QDoubleSpinBox *m_twoToneSpacingHz = nullptr; // spacing for 2-tone

    QString m_lastAnalyzerExportDir;

    void ensureToneGenerator();
    void refreshToneDevices();
    void applyToneSettingsToGenerator();
    void startAnalyzerTone();
    void stopAnalyzerTone();
};
