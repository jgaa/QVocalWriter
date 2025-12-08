#pragma once

#include <atomic>
#include <thread>
#include <span>

#include <QObject>
#include <QFile>
#include <QAudioFormat>
#include <QString>
#include <QPromise>

#include <qcoro/core/qcorofuture.h>
#include "Model.h"

class Transcriber : public Model
{
    Q_OBJECT
public:
    Transcriber(std::unique_ptr<Config> &&config,
                chunk_queue_t *queue,
                const QString &pcmFilePath,
                QAudioFormat format);

    virtual ~Transcriber() = default;

    const auto& format() const noexcept { return format_; }

    QCoro::Task<bool> transcribeChunks();
    QCoro::Task<bool> transcribeRecording();

    void stopTranscribing();

    const std::string& language() const noexcept;

protected:
    virtual void processChunk(std::span<const uint8_t> data, bool lastChunk = false) = 0;
    virtual void processRecording(std::span<const float> data) = 0;

private:
    bool transcribeSegments();
    void processRecordingFromFile();

    std::string      language_;
    chunk_queue_t    *queue_;
    QFile            file_;
    QAudioFormat     format_;
    std::optional<QPromise<bool>> promise_;
};


