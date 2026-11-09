// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "Updater.h"

#include <QCoreApplication>
#include <QPushButton>
#include <QSysInfo>
#include <QtGlobal>

#include "comm/RayCommManager.h"
#include "core/AppSettings.h"

Updater::Updater(raypm::RayCommManager *comm, raypm::AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_comm(comm)
    , m_settings(settings)
{
    m_otaPollTimer.setInterval(800);

    connect(&m_otaPollTimer, &QTimer::timeout, this, [this]() {
        if (m_comm)
            m_comm->requestOtaStatus();
    });
}

QVersionNumber Updater::parseVer(const QString &s)
{
    return QVersionNumber::fromString(s.trimmed());
}

bool Updater::isVersionNewer(const QString &latest, const QString &current)
{
    if (latest.trimmed().isEmpty() || current.trimmed().isEmpty())
        return false;
    return QVersionNumber::compare(parseVer(latest), parseVer(current)) > 0;
}

void Updater::checkForUpdatesInteractive(QWidget *parent)
{
    if (!m_settings) {
        QMessageBox::warning(parent, tr("Updates"), tr("App settings not available."));
        return;
    }

    if (m_settings->updateManifestUrl.isEmpty()) {
        QMessageBox::warning(parent, tr("Updates"), tr("Update manifest URL is not configured."));
        return;
    }

    if (!m_busyDialog) {
        m_busyDialog = new QProgressDialog(tr("Checking for updates..."), tr("Cancel"), 0, 0, parent);
        m_busyDialog->setWindowModality(Qt::WindowModal);
        m_busyDialog->setAutoClose(true);
        m_busyDialog->setAutoReset(true);
    } else {
        // ensure it stays on top of the provided parent
        m_busyDialog->setParent(parent);
    }

    m_busyDialog->setLabelText(tr("Downloading manifest..."));
    m_busyDialog->setValue(0);

    m_busyDialog->show();

    fetchManifest();

    // cancel -> abort network
    connect(m_busyDialog, &QProgressDialog::canceled, this, [this]() {
        if (m_reply)
            m_reply->abort();
        if (m_busyDialog)
            m_busyDialog->hide();
    });
}

void Updater::fetchManifest()
{
    if (!m_settings)
        return;

    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    const QUrl url(m_settings->updateManifestUrl);
    m_reply = m_nam.get(QNetworkRequest(url));

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (m_busyDialog)
            m_busyDialog->hide();

        if (!m_reply)
            return;

        const auto reply = m_reply;
        m_reply = nullptr;

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(nullptr,
                                 tr("Updates"),
                                 tr("Failed to fetch update manifest:\n%1")
                                     .arg(reply->errorString()));
            reply->deleteLater();
            return;
        }

        const QByteArray raw = reply->readAll();
        reply->deleteLater();

        QString err;
        UpdateManifest m;
        if (!parseManifestJson(raw, m, err)) {
            QMessageBox::warning(nullptr, tr("Updates"), tr("Manifest JSON invalid:\n%1").arg(err));
            return;
        }

        m_lastManifest = m;

        // Show interactive update choices.
        // Parent: if busyDialog exists, use its parentWidget; otherwise nullptr.
        QWidget *parent = nullptr;
        if (m_busyDialog)
            parent = m_busyDialog->parentWidget();

        showUpdateDialog(parent, m_lastManifest);
    });
}

bool Updater::parseManifestJson(const QByteArray &raw, UpdateManifest &out, QString &error) const
{
    QJsonParseError perr;
    const QJsonDocument jd = QJsonDocument::fromJson(raw, &perr);

    if (perr.error != QJsonParseError::NoError || !jd.isObject()) {
        error = tr("JSON parse error: %1").arg(perr.errorString());
        return false;
    }

    const QJsonObject root = jd.object();

    out.product = root.value(QStringLiteral("product")).toString();
    out.manifestVersion = root.value(QStringLiteral("manifest_version")).toInt(0);
    out.timestamp = root.value(QStringLiteral("timestamp")).toString();

    const QJsonObject app = root.value(QStringLiteral("app")).toObject();
    out.app.version = app.value(QStringLiteral("version")).toString();
    out.app.changelog = app.value(QStringLiteral("changelog")).toString();

    const QJsonObject dl = app.value(QStringLiteral("downloads")).toObject();
    out.app.windowsUrl = dl.value(QStringLiteral("windows")).toString();
    out.app.macosUrl = dl.value(QStringLiteral("macos")).toString();
    out.app.linuxX86_64Url = dl.value(QStringLiteral("linux-x86_64")).toString();
    // Support aarch64 key variants
    out.app.linuxAarch64Url = dl.value(QStringLiteral("linux-aarch64")).toString();

    if (out.app.linuxAarch64Url.isEmpty()) {
        out.app.linuxAarch64Url = dl.value(QStringLiteral("linux-arch_64")).toString();
    }

    const QJsonObject fw = root.value(QStringLiteral("firmware")).toObject();
    out.firmware.version = fw.value(QStringLiteral("version")).toString();
    out.firmware.build = fw.value(QStringLiteral("build")).toString();
    out.firmware.minAppVersion = fw.value(QStringLiteral("min_app_version")).toString();
    out.firmware.size = static_cast<qint64>(fw.value(QStringLiteral("size")).toDouble(0.0));
    out.firmware.sha256 = fw.value(QStringLiteral("sha256")).toString();
    out.firmware.url = fw.value(QStringLiteral("url")).toString();
    out.firmware.notes = fw.value(QStringLiteral("notes")).toString();

    if (!out.isValid()) {
        error = tr("Missing or invalid 'product' or 'manifest_version'.");
        return false;
    }

    return true;
}

QString Updater::pickAppDownloadUrlForThisOs(const UpdateManifest &m) const
{
#if defined(Q_OS_WIN)
    return m.app.windowsUrl;
#elif defined(Q_OS_MAC)
    return m.app.macosUrl;
#else
    // Linux: choose based on CPU arch
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();

    const bool isArm64 = arch.contains(QStringLiteral("aarch64"))
                         || arch.contains(QStringLiteral("arm64"));

    if (isArm64) {
        return !m.app.linuxAarch64Url.isEmpty() ? m.app.linuxAarch64Url
                                                : m.app.linuxX86_64Url; // fallback
    }

    return !m.app.linuxX86_64Url.isEmpty() ? m.app.linuxX86_64Url
                                           : m.app.linuxAarch64Url; // fallback
#endif
}

void Updater::showUpdateDialog(QWidget *parent, const UpdateManifest &m)
{
    const QString appCur = QCoreApplication::applicationVersion();
    const QString fwCur = (m_comm ? m_comm->deviceAppVersion() : QString());

    const bool appUpdateAvailable = isVersionNewer(m.app.version, appCur);
    const bool fwUpdateOfferable = !m.firmware.url.trimmed().isEmpty();

    const QString appCurStr = appCur.isEmpty() ? tr("(unknown)") : appCur;
    const QString appNewStr = m.app.version.isEmpty() ? tr("(unknown)") : m.app.version;

    const QString fwCurStr = fwCur.isEmpty() ? tr("(unknown)") : fwCur;
    const QString fwNewStr = m.firmware.version.isEmpty() ? tr("(unknown)") : m.firmware.version;

    QMessageBox box(parent);
    box.setWindowTitle(tr("Updates"));

    // Title text (kept short)
    box.setText(tr("Update availability"));

    // Aligned "App:" / "Firmware:" lines via HTML table.
    // (Note: use &rarr; for a nicer arrow)
    QString info;
    info += QStringLiteral("<table style='border-collapse:collapse;'>");
    info += QStringLiteral("<tr>"
                           "<td style='padding-right:10px; white-space:nowrap;'><b>%1</b></td>"
                           "<td style='white-space:nowrap;'>%2 &nbsp;&rarr;&nbsp; %3</td>"
                           "</tr>")
                .arg(tr("App:"), appCurStr, appNewStr);

    info += QStringLiteral("<tr>"
                           "<td style='padding-right:10px; white-space:nowrap;'><b>%1</b></td>"
                           "<td style='white-space:nowrap;'>%2 &nbsp;&rarr;&nbsp; %3</td>"
                           "</tr>")
                .arg(tr("Firmware:"), fwCurStr, fwNewStr);

    info += QStringLiteral("</table>");

    // Add changelog/notes under the aligned header section
    if (!m.app.changelog.isEmpty()) {
        info += QStringLiteral("<br><br><b>%1</b><br>%2")
                    .arg(tr("App revisions:"),
                         m.app.changelog.toHtmlEscaped().replace("\n", "<br>"));
    }

    if (!m.firmware.notes.isEmpty()) {
        info += QStringLiteral("<br><br><b>%1</b><br>%2")
                    .arg(tr("Firmware revisions:"),
                         m.firmware.notes.toHtmlEscaped().replace("\n", "<br>"));
    }

    box.setInformativeText(info);

    QPushButton *btnApp = nullptr;
    QPushButton *btnFw = nullptr;

    if (appUpdateAvailable) {
        btnApp = box.addButton(tr("Download App Update"), QMessageBox::AcceptRole);
    }

    // Enforce min_app_version for firmware if possible
    bool fwAllowed = fwUpdateOfferable;
    if (fwAllowed && !m.firmware.minAppVersion.isEmpty() && !appCur.isEmpty()) {
        if (QVersionNumber::compare(parseVer(appCur), parseVer(m.firmware.minAppVersion)) < 0) {
            fwAllowed = false;
            box.setInformativeText(
                box.informativeText()
                + QStringLiteral("<br><br><i>%1</i>")
                      .arg(tr("Firmware update requires app version %1 or newer.")
                               .arg(m.firmware.minAppVersion)
                               .toHtmlEscaped()));
        }
    }

    if (fwAllowed) {
        btnFw = box.addButton(tr("Update Firmware"), QMessageBox::ActionRole);
    }

    box.addButton(QMessageBox::Close);
    box.exec();

    if (btnApp && box.clickedButton() == btnApp) {
        const QString url = pickAppDownloadUrlForThisOs(m);
        if (url.isEmpty()) {
            QMessageBox::information(parent, tr("Updates"), tr("No download URL for this OS."));
        } else {
            QDesktopServices::openUrl(QUrl(url));
        }
        return;
    }

    if (btnFw && box.clickedButton() == btnFw) {
        startFirmwareOtaFromUrl(parent, m.firmware.url, m.firmware.version);
        return;
    }

    if (!appUpdateAvailable && !fwUpdateOfferable) {
        QMessageBox::information(parent, tr("Updates"), tr("You are up to date."));
    }
}

void Updater::startFirmwareOtaFromUrl(QWidget *parent,
                                      const QString &url,
                                      const QString &targetVersion)
{
    if (!m_comm) {
        QMessageBox::warning(parent, tr("Firmware Update"), tr("No comm manager available."));
        return;
    }

    if (url.trimmed().isEmpty()) {
        QMessageBox::warning(parent, tr("Firmware Update"), tr("Firmware URL is empty."));
        return;
    }

    m_otaTargetVersion = targetVersion;

    if (!m_otaDialog) {
        m_otaDialog = new QProgressDialog(tr("Starting firmware update..."),
                                          tr("Stop polling"),
                                          0,
                                          100,
                                          parent);
        m_otaDialog->setWindowModality(Qt::WindowModal);
        m_otaDialog->setAutoClose(false);
        m_otaDialog->setAutoReset(false);

        connect(m_otaDialog, &QProgressDialog::canceled, this, [this, parent]() {
            // Current firmware API does not support canceling the on-device OTA;
            // this only stops polling from the PC side.
            stopOtaPolling();

            if (m_comm)
                m_comm->setBackgroundPollingEnabled(true);

            if (m_otaDialog)
                m_otaDialog->hide();

            QMessageBox::information(
                parent,
                tr("Firmware Update"),
                tr("Stopped polling. The device may still be updating in the background."));
        });

    } else {
        m_otaDialog->setParent(parent);
    }

    m_otaDialog->setLabelText(tr("Starting update..."));
    m_otaDialog->setValue(0);
    m_otaDialog->show();

    // Hook comm signals once
    if (!m_commSignalsHooked) {
        m_commSignalsHooked = true;

        connect(m_comm,
                &raypm::RayCommManager::otaStartAck,
                this,
                [this, parent](bool ok, const QString &message) {
                    if (!m_otaDialog)
                        return;

                    if (!ok) {
                        m_otaDialog->hide();
                        stopOtaPolling();

                        if (m_comm)
                            m_comm->setBackgroundPollingEnabled(true);

                        QMessageBox::warning(parent,
                                             tr("Firmware Update"),
                                             tr("Failed to start OTA:\n%1").arg(message));
                        return;
                    }

                    m_otaDialog->setLabelText(tr("Starting download..."));
                });

        connect(m_comm,
                &raypm::RayCommManager::otaStatusReceived,
                this,
                [this, parent](const QString &state,
                               int progressPercent,
                               const QString &message,
                               bool done,
                               bool ok,
                               const QString &rawJson) {
                    Q_UNUSED(rawJson);

                    if (!m_otaDialog)
                        return;

                    const int pct = qBound(0, progressPercent, 100);

                    if (pct > 0) {
                        // Switch to determinate only when we actually have percent
                        if (m_otaDialog->minimum() == 0 && m_otaDialog->maximum() == 0)
                            m_otaDialog->setRange(0, 100);

                        m_otaDialog->setValue(pct);
                    } else {
                        // No meaningful percent yet -> indeterminate
                        m_otaDialog->setRange(0, 0);
                    }

                    const QString st = state.trimmed().toLower();

                    QString label;
                    if (st == QLatin1String("downloading")) {
                        label = tr("Downloading firmware...");
                    } else if (st == QLatin1String("applying")) {
                        label = tr("Applying update... (device may reboot)");
                    } else if (st == QLatin1String("idle")) {
                        label = tr("Finalizing...");
                    } else {
                        label = tr("Updating... (%1)").arg(state);
                    }

                    if (!message.isEmpty())
                        label += tr("\n%1").arg(message);

                    m_otaDialog->setLabelText(label);

                    // Firmware clears OTA state after apply/reboot and returns to "idle" without "done=true".
                    // Treat idle (seen twice) as completion when OTA is in progress.
                    if (m_otaInProgress && st == QLatin1String("idle")) {
                        ++m_idleSeenCount;

                        if (m_idleSeenCount >= 2) {
                            stopOtaPolling();

                            // Show completed for ~2s, then close and resume polling
                            if (m_otaDialog) {
                                m_otaDialog->setRange(0, 100);
                                m_otaDialog->setValue(100);
                                m_otaDialog->setLabelText(tr("Completed."));
                            }

                            QTimer::singleShot(2000, this, [this]() {
                                if (m_comm)
                                    m_comm->setBackgroundPollingEnabled(true);

                                if (m_otaDialog)
                                    m_otaDialog->hide();

                                m_otaInProgress = false;
                            });

                            return;
                        }

                    } else if (m_otaInProgress) {
                        // Reset idle counter if we see any non-idle state again.
                        m_idleSeenCount = 0;
                    }

                    if (done) {
                        stopOtaPolling();
                        m_otaDialog->hide();

                        if (!ok) {
                            // OTA failed: resume polling immediately
                            if (m_comm)
                                m_comm->setBackgroundPollingEnabled(true);

                            QMessageBox::warning(parent,
                                                 tr("Firmware Update"),
                                                 tr("Firmware update failed.\nState: %1\n%2")
                                                     .arg(state, message));
                            return;
                        }

                        // OTA succeeded: device may reboot, so resume polling after a short delay
                        if (m_comm) {
                            QTimer::singleShot(2000, this, [this]() {
                                if (m_comm)
                                    m_comm->setBackgroundPollingEnabled(true);
                            });
                        }

                        QMessageBox::information(
                            parent,
                            tr("Firmware Update"),
                            tr("Firmware update complete.\n"
                               "The device may reboot automatically.\n\n"
                               "After reconnecting, verify firmware version is %1.")
                                .arg(m_otaTargetVersion.isEmpty() ? tr("(new version)")
                                                                  : m_otaTargetVersion));
                    }
                });
    }

    // Pause normal status/telemetry polling while OTA runs
    m_comm->setBackgroundPollingEnabled(false);

    // Start OTA state
    m_otaInProgress = true;
    m_idleSeenCount = 0;
    m_otaTargetVersion = targetVersion;
    m_otaStartedAt.restart();

    // Start OTA (device downloads firmware from URL)
    m_comm->startOtaFromUrl(url);

    beginOtaPolling();
}

void Updater::beginOtaPolling()
{
    if (!m_otaPollTimer.isActive())
        m_otaPollTimer.start();
}

void Updater::stopOtaPolling()
{
    if (m_otaPollTimer.isActive())
        m_otaPollTimer.stop();
}
