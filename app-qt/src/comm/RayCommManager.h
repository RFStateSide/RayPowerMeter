// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QObject>
#include <QThread>
#include <QVector>

#include "core/AppSettings.h"
#include "core/RayAuxConfig.h"
#include "core/RayCouplerConfig.h"
#include "core/RayTelemetry.h"

namespace raypm {

class RayCommWorkerBase;

class RayCommManager : public QObject
{
    Q_OBJECT
public:
    explicit RayCommManager(QObject *parent = nullptr);
    ~RayCommManager() override;

    // Called from MainWindow after loading settings
    void setSettings(const AppSettings &settings);

    // Ask the device to clear any latched protection / alarms
    void requestProtectionReset();

    // Apply new coupler configuration to the device
    void applyCouplerConfig(const RayCouplerConfig &cfg);
    void applyCouplerCalibrationTable(int index,
                                      bool forwardPath,
                                      const QVector<RayCalPoint> &points);
    // Apply new device configuration to the device
    void requestDeviceConfig();
    void applyDeviceConfig(int brightness, qint64 utcEpochSeconds);
    void applyCouplerCalibration(
        int index, bool forwardPath, double vLow, double wLowTrue, double vHigh, double wHighTrue);
    void couplerCalibrationCopied(int index, bool ok, double m, double b, const QString &message);
    void resetCouplerCalibration(int index, const QString &path, const QString &calSource);
    void requestModulationStart(int coupler, const QString &path, int samples, int sampleRateHz);
    void requestModulationStatus();
    void requestModulationData(int maxSamples);
    // Aux Inputs configuration
    void requestAuxConfig();
    void applyAuxConfig(const RayAuxConfig &cfg);
    // Aux Digital I/O
    void requestAuxDigitalConfig(int index = -1);
    void requestAuxDigitalState(int index = -1);

    void setAuxDigitalValue(int index, bool value);

    // Simple helper: keeps existing call sites working.
    void applyAuxDigitalConfig(int index, int modeId);

    // Full firmware-level API, matching set_aux_digital_config.
    void applyAuxDigitalConfig(
        int index, int modeId, int pullId, bool defaultState, const QString &name);

    // 2-point Aux calibration
    void applyAuxCalibration(int index, double vLow, double engLow, double vHigh, double engHigh);

    // OTA
    void startOtaFromUrl(const QString &url);
    void requestOtaStatus();
    QString deviceAppName() const { return m_deviceAppName; }
    QString deviceAppVersion() const { return m_deviceAppVersion; }

public slots:
    void start();
    void stop();
    void setBackgroundPollingEnabled(bool enabled);
    void requestCouplerConfig();
    void copyCouplerCalibration(int index, bool fromForward, bool toForward);

signals:
    void connectedChanged(bool connected);
    void telemetryReceived(const RayTelemetry &telemetry);
    void errorOccurred(const QString &message);
    void couplerConfigReceived(const RayCouplerConfig &config);
    void deviceConfigReceived(int brightness, qint64 utcEpochSeconds);
    void couplerCalibrationApplied(int index, bool ok, const QString &message);
    void modulationStartAck(bool ok,
                            const QString &message,
                            int coupler,
                            const QString &path,
                            int samples,
                            int sampleRateHz);

    void modulationStatusReceived(
        const QString &state, int coupler, const QString &path, int samples, int sampleRateHz);

    void modulationDataReceived(int coupler,
                                const QString &path,
                                int sampleRateHz,
                                const QVector<qint16> &samples);

    void auxConfigReceived(const RayAuxConfig &config);
    void auxConfigApplied(int index, bool ok, const QString &message);
    void auxCalibrationApplied(int index, bool ok, const QString &message);

    // OTA
    void otaStartAck(bool ok, const QString &message);
    void otaStatusReceived(const QString &state,
                           int progressPercent,
                           const QString &message,
                           bool done,
                           bool ok,
                           const QString &rawJson);

private:
    void recreateWorker();

    AppSettings m_settings{};
    RayCommWorkerBase *m_worker = nullptr;
    QThread *m_thread = nullptr; // used only for Serial later

    QString m_deviceAppName;
    QString m_deviceAppVersion;
};

} // namespace raypm
