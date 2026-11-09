// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QString>

namespace raypm {

enum class TransportType { Serial, Http };

enum class AppTheme { Light, Dark };

struct SerialSettings
{
    QString portName;
    int baudRate = 115200;
};

struct HttpSettings
{
    QString host;     // e.g. "192.168.4.1"
    int port = 80;    // e.g. 80, 8080, 5000
    QString basePath; // e.g. "/api" (optional)
    int pollIntervalMs = 100;
    bool enableTls = false;

    // 0 = Disabled, 1 = HTTP (LAN), 2 = HTTP (Access Point)
    int connectionModeIndex = 0;
};

struct HamlibSerialSettings
{
    // Hamlib rig model ID (from rigctl -l). -1 means not selected.
    int rigModelId = -1;

    // Serial CAT port settings (used when starting rigctld)
    QString portName;
    int baudRate = 19200;
    int dataBits = 8;    // 5..8 (Qt uses QSerialPort::DataBits)
    int stopBits = 1;    // 1 or 2
    int parity = 0;      // 0=None, 2=Even, 3=Odd, etc (Qt uses QSerialPort::Parity)
    int flowControl = 0; // 0=None, 1=RTS/CTS, 2=XON/XOFF (Qt uses QSerialPort::FlowControl)

    // TCP port rigctld listens on (default 4532)
    int tcpPort = 4532;

    // Only allowed to be true if lastTestOk is true
    bool autoStartRigctld = false;
};

struct HamlibNetworkSettings
{
    QString host = "127.0.0.1";
    int port = 4532;
};

struct HamlibSettings
{
    bool enabled = false;

    // Optional: if Hamlib isn't on PATH, users can point to the installed bin folder.
    // Example: C:\Program Files\hamlib-w64-4.6.5\bin
    QString binPath;

    // 0 = Serial tab (local rigctld we start/configure)
    // 1 = Network tab (connect to existing rigctld host:port)
    int connectionModeIndex = 0;

    HamlibSerialSettings serial;
    HamlibNetworkSettings network;

    // Set only after a successful Test (connect + basic rigctld command OK).
    bool lastTestOk = false;
};

struct AppSettings
{
    TransportType transport = TransportType::Serial;
    SerialSettings serial;
    HttpSettings http;

    HamlibSettings hamlib;
    // Units (indices into the combo boxes)
    int unitsTempIndex = 0;
    int unitsPowerIndex = 0;
    int unitsReflectedIndex = 0;

    // Appearance
    AppTheme theme = AppTheme::Dark;
    double appScale = 1.0; // 1.0 = 100%

    // Layout
    QString startupLayoutName; // filename in Layouts folder; empty = built-in default
    int layoutGridColumns = 2;

    // Calibration
    double calPowerFactor = 0.00305;
    double calCurrentFactor = 0.135;

    // Updates
    QString updateManifestUrl = QStringLiteral(
        "https://rfstateside.com/updates/raypowermeter.json");

    // Firmware platform key used to pick the correct download URL in the manifest
    // (matches your manifest schema; example: "esp32s3")
    QString firmwarePlatformKey = QStringLiteral("esp32s3");
};

class SettingsManager
{
public:
    static AppSettings load();
    static void save(const AppSettings &settings);
};

} // namespace raypm
