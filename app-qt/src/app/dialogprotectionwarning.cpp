// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "dialogprotectionwarning.h"
#include "ui_dialogprotectionwarning.h"

#include <QCheckBox>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>

namespace raypm {

DialogProtectionWarning::DialogProtectionWarning(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogProtectionWarning)
{
    ui->setupUi(this);

    // Standard dialog flags (no "?" help button)
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    // Wire buttons
    connect(ui->buttonReset, &QPushButton::clicked, this, &DialogProtectionWarning::onResetClicked);
    connect(ui->buttonClose, &QPushButton::clicked, this, &DialogProtectionWarning::onCloseClicked);

    // Sensible defaults
    ui->labelTitle->setText(tr("Protection Trip Detected"));
    ui->labelSummary->setText(
        tr("RayPowerMeter has detected one or more active protection trips.\n"
           "Please verify your load, cabling, and drive level before resetting."));

    ui->plainTextDetails->setReadOnly(true);
    ui->checkBoxShowAgain->setChecked(true);

    applyTheme();
}

DialogProtectionWarning::~DialogProtectionWarning()
{
    delete ui;
}

void DialogProtectionWarning::setIsDarkTheme(bool dark)
{
    if (m_isDarkTheme == dark)
        return;

    m_isDarkTheme = dark;
    applyTheme();
}

void DialogProtectionWarning::setSummaryText(const QString &text)
{
    ui->labelSummary->setText(text);
}

void DialogProtectionWarning::setDetailsText(const QString &text)
{
    ui->plainTextDetails->setPlainText(text);
}

bool DialogProtectionWarning::shouldShowOnNextTrip() const
{
    return ui->checkBoxShowAgain->isChecked();
}

void DialogProtectionWarning::onResetClicked()
{
    emit resetProtectionRequested();
    // Dialog stays open until MainWindow sees that protection is cleared
}

void DialogProtectionWarning::onCloseClicked()
{
    reject();
}

void DialogProtectionWarning::applyTheme()
{
    QPalette pal = palette();

    if (m_isDarkTheme) {
        pal.setColor(QPalette::Window, QColor(35, 35, 35));
        pal.setColor(QPalette::Base, QColor(30, 30, 30));
        pal.setColor(QPalette::WindowText, Qt::white);
        pal.setColor(QPalette::Text, Qt::white);
    } else {
        pal.setColor(QPalette::Window, Qt::white);
        pal.setColor(QPalette::Base, Qt::white);
        pal.setColor(QPalette::WindowText, Qt::black);
        pal.setColor(QPalette::Text, Qt::black);
    }

    setPalette(pal);
    ui->plainTextDetails->setPalette(pal);
}

void DialogProtectionWarning::autoCloseIfSafe(bool protectionActive)
{
    // If protection is no longer active, close the dialog.
    // Use hide() so we can show() it again on the next trip.
    if (!protectionActive) {
        hide();
    }
}

} // namespace raypm
