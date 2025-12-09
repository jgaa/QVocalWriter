#pragma once

#include <span>

#include <QAudioFormat>
#include <QNetworkAccessManager>
#include <QStandardPaths>


#include "Transcriber.h"

#include "WhisperInstance.h"

struct TranscriptSegment {
    float   start_ms = 0.0f;
    float   end_ms   = 0.0f;
    QString text;
};


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

protected:
    bool createContextImpl() override;
    void processChunk(std::span<const uint8_t> data, bool lastChunk) override;
    void processRecording(std::span<const float> data) override;
    //bool init() override;

private:
    bool ensureModelOnDisk();           // check + download if needed
    bool downloadModelBlocking(const ModelInfo &model);

    void startSession();

private:
    whisper_context *w_ctx_{};
    std::shared_ptr<whisper_state> whisper_state_;
    size_t chunks_ = 0;

    // Audio parameters
    int sample_rate_ = 16000; // Hz
    int window_ms_ = 10000; // total buffer span, e.g. 10000 ms
    float overlap_fraction_ = 0.30F; // 0.0 .. 0.9
    int min_ms_before_process_ = 200; // minimum audio before first Whisper call

    // Derived / state
    std::vector<float> pcm_; // contiguous sliding window
    int pcm_fill_ = 0; // number of valid samples in m_pcm

    int64_t total_samples_ = 0; // global samples received
    int64_t last_processed_sample_ = 0; // last sample index we processed with Whisper
    float last_emitted_end_time_ms_ = 0.0f; // last segment end time we emitted

    // Transcript accumulation
    QString final_text_;
    float   stable_until_ms_ = 0.0f; // global time up to which text is stable

    // streaming config
    float unstable_margin_ms_ = 1500.0f; // last 1000 ms are "unstable" tail
    float last_seen_ms_ = 0.0f; // last time we saw in the audio

    QVector<TranscriptSegment> segments_;
};
