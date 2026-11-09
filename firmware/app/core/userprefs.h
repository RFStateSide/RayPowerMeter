// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once
#include <Arduino.h>
#include "core/Constants.h"

// ============================================================================
// RayPowerMeter - User Preferences (EEPROM-backed)
// ============================================================================

// -------------------------------------------------------------------------
// TODO (Migration v1→v2+):
// If adding new fields to UserPreferences or CouplerPreferences, implement
// a versioned migration here instead of wiping EEPROM.
//
// Example:
//   if (tmp.version == 1) {
//       migrateV1toV2(tmp);    // Copy old fields, set defaults for new ones
//       tmp.version = 2;
//       EEPROM.put(EPR::BASE_ADDR, tmp);
//       EEPROM.commit();
//   }
//
// For now, v1 is incompatible → we fall back to full reset.
// -------------------------------------------------------------------------

// "RPMP"
constexpr uint32_t USERPREFS_MAGIC = 0x52504D50;
constexpr uint16_t USERPREFS_VERSION = 3; // TODO: bump on changes + add migration

// Wi-Fi & string limits
constexpr size_t WIFI_SSID_MAX = 32;
constexpr size_t WIFI_PASS_MAX = 64;
constexpr size_t HOSTNAME_MAX = 32;
constexpr size_t CALLSIGN_MAX = 16;
constexpr size_t PRODUCT_NAME_MAX = 32;
constexpr size_t FIRM_VERSION_MAX = 8;
constexpr size_t HMI_VERSION_MAX = 8;
constexpr size_t MFG_DATE_MAX = 16;
constexpr size_t MFG_NAME_MAX = 32;
constexpr size_t SHORT_INFO_MAX = 32;
constexpr size_t LONG_INFO_MAX = 256;

// ---------------------------------------------------------------------------
// Trip mask bits – these map conceptually to RayTripType:
//   HighForwardPower, HighReflectedPower, HighSWR, ExternalInterrupt1, ExternalInterrupt2
// We keep them here so userprefs does not need to include RayProtectionController.h.
// ---------------------------------------------------------------------------
enum : uint8_t
{
    TRIPMASK_HIGH_FWD = 1u << 0,
    TRIPMASK_HIGH_REF = 1u << 1,
    TRIPMASK_HIGH_SWR = 1u << 2,
    TRIPMASK_EXT_INT1 = 1u << 3,
    TRIPMASK_EXT_INT2 = 1u << 4,
};

// ---------------------------------------------------------------------------
// Aux input type (for flexible 0–3.3 V → engineering units mapping)
// ---------------------------------------------------------------------------
enum class AuxInputType : uint8_t
{
    None = 0,       // disabled / not used
    Voltage = 1,    // maps to volts  (0–Vmax)
    Current = 2,    // maps to amps   (0–Imax)
    Temperature = 3 // maps to degC   (sensor/model dependent)
};

// ---------------------------------------------------------------------------
// Aux input temperature sensor model
// ---------------------------------------------------------------------------
enum class AuxTempModel : uint8_t
{
    Linear = 0,        // 0..3.3V -> 0..temperatureMaxRange
    ThermistorBeta = 1 // 10k NTC divider using Beta model
};

// ---------------------------------------------------------------------------
// Per-AUX input preferences
// Each AUX channel can be independently configured as voltage or current.
// 0–3.3 V ADC reading is mapped linearly to either voltageMaxRange or
// currentMaxRange depending on AuxInputType.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// AUX digital I/O configuration
// Each AUXD channel can be independently configured as input or output,
// with optional pull and default output state.
// ---------------------------------------------------------------------------
enum class AuxDigitalMode : uint8_t
{
    Disabled = 0,
    Input = 1,
    Output = 2
};

enum class AuxDigitalPull : uint8_t
{
    None = 0,
    PullUp = 1,
    PullDown = 2
};

struct AuxDigitalPreferences
{
    char name[16];           // "AuxD1", "PTT In", etc.
    AuxDigitalMode mode;     // Disabled / Input / Output
    AuxDigitalPull pull;     // None / PullUp / PullDown
    bool defaultState;       // Output initial state (ignored for inputs)
    uint8_t _reserved[3]{0}; // pad to 4-byte boundary / future use
};
struct AuxInputPreferences
{
    char name[16]; // "Aux 1", "Battery", etc.

    AuxInputType type; // NONE / VOLTAGE / CURRENT / TEMPERATURE

    // Linear engineering ranges for 0..ADC_VREF
    float voltageMaxRange;     // engineering max (e.g. 60.0 V)
    float currentMaxRange;     // engineering max (e.g. 25.0 A)
    float temperatureMaxRange; // engineering max (e.g. 150.0 C) for Linear display/clamp

    // Temperature model (only used when type==Temperature)
    AuxTempModel tempModel; // Linear or ThermistorBeta

    // Thermistor divider parameters (ThermistorBeta model)
    float thermSeriesOhms; // fixed resistor to GND (e.g. 10000)
    float thermR0Ohms;     // thermistor nominal resistance at T0 (e.g. 10000 @ 25C)
    float thermBeta;       // Beta value (e.g. 3950)
    float thermT0C;        // nominal temperature for R0 (usually 25C)

    // Optional post-conversion trim: value = (value * cal_m + cal_b)
    float cal_m;
    float cal_b;

    uint8_t reserved[2]{0, 0};
};

enum class CalSource : uint8_t
{
    FactoryLinear = 0, // your current factory default (m/b only)
    FactoryTable = 1,  // built-in multi-point profile for coupler type
    User = 2           // user-modified (never auto-overwrite)
};

// ---------------------------------------------------------------------------
// Per-coupler preferences
// Each coupler can have its own bar ranges, thresholds, and enabled trip mask.
// ---------------------------------------------------------------------------
struct CouplerPreferences
{
    char name[16]; // "Coupler 1", "HF-1", etc.

    float couplerFullScaleW; // Physical full scale for V→W
    uint8_t couplerType;     // Enum: 0=200W, 1=2kW, etc.

    // Bar graph - UI only
    float fwdBarRangeW; // max forward power for this coupler
    float refBarRangeW; // max reflected power for this coupler

    // --- Legacy 2-point linear calibration (still supported) ---
    float fwdCal_m; // calibration slope
    float fwdCal_b; // calibration offset
    float refCal_m; // calibration slope
    float refCal_b; // calibration offset

    uint8_t fwdCalCount = 0; // 0..CAL_TABLE_MAX
    uint8_t refCalCount = 0; // 0..CAL_TABLE_MAX
    uint8_t _padCal[2]{0, 0};

    float fwdCalPtsV[CAL_MAX_POINTS]; // volts
    float fwdCalPtsW[CAL_MAX_POINTS]; // watts
    float refCalPtsV[CAL_MAX_POINTS]; // volts
    float refCalPtsW[CAL_MAX_POINTS]; // watts

    // --- Calibration come from? (per-path) ---
    CalSource fwdCalSource = CalSource::FactoryLinear;
    CalSource refCalSource = CalSource::FactoryLinear;
    uint8_t _padCalSource[2]{0, 0};

    // Protection thresholds for this coupler
    float highSWRTrip;  // SWR threshold to trip
    float highRefTripW; // Ref power trip (W)
    float highFwdTripW; // Fwd power trip (W)

    // Trip types enabled for this coupler (bitwise OR of TRIPMASK_* above)
    uint8_t enabledTripMask;

    uint8_t reserved[3]; // alignment / future use
};

// ---------------------------------------------------------------------------
// Global preferences blob
// ---------------------------------------------------------------------------
struct UserPreferences
{
    // Header
    uint32_t magic;   // must be USERPREFS_MAGIC
    uint16_t version; // USERPREFS_VERSION
    uint16_t _pad0{0};

    // --- UI / general ---
    uint8_t displayBrightness = 50;     // 10–100
    uint8_t activeCoupler = 0;          // 0..(numCouplers-1)
    uint8_t numCouplers = COUPLERS_MAX; // COUPLERS_MAX (4)
    uint8_t _pad1{0};

    // --- Global protection enable (per-coupler thresholds live in CouplerPreferences) ---
    bool protectionEnabled = true;
    uint8_t _pad2[3]{0, 0, 0};
    bool protectionRelayEnabled = true; // if true, activate alarm relay on trip
    bool extInt1Enabled = true;
    bool extInt2Enabled = true;

    // --- Network / Wi-Fi ---
    bool wifiEnabled = true; // enable WiFi STA
    bool wifiDhcp = true;    // use DHCP vs static
    uint8_t _pad3[2]{0, 0};

    char wifiSsid[WIFI_SSID_MAX]; // saved SSID
    char wifiPass[WIFI_PASS_MAX]; // saved password
    char hostname[HOSTNAME_MAX];  // mDNS / host name

    // --- Identity / metadata / About info ---
    char callSign[CALLSIGN_MAX];        // operator callsign
    char productName[PRODUCT_NAME_MAX]; // e.g. "RayPowerMeter"

    char firmVersion[FIRM_VERSION_MAX]; // firmware version (e.g. "0.1")
    char hmiVersion[HMI_VERSION_MAX];   // Nextion HMI version (e.g. "0.1")
    char mfgDate[MFG_DATE_MAX];         // manufacturing date (e.g. "8/20/2025")
    char mfgName[MFG_NAME_MAX];         // e.g. "RF StateSide LLC"
    char shortInfo[SHORT_INFO_MAX];     // short product name
    char longInfo[LONG_INFO_MAX];       // detailed description

    // --- Per-coupler preferences ---
    CouplerPreferences couplers[COUPLERS_MAX];

    // --- AUX analog input preferences (2 flexible channels) ---
    AuxInputPreferences aux[NUM_AUX_INPUTS]; // [0] -> AUX1_ADC_PIN, [1] -> AUX2_ADC_PIN, [2] -> AUX3_ADC_PIN

    // --- AUX digital I/O preferences ---
    AuxDigitalPreferences auxDigital[NUM_AUX_DIGITAL]; // [0] -> AUXD1_PIN, etc.

    // Reserve a small block for future expansion (calibration tables, etc.)
    uint8_t reserved[32]{0}; // shrink if needed to keep sizeof(UserPreferences) <= EPR::SIZE
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Fill prefs with factory defaults (does NOT write EEPROM).
void setDefaultUserPreferences(UserPreferences &prefs);

// Clamping helpers
inline uint8_t clampNumCouplers(uint8_t n)
{
    if (n < 1)
        return 1;
    if (n > COUPLERS_MAX)
        return COUPLERS_MAX;
    return n;
}

inline uint8_t clampActiveCoupler(uint8_t idx, uint8_t numC)
{
    if (numC == 0)
        return 0;
    if (idx >= numC)
        return 0;
    return idx;
}
