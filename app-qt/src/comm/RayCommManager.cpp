// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "RayCommManager.h"
#include <QMetaType>

#include "RayCommWorkerBase.h"
#include "RayHttpCommWorker.h"
#include "RaySerialCommWorker.h"

namespace raypm {

RayCommManager::RayCommManager(QObject *parent)
    : QObject(parent)
{}

RayCommManager::~RayCommManager()
{
    stop();
}

void RayCommManager::setSettings(const AppSettings &settings)
{
    m_settings = settings;

    // If we already have a worker running, rebuild it with the new settings.
    if (m_worker) {
        recreateWorker();
    }
}

void RayCommManager::start()
{
    if (!m_worker) {
        recreateWorker();
    }
}

void RayCommManager::stop()
{
    if (!m_worker)
        return;

    // If we have a dedicated thread (Serial case), shut it down cleanly.
    if (m_thread) {
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    } else {
        // HTTP case: worker lives in the same thread as us.
        m_worker->stop();
        m_worker->deleteLater();
    }

    m_worker = nullptr;
}

void RayCommManager::setBackgroundPollingEnabled(bool enabled)
{
    if (!m_worker)
        return;

    // Worker might be in another thread (Serial). Use invokeMethod safely.
    QMetaObject::invokeMethod(m_worker,
                              "setBackgroundPollingEnabled",
                              Qt::QueuedConnection,
                              Q_ARG(bool, enabled));
}

void RayCommManager::requestCouplerConfig()
{
    if (!m_worker)
        return;

    // Call down into the worker on its own thread
    QMetaObject::invokeMethod(m_worker, "requestCouplerConfig", Qt::QueuedConnection);
}
void RayCommManager::applyCouplerConfig(const RayCouplerConfig &cfg)
{
    if (!m_worker) {
        emit errorOccurred(tr("No comm worker is active."));
        return;
    }

    if (auto *http = qobject_cast<RayHttpCommWorker *>(m_worker)) {
        http->applyCouplerConfig(cfg);
        return;
    }

    if (auto *ser = qobject_cast<RaySerialCommWorker *>(m_worker)) {
        // If you don't support it yet, keep this as a stub there.
        ser->applyCouplerConfig(cfg);
        return;
    }

    emit errorOccurred(tr("Unsupported comm worker type for applyCouplerConfig()."));
}

void RayCommManager::applyCouplerCalibrationTable(int index,
                                                  bool forwardPath,
                                                  const QVector<RayCalPoint> &points)
{
    if (!m_worker)
        return;

    QMetaObject::invokeMethod(m_worker,
                              "applyCouplerCalibrationTable",
                              Qt::QueuedConnection,
                              Q_ARG(int, index),
                              Q_ARG(bool, forwardPath),
                              Q_ARG(QVector<raypm::RayCalPoint>, points));
}

void RayCommManager::requestDeviceConfig()
{
    if (!m_worker) {
        emit errorOccurred(tr("No comm worker is active."));
        return;
    }

    QMetaObject::invokeMethod(
        m_worker, [w = m_worker]() { w->requestDeviceConfig(); }, Qt::QueuedConnection);
}

void RayCommManager::applyDeviceConfig(int brightness, qint64 utcEpochSeconds)
{
    if (!m_worker) {
        emit errorOccurred(tr("No comm worker is active."));
        return;
    }

    if (auto *http = qobject_cast<RayHttpCommWorker *>(m_worker)) {
        http->applyDeviceConfig(brightness, utcEpochSeconds);
        return;
    }

    if (auto *ser = qobject_cast<RaySerialCommWorker *>(m_worker)) {
        QMetaObject::invokeMethod(
            ser,
            [ser, brightness, utcEpochSeconds]() {
                ser->applyDeviceConfig(brightness, utcEpochSeconds);
            },
            Qt::QueuedConnection);
        return;
    }

    emit errorOccurred(tr("Unsupported comm worker type for applyDeviceConfig()."));
}

void RayCommManager::applyCouplerCalibration(
    int index, bool forwardPath, double vLow, double wLowTrue, double vHigh, double wHighTrue)
{
    if (!m_worker)
        return;

    QMetaObject::invokeMethod(m_worker,
                              "applyCouplerCalibration",
                              Qt::QueuedConnection,
                              Q_ARG(int, index),
                              Q_ARG(bool, forwardPath),
                              Q_ARG(double, vLow),
                              Q_ARG(double, wLowTrue),
                              Q_ARG(double, vHigh),
                              Q_ARG(double, wHighTrue));
}

void RayCommManager::resetCouplerCalibration(int index,
                                             const QString &path,
                                             const QString &calSource)
{
    if (!m_worker)
        return;

    QMetaObject::invokeMethod(m_worker,
                              "resetCouplerCalibration",
                              Qt::QueuedConnection,
                              Q_ARG(int, index),
                              Q_ARG(QString, path),
                              Q_ARG(QString, calSource));
}

void RayCommManager::copyCouplerCalibration(int index, bool fromForward, bool toForward)
{
    if (!m_worker)
        return;

    QMetaObject::invokeMethod(m_worker,
                              "copyCouplerCalibration",
                              Qt::QueuedConnection,
                              Q_ARG(int, index),
                              Q_ARG(bool, fromForward),
                              Q_ARG(bool, toForward));
}

void RayCommManager::recreateWorker()
{
    // Tear down any existing worker/thread.
    stop();

    //
    // Keep comm transport in sync with the global connection mode:
    //  0 = COMM (Serial)
    //  1 = HTTP (LAN)
    //  2 = HTTP (Access Point)
    //
    const int modeIndex = m_settings.http.connectionModeIndex;
    if (modeIndex == 0) {
        m_settings.transport = TransportType::Serial;
    } else {
        m_settings.transport = TransportType::Http;
    }

    // HTTP worker in the GUI thread (async via QNetworkAccessManager)
    if (m_settings.transport == TransportType::Http) {
        auto *worker = new RayHttpCommWorker(m_settings, this);

        // Bubble up worker signals.
        connect(worker,
                &RayCommWorkerBase::connectedChanged,
                this,
                &RayCommManager::connectedChanged);

        connect(worker, &RayCommWorkerBase::telemetryReceived, this, [this](const RayTelemetry &t) {
            if (!t.deviceAppName.isEmpty())
                m_deviceAppName = t.deviceAppName;
            if (!t.deviceAppVersion.isEmpty())
                m_deviceAppVersion = t.deviceAppVersion;

            emit telemetryReceived(t);
        });

        connect(worker, &RayCommWorkerBase::errorOccurred, this, &RayCommManager::errorOccurred);

        connect(worker,
                &RayCommWorkerBase::couplerConfigReceived,
                this,
                &RayCommManager::couplerConfigReceived);
        connect(worker,
                &RayCommWorkerBase::deviceConfigReceived,
                this,
                &RayCommManager::deviceConfigReceived);
        connect(worker,
                &RayCommWorkerBase::couplerCalibrationApplied,
                this,
                &RayCommManager::couplerCalibrationApplied);

        connect(worker,
                &RayCommWorkerBase::modulationStartAck,
                this,
                &RayCommManager::modulationStartAck);
        connect(worker,
                &RayCommWorkerBase::modulationStatusReceived,
                this,
                &RayCommManager::modulationStatusReceived);
        connect(worker,
                &RayCommWorkerBase::modulationDataReceived,
                this,
                &RayCommManager::modulationDataReceived);

        connect(worker,
                &RayCommWorkerBase::auxConfigReceived,
                this,
                &RayCommManager::auxConfigReceived);
        connect(worker,
                &RayCommWorkerBase::auxConfigApplied,
                this,
                &RayCommManager::auxConfigApplied);
        connect(worker,
                &RayCommWorkerBase::auxCalibrationApplied,
                this,
                &RayCommManager::auxCalibrationApplied);

        connect(worker, &RayCommWorkerBase::otaStartAck, this, &RayCommManager::otaStartAck);
        connect(worker,
                &RayCommWorkerBase::otaStatusReceived,
                this,
                &RayCommManager::otaStatusReceived);

        m_worker = worker;
        m_thread = nullptr;

        // Start polling immediately
        m_worker->start();
        return;
    }

    // SERIAL worker in its own QThread (QSerialPort + QTimer live in that thread)
    if (m_settings.transport == TransportType::Serial) {
        auto *worker = new RaySerialCommWorker(m_settings);
        auto *thread = new QThread(this);

        // Move worker and its internal QObjects into the worker thread
        worker->migrateToThread(thread);
        worker->moveToThread(thread);

        connect(thread, &QThread::started, worker, &RayCommWorkerBase::start);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);

        // Bubble up the same signals as HTTP
        connect(worker,
                &RayCommWorkerBase::connectedChanged,
                this,
                &RayCommManager::connectedChanged);

        connect(worker, &RayCommWorkerBase::telemetryReceived, this, [this](const RayTelemetry &t) {
            if (!t.deviceAppName.isEmpty())
                m_deviceAppName = t.deviceAppName;
            if (!t.deviceAppVersion.isEmpty())
                m_deviceAppVersion = t.deviceAppVersion;

            emit telemetryReceived(t);
        });

        connect(worker, &RayCommWorkerBase::errorOccurred, this, &RayCommManager::errorOccurred);

        connect(worker,
                &RayCommWorkerBase::couplerConfigReceived,
                this,
                &RayCommManager::couplerConfigReceived);
        connect(worker,
                &RayCommWorkerBase::deviceConfigReceived,
                this,
                &RayCommManager::deviceConfigReceived);
        connect(worker,
                &RayCommWorkerBase::couplerCalibrationApplied,
                this,
                &RayCommManager::couplerCalibrationApplied);

        connect(worker,
                &RayCommWorkerBase::modulationStartAck,
                this,
                &RayCommManager::modulationStartAck);
        connect(worker,
                &RayCommWorkerBase::modulationStatusReceived,
                this,
                &RayCommManager::modulationStatusReceived);
        connect(worker,
                &RayCommWorkerBase::modulationDataReceived,
                this,
                &RayCommManager::modulationDataReceived);

        connect(worker,
                &RayCommWorkerBase::auxConfigReceived,
                this,
                &RayCommManager::auxConfigReceived);
        connect(worker,
                &RayCommWorkerBase::auxConfigApplied,
                this,
                &RayCommManager::auxConfigApplied);
        connect(worker,
                &RayCommWorkerBase::auxCalibrationApplied,
                this,
                &RayCommManager::auxCalibrationApplied);

        connect(worker, &RayCommWorkerBase::otaStartAck, this, &RayCommManager::otaStartAck);
        connect(worker,
                &RayCommWorkerBase::otaStatusReceived,
                this,
                &RayCommManager::otaStatusReceived);

        m_worker = worker;
        m_thread = thread;

        thread->start();
        return;
    }

    // If transport is Disabled or unknown, do nothing.
}

void RayCommManager::requestModulationStart(int coupler,
                                            const QString &path,
                                            int samples,
                                            int sampleRateHz)
{
    if (!m_worker)
        return;

    // HTTP worker runs in the same thread; direct call is simplest
    if (auto *http = qobject_cast<RayHttpCommWorker *>(m_worker)) {
        http->requestModulationStart(coupler, path, samples, sampleRateHz);
        return;
    }

    // Serial worker lives in its own QThread — use a queued call.
    if (auto *ser = qobject_cast<RaySerialCommWorker *>(m_worker)) {
        QMetaObject::invokeMethod(
            ser,
            [ser, coupler, path, samples, sampleRateHz]() {
                ser->requestModulationStart(coupler, path, samples, sampleRateHz);
            },
            Qt::QueuedConnection);
        return;
    }

    emit errorOccurred(tr("Unsupported comm worker type for requestModulationStart()."));
}

void RayCommManager::requestModulationStatus()
{
    if (!m_worker)
        return;

    if (auto *http = qobject_cast<RayHttpCommWorker *>(m_worker)) {
        http->requestModulationStatus();
        return;
    }

    if (auto *ser = qobject_cast<RaySerialCommWorker *>(m_worker)) {
        QMetaObject::invokeMethod(
            ser, [ser]() { ser->requestModulationStatus(); }, Qt::QueuedConnection);
        return;
    }

    emit errorOccurred(tr("Unsupported comm worker type for requestModulationStatus()."));
}

void RayCommManager::requestModulationData(int maxSamples)
{
    if (!m_worker)
        return;

    if (auto *http = qobject_cast<RayHttpCommWorker *>(m_worker)) {
        http->requestModulationData(maxSamples);
        return;
    }

    if (auto *ser = qobject_cast<RaySerialCommWorker *>(m_worker)) {
        QMetaObject::invokeMethod(
            ser,
            [ser, maxSamples]() { ser->requestModulationData(maxSamples); },
            Qt::QueuedConnection);
        return;
    }

    emit errorOccurred(tr("Unsupported comm worker type for requestModulationData()."));
}

void RayCommManager::requestProtectionReset()
{
    if (!m_worker) {
        emit errorOccurred(tr("No comm worker is active."));
        return;
    }

    if (auto *http = qobject_cast<RayHttpCommWorker *>(m_worker)) {
        http->requestProtectionReset();
        return;
    }

    if (auto *ser = qobject_cast<RaySerialCommWorker *>(m_worker)) {
        QMetaObject::invokeMethod(
            ser, [ser]() { ser->requestProtectionReset(); }, Qt::QueuedConnection);
        return;
    }

    emit errorOccurred(tr("Unsupported comm worker type for requestProtectionReset()."));
}

void RayCommManager::requestAuxConfig()
{
    if (!m_worker)
        return;

    QMetaObject::invokeMethod(m_worker, "requestAuxConfig", Qt::QueuedConnection);
}

void RayCommManager::applyAuxConfig(const RayAuxConfig &cfg)
{
    if (!m_worker) {
        emit errorOccurred(tr("No comm worker is active."));
        return;
    }

    if (auto *http = qobject_cast<RayHttpCommWorker *>(m_worker)) {
        http->applyAuxConfig(cfg);
        return;
    }

    if (auto *ser = qobject_cast<RaySerialCommWorker *>(m_worker)) {
        QMetaObject::invokeMethod(
            ser, [ser, cfg]() { ser->applyAuxConfig(cfg); }, Qt::QueuedConnection);
        return;
    }

    emit errorOccurred(tr("Unsupported comm worker type for applyAuxConfig()."));
}

void RayCommManager::requestAuxDigitalConfig(int index)
{
    if (!m_worker)
        return;

    // index < 0 means "all" on the firmware side
    QMetaObject::invokeMethod(m_worker,
                              "requestAuxDigitalConfig",
                              Qt::QueuedConnection,
                              Q_ARG(int, index));
}

void RayCommManager::requestAuxDigitalState(int index)
{
    if (!m_worker)
        return;

    QMetaObject::invokeMethod(m_worker,
                              "requestAuxDigitalState",
                              Qt::QueuedConnection,
                              Q_ARG(int, index));
}

void RayCommManager::setAuxDigitalValue(int index, bool value)
{
    if (!m_worker) {
        emit errorOccurred(tr("No comm worker is active."));
        return;
    }

    // HTTP: direct call (same thread)
    if (auto *http = qobject_cast<RayHttpCommWorker *>(m_worker)) {
        http->setAuxDigitalValue(index, value);
        return;
    }

    // Serial: run on the worker's thread via queued lambda
    if (auto *ser = qobject_cast<RaySerialCommWorker *>(m_worker)) {
        QMetaObject::invokeMethod(
            ser,
            [ser, index, value]() { ser->setAuxDigitalValue(index, value); },
            Qt::QueuedConnection);
        return;
    }

    emit errorOccurred(tr("Unsupported comm worker type for Aux Digital I/O."));
}

// Backwards-compatible helper: call the full overload with defaults.
void RayCommManager::applyAuxDigitalConfig(int index, int modeId)
{
    if (!m_worker) {
        emit errorOccurred(tr("No comm worker is active."));
        return;
    }

    // Dispatch to the worker's thread via the virtual API so both HTTP and
    // Serial implementations are always hit.
    RayCommWorkerBase *worker = m_worker;
    QMetaObject::invokeMethod(
        worker,
        [worker, index, modeId]() { worker->applyAuxDigitalConfig(index, modeId); },
        Qt::QueuedConnection);

    qDebug() << "[CommManager] applyAuxDigitalConfig -> index=" << index << "modeId=" << modeId;
}

void RayCommManager::applyAuxDigitalConfig(
    int index, int modeId, int pullId, bool defaultState, const QString &name)
{
    if (!m_worker)
        return;

    if (auto *http = qobject_cast<RayHttpCommWorker *>(m_worker)) {
        QMetaObject::invokeMethod(
            http,
            [http, index, modeId, pullId, defaultState, name]() {
                http->applyAuxDigitalConfig(index, modeId, pullId, defaultState, name);
            },
            Qt::QueuedConnection);
    } else if (auto *serial = qobject_cast<RaySerialCommWorker *>(m_worker)) {
        QMetaObject::invokeMethod(
            serial,
            [serial, index, modeId, pullId, defaultState, name]() {
                serial->applyAuxDigitalConfig(index, modeId, pullId, defaultState, name);
            },
            Qt::QueuedConnection);
    }
}

void RayCommManager::applyAuxCalibration(
    int index, double vLow, double engLow, double vHigh, double engHigh)
{
    if (!m_worker) {
        emit errorOccurred(tr("No comm worker is active."));
        return;
    }

    // HTTP/Serial implementations will expose a slot named "applyAuxCalibration"
    QMetaObject::invokeMethod(m_worker,
                              "applyAuxCalibration",
                              Qt::QueuedConnection,
                              Q_ARG(int, index),
                              Q_ARG(double, vLow),
                              Q_ARG(double, engLow),
                              Q_ARG(double, vHigh),
                              Q_ARG(double, engHigh));
}

void RayCommManager::startOtaFromUrl(const QString &url)
{
    if (!m_worker) {
        emit errorOccurred(tr("No comm worker is active."));
        return;
    }

    RayCommWorkerBase *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, url]() { worker->startOtaFromUrl(url); });
}

void RayCommManager::requestOtaStatus()
{
    if (!m_worker) {
        emit errorOccurred(tr("No comm worker is active."));
        return;
    }

    RayCommWorkerBase *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker]() { worker->requestOtaStatus(); });
}

} // namespace raypm
