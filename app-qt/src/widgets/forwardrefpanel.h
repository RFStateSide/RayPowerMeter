// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QFont>
#include "widgetbase.h"

class QLabel;
class QGridLayout;

namespace raypm {

class ForwardRefPanel : public WidgetBase
{
    Q_OBJECT
public:
    explicit ForwardRefPanel(QWidget *parent = nullptr);

    int layoutRowSpan() const override { return 1; }
    int layoutColSpan() const override { return 1; }

    void onAppScaleFactorChanged(double scale) override;

public slots:
    void setValues(double forwardW, double reflectedW, double swr);
    void setScaleFactor(double scale);

protected:
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    QLabel *m_lblForwardValue = nullptr;
    QLabel *m_lblRefValue = nullptr;
    QLabel *m_lblSwrValue = nullptr;

    QLabel *m_lblForwardLabel = nullptr;
    QLabel *m_lblRefLabel = nullptr;
    QLabel *m_lblSwrLabel = nullptr;

    QFont m_baseLabelFont;
    QFont m_baseValueFont;

    int compactHeightForFonts(const QFont &labelFont, const QFont &valueFont) const;
};

} // namespace raypm
