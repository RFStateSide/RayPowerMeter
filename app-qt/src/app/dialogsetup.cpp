// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "dialogsetup.h"
#include "common/Constants.h"
#include "ui_dialogsetup.h"

#include <QApplication>
#include <QFontDialog>
#include <QListView>
#include <QSerialPortInfo>
#include <QtGlobal>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QThread>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

using namespace raypm;

// Helper - Same logic as MainWindow::layoutConfigDirectory()
QString layoutConfigDirectory()
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    qDebug() << "Layout config base dir:" << baseDir;
    if (baseDir.isEmpty())
        baseDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    QDir dir(baseDir);

    // If the base dir doesn't exist, create it
    if (!dir.exists())
        dir.mkdir(baseDir);

    // Ensure "Layouts" subdirectory exists
    const QString layoutsDirName = QStringLiteral("Layouts");
    // If the layouts dir doesn't exist, create it
    if (!dir.exists(layoutsDirName))
        dir.mkdir(layoutsDirName);
    dir.cd(layoutsDirName);

    return dir.absolutePath();
}

DialogSetup::DialogSetup(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogSetup)
{
    ui->setupUi(this);
    initializeGui();

    // Load settings from persistent storage
    AppSettings s = SettingsManager::load();
    applyFromSettings(s);

    // --- Setup test-connection helpers ---
    m_testTimeout.setSingleShot(true);
    connect(&m_testTimeout, &QTimer::timeout, this, &DialogSetup::handleTestTimeout);

    connect(&m_netManager,
            &QNetworkAccessManager::finished,
            this,
            &DialogSetup::handleTestHttpFinished);

    connect(&m_testSerial, &QSerialPort::readyRead, this, &DialogSetup::handleTestSerialReadyRead);

    connect(ui->pushButtonTestConnection,
            &QPushButton::clicked,
            this,
            &DialogSetup::on_pushButtonTestConnection_clicked);
}

DialogSetup::~DialogSetup()
{
    stopRigctld();
    delete ui;
}

void DialogSetup::initializeGui()
{
    setWindowTitle(tr("Setup"));

    // Optional icons (safe even if resources don't exist)
    m_icOk = QIcon(":/icons/ok.png");
    m_icWarn = QIcon(":/icons/warn.png");
    m_icStop = QIcon(":/icons/stop.png");

    // Use list views for nicer drop-downs
    ui->comboBoxMercuryPort->setView(new QListView(this));
    ui->comboBoxUnitsTemp->setView(new QListView(this));
    ui->comboBoxUnitsPower->setView(new QListView(this));
    ui->comboBoxUnitsReflected->setView(new QListView(this));
    ui->comboBoxWebSocketMode->setView(new QListView(this));
    ui->comboBoxAppTheme->setView(new QListView(this));

    // Populate static combo contents
    ui->comboBoxUnitsTemp->clear();
    ui->comboBoxUnitsTemp->addItems(unitsTemp);

    ui->comboBoxUnitsPower->clear();
    ui->comboBoxUnitsPower->addItems(unitsPower);

    ui->comboBoxUnitsReflected->clear();
    ui->comboBoxUnitsReflected->addItems(unitsReflected);

    ui->comboBoxWebSocketMode->clear();
    ui->comboBoxWebSocketMode->addItems(connectionMode);

    ui->comboBoxAppTheme->clear();
    ui->comboBoxAppTheme->addItems(appTheme);

    // Initial scale range; actual value comes from settings
    ui->horizontalSliderAppScaleFactor->setMinimum(SCALE_FACTOR_MIN);
    ui->horizontalSliderAppScaleFactor->setMaximum(SCALE_FACTOR_MAX);

    ui->spinBoxGridColumns->setMinimum(MIN_GRID_COLS);
    ui->spinBoxGridColumns->setMaximum(MAX_GRID_COLS);

    ui->comboBoxStartupLayout->setView(new QListView(this));

    initializeHamlibUi();
}

void DialogSetup::applyFromSettings(const AppSettings &s)
{
    // Serial ports
    reloadSerialPorts(s.serial.portName);

    // Connection mode
    int modeIndex = s.http.connectionModeIndex;
    if (modeIndex < 0 || modeIndex >= connectionMode.size())
        modeIndex = 1; // HTTP (LAN)
    ui->comboBoxWebSocketMode->setCurrentIndex(modeIndex);

    // Ensure the correct groups are enabled/disabled initially
    on_comboBoxWebSocketMode_currentIndexChanged(modeIndex);

    // HTTP host/port
    ui->lineEditWebSocketAddress->setText(s.http.host);
    ui->lineEditWebSocketPortNum->setText(QString::number(s.http.port));
    ui->checkBoxEnableTls->setChecked(s.http.enableTls);

    // Units
    ui->comboBoxUnitsTemp->setCurrentIndex(qBound(0, s.unitsTempIndex, unitsTemp.size() - 1));
    ui->comboBoxUnitsPower->setCurrentIndex(qBound(0, s.unitsPowerIndex, unitsPower.size() - 1));
    ui->comboBoxUnitsReflected->setCurrentIndex(
        qBound(0, s.unitsReflectedIndex, unitsReflected.size() - 1));

    // Theme
    int themeIndex = (s.theme == AppTheme::Dark) ? 1 : 0;
    ui->comboBoxAppTheme->setCurrentIndex(themeIndex);

    // Layout
    refreshStartupLayoutCombo(s.startupLayoutName);
    ui->spinBoxGridColumns->setValue(s.layoutGridColumns);

    // Scale
    m_scaleFactor = s.appScale;
    int sliderVal = static_cast<int>(m_scaleFactor * 100.0 + 0.5);
    sliderVal = qBound(50, sliderVal, 200);
    ui->horizontalSliderAppScaleFactor->setValue(sliderVal);
    ui->lineEditScale->setText(QString::number(m_scaleFactor, 'f', 2));

    // Hamlib
    applyHamlibFromSettings(s);

    // Auto-start rigctld (Serial tab only)
    // - hamlib.enabled must be on
    // - connectionModeIndex == 0 means Serial tab (local rigctld)
    // - lastTestOk gates the checkbox + persisted autoStart flag
    if (s.hamlib.enabled && s.hamlib.connectionModeIndex == 0 && s.hamlib.lastTestOk
        && s.hamlib.serial.autoStartRigctld) {
        QString err;
        if (!startRigctldForSerial(&err)) {
            emit ShowStatusBarMessage(tr("Auto-start rigctld failed: %1").arg(err), 8000);
        } else {
            emit ShowStatusBarMessage(tr("rigctld auto-started"), 3000);
        }
    }

    // Always show Main tab first
    ui->tabWidgetHamlib->setCurrentIndex(0);
    ui->tabWidget->setCurrentIndex(0);
}

AppSettings DialogSetup::saveSettings() const
{
    AppSettings s = SettingsManager::load(); // start from persisted

    // Serial
    s.serial.portName = ui->comboBoxMercuryPort->currentText();

    // Connection mode + HTTP
    s.http.connectionModeIndex = ui->comboBoxWebSocketMode->currentIndex();
    s.http.host = ui->lineEditWebSocketAddress->text().trimmed();
    s.http.port = ui->lineEditWebSocketPortNum->text().toInt();
    s.http.enableTls = ui->checkBoxEnableTls->isChecked();

    // Units
    s.unitsTempIndex = ui->comboBoxUnitsTemp->currentIndex();
    s.unitsPowerIndex = ui->comboBoxUnitsPower->currentIndex();
    s.unitsReflectedIndex = ui->comboBoxUnitsReflected->currentIndex();

    // Theme
    const int themeIndex = ui->comboBoxAppTheme->currentIndex();
    s.theme = (themeIndex == 1) ? AppTheme::Dark : AppTheme::Light;

    // Layout
    if (ui->comboBoxStartupLayout) {
        s.startupLayoutName = ui->comboBoxStartupLayout->currentData().toString();
    }
    s.layoutGridColumns = ui->spinBoxGridColumns->value();

    // Scale
    s.appScale = m_scaleFactor;

    // Hamlib
    saveHamlibToSettings(s);

    return s;
}

void DialogSetup::reloadSerialPorts(const QString &preferredPort)
{
    ui->comboBoxMercuryPort->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    int preferredIndex = -1;
    int index = 0;

    for (const QSerialPortInfo &info : ports) {
        ui->comboBoxMercuryPort->addItem(info.portName());
        if (info.portName() == preferredPort) {
            preferredIndex = index;
        }
        ++index;
    }

    if (ui->comboBoxMercuryPort->count() == 0) {
        ui->comboBoxMercuryPort->addItem(QStringLiteral("None"));
    }

    if (preferredIndex >= 0) {
        ui->comboBoxMercuryPort->setCurrentIndex(preferredIndex);
    }
}

void DialogSetup::on_pushButtonPortRefresh_clicked()
{
    reloadSerialPorts(ui->comboBoxMercuryPort->currentText());
}

void DialogSetup::on_pushButtonLoad_clicked()
{
    AppSettings s = SettingsManager::load();
    applyFromSettings(s);
    emit ShowStatusBarMessage(tr("Settings reloaded"), 2000);
}

void DialogSetup::on_pushButtonSave_clicked()
{
    AppSettings s = saveSettings();
    SettingsManager::save(s);

    // If user enabled auto-start, do it immediately (so they don’t have to hit Start)
    if (s.hamlib.enabled && s.hamlib.connectionModeIndex == 0 && s.hamlib.lastTestOk
        && s.hamlib.serial.autoStartRigctld) {
        QString err;
        if (!startRigctldForSerial(&err)) {
            emit ShowStatusBarMessage(tr("Settings saved, but rigctld auto-start failed: %1")
                                          .arg(err),
                                      8000);
        } else {
            emit ShowStatusBarMessage(tr("Settings saved (rigctld auto-started)"), 3000);
        }
    } else {
        emit ShowStatusBarMessage(tr("Settings saved"), 3000);
    }

    emit ReloadSettings();
}

void DialogSetup::on_pushButtonClose_clicked()
{
    close();
}

void DialogSetup::on_pushButtonFont_clicked()
{
    bool ok = false;
    QFont currentFont = QApplication::font();

    QFont selected = QFontDialog::getFont(&ok, currentFont, this, tr("Choose Application Font"));

    if (ok) {
        m_fontFamily = selected.family();
        m_fontSize = selected.pointSize();
        m_fontWeight = selected.weight();
        m_fontItalic = selected.italic();
        QApplication::setFont(selected);
    } else {
        QApplication::setFont(currentFont);
    }
}

void DialogSetup::on_horizontalSliderAppScaleFactor_valueChanged(int value)
{
    double newScale = value / 100.0;
    ui->horizontalSliderAppScaleFactor->setToolTip(QString::number(value) + "%");
    ui->lineEditScale->setText(QString::number(newScale, 'f', 2));

    m_scaleFactor = newScale;
    emit scaleFactorChanged(m_scaleFactor);
}

void DialogSetup::on_comboBoxWebSocketMode_currentIndexChanged(int index)
{
    // index 0 = COMM (Serial)
    // index 1 = HTTP (LAN)
    // index 2 = HTTP (Access Point)

    const bool useSerial = (index == 0);
    const bool useHttp = (index == 1 || index == 2);

    // Serial group: only meaningful in COMM mode
    if (ui->groupSerial)
        ui->groupSerial->setEnabled(useSerial);

    // IMPORTANT:
    // Keep the Mode combo itself *always* enabled, and just enable/disable
    // the HTTP-specific fields.
    if (ui->groupNetwork) {
        ui->groupNetwork->setEnabled(true); // don’t disable the whole group

        // If your object names differ, adjust them accordingly:
        ui->labelConnMode->setEnabled(true);         // label "Mode:"
        ui->comboBoxWebSocketMode->setEnabled(true); // always enabled

        ui->labelAddress->setEnabled(useHttp);
        ui->lineEditWebSocketAddress->setEnabled(useHttp);

        ui->labelPort->setEnabled(useHttp);
        ui->lineEditWebSocketPortNum->setEnabled(useHttp);

        ui->checkBoxEnableTls->setEnabled(useHttp);
    }
}

void DialogSetup::on_pushButtonTestConnection_clicked()
{
    // Cancel any previous test
    if (m_testTransport == TestTransport::Serial && m_testSerial.isOpen())
        m_testSerial.close();
    m_testTimeout.stop();
    m_testSerialBuffer.clear();
    m_testTransport = TestTransport::None;

    const int modeIndex = ui->comboBoxWebSocketMode->currentIndex();

    if (modeIndex == 0) {
        // --- COMM (Serial) test: send {"cmd":"get_status"} and expect a status JSON back ---
        const QString portName = ui->comboBoxMercuryPort->currentText().trimmed();
        if (portName.isEmpty() || portName == QStringLiteral("None")) {
            emit ShowStatusBarMessage(tr("No serial port selected."), 5000);
            return;
        }

        m_testSerial.setPortName(portName);
        m_testSerial.setBaudRate(115200);
        m_testSerial.setDataBits(QSerialPort::Data8);
        m_testSerial.setParity(QSerialPort::NoParity);
        m_testSerial.setStopBits(QSerialPort::OneStop);
        m_testSerial.setFlowControl(QSerialPort::NoFlowControl);

        if (!m_testSerial.open(QIODevice::ReadWrite)) {
            emit ShowStatusBarMessage(tr("Failed to open port %1: %2")
                                          .arg(portName, m_testSerial.errorString()),
                                      5000);
            return;
        }

        m_testTransport = TestTransport::Serial;
        m_testTimeout.start(3000);
        emit ShowStatusBarMessage(tr("Testing COMM on %1 ...").arg(portName), 5000);

        static const QByteArray cmd("{\"cmd\":\"get_status\"}\n");
        m_testSerial.write(cmd);
        m_testSerial.flush();
    } else {
        // --- HTTP test: GET /ping and expect "pong" ---
        QString host = ui->lineEditWebSocketAddress->text().trimmed();

        if (host.isEmpty()) {
            // Reasonable defaults
            host = (modeIndex == 2) ? QStringLiteral("192.168.4.1")
                                    : QStringLiteral("raypowermeter.local");
        }

        bool ok = false;
        int port = ui->lineEditWebSocketPortNum->text().toInt(&ok);
        const bool useTls = ui->checkBoxEnableTls->isChecked();

        if (!ok || port <= 0) {
            port = useTls ? 443 : 80;
        }

        QUrl url;
        url.setScheme(useTls ? QStringLiteral("https") : QStringLiteral("http"));
        url.setHost(host);
        url.setPort(port);
        url.setPath(QStringLiteral("/ping"));

        m_testTransport = TestTransport::Http;
        m_testTimeout.start(3000);

        emit ShowStatusBarMessage(tr("Testing HTTP connection to %1 ...").arg(url.toString()), 3000);

        QNetworkRequest req(url);
        m_netManager.get(req);
    }
}

void DialogSetup::handleTestHttpFinished(QNetworkReply *reply)
{
    // Always delete the reply
    reply->deleteLater();

    if (m_testTransport != TestTransport::Http)
        return; // stale reply from a previous test

    m_testTimeout.stop();
    m_testTransport = TestTransport::None;

    if (reply->error() != QNetworkReply::NoError) {
        emit ShowStatusBarMessage(tr("HTTP test failed: %1").arg(reply->errorString()), 6000);
        return;
    }

    const QByteArray body = reply->readAll().trimmed();
    if (body == QByteArrayLiteral("pong")) {
        emit ShowStatusBarMessage(tr("HTTP test OK: received 'pong'."), 6000);
    } else {
        emit ShowStatusBarMessage(tr("HTTP test OK: unexpected response '%1'.")
                                      .arg(QString::fromUtf8(body)),
                                  6000);
    }
}

void DialogSetup::handleTestSerialReadyRead()
{
    if (m_testTransport != TestTransport::Serial)
        return;

    m_testSerialBuffer.append(m_testSerial.readAll());

    for (;;) {
        int nl = m_testSerialBuffer.indexOf('\n');
        int cr = m_testSerialBuffer.indexOf('\r');
        int eol = -1;

        if (nl >= 0 && cr >= 0)
            eol = qMin(nl, cr);
        else if (nl >= 0)
            eol = nl;
        else if (cr >= 0)
            eol = cr;

        if (eol < 0)
            break;

        QByteArray line = m_testSerialBuffer.left(eol).trimmed();
        m_testSerialBuffer.remove(0, eol + 1);

        if (line.isEmpty())
            continue;

        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject root = doc.object();
        const QString typeStr = root.value(QStringLiteral("type")).toString();

        if (typeStr == QLatin1String("status")) {
            // Success: Comm server replied to get_status
            m_testTimeout.stop();
            m_testTransport = TestTransport::None;

            if (m_testSerial.isOpen())
                m_testSerial.close();

            emit ShowStatusBarMessage(tr("Serial test OK: received status from device."), 6000);
            return;
        }
    }
}

void DialogSetup::handleTestTimeout()
{
    if (m_testTransport == TestTransport::None)
        return;

    if (m_testTransport == TestTransport::Serial) {
        if (m_testSerial.isOpen())
            m_testSerial.close();
        emit ShowStatusBarMessage(tr("Serial test timed out."), 6000);
    } else if (m_testTransport == TestTransport::Http) {
        emit ShowStatusBarMessage(tr("HTTP test timed out."), 6000);
    }

    m_testTransport = TestTransport::None;
}

void DialogSetup::refreshStartupLayoutCombo(const QString &selectedName)
{
    if (!ui->comboBoxStartupLayout)
        return;

    ui->comboBoxStartupLayout->clear();

    // Entry 0: built-in default (no file)
    ui->comboBoxStartupLayout->addItem(tr("<Built-in default layout>"), QString());

    const QString layoutsPath = layoutConfigDirectory();
    QDir dir(layoutsPath);
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"),
                                            QDir::Files,
                                            QDir::Name);

    int indexToSelect = 0;
    int idx = 1;

    for (const QString &file : files) {
        ui->comboBoxStartupLayout->addItem(file, file);

        if (!selectedName.isEmpty() && file.compare(selectedName, Qt::CaseInsensitive) == 0) {
            indexToSelect = idx;
        }

        ++idx;
    }

    ui->comboBoxStartupLayout->setCurrentIndex(indexToSelect);
}

// -----------------------------------------------------------------------------
// Hamlib / rigctld
//
// We intentionally do NOT allow auto-start rigctld unless the user has run a
// successful Test at least once (m_hamlibLastTestOk == true).
// -----------------------------------------------------------------------------

namespace {

QStringList baudOptions()
{
    return {"4800", "9600", "19200", "38400", "57600", "115200"};
}

bool tcpCommand(
    const QString &host, int port, const QByteArray &cmd, QByteArray *replyLine, QString *errOut)
{
    QTcpSocket sock;
    sock.connectToHost(host, static_cast<quint16>(port));
    if (!sock.waitForConnected(800)) {
        if (errOut)
            *errOut = QObject::tr("TCP connect failed: %1").arg(sock.errorString());
        return false;
    }

    QByteArray line = cmd;
    if (!line.endsWith('\n'))
        line.append('\n');

    if (sock.write(line) != line.size()) {
        if (errOut)
            *errOut = QObject::tr("TCP write failed: %1").arg(sock.errorString());
        return false;
    }
    if (!sock.waitForBytesWritten(500)) {
        if (errOut)
            *errOut = QObject::tr("TCP write timeout");
        return false;
    }
    if (!sock.waitForReadyRead(800)) {
        if (errOut)
            *errOut = QObject::tr("No reply from rigctld");
        return false;
    }

    QByteArray resp = sock.readLine().trimmed();

    if (replyLine)
        *replyLine = resp;

    if (resp.startsWith("RPRT")) {
        if (errOut)
            *errOut = QObject::tr("rigctld error: %1").arg(QString::fromUtf8(resp));
        return false;
    }

    return true;
}

double mhzToHz(const QString &mhzText, bool *okOut)
{
    bool ok = false;
    const double mhz = mhzText.trimmed().toDouble(&ok);
    if (!ok) {
        if (okOut)
            *okOut = false;
        return 0.0;
    }
    if (okOut)
        *okOut = true;
    return mhz * 1000000.0;
}

QString hzToMhz(double hz)
{
    return QString::number(hz / 1000000.0, 'f', 6);
}

class HamlibTestDialog final : public QDialog
{
public:
    HamlibTestDialog(const QString &rigText,
                     const QString &host,
                     int port,
                     QWidget *parent = nullptr)
        : QDialog(parent)
        , m_host(host)
        , m_port(port)
    {
        setWindowTitle(tr("RayWn2Meter Hamlib Test"));
        setModal(true);

        auto *grid = new QGridLayout(this);

        int row = 0;

        auto *lblRadio = new QLabel(tr("Radio: %1").arg(rigText), this);
        grid->addWidget(lblRadio, row++, 0, 1, 4);

        auto *lblHost = new QLabel(tr("rigctld: %1:%2").arg(host).arg(port), this);
        grid->addWidget(lblHost, row++, 0, 1, 4);

        // VFO A row
        grid->addWidget(new QLabel(tr("VFO A"), this), row, 0);
        m_editVfoA = new QLineEdit(this);
        m_editVfoA->setMinimumWidth(120);
        grid->addWidget(m_editVfoA, row, 1);

        auto *btnSetVfo = new QPushButton(tr("Set VFO"), this);
        auto *btnGetVfo = new QPushButton(tr("Get VFO"), this);
        grid->addWidget(btnSetVfo, row, 2);
        grid->addWidget(btnGetVfo, row, 3);
        row++;

        // Mode row
        grid->addWidget(new QLabel(tr("Mode"), this), row, 0);
        m_comboMode = new QComboBox(this);
        m_comboMode->addItems({"USB", "LSB", "CW", "AM", "FM"});
        grid->addWidget(m_comboMode, row, 1);

        auto *btnSetMode = new QPushButton(tr("Set Mode"), this);
        auto *btnGetMode = new QPushButton(tr("Get Mode"), this);
        grid->addWidget(btnSetMode, row, 2);
        grid->addWidget(btnGetMode, row, 3);
        row++;

        m_labelStatus = new QLabel(tr(""), this);
        m_labelStatus->setWordWrap(true);
        grid->addWidget(m_labelStatus, row++, 0, 1, 4);

        auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        grid->addWidget(bb, row++, 0, 1, 4);

        connect(btnGetVfo, &QPushButton::clicked, this, [this]() { onGetVfo(); });
        connect(btnSetVfo, &QPushButton::clicked, this, [this]() { onSetVfo(); });
        connect(btnGetMode, &QPushButton::clicked, this, [this]() { onGetMode(); });
        connect(btnSetMode, &QPushButton::clicked, this, [this]() { onSetMode(); });

        // Prime fields
        onGetVfo();
        onGetMode();
    }

private:
    void setStatusOk(const QString &msg) { m_labelStatus->setText(tr("OK: %1").arg(msg)); }
    void setStatusErr(const QString &msg) { m_labelStatus->setText(tr("Error: %1").arg(msg)); }

    void onGetVfo()
    {
        QByteArray reply;
        QString err;
        if (!tcpCommand(m_host, m_port, "f", &reply, &err)) {
            setStatusErr(err);
            return;
        }
        bool ok = false;
        const double hz = QString::fromUtf8(reply).toDouble(&ok);
        if (!ok) {
            setStatusErr(tr("Unexpected rigctld reply: %1").arg(QString::fromUtf8(reply)));
            return;
        }
        m_editVfoA->setText(hzToMhz(hz));
        setStatusOk(tr("Read frequency %1 Hz").arg(hz, 0, 'f', 0));
    }

    void onSetVfo()
    {
        bool ok = false;
        const double hz = mhzToHz(m_editVfoA->text(), &ok);
        if (!ok) {
            setStatusErr(tr("Enter a valid frequency (MHz). Example: 14.2416"));
            return;
        }

        QString err;
        QByteArray reply;
        const QByteArray cmd = QByteArray("F ") + QByteArray::number(static_cast<qlonglong>(hz));
        if (!tcpCommand(m_host, m_port, cmd, &reply, &err)) {
            setStatusErr(err);
            return;
        }

        // Many rigs don't echo; do a read-back to confirm
        onGetVfo();
    }

    void onGetMode()
    {
        QByteArray reply;
        QString err;
        if (!tcpCommand(m_host, m_port, "m", &reply, &err)) {
            // Some rigs may not support; keep non-fatal
            setStatusErr(err);
            return;
        }

        // rigctld typically returns: "USB\n2400" (we only read first line)
        const QString mode = QString::fromUtf8(reply).trimmed();
        const int idx = m_comboMode->findText(mode);
        if (idx >= 0)
            m_comboMode->setCurrentIndex(idx);

        setStatusOk(tr("Read mode %1").arg(mode));
    }

    void onSetMode()
    {
        const QString mode = m_comboMode->currentText();
        QString err;
        QByteArray reply;
        // Passband 0 lets rig decide
        const QByteArray cmd = QByteArray("M ") + mode.toUtf8() + QByteArray(" 0");
        if (!tcpCommand(m_host, m_port, cmd, &reply, &err)) {
            setStatusErr(err);
            return;
        }
        onGetMode();
    }

    QString m_host;
    int m_port = 0;

    QLineEdit *m_editVfoA = nullptr;
    QComboBox *m_comboMode = nullptr;
    QLabel *m_labelStatus = nullptr;
};

} // namespace

QString DialogSetup::hamlibExePath(const QString &exeName) const
{
    // Defensive: if UI is older, skip silently
    if (!ui->lineEditHamlibBinPath) {
        return QStandardPaths::findExecutable(exeName);
    }

    const QString bin = ui->lineEditHamlibBinPath->text().trimmed();
    if (!bin.isEmpty()) {
        const QString candidate = QDir(bin).filePath(exeName);
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }
    return QStandardPaths::findExecutable(exeName);
}

void DialogSetup::initializeHamlibUi()
{
    if (!ui->comboBoxHamlibRigModel) {
        // Older UI file (no Hamlib tab) – nothing to do.
        return;
    }

    // Make dropdowns consistent with other tabs
    ui->comboBoxHamlibRigModel->setView(new QListView(this));
    ui->comboBoxHamlibSerialPort->setView(new QListView(this));
    ui->comboBoxHamlibSerialBaud->setView(new QListView(this));
    ui->comboBoxHamlibDataBits->setView(new QListView(this));
    ui->comboBoxHamlibStopBits->setView(new QListView(this));
    ui->comboBoxHamlibFlowControl->setView(new QListView(this));
    ui->comboBoxHamlibParity->setView(new QListView(this));

    ui->comboBoxHamlibSerialBaud->addItems(baudOptions());
    ui->comboBoxHamlibDataBits->addItems({"5", "6", "7", "8"});
    ui->comboBoxHamlibStopBits->addItems({"1", "2"});
    ui->comboBoxHamlibFlowControl->addItems({"None", "RTS/CTS", "XON/XOFF"});
    ui->comboBoxHamlibParity->addItems({"None", "Even", "Odd"});

    // Default: gated until successful test
    ui->checkBoxHamlibAutoStart->setEnabled(false);

    // If the user changes anything that affects connectivity, require a new Test.
    auto invalidate = [this]() {
        // ✅ FIX: don’t invalidate while we are applying persisted settings
        if (m_applyingHamlibSettings)
            return;

        m_hamlibLastTestOk = false;
        if (ui->checkBoxHamlibAutoStart) {
            ui->checkBoxHamlibAutoStart->setChecked(false);
            ui->checkBoxHamlibAutoStart->setEnabled(false);
        }
        updateHamlibUiState();
    };

    connect(ui->comboBoxHamlibRigModel, &QComboBox::currentIndexChanged, this, invalidate);
    connect(ui->comboBoxHamlibSerialPort, &QComboBox::currentIndexChanged, this, invalidate);
    connect(ui->comboBoxHamlibSerialBaud, &QComboBox::currentIndexChanged, this, invalidate);
    connect(ui->spinBoxHamlibRigctldPort, qOverload<int>(&QSpinBox::valueChanged), this, invalidate);
    connect(ui->lineEditHamlibHost, &QLineEdit::textChanged, this, invalidate);
    connect(ui->spinBoxHamlibPort, qOverload<int>(&QSpinBox::valueChanged), this, invalidate);

    if (ui->tabWidgetHamlib) {
        connect(ui->tabWidgetHamlib, &QTabWidget::currentChanged, this, invalidate);
    }

    refreshHamlibRigList();
    refreshHamlibSerialPorts(QString());
    updateHamlibUiState();
}

void DialogSetup::refreshHamlibSerialPorts(const QString &preferredPort)
{
    if (!ui->comboBoxHamlibSerialPort)
        return;

    ui->comboBoxHamlibSerialPort->clear();

    const auto ports = QSerialPortInfo::availablePorts();
    int preferredIndex = -1;
    int i = 0;
    for (const auto &info : ports) {
        ui->comboBoxHamlibSerialPort->addItem(info.portName());
        if (!preferredPort.isEmpty() && info.portName() == preferredPort) {
            preferredIndex = i;
        }
        ++i;
    }
    if (preferredIndex >= 0)
        ui->comboBoxHamlibSerialPort->setCurrentIndex(preferredIndex);
}

void DialogSetup::refreshHamlibRigList()
{
    if (!ui->comboBoxHamlibRigModel)
        return;

    ui->comboBoxHamlibRigModel->clear();
    ui->comboBoxHamlibRigModel->addItem(tr("Select rig..."), -1);

    const QString rigctl = hamlibExePath("rigctl.exe");
    if (rigctl.isEmpty()) {
        ui->labelHamlibStatus->setText(tr("Hamlib not found (rigctl.exe)"));
        return;
    }

    QProcess p;
    p.start(rigctl, {"-l"});
    if (!p.waitForFinished(2000)) {
        ui->labelHamlibStatus->setText(tr("Failed to run rigctl -l"));
        return;
    }

    const QByteArray out = p.readAllStandardOutput();
    const QList<QByteArray> lines = out.split('\n');

    QRegularExpression re("^\\s*(\\d+)\\s+(.*)$");

    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith('#'))
            continue;

        auto m = re.match(line);
        if (!m.hasMatch())
            continue;

        const int id = m.captured(1).toInt();
        const QString full = m.captured(2).trimmed(); // full rigctl -l line after the ID

        QString display = full;
        const QStringList cols = full.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (cols.size() >= 2) {
            display = cols[0] + " " + cols[1]; // e.g. "FlexRadio/Apache PowerSDR"
        }

        ui->comboBoxHamlibRigModel->addItem(display, id);

        // Optional: keep the full info accessible via hover tooltip
        const int idx = ui->comboBoxHamlibRigModel->count() - 1;
        ui->comboBoxHamlibRigModel->setItemData(idx, full, Qt::ToolTipRole);
    }

    ui->labelHamlibStatus->setText(tr("Hamlib OK"));
}

void DialogSetup::applyHamlibFromSettings(const raypm::AppSettings &s)
{
    if (!ui->checkBoxHamlibEnabled)
        return;

    // ✅ FIX: prevent invalidate() from firing while we apply persisted settings
    m_applyingHamlibSettings = true;

    ui->checkBoxHamlibEnabled->setChecked(s.hamlib.enabled);
    ui->lineEditHamlibBinPath->setText(s.hamlib.binPath);

    refreshHamlibRigList();

    // Select rig model, if present
    const int rigId = s.hamlib.serial.rigModelId;
    const int rigIndex = ui->comboBoxHamlibRigModel->findData(rigId);
    if (rigIndex >= 0)
        ui->comboBoxHamlibRigModel->setCurrentIndex(rigIndex);

    refreshHamlibSerialPorts(s.hamlib.serial.portName);

    const QString baudStr = QString::number(s.hamlib.serial.baudRate);
    const int baudIndex = ui->comboBoxHamlibSerialBaud->findText(baudStr);
    if (baudIndex >= 0)
        ui->comboBoxHamlibSerialBaud->setCurrentIndex(baudIndex);

    ui->comboBoxHamlibDataBits->setCurrentText(QString::number(s.hamlib.serial.dataBits));
    ui->comboBoxHamlibStopBits->setCurrentText(QString::number(s.hamlib.serial.stopBits));

    if (ui->comboBoxHamlibFlowControl->count() > 0)
        ui->comboBoxHamlibFlowControl->setCurrentIndex(
            qBound(0, s.hamlib.serial.flowControl, ui->comboBoxHamlibFlowControl->count() - 1));

    if (ui->comboBoxHamlibParity->count() > 0)
        ui->comboBoxHamlibParity->setCurrentIndex(
            qBound(0, s.hamlib.serial.parity, ui->comboBoxHamlibParity->count() - 1));

    ui->spinBoxHamlibRigctldPort->setValue(s.hamlib.serial.tcpPort);

    ui->lineEditHamlibHost->setText(s.hamlib.network.host);
    ui->spinBoxHamlibPort->setValue(s.hamlib.network.port);

    m_hamlibLastTestOk = s.hamlib.lastTestOk;

    ui->checkBoxHamlibAutoStart->setChecked(s.hamlib.serial.autoStartRigctld);

    if (ui->tabWidgetHamlib) {
        const int idx = qBound(0, s.hamlib.connectionModeIndex, ui->tabWidgetHamlib->count() - 1);
        ui->tabWidgetHamlib->setCurrentIndex(idx);
    }

    m_applyingHamlibSettings = false;

    // Recompute enabled/disabled state after apply
    updateHamlibUiState();
}

void DialogSetup::saveHamlibToSettings(raypm::AppSettings &s) const
{
    if (!ui->checkBoxHamlibEnabled)
        return;

    s.hamlib.enabled = ui->checkBoxHamlibEnabled->isChecked();
    s.hamlib.binPath = ui->lineEditHamlibBinPath->text().trimmed();

    s.hamlib.serial.rigModelId = ui->comboBoxHamlibRigModel->currentData().toInt();
    s.hamlib.serial.portName = ui->comboBoxHamlibSerialPort->currentText().trimmed();
    s.hamlib.serial.baudRate = ui->comboBoxHamlibSerialBaud->currentText().toInt();
    s.hamlib.serial.dataBits = ui->comboBoxHamlibDataBits->currentText().toInt();
    s.hamlib.serial.stopBits = ui->comboBoxHamlibStopBits->currentText().toInt();
    s.hamlib.serial.flowControl = ui->comboBoxHamlibFlowControl->currentIndex();
    s.hamlib.serial.parity = ui->comboBoxHamlibParity->currentIndex();
    s.hamlib.serial.tcpPort = ui->spinBoxHamlibRigctldPort->value();

    s.hamlib.network.host = ui->lineEditHamlibHost->text().trimmed();
    s.hamlib.network.port = ui->spinBoxHamlibPort->value();

    s.hamlib.lastTestOk = m_hamlibLastTestOk;

    // Gate auto-start behind test success
    s.hamlib.serial.autoStartRigctld = m_hamlibLastTestOk
                                       && ui->checkBoxHamlibAutoStart->isChecked();

    if (ui->tabWidgetHamlib)
        s.hamlib.connectionModeIndex = ui->tabWidgetHamlib->currentIndex();
}

void DialogSetup::updateHamlibUiState()
{
    // Keep the Hamlib UI usable even when Hamlib isn't installed yet.
    // Users must be able to open Help and set the bin folder.
    if (!ui->labelHamlibStatus)
        return;

    const bool enabled = (ui->checkBoxHamlibEnabled && ui->checkBoxHamlibEnabled->isChecked());

    const QString rigctld = hamlibExePath(QStringLiteral("rigctld.exe"));
    const QString rigctl = hamlibExePath(QStringLiteral("rigctl.exe"));
    const bool haveHamlib = (!rigctld.isEmpty() && !rigctl.isEmpty());

    // Allow configuration at all times
    if (ui->tabWidgetHamlib)
        ui->tabWidgetHamlib->setEnabled(true);

    if (ui->comboBoxHamlibRigModel)
        ui->comboBoxHamlibRigModel->setEnabled(true);
    if (ui->comboBoxHamlibSerialPort)
        ui->comboBoxHamlibSerialPort->setEnabled(true);
    if (ui->comboBoxHamlibSerialBaud)
        ui->comboBoxHamlibSerialBaud->setEnabled(true);
    if (ui->comboBoxHamlibDataBits)
        ui->comboBoxHamlibDataBits->setEnabled(true);
    if (ui->comboBoxHamlibStopBits)
        ui->comboBoxHamlibStopBits->setEnabled(true);
    if (ui->comboBoxHamlibFlowControl)
        ui->comboBoxHamlibFlowControl->setEnabled(true);
    if (ui->comboBoxHamlibParity)
        ui->comboBoxHamlibParity->setEnabled(true);
    if (ui->spinBoxHamlibRigctldPort)
        ui->spinBoxHamlibRigctldPort->setEnabled(true);
    if (ui->lineEditHamlibHost)
        ui->lineEditHamlibHost->setEnabled(true);
    if (ui->spinBoxHamlibPort)
        ui->spinBoxHamlibPort->setEnabled(true);
    if (ui->lineEditHamlibBinPath)
        ui->lineEditHamlibBinPath->setEnabled(true);
    if (ui->pushButtonHamlibBrowseBin)
        ui->pushButtonHamlibBrowseBin->setEnabled(true);

    // Actions that require Hamlib binaries
    if (ui->pushButtonHamlibRefreshRigList)
        ui->pushButtonHamlibRefreshRigList->setEnabled(haveHamlib);
    if (ui->pushButtonHamlibTest)
        ui->pushButtonHamlibTest->setEnabled(haveHamlib);

    // Start/Stop rigctld: allow when installed (even if not "Enabled",
    // since users may want to test configuration first).
    const bool running = (m_rigctldProcess && m_rigctldProcess->state() != QProcess::NotRunning);
    if (ui->pushButtonHamlibStart)
        ui->pushButtonHamlibStart->setEnabled(haveHamlib && !running);
    if (ui->pushButtonHamlibStop)
        ui->pushButtonHamlibStop->setEnabled(haveHamlib && running);

    // Auto-start is only meaningful if Hamlib integration is enabled AND a test succeeded.
    if (ui->checkBoxHamlibAutoStart)
        ui->checkBoxHamlibAutoStart->setEnabled(haveHamlib && m_hamlibLastTestOk);

    // Status text + help nudge
    if (!haveHamlib) {
        ui->labelHamlibStatus->setText(tr("Hamlib not found (set bin folder)"));

        // If bin path is empty, guide the user to Help automatically.
        if (ui->tabWidgetHamlib && ui->tabHamlibHelp && ui->lineEditHamlibBinPath
            && ui->lineEditHamlibBinPath->text().trimmed().isEmpty()) {
            ui->tabWidgetHamlib->setCurrentWidget(ui->tabHamlibHelp);
        }
    } else {
        if (running) {
            ui->labelHamlibStatus->setText(tr("rigctld running"));
        } else if (enabled) {
            ui->labelHamlibStatus->setText(tr("Hamlib OK"));
        } else {
            ui->labelHamlibStatus->setText(tr("Hamlib OK (disabled)"));
        }
    }
}

bool DialogSetup::startRigctldForSerial(QString *errorOut)
{
    const QString rigctld = hamlibExePath("rigctld.exe");
    if (rigctld.isEmpty()) {
        if (errorOut)
            *errorOut = tr("rigctld.exe not found. Set Hamlib bin folder or add to PATH.");
        return false;
    }

    const int rigId = ui->comboBoxHamlibRigModel->currentData().toInt();
    if (rigId < 0) {
        if (errorOut)
            *errorOut = tr("Select a rig model first (Rig list).");
        return false;
    }

    const QString port = ui->comboBoxHamlibSerialPort->currentText().trimmed();
    if (port.isEmpty()) {
        if (errorOut)
            *errorOut = tr("Select a serial port (COMx).");
        return false;
    }

    const int baud = ui->comboBoxHamlibSerialBaud->currentText().toInt();
    const int tcpPort = ui->spinBoxHamlibRigctldPort->value();

    // If already running, don't start another
    if (m_rigctldProcess && m_rigctldProcess->state() != QProcess::NotRunning) {
        return true;
    }

    if (!m_rigctldProcess) {
        m_rigctldProcess = new QProcess(this);
        connect(m_rigctldProcess, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            updateHamlibUiState();
        });
    }

    QStringList args;
    args << "-m" << QString::number(rigId) << "-r" << port << "-s" << QString::number(baud) << "-t"
         << QString::number(tcpPort);

    m_rigctldStartedByDialog = true;
    m_rigctldProcess->start(rigctld, args);

    if (!m_rigctldProcess->waitForStarted(1200)) {
        if (errorOut)
            *errorOut = tr("Failed to start rigctld: %1").arg(m_rigctldProcess->errorString());
        return false;
    }

    // Let rigctld bind/open port
    QThread::msleep(150);

    updateHamlibUiState();
    return true;
}

void DialogSetup::stopRigctld()
{
    if (!m_rigctldProcess)
        return;

    if (m_rigctldProcess->state() != QProcess::NotRunning) {
        m_rigctldProcess->terminate();
        if (!m_rigctldProcess->waitForFinished(500)) {
            m_rigctldProcess->kill();
            m_rigctldProcess->waitForFinished(500);
        }
    }
    m_rigctldStartedByDialog = false;
    updateHamlibUiState();
}

void DialogSetup::on_pushButtonHamlibRefreshRigList_clicked()
{
    refreshHamlibRigList();
}

void DialogSetup::on_pushButtonHamlibBrowseBin_clicked()
{
    const QString start = ui->lineEditHamlibBinPath->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(this,
                                                          tr("Select Hamlib bin folder"),
                                                          start);
    if (!dir.isEmpty()) {
        ui->lineEditHamlibBinPath->setText(QDir::toNativeSeparators(dir));
        refreshHamlibRigList();
        updateHamlibUiState();
    }
}

void DialogSetup::on_checkBoxHamlibEnabled_toggled(bool)
{
    updateHamlibUiState();
}

void DialogSetup::on_pushButtonHamlibStart_clicked()
{
    QString err;
    if (!startRigctldForSerial(&err)) {
        QMessageBox::warning(this, tr("Hamlib"), err);
        return;
    }
    updateHamlibUiState();
}

void DialogSetup::on_pushButtonHamlibStop_clicked()
{
    stopRigctld();
}

void DialogSetup::on_pushButtonHamlibTest_clicked()
{
    if (!ui->tabWidgetHamlib)
        return;

    auto persistHamlibOnly = [this]() {
        raypm::AppSettings s = raypm::SettingsManager::load();
        saveHamlibToSettings(s); // writes lastTestOk using m_hamlibLastTestOk
        raypm::SettingsManager::save(s);
        emit ReloadSettings(); // makes MainWindow re-read immediately
    };

    const int tabIdx = ui->tabWidgetHamlib->currentIndex(); // 0=Serial, 1=Network
    QString host;
    int port = 0;

    bool startedForThisTest = false;

    if (tabIdx == 0) {
        // Serial tab: we start/target local rigctld
        host = "127.0.0.1";
        port = ui->spinBoxHamlibRigctldPort->value();

        // Try connect; if it fails, attempt to start rigctld
        QByteArray reply;
        if (!tcpCommand(host, port, "f", &reply, nullptr)) {
            QString err;
            if (!startRigctldForSerial(&err)) {
                QMessageBox::warning(this, tr("Hamlib Test"), err);
                m_hamlibLastTestOk = false;
                ui->checkBoxHamlibAutoStart->setEnabled(false);
                ui->checkBoxHamlibAutoStart->setChecked(false);
                updateHamlibUiState();
                return;
            }
            startedForThisTest = true;
        }
    } else {
        // Network tab: user points to an existing rigctld
        host = ui->lineEditHamlibHost->text().trimmed();
        port = ui->spinBoxHamlibPort->value();
    }

    // Verify rigctld responds
    QByteArray reply;
    QString err;
    if (!tcpCommand(host, port, "f", &reply, &err)) {
        QMessageBox::warning(this, tr("Hamlib Test"), err);
        m_hamlibLastTestOk = false;
        ui->checkBoxHamlibAutoStart->setEnabled(false);
        ui->checkBoxHamlibAutoStart->setChecked(false);
        updateHamlibUiState();

        if (startedForThisTest && !ui->checkBoxHamlibAutoStart->isChecked()) {
            stopRigctld();
        }
        return;
    }

    // Success: unlock auto-start
    m_hamlibLastTestOk = true;
    ui->checkBoxHamlibAutoStart->setEnabled(true);

    // Show interactive test dialog like your screenshot
    const QString rigText = ui->comboBoxHamlibRigModel->currentText();
    HamlibTestDialog dlg(rigText, host, port, this);
    dlg.exec();

    // If we started rigctld only for testing, stop it unless user wants auto-start.
    if (startedForThisTest && !ui->checkBoxHamlibAutoStart->isChecked()) {
        stopRigctld();
    }

    m_hamlibLastTestOk = true;
    updateHamlibUiState();
    persistHamlibOnly();

    emit ShowStatusBarMessage(tr("Hamlib test OK. Settings updated."), 4000);

    updateHamlibUiState();
}
