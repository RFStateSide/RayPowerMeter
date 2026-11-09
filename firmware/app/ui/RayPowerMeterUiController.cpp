// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "ui/RayPowerMeterUiController.h"
#include "core/Constants.h"

// ---- Small helpers ---------------------------------------------------------

static String formatFloat1(float v)
{
    if (isnan(v) || isinf(v))
        return String("NaN");
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", v);
    return String(buf);
}

static String formatSwr(float swr)
{
    swr = rpmClipVswr(swr);
    if (isnan(swr) || isinf(swr))
        return String("-.-");
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", swr);
    return String(buf);
}

static String formatWatts(float w)
{
    if (isnan(w) || isinf(w))
        return String("---");
    if (w < 1.0f)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f W", w);
        return String(buf);
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f W", w);
    return String(buf);
}

static String formatIp(const IPAddress &ip)
{
    if (!ip)
        return String("0.0.0.0");
    return ip.toString();
}

static uint8_t rssiToBars(int rssiDbm)
{
    // Simple 0..4 bar mapping, reserved for when you hook up an RSSI icon
    if (rssiDbm >= -55)
        return 4;
    if (rssiDbm >= -65)
        return 3;
    if (rssiDbm >= -75)
        return 2;
    if (rssiDbm >= -85)
        return 1;
    return 0;
}

// ---- RayPowerMeterUiController implementation -----------------------------

RayPowerMeterUiController::RayPowerMeterUiController(ui::NextionDisplay &display,
                                                     std::vector<RfPowerSensor *> &sensors,
                                                     RayProtectionController &protection,
                                                     SystemStatus &status,
                                                     UserPreferences &prefs)
    : _display(display),
      _sensors(sensors),
      _protection(protection),
      _status(status),
      _prefs(prefs)
{
}

bool RayPowerMeterUiController::begin(uint32_t baud)
{
    (void)baud; // reserved for future if you want to send "baud=115200" etc

    // Start on status page
    _display.setPage(HMI_PAGE_STATUS);

    // Clear alarms and network text
    _display.setText(HMI_COMP_ALARMS_TEXT, "NONE");
    _display.setText(HMI_COMP_LAN_TEXT, "Offline");

    // One-time init for pNetwork2
    _display.setText(HMI_COMP_NET2_STATUS, "Press Scan to search...");
    _display.setText(HMI_COMP_NET2_AP1, "");
    _display.setText(HMI_COMP_NET2_AP2, "");
    _display.setText(HMI_COMP_NET2_AP3, "");
    _display.setText(HMI_COMP_NET2_AP4, "");
    _display.setText(HMI_COMP_NET2_AP1_RSSI, "");
    _display.setText(HMI_COMP_NET2_AP2_RSSI, "");
    _display.setText(HMI_COMP_NET2_AP3_RSSI, "");
    _display.setText(HMI_COMP_NET2_AP4_RSSI, "");

    _lastUiUpdateMs = 0;
    _hasTrip = false;

    // Init NTP for RTC sync
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    return true;
}

void RayPowerMeterUiController::update(uint32_t nowMs, AppMode mode)
{
    // Let display parse incoming frames (touch, custom, page reports)
    _display.loop();

    // Throttle HMI updates (avoid hammering serial)
    if (nowMs - _lastUiUpdateMs < _uiUpdateInterval)
    {
        return;
    }
    _lastUiUpdateMs = nowMs;

    // Make updates page-aware so we don't poke components
    // that don't exist on the current page (avoids Nextion 0x1A).
    uint8_t page = _display.currentPage();

    switch (page)
    {
    case HMI_PAGE_STATUS:
        // Main measurement page: coupler rows + alarms + datetime + LAN text
        updateMainPage_();
        updateProfileSummary_();
        updateProtectionStatus_(mode);
        updateNetworkStrip_();
        for (int i = 0; i < HMI_NUM_COUPLERS; ++i)
        {
            bool active = (_prefs.couplers[i].couplerType != CouplerType::NONE);
            applyCouplerRowStyle_(i, active);
        }
        break;

    case HMI_PAGE_NETWORK:
        // /pNetwork – Wi-Fi status, IP, RSSI, etc.
        updateNetworkPages_();
        break;

    case HMI_PAGE_NETWORK2:
        // /pNetwork2 – AP scan results are mostly driven by scan callbacks
        // (You can add a periodic refresh here later if needed.)
        break;

    default:
        // Other pages: no periodic HMI writes for now
        break;
    }
}

void RayPowerMeterUiController::onProtectionTrip(const RayTripEvent &ev)
{
    _hasTrip = true;
    _lastTrip = ev;

    // Mark status struct so other code (logging, etc.) can see it.
    _status.protectionTriggered = true;
    _status.lastTripType = ev.type;
    _status.lastTripSource = ev.source;

    char buf[64];
    snprintf(buf, sizeof(buf), "Trip: type=%u src=%u",
             static_cast<unsigned>(ev.type),
             static_cast<unsigned>(ev.source));
    _status.lastAlarmMessage = buf;

    // Update alarm banner immediately *only* if we're on the status page.
    if (_display.currentPage() == HMI_PAGE_STATUS)
    {
        _display.setText(HMI_COMP_ALARMS_TEXT, _status.lastAlarmMessage);
    }
}

void RayPowerMeterUiController::handleTouch(uint8_t pageId,
                                            uint8_t componentId,
                                            uint8_t state)
{
    (void)state;

    // Stub for now. When you wire Nextion component IDs in,
    // you can switch on (pageId, componentId) here.
    if (pageId == HMI_PAGE_STATUS)
    {
        // Example: if you know the ID for bNetwork, you could:
        // if (componentId == <id_of_bNetwork>) _display.setPage(HMI_PAGE_NETWORK);
    }
}

RayTripSource RayPowerMeterUiController::indexToTripSource(int i)
{
    switch (i)
    {
    case 0:
        return RayTripSource::Coupler1;
    case 1:
        return RayTripSource::Coupler2;
    case 2:
        return RayTripSource::Coupler3;
    case 3:
        return RayTripSource::Coupler4;
    default:
        return RayTripSource::None;
    }
}

// ---- Private helpers -------------------------------------------------------

void RayPowerMeterUiController::updateMainPage_()
{
    // Map each coupler row to its component names
    struct Row
    {
        const char *fwd;
        const char *fwdU;
        const char *ref;
        const char *refU;
        const char *swr;
        const char *swrU;
        const char *stat; // pC?Stat
    };

    static const Row rows[HMI_NUM_COUPLERS] = {
        {HMI_COMP_C1_FWD, HMI_COMP_C1_FWD_U, HMI_COMP_C1_REF, HMI_COMP_C1_REF_U,
         HMI_COMP_C1_SWR, HMI_COMP_C1_SWR_U, HMI_COMP_C1_STAT},
        {HMI_COMP_C2_FWD, HMI_COMP_C2_FWD_U, HMI_COMP_C2_REF, HMI_COMP_C2_REF_U,
         HMI_COMP_C2_SWR, HMI_COMP_C2_SWR_U, HMI_COMP_C2_STAT},
        {HMI_COMP_C3_FWD, HMI_COMP_C3_FWD_U, HMI_COMP_C3_REF, HMI_COMP_C3_REF_U,
         HMI_COMP_C3_SWR, HMI_COMP_C3_SWR_U, HMI_COMP_C3_STAT},
        {HMI_COMP_C4_FWD, HMI_COMP_C4_FWD_U, HMI_COMP_C4_REF, HMI_COMP_C4_REF_U,
         HMI_COMP_C4_SWR, HMI_COMP_C4_SWR_U, HMI_COMP_C4_STAT},
    };

    // Nextion picture IDs for pC?Stat:
    // ID1 = OK, ID2 = Alarmed, ID3 = N/A
    constexpr uint16_t PIC_OK = 1;
    constexpr uint16_t PIC_ALRM = 2;
    constexpr uint16_t PIC_NA = 3;

    const size_t n = _sensors.size() < HMI_NUM_COUPLERS
                         ? _sensors.size()
                         : HMI_NUM_COUPLERS;

    for (size_t i = 0; i < HMI_NUM_COUPLERS; ++i)
    {
        const Row &row = rows[i];

        // Figure out if this coupler is configured as "None"
        CouplerType ctype = CouplerType::NONE;
        if (i < COUPLERS_MAX)
        {
            ctype = static_cast<CouplerType>(_prefs.couplers[i].couplerType);
        }

        // If there's no sensor in the vector OR coupler type is NONE → show N/A
        if (i >= n || _sensors[i] == nullptr || ctype == CouplerType::NONE)
        {
            _display.setText(row.fwd, "---");
            _display.setText(row.ref, "---");
            _display.setText(row.swr, "---");

            _display.setText(row.fwdU, "W");
            _display.setText(row.refU, "W");
            _display.setText(row.swrU, "");

            _display.setPic(row.stat, PIC_NA);
            continue;
        }

        // Active, configured coupler
        RfPowerSensor *s = _sensors[i];
        const float fwd = s->getForwardPowerWatts();
        const float ref = s->getReflectedPowerWatts();
        const float swr = s->getSwr();

        _display.setText(row.fwd, formatWatts(fwd));
        _display.setText(row.ref, formatWatts(ref));
        _display.setText(row.swr, formatSwr(swr));

        // Units (for now always W / W / SWR)
        _display.setText(row.fwdU, "W");
        _display.setText(row.refU, "W");
        _display.setText(row.swrU, "SWR");

        // Status icon:
        //  - Default = OK
        //  - If a protection trip is latched for this coupler, show ALARM
        uint16_t pic = PIC_OK;

        if (_status.protectionTriggered &&
            _status.lastTripSource == indexToTripSource(static_cast<int>(i)))
        {
            pic = PIC_ALRM;
        }

        _display.setPic(row.stat, pic);
    }
}

RayPowerMeterUiController::CouplerIconState
RayPowerMeterUiController::couplerIconState_(int index) const
{
    if (index < 0 || index >= COUPLERS_MAX)
    {
        return CouplerIconState::NA;
    }

    const CouplerPreferences &cp = _prefs.couplers[index];

    // 1) Coupler disabled → N/A icon
    if (cp.couplerType == CouplerType::NONE)
    {
        return CouplerIconState::NA;
    }

    // 2) If protection is tripped and this coupler was the source → ALARM
    if (_hasTrip)
    {
        int tripIdx = -1;
        switch (_lastTrip.source)
        {
        case RayTripSource::Coupler1:
            tripIdx = 0;
            break;
        case RayTripSource::Coupler2:
            tripIdx = 1;
            break;
        case RayTripSource::Coupler3:
            tripIdx = 2;
            break;
        case RayTripSource::Coupler4:
            tripIdx = 3;
            break;
        default:
            break;
        }

        if (tripIdx == index)
        {
            return CouplerIconState::ALARM;
        }
    }

    // 3) Otherwise, active & not tripped → OK
    return CouplerIconState::OK;
}

void RayPowerMeterUiController::updateCouplerStatusIcon_(int index)
{
    if (index < 0 || index >= COUPLERS_MAX)
    {
        return;
    }

    // Map index 0..3 -> component name "pC1Stat" .. "pC4Stat"
    char compName[16];
    snprintf(compName, sizeof(compName), "pC%dStat", index + 1);

    // Your image IDs: 1 = OK, 2 = Alarm, 3 = N/A
    static constexpr int PIC_OK = 1;
    static constexpr int PIC_ALARM = 2;
    static constexpr int PIC_NA = 3;

    CouplerIconState st = couplerIconState_(index);
    int picId = PIC_NA;

    switch (st)
    {
    case CouplerIconState::OK:
        picId = PIC_OK;
        break;
    case CouplerIconState::ALARM:
        picId = PIC_ALARM;
        break;
    case CouplerIconState::NA:
    default:
        picId = PIC_NA;
        break;
    }

    _display.setPic(compName, picId);
}

void RayPowerMeterUiController::updateProtectionStatus_(AppMode mode)
{
    (void)mode;

    // Simple banner: show last alarm if protection is triggered, else "NONE"
    if (_status.protectionTriggered && _status.lastAlarmMessage.length() > 0)
    {
        _display.setText(HMI_COMP_ALARMS_TEXT, _status.lastAlarmMessage);
    }
    else
    {
        _display.setText(HMI_COMP_ALARMS_TEXT, "NONE");
    }

    // Date/Time for footer
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    // Format: MM/DD/YYYY  HH:MM:SS
    char dtBuf[32];
    snprintf(dtBuf, sizeof(dtBuf), "%02d/%02d/%04d  %02d:%02d:%02d",
             t.tm_mon + 1,
             t.tm_mday,
             t.tm_year + 1900,
             t.tm_hour,
             t.tm_min,
             t.tm_sec);

    // No magic strings:
    _display.setText(HMI_COMP_DATETIME_TEXT, dtBuf);
}

void RayPowerMeterUiController::updateNetworkStrip_()
{
    wl_status_t st = WiFi.status();
    String lanText;
    int rssi = 0;

    if (st == WL_CONNECTED)
    {
        lanText = formatIp(WiFi.localIP());
        rssi = WiFi.RSSI();
    }
    else
    {
        lanText = "Offline";
    }

    _display.setText(HMI_COMP_LAN_TEXT, lanText);

    // If you later add an RSSI icon sprite (0..4), you can drive it here:
    // uint8_t bars = rssiToBars(rssi);
    (void)rssi;
}

void RayPowerMeterUiController::updateNetworkPages_()
{
    // Keep /pNetwork in sync with current Wi-Fi status
    wl_status_t st = WiFi.status();
    String modeStr = "OFF";
    String statusStr = "Offline";
    String ssidStr;
    String rssiStr;
    IPAddress ip = WiFi.localIP();

    if (st == WL_CONNECTED)
    {
        modeStr = "WiFi STA";
        statusStr = "Connected";
        ssidStr = WiFi.SSID();
        int rssi = WiFi.RSSI();
        char buf[16];
        snprintf(buf, sizeof(buf), "%d dBm", rssi);
        rssiStr = buf;
    }
    else if (st == WL_IDLE_STATUS)
    {
        modeStr = "WiFi STA";
        statusStr = "Idle";
    }
    else if (st == WL_DISCONNECTED)
    {
        modeStr = "WiFi STA";
        statusStr = "Disconnected";
    }

    _display.setText(HMI_COMP_NET_MODE, modeStr);
    _display.setText(HMI_COMP_NET_IP, formatIp(ip));
    _display.setText(HMI_COMP_NET_SSID, ssidStr);
    _display.setText(HMI_COMP_NET_RSSI, rssiStr);
    _display.setText(HMI_COMP_NET_STATUS, statusStr);
}

void RayPowerMeterUiController::renderTripSummary_()
{
    if (!_hasTrip)
    {
        return;
    }

    // Placeholder for future detail-page logic (e.g., pDetail / pProtection).
    // For now, onProtectionTrip() already ensured the banner is updated.
}

// Convert a coupler full-scale in watts into a short label like "200W" or "2kW"
static String shortPowerLabel(float fullScaleW)
{
    if (isnan(fullScaleW) || fullScaleW <= 0.0f)
    {
        return String("?");
    }

    if (fullScaleW >= 1000.0f)
    {
        float kw = fullScaleW / 1000.0f;
        int ik = int(kw + 0.5f); // round to nearest kW
        char buf[8];
        snprintf(buf, sizeof(buf), "%dkW", ik);
        return String(buf);
    }
    else
    {
        int w = int(fullScaleW + 0.5f); // round
        char buf[8];
        snprintf(buf, sizeof(buf), "%dW", w);
        return String(buf);
    }
}

String RayPowerMeterUiController::buildProfileSummary_() const
{
    String result;

    const uint8_t n = clampNumCouplers(_prefs.numCouplers);

    for (uint8_t i = 0; i < n; ++i)
    {
        const CouplerPreferences &cp = _prefs.couplers[i];

        // Build token "1=200W", "2=2kW", etc.
        String scale = shortPowerLabel(cp.couplerFullScaleW);

        char token[16];
        snprintf(token, sizeof(token), "%u=%s",
                 static_cast<unsigned>(i + 1),
                 scale.c_str());

        String trial = result;
        if (!trial.isEmpty())
            trial += ' ';
        trial += token;

        // Enforce 14-character limit
        if (trial.length() > 14)
        {
            // If we already have something, just add a '+' to indicate more
            if (!result.isEmpty())
            {
                if (result.length() < 14)
                    result += '+';
            }
            else
            {
                // Nothing yet; just truncate
                result = trial.substring(0, 14);
            }
            break;
        }

        result = trial;
    }

    if (result.isEmpty())
    {
        result = "No couplers";
    }

    return result;
}

void RayPowerMeterUiController::updateProfileSummary_()
{
    if (_display.currentPage() != HMI_PAGE_STATUS)
    {
        return;
    }

    String label = buildProfileSummary_();
    _display.setText(HMI_COMP_PROFILE_TEXT, label.c_str());
}

void RayPowerMeterUiController::applyCouplerRowStyle_(int row, bool active)
{
    static const char *BUTTON[] = {"bC1", "bC2", "bC3", "bC4"};
    static const char *LABEL_V[] = {"lC1V", "lC2V", "lC3V", "lC4V"};
    static const char *FWD[] = {"tvC1Fwd", "tvC2Fwd", "tvC3Fwd", "tvC4Fwd"};
    static const char *FUNIT[] = {"lC1FwdU", "lC2FwdU", "lC3FwdU", "lC4FwdU"};
    static const char *LABEL_I[] = {"lC1I", "lC2I", "lC3I", "lC4I"};
    static const char *REF[] = {"tvC1Ref", "tvC2Ref", "tvC3Ref", "tvC4Ref"};
    static const char *RUNIT[] = {"lC1RefU", "lC2RefU", "lC3RefU", "lC4RefU"};
    static const char *SWR[] = {"tvC1Swr", "tvC2Swr", "tvC3Swr", "tvC4Swr"};
    static const char *SWUNIT[] = {"lC1SwrU", "lC2SwrU", "lC3SwrU", "lC4SwrU"};

    const uint16_t ACTIVE_COLOR = 65535;   // White
    const uint16_t INACTIVE_COLOR = 33808; // Your selected dim gray-blue

    auto pcoColor = active ? ACTIVE_COLOR : INACTIVE_COLOR;

    _display.setTextPco(BUTTON[row], pcoColor);
    _display.setTextPco(LABEL_V[row], pcoColor);
    _display.setTextPco(FWD[row], pcoColor);
    _display.setTextPco(FUNIT[row], pcoColor);
    _display.setTextPco(LABEL_I[row], pcoColor);
    _display.setTextPco(REF[row], pcoColor);
    _display.setTextPco(RUNIT[row], pcoColor);
    _display.setTextPco(SWR[row], pcoColor);
    _display.setTextPco(SWUNIT[row], pcoColor);

    // Optional: blank unused values
    if (!active)
    {
        _display.setText(FWD[row], "---");
        _display.setText(REF[row], "---");
        _display.setText(SWR[row], "---");
    }
}
