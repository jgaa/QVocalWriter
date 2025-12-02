#pragma once

#include <atomic>
#include <thread>
#include <span>

#include <QObject>
#include <QFile>
#include <QAudioFormat>
#include <QString>

class ChunkQueue;

class Transcriber : public QObject
{
    Q_OBJECT
public:
    using finished_t = std::function<void(bool ok)>;

    Transcriber(ChunkQueue *queue,
                const QString &filePath,
                QAudioFormat format);


    virtual ~Transcriber() = default;

    void start(finished_t finish = {});
    void stop();
    const auto& format() const noexcept { return format_; }

    virtual bool initialized() const noexcept = 0;

signals:
    void partialTextAvailable(const QString &text);
    void finalSegmentAvailable(const QString &text);
    void modelDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void modelReady(const QString &modelPath);
    void errorOccurred(const QString &message);
    void ready();

protected:
    virtual bool init() = 0;
    virtual void processChunk(std::span<const uint8_t> data, bool lastChunk = false) = 0;

private:
    void run();

    ChunkQueue      *queue_;
    QFile            file_;
    QAudioFormat     format_;
    std::optional<std::jthread>     thread_;
    std::atomic_bool stopped_{true};
    finished_t finished_fn_;
};
