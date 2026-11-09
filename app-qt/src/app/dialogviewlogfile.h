// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2024 RF StateSide LLC (RayVessel).
// Original RayVessel engineering by Ray Faucher (KB1SO), RF StateSide LLC.
// Copyright (c) 2025 Ray Faucher (KB1SO) (MercuryLuxPc).
// Copyright (c) 2026 RF StateSide LLC (RayPowerMeter).
// SPDX-License-Identifier: LGPL-3.0-only
// Derived from MercuryLuxPc, with earlier RayVessel code. See docs/licensing.md.

#ifndef DIALOGVIEWLOGFILE_H
#define DIALOGVIEWLOGFILE_H

#include <QDebug>
#include <QDialog>
#include <QFile>
#include <QMessageBox>
#include <QShowEvent>
#include <QTextStream>
#include <QTimer>

namespace Ui {
class DialogViewLogFile;
}

class DialogViewLogFile : public QDialog
{
    Q_OBJECT

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

public:
    explicit DialogViewLogFile(QWidget *parent = nullptr);
    ~DialogViewLogFile();

private slots:
    void on_pushButtonClear_clicked();

    void on_pushButtonClose_clicked();

    void on_pushButtonPauseLog_clicked(bool checked);

private:
    Ui::DialogViewLogFile *ui;
    QTimer *updateTimer = nullptr;
    QString lastLogText;

    void loadLogFile();
    void clearLogFile();

    QString logFilePath = "MercuryLuxPc.log";
};

#endif // DIALOGVIEWLOGFILE_H
