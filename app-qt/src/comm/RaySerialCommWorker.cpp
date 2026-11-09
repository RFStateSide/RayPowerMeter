// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "common/Constants.h"
#include "RaySerialCommWorker.h"
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

namespace {

static raypm::RayCalTable parseCalTable(const QJsonObject &pathObj)
{
    raypm::RayCalTable t;
    t.mode = pathObj.value(QStringLiteral("mode")).toString();
    t.count = pathObj.value(QStringLiteral("count")).toInt(0);

    if (pathObj.contains(QStringLiteral("m")))
        t.m = pathObj.value(QStringLiteral("m")).toDouble(1.0);
    if (pathObj.contains(QStringLiteral("b")))
        t.b = pathObj.value(QStringLiteral("b")).toDouble(0.0);

    const QJsonArray pts = pathObj.value(QStringLiteral("points")).toArray();
    t.points.reserve(pts.size());
    for (const auto &it : pts) {
        const QJsonObject p = it.toObject();
        raypm::RayCalPoint cp;
        cp.volts = p.value(QStringLiteral("v")).toDouble(0.0);
        cp.watts = p.value(QStringLiteral("w")).toDouble(0.0);
        t.points.push_back(cp);
    }

    if (t.count <= 0 && !t.points.isEmpty())
        t.count = t.points.size();

    return t;
}

static void parseCouplerCalSection(const QJsonObject &couplerObj, raypm::RayCouplerConfig &cfg)
{
    const QJsonObject cal = couplerObj.value(QStringLiteral("cal")).toObject();
    if (cal.isEmpty())
        return;

    const QJsonObject fwd = cal.value(QStringLiteral("fwd")).toObject();
    if (!fwd.isEmpty())
        cfg.fwdCal = parseCalTable(fwd);

    const QJsonObject ref = cal.value(QStringLiteral("ref")).toObject();
    if (!ref.isEmpty())
        cfg.refCal = parseCalTable(ref);
}

} // namespace

namespace raypm
{

    RaySerialCommWorker::RaySerialCommWorker(const AppSettings &settings, QObject *parent)
        : RayCommWorkerBase(settings, parent)
    {
        connect(&m_serial, &QSerialPort::readyRead,
                this, &RaySerialCommWorker::onReadyRead);

        m_pollTimer.setInterval(100); // 10 Hz default, adjust later
        connect(&m_pollTimer, &QTimer::timeout,
                this, &RaySerialCommWorker::onPollTimeout);
    }

    // New: move internal QObjects into the worker thread
    void RaySerialCommWorker::migrateToThread(QThread *thread)
    {
        m_serial.moveToThread(thread);
        m_pollTimer.moveToThread(thread);
    }

    void RaySerialCommWorker::start()
    {
        openPort();
        if (m_serial.isOpen())
        {
            m_pollTimer.start();
        }
    }

    void RaySerialCommWorker::stop()
    {
        m_pollTimer.stop();
        closePort();
    }

    void RaySerialCommWorker::setBackgroundPollingEnabled(bool enabled)
    {
        if (enabled) {
            if (m_serial.isOpen() && !m_pollTimer.isActive())
                m_pollTimer.start();
        } else {
            m_pollTimer.stop();
        }
    }

    void RaySerialCommWorker::applyCouplerCalibration(int couplerIndex,
                                                      bool isForward,
                                                      double adcVoltsLow,
                                                      double wattsTrueLow,
                                                      double adcVoltsHigh,
                                                      double wattsTrueHigh)
    {
        QJsonObject low;
        low["adc_v"] = adcVoltsLow;
        low["w_true"] = wattsTrueLow;

        QJsonObject high;
        high["adc_v"] = adcVoltsHigh;
        high["w_true"] = wattsTrueHigh;

        QJsonObject obj;
        obj["cmd"] = "calibrate_coupler";
        obj["index"] = couplerIndex;
        obj["path"] = isForward ? "fwd" : "ref";
        obj["low"] = low;
        obj["high"] = high;

        qDebug() << "[SerialComm] Sending calibrate_coupler:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::applyCouplerCalibrationTable(int couplerIndex,
                                                           bool isForward,
                                                           const QVector<RayCalPoint> &points)
    {
        if (points.size() < 2 || points.size() > MAX_CAL_POINTS) {
            emit errorOccurred(tr("Calibration requires 2..%1 points (got %2).").arg(MAX_CAL_POINTS).arg(points.size()));
            return;
        }

        QJsonObject obj;
        obj["cmd"] = "calibrate_coupler";
        obj["index"] = couplerIndex;
        obj["path"] = isForward ? "fwd" : "ref";

        QJsonArray pts;
        for (const auto &p : points) {
            QJsonObject jp;
            jp["v"] = p.volts;
            jp["w"] = p.watts;
            pts.append(jp);
        }

        obj["points"] = pts;
        obj["count"] = points.size();
        obj["mode"] = "table";

        qDebug() << "[SerialComm] Sending calibrate_coupler (table):"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);

        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::copyCouplerCalibration(int couplerIndex,
                                                     bool fromForward,
                                                     bool toForward)
    {
        QJsonObject obj;
        obj["cmd"] = "copy_coupler_calibration";
        obj["index"] = couplerIndex;
        obj["from"] = fromForward ? "fwd" : "ref";
        obj["to"] = toForward ? "fwd" : "ref";
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::resetCouplerCalibration(int index,
                                                      const QString &path,
                                                      const QString &calSource)
    {
        if (!m_serial.isOpen())
        {
            emit errorOccurred(tr("Serial port not open."));
            return;
        }

        QJsonObject obj;
        obj["cmd"] = QStringLiteral("reset_calibration");
        obj["index"] = index;
        if (!path.isEmpty())
            obj["path"] = path; // "both"
        if (!calSource.isEmpty())
            obj["cal_source"] = calSource; // "FactoryLinear" or "FactoryTable"

        const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
        m_serial.write(line);

        qDebug() << "[SerialComm] Sending reset_calibration:" << line;
    }

    void RaySerialCommWorker::openPort()
    {
        if (m_serial.isOpen())
            return;

        m_serial.setPortName(m_settings.serial.portName);
        m_serial.setBaudRate(m_settings.serial.baudRate);

        if (!m_serial.open(QIODevice::ReadWrite))
        {
            emit errorOccurred(tr("Failed to open serial port %1").arg(m_serial.portName()));
            emit connectedChanged(false);
            return;
        }

        m_connected = true;
        m_couplerConfigRequested = false;
        m_auxConfigRequested = false;
        emit connectedChanged(true);
    }

    void RaySerialCommWorker::closePort()
    {
        if (m_serial.isOpen())
            m_serial.close();

        if (m_connected)
        {
            m_connected = false;
            emit connectedChanged(false);
        }

        m_couplerConfigRequested = false;
        m_auxConfigRequested = false;
    }

    void RaySerialCommWorker::onReadyRead()
    {
        if (!m_serial.isOpen())
            return;

        m_rxBuffer.append(m_serial.readAll());

        // Process all complete lines (terminated by CR or LF)
        for (;;)
        {
            int nlPos = m_rxBuffer.indexOf('\n');
            int crPos = m_rxBuffer.indexOf('\r');

            int eolPos = -1;
            if (nlPos >= 0 && crPos >= 0)
                eolPos = qMin(nlPos, crPos);
            else if (nlPos >= 0)
                eolPos = nlPos;
            else if (crPos >= 0)
                eolPos = crPos;

            if (eolPos < 0)
                break; // no full line yet

            QByteArray line = m_rxBuffer.left(eolPos);
            m_rxBuffer.remove(0, eolPos + 1);

            line = line.trimmed();
            if (line.isEmpty())
                continue;

            // Phase-1 tolerant client:
            // - Ignore human/debug lines (don’t start with '{')
            // - Ignore echoed request frames that don’t have "type" (handled in handleJsonLine)
            if (!line.startsWith('{'))
                continue;

            handleJsonLine(line);
        }

        // Guard against runaway buffer growth.
        // Spectrum / modulation frames can be fairly large JSON blobs,
        // so allow a generous buffer before we panic.
        constexpr int kMaxRxBufferBytes = 128 * 1024; // 128 KB

        if (m_rxBuffer.size() > kMaxRxBufferBytes)
        {
            qWarning() << "[SerialComm] RX buffer overflow, clearing";
            m_rxBuffer.clear();
        }
    }

    bool RaySerialCommWorker::sendJsonCommand(const QJsonObject &obj)
    {
        if (!m_serial.isOpen())
        {
            emit errorOccurred(tr("Serial port is not open."));
            return false;
        }

        QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        line.append('\n');

        const qint64 written = m_serial.write(line);
        if (written == -1)
        {
            emit errorOccurred(tr("Serial write failed: %1").arg(m_serial.errorString()));
            return false;
        }
        return true;
    }

    void RaySerialCommWorker::requestStatus()
    {
        QJsonObject obj;
        obj["cmd"] = "get_status";
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::onPollTimeout()
    {
        if (!m_serial.isOpen())
            return;

        requestStatus();
    }

    void RaySerialCommWorker::handleJsonLine(const QByteArray &line)
    {
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);

        // Phase-1 tolerant client: ignore malformed JSON (don’t hard-error the UI)
        if (perr.error != QJsonParseError::NoError || !doc.isObject())
            return;

        const QJsonObject root = doc.object();
        const QString typeStr = root.value(QStringLiteral("type")).toString();

        // Ignore echoed request frames or other JSON that doesn’t declare a type
        if (typeStr.isEmpty())
            return;

        // Any valid typed frame means the link is alive
        if (!m_connected)
        {
            m_connected = true;
            emit connectedChanged(true);
        }

        // 1) Firmware error frames
        if (typeStr == QLatin1String("error"))
        {
            const QString msg = root.value(QStringLiteral("message")).toString();
            const QString code = root.value(QStringLiteral("code")).toString();
            emit errorOccurred(tr("COMM error (%1): %2").arg(code, msg));

            // If a copy is pending, cancel it
            return;
        }

        // 2) Status / Telemetry => RayTelemetry snapshots
        if (typeStr == QLatin1String("status") || typeStr == QLatin1String("telemetry"))
        {
            RayTelemetry t;
            const bool ok = (typeStr == QLatin1String("status")) ? parseStatusJson(root, t)
                                                                 : parseTelemetryJson(root, t);

            if (ok)
            {
                emit telemetryReceived(t);

                // For Serial, mirror HTTP behaviour: after the first *status* frame
                // that parses OK, pull down static config exactly once.
                if (typeStr == QLatin1String("status"))
                {
                    if (!m_couplerConfigRequested)
                    {
                        m_couplerConfigRequested = true;
                        qDebug() << "[COMM] First status OK -> requesting coupler_config";
                        requestCouplerConfig();
                    }
                    if (!m_auxConfigRequested)
                    {
                        m_auxConfigRequested = true;
                        qDebug() << "[COMM] First status OK -> requesting aux_config";
                        requestAuxConfig();
                    }
                }
            }

            return;
        }

        // 3) Coupler config (get/set)
        if (typeStr == QLatin1String("coupler_config"))
        {
            const QJsonArray couplers = root.value(QStringLiteral("couplers")).toArray();

            int i = 0;
            for (const auto &v : couplers)
            {
                const QJsonObject c = v.toObject();
                if (c.isEmpty())
                {
                    ++i;
                    continue;
                }

                RayCouplerConfig cfg;
                cfg.index = c.value("index").toInt(i); // default to array position if missing
                cfg.name = c.value("name").toString();
                cfg.fullScaleW = c.value("full_scale_w").toDouble(0.0);
                cfg.fwdBarRangeW = c.value("fwd_bar_range_w").toDouble(0.0);
                cfg.refBarRangeW = c.value("ref_bar_range_w").toDouble(0.0);
                cfg.highFwdTripW = c.value("high_fwd_trip_w").toDouble(0.0);
                cfg.highRefTripW = c.value("high_ref_trip_w").toDouble(0.0);
                cfg.highSWRTrip = c.value("high_swr_trip").toDouble(0.0);
                cfg.tripMask = static_cast<quint8>(c.value("trip_mask").toInt(0));
                cfg.typeId = static_cast<quint8>(c.value("type_id").toInt(0));
                cfg.typeName = c.value("type_name").toString();

                parseCouplerCalSection(c, cfg);

                emit couplerConfigReceived(cfg);
                ++i;
            }
            return;
        }

        // 3a) Aux config (get/set)
        if (typeStr == QLatin1String("aux_config"))
        {
            const QJsonArray auxArr = root.value(QStringLiteral("aux")).toArray();

            if (auxArr.isEmpty())
            {
                emit errorOccurred(tr("Aux config JSON: aux[] missing/empty"));
                return;
            }

            for (int i = 0; i < auxArr.size(); ++i)
            {
                const QJsonObject a = auxArr.at(i).toObject();
                if (a.isEmpty())
                    continue;

                RayAuxConfig cfg;
                cfg.index = a.value(QStringLiteral("index")).toInt(i);
                cfg.name = a.value(QStringLiteral("name")).toString();
                cfg.typeName = a.value(QStringLiteral("type")).toString();
                cfg.tempModel = a.value(QStringLiteral("tempModel")).toString();

                cfg.voltageMaxRange = a.value(QStringLiteral("voltageMaxRange")).toDouble(0.0);
                cfg.currentMaxRange = a.value(QStringLiteral("currentMaxRange")).toDouble(0.0);
                cfg.temperatureMaxRange = a.value(QStringLiteral("temperatureMaxRange")).toDouble(0.0);

                cfg.thermSeriesOhms = a.value(QStringLiteral("thermSeriesOhms")).toDouble(0.0);
                cfg.thermR0Ohms = a.value(QStringLiteral("thermR0Ohms")).toDouble(0.0);
                cfg.thermBeta = a.value(QStringLiteral("thermBeta")).toDouble(0.0);
                cfg.thermT0C = a.value(QStringLiteral("thermT0C")).toDouble(0.0);

                cfg.calM = a.value(QStringLiteral("cal_m")).toDouble(1.0);
                cfg.calB = a.value(QStringLiteral("cal_b")).toDouble(0.0);

                qDebug() << "[COMM] Aux config"
                         << "idx=" << cfg.index << "type=" << cfg.typeName
                         << "vMax=" << cfg.voltageMaxRange << "iMax=" << cfg.currentMaxRange
                         << "tMax=" << cfg.temperatureMaxRange << "beta=" << cfg.thermBeta
                         << "cal_m=" << cfg.calM << "cal_b=" << cfg.calB;

                emit auxConfigReceived(cfg);
            }
            return;
        }

        // 3b) Device config (get/set)
        if (typeStr == QLatin1String("device_config"))
        {
            const int brightness = root.value(QStringLiteral("brightness")).toInt(-1);
            const qint64 unixTime = static_cast<qint64>(
                root.value(QStringLiteral("unix_time")).toDouble(0.0));

            if (brightness < 0)
            {
                emit errorOccurred(tr("Device config JSON missing brightness"));
                return;
            }

            qDebug() << "[COMM] Device config received: brightness=" << brightness
                     << "unix_time=" << unixTime;

            emit deviceConfigReceived(brightness, unixTime);
            return;
        }

        // 4) Calibration reset ack
        if (typeStr == QLatin1String("reset_calibration"))
        {
            const QString status = root.value(QStringLiteral("status")).toString();
            const bool ok2 = (status == QLatin1String("ok"));
            const int idx = root.value(QStringLiteral("index")).toInt(-1);

            emit couplerCalibrationApplied(idx,
                                           ok2,
                                           ok2 ? tr("Calibration reset to factory defaults.")
                                               : tr("Calibration reset failed."));
            return;
        }

        // 5) Calibrate-coupler ack
        if (typeStr == QLatin1String("calibrate_coupler"))
        {
            const QString status = root.value(QStringLiteral("status")).toString();
            const bool ok2 = (status == QLatin1String("ok"));
            const int idx = root.value(QStringLiteral("index")).toInt(-1);
            const QString msg = root.value(QStringLiteral("message")).toString();

            emit couplerCalibrationApplied(idx,
                                           ok2,
                                           ok2 ? (msg.isEmpty() ? tr("Calibration updated.") : msg)
                                               : (msg.isEmpty() ? tr("Calibration update failed.")
                                                                : msg));
            return;
        }

        if (typeStr == QLatin1String("ok") && root.contains("from") && root.contains("to"))
        {
            const int index = root.value("index").toInt(-1);
            emit couplerCalibrationApplied(index, true, tr("Calibration copied successfully."));
            return;
        }

        // 7) Modulation capture frames
        if (typeStr == QLatin1String("modulation_start"))
        {
            const QString status = root.value(QStringLiteral("status")).toString();
            const bool ok2 = (status == QLatin1String("ok"));
            const QString msg = root.value(QStringLiteral("message")).toString();
            const int coupler = root.value(QStringLiteral("coupler")).toInt(0);
            const QString path = root.value(QStringLiteral("path")).toString();
            const bool isFwd = (path == QLatin1String("fwd"));
            const int samples = root.value(QStringLiteral("samples")).toInt(0);
            const int rate = root.value(QStringLiteral("sample_rate_hz")).toInt(0);

            emit modulationStartAck(ok2, msg, coupler, path, samples, rate);
            return;
        }

        if (typeStr == QLatin1String("modulation_status"))
        {
            const QString state = root.value(QStringLiteral("status")).toString();
            const int coupler = root.value(QStringLiteral("coupler")).toInt(0);
            const QString path = root.value(QStringLiteral("path")).toString();
            const bool isFwd = (path == QLatin1String("fwd"));
            const int samples = root.value(QStringLiteral("samples")).toInt(0);
            const int rate = root.value(QStringLiteral("sample_rate_hz")).toInt(0);

            emit modulationStatusReceived(state, coupler, path, samples, rate);
            return;
        }

        if (typeStr == QLatin1String("modulation_ready"))
        {
            const int coupler = root.value(QStringLiteral("coupler")).toInt(0);
            const QString path = root.value(QStringLiteral("path")).toString();
            const bool isFwd = (path == QLatin1String("fwd"));
            const int samples = root.value(QStringLiteral("samples")).toInt(0);
            const int rate = root.value(QStringLiteral("sample_rate_hz")).toInt(0);

            emit modulationStatusReceived(QStringLiteral("ready"), coupler, path, samples, rate);
            return;
        }

        if (typeStr == QLatin1String("modulation_data"))
        {
            const int coupler = root.value(QStringLiteral("coupler")).toInt(0);
            const QString path = root.value(QStringLiteral("path")).toString();
            const bool isFwd = (path == QLatin1String("fwd"));
            const int rate = root.value(QStringLiteral("sample_rate_hz")).toInt(0);

            const QJsonArray arr = root.value(QStringLiteral("samples")).toArray();
            QVector<int16_t> samples;
            samples.reserve(arr.size());
            for (const auto &v : arr)
                samples.push_back(static_cast<int16_t>(v.toInt(0)));

            emit modulationDataReceived(coupler, path, rate, samples);
            return;
        }

        // 8) OTA frames (typed)
        if (typeStr == QLatin1String("ota_start")) {
            // Expected examples:
            // {"type":"ota_start","ok":true,"message":"..."}
            // {"type":"ota_start","ok":false,"error":"..."}
            const bool ok = root.value(QStringLiteral("ok")).toBool(false);
            const QString msg = root.value(QStringLiteral("message"))
                                    .toString(
                                        root.value(QStringLiteral("error"))
                                            .toString(ok ? QString() : tr("OTA start failed.")));

            emit otaStartAck(ok, msg);
            return;
        }

        if (typeStr == QLatin1String("ota_status")) {
            // Expected examples:
            // {"type":"ota_status","ok":true,"ota":{...}}
            // We parse tolerantly since ota schema may evolve.
            const QJsonObject ota = root.value(QStringLiteral("ota")).toObject();

            const QString state = ota.value(QStringLiteral("state"))
                                      .toString(
                                          ota.value(QStringLiteral("status"))
                                              .toString(ota.value(QStringLiteral("phase"))
                                                            .toString(QStringLiteral("unknown"))));

            int progress = 0;
            if (ota.value(QStringLiteral("progress")).isDouble())
                progress = ota.value(QStringLiteral("progress")).toInt();
            else if (ota.value(QStringLiteral("progress_pct")).isDouble())
                progress = ota.value(QStringLiteral("progress_pct")).toInt();
            else if (ota.value(QStringLiteral("percent")).isDouble())
                progress = ota.value(QStringLiteral("percent")).toInt();

            const QString message = ota.value(QStringLiteral("message"))
                                        .toString(
                                            ota.value(QStringLiteral("msg"))
                                                .toString(
                                                    ota.value(QStringLiteral("error")).toString()));

            const bool ok = ota.value(QStringLiteral("ok"))
                                .toBool(root.value(QStringLiteral("ok")).toBool(true));

            const bool done = ota.value(QStringLiteral("done")).toBool(false)
                              || state.contains(QStringLiteral("done"), Qt::CaseInsensitive)
                              || state.contains(QStringLiteral("success"), Qt::CaseInsensitive)
                              || state.contains(QStringLiteral("fail"), Qt::CaseInsensitive)
                              || state.contains(QStringLiteral("error"), Qt::CaseInsensitive);

            const QString rawJson = QString::fromUtf8(
                QJsonDocument(root).toJson(QJsonDocument::Compact));

            emit otaStatusReceived(state, progress, message, done, ok, rawJson);
            return;
        }

        // Unknown typed frame: ignore
    }

    bool RaySerialCommWorker::parseStatusJson(const QJsonObject &root, RayTelemetry &out)
    {
        const QJsonObject rfObj = root.value(QStringLiteral("rf")).toObject();
        if (rfObj.isEmpty())
            return false;

        // --- Device app/firmware identity (root.app) -----------------------------
        const QJsonObject appObj = root.value(QStringLiteral("app")).toObject();
        if (!appObj.isEmpty()) {
            out.deviceAppName = appObj.value(QStringLiteral("name")).toString();
            out.deviceAppVersion = appObj.value(QStringLiteral("version")).toString();
        }
        // --- Timestamp -----------------------------------------------------------
        out.timestamp = QDateTime::currentDateTimeUtc();

        // RF frequency
        out.rfFrequencyHz = rfObj.value(QStringLiteral("freq_hz")).toDouble(0.0);
        out.rfFrequencyValid = rfObj.value(QStringLiteral("freq_valid")).toBool(false);

        out.rfMode = static_cast<quint8>(rfObj.value(QStringLiteral("mode")).toInt(0));
        out.protectionOn = rfObj.value(QStringLiteral("protection_on")).toBool(true);

        // Trip info
        out.trip.tripped = rfObj.value(QStringLiteral("tripped")).toBool(false);
        if (out.trip.tripped)
        {
            const QJsonObject tripObj = rfObj.value(QStringLiteral("trip")).toObject();
            out.trip.type = static_cast<quint8>(tripObj.value(QStringLiteral("type")).toInt(0));
            out.trip.source = static_cast<quint8>(tripObj.value(QStringLiteral("source")).toInt(0));
            out.trip.threshold = tripObj.value(QStringLiteral("threshold")).toDouble(0.0);
            out.trip.value = tripObj.value(QStringLiteral("value")).toDouble(0.0);
        }

        // Couplers (status has name + peaks)
        const QJsonArray couplersArr = rfObj.value(QStringLiteral("couplers")).toArray();
        if (couplersArr.isEmpty())
            return false;

        constexpr int MaxCouplers = 4; // adjust if you ever support more
        const int couplerCount = qMin(couplersArr.size(), MaxCouplers);
        out.couplers.reserve(couplerCount);

        for (int i = 0; i < couplerCount; ++i)
        {
            const QJsonObject c = couplersArr.at(i).toObject();

            RayCouplerSample cs;
            cs.index = c.value(QStringLiteral("index")).toInt(i);
            cs.name = c.value(QStringLiteral("name")).toString();
            cs.forwardWatts = c.value(QStringLiteral("fwd_w")).toDouble(0.0);
            cs.reflectedWatts = c.value(QStringLiteral("ref_w")).toDouble(0.0);
            cs.swr = c.value(QStringLiteral("swr")).toDouble(1.0);
            cs.peakForwardWatts = c.value(QStringLiteral("peak_f")).toDouble(0.0);
            cs.peakReflectedWatts = c.value(QStringLiteral("peak_r")).toDouble(0.0);

            out.couplers.push_back(cs);
        }

        // Mirror first coupler into legacy fields for any old code still using them
        if (!out.couplers.isEmpty())
        {
            const auto &c0 = out.couplers.first();
            out.couplerIndex = c0.index;
            out.forwardWatts = c0.forwardWatts;
            out.reflectedWatts = c0.reflectedWatts;
            out.swrLegacy = c0.swr;
            out.rawFwd = c0.peakForwardWatts;
            out.rawRef = c0.peakReflectedWatts;
        }

        // AUX channels
        const QJsonArray auxArr = rfObj.value(QStringLiteral("aux")).toArray();
        for (int i = 0; i < auxArr.size(); ++i)
        {
            const QJsonObject a = auxArr.at(i).toObject();

            RayAuxSample ax;
            ax.index = a.value(QStringLiteral("index")).toInt(i);
            ax.typeName = a.value(QStringLiteral("type")).toString();
            ax.rawVolts = a.value(QStringLiteral("raw_volts"))
                              .toDouble(
                                  a.value(QStringLiteral("raw_v")).toDouble(0.0)); // tolerate either key
            ax.value = a.value(QStringLiteral("value")).toDouble(0.0);

            if (ax.typeName == QLatin1String("voltage"))
                ax.type = RayAuxType::Voltage;
            else if (ax.typeName == QLatin1String("current"))
                ax.type = RayAuxType::Current;
            else if (ax.typeName == QLatin1String("temperature"))
                ax.type = RayAuxType::Temperature;
            else
                ax.type = RayAuxType::None;

            out.auxChannels.push_back(ax);
        }

        if (!out.auxChannels.isEmpty())
        {
            const auto &a0 = out.auxChannels.first();
            out.auxIndex = a0.index;
            if (a0.type == RayAuxType::Voltage)
                out.voltage = a0.value;
            else if (a0.type == RayAuxType::Current)
                out.current = a0.value;
            else if (a0.type == RayAuxType::Temperature)
                out.temperatureC = a0.value;
        }

        // Aux digital I/O status
        QVector<RayAuxDigitalStatus> auxDigitalChannels;
        const QJsonArray auxDigArr = rfObj.value(QStringLiteral("aux_digital")).toArray();

        for (int i = 0; i < auxDigArr.size(); ++i)
        {
            const QJsonObject d = auxDigArr.at(i).toObject();

            RayAuxDigitalStatus ds;
            ds.index = d.value(QStringLiteral("index")).toInt(i);
            ds.name = d.value(QStringLiteral("name")).toString();
            ds.modeName = d.value(QStringLiteral("mode")).toString();
            ds.value = d.value(QStringLiteral("value")).toBool(false);

            const int modeId = d.value(QStringLiteral("mode_id")).toInt(0);
            switch (modeId)
            {
            case 1:
                ds.mode = RayAuxDigitalMode::Input;
                break;
            case 2:
                ds.mode = RayAuxDigitalMode::Output;
                break;
            default:
                ds.mode = RayAuxDigitalMode::Disabled;
                break;
            }

            auxDigitalChannels.push_back(ds);
        }

        out.auxDigitalChannels = auxDigitalChannels;

        // Note: no net{} on COMM yet; RayNetStatus stays default-constructed.
        return true;
    }

    bool RaySerialCommWorker::parseTelemetryJson(const QJsonObject &root, RayTelemetry &out)
    {
        out.tsMs = static_cast<quint32>(root.value(QStringLiteral("ts_ms")).toInt(0));

        const QJsonArray couplersArr = root.value(QStringLiteral("couplers")).toArray();
        const QJsonArray auxArr = root.value(QStringLiteral("aux")).toArray();

        if (couplersArr.isEmpty() && auxArr.isEmpty())
            return false; // nothing to report

        constexpr int MaxCouplers = 4;
        const int couplerCount = qMin(couplersArr.size(), MaxCouplers);
        out.couplers.reserve(couplerCount);

        // Couplers (no peaks or names in telemetry stream)
        for (int i = 0; i < couplerCount; ++i)
        {
            const QJsonObject c = couplersArr.at(i).toObject();

            RayCouplerSample cs;
            cs.index = c.value(QStringLiteral("index")).toInt(i);
            cs.forwardWatts = c.value(QStringLiteral("fwd_w")).toDouble(0.0);
            cs.reflectedWatts = c.value(QStringLiteral("ref_w")).toDouble(0.0);
            cs.swr = c.value(QStringLiteral("swr")).toDouble(1.0);

            out.couplers.push_back(cs);
        }

        if (!out.couplers.isEmpty())
        {
            const auto &c0 = out.couplers.first();
            out.couplerIndex = c0.index;
            out.forwardWatts = c0.forwardWatts;
            out.reflectedWatts = c0.reflectedWatts;
            out.swrLegacy = c0.swr;
        }

        // AUX
        for (int i = 0; i < auxArr.size(); ++i)
        {
            const QJsonObject a = auxArr.at(i).toObject();

            RayAuxSample ax;
            ax.index = a.value(QStringLiteral("index")).toInt(i);
            ax.typeName = a.value(QStringLiteral("type")).toString();
            ax.rawVolts = a.value(QStringLiteral("raw_volts"))
                              .toDouble(a.value(QStringLiteral("raw_v")).toDouble(0.0));
            ax.value = a.value(QStringLiteral("value")).toDouble(0.0);

            if (ax.typeName == QLatin1String("voltage"))
                ax.type = RayAuxType::Voltage;
            else if (ax.typeName == QLatin1String("current"))
                ax.type = RayAuxType::Current;
            else if (ax.typeName == QLatin1String("temperature"))
                ax.type = RayAuxType::Temperature;
            else
                ax.type = RayAuxType::None;

            out.auxChannels.push_back(ax);
        }

        if (!out.auxChannels.isEmpty())
        {
            const auto &a0 = out.auxChannels.first();
            out.auxIndex = a0.index;
            if (a0.type == RayAuxType::Voltage)
                out.voltage = a0.value;
            else if (a0.type == RayAuxType::Current)
                out.current = a0.value;
            else if (a0.type == RayAuxType::Temperature)
                out.temperatureC = a0.value;
        }

        return true;
    }

    void RaySerialCommWorker::requestCouplerConfig()
    {
        QJsonObject obj;
        obj["cmd"] = "get_coupler_config";

        qDebug() << "[SerialComm] Sending get_coupler_config:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::applyCouplerConfig(const RayCouplerConfig &cfg)
    {
        QJsonObject c;
        c["index"] = cfg.index;
        c["type_id"] = static_cast<int>(cfg.typeId);
        c["name"] = cfg.name;

        // Coupler UI/config fields (COMM parity with HTTP)
        c["full_scale_w"] = cfg.fullScaleW;
        c["fwd_bar_range_w"] = cfg.fwdBarRangeW;
        c["ref_bar_range_w"] = cfg.refBarRangeW;

        c["high_fwd_trip_w"] = cfg.highFwdTripW;
        c["high_ref_trip_w"] = cfg.highRefTripW;
        c["high_swr_trip"] = cfg.highSWRTrip;

        c["trip_mask"] = static_cast<int>(cfg.tripMask);

        // Optional: firmware may ignore this, but safe if present in schema
        // c["type_name"] = cfg.typeName;

        QJsonArray couplers;
        couplers.append(c);

        QJsonObject obj;
        obj["cmd"] = "set_coupler_config";
        obj["couplers"] = couplers;

        qDebug() << "[SerialComm] Sending set_coupler_config:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::requestDeviceConfig()
    {
        // Mirror HTTP GET /api/device/config as a simple serial JSON command.
        // Firmware is expected to reply with a frame like:
        // { "type": "device_config", "status": "ok", "brightness": 75, "unix_time": 1734839999 }
        QJsonObject obj;
        obj["cmd"] = QStringLiteral("get_device_config");

        qDebug() << "[SerialComm] Sending get_device_config:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::applyDeviceConfig(int brightness, const qint64 utcEpochSeconds)
    {
        // Mirror HTTP POST /api/device/config over serial.
        // This assumes the firmware accepts:
        // { "cmd": "set_device_config", "brightness": N, "unix_time": epoch }
        QJsonObject obj;
        obj["cmd"] = QStringLiteral("set_device_config");
        obj["brightness"] = brightness;

        // Only send time if we have something sane (> 0)
        if (utcEpochSeconds > 0)
            obj["unix_time"] = utcEpochSeconds;

        qDebug() << "[SerialComm] Sending set_device_config:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::requestAuxConfig()
    {
        // Mirror HTTP GET /api/aux/config:
        // Expect firmware to reply with:
        // {
        //   "type": "aux_config",
        //   "status": "ok",
        //   "aux": [ { ... }, ... ]
        // }
        QJsonObject obj;
        obj["cmd"] = QStringLiteral("get_aux_config");

        qDebug() << "[SerialComm] Sending get_aux_config:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::applyAuxConfig(const RayAuxConfig &cfg)
    {
        // Serial version of HTTP POST /api/aux/config.
        // Match the JSON shape documented in RayAuxConfig.h:
        //
        // {
        //   "cmd": "set_aux_config",
        //   "aux": [
        //     {
        //       "index": 0,
        //       "name": "Aux 1",
        //       "type": "voltage",
        //       "tempModel": "beta",
        //       "voltageMaxRange": 60.0,
        //       "currentMaxRange": 25.0,
        //       "temperatureMaxRange": 150.0,
        //       "thermSeriesOhms": 10000.0,
        //       "thermR0Ohms": 10000.0,
        //       "thermBeta": 3950.0,
        //       "thermT0C": 25.0,
        //       "cal_m": 1.0,
        //       "cal_b": 0.0
        //     }
        //   ]
        // }

        QJsonObject a;
        a["index"] = cfg.index;

        if (!cfg.name.isEmpty())
            a["name"] = cfg.name;

        if (!cfg.typeName.isEmpty())
            a["type"] = cfg.typeName;

        if (!cfg.tempModel.isEmpty())
            a["tempModel"] = cfg.tempModel;

        // Ranges: only send if > 0
        if (cfg.voltageMaxRange > 0.0)
            a["voltageMaxRange"] = cfg.voltageMaxRange;
        if (cfg.currentMaxRange > 0.0)
            a["currentMaxRange"] = cfg.currentMaxRange;
        if (cfg.temperatureMaxRange > 0.0)
            a["temperatureMaxRange"] = cfg.temperatureMaxRange;

        // Thermistor params: only send if > 0
        if (cfg.thermSeriesOhms > 0.0)
            a["thermSeriesOhms"] = cfg.thermSeriesOhms;
        if (cfg.thermR0Ohms > 0.0)
            a["thermR0Ohms"] = cfg.thermR0Ohms;
        if (cfg.thermBeta > 0.0)
            a["thermBeta"] = cfg.thermBeta;

        // thermT0C: allow zero, but only send if explicitly set (use a sane heuristic)
        // If you want to allow exactly 0C, this is fine because it will send it.
        // If you want "unset", leave thermT0C at something like NAN in RayAuxConfig.
        if (cfg.thermT0C > -1000.0) // crude “always valid” guard; adjust if you use NAN elsewhere
            a["thermT0C"] = cfg.thermT0C;

        // Calibration: always include (these are safe defaults in RayAuxConfig.h)
        a["cal_m"] = cfg.calM;
        a["cal_b"] = cfg.calB;

        QJsonArray auxArray;
        auxArray.append(a);

        QJsonObject obj;
        obj["cmd"] = QStringLiteral("set_aux_config");
        obj["aux"] = auxArray;

        qDebug() << "[SerialComm] Sending set_aux_config:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);

        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::applyAuxCalibration(
        int index, double vLow, double engLow, double vHigh, double engHigh)
    {
        // Serial equivalent of HTTP POST /api/aux/calibrate/2point.
        // Assumed firmware side JSON:
        // {
        //   "cmd": "aux_calibrate_2point",
        //   "index": 0,
        //   "low":  { "v": <volts>, "eng": <engineering> },
        //   "high": { "v": <volts>, "eng": <engineering> }
        // }

        QJsonObject low;
        low["v"] = vLow;
        low["eng"] = engLow;

        QJsonObject high;
        high["v"] = vHigh;
        high["eng"] = engHigh;

        QJsonObject obj;
        obj["cmd"] = QStringLiteral("aux_calibrate_2point");
        obj["index"] = index;
        obj["low"] = low;
        obj["high"] = high;

        qDebug() << "[SerialComm] Sending aux_calibrate_2point:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::requestAuxDigitalConfig(int index)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("cmd"), QStringLiteral("get_aux_digital_config"));
        if (index >= 0)
            obj.insert(QStringLiteral("index"), index);

        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::requestAuxDigitalState(int index)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("cmd"), QStringLiteral("get_aux_digital_state"));
        if (index >= 0)
            obj.insert(QStringLiteral("index"), index);

        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::setAuxDigitalValue(int index, bool value)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("cmd"), QStringLiteral("set_aux_digital_state"));
        obj.insert(QStringLiteral("index"), index);
        obj.insert(QStringLiteral("value"), value);

        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::applyAuxDigitalConfig(int index, int modeId)
    {
        // Backwards-compatible helper: call the full overload with defaults.
        applyAuxDigitalConfig(index, modeId, -1, false, QString());
    }

    void RaySerialCommWorker::applyAuxDigitalConfig(
        int index, int modeId, int pullId, bool defaultState, const QString &name)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("cmd"), QStringLiteral("set_aux_digital_config"));
        obj.insert(QStringLiteral("index"), index);
        obj.insert(QStringLiteral("mode_id"), modeId);

        if (pullId >= 0)
            obj.insert(QStringLiteral("pull_id"), pullId);

        obj.insert(QStringLiteral("defaultState"), defaultState);

        if (!name.isEmpty())
            obj.insert(QStringLiteral("name"), name);

        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::requestModulationStart(int coupler,
                                                     const QString &path,
                                                     int samples,
                                                     int sampleRateHz)
    {
        // Normalize path to what firmware expects: "fwd" or "ref"
        QString p = path.trimmed().toLower();
        if (p == "forward" || p == "fwd")
            p = "fwd";
        else if (p == "reflected" || p == "ref")
            p = "ref";
        else
            p = "fwd"; // default

        QJsonObject obj;
        obj["cmd"] = "start_modulation_capture";
        obj["coupler"] = coupler;
        obj["path"] = p;
        obj["samples"] = samples;
        obj["sample_rate_hz"] = sampleRateHz;

        qDebug() << "[SerialComm] Sending start_modulation_capture:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::requestModulationStatus()
    {
        QJsonObject obj;
        obj["cmd"] = "get_modulation_status";

        qDebug() << "[SerialComm] Sending get_modulation_status:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::requestModulationData(int maxSamples)
    {
        QJsonObject obj;
        obj["cmd"] = "get_modulation_data";
        obj["max_samples"] = maxSamples;

        qDebug() << "[SerialComm] Sending get_modulation_data:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::requestProtectionReset()
    {
        QJsonObject obj;
        obj["cmd"] = "reset_protection";

        qDebug() << "[SerialComm] Sending reset_protection:"
                 << QJsonDocument(obj).toJson(QJsonDocument::Compact);
        sendJsonCommand(obj);
    }

    void RaySerialCommWorker::startOtaFromUrl(const QString &url)
    {
        if (!m_serial.isOpen()) {
            emit errorOccurred(tr("Serial port not open."));
            emit otaStartAck(false, tr("Serial port not open."));
            return;
        }

        QJsonObject obj;
        obj["cmd"] = "ota_start";
        obj["url"] = url;

        sendJsonCommand(obj);
        // Ack will come back asynchronously; we’ll emit otaStartAck() when we parse it.
    }

    void RaySerialCommWorker::requestOtaStatus()
    {
        if (!m_serial.isOpen()) {
            emit errorOccurred(tr("Serial port not open."));
            return;
        }

        QJsonObject obj;
        obj["cmd"] = "ota_status";
        sendJsonCommand(obj);
    }

} // namespace raypm
