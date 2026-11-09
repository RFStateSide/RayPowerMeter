// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "app/customsplashscreen.h"
#include "app/instanceselectordialog.h"
#include "app/mainwindow.h"
#include "core/logger.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Load the splash image
    QPixmap pixmap(":/assets/icons/Splash.png");

    // Check if the pixmap loaded correctly
    if (pixmap.isNull()) {
        qWarning() << "Failed to load splash image.";
        return -1;
    }

    // Scale the pixmap to 1.5x its original size
    QPixmap scaledPixmap = pixmap.scaled(pixmap.width() * 1.5,
                                         pixmap.height() * 1.5,
                                         Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);

    // Create the splash screen with the pixmap
    CustomSplashScreen splash(scaledPixmap);

    QApplication::setApplicationName(APP_NAME);
    QApplication::setOrganizationName(APP_COMPANY);
    QApplication::setOrganizationDomain(APP_DOMAIN);
    QApplication::setApplicationVersion(APP_VERSION);
    QApplication::setWindowIcon(QIcon(":/assets/icons/raypowermeter.ico"));

    // Support multiple instances of the application for useres that have more than one Lux device
    QString appBaseName = APP_NAME;
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir settingsDir(appDataPath);
    if (!settingsDir.exists())
        settingsDir.mkpath(".");

    // Search for existing settings files
    int maxInstances = 6;
    QList<int> usedInstances;
    QList<int> runningInstances;

    for (int i = 1; i <= maxInstances; ++i) {
        // Check if QSettings for this instance exists
        QSettings settings(QSettings::IniFormat,
                           QSettings::UserScope,
                           appBaseName + QString::number(i));
        if (!settings.allKeys().isEmpty())
            usedInstances.append(i);

        // Check if lock file exists
        QString lockPath = appDataPath + QDir::separator() + appBaseName + QString::number(i)
                           + ".lock";
        QLockFile checkLock(lockPath);
        if (!checkLock.tryLock(10)) {
            runningInstances.append(i);
            checkLock.unlock(); // Optional: don't hold the lock now
        }
    }

    int selectedInstance = 1;
    if (usedInstances.size() > 1 || runningInstances.size() > 0) {
        InstanceSelectorDialog dlg(maxInstances);
        dlg.setUsedInstances(usedInstances);
        dlg.setRunningInstances(runningInstances);
        if (dlg.exec() != QDialog::Accepted)
            return 0;
        selectedInstance = dlg.selectedInstance();
    }

    QString instanceAppName = appBaseName + QString::number(selectedInstance);
    QCoreApplication::setApplicationName(instanceAppName);
    qDebug() << "Instance App Name:" << instanceAppName;

    // Now do the lock check AFTER user selects instance
    QString lockFilePath = appDataPath + QDir::separator() + instanceAppName + ".lock";
    QLockFile lockFile(lockFilePath);
    lockFile.setStaleLockTime(5000);
    if (!lockFile.tryLock(100)) {
        QMessageBox::warning(nullptr,
                             "Instance Already Running",
                             QString("Instance #%1 of RayPowerMeter is already running.")
                                 .arg(selectedInstance));
        return 1;
    }

    QCoreApplication::setApplicationName(instanceAppName);

    splash.show();

    // Process events to ensure the splash screen is shown
    app.processEvents();
    // Ensure that all potentially blocking tasks occur after the splash screen is shown

    // Initialize the logger
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString logPath = logDir + "/RayPowerMeter.log";
    QDir().mkpath(logDir);
    Logger::init(logPath);

    raypm::MainWindow w(&splash);

    // Finish the splash screen when the main window is ready
    splash.finish(&w);

    w.show();
    w.raise();
    w.activateWindow();

    return app.exec();
}
