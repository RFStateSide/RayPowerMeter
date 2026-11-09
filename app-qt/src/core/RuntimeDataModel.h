// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QObject>
#include "core/RayCouplerConfig.h"
#include "core/RayTelemetry.h"

namespace raypm {

// Phase 1: simple holder for latest telemetry + coupler config.
// Later we can extend this with more derived values and per-coupler structures.
class RuntimeDataModel : public QObject
{
    Q_OBJECT
public:
    explicit RuntimeDataModel(QObject *parent = nullptr);

    RayTelemetry telemetry() const;
    RayCouplerConfig couplerConfig() const;
    bool hasCouplerConfig() const;

public slots:
    void setTelemetry(const RayTelemetry &t);
    void setCouplerConfig(const RayCouplerConfig &cfg);

signals:
    void telemetryChanged(const RayTelemetry &t);
    void couplerConfigChanged(const RayCouplerConfig &cfg);

private:
    RayTelemetry m_lastTelemetry{};
    RayCouplerConfig m_lastCouplerConfig{};
    bool m_hasCouplerConfig = false;
};

} // namespace raypm
