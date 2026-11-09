// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "app/dialoghardwareconfig.h"
#include "common/Constants.h"
#include "core/RayCouplerConfig.h"
#include "ui_DialogHardwareConfig.h"

#include <QHeaderView>
#include <cmath>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <algorithm>

// Your real struct definition
#include "core/RayCouplerConfig.h"

using namespace raypm;

DialogHardwareConfig::DialogHardwareConfig(QWidget *parent)
    : QDialog(parent), ui(new Ui::DialogHardwareConfig), m_cfgCopy{}
{
    ui->setupUi(this);
    setMinimumSize(QSize(0, 0));

    configureCalibrationTable();

    // --- Coupler Type combo (TypeId drives firmware enable/disable; None = disabled) ---
    ui->comboCouplerType->clear();

    // Display text, type_id, firmware enum name (stored for round-trip)
    ui->comboCouplerType->addItem(tr("None (Disabled)"), 0);
    ui->comboCouplerType->setItemData(ui->comboCouplerType->count() - 1,
                                      QStringLiteral("NONE"),
                                      Qt::UserRole + 1);

    ui->comboCouplerType->addItem(tr("RayCoupler 200W"), 1);
    ui->comboCouplerType->setItemData(ui->comboCouplerType->count() - 1,
                                      QStringLiteral("COUPLER_200W"),
                                      Qt::UserRole + 1);

    ui->comboCouplerType->addItem(tr("RayCoupler 2kW"), 2);
    ui->comboCouplerType->setItemData(ui->comboCouplerType->count() - 1,
                                      QStringLiteral("COUPLER_2KW"),
                                      Qt::UserRole + 1);

    // --- Alarms tab connections ---
    // Keep mask in sync when checkboxes change
    connect(ui->checkTripFwd,
            &QCheckBox::toggled,
            this,
            &DialogHardwareConfig::onTripCheckboxChanged);
    connect(ui->checkTripRef,
            &QCheckBox::toggled,
            this,
            &DialogHardwareConfig::onTripCheckboxChanged);
    connect(ui->checkTripSWR,
            &QCheckBox::toggled,
            this,
            &DialogHardwareConfig::onTripCheckboxChanged);

    // --- Device tab connections ---
    // Keep slider/spin in sync
    connect(ui->sliderBrightness, &QSlider::valueChanged, ui->spinBrightness, &QSpinBox::setValue);

    connect(ui->spinBrightness,
            QOverload<int>::of(&QSpinBox::valueChanged),
            ui->sliderBrightness,
            &QSlider::setValue);

    connect(ui->pushButtonSetTimeFromPc, &QPushButton::clicked, this, [this]()
            { ui->dateTimeUtc->setDateTime(QDateTime::currentDateTimeUtc()); });

    // --- Calibration tab button handlers (Couplers) ---
    connect(ui->pushButtonCalCaptureLow, &QPushButton::clicked, this, [this]()
            {
        if (!m_calLive.valid)
            return;

        m_calLow = m_calLive;
        m_calLow.trueW = ui->spinCalLowTrueW->value();
        m_calLow.valid = true;

        ui->labelCapLowRawV->setText(QString::number(m_calLow.rawV, 'f', 3));
        ui->labelCapLowPwrW->setText(QString::number(m_calLow.liveW, 'f', 1)); });

    connect(ui->pushButtonCalCaptureHigh, &QPushButton::clicked, this, [this]()
            {
        if (!m_calLive.valid)
            return;

        m_calHigh = m_calLive;
        m_calHigh.trueW = ui->spinCalHighTrueW->value();
        m_calHigh.valid = true;

        ui->labelCapHighRawV->setText(QString::number(m_calHigh.rawV, 'f', 3));
        ui->labelCapHighPwrW->setText(QString::number(m_calHigh.liveW, 'f', 1)); });

    connect(ui->pushButtonCalReset, &QPushButton::clicked, this, [this]()
            {
        m_calLow = {};
        m_calHigh = {};

        ui->labelCapLowRawV->setText(QStringLiteral("---"));
        ui->labelCapLowPwrW->setText(QStringLiteral("---"));
        ui->labelCapHighRawV->setText(QStringLiteral("---"));
        ui->labelCapHighPwrW->setText(QStringLiteral("---")); });

    connect(ui->comboCalPath,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int idx)
            {
                m_calPath = (idx == 1) ? CalPath::Reflected : CalPath::Forward;
                updateCalibrationFirmwareStatus();
            });

    // --- Multi-point calibration UI ---
    if (ui->groupBoxCalMulti)
        ui->groupBoxCalMulti->setVisible(false);

    connect(ui->comboCalMode,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DialogHardwareConfig::on_comboCalMode_currentIndexChanged);

    connect(ui->pushButtonCalCapturePoint,
            &QPushButton::clicked,
            this,
            &DialogHardwareConfig::on_pushButtonCalCapturePoint_clicked);

    connect(ui->pushButtonCalRemovePoint,
            &QPushButton::clicked,
            this,
            &DialogHardwareConfig::on_pushButtonCalRemovePoint_clicked);

    connect(ui->pushButtonCalClearPoints,
            &QPushButton::clicked,
            this,
            &DialogHardwareConfig::on_pushButtonCalClearPoints_clicked);

    updateCalibrationUiMode();
    updateCalibrationFirmwareStatus();

    ui->dateTimeUtc->setDateTime(QDateTime::currentDateTimeUtc());

    setWindowTitle(tr("Hardware Configuration"));

    // For now we just populate a single coupler entry; later you can
    // extend this to multiple couplers if/when firmware supports it.
    ui->comboCoupler->clear();
    for (int i = 0; i < MAX_COUPLERS; ++i)
        ui->comboCoupler->addItem(QString("Coupler %1").arg(i + 1), i);

    // Keep one config slot per coupler index
    m_couplerConfigs.resize(MAX_COUPLERS);

    // Reasonable defaults for spin boxes
    ui->doubleSpinFullScale->setRange(0.0, 10000.0);
    ui->doubleSpinFullScale->setDecimals(1);

    ui->doubleSpinFwdRange->setRange(0.0, 10000.0);
    ui->doubleSpinFwdRange->setDecimals(1);

    ui->doubleSpinRefRange->setRange(0.0, 10000.0);
    ui->doubleSpinRefRange->setDecimals(1);

    ui->doubleSpinHighFwd->setRange(0.0, 10000.0);
    ui->doubleSpinHighFwd->setDecimals(1);

    ui->doubleSpinHighRef->setRange(0.0, 10000.0);
    ui->doubleSpinHighRef->setDecimals(1);

    ui->doubleSpinHighSWR->setRange(1.0, 10.0);
    ui->doubleSpinHighSWR->setDecimals(2);

    ui->spinTripMask->setRange(0, 0xFFFF);

    // Device tab: brightness (min 10%)
    ui->sliderBrightness->setRange(10, 100);
    ui->spinBrightness->setRange(10, 100);
    ui->sliderBrightness->setValue(50);
    ui->spinBrightness->setValue(50);

    // --- Calibration tab defaults ---
    ui->comboCalPath->clear();
    ui->comboCalPath->addItem(tr("Forward"), 0);
    ui->comboCalPath->addItem(tr("Reflected"), 1);

    ui->spinCalLowTrueW->setRange(0.0, 10000.0);
    ui->spinCalLowTrueW->setDecimals(1);
    ui->spinCalLowTrueW->setValue(100.0);

    ui->spinCalHighTrueW->setRange(0.0, 10000.0);
    ui->spinCalHighTrueW->setDecimals(1);
    ui->spinCalHighTrueW->setValue(1000.0);

    // Live display placeholders
    ui->labelLiveRawValue->setText(QStringLiteral("---"));
    ui->labelLivePwrValue->setText(QStringLiteral("---"));

    // Captured display placeholders
    ui->labelCapLowRawV->setText(QStringLiteral("---"));
    ui->labelCapLowPwrW->setText(QStringLiteral("---"));
    ui->labelCapHighRawV->setText(QStringLiteral("---"));
    ui->labelCapHighPwrW->setText(QStringLiteral("---"));

    // --- Aux calibration tab placeholders ---
    ui->labelAuxCalLiveRawValue->setText(QStringLiteral("---"));

    // Defaults for true values (user-entered)
    ui->doubleAuxCalTrueLow->setRange(-1e6, 1e6);
    ui->doubleAuxCalTrueHigh->setRange(-1e6, 1e6);
    ui->doubleAuxCalTrueLow->setDecimals(3);
    ui->doubleAuxCalTrueHigh->setDecimals(3);
    ui->doubleAuxCalTrueLow->setValue(0.0);
    ui->doubleAuxCalTrueHigh->setValue(0.0);

    // Defaults for true values (user-entered)
    ui->doubleAuxCalTrueLow->setRange(-1e6, 1e6);
    ui->doubleAuxCalTrueHigh->setRange(-1e6, 1e6);
    ui->doubleAuxCalTrueLow->setDecimals(3);
    ui->doubleAuxCalTrueHigh->setDecimals(3);
    ui->doubleAuxCalTrueLow->setValue(0.0);
    ui->doubleAuxCalTrueHigh->setValue(0.0);

    // --- Aux tab: populate type / model combos ---
    ui->comboAuxType->clear();
    ui->comboAuxType->addItem(tr("None"), QStringLiteral("none"));
    ui->comboAuxType->addItem(tr("Voltage"), QStringLiteral("voltage"));
    ui->comboAuxType->addItem(tr("Current"), QStringLiteral("current"));
    ui->comboAuxType->addItem(tr("Temperature"), QStringLiteral("temperature"));

    ui->comboAuxTempModel->clear();
    ui->comboAuxTempModel->addItem(tr("Linear"), QStringLiteral("linear"));
    ui->comboAuxTempModel->addItem(tr("Thermistor (Beta)"), QStringLiteral("thermistor"));

    // Sensible defaults for thermistor fields
    ui->doubleAuxVoltageMaxRange->setValue(AUX_VOLTAGE_MAX_RANGE_DEFAULT);
    ui->doubleAuxCurrentMaxRange->setValue(AUX_CURRENT_MAX_RANGE_DEFAULT);
    ui->doubleAuxTemperatureMaxRange->setValue(AUX_TEMPERATURE_MAX_RANGE_DEFAULT);
    ui->doubleAuxThermSeriesOhms->setValue(10000.0);
    ui->doubleAuxThermR0Ohms->setValue(10000.0);
    ui->doubleAuxThermBeta->setValue(3950.0);
    ui->doubleAuxThermT0C->setValue(25.0);

    updateAuxUiEnabledState();

    // --- Global: populate Aux input selector with known channels ---
    ui->comboAuxIndex->clear();
    for (int i = 0; i < MAX_AUX_CHANNELS; ++i)
    {
        ui->comboAuxIndex->addItem(tr("Aux %1").arg(i + 1), i);
    }
    if (ui->comboAuxIndex->count() > 0)
    {
        ui->comboAuxIndex->setCurrentIndex(0);
        m_currentAuxIndex = ui->comboAuxIndex->currentData().toInt();
    }

    // Units labels just start as generic "units". These can be updated when
    // Aux type changes (V, A, °C, etc.).
    ui->labelAuxCalUnitsLow->setText(tr("units"));
    ui->labelAuxCalUnitsHigh->setText(tr("units"));

    // Clear internal samples
    m_auxCalLive = AuxCalSample{};
    m_auxCalLow = AuxCalSample{};
    m_auxCalHigh = AuxCalSample{};

    // Always show 1st tab first
    ui->tabWidget->setCurrentIndex(0);
}

DialogHardwareConfig::~DialogHardwareConfig()
{
    delete ui;
}

void DialogHardwareConfig::setCouplerConfig(const RayCouplerConfig &cfg)
{
    if (!ui)
        return;

    const int idx = cfg.index;
    ensureCouplerSizeForIndex(idx);

    // Cache config for this coupler index
    m_couplerConfigs[idx] = cfg;
    m_hasConfig = true;

    // Update / create combobox entry
    const QString displayName = cfg.name.isEmpty() ? tr("Coupler %1").arg(idx + 1) : cfg.name;

    int comboIdx = ui->comboCoupler->findData(idx);
    if (comboIdx < 0)
    {
        ui->comboCoupler->addItem(displayName, idx);
        comboIdx = ui->comboCoupler->findData(idx);
    }
    else
    {
        ui->comboCoupler->setItemText(comboIdx, displayName);
    }

    // If this config is for the currently selected coupler
    // (or nothing selected yet), refresh the UI from it.
    const int currentIdx = ui->comboCoupler->currentData().toInt();
    if (currentIdx == idx || currentIdx < 0)
    {
        m_currentCouplerIndex = idx;
        m_cfgCopy = cfg;
        loadFromConfigToUi();
    }
}

void DialogHardwareConfig::selectInitialCoupler()
{
    if (!ui || m_couplerConfigs.isEmpty())
        return;

    // Prefer first "enabled" coupler (by convention: fullScaleW > 0)
    int candidate = -1;

    for (int i = 0; i < m_couplerConfigs.size(); ++i)
    {
        const RayCouplerConfig &cfg = m_couplerConfigs.at(i);

        if (cfg.fullScaleW > 0.0)
        {
            candidate = i;
            break;
        }

        // Fallback: remember first non-empty slot even if disabled
        if (candidate < 0 && (!cfg.name.isEmpty() || cfg.typeId != 0))
            candidate = i;
    }

    if (candidate < 0)
        return;

    int comboIdx = ui->comboCoupler->findData(candidate);
    if (comboIdx < 0)
        comboIdx = candidate; // best effort

    ui->comboCoupler->setCurrentIndex(comboIdx);

    m_currentCouplerIndex = candidate;
    m_cfgCopy = m_couplerConfigs.value(candidate);
    m_hasConfig = true;

    loadFromConfigToUi();
}

void DialogHardwareConfig::setAuxConfig(const raypm::RayAuxConfig &cfg)
{
    ensureAuxSizeForIndex(cfg.index);
    m_auxConfigs[cfg.index] = cfg;

    // Keep the index in the Aux combo's userData as well
    int comboIdx = ui->comboAuxIndex->findData(cfg.index);
    if (comboIdx < 0)
    {
        ui->comboAuxIndex->addItem(tr("Aux %1").arg(cfg.index + 1), cfg.index);
        comboIdx = ui->comboAuxIndex->findData(cfg.index);
    }

    if (ui->comboAuxIndex->currentIndex() < 0)
        ui->comboAuxIndex->setCurrentIndex(comboIdx);

    const int currentAuxIndex = ui->comboAuxIndex->currentData().toInt();
    if (currentAuxIndex == cfg.index)
    {
        m_currentAuxIndex = cfg.index;
        loadAuxToUi(cfg); // <-- only here for “real” configs
    }
}

void DialogHardwareConfig::setDeviceConfig(int brightness, qint64 utcEpochSeconds)
{
    // Keep your existing min constraint (you mentioned 10% now)
    if (brightness < ui->sliderBrightness->minimum())
        brightness = ui->sliderBrightness->minimum();
    if (brightness > ui->sliderBrightness->maximum())
        brightness = ui->sliderBrightness->maximum();

    // Block signals to avoid feedback loops (slider<->spin)
    QSignalBlocker b1(ui->sliderBrightness);
    QSignalBlocker b2(ui->spinBrightness);
    ui->sliderBrightness->setValue(brightness);
    ui->spinBrightness->setValue(brightness);

    if (utcEpochSeconds <= 0)
    {
        ui->labelDeviceTime->setText(QStringLiteral("---"));
        // Keep the editor unchanged or set to now; your choice:
        // ui->dateTimeUtc->setDateTime(QDateTime::currentDateTimeUtc());
        return;
    }

    const QDateTime dtUtc = QDateTime::fromSecsSinceEpoch(utcEpochSeconds, Qt::UTC);
    ui->labelDeviceTime->setText(dtUtc.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'")));

    ui->dateTimeUtc->setDateTime(dtUtc);
}

RayCouplerConfig DialogHardwareConfig::currentConfig() const
{
    return m_cfgCopy;
}

void DialogHardwareConfig::loadFromConfigToUi()
{
    if (!m_hasConfig)
        return;

    ui->lineName->setText(m_cfgCopy.name);

    ui->doubleSpinFullScale->setValue(m_cfgCopy.fullScaleW);
    ui->doubleSpinFwdRange->setValue(m_cfgCopy.fwdBarRangeW);
    ui->doubleSpinRefRange->setValue(m_cfgCopy.refBarRangeW);

    ui->doubleSpinHighFwd->setValue(m_cfgCopy.highFwdTripW);
    ui->doubleSpinHighRef->setValue(m_cfgCopy.highRefTripW);
    ui->doubleSpinHighSWR->setValue(m_cfgCopy.highSWRTrip);

    ui->spinTripMask->setValue(static_cast<int>(m_cfgCopy.tripMask));

    // Select type by typeId; if unknown, add it so UI still reflects firmware state
    int typeIdx = ui->comboCouplerType->findData(static_cast<int>(m_cfgCopy.typeId));
    if (typeIdx < 0) {
        const QString label = m_cfgCopy.typeName.isEmpty()
                                  ? tr("Type %1").arg(static_cast<int>(m_cfgCopy.typeId))
                                  : m_cfgCopy.typeName;

        ui->comboCouplerType->addItem(label, static_cast<int>(m_cfgCopy.typeId));
        typeIdx = ui->comboCouplerType->findData(static_cast<int>(m_cfgCopy.typeId));
    }

    QSignalBlocker b(ui->comboCouplerType);
    ui->comboCouplerType->setCurrentIndex(typeIdx);

    // Trip checkboxes from mask
    updateTripCheckboxesFromMask(m_cfgCopy.tripMask);

    // Keep calibration tab in sync with the selected coupler + firmware state
    updateCalibrationFirmwareStatus();
}

void DialogHardwareConfig::saveFromUiToConfig()
{
    if (!m_hasConfig)
        return;

    m_cfgCopy.name = ui->lineName->text().trimmed();
    m_cfgCopy.fullScaleW = ui->doubleSpinFullScale->value();
    m_cfgCopy.fwdBarRangeW = ui->doubleSpinFwdRange->value();
    m_cfgCopy.refBarRangeW = ui->doubleSpinRefRange->value();

    m_cfgCopy.highFwdTripW = ui->doubleSpinHighFwd->value();
    m_cfgCopy.highRefTripW = ui->doubleSpinHighRef->value();
    m_cfgCopy.highSWRTrip = ui->doubleSpinHighSWR->value();

    // Coupler type is the firmware's enable/disable mechanism (None disables)
    m_cfgCopy.typeId = static_cast<quint8>(ui->comboCouplerType->currentData().toInt());

    // Prefer sending firmware enum name when we have it
    const QString fwName = ui->comboCouplerType->currentData(Qt::UserRole + 1).toString();
    if (!fwName.isEmpty())
        m_cfgCopy.typeName = fwName;
    else
        m_cfgCopy.typeName = ui->comboCouplerType->currentText();

    // m_cfgCopy.tripMask = static_cast<quint32>(ui->spinTripMask->value());
    //  spinTripMask stays in sync with the checkboxes for debugging,
    //  but the *authoritative* mask is computed from the checkboxes:
    m_cfgCopy.tripMask = tripMaskFromCheckboxes();
    ui->spinTripMask->setValue(m_cfgCopy.tripMask);
}

void DialogHardwareConfig::on_pushButtonLoad_clicked()
{
    emit ShowStatusBarMessage(tr("Reloading coupler configuration from hardware..."), 2000);
    emit reloadRequested();
}

// Renamed Save button text to Apply in the UI, still using Save in code
void DialogHardwareConfig::on_pushButtonSave_clicked()
{
    if (!ui)
        return;

    QWidget *current = ui->tabWidget->currentWidget();

    if (current == ui->tabDevice)
    { // Device tab
        const int brightness = ui->spinBrightness->value();
        const qint64 epoch = ui->dateTimeUtc->dateTime().toUTC().toSecsSinceEpoch();

        emit ShowStatusBarMessage(tr("Applying device settings to hardware..."), 2000);
        emit applyDeviceRequested(brightness, epoch);
        return;
    }

    if (current == ui->tabCalibration)
    { // Calibration tab
        const int couplerIndex = ui->comboCoupler->currentData().toInt();
        const bool forward = (m_calPath == CalPath::Forward);

        // Multi-point mode
        if (ui->comboCalMode && ui->comboCalMode->currentIndex() == 1) {
            QString errorMessage;
            if (!validateCalibrationTable(&errorMessage)) {
                emit ShowStatusBarMessage(errorMessage, 3500);
                return;
            }

            const QVector<raypm::RayCalPoint> points = calibrationPointsFromTable();

            emit ShowStatusBarMessage(tr("Applying multi-point calibration to %1 path...")
                                          .arg(forward ? tr("Forward") : tr("Reflected")),
                                      2000);

            emit applyCalibrationTableRequested(couplerIndex, forward, points);
            return;
        }

        // 2-point mode
        if (!m_calLow.valid || !m_calHigh.valid)
        {
            emit ShowStatusBarMessage(tr("Capture Low and High first."), 2500);
            return;
        }

        // NOTE: We send raw V + TRUE watts (the user-entered values).
        emit ShowStatusBarMessage(tr("Applying calibration to hardware..."), 2000);
        emit applyCalibrationRequested(couplerIndex,
                                       forward,
                                       m_calLow.rawV,
                                       m_calLow.trueW,
                                       m_calHigh.rawV,
                                       m_calHigh.trueW);
        return;
    }

    // Existing coupler/alarm apply path
    if (!m_hasConfig)
    {
        QMessageBox::warning(this,
                             tr("No configuration"),
                             tr("No coupler configuration is available to save."));
        return;
    }

    saveFromUiToConfig();
    emit ShowStatusBarMessage(tr("Applying coupler configuration to hardware..."), 2000);
    emit applyCouplerRequested(m_cfgCopy);
}

void DialogHardwareConfig::on_pushButtonClose_clicked()
{
    close();
}

void DialogHardwareConfig::on_comboCoupler_currentIndexChanged(int index)
{
    Q_UNUSED(index);

    if (!ui)
        return;

    const int newIdx = ui->comboCoupler->currentData().toInt();
    if (newIdx < 0)
        return;

    // 1) Persist edits for the previously selected coupler
    if (m_hasConfig)
    {
        ensureCouplerSizeForIndex(m_currentCouplerIndex);
        saveFromUiToConfig();
        m_couplerConfigs[m_currentCouplerIndex] = m_cfgCopy;
    }

    // 2) Switch to the newly selected coupler
    m_currentCouplerIndex = newIdx;
    ensureCouplerSizeForIndex(newIdx);

    RayCouplerConfig cfg = m_couplerConfigs.value(newIdx);

    // If we don't yet have anything meaningful in this slot, start from defaults
    const bool looksEmpty = cfg.name.isEmpty() && cfg.fullScaleW <= 0.0 && cfg.typeName.isEmpty() && cfg.typeId == 0;

    if (looksEmpty)
    {
        cfg = defaultConfigForIndex(newIdx);
        m_couplerConfigs[newIdx] = cfg;
    }

    m_cfgCopy = cfg;
    m_hasConfig = true;

    loadFromConfigToUi();
    updateCalibrationFirmwareStatus();
}

RayCouplerConfig DialogHardwareConfig::defaultConfigForIndex(int idx) const
{
    RayCouplerConfig c;
    c.index = idx;
    c.name = QString("Coupler %1").arg(idx + 1);

    // Default values — match firmware defaults
    c.typeId = 1;
    c.typeName = "COUPLER_200W";
    c.fullScaleW = 200.0;
    c.fwdBarRangeW = 200.0;
    c.refBarRangeW = 200.0;

    c.highFwdTripW = 200.0;
    c.highRefTripW = 50.0;
    c.highSWRTrip = 3.0;
    c.tripMask = 0x07; // default: enable all three trips

    return c;
}

void DialogHardwareConfig::updateTripCheckboxesFromMask(quint8 mask)
{
    if (!ui)
        return;

    ui->checkTripFwd->setChecked(mask & 0x01);
    ui->checkTripRef->setChecked(mask & 0x02);
    ui->checkTripSWR->setChecked(mask & 0x04);
}

quint8 DialogHardwareConfig::tripMaskFromCheckboxes() const
{
    quint8 mask = 0;

    if (ui->checkTripFwd->isChecked())
        mask |= 0x01;
    if (ui->checkTripRef->isChecked())
        mask |= 0x02;
    if (ui->checkTripSWR->isChecked())
        mask |= 0x04;

    return mask;
}

void DialogHardwareConfig::onTripCheckboxChanged(bool /*checked*/)
{
    if (!ui)
        return;

    const quint8 mask = tripMaskFromCheckboxes();
    ui->spinTripMask->setValue(mask);
}

int DialogHardwareConfig::selectedCouplerIndex() const
{
    if (!ui)
        return 0;
    return ui->comboCoupler->currentData().toInt(); // 0-based index
}

bool DialogHardwareConfig::isCalForwardPath() const
{
    if (!ui)
        return true;
    return ui->comboCalPath->currentIndex() == 0; // 0=Forward, 1=Reflected
}

void DialogHardwareConfig::setCalibrationLive(double rawValue, double liveWatts)
{
    if (!ui)
        return;

    m_calLive.rawV = rawValue;
    m_calLive.liveW = liveWatts;
    m_calLive.valid = true;

    // If you prefer to avoid “0.0” when idle, you can keep --- if watts==0
    ui->labelLiveRawValue->setText(QString::number(rawValue, 'f', 3));
    ui->labelLivePwrValue->setText(QString::number(liveWatts, 'f', 1));
}

void DialogHardwareConfig::setAuxCalibrationLive(int auxIndex, double volts)
{
    if (!ui)
        return;

    // Only care about the currently selected Aux input
    const int currentIdx = ui->comboAuxIndex->currentData().toInt();
    if (currentIdx != auxIndex)
        return;

    m_auxCalLive.rawV = volts;
    m_auxCalLive.valid = true;

    ui->labelAuxCalLiveRawValue->setText(QString::number(volts, 'f', 3));
}

int DialogHardwareConfig::calibrationSelectedCouplerIndex() const
{
    return ui->comboCoupler->currentData().toInt();
}

bool DialogHardwareConfig::calibrationSelectedForwardPath() const
{
    return ui->comboCalPath->currentData().toInt() == 0; // 0=Forward, 1=Reflected
}

void DialogHardwareConfig::on_pushButtonCalCopy_clicked()
{
    if (!ui)
        return;

    const int idx = calibrationSelectedCouplerIndex();
    const bool fromFwd = calibrationSelectedForwardPath();
    const bool toFwd = !fromFwd;

    const bool isMulti = (ui->comboCalMode && ui->comboCalMode->currentIndex() == 1);

    // --- Multi-point mode: copy current table points to the other path ---
    if (isMulti)
    {
        QString errorMessage;
        if (!validateCalibrationTable(&errorMessage)) {
            emit ShowStatusBarMessage(errorMessage, 3500);
            return;
        }
        const QVector<raypm::RayCalPoint> pts = calibrationPointsFromTable();

        emit ShowStatusBarMessage(tr("Copying table calibration (%1 → %2), %3 point(s)...")
                                      .arg(fromFwd ? tr("Forward") : tr("Reflected"))
                                      .arg(toFwd ? tr("Forward") : tr("Reflected"))
                                      .arg(pts.size()),
                                  2500);

        // Apply the current table to the destination path
        emit applyCalibrationTableRequested(idx, toFwd, pts);
        return;
    }

    // --- 2-point mode: keep legacy firmware copy behavior ---
    emit ShowStatusBarMessage(tr("Copying calibration (%1 → %2)...")
                                  .arg(fromFwd ? tr("Forward") : tr("Reflected"))
                                  .arg(toFwd ? tr("Forward") : tr("Reflected")),
                              2000);

    emit copyCalibrationRequested(idx, fromFwd, toFwd);
}

void DialogHardwareConfig::on_pushButtonCouplerCalReset_clicked()
{
    if (!ui)
        return;

    const int idx = calibrationSelectedCouplerIndex();

    // Reset BOTH paths (label matches UX)
    const QString path = QStringLiteral("both");

    // Decide reset behavior from current calibration source selection:
    //  - 2-point mode  -> FactoryLinear
    //  - multi-point   -> FactoryTable (firmware selects 200W vs 2kW profile based on coupler type)
    const bool isTwoPointMode = (ui->comboCalMode && ui->comboCalMode->currentIndex() == 0);
    const QString calSource = isTwoPointMode ? QStringLiteral("FactoryLinear")
                                             : QStringLiteral("FactoryTable");

    const auto ret = QMessageBox::question(
        this,
        tr("Reset Coupler Calibration"),
        tr("Reset Coupler %1 calibration back to factory defaults?\n\n"
           "This will overwrite the stored Forward and Reflected calibration.\n\n"
           "Reset type: %2")
            .arg(idx + 1)
            .arg(isTwoPointMode ? tr("2-point (FactoryLinear)") : tr("Multi-point (FactoryTable)")),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (ret != QMessageBox::Yes)
        return;

    emit ShowStatusBarMessage(tr("Resetting calibration for Coupler %1...").arg(idx + 1), 2000);
    emit resetCalibrationRequested(idx, path, calSource);
}

void DialogHardwareConfig::ensureAuxSizeForIndex(int auxIndex)
{
    if (auxIndex < 0)
        return;
    if (m_auxConfigs.size() <= auxIndex)
    {
        m_auxConfigs.resize(auxIndex + 1);
    }
}

void DialogHardwareConfig::ensureCouplerSizeForIndex(int couplerIndex)
{
    if (couplerIndex < 0)
        return;

    if (m_couplerConfigs.size() <= couplerIndex)
        m_couplerConfigs.resize(couplerIndex + 1);
}

void DialogHardwareConfig::loadAuxToUi(const raypm::RayAuxConfig &cfg)
{
    QSignalBlocker b1(ui->comboAuxType);
    QSignalBlocker b2(ui->comboAuxTempModel);

    ui->lineAuxName->setText(cfg.name);

    // Type
    int typeIdx = ui->comboAuxType->findData(cfg.typeName);
    if (typeIdx < 0)
        typeIdx = 0;
    ui->comboAuxType->setCurrentIndex(typeIdx);

    // Temperature model
    int modelIdx = ui->comboAuxTempModel->findData(cfg.tempModel);
    if (modelIdx < 0)
        modelIdx = 0;
    ui->comboAuxTempModel->setCurrentIndex(modelIdx);

    // Ranges
    ui->doubleAuxVoltageMaxRange->setValue(cfg.voltageMaxRange);
    ui->doubleAuxCurrentMaxRange->setValue(cfg.currentMaxRange);
    ui->doubleAuxTemperatureMaxRange->setValue(cfg.temperatureMaxRange);

    // Thermistor params
    ui->doubleAuxThermSeriesOhms->setValue(cfg.thermSeriesOhms);
    ui->doubleAuxThermR0Ohms->setValue(cfg.thermR0Ohms);
    ui->doubleAuxThermBeta->setValue(cfg.thermBeta);
    ui->doubleAuxThermT0C->setValue(cfg.thermT0C);

    updateAuxUiEnabledState();
}

void DialogHardwareConfig::saveAuxFromUi(raypm::RayAuxConfig &cfg,
                                         bool includeCalibrationFields /*= false*/) const
{
    // Always update the core identity fields
    cfg.name = ui->lineAuxName->text().trimmed();
    cfg.typeName = ui->comboAuxType->currentData().toString(); // "none|voltage|current|temperature"
    cfg.tempModel = ui->comboAuxTempModel->currentData().toString(); // "linear|thermistor"

    const QString typeKey = cfg.typeName;
    const bool isVoltage = (typeKey == "voltage");
    const bool isCurrent = (typeKey == "current");
    const bool isTemp = (typeKey == "temperature");
    const bool isTherm = (isTemp && cfg.tempModel == "thermistor");

    // --- Ranges: only set the one that applies; clear the others ---
    if (isVoltage) {
        cfg.voltageMaxRange = ui->doubleAuxVoltageMaxRange->value();
        cfg.currentMaxRange = 0.0;
        cfg.temperatureMaxRange = 0.0;
    } else if (isCurrent) {
        cfg.currentMaxRange = ui->doubleAuxCurrentMaxRange->value();
        cfg.voltageMaxRange = 0.0;
        cfg.temperatureMaxRange = 0.0;
    } else if (isTemp) {
        cfg.temperatureMaxRange = ui->doubleAuxTemperatureMaxRange->value();
        cfg.voltageMaxRange = 0.0;
        cfg.currentMaxRange = 0.0;
    } else {
        // none
        cfg.voltageMaxRange = 0.0;
        cfg.currentMaxRange = 0.0;
        cfg.temperatureMaxRange = 0.0;
    }

    // --- Thermistor fields: only valid for temperature+thermistor, else clear ---
    if (isTherm) {
        cfg.thermSeriesOhms = ui->doubleAuxThermSeriesOhms->value();
        cfg.thermR0Ohms = ui->doubleAuxThermR0Ohms->value();
        cfg.thermBeta = ui->doubleAuxThermBeta->value();
        cfg.thermT0C = ui->doubleAuxThermT0C->value();
    } else {
        cfg.thermSeriesOhms = 0.0;
        cfg.thermR0Ohms = 0.0;
        cfg.thermBeta = 0.0;
        cfg.thermT0C = 0.0;
    }

    // IMPORTANT:
    // Do NOT touch cfg.calM / cfg.calB here unless Apply Aux calibration was pressed.
    if (includeCalibrationFields) {
        // Build a linear cal from the two captured points:
        //   true = m*raw + b
        // where raw is in volts (device raw_volts) and true is in user units.
        const double rawLowV = m_auxCalLow.rawV;
        const double rawHighV = m_auxCalHigh.rawV;
        const double trueLow = m_auxCalLow.trueVal;
        const double trueHigh = m_auxCalHigh.trueVal;

        const double dx = (rawHighV - rawLowV);

        // Must have two distinct raw points
        if (m_auxCalLow.valid && m_auxCalHigh.valid && std::fabs(dx) > 1e-9) {
            const double m = (trueHigh - trueLow) / dx;
            const double b = trueLow - (m * rawLowV);

            cfg.calM = static_cast<float>(m);
            cfg.calB = static_cast<float>(b);
        } else {
            // If invalid or degenerate, do not stomp existing cfg.calM/calB.
            // (You can optionally log or message the user elsewhere.)
        }
    }
}

void DialogHardwareConfig::updateAuxUiEnabledState()
{
    const QString typeKey = ui->comboAuxType->currentData().toString();

    const bool isVoltage = (typeKey == QLatin1String("voltage"));
    const bool isCurrent = (typeKey == QLatin1String("current"));
    const bool isTemperature = (typeKey == QLatin1String("temperature"));

    ui->labelAuxVoltageMaxRange->setEnabled(isVoltage);
    ui->labelAuxVoltageMaxRange->setVisible(isVoltage);
    ui->doubleAuxVoltageMaxRange->setEnabled(isVoltage);
    ui->doubleAuxVoltageMaxRange->setVisible(isVoltage);

    ui->labelAuxCurrentMaxRange->setEnabled(isCurrent);
    ui->labelAuxCurrentMaxRange->setVisible(isCurrent);
    ui->doubleAuxCurrentMaxRange->setEnabled(isCurrent);
    ui->doubleAuxCurrentMaxRange->setVisible(isCurrent);

    ui->labelAuxTemperatureMaxRange->setEnabled(isTemperature);
    ui->labelAuxTemperatureMaxRange->setVisible(isTemperature);
    ui->doubleAuxTemperatureMaxRange->setEnabled(isTemperature);
    ui->doubleAuxTemperatureMaxRange->setVisible(isTemperature);

    const bool thermFieldsEnabled = isTemperature;
    ui->labelAuxTempModel->setEnabled(isTemperature);
    ui->labelAuxTempModel->setVisible(isTemperature);
    ui->comboAuxTempModel->setEnabled(isTemperature);
    ui->comboAuxTempModel->setVisible(isTemperature);

    ui->labelAuxThermSeriesOhms->setEnabled(thermFieldsEnabled);
    ui->labelAuxThermSeriesOhms->setVisible(thermFieldsEnabled);
    ui->doubleAuxThermSeriesOhms->setEnabled(thermFieldsEnabled);
    ui->doubleAuxThermSeriesOhms->setVisible(thermFieldsEnabled);
    ui->labelAuxThermR0Ohms->setEnabled(thermFieldsEnabled);
    ui->labelAuxThermR0Ohms->setVisible(thermFieldsEnabled);
    ui->doubleAuxThermR0Ohms->setEnabled(thermFieldsEnabled);
    ui->doubleAuxThermR0Ohms->setVisible(thermFieldsEnabled);
    ui->labelAuxThermBeta->setEnabled(thermFieldsEnabled);
    ui->labelAuxThermBeta->setVisible(thermFieldsEnabled);
    ui->doubleAuxThermBeta->setEnabled(thermFieldsEnabled);
    ui->doubleAuxThermBeta->setVisible(thermFieldsEnabled);
    ui->labelAuxThermT0C->setEnabled(thermFieldsEnabled);
    ui->labelAuxThermT0C->setVisible(thermFieldsEnabled);
    ui->doubleAuxThermT0C->setEnabled(thermFieldsEnabled);
    ui->doubleAuxThermT0C->setVisible(thermFieldsEnabled);
}

void DialogHardwareConfig::on_comboAuxIndex_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    const int auxIndex = ui->comboAuxIndex->currentData().toInt();
    m_currentAuxIndex = auxIndex;

    if (auxIndex < 0)
        return;

    // If we’ve already cached config for this Aux index, show it.
    if (auxIndex >= 0 && auxIndex < m_auxConfigs.size())
    {
        loadAuxToUi(m_auxConfigs[auxIndex]);
    }
    else
    {
        // No config yet for this Aux index:
        // - leave the fields blank / default (what you see now), OR
        // - optionally clear them explicitly if you like.
        ui->lineAuxName->clear();
        ui->comboAuxType->setCurrentIndex(0); // "None"
        // (Other fields can be reset here if you want a clean "Loading..." state.)
    }
}

void DialogHardwareConfig::on_comboAuxType_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    updateAuxUiEnabledState();
}

void DialogHardwareConfig::on_pushButtonAuxApply_clicked()
{
    if (ui->comboAuxIndex->currentIndex() < 0)
        return;

    const int auxIndex = ui->comboAuxIndex->currentData().toInt();
    if (auxIndex < 0)
        return;

    // Ensure we have a config slot for this Aux index, even if no config
    // was fetched from firmware yet.
    ensureAuxSizeForIndex(auxIndex);

    raypm::RayAuxConfig &cfg = m_auxConfigs[auxIndex];
    cfg.index = auxIndex; // make sure index is correct

    // Copy current UI state into cfg
    saveAuxFromUi(cfg, false);

    emit applyAuxRequested(cfg);
    emit ShowStatusBarMessage(tr("Aux %1 configuration updated (pending apply).").arg(auxIndex + 1),
                              3000);
}

void DialogHardwareConfig::on_pushButtonAuxCalCaptureLow_clicked()
{
    if (!m_auxCalLive.valid)
        return; // nothing to capture yet

    m_auxCalLow.rawV = m_auxCalLive.rawV;

    m_auxCalLow.trueVal = ui->doubleAuxCalTrueLow->value();
    m_auxCalLow.valid = true;
}

void DialogHardwareConfig::on_pushButtonAuxCalCaptureHigh_clicked()
{
    if (!m_auxCalLive.valid)
        return;

    m_auxCalHigh.rawV = m_auxCalLive.rawV;
    m_auxCalHigh.trueVal = ui->doubleAuxCalTrueHigh->value();
    m_auxCalHigh.valid = true;
}

void DialogHardwareConfig::on_pushButtonAuxCalReset_clicked()
{
    m_auxCalLive = AuxCalSample{};
    m_auxCalLow = AuxCalSample{};
    m_auxCalHigh = AuxCalSample{};

    // No special labels to clear, since we’re not showing captured raw V
    // explicitly for Aux. The True Low/High spin boxes stay as-is.
}

void DialogHardwareConfig::on_pushButtonAuxCalApply_clicked()
{
    // Determine which Aux index is selected
    int auxIndex = m_currentAuxIndex;
    if (ui->comboAuxIndex->currentData().isValid())
        auxIndex = ui->comboAuxIndex->currentData().toInt();

    if (auxIndex < 0 || auxIndex >= m_auxConfigs.size()) {
        QMessageBox::warning(this, tr("Aux Calibration"), tr("Invalid Aux input selection."));
        return;
    }

    // Require both captures
    if (!m_auxCalLow.valid || !m_auxCalHigh.valid) {
        QMessageBox::warning(this,
                             tr("Aux Calibration"),
                             tr("Capture both Low and High before applying calibration."));
        return;
    }

    const float rawLow = m_auxCalLow.rawV;
    const float rawHigh = m_auxCalHigh.rawV;

    if (qFuzzyCompare(rawLow, rawHigh)) {
        QMessageBox::warning(
            this,
            tr("Aux Calibration"),
            tr("Low/High raw readings are identical. Re-capture using two different levels."));
        return;
    }

    const float trueLow = static_cast<float>(ui->doubleAuxCalTrueLow->value());
    const float trueHigh = static_cast<float>(ui->doubleAuxCalTrueHigh->value());

    // Emit the dedicated calibration request (HTTP worker uses /api/aux/calibrate/2point)
    emit auxCalibrationApplyRequested(auxIndex, rawLow, trueLow, rawHigh, trueHigh);

    // Update local cached cfg.calM/calB so the dialog reflects what we just applied
    // without waiting for a round-trip. This does NOT send config; it only updates the cache/UI.
    raypm::RayAuxConfig &cfg = m_auxConfigs[auxIndex];
    saveAuxFromUi(cfg, /*includeCalibrationFields=*/true);
}

void DialogHardwareConfig::on_comboCalMode_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    if (!ui)
        return;

    // If switching back to 2-point while points exist, warn user we won't use them.
    if (ui->comboCalMode->currentIndex() == 0 && ui->tableCalPoints->rowCount() > 0)
    {
        const auto ret = QMessageBox::question(this,
                                               tr("Switch to 2-point mode"),
                                               tr("Multi-point table has %1 point(s).\n"
                                                  "2-point mode will ignore the table.\n\n"
                                                  "Do you want to continue?")
                                                   .arg(ui->tableCalPoints->rowCount()),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No);

        if (ret != QMessageBox::Yes)
        {
            QSignalBlocker b(ui->comboCalMode);
            ui->comboCalMode->setCurrentIndex(1); // revert to Multi-point
            return;
        }
    }

    updateCalibrationUiMode();
    updateCalibrationFirmwareStatus();
}

void DialogHardwareConfig::on_pushButtonCalCapturePoint_clicked()
{
    if (!ui)
        return;

    if (!m_calLive.valid)
    {
        emit ShowStatusBarMessage(tr("No live value yet to capture."), 2500);
        return;
    }

    if (!ui->tableCalPoints)
        return;

    const double v = m_calLive.rawV;
    const double wTrue = ui->doubleSpinCalPointTrueW->value();

    if (!std::isfinite(v) || v < 0.0 || v > 3.3)
    {
        emit ShowStatusBarMessage(tr("Live calibration voltage must be between 0 and 3.3 V."), 2500);
        return;
    }

    auto *table = ui->tableCalPoints;
    const int rows = table->rowCount();
    int insertAt = rows;
    int rowToUse = -1;
    for (int r = 0; r < rows; ++r)
    {
        const auto *item = table->item(r, 0);
        bool ok = false;
        const double existingV = item ? item->text().toDouble(&ok) : 0.0;
        if (!ok || !std::isfinite(existingV))
            continue;
        if (std::abs(existingV - v) <= 1e-6)
        {
            rowToUse = r;
            break;
        }
        if (insertAt == rows && existingV > v)
            insertAt = r;
    }

    if (rowToUse < 0)
    {
        if (rows >= MAX_CAL_POINTS)
        {
            emit ShowStatusBarMessage(tr("Calibration table is full (%1 points).").arg(MAX_CAL_POINTS), 2500);
            return;
        }
        rowToUse = insertAt;
        table->insertRow(rowToUse);
    }

    table->setItem(rowToUse, 0, makeEditableCalItem(QString::number(v, 'g', 9)));
    table->setItem(rowToUse, 1, makeEditableCalItem(QString::number(wTrue, 'g', 9)));

    emit ShowStatusBarMessage(tr("Captured point: %1 V → %2 W")
                                  .arg(v, 0, 'f', 3)
                                  .arg(wTrue, 0, 'f', 1),
                              1500);
}

void DialogHardwareConfig::on_pushButtonCalRemovePoint_clicked()
{
    if (!ui || !ui->tableCalPoints)
        return;

    const int row = ui->tableCalPoints->currentRow();
    if (row < 0)
        return;

    ui->tableCalPoints->removeRow(row);
}

void DialogHardwareConfig::on_pushButtonCalClearPoints_clicked()
{
    if (!ui || !ui->tableCalPoints)
        return;

    ui->tableCalPoints->setRowCount(0);
}

void DialogHardwareConfig::updateCalibrationUiMode()
{
    if (!ui)
        return;

    const bool multi = (ui->comboCalMode && ui->comboCalMode->currentIndex() == 1);

    if (ui->groupBoxCalMulti)
        ui->groupBoxCalMulti->setVisible(multi);

    // 2-point widgets
    const bool two = !multi;
    const auto setTwoVisible = [&](QWidget *w)
    {
        if (w)
            w->setVisible(two);
    };

    setTwoVisible(ui->labelTrueLow);
    setTwoVisible(ui->spinCalLowTrueW);
    setTwoVisible(ui->labelTrueHigh);
    setTwoVisible(ui->spinCalHighTrueW);
    setTwoVisible(ui->pushButtonCalCaptureLow);
    setTwoVisible(ui->pushButtonCalCaptureHigh);
    setTwoVisible(ui->pushButtonCalReset);

    setTwoVisible(ui->labelCapLowCaption);
    setTwoVisible(ui->labelCapLowRawV);
    setTwoVisible(ui->labelCapLowRawVUnit);
    setTwoVisible(ui->labelCapLowPwrWCaption);
    setTwoVisible(ui->labelCapLowPwrW);
    setTwoVisible(ui->labelCapLowPwrWUnit);

    setTwoVisible(ui->labelCapHighCaption);
    setTwoVisible(ui->labelCapHighRawV);
    setTwoVisible(ui->labelCapHighRawVUnit);
    setTwoVisible(ui->labelCapHighPwrWCaption);
    setTwoVisible(ui->labelCapHighPwrW);
    setTwoVisible(ui->labelCapHighPwrWUnit);
}

void DialogHardwareConfig::updateCalibrationFirmwareStatus()
{
    if (!ui)
        return;

    const int idx = calibrationSelectedCouplerIndex();
    const bool forward = (m_calPath == CalPath::Forward);

    if (idx < 0 || idx >= m_couplerConfigs.size()) {
        ui->labelCalFirmwareStatus->setText(QStringLiteral("---"));
        return;
    }

    const raypm::RayCouplerConfig &cfg = m_couplerConfigs[idx];
    const raypm::RayCalTable &t = forward ? cfg.fwdCal : cfg.refCal;

    const QString mode = t.mode.trimmed().toLower();
    const int count = (t.count > 0) ? t.count : t.points.size();

    // Detect when the VIEW context changes (coupler or path).
    // Only then do we clear the table for linear mode (prevents stale data).
    const bool viewChanged = (idx != m_lastCalViewCouplerIndex)
                             || (forward != m_lastCalViewForward);
    m_lastCalViewCouplerIndex = idx;
    m_lastCalViewForward = forward;

    // Show firmware state (always accurate)
    if (mode == QLatin1String("table"))
        ui->labelCalFirmwareStatus->setText(tr("Table (%1 point(s))").arg(count));
    else
        ui->labelCalFirmwareStatus->setText(tr("Linear (2-point)"));

    // Update visibility of 2-point vs table controls based on the USER-selected mode
    updateCalibrationUiMode();

    // If firmware is table and UI is in table mode, load firmware points
    if (ui->comboCalMode && ui->comboCalMode->currentIndex() == 1) {
        if (mode == QLatin1String("table") && count >= 2) {
            loadCalibrationPointsToTable(t.points);
        } else {
            // Firmware is linear: clear table ONLY when switching couplers/paths
            // (so we don't wipe user edits while they are building a table)
            if (viewChanged)
                loadCalibrationPointsToTable(QVector<raypm::RayCalPoint>{});
        }
    }
}

void DialogHardwareConfig::configureCalibrationTable()
{
    if (!ui || !ui->tableCalPoints)
        return;

    auto *table = ui->tableCalPoints;

    table->setColumnCount(2);
    table->setHorizontalHeaderLabels(QStringList{tr("Raw (V)"), tr("True (W)")});

    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);

    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked
                           | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);

    table->setTabKeyNavigation(true);
    table->setAlternatingRowColors(false);

    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    table->verticalHeader()->setVisible(true);
}

QTableWidgetItem *DialogHardwareConfig::makeEditableCalItem(const QString &text) const
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    return item;
}

bool DialogHardwareConfig::validateCalibrationTable(QString *errorMessage) const
{
    if (!ui || !ui->tableCalPoints) {
        if (errorMessage)
            *errorMessage = tr("Calibration table widget is not available.");
        return false;
    }

    const int rows = ui->tableCalPoints->rowCount();
    int validCount = 0;
    double lastVolts = -1.0;

    for (int r = 0; r < rows; ++r) {
        const auto *iv = ui->tableCalPoints->item(r, 0);
        const auto *iw = ui->tableCalPoints->item(r, 1);

        const QString sv = iv ? iv->text().trimmed() : QString();
        const QString sw = iw ? iw->text().trimmed() : QString();

        // allow completely empty row to be ignored
        if (sv.isEmpty() && sw.isEmpty())
            continue;

        bool okV = false;
        bool okW = false;
        const double v = sv.toDouble(&okV);
        const double w = sw.toDouble(&okW);

        if (!okV || !okW || !std::isfinite(v) || !std::isfinite(w)) {
            if (errorMessage)
                *errorMessage = tr("Row %1 contains invalid numeric text.").arg(r + 1);
            return false;
        }

        if (v < 0.0 || v > 3.3) {
            if (errorMessage)
                *errorMessage = tr("Row %1 Raw (V) must be between 0 and 3.3.").arg(r + 1);
            return false;
        }

        if (w < 0.0) {
            if (errorMessage)
                *errorMessage = tr("Row %1 True (W) cannot be negative.").arg(r + 1);
            return false;
        }

        if (validCount > 0 && v - lastVolts <= 1e-6) {
            if (errorMessage)
                *errorMessage = tr("Table requires strictly increasing voltage. "
                                   "Fix row %1.")
                                    .arg(r + 1);
            return false;
        }

        lastVolts = v;
        ++validCount;
    }

    if (validCount < 2 || validCount > MAX_CAL_POINTS) {
        if (errorMessage)
            *errorMessage = tr("Calibration requires 2..20 valid points (got %1).").arg(validCount);
        return false;
    }

    return true;
}

void DialogHardwareConfig::loadCalibrationPointsToTable(const QVector<raypm::RayCalPoint> &points)
{
    if (!ui || !ui->tableCalPoints)
        return;

    QVector<raypm::RayCalPoint> sorted = points;
    std::sort(sorted.begin(),
              sorted.end(),
              [](const raypm::RayCalPoint &a, const raypm::RayCalPoint &b) {
                  return a.volts < b.volts;
              });

    QSignalBlocker b(ui->tableCalPoints);
    ui->tableCalPoints->setRowCount(0);

    for (const auto &p : sorted) {
        const int r = ui->tableCalPoints->rowCount();
        ui->tableCalPoints->insertRow(r);
        ui->tableCalPoints->setItem(r, 0, makeEditableCalItem(QString::number(p.volts, 'g', 9)));
        ui->tableCalPoints->setItem(r, 1, makeEditableCalItem(QString::number(p.watts, 'g', 9)));
    }
}

QVector<raypm::RayCalPoint> DialogHardwareConfig::calibrationPointsFromTable() const
{
    QVector<raypm::RayCalPoint> out;
    if (!ui || !ui->tableCalPoints)
        return out;

    out.reserve(ui->tableCalPoints->rowCount());

    for (int r = 0; r < ui->tableCalPoints->rowCount(); ++r)
    {
        const auto *iv = ui->tableCalPoints->item(r, 0);
        const auto *iw = ui->tableCalPoints->item(r, 1);

        if (!iv || !iw || iv->text().trimmed().isEmpty() || iw->text().trimmed().isEmpty())
            continue;

        const double v = iv->text().toDouble();
        const double w = iw->text().toDouble();

        raypm::RayCalPoint p;
        p.volts = v;
        p.watts = w;
        out.push_back(p);
    }

    std::sort(out.begin(), out.end(), [](const raypm::RayCalPoint &a, const raypm::RayCalPoint &b)
              { return a.volts < b.volts; });

    return out;
}
