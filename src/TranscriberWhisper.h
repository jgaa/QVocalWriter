#pragma once

#include <span>

#include <QAudioFormat>
#include <QNetworkAccessManager>
#include <QStandardPaths>


#include "Transcriber.h"

struct whisper_context; // from whisper.cpp (forward-declare)

struct TranscriptSegment {
    float   start_ms = 0.0f;
    float   end_ms   = 0.0f;
    QString text;
};


class TranscriberWhisper final : public Transcriber
{
public:
    struct ModelInfo {
        enum Quatization {
            Q4_0,
            Q4_1,
            Q5_0,
            Q5_1,
            Q8_0,
            FP16,
            FP32,
        };
        std::string_view id;
        std::string_view filename;
        Quatization quantization{};
        size_t size_mb{};   // approximate in megabytes
        std::string_view sha;
    };

    TranscriberWhisper(chunk_queue_t *queue,
                       const QString &filePath,
                       QAudioFormat format);

    ~TranscriberWhisper() override;

    void setModelId(const QString &id);      // must be called before start()
    void setLanguage(const QString &lang);   // "en", "auto", "nb", etc.

    void setModelDirectory(const QString &dir);

    static std::span<const ModelInfo> builtinModels() noexcept;

    bool initialized() const noexcept override { return initialized_; }


protected:
    void processChunk(std::span<const uint8_t> data, bool lastChunk) override;
    void processRecording(std::span<const float> data) override;
    bool init() override;

private:
    bool ensureModelOnDisk();           // check + download if needed
    bool downloadModelBlocking(const ModelInfo &model);
    bool loadModelContext();            // whisper_init_from_file()

    void startSession();


    QString resolveModelPath() const;
    std::optional<ModelInfo> currentModelInfo() const;

private:
    QString modelId_ = QStringLiteral("base.en");
    QString language_ = QStringLiteral("auto");
    QString modelDir_;
    whisper_context* ctx_{};
    bool initialized_{false};
    size_t chunks_ = 0;

    // For network download (runs on the main thread; we’ll use a blocking
    // helper in ensureModelOnDisk() via a local QEventLoop).
    QNetworkAccessManager *nam_ = nullptr;

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
