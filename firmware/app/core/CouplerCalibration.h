// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "userprefs.h"
#include <mutex>

extern std::mutex gCalibrationMutex;

enum class CalibrationResult
{
    Ok,
    InvalidPoints,
    SaveFailed
};

struct CalibrationInput
{
    float volts[CAL_MAX_POINTS];
    float watts[CAL_MAX_POINTS];
    size_t count = 0;
    bool hasSlope = false;
    bool hasOffset = false;
    float slope = 0.0f;
    float offset = 0.0f;
};

CalibrationResult setCouplerCalibration(int index, const CalibrationInput *forward,
                                        const CalibrationInput *reflected);

CalibrationResult calibrateCouplerPath(int index, bool forward,
                                       float lowV, float lowW, float highV, float highW);
CalibrationResult calibrateCouplerPathTable(int index, bool forward,
                                            const float *volts, const float *watts, size_t count);
CalibrationResult copyCouplerLinearCalibration(int index, bool fromForward, float &slope, float &offset);
CalibrationResult resetCouplerCalibration(int index, CalSource &source, bool forward, bool reflected);

class RfPowerSensor;
void applyCouplerCalibrationToSensor(const CouplerPreferences &cp, RfPowerSensor &sensor);
void restoreCouplerCalibration();

// Stage factory calibration for callers already updating a larger preferences transaction.
void applyFactoryResetCalSource(uint8_t index, CalSource source, bool forward, bool reflected);
void ensureFactoryTableProfileIfAllowed(uint8_t index, CouplerType oldType, CouplerType newType);
