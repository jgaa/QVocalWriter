
#include <array>
#include <format>

#include <QStringLiteral>
#include <QDir>
#include <QStandardPaths>

#include "AppEngine.h"
#include "ChunkQueue.h"
#include "AudioRecorder.h"
#include "AudioFileWriter.h"
#include "TranscriberWhisper.h"

#include "logging.h"

using namespace std;

namespace {

struct Language {
    string_view name;
    string_view whisper_language;
};

constexpr array<Language, 8> language_table = {
    Language{ "Auto",      ""            },
    Language{ "Chinese",   "zh"          },
    Language{ "English",   "en"          },
    Language{ "French",    "fr"          },
    Language{ "German",    "de"          },
    Language{ "Japanese",  "ja"          },
    Language{ "Norwegian", "no"          },
    Language{ "Spanish",   "es"          },
    };

struct Model {
    string_view name;
    string_view whisper_size;
    bool has_lng_postfix{ false};
};

constexpr array<Model, 5> model_table = {
    Model{ "Tiny",   "tiny",   true  },
    Model{ "Base",   "base",   true  },
    Model{ "Small",  "small",  true  },
    Model{ "Medium", "medium", true  },
    Model{ "Large",  "large-v2", false },
    };

} // anon ns

void AppEngine::startRecording()
{
    if (!canStart()) {
        LOG_WARN_N << "Cannot start recording in current state";
        return;
    }

    assert(recorder_);
    assert(transcriber_);

    // Clear old file if needed; AudioFileWriter should reopen it
    QFile::remove(pcm_file_path_);

    recorder_->start();
    transcriber_->start([this](bool ok) {
        if (ok) {
            setRecordingState(RecordingState::Idle);
        } else {
            setRecordingState(RecordingState::Error);
        }
    });
    setRecordingState(RecordingState::Recording);
}

void AppEngine::stopRecording()
{
    if (!canStop()) {
        LOG_WARN_N << "Cannot stop recording in current state";
        return;
    }

    setRecordingState(RecordingState::Processing);

    if (recorder_) {
        recorder_->stop();
    }

    if (file_writer_) {
        file_writer_->stop();
    }

    setRecordingState(RecordingState::Finishing);
}

void AppEngine::prepare()
{
    prepareTranscriber();
}

AppEngine::AppEngine() {
    for (const auto &lang : language_table) {
        languages_.append(QString::fromUtf8(lang.name.data(), int(lang.name.size())));
    }

    for (const auto &m : model_table) {
        model_sizes_.append(QString::fromUtf8(m.name.data(), int(m.name.size())));
    }

    // Default to "medium".
    model_index_ = std::distance(
        model_table.begin(),
        std::find_if(
            model_table.begin(),
            model_table.end(),
            [](const Model &m) { return m.whisper_size == "medium"; }
        )
    );

    assert(model_index_ == 3);

    const QString baseDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(baseDir);
    pcm_file_path_ = baseDir + QLatin1String("/recording.pcm");
}

bool AppEngine::canPrepare() const
{
    const bool have_selecion = language_index_ >= 0 && model_index_ >= 0;
    return recording_state_ == RecordingState::Idle && have_selecion;
}

bool AppEngine::canStart() const
{
    return recording_state_ == RecordingState::Ready;
}

bool AppEngine::canStop() const
{
    return recording_state_ == RecordingState::Recording
           || recording_state_ == RecordingState::Processing;
}

void AppEngine::setLanguageIndex(int index)
{
    assert(index >= 0 && index < int(language_table.size()));
    if (language_index_ != index) {
        language_index_ = index;
        emit languageIndexChanged(index);
        emit stateFlagsChanged();
    }
}

void AppEngine::setModelIndex(int index)
{
    assert(index >= 0 && index < int(model_table.size()));
    if (model_index_ != index) {
        model_index_ = index;
        emit modelIndexChanged(index);
        emit stateFlagsChanged();
    }
}

void AppEngine::setRecordingState(RecordingState newState)
{
    if (recording_state_ != newState) {
        recording_state_ = newState;
        emit recordingStateChanged(newState);
        emit stateFlagsChanged();
    }
}

void AppEngine::createPipelineIfNeeded()
{
    if (!chunk_queue_) {
        chunk_queue_ = make_shared<ChunkQueue>();
    }

    if (!recorder_) {
        recorder_ = make_shared<AudioRecorder>(audio_controller_.currentInputDevice());
    }

    if (!file_writer_) {
        file_writer_ = make_shared<AudioFileWriter>(recorder_->ringBuffer(), chunk_queue_.get(), pcm_file_path_);
    }

    if (!transcriber_) {
        const auto &model = model_table.at(size_t(model_index_));
        const auto language = language_table.at(size_t(language_index_));


        string model_id;
        if (model.has_lng_postfix && language.whisper_language == "en") {
            model_id = format("{}.en", model.whisper_size);
        } else {
            model_id = model.whisper_size;
        }

        LOG_INFO_N << "Using Whisper model id: " << model_id;

        transcriber_ = make_shared<TranscriberWhisper>(chunk_queue_.get(), pcm_file_path_, recorder_->format());
        transcriber_->setModelId(QString::fromLatin1(model_id));
        if (!language.whisper_language.empty()) {
            transcriber_->setLanguage(QString::fromUtf8(language.whisper_language.data(), int(language.whisper_language.size())));
        }

        // Relay partial text signals
        connect(
            transcriber_.get(),
            &Transcriber::partialTextAvailable,
            this,
            &AppEngine::partialTextAvailable
        );

        // Relay error signals
        connect(
            transcriber_.get(),
            &Transcriber::errorOccurred,
            this,
            [this](const QString &msg) {
                if (recording_state_ == RecordingState::Preparing) {
                    onTranscriberPrepared(false, msg);
                }
                else {
                    setRecordingState(RecordingState::Error);
                    emit errorOccurred(msg);
                }
            }
        );

        // Relay ready signal
        connect(
            transcriber_.get(),
            &Transcriber::ready,
            this,
            [this]() {
                onTranscriberPrepared(true, QString{});
            }
        );

        // Relay download progress signal
        connect(
            transcriber_.get(),
            &Transcriber::modelDownloadProgress,
            this,
            &AppEngine::modelDownloadProgress
        );

        transcriber_->start([this](bool ok) {
            LOG_DEBUG_N << "Transcriber finished with status: " << (ok ? "OK" : "Error");
            setRecordingState(RecordingState::DoneRecording);
        });
    }
}

void AppEngine::prepareTranscriber()
{
    if (!canPrepare()) {
        LOG_WARN_N << "Cannot prepare transcriber in current state";
        return;
    }

    setRecordingState(RecordingState::Preparing);
    createPipelineIfNeeded();

    std::jthread([this]() {
        bool ok = true;
        QString errorText;

        // in your real code:
        // ok = m_transcriber->prepare(&errorText);

        QMetaObject::invokeMethod(
            this,
            [this, ok, errorText]() {
                onTranscriberPrepared(ok, errorText);
            },
            Qt::QueuedConnection);
    }).detach();
}

void AppEngine::onTranscriberPrepared(bool ok, const QString &errorText)
{
    if (!ok) {
        setRecordingState(Error);
        emit errorOccurred(errorText.isEmpty()
                              ? tr("Failed to prepare transcriber")
                              : errorText);
        return;
    }

    setRecordingState(Ready);
}

