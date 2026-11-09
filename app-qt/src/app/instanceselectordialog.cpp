// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "instanceselectordialog.h"
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

InstanceSelectorDialog::InstanceSelectorDialog(int maxInstance, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Select Power Meter");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *topLayout = new QHBoxLayout();

    QLabel *label = new QLabel("Select meter number:", this);
    spinBox = new QSpinBox(this);
    spinBox->setRange(1, maxInstance);
    // Try to default to an unused and not-running instance
    for (int i = 1; i <= maxInstance; ++i) {
        if (!runningInstances.contains(i)) {
            spinBox->setValue(i);
            break;
        }
    }
    //spinBox->setValue(1);

    topLayout->addWidget(label);
    topLayout->addWidget(spinBox);
    mainLayout->addLayout(topLayout);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                     | QDialogButtonBox::Cancel);
    //connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        int selected = spinBox->value();
        if (runningInstances.contains(selected)) {
            QMessageBox::warning(this,
                                 "Instance Running",
                                 QString("RayPowerMeter #%1 is already running. Please "
                                         "select a different one.")
                                     .arg(selected));
            return; // Don't close the dialog
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

int InstanceSelectorDialog::selectedInstance() const
{
    return spinBox->value();
}

void InstanceSelectorDialog::setUsedInstances(const QList<int> &instances)
{
    usedInstances = instances;
}

void InstanceSelectorDialog::setRunningInstances(const QList<int> &instances)
{
    runningInstances = instances;
}
