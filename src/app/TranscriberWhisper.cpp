#include <QDir>
#include <QNetworkReply>
#include <QEventLoop>
#include <QSettings>

#include <algorithm>
#include <cmath>

#include "TranscriberWhisper.h"
#include "ScopedTimer.h"
#include "logging.h"

namespace logfault {

std::pair<bool /* json */, std::string /* content or json */> toLog(const TranscriberWhisper& m, bool json) {
    return toLogHandler(m, json, "TranscriberWhisper");
}
} // logfault ns

using namespace std;

TranscriberWhisper::TranscriberWhisper(std::string name, std::unique_ptr<Config> &&cfg, chunk_queue_t *queue, const QString &filePath, QAudioFormat format)
    : Transcriber(std::move(name), std::move(cfg), queue, filePath, format)
{
    QSettings settings;
    max_live_latency_ms_ = std::max(200, settings.value("transcribe.live.max_latency_ms", 1500).toInt());
    min_live_submit_ms_ = std::max(50, settings.value("transcribe.live.min_submit_ms", 220).toInt());
    min_live_rms_dbfs_ = std::clamp(settings.value("transcribe.live.min_rms_dbfs", -52.0).toFloat(), -90.0F, -20.0F);

    LOG_TRACE_EX(*this) << "TranscriberWhisper: constructor called for model "
                << modelName()
                << " with language '" << language()
                << "', max_live_latency_ms=" << max_live_latency_ms_
                << ", min_live_submit_ms=" << min_live_submit_ms_
                << ", min_live_rms_dbfs=" << min_live_rms_dbfs_;
}

TranscriberWhisper::~TranscriberWhisper()
{
    LOG_DEBUG_EX(*this) << "TranscriberWhisper: destructor called";
}

bool TranscriberWhisper::createContextImpl()
{
    LOG_DEBUG_EX(*this) << "Creating a context/session for a loaded Whisper model";
    assert(session_ctx_ == nullptr);
    assert(haveModel());

    auto model_instance = modelInstance();
    if (!model_instance) {
        return failed("Model instance is null in createContextImpl");
    }

    auto whisper_ctx = model_instance->modelCtx();

    session_ctx_ = whisper_ctx->createWhisperSession();
    if (!session_ctx_) {
        return failed("Failed to create Whisper session context in createContextImpl");
    }

    return true;
}

void TranscriberWhisper::processChunk(std::span<const uint8_t> data, bool lastChunk, bool forceProcess)
{
    if (isCancelled()) {
        LOG_WARN_EX(*this) << "Called when cancelled. Ignoring.";
        return;
    }

    assert(session_ctx_ != nullptr);

    LOG_TRACE_EX(*this) << "TranscriberWhisper::processChunk #" << ++chunks_ << " called with data size ="
                << data.size() << " lastChunk =" << lastChunk
                << " forceProcess=" << forceProcess;

    // Append voiced PCM16 as float; silence is handled by caller.
    if (!data.empty()) {
        auto *samplesI16 = reinterpret_cast<const int16_t*>(data.data());
        const auto newSamples = static_cast<int>(data.size() / sizeof(int16_t));
        if (newSamples > 0) {
            pending_pcm_.reserve(pending_pcm_.size() + static_cast<size_t>(newSamples));
            for (int i = 0; i < newSamples; ++i) {
                pending_pcm_.push_back(samplesI16[i] / 32768.0F);
            }
            pending_samples_ += newSamples;
        }
    }

    // Nothing pending means nothing to submit.
    if (pending_pcm_.empty()) {
        if (lastChunk) {
            emit partialTextAvailable(QString::fromStdString(final_text_));
        }
        return;
    }

    // Primary trigger is pause boundary (forceProcess), with a latency fallback.
    const int64_t maxLatencySamples =
        (static_cast<int64_t>(max_live_latency_ms_) * sample_rate_) / 1000;

    bool shouldRun = false;

    if (lastChunk || forceProcess) {
        shouldRun = true;
    } else {
        shouldRun = pending_samples_ >= maxLatencySamples;
    }

    if (!shouldRun)
        return;

    const int64_t pending_duration_ms =
        (pending_samples_ * 1000) / std::max(1, sample_rate_);

    if (forceProcess && !lastChunk && pending_duration_ms < min_live_submit_ms_) {
        LOG_TRACE_EX(*this) << "Dropping short forced chunk: "
                            << pending_duration_ms << "ms < " << min_live_submit_ms_ << "ms";
        pending_pcm_.clear();
        pending_samples_ = 0;
        return;
    }

    double sum_square = 0.0;
    for (const float sample : pending_pcm_) {
        sum_square += static_cast<double>(sample) * static_cast<double>(sample);
    }
    const double rms = std::sqrt(sum_square / static_cast<double>(pending_pcm_.size()));
    const float rms_dbfs = static_cast<float>(20.0 * std::log10(std::max(rms, 1e-6)));
    if (forceProcess && !lastChunk && rms_dbfs < min_live_rms_dbfs_) {
        LOG_TRACE_EX(*this) << "Dropping near-silent forced chunk: rms_dbfs="
                            << rms_dbfs << " < " << min_live_rms_dbfs_;
        pending_pcm_.clear();
        pending_samples_ = 0;
        return;
    }

    // --- 4) Prepare Whisper parameters -----------------------------------

    qvw::WhisperSessionCtx::WhisperFullParams params;

    // Threads
    const unsigned hwThreads = std::max(1u, std::thread::hardware_concurrency());
    //params.threads = std::min<unsigned>(hwThreads, 48u);  // tune as needed

    params.print_progress   = false;
    params.print_realtime   = false;
    params.print_timestamps = true;
    params.offset_ms = 0;  // or leave default

    const auto& lng = language();
    params.language = lng.empty() ? nullptr : lng.c_str();

    params.no_context     = false;
    params.single_segment = false;

    if (lastChunk) {
        // Let Whisper be a bit more thorough for the final pass.
        params.max_len          = 0;     // no token limit
        params.token_timestamps = true;
    }


    // Run Whisper only on new pending voiced audio.

    LOG_TRACE_EX(*this) << "Calling whisper_full() with "
                 << pending_pcm_.size() << " samples ("
                 << (pending_pcm_.size() * 1000 / sample_rate_) << " ms), "
                 << "offset_ms=" << (params.offset_ms.has_value() ? to_string(params.offset_ms.value()) : "[empty]"s);

    const auto pcm_window = std::span<const float>(pending_pcm_.data(), pending_pcm_.size());
    qvw::WhisperSessionCtx::Transcript transcript_out;
    ScopedTimer timer;
    const auto ok = session_ctx_->whisperFull(pcm_window, params, transcript_out);
    LOG_DEBUG_EX(*this) << "whisper_full() returned ok =" << ok
                        << " in " << timer.elapsed() << " seconds.";

    if (isCancelled()) {
        LOG_DEBUG_EX(*this) << "Cancelled during whisper_full() call. Aborting furtner processing.";
        return;
    }

    if (!ok) {
        LOG_ERROR_N << "whisper_full() failed";
        return;
    }

    std::string chunk_text;
    for (const auto& segment : transcript_out.segments) {
        chunk_text += segment.text;
    }

    final_text_ += chunk_text;
    LOG_DEBUG_EX(*this) << "Emitting partial text:" << final_text_;
    emit partialTextAvailable(QString::fromStdString(final_text_));

    pending_pcm_.clear();
    pending_samples_ = 0;

    if (lastChunk) {
        LOG_DEBUG_EX(*this) << "Final text:" << final_text_;
    }
}

bool TranscriberWhisper::processRecording(std::span<const float> data)
{
    LOG_DEBUG_EX(*this) << name() << ": Called with data size ="
                << data.size();

    assert(session_ctx_ != nullptr);

    final_text_.clear();

    qvw::WhisperSessionCtx::WhisperFullParams params;

    // Threads
    const unsigned hwThreads = std::max(1u, std::thread::hardware_concurrency());
    params.print_progress   = false;
    params.print_realtime   = false;
    params.print_timestamps = true;

    const auto& lng = language();
    params.language = lng.empty() ? nullptr : lng.c_str();

    params.no_context     = false;
    params.single_segment = false;

    params.max_len          = 0;     // no token limit
    params.token_timestamps = true;

    params.vocabulary = vocabulary();

    LOG_DEBUG_EX(*this) << "Calling whisper_full() with " << data.size()
                        << " samples and vocabulary: " << params.vocabulary;
    qvw::WhisperSessionCtx::Transcript transcript_out;
    ScopedTimer timer;
    const auto ok = session_ctx_->whisperFull(data, params, transcript_out);
    LOG_DEBUG_EX(*this) << "whisper_full() returned ok =" << ok
                        << " in " << timer.elapsed() << " seconds.";

    if (!ok) {
        LOG_ERROR_N << "whisper_full() failed.";
        return false;
    }

    // Get all the returned test into final_text_
    final_text_.clear();
    for(const auto segment: transcript_out.segments){
        final_text_ += segment.text;
    }

    // if (config().submit_filal_text) {
    //     LOG_TRACE_EX(*this) << "Emitting final text:" << final_text_;
    //     emit Model::finalTextAvailable(QString::final_text_);
    // }

    return true;
}

bool TranscriberWhisper::stopImpl()
{
    LOG_DEBUG_EX(*this) << "TranscriberWhisper::stopImpl called";
    // Nothing special to do here for Whisper
    return true;
}
