// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "core/Constants.h"

// Forward declarations of classes/structs owned elsewhere in the firmware
class NetManager;
class RayProtectionController;
struct RayProtectionConfig;
struct SystemStatus;

// Extern global objects we reference (declared in main.cpp)
extern NetManager             gNet;
extern RayProtectionController g_protection;
extern RayProtectionConfig     g_protCfg;
extern SystemStatus            gStatus;

// -----------------------------------------------------------------------------
// RayCommServer
// Serial JSON API for the RayPowerMeter.
//
// Responsibilities:
//   - Receive JSON commands over USB/Serial
//   - Dispatch commands to existing controllers
//   - Return JSON responses (status, config, results)
//   - Mirrors the existing HTTP API, but over Serial
//
// This class does NOT own any subsystem logic.
// It only routes and encodes/decodes commands to/from JSON.
// -----------------------------------------------------------------------------
class RayCommServer
{
public:
    explicit RayCommServer(Stream &serial);

    // Initialize the server (call from setup())
    void begin();

    // Poll for serial data (call from loop(now))
    void update(uint32_t nowMs);

private:
    Stream &_serial;
    String  _rxLine;
    bool _rxLineOverflow = false;

    uint32_t _lastTelemetryMs     = 0;
    uint32_t _telemetryIntervalMs = 0;
    bool     _telemetryEnabled    = false;

    // one-shot notification flags
    bool _modulationReadyNotified = false;
    // Build a status JSON identical to what /api/status returns
    void buildStatusJson(JsonDocument &doc);

    // Helpers to send JSON responses
    template <typename TDoc>
    void sendJson(TDoc &doc);

    void sendError(const char *msg, const char *code = "error");

    // Handle parsing and dispatch
    void handleSerialRx();
    void handleCommand(const JsonDocument &doc);

    void sendTelemetrySnapshot(uint32_t nowMs);

};


// Inline template implementation
template <typename TDoc>
inline void RayCommServer::sendJson(TDoc &doc)
{
    String out;
    serializeJson(doc, out);
    _serial.println(out);
}
