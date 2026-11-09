// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Lightweight wrapper around AsyncWebServer that exposes the
// RayPowerMeter control/telemetry JSON API over HTTP.
class RayHttpServer
{
public:
    explicit RayHttpServer(AsyncWebServer &server);

    void begin();
    void update(uint32_t nowMs);

    void setTelemetryEnabled(bool enabled) { _telemetryEnabled = enabled; }
    bool isTelemetryEnabled() const { return _telemetryEnabled; }

    static void addCorsHeaders(AsyncWebServerResponse *resp);

private:
    AsyncWebServer &_server;
    bool _telemetryEnabled = false;
    uint32_t _lastTelemetryMs = 0;
    uint32_t _telemetryIntervalMs = 250;

    void registerRoutes();
    void handleApiAuxConfigGet_();
    void handleApiAuxConfigPost_();
    void handleApiAuxCalibrate2Point_();
};
