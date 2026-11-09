// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#ifndef DIALOGABOUT_H
#define DIALOGABOUT_H

#include <QDialog>
#include <QMap>

QT_BEGIN_NAMESPACE
namespace Ui {
class DialogAbout;
}
QT_END_NAMESPACE

class DialogAbout : public QDialog
{
    Q_OBJECT

public:
    enum class AboutSection {
        LOGO,
        TITLE,
        VERSION,
        REVISION,
        DATE,
        SHORT_INFO,
        DESCRIPTION,
        RESOURCES,
        CREDITS,
        AUTHOR,
        LICENCE
    };

    explicit DialogAbout(QWidget *parent = nullptr);
    ~DialogAbout();

    void populate(const QMap<AboutSection, QString> &contents);

private:
    Ui::DialogAbout *ui;
};

#endif // DIALOGABOUT_H
