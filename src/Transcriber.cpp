
#include <array>
#include <string_view>

#include "Transcriber.h"
#include "AudioRecorder.h"

#include "logging.h"

using namespace std;



Transcriber::Transcriber(std::string name,
                         std::unique_ptr<Config> && config,
                         chunk_queue_t *queue,
                         const QString &pcmFilePath,
                         QAudioFormat format)
    : Model(std::move(name), std::move(config)), queue_(queue), file_(pcmFilePath), format_(format)
{
    const auto *m = dynamic_cast<Model*>(this);
    assert(m);
    if (!file_.open(QIODevice::ReadOnly)) {
        LOG_ERROR_EX(*m) << "Failed to open file for writing: " << pcmFilePath;
        throw runtime_error("Failed to open file for writing");
        return;
    }
}

QCoro::Task<bool> Transcriber::transcribeChunks()
{
    auto op = make_unique<Model::Operation>([this]() -> bool {
        return transcribeSegments();
    });

    auto future = op->future();

    LOG_TRACE_EX(*this) << "Enqueuing TranscribeChunks command...";
    enqueueCommand(std::move(op));
    const auto result = co_await future;
    LOG_TRACE_EX(*this) << "TranscribeChunks command completed.";
    co_return result;
}

QCoro::Task<bool> Transcriber::transcribeRecording()
{
    auto op = make_unique<Model::Operation>([this]() -> bool {
        LOG_TRACE_EX(*this) << "Callig processRecordingFromFile... from worker thread";
        processRecordingFromFile();
        LOG_TRACE_EX(*this) << "processRecordingFromFile completed.";
        return true;
    });

    auto future = op->future();

    LOG_TRACE_EX(*this) << "Enqueuing TranscribeRecording command...";
    enqueueCommand(std::move(op));
    const auto result = co_await future;
    LOG_TRACE_EX(*this) << "TranscribeRecording command completed.";
    co_return result;
}

void Transcriber::stopTranscribing() {
    if (state() == State::RUNNING) {
        LOG_TRACE_EX(*this) << "Stopping ongoing transibing...";
        setState(State::STOPPING);
    }
}

const string &Transcriber::language() const noexcept
{
    return config().from_language;
}


bool Transcriber::transcribeSegments()
{
    // Assumed to run in worker_ thread
    assert(worker());
    assert(this_thread::get_id() == worker()->get_id());

    assert(file_.isOpen());
    if (!file_.isOpen()) {
        LOG_ERROR_EX(*this) << "Transcriber: file not open";
        return false;
    }

    FileChunk fc;
    auto segment = 0u;

    while(!cancelled()) {
        if (!queue_->pop(fc)) {
            LOG_DEBUG_EX(*this) << "Transcriber: queue stopped or empty";

            // Finish transcription of any remaining data
            processChunk({}, true);
            return true;;
        }

        LOG_TRACE_EX(*this) << "Reading #" << ++segment << " offset=" << fc.offset << " size=" << fc.size;

        if (!file_.seek(fc.offset)) {
            LOG_ERROR_EX(*this) << "Transcriber: failed to seek to offset " << fc.offset;
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
                // LOG_TRACE_EX(*this) << "Transcriber: reading chunk at offset " << (fc.offset + read + chunk_read)
                //             << " size=" << (chunk_size - chunk_read);
                const auto bytes = file_.read(buffer.data() + chunk_read, static_cast<qint64>(chunk_size - chunk_read));
                assert(bytes >= 0);
                if (bytes < 0) {
                    LOG_ERROR_EX(*this) << "Transcriber: failed to read " << (chunk_size - chunk_read)
                                << " bytes from file at offset "
                                << (fc.offset + read + chunk_read);
                    emit errorOccurred("Transcriber: file read error");
                    setState(State::ERROR);
                    return false;;
                }
                chunk_read += static_cast<size_t>(bytes);
            } while (chunk_read < chunk_size);

            if (chunk_read > 0) {
                read += chunk_read;
                try {
                    processChunk({reinterpret_cast<const uint8_t*>(buffer.data()), chunk_read});
                } catch (const exception& ex) {
                    LOG_ERROR_EX(*this) << "Transcriber: exception during processChunk: " << ex.what();
                    emit errorOccurred(QString("Transcriber: exception during processChunk: %1").arg(ex.what()));
                    setState(State::ERROR);
                    return false;
                }
            } else {
                LOG_WARN_EX(*this) << "Transcriber: read 0 bytes...";
            }
        }
    }

    return true;
}

void Transcriber::processRecordingFromFile()
{
    assert(file_.isOpen());
    file_.seek(0);

    LOG_DEBUG_EX(*this) << name() << ": Post-processing complete recording from file, size=" << file_.size();

    static_assert(AUDIO_BUFFER_SIZE % sizeof(qint16) == 0, "AUDIO_BUFFER_SIZE must be aligned with sizeof(qint16)");

    array<char, AUDIO_BUFFER_SIZE> buffer;
    vector<float> whisper_pcm;
    whisper_pcm.resize(file_.size() / sizeof(qint16));

    qint64 total_read = 0;
    // Read the entire file in chunks and copy the data to the whisper_pcm buffer
    while (total_read < file_.size()) {
        const auto to_read = min<size_t>(buffer.size(), static_cast<size_t>(file_.size() - total_read));
        const auto bytes = file_.read(buffer.data(), static_cast<qint64>(to_read));
        if (bytes <= 0) {
            LOG_ERROR_EX(*this) << "Transcriber: failed to read from file during post-processing";
            emit errorOccurred("Transcriber: file read error during post-processing");
            setState(State::ERROR);
            return;
        }

        // Copy and convert to float
        const auto samples = bytes / sizeof(qint16);
        const auto* src = reinterpret_cast<const qint16*>(buffer.data());
        for (size_t i = 0; i < samples; ++i) {
            whisper_pcm[(total_read / sizeof(qint16)) + i] = static_cast<float>(src[i]) / 32768.0f;
        }

        total_read += bytes;
    }

    processRecording(std::span<const float>(whisper_pcm.data(), whisper_pcm.size()));
}
