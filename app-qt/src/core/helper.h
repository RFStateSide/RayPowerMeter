// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

/****************************************************************************
 * RayPowerMeter
 * Helper utilities (theme, scaling, serial ports, basic conversions)
 ****************************************************************************/

#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class Helper
{
public:
    Helper();

    static void CenterWidgetOnVisibleScreen(QWidget *widget);
    static bool IsWidgetGeometryVisible(QWidget *widget);
    static QList<QString> GetCommPorts();

    // Handy conversions – you’ll very likely use these later
    static double CelsiusToFahrenheit(double tempC);
    static double WattsToDBm(double powerW);
    static double SWRToReturnLoss(double swr);

    // Theme + style
    static void ApplyAppTheme(bool isDarkTheme);
    static QString GlobalStyleSheet(bool isDarkTheme);

    // Global scaling for fonts, margins, spacing
    static void ApplyUserScaleFactor(QWidget *widget, double scaleFactor);
};
