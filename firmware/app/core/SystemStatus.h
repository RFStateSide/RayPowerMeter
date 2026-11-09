// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once
#include <Arduino.h>
#include "core/Constants.h"
#include "core/AppState.h"
#include "core/TripTypes.h"
#include "core/userprefs.h"

struct CouplerStatus
{
    float fwdVolts = 0.0f; // raw detector volts
    float refVolts = 0.0f;
    float fwdWatts = 0.0f; // calibrated
    float refWatts = 0.0f;
    float swr = NAN; // per-coupler SWR
    float fwdPeakW = 0.0f;
    float refPeakW = 0.0f;
};

struct AuxInputStatus
{
    char name[16]{};       // "Aux 1", "Battery", etc.
    AuxInputType type;     // current configuration
    float rawVolts = 0.0f; // ADC → volts (0..3.3)
    float value = 0.0f;    // mapped engineering value (0..maxRange)
};

struct AuxDigitalStatus
{
    char name[16]{}; // "AuxD1", "PTT In", etc.
    AuxDigitalMode mode = AuxDigitalMode::Disabled;
    bool value = false; // last sampled logic level
};

struct SystemStatus
{
    // RF / couplers
    CouplerStatus couplers[NUM_COUPLERS];
    AuxInputStatus auxInputs[NUM_AUX_INPUTS];     // AUX1, AUX2, AUX3
    AuxDigitalStatus auxDigital[NUM_AUX_DIGITAL]; // AUXD1..AUXD4

    // “primary” view (e.g., coupler 3 = index 2)
    float primaryFwdWatts = 0.0f;
    float primaryRefWatts = 0.0f;
    float primarySwr = NAN;

    // Protection
    bool protectionTriggered = false;
    RayTripType lastTripType = RayTripType::None;
    RayTripSource lastTripSource = RayTripSource::None;
    String lastAlarmMessage;

    // Mode / global app state
    AppMode mode = AppMode::Idle;

    // RF frequency from prescaler (square-wave at FREQ_INPUT_PIN)
    float rfFrequencyHz = 0.0f;
    bool rfFrequencyValid = false;

    // Network (filled by NetManager, not protection)
    bool wifiConnected = false;
    IPAddress wifiIp;
    int16_t wifiRssi = 0;
    bool ethUp = false;
    IPAddress ethIp;

    uint32_t lastUpdateMs = 0;

    void clearTrips()
    {
        protectionTriggered = false;
        lastTripType = RayTripType::None;
        lastTripSource = RayTripSource::None;
        lastAlarmMessage = "";
    }
};
