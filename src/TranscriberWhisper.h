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
    void processChunk(const QByteArray &data) override;
    bool init() override;

private:
    bool ensureModelOnDisk();           // check + download if needed
    bool downloadModelBlocking(const ModelInfo &model);
    bool loadModelContext();            // whisper_init_from_file()

    QString resolveModelPath() const;
    std::optional<ModelInfo> currentModelInfo() const;

private:
    QString                 modelId_   = QStringLiteral("base.en");
    QString                 language_  = QStringLiteral("auto");
    QString                 modelDir_;
    whisper_context*        ctx_{};
    bool                    initialized_{false};

    // For network download (runs on the main thread; we’ll use a blocking
    // helper in ensureModelOnDisk() via a local QEventLoop).
    QNetworkAccessManager   *nam_      = nullptr;
};
