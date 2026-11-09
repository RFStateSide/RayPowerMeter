// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "RayCommWorkerBase.h"
#include "core/RayAuxConfig.h"
#include "core/RayCouplerConfig.h"

#include <QByteArray>
#include <QJsonObject>
#include <QSerialPort>
#include <QTimer>

class QThread;

namespace raypm {

class RaySerialCommWorker : public RayCommWorkerBase
{
    Q_OBJECT
public:
    explicit RaySerialCommWorker(const AppSettings &settings, QObject *parent = nullptr);

    // Called by RayCommManager before starting the worker thread.
    // Moves QSerialPort + poll timer into the same thread as the worker.
    void migrateToThread(QThread *thread);

    void applyCouplerConfig(const RayCouplerConfig &cfg);
    void requestDeviceConfig() override;
    void applyDeviceConfig(int brightness, const qint64 utcEpochSeconds) override;

    void requestModulationStart(int coupler,
                                const QString &path,
                                int samples,
                                int sampleRateHz) override;

    void requestModulationStatus() override;

    void requestModulationData(int maxSamples) override;

public slots:
    void start() override;
    void stop() override;
    void setBackgroundPollingEnabled(bool enabled) override;
    void requestCouplerConfig();
    void applyCouplerCalibration(int index,
                                 bool forwardPath,
                                 double vLow,
                                 double wLowTrue,
                                 double vHigh,
                                 double wHighTrue) override;
    void applyCouplerCalibrationTable(int index,
                                      bool forwardPath,
                                      const QVector<RayCalPoint> &points) override;
    void copyCouplerCalibration(int index, bool fromForward, bool toForward) override;
    void resetCouplerCalibration(int index, const QString &path, const QString &calSource) override;

    // Aux Inputs (currently not supported over Serial; stubbed)
    void requestAuxConfig() override;
    void applyAuxConfig(const raypm::RayAuxConfig &cfg) override;
    void applyAuxCalibration(
        int index, double vLow, double engLow, double vHigh, double engHigh) override;

    // Aux Digital I/O
    void setAuxDigitalValue(int index, bool value) override;
    void applyAuxDigitalConfig(int index, int modeId) override;
    void applyAuxDigitalConfig(
        int index, int modeId, int pullId, bool defaultState, const QString &name) override;
    void requestAuxDigitalConfig(int index) override;
    void requestAuxDigitalState(int index) override;

    void requestProtectionReset();

    void startOtaFromUrl(const QString &url) override;
    void requestOtaStatus() override;

private slots:
    void onReadyRead();
    void onPollTimeout();

private:
    void openPort();
    void closePort();
    void requestStatus();

    // Helpers for JSON parsing
    void handleJsonLine(const QByteArray &line);
    bool parseStatusJson(const QJsonObject &root, RayTelemetry &out);
    bool parseTelemetryJson(const QJsonObject &root, RayTelemetry &out);
    bool sendJsonCommand(const QJsonObject &obj);

    QSerialPort m_serial;
    QTimer      m_pollTimer;
    bool        m_connected = false;

    // Track whether we’ve already fetched static config for this session
    bool m_couplerConfigRequested = false;
    bool m_auxConfigRequested = false;

    // Raw line buffer for newline-terminated JSON from firmware
    QByteArray m_rxBuffer;

};

} // namespace raypm
