#pragma once

#include <QObject>
#include <QAudioSource>
#include <QAudioFormat>
#include <QByteArray>

#include "AudioCaptureDevice.h"

constexpr int AUDIO_BUFFER_SIZE = 1024 * 16;  // 32 KB buffer;

class AudioRecorder : public QObject
{
    Q_OBJECT

    enum class State {
        STARTED,
        STOPPED
    };

public:
    explicit AudioRecorder(const QAudioDevice &device, QObject *parent = nullptr);

    QAudioFormat format() const { return format_; }

    void start();
    void stop();

    State state() const noexcept { return state_;}
    bool isRunning() const noexcept { return state_ == State::STARTED;}

    auto * ringBuffer() const noexcept { return ringBuffer_.get(); }

signals:
    void started();
    void stopped();

private:
    QAudioFormat createWhisperFormat(const QAudioDevice &device);
    void setState(State state);


    QAudioDevice  device_;
    QAudioFormat  format_;
    QAudioSource *audioSource_ = nullptr;
    std::unique_ptr<AudioRingBuffer> ringBuffer_;
    std::unique_ptr<AudioCaptureDevice> captureDevice_;
    State state_{State::STOPPED};
};
