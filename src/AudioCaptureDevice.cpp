#include "AudioCaptureDevice.h"
#include "AudioRecorder.h"

#include "logging.h"
using namespace std;


AudioCaptureDevice::AudioCaptureDevice(AudioRingBuffer *ring, QObject *parent)
    : QIODevice(parent),
    m_ring(ring)
{
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
        const auto bytes_left = AUDIO_BUFFER_SIZE - audioBuffer_.size();
        const auto bytes_to_add = min<size_t>(bytes_left, len);

        audioBuffer_.append(data, bytes_to_add);
        written += bytes_to_add;

        const auto chunk_duration = chrono::steady_clock::now() - chunk_start_time_;
        if (chunk_duration >= 200ms || audioBuffer_.size() >= AUDIO_BUFFER_SIZE) {
            //LOG_TRACE_N << "AudioCaptureDevice::writeData pushing segment << " << ++segment_ << " of size " << audioBuffer_.size();
            recalculateRecordingLevel(std::span<const qint16>(reinterpret_cast<const qint16*>(audioBuffer_.data()),
                                                                       audioBuffer_.size() / sizeof(qint16)));
            m_ring->push(std::move(audioBuffer_));
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
