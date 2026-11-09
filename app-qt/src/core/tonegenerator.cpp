// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "core/tonegenerator.h"

#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QtMath>

// Simple streaming source that synthesizes 1-2 tone audio continuously
class ToneIODevice : public QIODevice
{
public:
    ToneIODevice(const QAudioFormat &fmt, QObject *parent = nullptr)
        : QIODevice(parent)
        , m_fmt(fmt)
    {
        m_channels = qMax(1, fmt.channelCount());
        m_sr = qMax(8000, fmt.sampleRate());
        m_bytesPerSample = fmt.bytesPerSample();
    }

    void setFrequenciesHz(const QVector<double> &f) { m_freqs = f; }
    void setAmplitude(double a01) { m_amp = qBound(0.0, a01, 1.0); }

    void start() { open(QIODevice::ReadOnly); }
    void stop() { close(); }

    qint64 readData(char *data, qint64 maxlen) override
    {
        if (!isOpen() || maxlen <= 0)
            return 0;

        // We only implement Int16 here (most rigs/soundcards fine with that).
        // If you need Float later, we can extend.
        if (m_fmt.sampleFormat() != QAudioFormat::Int16)
            return 0;

        const int bytesPerFrame = m_channels * int(sizeof(qint16));
        const qint64 frames = maxlen / bytesPerFrame;
        qint16 *out = reinterpret_cast<qint16 *>(data);

        const double twoPi = 2.0 * M_PI;

        for (qint64 i = 0; i < frames; ++i) {
            // Sum 1-2 sines, normalized to avoid clipping.
            double v = 0.0;
            const int n = qMax(1, m_freqs.size());
            for (int k = 0; k < m_freqs.size(); ++k) {
                const double f = m_freqs[k];
                const double phase = twoPi * f * (m_phase / double(m_sr));
                v += qSin(phase);
            }
            v /= double(n);
            v *= m_amp;

            const qint16 s = qint16(qBound(-1.0, v, 1.0) * 32767.0);

            for (int ch = 0; ch < m_channels; ++ch)
                *out++ = s;

            m_phase++;
        }

        return frames * bytesPerFrame;
    }

    qint64 writeData(const char *, qint64) override { return 0; }
    qint64 bytesAvailable() const override { return 4096 + QIODevice::bytesAvailable(); }

private:
    QAudioFormat m_fmt;
    QVector<double> m_freqs{1000.0};
    double m_amp = 0.25;

    int m_channels = 1;
    int m_sr = 48000;
    int m_bytesPerSample = 2;

    quint64 m_phase = 0;
};

ToneGenerator::ToneGenerator(QObject *parent)
    : QObject(parent)
    , m_devices(new QMediaDevices(this))
{
    connect(m_devices.data(),
            &QMediaDevices::audioOutputsChanged,
            this,
            &ToneGenerator::devicesChanged);

    m_device = QMediaDevices::defaultAudioOutput();

    m_format.setSampleRate(m_sampleRate);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
}

ToneGenerator::~ToneGenerator()
{
    stop();
}

QVector<QAudioDevice> ToneGenerator::outputDevices() const
{
    return m_devices ? m_devices->audioOutputs() : QVector<QAudioDevice>{};
}

QAudioDevice ToneGenerator::defaultOutputDevice() const
{
    return QMediaDevices::defaultAudioOutput();
}

bool ToneGenerator::setOutputDeviceByDescription(const QString &description)
{
    for (const auto &dev : outputDevices()) {
        if (dev.description() == description) {
            setOutputDevice(dev);
            return true;
        }
    }
    return false;
}

void ToneGenerator::setOutputDevice(const QAudioDevice &device)
{
    if (device == m_device)
        return;

    const bool wasRunning = isRunning();
    if (wasRunning)
        stop();

    m_device = device;

    if (wasRunning)
        start();
}

void ToneGenerator::setFrequenciesHz(const QVector<double> &freqs)
{
    QVector<double> f = freqs;
    if (f.isEmpty())
        f = {1000.0};
    if (f.size() > 2)
        f = f.mid(0, 2);

    m_freqs = f;

    if (auto *src = dynamic_cast<ToneIODevice *>(m_source.data())) {
        src->setFrequenciesHz(m_freqs);
    }
}

void ToneGenerator::setAmplitude(double a01)
{
    m_amp = qBound(0.0, a01, 1.0);

    if (auto *src = dynamic_cast<ToneIODevice *>(m_source.data())) {
        src->setAmplitude(m_amp);
    }
    if (m_sink)
        m_sink->setVolume(1.0); // amplitude is handled in generator
}

void ToneGenerator::setSampleRate(int sr)
{
    sr = qBound(8000, sr, 192000);
    if (sr == m_sampleRate)
        return;

    const bool wasRunning = isRunning();
    if (wasRunning)
        stop();

    m_sampleRate = sr;
    m_format.setSampleRate(m_sampleRate);

    if (wasRunning)
        start();
}

bool ToneGenerator::start()
{
    stop();

    if (!m_device.isNull()) {
        // If device can’t do Int16 mono at requested SR, Qt will still often handle it,
        // but safest is to use device preferred format as a base and force Int16.
        QAudioFormat fmt = m_device.preferredFormat();
        fmt.setSampleRate(m_sampleRate);
        fmt.setChannelCount(1);
        fmt.setSampleFormat(QAudioFormat::Int16);
        m_format = fmt;
    }

    auto *src = new ToneIODevice(m_format, this);
    src->setFrequenciesHz(m_freqs);
    src->setAmplitude(m_amp);
    src->start();

    m_source.reset(src);
    m_sink.reset(new QAudioSink(m_device, m_format));
    m_sink->setVolume(1.0);
    m_sink->start(m_source.data());

    emit started();
    return true;
}

void ToneGenerator::stop()
{
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
    if (m_source) {
        if (auto *src = dynamic_cast<ToneIODevice *>(m_source.data()))
            src->stop();
        m_source.reset();
    }

    emit stopped();
}

bool ToneGenerator::isRunning() const
{
    return (m_sink != nullptr);
}
