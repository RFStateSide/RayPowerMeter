// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only


#include "dialogabout.h"
#include <QFont>
#include <QLabel>
#include <QPixmap>
#include "ui_dialogabout.h"

DialogAbout::DialogAbout(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogAbout)
{
    ui->setupUi(this);

    // Style for dark theme
    this->setStyleSheet(R"(
        QDialog {
            background-color: #2d2d2d;
            color: white;
        }

        QLabel {
            color: white;
        }

        QDialogButtonBox QPushButton {
            background-color: #007878;
            color: white;
            border: 1px solid #00aaaa;
            padding: 4px 10px;
            border-radius: 4px;
        }

        QDialogButtonBox QPushButton:hover {
            background-color: #00aaaa;
        }

        QFrame {
            background-color: #2d2d2d;
        }
    )");
}

DialogAbout::~DialogAbout()
{
    delete ui;
}

void DialogAbout::populate(const QMap<AboutSection, QString> &contents)
{
    // Load logo image
    if (contents.contains(AboutSection::LOGO)) {
        QPixmap pixmap(contents[AboutSection::LOGO]);

        const QSize iconSize(128, 128);

        ui->label_appIconPlaceholder->setFixedSize(iconSize);
        ui->label_appIconPlaceholder->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

        ui->label_appIconPlaceholder->setPixmap(
            pixmap.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    QVBoxLayout *labelLayout = ui->verticalLayout_labels;

    auto addLabel = [&](const QString &text, bool bold = false) {
        if (!text.isEmpty()) {
            QLabel *label = new QLabel(text);
            label->setWordWrap(true);
            if (bold) {
                QFont f = label->font();
                f.setBold(true);
                label->setFont(f);
            }
            labelLayout->addWidget(label);
        }
    };

    addLabel(contents.value(AboutSection::TITLE), true);
    addLabel("Version: " + contents.value(AboutSection::VERSION));
    addLabel(contents.value(AboutSection::REVISION));
    addLabel(contents.value(AboutSection::DATE));
    addLabel(contents.value(AboutSection::SHORT_INFO));
    addLabel(contents.value(AboutSection::DESCRIPTION));
    addLabel(contents.value(AboutSection::RESOURCES));
    addLabel(contents.value(AboutSection::CREDITS));
    addLabel("Author: " + contents.value(AboutSection::AUTHOR));
    addLabel("License: " + contents.value(AboutSection::LICENCE));
}
