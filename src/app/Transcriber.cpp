
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

#include <QSettings>

#include "Transcriber.h"
#include "AudioRecorder.h"

#include "logging.h"

using namespace std;

namespace logfault {
std::pair<bool /* json */, std::string /* content or json */> toLog(const Transcriber& m, bool json) {
    return toLogHandler(m, json, "Transcriber");
}
} // ns

namespace {

std::vector<float> compactPcmBySilence(const std::vector<float>& input, int sampleRate)
{
    if (input.empty() || sampleRate <= 0) {
        return input;
    }

    QSettings settings;
    const bool vad_enabled = settings.value("transcribe.vad.enabled", true).toBool();
    const bool post_skip_silence = settings.value("transcribe.post.skip_silence", true).toBool();
    if (!vad_enabled || !post_skip_silence) {
        return input;
    }

    const int frame_ms = std::max(10, settings.value("transcribe.vad.frame_ms", 20).toInt());
    const int frame_samples = std::max(1, (sampleRate * frame_ms) / 1000);
    const float speech_margin_db = settings.value("transcribe.vad.speech_margin_db", 10.0).toFloat();
    const int min_speech_ms = std::max(0, settings.value("transcribe.vad.min_speech_ms", 120).toInt());
    const int min_silence_ms = std::max(0, settings.value("transcribe.vad.min_silence_ms", 450).toInt());
    const int preroll_ms = std::max(0, settings.value("transcribe.vad.preroll_ms", 120).toInt());
    const int postroll_ms = std::max(0, settings.value("transcribe.vad.postroll_ms", 180).toInt());
    const float alpha = std::clamp(settings.value("transcribe.vad.noise_floor_alpha", 0.02).toFloat(), 0.001F, 0.5F);

    const int min_speech_frames = std::max(1, (min_speech_ms + frame_ms - 1) / frame_ms);
    const int min_silence_frames = std::max(1, (min_silence_ms + frame_ms - 1) / frame_ms);
    const int preroll_frames = std::max(0, (preroll_ms + frame_ms - 1) / frame_ms);
    const int postroll_frames = std::max(0, (postroll_ms + frame_ms - 1) / frame_ms);

    const int frame_count = static_cast<int>((input.size() + frame_samples - 1) / frame_samples);
    std::vector<bool> keep(frame_count, false);

    float noise_floor_dbfs = -70.0F;
    bool in_speech = false;
    int speech_frames = 0;
    int silence_frames = 0;
    int speech_start_candidate = 0;

    auto rmsDbfs = [&](int frame_ix) -> float {
        const size_t start = static_cast<size_t>(frame_ix) * static_cast<size_t>(frame_samples);
        const size_t end = std::min(input.size(), start + static_cast<size_t>(frame_samples));
        if (start >= end) {
            return -120.0F;
        }

        double sum_square = 0.0;
        for (size_t i = start; i < end; ++i) {
            const auto v = static_cast<double>(input[i]);
            sum_square += v * v;
        }
        const double rms = std::sqrt(sum_square / static_cast<double>(end - start));
        return static_cast<float>(20.0 * std::log10(std::max(rms, 1e-6)));
    };

    for (int i = 0; i < frame_count; ++i) {
        const float db = rmsDbfs(i);
        const bool speech_now = db > (noise_floor_dbfs + speech_margin_db);

        if (speech_now) {
            if (speech_frames == 0) {
                speech_start_candidate = i;
            }
            ++speech_frames;
            silence_frames = 0;

            if (db < noise_floor_dbfs) {
                noise_floor_dbfs = (1.0F - alpha) * noise_floor_dbfs + alpha * db;
            }

            if (!in_speech && speech_frames >= min_speech_frames) {
                in_speech = true;
                const int from = std::max(0, speech_start_candidate - preroll_frames);
                for (int k = from; k <= i; ++k) {
                    keep[static_cast<size_t>(k)] = true;
                }
            } else if (in_speech) {
                keep[static_cast<size_t>(i)] = true;
            }
        } else {
            ++silence_frames;
            speech_frames = 0;
            noise_floor_dbfs = (1.0F - alpha) * noise_floor_dbfs + alpha * db;

            if (in_speech) {
                keep[static_cast<size_t>(i)] = true;
                if (silence_frames >= min_silence_frames) {
                    const int to = std::min(frame_count - 1, i + postroll_frames);
                    for (int k = i; k <= to; ++k) {
                        keep[static_cast<size_t>(k)] = true;
                    }
                    in_speech = false;
                    silence_frames = 0;
                }
            }
        }
    }

    if (in_speech && frame_count > 0) {
        for (int k = std::max(0, frame_count - postroll_frames - 1); k < frame_count; ++k) {
            keep[static_cast<size_t>(k)] = true;
        }
    }

    std::vector<float> output;
    output.reserve(input.size());
    for (int i = 0; i < frame_count; ++i) {
        if (!keep[static_cast<size_t>(i)]) {
            continue;
        }
        const size_t start = static_cast<size_t>(i) * static_cast<size_t>(frame_samples);
        const size_t end = std::min(input.size(), start + static_cast<size_t>(frame_samples));
        output.insert(output.end(), input.begin() + static_cast<ptrdiff_t>(start), input.begin() + static_cast<ptrdiff_t>(end));
    }

    // If VAD compaction removed everything or almost everything, keep original as safe fallback.
    if (output.size() < static_cast<size_t>(frame_samples)) {
        return input;
    }

    return output;
}

} // namespace


Transcriber::Transcriber(std::string name,
                         std::unique_ptr<Config> && config,
                         chunk_queue_t *queue,
                         const QString &pcmFilePath,
                         QAudioFormat format)
    : Model(std::move(name), std::move(config)), queue_(queue), file_(pcmFilePath), format_(format)
{
    const auto *m = dynamic_cast<Model*>(this);
    assert(m);
    if (!file_.open(QIODevice::ReadOnly)) {
        LOG_ERROR_EX(*m) << "Failed to open file for writing: " << pcmFilePath;
        throw runtime_error("Failed to open file for writing");
        return;
    }
}

QCoro::Task<bool> Transcriber::transcribeChunks()
{
    auto op = make_unique<Model::Operation>([this]() -> bool {
        return transcribeSegments();
    });

    auto future = op->future();

    LOG_TRACE_EX(*this) << "Enqueuing TranscribeChunks command...";
    enqueueCommand(std::move(op));
    const auto result = co_await future;
    LOG_TRACE_EX(*this) << "TranscribeChunks command completed.";
    co_return result;
}

QCoro::Task<bool> Transcriber::transcribeRecording()
{
    auto op = make_unique<Model::Operation>([this]() -> bool {
        LOG_TRACE_EX(*this) << "Callig processRecordingFromFile... from worker thread";
        processRecordingFromFile();
        LOG_TRACE_EX(*this) << "processRecordingFromFile completed.";
        return true;
    });

    auto future = op->future();

    LOG_TRACE_EX(*this) << "Enqueuing TranscribeRecording command...";
    enqueueCommand(std::move(op));
    const auto result = co_await future;
    LOG_TRACE_EX(*this) << "TranscribeRecording command completed.";
    co_return result;
}

void Transcriber::stopTranscribing() {
    if (state() < ModelState::STOPPING) {
        LOG_TRACE_EX(*this) << "Stopping transcriber.";
        setState(ModelState::STOPPING);
    }
}

const string &Transcriber::language() const noexcept
{
    return config().from_language;
}


bool Transcriber::transcribeSegments()
{
    // Assumed to run in worker_ thread
    assert(worker());
    assert(this_thread::get_id() == worker()->get_id());

    assert(file_.isOpen());
    if (!file_.isOpen()) {
        LOG_ERROR_EX(*this) << "Transcriber: file not open";
        return false;
    }

    FileChunk fc;
    auto segment = 0u;
    bool in_speech_run = false;

    while(!isCancelled()) {
        if (!queue_->pop(fc)) {
            LOG_DEBUG_EX(*this) << "Transcriber: queue stopped or empty. submit_filal_text="
                                << config().submit_filal_text;

            // Finish transcription of any remaining data
            if (config().submit_filal_text) {
                LOG_TRACE_EX(*this) << "Processing last chunk";
                processChunk({}, true, in_speech_run);
            }
            return true;;
        }

        LOG_TRACE_EX(*this) << "Reading #" << ++segment
                            << " offset=" << fc.offset
                            << " size=" << fc.size
                            << " speech=" << fc.is_speech;

        // Silence-aware live path:
        // - Do not feed silence buffers to the model.
        // - Trigger immediate partial processing once speech ends.
        if (!fc.is_speech) {
            if (in_speech_run) {
                LOG_TRACE_EX(*this) << "Speech boundary detected. Forcing partial processing.";
                processChunk({}, false, true);
                in_speech_run = false;
            }
            continue;
        }
        in_speech_run = true;

        if (!file_.seek(fc.offset)) {
            LOG_ERROR_EX(*this) << "Transcriber: failed to seek to offset " << fc.offset;
            continue;
        }

        array<char, AUDIO_BUFFER_SIZE> buffer; // No need to initialzie the buffer. We will write into it.

        decltype(fc.size) read = 0;

        // Repeat in case the buffer size is smaller than fc.size (unlikely)
        while(read < fc.size) {
            const auto chunk_size = min<size_t>(fc.size, buffer.size());
            size_t chunk_read = 0;

            // Repeat until we get the full chunk. Since we have it's size, it has already been written.
            do {
                // LOG_TRACE_EX(*this) << "Transcriber: reading chunk at offset " << (fc.offset + read + chunk_read)
                //             << " size=" << (chunk_size - chunk_read);
                const auto bytes = file_.read(buffer.data() + chunk_read, static_cast<qint64>(chunk_size - chunk_read));
                assert(bytes >= 0);
                if (bytes < 0) {
                    LOG_ERROR_EX(*this) << "Transcriber: failed to read " << (chunk_size - chunk_read)
                                << " bytes from file at offset "
                                << (fc.offset + read + chunk_read);
                    emit errorOccurred("Transcriber: file read error");
                    setState(ModelState::ERROR);
                    return false;;
                }
                chunk_read += static_cast<size_t>(bytes);
            } while (chunk_read < chunk_size);

            if (chunk_read > 0) {
                read += chunk_read;
                try {
                    processChunk({reinterpret_cast<const uint8_t*>(buffer.data()), chunk_read},
                                 false,
                                 false);
                } catch (const exception& ex) {
                    LOG_ERROR_EX(*this) << "Transcriber: exception during processChunk: " << ex.what();
                    emit errorOccurred(QString("Transcriber: exception during processChunk: %1").arg(ex.what()));
                    setState(ModelState::ERROR);
                    return false;
                }
            } else {
                LOG_WARN_EX(*this) << "Transcriber: read 0 bytes...";
            }
        }
    }

    return true;
}

void Transcriber::processRecordingFromFile()
{
    assert(file_.isOpen());
    file_.seek(0);

    LOG_DEBUG_EX(*this) << name() << ": Post-processing complete recording from file, size=" << file_.size();

    static_assert(AUDIO_BUFFER_SIZE % sizeof(qint16) == 0, "AUDIO_BUFFER_SIZE must be aligned with sizeof(qint16)");

    array<char, AUDIO_BUFFER_SIZE> buffer;
    vector<float> whisper_pcm;
    whisper_pcm.resize(file_.size() / sizeof(qint16));

    qint64 total_read = 0;
    // Read the entire file in chunks and copy the data to the whisper_pcm buffer
    while (total_read < file_.size()) {
        const auto to_read = min<size_t>(buffer.size(), static_cast<size_t>(file_.size() - total_read));
        const auto bytes = file_.read(buffer.data(), static_cast<qint64>(to_read));
        if (bytes <= 0) {
            LOG_ERROR_EX(*this) << "Transcriber: failed to read from file during post-processing";
            emit errorOccurred("Transcriber: file read error during post-processing");
            setState(ModelState::ERROR);
            return;
        }

        // Copy and convert to float
        const auto samples = bytes / sizeof(qint16);
        const auto* src = reinterpret_cast<const qint16*>(buffer.data());
        for (size_t i = 0; i < samples; ++i) {
            whisper_pcm[(total_read / sizeof(qint16)) + i] = static_cast<float>(src[i]) / 32768.0f;
        }

        total_read += bytes;
    }

    auto compacted_pcm = compactPcmBySilence(whisper_pcm, format_.sampleRate());
    if (compacted_pcm.size() != whisper_pcm.size()) {
        LOG_DEBUG_EX(*this) << name() << ": Silence compaction reduced samples from "
                            << whisper_pcm.size() << " to " << compacted_pcm.size();
    }

    processRecording(std::span<const float>(compacted_pcm.data(), compacted_pcm.size()));
}
