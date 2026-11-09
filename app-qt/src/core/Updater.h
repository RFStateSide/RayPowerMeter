// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QDesktopServices>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QProgressDialog>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

class QNetworkReply;
class QProgressDialog;
class QWidget;

namespace raypm {
class RayCommManager;
struct AppSettings;
} // namespace raypm

// ---------------- Manifest model ----------------

struct UpdateManifest
{
    QString product;
    int manifestVersion = 0;
    QString timestamp;

    struct AppSection
    {
        QString version;
        QString changelog;
        QString windowsUrl;
        QString macosUrl;
        QString linuxX86_64Url;
        QString linuxAarch64Url;
    } app;

    struct FirmwareSection
    {
        QString version;
        QString build;
        QString minAppVersion;
        qint64 size = 0;
        QString sha256;
        QString url;
        QString notes;
    } firmware;

    bool isValid() const { return !product.isEmpty() && manifestVersion > 0; }
};

// ---------------- Updater ----------------

class Updater : public QObject
{
    Q_OBJECT
public:
    explicit Updater(raypm::RayCommManager *comm,
                     raypm::AppSettings *settings,
                     QObject *parent = nullptr);

    // Call this from MainWindow::on_actionCheck_for_Updates_triggered()
    void checkForUpdatesInteractive(QWidget *parent);

private:
    // Manifest
    void fetchManifest();
    bool parseManifestJson(const QByteArray &raw, UpdateManifest &out, QString &error) const;

    // UI actions
    void showUpdateDialog(QWidget *parent, const UpdateManifest &m);
    QString pickAppDownloadUrlForThisOs(const UpdateManifest &m) const;

    // Firmware OTA via URL (device downloads firmware)
    void startFirmwareOtaFromUrl(QWidget *parent, const QString &url, const QString &targetVersion);
    void beginOtaPolling();
    void stopOtaPolling();

    // Version compare helpers
    static QVersionNumber parseVer(const QString &s);
    static bool isVersionNewer(const QString &latest, const QString &current);

private:
    raypm::RayCommManager *m_comm = nullptr;
    raypm::AppSettings *m_settings = nullptr;

    // Network
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;

    // UI
    QPointer<QProgressDialog> m_busyDialog; // manifest download busy
    QPointer<QProgressDialog> m_otaDialog;  // firmware ota progress

    // OTA state
    QTimer m_otaPollTimer;
    QString m_otaTargetVersion;
    bool m_commSignalsHooked = false;

    // Last manifest
    UpdateManifest m_lastManifest;

    bool m_otaInProgress = false;
    QElapsedTimer m_otaStartedAt;
    int m_idleSeenCount = 0;

    //void finishOtaUiSuccess(const QString &message);
    //void finishOtaUiFailed(const QString &message);
};
