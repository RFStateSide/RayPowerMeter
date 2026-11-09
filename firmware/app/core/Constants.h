// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once
#include <Arduino.h>

// ============================================================================
// RayPowerMeter – Core Constants
// ============================================================================

// ---------------------------------------------------------------------------
// Max couplers supported by firmware/HMI
// ---------------------------------------------------------------------------
constexpr uint8_t COUPLERS_MAX = 4;
// Number of coupler channels in RayPowerMeter
constexpr size_t NUM_COUPLERS = COUPLERS_MAX; // backwards compatibility

// Number of AUX analog inputs
constexpr size_t NUM_AUX_INPUTS = 3;

// Number of AUX digital I/O channels
constexpr size_t NUM_AUX_DIGITAL = 4;

// ---------------------------------------------------------------------------
// Frequency Counter configuration
// ---------------------------------------------------------------------------
constexpr float PRESCALER_DIVIDER = 4.0f; // RF power prescaler divider (4:1 for RayPowerCoupler)

// ---------------------------------------------------------------------------
// Global / shared app constants
// ---------------------------------------------------------------------------
constexpr uint8_t GLOBAL = 255;

// ---------------------------------------------------------------------------
// ADC configuration (MCU version)
// ---------------------------------------------------------------------------
constexpr uint8_t ADC_BITS = 12;                    // ESP32-S3 ADC width
constexpr int ADC_COUNTS_MAX = (1 << ADC_BITS) - 1; // 4095 (based on bits to support other ADCs later e.g. ADS1115)
constexpr float ADC_VREF = 3.3f;                    // adjust if calibrated
static constexpr float ADC_MAX = 4095.0f;           // 12-bit resolution for AuxInputs.cpp

// ---------------------------------------------------------------------------
// ADS1115 configuration (for V2) — kept for compatibility
// ---------------------------------------------------------------------------
// Single-ended mode: library returns 0..32767
constexpr float ADS_COUNTS_MAX_SE = 32767.0f;

// ---------------------------------------------------------------------------
// RF measurement defaults
// ---------------------------------------------------------------------------

// Forward/reflected power ranges (used as UI defaults + safety)
// Values represent MAX expected measurement range per coupler
constexpr float DEFAULT_FWD_BAR_RANGE_W[] = {200.0f, 2000.0f, 2000.0f, 2000.0f};
constexpr float DEFAULT_REF_BAR_RANGE_W[] = {100.0f, 1000.0f, 1000.0f, 1000.0f};

// VSWR sanity minimum/maximum
constexpr float VSWR_MIN = 1.0f;
constexpr float VSWR_MAX = 99.0f;

// CouplerType used by HMI + JSON API (field: type_id)
enum CouplerType : uint8_t
{
    NONE = 0,         // JSON: type_id = 0  ("none")
    COUPLER_200W = 1, // JSON: type_id = 1  ("200w")
    COUPLER_2KW = 2   // JSON: type_id = 2  ("2kw")
};

constexpr const char *COUPLER_TYPE_STR_NONE = "none";
constexpr const char *COUPLER_TYPE_STR_200W = "200w";
constexpr const char *COUPLER_TYPE_STR_2KW = "2kw";

inline const char *couplerTypeToString(CouplerType t)
{
    switch (t)
    {
    case COUPLER_200W:
        return COUPLER_TYPE_STR_200W;
    case COUPLER_2KW:
        return COUPLER_TYPE_STR_2KW;
    default:
        return COUPLER_TYPE_STR_NONE;
    }
}

inline CouplerType couplerTypeFromId(uint8_t id)
{
    switch (id)
    {
    case 1:
        return CouplerType::COUPLER_200W;
    case 2:
        return CouplerType::COUPLER_2KW;
    default:
        return CouplerType::NONE;
    }
}

// Physical full-scale power (watts) per coupler type
constexpr float COUPLER_FULL_SCALE[] = {
    0.0f,   // NONE
    200.0f, // 200W coupler
    2000.0f // 2kW coupler
};

// Factory calibration defaults per coupler type for 2-point linear cal (m/b).
static constexpr float kFactoryCal200_m = 60.6060606f;  // example: 200W full scale over 3.3V ADC
static constexpr float kFactoryCal2000_m = 606.060606f; // example: 2000W full scale over 3.3V ADC
static constexpr float kFactoryCal_b = 0.0f;
static constexpr float kLeakCoef = 0.10f; // 10% leakage correction

// Factory multi-point table profiles are defined in main.cpp for better access to PROGMEM.
static constexpr uint8_t CAL_MAX_POINTS = 20; // max points in calibration table (per path)

// Aux Analog Max Ranges & Thermistor Defaults
static constexpr float AUX_VOLTAGE_MAX_RANGE_DEFAULT = 65.0f;
static constexpr float AUX_CURRENT_MAX_RANGE_DEFAULT = 45.0f;
static constexpr float AUX_TEMPERATURE_MAX_RANGE_DEFAULT = 150.0f;
static constexpr float AUX_THERMISTOR_SERIES_OHMS_DEFAULT = 10000.0f; // 10k ohms
static constexpr float AUX_THERMISTOR_R0_OHMS_DEFAULT = 10000.0f;
static constexpr float AUX_THERMISTOR_BETA_DEFAULT = 3950.0f;
static constexpr float AUX_THERMISTOR_T0C_DEFAULT = 25.0f;

// Status icon picture IDs for pC?Stat / pCiStat
constexpr uint16_t HMI_PIC_STAT_OK = 1;   // green / normal
constexpr uint16_t HMI_PIC_STAT_ALRM = 2; // alarm / tripped
constexpr uint16_t HMI_PIC_STAT_NA = 3;   // N/A / not used

// pConfigAlarms (page 18)
#define HMI_COMP_A1_FWD_TRIP "nA1FwdTh"
#define HMI_COMP_A1_REF_TRIP "nA1RefTh"
#define HMI_COMP_A1_SWR_TRIP "xA1SwrTh"
#define HMI_COMP_A1_HFWD "cbA1HFwd"
#define HMI_COMP_A1_HREF "cbA1HRef"
#define HMI_COMP_A1_HSWR "cbA1HSwr"

#define HMI_COMP_A2_FWD_TRIP "nA2FwdTh"
#define HMI_COMP_A2_REF_TRIP "nA2RefTh"
#define HMI_COMP_A2_SWR_TRIP "xA2SwrTh"
#define HMI_COMP_A2_HFWD "cbA2HFwd"
#define HMI_COMP_A2_HREF "cbA2HRef"
#define HMI_COMP_A2_HSWR "cbA2HSwr"

#define HMI_COMP_A3_FWD_TRIP "nA3FwdTh"
#define HMI_COMP_A3_REF_TRIP "nA3RefTh"
#define HMI_COMP_A3_SWR_TRIP "xA3SwrTh"
#define HMI_COMP_A3_HFWD "cbA3HFwd"
#define HMI_COMP_A3_HREF "cbA3HRef"
#define HMI_COMP_A3_HSWR "cbA3HSwr"

#define HMI_COMP_A4_FWD_TRIP "nA4FwdTh"
#define HMI_COMP_A4_REF_TRIP "nA4RefTh"
#define HMI_COMP_A4_SWR_TRIP "xA4SwrTh"
#define HMI_COMP_A4_HFWD "cbA4HFwd"
#define HMI_COMP_A4_HREF "cbA4HRef"
#define HMI_COMP_A4_HSWR "cbA4HSwr"

#define HMI_COMP_ALARM_RELAY "cbRelayEnable"

// ---------------------------------------------------------------------------
// Boot window where protection is ignored
// ---------------------------------------------------------------------------
constexpr uint32_t BOOT_IGNORED_MS = 2000; // short delay for RayPowerMeter

// ---------------------------------------------------------------------------
// Protection timing & thresholds
// ---------------------------------------------------------------------------
constexpr uint32_t PROTECTION_CHECK_PERIOD_MS = 20; // run protection @50 Hz
constexpr uint32_t TRIP_HOLD_TIME_MS = 100;         // debounce trip detection
constexpr uint32_t TRIP_COOLDOWN_MS = 5000;         // allow reset after 5s

// ---------------------------------------------------------------------------
// JSON / Web API cadences
// ---------------------------------------------------------------------------
constexpr uint32_t API_STATUS_CADENCE_MS = 200; // ~5 Hz web/API refresh

// ---------------------------------------------------------------------------
// Event log configuration (future use)
// ---------------------------------------------------------------------------
constexpr const char *PROT_FILE_NAME = "/protect.log";
constexpr uint32_t PROT_LOG_MAX_SIZE = 1024UL * 1024UL; // 1 MB

// ---------------------------------------------------------------------------
// Utility constants
// ---------------------------------------------------------------------------

// Convert signed 16-bit ADS to volts given ±4.096V FSR
// Keeping this as placeholder until real ADS driver is added.
constexpr float ADS_LSB_4V096 = 4.096f / 32768.0f;

// Return a clipped SWR value to stay sane for display/API
static inline float clipVswr(float v)
{
    if (v < VSWR_MIN)
        return VSWR_MIN;
    if (v > VSWR_MAX)
        return VSWR_MAX;
    return v;
}

// ------------------------------------------------------------------
// ADC Trimmed mean configuration
// ------------------------------------------------------------------
static constexpr uint8_t ADC_SAMPLES_BURST = 16; // number of ADC samples to average
static constexpr uint16_t ADC_AVG_DELAY_US = 0;  // start with 0; raise to ~10–30 if needed
static constexpr int ADC_TRIM_COUNT = 2;         // number of samples for trimmed mean, thrown from each end

// ------------------------------------------------------------------
// RF power smoothing / "meter ballistics" (tune for responsiveness vs stability)
// Notes:
// - update() runs ~50 Hz (every 20 ms) in main loop.
// - Use fast attack (rise) and slower decay (fall) for a natural analog meter feel.
// ------------------------------------------------------------------
constexpr float RF_EMA_FWD_ATTACK = 0.65f; // 0.35–0.60: higher = snappier on rising power
constexpr float RF_EMA_FWD_DECAY = 0.18f;  // 0.06–0.20: lower = steadier but more lag on falling power
constexpr float RF_EMA_REF_ATTACK = 0.35f; // reflected tends to be noisier; keep slightly calmer
constexpr float RF_EMA_REF_DECAY = 0.10f;

// SWR stability: treat very small reflected power as noise (applied only in getSwrSmoothed()).
constexpr float RF_SWR_REF_FLOOR_W_ABS = 0.02f;  // absolute floor in watts
constexpr float RF_SWR_REF_FLOOR_W_REL = 0.005f; // relative floor (fraction of forward watts)

// ---------------------------------------------------------------------------
// Network / mDNS
// ---------------------------------------------------------------------------

// mDNS hostname: <host>.local
constexpr char MDNS_HOSTNAME[] = "raypowermeter";
