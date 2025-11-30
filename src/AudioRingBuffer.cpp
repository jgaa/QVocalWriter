#include "AudioRingBuffer.h"


void AudioRingBuffer::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();
}
