// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/ReleaseDefaults.h"
#include "api/CouplerCalibrationJson.h"
#include "core/eepromprefs.h"
#include <AsyncJson.h>
#include "api/RayHttpServer.h"

#include <ArduinoJson.h>
#include <sys/time.h>
#include <time.h>
#include <strings.h>
#include <math.h>

#include "core/Constants.h"
#include "core/userprefs.h"
#include "core/RayProtectionController.h"
#include "core/AppState.h"
#include "core/SystemStatus.h"
#include "coupler/rfpowersensor.h"
#include "net/NetManager.h"
#include "core/ModulationScope.h"
#include "core/ModScopeController.h"
#include "ui/NextionDisplay.h"
#include "core/AuxDigital.h"
#include "core/OtaManager.h"
#include <WiFi.h>

// -----------------------------------------------------------------------------
// Externals from main.cpp and other modules
// -----------------------------------------------------------------------------
extern std::vector<RfPowerSensor *> g_sensors;
extern AppMode g_mode;
extern UserPreferences gPrefs;
extern RayProtectionController g_protection;
extern RayProtectionConfig g_protCfg;
extern SystemStatus gStatus;
extern NetManager gNet;
extern ui::NextionDisplay gDisplay;

extern void applyCouplerPrefsToRuntime();
extern ModScopeController gModScope;
extern void applyAuxPrefsToRuntime();
extern void applyAuxDigitalPrefsToRuntime();
extern AuxDigital gAuxDigital;
extern OtaManager gOta;

// -----------------------------------------------------------------------------
// App name/version defaults (same as RayCommServer / main.cpp)
// -----------------------------------------------------------------------------
#ifndef APP_NAME
#define APP_NAME "RayPowerMeter"
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

// -----------------------------------------------------------------------------
// Local helpers
// -----------------------------------------------------------------------------

// Map CouplerType -> human/string name for JSON
static const char *couplerTypeNameLocal(CouplerType t)
{
    switch (t)
    {
    case CouplerType::NONE:
        return "NONE";
    case CouplerType::COUPLER_200W:
        return "COUPLER_200W";
    case CouplerType::COUPLER_2KW:
        return "COUPLER_2KW";
    default:
        return "UNKNOWN";
    }
}

// Map AuxInputType -> string name for JSON
static const char *auxInputTypeNameLocal(AuxInputType t)
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

static const char *auxDigitalModeName_(AuxDigitalMode m)
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

static AuxDigitalMode auxDigitalModeFromJson_(JsonVariantConst v)
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

static const char *auxDigitalPullName_(AuxDigitalPull p)
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

static AuxDigitalPull auxDigitalPullFromJson_(JsonVariantConst v)
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

static void sendJsonDoc(AsyncWebServerRequest *request,
                        JsonDocument &doc,
                        int statusCode = 200)
{
    String payload;
    serializeJson(doc, payload);

    AsyncWebServerResponse *resp =
        request->beginResponse(statusCode, "application/json", payload);
    RayHttpServer::addCorsHeaders(resp);
    request->send(resp);
}

// Common error payload
static void sendJsonError(AsyncWebServerRequest *request,
                          const char *msg,
                          const char *code = "error",
                          int statusCode = 400)
{
    StaticJsonDocument<256> doc;
    JsonObject root = doc.to<JsonObject>();
    root["type"] = "error";
    root["message"] = msg;
    root["code"] = code;
    sendJsonDoc(request, doc, statusCode);
}

// Build compact RF + app + net status (mirrors RayCommServer::buildStatusJson,
// with a bit of extra network info).
static void buildStatusJson(JsonDocument &doc)
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

    // RF frequency from SystemStatus
    rf["freq_hz"] = gStatus.rfFrequencyHz;
    rf["freq_valid"] = gStatus.rfFrequencyValid;

    if (g_protection.isTripped())
    {
        RayTripEvent ev = g_protection.lastTrip();
        JsonObject trip = rf.createNestedObject("trip");
        trip["type"] = static_cast<uint8_t>(ev.type);
        trip["source"] = static_cast<uint8_t>(ev.source);
        trip["threshold"] = ev.threshold;
        trip["value"] = ev.value;
    }

    // Coupler telemetry
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
        c["fwd_v"] = s->getLastFwdVoltage();
        c["ref_v"] = s->getLastRefVoltage();
        c["fwd_w"] = s->getForwardPowerWatts();
        c["ref_w"] = s->getReflectedPowerWatts();
        c["swr"] = s->getSwr();
        c["peak_f"] = s->getPeakForwardPowerWatts();
        c["peak_r"] = s->getPeakReflectedPowerWatts();
    }

    // Aux inputs (live values mirrored from SystemStatus)
    JsonArray auxArr = rf.createNestedArray("aux");
    for (int ch = 0; ch < NUM_AUX_INPUTS; ++ch)
    {
        const AuxInputStatus &st = gStatus.auxInputs[ch];

        JsonObject a = auxArr.createNestedObject();
        a["index"] = ch;
        a["name"] = st.name;
        a["type_id"] = static_cast<uint8_t>(st.type);
        a["type"] = auxInputTypeNameLocal(st.type);
        a["raw_volts"] = st.rawVolts;
        a["value"] = st.value;
    }

    // Aux digital I/O
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

    // Network info (from NetManager)
    JsonObject net = root.createNestedObject("net");
    net["state"] = static_cast<int>(gNet.state());
    net["ip"] = gNet.ip().toString();

    int rssi = gNet.wifiRssi();
    if (rssi != -127)
        net["rssi_dbm"] = rssi;

    if (gPrefs.hostname[0] != '\0')
        net["hostname"] = gPrefs.hostname;
}

// Build protection config JSON (same structure as Serial get_protection_config)
static void buildProtectionConfigJson(JsonDocument &doc)
{
    JsonObject rootOut = doc.to<JsonObject>();

    JsonArray fwd = rootOut.createNestedArray("fwd_max_w");
    JsonArray ref = rootOut.createNestedArray("ref_max_w");
    JsonArray swr = rootOut.createNestedArray("swr_max");

    for (size_t i = 0; i < NUM_COUPLERS; ++i)
    {
        fwd.add(g_protCfg.fwdMaxW[i]);
        ref.add(g_protCfg.reflMaxW[i]);
        swr.add(g_protCfg.swrMax[i]);
    }

    rootOut["boot_ignore_ms"] = g_protCfg.bootIgnoreMs;
    rootOut["gate_on_tx"] = g_protCfg.gateOnTx;
    rootOut["type"] = "protection_config";
}

// Emit coupler config for either one index or all (same as Serial
// get_coupler_config, including type_id + type_name).
static void buildCouplerConfigJson(JsonDocument &doc, int reqIndex)
{
    JsonObject rootOut = doc.to<JsonObject>();

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
        c["type_name"] = couplerTypeNameLocal(static_cast<CouplerType>(cp.couplerType));

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
}

static const char *auxTempModelName_(AuxTempModel m)
{
    switch (m)
    {
    case AuxTempModel::Linear:
        return "linear";
    case AuxTempModel::ThermistorBeta:
        return "thermistor";
    default:
        return "linear";
    }
}

static AuxTempModel auxTempModelFromString_(const String &s)
{
    if (s.equalsIgnoreCase("thermistor"))
        return AuxTempModel::ThermistorBeta;
    return AuxTempModel::Linear;
}

static AuxInputType auxInputTypeFromString_(const String &s)
{
    if (s.equalsIgnoreCase("voltage"))
        return AuxInputType::Voltage;
    if (s.equalsIgnoreCase("current"))
        return AuxInputType::Current;
    if (s.equalsIgnoreCase("temperature"))
        return AuxInputType::Temperature;
    return AuxInputType::None;
}

// -----------------------------------------------------------------------------
// RayHttpServer implementation
// -----------------------------------------------------------------------------

RayHttpServer::RayHttpServer(AsyncWebServer &server)
    : _server(server)
{
}

void RayHttpServer::begin()
{
    if (!ReleaseDefaults::labNetworkEnabled)
        return;

    registerRoutes();
    _server.begin();
    Serial.println("[HTTP] RayHttpServer started on /api/*");
}

void RayHttpServer::update(uint32_t nowMs)
{
    (void)nowMs;
    // Reserved for future timed/streaming work (SSE, telemetry, etc.).
}

void RayHttpServer::addCorsHeaders(AsyncWebServerResponse *resp)
{
    if (!resp)
        return;

    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

// -----------------------------------------------------------------------------
// Register all HTTP API routes (endpoints)
// (mirrors RayCommServer API where applicable)
// -----------------------------------------------------------------------------
void RayHttpServer::registerRoutes()
{
    // Convenience lambda for generic CORS OPTIONS replies
    auto optionsHandler = [](AsyncWebServerRequest *request)
    {
        AsyncWebServerResponse *resp =
            request->beginResponse(204); // No Content
        RayHttpServer::addCorsHeaders(resp);
        request->send(resp);
    };

    // -------------------------------------------------------------------------
    // GET /ping  -> "pong"
    // -------------------------------------------------------------------------
    _server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send(200, "text/plain", "pong"); });

    // -------------------------------------------------------------------------
    // GET /api/status   -> JSON status (mirrors get_status)
    // -------------------------------------------------------------------------
    _server.on("/api/status", HTTP_GET,
               [](AsyncWebServerRequest *request)
               {
                   StaticJsonDocument<2048> out;
                   buildStatusJson(out);
                   out["type"] = "status";
                   sendJsonDoc(request, out);
               });

    _server.on("/api/status", HTTP_OPTIONS, optionsHandler);

    // -------------------------------------------------------------------------
    // POST /api/reset_protection   -> reset_protection
    // -------------------------------------------------------------------------
    _server.on("/api/reset_protection", HTTP_POST,
               [](AsyncWebServerRequest *request)
               {
                   g_protection.reset();
                   gStatus.clearTrips();

                   StaticJsonDocument<128> out;
                   out["type"] = "protection_reset";
                   out["status"] = "ok";
                   sendJsonDoc(request, out);
               });

    _server.on("/api/reset_protection", HTTP_OPTIONS, optionsHandler);

    // -------------------------------------------------------------------------
    // GET  /api/protection/config  -> get_protection_config
    // POST /api/protection/config  -> set_protection_config
    // -------------------------------------------------------------------------
    _server.on("/api/protection/config", HTTP_GET,
               [](AsyncWebServerRequest *request)
               {
                   StaticJsonDocument<512> out;
                   buildProtectionConfigJson(out);
                   sendJsonDoc(request, out);
               });

    _server.on(
        "/api/protection/config", HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            if (!request->hasParam("plain", true))
            {
                sendJsonError(request, "missing JSON body", "missing_body");
                return;
            }

            String body = request->getParam("plain", true)->value();

            StaticJsonDocument<512> doc;
            DeserializationError err = deserializeJson(doc, body);
            if (err)
            {
                sendJsonError(request, "invalid json", err.c_str());
                return;
            }

            JsonObject obj = doc.as<JsonObject>();
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
            sendJsonDoc(request, out);
        });

    _server.on("/api/protection/config", HTTP_OPTIONS, optionsHandler);

    // -------------------------------------------------------------------------
    // AsyncJson assembles bounded bodies in request-owned storage, including
    // fragmented requests that overlap on different connections.
    auto registerCouplerJsonPost = [&](const char *uri, int (*handle)(JsonObjectConst, JsonDocument &))
    {
        auto *handler = new AsyncCallbackJsonWebHandler(uri, [handle](AsyncWebServerRequest *request, JsonVariant &body)
        {
            JsonDocument response;
            const auto object = body.as<JsonObjectConst>();
            const int status = object.isNull()
                ? CouplerCalibrationJson::error(response, "expected a JSON object", "bad_json")
                : handle(object, response);
            sendJsonDoc(request, response, status);
        });
        handler->setMethod(HTTP_POST);
        handler->setMaxContentLength(CouplerCalibrationJson::MaxRequestBytes);
        _server.addHandler(handler);
        _server.on(uri, HTTP_OPTIONS, optionsHandler);
    };

    registerCouplerJsonPost("/api/coupler/calibrate/copy", CouplerCalibrationJson::copyLinear);
    registerCouplerJsonPost("/api/coupler/calibrate/reset", [](JsonObjectConst obj, JsonDocument &out)
    {
        const int status = CouplerCalibrationJson::reset(obj, out);
        if (status == 200)
            out["calibrated"] = false;
        return status;
    });
    registerCouplerJsonPost("/api/coupler/calibrate", CouplerCalibrationJson::apply);

    // -------------------------------------------------------------------------
    // GET  /api/coupler/config[?index=N]  -> get_coupler_config
    // POST /api/coupler/config            -> set_coupler_config
    // -------------------------------------------------------------------------
    _server.on("/api/coupler/config", HTTP_GET,
               [](AsyncWebServerRequest *request)
               {
                   int idx = -1;
                   if (request->hasParam("index"))
                   {
                       idx = request->getParam("index")->value().toInt();
                   }

                   StaticJsonDocument<1024> out;
                   buildCouplerConfigJson(out, idx);
                   sendJsonDoc(request, out);
               });

    registerCouplerJsonPost("/api/coupler/config", [](JsonObjectConst obj, JsonDocument &out)
    {
        const std::lock_guard<std::mutex> lock(gCalibrationMutex);
        const UserPreferences previous = gPrefs;

        if (obj.containsKey("num_couplers"))
            gPrefs.numCouplers = obj["num_couplers"].as<int>();

        if (obj.containsKey("active_index"))
            gPrefs.activeCoupler = obj["active_index"].as<int>();

        // Helper lambda: apply ONE coupler object
        auto applyOneCoupler = [&](JsonObjectConst c)
        {
            int idx = -1;
            if (c.containsKey("index"))
                idx = c["index"].as<int>();

            if (idx < 0 || idx >= COUPLERS_MAX)
                return;

            CouplerPreferences &cp = gPrefs.couplers[idx];

            if (c.containsKey("name"))
            {
                const char *nm = c["name"];
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
                cp.enabledTripMask = (uint8_t)c["trip_mask"].as<int>();

            if (c.containsKey("type_id"))
            {
                const CouplerType oldType = static_cast<CouplerType>(cp.couplerType);
                uint8_t t = c["type_id"].as<uint8_t>();
                cp.couplerType = couplerTypeFromId(t);
                ensureFactoryTableProfileIfAllowed((uint8_t)idx, oldType, static_cast<CouplerType>(cp.couplerType));
            }
            if (c.containsKey("full_scale_w"))
                cp.couplerFullScaleW = c["full_scale_w"].as<float>();
        };

        // --- Robust handling: batch array OR flat single-object payload ---
        if (obj.containsKey("couplers"))
        {
            JsonVariantConst vCouplers = obj["couplers"];
            if (vCouplers.is<JsonArrayConst>())
            {
                JsonArrayConst arr = vCouplers.as<JsonArrayConst>();
                for (JsonVariantConst v : arr)
                {
                    if (!v.is<JsonObjectConst>())
                        continue;

                    JsonObjectConst c = v.as<JsonObjectConst>();
                    applyOneCoupler(c);
                }
            }
        }
        else if (obj.containsKey("index"))
        {
            // Qt currently sends a flat object; apply it directly
            applyOneCoupler(obj);
        }

        if (!saveUserPreferences(gPrefs))
        {
            gPrefs = previous;
            return CouplerCalibrationJson::error(out, "could not save coupler configuration", "save_failed", 500);
        }
        applyCouplerPrefsToRuntime();

        out["type"] = "coupler_config";
        out["status"] = "ok";
        out["num_couplers"] = gPrefs.numCouplers;
        out["active_index"] = gPrefs.activeCoupler;
        return 200;
    });

    // -------------------------------------------------------------------------


    // -------------------------------------------------------------------------
    // POST /api/coupler/active   -> set_active_coupler
    // Body: { "index": N }
    // -------------------------------------------------------------------------
    _server.on(
        "/api/coupler/active", HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            if (!request->hasParam("plain", true))
            {
                sendJsonError(request, "missing JSON body", "missing_body");
                return;
            }

            String body = request->getParam("plain", true)->value();
            StaticJsonDocument<256> doc;
            DeserializationError err = deserializeJson(doc, body);
            if (err)
            {
                sendJsonError(request, "invalid json", err.c_str());
                return;
            }

            JsonObject obj = doc.as<JsonObject>();

            int index = obj["index"] | 0;
            if (index < 0 || index >= static_cast<int>(gPrefs.numCouplers))
            {
                sendJsonError(request, "invalid active coupler index",
                              "bad_index");
                return;
            }

            gPrefs.activeCoupler = static_cast<uint8_t>(index);
            applyCouplerPrefsToRuntime();
            saveUserPreferences(gPrefs);

            StaticJsonDocument<128> out;
            out["type"] = "active_coupler";
            out["status"] = "ok";
            out["index"] = index;
            sendJsonDoc(request, out);
        });

    _server.on("/api/coupler/active", HTTP_OPTIONS, optionsHandler);

    // -------------------------------------------------------------------------
    // GET  /api/aux/config
    // POST /api/aux/config
    // POST /api/aux/calibrate/2point
    // -------------------------------------------------------------------------

    _server.on("/api/aux/config", HTTP_GET,
               [](AsyncWebServerRequest *request)
               {
                   StaticJsonDocument<1024> out;
                   JsonObject root = out.to<JsonObject>();
                   root["type"] = "aux_config";
                   root["status"] = "ok";

                   JsonArray arr = root.createNestedArray("aux");

                   for (int i = 0; i < NUM_AUX_INPUTS; ++i)
                   {
                       const auto &a = gPrefs.aux[i];
                       JsonObject o = arr.createNestedObject();

                       o["index"] = i;
                       o["name"] = a.name;
                       o["type"] = auxInputTypeNameLocal(a.type);
                       o["tempModel"] = auxTempModelName_(a.tempModel);

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

                   sendJsonDoc(request, out);
               });

    _server.on(
        "/api/aux/config",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            // response in body handler
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
        {
            static String s_body;

            if (index == 0)
            {
                s_body = "";
                s_body.reserve(total);
            }

            s_body.concat((const char *)data, len);

            if (index + len != total)
                return; // wait for full body

            StaticJsonDocument<512> doc;
            DeserializationError err = deserializeJson(doc, s_body);
            if (err)
            {
                sendJsonError(request, "invalid json", err.c_str());
                return;
            }

            JsonObject obj = doc.as<JsonObject>();

            const int idx = obj["index"] | -1;
            if (idx < 0 || idx >= NUM_AUX_INPUTS)
            {
                sendJsonError(request, "Invalid aux index", "bad_index", 400);
                return;
            }

            auto &a = gPrefs.aux[idx];

            // Optional name override
            if (obj.containsKey("name"))
            {
                const char *nm = obj["name"];
                if (nm)
                {
                    strncpy(a.name, nm, sizeof(a.name) - 1);
                    a.name[sizeof(a.name) - 1] = '\0';
                }
                else
                {
                    a.name[0] = '\0';
                }
            }

            if (obj.containsKey("type"))
                a.type = auxInputTypeFromString_(obj["type"].as<String>());

            if (obj.containsKey("tempModel"))
                a.tempModel = auxTempModelFromString_(obj["tempModel"].as<String>());

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

            // --- enforce consistency ---
            // Type is canonical:
            //  - Non-temperature inputs must use Linear temp model (ignored anyway)
            //  - Temperature inputs may use either Linear or ThermistorBeta
            if (a.type != AuxInputType::Temperature)
            {
                a.tempModel = AuxTempModel::Linear;
            }

            // debug print aux config values
            Serial.printf("[HTTP] Aux %d config updated: name=\"%s\" type=%s tempModel=%s Vmax=%.2f Imax=%.2f Tmax=%.1f\n",
                          idx,
                          a.name,
                          auxInputTypeNameLocal(a.type),
                          auxTempModelName_(a.tempModel),
                          a.voltageMaxRange,
                          a.currentMaxRange,
                          a.temperatureMaxRange);

            saveUserPreferences(gPrefs);
            applyAuxPrefsToRuntime();

            StaticJsonDocument<256> out;
            out["type"] = "aux_config";
            out["status"] = "ok";
            out["index"] = idx;
            sendJsonDoc(request, out);
        });

    _server.on(
        "/api/aux/calibrate/2point",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            // response in body handler
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
        {
            static String s_body;

            if (index == 0)
            {
                s_body = "";
                s_body.reserve(total);
            }

            s_body.concat((const char *)data, len);

            if (index + len != total)
                return;

            StaticJsonDocument<512> doc;
            DeserializationError err = deserializeJson(doc, s_body);
            if (err)
            {
                sendJsonError(request, "invalid json", err.c_str());
                return;
            }

            JsonObject obj = doc.as<JsonObject>();

            const int idx = obj["index"] | -1;
            if (idx < 0 || idx >= NUM_AUX_INPUTS)
            {
                sendJsonError(request, "Invalid aux index", "bad_index", 400);
                return;
            }

            // Accept BOTH schemas:
            //  A) New: low/high objects (Coupler-style)
            //     { index, low:{v,eng}, high:{v,eng} }
            //  B) Legacy: { index, t1C,t2C,v1,v2 }
            bool legacy = false;

            float eng1 = NAN;
            float eng2 = NAN;
            float v1 = NAN;
            float v2 = NAN;

            if (obj.containsKey("low") && obj.containsKey("high") &&
                obj["low"].is<JsonObject>() && obj["high"].is<JsonObject>())
            {
                JsonObject lo = obj["low"].as<JsonObject>();
                JsonObject hi = obj["high"].as<JsonObject>();

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
                sendJsonError(request, "Missing/invalid calibration fields", "bad_fields", 400);
                return;
            }

            if (v1 <= 0.01f || v2 <= 0.01f || v1 >= ADC_VREF || v2 >= ADC_VREF)
            {
                sendJsonError(request, "Invalid calibration voltages", "bad_voltage", 400);
                return;
            }

            auto &a = gPrefs.aux[idx];

            // Decide which calibration path to apply.
            // - Legacy payload implies Temperature thermistor calibration.
            // - Otherwise: if channel is Temperature => thermistor calibration.
            // - Else: Voltage/Current linear calibration (cal_m/cal_b).
            const bool doThermistor = legacy || (a.type == AuxInputType::Temperature);

            if (doThermistor)
            {
                // -----------------------------
                // Temperature thermistor 2-point
                // eng1/eng2 are °C
                // -----------------------------
                const float t1C = eng1;
                const float t2C = eng2;

                if (!isfinite(t1C) || !isfinite(t2C) || fabsf(t2C - t1C) < 0.01f)
                {
                    sendJsonError(request, "Invalid temperature points", "bad_temp", 400);
                    return;
                }

                const float Rs = a.thermSeriesOhms;
                const float T1K = t1C + 273.15f;
                const float T2K = t2C + 273.15f;
                const float T0K = a.thermT0C + 273.15f;

                // IMPORTANT: Must match AuxInputs.cpp topology:
                // THERMISTOR_PULLUP_TO_VREF = true  (3.3V -> Rs -> ADC -> NTC -> GND)
                // V = Vref * (Rntc / (Rs + Rntc))
                // => Rntc = Rs * V / (Vref - V)
                if (v1 >= (ADC_VREF - 0.001f) || v2 >= (ADC_VREF - 0.001f))
                {
                    sendJsonError(request, "Invalid calibration voltages", "bad_voltage", 400);
                    return;
                }

                const float R1 = Rs * (v1 / (ADC_VREF - v1));
                const float R2 = Rs * (v2 / (ADC_VREF - v2));

                if (!isfinite(R1) || !isfinite(R2) || R1 <= 0.0f || R2 <= 0.0f)
                {
                    sendJsonError(request, "Invalid thermistor resistance", "bad_r", 400);
                    return;
                }

                const float beta = logf(R1 / R2) / ((1.0f / T1K) - (1.0f / T2K));
                const float R0 = R1 * expf(-beta * ((1.0f / T1K) - (1.0f / T0K)));

                if (!isfinite(beta) || !isfinite(R0) || beta <= 0.0f || R0 <= 0.0f)
                {
                    sendJsonError(request, "Invalid beta/R0 result", "bad_result", 400);
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
                sendJsonDoc(request, out);
            }
            else
            {
                // -----------------------------
                // Voltage/Current linear 2-point
                // eng1/eng2 are engineering units (volts or amps)
                // We solve cal_m/cal_b so that:
                //   eng_out = raw_eng * cal_m + cal_b
                // where raw_eng = (v/ADC_VREF) * maxRange
                // -----------------------------
                if (a.type != AuxInputType::Voltage && a.type != AuxInputType::Current)
                {
                    sendJsonError(request, "Aux type does not support linear calibration", "bad_type", 400);
                    return;
                }

                const float maxRange = (a.type == AuxInputType::Voltage) ? a.voltageMaxRange : a.currentMaxRange;

                const float x1 = (v1 / ADC_VREF) * maxRange;
                const float x2 = (v2 / ADC_VREF) * maxRange;

                if (!isfinite(x1) || !isfinite(x2) || fabsf(x2 - x1) < 0.0001f)
                {
                    sendJsonError(request, "Invalid calibration points", "bad_points", 400);
                    return;
                }

                const float m = (eng2 - eng1) / (x2 - x1);
                const float b = eng1 - (x1 * m);

                if (!isfinite(m) || !isfinite(b))
                {
                    sendJsonError(request, "Invalid cal_m/cal_b result", "bad_result", 400);
                    return;
                }

                a.cal_m = m;
                a.cal_b = b;
                a.tempModel = AuxTempModel::Linear; // runtime will enforce this anyway for non-temp

                saveUserPreferences(gPrefs);
                applyAuxPrefsToRuntime();

                StaticJsonDocument<256> out;
                out["type"] = "aux_calibrate_2point";
                out["status"] = "ok";
                out["index"] = idx;
                out["cal_m"] = m;
                out["cal_b"] = b;
                sendJsonDoc(request, out);
            }
        });

    // -------------------------------------------------------------------------
    // GET  /api/aux_digital/config
    // POST /api/aux_digital/config
    // -------------------------------------------------------------------------
    _server.on("/api/aux_digital/config", HTTP_GET,
               [](AsyncWebServerRequest *request)
               {
                   StaticJsonDocument<1024> out;
                   JsonObject root = out.to<JsonObject>();
                   root["type"] = "aux_digital_config";
                   root["status"] = "ok";

                   JsonArray arr = root.createNestedArray("aux_digital");
                   for (int i = 0; i < (int)NUM_AUX_DIGITAL; ++i)
                   {
                       const auto &d = gPrefs.auxDigital[i];
                       JsonObject o = arr.createNestedObject();

                       o["index"] = i;
                       o["name"] = d.name;
                       o["mode_id"] = static_cast<uint8_t>(d.mode);
                       o["mode"] = auxDigitalModeName_(d.mode);
                       o["pull_id"] = static_cast<uint8_t>(d.pull);
                       o["pull"] = auxDigitalPullName_(d.pull);
                       o["defaultState"] = d.defaultState;
                   }

                   sendJsonDoc(request, out);
               });

    _server.on(
        "/api/aux_digital/config",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
        {
            static String s_body;
            if (index == 0)
                s_body = "";
            s_body.reserve(total);
            s_body.concat((const char *)data, len);
            if (index + len < total)
                return; // wait for full body

            StaticJsonDocument<512> doc;
            DeserializationError err = deserializeJson(doc, s_body);
            if (err)
            {
                StaticJsonDocument<256> out;
                out["type"] = "error";
                out["message"] = "invalid JSON";
                out["code"] = "bad_json";
                sendJsonDoc(request, out, 400);
                return;
            }

            JsonObject obj = doc.as<JsonObject>();
            int idx = obj["index"] | -1;
            if (idx < 0 || idx >= (int)NUM_AUX_DIGITAL)
            {
                StaticJsonDocument<256> out;
                out["type"] = "error";
                out["message"] = "invalid aux_digital index";
                out["code"] = "bad_index";
                sendJsonDoc(request, out, 400);
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
                d.mode = auxDigitalModeFromJson_(obj["mode"]);

            if (obj.containsKey("mode_id"))
                d.mode = auxDigitalModeFromJson_(obj["mode_id"]);

            if (obj.containsKey("pull"))
                d.pull = auxDigitalPullFromJson_(obj["pull"]);

            if (obj.containsKey("pull_id"))
                d.pull = auxDigitalPullFromJson_(obj["pull_id"]);

            if (obj.containsKey("defaultState"))
                d.defaultState = obj["defaultState"] | false;

            // Persist and apply to runtime
            saveUserPreferences(gPrefs);
            applyAuxDigitalPrefsToRuntime();

            StaticJsonDocument<256> out;
            out["type"] = "aux_digital_config";
            out["status"] = "ok";
            out["index"] = idx;
            sendJsonDoc(request, out);
        });

    _server.on("/api/aux_digital/config", HTTP_OPTIONS, optionsHandler);

    // -------------------------------------------------------------------------
    // GET  /api/aux_digital/state
    // POST /api/aux_digital/state
    // -------------------------------------------------------------------------
    _server.on("/api/aux_digital/state", HTTP_GET,
               [](AsyncWebServerRequest *request)
               {
                   StaticJsonDocument<512> out;
                   JsonObject root = out.to<JsonObject>();
                   root["type"] = "aux_digital_state";
                   root["status"] = "ok";

                   JsonArray arr = root.createNestedArray("aux_digital");
                   for (int i = 0; i < (int)NUM_AUX_DIGITAL; ++i)
                   {
                       JsonObject o = arr.createNestedObject();
                       o["index"] = i;
                       o["value"] = gStatus.auxDigital[i].value;
                   }

                   sendJsonDoc(request, out);
               });

    _server.on(
        "/api/aux_digital/state",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
        {
            static String s_body;
            if (index == 0)
                s_body = "";
            s_body.reserve(total);
            s_body.concat((const char *)data, len);
            if (index + len < total)
                return;

            StaticJsonDocument<256> doc;
            if (deserializeJson(doc, s_body))
            {
                StaticJsonDocument<256> out;
                out["type"] = "error";
                out["message"] = "invalid JSON";
                out["code"] = "bad_json";
                sendJsonDoc(request, out, 400);
                return;
            }

            JsonObject obj = doc.as<JsonObject>();
            int idx = obj["index"] | -1;
            if (idx < 0 || idx >= (int)NUM_AUX_DIGITAL)
            {
                StaticJsonDocument<256> out;
                out["type"] = "error";
                out["message"] = "invalid aux_digital index";
                out["code"] = "bad_index";
                sendJsonDoc(request, out, 400);
                return;
            }

            bool value = obj["value"] | false;
            gAuxDigital.write(idx, value);

            StaticJsonDocument<256> out;
            out["type"] = "aux_digital_state";
            out["status"] = "ok";
            out["index"] = idx;
            out["value"] = value;
            sendJsonDoc(request, out);
        });

    _server.on("/api/aux_digital/state", HTTP_OPTIONS, optionsHandler);

    // -------------------------------------------------------------------------
    // POST /api/aux/calibrate/reset
    // Body: { "index": 0 }
    // Resets aux calibration back to factory defaults (per index).
    // -------------------------------------------------------------------------
    _server.on(
        "/api/aux/calibrate/reset",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            // response is sent in the body handler
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
        {
            static String s_body;

            if (index == 0)
            {
                s_body = "";
                s_body.reserve(total);
            }

            s_body.concat((const char *)data, len);

            if (index + len != total)
                return;

            StaticJsonDocument<256> doc;
            DeserializationError err = deserializeJson(doc, s_body);
            if (err)
            {
                sendJsonError(request, "invalid json", err.c_str());
                return;
            }

            JsonObject obj = doc.as<JsonObject>();
            int ch = obj["index"] | -1;

            if (ch < 0 || ch >= (int)NUM_AUX_INPUTS)
            {
                sendJsonError(request, "invalid aux index", "bad_index");
                return;
            }

            // Keep a static factory-default prefs snapshot so we don't allocate a large struct on stack.
            static bool s_defaultsInit = false;
            static UserPreferences s_defaults;
            if (!s_defaultsInit)
            {
                setDefaultUserPreferences(s_defaults);
                s_defaultsInit = true;
            }

            AuxInputPreferences &a = gPrefs.aux[ch];
            const AuxInputPreferences &d = s_defaults.aux[ch];

            // Reset linear trim
            a.cal_m = d.cal_m;
            a.cal_b = d.cal_b;

            // Reset thermistor calibration parameters (only meaningful for Temperature)
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
            sendJsonDoc(request, out);
        });

    _server.on("/api/aux/config", HTTP_OPTIONS, optionsHandler);
    _server.on("/api/aux/calibrate/2point", HTTP_OPTIONS, optionsHandler);
    _server.on("/api/aux/calibrate/reset", HTTP_OPTIONS, optionsHandler);

    // -------------------------------------------------------------------------
    // GET  /api/device/config  -> get device settings (brightness + time info)
    // POST /api/device/config  -> set device settings (brightness + unix epoch)
    // -------------------------------------------------------------------------
    _server.on("/api/device/config", HTTP_GET,
               [](AsyncWebServerRequest *request)
               {
                   StaticJsonDocument<256> out;
                   out["type"] = "device_config";
                   out["status"] = "ok";
                   out["brightness"] = gPrefs.displayBrightness; // 0-100 (or whatever you use)

                   // Optional: return current epoch seconds (UTC)
                   time_t now = time(nullptr);
                   out["unix_time"] = (uint32_t)now;

                   sendJsonDoc(request, out);
               });

    _server.on(
        "/api/device/config",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            // We respond in the body handler below
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
        {
            static String s_body;

            if (index == 0)
            {
                s_body = "";
                s_body.reserve(total);
            }

            s_body.concat((const char *)data, len);

            if (index + len != total)
            {
                return; // wait for all chunks
            }

            Serial.printf("[HTTP] /api/device/config POST: total=%u\n", (unsigned)total);
            Serial.printf("[HTTP] body=%s\n", s_body.c_str());

            StaticJsonDocument<256> doc;
            DeserializationError err = deserializeJson(doc, s_body);
            if (err)
            {
                sendJsonError(request, "invalid json", err.c_str());
                return;
            }

            JsonObject obj = doc.as<JsonObject>();

            // ---- Apply brightness ----
            if (obj.containsKey("brightness"))
            {
                int br = obj["brightness"].as<int>();

                // Clamp: 10..100 (you requested min 10%)
                if (br < 10)
                    br = 10;
                if (br > 100)
                    br = 100;

                gPrefs.displayBrightness = static_cast<uint8_t>(br);

                // Apply to Nextion runtime brightness
                gDisplay.setBrightness(gPrefs.displayBrightness);

                char cmd[32];
                snprintf(cmd, sizeof(cmd), "hBrightness.val=%u", (unsigned)gPrefs.displayBrightness);
                gDisplay.sendCommand(cmd);

                Serial.printf("[HTTP] brightness applied: %u\n", (unsigned)gPrefs.displayBrightness);
            }

            // ---- Apply UTC epoch time (display is the source of truth) ----
            time_t epoch = 0;

            // Accept either utc_epoch or unix_time for convenience
            if (obj.containsKey("utc_epoch"))
            {
                epoch = (time_t)obj["utc_epoch"].as<long long>();
            }
            else if (obj.containsKey("unix_time"))
            {
                epoch = (time_t)obj["unix_time"].as<long long>();
            }

            if (epoch > 0)
            {
                // 1) Update Nextion's RTC (authoritative time)
                gDisplay.setRtcEpoch(epoch);

                // 2) Optionally mirror to ESP32 system clock so logs/etc use it immediately
                struct timeval tv{};
                tv.tv_sec = epoch;
                tv.tv_usec = 0;
                settimeofday(&tv, nullptr);

                Serial.printf("[HTTP] utc_epoch applied (display + system): %lld\n",
                              (long long)epoch);
            }

            saveUserPreferences(gPrefs);
            Serial.println("[HTTP] prefs saved");

            StaticJsonDocument<128> out;
            JsonObject rootOut = out.to<JsonObject>();
            rootOut["type"] = "device_config";
            rootOut["status"] = "ok";
            rootOut["brightness"] = gPrefs.displayBrightness;
            if (epoch > 0)
            {
                rootOut["unix_time"] = (uint32_t)epoch;
            }
            sendJsonDoc(request, out);
        });

    _server.on("/api/device/config", HTTP_OPTIONS, optionsHandler);

    // -------------------------------------------------------------------------
    // POST /api/save_prefs   -> save_prefs
    // -------------------------------------------------------------------------
    _server.on("/api/save_prefs", HTTP_POST,
               [](AsyncWebServerRequest *request)
               {
                   saveUserPreferences(gPrefs);

                   StaticJsonDocument<128> out;
                   out["type"] = "save_prefs";
                   out["status"] = "ok";
                   sendJsonDoc(request, out);
               });

    _server.on("/api/save_prefs", HTTP_OPTIONS, optionsHandler);

    // -------------------------------------------------------------------------
    // POST /api/reset_defaults   -> reset_defaults (placeholder)
    // -------------------------------------------------------------------------
    _server.on("/api/reset_defaults", HTTP_POST,
               [](AsyncWebServerRequest *request)
               {
                   setDefaultUserPreferences(gPrefs);
                   saveUserPreferences(gPrefs);

                   // Apply runtime pieces that are safe to update live
                   applyCouplerPrefsToRuntime();
                   applyAuxPrefsToRuntime();

                   // Apply brightness immediately (same as /api/device/config)
                   gDisplay.setBrightness(gPrefs.displayBrightness);

                   char cmd[32];
                   snprintf(cmd, sizeof(cmd), "hBrightness.val=%u", (unsigned)gPrefs.displayBrightness);
                   gDisplay.sendCommand(cmd);

                   StaticJsonDocument<256> out;
                   out["type"] = "reset_defaults";
                   out["status"] = "ok";
                   out["reboot_required"] = true; // Wi-Fi creds etc. may require restart to fully take effect
                   sendJsonDoc(request, out);
               });

    _server.on("/api/reset_defaults", HTTP_OPTIONS, optionsHandler);

    // Modulation capture (CORS preflight)
    _server.on("/api/modulation/start", HTTP_OPTIONS, optionsHandler);
    _server.on("/api/modulation/status", HTTP_OPTIONS, optionsHandler);
    _server.on("/api/modulation/data", HTTP_OPTIONS, optionsHandler);

    // -------------------------------------------------------------------------
    // POST /api/modulation/start
    // Body JSON:
    // {
    //   "coupler": 0,
    //   "path": "fwd" | "ref",
    //   "samples": 1024,
    //   "sample_rate_hz": 10000.0
    // }
    // -------------------------------------------------------------------------
    _server.on(
        "/api/modulation/start",
        HTTP_POST,
        [this](AsyncWebServerRequest *request)
        {
            // We'll respond from body handler
        },
        nullptr,
        [this](AsyncWebServerRequest *request,
               uint8_t *data, size_t len, size_t index, size_t total)
        {
            if (index == 0)
            {
                // first chunk
                // (NOTE: modulation/start currently assumes single-chunk JSON,
                // consistent with existing behavior)
            }

            // Parse JSON body
            DynamicJsonDocument doc(512);
            DeserializationError err = deserializeJson(doc, data, len);
            if (err)
            {
                AsyncWebServerResponse *resp =
                    request->beginResponse(
                        400,
                        "application/json",
                        "{\"type\":\"error\",\"message\":\"invalid json\",\"code\":\"invalid_json\"}");
                addCorsHeaders(resp);
                request->send(resp);
                return;
            }

            JsonVariantConst root = doc.as<JsonVariantConst>();

            // -------------------------------
            // Extract parameters
            // -------------------------------
            const uint8_t couplerIndex = root["coupler"] | 0;

            const char *path = root["path"] | "fwd";
            const bool forward = !(path && strcmp(path, "ref") == 0);

            // -------------------------------
            // Build ModScope capture config
            // -------------------------------
            ModScopeConfig cfg;
            cfg.couplerIndex = couplerIndex;

            // NOTE: ModScopeConfig currently has no forward/ref field.
            // Keep parsing path for API compatibility, but it is not used yet.
            (void)forward;

            uint16_t samples = root["samples"] | 1024;
            if (samples < MODSCOPE_MIN_SAMPLES)
                samples = MODSCOPE_MIN_SAMPLES;
            if (samples > MODSCOPE_MAX_SAMPLES)
                samples = MODSCOPE_MAX_SAMPLES;
            cfg.sampleCount = samples;

            // ModScopeConfig default sampleRateHz is already set in the struct (20000).
            cfg.sampleRateHz = root["sample_rate_hz"] | cfg.sampleRateHz;

            // -------------------------------
            // REAL ADC CAPTURE (ModScopeController)
            // -------------------------------
            const bool ok = gModScope.startCapture(cfg);
            uint32_t acceptedRate = cfg.sampleRateHz;
            if (ok)
            {
                acceptedRate = gModScope.getMeta().sampleRateHz; // will be target while capturing
            }

            // -------------------------------
            // Response
            // -------------------------------
            DynamicJsonDocument out(256);
            JsonObject respRoot = out.to<JsonObject>();
            respRoot["type"] = "modulation_start";
            respRoot["status"] = ok ? "ok" : "busy";
            respRoot["coupler"] = couplerIndex;
            respRoot["path"] = forward ? "fwd" : "ref"; // reported for UI; not used by capture yet
            respRoot["samples"] = cfg.sampleCount;
            respRoot["sample_rate_hz"] = acceptedRate;

            String body;
            serializeJson(out, body);

            AsyncWebServerResponse *resp =
                request->beginResponse(200, "application/json", body);
            addCorsHeaders(resp);
            request->send(resp);

            // Add debug info to console
            Serial.printf("[HTTP] modulation/start: coupler=%u samples=%u rate_req=%lu rate_acc=%lu\n",
                          (unsigned)cfg.couplerIndex,
                          (unsigned)cfg.sampleCount,
                          (unsigned long)(root["sample_rate_hz"] | 0),
                          (unsigned long)acceptedRate);
        });

    // -------------------------------------------------------------------------
    // GET /api/modulation/status
    // -------------------------------------------------------------------------
    _server.on("/api/modulation/status", HTTP_GET,
               [](AsyncWebServerRequest *request)
               {
                   const ModScopeMeta meta = gModScope.getMeta();

                   DynamicJsonDocument out(256);
                   JsonObject root = out.to<JsonObject>();
                   root["type"] = "modulation_status";

                   // Match your Qt-side expectations
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
                   root["status"] = statusStr;

                   root["coupler"] = meta.coupler;
                   root["path"] = "fwd"; // until you track path in meta
                   root["samples"] = meta.sampleCount;
                   root["sample_rate_hz"] = meta.sampleRateHz;

                   String body;
                   serializeJson(out, body);

                   AsyncWebServerResponse *resp =
                       request->beginResponse(200, "application/json", body);
                   RayHttpServer::addCorsHeaders(resp);
                   request->send(resp);

                   // Add debug info to console
                   Serial.printf("[HTTP] modulation/status: state=%s coupler=%u samples=%u rate=%lu\n",
                                 statusStr,
                                 (unsigned)meta.coupler,
                                 (unsigned)meta.sampleCount,
                                 (unsigned long)meta.sampleRateHz);
               });

    // -------------------------------------------------------------------------
    // GET /api/modulation/data?max_samples=512
    // -------------------------------------------------------------------------
    _server.on("/api/modulation/data", HTTP_GET,
               [](AsyncWebServerRequest *request)
               {
                   // -------------------------------
                   // REAL ADC CAPTURE (ModScopeController)
                   // -------------------------------

                   const ModScopeMeta meta = gModScope.getMeta();

                   if (meta.state != ModScopeState::Ready)
                   {
                       AsyncWebServerResponse *resp =
                           request->beginResponse(
                               400, "application/json",
                               "{\"type\":\"error\",\"message\":\"modulation capture not ready\",\"code\":\"mod_not_ready\"}");
                       RayHttpServer::addCorsHeaders(resp);
                       request->send(resp);
                       return;
                   }

                   uint16_t total = meta.sampleCount;
                   if (total == 0)
                   {
                       AsyncWebServerResponse *resp =
                           request->beginResponse(
                               400, "application/json",
                               "{\"type\":\"error\",\"message\":\"no modulation samples\",\"code\":\"mod_empty\"}");
                       RayHttpServer::addCorsHeaders(resp);
                       request->send(resp);
                       return;
                   }

                   uint16_t maxSamples = total;
                   if (request->hasParam("max_samples"))
                   {
                       const bool post = false;
                       const bool file = false;
                       const AsyncWebParameter *p = request->getParam("max_samples", post, file);
                       if (p)
                       {
                           maxSamples = static_cast<uint16_t>(p->value().toInt());
                           if (maxSamples > total)
                               maxSamples = total;
                       }
                   }

                   size_t capacity = JSON_ARRAY_SIZE(maxSamples) + 256;
                   DynamicJsonDocument out(capacity);

                   JsonObject root = out.to<JsonObject>();
                   root["type"] = "modulation_data";
                   root["count"] = maxSamples;
                   root["sample_rate_hz"] = meta.sampleRateHz;
                   root["coupler"] = meta.coupler;
                   root["path"] = "fwd"; // path not yet tracked in ModScopeMeta

                   JsonArray arr = root.createNestedArray("samples");

                   // Copy samples out of ModScopeController
                   static uint16_t tmpBuf[MODSCOPE_MAX_SAMPLES];
                   const uint16_t copied = gModScope.copySamples(tmpBuf, maxSamples);

                   for (uint16_t i = 0; i < copied; ++i)
                   {
                       arr.add(tmpBuf[i]); // raw ADC samples (0..4095)
                   }

                   String body;
                   serializeJson(out, body);

                   AsyncWebServerResponse *resp =
                       request->beginResponse(200, "application/json", body);
                   RayHttpServer::addCorsHeaders(resp);
                   request->send(resp);

                   // Add debug info to console
                   Serial.printf("[HTTP] modulation/data: samples=%u maxSamples=%u\n",
                                 (unsigned)copied, (unsigned)maxSamples);
               });

    _server.on("/api/ota/status", HTTP_GET,
               [](AsyncWebServerRequest *request)
               {
                   JsonDocument doc;
                   JsonObject root = doc.to<JsonObject>();
                   root["ok"] = true;
                   JsonObject ota = root.createNestedObject("ota");
                   gOta.toJson(ota);

                   String body;
                   serializeJson(doc, body);
                   request->send(200, "application/json", body);
               });

    if (!ReleaseDefaults::legacyOtaEnabled)
    {
        _server.on("/api/ota/start", HTTP_POST, [](AsyncWebServerRequest *request)
        {
            sendJsonError(request, ReleaseDefaults::otaDisabledMessage, "ota_disabled", 403);
        });
        return;
    }

    _server.on("/api/ota/start", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
               {
        // Buffer the body (small JSON) in one shot
        static String body;
        if (index == 0) body = "";
        body.reserve(total);
        body += String((const char*)data).substring(0, len);

        if (index + len != total)
            return;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err)
        {
            request->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"invalid json\"}");
            return;
        }

        const char *url = doc["url"] | "";
        if (!url || !*url)
        {
            request->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"missing url\"}");
            return;
        }

        if (WiFi.status() != WL_CONNECTED)
        {
            request->send(409, "application/json",
                          "{\"ok\":false,\"error\":\"wifi not connected\"}");
            return;
        }

        if (!gOta.startFromUrl(String(url)))
        {
            request->send(409, "application/json",
                          "{\"ok\":false,\"error\":\"ota busy or invalid url\"}");
            return;
        }

        request->send(200, "application/json",
                      "{\"ok\":true,\"message\":\"ota started\"}"); });
}
