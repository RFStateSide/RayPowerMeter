// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "OtaManager.h"
#include "ReleaseDefaults.h"

#if RAYPM_ENABLE_LEGACY_OTA == 1

#include <Update.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// --- OTA URL allow-list ------------------------------------------------------
// Only allow OTA downloads from https://rfstateside.com/firmware/...
// NOTE: This does NOT replace auth; it prevents "point OTA at attacker URL".
// This restricts URL selection. The historical TLS client skips certificate validation.
// Future hardening options:
//  - Require auth token to start OTA
//  - Enable TLS cert validation
//  - Enable signed firmware / secure boot v2
static bool isAllowedOtaUrl(const String &url, String &outReason)
{
    outReason = "";

    // Require HTTPS only (no HTTP).
    if (!url.startsWith("https://"))
    {
        outReason = "OTA URL must use https://";
        return false;
    }

    // Block credentials in URL (e.g., https://user:pass@host/...)
    // Also blocks many "confusable host" tricks.
    if (url.indexOf('@') >= 0)
    {
        outReason = "OTA URL must not contain credentials (userinfo)";
        return false;
    }

    // Very small, strict parse for scheme://host/path...
    // url = https://<host>/<path...>
    const int schemeLen = 8; // "https://"
    const int slash = url.indexOf('/', schemeLen);
    if (slash < 0)
    {
        outReason = "OTA URL missing path";
        return false;
    }

    const String host = url.substring(schemeLen, slash);
    const String path = url.substring(slash); // includes leading '/'

    // Host allow-list (exact match)
    if (host != "rfstateside.com")
    {
        outReason = String("OTA host not allowed: ") + host;
        return false;
    }

    // Path allow-list (prefix match)
    if (!path.startsWith("/firmware/"))
    {
        outReason = "OTA path must start with /firmware/";
        return false;
    }

    // Block obvious path traversal
    if (path.indexOf("..") >= 0)
    {
        outReason = "OTA path must not contain '..'";
        return false;
    }

    return true;
}

#endif

void OtaManager::begin()
{
    // nothing needed
}

bool OtaManager::isBusy() const
{
    portENTER_CRITICAL(&m_mux);
    const bool busy = (m_info.state == State::Downloading || m_info.state == State::Applying);
    portEXIT_CRITICAL(&m_mux);
    return busy;
}

OtaManager::Info OtaManager::getInfo() const
{
    portENTER_CRITICAL(&m_mux);
    Info copy = m_info;
    portEXIT_CRITICAL(&m_mux);
    return copy;
}

static const char *stateToString(OtaManager::State s)
{
    switch (s)
    {
    case OtaManager::State::Idle:
        return "idle";
    case OtaManager::State::Downloading:
        return "downloading";
    case OtaManager::State::Applying:
        return "applying";
    case OtaManager::State::Success:
        return "success";
    case OtaManager::State::Failed:
        return "failed";
    default:
        return "unknown";
    }
}

void OtaManager::toJson(JsonObject dst) const
{
    Info info = getInfo();
    dst["enabled"] = ReleaseDefaults::legacyOtaEnabled;
    dst["state_id"] = static_cast<uint8_t>(info.state);
    dst["state"] = stateToString(info.state);
    dst["url"] = info.url;
    dst["error"] = info.error;
    dst["bytes_written"] = static_cast<uint32_t>(info.bytesWritten);
    dst["total_bytes"] = info.totalBytes;
    dst["percent"] = info.lastPercent;
    dst["started_ms"] = info.startedMs;
    dst["finished_ms"] = info.finishedMs;
}

bool OtaManager::startFromUrl(const String &url)
{
#if RAYPM_ENABLE_LEGACY_OTA != 1
    (void)url;
    return false;
#else
    if (url.isEmpty())
        return false;

    String reason;
    if (!isAllowedOtaUrl(url, reason))
    {
        // Record why it was rejected (helpful for UI/API responses)
        portENTER_CRITICAL(&m_mux);
        m_info = Info{};
        m_info.url = url;
        m_info.state = State::Failed;
        m_info.error = String("OTA URL rejected: ") + reason;
        m_info.startedMs = millis();
        m_info.finishedMs = m_info.startedMs;
        portEXIT_CRITICAL(&m_mux);
        return false;
    }

    if (isBusy())
        return false;

    if (WiFi.status() != WL_CONNECTED)
        return false;

    portENTER_CRITICAL(&m_mux);
    m_info = Info{};
    m_info.url = url;
    m_info.startedMs = millis();
    m_info.state = State::Downloading;
    m_taskUrl = url;
    portEXIT_CRITICAL(&m_mux);

    // Run OTA in background so HTTP/Serial can respond immediately.
    BaseType_t ok = xTaskCreatePinnedToCore(
        &OtaManager::taskTrampoline,
        "ota_task",
        8192,
        this,
        1,
        &m_task,
        1);

    if (ok != pdPASS)
    {
        portENTER_CRITICAL(&m_mux);
        m_info.state = State::Failed;
        m_info.error = "Failed to create OTA task";
        m_info.finishedMs = millis();
        portEXIT_CRITICAL(&m_mux);
        m_task = nullptr;
        return false;
    }

    return true;
#endif
}

#if RAYPM_ENABLE_LEGACY_OTA == 1
void OtaManager::taskTrampoline(void *arg)
{
    auto *self = static_cast<OtaManager *>(arg);
    self->taskBody();
    vTaskDelete(nullptr);
}

void OtaManager::taskBody()
{
    String url;
    portENTER_CRITICAL(&m_mux);
    url = m_taskUrl;
    portEXIT_CRITICAL(&m_mux);

    const bool ok = downloadAndUpdate(url);

    if (ok)
    {
        portENTER_CRITICAL(&m_mux);
        m_info.state = State::Success;
        m_info.finishedMs = millis();
        portEXIT_CRITICAL(&m_mux);

        // Give the Qt app a moment to read status, then reboot into the new slot.
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP.restart();
    }
    else
    {
        portENTER_CRITICAL(&m_mux);
        m_info.state = State::Failed;
        m_info.finishedMs = millis();
        portEXIT_CRITICAL(&m_mux);
    }

    m_task = nullptr;
}

bool OtaManager::downloadAndUpdate(const String &url)
{
    // Re-validate URL (prevents any future code paths from bypassing checks)
    String reason;
    if (!isAllowedOtaUrl(url, reason))
    {
        portENTER_CRITICAL(&m_mux);
        m_info.error = String("OTA URL rejected: ") + reason;
        portEXIT_CRITICAL(&m_mux);
        return false;
    }

    HTTPClient http;

    bool isHttps = url.startsWith("https://");

    WiFiClient *client = nullptr;
    WiFiClientSecure secure;
    WiFiClient plain;

    if (isHttps)
    {
        // Dev-friendly: skip CA verification.
        // Production improvement: setCACert(...) or certificate pinning.
        secure.setInsecure();
        client = &secure;
    }
    else
    {
        client = &plain;
    }

    if (!http.begin(*client, url))
    {
        portENTER_CRITICAL(&m_mux);
        m_info.error = "HTTP begin() failed";
        portEXIT_CRITICAL(&m_mux);
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        portENTER_CRITICAL(&m_mux);
        m_info.error = String("HTTP GET failed: ") + code;
        portEXIT_CRITICAL(&m_mux);
        http.end();
        return false;
    }

    const int total = http.getSize();
    portENTER_CRITICAL(&m_mux);
    m_info.totalBytes = total;
    portEXIT_CRITICAL(&m_mux);

    // Switch to "Applying" once we start writing flash.
    portENTER_CRITICAL(&m_mux);
    m_info.state = State::Applying;
    portEXIT_CRITICAL(&m_mux);

    if (!Update.begin((total > 0) ? (size_t)total : UPDATE_SIZE_UNKNOWN))
    {
        portENTER_CRITICAL(&m_mux);
        m_info.error = String("Update.begin failed: ") + Update.errorString();
        portEXIT_CRITICAL(&m_mux);
        http.end();
        return false;
    }

    WiFiClient &stream = http.getStream();
    uint8_t buf[2048];
    size_t written = 0;

    while (http.connected())
    {
        int avail = stream.available();
        if (avail <= 0)
        {
            delay(1);
            continue;
        }

        int toRead = avail;
        if (toRead > (int)sizeof(buf))
            toRead = sizeof(buf);

        int r = stream.readBytes(buf, toRead);
        if (r <= 0)
            break;

        size_t w = Update.write(buf, (size_t)r);
        if (w != (size_t)r)
        {
            portENTER_CRITICAL(&m_mux);
            m_info.error = String("Update.write failed: ") + Update.errorString();
            portEXIT_CRITICAL(&m_mux);
            Update.abort();
            http.end();
            return false;
        }

        written += w;

        uint8_t percent = 0;
        if (total > 0)
            percent = (uint8_t)((written * 100ULL) / (uint64_t)total);

        portENTER_CRITICAL(&m_mux);
        m_info.bytesWritten = written;
        m_info.lastPercent = percent;
        portEXIT_CRITICAL(&m_mux);

        if (total > 0 && (int)written >= total)
            break;
    }

    if (!Update.end(true))
    {
        portENTER_CRITICAL(&m_mux);
        m_info.error = String("Update.end failed: ") + Update.errorString();
        portEXIT_CRITICAL(&m_mux);
        http.end();
        return false;
    }

    http.end();
    return true;
}
#endif
