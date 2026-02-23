#pragma once

#include <span>

#include <QAudioFormat>
#include <QNetworkAccessManager>
#include <QStandardPaths>

#include "qvw/WhisperEngine.h"
#include "Transcriber.h"

//#include "WhisperInstance.h"

class TranscriberWhisper final : public Transcriber
{
public:
    TranscriberWhisper(std::string name,
                       std::unique_ptr<Config> && config,
                       chunk_queue_t *queue,
                       const QString &filePath,
                       QAudioFormat format);

    ~TranscriberWhisper() override;

    ModelKind kind() const noexcept override {
        return ModelKind::WHISPER;
    }

    const std::string& finalText() const noexcept override {
        return final_text_;
    }

protected:
    bool createContextImpl() override;
    void processChunk(std::span<const uint8_t> data, bool lastChunk, bool forceProcess) override;
    bool processRecording(std::span<const float> data) override;
    bool stopImpl() override;

private:
    bool ensureModelOnDisk();           // check + download if needed
    bool downloadModelBlocking(const ModelInfo &model);

private:
    std::shared_ptr<qvw::WhisperSessionCtx> session_ctx_;

    size_t chunks_ = 0;

    int sample_rate_ = 16000; // Hz
    int max_live_latency_ms_ = 1500; // fallback trigger during continuous speech

    // Pending voiced PCM that has not yet been submitted to Whisper.
    std::vector<float> pending_pcm_;
    int64_t pending_samples_ = 0;

    // Transcript accumulation
    std::string final_text_;
};
