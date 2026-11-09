// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class OtaManager
{
public:
    enum class State : uint8_t { Idle=0, Downloading, Applying, Success, Failed };

    struct Info {
        State    state = State::Idle;
        String   url;
        String   error;
        uint32_t startedMs = 0;
        uint32_t finishedMs = 0;
        size_t   bytesWritten = 0;
        int32_t  totalBytes = -1;   // -1 unknown
        uint8_t  lastPercent = 0;
    };

    void begin();
    bool isBusy() const;
    Info getInfo() const;

    // Starts a background task that downloads + flashes the new firmware.
    // Returns false when OTA is disabled or cannot start.
    bool startFromUrl(const String &url);

    void toJson(JsonObject dst) const;

private:
    static void taskTrampoline(void *arg);
    void taskBody();
    bool downloadAndUpdate(const String &url);

    mutable portMUX_TYPE m_mux = portMUX_INITIALIZER_UNLOCKED;
    Info m_info;

    TaskHandle_t m_task = nullptr;
    String m_taskUrl;
};
