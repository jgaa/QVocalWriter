#include <QDir>
#include <QNetworkReply>
#include <QEventLoop>
#include <QSettings>

//#include <whisper.h>
#include <string_view>

#include "TranscriberWhisper.h"
#include "ScopedTimer.h"
#include "logging.h"

namespace logfault {

std::pair<bool /* json */, std::string /* content or json */> toLog(const TranscriberWhisper& m, bool json) {
    return toLogHandler(m, json, "TranscriberWhisper");
}
} // logfault ns

using namespace std;


namespace {


constexpr float MERGE_EPS_MS = 50.0F; // tune between ~20–100 ms

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
    while (pos < segments.size() && segments[pos].start_ms < seg.start_ms) {
        ++pos;
    }

    segments.insert(pos, seg);
}

QString assembleTranscript(const QVector<TranscriptSegment> &segments)
{
    QString out;
    out.reserve(4096);
    for (const auto &s : segments) {
        out += s.text;
    }
    return out;
}

} // anon ns

void TranscriberWhisper::startSession()
{
    const int windowSamples = (window_ms_ * sample_rate_) / 1000;
    pcm_.assign(windowSamples, 0.0F);
    pcm_fill_ = 0;

    total_samples_         = 0;
    last_processed_sample_  = 0;
    last_emitted_end_time_ms_ = 0.0F;
    final_text_.clear();
    stable_until_ms_ = 0.0F;

    LOG_DEBUG_EX(*this) << "TranscriberWhisper: started session with window "
                 << window_ms_ << " ms ("
                 << windowSamples << " samples)";
}

TranscriberWhisper::TranscriberWhisper(std::string name, std::unique_ptr<Config> &&cfg, chunk_queue_t *queue, const QString &filePath, QAudioFormat format)
    : Transcriber(std::move(name), std::move(cfg), queue, filePath, format)
{
    QSettings settings;
    min_ms_before_process_ = std::max(0, settings.value("transcribe.live.min_ms_before_process", 200).toInt());
    max_live_latency_ms_ = std::max(200, settings.value("transcribe.live.max_latency_ms", 1500).toInt());

    LOG_TRACE_EX(*this) << "TranscriberWhisper: constructor called for model "
                << modelName()
                << " with language '" << language()
                << "', min_ms_before_process=" << min_ms_before_process_
                << ", max_live_latency_ms=" << max_live_latency_ms_;
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

    // --- 1) Derive window size in samples ---------------------------------
    const int windowSamples = (window_ms_ * sample_rate_) / 1000;
    if (windowSamples <= 0)
        return;

    // If settings changed mid-session, re-init buffer
    if (static_cast<int>(pcm_.size()) != windowSamples) {
        pcm_.assign(windowSamples, 0.0F);
        pcm_fill_             = 0;
        total_samples_        = 0;
        last_processed_sample_ = 0;
        last_emitted_end_time_ms_ = 0.0F;
    }

    // --- 2) Append new samples (if any) with sliding window via memmove ---

    if (!data.empty()) {
        auto *samplesI16 = reinterpret_cast<const int16_t*>(data.data());
        auto newSamples = static_cast<int>(data.size() / sizeof(int16_t));

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
                pcm_[pcm_fill_++] = samplesI16[i] / 32768.0F;
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

    const int64_t maxLatencySamples =
        (static_cast<int64_t>(max_live_latency_ms_) * sample_rate_) / 1000;

    bool shouldRun = false;

    if (lastChunk || forceProcess) {
        // On final call: always flush pending audio
        // (even if we didn’t reach the live-latency threshold yet)
        shouldRun = true;
    } else {
        // Not a forced/final call: enforce minMs + fallback max latency.
        if (total_samples_ >= minSamplesBeforeProcess) {
            const int64_t sinceLastProcessed = total_samples_ - last_processed_sample_;
            if (sinceLastProcessed >= maxLatencySamples) {
                shouldRun = true;
            }
        }
    }

    if (!shouldRun)
        return;

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


    // --- 5) Run Whisper on the current sliding buffer --------------------

    LOG_TRACE_EX(*this) << "Calling whisper_full() with "
                 << pcm_fill_ << " samples ("
                 << (pcm_fill_ * 1000 / sample_rate_) << " ms), "                 << "offset_ms=" << (params.offset_ms.has_value() ? to_string(params.offset_ms.value()) : "[empty]"s);;

    const auto pcm_window = std::span<const float>(pcm_.data(), pcm_fill_);
    qvw::WhisperSessionCtx::Transcript transcript_out;
    ScopedTimer timer;
    const auto ok = session_ctx_->whisperFull(pcm_window, params, transcript_out);
    LOG_DEBUG_EX(*this) << "whisper_full() returned ok =" << ok
                        << " in " << timer.elapsed() << " seconds.";

    if (isCancelled()) {
        LOG_DEBUG_EX(*this) << "Cancelled during whisper_full() call. Aborting furtner processing.";
        return;
    }

    last_processed_sample_ = total_samples_;

    if (!ok) {
        LOG_ERROR_N << "whisper_full() failed";
        return;
    }

    // 1) Compute where this buffer sits in global time
    const int64_t global_start_samples =
        std::max<int64_t>(0, total_samples_ - pcm_fill_);
    const float global_start_ms =
        (global_start_samples * 1000.0F) / sample_rate_;


    for(const auto segment: transcript_out.segments){
        TranscriptSegment seg;
        seg.start_ms = global_start_ms + static_cast<float>(segment.t0_ms);
        seg.end_ms   = global_start_ms + static_cast<float>(segment.t1_ms);
        seg.text     = QString::fromUtf8(segment.text.c_str());
        insertOrReplaceSegment(segments_, seg);
    }

    const QString full_text = assembleTranscript(segments_);
    final_text_ = full_text.toStdString();
    LOG_DEBUG_EX(*this) << "Emitting partial text:" << final_text_;
    emit partialTextAvailable(QString::fromStdString(final_text_));

    if (lastChunk) {
        LOG_DEBUG_EX(*this) << "Final text:" << final_text_;
        //emit finalTextAvailable(final_text_);
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
