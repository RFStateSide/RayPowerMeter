// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QObject>
#include <QScopedPointer>
#include <QVector>

class QAudioSink;
class QMediaDevices;
class QIODevice;

class ToneGenerator : public QObject
{
    Q_OBJECT
public:
    explicit ToneGenerator(QObject *parent = nullptr);
    ~ToneGenerator() override;

    // Device selection
    QVector<QAudioDevice> outputDevices() const;
    QAudioDevice defaultOutputDevice() const;
    QAudioDevice currentOutputDevice() const { return m_device; }

    // You can store/restore this from your AppSettings by description or id string.
    bool setOutputDeviceByDescription(const QString &description);
    void setOutputDevice(const QAudioDevice &device);

    // Tone config
    void setFrequenciesHz(const QVector<double> &freqs); // size 1 or 2 typical
    QVector<double> frequenciesHz() const { return m_freqs; }

    void setAmplitude(double a01); // 0..1
    double amplitude() const { return m_amp; }

    void setSampleRate(int sr);
    int sampleRate() const { return m_sampleRate; }

    // Run
    bool start();
    void stop();
    bool isRunning() const;

signals:
    void devicesChanged();
    void started();
    void stopped();

private:
    void rebuildAudio();

private:
    QAudioDevice m_device;
    QAudioFormat m_format;

    int m_sampleRate = 48000;
    QVector<double> m_freqs{1000.0}; // default 1 kHz
    double m_amp = 0.25;

    QScopedPointer<QIODevice> m_source;
    QScopedPointer<QAudioSink> m_sink;

    QScopedPointer<QMediaDevices> m_devices;
};
