#include <QDir>
#include <QNetworkReply>
#include <QEventLoop>

#include <whisper.h>

#include "TranscriberWhisper.h"

#include "logging.h"

TranscriberWhisper::TranscriberWhisper(ChunkQueue *queue, const QString &filePath, QAudioFormat format)
: Transcriber(queue, filePath, format)
{
}

TranscriberWhisper::~TranscriberWhisper()
{
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
}

QVector<TranscriberWhisper::ModelInfo> TranscriberWhisper::builtinModels()
{
    using MI = TranscriberWhisper::ModelInfo;
    return {
        { "tiny.en",  "ggml-tiny.en.bin",  75ll * 1024 * 1024 },
        { "base.en",  "ggml-base.en.bin", 142ll * 1024 * 1024 },
        { "small.en", "ggml-small.en.bin",466ll * 1024 * 1024 },
        { "medium.en","ggml-medium.en.bin",1'478ll * 1024 * 1024 },
        { "large-v2","ggml-large-v2.bin", 4'862ll * 1024 * 1024 },
    };
}

QString TranscriberWhisper::resolveModelPath() const
{
    const QString baseDir = !modelDir_.isEmpty()
        ? modelDir_
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/whisper-models");

    QDir().mkpath(baseDir);

    const auto infoOpt = currentModelInfo();
    const QString fileName = infoOpt ? infoOpt->filename
                                     : QStringLiteral("ggml-%1.bin").arg(modelId_);
    return baseDir + QLatin1Char('/') + fileName;
}

std::optional<TranscriberWhisper::ModelInfo> TranscriberWhisper::currentModelInfo() const
{
    const auto models = builtinModels();
    for (const auto &m : models) {
        if (m.id == modelId_)
            return m;
    }
    return std::nullopt;
}

bool TranscriberWhisper::ensureModelOnDisk()
{
    const QString modelPath = resolveModelPath();
    QFile f(modelPath);
    if (f.exists() && f.size() > 0)
        return true;

    const auto infoOpt = currentModelInfo();
    if (!infoOpt) {
        emit errorOccurred(tr("Unknown Whisper model id: %1").arg(modelId_));
        return false;
    }

    return downloadModelBlocking(*infoOpt);
}

bool TranscriberWhisper::downloadModelBlocking(const ModelInfo &model)
{
    if (!nam_) {
        nam_ = new QNetworkAccessManager(this);
    }

    // Hugging Face raw download URL pattern:
    // https://huggingface.co/ggerganov/whisper.cpp/resolve/main/<filename>
    const QUrl url(
        QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/%1")
            .arg(model.filename)
        );

    QNetworkRequest req(url);
    auto reply = nam_->get(req);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::downloadProgress,
                     [this] (qint64 bytesReceived, qint64 bytesTotal) {

        LOG_TRACE_N << "Model download progress: "
                    << bytesReceived << " / " << bytesTotal;

        emit TranscriberWhisper::modelDownloadProgress(bytesReceived, bytesTotal);
    });

    QObject::connect(reply, &QNetworkReply::finished,
                     &loop, &QEventLoop::quit);

    loop.exec(); // blocks this thread until finished

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(tr("Failed to download model: %1")
                               .arg(reply->errorString()));
        return false;
    }

    const QByteArray data = reply->readAll();
    const QString modelPath = resolveModelPath();
    QFile out(modelPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(tr("Failed to save model to %1").arg(modelPath));
        return false;
    }
    out.write(data);
    out.close();

    emit modelReady(modelPath);
    return true;
}

void TranscriberWhisper::setModelId(const QString &id)
{
    modelId_ = id;
}

void TranscriberWhisper::setLanguage(const QString &lang)
{
    language_ = lang;
}

void TranscriberWhisper::setModelDirectory(const QString &dir)
{
    modelDir_ = dir;
}

bool TranscriberWhisper::init()
{
    if (initialized_)
        return true;

    if (!ensureModelOnDisk())
        return false;

    if (!loadModelContext())
        return false;

    initialized_ = true;
    return true;
}

bool TranscriberWhisper::loadModelContext()
{
    const QString modelPath = resolveModelPath();
    whisper_context_params cparams = whisper_context_default_params();

    // Optionally modify defaults:
    cparams.use_gpu = false;          // TODO: Make optional later
    cparams.flash_attn = false;       // TODO: Make optional later (require gpu)
    cparams.gpu_device = 0;           // ignored for CPU mode

    // DTW features are advanced; keep disabled for now
    cparams.dtw_token_timestamps = false;
    cparams.dtw_aheads_preset = WHISPER_AHEADS_NONE;
    cparams.dtw_n_top = 0;            // default means let whisper choose

    ctx_ = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), cparams);

    if (!ctx_) {
        emit errorOccurred(tr("Failed to load Whisper model from %1").arg(modelPath));
        return false;
    }

    return true;
}

void TranscriberWhisper::processChunk(const QByteArray &data)
{
    assert(ctx_);
    assert(initialized());

    if (!initialized() || !ctx_) {
        return;
    }

    assert(format().sampleFormat() == QAudioFormat::Int16);
    const auto *samplesI16 = reinterpret_cast<const int16_t*>(data.constData());
    const int nSamples = data.size() / sizeof(int16_t);

    std::vector<float> pcm;
    pcm.reserve(nSamples);
    for (int i = 0; i < nSamples; ++i) {
        pcm.push_back(samplesI16[i] / 32768.0f);
    }

    // TODO: you can keep a rolling buffer and only call whisper_full()
    // once you have enough samples for e.g. 5-10 seconds. For now: naive.

    auto params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_progress   = false;
    params.print_realtime   = false;
    params.print_timestamps = true;

    QByteArray langUtf8 = language_.toUtf8();
    params.language      = langUtf8.constData(); // "en", "auto", etc.

    // Blocking call on this worker thread:
    const int rc = whisper_full(ctx_, params, pcm.data(), pcm.size());
    if (rc != 0) {
        emit errorOccurred(tr("whisper_full() failed with code %1").arg(rc));
        return;
    }

    // Collect all segment texts
    const int nSegments = whisper_full_n_segments(ctx_);
    QString combined;
    for (int i = 0; i < nSegments; ++i) {
        const char *txt = whisper_full_get_segment_text(ctx_, i);
        if (!txt)
            continue;
        combined += QString::fromUtf8(txt);
    }

    if (!combined.isEmpty()) {
        emit partialTextAvailable(combined);
        // If you want to treat each chunk as "final", also emit:
       // emit finalSegmentAvailable(combined);
    }
}
