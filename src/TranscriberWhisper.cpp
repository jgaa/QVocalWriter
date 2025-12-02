#include <QDir>
#include <QNetworkReply>
#include <QEventLoop>

#include <whisper.h>
#include <string_view>

#include "TranscriberWhisper.h"

#include "logging.h"

using namespace std;

namespace {

/*
 *     enum ggml_log_level {
        GGML_LOG_LEVEL_NONE  = 0,
        GGML_LOG_LEVEL_DEBUG = 1,
        GGML_LOG_LEVEL_INFO  = 2,
        GGML_LOG_LEVEL_WARN  = 3,
        GGML_LOG_LEVEL_ERROR = 4,
        GGML_LOG_LEVEL_CONT  = 5, // continue previous log
    };
*/

void whisperLogger(ggml_log_level level, const char *msg, void *user_data) {
    string_view message(msg);
    message = message.substr(0, message.empty() ? 0 : message.size() -1);

    switch(level) {
        case GGML_LOG_LEVEL_ERROR:
            LOG_ERROR << "[whisper] " << message;
            break;
        case GGML_LOG_LEVEL_WARN:
            LOG_WARN << "[whisper] " << message;
            break;
        case GGML_LOG_LEVEL_INFO:
            LOG_INFO << "[whisper] " << message;
            break;
        case GGML_LOG_LEVEL_DEBUG:
            LOG_DEBUG << "[whisper] " << message;
            break;
        case GGML_LOG_LEVEL_CONT:
            LOG_TRACE << "[whisper] " << message;
            break;
        case GGML_LOG_LEVEL_NONE:
            // no logging
            break;
    }
}
} // anon ns

TranscriberWhisper::TranscriberWhisper(ChunkQueue *queue, const QString &filePath, QAudioFormat format)
: Transcriber(queue, filePath, format)
{
    whisper_log_set(whisperLogger, nullptr);
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

    startSession();

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

void TranscriberWhisper::startSession()
{
    const int windowSamples = (window_ms_ * sample_rate_) / 1000;
    pcm_.assign(windowSamples, 0.0f);
    pcm_fill_ = 0;

    total_samples_         = 0;
    last_processed_sample_  = 0;
    last_emitted_end_time_ms_ = 0.0f;

    LOG_DEBUG_N << "TranscriberWhisper: started session with window "
                 << window_ms_ << " ms ("
                 << windowSamples << " samples)";
}

void TranscriberWhisper::processChunk(std::span<const uint8_t> data, bool lastChunk)
{
    if (!ctx_)
        return;

    // LOG_TRACE_N << "TranscriberWhisper::processChunk #" << ++chunks_ << " called with data size ="
    //             << data.size() << " lastChunk =" << lastChunk;

    // --- 1) Derive window size in samples ---------------------------------
    const int windowSamples = (window_ms_ * sample_rate_) / 1000;
    if (windowSamples <= 0)
        return;

    // If settings changed mid-session, re-init buffer
    if (static_cast<int>(pcm_.size()) != windowSamples) {
        pcm_.assign(windowSamples, 0.0f);
        pcm_fill_             = 0;
        total_samples_        = 0;
        last_processed_sample_ = 0;
        last_emitted_end_time_ms_ = 0.0f;
    }

    // --- 2) Append new samples (if any) with sliding window via memmove ---

    const int16_t *samplesI16 = nullptr;
    int newSamples = 0;

    if (!data.empty()) {
        samplesI16 = reinterpret_cast<const int16_t*>(data.data());
        newSamples = static_cast<int>(data.size() / sizeof(int16_t));

        if (newSamples > 0) {
            const int totalNeeded = pcm_fill_ + newSamples;

            if (totalNeeded > windowSamples) {
                const int overflow = totalNeeded - windowSamples;

                if (overflow >= pcm_fill_) {
                    // we overflowed past everything we had -> drop all
                    pcm_fill_ = 0;
                } else {
                    // slide existing data left by 'overflow' samples
                    std::memmove(
                        pcm_.data(),
                        pcm_.data() + overflow,
                        (pcm_fill_ - overflow) * sizeof(float)
                        );
                    pcm_fill_ -= overflow;
                }
            }

            // now there is space for newSamples
            for (int i = 0; i < newSamples; ++i) {
                pcm_[pcm_fill_++] = samplesI16[i] / 32768.0f;
            }

            total_samples_ += newSamples;
        }
    }

    // At this point:
    //  - m_pcm[0 .. m_pcmFill) contains the latest audio (up to windowSamples)
    //  - m_totalSamples is updated with all samples we've seen this session

    // If there's no audio at all, nothing to do (even for lastChunk)
    if (pcm_fill_ <= 0)
        return;

    // --- 3) Decide whether to run Whisper now -----------------------------

    // Minimum audio before first/regular calls (unless lastChunk)
    const int64_t minSamplesBeforeProcess =
        (static_cast<int64_t>(min_ms_before_process_) * sample_rate_) / 1000;

    // Overlap fraction controls how often we call Whisper (step size)
    const float overlapClamped = std::clamp(overlap_fraction_, 0.0f, 0.9f);
    const int64_t stepSamples =
        static_cast<int64_t>(windowSamples * (1.0f - overlapClamped));

    bool shouldRun = false;

    if (lastChunk) {
        // On final call: always flush pending audio
        // (even if we didn’t reach the 'step' yet)
        shouldRun = true;
    } else {
        // Not the last chunk: enforce minMs + step
        if (total_samples_ >= minSamplesBeforeProcess) {
            const int64_t sinceLastProcessed = total_samples_ - last_processed_sample_;
            if (sinceLastProcessed >= stepSamples) {
                shouldRun = true;
            }
        }
    }

    if (!shouldRun)
        return;

    // --- 4) Prepare Whisper parameters -----------------------------------

    auto params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    // Threads
    const unsigned hwThreads = std::max(1u, std::thread::hardware_concurrency());
    params.n_threads = std::min<unsigned>(hwThreads, 48u);  // tune as needed

    params.print_progress   = false;
    params.print_realtime   = false;
    params.print_timestamps = true;
    params.offset_ms = 0;  // or leave default

    const QByteArray langUtf8 = language_.toUtf8();
    params.language = langUtf8.isEmpty() ? nullptr : langUtf8.constData();

    params.no_context     = false;
    params.single_segment = false;

    if (lastChunk) {
        // Let Whisper be a bit more thorough for the final pass.
        params.max_len          = 0;     // no token limit
        params.token_timestamps = true;
    }

    // --- 5) Run Whisper on the current sliding buffer --------------------

    LOG_TRACE_N << "Calling whisper_full() with "
                 << pcm_fill_ << " samples ("
                 << (pcm_fill_ * 1000 / sample_rate_) << " ms), "
                 << "offset_ms=" << params.offset_ms;
    const auto when = std::chrono::steady_clock::now();
    const int rc = whisper_full(
        ctx_,
        params,
        pcm_.data(),
        pcm_fill_
        );

    const auto duration = std::chrono::steady_clock::now() - when;
    LOG_DEBUG_N << "whisper_full() returned rc =" << rc
                << " in " << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()
                << " ms";

    last_processed_sample_ = total_samples_;

    if (rc != 0) {
        // Optional: log via your logger
        return;
    }

    const int64_t globalStartSamples =
        std::max<int64_t>(0, total_samples_ - pcm_fill_);
    const float globalStartMs =
        (globalStartSamples * 1000.0f) / sample_rate_;

    const int nSegments = whisper_full_n_segments(ctx_);

    QString unstableTail;      // this call's "tail" for UI
    float   newestSegEndMs = m_stableUntilMs;

    QString thisBatch;
    for (int i = 0; i < nSegments; ++i) {
        const float segLocalStartMs = whisper_full_get_segment_t0(ctx_, i);
        const float segLocalEndMs   = whisper_full_get_segment_t1(ctx_, i);

        const float segGlobalStartMs = globalStartMs + segLocalStartMs;
        const float segGlobalEndMs   = globalStartMs + segLocalEndMs;

        const char *segTextC = whisper_full_get_segment_text(ctx_, i);
        if (!segTextC || !*segTextC)
            continue;

        const QString segText = QString::fromUtf8(segTextC);
        thisBatch += segText;

        newestSegEndMs = std::max(newestSegEndMs, segGlobalEndMs);

        // Already fully committed → skip
        if (segGlobalEndMs <= m_stableUntilMs + 1.0f)
            continue;

        const float liveEdgeMs = newestSegEndMs;
        const float thresholdStableMs = liveEdgeMs - m_unstableMarginMs;

        if (segGlobalEndMs < thresholdStableMs) {
            // This segment is safely in the "past" → commit permanently
            m_stableTranscript += segText;
            m_stableUntilMs = segGlobalEndMs;
        } else {
            // Still close to the live edge → unstable tail
            unstableTail += segText;
        }
    }

    if (!thisBatch.isEmpty()) {
        LOG_DEBUG_N << "Emitting partial text: " << thisBatch;
        emit partialTextAvailable(thisBatch);
    }

    if (lastChunk) {
        // After the loop above, just treat the remaining unstable tail as final
        m_stableTranscript += unstableTail;
        m_stableUntilMs = newestSegEndMs;
        unstableTail.clear();

        LOG_DEBUG_N << "Final text: " << m_stableTranscript;
    }
}
