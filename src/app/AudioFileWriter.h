#pragma once

#include <thread>

#include <QFile>

#include "AudioRingBuffer.h"
#include "Queue.h"

class AudioFileWriter
{
public:
    AudioFileWriter(AudioRingBuffer *ring,
                    chunk_queue_t *chunkQueue,
                    const QString &filePath);

    ~AudioFileWriter();

    void stop();

private:
    void run();

    AudioRingBuffer *ring_{};
    chunk_queue_t *chunkQueue_{};
    QFile            file_;
    std::jthread     thread_;
    std::atomic_bool stopped_{false};
};
