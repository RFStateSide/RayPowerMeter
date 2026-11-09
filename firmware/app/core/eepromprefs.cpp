// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "eepromprefs.h"
#include <string.h>
#include <mutex>
#include "Constants.h"

// Helper to track whether EEPROM has been initialized
static bool s_eepromInit = false;

static bool ensureEepromInit_()
{
    if (!s_eepromInit)
    {
        s_eepromInit = EEPROM.begin(EPR::SIZE);
    }
    return s_eepromInit;
}

// Helper to print multi-point calibration table
static void printCalTable_(Stream &s,
                           const char *label,
                           uint8_t count,
                           const float *v,
                           const float *w)
{
    if (count < 2)
        return;

    s.printf("  %s: table (%u pts)\n", label, static_cast<unsigned>(count));

    for (uint8_t i = 0; i < count && i < CAL_MAX_POINTS; ++i)
    {
        s.printf("    [%u] v=%.6f  w=%.1f\n",
                 static_cast<unsigned>(i),
                 static_cast<double>(v[i]),
                 static_cast<double>(w[i]));
    }
}

// Helper to check if aux input name looks "unset" (empty or "Aux 1/2/3")
static bool isDefaultAuxName_(const char *name, uint8_t idx)
{
    if (!name || name[0] == '\0')
        return true;

    // Accept both empty and legacy "Aux 1/2/3" as "unset"
    char expected[16];
    snprintf(expected, sizeof(expected), "Aux %u", (unsigned)(idx + 1));
    return (strcmp(name, expected) == 0);
}

// Helpers to apply default AuxInputType and names if all aux channels are currently None and have default/empty names.
static void applyDefaultAuxTypesIfUnset_(UserPreferences &prefs)
{
    // Only do this if ALL aux channels are currently None
    bool allNone = true;
    for (uint8_t i = 0; i < NUM_AUX_INPUTS; ++i)
    {
        if (prefs.aux[i].type != AuxInputType::None)
        {
            allNone = false;
            break;
        }
    }
    if (!allNone)
        return;

    // Only do it if names look "unset" (empty or "Aux 1/2/3")
    bool namesUnset = true;
    for (uint8_t i = 0; i < NUM_AUX_INPUTS; ++i)
    {
        if (!isDefaultAuxName_(prefs.aux[i].name, i))
        {
            namesUnset = false;
            break;
        }
    }
    if (!namesUnset)
        return;

    // Apply your baseline defaults so /api/status always shows voltage/current/temp
    prefs.aux[0].type = AuxInputType::Voltage;
    strlcpy(prefs.aux[0].name, "Voltage", sizeof(prefs.aux[0].name));

    prefs.aux[1].type = AuxInputType::Current;
    strlcpy(prefs.aux[1].name, "Current", sizeof(prefs.aux[1].name));

    prefs.aux[2].type = AuxInputType::Temperature;
    strlcpy(prefs.aux[2].name, "Temperature", sizeof(prefs.aux[2].name));

    // Note: ranges/cal already clamped in sanity loop, so leave them as-is.
    Serial.println("[Prefs] Aux inputs were unset -> applied default V/C/T types + names");
}

bool prefsStorageIsSane()
{
    return sizeof(UserPreferences) <= static_cast<size_t>(EPR::SIZE);
}

void setDefaultUserPreferences(UserPreferences &p)
{
    // Zero everything first
    memset(&p, 0, sizeof(p));

    p.magic = USERPREFS_MAGIC;
    p.version = USERPREFS_VERSION;

    // Core defaults
    p.displayBrightness = 50;
    p.numCouplers = COUPLERS_MAX;
    p.activeCoupler = 0;
    p.protectionEnabled = true;

    p.wifiEnabled = false;
    p.wifiDhcp = true;

    // Strings: Wi-Fi / hostname
    p.wifiSsid[0] = '\0';

    p.wifiPass[0] = '\0'; // configured later

    strncpy(p.hostname, "raypowermeter", HOSTNAME_MAX - 1);
    p.hostname[HOSTNAME_MAX - 1] = '\0';

    // Identity / About metadata
    p.callSign[0] = '\0'; // user can set later

    strncpy(p.productName, "RayPowerMeter", PRODUCT_NAME_MAX - 1);
    p.productName[PRODUCT_NAME_MAX - 1] = '\0';

    strncpy(p.firmVersion, "0.1", FIRM_VERSION_MAX - 1);
    p.firmVersion[FIRM_VERSION_MAX - 1] = '\0';

    strncpy(p.hmiVersion, "0.1", HMI_VERSION_MAX - 1);
    p.hmiVersion[HMI_VERSION_MAX - 1] = '\0';

    strncpy(p.mfgDate, "8/20/2025", MFG_DATE_MAX - 1);
    p.mfgDate[MFG_DATE_MAX - 1] = '\0';

    strncpy(p.mfgName, "RF StateSide LLC", MFG_NAME_MAX - 1);
    p.mfgName[MFG_NAME_MAX - 1] = '\0';

    strncpy(p.shortInfo, "RayPowerMeter", SHORT_INFO_MAX - 1);
    p.shortInfo[SHORT_INFO_MAX - 1] = '\0';

    strncpy(p.longInfo,
            "RayPowerMeter RF coupler monitor - multi-coupler, Wi-Fi enabled power and SWR telemetry.",
            LONG_INFO_MAX - 1);
    p.longInfo[LONG_INFO_MAX - 1] = '\0';

    // Per-coupler defaults
    for (uint8_t i = 0; i < COUPLERS_MAX; ++i)
    {
        CouplerPreferences &c = p.couplers[i];

        // Name: "Coupler 1" .. "Coupler 4"
        char buf[16];
        snprintf(buf, sizeof(buf), "Coupler %u", static_cast<unsigned>(i + 1));
        strncpy(c.name, buf, sizeof(c.name) - 1);
        c.name[sizeof(c.name) - 1] = '\0';

        // ---------------------------------------------------------------------
        // Default ALL couplers to NONE (disabled) on fresh EEPROM/version bump.
        // BUT suggest bar ranges - shipping defaults so the HMI
        // looks reasonable until user selects the real coupler type.
        // ---------------------------------------------------------------------
        c.couplerType = CouplerType::NONE;

        // Preserve prior bar ranges (visual only until enabled)
        if (i == 0)
        {
            c.couplerFullScaleW = 200.0f;
            c.fwdBarRangeW = 200.0f;
            c.refBarRangeW = 100.0f;

            c.highFwdTripW = 200.0f;
            c.highRefTripW = 100.0f;
        }
        else
        {
            c.couplerFullScaleW = 2000.0f;
            c.fwdBarRangeW = 2000.0f;
            c.refBarRangeW = 1000.0f;

            c.highFwdTripW = 2000.0f;
            c.highRefTripW = 1000.0f;
        }

        // Trips disabled by default for NONE (prevents floating-coupler false trips)
        c.highSWRTrip = 3.0f;
        c.enabledTripMask = 0;

        // Calibration defaults (safe identity; ignored while type is NONE)
        c.fwdCal_m = 1.0f;
        c.fwdCal_b = 0.0f;
        c.refCal_m = 1.0f;
        c.refCal_b = 0.0f;

        // Calibration tables cleared
        c.fwdCalCount = 0;
        c.refCalCount = 0;
        memset(c.fwdCalPtsV, 0, sizeof(c.fwdCalPtsV));
        memset(c.fwdCalPtsW, 0, sizeof(c.fwdCalPtsW));
        memset(c.refCalPtsV, 0, sizeof(c.refCalPtsV));
        memset(c.refCalPtsW, 0, sizeof(c.refCalPtsW));

        // CalSource fallback (keeps your “default to FactoryTable if no CalSource” rule consistent)
        c.fwdCalSource = CalSource::FactoryTable;
        c.refCalSource = CalSource::FactoryTable;

        memset(c.reserved, 0, sizeof(c.reserved));
    }

    // AUX analog defaults
    for (uint8_t i = 0; i < NUM_AUX_INPUTS; ++i)
    {
        // Name: "Aux 1", "Aux 2"
        char buf[16];
        snprintf(buf, sizeof(buf), "Aux %u", static_cast<unsigned>(i + 1));
        strncpy(p.aux[i].name, buf, sizeof(p.aux[i].name) - 1);
        p.aux[i].name[sizeof(p.aux[i].name) - 1] = '\0';

        switch (i)
        {
        case 0:
            p.aux[i].type = AuxInputType::Voltage;
            p.aux[i].voltageMaxRange = AUX_VOLTAGE_MAX_RANGE_DEFAULT;
            break;
        case 1:
            p.aux[i].type = AuxInputType::Current;
            p.aux[i].currentMaxRange = AUX_CURRENT_MAX_RANGE_DEFAULT;
            break;
        case 2:
            p.aux[i].type = AuxInputType::Temperature;
            p.aux[i].temperatureMaxRange = AUX_TEMPERATURE_MAX_RANGE_DEFAULT;
            p.aux[i].tempModel = AuxTempModel::ThermistorBeta;
            break;
        default:
            p.aux[i].type = AuxInputType::None;
            p.aux[i].voltageMaxRange = AUX_VOLTAGE_MAX_RANGE_DEFAULT;
        }

        // 10k NTC + 10k series-to-GND, Beta=3950, R0=10k @ 25C
        p.aux[i].thermSeriesOhms = AUX_THERMISTOR_SERIES_OHMS_DEFAULT;
        p.aux[i].thermR0Ohms = AUX_THERMISTOR_R0_OHMS_DEFAULT;
        p.aux[i].thermBeta = AUX_THERMISTOR_BETA_DEFAULT;
        p.aux[i].thermT0C = AUX_THERMISTOR_T0C_DEFAULT;

        p.aux[i].cal_m = 1.0f;
        p.aux[i].cal_b = 0.0f;
    }

    // AUX digital defaults
    for (uint8_t i = 0; i < NUM_AUX_DIGITAL; ++i)
    {
        // Name: "AuxD1", "AuxD2", etc.
        char buf[16];
        snprintf(buf, sizeof(buf), "AuxD%u", static_cast<unsigned>(i + 1));
        strncpy(p.auxDigital[i].name, buf, sizeof(p.auxDigital[i].name) - 1);
        p.auxDigital[i].name[sizeof(p.auxDigital[i].name) - 1] = '\0';

        p.auxDigital[i].mode = AuxDigitalMode::Disabled;
        p.auxDigital[i].pull = AuxDigitalPull::None;
        p.auxDigital[i].defaultState = false;
    }
}

static bool headerValid_(const UserPreferences &p)
{
    return (p.magic == USERPREFS_MAGIC &&
            p.version == USERPREFS_VERSION);
}

void loadUserPreferences(UserPreferences &prefs)
{
    if (!prefsStorageIsSane() || !ensureEepromInit_())
    {
        setDefaultUserPreferences(prefs);
        return;
    }

    UserPreferences tmp{};
    EEPROM.get(EPR::BASE_ADDR, tmp);

    if (!headerValid_(tmp))
    {
        // First boot or incompatible version – reset to defaults and save.
        setDefaultUserPreferences(prefs);
        saveUserPreferences(prefs);
        return;
    }

    prefs = tmp;

    // Basic sanity clamps
    prefs.numCouplers = clampNumCouplers(prefs.numCouplers);
    prefs.activeCoupler = clampActiveCoupler(prefs.activeCoupler,
                                             prefs.numCouplers);

    if (prefs.displayBrightness > 100)
        prefs.displayBrightness = 100;

    // Per-coupler sanity
    for (uint8_t i = 0; i < COUPLERS_MAX; ++i)
    {
        // SWR trip should be in a reasonable range (e.g. 1.0–10.0)
        auto &c = prefs.couplers[i];
        if (c.highSWRTrip < 1.0f || c.highSWRTrip > 10.0f)
            c.highSWRTrip = 3.0f;
        if (c.fwdBarRangeW <= 0.0f)
            c.fwdBarRangeW = DEFAULT_FWD_BAR_RANGE_W[i];
        if (c.refBarRangeW <= 0.0f)
            c.refBarRangeW = DEFAULT_REF_BAR_RANGE_W[i];

        // ---- CalSource sanity (default fallback to FactoryTable) ----
        auto isValidCalSource = [](uint8_t v) -> bool
        {
            return v <= static_cast<uint8_t>(CalSource::User);
        };

        if (!isValidCalSource(static_cast<uint8_t>(c.fwdCalSource)))
            c.fwdCalSource = CalSource::FactoryTable;
        if (!isValidCalSource(static_cast<uint8_t>(c.refCalSource)))
            c.refCalSource = CalSource::FactoryTable;
    }

    // AUX input sane ranges
    for (uint8_t i = 0; i < NUM_AUX_INPUTS; ++i)
    {
        auto &a = prefs.aux[i];

        if (a.name[0] == '\0')
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "Aux %u", static_cast<unsigned>(i + 1));
            strncpy(a.name, buf, sizeof(a.name) - 1);
            a.name[sizeof(a.name) - 1] = '\0';
        }

        if (a.voltageMaxRange <= 0.0f || !isfinite(a.voltageMaxRange))
            a.voltageMaxRange = AUX_VOLTAGE_MAX_RANGE_DEFAULT;
        if (a.currentMaxRange <= 0.0f || !isfinite(a.currentMaxRange))
            a.currentMaxRange = AUX_CURRENT_MAX_RANGE_DEFAULT;
        if (a.temperatureMaxRange <= 0.0f || !isfinite(a.temperatureMaxRange))
            a.temperatureMaxRange = AUX_TEMPERATURE_MAX_RANGE_DEFAULT;

        if (a.tempModel != AuxTempModel::Linear &&
            a.tempModel != AuxTempModel::ThermistorBeta)
        {
            a.tempModel = AuxTempModel::ThermistorBeta;
        }

        if (a.thermSeriesOhms <= 0.0f || !isfinite(a.thermSeriesOhms))
            a.thermSeriesOhms = AUX_THERMISTOR_SERIES_OHMS_DEFAULT;
        if (a.thermR0Ohms <= 0.0f || !isfinite(a.thermR0Ohms))
            a.thermR0Ohms = 10000.0f;
        if (a.thermBeta <= 0.0f || !isfinite(a.thermBeta))
            a.thermBeta = 3950.0f;
        if (!isfinite(a.thermT0C))
            a.thermT0C = 25.0f;

        if (!isfinite(a.cal_m) || a.cal_m == 0.0f)
            a.cal_m = 1.0f;
        if (!isfinite(a.cal_b))
            a.cal_b = 0.0f;

        if (a.type != AuxInputType::Voltage &&
            a.type != AuxInputType::Current &&
            a.type != AuxInputType::Temperature)
        {
            a.type = AuxInputType::None;
        }
    }

    applyDefaultAuxTypesIfUnset_(prefs);

    // AUX digital sanity
    for (uint8_t i = 0; i < NUM_AUX_DIGITAL; ++i)
    {
        auto &d = prefs.auxDigital[i];

        if (d.name[0] == '\0')
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "AuxD%u", static_cast<unsigned>(i + 1));
            strncpy(d.name, buf, sizeof(d.name) - 1);
            d.name[sizeof(d.name) - 1] = '\0';
        }

        // Clamp mode
        if (d.mode != AuxDigitalMode::Disabled &&
            d.mode != AuxDigitalMode::Input &&
            d.mode != AuxDigitalMode::Output)
        {
            d.mode = AuxDigitalMode::Disabled;
        }

        // Clamp pull
        if (d.pull != AuxDigitalPull::None &&
            d.pull != AuxDigitalPull::PullUp &&
            d.pull != AuxDigitalPull::PullDown)
        {
            d.pull = AuxDigitalPull::None;
        }

        // defaultState is just a bool; no extra clamp needed
    }
}

bool saveUserPreferences(const UserPreferences &prefsIn)
{
    static std::mutex storageMutex;
    const std::lock_guard<std::mutex> lock(storageMutex);
    if (!prefsStorageIsSane() || !ensureEepromInit_())
        return false;

    // Keep the full blobs off the ESP32 loop and AsyncTCP task stacks.
    static UserPreferences tmp;
    static UserPreferences previous;
    tmp = prefsIn;
    tmp.magic = USERPREFS_MAGIC;
    tmp.version = USERPREFS_VERSION;

    // A failed commit leaves the staged bytes in the EEPROM library's RAM buffer.
    EEPROM.get(EPR::BASE_ADDR, previous);
    EEPROM.put(EPR::BASE_ADDR, tmp);
    if (EEPROM.commit())
        return true;

    EEPROM.put(EPR::BASE_ADDR, previous);
    return false;
}

void resetUserPreferences(UserPreferences &prefs)
{
    setDefaultUserPreferences(prefs);
    saveUserPreferences(prefs);
}

void printUserPreferences(const UserPreferences &p, Stream &s)
{
    s.println(F("=== RayPowerMeter UserPreferences ==="));
    s.printf("magic=0x%08lX  ver=%u\n",
             static_cast<unsigned long>(p.magic),
             static_cast<unsigned>(p.version));

    s.printf("displayBrightness=%u\n", p.displayBrightness);
    s.printf("numCouplers=%u  activeCoupler=%u\n",
             p.numCouplers, p.activeCoupler);
    s.printf("protectionEnabled=%s\n",
             p.protectionEnabled ? "true" : "false");

    s.printf("wifiEnabled=%s  wifiDhcp=%s\n",
             p.wifiEnabled ? "true" : "false",
             p.wifiDhcp ? "true" : "false");
    s.printf("wifiSsid=\"%s\"\n", p.wifiSsid);
    s.printf("hostname=\"%s\"\n", p.hostname);

    s.printf("callSign=\"%s\"\n", p.callSign);
    s.printf("productName=\"%s\"\n", p.productName);
    s.printf("firmVersion=\"%s\"  hmiVersion=\"%s\"\n",
             p.firmVersion, p.hmiVersion);
    s.printf("mfgDate=\"%s\"  mfgName=\"%s\"\n",
             p.mfgDate, p.mfgName);
    s.printf("shortInfo=\"%s\"\n", p.shortInfo);
    s.printf("longInfo=\"%s\"\n", p.longInfo);

    for (uint8_t i = 0; i < COUPLERS_MAX; ++i)
    {
        const auto &c = p.couplers[i];
        s.printf("Coupler[%u]: name=\"%s\" fwdRange=%.1fW refRange=%.1fW "
                 "SWRtrip=%.2f refTrip=%.1fW fwdTrip=%.1fW mask=0x%02X\n",
                 i, c.name,
                 c.fwdBarRangeW, c.refBarRangeW,
                 c.highSWRTrip, c.highRefTripW, c.highFwdTripW,
                 c.enabledTripMask);

        // Multi-point calibration tables (only printed when count >= 2)
        printCalTable_(s, "fwdCal", c.fwdCalCount, c.fwdCalPtsV, c.fwdCalPtsW);
        printCalTable_(s, "refCal", c.refCalCount, c.refCalPtsV, c.refCalPtsW);
    }

    s.println(F("====================================="));
}
