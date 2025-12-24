
#include <array>
#include <format>
#include <ranges>

#include <QStringLiteral>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QTimer>
#include <QCollator>

#include "AppEngine.h"
#include "AudioRecorder.h"
#include "AudioFileWriter.h"
#include "TranscriberWhisper.h"
#include "AudioCaptureDevice.h"
#include "GeneralModel.h"
#include "ChatConversation.h"

#include "logging.h"

using namespace std;

namespace {

// string getTranscriberModelId(const ModelDef &model, const Language &language)
// {
//     if (model.has_lng_postfix && language.whisper_language == "en") {
//         return format("{}.en", model.whisper_size);
//     }

//     return string{model.whisper_size};
// }

template <typename T>
constexpr bool is_one_of(T value, std::initializer_list<T> list)
{
    for (auto v : list)
        if (v == value)
            return true;
    return false;
}

} // anon ns

ostream& operator << (ostream& os, AppEngine::State state) {
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
    setState(State::Recording);

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

void AppEngine::prepareForChat()
{
    LOG_TRACE_N << "Preparing for chat with model: " << chat_models_.currentId();
    if (chat_models_.empty()) {
        failed("No chat model is selected.");
        return;
    }
    startPrepareForChat(QString::fromUtf8(chat_models_.currentId()));
}

void AppEngine::chatPrompt(const QString &prompt)
{
    sendChatPrompt(prompt);
}

QCoro::Task<bool> AppEngine::sendChatPrompt(const QString &prompt)
{
    if (!chat_model_) {
        failed("Chat model is not prepared.");
        co_return false;
    }

    if (!chat_model_->isLoaded()) {
        failed ("Chat model is not loaded.");
        co_return false;
    }

    LOG_INFO_N << "Sending chat prompt: " << prompt;

    // TODO: Compress conversation history summaries if we run out of tokens for the model

    if (!chat_conversation_) {
        startChatConversation(tr("Unnamed"));
    }

    auto current_conversation = chat_conversation_;

    assert(current_conversation);
    current_conversation->addMessage(make_shared<ChatMessage>(PromptRole::User, prompt.toStdString()));
    auto formatted_prompt = chat_model_->modelInfo().formatPrompt(chat_conversation_->getLastMessageAsView()); //getMessages());

    setState(State::Processing);

    // Handle partial updates
    current_conversation->addMessage(make_shared<ChatMessage>(PromptRole::Assistant, ""));

    connect(chat_model_.get(), &Model::partialTextAvailable, [this](const QString& msg) {
        LOG_TRACE_N << "Chat model partial text available: " << msg;
        if (chat_conversation_) {
            chat_conversation_->updateLastMessage(msg.toStdString());
        }
    });

    // TODO: Handle replay of the full conversation if we resumed an existing one from storage.
    // Conversation continuation is handled by the model context params.
    auto result = co_await chat_model_->prompt(std::move(formatted_prompt),
                                               qvw::LlamaSessionCtx::Params::Chat(true));
    assert(current_conversation);
    assert(chat_model_);

    // unconnect partial text signal
    disconnect(chat_model_.get(), &GeneralModel::partialTextAvailable, nullptr, nullptr);


    // (we just change the text of the last message (agent message for partial updates) and set the completed flag to true).
    current_conversation->updateLastMessage(chat_model_->finalText());
    current_conversation->finalizeLastMessage();
    setState(State::Ready);
    co_return result;
}

void AppEngine::prepareAvailableModels()
{
    chat_models_.SetModels(
        ModelMgr::instance().availableModels(
            chat_models_.kind(), ModelInfo::Capability::Chat));

    live_transcribe_models_.SetModels(
        ModelMgr::instance().availableModels(
            live_transcribe_models_.kind(), ModelInfo::Capability::Transcribe));

    post_transcribe_models_.SetModels(
        ModelMgr::instance().availableModels(
            post_transcribe_models_.kind(), ModelInfo::Capability::Transcribe));
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

void AppEngine::startChatConversation(const QString &name)
{
    LOG_INFO_N << "Starting chat conversation: " << name.toStdString();
    chat_conversation_ = make_shared<ChatConversation>(name);
    chat_conversation_->setModel(&chat_messages_model_);
    chat_conversation_->addMessage(make_shared<ChatMessage>(PromptRole::System, getChatSystemPrompt()));
}

AppEngine::AppEngine() {
    QSettings settings{};

    setStateText();
    prepareLanguages();
    language_index_ = 0; // auto

    model_mgr_ = make_shared<ModelMgr>();
    assert(model_mgr_);

    /*! We store the language as it's whisper code so that we can update languages in the
     *  future without breaking user settings.
     */
    if (auto ln = settings.value("transcribe.language", "").toString(); !ln.isEmpty()) {
        const string target = ln.toStdString();
        for (int i = 0; i < languageList_.size(); ++i) {
            if (languageList_.at(i).whisper_language == target) {
                language_index_ = i;
                break;
            }
        }
    }

    {
        // We store the id, not the model name (which may be localized)
        auto lookup_name = [](const QString &stored_value, ModelKind kind) -> QString {
            const string target = stored_value.toStdString();
            const auto models = ModelMgr::instance().availableModels(kind);
            for (int i = 0; i < models.size(); ++i) {
                if (models[(size_t(i))].id == target) {
                    return QString::fromUtf8(models[(size_t(i))].name);
                }
            }
            return {};
        };

        transcribe_post_model_name_ = lookup_name(settings.value("transcribe.post-model", "base").toString(), ModelKind::WHISPER);
    }

    const QString baseDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(baseDir);
    pcm_file_path_ = baseDir + QLatin1String("/recording.pcm");


    connect(
        model_mgr_.get(),
        &ModelMgr::downloadProgressRatio,
        this,
        &AppEngine::downloadProgressRatio
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

    prepareAvailableModels();

    connect(&chat_models_,
            &AvailableModelsModel::selectedChanged,
            this,
            &AppEngine::stateFlagsChanged);

    connect(&live_transcribe_models_,
            &AvailableModelsModel::selectedChanged,
            this,
            &AppEngine::stateFlagsChanged);

    connect(&post_transcribe_models_,
            &AvailableModelsModel::selectedChanged,
            this,
            &AppEngine::stateFlagsChanged);
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

QStringList AppEngine::translateModels() const
{
    QStringList models;
    auto all = ModelMgr::instance().availableModels(ModelKind::GENERAL, ModelInfo::Capability::Translate);
    for (const auto *m : all) {
        assert(m);
        models.append(QString::fromUtf8(m->name));
    }
    return models;
}

bool AppEngine::canPrepare() const
{
    const bool have_selecion = live_transcribe_models_.hasSelection()
                               || post_transcribe_models_.hasSelection();
    return state() == State::Idle && have_selecion;
}

bool AppEngine::canPrepareForChat() const
{
    return state() == State::Idle && !chat_models_.hasSelection();
}

bool AppEngine::canStart() const
{
    return state_ == State::Ready;
}

bool AppEngine::canStop() const
{
    return stateIn({State::Recording, State::Processing});
}

bool AppEngine::isBusy() const
{
    return stateIn({State::Processing, State::Preparing});
}

void AppEngine::setLanguageIndex(int index)
{
    assert(index >= 0 && index < int(languageList_.size()));
    if (language_index_ != index) {
        language_index_ = index;
        emit languageIndexChanged(index);
        emit stateFlagsChanged();
        QSettings{}.setValue("transcribe.language", QString::fromUtf8(languageList_.at(index).whisper_language));
    }
}

QString AppEngine::chatModelName() const
{
    return chat_models_.selectedModelName();
}

string AppEngine::getChatSystemPrompt() const
{
    constexpr string_view default_prompt = R"(You are a helpful assistant.

Goals:
- Be polite, professional, and direct.
- Prefer correctness over speed.
- If the user’s request is ambiguous or missing key details, ask a clarifying question before answering.
- If you must proceed with incomplete information, state your assumptions explicitly and keep them minimal.

Behavior:
- Do not invent facts. If you are unsure, say so and suggest how to verify.
- When the user asks for an opinion or recommendation, explain the tradeoffs briefly.
- Do not be overly agreeable: push back on incorrect premises and unsafe or unreasonable requests.

Output:
- Respond in Markdown.
- Use short sections and bullet points when helpful.
- Avoid unnecessary preambles and avoid signatures.

Reasoning:
- Provide a brief explanation of your reasoning when it helps the user.
- Do not include hidden chain-of-thought; instead, show assumptions, key steps, and conclusions.
- When generating code, include comments explaining non-trivial parts.)";

    return string{default_prompt};
}


void AppEngine::setState(State newState)
{    
    if (state_ != newState) {
        LOG_DEBUG_N << "Recording state changed from " << state_ << " to " << newState;
        state_ = newState;
        emit stateChanged(newState);
        emit stateFlagsChanged();
        setStateText();
    }
}

QCoro::Task<void> AppEngine::startPrepareForRecording()
{
    LOG_INFO_N << "Preparing for recording";
    setState(State::Preparing);

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

QCoro::Task<void> AppEngine::startPrepareForChat(QString id)
{
    LOG_INFO_N << "Preparing for chat with model: " << id;

    setStateText(tr("Preparing chat model..."));

    auto mi = ModelMgr::instance().findModelById(ModelKind::GENERAL, id);

    if (!mi) {
        failed(tr("Chat model not found: ") + id);
        co_return;
    }

    assert(mi);
    chat_model_ = co_await prepareGeneralModel(
        "chat-model",
        *mi,
        true // Load it
        );

    setState(State::Ready);

    co_return;
}

QCoro::Task<bool> AppEngine::prepareTranscriberModels()
{
    // Live transcriber.Optional.
    assert(rec_transcriber_ == nullptr);
    const auto whisper_models = ModelMgr::instance().availableModels(ModelKind::WHISPER, ModelInfo::Capability::Transcribe);
    if (live_transcribe_models_.hasSelection()) {
        const auto qid = QString::fromUtf8(post_transcribe_models_.currentId());
        if (auto model = ModelMgr::instance().findModelById(ModelKind::WHISPER, qid); model) {

            LOG_DEBUG_N << "Preparing live transcriber model: " << qid;

            const auto language = languageList_.at(size_t(language_index_));
            if (rec_transcriber_ = co_await prepareTranscriber(
                    "live-transcriber"s,
                    *model,
                    language.whisper_language,
                    true, // Load the live transcriber so it reacts faster when we start recording
                    !post_transcribe_models_.hasSelection() // Submit final text?
                    ); !rec_transcriber_) {
                co_return failed(tr("Failed to prepare live transcriber %1").arg(qid));
            }

        } else {
            co_return failed(tr("Transcription model not found: %1").arg(qid));
        }
    }

    // same with post_transcriber_ and transcribe_post_model_name_
    if (post_transcribe_models_.hasSelection()) {
        const auto qid = QString::fromUtf8(post_transcribe_models_.currentId());
        if (auto model = ModelMgr::instance().findModelById(ModelKind::WHISPER, qid); model) {

            LOG_DEBUG_N << "Preparing post transcriber model: " << qid;

            const auto language = languageList_.at(size_t(language_index_));
            if (post_transcriber_ = co_await prepareTranscriber(
                    "post--transcriber"s,
                    *model,
                    language.whisper_language,
                    true, // Load the live transcriber so it reacts faster when we start recording
                    true // Submit final text?
                    ); !rec_transcriber_) {
                co_return failed(tr("Failed to prepare post-transcriber %1").arg(qid));
            }

        } else {
            co_return failed(tr("Transcription model not found: %1").arg(qid));
        }
    }

    setState(State::Ready);

    co_return true;
}

QCoro::Task<std::shared_ptr<GeneralModel> > AppEngine::prepareGeneralModel(
    std::string name, ModelInfo modelInfo, bool loadModel)
{
    const QString model_id = QString::fromUtf8(modelInfo.id);
    auto params = make_unique<Model::Config>();
    params->model_info = std::move(modelInfo);
    params->submit_filal_text = true;
    shared_ptr<GeneralModel> model = make_shared<GeneralModel>(name, std::move(params));

    // Relay error signals
    connect(
        model.get(),
        &GeneralModel::errorOccurred,
        this,
        [this](const QString &msg) {
            failed(msg);
        }
    );

    co_await model->init(model_id);

    if (loadModel) {
        co_await model->loadModel();
    }

    co_return model;
}

QCoro::Task<shared_ptr<Transcriber>> AppEngine::prepareTranscriber(
    std::string name, ModelInfo modelInfo, string_view language,
    bool loadModel, bool submitFilalText)
{
    const QString model_id = QString::fromUtf8(modelInfo.id);
    auto cfg = make_unique<Transcriber::Config>();
    cfg->model_info = modelInfo;
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


    co_await transcriber->init(model_id);

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

        setState(State::Processing);
        assert(post_transcriber_->haveModel());
        if (!post_transcriber_->isLoaded()) {
            co_await post_transcriber_->loadModel();
        }

        if (!co_await post_transcriber_->transcribeRecording()) {
            failed(tr("Post-processing transcription failed"));
            co_return;
        }
    }

    setState(State::Done);
}

bool AppEngine::failed(const QString &why)
{
    LOG_ERROR_N << "Operation failed: " << why;
    emit errorOccurred(why);
    setState(State::Error);
    return false;
}

QCoro::Task<void> AppEngine::doReset()
{
    LOG_DEBUG_N << "Resetting AppEngine";
    setState(State::Resetting);

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
    setState(State::Idle);
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
        text = rec_names.at(static_cast<int>(state_));
    }

    if (text != state_text_) {
        LOG_DEBUG_N << "State text changed to: " << text.toStdString();
        state_text_ = text;
        emit stateTextChanged();
    }
}

void AppEngine::prepareLanguages()
{
    languageList_.clear();

    auto add = [this](const char* displayName, std::string_view whisperCode) {
        languageList_.append(Language{tr(displayName), whisperCode});
    };

    // =======================================================
    // EU OFFICIAL LANGUAGES
    // =======================================================
    add("Bulgarian",   "bg");
    add("Croatian",    "hr");
    add("Czech",       "cs");
    add("Danish",      "da");
    add("Dutch",       "nl");
    add("English",     "en");
    add("Estonian",    "et");
    add("Finnish",     "fi");
    add("French",      "fr");
    add("German",      "de");
    add("Greek",       "el");
    add("Hungarian",   "hu");
    add("Irish",       "ga");
    add("Italian",     "it");
    add("Latvian",     "lv");
    add("Lithuanian",  "lt");
    add("Maltese",     "mt");
    add("Polish",      "pl");
    add("Portuguese",  "pt");
    add("Romanian",    "ro");
    add("Slovak",      "sk");
    add("Slovenian",   "sl");
    add("Spanish",     "es");
    add("Swedish",     "sv");

    // =======================================================
    // EUROPE / NEAR-EU (COMMON)
    // =======================================================
    add("Norwegian",   "no");
    add("Icelandic",   "is");

    add("Albanian",    "sq");
    add("Bosnian",     "bs");
    add("Macedonian",  "mk");
    add("Serbian",     "sr");

    add("Turkish",     "tr");
    add("Ukrainian",   "uk");
    add("Russian",     "ru");

    // =======================================================
    // AMERICAS
    // =======================================================
    add("Portuguese (Brazil)", "pt");  // Whisper distinguishes via accent, not code
    add("Spanish (Latin America)", "es");

    // =======================================================
    // MIDDLE EAST & AFRICA
    // =======================================================
    add("Arabic",      "ar");
    add("Hebrew",      "he");
    add("Persian",     "fa");
    add("Swahili",     "sw");
    add("Afrikaans",   "af");
    add("Amharic",     "am");

    // =======================================================
    // SOUTH ASIA
    // =======================================================
    add("Hindi",       "hi");
    add("Urdu",        "ur");
    add("Bengali",     "bn");
    add("Tamil",       "ta");
    add("Telugu",      "te");
    add("Marathi",     "mr");

    // =======================================================
    // EAST & SOUTHEAST ASIA
    // =======================================================
    add("Chinese",     "zh");
    add("Japanese",    "ja");
    add("Korean",      "ko");

    add("Vietnamese",  "vi");
    add("Thai",        "th");
    add("Indonesian",  "id");
    add("Malay",       "ms");
    add("Filipino",    "tl");

    // =======================================================
    // OCEANIA
    // =======================================================
    add("Maori",       "mi");

    // Sort by name
    {
        QCollator coll;
        coll.setNumericMode(true);
        coll.setCaseSensitivity(Qt::CaseInsensitive);

        std::ranges::sort(languageList_,
                          [&](const Language& a, const Language& b) {
                              return coll.compare(a.name, b.name) < 0;
                          });
    }

    languageList_.insert(languageList_.begin(), Language{tr("[Auto]"), ""});

    // Populate languages_

    languages_.clear();
    languages_.reserve(languageList_.size());
    for (const auto &lang : languageList_) {
        languages_.append(lang.name);
    }

    emit languagesChanged();
}


