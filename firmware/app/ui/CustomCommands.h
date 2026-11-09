// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only
// Protocol pattern adapted from RayEltekController by Ray Faucher (KB1SO).

#pragma once
#include <Arduino.h>

namespace HmiCmd {

// Command “types” for 0x62 frames
constexpr uint8_t TYPE_STATUS   = 'H';   // 0x48
constexpr uint8_t TYPE_TOAST   = 'T';   // 0x54
constexpr uint8_t TYPE_NETWORK = 'N';   // 0x4E
constexpr uint8_t TYPE_SETUP   = 'S';   // 0x53
constexpr uint8_t TYPE_CONFIG_ALARM   = 'A';   // 0x41 (pConfigAlarm)
constexpr uint8_t TYPE_CAL = 'C';  // 0x43 (pCal)

// Page IDs (match your .HMI)
constexpr uint8_t PAGE_STATUS   = 5;
constexpr uint8_t PAGE_SETUP    = 7;      
constexpr uint8_t PAGE_SETUP2   = 17;      
constexpr uint8_t PAGE_NETWORK  = 8;
constexpr uint8_t PAGE_NETWORK2 = 9;
constexpr uint8_t PAGE_CONFIG_ALARM = 18; // 0x12 pConfigAlarm
constexpr uint8_t PAGE_CAL = 10; // 0x0A pCal
constexpr uint8_t STATUS_ALARM_CLR   = 1;   // pStatus: 

// Network command instances (from your printh notes)
constexpr uint8_t NET_RECONNECT   = 1;   // pNetwork: bReconnect
constexpr uint8_t NET_RESET       = 2;   // pNetwork: bResetNet
constexpr uint8_t NET_SHOW_TOKEN  = 3;   // pNetwork: bShowToken
constexpr uint8_t NET_CONNECT     = 4;   // pNetwork2: b2Connect
constexpr uint8_t NET_SCAN_AND_GOTO = 10; // pNetwork: bScan → goto pNetwork2 + scan
constexpr uint8_t NET_REFRESH_SCAN = 18;  // pNetwork2: b2Refresh

constexpr uint8_t SETUP_WIFI_ENABLE = 0x01;      // pSetup: cbWifiEnable
constexpr uint8_t SETUP_BRIGHTNESS    = 0x02;
constexpr uint8_t SETUP_SET_TIME      = 0x03;
constexpr uint8_t SETUP2_APPLY        = 0x04;
constexpr uint8_t SETUP2_BACK         = 0x05;
constexpr uint8_t SETUP2_SYNC_FROM_PREFS = 0x10;

constexpr uint8_t SETUP_ALARM_APPLY  = 0x10;
constexpr uint8_t SETUP_ALARM_BACK   = 0x11;
constexpr uint8_t SETUP_ALARM_SYNC_FROM_PREFS = 0x12;

// Calibration page instances
constexpr uint8_t CAL_CAP_LOW  = 0x01;
constexpr uint8_t CAL_CAP_HIGH = 0x02;
constexpr uint8_t CAL_APPLY = 0x03;
constexpr uint8_t CAL_RESET = 0x04;
constexpr uint8_t CAL_SYNC_FROM_PREFS = 0x05;



// Instances for pSetup actions
enum SetupInstance : uint8_t {
    SETUP_TOGGLE_WIFI = 1,  // cbWifiEnable
    SETUP_OPEN_NET    = 2   // bSetupNet
};

// Quick-pick buttons on pNetwork2 (b2AP1..b2AP4)
// These are optional, but match RayEltek pattern if you add them later.
// constexpr uint8_t NET_PICK_AP1 = 13;
// constexpr uint8_t NET_PICK_AP4 = 16;

} // namespace HmiCmd
