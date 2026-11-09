// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/AppSettings.h"

#include <QApplication>
#include <QSettings>

namespace raypm {

static constexpr const char *kGroupConnection = "Connection";
static constexpr const char *kGroupUnits = "Units";
static constexpr const char *kGroupAppearance = "Appearance";
static constexpr const char *kGroupLayout = "Layout";
static constexpr const char *kGroupCal = "Calibration";
static constexpr const char *kGroupHamlib = "Hamlib";
static constexpr const char *kGroupUpdates = "Updates";

AppSettings SettingsManager::load()
{
    AppSettings s;

    // APP_COMPANY / APP_NAME come from CMake add_compile_definitions
    QSettings settings(APP_COMPANY, QCoreApplication::applicationName());

    // ---------------------- Connection ----------------------------
    settings.beginGroup(kGroupConnection);
    const int transportInt = settings.value("Transport", static_cast<int>(s.transport)).toInt();
    s.transport = static_cast<TransportType>(transportInt);

    s.serial.portName = settings.value("SerialPortName", s.serial.portName).toString();
    s.serial.baudRate = settings.value("SerialBaudRate", s.serial.baudRate).toInt();

    s.http.host = settings.value("HttpHost", "raypowermeter.local").toString();
    s.http.port = settings.value("HttpPort", s.http.port).toInt();
    s.http.basePath = settings.value("HttpBasePath", s.http.basePath).toString();
    s.http.pollIntervalMs = settings.value("PollIntervalMs", s.http.pollIntervalMs).toInt();
    s.http.enableTls = settings.value("EnableTls", s.http.enableTls).toBool();
    s.http.connectionModeIndex = settings.value("ConnectionModeIndex", s.http.connectionModeIndex)
                                     .toInt();
    settings.endGroup();

    // ------------------------ Units -------------------------------
    settings.beginGroup(kGroupUnits);
    s.unitsTempIndex = settings.value("UnitsTempIndex", s.unitsTempIndex).toInt();
    s.unitsPowerIndex = settings.value("UnitsPowerIndex", s.unitsPowerIndex).toInt();
    s.unitsReflectedIndex = settings.value("UnitsReflectedIndex", s.unitsReflectedIndex).toInt();
    settings.endGroup();

    // ---------------------- Appearance ----------------------------
    settings.beginGroup(kGroupAppearance);
    const int themeInt = settings.value("Theme", static_cast<int>(s.theme)).toInt();
    s.theme = static_cast<AppTheme>(themeInt);
    s.appScale = settings.value("AppScale", s.appScale).toDouble();
    settings.endGroup();

    // ---------------------- Layout ----------------------------
    settings.beginGroup(kGroupLayout);
    s.startupLayoutName = settings.value("StartupLayoutName", s.startupLayoutName).toString();
    s.layoutGridColumns = settings.value("LayoutGridColumns", s.layoutGridColumns).toInt();
    settings.endGroup();

    // ---------------------- Calibration ---------------------------
    settings.beginGroup(kGroupCal);
    s.calPowerFactor = settings.value("CalPowerFactor", s.calPowerFactor).toDouble();
    s.calCurrentFactor = settings.value("CalCurrentFactor", s.calCurrentFactor).toDouble();
    settings.endGroup();

    // ---------------------- Hamlib ----------------------------
    settings.beginGroup(kGroupHamlib);
    s.hamlib.enabled = settings.value("Enabled", s.hamlib.enabled).toBool();
    s.hamlib.binPath = settings.value("BinPath", s.hamlib.binPath).toString();

    s.hamlib.network.host = settings.value("Host", s.hamlib.network.host).toString();
    s.hamlib.network.port = settings.value("Port", s.hamlib.network.port).toInt();

    s.hamlib.serial.rigModelId = settings.value("RigModelId", s.hamlib.serial.rigModelId).toInt();
    s.hamlib.serial.portName = settings.value("SerialPortName", s.hamlib.serial.portName).toString();
    s.hamlib.serial.baudRate = settings.value("SerialBaudRate", s.hamlib.serial.baudRate).toInt();
    s.hamlib.serial.dataBits = settings.value("SerialDataBits", s.hamlib.serial.dataBits).toInt();
    s.hamlib.serial.stopBits = settings.value("SerialStopBits", s.hamlib.serial.stopBits).toInt();
    s.hamlib.serial.parity = settings.value("SerialParity", s.hamlib.serial.parity).toInt();
    s.hamlib.serial.flowControl = settings.value("SerialFlowControl", s.hamlib.serial.flowControl)
                                      .toInt();
    s.hamlib.serial.tcpPort = settings.value("RigctldTcpPort", s.hamlib.serial.tcpPort).toInt();

    s.hamlib.lastTestOk = settings.value("LastTestOk", s.hamlib.lastTestOk).toBool();
    s.hamlib.connectionModeIndex
        = settings.value("ConnectionModeIndex", s.hamlib.connectionModeIndex).toInt();

    // Safety gate: only load AutoStartRigctld if lastTestOk is true.
    s.hamlib.serial.autoStartRigctld = settings.value("AutoStartRigctld", false).toBool()
                                       && s.hamlib.lastTestOk;
    settings.endGroup();

    // ---------------------- Updates ----------------------------
    settings.beginGroup(kGroupUpdates);
    s.updateManifestUrl = settings.value("ManifestUrl", s.updateManifestUrl).toString();
    s.firmwarePlatformKey = settings.value("FirmwarePlatformKey", s.firmwarePlatformKey).toString();
    settings.endGroup();

    return s;
}

void SettingsManager::save(const AppSettings &s)
{
    QSettings settings(APP_COMPANY, QCoreApplication::applicationName());

    // ---------------------- Connection ----------------------------
    settings.beginGroup(kGroupConnection);
    settings.setValue("Transport", static_cast<int>(s.transport));
    settings.setValue("SerialPortName", s.serial.portName);
    settings.setValue("SerialBaudRate", s.serial.baudRate);

    settings.setValue("HttpHost", s.http.host);
    settings.setValue("HttpPort", s.http.port);
    settings.setValue("HttpBasePath", s.http.basePath);
    settings.setValue("PollIntervalMs", s.http.pollIntervalMs);
    settings.setValue("EnableTls", s.http.enableTls);
    settings.setValue("ConnectionModeIndex", s.http.connectionModeIndex);
    settings.endGroup();

    // ------------------------ Units -------------------------------
    settings.beginGroup(kGroupUnits);
    settings.setValue("UnitsTempIndex", s.unitsTempIndex);
    settings.setValue("UnitsPowerIndex", s.unitsPowerIndex);
    settings.setValue("UnitsReflectedIndex", s.unitsReflectedIndex);
    settings.endGroup();

    // ---------------------- Appearance ----------------------------
    settings.beginGroup(kGroupAppearance);
    settings.setValue("Theme", static_cast<int>(s.theme));
    settings.setValue("AppScale", s.appScale);
    settings.endGroup();

    // ---------------------- Layout ----------------------------
    settings.beginGroup(kGroupLayout);
    settings.setValue("StartupLayoutName", s.startupLayoutName);
    settings.setValue("LayoutGridColumns", s.layoutGridColumns);
    settings.endGroup();

    // ---------------------- Calibration ---------------------------
    settings.beginGroup(kGroupCal);
    settings.setValue("CalPowerFactor", s.calPowerFactor);
    settings.setValue("CalCurrentFactor", s.calCurrentFactor);
    settings.endGroup();

    // ---------------------- Hamlib ----------------------------
    settings.beginGroup(kGroupHamlib);

    settings.setValue("Enabled", s.hamlib.enabled);
    settings.setValue("BinPath", s.hamlib.binPath);

    settings.setValue("Host", s.hamlib.network.host);
    settings.setValue("Port", s.hamlib.network.port);

    settings.setValue("RigModelId", s.hamlib.serial.rigModelId);
    settings.setValue("SerialPortName", s.hamlib.serial.portName);
    settings.setValue("SerialBaudRate", s.hamlib.serial.baudRate);
    settings.setValue("SerialDataBits", s.hamlib.serial.dataBits);
    settings.setValue("SerialStopBits", s.hamlib.serial.stopBits);
    settings.setValue("SerialParity", s.hamlib.serial.parity);
    settings.setValue("SerialFlowControl", s.hamlib.serial.flowControl);
    settings.setValue("RigctldTcpPort", s.hamlib.serial.tcpPort);

    settings.setValue("LastTestOk", s.hamlib.lastTestOk);
    settings.setValue("ConnectionModeIndex", s.hamlib.connectionModeIndex);

    // Safety gate: never persist auto-start unless a prior test succeeded.
    settings.setValue("AutoStartRigctld",
                      (s.hamlib.lastTestOk ? s.hamlib.serial.autoStartRigctld : false));

    settings.endGroup();

    // ---------------------- Updates ----------------------------
    settings.beginGroup(kGroupUpdates);
    settings.setValue("ManifestUrl", s.updateManifestUrl);
    settings.setValue("FirmwarePlatformKey", s.firmwarePlatformKey);
    settings.endGroup();
}

} // namespace raypm
