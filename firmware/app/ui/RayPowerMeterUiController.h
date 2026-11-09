// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only
// RayEltekController component names by Ray Faucher (KB1SO) identified below.

#pragma once

#include <Arduino.h>
#include <vector>
#include <WiFi.h>

#include "core/Constants.h"
#include "ui/NextionDisplay.h"
#include "coupler/rfpowersensor.h"
#include "core/RayProtectionController.h"
#include "core/AppState.h"
#include "core/SystemStatus.h"
#include "core/userprefs.h" 

// ============================================================================
// RayPowerMeter UI → Nextion mapping
// Central place for all HMI page IDs and component names so we avoid
// magic strings scattered through the code.
// ============================================================================

// Page IDs (match RayPowerMeter.HMI)
#define HMI_PAGE_SPLASH 0
#define HMI_PAGE_KEYBRD_A 1
#define HMI_PAGE_KEYBRD_B 2
#define HMI_PAGE_KEYBRD_BB 3
#define HMI_PAGE_MENU 4
#define HMI_PAGE_STATUS 5 // pStatus - main measurement page
#define HMI_PAGE_DETAIL 6
#define HMI_PAGE_SETUP 7
#define HMI_PAGE_NETWORK 8  // pNetwork
#define HMI_PAGE_NETWORK2 9 // pNetwork2
#define HMI_PAGE_CAL 10
#define HMI_PAGE_GRAPH 11
#define HMI_PAGE_ABOUT 12
#define HMI_PAGE_TOAST 13 // pToast
#define HMI_PAGE_SERIAL_CONFIG 14
#define HMI_PAGE_PORT_LOG 15
#define HMI_PAGE_DIAG 16
#define HMI_PAGE_SETUP1 17


// Number of coupler rows supported by the UI
static constexpr size_t HMI_NUM_COUPLERS = 4;

// ---- pStatus row components -------------------------------------------------
// Row 1
#define HMI_COMP_C1_BUTTON "bC1"
#define HMI_COMP_C1_LBL_V "lC1V"
#define HMI_COMP_C1_FWD "tvC1Fwd"
#define HMI_COMP_C1_FWD_U "lC1FwdU"
#define HMI_COMP_C1_LBL_I "lC1I"
#define HMI_COMP_C1_REF "tvC1Ref"
#define HMI_COMP_C1_REF_U "lC1RefU"
#define HMI_COMP_C1_SWR "tvC1Swr"
#define HMI_COMP_C1_SWR_U "lC1SwrU"
#define HMI_COMP_C1_STAT "pC1Stat"

// Row 2
constexpr char HMI_COMP_C2_BUTTON[] = "bC2";
#define HMI_COMP_C2_LBL_V "lC2V"
#define HMI_COMP_C2_FWD "tvC2Fwd"
#define HMI_COMP_C2_FWD_U "lC2FwdU"
#define HMI_COMP_C2_LBL_I "lC2I"
#define HMI_COMP_C2_REF "tvC2Ref"
#define HMI_COMP_C2_REF_U "lC2RefU"
#define HMI_COMP_C2_SWR "tvC2Swr"
#define HMI_COMP_C2_SWR_U "lC2SwrU"
#define HMI_COMP_C2_STAT "pC2Stat"

// Row 3
#define HMI_COMP_C3_BUTTON "bC3"
#define HMI_COMP_C3_LBL_V "lC3V"
#define HMI_COMP_C3_FWD "tvC3Fwd"
#define HMI_COMP_C3_FWD_U "lC3FwdU"
#define HMI_COMP_C3_LBL_I "lC3I"
#define HMI_COMP_C3_REF "tvC3Ref"
#define HMI_COMP_C3_REF_U "lC3RefU"
#define HMI_COMP_C3_SWR "tvC3Swr"
#define HMI_COMP_C3_SWR_U "lC3SwrU"
#define HMI_COMP_C3_STAT "pC3Stat"

// Row 4
#define HMI_COMP_C4_BUTTON "bC4"
#define HMI_COMP_C4_LBL_V "lC4V"
#define HMI_COMP_C4_FWD "tvC4Fwd"
#define HMI_COMP_C4_FWD_U "lC4FwdU"
#define HMI_COMP_C4_LBL_I "lC4I"
#define HMI_COMP_C4_REF "tvC4Ref"
#define HMI_COMP_C4_REF_U "lC4RefU"
#define HMI_COMP_C4_SWR "tvC4Swr"
#define HMI_COMP_C4_SWR_U "lC4SwrU"
#define HMI_COMP_C4_STAT "pC4Stat"

// Top strip on pStatus
#define HMI_COMP_PROFILE_BTN "bProfile"
#define HMI_COMP_PROFILE_TEXT "tvProfile"
#define HMI_COMP_NETWORK_BTN "bNetwork"
#define HMI_COMP_LAN_TEXT "tvLan"
#define HMI_COMP_RSSI_PIC "pRSSI"

// Bottom strip on pStatus
#define HMI_COMP_ALARMS_LABEL "lAlarms"
#define HMI_COMP_ALARMS_TEXT "tvAlarms"
#define HMI_COMP_MENU_BTN "bMenu"
#define HMI_COMP_DATETIME_TEXT "tvDT"

// pNetwork components (copied from RayEltekController)
#define HMI_COMP_NET_MODE "tvMode"
#define HMI_COMP_NET_IP "tvIP"
#define HMI_COMP_NET_SSID "tvSSID"
#define HMI_COMP_NET_RSSI "tvRSSI"
#define HMI_COMP_NET_STATUS "tvNetStatus"

// pNetwork2 components (AP scan page)
#define HMI_COMP_NET2_STATUS "tv2Status"
#define HMI_COMP_NET2_AP1 "b2AP1"
#define HMI_COMP_NET2_AP2 "b2AP2"
#define HMI_COMP_NET2_AP3 "b2AP3"
#define HMI_COMP_NET2_AP4 "b2AP4"
#define HMI_COMP_NET2_AP1_RSSI "tv2AP1RSSI"
#define HMI_COMP_NET2_AP2_RSSI "tv2AP2RSSI"
#define HMI_COMP_NET2_AP3_RSSI "tv2AP3RSSI"
#define HMI_COMP_NET2_AP4_RSSI "tv2AP4RSSI"

// pStatus components
#define HMI_COMP_PROFILE_TEXT "tvProfile"   // profile summary text
#define HMI_COMP_PROFILE_BUTTON "bProfile"  // left button, already on HMI_PAGE_STATUS

// Simple helper: clamp VSWR to a sane display range
inline float rpmClipVswr(float v)
{
    if (v < 1.0f)
        return 1.0f;
    if (v > 99.9f)
        return 99.9f;
    return v;
}

class RayPowerMeterUiController
{
public:
    RayPowerMeterUiController(ui::NextionDisplay &display,
                              std::vector<RfPowerSensor *> &sensors,
                              RayProtectionController &protection,
                              SystemStatus &status,
                            UserPreferences &prefs );

    // Initialize the Nextion side (baud, callbacks, initial page, etc.)
    bool begin(uint32_t baud = 115200);

    // Called periodically from loop() with millis()
    void update(uint32_t nowMs, AppMode mode);

    // Called by main when protection trips (hook this into your trip callback)
    void onProtectionTrip(const RayTripEvent &ev);

    // Optional: wire NextionDisplay::onTouchCommand to this
    // to handle button presses (e.g., open network page, reset alarms).
    void handleTouch(uint8_t pageId, uint8_t componentId, uint8_t state);

    // Map coupler index -> RayTripSource enum (for status icons)
    RayTripSource indexToTripSource(int i);

    void applyCouplerRowStyle_(int row, bool active);

private:
    ui::NextionDisplay &_display;
    std::vector<RfPowerSensor *> &_sensors;
    RayProtectionController &_protection;
    SystemStatus &_status;
    UserPreferences &_prefs;

    uint32_t _lastUiUpdateMs{0};
    uint32_t _uiUpdateInterval{100}; // ms, main page refresh rate (~10 Hz)

    // Cached last trip for display
    bool _hasTrip{false};
    RayTripEvent _lastTrip{};

    void updateMainPage_();
    void updateProtectionStatus_(AppMode mode);
    void updateNetworkStrip_();
    void updateNetworkPages_();
    void renderTripSummary_();

    enum class CouplerIconState : uint8_t {
        NA,
        OK,
        ALARM
    };

    CouplerIconState couplerIconState_(int index) const;
    void updateCouplerStatusIcon_(int index);

    String buildProfileSummary_() const;
    void   updateProfileSummary_();
};
