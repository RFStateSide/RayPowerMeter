// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QString>

namespace raypm
{

    // Qt-side mirror of the firmware AuxInputPreferences + calibration fields.
    // JSON example (GET /api/aux/config):
    // {
    //   "type": "aux_config",
    //   "status": "ok",
    //   "aux": [
    //     {
    //       "index": 0,
    //       "name": "Aux 1",
    //       "type": "voltage",
    //       "tempModel": "thermistor",
    //       "voltageMaxRange": 60,
    //       "currentMaxRange": 25,
    //       "temperatureMaxRange": 150,
    //       "thermSeriesOhms": 10000,
    //       "thermR0Ohms": 10000,
    //       "thermBeta": 3950,
    //       "thermT0C": 25,
    //       "cal_m": 1,
    //       "cal_b": 0
    //     },
    //     ...
    //   ]
    // }
    struct RayAuxConfig
    {
        int index = 0;
        QString name;

        // Firmware "type" string: "none", "voltage", "current", "temperature"
        QString typeName;

        // Firmware "tempModel" string: e.g. "linear", "thermistor"
        QString tempModel;

        // Linear engineering ranges for 0..ADC_VREF
        double voltageMaxRange = 0.0;
        double currentMaxRange = 0.0;
        double temperatureMaxRange = 0.0;

        // Thermistor (Beta model) parameters
        double thermSeriesOhms = 0.0;
        double thermR0Ohms = 0.0;
        double thermBeta = 0.0;
        double thermT0C = 0.0;

        // Simple linear calibration y = m*x + b in engineering units
        double calM = 1.0;
        double calB = 0.0;
    };

} // namespace raypm
