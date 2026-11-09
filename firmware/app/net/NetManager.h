// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only
// Derived from RayEltekController by Ray Faucher (KB1SO).

#pragma once
#include <Arduino.h>
#include <WiFi.h>

enum class NetState { OFFLINE, WIFI_UP, LAN_UP, FALLBACK };

class NetManager {
  public:
  void configure(const char* ssid, const char* pass, bool wifiEnabled);

    void begin();
    void loop();
    NetState state() const;
    IPAddress ip() const;
    IPAddress wifiIp() const;
    IPAddress ethIp() const;
    int wifiRssi() const;
    int rssiToBars(int rssiDbm) const;
};