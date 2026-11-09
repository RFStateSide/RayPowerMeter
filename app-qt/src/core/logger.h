// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#ifndef LOGGER_H
#define LOGGER_H

#include <QDateTime>
#include <QFile>
#include <QMessageLogContext>
#include <QMutex>
#include <QTextStream>

class Logger
{
public:
    static void init(const QString &logFilePath);
    static void shutdown();
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &context,
                               const QString &msg);

private:
    static QFile logFile;
    static QMutex logMutex;
};

#endif // LOGGER_H
