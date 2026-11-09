// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "RayHttpCommWorker.h"
#include "common/Constants.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <cmath>

namespace {

static raypm::RayCalTable parseCalTable(const QJsonObject &pathObj)
{
    raypm::RayCalTable t;
    t.mode = pathObj.value(QStringLiteral("mode")).toString();
    t.count = pathObj.value(QStringLiteral("count")).toInt(0);

    // Optional linear params (firmware may include them)
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

    // If firmware didn't provide count, infer it from points.
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

namespace raypm {

// Helper to build the base URL from settings (host, port, basePath, TLS).
static QUrl buildBaseUrl(const AppSettings &settings)
{
    const auto &http = settings.http;

    const QString scheme = http.enableTls ? QStringLiteral("https") : QStringLiteral("http");

    QUrl url;
    url.setScheme(scheme);
    url.setHost(http.host.isEmpty() ? QStringLiteral("raypowermeter.local") : http.host);
    url.setPort(http.port > 0 ? http.port : 80);

    QString base = http.basePath.trimmed();

    // FIX: legacy reverse-proxy prefix (breaks direct-to-ESP32)
    if (base == QStringLiteral("/pi") || base == QStringLiteral("pi"))
        base.clear();

    if (!base.isEmpty()) {
        if (!base.startsWith('/'))
            base.prepend('/');
        if (base.endsWith('/'))
            base.chop(1);
        url.setPath(base);
    }

    return url;
}

RayHttpCommWorker::RayHttpCommWorker(const AppSettings &settings, QObject *parent)
    : RayCommWorkerBase(settings, parent)
{
    const int intervalMs = (m_settings.http.pollIntervalMs > 0) ? m_settings.http.pollIntervalMs
                                                                : 100; // default ~10 Hz

    m_pollTimer.setInterval(intervalMs);
    m_pollTimer.setSingleShot(false);

    connect(&m_pollTimer, &QTimer::timeout, this, &RayHttpCommWorker::onPollTimeout);

    connect(&m_nam, &QNetworkAccessManager::finished, this, &RayHttpCommWorker::onReplyFinished);
}

void RayHttpCommWorker::requestModulationStart(int coupler,
                                               const QString &path,
                                               int samples,
                                               int sampleRateHz)
{
    QUrl url = buildBaseUrl(m_settings);

    // Normalize path to what firmware expects: "fwd" or "ref"
    QString p = path.trimmed().toLower();
    if (p == "forward" || p == "fwd")
        p = "fwd";
    else if (p == "reflected" || p == "ref")
        p = "ref";

    // Build final endpoint path by APPENDING to any basePath (don’t overwrite with p)
    QString endpointPath = url.path(); // may be "" or "/something"
    if (endpointPath.isEmpty())
        endpointPath = QStringLiteral("/");

    if (!endpointPath.endsWith('/'))
        endpointPath += '/';

    endpointPath += QStringLiteral("api/modulation/start");
    url.setPath(endpointPath);

    QJsonObject obj;
    obj["coupler"] = coupler;
    obj["path"] = p; // FIX: send normalized value
    obj["samples"] = samples;
    obj["sample_rate_hz"] = sampleRateHz;

    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qInfo() << "[HTTP] POST" << url.toString() << body;

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", QStringLiteral("mod_start"));
}

void RayHttpCommWorker::requestModulationStatus()
{
    QUrl url = buildBaseUrl(m_settings);

    QString p = url.path();
    if (!p.endsWith('/'))
        p += '/';
    p += QStringLiteral("api/modulation/status");
    url.setPath(p);

    QNetworkRequest req(url);
    auto *reply = m_nam.get(req);
    reply->setProperty("raypm_kind", QStringLiteral("mod_status"));
}

void RayHttpCommWorker::requestModulationData(int maxSamples)
{
    QUrl url = buildBaseUrl(m_settings);

    QString p = url.path();
    if (!p.endsWith('/'))
        p += '/';
    p += QStringLiteral("api/modulation/data");
    url.setPath(p);

    QUrlQuery q(url);
    q.addQueryItem(QStringLiteral("max_samples"), QString::number(maxSamples));
    url.setQuery(q);

    QNetworkRequest req(url);
    auto *reply = m_nam.get(req);
    reply->setProperty("raypm_kind", QStringLiteral("mod_data"));
}

void RayHttpCommWorker::requestProtectionReset()
{
    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/reset_protection");
    url.setPath(path);

    const QJsonObject obj;
    const QJsonDocument doc(obj);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    qDebug() << "[HTTP] POST reset_protection ->" << url.toString() << "bytes=" << body.size();

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", QStringLiteral("protection_reset"));
}

void RayHttpCommWorker::requestOtaStatus()
{
    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/ota/status");
    url.setPath(path);

    QNetworkRequest req(url);
    auto *reply = m_nam.get(req);
    reply->setProperty("raypm_kind", QStringLiteral("ota_status"));
}

void RayHttpCommWorker::startOtaFromUrl(const QString &fwUrl)
{
    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/ota/start");
    url.setPath(path);

    QJsonObject obj;
    obj["url"] = fwUrl;

    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", QStringLiteral("ota_start"));
}

void RayHttpCommWorker::start()
{
    m_connected = false;
    m_couplerConfigRequested = false;

    if (!m_pollTimer.isActive())
        m_pollTimer.start();
}

void RayHttpCommWorker::stop()
{
    m_pollTimer.stop();

    if (m_connected) {
        m_connected = false;
        emit connectedChanged(false);
    }
}

void RayHttpCommWorker::setBackgroundPollingEnabled(bool enabled)
{
    if (enabled) {
        if (!m_pollTimer.isActive())
            m_pollTimer.start();
    } else {
        m_pollTimer.stop();
    }
}

void RayHttpCommWorker::onPollTimeout()
{
    requestStatus();
}

void RayHttpCommWorker::requestStatus()
{
    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/status");
    url.setPath(path);

    QNetworkRequest req(url);
    auto *reply = m_nam.get(req);
    reply->setProperty("raypm_kind", QStringLiteral("status"));
}

void RayHttpCommWorker::requestCouplerConfig()
{
    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/coupler/config");
    url.setPath(path);

    QNetworkRequest req(url);
    auto *reply = m_nam.get(req);
    reply->setProperty("raypm_kind", QStringLiteral("coupler"));
}

void RayHttpCommWorker::onReplyFinished(QNetworkReply *reply)
{
    const QString kind = reply->property("raypm_kind").toString();

    if (reply->error() != QNetworkReply::NoError) {
        const QByteArray errBody = reply->readAll();
        qWarning() << "[HTTP] error from" << reply->url() << "kind=" << kind << "status="
                   << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() << ":"
                   << reply->errorString() << "body=" << QString::fromUtf8(errBody);

        if (m_connected) {
            m_connected = false;
            emit connectedChanged(false);
        }

        const QString detail = QJsonDocument::fromJson(errBody).object().value("message").toString();
        emit errorOccurred(tr("HTTP error (%1): %2").arg(kind, detail.isEmpty() ? reply->errorString() : detail));

        reply->deleteLater();
        return;
    }

    const QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit errorOccurred(tr("HTTP error (%1): Invalid JSON (%2)").arg(kind, perr.errorString()));
        return;
    }

    if (kind == QLatin1String("status")) {
        const bool ok = handleStatusJson(doc);

        // After the first good status packet, fetch coupler config once.
        if (ok && !m_couplerConfigRequested) {
            m_couplerConfigRequested = true;
            requestCouplerConfig();
        }
        if (ok && !m_auxConfigRequested) {
            m_auxConfigRequested = true;
            requestAuxConfig();
        }
    } else if (kind == QLatin1String("ota_start")) {
        const QJsonObject root = doc.object();

        const bool ok = root.value(QStringLiteral("ok")).toBool(false);

        QString msg = root.value(QStringLiteral("message")).toString();
        if (msg.isEmpty())
            msg = root.value(QStringLiteral("error")).toString();
        if (msg.isEmpty() && !ok)
            msg = tr("OTA start failed.");

        qDebug() << "[HTTP] OTA start ack:" << ok << msg;
        emit otaStartAck(ok, msg);

    } else if (kind == QLatin1String("ota_status")) {
        const QJsonObject root = doc.object();
        const QJsonObject ota = root.value(QStringLiteral("ota")).toObject();

        // State/phase (tolerant)
        QString state = ota.value(QStringLiteral("state"))
                            .toString(ota.value(QStringLiteral("status"))
                                          .toString(ota.value(QStringLiteral("phase"))
                                                        .toString(QStringLiteral("unknown"))));

        qDebug() << "[HTTP] OTA status:" << state;
        // Progress (tolerant)
        int progress = 0;
        if (ota.contains(QStringLiteral("progress_pct")))
            progress = ota.value(QStringLiteral("progress_pct")).toInt(0);
        else if (ota.contains(QStringLiteral("progress")))
            progress = ota.value(QStringLiteral("progress")).toInt(0);
        else if (ota.contains(QStringLiteral("percent")))
            progress = ota.value(QStringLiteral("percent")).toInt(0);

        // Message/error (tolerant)
        QString message = ota.value(QStringLiteral("message"))
                              .toString(
                                  ota.value(QStringLiteral("msg"))
                                      .toString(ota.value(QStringLiteral("error")).toString()));

        // Overall OK
        bool ok = root.value(QStringLiteral("ok")).toBool(true);
        if (ota.contains(QStringLiteral("ok")))
            ok = ok && ota.value(QStringLiteral("ok")).toBool(true);

        // Done detection
        bool done = ota.value(QStringLiteral("done")).toBool(false);
        if (!done) {
            const QString s = state.toLower();
            done = s.contains(QStringLiteral("done")) || s.contains(QStringLiteral("success"))
                   || s.contains(QStringLiteral("complete")) || s.contains(QStringLiteral("fail"))
                   || s.contains(QStringLiteral("error"));
        }

        const QString rawJson = QString::fromUtf8(
            QJsonDocument(root).toJson(QJsonDocument::Compact));

        emit otaStatusReceived(state, progress, message, done, ok, rawJson);
    } else if (kind == QLatin1String("coupler")) {
        handleCouplerConfigJson(doc);
    } else if (kind == QLatin1String("coupler_config_set")) {
        const int idx = reply->property("raypm_index").toInt();

        qDebug() << "[HTTP] Coupler config applied OK for index" << idx;

        emit couplerConfigApplied(idx, true, tr("Coupler config applied."));
        requestCouplerConfig(); // refresh so reload reflects new values
    } else if (kind == QLatin1String("device_config_get")) {
        handleDeviceConfigJson(doc);
    } else if (kind == QLatin1String("device_config_set")) {
        qDebug() << "[HTTP] Device config applied OK";

        emit deviceConfigApplied(true, tr("Device config applied."));
        requestDeviceConfig(); // refresh so UI reflects stored values
    } else if (kind == QLatin1String("coupler_cal_set")) {
        const int idx = reply->property("raypm_index").toInt();
        qDebug() << "[HTTP] Coupler calibration applied OK for index" << idx;

        emit couplerCalibrationApplied(idx, true, tr("Calibration applied."));
        requestCouplerConfig(); // optional refresh
    } else if (kind == QLatin1String("coupler_cal_copy")) {
        const int idx = reply->property("raypm_index").toInt();
        const QJsonObject o = doc.object();

        // Expected from firmware: { type:"ok", index, from, to, m, b }
        const double m = o.value(QStringLiteral("m")).toDouble(0.0);
        const double b = o.value(QStringLiteral("b")).toDouble(0.0);

        const QString from = o.value(QStringLiteral("from")).toString();
        const QString to = o.value(QStringLiteral("to")).toString();

        qDebug() << "[HTTP] Coupler calibration copied OK for index" << idx << "from=" << from
                 << "to=" << to << "m=" << m << "b=" << b;

        const QString msg = tr("Calibration copied (%1 → %2): m=%3, b=%4")
                                .arg(from.isEmpty() ? tr("forward") : from,
                                     to.isEmpty() ? tr("reflected") : to)
                                .arg(m, 0, 'f', 5)
                                .arg(b, 0, 'f', 5);

        emit couplerCalibrationApplied(idx, true, msg);
        requestCouplerConfig(); // refresh so UI reflects stored values
    } else if (kind == QLatin1String("coupler_cal_reset")) {
        const int idx = reply->property("raypm_index").toInt();

        qDebug() << "[HTTP] Coupler calibration reset OK for index" << idx;

        emit couplerCalibrationApplied(idx, true, tr("Calibration reset to factory defaults."));
        requestCouplerConfig(); // refresh UI
    } else if (kind == QLatin1String("mod_start")) {
        const QJsonObject o = doc.object();

        const QString status = o.value(QStringLiteral("status")).toString();
        const bool ok = (status == QLatin1String("ok"));

        const int coupler = o.value(QStringLiteral("coupler")).toInt(0);
        const QString path = o.value(QStringLiteral("path")).toString();
        const int samples = o.value(QStringLiteral("samples")).toInt(0);
        const int rate = o.value(QStringLiteral("sample_rate_hz")).toInt(0);

        emit modulationStartAck(ok,
                                ok ? tr("Modulation capture started.")
                                   : tr("Modulation start failed."),
                                coupler,
                                path,
                                samples,
                                rate);

    } else if (kind == QLatin1String("mod_status")) {
        const QJsonObject o = doc.object();

        // Firmware naming may vary; accept either "state" or "status"
        QString state = o.value(QStringLiteral("state")).toString();
        if (state.isEmpty())
            state = o.value(QStringLiteral("status")).toString();

        const int coupler = o.value(QStringLiteral("coupler")).toInt(0);
        const QString path = o.value(QStringLiteral("path")).toString();
        const int samples = o.value(QStringLiteral("samples")).toInt(0);
        const int rate = o.value(QStringLiteral("sample_rate_hz")).toInt(0);

        emit modulationStatusReceived(state, coupler, path, samples, rate);

    } else if (kind == QLatin1String("mod_data")) {
        const QJsonObject o = doc.object();

        const int coupler = o.value(QStringLiteral("coupler")).toInt(0);
        const QString path = o.value(QStringLiteral("path")).toString();
        const int rate = o.value(QStringLiteral("sample_rate_hz")).toInt(0);

        QVector<qint16> samples;
        const QJsonArray arr = o.value(QStringLiteral("samples")).toArray();
        samples.reserve(arr.size());
        for (const auto &v : arr)
            samples.push_back(static_cast<qint16>(v.toInt()));

        emit modulationDataReceived(coupler, path, rate, samples);
    } else if (kind == QLatin1String("aux_config_get")) {
        handleAuxConfigJson(doc);
    } else if (kind == QLatin1String("aux_config_set")) {
        const int idx = reply->property("raypm_index").toInt();
        qDebug() << "[HTTP] Aux config applied OK for index" << idx;

        emit auxConfigApplied(idx, true, tr("Aux config applied."));
        requestAuxConfig(); // refresh config from firmware
    } else if (kind == QLatin1String("aux_cal_set")) {
        const int idx = reply->property("raypm_index").toInt();
        qDebug() << "[HTTP] Aux calibration applied OK for index" << idx;

        emit auxCalibrationApplied(idx, true, tr("Aux calibration applied."));
        requestAuxConfig(); // refresh cal_m/cal_b from firmware
    } else if (kind == QLatin1String("protection_reset")) {
        qDebug() << "[HTTP] Protection reset OK";
        requestStatus();
    } else if (kind == "ota_start") {
        const QByteArray raw = reply->readAll();
        const QJsonDocument jd = QJsonDocument::fromJson(raw);
        if (!jd.isObject()) {
            emit otaStartAck(false, tr("Invalid JSON from /api/ota/start"));
            return;
        }

        const QJsonObject root = jd.object();
        const bool ok = root.value("ok").toBool(false);
        const QString msg = root.value("message").toString(
            root.value("error").toString(ok ? QString() : tr("OTA start failed.")));

        emit otaStartAck(ok, msg);
    } else if (kind == "ota_status") {
        const QByteArray raw = reply->readAll();
        const QJsonDocument jd = QJsonDocument::fromJson(raw);
        if (!jd.isObject()) {
            emit otaStatusReceived("unknown",
                                   0,
                                   tr("Invalid JSON from /api/ota/status"),
                                   false,
                                   false,
                                   QString::fromUtf8(raw));
            return;
        }

        const QJsonObject root = jd.object();
        const QJsonObject ota = root.value("ota").toObject();

        // Tolerant parsing: try multiple common field names
        const QString state = ota.value("state").toString(
            ota.value("status").toString(ota.value("phase").toString("unknown")));

        int progress = 0;
        if (ota.value("progress").isDouble())
            progress = ota.value("progress").toInt();
        else if (ota.value("progress_pct").isDouble())
            progress = ota.value("progress_pct").toInt();
        else if (ota.value("percent").isDouble())
            progress = ota.value("percent").toInt();

        const QString message = ota.value("message").toString(
            ota.value("msg").toString(ota.value("error").toString()));

        const bool ok = ota.value("ok").toBool(root.value("ok").toBool(true));

        // "done" heuristic:
        const bool done = ota.value("done").toBool(false)
                          || state.contains("done", Qt::CaseInsensitive)
                          || state.contains("success", Qt::CaseInsensitive)
                          || state.contains("fail", Qt::CaseInsensitive)
                          || state.contains("error", Qt::CaseInsensitive);

        emit otaStatusReceived(state, progress, message, done, ok, QString::fromUtf8(raw));
    } else {
        // Fallback (if no property set): treat as status JSON.
        handleStatusJson(doc);
    }
}

bool RayHttpCommWorker::handleStatusJson(const QJsonDocument &doc)
{
    const QJsonObject root = doc.object();

    const QJsonObject rfObj = root.value(QStringLiteral("rf")).toObject();
    const QJsonArray couplersArr = rfObj.value(QStringLiteral("couplers")).toArray();
    const QJsonArray auxArr = rfObj.value(QStringLiteral("aux")).toArray();

    const double freqHz = rfObj.value(QStringLiteral("freq_hz")).toDouble(0.0);
    const bool freqValid = rfObj.value(QStringLiteral("freq_valid")).toBool(false);

    if (couplersArr.isEmpty()) {
        emit errorOccurred(tr("Status JSON: rf.couplers[] missing/empty"));
        return false;
    }

    RayTelemetry t;

    // --- Device app/firmware identity (root.app) -----------------------------
    const QJsonObject appObj = root.value(QStringLiteral("app")).toObject();
    if (!appObj.isEmpty()) {
        t.deviceAppName = appObj.value(QStringLiteral("name")).toString();
        t.deviceAppVersion = appObj.value(QStringLiteral("version")).toString();
    }

    t.timestamp = QDateTime::currentDateTime();

    // --- RF high-level state ------------------------------------------------
    t.rfMode = static_cast<quint8>(rfObj.value(QStringLiteral("mode")).toInt(0));
    t.protectionOn = rfObj.value(QStringLiteral("protection_on")).toBool(true);

    // Frequency meter
    t.rfFrequencyHz = freqHz;
    t.rfFrequencyValid = freqValid;

    // Trip info
    t.trip.tripped = rfObj.value(QStringLiteral("tripped")).toBool(false);
    if (t.trip.tripped) {
        const QJsonObject tripObj = rfObj.value(QStringLiteral("trip")).toObject();
        t.trip.type = static_cast<quint8>(tripObj.value(QStringLiteral("type")).toInt(0));
        t.trip.source = static_cast<quint8>(tripObj.value(QStringLiteral("source")).toInt(0));
        t.trip.threshold = tripObj.value(QStringLiteral("threshold")).toDouble(0.0);
        t.trip.value = tripObj.value(QStringLiteral("value")).toDouble(0.0);
    }

    // --- Network status (HTTP only) -----------------------------------------
    const QJsonObject netObj = root.value(QStringLiteral("net")).toObject();
    if (!netObj.isEmpty()) {
        t.net.state = netObj.value(QStringLiteral("state")).toInt(0);
        t.net.ip = netObj.value(QStringLiteral("ip")).toString();

        if (netObj.contains(QStringLiteral("rssi_dbm")))
            t.net.rssiDbm = netObj.value(QStringLiteral("rssi_dbm")).toInt(0);

        t.net.hostname = netObj.value(QStringLiteral("hostname")).toString();
    }

    // --- Couplers ------------------------------------------------------------
    const int couplerCount = qMin(couplersArr.size(), MAX_COUPLERS);
    t.couplers.reserve(couplerCount);

    for (int i = 0; i < couplerCount; ++i) {
        const QJsonObject c = couplersArr.at(i).toObject();

        RayCouplerSample cs;
        cs.index = c.value(QStringLiteral("index")).toInt(i);
        cs.name = c.value(QStringLiteral("name")).toString();
        cs.forwardVolts = c.value("fwd_v").toDouble(0.0);
        cs.reflectedVolts = c.value("ref_v").toDouble(0.0);
        cs.forwardWatts = c.value(QStringLiteral("fwd_w")).toDouble(0.0);
        cs.reflectedWatts = c.value(QStringLiteral("ref_w")).toDouble(0.0);
        cs.swr = c.value(QStringLiteral("swr")).toDouble(1.0);
        cs.peakForwardWatts = c.value(QStringLiteral("peak_f")).toDouble(0.0);
        cs.peakReflectedWatts = c.value(QStringLiteral("peak_r")).toDouble(0.0);

        t.couplers.push_back(cs);
    }

    // Optional legacy convenience: mirror first coupler into flat fields
    if (!t.couplers.isEmpty()) {
        const auto &c0 = t.couplers.first();
        t.couplerIndex = c0.index;
        t.forwardWatts = c0.forwardWatts;
        t.reflectedWatts = c0.reflectedWatts;
        t.swrLegacy = c0.swr;
        t.rawFwd = c0.peakForwardWatts;
        t.rawRef = c0.peakReflectedWatts;
    }

    // --- AUX channels --------------------------------------------------------
    for (int i = 0; i < auxArr.size(); ++i) {
        const QJsonObject a = auxArr.at(i).toObject();

        RayAuxSample ax;
        ax.index = a.value(QStringLiteral("index")).toInt(i);
        ax.typeName = a.value(QStringLiteral("type")).toString();
        ax.rawVolts = a.value(QStringLiteral("raw_volts"))
                          .toDouble(
                              a.value(QStringLiteral("raw_v")).toDouble(0.0)); // tolerate either key
        ax.value = a.value(QStringLiteral("value")).toDouble(0.0);

        // Map type string -> enum
        if (ax.typeName == QLatin1String("voltage"))
            ax.type = RayAuxType::Voltage;
        else if (ax.typeName == QLatin1String("current"))
            ax.type = RayAuxType::Current;
        else if (ax.typeName == QLatin1String("temperature"))
            ax.type = RayAuxType::Temperature;
        else
            ax.type = RayAuxType::None;

        t.auxChannels.push_back(ax);
    }

    // Legacy convenience: mirror the first AUX voltage into flat fields
    if (!t.auxChannels.isEmpty()) {
        const auto &a0 = t.auxChannels.first();
        t.auxIndex = a0.index;
        t.voltage = (a0.type == RayAuxType::Voltage) ? a0.value : 0.0;
        t.current = (a0.type == RayAuxType::Current) ? a0.value : 0.0;
        t.temperatureC = (a0.type == RayAuxType::Temperature) ? a0.value : 0.0;
    }

    // --- AUX digital I/O channels ----------------------------------------------
    QVector<RayAuxDigitalStatus> auxDigitalChannels;

    // BEFORE (buggy):
    // const auto auxDigArr = root.value(QStringLiteral("aux_digital")).toArray();

    // AFTER (correct – matches firmware + Serial parser):
    const auto auxDigArr = rfObj.value(QStringLiteral("aux_digital")).toArray();

    for (const auto &v : auxDigArr) {
        const auto obj = v.toObject();
        RayAuxDigitalStatus d;
        d.index = obj.value(QStringLiteral("index")).toInt(-1);
        d.name = obj.value(QStringLiteral("name")).toString();
        d.modeName = obj.value(QStringLiteral("mode")).toString();
        d.value = obj.value(QStringLiteral("value")).toBool(false);

        const int modeId = obj.value(QStringLiteral("mode_id")).toInt(0);
        switch (modeId) {
        case 1:
            d.mode = RayAuxDigitalMode::Input;
            break;
        case 2:
            d.mode = RayAuxDigitalMode::Output;
            break;
        default:
            d.mode = RayAuxDigitalMode::Disabled;
            break;
        }

        auxDigitalChannels.push_back(d);
    }

    t.auxDigitalChannels = auxDigitalChannels;

    // --- Notify listeners ----------------------------------------------------
    emit telemetryReceived(t);

    if (!m_connected) {
        m_connected = true;
        emit connectedChanged(true);
    }

    return true;
}

void RayHttpCommWorker::handleCouplerConfigJson(const QJsonDocument &doc)
{
    const QJsonObject root = doc.object();

    const int activeIndex = root.value(QStringLiteral("active_index")).toInt(0);
    const QJsonArray arr = root.value(QStringLiteral("couplers")).toArray();

    if (arr.isEmpty()) {
        emit errorOccurred(tr("Coupler config JSON: couplers[] missing/empty"));
        return;
    }

    // For now we just log activeIndex; later we can use this to highlight
    // the active coupler in the UI if we want.
    //qDebug() << "[HTTP] Coupler config JSON active_index =" << activeIndex
    //         << "num_couplers =" << arr.size();

    // Emit a RayCouplerConfig for each coupler entry.
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject c = arr.at(i).toObject();

        RayCouplerConfig cfg;
        cfg.index = c.value(QStringLiteral("index")).toInt(i);
        cfg.name = c.value(QStringLiteral("name")).toString();
        cfg.fullScaleW = c.value(QStringLiteral("full_scale_w")).toDouble(0.0);
        cfg.fwdBarRangeW = c.value(QStringLiteral("fwd_bar_range_w")).toDouble(0.0);
        cfg.refBarRangeW = c.value(QStringLiteral("ref_bar_range_w")).toDouble(0.0);
        cfg.highFwdTripW = c.value(QStringLiteral("high_fwd_trip_w")).toDouble(0.0);
        cfg.highRefTripW = c.value(QStringLiteral("high_ref_trip_w")).toDouble(0.0);
        cfg.highSWRTrip = c.value(QStringLiteral("high_swr_trip")).toDouble(0.0);
        cfg.tripMask = static_cast<quint8>(c.value(QStringLiteral("trip_mask")).toInt(0));
        cfg.typeId = static_cast<quint8>(c.value(QStringLiteral("type_id")).toInt(0));
        cfg.typeName = c.value(QStringLiteral("type_name")).toString();

        // Parse multi-point calibration (cal.fwd / cal.ref)
        parseCouplerCalSection(c, cfg);

        // Debug each coupler config
        qDebug() << "[HTTP] Coupler config received:" << cfg.index << cfg.name
                 << "FullScaleW=" << cfg.fullScaleW << "FwdBarRangeW=" << cfg.fwdBarRangeW
                 << "RefBarRangeW=" << cfg.refBarRangeW;

        emit couplerConfigReceived(cfg);
    }
}

void RayHttpCommWorker::applyCouplerConfig(const RayCouplerConfig &cfg)
{
    if (!m_connected) {
        emit errorOccurred(tr("HTTP worker not configured."));
        return;
    }

    // Build JSON payload using the same keys you parse in handleCouplerConfigJson()
    QJsonObject obj;
    obj["index"] = cfg.index;
    obj["type_id"] = static_cast<int>(cfg.typeId);
    obj["name"] = cfg.name;

    obj["full_scale_w"] = cfg.fullScaleW;
    obj["fwd_bar_range_w"] = cfg.fwdBarRangeW;
    obj["ref_bar_range_w"] = cfg.refBarRangeW;

    obj["high_fwd_trip_w"] = cfg.highFwdTripW;
    obj["high_ref_trip_w"] = cfg.highRefTripW;
    obj["high_swr_trip"] = cfg.highSWRTrip;
    obj["trip_mask"] = cfg.tripMask;

    const QJsonDocument doc(obj);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    QUrl url = buildBaseUrl(m_settings);

    // --- FIX: append the REST path like requestCouplerConfig() does ---
    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/coupler/config");
    url.setPath(path);

    qDebug() << "[HTTP] POST coupler_config_set ->" << url.toString() << "bytes=" << body.size()
             << "index=" << cfg.index;
    // ---------------------------------------------------------------

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", "coupler_config_set");
    reply->setProperty("raypm_index", cfg.index);
}

void RayHttpCommWorker::requestDeviceConfig()
{
    if (!m_connected) {
        emit errorOccurred(tr("HTTP worker not configured."));
        return;
    }

    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/device/config");
    url.setPath(path);

    qDebug() << "[HTTP] GET device_config_get ->" << url.toString();

    QNetworkRequest req(url);
    auto *reply = m_nam.get(req);
    reply->setProperty("raypm_kind", "device_config_get");
}

void RayHttpCommWorker::applyDeviceConfig(int brightness, qint64 utcEpochSeconds)
{
    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/device/config");
    url.setPath(path);

    QJsonObject obj;
    obj["brightness"] = brightness;
    obj["unix_time"] = utcEpochSeconds;

    const QJsonDocument doc(obj);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    qDebug() << "[HTTP] POST device_config_set ->" << url.toString() << "bytes=" << body.size();

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", "device_config_set");
}

void RayHttpCommWorker::copyCouplerCalibration(int index, bool fromForward, bool toForward)
{
    if (!m_connected) {
        emit errorOccurred(tr("HTTP worker not configured."));
        return;
    }

    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/coupler/calibrate/copy");
    url.setPath(path);

    QJsonObject obj;
    obj["index"] = index;
    obj["from"] = fromForward ? QStringLiteral("forward") : QStringLiteral("reflected");
    obj["to"] = toForward ? QStringLiteral("forward") : QStringLiteral("reflected");

    const QJsonDocument doc(obj);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    qDebug() << "[HTTP] POST coupler_cal_copy ->" << url.toString() << "bytes=" << body.size()
             << "index=" << index;

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", "coupler_cal_copy");
    reply->setProperty("raypm_index", index);
}

void RayHttpCommWorker::resetCouplerCalibration(int index,
                                                const QString &path,
                                                const QString &calSource)
{
    if (!m_connected) {
        emit errorOccurred(tr("HTTP worker not configured."));
        return;
    }

    QUrl url = buildBaseUrl(m_settings);

    QString p = url.path();
    if (!p.endsWith('/'))
        p += '/';
    p += QStringLiteral("api/coupler/calibrate/reset");
    url.setPath(p);

    QJsonObject obj;
    obj["index"] = index;
    if (!path.isEmpty())
        obj["path"] = path; // "both" recommended
    if (!calSource.isEmpty())
        obj["cal_source"] = calSource; // "FactoryLinear" | "FactoryTable"

    const QJsonDocument doc(obj);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    qDebug() << "[HTTP] POST coupler_cal_reset ->" << url.toString() << "bytes=" << body.size()
             << "index=" << index << "cal_source=" << calSource;

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", "coupler_cal_reset");
}

void RayHttpCommWorker::applyCouplerCalibrationTable(int index,
                                                     bool forwardPath,
                                                     const QVector<RayCalPoint> &points)
{
    if (!m_connected) {
        emit errorOccurred(tr("HTTP worker not configured."));
        return;
    }

    if (points.size() < 2 || points.size() > MAX_CAL_POINTS) {
        emit errorOccurred(tr("Calibration requires 2..%1 points (got %2).").arg(MAX_CAL_POINTS).arg(points.size()));
        return;
    }

    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/coupler/calibrate");
    url.setPath(path);

    QJsonObject obj;
    obj["index"] = index;

    // Firmware uses "fwd"/"ref" (Serial does), but your HTTP uses forward/reflected elsewhere.
    // If your firmware expects "fwd"/"ref" for HTTP too, change here to match.
    obj["path"] = forwardPath ? QStringLiteral("fwd") : QStringLiteral("ref");

    QJsonArray pts;
    for (const auto &p : points) {
        QJsonObject jp;
        jp["v"] = p.volts;
        jp["w"] = p.watts;
        pts.append(jp);
    }
    obj["points"] = pts;

    // Optional explicit fields (safe if firmware ignores)
    obj["count"] = points.size();
    obj["mode"] = QStringLiteral("table");

    const QJsonDocument doc(obj);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    qDebug() << "[HTTP] POST coupler_cal_table ->" << url.toString() << "bytes=" << body.size();

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", QStringLiteral("coupler_cal_table"));
}

void RayHttpCommWorker::applyCouplerCalibration(
    int index, bool forwardPath, double vLow, double wLowTrue, double vHigh, double wHighTrue)
{
    if (!m_connected) {
        emit errorOccurred(tr("HTTP worker not configured."));
        return;
    }

    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/coupler/calibrate");
    url.setPath(path);

    QJsonObject low;
    low["v"] = vLow;
    low["w"] = wLowTrue;

    QJsonObject high;
    high["v"] = vHigh;
    high["w"] = wHighTrue;

    QJsonObject obj;
    obj["index"] = index;
    obj["path"] = forwardPath ? QStringLiteral("forward") : QStringLiteral("reflected");
    obj["low"] = low;
    obj["high"] = high;

    const QJsonDocument doc(obj);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    qDebug() << "[HTTP] POST coupler_cal_set ->" << url.toString() << "bytes=" << body.size()
             << "index=" << index;

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", "coupler_cal_set");
    reply->setProperty("raypm_index", index);
}

void RayHttpCommWorker::handleDeviceConfigJson(const QJsonDocument &doc)
{
    const QJsonObject o = doc.object();

    const int brightness = o.value(QStringLiteral("brightness")).toInt(-1);
    const qint64 unixTime = o.value(QStringLiteral("unix_time")).toVariant().toLongLong();

    if (brightness < 0) {
        emit errorOccurred(tr("Device config JSON missing brightness"));
        return;
    }

    qDebug() << "[HTTP] Device config received: brightness=" << brightness
             << "unix_time=" << unixTime;

    emit deviceConfigReceived(brightness, unixTime);
}

void RayHttpCommWorker::requestAuxConfig()
{
    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/aux/config");
    url.setPath(path);

    QNetworkRequest req(url);
    auto *reply = m_nam.get(req);
    reply->setProperty("raypm_kind", QStringLiteral("aux_config_get"));
    qDebug() << "[HTTP] GET aux_config_get ->" << url.toString();
}

void RayHttpCommWorker::handleAuxConfigJson(const QJsonDocument &doc)
{
    const QJsonObject root = doc.object();
    const QJsonArray auxArr = root.value(QStringLiteral("aux")).toArray();

    if (auxArr.isEmpty()) {
        emit errorOccurred(tr("Aux config JSON: aux[] missing/empty"));
        return;
    }

    for (int i = 0; i < auxArr.size(); ++i) {
        const QJsonObject a = auxArr.at(i).toObject();

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

        qDebug() << "[HTTP] Aux config"
                 << "idx=" << cfg.index << "type=" << cfg.typeName << "vMax=" << cfg.voltageMaxRange
                 << "iMax=" << cfg.currentMaxRange << "tMax=" << cfg.temperatureMaxRange
                 << "beta=" << cfg.thermBeta << "cal_m=" << cfg.calM << "cal_b=" << cfg.calB;

        emit auxConfigReceived(cfg);
    }
}

void RayHttpCommWorker::applyAuxConfig(const RayAuxConfig &cfg)
{
    if (!m_connected) {
        emit errorOccurred(tr("HTTP worker not configured."));
        return;
    }

    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/aux/config");
    url.setPath(path);

    QJsonObject obj;
    obj["index"] = cfg.index;

    // Only send strings if meaningful
    if (!cfg.name.isEmpty())
        obj["name"] = cfg.name;
    if (!cfg.typeName.isEmpty())
        obj["type"] = cfg.typeName;
    if (!cfg.tempModel.isEmpty())
        obj["tempModel"] = cfg.tempModel;

    // Only send numeric fields if they look valid.
    // This avoids clobbering firmware prefs with 0.0 when cfg isn't fully populated.
    if (cfg.voltageMaxRange > 0.0)
        obj["voltageMaxRange"] = cfg.voltageMaxRange;
    if (cfg.currentMaxRange > 0.0)
        obj["currentMaxRange"] = cfg.currentMaxRange;
    if (cfg.temperatureMaxRange > 0.0)
        obj["temperatureMaxRange"] = cfg.temperatureMaxRange;

    if (cfg.thermSeriesOhms > 0.0)
        obj["thermSeriesOhms"] = cfg.thermSeriesOhms;
    if (cfg.thermR0Ohms > 0.0)
        obj["thermR0Ohms"] = cfg.thermR0Ohms;
    if (cfg.thermBeta > 0.0)
        obj["thermBeta"] = cfg.thermBeta;
    if (cfg.thermT0C > 0.0)
        obj["thermT0C"] = cfg.thermT0C;

    // Calibration: allow cal_b = 0, so include if finite. cal_m must be finite too.
    if (std::isfinite(cfg.calM))
        obj["cal_m"] = cfg.calM;
    if (std::isfinite(cfg.calB))
        obj["cal_b"] = cfg.calB;

    const QJsonDocument doc(obj);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    qDebug() << "[HTTP] POST aux_config_set ->" << url.toString() << "index=" << cfg.index
             << "bytes=" << body.size() << "keys=" << obj.keys();

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", QStringLiteral("aux_config_set"));
    reply->setProperty("raypm_index", cfg.index);
}

void RayHttpCommWorker::applyAuxCalibration(
    int index, double vLow, double engLow, double vHigh, double engHigh)
{
    if (!m_connected) {
        emit errorOccurred(tr("HTTP worker not configured."));
        return;
    }

    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    // Endpoint per your plan:
    path += QStringLiteral("api/aux/calibrate/2point");
    url.setPath(path);

    QJsonObject low;
    low["v"] = vLow;     // raw ADC volts
    low["eng"] = engLow; // engineering unit (V, A, or °C)

    QJsonObject high;
    high["v"] = vHigh;
    high["eng"] = engHigh;

    QJsonObject obj;
    obj["index"] = index;
    obj["low"] = low;
    obj["high"] = high;

    const QJsonDocument doc(obj);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    qDebug() << "[HTTP] POST aux_cal_set ->" << url.toString() << "index=" << index
             << "bytes=" << body.size();

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", QStringLiteral("aux_cal_set"));
    reply->setProperty("raypm_index", index);
}

void RayHttpCommWorker::applyAuxDigitalConfig(int index, int modeId)
{
    // Backwards-compatible helper:
    // Only change the mode, do not touch pull/name/defaultState.
    applyAuxDigitalConfig(index, modeId, -1, false, QString());
}

void RayHttpCommWorker::applyAuxDigitalConfig(
    int index, int modeId, int pullId, bool defaultState, const QString &name)
{
    //if (!m_connected) {
    //    emit errorOccurred(tr("HTTP worker not configured."));
    //    return;
    //}

    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/aux_digital/config");
    url.setPath(path);

    QJsonObject obj;
    obj.insert(QStringLiteral("index"), index);
    obj.insert(QStringLiteral("mode_id"), modeId);

    // Optional pull config
    if (pullId >= 0)
        obj.insert(QStringLiteral("pull_id"), pullId);

    // Only send defaultState when we know the caller is doing a "full" config
    // (i.e. they are also changing pull and/or name). This keeps the old
    // 2-arg helper from unintentionally changing the default state.
    const bool sendDefaultState = (pullId >= 0) || !name.isEmpty();
    if (sendDefaultState)
        obj.insert(QStringLiteral("defaultState"), defaultState);

    // Optional name
    if (!name.isEmpty())
        obj.insert(QStringLiteral("name"), name);

    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", QStringLiteral("aux_digital_config_set"));
    reply->setProperty("raypm_index", index);

    qDebug() << "[HTTP] POST aux_digital/config ->" << url.toString() << "index=" << index
             << "mode_id=" << modeId << "pull_id=" << pullId
             << "sendDefaultState=" << sendDefaultState << "defaultState=" << defaultState
             << "name=" << name << "bytes=" << body.size();
}

void RayHttpCommWorker::setAuxDigitalValue(int index, bool value)
{
    if (!m_connected) {
        emit errorOccurred(tr("HTTP worker not configured."));
        return;
    }

    QUrl url = buildBaseUrl(m_settings);

    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += QStringLiteral("api/aux_digital/state");
    url.setPath(path);

    QJsonObject obj;
    obj.insert(QStringLiteral("index"), index);
    obj.insert(QStringLiteral("value"), value);

    const QJsonDocument doc(obj);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    auto *reply = m_nam.post(req, body);
    reply->setProperty("raypm_kind", QStringLiteral("aux_digital_state_set"));
    reply->setProperty("raypm_index", index);

    qDebug() << "[HTTP] POST aux_digital/state ->" << url.toString() << "index=" << index
             << "value=" << value << "bytes=" << body.size();
}

void RayHttpCommWorker::requestAuxDigitalConfig(int index)
{
    Q_UNUSED(index);
    // Aux Digital configuration is included in the /api/status payload.
    // Just request a fresh status update.
    requestStatus();
}

void RayHttpCommWorker::requestAuxDigitalState(int index)
{
    Q_UNUSED(index);
    // Same as above – state is part of status.
    requestStatus();
}

} // namespace raypm
