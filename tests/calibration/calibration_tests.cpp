// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include <Arduino.h>
#include <cstdlib>
#include <iostream>
#include <string>

#include "api/CouplerCalibrationJson.h"
#include "core/eepromprefs.h"

#include "coupler/rfpowersensor.h"

// Inject exact detector voltages for knot tests independently of ADC quantization.
struct CalibrationTestAccess
{
    static void sample(RfPowerSensor &sensor, float forward, float reflected)
    {
        sensor._voltageFwd = sensor._emaFwd = forward;
        sensor._voltageRef = sensor._emaRef = reflected;
    }

    static uint8_t reflectedCount(const RfPowerSensor &sensor) { return sensor._refTableCount; }
};

UserPreferences gPrefs{};
volatile bool g_modScopeCaptureActive = false;
static RfPowerSensor sensor("test", 0, 1, 200.0f, -1, 1000);

void applyCouplerPrefsToRuntime()
{
    const auto &cp = gPrefs.couplers[0];
    sensor.setCouplerType(couplerTypeFromId(cp.couplerType));
    applyCouplerCalibrationToSensor(cp, sensor);
}

static void check(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

static bool near(float value, float expected)
{
    return std::abs(value - expected) < 0.002f;
}

static void sample(float forward, float reflected)
{
    CalibrationTestAccess::sample(sensor, forward, reflected);
}

static void checkFactory(CouplerType type, int measuredCount, int totalCount)
{
    auto &cp = gPrefs.couplers[0];
    cp.couplerType = type;
    CalSource source = CalSource::FactoryTable;
    check(resetCouplerCalibration(0, source, true, true) == CalibrationResult::Ok, "factory reset");
    check(cp.fwdCalCount == totalCount && cp.refCalCount == totalCount, "factory table capacity");
    for (int i = 0; i < measuredCount; ++i)
    {
        sample(cp.fwdCalPtsV[i], cp.refCalPtsV[i]);
        check(near(sensor.getForwardPowerWatts(), cp.fwdCalPtsW[i]), "forward knot");
        check(near(sensor.getReflectedPowerWatts(), cp.refCalPtsW[i]), "reflected baseline knot");
        check(near(sensor.getForwardPowerWattsSmoothed(), cp.fwdCalPtsW[i]), "smoothed forward knot");
        check(near(sensor.getReflectedPowerWattsSmoothed(), cp.refCalPtsW[i]), "smoothed reflected knot");
    }
    sample(0.0f, 0.0f);
    check(sensor.getForwardPowerWatts() == 0.0f && sensor.getSwr() == 1.0f, "zero power");
    sample(ADC_VREF, ADC_VREF);
    check(near(sensor.getForwardPowerWatts(), cp.fwdCalPtsW[totalCount - 1]), "saturation");
    check(sensor.getSwr() == VSWR_MAX, "equal power SWR");
    sample(cp.fwdCalPtsV[0], ADC_VREF);
    check(sensor.getSwr() == (cp.fwdCalPtsW[0] <= 0.25f ? 1.0f : VSWR_MAX), "near-zero and reflected > forward SWR");
    sample((cp.fwdCalPtsV[3] + cp.fwdCalPtsV[4]) / 2.0f, 0.0f);
    check(near(sensor.getForwardPowerWatts(), (cp.fwdCalPtsW[3] + cp.fwdCalPtsW[4]) / 2.0f), "interpolation");
}

static int applyPoints(size_t count, bool invalidLast = false)
{
    JsonDocument payload;
    payload["index"] = 0;
    payload["path"] = "ref";
    JsonArray points = payload["points"].to<JsonArray>();
    for (size_t i = 0; i < count; ++i)
    {
        JsonObject point = points.add<JsonObject>();
        point["v"] = static_cast<float>(i) * 0.1001234f;
        point["w"] = static_cast<float>(i) * 12.345678f;
        if (invalidLast && i == count - 1)
            point["v"] = 4.0f;
    }

    std::string wire;
    serializeJson(payload, wire);
    check(wire.size() < CouplerCalibrationJson::MaxRequestBytes, "frame fits transport");
    if (count == 20)
        check(wire.size() > 512, "20-point request exercises old serial limit");

    JsonDocument decoded;
    JsonDocument response;
    check(!deserializeJson(decoded, wire), "request JSON parses");
    return CouplerCalibrationJson::apply(decoded.as<JsonObjectConst>(), response);
}

int main()
{
    setDefaultUserPreferences(gPrefs);
    check(!gPrefs.wifiEnabled && gPrefs.wifiSsid[0] == '\0', "new EEPROM starts without Wi-Fi");
    gPrefs.wifiEnabled = true;
    check(saveUserPreferences(gPrefs), "save legacy Wi-Fi preference");
    EEPROM.restart();
    loadUserPreferences(gPrefs);
    check(gPrefs.wifiEnabled, "saved Wi-Fi preference is retained on load");

    auto &cp = gPrefs.couplers[0];
    checkFactory(CouplerType::COUPLER_200W, 19, 19);
    sample(1.980f, 2.570f);
    check(near(sensor.getForwardPowerWatts(), 125.0f), "1.980 V = 125 W");
    check(near(sensor.getReflectedPowerWatts(), 200.0f), "2.570 V = 200 W");
    checkFactory(CouplerType::COUPLER_2KW, 14, 15);

    check(applyPoints(20) == 200 && cp.refCalCount == 20 && cp.fwdCalCount == 15, "20-point apply");
    JsonDocument readback;
    CouplerCalibrationJson::writePath(readback.to<JsonObject>(), cp, false);
    check(readback["count"] == 20 && readback["points"].as<JsonArray>().size() == 20,
          "readback includes every point");

    CalibrationInput legacyForward;
    legacyForward.hasSlope = true;
    legacyForward.slope = 60.0f;
    CalibrationInput legacyReflected;
    legacyReflected.count = CAL_MAX_POINTS + 1;
    check(setCouplerCalibration(0, &legacyForward, &legacyReflected) == CalibrationResult::InvalidPoints &&
          cp.fwdCalCount == 15 && cp.refCalCount == 20, "legacy two-path update is atomic");

    sample(0.0f, cp.refCalPtsV[19]);
    check(near(sensor.getReflectedPowerWatts(), cp.refCalPtsW[19]), "20th uploaded knot is live");
    check(applyPoints(21) == 400 && cp.refCalCount == 20, "oversized request rejected");
    check(applyPoints(20, true) == 400 && cp.refCalCount == 20, "invalid voltage rejected");
    const float invalidVolts[] = {0.5f, 0.4f};
    const float testWatts[] = {10.0f, 20.0f};
    check(calibrateCouplerPathTable(0, true, invalidVolts, testWatts, 2) == CalibrationResult::InvalidPoints,
          "unsorted volts rejected");
    check(calibrateCouplerPath(0, true, 0.1f, 10.0f, 0.1005f, 20.0f) == CalibrationResult::InvalidPoints,
          "invalid two-point fit rejected");

    CalSource mode = CalSource::FactoryTable;
    check(resetCouplerCalibration(0, mode, true, false) == CalibrationResult::Ok &&
          cp.refCalSource == CalSource::User && cp.refCalCount == 20, "one-path reset preserves opposite path");

    cp.couplerType = CouplerType::COUPLER_200W;
    ensureFactoryTableProfileIfAllowed(0, CouplerType::COUPLER_2KW, couplerTypeFromId(cp.couplerType));
    check(cp.fwdCalCount == 19 && cp.refCalCount == 20, "type switch updates only factory path");
    // Emulate an older serial import which left the factory flag on custom points.
    cp.refCalSource = CalSource::FactoryTable;
    check(saveUserPreferences(gPrefs), "save type switch");
    EEPROM.restart();
    loadUserPreferences(gPrefs);
    restoreCouplerCalibration();
    applyCouplerPrefsToRuntime();
    check(cp.fwdCalCount == 19 && cp.refCalCount == 20 && cp.refCalPtsV[0] == 0.0f &&
          cp.refCalSource == CalSource::User, "reboot preserves valid legacy imports including zero knot");

    EEPROM.failCommit = true;
    check(calibrateCouplerPath(0, false, 0.0f, 0.0f, 2.0f, 100.0f) == CalibrationResult::SaveFailed,
          "commit failure returned");
    check(cp.refCalCount == 20 && CalibrationTestAccess::reflectedCount(sensor) == 20, "failed save preserves stored and live selection");
    EEPROM.failCommit = false;
    check(calibrateCouplerPath(0, false, 0.0f, 0.0f, 2.0f, 100.0f) == CalibrationResult::Ok &&
          cp.refCalCount == 0 && CalibrationTestAccess::reflectedCount(sensor) == 0, "linear fit replaces table");
    sample(1.717f, 0.5f);
    check(near(sensor.getForwardPowerWatts(), 100.0f) && near(sensor.getReflectedPowerWatts(), 25.0f) &&
          near(sensor.getSwr(), 3.0f), "100 W / 25 W gives 3:1 SWR");

    EEPROM.restart();
    loadUserPreferences(gPrefs);
    restoreCouplerCalibration();
    applyCouplerPrefsToRuntime();
    check(cp.refCalSource == CalSource::User && cp.refCalCount == 0 &&
          near(sensor.getReflectedPowerWatts(), 25.0f), "reboot restores linear selection");

    float slope = 0.0f, offset = 0.0f;
    check(copyCouplerLinearCalibration(0, false, slope, offset) == CalibrationResult::Ok &&
          cp.fwdCalCount == 0 && cp.refCalCount == 0 && cp.fwdCal_m == cp.refCal_m,
          "legacy linear copy replaces only the target curve");

    adcCounts[0] = ADC_COUNTS_MAX;
    adcCounts[1] = 0;
    sensor.update();
    check(near(sensor.getForwardPowerWatts(), 165.0f) && sensor.getReflectedPowerWatts() == 0.0f &&
          sensor.getSwr() == 1.0f, "ADC burst uses the same calibrated power and SWR");
    std::cout << "Calibration checks passed: 19 + 14 measured knots per path, endpoints, interpolation, SWR, "
                 "20-point JSON, validation, reset, type switch, save failure and reboot.\n";
}
