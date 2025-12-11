
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

    setStateText();

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
    assert(model_mgr_);

    connect(
        model_mgr_.get(),
        &ModelMgr::downloadProgress,
        this,
        &AppEngine::downloadProgress
        );

    connect (
        &audio_controller_,
        &AudioController::inputDevicesChanged,
        this,
        &AppEngine::microphonesChanged
        );

    connect (
        &audio_controller_,
        &AudioController::currentInputDeviceChanged,
        this,
        &AppEngine::currentMicChanged
        );
}

QStringList AppEngine::microphones() const
{
    QStringList mics;
    audio_controller_.inputDevices();

    for (const auto &dev : audio_controller_.inputDevices()) {
        mics.append(dev.description());
    }

    return mics;
}

int AppEngine::currentMic() const
{
    return audio_controller_.getCurrentDeviceIndex();
}

void AppEngine::setCurrentMic(int index)
{
    audio_controller_.setInputDevice(index);
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
        setStateText();
    }
}

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
            "live-transcriber"s,
            model_id,
            language.whisper_language,
            true, // Load the live transcriber so it reacts faster when we start recording
            (post_model_index_ < 1) // Submit final text?
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
                "post-transcriber"s,
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

QCoro::Task<shared_ptr<Transcriber>> AppEngine::prepareModel(
    std::string name, string_view modelId, string_view language,
    bool loadModel, bool submitFilalText)
{
    auto cfg = make_unique<Transcriber::Config>();
    cfg->model_name = modelId;
    cfg->from_language = language;
    cfg->submit_filal_text = submitFilalText;

    shared_ptr<TranscriberWhisper> transcriber = make_shared<
        TranscriberWhisper>(name, std::move(cfg), chunk_queue_.get(), pcm_file_path_, recorder_->format());

    // Relay partial text signals
    connect(
        transcriber.get(),
        &Model::partialTextAvailable,
        this,
        [this](const QString &text) {
            LOG_TRACE_N << "Partial text available: " << text;
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

    if (submitFilalText) {
        // Relay final text signal
        LOG_TRACE_N << "Connecting finalTextAvailable signal for transcriber: " << name;
        connect(transcriber.get(),
                &Model::finalTextAvailable,
                [this](const QString &text) {
                    LOG_TRACE_N << "Final text available: " << text;
                    onFinalRecordingTextAvailable(text);
                });
    }


    co_await transcriber->init(QString::fromUtf8(modelId));

    if (loadModel) {
        co_await transcriber->loadModel();
    }

    co_return transcriber;
}

void AppEngine::onFinalRecordingTextAvailable(const QString &text)
{
    LOG_INFO_N << "Final text available: " << text.toStdString();
    current_recorded_text_ = text;
    emit recordedTextChanged();
    //setRecordingState(RecordingState::Done);
}

QCoro::Task<void> AppEngine::transcribeChunks()
{
    if (!co_await rec_transcriber_->transcribeChunks()) {
        failed(tr("Live transcription failed"));
        co_return;
    }

    co_return;
}

QCoro::Task<void> AppEngine::onRecordingDone()
{
    LOG_DEBUG_N << "Recording done, starting post-processing transcription if needed";
    if (post_transcriber_) {
        LOG_DEBUG_N << "Stopping live transcriber before post-processing";
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
    LOG_DEBUG_N << "Resetting AppEngine";
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
            post_transcriber_.reset();
        });
    }

    file_writer_.reset();
    recorder_.reset();
    chunk_queue_.reset();

    current_recorded_text_.clear();
    setRecordingState(RecordingState::Idle);
    emit recordedTextChanged();
    LOG_TRACE_N << "Reset done";
}


void AppEngine::setStateText(QString text) {

    static const auto rec_names = to_array<QString>({
        tr("Idle"),
        tr("Preparing"),
        tr("Ready"),
        tr("Recording"),
        tr("Processing"),
        tr("Done"),
        tr("Resetting"),
        tr("Error")
    });

    if (text.isEmpty()) {
        text = rec_names.at(static_cast<int>(recording_state_));
    }

    if (text != state_text_) {
        LOG_DEBUG_N << "State text changed to: " << text.toStdString();
        state_text_ = text;
        emit stateTextChanged();
    }
}
