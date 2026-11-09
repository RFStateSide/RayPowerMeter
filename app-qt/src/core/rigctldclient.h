// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QObject>
#include <QQueue>
#include <QStringList>

class QTcpSocket;
class QTimer;

namespace raypm {

class RigCtldClient : public QObject
{
    Q_OBJECT
public:
    using CommandId = quint64;

    explicit RigCtldClient(QObject *parent = nullptr);

    void connectToHost(const QString &host, int port);
    void disconnectFromHost();

    bool isConnected() const;

    // Sends a single rigctld command (without trailing newline).
    // Emits commandFinished(id, ok, lines, rprtCode) when done.
    CommandId sendCommand(const QString &cmd, int timeoutMs = 2500);

    // NEW:
    // Immediately fails the in-flight command (if any) and clears any queued commands.
    // This prevents "late TX" where commands enqueued while disconnected get pumped later.
    void cancelAll(const QString &reason = QStringLiteral("Cancelled"));

signals:
    void connectedChanged(bool connected);
    void errorOccurred(const QString &message);

    void commandFinished(raypm::RigCtldClient::CommandId id,
                         bool ok,
                         const QStringList &lines,
                         int rprtCode);

private slots:
    void onReadyRead();
    void onCommandTimeout();
    void onResponseIdle();

private:
    struct PendingCmd
    {
        CommandId id = 0;
        QString cmd;
        QStringList lines; // all non-RPRT lines
        int timeoutMs = 2500;
    };

    void pumpQueue();
    void failCurrent(const QString &why);

    QTcpSocket *m_sock = nullptr;
    QTimer *m_cmdTimer = nullptr;

    QByteArray m_rxBuf;

    QQueue<PendingCmd> m_queue;
    bool m_busy = false;

    PendingCmd m_current;
    CommandId m_nextId = 1;

    QTimer *m_idleTimer = nullptr; // ends replies that don't include an RPRT line
};

} // namespace raypm
