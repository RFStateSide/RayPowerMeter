// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "widgets/amplifieranalyzerwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>

#include "widgets/amplifieranalyzerplotwidget.h"
#include "widgets/amplifieranalyzertablemodel.h"

AmplifierAnalyzerWidget::AmplifierAnalyzerWidget(raypm::RuntimeDataModel *runtimeModel,
                                                 raypm::RigCtldClient *rigCtld,
                                                 const raypm::AppSettings &settings,
                                                 bool isDarkTheme,
                                                 QWidget *parent)
    : QWidget(parent)
    , m_runtime(runtimeModel)
    , m_rig(rigCtld)
    , m_settings(settings)
    , m_isDarkTheme(isDarkTheme)
{
    buildUi();

    if (m_runtime) {
        connect(m_runtime,
                &raypm::RuntimeDataModel::telemetryChanged,
                this,
                &AmplifierAnalyzerWidget::onTelemetryChanged);
    }

    if (m_rig) {
        connect(m_rig,
                &raypm::RigCtldClient::commandFinished,
                this,
                &AmplifierAnalyzerWidget::onRigCommandFinished);

        // If rigctld disconnects mid-sweep, abort so Start becomes usable again
        connect(m_rig, &raypm::RigCtldClient::connectedChanged, this, [this](bool connected) {
            if (!connected && m_state != SweepState::Idle) {
                abortSweep(tr("Rig control disconnected."));
            }
        });

        // Surface errors while running (helps debug “stuck after first run”)
        connect(m_rig, &raypm::RigCtldClient::errorOccurred, this, [this](const QString &msg) {
            if (m_state != SweepState::Idle) {
                abortSweep(tr("Rig control error: %1").arg(msg));
            }
        });
    }

    m_sweepTimer = new QTimer(this);
    m_sweepTimer->setInterval(20);
    connect(m_sweepTimer, &QTimer::timeout, this, &AmplifierAnalyzerWidget::onSweepTick);
}

void AmplifierAnalyzerWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // Row: Mode + buttons
    auto *row1 = new QHBoxLayout;
    row1->setContentsMargins(0, 0, 0, 0);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("Manual"), int(Mode::Manual));
    m_modeCombo->addItem(tr("Gain Flatness (Freq Sweep)"), int(Mode::GainFlatness));
    m_modeCombo->addItem(tr("Linearity (Drive Sweep)"), int(Mode::Linearity));
    m_modeCombo->addItem(tr("1 dB Compression"), int(Mode::Compression1dB));
    m_modeCombo->setCurrentIndex(0);

    m_btnStart = new QPushButton(tr("Start"), this);
    m_btnStop = new QPushButton(tr("Stop"), this);
    m_btnCapture = new QPushButton(tr("Capture Point"), this);
    m_btnClear = new QPushButton(tr("Clear"), this);
    m_btnExportCsv = new QPushButton(tr("Export CSV"), this);
    m_btnExportPng = new QPushButton(tr("Export PNG"), this);

    m_btnStop->setEnabled(false);

    connect(m_btnStart, &QPushButton::clicked, this, &AmplifierAnalyzerWidget::onStartClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &AmplifierAnalyzerWidget::onStopClicked);
    connect(m_btnCapture, &QPushButton::clicked, this, &AmplifierAnalyzerWidget::onCaptureClicked);
    connect(m_btnClear, &QPushButton::clicked, this, &AmplifierAnalyzerWidget::onClearClicked);
    connect(m_btnExportCsv,
            &QPushButton::clicked,
            this,
            &AmplifierAnalyzerWidget::onExportCsvClicked);
    connect(m_btnExportPng,
            &QPushButton::clicked,
            this,
            &AmplifierAnalyzerWidget::onExportPngClicked);

    row1->addWidget(new QLabel(tr("Mode:"), this));
    row1->addWidget(m_modeCombo);
    row1->addSpacing(10);
    row1->addWidget(m_btnStart);
    row1->addWidget(m_btnStop);
    row1->addWidget(m_btnCapture);
    row1->addWidget(m_btnClear);
    row1->addStretch(1);
    row1->addWidget(m_btnExportCsv);
    row1->addWidget(m_btnExportPng);

    root->addLayout(row1);

    // Row: selectors + attenuator
    auto *row2 = new QHBoxLayout;
    row2->setContentsMargins(0, 0, 0, 0);

    m_inCouplerCombo = new QComboBox(this);
    m_outCouplerCombo = new QComboBox(this);

    for (int i = 0; i < 4; ++i) {
        m_inCouplerCombo->addItem(tr("Coupler %1 (Input)").arg(i + 1), i);
        m_outCouplerCombo->addItem(tr("Coupler %1 (Output)").arg(i + 1), i);
    }
    m_inCouplerCombo->setCurrentIndex(0);
    m_outCouplerCombo->setCurrentIndex(1);

    m_auxVCombo = new QComboBox(this);
    m_auxICombo = new QComboBox(this);
    m_auxTCombo = new QComboBox(this);
    for (int i = 0; i < 3; ++i) {
        m_auxVCombo->addItem(tr("Aux %1").arg(i + 1), i);
        m_auxICombo->addItem(tr("Aux %1").arg(i + 1), i);
        m_auxTCombo->addItem(tr("Aux %1").arg(i + 1), i);
    }
    m_auxVCombo->setCurrentIndex(0);
    m_auxICombo->setCurrentIndex(1);
    m_auxTCombo->setCurrentIndex(2);

    m_attnDb = new QDoubleSpinBox(this);
    m_attnDb->setRange(0.0, 80.0);
    m_attnDb->setDecimals(1);
    m_attnDb->setSingleStep(0.5);
    m_attnDb->setSuffix(tr(" dB"));
    m_attnDb->setValue(10.0); // default attenuator value (adjust as needed)

    row2->addWidget(new QLabel(tr("Input Coupler:"), this));
    row2->addWidget(m_inCouplerCombo);
    row2->addSpacing(8);
    row2->addWidget(new QLabel(tr("Output Coupler:"), this));
    row2->addWidget(m_outCouplerCombo);
    row2->addSpacing(12);
    row2->addWidget(new QLabel(tr("V:"), this));
    row2->addWidget(m_auxVCombo);
    row2->addWidget(new QLabel(tr("I:"), this));
    row2->addWidget(m_auxICombo);
    row2->addWidget(new QLabel(tr("T:"), this));
    row2->addWidget(m_auxTCombo);
    row2->addSpacing(12);
    row2->addWidget(new QLabel(tr("Input Attn:"), this));
    row2->addWidget(m_attnDb);
    row2->addStretch(1);

    root->addLayout(row2);

    // Sweep + Safety + Hamlib command templates
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);

    auto makeGridLabel = [this](const QString &text) -> QLabel * {
        auto *lbl = new QLabel(text, this);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        return lbl;
    };

    int r = 0;

    // Gain flatness sweep
    m_startMHz = new QDoubleSpinBox(this);
    m_endMHz = new QDoubleSpinBox(this);
    m_points = new QSpinBox(this);

    for (auto *sb : {m_startMHz, m_endMHz}) {
        sb->setRange(0.001, 1000.0);
        sb->setDecimals(3);
        sb->setSingleStep(0.001);
        sb->setSuffix(tr(" MHz"));
    }
    m_startMHz->setValue(14.000);
    m_endMHz->setValue(14.350);

    m_points->setRange(2, 1000);
    m_points->setValue(25);

    grid->addWidget(makeGridLabel(tr("Freq Sweep Start:")), r, 0);
    grid->addWidget(m_startMHz, r, 1);
    grid->addWidget(makeGridLabel(tr("End:")), r, 2);
    grid->addWidget(m_endMHz, r, 3);
    grid->addWidget(makeGridLabel(tr("Points:")), r, 4);
    grid->addWidget(m_points, r, 5);
    r++;

    // Linearity sweep
    m_fixedMHz = new QDoubleSpinBox(this);
    m_fixedMHz->setRange(0.001, 1000.0);
    m_fixedMHz->setDecimals(3);
    m_fixedMHz->setSingleStep(0.001);
    m_fixedMHz->setSuffix(tr(" MHz"));
    m_fixedMHz->setValue(14.200);

    m_driveLow = new QDoubleSpinBox(this);
    m_driveHigh = new QDoubleSpinBox(this);
    m_driveSteps = new QSpinBox(this);

    m_driveLow->setRange(0.0, 100.0);
    m_driveHigh->setRange(0.0, 100.0);
    m_driveLow->setDecimals(1);
    m_driveHigh->setDecimals(1);
    m_driveLow->setSuffix(tr(" %"));
    m_driveHigh->setSuffix(tr(" %"));
    m_driveLow->setValue(5.0);
    m_driveHigh->setValue(15.0);

    m_driveSteps->setRange(2, 200);
    m_driveSteps->setValue(10);

    grid->addWidget(makeGridLabel(tr("Drive Sweep Freq:")), r, 0);
    grid->addWidget(m_fixedMHz, r, 1);
    grid->addWidget(makeGridLabel(tr("Low:")), r, 2);
    grid->addWidget(m_driveLow, r, 3);
    grid->addWidget(makeGridLabel(tr("High:")), r, 4);
    grid->addWidget(m_driveHigh, r, 5);
    r++;

    grid->addWidget(makeGridLabel(tr("Drive Steps:")), r, 0);
    grid->addWidget(m_driveSteps, r, 1);
    r++;

    // Safety stops
    m_maxPinW = new QDoubleSpinBox(this);
    m_maxPoutW = new QDoubleSpinBox(this);
    m_maxAmps = new QDoubleSpinBox(this);
    m_maxTempC = new QDoubleSpinBox(this);

    m_maxPinW->setRange(0.0, 5000.0);
    m_maxPoutW->setRange(0.0, 10000.0);
    m_maxAmps->setRange(0.0, 200.0);
    m_maxTempC->setRange(-40.0, 200.0);

    m_maxPinW->setSuffix(tr(" W"));
    m_maxPoutW->setSuffix(tr(" W"));
    m_maxAmps->setSuffix(tr(" A"));
    m_maxTempC->setSuffix(tr(" C"));

    m_maxPinW->setValue(
        5.0); // typical small-signal input power limit for a linear amp (adjust as needed)
    m_maxPoutW->setValue(1000.0); // typical max output power for a 1kW amplifier (adjust as needed)
    m_maxAmps->setValue(20.0); // typical max current for a solid-state amplifier (adjust as needed)
    m_maxTempC->setValue(
        65.0); // typical max operating temperature for many amplifiers (adjust as needed)

    grid->addWidget(makeGridLabel(tr("Safety: Max Pin(DUT):")), r, 0);
    grid->addWidget(m_maxPinW, r, 1);
    grid->addWidget(makeGridLabel(tr("Max Pout:")), r, 2);
    grid->addWidget(m_maxPoutW, r, 3);
    grid->addWidget(makeGridLabel(tr("Max I:")), r, 4);
    grid->addWidget(m_maxAmps, r, 5);
    r++;

    grid->addWidget(makeGridLabel(tr("Safety: Max Temp:")), r, 0);
    grid->addWidget(m_maxTempC, r, 1);
    r++;

    // Timing
    m_settleMs = new QSpinBox(this);
    m_timeoutMs = new QSpinBox(this);
    m_settleMs->setRange(0, 2000);
    m_settleMs->setValue(150);
    m_timeoutMs->setRange(200, 15000);
    m_timeoutMs->setValue(2500);

    grid->addWidget(makeGridLabel(tr("Settle (ms):")), r, 0);
    grid->addWidget(m_settleMs, r, 1);
    grid->addWidget(makeGridLabel(tr("Cmd Timeout (ms):")), r, 2);
    grid->addWidget(m_timeoutMs, r, 3);
    r++;

    // TX Method + Rig TX ON/OFF (SAME ROW)
    m_txMethodCombo = new QComboBox(this);
    m_txMethodCombo->addItem(tr("MOX/PTT"), int(TxMethod::MoxPtt));
    m_txMethodCombo->addItem(tr("Carrier Tune (CW + MOX)"), int(TxMethod::CarrierTuneCw));
    m_txMethodCombo->addItem(tr("Audio Tone (SSB + MOX)"), int(TxMethod::AudioToneSsb));
    m_txMethodCombo->setCurrentIndex(0);

    m_cmdTxOn = new QLineEdit(this);
    m_cmdTxOff = new QLineEdit(this);
    m_cmdTxOn->setText(QStringLiteral("T 1"));
    m_cmdTxOff->setText(QStringLiteral("T 0"));
    m_cmdTxOn->setToolTip(tr("rigctld command to key TX (default: T 1)"));
    m_cmdTxOff->setToolTip(tr("rigctld command to unkey TX (default: T 0)"));

    grid->addWidget(makeGridLabel(tr("TX Method:")), r, 0);
    grid->addWidget(m_txMethodCombo, r, 1);
    grid->addWidget(makeGridLabel(tr("TX ON:")), r, 2);
    grid->addWidget(m_cmdTxOn, r, 3);
    grid->addWidget(makeGridLabel(tr("TX OFF:")), r, 4);
    grid->addWidget(m_cmdTxOff, r, 5);
    r++;

    // Tone controls row (used for AudioToneSsb)
    m_toneDeviceCombo = new QComboBox(this);
    m_toneCountCombo = new QComboBox(this);
    m_toneCountCombo->addItem(tr("1-tone"), 1);
    m_toneCountCombo->addItem(tr("2-tone"), 2);
    m_toneCountCombo->setCurrentIndex(0);

    m_toneFreqHz = new QDoubleSpinBox(this);
    m_toneFreqHz->setRange(50.0, 5000.0);
    m_toneFreqHz->setDecimals(1);
    m_toneFreqHz->setSingleStep(10.0);
    m_toneFreqHz->setSuffix(tr(" Hz"));
    m_toneFreqHz->setValue(700.0);

    m_twoToneSpacingHz = new QDoubleSpinBox(this);
    m_twoToneSpacingHz->setRange(10.0, 4000.0);
    m_twoToneSpacingHz->setDecimals(1);
    m_twoToneSpacingHz->setSingleStep(10.0);
    m_twoToneSpacingHz->setSuffix(tr(" Hz"));
    m_twoToneSpacingHz->setValue(1900.0);

    m_toneLevelSpin = new QDoubleSpinBox(this);
    m_toneLevelSpin->setRange(0.0, 1.25);
    m_toneLevelSpin->setDecimals(2);
    m_toneLevelSpin->setSingleStep(0.05);
    m_toneLevelSpin->setValue(1.0);

    grid->addWidget(makeGridLabel(tr("Tone Out:")), r, 0);
    grid->addWidget(m_toneDeviceCombo, r, 1);
    grid->addWidget(makeGridLabel(tr("Level:")), r, 2);
    grid->addWidget(m_toneLevelSpin, r, 3);
    grid->addWidget(makeGridLabel(tr("Tones:")), r, 4);
    grid->addWidget(m_toneCountCombo, r, 5);
    grid->addWidget(makeGridLabel(tr("Freq/Spacing:")), r, 6);

    auto *toneBox = new QWidget(this);
    auto *toneRow = new QHBoxLayout(toneBox);
    toneRow->setContentsMargins(0, 0, 0, 0);
    toneRow->addWidget(m_toneFreqHz);
    toneRow->addSpacing(6);
    toneRow->addWidget(m_twoToneSpacingHz);

    grid->addWidget(toneBox, r, 7);
    r++;

    // Rig drive template
    m_cmdSetPowerTemplate = new QLineEdit(this);
    m_cmdSetPowerTemplate->setText(QStringLiteral("L RFPOWER {pn}"));
    m_cmdSetPowerTemplate->setToolTip(
        tr("Template for drive control. Use {p} for 0..100 (%) or {pn} for 0.0..1.0."));

    grid->addWidget(makeGridLabel(tr("Rig Set Power Template:")), r, 0);
    grid->addWidget(m_cmdSetPowerTemplate, r, 1, 1, 5);
    r++;

    // Plot toggles
    auto *row3 = new QHBoxLayout;
    m_chkAutoScale = new QCheckBox(tr("Auto-scale Y"), this);
    m_chkAutoScale->setChecked(true);
    m_chkShowPoints = new QCheckBox(tr("Show Points"), this);
    m_chkShowPoints->setChecked(false);
    m_chkMeasurements = new QCheckBox(tr("Measures"), this);
    m_chkMeasurements->setChecked(true);

    row3->addWidget(m_chkAutoScale);
    row3->addWidget(m_chkShowPoints);
    row3->addWidget(m_chkMeasurements);
    row3->addStretch(1);

    connect(m_chkAutoScale, &QCheckBox::toggled, this, [this](bool on) {
        if (m_plot)
            m_plot->setAutoScaleY(on);
    });
    connect(m_chkShowPoints, &QCheckBox::toggled, this, [this](bool on) {
        if (m_plot)
            m_plot->setShowPoints(on);
    });
    connect(m_chkMeasurements, &QCheckBox::toggled, this, [this](bool on) {
        if (m_plot)
            m_plot->setShowMeasurements(on);
    });

    // Tone UI -> apply to generator (lazy init)
    connect(m_txMethodCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        // optional: could enable/disable tone controls based on method
        const bool toneOn = (txMethod() == TxMethod::AudioToneSsb);
        if (m_toneDeviceCombo)
            m_toneDeviceCombo->setEnabled(toneOn);
        if (m_toneCountCombo)
            m_toneCountCombo->setEnabled(toneOn);
        if (m_toneFreqHz)
            m_toneFreqHz->setEnabled(toneOn);
        if (m_twoToneSpacingHz)
            m_twoToneSpacingHz->setEnabled(toneOn);
    });
    connect(m_toneDeviceCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        applyToneSettingsToGenerator();
    });
    connect(m_toneCountCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        applyToneSettingsToGenerator();
    });
    connect(m_toneFreqHz, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyToneSettingsToGenerator();
    });
    connect(m_twoToneSpacingHz,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this](double) { applyToneSettingsToGenerator(); });

    root->addLayout(grid);
    root->addLayout(row3);

    // Plot + Table
    m_plot = new AmplifierAnalyzerPlotWidget(this);
    m_plot->setTitle(tr("Amplifier Analyzer"));
    m_plot->setXLabel(tr("Frequency (MHz)"));
    m_plot->setYLabel(tr("Gain (dB)"));
    m_plot->setAutoScaleY(true);

    m_model = new AmplifierAnalyzerTableModel(this);

    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);

    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_plot);
    split->addWidget(m_table);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    root->addWidget(split, 1);

    setLayout(root);

    // Initial enable/disable for tone controls
    const bool toneOn = (txMethod() == TxMethod::AudioToneSsb);
    if (m_toneDeviceCombo)
        m_toneDeviceCombo->setEnabled(toneOn);
    if (m_toneCountCombo)
        m_toneCountCombo->setEnabled(toneOn);
    if (m_toneFreqHz)
        m_toneFreqHz->setEnabled(toneOn);
    if (m_twoToneSpacingHz)
        m_twoToneSpacingHz->setEnabled(toneOn);

    // Populate audio devices lazily once UI exists
    ensureToneGenerator();
    applyToneSettingsToGenerator();
}

void AmplifierAnalyzerWidget::onTelemetryChanged(const raypm::RayTelemetry &t)
{
    m_lastTelemetry = t;
    m_hasTelemetry = true;
    ++m_telemetrySeq;

    // Safety: if sweeping, abort immediately on trip
    if (m_state != SweepState::Idle) {
        QString reason;
        // We check stops in onSweepTick too, but this gives fast reaction.
        const int inIdx = m_inCouplerCombo ? m_inCouplerCombo->currentData().toInt() : 0;
        const int outIdx = m_outCouplerCombo ? m_outCouplerCombo->currentData().toInt() : 1;
        const double pinRig = readCouplerForwardW(t, inIdx);
        const double pout = readCouplerForwardW(t, outIdx);
        const double pinDut = attnCorrectPin(pinRig, m_attnDb ? m_attnDb->value() : 0.0);

        const int vIdx = m_auxVCombo ? m_auxVCombo->currentData().toInt() : 0;
        const int iIdx = m_auxICombo ? m_auxICombo->currentData().toInt() : 1;
        const int tIdx = m_auxTCombo ? m_auxTCombo->currentData().toInt() : 2;

        const double v = readAuxValue(t, vIdx);
        const double a = readAuxValue(t, iIdx);
        const double tc = readAuxValue(t, tIdx);

        if (!checkSafetyStops(t, pinDut, pout, a, tc, &reason)) {
            abortSweep(reason);
        }
    }
}

void AmplifierAnalyzerWidget::onStartClicked()
{
    beginSweep();
}

void AmplifierAnalyzerWidget::onStopClicked()
{
    qInfo() << "SweepTick state =" << int(m_state);
    abortSweep(tr("User stopped"));
}

void AmplifierAnalyzerWidget::onCaptureClicked()
{
    if (!m_hasTelemetry) {
        QMessageBox::warning(this, tr("No telemetry"), tr("No telemetry received yet."));
        return;
    }

    // Manual capture: one point from latest telemetry
    const int inIdx = m_inCouplerCombo ? m_inCouplerCombo->currentData().toInt() : 0;
    const int outIdx = m_outCouplerCombo ? m_outCouplerCombo->currentData().toInt() : 1;

    const double pinRigW = readCouplerForwardW(m_lastTelemetry, inIdx);
    const double poutW = readCouplerForwardW(m_lastTelemetry, outIdx);

    const double attnDb = m_attnDb ? m_attnDb->value() : 0.0;
    const double pinDutW = attnCorrectPin(pinRigW, attnDb);

    const int vIdx = m_auxVCombo ? m_auxVCombo->currentData().toInt() : 0;
    const int iIdx = m_auxICombo ? m_auxICombo->currentData().toInt() : 1;
    const int tIdx = m_auxTCombo ? m_auxTCombo->currentData().toInt() : 2;

    const double volts = readAuxValue(m_lastTelemetry, vIdx);
    const double amps = readAuxValue(m_lastTelemetry, iIdx);
    const double tempC = readAuxValue(m_lastTelemetry, tIdx);

    QString reason;
    if (!checkSafetyStops(m_lastTelemetry, pinDutW, poutW, amps, tempC, &reason)) {
        QMessageBox::warning(this, tr("Safety stop"), reason);
        return;
    }

    AmplifierAnalyzerTableModel::Point p;
    p.freqMHz = readTelemetryFreqMHz(m_lastTelemetry);
    p.pinRigW = pinRigW;
    p.attnDb = attnDb;
    p.pinDutW = pinDutW;
    p.poutW = poutW;

    p.gainDb = (pinDutW > 0.0 && poutW > 0.0) ? (10.0 * std::log10(poutW / pinDutW)) : 0.0;
    p.volts = volts;
    p.amps = amps;
    p.tempC = tempC;
    p.effPct = efficiencyPct(poutW, volts, amps);

    m_model->addPoint(p);

    // Default plot: Gain vs Frequency
    m_plot->setXLabel(tr("Frequency (MHz)"));
    m_plot->setYLabel(tr("Gain (dB)"));
    m_plot->addPoint(p.freqMHz, p.gainDb);
}

void AmplifierAnalyzerWidget::onClearClicked()
{
    if (m_plot)
        m_plot->clearPoints();
    if (m_model)
        m_model->clear();

    resetP1dB();
    updatePlotMeasurementsOverlay();
}

void AmplifierAnalyzerWidget::onExportCsvClicked()
{
    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString base = QString("AmplifierAnalyzer_%1.csv").arg(ts);

    const QString docsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString startDirFinal = m_lastAnalyzerExportDir.isEmpty() ? docsDir
                                                                    : m_lastAnalyzerExportDir;
    const QString defaultPath = QDir(startDirFinal).filePath(base);

    const QString filePath = QFileDialog::getSaveFileName(this,
                                                          tr("Export CSV"),
                                                          defaultPath,
                                                          tr("CSV (*.csv)"));
    if (filePath.isEmpty())
        return;

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"), tr("Could not open file for writing."));
        return;
    }

    QTextStream out(&f);
    out << "Freq_MHz,PinRig_W,Attn_dB,PinDut_W,Pout_W,Gain_dB,Volts,Amps,TempC,EffPct\n";

    for (const auto &pt : m_model->points()) {
        out << QString::number(pt.freqMHz, 'f', 6) << "," << QString::number(pt.pinRigW, 'f', 3)
            << "," << QString::number(pt.attnDb, 'f', 2) << ","
            << QString::number(pt.pinDutW, 'f', 3) << "," << QString::number(pt.poutW, 'f', 3)
            << "," << QString::number(pt.gainDb, 'f', 3) << "," << QString::number(pt.volts, 'f', 3)
            << "," << QString::number(pt.amps, 'f', 3) << "," << QString::number(pt.tempC, 'f', 2)
            << "," << QString::number(pt.effPct, 'f', 2) << "\n";
    }

    // Remember folder
    m_lastAnalyzerExportDir = QFileInfo(filePath).absolutePath();
}

void AmplifierAnalyzerWidget::onExportPngClicked()
{
    if (!m_plot)
        return;

    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString base = QString("AmplifierAnalyzer_%1.png").arg(ts);

    const QString docsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString startDirFinal = m_lastAnalyzerExportDir.isEmpty() ? docsDir
                                                                    : m_lastAnalyzerExportDir;
    const QString defaultPath = QDir(startDirFinal).filePath(base);

    const QString filePath = QFileDialog::getSaveFileName(this,
                                                          tr("Export PNG"),
                                                          defaultPath,
                                                          tr("PNG (*.png)"));
    if (filePath.isEmpty())
        return;

    QPixmap pm = m_plot->grab();

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
        painter.setOpacity(0.12);

        const int padX = 15;
        const int liftUp = 55;
        const int padY = 10;

        const int x = qMax(0, pm.width() - mark.width() - padX);
        const int y = qMax(0, pm.height() - mark.height() - liftUp - padY);
        painter.drawPixmap(x, y, mark);
    }

    pm.save(filePath, "PNG");

    // Remember folder
    m_lastAnalyzerExportDir = QFileInfo(filePath).absolutePath();
}

bool AmplifierAnalyzerWidget::hamlibReadyOrWarn() const
{
    if (!m_settings.hamlib.enabled) {
        QMessageBox::warning(nullptr,
                             tr("Hamlib not configured"),
                             tr("This mode requires a successful Hamlib Test in Setup → Hamlib."));
        return false;
    }
    return true;
}

QString AmplifierAnalyzerWidget::resolveRigHost() const
{
    if (m_settings.hamlib.connectionModeIndex == 0) {
        // Serial mode: rigctld is local
        return QStringLiteral("127.0.0.1");
    }
    return m_settings.hamlib.network.host.trimmed().isEmpty()
               ? QStringLiteral("127.0.0.1")
               : m_settings.hamlib.network.host.trimmed();
}

int AmplifierAnalyzerWidget::resolveRigPort() const
{
    if (m_settings.hamlib.connectionModeIndex == 0) {
        return (m_settings.hamlib.serial.tcpPort > 0) ? m_settings.hamlib.serial.tcpPort : 4532;
    }
    return (m_settings.hamlib.network.port > 0) ? m_settings.hamlib.network.port : 4532;
}

bool AmplifierAnalyzerWidget::ensureRigConnectedOrWarn()
{
    if (!m_rig) {
        QMessageBox::warning(this, tr("Rig control"), tr("RigCtldClient is not available."));
        return false;
    }

    if (m_rig->isConnected())
        return true;

    const QString host = resolveRigHost();
    const int port = resolveRigPort();

    m_rig->connectToHost(host, port);

    // We don’t block here; the sweep engine will proceed once commands succeed.
    return true;
}

void AmplifierAnalyzerWidget::rebuildSweepPlan()
{
    m_steps.clear();

    const Mode mode = Mode(m_modeCombo->currentData().toInt());

    if (mode == Mode::GainFlatness) {
        const int n = m_points->value();
        const double startHz = m_startMHz->value() * 1e6;
        const double endHz = m_endMHz->value() * 1e6;

        const double step = (endHz - startHz) / double(n - 1);
        for (int i = 0; i < n; ++i) {
            SweepStep s;
            s.freqHz = startHz + step * double(i);
            s.drivePct = -1.0; // unchanged
            m_steps.push_back(s);
        }
    } else if (mode == Mode::Linearity || mode == Mode::Compression1dB) {
        const int n = m_driveSteps->value();
        const double hz = m_fixedMHz->value() * 1e6;

        // GUI is in percent already (0..100)
        const double pLo = m_driveLow->value();
        const double pHi = m_driveHigh->value();
        const double step = (pHi - pLo) / double(n - 1);

        for (int i = 0; i < n; ++i) {
            SweepStep s;
            s.freqHz = hz;
            s.drivePct = qBound(0.0, pLo + step * double(i), 100.0);
            m_steps.push_back(s);
        }
    }
}

void AmplifierAnalyzerWidget::beginSweep()
{
    const Mode mode = Mode(m_modeCombo->currentData().toInt());

    if (mode == Mode::Manual) {
        QMessageBox::information(this,
                                 tr("Manual mode"),
                                 tr("Use Capture Point to record points in Manual mode."));
        return;
    }

    if (!hamlibReadyOrWarn())
        return;

    // ---- HARD RESET so Start can be pressed repeatedly ----
    if (m_sweepTimer)
        m_sweepTimer->stop();

    m_pendingCmdTags.clear();
    m_stepIndex = -1;
    m_state = SweepState::Idle;

    // Stop any tone that might still be running
    stopAnalyzerTone();

    // CRITICAL: kill any queued/late rigctld commands from previous runs
    if (m_rig)
        m_rig->cancelAll(QStringLiteral("New sweep started"));

    if (!ensureRigConnectedOrWarn())
        return;

    rebuildSweepPlan();
    if (m_steps.isEmpty())
        return;

    onClearClicked();
    resetP1dB();
    updatePlotMeasurementsOverlay();

    if (mode == Mode::GainFlatness) {
        m_plot->setTitle(tr("Gain Flatness"));
        m_plot->setXLabel(tr("Frequency (MHz)"));
        m_plot->setYLabel(tr("Gain (dB)"));
        m_plot->setFixedXRange(m_startMHz->value(), m_endMHz->value());
    } else if (mode == Mode::Compression1dB) {
        m_plot->setTitle(tr("1 dB Compression"));
        m_plot->setXLabel(tr("Input (W)"));
        m_plot->setYLabel(tr("Output (W)"));
        m_plot->clearFixedXRange();
    } else {
        m_plot->setTitle(tr("Linearity"));
        m_plot->setXLabel(tr("Input (W)"));
        m_plot->setYLabel(tr("Output (W)"));
        m_plot->clearFixedXRange();
    }

    m_stepIndex = 0;

    // Audio Tone TX: first point tends to ramp up, so apply one-time extra settle
    m_initialToneWarmupPending = (txMethod() == TxMethod::AudioToneSsb);

    m_restoreModeAfterSweep = false;
    m_savedMode.clear();
    m_savedPassband = 0;

    if (txMethod() == TxMethod::CarrierTuneCw || txMethod() == TxMethod::AudioToneSsb) {
        m_state = SweepState::ReadOrigMode;
    } else {
        m_state = SweepState::PreTxOn;
    }

    m_btnStart->setEnabled(false);
    m_btnStop->setEnabled(true);
    m_btnCapture->setEnabled(false);

    m_sweepTimer->start();
}

void AmplifierAnalyzerWidget::abortSweep(const QString &why)
{
    if (m_state == SweepState::Idle)
        return;

    m_state = SweepState::Aborted;

    stopAnalyzerTone();
    m_pendingCmdTags.clear();

    if (m_rig)
        m_rig->cancelAll(QStringLiteral("Sweep aborted"));

    if (m_rig && m_rig->isConnected()) {
        requestTxOff();

        if ((txMethod() == TxMethod::CarrierTuneCw || txMethod() == TxMethod::AudioToneSsb)
            && m_restoreModeAfterSweep && !m_savedMode.isEmpty()) {
            const QString restoreCmd
                = QStringLiteral("M %1 %2").arg(m_savedMode).arg(QString::number(m_savedPassband));
            sendRig(restoreCmd, m_timeoutMs ? m_timeoutMs->value() : 2500);
        }
    }

    finishSweep(false, why);
}

void AmplifierAnalyzerWidget::finishSweep(bool ok, const QString &msg)
{
    if (m_sweepTimer)
        m_sweepTimer->stop();

    stopAnalyzerTone();

    m_pendingCmdTags.clear();
    if (m_rig)
        m_rig->cancelAll(QStringLiteral("Sweep finished"));

    m_state = SweepState::Idle;
    m_stepIndex = -1;

    m_btnStart->setEnabled(true);
    m_btnStop->setEnabled(false);
    m_btnCapture->setEnabled(true);

    if (!msg.isEmpty()) {
        QMessageBox::information(this, ok ? tr("Sweep complete") : tr("Sweep aborted"), msg);
    }
}

raypm::RigCtldClient::CommandId AmplifierAnalyzerWidget::sendRig(const QString &cmd, int timeoutMs)
{
    if (!m_rig)
        return 0;

    if (!m_rig->isConnected())
        return 0;

    const QString c = cmd.trimmed();
    if (c.isEmpty())
        return 0;

    const auto id = m_rig->sendCommand(c, timeoutMs);
    if (id != 0)
        m_pendingCmdTags.insert(id, c);

    return id;
}

QString AmplifierAnalyzerWidget::expandTemplate(const QString &tmpl, double drivePct) const
{
    auto fmt = [](double v) -> QString {
        QString s = QString::number(v, 'f', 3);
        while (s.contains('.') && s.endsWith('0'))
            s.chop(1);
        if (s.endsWith('.'))
            s.chop(1);
        return s;
    };

    auto quantize = [](double v, double step) -> double {
        if (step <= 0.0)
            return v;
        return qRound(v / step) * step;
    };

    const double pct = qBound(0.0, drivePct, 100.0); // 0..100
    double pn = pct / 100.0;                         // 0..1

    // Special handling for RFPOWER (Thetis/hamlib expects 0.05..1.00 in 0.01 steps).
    // Your terminal testing proves: Drive 5 -> 0.05, Drive 10 -> 0.10, etc.
    // So we DO NOT apply any offset mapping; just clamp/quantize to valid range.
    if (tmpl.contains(QStringLiteral("RFPOWER"), Qt::CaseInsensitive)) {
        if (pct <= 0.0) {
            pn = 0.0; // (if your backend rejects 0, you can change this to 0.05)
        } else {
            pn = qBound(0.05, pn, 1.0);
            pn = quantize(pn, 0.01);
        }
    }

    QString s = tmpl;
    // {p}  = 0..100 (percent)
    // {pn} = 0..1 (normalized)
    s.replace(QStringLiteral("{pn}"), fmt(pn));
    s.replace(QStringLiteral("{p}"), fmt(pct));
    return s;
}

AmplifierAnalyzerWidget::TxMethod AmplifierAnalyzerWidget::txMethod() const
{
    if (!m_txMethodCombo)
        return TxMethod::MoxPtt;

    return TxMethod(m_txMethodCombo->currentData().toInt());
}

void AmplifierAnalyzerWidget::requestTxOn()
{
    if (!m_rig || !m_rig->isConnected())
        return;

    // For both methods, TX ON is always the template command (default "T 1").
    // CarrierTuneCw handles mode changes in the sweep state machine.
    const QString cmd = m_cmdTxOn ? m_cmdTxOn->text().trimmed() : QString();
    if (!cmd.isEmpty())
        sendRig(cmd, m_timeoutMs ? m_timeoutMs->value() : 2500);
}

void AmplifierAnalyzerWidget::requestTxOff()
{
    if (!m_rig || !m_rig->isConnected())
        return;

    const QString cmd = m_cmdTxOff ? m_cmdTxOff->text().trimmed() : QString();
    if (!cmd.isEmpty())
        sendRig(cmd, m_timeoutMs ? m_timeoutMs->value() : 2500);
}

void AmplifierAnalyzerWidget::ensureToneGenerator()
{
    if (m_toneGen)
        return;

    m_toneGen = new ToneGenerator(this);

    // Keep device list in sync with OS changes
    connect(m_toneGen, &ToneGenerator::devicesChanged, this, [this]() {
        refreshToneDevices();
        applyToneSettingsToGenerator();
    });

    refreshToneDevices();
}

void AmplifierAnalyzerWidget::refreshToneDevices()
{
    if (!m_toneDeviceCombo)
        return;

    ensureToneGenerator();
    if (!m_toneGen)
        return;

    const QString prev = m_toneDeviceCombo->currentData().toString();

    m_toneDeviceCombo->blockSignals(true);
    m_toneDeviceCombo->clear();

    const auto devs = m_toneGen->outputDevices();
    for (const auto &d : devs) {
        // Store description as the “id” (simple + portable)
        m_toneDeviceCombo->addItem(d.description(), d.description());
    }

    // Restore previous selection if possible
    int sel = -1;
    if (!prev.isEmpty())
        sel = m_toneDeviceCombo->findData(prev);

    if (sel < 0) {
        // Otherwise default output device
        const QString defDesc = m_toneGen->defaultOutputDevice().description();
        sel = m_toneDeviceCombo->findData(defDesc);
    }

    if (sel < 0 && m_toneDeviceCombo->count() > 0)
        sel = 0;

    if (sel >= 0)
        m_toneDeviceCombo->setCurrentIndex(sel);

    m_toneDeviceCombo->blockSignals(false);
}

void AmplifierAnalyzerWidget::applyToneSettingsToGenerator()
{
    if (!m_toneDeviceCombo || !m_toneCountCombo || !m_toneFreqHz || !m_twoToneSpacingHz
        || !m_toneLevelSpin)
        return;

    if (txMethod() != TxMethod::AudioToneSsb)
        return;

    ensureToneGenerator();
    if (!m_toneGen)
        return;

    // Device
    const QString desc = m_toneDeviceCombo->currentData().toString();
    if (!desc.isEmpty())
        m_toneGen->setOutputDeviceByDescription(desc);

    // Frequencies
    const int toneCount = m_toneCountCombo->currentData().toInt();
    const double f1 = m_toneFreqHz->value();
    const double spacing = m_twoToneSpacingHz->value();

    QVector<double> freqs;
    if (toneCount <= 1) {
        freqs = {f1};
    } else {
        freqs = {f1, f1 + spacing};
    }

    m_toneGen->setFrequenciesHz(freqs);

    // Amplitude
    m_toneGen->setAmplitude(m_toneLevelSpin->value());

    m_toneGen->setSampleRate(48000);
}

void AmplifierAnalyzerWidget::startAnalyzerTone()
{
    ensureToneGenerator();
    if (!m_toneGen)
        return;

    applyToneSettingsToGenerator();

    if (!m_toneGen->isRunning())
        m_toneGen->start();
}

void AmplifierAnalyzerWidget::stopAnalyzerTone()
{
    if (!m_toneGen)
        return;

    if (m_toneGen->isRunning())
        m_toneGen->stop();
}

void AmplifierAnalyzerWidget::onRigCommandFinished(raypm::RigCtldClient::CommandId id,
                                                   bool ok,
                                                   const QStringList &lines,
                                                   int /*rprt*/)
{
    if (!m_pendingCmdTags.contains(id))
        return;

    const QString cmd = m_pendingCmdTags.value(id);
    m_pendingCmdTags.remove(id);

    if (!ok) {
        abortSweep(tr("rigctld command failed: %1").arg(cmd));
        return;
    }

    // Parse original mode response: rigctld "m" typically returns: "USB 2400"
    if (cmd == QStringLiteral("m")) {
        if (lines.isEmpty()) {
            abortSweep(tr("rigctld returned empty mode response"));
            return;
        }

        const QString line = lines.first().trimmed();
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            abortSweep(tr("rigctld returned invalid mode response: %1").arg(line));
            return;
        }

        m_savedMode = parts.value(0).trimmed();
        bool okNum = false;
        m_savedPassband = parts.value(1).toInt(&okNum);
        if (!okNum)
            m_savedPassband = 0;

        // We successfully captured original mode → we will restore at end
        m_restoreModeAfterSweep = !m_savedMode.isEmpty();
    }
}

void AmplifierAnalyzerWidget::onSweepTick()
{
    if (m_state == SweepState::Idle)
        return;

    if (m_stepIndex < 0 || m_stepIndex >= m_steps.size()) {
        m_state = SweepState::TxOff;
    }

    const Mode mode = Mode(m_modeCombo->currentData().toInt());

    // Safety checks
    if (m_hasTelemetry) {
        const int inIdx = m_inCouplerCombo ? m_inCouplerCombo->currentData().toInt() : 0;
        const int outIdx = m_outCouplerCombo ? m_outCouplerCombo->currentData().toInt() : 1;

        const double pinRigW = readCouplerForwardW(m_lastTelemetry, inIdx);
        const double poutW = readCouplerForwardW(m_lastTelemetry, outIdx);
        const double pinDutW = attnCorrectPin(pinRigW, m_attnDb ? m_attnDb->value() : 0.0);

        const int vIdx = m_auxVCombo ? m_auxVCombo->currentData().toInt() : 0;
        const int iIdx = m_auxICombo ? m_auxICombo->currentData().toInt() : 1;
        const int tIdx = m_auxTCombo ? m_auxTCombo->currentData().toInt() : 2;

        const double volts = readAuxValue(m_lastTelemetry, vIdx);
        const double amps = readAuxValue(m_lastTelemetry, iIdx);
        const double tempC = readAuxValue(m_lastTelemetry, tIdx);

        QString reason;
        if (!checkSafetyStops(m_lastTelemetry, pinDutW, poutW, amps, tempC, &reason)) {
            abortSweep(reason);
            return;
        }
    }

    switch (m_state) {
    case SweepState::ReadOrigMode: {
        // Query current mode so we can restore later
        sendRig(QStringLiteral("m"), m_timeoutMs ? m_timeoutMs->value() : 2500);

        // Choose next state based on TX method
        if (txMethod() == TxMethod::CarrierTuneCw) {
            m_state = SweepState::SetCwMode;
        } else if (txMethod() == TxMethod::AudioToneSsb) {
            m_state = SweepState::SetAudioMode;
        } else {
            m_state = SweepState::PreTxOn;
        }
        return;
    }

    case SweepState::SetCwMode: {
        sendRig(QStringLiteral("M CW 0"), m_timeoutMs ? m_timeoutMs->value() : 2500);
        m_state = SweepState::PreTxOn;
        return;
    }

    case SweepState::SetAudioMode: {
        sendRig(QStringLiteral("M USB 2400"), m_timeoutMs ? m_timeoutMs->value() : 2500);
        m_state = SweepState::StartAudioTone;
        return;
    }

    case SweepState::StartAudioTone: {
        startAnalyzerTone();
        m_state = SweepState::PreTxOn;
        return;
    }

    case SweepState::PreTxOn: {
        // Do NOT key TX yet.
        // We want to set frequency + RFPOWER first, then key TX.
        m_state = SweepState::SetFreq;
        return;
    }

    case SweepState::SetFreq: {
        const double hz = m_steps[m_stepIndex].freqHz;
        const QString cmd = QStringLiteral("F %1").arg(QString::number((quint64) llround(hz)));
        sendRig(cmd, m_timeoutMs ? m_timeoutMs->value() : 2500);

        // After changing frequency, require at least one NEW telemetry frame before capturing
        m_captureStartSeq = m_telemetrySeq;
        m_captureTimer.invalidate();

        m_state = SweepState::SetPower;
        return;
    }

    case SweepState::SetPower: {
        const double drivePct = m_steps[m_stepIndex].drivePct;

        if ((mode == Mode::Linearity || mode == Mode::Compression1dB) && drivePct >= 0.0) {
            const QString tmpl = m_cmdSetPowerTemplate ? m_cmdSetPowerTemplate->text().trimmed()
                                                       : QString();
            if (!tmpl.isEmpty()) {
                const QString cmd = expandTemplate(tmpl, drivePct);
                sendRig(cmd, m_timeoutMs ? m_timeoutMs->value() : 2500);

                // After changing power, require at least one NEW telemetry frame before capturing
                m_captureStartSeq = m_telemetrySeq;
                m_captureTimer.invalidate(); // restart the RF wait timer for this point
            }
        }

        // Key TX only once, AFTER freq + power have been applied
        if (m_stepIndex == 0) {
            requestTxOn();
        }

        m_settleTimer.restart();
        m_state = SweepState::Settling;
        return;
    }

    case SweepState::Settling: {
        const int baseSettleMs = m_settleMs->value();
        int settleMs = baseSettleMs;

        // Audio Tone TX: add one-time warm-up delay for the very first point
        if (m_initialToneWarmupPending && m_stepIndex == 0)
            settleMs = qMax(settleMs, m_initialToneWarmupMs);

        if (!m_settleTimer.isValid() || m_settleTimer.elapsed() < settleMs)
            return;

        // Warm-up consumed
        if (m_initialToneWarmupPending && m_stepIndex == 0)
            m_initialToneWarmupPending = false;

        m_state = SweepState::Capture;
        return;
    }

    case SweepState::Capture: {
        if (!m_hasTelemetry)
            return;

        // Wait until we’ve received at least one new telemetry update since SetPower or SetFreq.
        // Prevents re-using the previous point’s readings.
        const bool requireFresh = (mode == Mode::GainFlatness || mode == Mode::Linearity);
        if (requireFresh && m_telemetrySeq <= m_captureStartSeq) {
            // Also honor the timeout so we don’t hang forever
            const int maxWaitMs = (m_timeoutMs ? m_timeoutMs->value() : 2500);
            if (!m_captureTimer.isValid())
                m_captureTimer.start();
            if (m_captureTimer.elapsed() < maxWaitMs)
                return;

            stopAnalyzerTone();
            requestTxOff();
            finishSweep(false, tr("Timed out waiting for fresh telemetry after drive change."));
            return;
        }

        const int inIdx = m_inCouplerCombo ? m_inCouplerCombo->currentData().toInt() : 0;
        const int outIdx = m_outCouplerCombo ? m_outCouplerCombo->currentData().toInt() : 1;

        const double pinRigW = readCouplerForwardW(m_lastTelemetry, inIdx);
        const double poutW = readCouplerForwardW(m_lastTelemetry, outIdx);

        // --- NEW: wait for RF to appear before recording the point ---
        // (prevents the first sample being 0 W / 0 dB)
        const double kMinRfW = 0.05; // small threshold so we don't hang at very low power
        const int maxWaitMs = (m_timeoutMs ? m_timeoutMs->value() : 2500);

        // Start a wait timer the first time we hit Capture after Settling
        if (!m_captureTimer.isValid())
            m_captureTimer.start();

        if ((pinRigW < kMinRfW || poutW < kMinRfW)) {
            if (m_captureTimer.elapsed() < maxWaitMs) {
                // keep waiting for RF to ramp up
                return;
            }

            // Give up: no RF detected
            stopAnalyzerTone();
            requestTxOff();
            finishSweep(false, tr("No RF detected (input/output power stayed near 0 W)."));
            return;
        }

        // We have RF, record the point
        const double attnDb = m_attnDb ? m_attnDb->value() : 0.0;
        const double pinDutW = attnCorrectPin(pinRigW, attnDb);

        const int vIdx = m_auxVCombo ? m_auxVCombo->currentData().toInt() : 0;
        const int iIdx = m_auxICombo ? m_auxICombo->currentData().toInt() : 1;
        const int tIdx = m_auxTCombo ? m_auxTCombo->currentData().toInt() : 2;

        const double volts = readAuxValue(m_lastTelemetry, vIdx);
        const double amps = readAuxValue(m_lastTelemetry, iIdx);
        const double tempC = readAuxValue(m_lastTelemetry, tIdx);

        AmplifierAnalyzerTableModel::Point pt;
        pt.freqMHz = m_steps[m_stepIndex].freqHz / 1e6;
        pt.pinRigW = pinRigW;
        pt.attnDb = attnDb;
        pt.pinDutW = pinDutW;
        pt.poutW = poutW;
        pt.gainDb = (pinDutW > 0.0 && poutW > 0.0) ? (10.0 * std::log10(poutW / pinDutW)) : 0.0;
        pt.volts = volts;
        pt.amps = amps;
        pt.tempC = tempC;
        pt.effPct = efficiencyPct(poutW, volts, amps);

        m_model->addPoint(pt);

        // Update measurements overlay each point
        updatePlotMeasurementsOverlay();

        // 1 dB Compression: check for P1dB and stop when found
        if (mode == Mode::Compression1dB) {
            processP1dBAfterPoint();
            updatePlotMeasurementsOverlay();

            if (m_p1db.found) {
                // Stop immediately after reaching P1dB
                m_captureTimer.invalidate();
                m_state = SweepState::TxOff;
                return;
            }
        }

        if (mode == Mode::GainFlatness) {
            m_plot->addPoint(pt.freqMHz, pt.gainDb);
        } else {
            m_plot->addPoint(pt.pinDutW, pt.poutW);
        }

        // Reset for the next step
        m_captureTimer.invalidate();

        m_state = SweepState::NextStep;
        return;
    }

    case SweepState::NextStep:
        m_stepIndex++;
        m_state = (m_stepIndex >= m_steps.size()) ? SweepState::TxOff : SweepState::SetFreq;
        return;

    case SweepState::TxOff: {
        // Turn off TX (best effort)
        requestTxOff();

        // Always stop tone if we used it
        stopAnalyzerTone();

        // ---- KEY FIX: restore NOW, not on a later tick ----
        if ((txMethod() == TxMethod::CarrierTuneCw || txMethod() == TxMethod::AudioToneSsb)
            && m_restoreModeAfterSweep && !m_savedMode.isEmpty()) {
            const QString restoreCmd
                = QStringLiteral("M %1 %2").arg(m_savedMode).arg(QString::number(m_savedPassband));
            sendRig(restoreCmd, m_timeoutMs ? m_timeoutMs->value() : 2500);
        }

        finishSweep(true, tr("Sweep complete"));
        return;
    }

    case SweepState::RestoreOrigMode:
    case SweepState::Done:
    case SweepState::Aborted:
    case SweepState::Idle:
        return;
    }
}

void AmplifierAnalyzerWidget::resetP1dB()
{
    m_p1db = P1dBState{};
    m_p1db.refN = 3;
    m_p1db.statusText.clear();

    if (m_plot)
        m_plot->clearExtraMeasurements();
}

void AmplifierAnalyzerWidget::updatePlotMeasurementsOverlay() const
{
    const Mode mode = Mode(m_modeCombo->currentData().toInt());
    if (!m_plot)
        return;

    if (mode != Mode::Compression1dB) {
        // Clear extra overlay when not in this mode
        m_plot->clearExtraMeasurements();
        return;
    }

    QStringList lines;
    lines << tr("1 dB Compression");

    if (m_state == SweepState::Idle) {
        lines << tr("Status: Idle");
        m_plot->setExtraMeasurements(lines);
        return;
    }

    if (!m_p1db.haveRef) {
        lines << tr("Status: Running");
        lines << tr("GainRef: (waiting)");
        m_plot->setExtraMeasurements(lines);
        return;
    }

    lines << (m_p1db.found ? tr("Status: Found") : tr("Status: Running"));
    lines << tr("GainRef: %1 dB").arg(m_p1db.gainRefDb, 0, 'f', 2);
    lines << tr("Threshold: %1 dB").arg(m_p1db.thresholdDb, 0, 'f', 2);

    if (m_p1db.found) {
        lines << tr("P1dB Drive: %1 %").arg(m_p1db.drivePct, 0, 'f', 2);
        lines << tr("Pin(DUT): %1 W").arg(m_p1db.pinDutW, 0, 'f', 3);
        lines << tr("Pout: %1 W").arg(m_p1db.poutW, 0, 'f', 1);
        lines << tr("Gain@P1dB: %1 dB").arg(m_p1db.gainDb, 0, 'f', 2);
    }

    m_plot->setExtraMeasurements(lines);
}

void AmplifierAnalyzerWidget::processP1dBAfterPoint()
{
    // Use only valid points for reference / crossing detection
    const auto &pts = m_model->points();
    if (pts.isEmpty())
        return;

    auto isValid = [](const AmplifierAnalyzerTableModel::Point &p) {
        return (p.pinDutW > 0.0 && p.poutW > 0.0);
    };

    // Establish GainRef from first N valid points
    if (!m_p1db.haveRef) {
        double sum = 0.0;
        int count = 0;
        for (const auto &p : pts) {
            if (!isValid(p))
                continue;
            sum += p.gainDb;
            if (++count >= m_p1db.refN)
                break;
        }
        if (count >= m_p1db.refN) {
            m_p1db.haveRef = true;
            m_p1db.gainRefDb = sum / double(count);
            m_p1db.thresholdDb = m_p1db.gainRefDb - 1.0;
        }
        return;
    }

    if (m_p1db.found)
        return;

    // Check latest valid point for crossing
    const int i = pts.size() - 1;
    const auto &p1 = pts[i];
    if (!isValid(p1))
        return;

    if (p1.gainDb > m_p1db.thresholdDb)
        return; // not yet compressed

    // Find previous valid point to interpolate against
    int j = i - 1;
    while (j >= 0 && !isValid(pts[j]))
        --j;

    if (j < 0) {
        // No previous point; fall back to current (non-interpolated)
        const double drivePct = (m_stepIndex >= 0 && m_stepIndex < m_steps.size())
                                    ? (m_steps[m_stepIndex].drivePct)
                                    : 0.0;
        m_p1db.found = true;
        m_p1db.drivePct = drivePct;
        m_p1db.pinDutW = p1.pinDutW;
        m_p1db.poutW = p1.poutW;
        m_p1db.gainDb = p1.gainDb;
        return;
    }

    const auto &p0 = pts[j];
    const double g0 = p0.gainDb;
    const double g1 = p1.gainDb;
    const double gt = m_p1db.thresholdDb;

    double t = 0.0;
    const double denom = (g1 - g0);
    if (std::fabs(denom) > 1e-9) {
        t = (gt - g0) / denom;
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
    }

    // Drives come from sweep plan step indices (j corresponds to earlier capture)
    const double drive0Pct = (j >= 0 && j < m_steps.size()) ? (m_steps[j].drivePct) : 0.0;
    const double drive1Pct = (i >= 0 && i < m_steps.size()) ? (m_steps[i].drivePct) : drive0Pct;

    auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };

    m_p1db.found = true;
    m_p1db.drivePct = lerp(drive0Pct, drive1Pct, t);
    m_p1db.pinDutW = lerp(p0.pinDutW, p1.pinDutW, t);
    m_p1db.poutW = lerp(p0.poutW, p1.poutW, t);
    m_p1db.gainDb = gt; // by definition at the threshold
}

double AmplifierAnalyzerWidget::readCouplerForwardW(const raypm::RayTelemetry &t,
                                                    int couplerIndex) const
{
    for (const auto &c : t.couplers) {
        if (c.index == couplerIndex)
            return c.forwardWatts;
    }

    // fallback legacy single-coupler field if it matches requested index
    if (t.couplerIndex == couplerIndex)
        return t.forwardWatts;

    return 0.0;
}

double AmplifierAnalyzerWidget::readAuxValue(const raypm::RayTelemetry &t, int auxIndex) const
{
    for (const auto &a : t.auxChannels) {
        if (a.index == auxIndex)
            return a.value;
    }

    // fallback legacy flat fields (not index-safe)
    if (auxIndex == 0)
        return t.voltage;
    if (auxIndex == 1)
        return t.current;
    if (auxIndex == 2)
        return t.temperatureC;

    return 0.0;
}

double AmplifierAnalyzerWidget::readTelemetryFreqMHz(const raypm::RayTelemetry &t) const
{
    if (t.rfFrequencyValid && t.rfFrequencyHz > 0.0)
        return t.rfFrequencyHz / 1e6;
    // otherwise unknown; return 0
    return 0.0;
}

double AmplifierAnalyzerWidget::attnCorrectPin(double pinRigW, double attnDb)
{
    // Pin_dut = Pin_rig * 10^(-Attn/10)
    const double factor = std::pow(10.0, (-attnDb / 10.0));
    return pinRigW * factor;
}

double AmplifierAnalyzerWidget::efficiencyPct(double poutW, double volts, double amps)
{
    const double pdc = volts * amps;
    if (pdc <= 0.0)
        return 0.0;
    return 100.0 * (poutW / pdc);
}

bool AmplifierAnalyzerWidget::checkSafetyStops(const raypm::RayTelemetry &t,
                                               double pinDutW,
                                               double poutW,
                                               double amps,
                                               double tempC,
                                               QString *reasonOut) const
{
    if (t.trip.tripped) {
        if (reasonOut)
            *reasonOut = tr("Device protection is TRIPPED. Aborting.");
        return false;
    }

    if (m_maxPinW && m_maxPinW->value() > 0.0 && pinDutW > m_maxPinW->value()) {
        if (reasonOut)
            *reasonOut = tr("Safety stop: Pin(DUT) exceeded (%1 W > %2 W)")
                             .arg(pinDutW, 0, 'f', 1)
                             .arg(m_maxPinW->value(), 0, 'f', 1);
        return false;
    }
    if (m_maxPoutW && m_maxPoutW->value() > 0.0 && poutW > m_maxPoutW->value()) {
        if (reasonOut)
            *reasonOut = tr("Safety stop: Pout exceeded (%1 W > %2 W)")
                             .arg(poutW, 0, 'f', 1)
                             .arg(m_maxPoutW->value(), 0, 'f', 1);
        return false;
    }
    if (m_maxAmps && m_maxAmps->value() > 0.0 && amps > m_maxAmps->value()) {
        if (reasonOut)
            *reasonOut = tr("Safety stop: Current exceeded (%1 A > %2 A)")
                             .arg(amps, 0, 'f', 1)
                             .arg(m_maxAmps->value(), 0, 'f', 1);
        return false;
    }
    if (m_maxTempC && m_maxTempC->value() > -30.0 && tempC > m_maxTempC->value()) {
        if (reasonOut)
            *reasonOut = tr("Safety stop: Temperature exceeded (%1 C > %2 C)")
                             .arg(tempC, 0, 'f', 1)
                             .arg(m_maxTempC->value(), 0, 'f', 1);
        return false;
    }

    return true;
}
