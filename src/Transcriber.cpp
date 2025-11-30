#include "Transcriber.h"
#include "ChunkQueue.h"

#include "logging.h"

using namespace std;

Transcriber::Transcriber(ChunkQueue *queue, const QString &filePath, QAudioFormat format)
: queue_(queue), file_(filePath), format_(format)
{
    if (!file_.open(QIODevice::WriteOnly)) {
        LOG_ERROR_N << "Failed to open file for writing: " << filePath.toStdString();
        throw runtime_error("Failed to open file for writing");
        return;
    }
}

void Transcriber::start(finished_t finish)
{
    finished_ = std::move(finish);
    thread_ = std::jthread([this] { run(); });
}


void Transcriber::stop()
{
    stopped_ = true;
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
}

void Transcriber::run()
{
    assert(stopped_);
    stopped_ = false;

    if (!init()) {
        LOG_ERROR_N << "Transcriber: initialization failed";
        emit errorOccurred("Transcriber initialization failed");
        return;
    }

    assert(file_.isOpen());
    if (!file_.isOpen()) {
        LOG_ERROR_N << "Transcriber: file not open";
        return;
    }

    FileChunk fc;

    while(!stopped_) {
        if (!queue_->pop(fc)) {
            LOG_DEBUG_N << "Transcriber: queue stopped or empty";
            break;
        }

        if (!file_.seek(fc.offset)) {
            LOG_ERROR_N << "Transcriber: failed to seek to offset " << fc.offset;
            continue;
        }

        QByteArray data = file_.read(fc.size);
        if (data.size() != fc.size) {
            LOG_WARN_N << "Transcriber: failed to read expected size " << fc.size;
        }

        if (data.isEmpty()) {
            LOG_WARN_N << "Transcriber: read empty data at offset " << fc.offset;
            continue;
        }
    }

    if (finished_) {
        finished_(true);
    }
}
