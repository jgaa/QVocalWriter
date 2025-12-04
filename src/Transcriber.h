#pragma once

#include <atomic>
#include <thread>
#include <span>

#include <QObject>
#include <QFile>
#include <QAudioFormat>
#include <QString>

#include "Queue.h"

class Transcriber : public QObject
{
    Q_OBJECT
public:
    enum class State {
        CREATED,
        STARTED, // worker thread called run()
        INIT,
        READY,   // Initialized, transcribing is possible
        TRANSACRIBING,
        POST_TRANSCRIBING,
        STOPPING,
        STOPPED,
        ERROR
    };

    enum class CmdType {
        Prepare, // emits modelReady when done
        TranscribeChunks,
        PostTranscribe,
        Exit
    };

    using cmd_queue_t = Queue<CmdType>;

    Transcriber(chunk_queue_t *queue,
                const QString &filePath,
                QAudioFormat format);


    virtual ~Transcriber() = default;

    const auto& format() const noexcept { return format_; }

    virtual bool initialized() const noexcept = 0;

    void prepareModel() {
        enqueueCommand(CmdType::Prepare);
    }

    void startTranscribingChunks() {
        enqueueCommand(CmdType::TranscribeChunks);
    }

    void postTranscribe() {
        enqueueCommand(CmdType::PostTranscribe);
    }

    void stopTranscribing() {
        setState(State::STOPPING);
    }

    void stop();

    State state() const noexcept {
        return state_.load();
    }

signals:
    void partialTextAvailable(const QString &text);
    void finalTextAvailable(const QString &text);
    void modelDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void modelReady(const QString &modelPath);
    void errorOccurred(const QString &message);
    void stateChanged();


protected:
    void setState(State newState);

    void enqueueCommand(CmdType cmd) {
        cmdQueue_.push(cmd);
    }
    virtual bool init() = 0;
    virtual void processChunk(std::span<const uint8_t> data, bool lastChunk = false) = 0;
    virtual void processRecording(std::span<const float> data) = 0;

private:
    bool transcribeSegments();
    void processRecordingFromFile();
    void run();

    chunk_queue_t    *queue_;
    cmd_queue_t      cmdQueue_;
    QFile            file_;
    QAudioFormat     format_;
    std::optional<std::jthread>     thread_;
    bool result_{true};
    std::atomic<State> state_{State::CREATED};
};

std::ostream& operator<<(std::ostream &os, Transcriber::State state);

