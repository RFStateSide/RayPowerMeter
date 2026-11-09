// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "CouplerCalibration.h"

#include "eepromprefs.h"
#include "coupler/rfpowersensor.h"
#include <cmath>

extern UserPreferences gPrefs;
extern void applyCouplerPrefsToRuntime();

std::mutex gCalibrationMutex;

static bool validCalibrationPoint(float volts, float watts)
{
    return isfinite(volts) && volts >= 0.0f && volts <= ADC_VREF &&
           isfinite(watts) && watts >= 0.0f;
}

static bool validCalibrationTable(const float *volts, const float *watts, size_t count)
{
    if (!volts || !watts || count < 2 || count > CAL_MAX_POINTS)
        return false;

    for (size_t i = 0; i < count; ++i)
    {
        if (!validCalibrationPoint(volts[i], watts[i]) ||
            (i > 0 && volts[i] - volts[i - 1] <= 1e-6f))
            return false;
    }
    return true;
}

static void setPathCalibration(CouplerPreferences &cp, bool forward, CalSource source,
                               float slope, float offset, const float *volts = nullptr,
                               const float *watts = nullptr, uint8_t count = 0)
{
    float *storedVolts = forward ? cp.fwdCalPtsV : cp.refCalPtsV;
    float *storedWatts = forward ? cp.fwdCalPtsW : cp.refCalPtsW;
    for (uint8_t i = 0; i < CAL_MAX_POINTS; ++i)
    {
        storedVolts[i] = i < count ? volts[i] : 0.0f;
        storedWatts[i] = i < count ? watts[i] : 0.0f;
    }
    (forward ? cp.fwdCalCount : cp.refCalCount) = count;
    (forward ? cp.fwdCalSource : cp.refCalSource) = source;
    (forward ? cp.fwdCal_m : cp.refCal_m) = slope;
    (forward ? cp.fwdCal_b : cp.refCal_b) = offset;
}

// Calibration callers hold gCalibrationMutex through staging, save and runtime apply.
static CalibrationResult saveCouplerCalibration(int index, const CouplerPreferences &previous)
{
    if (!saveUserPreferences(gPrefs))
    {
        gPrefs.couplers[index] = previous;
        return CalibrationResult::SaveFailed;
    }
    applyCouplerPrefsToRuntime();
    return CalibrationResult::Ok;
}

CalibrationResult calibrateCouplerPath(int index, bool forward,
                                       float lowV, float lowW, float highV, float highW)
{
    if (index < 0 || index >= COUPLERS_MAX ||
        !validCalibrationPoint(lowV, lowW) || !validCalibrationPoint(highV, highW) ||
        highV - lowV < 0.001f)
        return CalibrationResult::InvalidPoints;

    const float slope = (highW - lowW) / (highV - lowV);
    const float offset = lowW - slope * lowV;
    if (!isfinite(slope) || !isfinite(offset) || slope < 0.1f || slope > 10000.0f)
        return CalibrationResult::InvalidPoints;

    const std::lock_guard<std::mutex> lock(gCalibrationMutex);
    auto &cp = gPrefs.couplers[index];
    const auto previous = cp;
    setPathCalibration(cp, forward, CalSource::User, slope, offset);
    return saveCouplerCalibration(index, previous);
}

static bool stageTableCalibration(CouplerPreferences &cp, bool forward,
                                  const float *volts, const float *watts, size_t count)
{
    if (!validCalibrationTable(volts, watts, count))
        return false;

    const float slope = (watts[count - 1] - watts[0]) / (volts[count - 1] - volts[0]);
    const float offset = watts[0] - slope * volts[0];
    if (!isfinite(slope) || !isfinite(offset))
        return false;

    setPathCalibration(cp, forward, CalSource::User, slope, offset, volts, watts, count);
    return true;
}

CalibrationResult calibrateCouplerPathTable(int index, bool forward,
                                            const float *volts, const float *watts, size_t count)
{
    if (index < 0 || index >= COUPLERS_MAX)
        return CalibrationResult::InvalidPoints;

    const std::lock_guard<std::mutex> lock(gCalibrationMutex);
    auto &cp = gPrefs.couplers[index];
    const auto previous = cp;
    if (!stageTableCalibration(cp, forward, volts, watts, count))
        return CalibrationResult::InvalidPoints;
    return saveCouplerCalibration(index, previous);
}

static bool stageCalibrationInput(CouplerPreferences &cp, bool forward, const CalibrationInput &input)
{
    if (input.count != 0)
        return stageTableCalibration(cp, forward, input.volts, input.watts, input.count);

    const float slope = input.hasSlope ? input.slope : (forward ? cp.fwdCal_m : cp.refCal_m);
    const float offset = input.hasOffset ? input.offset : (forward ? cp.fwdCal_b : cp.refCal_b);
    if (!isfinite(slope) || slope <= 0.0f || !isfinite(offset))
        return false;

    setPathCalibration(cp, forward, CalSource::User, slope, offset);
    return true;
}

CalibrationResult setCouplerCalibration(int index, const CalibrationInput *forward,
                                        const CalibrationInput *reflected)
{
    if (index < 0 || index >= COUPLERS_MAX || (!forward && !reflected))
        return CalibrationResult::InvalidPoints;

    const std::lock_guard<std::mutex> lock(gCalibrationMutex);
    auto &cp = gPrefs.couplers[index];
    const auto previous = cp;
    auto staged = cp;
    if ((forward && !stageCalibrationInput(staged, true, *forward)) ||
        (reflected && !stageCalibrationInput(staged, false, *reflected)))
        return CalibrationResult::InvalidPoints;

    cp = staged;
    return saveCouplerCalibration(index, previous);
}

CalibrationResult copyCouplerLinearCalibration(int index, bool fromForward, float &slope, float &offset)
{
    if (index < 0 || index >= COUPLERS_MAX)
        return CalibrationResult::InvalidPoints;

    const std::lock_guard<std::mutex> lock(gCalibrationMutex);
    auto &cp = gPrefs.couplers[index];
    slope = fromForward ? cp.fwdCal_m : cp.refCal_m;
    offset = fromForward ? cp.fwdCal_b : cp.refCal_b;
    if (!isfinite(slope) || slope <= 0.0f || !isfinite(offset))
        return CalibrationResult::InvalidPoints;

    const auto previous = cp;
    setPathCalibration(cp, !fromForward, CalSource::User, slope, offset);
    return saveCouplerCalibration(index, previous);
}

// -----------------------------------------------------------------------------
// Factory multi-point calibration profiles (Option B)
//   - Auto-applied when CouplerType changes, unless cal source is User.
//   - These are "shipping defaults" per coupler hardware design.
// -----------------------------------------------------------------------------

struct FactoryCalPoint
{
    float v;
    float w;
};

// 200W profile (measured WN2_HF vs ADC volts) - BAT42W detector
// Divider: R7 & R11=3.3k, R8 & R12=1.6k
static constexpr FactoryCalPoint kCalProfile200W_Fwd_v1[] = {
    {0.005f, 0.1f},
    {0.070f, 0.5f},
    {0.115f, 1.0f},
    {0.360f, 5.0f},
    {0.525f, 10.0f},
    {0.766f, 20.0f},
    {0.957f, 30.0f},
    {1.097f, 40.0f},
    {1.227f, 50.0f},
    {1.351f, 60.0f},
    {1.450f, 70.0f},
    {1.555f, 80.0f},
    {1.666f, 90.0f},
    {1.717f, 100.0f},
    {1.980f, 125.0f},
    {2.150f, 150.0f},
    {2.430f, 175.0f},
    {2.570f, 200.0f},
    {3.000f, 250.0f},
};

// Historical 2 kW forward measurements through 1500 W. The old firmware
// comment named 1N4148; the fabricated RayCouplers design specifies BAT42W.
// Divider: R7 & R11=3.9k, R8 & R12=1.2k.
static constexpr FactoryCalPoint kCalProfile2KW_Fwd_v1[] = {
    {0.014f, 5.0f},
    {0.052f, 10.0f},
    {0.140f, 20.0f},
    {0.195f, 30.0f},
    {0.250f, 40.0f},
    {0.285f, 50.0f},
    {0.375f, 75.0f},
    {0.445f, 100.0f},
    {0.780f, 250.0f},
    {1.180f, 500.0f},
    {1.500f, 750.0f},
    {1.760f, 1000.0f},
    {1.950f, 1250.0f},
    {2.175f, 1500.0f},
    //{2.530f, 2000.0f}, // estimated (not measured yet)
    {2.820f, 2500.0f}, // estimated (not measured yet)
};

static void setFactoryPath(CouplerPreferences &cp, CouplerType type, bool forward, CalSource source)
{
    const FactoryCalPoint *points = nullptr;
    size_t count = 0;
    float slope = 0.0f;
    if (type == CouplerType::COUPLER_200W)
    {
        points = kCalProfile200W_Fwd_v1;
        count = sizeof(kCalProfile200W_Fwd_v1) / sizeof(points[0]);
        slope = kFactoryCal200_m;
    }
    else if (type == CouplerType::COUPLER_2KW)
    {
        points = kCalProfile2KW_Fwd_v1;
        count = sizeof(kCalProfile2KW_Fwd_v1) / sizeof(points[0]);
        slope = kFactoryCal2000_m;
    }

    if (source != CalSource::FactoryTable || !points)
    {
        setPathCalibration(cp, forward, CalSource::FactoryLinear, slope, kFactoryCal_b);
        return;
    }

    // Use the unscaled forward reference on both paths. This is not a reflected measurement.
    float volts[CAL_MAX_POINTS];
    float watts[CAL_MAX_POINTS];
    for (size_t i = 0; i < count; ++i)
    {
        volts[i] = points[i].v;
        watts[i] = points[i].w;
    }
    const size_t last = count - 1;
    slope = (watts[last] - watts[0]) / (volts[last] - volts[0]);
    setPathCalibration(cp, forward, CalSource::FactoryTable, slope,
                       watts[0] - slope * volts[0], volts, watts, count);
}

static_assert(sizeof(kCalProfile200W_Fwd_v1) / sizeof(FactoryCalPoint) <= CAL_MAX_POINTS);
static_assert(sizeof(kCalProfile2KW_Fwd_v1) / sizeof(FactoryCalPoint) <= CAL_MAX_POINTS);

void applyFactoryResetCalSource(uint8_t index, CalSource source, bool forward, bool reflected)
{
    auto &cp = gPrefs.couplers[index];
    const auto type = static_cast<CouplerType>(cp.couplerType);
    if (forward)
        setFactoryPath(cp, type, true, source);
    if (reflected)
        setFactoryPath(cp, type, false, source);
}

CalibrationResult resetCouplerCalibration(int index, CalSource &source, bool forward, bool reflected)
{
    if (index < 0 || index >= COUPLERS_MAX || (!forward && !reflected))
        return CalibrationResult::InvalidPoints;

    const std::lock_guard<std::mutex> lock(gCalibrationMutex);
    auto &cp = gPrefs.couplers[index];
    const auto previous = cp;
    applyFactoryResetCalSource(index, source, forward, reflected);
    source = forward ? cp.fwdCalSource : cp.refCalSource;
    return saveCouplerCalibration(index, previous);
}

void ensureFactoryTableProfileIfAllowed(uint8_t index, CouplerType oldType, CouplerType newType)
{
    if (index >= COUPLERS_MAX || oldType == newType)
        return;

    auto &cp = gPrefs.couplers[index];
    cp.couplerFullScaleW = COUPLER_FULL_SCALE[couplerTypeFromId(newType)];
    if (cp.fwdCalSource != CalSource::User)
        setFactoryPath(cp, newType, true, CalSource::FactoryTable);
    if (cp.refCalSource != CalSource::User)
        setFactoryPath(cp, newType, false, CalSource::FactoryTable);
}

template <size_t N>
static bool matchesFactoryTable(const float *volts, const float *watts, size_t count,
                                const FactoryCalPoint (&profile)[N])
{
    if (count != N)
        return false;
    for (size_t i = 0; i < N; ++i)
    {
        if (volts[i] != profile[i].v || watts[i] != profile[i].w)
            return false;
    }
    return true;
}

// Older serial imports could store user data without updating the source flag.
// Preserve valid, unrecognized curves rather than treating the flag alone as proof.
void restoreCouplerCalibration()
{
    for (auto &cp : gPrefs.couplers)
    {
        const auto type = static_cast<CouplerType>(cp.couplerType);
        for (bool forward : {true, false})
        {
            auto &source = forward ? cp.fwdCalSource : cp.refCalSource;
            auto &count = forward ? cp.fwdCalCount : cp.refCalCount;
            const float *volts = forward ? cp.fwdCalPtsV : cp.refCalPtsV;
            const float *watts = forward ? cp.fwdCalPtsW : cp.refCalPtsW;
            const bool validTable = validCalibrationTable(volts, watts, count);

            if (source == CalSource::User)
            {
                if (count != 0 && !validTable)
                {
                    Serial.printf("[Cal] Ignoring invalid saved %s table; using saved linear calibration if valid\n",
                                  forward ? "forward" : "reflected");
                    count = 0;
                }
                continue;
            }

            const float slope = forward ? cp.fwdCal_m : cp.refCal_m;
            const float offset = forward ? cp.fwdCal_b : cp.refCal_b;
            const bool knownTable = matchesFactoryTable(volts, watts, count, kCalProfile200W_Fwd_v1) ||
                                    matchesFactoryTable(volts, watts, count, kCalProfile2KW_Fwd_v1);
            const bool customLinear = source == CalSource::FactoryLinear && count == 0 &&
                isfinite(slope) && slope > 0.0f && isfinite(offset) &&
                (offset != kFactoryCal_b || (slope != 1.0f && slope != kFactoryCal200_m && slope != kFactoryCal2000_m));
            if ((validTable && !knownTable) || customLinear)
            {
                Serial.printf("[Cal] Preserving unrecognized saved %s calibration as user data\n",
                              forward ? "forward" : "reflected");
                source = CalSource::User;
                continue;
            }

            setFactoryPath(cp, type, forward, source);
        }
    }
}

void applyCouplerCalibrationToSensor(const CouplerPreferences &cp, RfPowerSensor &sensor)
{
    sensor.setFullScaleWatts(cp.couplerFullScaleW);

    // The linear fit remains available as a fallback for the selected table.
    if (isfinite(cp.fwdCal_m) && cp.fwdCal_m >= 0.0f && isfinite(cp.fwdCal_b))
        sensor.setFwdCalLinear(cp.fwdCal_m, cp.fwdCal_b);
    else
        sensor.clearFwdCal();
    if (isfinite(cp.refCal_m) && cp.refCal_m >= 0.0f && isfinite(cp.refCal_b))
        sensor.setRefCalLinear(cp.refCal_m, cp.refCal_b);
    else
        sensor.clearRefCal();

    // A zero count clears an older runtime table when switching to a linear fit.
    sensor.setFwdCalTable(cp.fwdCalPtsV, cp.fwdCalPtsW, cp.fwdCalCount);
    sensor.setRefCalTable(cp.refCalPtsV, cp.refCalPtsW, cp.refCalCount);
}
