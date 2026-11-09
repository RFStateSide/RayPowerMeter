// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

// Constants.h
#pragma once

constexpr bool DEBUG_HAMLIB_FREQ = false;

// Maximum number of couplers the system supports.
// We use this everywhere instead of hard-coding "4" or "3".
constexpr int MAX_COUPLERS = 4;
constexpr int MAX_AUX_CHANNELS = 3;
constexpr int MAX_AUX_DIGITAL_CHANNELS = 4; // 4 channels: AuxD1..AuxD4

constexpr int MIN_GRID_COLS = 2;
constexpr int MAX_GRID_COLS = 4;

// List of creatable widget types for layout designer
static const char* const WIDGET_TYPE_LIST[] = {"PowerBar",
                                               "PowerBarVert",
                                               "AnalogPowerMeter",
                                               "ForwardRefPanel",
                                               "AuxVoltage",
                                               "AuxSensor",
                                               "AuxDigital",
                                               "GainWidget",
                                               "FreqWidget",
                                               "MiniScope",
                                               "Spacer"};

constexpr int WIDGET_TYPE_COUNT = sizeof(WIDGET_TYPE_LIST) / sizeof(WIDGET_TYPE_LIST[0]);

// Scale factor limits
constexpr int SCALE_FACTOR_MIN = 50;  // 0.5 percent
constexpr int SCALE_FACTOR_MAX = 125; // 1.25 percent
constexpr double MIN_UI_FONT_PT = 9.0;
constexpr double MIN_UI_FONT_PX = 12.0;
constexpr double BASE_UI_FONT_PT = 10.0;
constexpr double MIN_TICK_FONT_PT = 7.5; // tick labels only

// Modulation Scope: Live-mode quiet detection
// If fewer than MODULATION_QUIET_ABOVE_FRACTION of samples exceed the Vpp threshold,
// treat the capture as "quiet" and hold the last valid capture.
constexpr double MODULATION_QUIET_VPP_THRESHOLD = 10.0; // Vpp @ 50Ω
constexpr double MODULATION_QUIET_ABOVE_FRACTION
    = 0.05; // 5% of samples must exceed threshold to be "active"

// ------------------------------------------------------------------
// SWR vs Frequency Scope: fixed Y ranges (when Auto-scale is OFF)
// ------------------------------------------------------------------
constexpr double SWR_VS_FREQ_FIXED_SWR_MAX = 3.0;

// Professional Return Loss display: RL_dB = 20*log10(|Γ|) (negative, 0 = worst).
// VSWR=3 => Γ=0.5 => RL=-6.0206 dB -> use -6 dB as a nice round fixed min.
constexpr double SWR_VS_FREQ_FIXED_RL_DB_MIN = -30.0;
constexpr double SWR_VS_FREQ_FIXED_RL_DB_MAX = 0.0;

// Clamp for "near perfect match" (RL -> -inf). Use a practical floor for display.
constexpr double SWR_VS_FREQ_RL_DB_FLOOR = -60.0;

// Maximum number of calibration points we allow the user to enter for a coupler path. This is a hard limit in the code firmware code.
constexpr int MAX_CAL_POINTS = 20;

// Aux Analog Max Ranges & Thermistor Defaults
static constexpr float AUX_VOLTAGE_MAX_RANGE_DEFAULT = 65.0f;
static constexpr float AUX_CURRENT_MAX_RANGE_DEFAULT = 25.0f;
static constexpr float AUX_TEMPERATURE_MAX_RANGE_DEFAULT = 150.0f;
static constexpr float AUX_THERMISTOR_SERIES_OHMS_DEFAULT = 10000.0f; // 10k ohms
static constexpr float AUX_THERMISTOR_R0_OHMS_DEFAULT = 10000.0f;
static constexpr float AUX_THERMISTOR_BETA_DEFAULT = 3950.0f;
static constexpr float AUX_THERMISTOR_T0C_DEFAULT = 25.0f;
