// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QAbstractTableModel>
#include <QVector>

class AmplifierAnalyzerTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    struct Point
    {
        double freqMHz = 0.0;

        double pinRigW = 0.0; // measured at Input Coupler (rig output)
        double attnDb = 0.0;  // external attenuator between input coupler and DUT input
        double pinDutW = 0.0; // corrected DUT input power
        double poutW = 0.0;

        double gainDb = 0.0;
        double volts = 0.0;
        double amps = 0.0;
        double tempC = 0.0;
        double effPct = 0.0;
    };

    explicit AmplifierAnalyzerTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    void clear();
    void addPoint(const Point &p);
    const QVector<Point> &points() const { return m_points; }

private:
    QVector<Point> m_points;

    static QString colName(int c);
};
