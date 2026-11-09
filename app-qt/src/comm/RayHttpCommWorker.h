// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "RayCommWorkerBase.h"
#include "core/RayAuxConfig.h"
#include "core/RayCouplerConfig.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>


namespace raypm {

// HTTP-based worker talking to the RayPowerMeter REST API.
class RayHttpCommWorker : public RayCommWorkerBase
{
    Q_OBJECT
public:
    explicit RayHttpCommWorker(const AppSettings &settings, QObject *parent = nullptr);

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
    void applyCouplerCalibration(int index,
                                 bool forwardPath,
                                 double vLow,
                                 double wLowTrue,
                                 double vHigh,
                                 double wHighTrue) override;
    void copyCouplerCalibration(int index, bool fromForward, bool toForward) override;
    void resetCouplerCalibration(int index, const QString &path, const QString &calSource) override;
    void applyCouplerCalibrationTable(int index,
                                      bool forwardPath,
                                      const QVector<RayCalPoint> &points) override;
    void requestCouplerConfig();

    // Aux Inputs
    void requestAuxConfig() override;
    void applyAuxConfig(const RayAuxConfig &cfg) override;
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
    void onPollTimeout();
    void onReplyFinished(QNetworkReply *reply);

signals:
    //void couplerConfigReceived(const RayCouplerConfig &config);
    void couplerConfigApplied(int index, bool ok, const QString &message);
    void deviceConfigApplied(bool ok, const QString &msg);

private:
    void requestStatus();
    bool handleStatusJson(const QJsonDocument &doc);
    void handleCouplerConfigJson(const QJsonDocument &doc);
    void handleDeviceConfigJson(const QJsonDocument &doc);
    void handleAuxConfigJson(const QJsonDocument &doc);

    QNetworkAccessManager m_nam;
    QTimer m_pollTimer;
    bool m_connected = false;
    bool m_couplerConfigRequested = false;
    bool m_auxConfigRequested = false;
};

} // namespace raypm
