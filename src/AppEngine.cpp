
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

struct ModelDef {
    string_view name;
    string_view whisper_size;
    bool has_lng_postfix{ false};
};

constexpr auto model_table = to_array<ModelDef>( {
    { "--None--",   "",   true  },
    { "Tiny",   "tiny",   true  },
    { "Base",   "base",   true  },
    { "Small",  "small",  true  },
    { "Medium", "medium", true  },
    { "Large",  "large-v3", false },
    { "Turbo",  "large-v3-turbo", false },
    });

string getTranscriberModelId(const ModelDef &model, const Language &language)
{
    if (model.has_lng_postfix && language.whisper_language == "en") {
        return format("{}.en", model.whisper_size);
    }

    return string{model.whisper_size};
}

} // anon ns

ostream& operator << (ostream& os, AppEngine::RecordingState state) {
    constexpr auto states = to_array<string_view>({
        "Idle",
        "Preparing",
        "Ready",
        "Recording",
        "Processing",
        "Done",
        "Resetting",
        "Error"
    });

    return os << states.at(static_cast<size_t>(state));
}

void AppEngine::startRecording()
{
    LOG_INFO << "Starting recording";
    if (!canStart()) {
        LOG_WARN_N << "Cannot start recording in current state";
        return;
    }

    assert(recorder_);

    // Clear old file if needed; AudioFileWriter should reopen it
    QFile::remove(pcm_file_path_);

    recorder_->start();
    setRecordingState(RecordingState::Recording);

    if (rec_transcriber_) {
        transcribeChunks();
    }
}

void AppEngine::stopRecording()
{
    LOG_INFO << "Stopping recording";
    if (!canStop()) {
        LOG_WARN_N << "Cannot stop recording in current state";
        return;
    }

    if (recorder_) {
        recorder_->stop();
    }

    if (file_writer_) {
        file_writer_->stop();
    }

    recording_level_ = {};
    emit recordingLevelChanged();

    onRecordingDone();
}

void AppEngine::prepareForRecording()
{
    if (!canPrepare()) {
        failed("Cannot prepare in this state.");
        return;
    }

    startPrepareForRecording();
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
    doReset();
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

    int default_transcriber_model = model_index_ = distance(
        model_table.begin(),
        ranges::find_if(model_table,
            [](const auto &m) { return m.whisper_size == "base"; }
            )
        );

    model_index_ = settings.value("transcribe.model", 0).toInt(); // None
    post_model_index_ = settings.value("transcribe.post-model", default_transcriber_model).toInt();

    const QString baseDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(baseDir);
    pcm_file_path_ = baseDir + QLatin1String("/recording.pcm");

    model_mgr_ = make_shared<ModelMgr>();
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
        LOG_DEBUG_N << "Recording state changed from " << recording_state_ << " to " << newState;
        recording_state_ = newState;
        emit recordingStateChanged(newState);
        emit stateFlagsChanged();
    }
}

// void AppEngine::createPipelineIfNeeded()
// {
//     if (!chunk_queue_) {
//         chunk_queue_ = make_shared<chunk_queue_t>();
//     }

//     if (!recorder_) {
//         recorder_ = make_shared<AudioRecorder>(audio_controller_.currentInputDevice());

//         connect(
//             recorder_->captureDevice(),
//             &AudioCaptureDevice::recordingLevelUpdated,
//             this,
//             [this] (qreal level) {
//                 LOG_TRACE_N << "Recording level updated: " << level;
//                 if (level != recording_level_) {
//                     recording_level_ = level;
//                     emit recordingLevelChanged();
//                 }
//             },
//             Qt::QueuedConnection
//         );
//     }

//     if (!file_writer_) {
//         file_writer_ = make_shared<AudioFileWriter>(recorder_->ringBuffer(), chunk_queue_.get(), pcm_file_path_);
//     }

//     if (!rec_transcriber_ && model_index_ >= 1) {
//         const auto &model = model_table.at(size_t(model_index_));
//         const auto language = language_table.at(size_t(language_index_));


//         string model_id;
//         if (model.has_lng_postfix && language.whisper_language == "en") {
//             model_id = format("{}.en", model.whisper_size);
//         } else {
//             model_id = model.whisper_size;
//         }

//         LOG_INFO_N << "Using Whisper model id: " << model_id;

//         rec_transcriber_ = make_shared<TranscriberWhisper>(chunk_queue_.get(), pcm_file_path_, recorder_->format());
//         rec_transcriber_->setModelId(QString::fromLatin1(model_id));
//         if (!language.whisper_language.empty()) {
//             rec_transcriber_->setLanguage(QString::fromUtf8(language.whisper_language.data(), int(language.whisper_language.size())));
//         }

//         // Relay partial text signals
//         connect(
//             rec_transcriber_.get(),
//             &Transcriber::partialTextAvailable,
//             this,
//             [this](const QString &text) {
//                 LOG_TRACE_N << "Partial text available: " << text.toStdString();
//                 current_recorded_text_ = text;
//                 emit recordedTextChanged();
//             }
//         );

//         // Relay error signals
//         connect(
//             rec_transcriber_.get(),
//             &Transcriber::errorOccurred,
//             this,
//             [this](const QString &msg) {
//                 if (recording_state_ == RecordingState::Preparing) {
//                     onTranscriberPrepared(false, msg);
//                 }
//                 else {
//                     setRecordingState(RecordingState::Error);
//                     emit errorOccurred(msg);
//                 }
//             }
//         );

//         // Handle state changed
//         connect(
//             rec_transcriber_.get(),
//             &Transcriber::stateChanged,
//             this,
//             &AppEngine::onTranscriberStateChanged);

//         // Relay download progress signal
//         connect(
//             rec_transcriber_.get(),
//             &Transcriber::modelDownloadProgress,
//             this,
//             &AppEngine::modelDownloadProgress
//         );

//         rec_transcriber_->prepareModel();
//     }

//     if (!post_transcriber_ && post_model_index_ >= 1) {
//         if (post_model_index_ == model_index_) {
//             post_transcriber_ = rec_transcriber_;
//         } else {
//             const auto &model = model_table.at(size_t(model_index_));
//             const auto language = language_table.at(size_t(language_index_));
//             string model_id;
//             if (model.has_lng_postfix && language.whisper_language == "en") {
//                 model_id = format("{}.en", model.whisper_size);
//             } else {
//                 model_id = model.whisper_size;
//             }

//             LOG_INFO_N << "Using Whisper model id: " << model_id;

//             post_transcriber_ = make_shared<TranscriberWhisper>(chunk_queue_.get(), pcm_file_path_, recorder_->format());
//             post_transcriber_->setModelId(QString::fromLatin1(model_id));
//             if (!language.whisper_language.empty()) {
//                 post_transcriber_->setLanguage(QString::fromUtf8(language.whisper_language.data(), int(language.whisper_language.size())));
//             }

//             // Handle state changed
//             connect(
//                 rec_transcriber_.get(),
//                 &Transcriber::stateChanged,
//                 this,
//                 &AppEngine::onPostTranscriberStateChanged);

//             // Relay download progress signal
//             connect(
//                 rec_transcriber_.get(),
//                 &Transcriber::modelDownloadProgress,
//                 this,
//                 &AppEngine::postModelDownloadProgress
//                 );

//             post_transcriber_->prepareModel();
//         }

//         assert(post_transcriber_);

//         connect(post_transcriber_.get(),
//                 &Transcriber::finalTextAvailable,
//                 this,
//                 &AppEngine::onFinalRecordingTextAvailable);

//     } else {
//         if (rec_transcriber_) {
//             connect(rec_transcriber_.get(),
//                     &Transcriber::finalTextAvailable,
//                     this,
//                     &AppEngine::onFinalRecordingTextAvailable);
//         }
//     }
// }

QCoro::Task<void> AppEngine::startPrepareForRecording()
{
    LOG_INFO_N << "Preparing for recording";
    setRecordingState(RecordingState::Preparing);

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

    const auto ok = co_await prepareTranscriberModels();
    if (!ok) {
        failed(tr("Failed to prepare for recording"));
        co_return;
    }

    co_return;
}

QCoro::Task<bool> AppEngine::prepareTranscriberModels()
{
    // Live transcriber.Optional.
    assert(rec_transcriber_ == nullptr);
    if (model_index_ > 0) {
        const auto &model = model_table.at(size_t(model_index_));
        const auto language = language_table.at(size_t(language_index_));
        const auto model_id = getTranscriberModelId(model, language);
        if (rec_transcriber_ = co_await prepareModel(
            model_id,
            language.whisper_language,
            true, // Load the live transcriber so it reacts faster when we start recording
            false // Do not submit final text yet
            ); !rec_transcriber_) {
            co_return failed(tr("Failed to prepare live transcriber") + ": " + QString::fromUtf8(model.name));
        }
    }

    assert(post_transcriber_ == nullptr);
    if (post_model_index_ > 0) {
        const auto &model = model_table.at(size_t(post_model_index_));
        const auto language = language_table.at(size_t(language_index_));
        const auto model_id = getTranscriberModelId(model, language);

        if (post_transcriber_ = co_await prepareModel(
                model_id,
                language.whisper_language,
                false, // Be lazy
                true // Submit final text
                ); !post_transcriber_) {
            co_return failed(tr("Failed to prepare post-recording transcriber") + ": " + QString::fromUtf8(model.name));
        }
    }

    setRecordingState(RecordingState::Ready);

    co_return true;
}

QCoro::Task<shared_ptr<Transcriber>> AppEngine::prepareModel(string_view modelId,
                                          string_view language,
                                          bool loadModel,
                                          bool submitFilalText)
{
    auto cfg = make_unique<Transcriber::Config>();
    cfg->model_name = modelId;
    cfg->from_language = language;
    cfg->submit_filal_text = submitFilalText;

    shared_ptr<TranscriberWhisper> transcriber = make_shared<
        TranscriberWhisper>(std::move(cfg), chunk_queue_.get(), pcm_file_path_, recorder_->format());

    // Relay partial text signals
    connect(
        transcriber.get(),
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
        transcriber.get(),
        &Transcriber::errorOccurred,
        this,
        [this](const QString &msg) {
            failed(msg);
        }
    );

    // Relay download progress signal
    connect(
        transcriber.get(),
        &Transcriber::modelDownloadProgress,
        this,
        &AppEngine::modelDownloadProgress
        );

    if (submitFilalText) {
        // Relay final text signal
        connect(post_transcriber_.get(),
                &Transcriber::finalTextAvailable,
                this,
                &AppEngine::onFinalRecordingTextAvailable);
    }

    co_await transcriber->init(QString::fromUtf8(modelId));

    if (loadModel) {
        co_await transcriber->loadModel();
    }

    co_return transcriber;
}

// void AppEngine::prepareTranscriber()
// {
//     if (!canPrepare()) {
//         LOG_WARN_N << "Cannot prepare transcriber in current state";
//         return;
//     }

//     setRecordingState(RecordingState::Preparing);
//     createPipelineIfNeeded();
// }

// void AppEngine::onTranscriberPrepared(bool ok, const QString &errorText)
// {
//     if (!ok) {
//         setRecordingState(Error);
//         emit errorOccurred(errorText.isEmpty()
//                               ? tr("Failed to prepare transcriber")
//                               : errorText);
//         return;
//     }

//     setRecordingState(Ready);
// }

// void AppEngine::onPostTranscriberStateChanged()
// {
//     assert(post_transcriber_);
//     const auto tst = post_transcriber_->state();
//     switch(tst) {
//     default:
//     case Transcriber::State::READY:
//         LOG_DEBUG_N << "Post-transcriber is ready";
//         if (recordingState() <= RecordingState::Preparing) {
//             if (!rec_transcriber_ || (rec_transcriber_->state() == Transcriber::State::READY)) {
//                 setRecordingState(RecordingState::Ready);
//             }
//         }
//         break;
//         LOG_TRACE_N << "Ignoring post-transcriber change state to " << tst;
//     }
// }

void AppEngine::onFinalRecordingTextAvailable(const QString &text)
{
    LOG_INFO_N << "Final text available: " << text.toStdString();
    current_recorded_text_ = text;
    emit recordedTextChanged();
    setRecordingState(RecordingState::Done);
}

QCoro::Task<void> AppEngine::transcribeChunks()
{
    if (!co_await rec_transcriber_->transcribeChunks()) {
        failed(tr("Live transcription failed"));
        co_return;
    }

    co_await onRecordingDone();

    co_return;
}

QCoro::Task<void> AppEngine::onRecordingDone()
{
    if (post_transcriber_) {
        if (rec_transcriber_) {
            rec_transcriber_->stopTranscribing();
        }

        setRecordingState(RecordingState::Processing);
        assert(post_transcriber_->haveModel());
        if (!post_transcriber_->isLoaded()) {
            co_await post_transcriber_->loadModel();
        }

        if (!co_await post_transcriber_->transcribeRecording()) {
            failed(tr("Post-processing transcription failed"));
            co_return;
        }
    }

    setRecordingState(RecordingState::Done);
}

bool AppEngine::failed(const QString &why)
{
    LOG_ERROR_N << "Recording failed: " << why;
    emit errorOccurred(why);
    setRecordingState(RecordingState::Error);
    return false;
}

QCoro::Task<void> AppEngine::doReset()
{
    setRecordingState(RecordingState::Resetting);

    if (rec_transcriber_) {
        co_await rec_transcriber_->stop();

        // reset later
        QTimer::singleShot(0, this, [this]() {
            rec_transcriber_.reset();
        });
    }

    if (post_transcriber_) {
        co_await post_transcriber_->stop();
        // reset later
        QTimer::singleShot(0, this, [this]() {
            rec_transcriber_.reset();
        });
    }

    if (file_writer_) {
        file_writer_.reset();
    }

    if (recorder_) {
        recorder_.reset();
    }

    setRecordingState(RecordingState::Idle);
    emit recordedTextChanged();
}

