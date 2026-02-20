#pragma once

#include <mutex>
#include <condition_variable>
#include <deque>

#include <QByteArray>
#include <QtGlobal>

class AudioRingBuffer
{
public:
    struct Chunk {
        QByteArray pcm;
        float rms_dbfs = -120.0F;
        float peak = 0.0F;
        qint64 capture_ts_ms = 0;
        int sample_count = 0;
        bool is_speech = false;
    };

    void push(Chunk &&chunk)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.size() >= maxChunks_) {
                // overwrite oldest or drop – up to you
                queue_.pop_front();
            }
            queue_.push_back(std::move(chunk));
        }
        cv_.notify_one();
    }

    // Blocks until data or stop.
    bool pop(Chunk &out)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]{ return !queue_.empty() || stopped_; });
        if (queue_.empty())
            return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void stop();

private:
    std::mutex              mutex_;
    std::condition_variable cv_;
    std::deque<Chunk>       queue_;
    static constexpr size_t maxChunks_ = 256;
    bool                    stopped_ = false;
};
