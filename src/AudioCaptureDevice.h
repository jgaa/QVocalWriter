#pragma once

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

private:
    AudioRingBuffer *m_ring;
};
