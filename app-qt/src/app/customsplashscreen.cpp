// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "customsplashscreen.h"
#include <QPainter>

CustomSplashScreen::CustomSplashScreen(const QPixmap &pixmap, QWidget *parent)
    : QSplashScreen(pixmap, Qt::WindowStaysOnTopHint), progress(0)
{
    setParent(parent);
}

void CustomSplashScreen::setProgress(int value)
{
    progress = value;
    repaint();
}

void CustomSplashScreen::setLoadingMessage(const QString &message)
{
    currentMessage = message;
    showMessage(currentMessage, Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
}

void CustomSplashScreen::setLoadingMessage(const QString &message, int progressValue)
{
    setLoadingMessage(message);
    setProgress(progressValue);
}

void CustomSplashScreen::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    // Draw the background
    //painter.setBrush(QColor(255, 255, 255, 200));  // Light semi-transparent background
    painter.setBrush(QColor(243, 243, 243));  // White background
    painter.setPen(Qt::NoPen);
    painter.drawRect(rect());

    // Draw a thin blue border
    painter.setBrush(Qt::NoBrush);  // No brush for drawing border, just outline
    painter.setPen(QPen(Qt::blue, 1));  // Thin blue pen with width 1
    painter.drawRect(rect().adjusted(0, 0, -1, -1));  // Adjust to ensure border fits inside


    // Draw the pixmap
    painter.drawPixmap(0, 0, pixmap());

    // Calculate margin sizes
    int topMargin = 5;     // Margin at the top of the splash screen
    int bottomMargin = 10; // Margin at the bottom of the splash screen

    // Set the pen and font for the title text
    //painter.setPen(Qt::blue);  // Use blue or another color for text
    QColor softCyan(0x6F, 0xD9, 0xFF);
    painter.setPen(softCyan);

    // Set a bold font for the title
    QFont titleFont("Arial", 24, QFont::Bold);  // Set font size, type, and weight for the title
    painter.setFont(titleFont);

    // Draw the title text at the top with margin
    QRect titleRect(0, topMargin, width(), 40); // Position title with top margin
    painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignTop, "RayPowerMeter");

    // Draw the progress bar with top margin
    int barWidth = width() * 0.8;  // 80% of the splash width
    int barHeight = 10;
    int x = (width() - barWidth) / 2;
    int y = height() - bottomMargin - barHeight - 25; // Position the progress bar with margin

    QRect progressRect(x, y, barWidth, barHeight);
    painter.setBrush(Qt::gray);
    painter.drawRect(progressRect);

    QRect fillRect(x, y, barWidth * progress / 100, barHeight);
    painter.setBrush(Qt::green);
    painter.drawRect(fillRect);

    // Set a font and pen for the loading message
    QFont messageFont("Arial", 12, QFont::Normal);  // Font size and weight for the message
    painter.setFont(messageFont);
    painter.setPen(Qt::white);

    // Draw the loading message with bottom margin
    QRect textRect(0, y + barHeight + 10, width(), 30); // Position text below the progress bar with a small gap
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, currentMessage);
}
