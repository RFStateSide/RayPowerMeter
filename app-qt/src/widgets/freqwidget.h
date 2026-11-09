// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include "widgetbase.h"

class QLabel;
class QVBoxLayout;
class QHBoxLayout;

namespace raypm {

/**
 * Simple RF frequency display widget.
 *
 * - Shows live frequency in MHz
 * - Tracks the last valid frequency even if RF drops out
 * - Uses WidgetBase::label as the title (e.g. "Frequency")
 */
class FreqWidget : public WidgetBase
{
    Q_OBJECT
    Q_PROPERTY(bool useHamlib READ useHamlib WRITE setUseHamlib NOTIFY useHamlibChanged)
    Q_PROPERTY(bool showHertz READ showHertz WRITE setShowHertz NOTIFY showHertzChanged)

public:
    enum class SourceType { RF, HAMLIB };

    explicit FreqWidget(QWidget *parent = nullptr);

    double frequencyHz() const { return m_frequencyHz; }
    double lastValidFrequencyHz() const { return m_lastValidFrequencyHz; }
    bool frequencyValid() const { return m_frequencyValid; }

    bool useHamlib() const;
    void setUseHamlib(bool newUseHamlib);

    bool showHertz() const;
    void setShowHertz(bool newShowHertz);

public slots:
    // Convenience: assumes valid = true
    void setFrequencyHz(double hz);

    // Full update: value + validity
    void setFrequency(double hz, bool valid, SourceType sourceType);

    void setFrequencyValid(bool valid);

signals:
    void useHamlibChanged();

    void showHertzChanged();

protected:
    // Called by WidgetBase when appScaleFactor changes
    void onAppScaleFactorChanged(double scale) override;

private slots:
    // Keep title in sync with WidgetBase::label
    void onLabelChanged(const QString &label);

private:
    void updateTitleFromLabel();
    void updateDisplay();

    QLabel *m_titleLabel = nullptr;
    QLabel *m_valueLabel = nullptr;
    QLabel *m_valueHertzLabel = nullptr;
    QLabel *m_unitsLabel = nullptr;
    QLabel *m_lastLabel = nullptr;

    QFont m_baseCaptionFont;
    QFont m_baseValueFont;
    bool m_scaleInitialized = false;

    double m_frequencyHz = 0.0;
    double m_lastValidFrequencyHz = 0.0;
    bool m_frequencyValid = false;
    bool m_useHamlib = false;
    SourceType m_sourceType = SourceType::RF;
    bool m_showHertz = true;
};

} // namespace raypm
