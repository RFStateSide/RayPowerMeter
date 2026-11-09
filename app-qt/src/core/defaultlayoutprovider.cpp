// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/defaultlayoutprovider.h"
#include <QDebug>
#include <QFile>

namespace raypm {

QString DefaultLayoutProvider::defaultLayoutJson()
{
    QFile f(QStringLiteral(":/layout/default_layout.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[DefaultLayoutProvider] Failed to open :/layout/default_layout.json:"
                   << f.errorString();
        return {};
    }

    QByteArray data = f.readAll();

    // Strip UTF-8 BOM if present (EF BB BF)
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF
        && static_cast<unsigned char>(data[1]) == 0xBB
        && static_cast<unsigned char>(data[2]) == 0xBF) {
        data.remove(0, 3);
    }

    // Optional debug: show first few chars so we can confirm it starts with '{'
    qDebug() << "[DefaultLayoutProvider] default_layout.json head:"
             << QString::fromUtf8(data.left(40));

    return QString::fromUtf8(data);
}

} // namespace raypm
