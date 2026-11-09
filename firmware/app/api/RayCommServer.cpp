// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/ReleaseDefaults.h"
#include "api/CouplerCalibrationJson.h"
#include "core/eepromprefs.h"
#include "api/RayCommServer.h"

#include <vector>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <sys/time.h>

#include "core/Constants.h"
#include "hw/pinout.h"
#include "coupler/rfpowersensor.h"
#include "core/RayProtectionController.h"
#include "core/AppState.h"
#include "core/TripTypes.h"
#include "core/SystemStatus.h"
#include "core/userprefs.h"
#include "net/NetManager.h"
#include "core/ModulationScope.h"
#include "core/ModScopeController.h"
#include "ui/NextionDisplay.h"
#include "core/AuxDigital.h"
#include "core/OtaManager.h"
#include <WiFi.h>

// -----------------------------------------------------------------------------
// External globals
// -----------------------------------------------------------------------------

extern std::vector<RfPowerSensor *> g_sensors;
extern AppMode g_mode;
extern UserPreferences gPrefs;
extern RayProtectionController g_protection;
extern RayProtectionConfig g_protCfg;
extern void applyCouplerPrefsToRuntime();
extern void applyAuxPrefsToRuntime();
extern SystemStatus gStatus;
extern NetManager gNet;
extern ui::NextionDisplay gDisplay;
extern ModScopeController gModScope;
extern void applyAuxDigitalPrefsToRuntime();
extern AuxDigital gAuxDigital;
extern OtaManager gOta;

// -----------------------------------------------------------------------------
// Local helpers
// -----------------------------------------------------------------------------

// Human-readable coupler type for JSON
static const char *couplerTypeName(CouplerType t)
{
    switch (t)
    {
    case COUPLER_200W:
        return "200w";
    case COUPLER_2KW:
        return "2kw";
    case NONE:
    default:
        return "none";
    }
}

// Human-readable AuxInputType for JSON
static const char *auxInputTypeName(AuxInputType t)
{
    switch (t)
    {
    case AuxInputType::Voltage:
        return "voltage";
    case AuxInputType::Current:
        return "current";
    case AuxInputType::Temperature:
        return "temperature";
    case AuxInputType::None:
    default:
        return "none";
    }
}

static const char *auxTempModelName(AuxTempModel m)
{
    switch (m)
    {
    case AuxTempModel::ThermistorBeta:
        return "thermistor";
    case AuxTempModel::Linear:
    default:
        return "linear";
    }
}

static AuxTempModel auxTempModelFromString(const char *s)
{
    if (!s)
        return AuxTempModel::Linear;

    if (!strcasecmp(s, "thermistor"))
        return AuxTempModel::ThermistorBeta;

    return AuxTempModel::Linear;
}

static AuxInputType auxInputTypeFromString(const char *s)
{
    if (!s)
        return AuxInputType::None;

    if (!strcasecmp(s, "voltage"))
        return AuxInputType::Voltage;
    if (!strcasecmp(s, "current"))
        return AuxInputType::Current;
    if (!strcasecmp(s, "temperature"))
        return AuxInputType::Temperature;

    return AuxInputType::None;
}

static const char *auxDigitalModeName(AuxDigitalMode m)
{
    switch (m)
    {
    case AuxDigitalMode::Input:
        return "input";
    case AuxDigitalMode::Output:
        return "output";
    case AuxDigitalMode::Disabled:
    default:
        return "disabled";
    }
}

static AuxDigitalMode auxDigitalModeFromJson(JsonVariantConst v)
{
    if (v.is<const char *>())
    {
        const char *s = v.as<const char *>();
        if (strcasecmp(s, "input") == 0)
            return AuxDigitalMode::Input;
        if (strcasecmp(s, "output") == 0)
            return AuxDigitalMode::Output;
        return AuxDigitalMode::Disabled;
    }
    int n = v | 0;
    switch (n)
    {
    case 1:
        return AuxDigitalMode::Input;
    case 2:
        return AuxDigitalMode::Output;
    case 0:
    default:
        return AuxDigitalMode::Disabled;
    }
}

static const char *auxDigitalPullName(AuxDigitalPull p)
{
    switch (p)
    {
    case AuxDigitalPull::PullUp:
        return "pullup";
    case AuxDigitalPull::PullDown:
        return "pulldown";
    case AuxDigitalPull::None:
    default:
        return "none";
    }
}

static AuxDigitalPull auxDigitalPullFromJson(JsonVariantConst v)
{
    if (v.is<const char *>())
    {
        const char *s = v.as<const char *>();
        if (strcasecmp(s, "pullup") == 0)
            return AuxDigitalPull::PullUp;
        if (strcasecmp(s, "pulldown") == 0)
            return AuxDigitalPull::PullDown;
        return AuxDigitalPull::None;
    }
    int n = v | 0;
    switch (n)
    {
    case 1:
        return AuxDigitalPull::PullUp;
    case 2:
        return AuxDigitalPull::PullDown;
    case 0:
    default:
        return AuxDigitalPull::None;
    }
}

// Detect whether a coupler looks "calibrated" vs factory defaults
static bool isCouplerCalibrated(const CouplerPreferences &cp)
{
    return !(cp.fwdCal_m == kFactoryCal200_m && cp.fwdCal_b == kFactoryCal_b &&
             cp.refCal_m == kFactoryCal200_m && cp.refCal_b == kFactoryCal_b);
}

// Reset a single coupler's prefs to defaults for a given type.
// If overrideName is non-null, we set that as the name; otherwise we keep the
// existing name.
static void resetCouplerDefaults(CouplerPreferences &cp,
                                 CouplerType newType,
                                 const char *overrideName = nullptr)
{
    // Preserve or override name
    char nameBuf[sizeof(cp.name)];
    if (overrideName && overrideName[0])
    {
        strncpy(nameBuf, overrideName, sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
    }
    else
    {
        strncpy(nameBuf, cp.name, sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
    }

    cp.couplerType = newType;

    strncpy(cp.name, nameBuf, sizeof(cp.name) - 1);
    cp.name[sizeof(cp.name) - 1] = '\0';

    // Full-scale from constants table if in range
    float fullScale = 0.0f;
    uint8_t typeIdx = static_cast<uint8_t>(newType);
    if (typeIdx < (sizeof(COUPLER_FULL_SCALE) / sizeof(COUPLER_FULL_SCALE[0])))
    {
        fullScale = COUPLER_FULL_SCALE[typeIdx];
    }

    cp.couplerFullScaleW = fullScale;

    // Reasonable defaults per type
    switch (newType)
    {
    case COUPLER_200W:
    case COUPLER_2KW:
        cp.fwdBarRangeW = fullScale;
        cp.refBarRangeW = fullScale / 5.0f; // e.g. 20 W for 100 W
        cp.highFwdTripW = fullScale;
        cp.highRefTripW = fullScale / 5.0f;
        cp.highSWRTrip = 3.0f;
        cp.enabledTripMask = 0x07; // fwd/ref/SWR trips enabled
        break;

    case NONE:
    default:
        cp.fwdBarRangeW = 0.0f;
        cp.refBarRangeW = 0.0f;
        cp.highFwdTripW = 0.0f;
        cp.highRefTripW = 0.0f;
        cp.highSWRTrip = 0.0f;
        cp.enabledTripMask = 0x00;
        break;
    }

    // Reset calibration to identity
    cp.fwdCal_m = 1.0f;
    cp.fwdCal_b = 0.0f;
    cp.refCal_m = 1.0f;
    cp.refCal_b = 0.0f;
}



// -----------------------------------------------------------------------------
// App name/version defaults
// -----------------------------------------------------------------------------
#ifndef APP_NAME
#define APP_NAME "RayPowerMeter"
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

// -----------------------------------------------------------------------------
// RayCommServer implementation
// -----------------------------------------------------------------------------

RayCommServer::RayCommServer(Stream &serial)
    : _serial(serial)
{
}

void RayCommServer::begin()
{
    RayModulation::begin();

    _serial.println(F("[Comm] Serial JSON API ready."));
}

void RayCommServer::update(uint32_t /*nowMs*/)
{
    handleSerialRx();
    uint32_t nowMs = millis();

    if (_telemetryEnabled && _telemetryIntervalMs > 0)
    {
        if (nowMs - _lastTelemetryMs >= _telemetryIntervalMs)
        {
            _lastTelemetryMs = nowMs;
            sendTelemetrySnapshot(nowMs);
        }
    }

    const ModScopeMeta meta = gModScope.getMeta();

    if (meta.state == ModScopeState::Ready && !_modulationReadyNotified)
    {
        _modulationReadyNotified = true;

        StaticJsonDocument<256> doc;
        JsonObject root = doc.to<JsonObject>();
        root["type"] = "modulation_ready";
        root["coupler"] = meta.coupler;
        root["path"] = "fwd"; // ModScopeMeta does not track path yet
        root["samples"] = meta.sampleCount;
        root["sample_rate_hz"] = meta.sampleRateHz;

        sendJson(doc);
    }
    else if (meta.state == ModScopeState::Idle)
    {
        // Fully idle again -> allow next ready notification
        _modulationReadyNotified = false;
    }
}

// Build a compact RF-oriented status JSON.
void RayCommServer::buildStatusJson(JsonDocument &doc)
{
    JsonObject root = doc.to<JsonObject>();

    // Application info
    JsonObject app = root.createNestedObject("app");
    app["name"] = APP_NAME;
    app["version"] = APP_VERSION;

    // RF / protection state
    JsonObject rf = root.createNestedObject("rf");

    rf["mode"] = static_cast<uint8_t>(g_mode);
    rf["protection_on"] = true;
    rf["tripped"] = g_protection.isTripped();

    if (g_protection.isTripped())
    {
        RayTripEvent ev = g_protection.lastTrip();
        JsonObject trip = rf.createNestedObject("trip");
        trip["type"] = static_cast<uint8_t>(ev.type);
        trip["source"] = static_cast<uint8_t>(ev.source);
        trip["threshold"] = ev.threshold;
        trip["value"] = ev.value;
    }

    // Coupler telemetry: read directly from the RfPowerSensor objects
    JsonArray couplers = rf.createNestedArray("couplers");

    const size_t count = g_sensors.size();
    for (size_t i = 0; i < count; ++i)
    {
        RfPowerSensor *s = g_sensors[i];
        if (!s)
            continue;

        JsonObject c = couplers.createNestedObject();
        c["index"] = static_cast<uint8_t>(i);
        c["name"] = s->getName();

        c["fwd_w"] = s->getForwardPowerWatts();
        c["ref_w"] = s->getReflectedPowerWatts();
        c["swr"] = s->getSwr();

        c["peak_f"] = s->getPeakForwardPowerWatts();
        c["peak_r"] = s->getPeakReflectedPowerWatts();
    }

    // Auxiliary inputs (live values mirrored from SystemStatus)
    JsonArray auxArr = rf.createNestedArray("aux");
    for (int ch = 0; ch < NUM_AUX_INPUTS; ++ch)
    {
        const AuxInputStatus &st = gStatus.auxInputs[ch];

        JsonObject a = auxArr.createNestedObject();
        a["index"] = ch;
        a["name"] = st.name;
        a["type_id"] = static_cast<uint8_t>(st.type);
        a["type"] = auxInputTypeName(st.type);
        a["raw_volts"] = st.rawVolts; // 0..3.3 V (or ADC full-scale)
        a["value"] = st.value;        // engineering units (0..maxRange)
    }

    // Auxiliary digital I/O (live values from SystemStatus)
    JsonArray auxDigArr = rf.createNestedArray("aux_digital");
    for (int ch = 0; ch < (int)NUM_AUX_DIGITAL; ++ch)
    {
        const AuxDigitalStatus &st = gStatus.auxDigital[ch];

        JsonObject d = auxDigArr.createNestedObject();
        d["index"] = ch;
        d["name"] = st.name;
        d["mode_id"] = static_cast<uint8_t>(st.mode);

        const char *modeStr = "disabled";
        switch (st.mode)
        {
        case AuxDigitalMode::Input:
            modeStr = "input";
            break;
        case AuxDigitalMode::Output:
            modeStr = "output";
            break;
        case AuxDigitalMode::Disabled:
        default:
            break;
        }
        d["mode"] = modeStr;

        d["value"] = st.value;
    }
}

void RayCommServer::sendError(const char *msg, const char *code)
{
    StaticJsonDocument<256> doc;
    JsonObject root = doc.to<JsonObject>();

    root["type"] = "error";
    root["message"] = msg;
    root["code"] = code ? code : "error";

    sendJson(doc);
}

// -----------------------------------------------------------------------------
// Read line-delimited JSON commands from the serial stream
// -----------------------------------------------------------------------------
void RayCommServer::handleSerialRx()
{
    while (_serial.available() > 0)
    {
        char c = static_cast<char>(_serial.read());

        // Echo back to the remote terminal
        _serial.write(c);

        // End-of-line: CR or LF
        if (c == '\r' || c == '\n')
        {
            if (_rxLineOverflow)
            {
                _rxLineOverflow = false;
                sendError("command exceeds maximum line length", "request_too_large");
                continue;
            }
            if (_rxLine.length() == 0)
                continue;

            String line = _rxLine;
            _rxLine.clear();

            line.trim();
            if (line.length() == 0)
                continue;

            // Debug: show exactly what we're parsing
            // Serial.print(F("[CommDBG] RX2 line: "));
            Serial.println(line);

            StaticJsonDocument<512> doc;
            DeserializationError err = deserializeJson(doc, line);
            if (err)
            {
                // Serial.print(F("[CommDBG] JSON parse error: "));
                Serial.println(err.c_str());

                StaticJsonDocument<256> errDoc;
                errDoc["type"] = "error";
                errDoc["message"] = "invalid json";
                errDoc["detail"] = err.f_str();
                sendJson(errDoc);
            }
            else
            {
                handleCommand(doc);
            }
        }
        else
        {
            // Accumulate printable characters only (avoid 0x00 & friends)
            if (c >= 0x20 && c <= 0x7E)
            {
                if (_rxLineOverflow)
                    continue;
                if (_rxLine.length() < CouplerCalibrationJson::MaxRequestBytes)
                {
                    _rxLine += c;
                }
                else
                {
                    _rxLine.clear();
                    _rxLineOverflow = true;
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Handle one parsed JSON command
// -----------------------------------------------------------------------------
void RayCommServer::handleCommand(const JsonDocument &doc)
{
    using namespace ArduinoJson;

    // Helpful debug: dump the parsed document
    // Serial.print(F("[CommDBG] handleCommand raw JSON: "));
    serializeJson(doc, Serial);
    Serial.println();

    JsonVariantConst rootVar = doc.as<JsonVariantConst>();

    if (!rootVar.is<JsonObjectConst>())
    {
        // Serial.println(F("[CommDBG] Root is NOT an object"));
        sendError("root JSON is not an object", "root_not_object");
        return;
    }

    JsonObjectConst obj = rootVar.as<JsonObjectConst>();

    // Debug: list keys in the root object
    // Serial.println(F("[CommDBG] Root object keys:"));
    for (JsonPairConst kv : obj)
    {
        Serial.print(F("  key='"));
        Serial.print(kv.key().c_str());
        Serial.print(F("' value="));
        serializeJson(kv.value(), Serial);
        Serial.println();
    }

    // Be a bit forgiving about key casing: cmd / Cmd / CMD
    const char *cmd = nullptr;
    if (obj.containsKey("cmd"))
        cmd = obj["cmd"].as<const char *>();
    else if (obj.containsKey("Cmd"))
        cmd = obj["Cmd"].as<const char *>();
    else if (obj.containsKey("CMD"))
        cmd = obj["CMD"].as<const char *>();

    if (!cmd || !*cmd)
    {
        // Serial.println(F("[CommDBG] 'cmd' missing or empty"));
        sendError("missing 'cmd' field", "missing_cmd");
        return;
    }

    // Serial.printf("[CommDBG] handleCommand cmd=\"%s\"\r\n", cmd);

    // -------------------------------------------------------------------------
    // 1) get_status
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "get_status") == 0)
    {
        // Serial.println(F("[CommDBG] -> get_status"));
        StaticJsonDocument<1024> out;
        buildStatusJson(out);
        out["type"] = "status";
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // get_device_config
    //   {"cmd":"get_device_config"}
    // Mirrors HTTP GET /api/device/config
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "get_device_config") == 0)
    {
        StaticJsonDocument<256> out;
        JsonObject root = out.to<JsonObject>();

        root["type"] = "device_config";
        root["status"] = "ok";
        root["brightness"] = gPrefs.displayBrightness;

        time_t now = time(nullptr);
        root["unix_time"] = (uint32_t)now;

        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // set_device_config
    //   {"cmd":"set_device_config","brightness":70,"utc_epoch":1735600000}
    // Accepts: brightness, utc_epoch OR unix_time
    // Mirrors HTTP POST /api/device/config
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "set_device_config") == 0)
    {
        time_t epoch = 0;

        // ---- Apply brightness ----
        if (obj.containsKey("brightness"))
        {
            int br = obj["brightness"].as<int>();

            // Clamp: 10..100 (same as HTTP)
            if (br < 10)
                br = 10;
            if (br > 100)
                br = 100;

            gPrefs.displayBrightness = (uint8_t)br;

            // Apply to Nextion runtime brightness
            gDisplay.setBrightness(gPrefs.displayBrightness);

            char cmdBuf[32];
            snprintf(cmdBuf, sizeof(cmdBuf), "hBrightness.val=%u", (unsigned)gPrefs.displayBrightness);
            gDisplay.sendCommand(cmdBuf);
        }

        // ---- Apply time ----
        if (obj.containsKey("utc_epoch"))
            epoch = (time_t)obj["utc_epoch"].as<long long>();
        else if (obj.containsKey("unix_time"))
            epoch = (time_t)obj["unix_time"].as<long long>();

        if (epoch > 0)
        {
            // 1) Update Nextion RTC
            gDisplay.setRtcEpoch(epoch);

            // 2) Update ESP32 system clock (so logs/etc use it immediately)
            struct timeval tv{};
            tv.tv_sec = epoch;
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);
        }

        saveUserPreferences(gPrefs);

        StaticJsonDocument<256> out;
        JsonObject root = out.to<JsonObject>();
        root["type"] = "device_config";
        root["status"] = "ok";
        root["brightness"] = gPrefs.displayBrightness;
        if (epoch > 0)
            root["unix_time"] = (uint32_t)epoch;

        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 2) reset_protection
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "reset_protection") == 0)
    {
        // Serial.println(F("[CommDBG] -> reset_protection"));
        g_protection.reset();

        StaticJsonDocument<128> out;
        out["type"] = "protection_reset";
        out["status"] = "ok";
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 3) get_protection_config
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "get_protection_config") == 0)
    {
        // Serial.println(F("[CommDBG] -> get_protection_config"));
        RayProtectionConfig cfg = g_protection.getConfig();

        StaticJsonDocument<512> out;
        JsonObject rootOut = out.to<JsonObject>();

        JsonArray fwd = rootOut.createNestedArray("fwd_max_w");
        JsonArray ref = rootOut.createNestedArray("ref_max_w");
        JsonArray swr = rootOut.createNestedArray("swr_max");

        for (size_t i = 0; i < NUM_COUPLERS; ++i)
        {
            fwd.add(cfg.fwdMaxW[i]);
            ref.add(cfg.reflMaxW[i]);
            swr.add(cfg.swrMax[i]);
        }

        rootOut["boot_ignore_ms"] = cfg.bootIgnoreMs;
        rootOut["gate_on_tx"] = cfg.gateOnTx;
        rootOut["type"] = "protection_config";

        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 4) set_protection_config
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "set_protection_config") == 0)
    {
        // Serial.println(F("[CommDBG] -> set_protection_config"));
        RayProtectionConfig cfg = g_protection.getConfig();

        // fwd_max_w: [...]
        if (obj.containsKey("fwd_max_w"))
        {
            JsonArrayConst arr = obj["fwd_max_w"].as<JsonArrayConst>();
            size_t i = 0;
            for (JsonVariantConst v : arr)
            {
                if (i >= NUM_COUPLERS)
                    break;
                cfg.fwdMaxW[i++] = v.as<float>();
            }
        }

        // ref_max_w: [...]
        if (obj.containsKey("ref_max_w"))
        {
            JsonArrayConst arr = obj["ref_max_w"].as<JsonArrayConst>();
            size_t i = 0;
            for (JsonVariantConst v : arr)
            {
                if (i >= NUM_COUPLERS)
                    break;
                cfg.reflMaxW[i++] = v.as<float>();
            }
        }

        // swr_max: [...]
        if (obj.containsKey("swr_max"))
        {
            JsonArrayConst arr = obj["swr_max"].as<JsonArrayConst>();
            size_t i = 0;
            for (JsonVariantConst v : arr)
            {
                if (i >= NUM_COUPLERS)
                    break;
                cfg.swrMax[i++] = v.as<float>();
            }
        }

        if (obj.containsKey("boot_ignore_ms"))
        {
            cfg.bootIgnoreMs = obj["boot_ignore_ms"].as<uint32_t>();
        }
        if (obj.containsKey("gate_on_tx"))
        {
            cfg.gateOnTx = obj["gate_on_tx"].as<bool>();
        }

        g_protCfg = cfg;
        g_protection.setConfig(cfg);

        StaticJsonDocument<128> out;
        out["type"] = "protection_config";
        out["status"] = "ok";
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 5) get_coupler_config   (NEW)
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "get_coupler_config") == 0)
    {
        // Serial.println(F("[CommDBG] -> get_coupler_config"));

        int reqIndex = -1;
        if (obj.containsKey("index"))
        {
            reqIndex = obj["index"] | -1;
        }

        StaticJsonDocument<1024> out;
        JsonObject rootOut = out.to<JsonObject>();

        rootOut["type"] = "coupler_config";
        rootOut["num_couplers"] = gPrefs.numCouplers;
        rootOut["active_index"] = gPrefs.activeCoupler;

        JsonArray arr = rootOut.createNestedArray("couplers");

        auto emitCoupler = [&](int i)
        {
            if (i < 0 || i >= COUPLERS_MAX)
                return;
            const CouplerPreferences &cp = gPrefs.couplers[i];

            JsonObject c = arr.createNestedObject();
            c["index"] = i;
            c["name"] = cp.name;
            c["full_scale_w"] = cp.couplerFullScaleW;
            c["fwd_bar_range_w"] = cp.fwdBarRangeW;
            c["ref_bar_range_w"] = cp.refBarRangeW;
            c["high_fwd_trip_w"] = cp.highFwdTripW;
            c["high_ref_trip_w"] = cp.highRefTripW;
            c["high_swr_trip"] = cp.highSWRTrip;
            c["trip_mask"] = cp.enabledTripMask;
            c["type_id"] = static_cast<uint8_t>(cp.couplerType);
            c["type_name"] = couplerTypeName(static_cast<CouplerType>(cp.couplerType));
            c["calibrated"] = isCouplerCalibrated(cp);
            JsonObject cal = c["cal"].to<JsonObject>();
            CouplerCalibrationJson::writePath(cal["fwd"].to<JsonObject>(), cp, true);
            CouplerCalibrationJson::writePath(cal["ref"].to<JsonObject>(), cp, false);
        };

        if (reqIndex >= 0)
        {
            emitCoupler(reqIndex);
        }
        else
        {
            const int n = gPrefs.numCouplers;
            for (int i = 0; i < n; ++i)
                emitCoupler(i);
        }

        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 6) set_coupler_config   (NEW)
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "set_coupler_config") == 0)
    {
        Serial.println(F("[CommDBG] -> set_coupler_config"));

        const std::lock_guard<std::mutex> lock(gCalibrationMutex);
        const UserPreferences previous = gPrefs;

        // Optional: update number of couplers and active index
        if (obj.containsKey("num_couplers"))
        {
            gPrefs.numCouplers = obj["num_couplers"].as<int>();
        }
        if (obj.containsKey("active_index"))
        {
            gPrefs.activeCoupler = obj["active_index"].as<int>();
        }

        if (obj.containsKey("couplers"))
        {
            JsonArrayConst arr = obj["couplers"].as<JsonArrayConst>();
            for (JsonVariantConst v : arr)
            {
                if (!v.is<JsonObjectConst>())
                    continue;

                JsonObjectConst c = v.as<JsonObjectConst>();
                int idx = c["index"] | -1;
                if (idx < 0 || idx >= COUPLERS_MAX)
                    continue;

                CouplerPreferences &cp = gPrefs.couplers[idx];

                if (c.containsKey("name"))
                {
                    const char *nm = c["name"].as<const char *>();
                    if (nm)
                    {
                        strncpy(cp.name, nm, sizeof(cp.name) - 1);
                        cp.name[sizeof(cp.name) - 1] = '\0';
                    }
                }
                if (c.containsKey("fwd_bar_range_w"))
                    cp.fwdBarRangeW = c["fwd_bar_range_w"].as<float>();
                if (c.containsKey("ref_bar_range_w"))
                    cp.refBarRangeW = c["ref_bar_range_w"].as<float>();
                if (c.containsKey("high_fwd_trip_w"))
                    cp.highFwdTripW = c["high_fwd_trip_w"].as<float>();
                if (c.containsKey("high_ref_trip_w"))
                    cp.highRefTripW = c["high_ref_trip_w"].as<float>();
                if (c.containsKey("high_swr_trip"))
                    cp.highSWRTrip = c["high_swr_trip"].as<float>();
                if (c.containsKey("trip_mask"))
                    cp.enabledTripMask = c["trip_mask"].as<uint8_t>();
                if (c.containsKey("type_id"))
                {
                    const CouplerType oldType = static_cast<CouplerType>(cp.couplerType);
                    uint8_t t = c["type_id"].as<uint8_t>();
                    cp.couplerType = couplerTypeFromId(t);
                    ensureFactoryTableProfileIfAllowed((uint8_t)idx, oldType, static_cast<CouplerType>(cp.couplerType));
                }
                if (c.containsKey("full_scale_w"))
                    cp.couplerFullScaleW = c["full_scale_w"].as<float>();
            }
        }

        if (!saveUserPreferences(gPrefs))
        {
            gPrefs = previous;
            sendError("could not save coupler configuration", "save_failed");
            return;
        }

        // Re-apply prefs to sensors + protection thresholds
        applyCouplerPrefsToRuntime();

        StaticJsonDocument<256> out;
        JsonObject rootOut = out.to<JsonObject>();
        rootOut["type"] = "coupler_config";
        rootOut["status"] = "ok";
        rootOut["num_couplers"] = gPrefs.numCouplers;
        rootOut["active_index"] = gPrefs.activeCoupler;
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 7) calibrate_coupler
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "calibrate_coupler") == 0)
    {
        JsonDocument out;
        CouplerCalibrationJson::apply(obj, out);
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 8) set_active_coupler
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "set_active_coupler") == 0)
    {
        int index = doc["index"] | 0;

        if (index < 0 || index >= static_cast<int>(gPrefs.numCouplers))
        {
            sendError("invalid active coupler index", "bad_index");
            return;
        }

        gPrefs.activeCoupler = static_cast<uint8_t>(index);
        applyCouplerPrefsToRuntime(); // keep runtime & protection in sync

        StaticJsonDocument<128> out;
        out["type"] = "active_coupler";
        out["status"] = "ok";
        out["index"] = index;
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // get_aux_config
    //   {"cmd":"get_aux_config"}                 -> returns aux[] for all channels
    //   {"cmd":"get_aux_config","index":0}       -> returns aux[] with one entry
    // Mirrors HTTP GET /api/aux/config
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "get_aux_config") == 0)
    {
        const int idx = obj.containsKey("index") ? (obj["index"] | -1) : -1;
        if (idx != -1 && (idx < 0 || idx >= (int)NUM_AUX_INPUTS))
        {
            sendError("invalid aux index", "bad_index");
            return;
        }

        StaticJsonDocument<1024> out;
        JsonObject root = out.to<JsonObject>();
        root["type"] = "aux_config";
        root["status"] = "ok";

        JsonArray arr = root.createNestedArray("aux");

        for (int i = 0; i < (int)NUM_AUX_INPUTS; ++i)
        {
            if (idx != -1 && i != idx)
                continue;

            const auto &a = gPrefs.aux[i];
            JsonObject o = arr.createNestedObject();

            o["index"] = i;
            o["name"] = a.name;
            o["type"] = auxInputTypeName(a.type);
            o["tempModel"] = auxTempModelName(a.tempModel);

            o["voltageMaxRange"] = a.voltageMaxRange;
            o["currentMaxRange"] = a.currentMaxRange;
            o["temperatureMaxRange"] = a.temperatureMaxRange;

            o["thermSeriesOhms"] = a.thermSeriesOhms;
            o["thermR0Ohms"] = a.thermR0Ohms;
            o["thermBeta"] = a.thermBeta;
            o["thermT0C"] = a.thermT0C;

            o["cal_m"] = a.cal_m;
            o["cal_b"] = a.cal_b;
        }

        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // set_aux_config
    //   {"cmd":"set_aux_config","index":0,"type":"voltage","name":"Battery","voltageMaxRange":60}
    // Mirrors HTTP POST /api/aux/config (same fields)
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "set_aux_config") == 0)
    {
        const int idx = obj["index"] | -1;
        if (idx < 0 || idx >= (int)NUM_AUX_INPUTS)
        {
            sendError("invalid aux index", "bad_index");
            return;
        }

        auto &a = gPrefs.aux[idx];

        // Optional name
        if (obj.containsKey("name"))
        {
            const char *nm = obj["name"];
            if (nm && *nm)
            {
                strncpy(a.name, nm, sizeof(a.name) - 1);
                a.name[sizeof(a.name) - 1] = 0;
            }
        }

        // Type (string preferred, numeric accepted)
        if (obj.containsKey("type"))
        {
            const char *ts = obj["type"];
            a.type = auxInputTypeFromString(ts);
        }
        else if (obj.containsKey("type_id"))
        {
            a.type = (AuxInputType)((int)(obj["type_id"] | 0));
        }

        // Temp model (string preferred, numeric accepted)
        if (obj.containsKey("tempModel"))
        {
            const char *ms = obj["tempModel"];
            a.tempModel = auxTempModelFromString(ms);
        }
        else if (obj.containsKey("tempModel_id"))
        {
            a.tempModel = (AuxTempModel)((int)(obj["tempModel_id"] | 0));
        }

        if (obj.containsKey("voltageMaxRange"))
            a.voltageMaxRange = obj["voltageMaxRange"].as<float>();
        if (obj.containsKey("currentMaxRange"))
            a.currentMaxRange = obj["currentMaxRange"].as<float>();
        if (obj.containsKey("temperatureMaxRange"))
            a.temperatureMaxRange = obj["temperatureMaxRange"].as<float>();

        if (obj.containsKey("thermSeriesOhms"))
            a.thermSeriesOhms = obj["thermSeriesOhms"].as<float>();
        if (obj.containsKey("thermR0Ohms"))
            a.thermR0Ohms = obj["thermR0Ohms"].as<float>();
        if (obj.containsKey("thermBeta"))
            a.thermBeta = obj["thermBeta"].as<float>();
        if (obj.containsKey("thermT0C"))
            a.thermT0C = obj["thermT0C"].as<float>();

        if (obj.containsKey("cal_m"))
            a.cal_m = obj["cal_m"].as<float>();
        if (obj.containsKey("cal_b"))
            a.cal_b = obj["cal_b"].as<float>();

        // Enforce consistency (same as HTTP):
        if (a.type != AuxInputType::Temperature)
        {
            a.tempModel = AuxTempModel::Linear;
        }

        saveUserPreferences(gPrefs);
        applyAuxPrefsToRuntime();

        StaticJsonDocument<256> out;
        out["type"] = "aux_config";
        out["status"] = "ok";
        out["index"] = idx;
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // aux_calibrate_2point
    //   New:    {"cmd":"aux_calibrate_2point","index":0,"low":{"v":1.2,"eng":25},"high":{"v":2.0,"eng":85}}
    //   Legacy: {"cmd":"aux_calibrate_2point","index":0,"t1C":25,"t2C":85,"v1":1.2,"v2":2.0}
    // Behavior:
    //   - If legacy OR current channel type==Temperature => compute thermBeta/thermR0Ohms
    //   - Else (Voltage/Current) => compute cal_m/cal_b mapping rawEng -> desired eng
    // Matches HTTP /api/aux/calibrate/2point (Patch #2)
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "aux_calibrate_2point") == 0)
    {
        const int idx = obj["index"] | -1;
        if (idx < 0 || idx >= (int)NUM_AUX_INPUTS)
        {
            sendError("invalid aux index", "bad_index");
            return;
        }

        bool legacy = false;

        float eng1 = NAN;
        float eng2 = NAN;
        float v1 = NAN;
        float v2 = NAN;

        if (obj.containsKey("low") && obj.containsKey("high") &&
            obj["low"].is<JsonObjectConst>() && obj["high"].is<JsonObjectConst>())
        {
            JsonObjectConst lo = obj["low"].as<JsonObjectConst>();
            JsonObjectConst hi = obj["high"].as<JsonObjectConst>();

            v1 = lo["v"] | NAN;
            eng1 = lo["eng"] | NAN;

            v2 = hi["v"] | NAN;
            eng2 = hi["eng"] | NAN;
        }
        else
        {
            legacy = true;

            eng1 = obj["t1C"] | NAN;
            eng2 = obj["t2C"] | NAN;
            v1 = obj["v1"] | NAN;
            v2 = obj["v2"] | NAN;
        }

        if (!isfinite(eng1) || !isfinite(eng2) || !isfinite(v1) || !isfinite(v2))
        {
            sendError("missing/invalid calibration fields", "bad_fields");
            return;
        }

        // Voltage sanity
        if (v1 <= 0.01f || v2 <= 0.01f || v1 >= (ADC_VREF - 0.001f) || v2 >= (ADC_VREF - 0.001f))
        {
            sendError("invalid calibration voltages", "bad_voltage");
            return;
        }

        auto &a = gPrefs.aux[idx];

        // Legacy implies temperature thermistor calibration; otherwise depend on configured type.
        const bool doThermistor = legacy || (a.type == AuxInputType::Temperature);

        if (doThermistor)
        {
            const float t1C = eng1;
            const float t2C = eng2;

            if (!isfinite(t1C) || !isfinite(t2C) || fabsf(t2C - t1C) < 0.01f)
            {
                sendError("invalid temperature points", "bad_temp");
                return;
            }

            const float Rs = a.thermSeriesOhms;
            const float T1K = t1C + 273.15f;
            const float T2K = t2C + 273.15f;
            const float T0K = a.thermT0C + 273.15f;

            // Must match AuxInputs.cpp topology:
            // THERMISTOR_PULLUP_TO_VREF = true  (3.3V -> Rs -> ADC -> NTC -> GND)
            // V = Vref * (Rntc / (Rs + Rntc))
            // => Rntc = Rs * V / (Vref - V)
            const float R1 = Rs * (v1 / (ADC_VREF - v1));
            const float R2 = Rs * (v2 / (ADC_VREF - v2));

            if (!isfinite(R1) || !isfinite(R2) || R1 <= 0.0f || R2 <= 0.0f)
            {
                sendError("invalid thermistor resistance", "bad_r");
                return;
            }

            const float beta = logf(R1 / R2) / ((1.0f / T1K) - (1.0f / T2K));
            const float R0 = R1 * expf(-beta * ((1.0f / T1K) - (1.0f / T0K)));

            if (!isfinite(beta) || !isfinite(R0) || beta <= 0.0f || R0 <= 0.0f)
            {
                sendError("invalid beta/R0 result", "bad_result");
                return;
            }

            a.thermBeta = beta;
            a.thermR0Ohms = R0;
            a.tempModel = AuxTempModel::ThermistorBeta;
            a.type = AuxInputType::Temperature;

            saveUserPreferences(gPrefs);
            applyAuxPrefsToRuntime();

            StaticJsonDocument<256> out;
            out["type"] = "aux_calibrate_2point";
            out["status"] = "ok";
            out["index"] = idx;
            out["beta"] = beta;
            out["r0_ohms"] = R0;
            sendJson(out);
            return;
        }

        // -------------------------------------------------------------------------
        // get_aux_digital_config
        //   {"cmd":"get_aux_digital_config"}
        //   {"cmd":"get_aux_digital_config","index":0}
        // -------------------------------------------------------------------------
        if (strcmp(cmd, "get_aux_digital_config") == 0)
        {
            const int idx = obj.containsKey("index") ? (obj["index"] | -1) : -1;
            if (idx != -1 && (idx < 0 || idx >= (int)NUM_AUX_DIGITAL))
            {
                sendError("invalid aux_digital index", "bad_index");
                return;
            }

            StaticJsonDocument<512> out;
            JsonObject root = out.to<JsonObject>();
            root["type"] = "aux_digital_config";
            root["status"] = "ok";

            JsonArray arr = root.createNestedArray("aux_digital");
            for (int i = 0; i < (int)NUM_AUX_DIGITAL; ++i)
            {
                if (idx != -1 && i != idx)
                    continue;

                const auto &d = gPrefs.auxDigital[i];
                JsonObject o = arr.createNestedObject();

                o["index"] = i;
                o["name"] = d.name;
                o["mode_id"] = static_cast<uint8_t>(d.mode);
                o["mode"] = auxDigitalModeName(d.mode);
                o["pull_id"] = static_cast<uint8_t>(d.pull);
                o["pull"] = auxDigitalPullName(d.pull);
                o["defaultState"] = d.defaultState;
            }

            sendJson(out);
            return;
        }

        // -------------------------------------------------------------------------
        // set_aux_digital_config
        //   {"cmd":"set_aux_digital_config","index":0,"mode":"output","pull":"none","name":"RelayOut","defaultState":false}
        // -------------------------------------------------------------------------
        if (strcmp(cmd, "set_aux_digital_config") == 0)
        {
            int idx = obj["index"] | -1;
            if (idx < 0 || idx >= (int)NUM_AUX_DIGITAL)
            {
                sendError("invalid aux_digital index", "bad_index");
                return;
            }

            auto &d = gPrefs.auxDigital[idx];

            if (obj.containsKey("name"))
            {
                const char *nm = obj["name"];
                if (nm && *nm)
                {
                    strncpy(d.name, nm, sizeof(d.name) - 1);
                    d.name[sizeof(d.name) - 1] = '\0';
                }
            }

            if (obj.containsKey("mode"))
                d.mode = auxDigitalModeFromJson(obj["mode"]);
            if (obj.containsKey("mode_id"))
                d.mode = auxDigitalModeFromJson(obj["mode_id"]);

            if (obj.containsKey("pull"))
                d.pull = auxDigitalPullFromJson(obj["pull"]);
            if (obj.containsKey("pull_id"))
                d.pull = auxDigitalPullFromJson(obj["pull_id"]);

            if (obj.containsKey("defaultState"))
                d.defaultState = obj["defaultState"] | false;

            saveUserPreferences(gPrefs);
            applyAuxDigitalPrefsToRuntime();

            StaticJsonDocument<256> out;
            JsonObject root = out.to<JsonObject>();
            root["type"] = "aux_digital_config";
            root["status"] = "ok";
            root["index"] = idx;
            sendJson(out);
            return;
        }

        // -------------------------------------------------------------------------
        // get_aux_digital_state
        //   {"cmd":"get_aux_digital_state"}
        //   {"cmd":"get_aux_digital_state","index":0}
        // -------------------------------------------------------------------------
        if (strcmp(cmd, "get_aux_digital_state") == 0)
        {
            const int idx = obj.containsKey("index") ? (obj["index"] | -1) : -1;
            if (idx != -1 && (idx < 0 || idx >= (int)NUM_AUX_DIGITAL))
            {
                sendError("invalid aux_digital index", "bad_index");
                return;
            }

            StaticJsonDocument<512> out;
            JsonObject root = out.to<JsonObject>();
            root["type"] = "aux_digital_state";
            root["status"] = "ok";

            JsonArray arr = root.createNestedArray("aux_digital");
            for (int i = 0; i < (int)NUM_AUX_DIGITAL; ++i)
            {
                if (idx != -1 && i != idx)
                    continue;

                JsonObject o = arr.createNestedObject();
                o["index"] = i;
                o["value"] = gStatus.auxDigital[i].value;
            }

            sendJson(out);
            return;
        }

        // -------------------------------------------------------------------------
        // set_aux_digital_state
        //   {"cmd":"set_aux_digital_state","index":0,"value":true}
        // -------------------------------------------------------------------------
        if (strcmp(cmd, "set_aux_digital_state") == 0)
        {
            int idx = obj["index"] | -1;
            if (idx < 0 || idx >= (int)NUM_AUX_DIGITAL)
            {
                sendError("invalid aux_digital index", "bad_index");
                return;
            }

            bool value = obj["value"] | false;
            gAuxDigital.write(idx, value);

            StaticJsonDocument<256> out;
            JsonObject root = out.to<JsonObject>();
            root["type"] = "aux_digital_state";
            root["status"] = "ok";
            root["index"] = idx;
            root["value"] = value;
            sendJson(out);
            return;
        }

        // Linear calibration for Voltage/Current
        if (a.type != AuxInputType::Voltage && a.type != AuxInputType::Current)
        {
            sendError("aux type does not support linear calibration", "bad_type");
            return;
        }

        const float maxRange = (a.type == AuxInputType::Voltage) ? a.voltageMaxRange : a.currentMaxRange;

        const float x1 = (v1 / ADC_VREF) * maxRange;
        const float x2 = (v2 / ADC_VREF) * maxRange;

        if (!isfinite(x1) || !isfinite(x2) || fabsf(x2 - x1) < 0.0001f)
        {
            sendError("invalid calibration points", "bad_points");
            return;
        }

        const float m = (eng2 - eng1) / (x2 - x1);
        const float b = eng1 - (x1 * m);

        if (!isfinite(m) || !isfinite(b))
        {
            sendError("invalid cal_m/cal_b result", "bad_result");
            return;
        }

        a.cal_m = m;
        a.cal_b = b;

        saveUserPreferences(gPrefs);
        applyAuxPrefsToRuntime();

        StaticJsonDocument<256> out;
        out["type"] = "aux_calibrate_2point";
        out["status"] = "ok";
        out["index"] = idx;
        out["cal_m"] = m;
        out["cal_b"] = b;
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 9) save_prefs
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "save_prefs") == 0)
    {
        saveUserPreferences(gPrefs);

        StaticJsonDocument<128> out;
        out["type"] = "save_prefs";
        out["status"] = "ok";
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 10) reset_defaults (factory defaults)
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "reset_defaults") == 0)
    {
        setDefaultUserPreferences(gPrefs);
        saveUserPreferences(gPrefs);

        // Apply runtime pieces that are safe to update live
        applyCouplerPrefsToRuntime();
        applyAuxPrefsToRuntime();

        // Apply brightness immediately
        gDisplay.setBrightness(gPrefs.displayBrightness);
        {
            char cmdBuf[32];
            snprintf(cmdBuf, sizeof(cmdBuf), "hBrightness.val=%u", (unsigned)gPrefs.displayBrightness);
            gDisplay.sendCommand(cmdBuf);
        }

        StaticJsonDocument<256> out;
        out["type"] = "reset_defaults";
        out["status"] = "ok";
        out["reboot_required"] = true;
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // reset_aux_calibration
    //   {"cmd":"reset_aux_calibration","index":0}
    // Resets aux calibration back to factory defaults (per index).
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "reset_aux_calibration") == 0)
    {
        int ch = obj["index"] | -1;
        if (ch < 0 || ch >= (int)NUM_AUX_INPUTS)
        {
            sendError("invalid aux index", "bad_index");
            return;
        }

        // Static factory-default prefs snapshot (avoid large stack allocation)
        static bool s_defaultsInit = false;
        static UserPreferences s_defaults;
        if (!s_defaultsInit)
        {
            setDefaultUserPreferences(s_defaults);
            s_defaultsInit = true;
        }

        AuxInputPreferences &a = gPrefs.aux[ch];
        const AuxInputPreferences &d = s_defaults.aux[ch];

        a.cal_m = d.cal_m;
        a.cal_b = d.cal_b;

        if (a.type == AuxInputType::Temperature)
        {
            a.thermBeta = d.thermBeta;
            a.thermR0Ohms = d.thermR0Ohms;
        }

        saveUserPreferences(gPrefs);
        applyAuxPrefsToRuntime();

        StaticJsonDocument<256> out;
        out["type"] = "reset_aux_calibration";
        out["status"] = "ok";
        out["index"] = ch;
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 11) reset_coupler_defaults
    //
    // Reset a single coupler's preferences to defaults for its type.
    // Request:
    //   {"cmd":"reset_coupler_defaults","index":0}
    //   {"cmd":"reset_coupler_defaults","index":0,"type_id":1}
    //   {"cmd":"reset_coupler_defaults","index":0,"type_id":1,"name":"COUPLER 1"}
    //   {"cmd":"reset_coupler_defaults","index":0,"cal_source":"FactoryLinear"}   // optional
    //   {"cmd":"reset_coupler_defaults","index":0,"cal_source":"FactoryTable"}    // optional (default)
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "reset_coupler_defaults") == 0)
    {
        const std::lock_guard<std::mutex> lock(gCalibrationMutex);
        const UserPreferences previous = gPrefs;

        int index = obj["index"] | -1;
        if (index < 0 || index >= COUPLERS_MAX)
        {
            sendError("invalid coupler index", "bad_index");
            return;
        }

        CouplerPreferences &cp = gPrefs.couplers[index];

        // Target type: keep existing, or override via type_id
        CouplerType newType = static_cast<CouplerType>(cp.couplerType);
        if (obj.containsKey("type_id"))
        {
            uint8_t t = obj["type_id"].as<uint8_t>();
            newType = static_cast<CouplerType>(t);
        }

        const char *overrideName = nullptr;
        if (obj.containsKey("name"))
        {
            overrideName = obj["name"].as<const char *>();
        }

        // Optional: cal_source (default FactoryTable if omitted/invalid)
        CalSource calSource = CalSource::FactoryTable;
        if (obj.containsKey("cal_source"))
        {
            const char *calSourceStr = obj["cal_source"].as<const char *>();
            if (calSourceStr && calSourceStr[0])
            {
                if (!strcasecmp(calSourceStr, "FactoryLinear") || !strcasecmp(calSourceStr, "linear"))
                    calSource = CalSource::FactoryLinear;
                else if (!strcasecmp(calSourceStr, "FactoryTable") || !strcasecmp(calSourceStr, "table"))
                    calSource = CalSource::FactoryTable;
                else
                    calSource = CalSource::FactoryTable; // fallback
            }
        }

        resetCouplerDefaults(cp, newType, overrideName);

        // Explicitly apply factory calibration mode during factory reset.
        // This sets cal source + clears/loads tables as appropriate.
        applyFactoryResetCalSource((uint8_t)index, calSource, /*doFwd*/ true, /*doRef*/ true);

        if (!saveUserPreferences(gPrefs))
        {
            gPrefs = previous;
            sendError("could not save coupler configuration", "save_failed");
            return;
        }
        applyCouplerPrefsToRuntime();

        StaticJsonDocument<256> out;
        JsonObject rootOut = out.to<JsonObject>();
        rootOut["type"] = "reset_coupler_defaults";
        rootOut["status"] = "ok";
        rootOut["index"] = index;
        rootOut["type_id"] = static_cast<uint8_t>(newType);
        rootOut["type_name"] = couplerTypeName(newType);
        rootOut["cal_source"] = (calSource == CalSource::FactoryLinear) ? "FactoryLinear" : "FactoryTable";
        rootOut["calibrated"] = isCouplerCalibrated(cp);
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 12) reset_all_couplers_defaults
    //
    // Reset ALL configured couplers to defaults, preserving their current type
    // (unless you later extend this to accept a global type override).
    // Request:
    //   {"cmd":"reset_all_couplers_defaults"}
    //   {"cmd":"reset_all_couplers_defaults","cal_source":"FactoryLinear"}   // optional
    //   {"cmd":"reset_all_couplers_defaults","cal_source":"FactoryTable"}    // optional (default)
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "reset_all_couplers_defaults") == 0)
    {
        const std::lock_guard<std::mutex> lock(gCalibrationMutex);
        const UserPreferences previous = gPrefs;

        int count = static_cast<int>(gPrefs.numCouplers);
        if (count > COUPLERS_MAX)
            count = COUPLERS_MAX;

        // Optional: cal_source (default FactoryTable if omitted/invalid)
        CalSource calSource = CalSource::FactoryTable;
        if (obj.containsKey("cal_source"))
        {
            const char *calSourceStr = obj["cal_source"].as<const char *>();
            if (calSourceStr && calSourceStr[0])
            {
                if (!strcasecmp(calSourceStr, "FactoryLinear") || !strcasecmp(calSourceStr, "linear"))
                    calSource = CalSource::FactoryLinear;
                else if (!strcasecmp(calSourceStr, "FactoryTable") || !strcasecmp(calSourceStr, "table"))
                    calSource = CalSource::FactoryTable;
                else
                    calSource = CalSource::FactoryTable; // fallback
            }
        }

        for (int i = 0; i < count; ++i)
        {
            CouplerPreferences &cp = gPrefs.couplers[i];
            CouplerType t = static_cast<CouplerType>(cp.couplerType);

            resetCouplerDefaults(cp, t); // preserve existing name

            // Explicitly apply factory calibration mode during factory reset.
            applyFactoryResetCalSource((uint8_t)i, calSource, /*doFwd*/ true, /*doRef*/ true);
        }

        if (!saveUserPreferences(gPrefs))
        {
            gPrefs = previous;
            sendError("could not save coupler configuration", "save_failed");
            return;
        }
        applyCouplerPrefsToRuntime();

        StaticJsonDocument<256> out;
        JsonObject rootOut = out.to<JsonObject>();
        rootOut["type"] = "reset_all_couplers_defaults";
        rootOut["status"] = "ok";
        rootOut["count"] = count;
        rootOut["cal_source"] = (calSource == CalSource::FactoryLinear) ? "FactoryLinear" : "FactoryTable";
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 13) get_calibration
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "get_calibration") == 0)
    {
        int index = obj["index"] | -1;
        if (index < 0 || index >= COUPLERS_MAX)
        {
            sendError("invalid coupler index", "bad_index");
            return;
        }

        const CouplerPreferences &cp = gPrefs.couplers[index];

        StaticJsonDocument<768> out;
        JsonObject rootOut = out.to<JsonObject>();
        rootOut["type"] = "calibration";
        rootOut["index"] = index;
        rootOut["name"] = cp.name;

        CouplerCalibrationJson::writePath(rootOut["fwd"].to<JsonObject>(), cp, true);
        CouplerCalibrationJson::writePath(rootOut["ref"].to<JsonObject>(), cp, false);

        rootOut["calibrated"] = isCouplerCalibrated(cp);
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 14) set_calibration
    //
    // Supports BOTH:
    //  - Legacy m/b:
    //      {"cmd":"set_calibration","index":0,"fwd":{"m":..,"b":..},"ref":{"m":..,"b":..}}
    //  - New table points:
    //      {"cmd":"set_calibration","index":0,"fwd":{"points":[{"v":..,"w":..},...]}, "ref":{...}}
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "set_calibration") == 0)
    {
        int index = obj["index"] | -1;
        if (index < 0 || index >= COUPLERS_MAX)
        {
            sendError("invalid coupler index", "bad_index");
            return;
        }

        JsonDocument validation;
        CalibrationInput forward, reflected;
        const bool hasForward = !obj["fwd"].isUnbound();
        const bool hasReflected = !obj["ref"].isUnbound();
        if ((hasForward && CouplerCalibrationJson::readCalibrationInput(obj["fwd"], forward, validation) != 200) ||
            (hasReflected && CouplerCalibrationJson::readCalibrationInput(obj["ref"], reflected, validation) != 200))
        {
            sendJson(validation);
            return;
        }
        const auto result = setCouplerCalibration(index, hasForward ? &forward : nullptr,
                                                  hasReflected ? &reflected : nullptr);
        if (result != CalibrationResult::Ok)
        {
            CouplerCalibrationJson::resultError(validation, result);
            sendJson(validation);
            return;
        }

        const CouplerPreferences &cp = gPrefs.couplers[index];
        StaticJsonDocument<256> out;
        JsonObject rootOut = out.to<JsonObject>();
        rootOut["type"] = "calibration";
        rootOut["status"] = "ok";
        rootOut["index"] = index;
        rootOut["calibrated"] = isCouplerCalibrated(cp);
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 14b) reset_calibration
    //
    // Reset calibration back to factory defaults for fwd/ref/both.
    // Now supports explicit CalSource selection:
    //   - FactoryLinear (2-point)
    //   - FactoryTable  (multi-point factory profile; default if omitted)
    //
    // Request:
    //   {"cmd":"reset_calibration","index":0}
    //   {"cmd":"reset_calibration","index":0,"path":"fwd"|"ref"|"both"}
    //   {"cmd":"reset_calibration","index":0,"cal_source":"FactoryLinear"}   // optional
    //   {"cmd":"reset_calibration","index":0,"cal_source":"FactoryTable"}    // optional (default)
    //   {"cmd":"reset_calibration","index":0,"path":"fwd","cal_source":"linear"}
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "reset_calibration") == 0)
    {
        JsonDocument out;
        if (CouplerCalibrationJson::reset(obj, out) == 200)
            out["calibrated"] = isCouplerCalibrated(gPrefs.couplers[out["index"].as<int>()]);
        sendJson(out);
        return;
    }

    if (strcmp(cmd, "copy_coupler_calibration") == 0 || strcmp(cmd, "copy_calibration") == 0)
    {
        JsonDocument out;
        CouplerCalibrationJson::copyLinear(obj, out);
        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // 15) set_telemetry
    //
    // Enable/disable periodic telemetry push.
    // Request:
    //   {"cmd":"set_telemetry","enabled":true,"interval_ms":200}
    // -------------------------------------------------------------------------
    if (strcmp(cmd, "set_telemetry") == 0)
    {
        bool enabled = obj["enabled"] | false;
        uint32_t interval = obj["interval_ms"] | 0;

        if (enabled && interval < 50)
            interval = 50; // basic safety bound

        _telemetryEnabled = enabled;
        _telemetryIntervalMs = interval;
        if (!enabled)
            _lastTelemetryMs = 0;

        StaticJsonDocument<128> out;
        out["type"] = "telemetry_config";
        out["status"] = "ok";
        out["enabled"] = _telemetryEnabled;
        out["interval_ms"] = _telemetryIntervalMs;
        sendJson(out);
        return;
    }

    // 16) start_modulation_capture
    // {
    //    "cmd": "start_modulation_capture",
    //    "coupler": 0,              // 0..3
    //    "path": "fwd" | "ref",     // optional, default "fwd"
    //    "samples": 1024,           // optional
    //    "sample_rate_hz": 10000.0  // optional
    // }
    if (strcmp(cmd, "start_modulation_capture") == 0)
    {
        // Mirror HTTP POST /api/modulation/start using ModScopeController backend.
        // Request:
        // {"cmd":"start_modulation_capture","coupler":0,"path":"fwd","samples":1024,"sample_rate_hz":20000}
        // Note: path is accepted for UI compatibility, but ModScopeConfig currently does not track it.
        const uint8_t couplerIndex = obj["coupler"] | 0;

        const char *path = obj["path"] | "fwd";
        const bool forward = !(path && strcmp(path, "ref") == 0);
        (void)forward; // not used by ModScope yet

        ModScopeConfig cfg;
        cfg.couplerIndex = couplerIndex;

        uint16_t samples = obj["samples"] | 1024;
        if (samples < MODSCOPE_MIN_SAMPLES)
            samples = MODSCOPE_MIN_SAMPLES;
        if (samples > MODSCOPE_MAX_SAMPLES)
            samples = MODSCOPE_MAX_SAMPLES;
        cfg.sampleCount = samples;

        cfg.sampleRateHz = obj["sample_rate_hz"] | cfg.sampleRateHz;

        const bool ok = gModScope.startCapture(cfg);
        uint32_t acceptedRate = cfg.sampleRateHz;
        if (ok)
            acceptedRate = gModScope.getMeta().sampleRateHz;

        _modulationReadyNotified = false; // new capture => reset ready latch

        StaticJsonDocument<256> out;
        JsonObject rootOut = out.to<JsonObject>();
        rootOut["type"] = "modulation_start";
        rootOut["status"] = ok ? "ok" : "busy";
        rootOut["coupler"] = couplerIndex;
        rootOut["path"] = forward ? "fwd" : "ref";
        rootOut["samples"] = cfg.sampleCount;
        rootOut["sample_rate_hz"] = acceptedRate;

        sendJson(out);
        return;
    }

    // 17) get_modulation_status
    //  -> returns armed/busy/ready and metadata
    if (strcmp(cmd, "get_modulation_status") == 0)
    {
        const ModScopeMeta meta = gModScope.getMeta();

        StaticJsonDocument<256> out;
        JsonObject rootOut = out.to<JsonObject>();
        rootOut["type"] = "modulation_status";

        const char *statusStr = "idle";
        switch (meta.state)
        {
        case ModScopeState::Capturing:
            statusStr = "busy";
            break;
        case ModScopeState::Ready:
            statusStr = "ready";
            break;
        case ModScopeState::Error:
            statusStr = "error";
            break;
        case ModScopeState::Idle:
        default:
            statusStr = "idle";
            break;
        }
        rootOut["status"] = statusStr;

        rootOut["coupler"] = meta.coupler;
        rootOut["path"] = "fwd"; // ModScopeMeta does not track path yet
        rootOut["samples"] = meta.sampleCount;
        rootOut["sample_rate_hz"] = meta.sampleRateHz;

        sendJson(out);
        return;
    }

    // 18) get_modulation_data
    // Request:
    //   {"cmd":"get_modulation_data","max_samples":512}
    //
    // Response:
    //   {
    //     "type":"modulation_data",
    //     "count": N,
    //     "sample_rate_hz": 10000.0,
    //     "coupler": 0,
    //     "path": "fwd",
    //     "samples":[<int16>, ...]
    //   }
    if (strcmp(cmd, "get_modulation_data") == 0)
    {
        const ModScopeMeta meta = gModScope.getMeta();

        if (meta.state != ModScopeState::Ready)
        {
            sendError("modulation capture not ready", "mod_not_ready");
            return;
        }

        uint16_t total = meta.sampleCount;
        if (total == 0)
        {
            sendError("no modulation samples", "mod_empty");
            return;
        }

        // Accept both: max_samples (preferred) and max (alias)
        uint16_t maxSamples = total;
        if (obj.containsKey("max_samples"))
            maxSamples = obj["max_samples"] | total;
        else if (obj.containsKey("max"))
            maxSamples = obj["max"] | total;

        if (maxSamples > total)
            maxSamples = total;

        size_t capacity = JSON_ARRAY_SIZE(maxSamples) + 256;
        DynamicJsonDocument out(capacity);

        JsonObject rootOut = out.to<JsonObject>();
        rootOut["type"] = "modulation_data";
        rootOut["sample_rate_hz"] = meta.sampleRateHz;
        rootOut["coupler"] = meta.coupler;
        rootOut["path"] = "fwd"; // ModScopeMeta does not track path yet

        JsonArray arr = rootOut.createNestedArray("samples");

        static uint16_t tmpBuf[MODSCOPE_MAX_SAMPLES];
        const uint16_t copied = gModScope.copySamples(tmpBuf, maxSamples);

        rootOut["count"] = copied;

        for (uint16_t i = 0; i < copied; ++i)
            arr.add(tmpBuf[i]);

        sendJson(out);
        return;
    }

    if (strcmp(cmd, "ota_status") == 0)
    {
        StaticJsonDocument<384> out;
        JsonObject root = out.to<JsonObject>();
        root["cmd"] = "ota_status";
        root["ok"] = true;
        JsonObject ota = root.createNestedObject("ota");
        gOta.toJson(ota);
        sendJson(out);
        return;
    }

    if (strcmp(cmd, "ota_start") == 0)
    {
        StaticJsonDocument<256> out;
        JsonObject root = out.to<JsonObject>();
        root["cmd"] = "ota_start";

        if (!ReleaseDefaults::legacyOtaEnabled)
        {
            root["ok"] = false;
            root["error"] = ReleaseDefaults::otaDisabledMessage;
            sendJson(out);
            return;
        }

        const char *url = obj["url"] | "";
        if (!url || !*url)
        {
            root["ok"] = false;
            root["error"] = "missing url";
            sendJson(out);
            return;
        }

        if (WiFi.status() != WL_CONNECTED)
        {
            root["ok"] = false;
            root["error"] = "wifi not connected";
            sendJson(out);
            return;
        }

        const bool started = gOta.startFromUrl(String(url));
        root["ok"] = started;
        if (!started)
            root["error"] = "ota busy or invalid url";

        sendJson(out);
        return;
    }

    // -------------------------------------------------------------------------
    // Unknown command
    // -------------------------------------------------------------------------
    // Serial.println(F("[CommDBG] -> unknown_cmd"));
    sendError("unknown command", "unknown_cmd");
}

void RayCommServer::sendTelemetrySnapshot(uint32_t nowMs)
{
    StaticJsonDocument<512> doc;
    JsonObject root = doc.to<JsonObject>();

    root["type"] = "telemetry";
    root["ts_ms"] = nowMs;

    // RF frequency from SystemStatus
    root["freq_hz"] = gStatus.rfFrequencyHz;
    root["freq_valid"] = gStatus.rfFrequencyValid;

    JsonArray couplers = root.createNestedArray("couplers");

    const size_t count = g_sensors.size();
    for (size_t i = 0; i < count; ++i)
    {
        RfPowerSensor *s = g_sensors[i];
        if (!s)
            continue;

        JsonObject c = couplers.createNestedObject();
        c["index"] = static_cast<uint8_t>(i);
        c["fwd_w"] = s->getForwardPowerWatts();
        c["ref_w"] = s->getReflectedPowerWatts();
        c["swr"] = s->getSwr();
    }

    // Aux snapshot (same structure as rf.aux[] in get_status)
    JsonArray auxArr = root.createNestedArray("aux");
    for (int ch = 0; ch < NUM_AUX_INPUTS; ++ch)
    {
        const AuxInputStatus &st = gStatus.auxInputs[ch];

        JsonObject a = auxArr.createNestedObject();
        a["index"] = ch;
        a["type_id"] = static_cast<uint8_t>(st.type);
        a["type"] = auxInputTypeName(st.type);
        a["raw_volts"] = st.rawVolts;
        a["value"] = st.value;
    }

    sendJson(doc);
}
