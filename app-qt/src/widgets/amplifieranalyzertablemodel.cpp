// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "widgets/amplifieranalyzertablemodel.h"
#include <QtMath>

AmplifierAnalyzerTableModel::AmplifierAnalyzerTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{}

int AmplifierAnalyzerTableModel::rowCount(const QModelIndex &) const
{
    return m_points.size();
}

int AmplifierAnalyzerTableModel::columnCount(const QModelIndex &) const
{
    // Matches your requested columns (with corrected DUT input)
    // Freq, Input (DUT), Output, Gain, V, I, T, Efficiency
    return 8;
}

QString AmplifierAnalyzerTableModel::colName(int c)
{
    switch (c) {
    case 0:
        return "Freq (MHz)";
    case 1:
        return "Input (W)"; // corrected DUT input
    case 2:
        return "Output (W)";
    case 3:
        return "Gain (dB)";
    case 4:
        return "Voltage (V)";
    case 5:
        return "Current (A)";
    case 6:
        return "Temp (C)";
    case 7:
        return "Eff (%)";
    default:
        return "";
    }
}

QVariant AmplifierAnalyzerTableModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole)
        return {};

    if (o == Qt::Horizontal)
        return colName(section);

    return section + 1;
}

QVariant AmplifierAnalyzerTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_points.size())
        return {};

    const Point &p = m_points[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0:
            return QString::number(p.freqMHz, 'f', 3);
        case 1:
            return QString::number(p.pinDutW, 'f', 2);
        case 2:
            return QString::number(p.poutW, 'f', 2);
        case 3:
            return QString::number(p.gainDb, 'f', 2);
        case 4:
            return QString::number(p.volts, 'f', 2);
        case 5:
            return QString::number(p.amps, 'f', 2);
        case 6:
            return QString::number(p.tempC, 'f', 1);
        case 7:
            return QString::number(p.effPct, 'f', 1);
        default:
            break;
        }
    }

    return {};
}

void AmplifierAnalyzerTableModel::clear()
{
    beginResetModel();
    m_points.clear();
    endResetModel();
}

void AmplifierAnalyzerTableModel::addPoint(const Point &p)
{
    const int row = m_points.size();
    beginInsertRows(QModelIndex(), row, row);
    m_points.push_back(p);
    endInsertRows();
}
