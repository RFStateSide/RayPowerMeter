// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only
// Adapted from RayEltekController by Ray Faucher (KB1SO).

#include "core/ReleaseDefaults.h"
/**
 * @file NetManager.h
 * @brief Lightweight, non-blocking network state machine for ESP32-S3.
 *
 * Responsibilities
 *  - Prefer Ethernet when link+IP are available; otherwise use Wi-Fi STA.
 *  - Never block inside begin()/loop(); all connects/retries are time-sliced.
 *  - Provide read-only status (NetState, current IPs, RSSI).
 *
 * Platform & deps
 *  - Arduino core for ESP32-S3.
 *  - <WiFi.h> always; <ETH.h> is optional (guarded by __has_include).
 *
 * Configuration
 *  - Compile-time Wi-Fi credentials (recommended for bench):
 *      -DWIFI_SSID="ssid"  -DWIFI_PASS="pass"
 *  - Retry cadence: WIFI_RETRY_MS (tunable in .cpp).
 *
 * Concurrency model
 *  - Single threaded (Arduino loop). No mutexes used/needed.
 *  - All state transitions occur in loop().
 *
 * Public API (summary)
 *  - void begin();           // starts Wi-Fi STA (non-blocking)
 *  - void loop();            // runs state machine; call every iteration
 *  - NetState state() const; // OFFLINE | WIFI_UP | LAN_UP | FALLBACK
 *  - IPAddress ip() const;   // active transport IP or 0.0.0.0
 *  - IPAddress wifiIp() const, ethIp() const;
 *  - int wifiRssi() const;   // RSSI when Wi-Fi connected
 *
 * State machine (simplified)
 *          +-----------+           Ethernet link up + IP
 *   start  | OFFLINE   |----------------------------------+
 *  ------->|           |                                  |
 *          +-----------+                                  v
 *           |  Wi-Fi retry (non-blocking)              +------+
 *           |----------------------------------------->|LAN_UP|
 *           |  Wi-Fi connected + IP                    +------+
 *           v                                               ^
 *         +--------+                                        |
 *         |WIFI_UP |----------------------------------------+
 *         +--------+  (Ethernet link appears)
 *
 * Error handling
 *  - Missing ETH.h: LAN paths compile out cleanly.
 *  - No creds: remains OFFLINE; still safe to call ip()/state().
 *
 * Gotchas / tips
 *  - Call loop() every iteration; avoid long blocking work elsewhere.
 *  - If you actively manage Wi-Fi elsewhere, keep that logic consistent.
 *  - If you initialize Ethernet yourself, NetManager will just detect it.
 *
 * Maintenance checklist
 *  [ ] Confirm compile without ETH.h (CI matrix target).
 *  [ ] Tune WIFI_RETRY_MS if APs are slow to respond.
 *  [ ] Keep API additive; NetManager is used by ApiServer and higher layers.
 *  [ ] Add unit/integration tests for state transitions when feasible (mock).
 *
 * @author  Ray Faucher, RFStateSide, LLC
 * @since   0.1
 * @version next
 */

// firmware/app/net/NetManager.cpp
#include "NetManager.h"

#include <Arduino.h>
#include <WiFi.h>

#if __has_include(<ETH.h>)
#include <ETH.h>
#define NETMGR_HAS_ETH 1
#else
#define NETMGR_HAS_ETH 0
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

static const uint32_t WIFI_RETRY_MS = 5000;

// Current network state
static NetState g_state = NetState::OFFLINE;
static uint32_t g_nextWifiAttemptMs = 0;

// Runtime Wi-Fi configuration (overridable via configure())
static char g_wifiSsid[33] = {0};
static char g_wifiPass[65] = {0};
static bool g_wifiEnabled = false;

// Small helper so we only pull in defaults once
static void initWifiDefaultsIfNeeded()
{
    if (g_wifiSsid[0] != '\0')
    {
        // Already initialized (configure() or previous call)
        return;
    }

    // Seed from compile-time macros as a fallback
    strncpy(g_wifiSsid, WIFI_SSID, sizeof(g_wifiSsid) - 1);
    strncpy(g_wifiPass, WIFI_PASS, sizeof(g_wifiPass) - 1);
    g_wifiEnabled = (g_wifiSsid[0] != '\0');
}

static inline bool hasIp(const IPAddress &ip)
{
    return static_cast<uint32_t>(ip) != 0; // 0.0.0.0 => 0
}

static bool ethernetUp()
{
#if NETMGR_HAS_ETH
    return ETH.linkUp() && hasIp(ETH.localIP());
#else
    return false;
#endif
}

static bool wifiUp()
{
    return WiFi.isConnected() && hasIp(WiFi.localIP());
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void NetManager::configure(const char *ssid, const char *pass, bool wifiEnabled)
{
    // Start from defaults
    initWifiDefaultsIfNeeded();

    // SSID: prefs override macros when non-empty
    if (ssid && ssid[0] != '\0')
    {
        strncpy(g_wifiSsid, ssid, sizeof(g_wifiSsid) - 1);
        g_wifiSsid[sizeof(g_wifiSsid) - 1] = '\0';
    }

    // Password
    if (pass && pass[0] != '\0')
    {
        strncpy(g_wifiPass, pass, sizeof(g_wifiPass) - 1);
        g_wifiPass[sizeof(g_wifiPass) - 1] = '\0';
    }

    g_wifiEnabled = wifiEnabled && (g_wifiSsid[0] != '\0');
}

void NetManager::begin()
{
    if (!ReleaseDefaults::labNetworkEnabled)
        return;

    g_state = NetState::OFFLINE;

    initWifiDefaultsIfNeeded();

    // If Wi-Fi is disabled, shut it down cleanly and exit.
    if (!g_wifiEnabled || g_wifiSsid[0] == '\0')
    {
        Serial.println("[Net] Wi-Fi disabled or no SSID; staying OFFLINE");

        WiFi.persistent(false);

        // Ensure Wi-Fi is initialized enough to allow disconnect() cleanly
        if (WiFi.getMode() != WIFI_MODE_STA)
            WiFi.mode(WIFI_MODE_STA);
        delay(20);

        // Turn Wi-Fi OFF (wifioff=true) and erase config (eraseap=true)
        WiFi.disconnect(true, true);
        delay(20);

        WiFi.mode(WIFI_MODE_NULL);
        return;
    }

    // Start Wi-Fi in STA mode
    WiFi.persistent(false); // don't write creds to flash

    if (WiFi.getMode() != WIFI_MODE_STA)
        WiFi.mode(WIFI_MODE_STA);

    delay(50);

    // IMPORTANT:
    // disconnect(wifioff, eraseap)
    // We want to erase old STA config WITHOUT turning Wi-Fi off.
    WiFi.disconnect(false, true);
    delay(50);

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.printf("[Net] Connecting to Wi-Fi SSID=\"%s\"...\n", g_wifiSsid);
        WiFi.begin(g_wifiSsid, g_wifiPass);
        g_nextWifiAttemptMs = millis() + WIFI_RETRY_MS;
    }
}

void NetManager::loop()
{
    if (!ReleaseDefaults::labNetworkEnabled)
        return;

    // Ethernet wins when present
    if (ethernetUp())
    {
        g_state = NetState::LAN_UP;
        return;
    }
    if (wifiUp())
    {
        g_state = NetState::WIFI_UP;
        return;
    }

    g_state = NetState::OFFLINE;

    if (!g_wifiEnabled || g_wifiSsid[0] == '\0')
    {
        return; // Wi-Fi disabled in prefs
    }

    const uint32_t now = millis();
    if (now >= g_nextWifiAttemptMs && WiFi.status() != WL_CONNECTED)
    {
        Serial.printf("[Net] Wi-Fi retry to SSID=\"%s\"...\n", g_wifiSsid);
        WiFi.disconnect();
        WiFi.begin(g_wifiSsid, g_wifiPass);
        g_nextWifiAttemptMs = now + WIFI_RETRY_MS;
    }
}

NetState NetManager::state() const
{
    return g_state;
}

IPAddress NetManager::ip() const
{
#if NETMGR_HAS_ETH
    if (ethernetUp())
        return ETH.localIP();
#endif
    if (wifiUp())
        return WiFi.localIP();
    return IPAddress(); // 0.0.0.0
}

IPAddress NetManager::wifiIp() const
{
    return wifiUp() ? WiFi.localIP() : IPAddress();
}

IPAddress NetManager::ethIp() const
{
#if NETMGR_HAS_ETH
    return ethernetUp() ? ETH.localIP() : IPAddress();
#else
    return IPAddress();
#endif
}

int NetManager::wifiRssi() const
{
    return wifiUp() ? WiFi.RSSI() : 0;
}

int NetManager::rssiToBars(int rssiDbm) const
{
    if (rssiDbm <= -90)
        return 0;
    if (rssiDbm <= -80)
        return 1;
    if (rssiDbm <= -70)
        return 2;
    if (rssiDbm <= -60)
        return 3;
    return 4;
}