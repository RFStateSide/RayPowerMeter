// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2024 RF StateSide LLC (RayVessel).
// Original RayVessel engineering by Ray Faucher (KB1SO), RF StateSide LLC.
// Copyright (c) 2025 Ray Faucher (KB1SO) (MercuryLuxPc).
// Copyright (c) 2026 RF StateSide LLC (RayPowerMeter).
// SPDX-License-Identifier: LGPL-3.0-only
// Adapted from MercuryLuxPc, with earlier RayVessel code. See docs/licensing.md.

#include "dialogviewlogfile.h"
#include <QStandardPaths>
#include "../ui_dialogviewlogfile.h"
#include "core/logger.h"

void DialogViewLogFile::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event); // Call base class
    loadLogFile();

    if (!updateTimer) {
        updateTimer = new QTimer(this);
        connect(updateTimer, &QTimer::timeout, this, &DialogViewLogFile::loadLogFile);
        updateTimer->start(1000); // every 1 second
    }
}

void DialogViewLogFile::closeEvent(QCloseEvent *event)
{
    if (updateTimer) {
        updateTimer->stop();
        updateTimer->deleteLater();
        updateTimer = nullptr;
    }
    QDialog::closeEvent(event);
}

DialogViewLogFile::DialogViewLogFile(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogViewLogFile)
{
    ui->setupUi(this);

    // Set logFilePath to the standard application data location
    logFilePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/RayPowerMeter.log";
}

DialogViewLogFile::~DialogViewLogFile()
{
    delete ui;
}

void DialogViewLogFile::loadLogFile()
{
    QFile logFile(logFilePath);
    if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&logFile);
        QString currentText = in.readAll();
        logFile.close();

        if (currentText != lastLogText) {
            lastLogText = currentText;
            ui->textEditLogViewer->setPlainText(currentText);
            ui->textEditLogViewer->moveCursor(QTextCursor::End);
        }
    } else {
        qDebug() << "Log file path:" << logFilePath;
        QMessageBox::warning(this, "Error", "Failed to open log file.");
        qWarning() << "Failed to open log file:" << logFile.errorString();
    }
}

void DialogViewLogFile::clearLogFile()
{
    QMessageBox::StandardButton reply
        = QMessageBox::question(this,
                                "Clear Log File",
                                "Are you sure you want to clear the log file?",
                                QMessageBox::Yes | QMessageBox::No,
                                QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        // Close the logger first to avoid write issues
        Logger::shutdown();

        QFile logFile(logFilePath);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            logFile.close();
            ui->textEditLogViewer->clear();
            qInfo() << "Log file cleared";
        } else {
            QMessageBox::warning(this, "Error", "Failed to clear log file.");
            qWarning() << "Failed to clear log file";
        }

        // Reopen the logger after clearing
        Logger::init(logFilePath);
    } else {
        qInfo() << "Log file clear cancelled by user";
    }
}

void DialogViewLogFile::on_pushButtonClear_clicked()
{
    clearLogFile();
}


void DialogViewLogFile::on_pushButtonClose_clicked()
{
    this->close();
}

void DialogViewLogFile::on_pushButtonPauseLog_clicked(bool checked)
{
    if (checked) {
        if (updateTimer) {
            updateTimer->stop();
            ui->pushButtonPauseLog->setText("Resume Log Updates");
        }
    } else {
        if (updateTimer) {
            updateTimer->start(1000);
        }
        ui->pushButtonPauseLog->setText("Pause Log Updates");
    }
}
