#pragma once

#include <mutex>
#include <deque>
#include <condition_variable>

#include <QObject>

struct FileChunk
{
    qint64  offset{};   // byte offset in file
    qsizetype size{};   // length of data segment
};


class ChunkQueue
{
public:
    ChunkQueue() = default;

    void push(const FileChunk &chunk)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(chunk);
        }
        cv_.notify_one();
    }

    bool pop(FileChunk &out)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]{ return !queue_.empty() || stopped_; });
        if (queue_.empty())
            return false;
        out = queue_.front();
        queue_.pop_front();
        return true;
    }

    void stop();

private:
    std::mutex              mutex_;
    std::condition_variable cv_;
    std::deque<FileChunk>   queue_;
    bool                    stopped_{false};
};
