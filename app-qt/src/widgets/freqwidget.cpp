// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "freqwidget.h"

#include "common/Constants.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace raypm {

FreqWidget::FreqWidget(QWidget *parent)
    : WidgetBase(parent)
{
    setObjectName(QStringLiteral("FreqWidget"));
    setAutoFillBackground(false);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(4, 1, 4, 1);
    rootLayout->setSpacing(2);

    // ---- Top row: label + (value + Hz tail + units) on one line ----------
    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(4);

    // Title / caption label ("Freq (RF)" or "Freq (RIG)")
    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    {
        QFont f = m_titleLabel->font();
        f.setPointSize(f.pointSize() + 1);
        m_titleLabel->setFont(f);
    }

    m_valueLabel = new QLabel(this);
    {
        QFont f = m_valueLabel->font();
        f.setPointSize(f.pointSize() + 1);
        f.setBold(true);
        m_valueLabel->setFont(f);
    }
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Small Hz (last 3 digits) label – a bit smaller and not bold
    m_valueHertzLabel = new QLabel(this);
    {
        QFont f = m_valueLabel->font();
        f.setBold(false);
        if (f.pointSize() > 0) {
            f.setPointSize(f.pointSize() - 4);
        } else if (f.pixelSize() > 0) {
            f.setPixelSize(qMax(1, f.pixelSize() - 4));
        }
        m_valueHertzLabel->setFont(f);
    }
    m_valueHertzLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_unitsLabel = new QLabel(this);
    {
        QFont f = m_unitsLabel->font();
        f.setPointSize(f.pointSize() + 1);
        f.setBold(true);
        m_unitsLabel->setFont(f);
    }
    m_unitsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Tight layout for value + Hz tail + units
    auto *valueRow = new QHBoxLayout();
    valueRow->setContentsMargins(0, 0, 0, 0);
    valueRow->setSpacing(2); // small gap between 28.500 | 015 | MHz
    valueRow->addWidget(m_valueLabel);
    valueRow->addWidget(m_valueHertzLabel);
    valueRow->addWidget(m_unitsLabel);

    topRow->addWidget(m_titleLabel);
    topRow->addStretch(1); // eats all extra space
    topRow->addLayout(valueRow);
    topRow->addStretch(1); // eats all extra space

    rootLayout->addLayout(topRow);

    // ---- Second row: optional "Last: xx.xxx MHz" -------------------------
    m_lastLabel = new QLabel(this);
    m_lastLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont lastFont = m_lastLabel->font();
    lastFont.setPointSize(lastFont.pointSize() - 1);
    lastFont.setItalic(true);
    m_lastLabel->setFont(lastFont);
    rootLayout->addWidget(m_lastLabel);

    // Capture base fonts for scaling
    m_baseCaptionFont = m_titleLabel->font();
    m_baseValueFont = m_valueLabel->font();
    m_scaleInitialized = true;

    setFrequency(0.0, false, m_sourceType);

    connect(this, &WidgetBase::labelChanged, this, &FreqWidget::onLabelChanged);

    updateTitleFromLabel();

    if (m_valueHertzLabel)
        m_valueHertzLabel->setVisible(m_showHertz);
}

void FreqWidget::setFrequencyHz(double hz)
{
    setFrequency(hz, true, SourceType::RF);
}

void FreqWidget::setFrequency(double hz, bool valid, SourceType sourceType = SourceType::RF)
{
    m_frequencyHz = hz;
    m_frequencyValid = valid;
    m_sourceType = sourceType;

    if (valid && hz > 1.0) {
        m_lastValidFrequencyHz = hz;
    }

    if (DEBUG_HAMLIB_FREQ) {
        qDebug() << "[FreqWidget]" << objectName() << "setFrequency hz=" << hz << "valid=" << valid
                 << "source=" << (sourceType == SourceType::HAMLIB ? "HAMLIB" : "RF");
    }

    updateDisplay();
}

void FreqWidget::setFrequencyValid(bool valid)
{
    m_frequencyValid = valid;
    if (!valid) {
        // keep lastValidFrequencyHz as-is
    }
    updateDisplay();
}

void FreqWidget::onAppScaleFactorChanged(double scale)
{
    if (!m_scaleInitialized)
        return;

    if (scale <= 0.0)
        scale = 1.0;

    auto scaleFont = [scale](const QFont &base) {
        QFont f = base;
        if (base.pointSizeF() > 0) {
            f.setPointSizeF(std::max(MIN_UI_FONT_PT, base.pointSizeF() * scale));
        } else if (base.pixelSize() > 0) {
            f.setPixelSize(std::max(MIN_UI_FONT_PX, base.pixelSize() * scale));
        }
        return f;
    };

    m_titleLabel->setFont(scaleFont(m_baseCaptionFont));

    // Main MHz value
    m_valueLabel->setFont(scaleFont(m_baseValueFont));

    // Smaller Hz tail: slightly reduced, non-bold version of base value font
    if (m_valueHertzLabel) {
        QFont hertzBase = m_baseValueFont;
        hertzBase.setBold(false);
        if (hertzBase.pointSize() > 0) {
            hertzBase.setPointSize(hertzBase.pointSize() - 2);
        } else if (hertzBase.pixelSize() > 0) {
            hertzBase.setPixelSize(hertzBase.pixelSize() - 2);
        }
        m_valueHertzLabel->setFont(scaleFont(hertzBase));
    }
}

void FreqWidget::onLabelChanged(const QString &)
{
    updateTitleFromLabel();
}

void FreqWidget::updateTitleFromLabel()
{
    if (!m_titleLabel)
        return;

    QString text = label();
    if (text.isEmpty())
        text = tr("Freq");

    if (m_sourceType == SourceType::HAMLIB) {
        text += QStringLiteral(" (%1)").arg(tr("RIG"));
    } else {
        text += QStringLiteral(" (%1)").arg(tr("RF"));
    }

    m_titleLabel->setText(text);
}

void FreqWidget::updateDisplay()
{
    auto updateHzTail = [this](qint64 hzInt) {
        if (!m_valueHertzLabel)
            return;

        if (!m_showHertz) {
            m_valueHertzLabel->clear();
            m_valueHertzLabel->setVisible(false);
            return;
        }

        hzInt = std::llabs(hzInt);
        const int tail = static_cast<int>(hzInt % 1000); // last 3 digits

        m_valueHertzLabel->setText(QString::number(tail).rightJustified(3, QLatin1Char('0')));

        m_valueHertzLabel->setVisible(true);
    };

    // 1) Live valid frequency?
    if (m_frequencyValid && m_frequencyHz > 1.0) {
        const qint64 hzInt = qRound64(m_frequencyHz);
        const double mhz = static_cast<double>(hzInt) / 1e6;

        m_valueLabel->setText(QString::number(mhz, 'f', 3));
        m_unitsLabel->setText(tr("MHz"));
        m_valueLabel->setEnabled(true);
        m_unitsLabel->setEnabled(true);

        updateHzTail(hzInt);

        // When live is valid, do NOT show "Last"
        m_lastLabel->clear();
        return;
    }

    // No live value – clear Hz tail
    if (m_valueHertzLabel) {
        m_valueHertzLabel->clear();
        m_valueHertzLabel->setVisible(false);
    }

    // 2) No live freq, but we have a last valid one?
    if (m_lastValidFrequencyHz > 1.0) {
        const double lastMhz = m_lastValidFrequencyHz / 1e6;

        // Main line goes to "---" and is greyed out
        m_valueLabel->setText(QStringLiteral("---"));
        m_unitsLabel->clear();
        m_valueLabel->setEnabled(false);
        m_unitsLabel->setEnabled(false);

        // Show last value on the small line only
        m_lastLabel->setText(tr("Last: %1 MHz").arg(lastMhz, 0, 'f', 3));
        return;
    }

    // 3) No live, no last: empty state
    m_valueLabel->setText(QStringLiteral("---"));
    m_unitsLabel->clear();
    m_valueLabel->setEnabled(false);
    m_unitsLabel->setEnabled(false);
    m_lastLabel->clear();
}

bool FreqWidget::useHamlib() const
{
    return m_useHamlib;
}

void FreqWidget::setUseHamlib(bool newUseHamlib)
{
    if (m_useHamlib == newUseHamlib)
        return;

    m_useHamlib = newUseHamlib;

    // When Hamlib is enabled/disabled, treat it as the authoritative source
    // for label suffix and live value.
    m_sourceType = m_useHamlib ? SourceType::HAMLIB : SourceType::RF;

    // Refresh title text and displayed value immediately so the UI reflects
    // the new source before the next update arrives.
    updateTitleFromLabel();
    updateDisplay();

    emit useHamlibChanged();
}

bool FreqWidget::showHertz() const
{
    return m_showHertz;
}

void FreqWidget::setShowHertz(bool newShowHertz)
{
    if (m_showHertz == newShowHertz)
        return;
    m_showHertz = newShowHertz;

    updateDisplay();
    emit showHertzChanged();
}

} // namespace raypm
