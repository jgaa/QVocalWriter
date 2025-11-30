#include "AudioCaptureDevice.h"

#include "logging.h"
using namespace std;


AudioCaptureDevice::AudioCaptureDevice(AudioRingBuffer *ring, QObject *parent)
    : QIODevice(parent),
    m_ring(ring)
{}

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
    LOG_TRACE_N << "AudioCaptureDevice::writeData called with len =" << len;

    // Called by QAudioSource thread.
    if (!m_ring) {
        assert(false);
        return len;
    }

    // TODO: keep the buffer in the ring to avoid malloc
    QByteArray chunk(data, static_cast<int>(len));
    m_ring->push(std::move(chunk));

    return len;
}
