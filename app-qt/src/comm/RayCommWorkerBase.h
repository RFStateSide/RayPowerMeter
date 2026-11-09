// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QObject>
#include "core/AppSettings.h"
#include "core/RayAuxConfig.h"
#include "core/RayCouplerConfig.h"
#include "core/RayTelemetry.h"

namespace raypm {

// Abstract base class for Serial/HTTP workers.
// Lives in its own QThread (or uses async in main thread for HTTP).
class RayCommWorkerBase : public QObject
{
    Q_OBJECT
public:
    explicit RayCommWorkerBase(const AppSettings &settings, QObject *parent = nullptr)
        : QObject(parent), m_settings(settings)
    {
    }

    virtual ~RayCommWorkerBase() override = default;

    virtual void requestDeviceConfig() = 0;
    virtual void applyDeviceConfig(int brightness, qint64 utcEpochSeconds) = 0;
    virtual void copyCouplerCalibration(int index, bool fromForward, bool toForward) = 0;

public slots:
    virtual void start() = 0;
    virtual void stop()  = 0;

    // Pause/resume background polling (status/telemetry) without tearing down transport
    virtual void setBackgroundPollingEnabled(bool enabled) = 0;

    // Coupler configuration (APIs)
    virtual void applyCouplerCalibration(
        int index, bool forwardPath, double vLow, double wLowTrue, double vHigh, double wHighTrue)
        = 0;
    virtual void resetCouplerCalibration(int index, const QString &path, const QString &calSource)
        = 0;
    // Multi-point calibration (up to MAX_CAL_POINTS).
    // Default implementation falls back to 2-point if exactly two points were provided.
    virtual void applyCouplerCalibrationTable(int index,
                                              bool forwardPath,
                                              const QVector<RayCalPoint> &points)
    {
        if (points.size() == 2) {
            applyCouplerCalibration(index,
                                    forwardPath,
                                    points[0].volts,
                                    points[0].watts,
                                    points[1].volts,
                                    points[1].watts);
            return;
        }

        // If a derived class doesn't override, at least fail loudly:
        emit errorOccurred(tr("applyCouplerCalibrationTable not implemented by this transport."));
    }

    // Modulation & Spectrum scope (APIs)
    virtual void requestModulationStart(int coupler,
                                        const QString &path, // "fwd" or "ref"
                                        int samples,
                                        int sampleRateHz)
        = 0;
    virtual void requestModulationStatus() = 0;
    virtual void requestModulationData(int maxSamples) = 0;

    // Aux Inputs configuration (APIs)
    virtual void requestAuxConfig() = 0;
    virtual void applyAuxConfig(const RayAuxConfig &cfg) = 0;
    virtual void applyAuxCalibration(
        int index, double vLow, double engLow, double vHigh, double engHigh)
        = 0;

    // Aux Digital I/O (APIs)
    virtual void setAuxDigitalValue(int index, bool value) = 0;

    // Extended Aux Digital config (name / pull / defaultState).
    // Default implementation falls back to the simpler (index, modeId) pure virtual.
    virtual void applyAuxDigitalConfig(
        int index, int modeId, int pullId, bool defaultState, const QString &name)
    {
        Q_UNUSED(pullId)
        Q_UNUSED(defaultState)
        Q_UNUSED(name)
        applyAuxDigitalConfig(index, modeId);
    }

    // Existing simple signature stays pure virtual for backwards compatibility.
    virtual void applyAuxDigitalConfig(int index, int modeId) = 0;

    // Optional “get” APIs – base implementation is a no-op.
    // Serial / HTTP workers override these.
    virtual void requestAuxDigitalConfig(int index) { Q_UNUSED(index); }
    virtual void requestAuxDigitalState(int index) { Q_UNUSED(index); }

    // OTA (device downloads firmware from a URL)
    virtual void startOtaFromUrl(const QString &url) = 0;
    virtual void requestOtaStatus() = 0;

signals:
    void connectedChanged(bool connected);
    void telemetryReceived(const RayTelemetry &telemetry);
    void errorOccurred(const QString &message);
    void couplerConfigReceived(const RayCouplerConfig &config);
    void couplerCalibrationApplied(int index, bool ok, const QString &message);
    void deviceConfigReceived(int brightness, qint64 utcEpochSeconds);
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
    // Aux configuration (GET /api/aux/config)
    void auxConfigReceived(const RayAuxConfig &config);

    // Aux config was applied (POST /api/aux/config)
    void auxConfigApplied(int index, bool ok, const QString &message);

    // Aux calibration applied (POST /api/aux/calibrate/2point)
    void auxCalibrationApplied(int index, bool ok, const QString &message);

    // Request to clear any latched protection / alarms
    void requestProtectionReset();

    // OTA
    void otaStartAck(bool ok, const QString &message);
    void otaStatusReceived(const QString &state,
                           int progressPercent,
                           const QString &message,
                           bool done,
                           bool ok,
                           const QString &rawJson);

protected:
    AppSettings m_settings;
};

} // namespace raypm
