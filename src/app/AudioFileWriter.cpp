#include "AudioFileWriter.h"

#include "logging.h"

using namespace std;

AudioFileWriter::AudioFileWriter(AudioRingBuffer *ring, chunk_queue_t *chunkQueue, const QString &filePath)
    : ring_(ring),
    chunkQueue_(chunkQueue),
    file_(filePath)
{
    LOG_DEBUG_N << "Creating AudioFileWriter for file" << filePath;
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_WARN_N << "Failed to open file" << filePath;
        throw runtime_error("Failed to open file");
    }
    thread_ = std::jthread([this] { run(); });
}

AudioFileWriter::~AudioFileWriter()
{
    stop();
}

void AudioFileWriter::stop()
{
    LOG_DEBUG_N << "Stopping AudioFileWriter";
    if (stopped_.exchange(true))
        return;
    if (ring_)
        ring_->stop();
    if (chunkQueue_)
        chunkQueue_->stop();
    if (thread_.joinable())
        thread_.join();
    file_.close();
}

void AudioFileWriter::run()
{
    if (!file_.isOpen()) {
        LOG_ERROR_N << "AudioFileWriter: file not open";
        return;
    }

    AudioRingBuffer::Chunk chunk;
    qint64 currentOffset = 0;
    auto segment = 0u;

    while (!stopped_) {
        if (!ring_->pop(chunk)) {
            LOG_DEBUG_N << "AudioFileWriter: ring buffer stopped or empty";
            break; // stopped or no more data
        }

        LOG_TRACE_N << "Writing #" << ++segment << " offset=" << currentOffset << " size=" << chunk.size();

        const qint64 written = file_.write(chunk);
        if (written <= 0) {
            LOG_ERROR_N << "AudioFileWriter: failed to write to file";
            break;
        }

        FileChunk fc{ currentOffset, static_cast<qsizetype>(written) };
        currentOffset += written;
        chunkQueue_->push(std::move(fc));
    }
}
