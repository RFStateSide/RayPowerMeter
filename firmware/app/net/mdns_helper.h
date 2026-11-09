// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

// mdns_helper.h
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

inline void mdnsEnsureStarted(const char* host) {
  static bool started = false;

  const bool haveIp =
      (WiFi.isConnected() && (uint32_t)WiFi.localIP() != 0);
  if (!started && haveIp) {
    if (!MDNS.begin(host)) {
      Serial.println("[mDNS] start failed");
      return;
    }
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] %s.local ready (%s)\n",
                  host, WiFi.localIP().toString().c_str());
    started = true;
  }

  // Optional: tear down if we lose IP (keeps logs clean on flaps)
  if (started && !haveIp) {
    MDNS.end();
    started = false;
    Serial.println("[mDNS] stopped (no IP)");
  }
}
