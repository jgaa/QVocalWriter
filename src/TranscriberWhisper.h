#pragma once

#include <QAudioFormat>
#include <QNetworkAccessManager>
#include <QStandardPaths>


#include "Transcriber.h"

struct whisper_context; // from whisper.cpp (forward-declare)

class TranscriberWhisper final : public Transcriber
{
public:
    struct ModelInfo {
        QString id;          // e.g. "base.en", "small", "tiny.en-q5_1"
        QString filename;    // e.g. "ggml-base.en.bin"
        qint64  sizeBytes{};   // approximate, optional
    };

    TranscriberWhisper(ChunkQueue *queue,
                       const QString &filePath,
                       QAudioFormat format);

    ~TranscriberWhisper() override;

    void setModelId(const QString &id);      // must be called before start()
    void setLanguage(const QString &lang);   // "en", "auto", "nb", etc.

    void setModelDirectory(const QString &dir);

    static QVector<ModelInfo> builtinModels();

    bool initialized() const noexcept override { return initialized_; }


protected:
    void processChunk(std::span<const uint8_t> data, bool lastChunk) override;
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
    float overlap_fraction_ = 0.15F; // 0.0 .. 0.9
    int min_ms_before_process_ = 1500; // minimum audio before first Whisper call

    // Derived / state
    std::vector<float> pcm_; // contiguous sliding window
    int pcm_fill_ = 0; // number of valid samples in m_pcm

    int64_t total_samples_ = 0; // global samples received
    int64_t last_processed_sample_ = 0; // last sample index we processed with Whisper
    float last_emitted_end_time_ms_ = 0.0f; // last segment end time we emitted
};
