// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once
#include "widgetbase.h"

#include <QSize>

class SpacerWidget : public WidgetBase
{
    Q_OBJECT
public:
    explicit SpacerWidget(QWidget *parent = nullptr);

    void setSpacerMinHeightPx(int px);
    int spacerMinHeightPx() const { return m_minHeightPx; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    void applyProperties(const QJsonObject &props);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_minHeightPx = 0;
};
