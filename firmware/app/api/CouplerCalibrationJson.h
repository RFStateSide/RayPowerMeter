// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <ArduinoJson.h>
#include <strings.h>

#include "core/CouplerCalibration.h"

namespace CouplerCalibrationJson
{
constexpr size_t MaxRequestBytes = 2048;

inline int error(JsonDocument &out, const char *message, const char *code, int status = 400)
{
    out["type"] = "error";
    out["message"] = message;
    out["code"] = code;
    return status;
}

inline int resultError(JsonDocument &out, CalibrationResult result)
{
    if (result == CalibrationResult::SaveFailed)
        return error(out, "could not save calibration", "save_failed", 500);
    return error(out, "invalid calibration points or fit", "bad_points");
}

inline bool pathIsForward(const char *path, bool &forward)
{
    if (!path)
        return false;
    if (!strcasecmp(path, "fwd") || !strcasecmp(path, "forward"))
    {
        forward = true;
        return true;
    }
    if (!strcasecmp(path, "ref") || !strcasecmp(path, "reflected") || !strcasecmp(path, "reverse"))
    {
        forward = false;
        return true;
    }
    return false;
}

inline bool readPoint(JsonVariantConst value, float &volts, float &watts)
{
    const auto point = value.as<JsonObjectConst>();
    if (!point["v"].is<float>() || !point["w"].is<float>())
        return false;
    volts = point["v"].as<float>();
    watts = point["w"].as<float>();
    return true;
}

inline void writePath(JsonObject out, const CouplerPreferences &cp, bool forward)
{
    const uint8_t count = forward ? cp.fwdCalCount : cp.refCalCount;
    out["m"] = forward ? cp.fwdCal_m : cp.refCal_m;
    out["b"] = forward ? cp.fwdCal_b : cp.refCal_b;
    out["mode"] = count >= 2 ? "table" : "linear";
    out["count"] = count;

    if (count >= 2)
    {
        JsonArray points = out["points"].to<JsonArray>();
        for (uint8_t i = 0; i < count && i < CAL_MAX_POINTS; ++i)
        {
            JsonObject point = points.add<JsonObject>();
            point["v"] = forward ? cp.fwdCalPtsV[i] : cp.refCalPtsV[i];
            point["w"] = forward ? cp.fwdCalPtsW[i] : cp.refCalPtsW[i];
        }
    }
}

inline int readTable(JsonVariantConst value, CalibrationInput &input, JsonDocument &out)
{
    const auto points = value.as<JsonArrayConst>();
    input.count = points.size();
    if (input.count < 2 || input.count > CAL_MAX_POINTS)
        return error(out, "calibration requires 2 to 20 points", "bad_point_count");

    size_t i = 0;
    for (JsonVariantConst point : points)
    {
        if (!readPoint(point, input.volts[i], input.watts[i]))
            return error(out, "each point requires numeric v and w", "bad_points");
        ++i;
    }
    return 200;
}

inline int readCalibrationInput(JsonVariantConst value, CalibrationInput &input, JsonDocument &out)
{
    const auto path = value.as<JsonObjectConst>();
    if (!path["points"].isUnbound())
        return readTable(path["points"], input, out);

    input.hasSlope = !path["m"].isUnbound();
    input.hasOffset = !path["b"].isUnbound();
    if ((!input.hasSlope && !input.hasOffset) ||
        (input.hasSlope && !path["m"].is<float>()) || (input.hasOffset && !path["b"].is<float>()))
        return error(out, "expected table points or numeric linear coefficients", "bad_fields");

    input.slope = path["m"].as<float>();
    input.offset = path["b"].as<float>();
    return 200;
}

inline int apply(JsonObjectConst obj, JsonDocument &out)
{
    const int index = obj["index"] | -1;
    if (index < 0 || index >= COUPLERS_MAX)
        return error(out, "invalid coupler index", "bad_index");

    bool forward = true;
    if (!obj["path"].isUnbound() && !pathIsForward(obj["path"].as<const char *>(), forward))
        return error(out, "invalid calibration path", "bad_path");

    const bool table = !obj["points"].isUnbound();
    size_t count = 0;
    CalibrationResult result;
    if (table)
    {
        CalibrationInput input;
        const int status = readTable(obj["points"], input, out);
        if (status != 200)
            return status;
        count = input.count;
        result = calibrateCouplerPathTable(index, forward, input.volts, input.watts, count);
    }
    else
    {
        float lowV, lowW, highV, highW;
        if (!readPoint(obj["low"], lowV, lowW) || !readPoint(obj["high"], highV, highW))
            return error(out, "low and high require numeric v and w", "missing_points");
        result = calibrateCouplerPath(index, forward, lowV, lowW, highV, highW);
    }

    if (result != CalibrationResult::Ok)
        return resultError(out, result);

    out["type"] = "calibrate_coupler";
    out["status"] = "ok";
    out["index"] = index;
    out["path"] = forward ? "fwd" : "ref";
    out["mode"] = table ? "table" : "linear";
    if (table)
        out["points"] = count;
    return 200;
}

inline int reset(JsonObjectConst obj, JsonDocument &out)
{
    const int index = obj["index"] | -1;
    if (index < 0 || index >= COUPLERS_MAX)
        return error(out, "invalid coupler index", "bad_index");

    const char *path = !obj["path"].isUnbound() ? obj["path"].as<const char *>() : "both";
    const bool both = path && (!strcasecmp(path, "both") || !strcasecmp(path, "all"));
    bool forward = true;
    if (!both && !pathIsForward(path, forward))
        return error(out, "invalid calibration path", "bad_path");

    CalSource source = CalSource::FactoryTable;
    if (!obj["cal_source"].isUnbound())
    {
        const char *mode = obj["cal_source"].as<const char *>();
        if (mode && (!strcasecmp(mode, "linear") || !strcasecmp(mode, "FactoryLinear")))
            source = CalSource::FactoryLinear;
        else if (!mode || (strcasecmp(mode, "table") && strcasecmp(mode, "FactoryTable")))
            return error(out, "invalid factory calibration source", "bad_cal_source");
    }

    const auto result = resetCouplerCalibration(index, source, both || forward, both || !forward);
    if (result != CalibrationResult::Ok)
        return resultError(out, result);

    out["type"] = "reset_calibration";
    out["status"] = "ok";
    out["index"] = index;
    out["path"] = both ? "both" : (forward ? "fwd" : "ref");
    out["cal_source"] = source == CalSource::FactoryLinear ? "FactoryLinear" : "FactoryTable";
    return 200;
}

inline int copyLinear(JsonObjectConst obj, JsonDocument &out)
{
    const int index = obj["index"] | -1;
    if (index < 0 || index >= COUPLERS_MAX)
        return error(out, "invalid coupler index", "bad_index");

    bool fromForward = true;
    if (!obj["from"].isUnbound() && !pathIsForward(obj["from"].as<const char *>(), fromForward))
        return error(out, "invalid source path", "bad_path");
    bool toForward = !fromForward;
    if ((!obj["to"].isUnbound() && !pathIsForward(obj["to"].as<const char *>(), toForward)) ||
        fromForward == toForward)
        return error(out, "from/to must be different valid paths", "bad_direction");

    float slope, offset;
    const auto result = copyCouplerLinearCalibration(index, fromForward, slope, offset);
    if (result == CalibrationResult::InvalidPoints)
        return error(out, "source linear calibration invalid", "bad_source_cal");
    if (result != CalibrationResult::Ok)
        return resultError(out, result);

    out["type"] = "ok";
    out["index"] = index;
    out["from"] = fromForward ? "forward" : "reflected";
    out["to"] = toForward ? "forward" : "reflected";
    out["m"] = slope;
    out["b"] = offset;
    return 200;
}
}
