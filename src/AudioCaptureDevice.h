#pragma once

#include <chrono>
#include <span>

#include <QIODevice>

#include "AudioRingBuffer.h"

class AudioCaptureDevice : public QIODevice
{
    Q_OBJECT
public:
    explicit AudioCaptureDevice(AudioRingBuffer *ring, QObject *parent = nullptr);

    bool open(OpenMode mode) override;

    void close() override;

protected:
    qint64 readData(char *, qint64) override
    {
        assert(false);
        return 0;
    }

    qint64 writeData(const char *data, qint64 len) override;

signals:
    void recordingLevelUpdated(qreal level);

private:
    void prepareBuffer();
    void recalculateRecordingLevel(std::span<const qint16> samples);

    AudioRingBuffer *m_ring;
    unsigned int segment_ = 0;
    QByteArray audioBuffer_;
    std::chrono::steady_clock::time_point chunk_start_time_;
    std::once_flag first_write_flag_;
    qreal recording_level_{};
};
