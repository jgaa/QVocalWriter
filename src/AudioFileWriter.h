#pragma once

#include <thread>

#include <QFile>

#include "AudioRingBuffer.h"
#include "ChunkQueue.h"

class AudioFileWriter
{
public:
    AudioFileWriter(AudioRingBuffer *ring,
                    ChunkQueue *chunkQueue,
                    const QString &filePath);

    ~AudioFileWriter();

    void stop();

private:
    void run();

    AudioRingBuffer *ring_;
    ChunkQueue      *chunkQueue_;
    QFile            file_;
    std::jthread     thread_;
    std::atomic_bool stopped_{false};
};
