#include <QDir>
#include <QNetworkReply>
#include <QEventLoop>

#include <whisper.h>
#include <string_view>

#include "TranscriberWhisper.h"

#include "logging.h"

using namespace std;

namespace {

using mi_t = TranscriberWhisper::ModelInfo;
constexpr auto all_models = std::to_array<mi_t>({
    // tiny
    { "tiny",               "ggml-tiny.bin",               mi_t::FP16,  75,  "bd577a113a864445d4c299885e0cb97d4ba92b5f" },
    { "tiny-q5_1",          "ggml-tiny-q5_1.bin",          mi_t::Q5_1,  31,  "2827a03e495b1ed3048ef28a6a4620537db4ee51" },
    { "tiny-q8_0",          "ggml-tiny-q8_0.bin",          mi_t::Q8_0,  42,  "19e8118f6652a650569f5a949d962154e01571d9" },

    // tiny.en
    { "tiny.en",            "ggml-tiny.en.bin",            mi_t::FP16,  75,  "c78c86eb1a8faa21b369bcd33207cc90d64ae9df" },
    { "tiny.en-q5_1",       "ggml-tiny.en-q5_1.bin",       mi_t::Q5_1,  31,  "3fb92ec865cbbc769f08137f22470d6b66e071b6" },
    { "tiny.en-q8_0",       "ggml-tiny.en-q8_0.bin",       mi_t::Q8_0,  42,  "802d6668e7d411123e672abe4cb6c18f12306abb" },

    // base
    { "base",               "ggml-base.bin",               mi_t::FP16, 142,  "465707469ff3a37a2b9b8d8f89f2f99de7299dac" },
    { "base-q5_1",          "ggml-base-q5_1.bin",          mi_t::Q5_1,  57,  "a3733eda680ef76256db5fc5dd9de8629e62c5e7" },
    { "base-q8_0",          "ggml-base-q8_0.bin",          mi_t::Q8_0,  78,  "7bb89bb49ed6955013b166f1b6a6c04584a20fbe" },

    // base.en
    { "base.en",            "ggml-base.en.bin",            mi_t::FP16, 142,  "137c40403d78fd54d454da0f9bd998f78703390c" },
    { "base.en-q5_1",       "ggml-base.en-q5_1.bin",       mi_t::Q5_1,  57,  "d26d7ce5a1b6e57bea5d0431b9c20ae49423c94a" },
    { "base.en-q8_0",       "ggml-base.en-q8_0.bin",       mi_t::Q8_0,  78,  "bb1574182e9b924452bf0cd1510ac034d323e948" },

    // small
    { "small",              "ggml-small.bin",              mi_t::FP16, 466,  "55356645c2b361a969dfd0ef2c5a50d530afd8d5" },
    { "small-q5_1",         "ggml-small-q5_1.bin",         mi_t::Q5_1, 181,  "6fe57ddcfdd1c6b07cdcc73aaf620810ce5fc771" },
    { "small-q8_0",         "ggml-small-q8_0.bin",         mi_t::Q8_0, 252,  "bcad8a2083f4e53d648d586b7dbc0cd673d8afad" },

    // small.en
    { "small.en",           "ggml-small.en.bin",           mi_t::FP16, 466,  "db8a495a91d927739e50b3fc1cc4c6b8f6c2d022" },
    { "small.en-q5_1",      "ggml-small.en-q5_1.bin",      mi_t::Q5_1, 181,  "20f54878d608f94e4a8ee3ae56016571d47cba34" },
    { "small.en-q8_0",      "ggml-small.en-q8_0.bin",      mi_t::Q8_0, 252,  "9d75ff4ccfa0a8217870d7405cf8cef0a5579852" },

    // small.en-tdrz
    { "small.en-tdrz",      "ggml-small.en-tdrz.bin",      mi_t::FP16, 465,  "b6c6e7e89af1a35c08e6de56b66ca6a02a2fdfa1" },

    // medium
    { "medium",             "ggml-medium.bin",             mi_t::FP16, 1500, "fd9727b6e1217c2f614f9b698455c4ffd82463b4" },
    { "medium-q5_0",        "ggml-medium-q5_0.bin",        mi_t::Q5_0, 514,  "7718d4c1ec62ca96998f058114db98236937490e" },
    { "medium-q8_0",        "ggml-medium-q8_0.bin",        mi_t::Q8_0, 785,  "e66645948aff4bebbec71b3485c576f3d63af5d6" },

    // medium.en
    { "medium.en",          "ggml-medium.en.bin",          mi_t::FP16, 1500, "8c30f0e44ce9560643ebd10bbe50cd20eafd3723" },
    { "medium.en-q5_0",     "ggml-medium.en-q5_0.bin",     mi_t::Q5_0, 514,  "bb3b5281bddd61605d6fc76bc5b92d8f20284c3b" },
    { "medium.en-q8_0",     "ggml-medium.en-q8_0.bin",     mi_t::Q8_0, 785,  "b1cf48c12c807e14881f634fb7b6c6ca867f6b38" },

    // large-v1
    { "large-v1",           "ggml-large-v1.bin",           mi_t::FP16, 2900, "b1caaf735c4cc1429223d5a74f0f4d0b9b59a299" },

    // large-v2
    { "large-v2",           "ggml-large-v2.bin",           mi_t::FP16, 2900, "0f4c8e34f21cf1a914c59d8b3ce882345ad349d6" },
    { "large-v2-q5_0",      "ggml-large-v2-q5_0.bin",      mi_t::Q5_0, 1100, "00e39f2196344e901b3a2bd5814807a769bd1630" },
    { "large-v2-q8_0",      "ggml-large-v2-q8_0.bin",      mi_t::Q8_0, 1500, "da97d6ca8f8ffbeeb5fd147f79010eeea194ba38" },

    // large-v3
    { "large-v3",           "ggml-large-v3.bin",           mi_t::FP16, 2900, "ad82bf6a9043ceed055076d0fd39f5f186ff8062" },
    { "large-v3-q5_0",      "ggml-large-v3-q5_0.bin",      mi_t::Q5_0, 1100, "e6e2ed78495d403bef4b7cff42ef4aaadcfea8de" },

    // large-v3-turbo
    { "large-v3-turbo",         "ggml-large-v3-turbo.bin",         mi_t::FP16, 1500, "4af2b29d7ec73d781377bfd1758ca957a807e941" },
    { "large-v3-turbo-q5_0",    "ggml-large-v3-turbo-q5_0.bin",    mi_t::Q5_0, 547,  "e050f7970618a659205450ad97eb95a18d69c9ee" },
    { "large-v3-turbo-q8_0",    "ggml-large-v3-turbo-q8_0.bin",    mi_t::Q8_0, 834,  "01bf15bedffe9f39d65c1b6ff9b687ea91f59e0e" }
});

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


constexpr float MERGE_EPS_MS = 50.0f; // tune between ~20–100 ms

void insertOrReplaceSegment(QVector<TranscriptSegment> &segments,
                               const TranscriptSegment &seg)
{
    // 1) Remove any segment that overlaps seg within MERGE_EPS_MS
    for (int i = 0; i < segments.size(); ) {
        const auto &s = segments[i];

        // intervals [s.start_ms, s.end_ms] and [seg.start_ms, seg.end_ms]
        // are considered overlapping if they intersect when we pad each by MERGE_EPS_MS
        const bool overlaps =
            (s.start_ms <= seg.end_ms + MERGE_EPS_MS) &&
            (s.end_ms   >= seg.start_ms - MERGE_EPS_MS);

        if (overlaps) {
            segments.removeAt(i);
        } else {
            ++i;
        }
    }

    // 2) Insert seg so the list stays sorted by start_ms
    int pos = 0;
    while (pos < segments.size() && segments[pos].start_ms < seg.start_ms)
        ++pos;

    segments.insert(pos, seg);
}

QString assembleTranscript(const QVector<TranscriptSegment> &segments)
{
    QString out;
    out.reserve(4096);
    for (const auto &s : segments)
        out += s.text;
    return out;
}

} // anon ns

TranscriberWhisper::TranscriberWhisper(chunk_queue_t *queue, const QString &filePath, QAudioFormat format)
: Transcriber(queue, filePath, format)
{
    whisper_log_set(whisperLogger, nullptr);
}

TranscriberWhisper::~TranscriberWhisper()
{
    LOG_DEBUG_N << "TranscriberWhisper: destructor called";
    if (ctx_) {
        LOG_DEBUG_N << "TranscriberWhisper: freeing whisper context";
        whisper_free(ctx_);
        ctx_ = nullptr;
    }

    LOG_TRACE_N << "TranscriberWhisper: destructor finished";
}

QString TranscriberWhisper::resolveModelPath() const
{
    const QString baseDir = !modelDir_.isEmpty()
        ? modelDir_
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/whisper-models");

    QDir().mkpath(baseDir);

    const auto infoOpt = currentModelInfo();
    const QString fileName = infoOpt ? QString::fromUtf8(infoOpt->filename)
                                     : QStringLiteral("ggml-%1.bin").arg(modelId_);
    return baseDir + QLatin1Char('/') + fileName;
}

std::optional<TranscriberWhisper::ModelInfo> TranscriberWhisper::currentModelInfo() const
{
    optional<ModelInfo> rval;
    const auto models = builtinModels();
    for (const auto &m : models) {
        // Strip off quantization suffix for matching
        auto base_name = m.id;
        if (auto pos = base_name.find('-'); pos != std::string_view::npos) {
            base_name = base_name.substr(0, pos);
        }
        if (base_name == modelId_) {
            if (rval) {
                if (m.quantization == ModelInfo::Quatization::Q5_1) {
                    rval = m;
                } else if (rval->quantization != ModelInfo::Quatization::Q5_1
                           && m.quantization == ModelInfo::Quatization::Q5_0) {
                    rval = m;
                } else if ((rval->quantization != ModelInfo::Quatization::Q5_1
                            && rval->quantization != ModelInfo::Quatization::Q5_1)
                            && rval->quantization < m.quantization) {
                    // prefer higher-quality quantization, unless we have Q5_1/Q5_0
                    rval = m;
                }
            } else {
                rval = m;
            }
        }
    }
    return rval;
}

bool TranscriberWhisper::ensureModelOnDisk()
{
    const QString modelPath = resolveModelPath();

    LOG_TRACE_N << "Checking for Whisper model at " << modelPath.toStdString();

    QFile f(modelPath);
    if (f.exists() && f.size() > 0)
        return true;

    const auto infoOpt = currentModelInfo();
    if (!infoOpt) {
        LOG_ERROR_N << "Unknown Whisper model id: " << modelId_;
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

    LOG_INFO_N << "Downloading Whisper model " << model.id << " from " << url.toString();

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

std::span<const TranscriberWhisper::ModelInfo> TranscriberWhisper::builtinModels() noexcept
{
    return all_models;
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

    LOG_DEBUG_N << "Loading Whisper model from " << modelPath.toStdString();
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
    final_text_.clear();
    stable_until_ms_ = 0.0f;

    LOG_DEBUG_N << "TranscriberWhisper: started session with window "
                 << window_ms_ << " ms ("
                 << windowSamples << " samples)";
}

void TranscriberWhisper::processChunk(std::span<const uint8_t> data, bool lastChunk)
{
    if (!ctx_)
        return;

    LOG_TRACE_N << "TranscriberWhisper::processChunk #" << ++chunks_ << " called with data size ="
                << data.size() << " lastChunk =" << lastChunk;

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

    // 1) Compute where this buffer sits in global time
    const int64_t global_start_samples =
        std::max<int64_t>(0, total_samples_ - pcm_fill_);
    const float global_start_ms =
        (global_start_samples * 1000.0f) / sample_rate_;

    const int n_segments = whisper_full_n_segments(ctx_);

    for (int i = 0; i < n_segments; ++i) {
        const float local_start_ms = whisper_full_get_segment_t0(ctx_, i);
        const float local_end_ms   = whisper_full_get_segment_t1(ctx_, i);

        const float start_ms = global_start_ms + local_start_ms;
        const float end_ms   = global_start_ms + local_end_ms;

        const char *text_c = whisper_full_get_segment_text(ctx_, i);
        if (!text_c || !*text_c)
            continue;

        TranscriptSegment seg;
        seg.start_ms = start_ms;
        seg.end_ms   = end_ms;
        seg.text     = QString::fromUtf8(text_c);

        insertOrReplaceSegment(segments_, seg);
    }


    // Still inside processChunk, after updating segments_:

    const QString full_text = assembleTranscript(segments_);
    LOG_DEBUG_N << "Emitting partial text:" << full_text;
    emit partialTextAvailable(full_text);

    if (lastChunk) {
        final_text_ = full_text;
        LOG_DEBUG_N << "Final text:" << final_text_;
        //emit finalTextAvailable(final_text_);
    }
}

void TranscriberWhisper::processRecording(std::span<const float> data)
{
    LOG_DEBUG_N << "TranscriberWhisper::processRecording called with data size ="
                << data.size();

    assert(ctx_);
    if (!ctx_) {
        throw std::runtime_error("TranscriberWhisper::processRecording ctx_ is not initialized");
    }

    final_text_.clear();

    auto params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    // Threads
    const unsigned hwThreads = std::max(1u, std::thread::hardware_concurrency());
    params.n_threads = std::min<unsigned>(hwThreads, 48u);  // tune as needed

    params.print_progress   = false;
    params.print_realtime   = false;
    params.print_timestamps = true;

    const string lang = language_.toStdString();
    params.language = lang.empty() ? nullptr : lang.c_str();

    params.no_context     = false;
    params.single_segment = false;

    params.max_len          = 0;     // no token limit
    params.token_timestamps = true;

    LOG_DEBUG_N << "Calling whisper_full() with " << data.size() << " samples.";
    const auto when = std::chrono::steady_clock::now();
    const int rc = whisper_full(ctx_, params, data.data(), data.size());

    const auto duration = std::chrono::steady_clock::now() - when;
    LOG_DEBUG_N << "whisper_full() returned rc =" << rc
                << " in " << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()
                << " ms";

    if (rc != 0) {
        throw std::runtime_error("TranscriberWhisper::processRecording whisper_full() failed");
        return;
    }

    // Get all the returned test into final_text_
    const int segments_count = whisper_full_n_segments(ctx_);
    final_text_.clear();
    for (int i = 0; i < segments_count; ++i) {
        const char *text_c = whisper_full_get_segment_text(ctx_, i);
        if (!text_c || !*text_c)
            continue;

        final_text_ += QString::fromUtf8(text_c);
    }

    emit finalTextAvailable(final_text_);
}


