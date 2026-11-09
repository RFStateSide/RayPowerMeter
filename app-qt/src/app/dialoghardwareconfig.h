// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QDialog>
#include <QTableWidgetItem>
#include <QVector>

#include "core/RayAuxConfig.h"
#include "core/RayCouplerConfig.h"

namespace raypm {
struct RayCouplerConfig;
struct RayAuxConfig;
} // namespace raypm

namespace Ui {
class DialogHardwareConfig;
}

class DialogHardwareConfig : public QDialog
{
    Q_OBJECT

public:
    explicit DialogHardwareConfig(QWidget *parent = nullptr);
    ~DialogHardwareConfig() override;

    // Called by MainWindow when fresh config arrives from the hardware
    void setCouplerConfig(const raypm::RayCouplerConfig &cfg);

    void setAuxConfig(const raypm::RayAuxConfig &cfg);

    // Called by MainWindow when fresh device settings arrive from the hardware
    raypm::RayCouplerConfig currentConfig() const;
    // Read back the edited device settings from the hardware
    void setDeviceConfig(int brightness, qint64 utcEpochSeconds);
    int selectedCouplerIndex() const;
    bool isCalForwardPath() const;
    void setCalibrationLive(double rawValue, double liveWatts);
    void setAuxCalibrationLive(int auxIndex, double volts);

    int calibrationSelectedCouplerIndex() const;
    bool calibrationSelectedForwardPath() const;

    void selectInitialCoupler();

signals:
    void reloadRequested();
    void applyCouplerRequested(const raypm::RayCouplerConfig &cfg);
    void applyAuxRequested(const raypm::RayAuxConfig &cfg);
    void ShowStatusBarMessage(const QString &msg, int timeoutMs = 3000);
    void applyDeviceRequested(int brightness, qint64 utcEpochSeconds);
    void applyCalibrationRequested(int couplerIndex,
                                   bool forwardPath,
                                   double vLow,
                                   double wLowTrue,
                                   double vHigh,
                                   double wHighTrue);
    void applyCalibrationTableRequested(int couplerIndex,
                                        bool forwardPath,
                                        const QVector<raypm::RayCalPoint> &points);
    void copyCalibrationRequested(int couplerIndex, bool fromForward, bool toForward);
    void resetCalibrationRequested(int couplerIndex,
                                   const QString &path,     // "fwd"|"ref"|"both"
                                   const QString &calSource // "FactoryLinear" | "FactoryTable"
    );
    void auxCalibrationApplyRequested(
        int auxIndex, double rawLow, double trueLow, double rawHigh, double trueHigh);

private slots:
    void on_pushButtonLoad_clicked();
    void on_pushButtonSave_clicked();
    void on_pushButtonClose_clicked();
    void on_comboCoupler_currentIndexChanged(int index);
    void onTripCheckboxChanged(bool checked);
    void on_pushButtonCalCopy_clicked();
    void on_pushButtonCouplerCalReset_clicked();
    void on_comboCalMode_currentIndexChanged(int index);
    void on_pushButtonCalCapturePoint_clicked();
    void on_pushButtonCalRemovePoint_clicked();
    void on_pushButtonCalClearPoints_clicked();
    void on_comboAuxIndex_currentIndexChanged(int index);
    void on_pushButtonAuxApply_clicked();
    void on_comboAuxType_currentIndexChanged(int index);
    void on_pushButtonAuxCalCaptureLow_clicked();
    void on_pushButtonAuxCalCaptureHigh_clicked();
    void on_pushButtonAuxCalReset_clicked();
    void on_pushButtonAuxCalApply_clicked();

private:
    void loadFromConfigToUi();
    void updateCalibrationUiMode();
    void updateCalibrationFirmwareStatus();
    void loadCalibrationPointsToTable(const QVector<raypm::RayCalPoint> &points);
    QVector<raypm::RayCalPoint> calibrationPointsFromTable() const;
    void saveFromUiToConfig();

    void configureCalibrationTable();
    QTableWidgetItem *makeEditableCalItem(const QString &text) const;
    bool validateCalibrationTable(QString *errorMessage = nullptr) const;

    // --- Aux Inputs state ---
    QVector<raypm::RayAuxConfig> m_auxConfigs;
    int m_currentAuxIndex = 0;

    void initAuxUi();
    void loadAuxFromConfigToUi(int index);
    void saveAuxFromUiToConfig(int index);
    void updateAuxUiEnabledState();

    // --- Coupler state (one RayCouplerConfig per index) ---
    QVector<raypm::RayCouplerConfig> m_couplerConfigs;
    int m_currentCouplerIndex = 0;

    void ensureCouplerSizeForIndex(int couplerIndex);

    Ui::DialogHardwareConfig *ui;

    // editable copy of current coupler config
    raypm::RayCouplerConfig m_cfgCopy;
    bool m_hasConfig = false;

    raypm::RayCouplerConfig defaultConfigForIndex(int idx) const;

    void ensureAuxSizeForIndex(int auxIndex);
    void loadAuxToUi(const raypm::RayAuxConfig &cfg);
    void saveAuxFromUi(raypm::RayAuxConfig &cfg, bool includeCalibrationFields) const;

    void updateTripCheckboxesFromMask(quint8 mask);
    quint8 tripMaskFromCheckboxes() const;

    // Calibration capture state (per open dialog; later store per coupler+path if desired)
    struct CalCapturePoint
    {
        double rawV = 0.0;
        double liveW = 0.0;
        double trueW = 0.0;
        bool valid = false;
    };

    CalCapturePoint m_calLive;
    CalCapturePoint m_calLow;
    CalCapturePoint m_calHigh;

    // Track which path the user is calibrating on the tab
    enum class CalPath { Forward = 0, Reflected = 1 };
    CalPath m_calPath = CalPath::Forward;

    // --- Aux calibration state ---
    struct AuxCalSample
    {
        double rawV = 0.0;    // captured raw ADC volts
        double trueVal = 0.0; // user-entered engineering value
        bool valid = false;
    };

    AuxCalSample m_auxCalLive;
    AuxCalSample m_auxCalLow;
    AuxCalSample m_auxCalHigh;

    int m_lastCalViewCouplerIndex = -1;
    bool m_lastCalViewForward = true;
};
