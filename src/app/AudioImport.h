#pragma once

#include <span>
#include <vector>
#include <mutex>

#include <QObject>

#include <qcorotask.h>

/*! Audio import helper.
 *
 *  Currently a class instance is designed to import one file.
 *
 *  The decoding happens in a worker-thread in the Qt thread-pool.
 */
class AudioImport : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Created,
        Decoding,
        Done,
        Error
    };

    AudioImport(QObject *parent = nullptr);


    /*! Returns the decoded audio samples in mono 16kHz float format.
     *
     *  This can be passed directly to the transcription engine.
     *
     * @return Span of float samples in range [-1.0, 1.0].
     */
    [[nodiscard]] std::span<const float> mono16kData() const noexcept;

    QString errorMessage() const noexcept;

    /*! Decodes audio from the media file at the given path.
     *
     * Supported input formats depend on the underlying audio decoding library.
     *
     * Note that this is not a general audio decoding class - it's only meant to provide
     *      audio data suitable for processing by the projects transcription engine, which today
     *      use Whisper.
     *
     * @param filePath Path to the audio file to decode.
     *
     * @return true on success, false on failure.

     */
    [[nodiscard]] QCoro::Task<bool> decodeMediaFile(const QString &filePath);

    State state() const noexcept;

signals:
    void stateChanged();

private:
    bool decode(const QString &filePath);

    void setErrorMessage(const QString &msg) noexcept;
    void setState(State newState) noexcept;

    QString error_message_;
    std::vector<float> samples_;
    State state_{State::Created};
    mutable std::mutex mutex_;
};

std::ostream& operator << (std::ostream& os, AudioImport::State state);
