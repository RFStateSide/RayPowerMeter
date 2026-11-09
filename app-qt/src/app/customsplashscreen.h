// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

// Original engineering authorship: Ray Faucher (KB1SO).

#ifndef CUSTOMSPLASHSCREEN_H
#define CUSTOMSPLASHSCREEN_H

#include <QSplashScreen>
#include <QPixmap>

class CustomSplashScreen : public QSplashScreen
{
    Q_OBJECT

public:
    explicit CustomSplashScreen(const QPixmap &pixmap, QWidget *parent = nullptr);
    void setProgress(int value);
    void setLoadingMessage(const QString &message);
    void setLoadingMessage(const QString &message, int progressValue);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int progress;
    QString currentMessage;
};

#endif // CUSTOMSPLASHSCREEN_H
