// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QDialog>
#include <QFont>
#include <QIcon>
#include <QList>
#include <QNetworkAccessManager>
#include <QSerialPort>
#include <QString>
#include <QTimer>

#include "core/AppSettings.h"

// forward declarations
class QNetworkReply;
class QProcess;

namespace Ui {
class DialogSetup;
}

// Note: this dialog is intentionally lightweight. All persistent state
// lives in raypm::AppSettings via raypm::SettingsManager.

class DialogSetup : public QDialog
{
    Q_OBJECT

public:
    explicit DialogSetup(QWidget *parent = nullptr);
    ~DialogSetup() override;

signals:
    void ReloadSettings();
    void ShowStatusBarMessage(const QString &message, int timeoutMs);
    void scaleFactorChanged(double newScale);

private slots:
    void on_pushButtonPortRefresh_clicked();
    void on_pushButtonLoad_clicked();
    void on_pushButtonSave_clicked();
    void on_pushButtonClose_clicked();
    void on_pushButtonFont_clicked();
    void on_horizontalSliderAppScaleFactor_valueChanged(int value);
    void on_comboBoxWebSocketMode_currentIndexChanged(int index);

    void on_pushButtonTestConnection_clicked();
    void handleTestHttpFinished(QNetworkReply *reply);
    void handleTestTimeout();
    void handleTestSerialReadyRead();

    // Hamlib
    void on_pushButtonHamlibRefreshRigList_clicked();
    void on_pushButtonHamlibBrowseBin_clicked();
    void on_pushButtonHamlibTest_clicked();
    void on_pushButtonHamlibStart_clicked();
    void on_pushButtonHamlibStop_clicked();
    void on_checkBoxHamlibEnabled_toggled(bool checked);

private:
    void initializeGui();
    void initializeHamlibUi();
    void applyHamlibFromSettings(const raypm::AppSettings &s);
    void saveHamlibToSettings(raypm::AppSettings &s) const;
    void refreshHamlibRigList();
    void refreshHamlibSerialPorts(const QString &preferredPort);
    void updateHamlibUiState();
    QString hamlibExePath(const QString &exeName) const;
    bool startRigctldForSerial(QString *errorOut);
    void stopRigctld();

    void applyFromSettings(const raypm::AppSettings &s);
    void refreshStartupLayoutCombo(const QString &selectedName);
    raypm::AppSettings saveSettings() const;
    void reloadSerialPorts(const QString &preferredPort);

    enum class TestTransport { None, Http, Serial };

    Ui::DialogSetup *ui = nullptr;

    QList<QString> unitsTemp = {"Celsius (°C)", "Fahrenheit (°F)"};
    QList<QString> unitsPower = {"Watts (W)", "Decibels (dBm)"};
    QList<QString> unitsReflected = {"SWR", "Return Loss (dB)"};
    QList<QString> appTheme = {"Light", "Dark"};
    QList<QString> connectionMode = {"COMM (Serial)", "HTTP (LAN)", "HTTP (Access Point)"};

    QString m_fontFamily = "Segoe UI";
    int m_fontSize = 10;
    int m_fontWeight = QFont::Normal;
    bool m_fontItalic = false;

    double m_scaleFactor = 1.0;
    QIcon m_icOk, m_icWarn, m_icStop;

    // Test-connection helpers
    QNetworkAccessManager m_netManager;
    QSerialPort m_testSerial;
    QTimer m_testTimeout;
    TestTransport m_testTransport = TestTransport::None;
    QByteArray m_testSerialBuffer;

    // Hamlib / rigctld
    QProcess *m_rigctldProcess = nullptr;
    bool m_rigctldStartedByDialog = false;
    bool m_hamlibLastTestOk = false;
    bool m_applyingHamlibSettings = false;
};
