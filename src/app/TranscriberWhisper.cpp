#include <QDir>
#include <QNetworkReply>
#include <QEventLoop>

#include <whisper.h>
#include <string_view>

#include "TranscriberWhisper.h"

#include "logging.h"

namespace logfault {

std::pair<bool /* json */, std::string /* content or json */> toLog(const TranscriberWhisper& m, bool json) {
    return toLogHandler(m, json, "TranscriberWhisper");
}
} // logfault ns

using namespace std;


namespace {

void whisperLogger(ggml_log_level level, const char *msg, void *) {
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
    LOG_TRACE_EX(*this) << "TranscriberWhisper: constructor called for model "
                << config().model_name
                << " with language '" << language() << "'";
}

TranscriberWhisper::~TranscriberWhisper()
{
    LOG_DEBUG_EX(*this) << "TranscriberWhisper: destructor called";
    whisper_state_.reset();
    w_ctx_ = {};
}

bool TranscriberWhisper::createContextImpl()
{
    assert(w_ctx_ == nullptr);
    assert(haveModel());

    auto * wi = dynamic_cast<WhisperInstance *>(modelInstance().get());
    if (!wi) {
        LOG_ERROR_EX(*this) << "TranscriberWhisper::createContextImpl: model instance is not WhisperInstance";
        return false;
    }

    w_ctx_ = wi->whisperCtx();
    assert(w_ctx_);
    if (!w_ctx_) {
        LOG_ERROR_EX(*this) << "Failed to get whisper context from model instance";
        return false;
    }

    whisper_state_ = wi->newState();
    assert(whisper_state_);
    if (!whisper_state_) {
        LOG_ERROR_EX(*this) << "Failed to create new whisper state";
        return false;
    }

    return true;
}

void TranscriberWhisper::processChunk(std::span<const uint8_t> data, bool lastChunk)
{
    if (isCancelled()) {
        LOG_WARN_EX(*this) << "Callen when cancelled. Ignoring.";
        return;
    }

    assert(w_ctx_ != nullptr);
    if (!w_ctx_)
        return;

    LOG_TRACE_EX(*this) << "TranscriberWhisper::processChunk #" << ++chunks_ << " called with data size ="
                << data.size() << " lastChunk =" << lastChunk;

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

    // Overlap fraction controls how often we call Whisper (step size)
    const float overlapClamped = std::clamp(overlap_fraction_, 0.0F, 0.9f);
    const int64_t stepSamples =
        static_cast<int64_t>(windowSamples * (1.0F - overlapClamped));

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

    const auto& lng = language();
    params.language = lng.empty() ? nullptr : lng.c_str();

    params.no_context     = false;
    params.single_segment = false;

    if (lastChunk) {
        // Let Whisper be a bit more thorough for the final pass.
        params.max_len          = 0;     // no token limit
        params.token_timestamps = true;
    }

    params.encoder_begin_callback_user_data = this;
    params.encoder_begin_callback = [](struct whisper_context * ctx, struct whisper_state * state, void * user_data) -> bool {
        auto * self = reinterpret_cast<TranscriberWhisper *>(user_data);
        LOG_TRACE_EX(*self) << "Whisper encoder begin callback called in state " << self->state();
        return self->isCancelled() == false;
    };

    // --- 5) Run Whisper on the current sliding buffer --------------------

    LOG_TRACE_EX(*this) << "Calling whisper_full() with "
                 << pcm_fill_ << " samples ("
                 << (pcm_fill_ * 1000 / sample_rate_) << " ms), "
                 << "offset_ms=" << params.offset_ms;
    const auto when = std::chrono::steady_clock::now();
    const int rc = whisper_full_with_state(
        w_ctx_,
        whisper_state_.get(),
        params,
        pcm_.data(),
        pcm_fill_
        );

    if (isCancelled()) {
        LOG_DEBUG_EX(*this) << "Cancelled during whisper_full() call. Aborting furtner processing.";
        return;
    }

    const auto duration = std::chrono::steady_clock::now() - when;
    LOG_DEBUG_EX(*this) << "whisper_full() returned rc =" << rc
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
        (global_start_samples * 1000.0F) / sample_rate_;

    const int n_segments = whisper_full_n_segments_from_state(whisper_state_.get());

    for (int i = 0; i < n_segments; ++i) {
        const float local_start_ms = whisper_full_get_segment_t0_from_state(whisper_state_.get(), i);
        const float local_end_ms   = whisper_full_get_segment_t1_from_state(whisper_state_.get(), i);

        const float start_ms = global_start_ms + local_start_ms;
        const float end_ms   = global_start_ms + local_end_ms;

        const char *text_c = whisper_full_get_segment_text_from_state(whisper_state_.get(), i);
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
    LOG_DEBUG_EX(*this) << "Emitting partial text:" << full_text;
    emit partialTextAvailable(full_text);

    if (lastChunk) {
        final_text_ = full_text;
        LOG_DEBUG_EX(*this) << "Final text:" << final_text_;
        //emit finalTextAvailable(final_text_);
    }
}

void TranscriberWhisper::processRecording(std::span<const float> data)
{
    LOG_DEBUG_EX(*this) << name() << ": Called with data size ="
                << data.size();

    assert(w_ctx_);
    if (!w_ctx_) {
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

    const auto& lng = language();
    params.language = lng.empty() ? nullptr : lng.c_str();

    params.no_context     = false;
    params.single_segment = false;

    params.max_len          = 0;     // no token limit
    params.token_timestamps = true;

    LOG_DEBUG_EX(*this) << "Calling whisper_full() with " << data.size() << " samples.";
    const auto when = std::chrono::steady_clock::now();
    const int rc = whisper_full_with_state(w_ctx_, whisper_state_.get(), params, data.data(), data.size());

    const auto duration = std::chrono::steady_clock::now() - when;
    LOG_DEBUG_EX(*this) << "whisper_full() returned rc =" << rc
                << " in " << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()
                << " ms";

    if (rc != 0) {
        throw std::runtime_error("TranscriberWhisper::processRecording whisper_full() failed");
    }

    // Get all the returned test into final_text_
    const int segments_count = whisper_full_n_segments_from_state(whisper_state_.get());
    final_text_.clear();
    for (int i = 0; i < segments_count; ++i) {
        const char *text_c = whisper_full_get_segment_text_from_state(whisper_state_.get(), i);
        if (!text_c || !*text_c)
            continue;

        final_text_ += QString::fromUtf8(text_c);
    }

    if (config().submit_filal_text) {
        LOG_TRACE_EX(*this) << "Emitting final text:" << final_text_;
        emit Model::finalTextAvailable(final_text_);
    }
}

bool TranscriberWhisper::stopImpl()
{
    LOG_DEBUG_EX(*this) << "TranscriberWhisper::stopImpl called";
    // Nothing special to do here for Whisper
    return true;
}


