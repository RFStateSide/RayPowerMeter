// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "helper.h"
#include "common/Constants.h"

#include <QApplication>
#include <QCursor>
#include <QFont>
#include <QGuiApplication>
#include <QLayout>
#include <QList>
#include <QMargins>
#include <QPalette>
#include <QScreen>
#include <QSerialPortInfo>
#include <QStyle>
#include <QtMath>
#include <functional>

#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>

Helper::Helper() {}

// ---------------------------------------------------------------------
// Window centering / visibility helpers
// ---------------------------------------------------------------------

void Helper::CenterWidgetOnVisibleScreen(QWidget *widget)
{
    if (!widget)
        return;

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    if (!screen)
        return;

    QRect screenGeometry = screen->availableGeometry();
    int x = screenGeometry.left() + (screenGeometry.width() - widget->width()) / 2;
    int y = screenGeometry.top() + (screenGeometry.height() - widget->height()) / 2;
    widget->move(x, y);
}

bool Helper::IsWidgetGeometryVisible(QWidget *widget)
{
    if (!widget)
        return false;

    QRect widgetGeometry = widget->frameGeometry();
    const auto &screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen->geometry().intersects(widgetGeometry)) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------
// Serial ports
// ---------------------------------------------------------------------

QList<QString> Helper::GetCommPorts()
{
    const auto serialPortInfos = QSerialPortInfo::availablePorts();
    QList<QString> ports;

    for (const QSerialPortInfo &portInfo : serialPortInfos) {
#ifdef _WIN32
        if (portInfo.portName().startsWith("COM")) {
            ports.append(portInfo.portName());
        }
#else
        ports.append(portInfo.portName());
#endif
    }

    ports.sort();
    ports.insert(0, "None");

    return ports;
}

// ---------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------

double Helper::CelsiusToFahrenheit(double tempC)
{
    return (tempC * 9.0 / 5.0) + 32.0;
}

double Helper::WattsToDBm(double powerW)
{
    if (powerW <= 0.0)
        return -INFINITY;

    // 10 * log10(P[W] * 1000)
    return 10.0 * qLn(powerW * 1000.0) / qLn(10.0);
}

double Helper::SWRToReturnLoss(double swr)
{
    if (swr <= 1.0)
        return INFINITY;

    double reflectionCoeff = (swr - 1.0) / (swr + 1.0);
    // -20 * log10(|Γ|)
    return -20.0 * qLn(reflectionCoeff) / qLn(10.0);
}

// ---------------------------------------------------------------------
// Theme palette + stylesheet
// ---------------------------------------------------------------------

void Helper::ApplyAppTheme(bool isDarkTheme)
{
    const QColor accentTeal(0, 96, 96);
    const QColor hiBlue(30, 90, 160);

    if (isDarkTheme) {
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(43, 43, 43));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(30, 30, 30));
        darkPalette.setColor(QPalette::AlternateBase, QColor(43, 43, 43));
        darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
        darkPalette.setColor(QPalette::ToolTipText, Qt::black);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Button, accentTeal);
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);
        QApplication::setPalette(darkPalette);
    } else {
        QPalette lightPalette = QApplication::style()->standardPalette();
        lightPalette.setColor(QPalette::Button, accentTeal);
        lightPalette.setColor(QPalette::Highlight, hiBlue);
        lightPalette.setColor(QPalette::HighlightedText, Qt::white);

        // Ensure standard text remains dark for readability
        lightPalette.setColor(QPalette::Text, Qt::black);
        lightPalette.setColor(QPalette::WindowText, Qt::black);

        QApplication::setPalette(lightPalette);
    }
}

QString Helper::GlobalStyleSheet(bool isDarkTheme)
{
    QString borderColor = isDarkTheme ? "#00cccc" : "#444";
    QString checkColor = isDarkTheme ? "rgb(0, 96, 96)" : "darkcyan";
    QString textColor = isDarkTheme ? "white" : "black";
    QString bgColor = isDarkTheme ? "#2d2d2d" : "white";
    QString borderAccent = isDarkTheme ? "#00cccc" : "#444";
    QString arrowIcon = isDarkTheme ? ":/icons/sort_down_light.png" : ":/icons/sort_down_dark.png";
    QString comboHoverBg = isDarkTheme ? "#009999" : "#cceeff";
    QString comboHoverTxt = isDarkTheme ? "white" : "black";

    return QString(R"(
        QRadioButton::indicator {
            width: 13px;
            height: 13px;
            border-radius: 6px;
            background-color: transparent;
            border: 1px solid %1;
        }

        QRadioButton::indicator:checked {
            background-color: %2;
            border: 1px solid %1;
        }

        QPushButton {
            background-color: rgb(0, 96, 96);
            color: white;
            border: 1px solid white;
            padding: 1px;
            border-radius: 4px;
            font-weight: bold;
        }

        QPushButton:checked {
            border: 1px solid %1;
        }

        QPushButton:hover {
            background-color: rgb(0, 110, 110);
        }

        QPushButton:pressed {
            background-color: rgb(0, 80, 80);
        }

        QLineEdit {
            background-color: %4;
            color: %3;
            border: 1px solid %5;
            padding: 1px;
            border-radius: 4px;
            font-weight: bold;
        }

        QComboBox {
            color: %3;
            background-color: %4;
            border: 1px solid %5;
            padding: 2px 5px;
            border-radius: 4px;
        }

        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 15px;
            border-left: 1px solid %5;
        }

        QComboBox::down-arrow {
            image: url(%6);
            width: 10px;
            height: 10px;
        }

        QComboBox QAbstractItemView {
            background-color: %4;
            color: %3;
            selection-background-color: %5;
            selection-color: %4;
            border: 1px solid %5;
            outline: none;
        }

        QComboBox QAbstractItemView::item {
            background-color: %4;
            color: %3;
            padding: 4px;
        }

        QComboBox QAbstractItemView::item:hover {
            background-color: %7;
            color: %8;
        }

        QMenuBar {
            background-color: %4;
            color: %3;
        }

        QMenuBar::item:selected {
            background: %5;
        }

        QMenu {
            background-color: %4;
            color: %3;
            border: 1px solid %5;
        }

        QMenu::item:selected {
            background-color: %5;
        }

        QTabWidget::pane {
            border: 1px solid %5;
            background: %4;
            top: -1px;
        }

        QTabBar::tab {
            background: %4;
            color: %3;
            border: 1px solid %5;
            padding: 5px 10px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
            margin-right: 2px;
        }

        QTabBar::tab:selected {
            background: %5;
            color: %4;
            font-weight: bold;
        }

        QTabBar::tab:!selected {
            background: %4;
            color: %3;
        }

        QTabBar::tab:hover {
            background: %5;
            color: %4;
        }

        QGroupBox {
            border: 1px solid %5;
            border-radius: 4px;
            margin-top: 20px;
            background-color: %4;
            color: %3;
            font-weight: bold;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 0 3px;
            background-color: %4;
            color: %3;
        }

        QPushButton, QLineEdit, QRadioButton {
            margin: 4px;
            padding: 1px;
        }
    )")
        .arg(borderColor,
             checkColor,
             textColor,
             bgColor,
             borderAccent,
             arrowIcon,
             comboHoverBg,
             comboHoverTxt);
}

// ---------------------------------------------------------------------
// Scaling
// ---------------------------------------------------------------------

static const QFont &originalFont()
{
    static QFont cached = QApplication::font();
    return cached;
}

// ---------------------------------------------------------------------
// Scaling
// ---------------------------------------------------------------------

static const char kPropOriginalMargins[] = "originalMargins";
static const char kPropOriginalSpacing[] = "originalSpacing";

void Helper::ApplyUserScaleFactor(QWidget *widget, double scaleFactor)
{
    if (!widget)
        return;

    const double minPt = MIN_UI_FONT_PT;
    const int minPx = MIN_UI_FONT_PX;
    scaleFactor = std::max(0.5, scaleFactor);

    const QFont baseOrig = originalFont();
    QFont baseScaled = baseOrig;
    if (baseOrig.pointSizeF() > 0) {
        baseScaled.setPointSizeF(std::max(minPt, baseOrig.pointSizeF() * scaleFactor));
    } else if (baseOrig.pixelSize() > 0) {
        baseScaled.setPixelSize(std::max(minPx, int(baseOrig.pixelSize() * scaleFactor)));
    }
    widget->setFont(baseScaled);

    std::function<void(QLayout *)> scaleLayoutRecursively = [&](QLayout *layout) {
        if (!layout)
            return;

        // ---- IDP: margins scale from ORIGINAL values, not current ----
        if (!layout->property(kPropOriginalMargins).isValid()) {
            const QMargins m = layout->contentsMargins();
            layout->setProperty(kPropOriginalMargins, m.left());
            layout->setProperty(kPropOriginalMargins, m.top());
            layout->setProperty(kPropOriginalMargins, m.right());
            layout->setProperty(kPropOriginalMargins, m.bottom());
        }

        const int l = layout->property(kPropOriginalMargins).toInt();
        const int t = layout->property(kPropOriginalMargins).toInt();
        const int r = layout->property(kPropOriginalMargins).toInt();
        const int b = layout->property(kPropOriginalMargins).toInt();

        layout->setContentsMargins(int(l * scaleFactor),
                                   int(t * scaleFactor),
                                   int(r * scaleFactor),
                                   int(b * scaleFactor));

        // Only scale spacing when it’s explicit (>=0). Preserve -1 “style default”.
        int origSpacing = -1;
        if (layout->property(kPropOriginalSpacing).isValid()) {
            origSpacing = layout->property(kPropOriginalSpacing).toInt();
        } else {
            origSpacing = layout->spacing();
            if (origSpacing >= 0)
                layout->setProperty(kPropOriginalSpacing, origSpacing);
        }
        if (origSpacing >= 0)
            layout->setSpacing(int(origSpacing * scaleFactor));
        // ---------------------------------------------------------------

        for (int i = 0; i < layout->count(); ++i) {
            if (QLayout *childLayout = layout->itemAt(i)->layout()) {
                scaleLayoutRecursively(childLayout);
                continue;
            }
            QWidget *child = layout->itemAt(i)->widget();
            if (!child)
                continue;

            if (child->property("keepFont").toBool())
                continue;

            if (!child->property("originalFont").isValid())
                child->setProperty("originalFont", child->font());

            QFont unscaled = qvariant_cast<QFont>(child->property("originalFont"));
            if (unscaled.pointSizeF() <= 0.0 && unscaled.pixelSize() <= 0) {
                unscaled = baseOrig;
            }

            QFont scaled = unscaled;
            if (unscaled.pointSizeF() > 0) {
                scaled.setPointSizeF(std::max(minPt, unscaled.pointSizeF() * scaleFactor));
            } else if (unscaled.pixelSize() > 0) {
                scaled.setPixelSize(std::max(minPx, int(unscaled.pixelSize() * scaleFactor)));
            }
            child->setFont(scaled);
        }
    };

    scaleLayoutRecursively(widget->layout());
}
