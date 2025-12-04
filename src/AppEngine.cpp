
#include <array>
#include <format>

#include <QStringLiteral>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QTimer>

#include "AppEngine.h"
#include "AudioRecorder.h"
#include "AudioFileWriter.h"
#include "TranscriberWhisper.h"
#include "AudioCaptureDevice.h"

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

constexpr auto model_table = to_array<Model>( {
    Model{ "--None--",   "",   true  },
    Model{ "Tiny",   "tiny",   true  },
    Model{ "Base",   "base",   true  },
    Model{ "Small",  "small",  true  },
    Model{ "Medium", "medium", true  },
    Model{ "Large",  "large-v3", false },
    Model{ "Turbo",  "large-v3-turbo", false },
    });

} // anon ns

void AppEngine::startRecording()
{
    LOG_INFO << "Starting recording";
    if (!canStart()) {
        LOG_WARN_N << "Cannot start recording in current state";
        return;
    }

    assert(recorder_);
    //assert(rec_transcriber_);

    // Clear old file if needed; AudioFileWriter should reopen it
    QFile::remove(pcm_file_path_);

    recorder_->start();
    if (rec_transcriber_) {
        rec_transcriber_->startTranscribingChunks();
    }
    setRecordingState(RecordingState::Recording);
}

void AppEngine::stopRecording()
{
    LOG_INFO << "Stopping recording";
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

    recording_level_ = {};
    emit recordingLevelChanged();

    setRecordingState(RecordingState::Processing);

    if (post_transcriber_) {
        if (rec_transcriber_) {
            rec_transcriber_->stopTranscribing(); // No need to process more buffers if we are doing a second pass...
        }

        // TODO: Start second pass.
        post_transcriber_->postTranscribe();
    }
}

void AppEngine::prepare()
{
    prepareTranscriber();
}

void AppEngine::saveTranscriptToFile(const QUrl &path)
{
    QString filename = path.toLocalFile();

    QFile f(filename);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << current_recorded_text_;
        f.close();
    }
}

void AppEngine::reset()
{
    auto has_distinct_post_transcriber = (!rec_transcriber_ && post_transcriber_)
                                         || (rec_transcriber_ && post_transcriber_ && (rec_transcriber_.get() != post_transcriber_.get()));

    if (rec_transcriber_) {
        LOG_DEBUG_N << "Stopping recorder transcriber";
        rec_transcriber_->stop();
        LOG_DEBUG_N << "Recorder transcriber stopped.";

        QTimer::singleShot(0, this, [=](){
            LOG_TRACE_N << "Resetting recorder transcriber";
            rec_transcriber_.reset();
        });
    }

    if (has_distinct_post_transcriber) {
        LOG_DEBUG_N << "Stopping post transcriber";
        post_transcriber_->stop();
        LOG_DEBUG_N << "Post transcriber stopped.";
    }

    QTimer::singleShot(0, this, [=](){
        LOG_TRACE_N << "Resetting post transcriber";
        post_transcriber_.reset();
    });

    QTimer::singleShot(0, this, [=](){
        LOG_TRACE_N << "Cleaningup and resetting state...";
        if (file_writer_) {
            file_writer_.reset();
        }

        if (recorder_) {
            recorder_.reset();
        }
        setRecordingState(RecordingState::Idle);
    });

    emit recordedTextChanged();
}

AppEngine::AppEngine() {
    QSettings settings{};

    for (const auto &lang : language_table) {
        languages_.append(QString::fromUtf8(lang.name.data(), int(lang.name.size())));
    }

    for (const auto &m : model_table) {
        model_sizes_.append(QString::fromUtf8(m.name.data(), int(m.name.size())));
    }

    language_index_ = settings.value("transcribe.language", 0).toInt();

    model_index_ = settings.value("transcribe.model", -1).toInt();
    post_model_index_ = settings.value("transcribe.post-model", 0).toInt(); // None

    if (model_index_ < 0) {
        // Default to "base".
        model_index_ = std::distance(
            model_table.begin(),
            std::find_if(
                model_table.begin(),
                model_table.end(),
                [](const Model &m) { return m.whisper_size == "base"; }
            )
        );
    }

    const QString baseDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(baseDir);
    pcm_file_path_ = baseDir + QLatin1String("/recording.pcm");
}

bool AppEngine::canPrepare() const
{
    const bool have_selecion = language_index_ >= 0 && (model_index_ >= 1 || post_model_index_ >= 1);
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
        QSettings{}.setValue("transcribe.language", index);
    }
}

void AppEngine::setModelIndex(int index)
{
    assert(index >= 0 && index < int(model_table.size()));
    if (model_index_ != index) {
        model_index_ = index;
        emit modelIndexChanged(index);
        emit stateFlagsChanged();
        QSettings{}.setValue("transcribe.model", index);
    }
}

void AppEngine::setPostModelIndex(int index)
{
    assert(index >= 0 && index < int(model_table.size()));
    if (post_model_index_ != index) {
        post_model_index_ = index;
        emit postModelIndexChanged(index);
        emit stateFlagsChanged();
        QSettings{}.setValue("transcribe.post-model", index);
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
        chunk_queue_ = make_shared<chunk_queue_t>();
    }

    if (!recorder_) {
        recorder_ = make_shared<AudioRecorder>(audio_controller_.currentInputDevice());

        connect(
            recorder_->captureDevice(),
            &AudioCaptureDevice::recordingLevelUpdated,
            this,
            [this] (qreal level) {
                LOG_TRACE_N << "Recording level updated: " << level;
                if (level != recording_level_) {
                    recording_level_ = level;
                    emit recordingLevelChanged();
                }
            },
            Qt::QueuedConnection
        );
    }

    if (!file_writer_) {
        file_writer_ = make_shared<AudioFileWriter>(recorder_->ringBuffer(), chunk_queue_.get(), pcm_file_path_);
    }

    if (!rec_transcriber_ && model_index_ >= 1) {
        const auto &model = model_table.at(size_t(model_index_));
        const auto language = language_table.at(size_t(language_index_));


        string model_id;
        if (model.has_lng_postfix && language.whisper_language == "en") {
            model_id = format("{}.en", model.whisper_size);
        } else {
            model_id = model.whisper_size;
        }

        LOG_INFO_N << "Using Whisper model id: " << model_id;

        rec_transcriber_ = make_shared<TranscriberWhisper>(chunk_queue_.get(), pcm_file_path_, recorder_->format());
        rec_transcriber_->setModelId(QString::fromLatin1(model_id));
        if (!language.whisper_language.empty()) {
            rec_transcriber_->setLanguage(QString::fromUtf8(language.whisper_language.data(), int(language.whisper_language.size())));
        }

        // Relay partial text signals
        connect(
            rec_transcriber_.get(),
            &Transcriber::partialTextAvailable,
            this,
            [this](const QString &text) {
                LOG_TRACE_N << "Partial text available: " << text.toStdString();
                current_recorded_text_ = text;
                emit recordedTextChanged();
            }
        );

        // Relay error signals
        connect(
            rec_transcriber_.get(),
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

        // Handle state changed
        connect(
            rec_transcriber_.get(),
            &Transcriber::stateChanged,
            this,
            &AppEngine::onTranscriberStateChanged);

        // Relay download progress signal
        connect(
            rec_transcriber_.get(),
            &Transcriber::modelDownloadProgress,
            this,
            &AppEngine::modelDownloadProgress
        );

        rec_transcriber_->prepareModel();
    }

    if (!post_transcriber_ && post_model_index_ >= 1) {
        if (post_model_index_ == model_index_) {
            post_transcriber_ = rec_transcriber_;
        } else {
            const auto &model = model_table.at(size_t(model_index_));
            const auto language = language_table.at(size_t(language_index_));
            string model_id;
            if (model.has_lng_postfix && language.whisper_language == "en") {
                model_id = format("{}.en", model.whisper_size);
            } else {
                model_id = model.whisper_size;
            }

            LOG_INFO_N << "Using Whisper model id: " << model_id;

            post_transcriber_ = make_shared<TranscriberWhisper>(chunk_queue_.get(), pcm_file_path_, recorder_->format());
            post_transcriber_->setModelId(QString::fromLatin1(model_id));
            if (!language.whisper_language.empty()) {
                post_transcriber_->setLanguage(QString::fromUtf8(language.whisper_language.data(), int(language.whisper_language.size())));
            }

            // Handle state changed
            connect(
                rec_transcriber_.get(),
                &Transcriber::stateChanged,
                this,
                &AppEngine::onPostTranscriberStateChanged);

            // Relay download progress signal
            connect(
                rec_transcriber_.get(),
                &Transcriber::modelDownloadProgress,
                this,
                &AppEngine::postModelDownloadProgress
                );

            post_transcriber_->prepareModel();
        }

        assert(post_transcriber_);

        connect(post_transcriber_.get(),
                &Transcriber::finalTextAvailable,
                this,
                &AppEngine::onFinalRecordingTextAvailable);

    } else {
        if (rec_transcriber_) {
            connect(rec_transcriber_.get(),
                    &Transcriber::finalTextAvailable,
                    this,
                    &AppEngine::onFinalRecordingTextAvailable);
        }
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

void AppEngine::onTranscriberStateChanged()
{
    assert(rec_transcriber_);
    const auto tst = rec_transcriber_->state();
    LOG_DEBUG_N << "Transcriber state changed to " << tst;
    switch(tst) {
    case Transcriber::State::ERROR:
        setRecordingState(RecordingState::Error);
        emit errorOccurred(tr("Transcriber failed"));
        break;
    case Transcriber::State::READY:
        LOG_TRACE_N << "Transcriber is ready";
        if (recordingState() <= RecordingState::Preparing) {
            if (!post_transcriber_ || (post_transcriber_->state() == Transcriber::State::READY)) {
                setRecordingState(RecordingState::Ready);
            }
        }
        break;
    case Transcriber::State::TRANSACRIBING:
        LOG_TRACE_N << "Transcriber started transcribing";
        break;
    default:
        LOG_TRACE_N << "Ignoring transcriber change state to " << tst;
    }
}

void AppEngine::onPostTranscriberStateChanged()
{
    assert(post_transcriber_);
    const auto tst = post_transcriber_->state();
    switch(tst) {
    default:
    case Transcriber::State::READY:
        LOG_DEBUG_N << "Post-transcriber is ready";
        if (recordingState() <= RecordingState::Preparing) {
            if (!rec_transcriber_ || (rec_transcriber_->state() == Transcriber::State::READY)) {
                setRecordingState(RecordingState::Ready);
            }
        }
        break;
        LOG_TRACE_N << "Ignoring post-transcriber change state to " << tst;
    }
}

void AppEngine::onFinalRecordingTextAvailable(const QString &text)
{
    LOG_INFO_N << "Final text available: " << text.toStdString();
    current_recorded_text_ = text;
    emit recordedTextChanged();
    setRecordingState(RecordingState::Done);
}

