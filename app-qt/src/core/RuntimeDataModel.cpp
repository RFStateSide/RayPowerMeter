// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/RuntimeDataModel.h"

namespace raypm {

RuntimeDataModel::RuntimeDataModel(QObject *parent)
    : QObject(parent)
{}

RayTelemetry RuntimeDataModel::telemetry() const
{
    return m_lastTelemetry;
}

RayCouplerConfig RuntimeDataModel::couplerConfig() const
{
    return m_lastCouplerConfig;
}

bool RuntimeDataModel::hasCouplerConfig() const
{
    return m_hasCouplerConfig;
}

void RuntimeDataModel::setTelemetry(const RayTelemetry &t)
{
    // For Phase 1 we keep it simple and always emit.
    // If needed later we can add a "changed" comparison.
    m_lastTelemetry = t;
    emit telemetryChanged(m_lastTelemetry);
}

void RuntimeDataModel::setCouplerConfig(const RayCouplerConfig &cfg)
{
    m_lastCouplerConfig = cfg;
    m_hasCouplerConfig = true;
    emit couplerConfigChanged(m_lastCouplerConfig);
}

} // namespace raypm
