#include "AudioCaptureDevice.h"
#include "AudioRecorder.h"

#include <algorithm>
#include <cmath>

#include <QSettings>

#include "logging.h"
using namespace std;


AudioCaptureDevice::AudioCaptureDevice(AudioRingBuffer *ring, int sampleRate, QObject *parent)
    : QIODevice(parent),
    m_ring(ring),
    sample_rate_(max(1, sampleRate))
{
    QSettings settings;
    vad_enabled_ = settings.value("transcribe.vad.enabled", true).toBool();
    vad_config_.speech_margin_db = settings.value("transcribe.vad.speech_margin_db", 10.0).toFloat();
    vad_config_.min_speech_ms = settings.value("transcribe.vad.min_speech_ms", 120).toInt();
    vad_config_.min_silence_ms = settings.value("transcribe.vad.min_silence_ms", 450).toInt();
    vad_config_.noise_floor_alpha = settings.value("transcribe.vad.noise_floor_alpha", 0.02).toFloat();

    vad_config_.min_speech_ms = max(0, vad_config_.min_speech_ms);
    vad_config_.min_silence_ms = max(0, vad_config_.min_silence_ms);
    vad_config_.noise_floor_alpha = std::clamp(vad_config_.noise_floor_alpha, 0.001F, 0.5F);
    vad_config_.speech_margin_db = std::clamp(vad_config_.speech_margin_db, 1.0F, 40.0F);

    LOG_DEBUG_N << "AudioCaptureDevice VAD: enabled=" << vad_enabled_
                << " speech_margin_db=" << vad_config_.speech_margin_db
                << " min_speech_ms=" << vad_config_.min_speech_ms
                << " min_silence_ms=" << vad_config_.min_silence_ms;
    prepareBuffer();
}

bool AudioCaptureDevice::open(OpenMode mode)
{
    LOG_DEBUG_N << "Opening AudioCaptureDevice in mode" << mode;
    if (!(mode & WriteOnly)) {
        LOG_ERROR_N << "AudioCaptureDevice can only be opened in WriteOnly mode";
        return false;
    }

    const auto res = QIODevice::open(mode);
    if (!res) {
        LOG_ERROR_N << "Failed to open AudioCaptureDevice";
    }
    return res;
}

void AudioCaptureDevice::close()
{
    LOG_DEBUG_N << "Closing AudioCaptureDevice";
    QIODevice::close();
}

qint64 AudioCaptureDevice::writeData(const char *data, qint64 len)
{
    //LOG_TRACE_N << "AudioCaptureDevice::writeData called with len =" << len;

    if (!m_ring) {
        assert(false);
        return len;
    }

    std::call_once(first_write_flag_, [this]() {
        chunk_start_time_ = std::chrono::steady_clock::now();
    });

    qint64 written = 0;

    // Fill the audio buffer and push to ring buffer when full, or after 200 ms accumulated audio
    do {
        const auto bytes_left = static_cast<size_t>(AUDIO_BUFFER_SIZE - audioBuffer_.size());
        const auto bytes_remaining = static_cast<size_t>(len - written);
        const auto bytes_to_add = min(bytes_left, bytes_remaining);
        if (bytes_to_add == 0) {
            break;
        }

        audioBuffer_.append(data + written, static_cast<qint64>(bytes_to_add));
        written += bytes_to_add;

        const auto chunk_duration = chrono::steady_clock::now() - chunk_start_time_;
        if (chunk_duration >= 200ms || audioBuffer_.size() >= AUDIO_BUFFER_SIZE) {
            //LOG_TRACE_N << "AudioCaptureDevice::writeData pushing segment << " << ++segment_ << " of size " << audioBuffer_.size();
            const auto samples = std::span<const qint16>(
                reinterpret_cast<const qint16*>(audioBuffer_.data()),
                static_cast<size_t>(audioBuffer_.size() / sizeof(qint16)));

            recalculateRecordingLevel(samples);
            const auto stats = calculateChunkStats(samples);
            const auto chunk_duration_ms = toDurationMs(static_cast<int>(samples.size()), sample_rate_);
            const auto is_speech = updateVadState(stats, chunk_duration_ms);

            AudioRingBuffer::Chunk chunk{
                .pcm = std::move(audioBuffer_),
                .rms_dbfs = stats.rms_dbfs,
                .peak = stats.peak,
                .capture_ts_ms = chrono::duration_cast<chrono::milliseconds>(
                    chrono::steady_clock::now().time_since_epoch()).count(),
                .sample_count = static_cast<int>(samples.size()),
                .is_speech = is_speech
            };
            m_ring->push(std::move(chunk));
            chunk_start_time_ = std::chrono::steady_clock::now();
            prepareBuffer();
        }

    } while (written < len);

    return len;
}

void AudioCaptureDevice::prepareBuffer()
{
    audioBuffer_.clear();
    audioBuffer_.reserve(AUDIO_BUFFER_SIZE);
}

void AudioCaptureDevice::recalculateRecordingLevel(std::span<const qint16> samples)
{
    if (samples.empty())
        return;

    // 1) Find peak amplitude in this chunk (normalized -1..1 → 0..1)
    double peak = 0.0;
    for (qint16 s : samples) {
        double v = static_cast<double>(s) / 32768.0;  // normalize
        double a = std::abs(v);
        if (a > peak)
            peak = a;
    }

    // 2) Smooth with simple low-pass filter so the UI doesn’t flicker
    constexpr double alpha = 0.3;  // 0..1, higher => more responsive
    double new_level = alpha * peak + (1.0 - alpha) * static_cast<double>(recording_level_);

    // Clamp to [0, 1]
    if (new_level < 0.0)
        new_level = 0.0;
    else if (new_level > 1.0)
        new_level = 1.0;

    // 3) Only update and emit if the change is significant
    if (std::abs(new_level - static_cast<double>(recording_level_)) < 0.001)
        return;

    recording_level_ = static_cast<qreal>(new_level);
    emit recordingLevelUpdated(recording_level_);
}

AudioCaptureDevice::ChunkStats AudioCaptureDevice::calculateChunkStats(std::span<const qint16> samples) const
{
    if (samples.empty()) {
        return {};
    }

    double sum_square = 0.0;
    float peak = 0.0F;
    for (const qint16 s : samples) {
        const float normalized = static_cast<float>(s) / 32768.0F;
        const float abs_sample = std::abs(normalized);
        peak = std::max(peak, abs_sample);
        sum_square += static_cast<double>(normalized) * static_cast<double>(normalized);
    }

    const auto inv_n = 1.0 / static_cast<double>(samples.size());
    const double rms = std::sqrt(sum_square * inv_n);
    const float rms_dbfs = static_cast<float>(20.0 * std::log10(std::max(rms, 1e-6)));
    return ChunkStats{.rms_dbfs = rms_dbfs, .peak = peak};
}

bool AudioCaptureDevice::updateVadState(const ChunkStats& stats, int chunkDurationMs)
{
    if (!vad_enabled_) {
        in_speech_ = true;
        return in_speech_;
    }

    if (chunkDurationMs <= 0) {
        return in_speech_;
    }

    const bool is_clearly_speech =
        stats.rms_dbfs > (noise_floor_dbfs_ + vad_config_.speech_margin_db);

    if (is_clearly_speech) {
        speech_ms_accum_ += chunkDurationMs;
        silence_ms_accum_ = 0;

        // When speech is present, adapt noise floor slowly and only from lower values.
        if (stats.rms_dbfs < noise_floor_dbfs_) {
            const float alpha = vad_config_.noise_floor_alpha;
            noise_floor_dbfs_ = (1.0F - alpha) * noise_floor_dbfs_ + alpha * stats.rms_dbfs;
        }

        if (!in_speech_ && speech_ms_accum_ >= vad_config_.min_speech_ms) {
            in_speech_ = true;
        }
    } else {
        silence_ms_accum_ += chunkDurationMs;
        speech_ms_accum_ = 0;

        const float alpha = vad_config_.noise_floor_alpha;
        noise_floor_dbfs_ = (1.0F - alpha) * noise_floor_dbfs_ + alpha * stats.rms_dbfs;

        if (in_speech_ && silence_ms_accum_ >= vad_config_.min_silence_ms) {
            in_speech_ = false;
        }
    }

    return in_speech_;
}

int AudioCaptureDevice::toDurationMs(int sampleCount, int sampleRate)
{
    if (sampleCount <= 0 || sampleRate <= 0) {
        return 0;
    }
    return static_cast<int>((static_cast<int64_t>(sampleCount) * 1000) / sampleRate);
}
