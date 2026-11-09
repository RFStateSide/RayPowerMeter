// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QDialog>

namespace Ui {
class DialogProtectionWarning;
}

namespace raypm {

class DialogProtectionWarning : public QDialog
{
    Q_OBJECT

public:
    explicit DialogProtectionWarning(QWidget *parent = nullptr);
    ~DialogProtectionWarning() override;

    // Theme sync from MainWindow
    void setIsDarkTheme(bool dark);

    // High-level summary, e.g. "High SWR" / "High forward power"
    void setSummaryText(const QString &text);

    // Detailed multi-line info: type/source/value/threshold/etc.
    void setDetailsText(const QString &text);

    // From the "Show this warning next time..." checkbox
    bool shouldShowOnNextTrip() const;

    // Called each telemetry tick so we can auto-close when safe
    void autoCloseIfSafe(bool protectionActive);

signals:
    void resetProtectionRequested();

private slots:
    void onResetClicked();
    void onCloseClicked();

private:
    void applyTheme();

    Ui::DialogProtectionWarning *ui = nullptr;
    bool m_isDarkTheme = true;
};

} // namespace raypm
