// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "spacerwidget.h"
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <qjsonobject.h>

SpacerWidget::SpacerWidget(QWidget *parent)
    : WidgetBase(parent)
{
    // We want this to fill whatever space the grid cell gives it,
    // but still be able to enforce a minimum height.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}

void SpacerWidget::setSpacerMinHeightPx(int px)
{
    px = qMax(0, px);
    if (m_minHeightPx == px)
        return;

    m_minHeightPx = px;

    // This affects layout calculations
    setMinimumHeight(m_minHeightPx);

    // Critical: tell layouts our geometry hints changed
    updateGeometry();

    // Visual refresh (for design mode outline/text)
    update();
}

QSize SpacerWidget::sizeHint() const
{
    // A small width; height influenced by minHeight
    return QSize(20, qMax(0, m_minHeightPx));
}

QSize SpacerWidget::minimumSizeHint() const
{
    return QSize(10, qMax(0, m_minHeightPx));
}

void SpacerWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const bool designMode = property("raypmDesignMode").toBool();
    if (!designMode)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Dashed outline so user can see the spacer in design mode
    QPen pen(QColor("#00ffff"));
    pen.setStyle(Qt::DashLine);
    pen.setWidth(1);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const QRect r = rect().adjusted(1, 1, -2, -2);
    p.drawRoundedRect(r, 4, 4);

    // Centered label
    p.setPen(QColor("#cfcfcf"));
    p.drawText(r, Qt::AlignCenter, tr("Spacer"));
}

void SpacerWidget::applyProperties(const QJsonObject &props)
{
    if (props.contains("minHeight")) {
        const int h = props.value("minHeight").toInt(0);
        setMinimumHeight(h);

        qDebug() << "[SpacerWidget] setMinimumHeight =" << h;
    }

    updateGeometry();
}
