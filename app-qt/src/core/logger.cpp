// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "logger.h"

QFile Logger::logFile;
QMutex Logger::logMutex;

void Logger::init(const QString &logFilePath)
{
    if (logFile.isOpen()) {
        logFile.close(); // Close before setting new file name
    }

    // If file not found, create file
    if (!QFile::exists(logFilePath)) {
        QFile file(logFilePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            fprintf(stderr, "Logger: Failed to create log file\n");
            return;
        }
        file.close();
    }

    logFile.setFileName(logFilePath);
    qInstallMessageHandler(Logger::messageHandler);
}

void Logger::shutdown()
{
    if (logFile.isOpen())
        logFile.close();
}

void Logger::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (msg.contains("iCCP: known incorrect sRGB profile") || msg.contains("ERROR:gbm_wrapper.cc")
        || msg.contains("ERROR:v4l2_utils.cc")) {
        return;
    }

    Q_UNUSED(context);
    QMutexLocker locker(&logMutex);

    const qint64 maxLogSize = 1024 * 1024; // 1 MB

    // Open file if not already open
    if (!logFile.isOpen()) {
        if (!logFile.open(QIODevice::Append | QIODevice::Text)) {
            fprintf(stderr, "Logger: Failed to open log file for appending\n");

            return;
        }
        if (!logFile.isOpen()) {
            fprintf(stderr, "Logger: Failed to open log file\n");
            return;
        }
    }

    // Check size and trim if necessary
    if (logFile.size() > maxLogSize) {
        logFile.seek(0);
        QByteArray logData = logFile.readAll();
        QList<QByteArray> lines = logData.split('\n');

        // Keep last 70% of lines
        int keepLines = lines.size() * 0.7;
        QByteArray trimmed;
        for (int i = lines.size() - keepLines; i < lines.size(); ++i) {
            if (!lines[i].isEmpty())
                trimmed.append(lines[i] + '\n');
        }

        // Truncate and rewrite trimmed content
        logFile.close();
        if (!logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            fprintf(stderr, "Logger: Failed to open log file for truncation\n");
            return;
        }
        if (logFile.isOpen()) {
            logFile.write(trimmed);
            logFile.flush();
        }
        logFile.close();
        if (!logFile.open(QIODevice::Append | QIODevice::Text)) {
            fprintf(stderr, "Logger: Failed to reopen log file after trimming\n");
            return;
        }
    }

    // Compose message
    QTextStream out(&logFile);
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QString message;

    switch (type) {
    case QtDebugMsg:
        message = QString("[%1] Debug: %2").arg(timestamp, msg);
        break;
    case QtInfoMsg:
        message = QString("[%1] Info: %2").arg(timestamp, msg);
        break;
    case QtWarningMsg:
        message = QString("[%1] Warning: %2").arg(timestamp, msg);
        break;
    case QtCriticalMsg:
        message = QString("[%1] Critical: %2").arg(timestamp, msg);
        break;
    case QtFatalMsg:
        message = QString("[%1] Fatal: %2").arg(timestamp, msg);
        break;
    }

    out << message << "\n";
    out.flush();

#ifdef QT_DEBUG
    fprintf(stderr, "%s\n", qUtf8Printable(message));
    fflush(stderr);
#endif
}
