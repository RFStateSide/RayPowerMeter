// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "rigctldclient.h"

#include <QTcpSocket>
#include <QTimer>

namespace raypm {

static bool parseRprtLine(const QString &line, int *outCode)
{
    // rigctld typically ends each response with: "RPRT 0"
    if (!line.startsWith(QStringLiteral("RPRT")))
        return false;

    const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return false;

    bool ok = false;
    const int code = parts[1].toInt(&ok);
    if (!ok)
        return false;

    if (outCode)
        *outCode = code;
    return true;
}

RigCtldClient::RigCtldClient(QObject *parent)
    : QObject(parent)
{
    m_sock = new QTcpSocket(this);
    m_cmdTimer = new QTimer(this);
    m_cmdTimer->setSingleShot(true);

    connect(m_sock, &QTcpSocket::connected, this, [this]() {
        emit connectedChanged(true);
        pumpQueue();
    });
    connect(m_sock, &QTcpSocket::disconnected, this, [this]() {
        emit connectedChanged(false);

        // If we disconnect mid-command, fail it and clear the queue
        if (m_busy) {
            failCurrent(QStringLiteral("Disconnected"));
        }
        while (!m_queue.isEmpty()) {
            const auto cmd = m_queue.dequeue();
            emit commandFinished(cmd.id, false, cmd.lines, -1);
        }
    });
    connect(m_sock, &QTcpSocket::readyRead, this, &RigCtldClient::onReadyRead);
    connect(m_sock, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        const QString err = m_sock->errorString();
        emit errorOccurred(err);

        // IMPORTANT:
        // If the socket errors while we have pending commands, callers must not hang.
        // This commonly happens when rigctld isn't running ("Connection refused").
        if (m_busy) {
            failCurrent(err);
        }

        // Fail anything still queued. This also covers the case where connectToHost fails
        // before we ever become "busy".
        while (!m_queue.isEmpty()) {
            const auto cmd = m_queue.dequeue();
            QStringList lines = cmd.lines;
            lines.append(QStringLiteral("[ERROR] %1").arg(err));
            emit commandFinished(cmd.id, false, lines, -1);
        }
    });

    connect(m_cmdTimer, &QTimer::timeout, this, &RigCtldClient::onCommandTimeout);

    // Make RigCtldClient complete replies that don’t include RPRT
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(40);
    connect(m_idleTimer, &QTimer::timeout, this, &RigCtldClient::onResponseIdle);
}

void RigCtldClient::connectToHost(const QString &host, int port)
{
    if (m_sock->state() == QAbstractSocket::ConnectedState)
        return;
    if (m_sock->state() == QAbstractSocket::ConnectingState)
        return;

    m_sock->connectToHost(host, quint16(port));
}

void RigCtldClient::disconnectFromHost()
{
    m_sock->disconnectFromHost();
}

bool RigCtldClient::isConnected() const
{
    return m_sock->state() == QAbstractSocket::ConnectedState;
}

RigCtldClient::CommandId RigCtldClient::sendCommand(const QString &cmd, int timeoutMs)
{
    PendingCmd pc;
    pc.id = m_nextId++;
    pc.cmd = cmd.trimmed();
    pc.timeoutMs = timeoutMs;

    //qDebug() << "RIGCLTD CMD:" << pc.cmd;

    m_queue.enqueue(pc);
    pumpQueue();
    return pc.id;
}

void RigCtldClient::cancelAll(const QString &reason)
{
    // Fail in-flight command immediately
    if (m_busy) {
        failCurrent(reason);
    }

    // Fail everything queued
    while (!m_queue.isEmpty()) {
        const auto cmd = m_queue.dequeue();
        QStringList lines = cmd.lines;
        lines.append(QStringLiteral("[CANCELLED] %1").arg(reason));
        emit commandFinished(cmd.id, false, lines, -1);
    }

    // Also clear any buffered RX so it can't contaminate next command
    m_rxBuf.clear();
    if (m_sock)
        m_sock->readAll();

    // Stop timers
    if (m_cmdTimer)
        m_cmdTimer->stop();
    if (m_idleTimer)
        m_idleTimer->stop();
}

void RigCtldClient::pumpQueue()
{
    if (m_busy)
        return;
    if (!isConnected())
        return;
    if (m_queue.isEmpty())
        return;

    m_current = m_queue.dequeue();
    m_current.lines.clear();
    m_rxBuf.clear();

    m_busy = true;

    QByteArray out = m_current.cmd.toUtf8();
    out.append('\n');
    m_sock->write(out);
    m_sock->flush();

    m_cmdTimer->start(m_current.timeoutMs);

    m_idleTimer->stop();
}

void RigCtldClient::onReadyRead()
{
    // Discard data if no command is in flight
    // (prevents stray late lines from contaminating the next command)
    if (!m_busy) {
        m_sock->readAll();
        m_rxBuf.clear();
        return;
    }

    m_rxBuf.append(m_sock->readAll());

    // Process complete lines
    while (true) {
        const int nl = m_rxBuf.indexOf('\n');
        if (nl < 0)
            break;

        QByteArray lineBytes = m_rxBuf.left(nl);
        m_rxBuf.remove(0, nl + 1);

        QString line = QString::fromUtf8(lineBytes).trimmed();
        if (line.isEmpty())
            continue;

        int rprt = 0;
        if (parseRprtLine(line, &rprt)) {
            m_cmdTimer->stop();
            const bool ok = (rprt == 0);

            const auto done = m_current;
            m_busy = false;

            emit commandFinished(done.id, ok, done.lines, rprt);

            pumpQueue();
            return;
        }

        // Non-RPRT line
        m_current.lines.append(line);

        // End of reply when no RPRT terminator exists.
        // Restart idle timer each time we get a line.
        m_idleTimer->start();
    }
}

void RigCtldClient::onCommandTimeout()
{
    // If rigctld didn't send an RPRT line but we DID receive content, treat it
    // as a completed (best-effort) reply instead of a hard failure.
    if (m_busy && !m_current.lines.isEmpty()) {
        onResponseIdle();
        return;
    }

    failCurrent(QStringLiteral("Timeout"));
    pumpQueue();
}

void RigCtldClient::failCurrent(const QString &why)
{
    if (!m_busy)
        return;

    m_cmdTimer->stop();
    m_idleTimer->stop();

    const auto done = m_current;
    m_busy = false;

    // Put the reason into lines so callers can display it
    QStringList lines = done.lines;
    lines.append(QStringLiteral("[ERROR] %1").arg(why));

    emit commandFinished(done.id, false, lines, -1);
}

void RigCtldClient::onResponseIdle()
{
    if (!m_busy)
        return;

    m_cmdTimer->stop();
    m_idleTimer->stop();

    const auto done = m_current;
    m_busy = false;

    // No explicit RPRT was received; assume success when we have data.
    emit commandFinished(done.id, true, done.lines, 0);

    pumpQueue();
}

} // namespace raypm
