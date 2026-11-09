// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QDialog>

class QSpinBox;
class QPushButton;

class InstanceSelectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InstanceSelectorDialog(int maxInstance = 6, QWidget *parent = nullptr);
    int selectedInstance() const;
    void setUsedInstances(const QList<int> &instances);
    void setRunningInstances(const QList<int> &instances);

private:
    QSpinBox *spinBox;
    QList<int> usedInstances;
    QList<int> runningInstances;
};
