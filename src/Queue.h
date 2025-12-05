#pragma once

#include <mutex>
#include <deque>
#include <condition_variable>

#include <QObject>

template <typename T>
class Queue
{
public:
    using type_t = T;

    Queue() = default;

    void push(const T &data)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(data);
        }
        cv_.notify_one();
    }

    bool pop(T &out)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]{ return !queue_.empty() || stopped_; });
        if (queue_.empty())
            return false;
        out = queue_.front();
        queue_.pop_front();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    bool stopped_{false};
};

struct FileChunk
{
    qint64  offset{};   // byte offset in file
    qsizetype size{};   // length of data segment
};

using chunk_queue_t = Queue<FileChunk>;
