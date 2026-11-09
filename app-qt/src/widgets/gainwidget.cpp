// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "gainwidget.h"
#include "common//Constants.h"

#include <QFont>
#include <QtMath>

namespace raypm {

GainWidget::GainWidget(QWidget *parent)
    : WidgetBase(parent)
{
    setObjectName(QStringLiteral("GainWidget"));

    setAutoFillBackground(false);
    // Default logical label; this also fires labelChanged
    setLabel(tr("Gain"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 1, 4, 1);
    layout->setSpacing(4);

    // Caption label on the left
    m_captionLabel = new QLabel(this);
    //QFont captionFont = m_captionLabel->font();
    //captionFont.setBold(true);
    //m_captionLabel->setFont(captionFont);
    m_captionLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_captionLabel, 0);

    // Value label on the right
    m_valueLabel = new QLabel(this);
    QFont valueFont = m_valueLabel->font();
    valueFont.setPointSize(valueFont.pointSize() + 1);
    valueFont.setBold(true);
    m_valueLabel->setFont(valueFont);
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    //m_valueLabel->setMinimumWidth(80);
    layout->addWidget(m_valueLabel, 0);

    m_unitsLabel = new QLabel(tr("dB"), this);
    m_unitsLabel->setFont(valueFont);
    m_unitsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_unitsLabel, 0);

    m_baseCaptionFont = m_captionLabel->font();
    m_baseValueFont = m_valueLabel->font();
    m_scaleInitialized = true;

    setLayout(layout);

    // Keep caption in sync with WidgetBase::label property
    connect(this, &WidgetBase::labelChanged, m_captionLabel, &QLabel::setText);
    // Initialize caption text
    m_captionLabel->setText(label());

    // Initialize displayed gain
    setGainDb(0.0);
}

void GainWidget::onAppScaleFactorChanged(double scale)
{
    if (!m_scaleInitialized)
        return;

    if (scale <= 0.0)
        scale = 1.0;

    auto scaleFont = [scale](const QFont &base) {
        QFont f = base;
        if (base.pointSizeF() > 0)
            f.setPointSizeF(std::max(MIN_UI_FONT_PT, base.pointSizeF() * scale));
        else if (base.pixelSize() > 0)
            f.setPixelSize(std::max(MIN_UI_FONT_PX, base.pixelSize() * scale));
        return f;
    };

    m_captionLabel->setFont(scaleFont(m_baseCaptionFont));
    m_valueLabel->setFont(scaleFont(m_baseValueFont));
}

void GainWidget::setRfInSource(const QString &src)
{
    if (m_rfInSource == src)
        return;
    m_rfInSource = src;
    // No direct UI impact; used by telemetry routing
}

void GainWidget::setRfOutSource(const QString &src)
{
    if (m_rfOutSource == src)
        return;
    m_rfOutSource = src;

    // Keep base "source" in sync with RF out so existing
    // widgetsForSource() lookup still works.
    setSource(src);
}

void GainWidget::setGainFromWatts(double inWatts, double outWatts)
{
    m_inWatts = inWatts;
    m_outWatts = outWatts;
    const bool haveSignal = (inWatts > 0.1 && outWatts > 0.5);

    if (haveSignal) {
        m_rawGainDb = 10.0 * qLn(outWatts / inWatts) / qLn(10.0);
        setGainDb(m_rawGainDb + m_attnDb);
    } else {
        // No meaningful RF input/output -> show 0.0 dB (don't display attenuation alone)
        m_rawGainDb = 0.0;
        setGainDb(0.0);
    }
}

void GainWidget::setGainDb(double db)
{
    if (qFuzzyCompare(1.0 + m_gainDb, 1.0 + db))
        return;

    m_gainDb = db;

    if (m_valueLabel)
        m_valueLabel->setText(tr("%1").arg(db, 0, 'f', 1));

    if (m_unitsLabel)
        m_unitsLabel->setText(tr("dB"));

    emit gainDbChanged(m_gainDb);
}

double GainWidget::attnDb() const
{
    return m_attnDb;
}

void GainWidget::setAttnDb(double newAttnDb)
{
    if (qFuzzyCompare(1.0 + m_attnDb, 1.0 + newAttnDb))
        return;

    m_attnDb = newAttnDb;

    // Only apply attenuation when we have meaningful signal (same thresholds as above)
    const bool haveSignal = (m_inWatts > 0.1 && m_outWatts > 0.5); // see note below
    if (haveSignal)
        setGainDb(m_rawGainDb + m_attnDb);
    else
        setGainDb(0.0);

    emit attnDbChanged();
}

} // namespace raypm
