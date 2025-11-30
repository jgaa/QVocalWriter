#include <memory>

#include "AudioRecorder.h"
#include "AudioRingBuffer.h"

#include "logging.h"

using namespace std;


AudioRecorder::AudioRecorder(const QAudioDevice &device, QObject *parent)
    : QObject(parent)
    , device_(device)
    , format_(createWhisperFormat(device))
    , audioSource_(new QAudioSource(device_, format_, this))
    , ringBuffer_(make_unique<AudioRingBuffer>())
    , captureDevice_(make_unique<AudioCaptureDevice>(ringBuffer_.get()))
{}

void AudioRecorder::start()
{
    LOG_DEBUG_N << "Starting audio recorder";
    if (isRunning()) {
        LOG_DEBUG_N << "Audio recorder already running";
        return;
    }

    setState(State::STARTED);

    captureDevice_->open(QIODevice::WriteOnly);
    audioSource_->start(captureDevice_.get());  // push mode

    emit started();
}

void AudioRecorder::stop()
{
    LOG_DEBUG_N << "Stopping audio recorder";
    if (!isRunning()) {
        LOG_DEBUG_N << "Audio recorder not running";
        return;
    }

    setState(State::STOPPED);
    audioSource_->stop();
    captureDevice_->close();
    ringBuffer_->stop();   // unblock consumer threads

    emit stopped();
}

QAudioFormat AudioRecorder::createWhisperFormat(const QAudioDevice &device)
{
    QAudioFormat format;
    format.setSampleRate(16000);                 // or 16000/24000/48000
    format.setChannelCount(1);                   // mono
    format.setSampleFormat(QAudioFormat::Int16); // 16-bit signed PCM

    if (!device.isFormatSupported(format)) {
        LOG_WARN_N << "Requested format not supported, using nearest";
        format = device.preferredFormat();
    }
    return format;
}

void AudioRecorder::setState(State state)
{
    if (state_ != state) {
        LOG_DEBUG_N << "AudioRecorder state changed from "
                     << (state_ == State::STARTED ? "STARTED" : "STOPPED")
                     << " to "
                     << (state == State::STARTED ? "STARTED" : "STOPPED");
        state_ = state;
    }
}
