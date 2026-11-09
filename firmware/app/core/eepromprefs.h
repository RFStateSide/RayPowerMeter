// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "userprefs.h"

// ============================================================================
// EEPROM layout - RayPowerMeter
// Single UserPreferences blob stored at address 0.
// ============================================================================

namespace EPR
{
    constexpr int SIZE = 4096; // big enough for UserPreferences
    constexpr int BASE_ADDR = 0;
}

static_assert(sizeof(UserPreferences) <= EPR::SIZE,
              "UserPreferences no longer fits in EEPROM - increase EPR::SIZE or shrink the struct.");

// Returns true if sizeof(UserPreferences) fits in the allocated EEPROM size.
bool prefsStorageIsSane();

// Loads preferences from EEPROM (or sets defaults if invalid),
// always ensuring magic/version and reasonable values.
// Also calls EEPROM.begin(EPR::SIZE) on first use.
void loadUserPreferences(UserPreferences &prefs);

// Returns true when EEPROM has committed the preferences.
bool saveUserPreferences(const UserPreferences &prefs);

// Resets prefs to factory defaults and saves them.
void resetUserPreferences(UserPreferences &prefs);

// Debug helper to print prefs to Serial (optional, but super handy).
void printUserPreferences(const UserPreferences &prefs, Stream &s = Serial);
