// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/CouplerCalibration.h"
#include "core/ReleaseDefaults.h"
#include <Arduino.h>
#include <WiFi.h>

// -----------------------------------------------------------------------------
// RayPowerMeter - ESP32-S3 main entry point (V1: MCU ADC based)
// -----------------------------------------------------------------------------

#ifndef APP_NAME
#define APP_NAME "RayPowerMeter"
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

// -----------------------------------------------------------------------------
// App includes (RayPowerMeter-specific)
// -----------------------------------------------------------------------------
#include "core/Constants.h"
#include "hw/pinout.h"
#include "coupler/rfpowersensor.h"
#include "core/RayProtectionController.h"
#include "core/AppState.h"
#include "net/NetManager.h"
#include "net/mdns_helper.h"
#include "ui/RayPowerMeterUiController.h"
#include "core/SystemStatus.h"
#include "core/userprefs.h"
#include "core/eepromprefs.h"
#include "core/SystemStatus.h"
#include "ui/CustomCommands.h"
#include <time.h>
#include <sys/time.h>
#include "api/RayCommServer.h"
#include "api/RayHttpServer.h"
#include "hw/AuxInputs.h"
#include "core/ModScopeController.h"
#include "core/ModulationScope.h"
#include "core/FrequencyMeter.h"
#include "core/AuxDigital.h"
#include "core/OtaManager.h"

extern "C" int ets_printf(const char *fmt, ...);

// Forward declarations
static void uiNetworkRefresh();
void applyCouplerPrefsToRuntime();
static void handleStatusCustom(uint8_t pageId, uint8_t type, uint8_t instance);
static void handleCalCustom(uint8_t pageId, uint8_t type, uint8_t instance);
static void updateCalLiveUi();
void applyAuxPrefsToRuntime();
void applyAuxDigitalPrefsToRuntime();

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

// User preferences
OtaManager gOta;
UserPreferences gPrefs;

// Network manager and HTTP server
NetManager gNet;
static AsyncWebServer gServer(80);
RayHttpServer gHttpServer(gServer);

// Remote command server over Serial
RayCommServer gCommServer(UART_REMOTE);

// System status
SystemStatus gStatus;

// Frequency meter (prescaler square wave -> Hz)
FrequencyMeter gFreqMeter(FREQ_INPUT_PIN, PRESCALER_DIVIDER);

// 4 coupler power sensors
RfPowerSensor g_coupler1("COUP1", COUPLER1_FWD_PIN, COUPLER1_REF_PIN, DEFAULT_FWD_BAR_RANGE_W[0]);
RfPowerSensor g_coupler2("COUP2", COUPLER2_FWD_PIN, COUPLER2_REF_PIN, DEFAULT_FWD_BAR_RANGE_W[1]);
RfPowerSensor g_coupler3("COUP3", COUPLER3_FWD_PIN, COUPLER3_REF_PIN, DEFAULT_FWD_BAR_RANGE_W[2]);
RfPowerSensor g_coupler4("COUP4", COUPLER4_FWD_PIN, COUPLER4_REF_PIN, DEFAULT_FWD_BAR_RANGE_W[3]);

std::vector<RfPowerSensor *> g_sensors = {
    &g_coupler1,
    &g_coupler2,
    &g_coupler3,
    &g_coupler4};

// Protection config: simple starting point
RayProtectionConfig g_protCfg = []
{
    RayProtectionConfig cfg{};

    // Default forward power limits (W)
    cfg.fwdMaxW[1] = DEFAULT_FWD_BAR_RANGE_W[1]; // COUP2 forward limit
    cfg.fwdMaxW[2] = DEFAULT_FWD_BAR_RANGE_W[2]; // COUP3 forward limit`
    cfg.fwdMaxW[3] = DEFAULT_FWD_BAR_RANGE_W[3]; // COUP4 forward limit

    cfg.reflMaxW[1] = DEFAULT_REF_BAR_RANGE_W[1]; // COUP2 reflected limit
    cfg.reflMaxW[2] = DEFAULT_REF_BAR_RANGE_W[2]; // COUP3 reflected limit
    cfg.reflMaxW[3] = DEFAULT_REF_BAR_RANGE_W[3]; // COUP4 reflected limit

    // Default SWR limits
    cfg.swrMax[0] = 3.0f; // COUP1 SWR limit
    cfg.swrMax[1] = 3.0f; // COUP2 SWR limit
    cfg.swrMax[2] = 3.0f; // COUP3 SWR limit
    cfg.swrMax[3] = 3.0f; // COUP4 SWR limit

    cfg.bootIgnoreMs = BOOT_IGNORED_MS;
    cfg.gateOnTx = false; // for now, enforce even w/out explicit TX sense
    return cfg;
}();

RayProtectionController g_protection(g_sensors, g_protCfg);

// Simple app mode
AppMode g_mode = AppMode::Idle;

// UI controller
ui::NextionDisplay gDisplay(UART_HMI);
RayPowerMeterUiController gUi(gDisplay, g_sensors, g_protection, gStatus, gPrefs);

// Modulation scope controller
ModScopeController gModScope;

// ---- Wi-Fi / HMI state helpers ----
static String gAPQuick[4];     // quick-pick SSIDs from last scan
static String gLastPickedSSID; // last SSID chosen via quick-pick
static String gPendingSsid;    // currently trying to connect to...
static String gPendingPass;
// Track connect progress explicitly
static bool gWifiConnectInProgress = false;
static uint32_t gWifiConnectStartMs = 0;

// AUX1: Voltage, AUX2: Current, AUX3: Temperature (see pinout.h)
AuxInputs gAuxInputs(AUX1_ADC_PIN, AUX2_ADC_PIN, AUX3_ADC_PIN);

// AUX digital I/O
AuxDigital gAuxDigital(AUXD1_PIN, AUXD2_PIN, AUXD3_PIN, AUXD4_PIN);

// -----------------------------------------------------------------------------
// Calibration state (multi-point table; user may capture up to CAL_MAX_POINTS, apply anytime)
// -----------------------------------------------------------------------------
struct CalPoint
{
    bool valid = false;
    float voltage = 0.0f; // ADC-derived volts
    float watts = 0.0f;   // user-entered watts
};

static CalPoint gCalPts[CAL_MAX_POINTS];
static uint8_t gCalCount = 0;

static uint8_t gCalActiveCouplerIdx = 0; // current selection for the working set
static bool gCalActiveForward = true;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// TX detect placeholder: later this can be CAT, a GPIO, network, etc.
static bool readTxActive()
{
    // For now always treated as active if you want protection always on.
    // When you add a TX sense line, update this.
    return true;
}

// LED helpers using pinout.h
static void setStatusLed(bool on)
{
    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, on ? LED_ON : LED_OFF);
}

static void setErrorLed(bool on)
{
    pinMode(LED_ERROR_PIN, OUTPUT);
    digitalWrite(LED_ERROR_PIN, on ? LED_ON : LED_OFF);
}

// Which coupler is selected on pCal? (ckCalC1..ckCalC4)
static int getCalSelectedCouplerFromHmi()
{
    uint32_t v = 0;
    for (int i = 0; i < COUPLERS_MAX; ++i)
    {
        char comp[24];
        snprintf(comp, sizeof(comp), "ckCalC%d.val", i + 1);
        if (gDisplay.getNumber(comp, v) && v != 0)
        {
            return i;
        }
    }
    // Default to coupler 0 if nothing is selected
    return 0;
}

// Which path is selected? (ckCalPathFwd / ckCalPathRef)
static bool getCalSelectedPathIsForward()
{
    uint32_t v = 0;

    if (gDisplay.getNumber("ckCalPathFwd.val", v) && v != 0)
    {
        return true;
    }
    if (gDisplay.getNumber("ckCalPathRef.val", v) && v != 0)
    {
        return false;
    }
    // Default to forward if unclear
    return true;
}

// Read the target watts fields nCalLowW / nCalHighW
static float getCalTargetWatts(bool lowPoint)
{
    char comp[24];
    snprintf(comp, sizeof(comp),
             lowPoint ? "nCalLowW.val" : "nCalHighW.val");

    uint32_t val = 0;
    if (!gDisplay.getNumber(comp, val))
    {
        return 0.0f;
    }
    return static_cast<float>(val);
}

// Clear working set
static void clearCalWorkingSet()
{
    for (uint8_t i = 0; i < CAL_MAX_POINTS; ++i)
        gCalPts[i] = CalPoint{};
    gCalCount = 0;
}



// Periodically push live values to pCal page
static void updateCalLiveUi()
{
    // Only update if we're on the pCal page
    if (gDisplay.currentPage() == HmiCmd::PAGE_CAL)
    {
        // Determine which coupler is selected
        int idx = getCalSelectedCouplerFromHmi();
        if (idx < 0 || idx >= COUPLERS_MAX)
        {
            idx = 0;
        }

        const CouplerPreferences &cp = gPrefs.couplers[idx];

        // If coupler is NONE, show N/A
        if (cp.couplerType == CouplerType::NONE)
        {
            gDisplay.setText("tvCalFwd", "N/A");
            gDisplay.setText("tvCalRef", "N/A");
            gDisplay.setText("tvRawFwd", "N/A");
            gDisplay.setText("tvRawRef", "N/A");
            return;
        }

        RfPowerSensor *s = g_sensors[idx];

        const float fwdW = s->getForwardPowerWatts();
        const float refW = s->getReflectedPowerWatts();
        const float vFwd = s->getLastFwdVoltage();
        const float vRef = s->getLastRefVoltage();

        char buf[32];

        // Forward power (watts)
        snprintf(buf, sizeof(buf), "%.1f", fwdW);
        gDisplay.setText("tvCalFwd", buf);

        // Reflected power (watts)
        snprintf(buf, sizeof(buf), "%.1f", refW);
        gDisplay.setText("tvCalRef", buf);

        // Raw ADC-derived voltages
        snprintf(buf, sizeof(buf), "%.3f", vFwd);
        gDisplay.setText("tvRawFwd", buf);

        snprintf(buf, sizeof(buf), "%.3f", vRef);
        gDisplay.setText("tvRawRef", buf);

        // Serial.printf("[Cal] live update coupler %d: Fwd %.1f W (%.3f V), Ref %.1f W (%.3f V)\n",
        //               idx + 1, fwdW, vFwd, refW, vRef);
    }
}

static void captureCalPoint(bool lowPoint)
{
    int idx = getCalSelectedCouplerFromHmi();
    if (idx < 0 || idx >= COUPLERS_MAX)
        idx = 0;

    CouplerPreferences &cp = gPrefs.couplers[idx];
    if (cp.couplerType == CouplerType::NONE)
    {
        gDisplay.toast("Select active coupler", 1500, HmiCmd::PAGE_CAL);
        return;
    }

    const bool forward = getCalSelectedPathIsForward();

    // If user changed coupler/path since last capture, start a new working set
    if ((uint8_t)idx != gCalActiveCouplerIdx || forward != gCalActiveForward)
    {
        gCalActiveCouplerIdx = (uint8_t)idx;
        gCalActiveForward = forward;
        clearCalWorkingSet();
    }

    RfPowerSensor *s = g_sensors[idx];

    const float V = forward ? s->getLastFwdVoltage()
                            : s->getLastRefVoltage();

    if (V <= 0.0f)
    {
        gDisplay.toast("No RF signal", 1500, HmiCmd::PAGE_CAL);
        return;
    }

    const float W = getCalTargetWatts(lowPoint);
    if (W <= 0.0f)
    {
        gDisplay.toast(lowPoint ? "Set low watts" : "Set high watts",
                       1500, HmiCmd::PAGE_CAL);
        return;
    }

    if (gCalCount >= CAL_MAX_POINTS)
    {
        gDisplay.toast("Point table full", 1500, HmiCmd::PAGE_CAL);
        return;
    }

    // Append
    gCalPts[gCalCount].valid = true;
    gCalPts[gCalCount].voltage = V;
    gCalPts[gCalCount].watts = W;
    ++gCalCount;

    char msg[48];
    snprintf(msg, sizeof(msg), "Point captured (%u)", (unsigned)gCalCount);
    gDisplay.toast(msg, 1000, HmiCmd::PAGE_CAL);
}

static void applyCalFromState()
{
    if (gCalCount < 2)
    {
        gDisplay.toast("Capture at least 2 points", 1500, HmiCmd::PAGE_CAL);
        return;
    }

    const int idx = (int)gCalActiveCouplerIdx;
    const bool forward = gCalActiveForward;

    // Copy valid points into local arrays
    float vPts[CAL_MAX_POINTS];
    float wPts[CAL_MAX_POINTS];
    uint8_t n = 0;

    for (uint8_t i = 0; i < gCalCount && i < CAL_MAX_POINTS; ++i)
    {
        if (gCalPts[i].valid && gCalPts[i].voltage > 0.0f && gCalPts[i].watts > 0.0f)
        {
            vPts[n] = gCalPts[i].voltage;
            wPts[n] = gCalPts[i].watts;
            ++n;
        }
    }

    if (n < 2)
    {
        gDisplay.toast("Not enough valid points", 1500, HmiCmd::PAGE_CAL);
        return;
    }

    // Sort by voltage ascending (simple selection sort)
    for (uint8_t i = 0; i < n; ++i)
    {
        uint8_t best = i;
        for (uint8_t j = i + 1; j < n; ++j)
        {
            if (vPts[j] < vPts[best])
                best = j;
        }
        if (best != i)
        {
            float tv = vPts[i];
            vPts[i] = vPts[best];
            vPts[best] = tv;
            float tw = wPts[i];
            wPts[i] = wPts[best];
            wPts[best] = tw;
        }
    }

    const auto result = calibrateCouplerPathTable(idx, forward, vPts, wPts, n);
    if (result != CalibrationResult::Ok)
    {
        gDisplay.toast(result == CalibrationResult::SaveFailed ? "Calibration save failed" : "Invalid calibration points",
                       1500, HmiCmd::PAGE_CAL);
        return;
    }

    Serial.printf("[Cal] applied TABLE to coupler %d %s: n=%u V[0]=%.4f W[0]=%.1f  V[last]=%.4f W[last]=%.1f\n",
                  idx + 1, forward ? "Fwd" : "Ref",
                  (unsigned)n, vPts[0], wPts[0], vPts[n - 1], wPts[n - 1]);

    gDisplay.toast("Calibration applied", 1500, HmiCmd::PAGE_CAL);
}

static void resetCalForSelection()
{
    int idx = getCalSelectedCouplerFromHmi();
    if (idx < 0 || idx >= COUPLERS_MAX)
    {
        idx = 0;
    }

    CouplerPreferences &cp = gPrefs.couplers[idx];

    if (cp.couplerType == CouplerType::NONE)
    {
        gDisplay.toast("Select active coupler", 1500, HmiCmd::PAGE_CAL);
        return;
    }

    const bool forward = getCalSelectedPathIsForward();

    CalSource source = CalSource::FactoryLinear;
    if (resetCouplerCalibration(idx, source, forward, !forward) != CalibrationResult::Ok)
    {
        gDisplay.toast("Calibration reset failed", 1500, HmiCmd::PAGE_CAL);
        return;
    }

    clearCalWorkingSet();

    gDisplay.toast("Calibration reset", 1500, HmiCmd::PAGE_CAL);
}

static void syncCalFromPrefs()
{
    Serial.println("[Cal] syncing calibration from prefs");

    // Default selection: Coupler 1, Forward path
    gDisplay.sendCommand("ckCalC1.val=1");
    gDisplay.sendCommand("ckCalC2.val=0");
    gDisplay.sendCommand("ckCalC3.val=0");
    gDisplay.sendCommand("ckCalC4.val=0");

    gDisplay.sendCommand("ckCalPathFwd.val=1");
    gDisplay.sendCommand("ckCalPathRef.val=0");

    // Reasonable default targets (user can change)
    gDisplay.sendCommand("nCalLowW.val=10");
    gDisplay.sendCommand("nCalHighW.val=100");

    // Clear local points
    clearCalWorkingSet();

    // Immediately push live readings once
    updateCalLiveUi();
}

static void wifiScanAndPopulate2()
{
    Serial.printf("[WiFi] scan start (status=%d, mode=%d)\n",
                  (int)WiFi.status(), (int)WiFi.getMode());

    gDisplay.setText("tv2Status", "Scanning...");

    // Clear cached SSIDs
    for (int i = 0; i < 4; ++i)
    {
        gAPQuick[i] = "";
    }

    // Clear the four quick-pick rows on pNetwork2
    for (int i = 1; i <= 4; ++i)
    {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "b2AP%d.txt=\"\"", i);
        gDisplay.sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "vis b2AP%d,0", i);
        gDisplay.sendCommand(cmd);

        snprintf(cmd, sizeof(cmd), "tv2AP%dRSSI.txt=\"NA\"", i);
        gDisplay.sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "vis tv2AP%dRSSI,0", i);
        gDisplay.sendCommand(cmd);
    }

    // Ensure STA mode for scanning
    if (WiFi.getMode() != WIFI_MODE_STA)
    {
        WiFi.mode(WIFI_MODE_STA);
    }

    // Cancel any in-flight connect but keep config
    //   disconnect(wifioff = false, erase = false)
    WiFi.disconnect(false, false);

    // Active scan (more robust than passive in many setups)
    int n = WiFi.scanNetworks(/*async=*/false,
                              /*show_hidden=*/true);
    Serial.printf("[WiFi] scan done: n=%d\n", n);

    if (n <= 0)
    {
        gDisplay.setText("tv2Status", "No networks found");
        WiFi.scanDelete();
        return;
    }

    // Sort indices by RSSI desc
    std::vector<int> idx;
    idx.reserve(n);
    for (int i = 0; i < n; ++i)
        idx.push_back(i);
    std::sort(idx.begin(), idx.end(),
              [](int a, int b)
              { return WiFi.RSSI(a) > WiFi.RSSI(b); });

    int shown = 0;
    for (int k = 0; k < n && shown < 4; ++k)
    {
        int i = idx[k];

        String ssid = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);
        int32_t chan = WiFi.channel(i);

        if (ssid.isEmpty())
            ssid = "<hidden>";

        gAPQuick[shown] = ssid; // cache

        char cmd[96];

        // Button text + visible
        snprintf(cmd, sizeof(cmd), "b2AP%d.txt=\"%s\"", shown + 1, ssid.c_str());
        gDisplay.sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "vis b2AP%d,1", shown + 1);
        gDisplay.sendCommand(cmd);

        // RSSI label + visible
        char rssiTxt[16];
        snprintf(rssiTxt, sizeof(rssiTxt), "%ld", (long)rssi);
        snprintf(cmd, sizeof(cmd), "tv2AP%dRSSI.txt=\"%s\"", shown + 1, rssiTxt);
        gDisplay.sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "vis tv2AP%dRSSI,1", shown + 1);
        gDisplay.sendCommand(cmd);

        Serial.printf("   %d) %-24s ch=%2ld  RSSI=%4ld\n",
                      shown, ssid.c_str(), (long)chan, (long)rssi);
        ++shown;
    }

    char st[32];
    snprintf(st, sizeof(st), "%d network(s) shown", shown);
    gDisplay.setText("tv2Status", st);

    WiFi.scanDelete();
}

// Helpers for pSetup page

// Generic numeric reader for .val properties
static float readNumericVal(uint8_t idx, const char *fmt, float fallback, float scale = 1.0f)
{
    char comp[24];
    snprintf(comp, sizeof(comp), fmt, (unsigned)(idx + 1));

    uint32_t raw = 0;
    if (!gDisplay.getNumber(comp, raw))
    {
        return fallback;
    }
    return (float(raw) * scale);
}

// Generic checkbox reader for cbA#... checkboxes
static bool readCheckbox(uint8_t idx, const char *fmt)
{
    char comp[24];
    snprintf(comp, sizeof(comp), fmt, (unsigned)(idx + 1));

    uint32_t val = 0;
    if (!gDisplay.getNumber(comp, val))
    {
        return false;
    }
    return (val != 0);
}

static void updateSetupBrightnessUi()
{
    // Sync hBrightness slider with prefs.displayBrightness
    // Optional: also send dim in case page was just opened
    delay(100);
    uint8_t br = gPrefs.displayBrightness;
    gDisplay.setBrightness(br);
}

static void handleSetupSetTime()
{
    // Read UTC components from Nextion
    String yrStr, monStr, dayStr, hrStr, minStr, secStr;

    if (!gDisplay.getText("nUtcYear", yrStr))
        return;
    if (!gDisplay.getText("nUtcMonth", monStr))
        return;
    if (!gDisplay.getText("nUtcDay", dayStr))
        return;
    if (!gDisplay.getText("nUtcHour", hrStr))
        return;
    if (!gDisplay.getText("nUtcMin", minStr))
        return;
    if (!gDisplay.getText("nUtcSec", secStr))
        return;

    int year = yrStr.toInt();
    int month = monStr.toInt();
    int day = dayStr.toInt();
    int hour = hrStr.toInt();
    int min = minStr.toInt();
    int sec = secStr.toInt();

    // Basic sanity
    if (year < 2020 || year > 2099)
        return;
    if (month < 1 || month > 12)
        return;
    if (day < 1 || day > 31)
        return;
    if (hour < 0 || hour > 23)
        return;
    if (min < 0 || min > 59)
        return;
    if (sec < 0 || sec > 59)
        return;

    struct tm tmUtc{};
    tmUtc.tm_year = year - 1900;
    tmUtc.tm_mon = month - 1;
    tmUtc.tm_mday = day;
    tmUtc.tm_hour = hour;
    tmUtc.tm_min = min;
    tmUtc.tm_sec = sec;

    // On ESP32, mktime() uses local TZ; if your TZ is UTC, this is fine.
    // If you later set a different TZ, you can swap to timegm() or adjust.
    time_t epoch = mktime(&tmUtc);

    if (epoch == (time_t)-1)
    {
        gDisplay.toast("Invalid time", 1500, HmiCmd::PAGE_SETUP);
        return;
    }

    struct timeval tv{};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;

    settimeofday(&tv, nullptr);

    gDisplay.toast("UTC updated", 1500, HmiCmd::PAGE_SETUP);
}

// Push gPrefs.couplers[] => pSetup2 radios / ranges / names
static void syncSetup2FromPrefs()
{
    for (uint8_t i = 0; i < COUPLERS_MAX; ++i)
    {
        const CouplerPreferences &cp = gPrefs.couplers[i];
        const uint8_t row = i + 1;
        char cmd[64];

        // 1) Name
        snprintf(cmd, sizeof(cmd),
                 "tvC%uName.txt=\"%s\"",
                 static_cast<unsigned>(row),
                 cp.name[0] ? cp.name : "");
        gDisplay.sendCommand(cmd);

        // 2) Bar ranges
        snprintf(cmd, sizeof(cmd),
                 "nC%uFwdRg.val=%u",
                 static_cast<unsigned>(row),
                 static_cast<unsigned>(cp.fwdBarRangeW));
        gDisplay.sendCommand(cmd);

        snprintf(cmd, sizeof(cmd),
                 "nC%uRefRg.val=%u",
                 static_cast<unsigned>(row),
                 static_cast<unsigned>(cp.refBarRangeW));
        gDisplay.sendCommand(cmd);

        // 3) Coupler type -> radio group
        const uint8_t t = static_cast<uint8_t>(cp.couplerType);

        const int selNone =
            (t == static_cast<uint8_t>(CouplerType::NONE)) ? 1 : 0;
        const int selLp =
            (t == static_cast<uint8_t>(CouplerType::COUPLER_200W)) ? 1 : 0;
        const int selHp =
            (t == static_cast<uint8_t>(CouplerType::COUPLER_2KW)) ? 1 : 0;

        // --- IMPORTANT: clear ALL first to avoid stale multi-selected state ---
        snprintf(cmd, sizeof(cmd), "rC%uNone.val=0", static_cast<unsigned>(row));
        gDisplay.sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "rC%uLp.val=0", static_cast<unsigned>(row));
        gDisplay.sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "rC%uHp.val=0", static_cast<unsigned>(row));
        gDisplay.sendCommand(cmd);

        // Now set exactly one
        if (selNone)
        {
            snprintf(cmd, sizeof(cmd), "rC%uNone.val=1", static_cast<unsigned>(row));
            gDisplay.sendCommand(cmd);
        }
        else if (selLp)
        {
            snprintf(cmd, sizeof(cmd), "rC%uLp.val=1", static_cast<unsigned>(row));
            gDisplay.sendCommand(cmd);
        }
        else if (selHp)
        {
            snprintf(cmd, sizeof(cmd), "rC%uHp.val=1", static_cast<unsigned>(row));
            gDisplay.sendCommand(cmd);
        }
        else
        {
            // Fallback: if unknown, force NONE
            snprintf(cmd, sizeof(cmd), "rC%uNone.val=1", static_cast<unsigned>(row));
            gDisplay.sendCommand(cmd);
        }
    }
}

// Helpers for pSetup2 (coupler setup)
static void readSetup2RowToPrefs(uint8_t idx /*0..3*/)
{
    if (idx >= COUPLERS_MAX)
        return;

    CouplerPreferences &cp = gPrefs.couplers[idx];

    // 1) Name: tvC1Name..tvC4Name  (text, so getText is correct)
    {
        char comp[16];
        snprintf(comp, sizeof(comp), "tvC%uName", static_cast<unsigned>(idx + 1));

        String name;
        if (gDisplay.getText(comp, name) && !name.isEmpty())
        {
            strlcpy(cp.name, name.c_str(), sizeof(cp.name));
        }
    }

    // Helper to read numeric fields
    auto readNumber = [&](const char *fmt, float fallback) -> float
    {
        char comp[24];
        snprintf(comp, sizeof(comp), fmt, static_cast<unsigned>(idx + 1));

        uint32_t val = 0;
        if (!gDisplay.getNumber(comp, val))
            return fallback;

        float f = static_cast<float>(val);
        return (f > 0.0f) ? f : fallback;
    };

    // 2) Bar ranges FIRST: nC?FwdRg / nC?ReRg
    // (This makes radio-conflict resolution stable and matches the HMI state.)
    cp.fwdBarRangeW = readNumber("nC%uFwdRg.val", cp.fwdBarRangeW);
    cp.refBarRangeW = readNumber("nC%uRefRg.val", cp.refBarRangeW);

    // 3) Type: rC?None / rC?Lp / rC?Hp  (val is numeric → use getNumber)
    uint8_t typeIndex = static_cast<uint8_t>(CouplerType::NONE);

    {
        uint32_t noneVal = 0;
        uint32_t lpVal = 0;
        uint32_t hpVal = 0;
        char comp[24];

        // None
        snprintf(comp, sizeof(comp), "rC%uNone.val", static_cast<unsigned>(idx + 1));
        bool haveNone = gDisplay.getNumber(comp, noneVal);

        // LP (200W)
        snprintf(comp, sizeof(comp), "rC%uLp.val", static_cast<unsigned>(idx + 1));
        bool haveLp = gDisplay.getNumber(comp, lpVal);

        // HP (2KW)
        snprintf(comp, sizeof(comp), "rC%uHp.val", static_cast<unsigned>(idx + 1));
        bool haveHp = gDisplay.getNumber(comp, hpVal);

        const bool selNone = haveNone && (noneVal != 0);
        const bool selLp = haveLp && (lpVal != 0);
        const bool selHp = haveHp && (hpVal != 0);

        if (selNone)
        {
            typeIndex = static_cast<uint8_t>(CouplerType::NONE);
        }
        else if (selLp && !selHp)
        {
            typeIndex = static_cast<uint8_t>(CouplerType::COUPLER_200W);
        }
        else if (selHp && !selLp)
        {
            typeIndex = static_cast<uint8_t>(CouplerType::COUPLER_2KW);
        }
        else if (selLp && selHp)
        {
            // Conflict: Nextion reported both selected.
            // Resolve using bar range (NOW already updated from HMI).
            const float fwdRg = cp.fwdBarRangeW;
            typeIndex = (fwdRg > 0.0f && fwdRg <= 300.0f)
                            ? static_cast<uint8_t>(CouplerType::COUPLER_200W)
                            : static_cast<uint8_t>(CouplerType::COUPLER_2KW);

            Serial.printf("[Setup2] Row %u: WARNING radio conflict (LP+HP). fwdRg=%.1f -> resolved type=%u\n",
                          (unsigned)(idx + 1), fwdRg, (unsigned)typeIndex);
        }
        else
        {
            typeIndex = static_cast<uint8_t>(CouplerType::NONE);
        }
    }

    const CouplerType oldType = static_cast<CouplerType>(cp.couplerType);
    cp.couplerType = static_cast<CouplerType>(typeIndex);

    // Apply default trip mask logic based on type change)
    if (cp.couplerType == CouplerType::NONE)
    {
        cp.enabledTripMask = 0;
    }
    else if (cp.enabledTripMask == 0)
    {
        // Re-arm defaults when enabling a coupler from NONE
        cp.enabledTripMask = 0x07;
    }

    // Apply factory table profile (if allowed) AFTER ranges are updated and type is final
    ensureFactoryTableProfileIfAllowed(idx, oldType, static_cast<CouplerType>(typeIndex));

    // 4) Physical full-scale watts (based on type)
    float fullScale = 0.0f;
    if (typeIndex < (sizeof(COUPLER_FULL_SCALE) / sizeof(COUPLER_FULL_SCALE[0])))
    {
        fullScale = COUPLER_FULL_SCALE[typeIndex];
    }
    cp.couplerFullScaleW = fullScale;

    // 5) If calibration is "empty", derive simple linear cal from full scale
    if (fullScale > 0.0f)
    {
        if (cp.fwdCal_m == 0.0f && cp.fwdCal_b == 0.0f)
        {
            cp.fwdCal_m = fullScale / ADC_VREF;
            cp.fwdCal_b = 0.0f;
        }
        if (cp.refCal_m == 0.0f && cp.refCal_b == 0.0f)
        {
            cp.refCal_m = fullScale / ADC_VREF;
            cp.refCal_b = 0.0f;
        }
    }

    // Debug (print human row number)
    Serial.printf("[Setup2] Row %u: name=\"%s\" type=%u fwdRg=%.1f refRg=%.1f\n",
                  (unsigned)(idx + 1), cp.name, (unsigned)cp.couplerType,
                  cp.fwdBarRangeW, cp.refBarRangeW);
}

static void handleSetup2Apply()
{
    const std::lock_guard<std::mutex> lock(gCalibrationMutex);
    const UserPreferences previous = gPrefs;

    // Read all 4 rows from pSetup2 into gPrefs.couplers[]
    for (uint8_t i = 0; i < COUPLERS_MAX; ++i)
    {
        readSetup2RowToPrefs(i);
    }

    // Recompute numCouplers based on how many are NOT "None"
    uint8_t activeCount = 0;
    for (uint8_t i = 0; i < COUPLERS_MAX; ++i)
    {
        if (gPrefs.couplers[i].couplerType != CouplerType::NONE)
        {
            ++activeCount;
        }
    }
    if (activeCount == 0)
    {
        activeCount = 1; // always keep at least one logical coupler
    }
    gPrefs.numCouplers = clampNumCouplers(activeCount);

    if (!saveUserPreferences(gPrefs))
    {
        gPrefs = previous;
        gDisplay.toast("Coupler settings save failed", 1500, HmiCmd::PAGE_SETUP2);
        return;
    }

    // Apply to runtime sensors + protection
    applyCouplerPrefsToRuntime();
    // TODO: after you send the pStatus row object names, we’ll hide/show
    // inactive rows based on couplerType == COUPLER_NONE.

    // Simple confirmation toast on pSetup2
    gDisplay.toast("Couplers updated", 1200, HmiCmd::PAGE_SETUP2);
}

static void readAlarmRelayToPrefs()
{
    uint32_t val = 0;
    if (!gDisplay.getNumber("cbRelayEnable.val", val))
    {
        return;
    }

    gPrefs.protectionRelayEnabled = (val != 0);

    Serial.printf("[Alarms] Relay enable: %s\n",
                  gPrefs.protectionRelayEnabled ? "ENABLED" : "DISABLED");
}

static void readExternalIntsToPrefs()
{
    uint32_t val = 0;

    // Ext Int 1
    if (gDisplay.getNumber("cbA1IntProt.val", val))
    {
        gPrefs.extInt1Enabled = (val != 0);
    }
    else
    {
        // if read fails, leave previous value unchanged (or force false)
        // gPrefs.extInt1Enabled = false;
    }

    // Ext Int 2
    if (gDisplay.getNumber("cbA2IntProt.val", val))
    {
        gPrefs.extInt2Enabled = (val != 0);
    }
    else
    {
        // gPrefs.extInt2Enabled = false;
    }

    Serial.printf("[Alarms] ExtInt1=%s ExtInt2=%s\n",
                  gPrefs.extInt1Enabled ? "EN" : "DIS",
                  gPrefs.extInt2Enabled ? "EN" : "DIS");
}

static void readAlarmRowToPrefs(uint8_t idx /*0..3*/)
{
    if (idx >= COUPLERS_MAX)
        return;

    CouplerPreferences &cp = gPrefs.couplers[idx];

    // 1) Numeric thresholds
    auto readFloat = [&](const char *fmt, float fallback) -> float
    {
        char comp[24];
        snprintf(comp, sizeof(comp), fmt, static_cast<unsigned>(idx + 1));
        String s;
        if (!gDisplay.getText(comp, s))
        {
            return fallback;
        }
        float v = s.toFloat();
        return (v >= 0.0f) ? v : fallback;
    };

    // Only update if this coupler is active; for NONE, leave as-is or force 0
    if (cp.couplerType == CouplerType::NONE)
    {
        cp.highFwdTripW = 0.0f;
        cp.highRefTripW = 0.0f;
        cp.highSWRTrip = 0.0f;
        cp.enabledTripMask = 0;
        return;
    }

    cp.highFwdTripW = readNumericVal(idx, "nA%uFwdTh.val", cp.highFwdTripW, 1.0f);
    cp.highRefTripW = readNumericVal(idx, "nA%uRefTh.val", cp.highRefTripW, 1.0f);

    cp.highSWRTrip = readNumericVal(idx, "xA%uSwrTh.val", cp.highSWRTrip, 0.01f);

    /*   // 2) Trip mask from checkboxes
       uint8_t mask = 0;
       auto readCheckbox = [&](const char *fmt) -> bool
       {
       char comp[24];
       snprintf(comp, sizeof(comp), fmt, static_cast<unsigned>(idx + 1));
       uint32_t val = 0;
       if (!gDisplay.getNumber(comp, val)) {
           return false;
       }
       return (val != 0);
       };
   */

    uint8_t mask = 0;

    if (readCheckbox(idx, "cbA%uHFwd.val"))
        mask |= TRIPMASK_HIGH_FWD;
    if (readCheckbox(idx, "cbA%uHRef.val"))
        mask |= TRIPMASK_HIGH_REF;
    if (readCheckbox(idx, "cbA%uHSwr.val"))
        mask |= TRIPMASK_HIGH_SWR;

    cp.enabledTripMask = mask;

    // Optional: debug
    Serial.printf("[Alarms] Row %u: highFwd=%.1f highRef=%.1f highSWR=%.2f mask=0x%02X\n",
                  (unsigned)idx,
                  cp.highFwdTripW,
                  cp.highRefTripW,
                  cp.highSWRTrip,
                  (unsigned)cp.enabledTripMask);
}

static void handleAlarmsApply()
{
    for (uint8_t i = 0; i < COUPLERS_MAX; ++i)
    {
        readAlarmRowToPrefs(i);
    }

    readAlarmRelayToPrefs();
    readExternalIntsToPrefs();

    saveUserPreferences(gPrefs);
    applyCouplerPrefsToRuntime(); // already pushes thresholds into g_protCfg

    gDisplay.toast("Alarms updated", 1200, HmiCmd::PAGE_CONFIG_ALARM);
}

// Push gPrefs.couplers[] + relay flag => pConfigAlarm controls
static void syncAlarmsFromPrefs()
{
    for (uint8_t i = 0; i < COUPLERS_MAX; ++i)
    {
        const CouplerPreferences &cp = gPrefs.couplers[i];
        const uint8_t row = i + 1;
        char cmd[48];

        // Consider a coupler "active" only if it has a type and is within numCouplers
        const bool active =
            (cp.couplerType != CouplerType::NONE) &&
            (i < gPrefs.numCouplers);

        // --- 1) Name label: tvA1Name..tvA4Name -----------------------
        snprintf(cmd, sizeof(cmd),
                 "tvA%uName.txt=\"%s\"",
                 static_cast<unsigned>(row),
                 cp.name[0] ? cp.name : "");
        gDisplay.sendCommand(cmd);

        // --- 2) Numeric thresholds: nA?FwdTh / nA?RefTh / xA?SwrTh ---

        // Per-slot defaults from Constants.h + SWR = 3.00
        const float defFwd = (i < (sizeof(DEFAULT_FWD_BAR_RANGE_W) / sizeof(DEFAULT_FWD_BAR_RANGE_W[0])))
                                 ? DEFAULT_FWD_BAR_RANGE_W[i]
                                 : 0.0f;
        const float defRef = (i < (sizeof(DEFAULT_REF_BAR_RANGE_W) / sizeof(DEFAULT_REF_BAR_RANGE_W[0])))
                                 ? DEFAULT_REF_BAR_RANGE_W[i]
                                 : 0.0f;
        const float defSwr = 3.0f; // logical default SWR trip

        float fwdTh = cp.highFwdTripW;
        float refTh = cp.highRefTripW;
        float swrTh = cp.highSWRTrip;

        if (!active)
        {
            // Unused couplers → show reasonable defaults in the UI
            fwdTh = defFwd;
            refTh = defRef;
            swrTh = defSwr;
        }
        else
        {
            // Active couplers with 0 thresholds → also show defaults
            if (fwdTh <= 0.0f)
                fwdTh = defFwd;
            if (refTh <= 0.0f)
                refTh = defRef;
            if (swrTh <= 0.0f)
                swrTh = defSwr;
        }

        // Integers for the Nextion .val properties
        uint32_t fwdInt = (fwdTh > 0.0f)
                              ? static_cast<uint32_t>(fwdTh + 0.5f)
                              : 0;
        uint32_t refInt = (refTh > 0.0f)
                              ? static_cast<uint32_t>(refTh + 0.5f)
                              : 0;

        // xfloat: dp=2 → scale SWR by 100 (3.00 => 300)
        uint32_t swrInt = 0;
        if (swrTh > 0.0f)
        {
            float scaled = swrTh * 100.0f;
            if (scaled < 0.0f)
                scaled = 0.0f;
            swrInt = static_cast<uint32_t>(scaled + 0.5f);
        }

        snprintf(cmd, sizeof(cmd),
                 "nA%uFwdTh.val=%lu",
                 static_cast<unsigned>(row),
                 static_cast<unsigned long>(fwdInt));
        gDisplay.sendCommand(cmd);

        snprintf(cmd, sizeof(cmd),
                 "nA%uRefTh.val=%lu",
                 static_cast<unsigned>(row),
                 static_cast<unsigned long>(refInt));
        gDisplay.sendCommand(cmd);

        snprintf(cmd, sizeof(cmd),
                 "xA%uSwrTh.val=%lu",
                 static_cast<unsigned>(row),
                 static_cast<unsigned long>(swrInt));
        gDisplay.sendCommand(cmd);

        // --- 3) Trip enable checkboxes: cbA?HFwd / cbA?HRef / cbA?HSwr ---

        // For inactive couplers, we keep trip mask 0 so the checkboxes are off,
        // even though thresholds show defaults.
        uint8_t mask = active ? cp.enabledTripMask : 0;

        const int hfwd = (mask & TRIPMASK_HIGH_FWD) ? 1 : 0;
        const int href = (mask & TRIPMASK_HIGH_REF) ? 1 : 0;
        const int hswr = (mask & TRIPMASK_HIGH_SWR) ? 1 : 0;

        snprintf(cmd, sizeof(cmd),
                 "cbA%uHFwd.val=%d",
                 static_cast<unsigned>(row),
                 hfwd);
        gDisplay.sendCommand(cmd);

        snprintf(cmd, sizeof(cmd),
                 "cbA%uHRef.val=%d",
                 static_cast<unsigned>(row),
                 href);
        gDisplay.sendCommand(cmd);

        snprintf(cmd, sizeof(cmd),
                 "cbA%uHSwr.val=%d",
                 static_cast<unsigned>(row),
                 hswr);
        gDisplay.sendCommand(cmd);
    }

    // --- 4) Alarm relay enable checkbox: cbRelayEnable, cbA1IntProt & cbA2IntProt ---------------
    {
        char cmd[32];
        snprintf(cmd, sizeof(cmd),
                 "cbRelayEnable.val=%d",
                 gPrefs.protectionRelayEnabled ? 1 : 0);
        gDisplay.sendCommand(cmd);

        snprintf(cmd, sizeof(cmd),
                 "cbA1IntProt.val=%d",
                 gPrefs.extInt1Enabled ? 1 : 0);
        gDisplay.sendCommand(cmd);

        snprintf(cmd, sizeof(cmd),
                 "cbA2IntProt.val=%d",
                 gPrefs.extInt2Enabled ? 1 : 0);
        gDisplay.sendCommand(cmd);
    }
}

// Keep pSetup Wi-Fi controls in sync with prefs + NetManager
static void updateSetupWifiUi()
{
    const bool wifiEnabled = ReleaseDefaults::wifiEnabled(gPrefs.wifiEnabled);
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "cbWifiEnable.val=%d", wifiEnabled ? 1 : 0);
    gDisplay.sendCommand(cmd);

    // Tiny status text
    auto st = gNet.state();
    const char *txt = nullptr;

    if (!wifiEnabled)
    {
        txt = ReleaseDefaults::labNetworkEnabled ? "Wi-Fi disabled" : "Wi-Fi disabled in this build";
    }
    else if (gPrefs.wifiSsid[0] == '\0')
    {
        txt = "No SSID set";
    }
    else if (st == NetState::WIFI_UP)
    {
        txt = "Wi-Fi connected";
    }
    else
    {
        txt = "Wi-Fi not connected";
    }

    gDisplay.setText("tvSetupNetStat", txt);
}

static void handleStatusCustom(uint8_t pageId, uint8_t type, uint8_t instance)
{
    Serial.printf("[HMI STATUS] page=%u type=%u inst=%u\n",
                  pageId, type, instance);

    // Only care about pStatus + TYPE_STATUS frames
    if (type != HmiCmd::TYPE_STATUS)
    {
        return;
    }
    if (pageId != HmiCmd::PAGE_STATUS)
    {
        return;
    }

    // User clicked the alarm icon → clear protection latch
    if (instance == HmiCmd::STATUS_ALARM_CLR)
    {
        // 1) Clear protection controller state
        g_protection.reset();

        // 2) Clear system status flag so UI stops showing "latched" state
        gStatus.clearTrips();

        // 3) LEDs back to non-fault indication (loop() will refine mode later)
        setErrorLed(false);
        setStatusLed(true);

        // 4) Optional: give the user feedback on pStatus
        gDisplay.toast("Alarm cleared", 1000, HmiCmd::PAGE_STATUS);
    }
    else
    {
        Serial.printf("[HMI STATUS] Unhandled instance=%u\n", instance);
    }
}

static void handleSetupCustom(uint8_t pageId, uint8_t type, uint8_t instance)
{
    Serial.printf("[HMI SETUP] page=%u type=%u inst=%u\n",
                  pageId, type, instance);

    if (type != HmiCmd::TYPE_SETUP)
    {
        return;
    }

    // Serial.printf("[HMI SETUP] page=%u inst=%u\n", pageId, instance);

    if (pageId != HmiCmd::PAGE_SETUP &&
        pageId != HmiCmd::PAGE_SETUP2)
    {
        return;
    }

    // --- pSetup2 (coupler setup) ---
    if (pageId == HmiCmd::PAGE_SETUP2)
    {
        if (instance == HmiCmd::SETUP2_APPLY)
        {
            handleSetup2Apply();
            return;
        }
        if (instance == HmiCmd::SETUP2_SYNC_FROM_PREFS)
        {
            // Called when pSetup2 opens (or when you want to refresh it)
            syncSetup2FromPrefs();
            return;
        }

        Serial.printf("[HMI SETUP2] Unhandled instance=%u\n", instance);
        return;
    }

    // --- pSetup (general setup) ---
    if (instance == HmiCmd::SETUP_WIFI_ENABLE)
    {
        if (!ReleaseDefaults::labNetworkEnabled)
        {
            updateSetupWifiUi();
            return;
        }

        uint32_t val = 0;
        bool ok = gDisplay.getNumber("cbWifiEnable.val", val);
        bool enable = ok && (val != 0);

        gPrefs.wifiEnabled = enable;
        saveUserPreferences(gPrefs);

        if (!enable)
        {
            gNet.configure("", "", false);
            WiFi.disconnect(true);
            WiFi.mode(WIFI_MODE_NULL);
        }
        else
        {
            if (gPrefs.wifiSsid[0] != '\0')
            {
                gNet.configure(gPrefs.wifiSsid, gPrefs.wifiPass, true);
                gNet.begin();
            }
        }

        updateSetupWifiUi();
        uiNetworkRefresh();
        return;
    }
    else if (instance == HmiCmd::SETUP_BRIGHTNESS)
    {
        uint32_t val = 0;
        if (!gDisplay.getNumber("hBrightness.val", val))
        {
            return;
        }

        uint8_t br = static_cast<uint8_t>(val);
        if (br < 10)
            br = 10;
        if (br > 100)
            br = 100;

        gPrefs.displayBrightness = br;
        gDisplay.setBrightness(br);
        saveUserPreferences(gPrefs);

        return;
    }
    else if (instance == HmiCmd::SETUP_SET_TIME)
    {
        // See section 2 below
        // Handle time update from pSetup
        handleSetupSetTime();
        return;
    }

    Serial.printf("[HMI SETUP] Unhandled instance=%u\n", instance);
}

static void handleAlarmConfigCustom(uint8_t pageId, uint8_t type, uint8_t instance)
{
    Serial.printf("[HMI Config Alarm] page=%u type=%u inst=%u\n",
                  pageId, type, instance);

    if (type != HmiCmd::TYPE_CONFIG_ALARM)
    {
        return;
    }

    if (instance == HmiCmd::SETUP_ALARM_APPLY)
    {
        handleAlarmsApply();
        return;
    }

    if (instance == HmiCmd::SETUP_ALARM_SYNC_FROM_PREFS)
    {
        syncAlarmsFromPrefs();
        return;
    }

    Serial.printf("[HMI ALARM CONFIG] Unhandled instance=%u\n", instance);
    return;
}

static void handleNetworkCustom(uint8_t pageId, uint8_t type, uint8_t instance)
{
    if (!ReleaseDefaults::labNetworkEnabled)
    {
        gDisplay.toast("Wi-Fi disabled in this build", 1500, pageId);
        return;
    }

    if (type != HmiCmd::TYPE_NETWORK)
    {
        return;
    }

    // Debug so we can see frames arriving
    Serial.printf("[HMI NET] page=%u inst=%u\n", pageId, instance);

    // --- Scan & goto pNetwork2: handle regardless of pageId (8 or 9) ---
    if (instance == HmiCmd::NET_SCAN_AND_GOTO)
    {
        // bScan: go to pNetwork2 and start scan
        gDisplay.setPage(HmiCmd::PAGE_NETWORK2);
        gDisplay.setText("tv2Status", "Scanning...");

        // Hide all rows first (wifiScanAndPopulate2 re-enables)
        for (int i = 1; i <= 4; ++i)
        {
            char cmd[48];
            snprintf(cmd, sizeof(cmd), "vis b2AP%d,0", i);
            gDisplay.sendCommand(cmd);
            snprintf(cmd, sizeof(cmd), "vis tv2AP%dRSSI,0", i);
            gDisplay.sendCommand(cmd);
        }

        wifiScanAndPopulate2();
        return;
    }

    // ----- pNetwork (page 8) actions -----
    if (pageId == HmiCmd::PAGE_NETWORK)
    {
        if (instance == HmiCmd::NET_RECONNECT)
        {
            // bReconnect: toggle connect / disconnect
            auto st = gNet.state();
            if (st == NetState::WIFI_UP)
            {
                // Currently connected -> disconnect
                gDisplay.setText("tvNetStatus", "Disconnecting...");

                WiFi.disconnect(true);
                WiFi.mode(WIFI_MODE_NULL);

                gNet.configure("", "", false);
                gDisplay.sendCommand("bReconnect.txt=\"Connect\"");
                uiNetworkRefresh();
            }
            else
            {
                // Need saved SSID/pass in prefs
                if (gPrefs.wifiSsid[0] == '\0')
                {
                    gDisplay.toast("No saved SSID", 1300, HmiCmd::PAGE_NETWORK);
                    return;
                }

                gDisplay.setText("tvNetStatus", "Connecting...");
                gPendingSsid = gPrefs.wifiSsid;
                gPendingPass = gPrefs.wifiPass;

                gNet.configure(gPrefs.wifiSsid, gPrefs.wifiPass, true);
                gNet.begin();
            }
            return;
        }
        else if (instance == HmiCmd::NET_RESET)
        {
            gDisplay.setText("tvNetStatus", "Resetting...");

            WiFi.disconnect(true);
            WiFi.mode(WIFI_MODE_NULL);

            gPrefs.wifiSsid[0] = '\0';
            gPrefs.wifiPass[0] = '\0';
            gPrefs.wifiEnabled = false;
            // saveUserPreferences(gPrefs);

            gNet.configure("", "", false);
            uiNetworkRefresh();
            return;
        }
        else if (instance == HmiCmd::NET_SHOW_TOKEN)
        {
            // Simple MAC-based pairing token
            uint64_t mac = ESP.getEfuseMac();
            char token[48];
            snprintf(token, sizeof(token),
                     "RPMTR|%02X:%02X:%02X:%02X:%02X:%02X",
                     (uint8_t)(mac >> 40), (uint8_t)(mac >> 32),
                     (uint8_t)(mac >> 24), (uint8_t)(mac >> 16),
                     (uint8_t)(mac >> 8), (uint8_t)(mac));

            char cmd[96];
            snprintf(cmd, sizeof(cmd), "qrToken.txt=\"%s\"", token);
            gDisplay.sendCommand(cmd);
            gDisplay.sendCommand("vis qrToken,1");
            snprintf(cmd, sizeof(cmd), "tToken.txt=\"%s\"", token);
            gDisplay.sendCommand(cmd);
            gDisplay.sendCommand("vis tToken,0");
            return;
        }
    }

    // ----- pNetwork2 (page 9) actions -----
    if (pageId == HmiCmd::PAGE_NETWORK2)
    {
        if (instance == HmiCmd::NET_REFRESH_SCAN)
        {
            // b2Refresh
            wifiScanAndPopulate2();
            return;
        }
        else if (instance == HmiCmd::NET_CONNECT)
        {
            // b2Connect: connect using text inputs
            String ssid, pass;
            bool okS = gDisplay.getText("t2SSIDInp", ssid);
            if ((!okS || ssid.isEmpty()) && !gLastPickedSSID.isEmpty())
            {
                ssid = gLastPickedSSID;
                gDisplay.setText("t2SSIDInp", ssid.c_str());
            }
            gDisplay.getText("t2PassInp", pass);

            if (ssid.isEmpty())
            {
                gDisplay.toast("Enter SSID", 1200, HmiCmd::PAGE_NETWORK2);
                return;
            }

            gDisplay.setText("tv2Status", "Connecting...");
            gPendingSsid = ssid;
            gPendingPass = pass;

            // Update prefs
            strlcpy(gPrefs.wifiSsid, ssid.c_str(), sizeof(gPrefs.wifiSsid));
            strlcpy(gPrefs.wifiPass, pass.c_str(), sizeof(gPrefs.wifiPass));
            gPrefs.wifiEnabled = true;
            saveUserPreferences(gPrefs);

            // Configure NetManager (so /api/status etc. can still use it)
            gNet.configure(gPrefs.wifiSsid, gPrefs.wifiPass, gPrefs.wifiEnabled);
            gNet.begin();

            // *** NEW: actually kick the ESP32 station connect ***
            WiFi.mode(WIFI_MODE_STA);
            WiFi.disconnect(false, false);          // drop any previous session
            WiFi.begin(ssid.c_str(), pass.c_str()); // start STA connect

            gWifiConnectInProgress = true;
            gWifiConnectStartMs = millis();

            return;
        }
    }

    // Anything else: log but don’t crash
    Serial.printf("[HMI] onCustomCommand 'N': page=%u instance=%u (unhandled)\n",
                  pageId, instance);
}

static void handleCalCustom(uint8_t pageId, uint8_t type, uint8_t instance)
{
    Serial.printf("[HMI CAL] page=%u type=%u inst=%u\n",
                  pageId, type, instance);

    if (type != HmiCmd::TYPE_CAL)
    {
        return;
    }

    if (pageId != HmiCmd::PAGE_CAL)
    {
        return;
    }

    switch (instance)
    {
    case HmiCmd::CAL_SYNC_FROM_PREFS:
        syncCalFromPrefs();
        break;

    case HmiCmd::CAL_CAP_LOW:
        captureCalPoint(true);
        break;

    case HmiCmd::CAL_CAP_HIGH:
        captureCalPoint(false);
        break;

    case HmiCmd::CAL_APPLY:
        applyCalFromState();
        break;

    case HmiCmd::CAL_RESET:
        resetCalForSelection();
        break;

    default:
        Serial.printf("[HMI CAL] Unhandled instance=%u\n", instance);
        break;
    }
}

// Optional: small helper to turn RSSI into a text label
static const char *rssiLabel(int rssiDbm)
{
    if (rssiDbm == 0 || rssiDbm <= -127)
        return "N/A";
    if (rssiDbm > -55)
        return "Excellent";
    if (rssiDbm > -65)
        return "Good";
    if (rssiDbm > -75)
        return "Fair";
    return "Weak";
}

static inline void nxSetPicRSSI(uint8_t bars0to4)
{
    // Only valid on pStatus and pNetwork pages where pRSSI exists
    uint8_t page = gDisplay.currentPage();
    if (page != HMI_PAGE_STATUS && page != HMI_PAGE_NETWORK)
    {
        // Avoid sending invalid commands when on other pages
        return;
    }

    // Picture IDs: 5..9 represent 0..4 bars (for 0..4 “bars”)
    const int picId = 5 + (bars0to4 > 4 ? 4 : bars0to4);

    // Use page-qualified component name so we don’t confuse Nextion:
    //   pStatus.pRSSI.pic=...
    //   pNetwork.pRSSI.pic=...
    const char *pagePrefix = (page == HMI_PAGE_STATUS) ? "pStatus" : "pNetwork";

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "%s.pRSSI.pic=%d", pagePrefix, picId);
    gDisplay.sendCommand(cmd);
}

static void uiNetworkRefresh()
{
    auto st = gNet.state();     // NetState
    IPAddress ip = gNet.ip();   // 0.0.0.0 if offline
    int rssi = gNet.wifiRssi(); // or -127 if not Wi-Fi/invalid

    // Ask Nextion which page is currently active
    uint8_t page = gDisplay.currentPage();
    const bool onStatus = (page == HmiCmd::PAGE_STATUS);
    const bool onNetwork = (page == HmiCmd::PAGE_NETWORK);

    // If we're on neither Status nor Network, don't spam page-specific widgets
    if (!onStatus && !onNetwork)
    {
        return;
    }

    // Mode label (tvMode)
    const char *modeTxt = "OFFLINE";
    switch (st)
    {
    case NetState::WIFI_UP:
        modeTxt = "Wi-Fi";
        break;
    case NetState::LAN_UP:
        modeTxt = "Ethernet";
        break;
    case NetState::FALLBACK:
        modeTxt = "Fallback";
        break;
    default:
        modeTxt = "Offline";
        break;
    }
    gDisplay.setText("tvMode", modeTxt);

    // IP / LAN label (tvLan)
    gDisplay.setText("tvLan", ip.toString().c_str());

    // RSSI numeric label (tvRSSI) + pRSSI bars
    if (st == NetState::WIFI_UP && rssi != -127)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d dBm", rssi);
        gDisplay.setText("tvRSSI", buf);

        // Use NetManager helper to map dBm -> 0..4 bars
        int bars = gNet.rssiToBars(rssi);
        if (bars < 0)
            bars = 0;
        if (bars > 4)
            bars = 4;
        nxSetPicRSSI(static_cast<uint8_t>(bars));
    }
    else
    {
        // Not on Wi-Fi / no valid RSSI
        gDisplay.setText("tvRSSI", "NA");
        nxSetPicRSSI(0); // show 0 bars
    }

    // --- THESE TWO ARE NETWORK-PAGE-ONLY CONTROLS ---
    if (onNetwork)
    {
        if (st == NetState::WIFI_UP || st == NetState::LAN_UP)
        {
            gDisplay.setText("tvNetStatus", "Connected");
        }
        else
        {
            gDisplay.setText("tvNetStatus", "Not connected");
        }

        if (st == NetState::WIFI_UP)
        {
            gDisplay.sendCommand("bReconnect.txt=\"Disconnect\"");
        }
        else
        {
            gDisplay.sendCommand("bReconnect.txt=\"Connect\"");
        }
    }
}

// -----------------------------------------------------------------------------
// Helpers to apply preferences at runtime, etc.
// -----------------------------------------------------------------------------
void applyCouplerPrefsToRuntime()
{
    // Make sure counts and active index are sane
    gPrefs.numCouplers = clampNumCouplers(gPrefs.numCouplers);
    gPrefs.activeCoupler = clampActiveCoupler(gPrefs.activeCoupler, gPrefs.numCouplers);

    const int maxRuntimeCouplers =
        std::min<int>(gPrefs.numCouplers, static_cast<int>(g_sensors.size()));

    for (int i = 0; i < maxRuntimeCouplers; ++i)
    {
        const CouplerPreferences &cp = gPrefs.couplers[i];
        RfPowerSensor *sensor = g_sensors[i];
        if (!sensor)
            continue;

        Serial.printf(
            "Coupler[%d]: name=\"%s\" fwdRange=%.1fW refRange=%.1fW "
            "SWRtrip=%.2f refTrip=%.1fW fwdTrip=%.1fW mask=0x%02X\n",
            i, cp.name, cp.fwdBarRangeW, cp.refBarRangeW,
            cp.highSWRTrip, cp.highRefTripW, cp.highFwdTripW, cp.enabledTripMask);

        applyCouplerCalibrationToSensor(cp, *sensor);

        Serial.printf(
            "  -> sensor fullScale=%.1fW fwdCal=(m=%.6f b=%.3f) fwdTbl=%u "
            "refCal=(m=%.6f b=%.3f) refTbl=%u\n",
            cp.couplerFullScaleW,
            cp.fwdCal_m, cp.fwdCal_b, (unsigned)cp.fwdCalCount,
            cp.refCal_m, cp.refCal_b, (unsigned)cp.refCalCount);

        // 3) Protection thresholds honoring enabledTripMask
        const uint8_t mask = cp.enabledTripMask;
        g_protCfg.fwdMaxW[i] = (mask & TRIPMASK_HIGH_FWD) ? cp.highFwdTripW : 0.0f;
        g_protCfg.reflMaxW[i] = (mask & TRIPMASK_HIGH_REF) ? cp.highRefTripW : 0.0f;
        g_protCfg.swrMax[i] = (mask & TRIPMASK_HIGH_SWR) ? cp.highSWRTrip : 0.0f;
    }

    // Keep each sensor's CouplerType in sync with UserPreferences
    const int sensorCount = static_cast<int>(g_sensors.size());
    const int maxSensors = std::min<int>(COUPLERS_MAX, sensorCount);
    for (int i = 0; i < maxSensors; ++i)
    {
        RfPowerSensor *sensor = g_sensors[i];
        if (!sensor)
            continue;

        const CouplerPreferences &cp = gPrefs.couplers[i];
        sensor->setCouplerType(static_cast<CouplerType>(cp.couplerType));
    }

    // Zero out any unused entries above maxRuntimeCouplers
    for (int i = maxRuntimeCouplers; i < COUPLERS_MAX; ++i)
    {
        g_protCfg.fwdMaxW[i] = 0.0f;
        g_protCfg.reflMaxW[i] = 0.0f;
        g_protCfg.swrMax[i] = 0.0f;
    }

    g_protCfg.bootIgnoreMs = 1000;
    g_protCfg.gateOnTx = false;

    g_protection.setConfig(g_protCfg);
}

void applyAuxPrefsToRuntime()
{
    gAuxInputs.begin();

    // Precompute enum->u8 values once (matches cfg.tempModel type)
    const uint8_t kTempModelTherm = static_cast<uint8_t>(AuxTempModel::ThermistorBeta);
    const uint8_t kTempModelLin = static_cast<uint8_t>(AuxTempModel::Linear);

    for (uint8_t i = 0; i < NUM_AUX_INPUTS; ++i)
    {
        const AuxInputPreferences &ap = gPrefs.aux[i];

        // Keep SystemStatus aux name in sync with prefs
        strncpy(gStatus.auxInputs[i].name,
                ap.name,
                sizeof(gStatus.auxInputs[i].name) - 1);
        gStatus.auxInputs[i].name[sizeof(gStatus.auxInputs[i].name) - 1] = '\0';

        AuxInputConfig cfg{};
        cfg.type = ap.type;

        // Copy temp model first (prefs type is compatible with cfg.tempModel)
        cfg.tempModel = static_cast<uint8_t>(ap.tempModel);

        switch (ap.type)
        {
        case AuxInputType::Voltage:
            cfg.maxRange = ap.voltageMaxRange;
            cfg.cal_m = ap.cal_m;
            cfg.cal_b = ap.cal_b;
            break;

        case AuxInputType::Current:
            cfg.maxRange = ap.currentMaxRange;
            cfg.cal_m = ap.cal_m;
            cfg.cal_b = ap.cal_b;
            break;

        case AuxInputType::Temperature:
            cfg.maxRange = ap.temperatureMaxRange;

            cfg.thermSeriesOhms = ap.thermSeriesOhms;
            cfg.thermR0Ohms = ap.thermR0Ohms;
            cfg.thermBeta = ap.thermBeta;
            cfg.thermT0C = ap.thermT0C;

            // Option A: thermistor ignores cal_m / cal_b by design
            cfg.cal_m = 1.0f;
            cfg.cal_b = 0.0f;
            break;

        case AuxInputType::None:
        default:
            cfg.maxRange = 0.0f;
            cfg.cal_m = 1.0f;
            cfg.cal_b = 0.0f;
            break;
        }

        // ---- Enforce consistency (runtime safety net) ----
        if (cfg.type == AuxInputType::Temperature)
        {
            // Temperature must be thermistor model (Option A)
            if (cfg.tempModel != kTempModelTherm)
                cfg.tempModel = kTempModelTherm;
        }
        else
        {
            // Non-temperature inputs must not use thermistor model
            cfg.tempModel = kTempModelLin;
        }

        gAuxInputs.configureChannel(i, cfg);
    }
}

void applyAuxDigitalPrefsToRuntime()
{
    // Configure runtime from persisted prefs
    for (uint8_t i = 0; i < NUM_AUX_DIGITAL; ++i)
    {
        const AuxDigitalPreferences &dp = gPrefs.auxDigital[i];
        gAuxDigital.configureChannel(i, dp);
    }
}

// -----------------------------------------------------------------------------
// Arduino setup/loop
// -----------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    delay(50);

    Serial.println("=== SETUP START ===");
    Serial.printf("[Boot] reset_reason=%d\n", (int)esp_reset_reason());

    Serial.printf("[%s] v%s booting... (%s %s)\n",
                  APP_NAME, APP_VERSION, __DATE__, __TIME__);

    // Debug output eeprom size check
    Serial.printf("UserPreferences size=%u, EPR::SIZE=%d, sane=%s\n",
                  (unsigned)sizeof(UserPreferences),
                  EPR::SIZE,
                  prefsStorageIsSane() ? "yes" : "no");

    // Load prefs from EEPROM
    Serial.println("[Prefs] Loading user preferences...");
    loadUserPreferences(gPrefs);

    // Initialize OTA
    Serial.println("[OTA] Initializing OTA update service...");
    gOta.begin();

    // Fix up calibration if invalid
    Serial.println("[Cal] Validating sensor calibration...");
    restoreCouplerCalibration();

    Serial.println("[Prefs] User preferences loaded:");
    printUserPreferences(gPrefs, Serial); // optional debug

    // Apply coupler prefs to sensors + protection
    Serial.println("[Prefs] Applying coupler preferences to runtime...");
    applyCouplerPrefsToRuntime();

    // LEDs
    Serial.println("[Setup] Initializing status LEDs...");
    setStatusLed(true);
    setErrorLed(false);

    Serial.println("[Setup] Initializing sensors...");
    for (auto *s : g_sensors)
    {
        s->begin();
    }

    // Protection callback: this is the "heart" behavior

    Serial.println("[Setup] Configuring protection trip callback...");
    g_protection.setTripCallback([](const RayTripEvent &ev)
                                 {
                                     setErrorLed(true);
                                     setStatusLed(false);

                                     Serial.printf(
                                         "[PROT] Trip: type=%u source=%u thr=%.2f val=%.2f at %lu ms\r\n",
                                         (unsigned)ev.type,
                                         (unsigned)ev.source,
                                         ev.threshold,
                                         ev.value,
                                         (unsigned long)ev.timestamp);

                                     gUi.onProtectionTrip(ev); // <--- NEW
                                 });

    g_protection.begin();
    g_mode = AppMode::Idle;

    // Nextion UI
    // Nextion HMI (must be initialized before we try to talk to it)
    Serial.println("[Setup] Initializing Nextion display...");
    if (!gDisplay.begin(115200))
    {
        Serial.println("[HMI] NextionDisplay.begin() FAILED");
    }
    else
    {
        Serial.println("[HMI] NextionDisplay.begin() OK");
    }

    // Optional: turn this on temporarily for debugging to see raw 0x65/0x66/etc
    gDisplay.enableRxDump(false);

    // UI controller
    gUi.begin(115200);

    // Apply stored brightness to Nextion after giving the display a moment to initilize
    delay(250);
    uint8_t br = gPrefs.displayBrightness;
    gDisplay.setBrightness(br);

    // ----------------------------
    // Remote UART (Serial2)
    // ----------------------------
    UART_REMOTE.begin(115200, SERIAL_8N1, UART_REMOTE_RX_PIN, UART_REMOTE_TX_PIN);
    delay(50);
    gCommServer.begin();
    Serial.println("[Comm] Remote UART server started on Serial2");

    // Configure NetManager from stored prefs (if any)

    Serial.println("[Setup] Configuring network manager from stored preferences...");
    const bool wifiEnabled = ReleaseDefaults::wifiEnabled(gPrefs.wifiEnabled);
    gNet.configure(gPrefs.wifiSsid, gPrefs.wifiPass, wifiEnabled);

    if (wifiEnabled && gPrefs.wifiSsid[0] != '\0')
    {
        // Start network manager (non-blocking)
        gNet.begin();
    }
    else
    {
        // Make sure radio is off when Wi-Fi disabled or no SSID yet
        WiFi.disconnect(true);
        WiFi.mode(WIFI_MODE_NULL);
    }
    // Initialize pNetwork UI
    Serial.println("[Setup] Initializing network UI...");
    updateSetupWifiUi();
    Serial.println("[Setup] Initializing brightness UI...");
    updateSetupBrightnessUi();

    // Custom 0x62 command handler (toast + network)
    gDisplay.onCustomCommand([](uint8_t pageId, uint8_t type, uint8_t instance)
                             {
        if (type == HmiCmd::TYPE_NETWORK) {
            handleNetworkCustom(pageId, type, instance);
        } else if (type == HmiCmd::TYPE_SETUP) {
            handleSetupCustom(pageId, type, instance);
        } else if (type == HmiCmd::TYPE_CONFIG_ALARM) {
            handleAlarmConfigCustom(pageId, type, instance);
        } else if (type == HmiCmd::TYPE_STATUS) {
            handleStatusCustom(pageId, type, instance);
        } else if (type == HmiCmd::TYPE_CAL) {
            handleCalCustom(pageId, type, instance);
        } });

    // HTTP API

    gHttpServer.begin();

    Serial.println("[Setup] Initializing auxiliary inputs...");
    gAuxInputs.begin();

    Serial.println("[Setup] Initializing auxiliary digital inputs...");
    gAuxDigital.begin();

    // Configure auxiliary inputs (from persisted prefs)
    Serial.println("[Setup] Applying auxiliary input preferences to runtime...");
    // applyAuxPrefsToRuntime();

    Serial.println("[Setup] Applying auxiliary digital input preferences to runtime...");
    applyAuxDigitalPrefsToRuntime();

    // Initialize modulation scope controller
    Serial.println("[Setup] Initializing modulation scope controller...");
    gModScope.begin();
    RayModulation::begin();

    // Frequency meter
    Serial.println("[Setup] Initializing frequency meter...");
    gFreqMeter.begin();
    Serial.println("=== SETUP COMPLETE ===");
    Serial.println("[Setup] RayPowerMeter ready.");
}

void loop()
{
    // Main loop timing
    static uint32_t lastMs = 0;
    const uint32_t nowMs = millis();

    // --- HARD heartbeat: proves loop() is running even if Serial breaks ---
    static uint32_t hbLast = 0;
    if (millis() - hbLast >= 250)
    { // 4 Hz blink
        hbLast = millis();
        digitalWrite(LED_STATUS_PIN, !digitalRead(LED_STATUS_PIN)); // GPIO38
    }

    static uint32_t lastSampleMs = 0;
    static uint32_t lastNetUiMs = 0;
    static uint32_t lastCalUiMs = 0;
    const uint32_t now = millis();

    // Nextion UI state machine

    gDisplay.loop();

    // Network state machine (Wi-Fi/Ethernet)
    gNet.loop();

    // Remote UART server
    gCommServer.update(now);

    // HTTP server
    gHttpServer.update(now);

    // --- NEW: monitor Wi-Fi connect progress for pNetwork2 ---
    if (gWifiConnectInProgress)
    {
        wl_status_t st = WiFi.status();

        if (st == WL_CONNECTED)
        {
            gWifiConnectInProgress = false;

            // Update "Network" page labels
            uiNetworkRefresh();

            // Update pNetwork2 status text
            gDisplay.setText("tv2Status", "Connected");
            gDisplay.toast("Wi-Fi connected", 1500, HmiCmd::PAGE_NETWORK2);

            // Optional: automatically jump back to pNetwork
            // gDisplay.setPage(HmiCmd::PAGE_NETWORK);
        }
        else if ((now - gWifiConnectStartMs) > 15000UL)
        {
            // Simple timeout after 15 seconds
            gWifiConnectInProgress = false;
            gDisplay.setText("tv2Status", "Connect failed");
            gDisplay.toast("Wi-Fi timeout", 1500, HmiCmd::PAGE_NETWORK2);
        }
    }

    // Periodically refresh network UI (pStatus + pNetwork)
    if (now - lastNetUiMs >= 1000)
    { // every 1 s
        lastNetUiMs = now;
        uiNetworkRefresh();
    }

    if (now - lastCalUiMs >= 200)
    { // ~5 Hz is plenty
        lastCalUiMs = now;
        updateCalLiveUi();
    }

    // mDNS: start/stop based on IP presence
    const char *host =
        (gPrefs.hostname[0] != '\0') ? gPrefs.hostname : MDNS_HOSTNAME;
    mdnsEnsureStarted(host);

    // Sample RF power from all couplers at ~50 Hz (every 20 ms)
    if (now - lastSampleMs >= 20)
    {
        lastSampleMs = now;

        for (int i = 0; i < COUPLERS_MAX; ++i)
        {
            if (gPrefs.couplers[i].couplerType == CouplerType::NONE)
            {
                // Skip ADC reads for inactive sensors
                continue;
            }
            g_sensors[i]->update();
        }
    }

    // Core: run protection (heart of the app)
    const bool txActive = readTxActive();
    g_protection.update(txActive);

    // Update app mode + LEDs based on protection + TX state
    if (g_protection.isTripped())
    {
        if (g_mode != AppMode::FaultLatched)
        {
            g_mode = AppMode::FaultLatched;
            setErrorLed(true);
            setStatusLed(false);
        }
    }
    else
    {
        if (txActive)
        {
            g_mode = AppMode::TxActive;
            setStatusLed(true);
            setErrorLed(false);
        }
        else
        {
            g_mode = AppMode::Idle;
            setStatusLed(true);
            setErrorLed(false);
        }
    }

    // UI update
    gUi.update(now, g_mode);

    // AsyncWebServer (via RayHttpServer) handles HTTP in the background
    // AsyncCommunicationServer (via RayCommServer) handles Remote UART in background
    gHttpServer.update(now);

    // Update auxiliary inputs
    gAuxInputs.update();
    gAuxDigital.update();

    // Store in SystemStatus for API / UI
    for (int ch = 0; ch < NUM_AUX_INPUTS; ++ch)
    {
        AuxReading r = gAuxInputs.getChannel(ch);

        gStatus.auxInputs[ch].type = gAuxInputs.getType(ch);
        gStatus.auxInputs[ch].rawVolts = r.rawVolts;
        gStatus.auxInputs[ch].value = r.value;
    }

    // Store digital AUX in SystemStatus
    for (int ch = 0; ch < (int)NUM_AUX_DIGITAL; ++ch)
    {
        AuxDigitalStatus &st = gStatus.auxDigital[ch];

        const AuxDigitalPreferences &dp = gPrefs.auxDigital[ch];
        strncpy(st.name, dp.name, sizeof(st.name) - 1);
        st.name[sizeof(st.name) - 1] = '\0';

        st.mode = dp.mode;
        st.value = gAuxDigital.read(ch);
    }

    // Let ModScope collect samples when a capture is active
    gModScope.update();

    // Frequency meter update
    const uint32_t nowIs = millis();
    gFreqMeter.update(nowIs);

    // Mirror into SystemStatus for API use
    gStatus.rfFrequencyHz = gFreqMeter.getFrequencyHz();
    gStatus.rfFrequencyValid = gFreqMeter.isValid();
    gStatus.lastUpdateMs = now; // if you aren't already setting this elsewhere
}
