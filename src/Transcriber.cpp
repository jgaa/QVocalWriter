#include "Transcriber.h"
#include "ChunkQueue.h"
#include "AudioRecorder.h"

#include "logging.h"

using namespace std;

Transcriber::Transcriber(ChunkQueue *queue, const QString &filePath, QAudioFormat format)
: queue_(queue), file_(filePath), format_(format)
{
    if (!file_.open(QIODevice::ReadOnly)) {
        LOG_ERROR_N << "Failed to open file for writing: " << filePath.toStdString();
        throw runtime_error("Failed to open file for writing");
        return;
    }
}

void Transcriber::start(finished_t finish)
{
    finished_fn_ = std::move(finish);
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
    const auto stopped = stopped_.load();
    assert(stopped == true);
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
    auto segment = 0u;

    while(!stopped_) {
        if (!queue_->pop(fc)) {
            LOG_DEBUG_N << "Transcriber: queue stopped or empty";
            processChunk({}, true);
            break;
        }

        //LOG_TRACE_N << "Reading #" << ++segment << " offset=" << fc.offset << " size=" << fc.size;

        if (!file_.seek(fc.offset)) {
            LOG_ERROR_N << "Transcriber: failed to seek to offset " << fc.offset;
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
                // LOG_TRACE_N << "Transcriber: reading chunk at offset " << (fc.offset + read + chunk_read)
                //             << " size=" << (chunk_size - chunk_read);
                const auto bytes = file_.read(buffer.data() + chunk_read, static_cast<qint64>(chunk_size - chunk_read));
                assert(bytes >= 0);
                if (bytes < 0) {
                    LOG_ERROR_N << "Transcriber: failed to read " << (chunk_size - chunk_read)
                                << " bytes from file at offset "
                                << (fc.offset + read + chunk_read);
                    emit errorOccurred("Transcriber: file read error");
                    goto failed;
                }
                chunk_read += static_cast<size_t>(bytes);
            } while (chunk_read < chunk_size);

            if (chunk_read > 0) {
                read += chunk_read;
                try {
                    processChunk({reinterpret_cast<const uint8_t*>(buffer.data()), chunk_read});
                } catch (const exception& ex) {
                    LOG_ERROR_N << "Transcriber: exception during processChunk: " << ex.what();
                    emit errorOccurred(QString("Transcriber: exception during processChunk: %1").arg(ex.what()));
                    goto failed;
                }
            } else {
                LOG_WARN_N << "Transcriber: read 0 bytes...";
            }
        }
    }

failed:

    if (finished_fn_) {
        finished_fn_(true);
    }
}
