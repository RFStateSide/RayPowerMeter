// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QHBoxLayout>
#include <QLabel>
#include "widgetbase.h"

namespace raypm {

class GainWidget : public WidgetBase
{
    Q_OBJECT
    Q_PROPERTY(double gainDb READ gainDb WRITE setGainDb NOTIFY gainDbChanged)
    Q_PROPERTY(double attnDb READ attnDb WRITE setAttnDb NOTIFY attnDbChanged)

public:
    explicit GainWidget(QWidget *parent = nullptr);

    int layoutRowSpan() const override { return 1; }
    int layoutColSpan() const override { return 1; }

    void onAppScaleFactorChanged(double scale) override;

    // Primary / secondary sources
    QString rfInSource() const { return m_rfInSource; }
    QString rfOutSource() const { return m_rfOutSource; }

    void setRfInSource(const QString &src);
    void setRfOutSource(const QString &src);

    // Convenience: compute and set from watts
    void setGainFromWatts(double inWatts, double outWatts);

    double gainDb() const { return m_gainDb; }
    void setGainDb(double db);

    double attnDb() const;
    void setAttnDb(double newAttnDb);

signals:
    void gainDbChanged(double db);

    void attnDbChanged();

private:
    QLabel *m_captionLabel = nullptr;
    QLabel *m_valueLabel = nullptr;
    QLabel *m_unitsLabel = nullptr;

    QString m_rfInSource;
    QString m_rfOutSource;
    double m_gainDb = 0.0;
    double m_rawGainDb = 0.0;
    double m_attnDb = 0.0;
    double m_inWatts = 0.0;
    double m_outWatts = 0.0;

    QFont m_baseCaptionFont;
    QFont m_baseValueFont;
    bool m_scaleInitialized = false;
};

} // namespace raypm
