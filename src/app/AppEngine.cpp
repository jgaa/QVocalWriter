
#include <array>
#include <format>
#include <ranges>

#include <QStringLiteral>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QTimer>
#include <QCollator>
#include <QClipboard>
#include <QGuiApplication>

#include "AppEngine.h"
#include "AudioRecorder.h"
#include "AudioFileWriter.h"
#include "TranscriberWhisper.h"
#include "AudioCaptureDevice.h"
#include "GeneralModel.h"
#include "ChatConversation.h"
#include "ScopedTimer.h"
#include "AudioImport.h"

#include "logging.h"

using namespace std;

namespace {


static constexpr auto translate_system_prompts = to_array<string_view>({
    string_view{R"(You are a professional translation engine.

Translate the user-provided text %1 to %2.

Rules:
- Preserve the original meaning exactly.
- Do NOT add, remove, or explain anything.
- Do NOT summarize or rewrite.
- Preserve formatting, punctuation, line breaks, lists, and code blocks.
- Keep proper names unchanged unless a standard translation exists.
- If the input is incomplete or ungrammatical, translate it as-is.
- Output ONLY the translated text.

Do not include comments, explanations, or metadata.)"},
    string_view{R"(ou are a strict translation system.

Translate the text %1 to %2.

Requirements:
- Accuracy is more important than fluency.
- Preserve sentence structure where possible.
- Preserve formatting, punctuation, whitespace, and line breaks.
- Do not paraphrase.
- Do not normalize terminology.
- Do not explain or annotate.

If a term has no direct equivalent, transliterate or leave it unchanged.

Output only the translated text.)"},
    string_view{R"(You are a professional human translator.

Translate the text %1 to %2.

Guidelines:
- Preserve the meaning and intent.
- Use natural, idiomatic language in the target language.
- Preserve formatting and structure.
- Do not add new information.
- Do not explain your choices.

Output only the translated text.)"}
});

constexpr string_view translate_document_prompt = // X) Final translation pass (preserve style, humor, and format)
    R"(You are a professional human translator.

Your task:
- Translate the document into the TARGET LANGUAGE.
- This is a FINAL TRANSLATION PASS, not a rewrite.

Absolute rules:
- Do NOT rewrite, summarize, or restructure the text.
- Do NOT add or remove content.
- Do NOT “improve” clarity beyond what is required for correct translation.

Preservation rules:
- Preserve tone, register, and voice (formal, informal, emotional, ranting, neutral, etc).
- Preserve humor, irony, sarcasm, and rhetorical devices.
- Preserve line breaks, paragraphs, and spacing.
- Preserve Markdown, formatting, bullet points, headings, emphasis, and code blocks exactly.
- Preserve emojis, punctuation style, and capitalization patterns where possible.

Translation rules:
- Translate idioms naturally into the target language where possible.
- If no natural equivalent exists, translate literally while preserving intent.
- Proper names should NOT be translated unless there is a well-established equivalent.
- Technical terms should remain consistent; prefer established translations.

Edge cases:
- If a word or phrase is untranslatable or ambiguous, keep the original and add [untranslated].
- Do NOT explain translation choices.

Output requirements:
- Output ONLY the translated text.
- No preface, no commentary, no notes.

TARGET LANGUAGE:
%1
)";



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

std::ostream& operator << (std::ostream& os, AppEngine::Mode mode) {
    constexpr auto modes = to_array<string_view>({
        "Transcribe",
        "Chat",
        "Translate"
    });
    return os << modes.at(static_cast<size_t>(mode));
}

std::ostream& operator << (std::ostream& os, AppEngine::TranscribeSource source) {
    constexpr auto sources = to_array<string_view>({
        "Mic",
        "File"
    });
    return os << sources.at(static_cast<size_t>(source));
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

void AppEngine::prepareForTranscribe()
{
    if (!canPrepareForTranscribe()) {
        failed("Cannot prepare in this state.");
        return;
    }

    if (transcribe_source_ == TranscribeSource::Mic) {
        startPrepareForRecording();
    }

    if (transcribe_source_ == TranscribeSource::File) {
        startPrepareForTranscribeFile();
    }
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

void AppEngine::prepareForTranslation()
{
    startPrepareForTranslation();
}

void AppEngine::resetTranslationState()
{
    translate_model_.reset();
    setState(State::Idle);
}

void AppEngine::chatPrompt(const QString &prompt)
{
    sendChatPrompt(prompt);
}

void AppEngine::translate(const QString &text)
{
    sendTranslatePrompt(text);
}

QCoro::Task<bool> AppEngine::sendChatPrompt(QString prompt)
{
    if (!chat_model_) {
        failed("Chat model is not prepared.");
        co_return false;
    }

    if (!chat_model_->isLoaded()) {
        failed ("Chat model is not loaded.");
        co_return false;
    }

    //QSettings settings;

    // if (settings.value("models/send.datetime", false).toBool()) {
    //     const auto tz = QTimeZone::systemTimeZone();
    //     const auto now = std::chrono::system_clock::now();

    //     const auto datetime_str = QString::fromUtf8(
    //         std::format(
    //             "{:%FT%T}",
    //             std::chrono::floor<std::chrono::seconds>(now)
    //         )
    //     );
    //     prompt = QString("META BEGIN Current date time is %1 GMT. Timezone is %2. META END\n%3")
    //                 .arg(datetime_str,
    //                      QString::fromUtf8(tz.id()),
    //                      prompt);
    // }


    LOG_TRACE_N << "Sending chat prompt: " << prompt;

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
    auto msg = make_shared<ChatMessage>(PromptRole::Assistant, "");
    msg->model_used = chat_model_->modelInfo().id;
    current_conversation->addMessage(msg);

    connect(chat_model_.get(), &Model::partialTextAvailable, [this](const QString& msg) {
        LOG_TRACE_N << "Chat model partial text available: " << msg;
        if (chat_conversation_) {
            chat_conversation_->updateLastMessage(msg.toStdString());
        }
    });

    // TODO: Handle replay of the full conversation if we resumed an existing one from storage.
    // Conversation continuation is handled by the model context params.
    ScopedTimer timer;
    auto result = co_await chat_model_->prompt(std::move(formatted_prompt),
                                               qvw::LlamaSessionCtx::Params::Chat(true));
    msg->duration_seconds = timer.elapsed();
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

QCoro::Task<bool> AppEngine::sendTranslatePrompt(const QString &prompt)
{
    // TODO: Support different translation styles
    const string_view system_prompt = translate_system_prompts.at(
        static_cast<size_t>(TranslationStyle::Default));

    if (!translate_model_) {
        failed("Chat model is not prepared.");
        co_return false;
    }

    if (!translate_model_->isLoaded()) {
        failed ("Chat model is not loaded.");
        co_return false;
    }

    // Don't specify source language if auto
    const auto from_phhrase = source_languages_model_.autoIsSelected()
                            ? QString{}
                            : QString{"from %1"}.arg(source_languages_model_.selectedName());

    std::array<ChatMessage, 2> messages = {
        ChatMessage{PromptRole::System, QString::fromUtf8(system_prompt)
                                            .arg(from_phhrase)
                                            .arg(target_languages_model_.selectedName())
                                            .toStdString()},
        ChatMessage{PromptRole::User, prompt.toStdString()}
    };
    std::array<const ChatMessage*, 2> message_ptrs = {
        &messages[0],
        &messages[1]
    };

    auto formatted_prompt = translate_model_->modelInfo().formatPrompt(message_ptrs); //getMessages());

    setState(State::Processing);

    auto result = co_await translate_model_->prompt(std::move(formatted_prompt),
                                                    qvw::LlamaSessionCtx::Params::Chat(true));

    if (!result) {
        failed("Translation failed.");
        co_return false;
    }

    assert(translate_model_);
    emit translationAvailable(QString::fromStdString(translate_model_->finalText()));

    setState(State::Ready);
    co_return result;
}

void AppEngine::prepareAvailableModels()
{
    chat_models_.SetModels(
        ModelMgr::instance().availableModels(
            chat_models_.kind(), ModelInfo::Capability::Chat));

    translation_models_.SetModels(
        ModelMgr::instance().availableModels(
            chat_models_.kind(), ModelInfo::Capability::Translate));

    live_transcribe_models_.SetModels(
        ModelMgr::instance().availableModels(
            live_transcribe_models_.kind(), ModelInfo::Capability::Transcribe));

    post_transcribe_models_.SetModels(
        ModelMgr::instance().availableModels(
            post_transcribe_models_.kind(), ModelInfo::Capability::Transcribe));

    doc_prepare_models_.SetModels(
        ModelMgr::instance().availableModels(
            doc_prepare_models_.kind(), ModelInfo::Capability::Rewrite));

    doc_translate_models_.SetModels(
        ModelMgr::instance().availableModels(
            doc_prepare_models_.kind(), ModelInfo::Capability::Translate));
}

void AppEngine::setRecordedText(const QString text)
{
    if (current_recorded_text_ != text) {
        current_recorded_text_ = std::move(text);
        emit recordedTextChanged();
    }
}

void AppEngine::onModelChangedState(const Model *model, ModelState state)
{
    assert(model);
    LOG_TRACE_N << "Model " << model->name() << " changed state to " << state;
    if (state == ModelState::LOADING) {
        setStateText(tr("Loading model: %1").arg(model->modelInfo().id));
    }
    if (state == ModelState::LOADED) {
        setStateText(tr("Model loaded: %1").arg(model->modelInfo().id));
    }
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
    setState(State::Idle);
}

bool AppEngine::swapTranslationLanguages()
{
    if (source_languages_model_.autoIsSelected()) {
        LOG_WARN_N << "Cannot swap languages when source is auto";
        return false;
    }

    if (!source_languages_model_.haveSelection() || !target_languages_model_.haveSelection()) {
        LOG_WARN_N << "Cannot swap languages when one side has no selection";
        return false;
    }

    const auto a = source_languages_model_.selectedCode();
    const auto b = target_languages_model_.selectedCode();

    source_languages_model_.setSelectedCode(b);
    target_languages_model_.setSelectedCode(a);
    return true;
}

void AppEngine::copyTextToClipboard(const QString &text)
{
    if (auto *clipb = QGuiApplication::clipboard()) {
        clipb->setText(text);
    } else {
        LOG_WARN_N << "Clipboard not available";
    }
}

QString AppEngine::aboutText() const
{
     return tr(R"(**QVocalWriter** is a cross-platform, privacy-focused application for working with speech and text.
It combines **transcription**, **translation**, and **assistant-based chat** in a modular design
focused on long-form work and local models.

This is QVocalWriter version: %1, using Qt version: %2 (GPL license).

## Overview

QVocalWriter started as a speech-to-text tool for long-form writing and has evolved into a flexible
toolkit for language workflows.

The application emphasizes:

- **Privacy**: All processing is done locally on your machine.
- **Modularity**: Choose which models and features to use.
- **Flexibility**: You can run models of varying sizes based on your hardware.

## Features

### Transcription
Convert speech to structured text, suitable for long recordings. The app can show live transcription while
you record, and supports post-processing for improved accuracy. Once the recording is complete, the app can
translate and/or send the text to a chat assistant for further refinement. Pre-defined prompts help guide the assistant
to make a:

- Blog post
- Email
- Social media posts (Linkedin, Reddit, Facebook, etc)
- Technical documentation
- Meeting notes
- Structured plans from inspired rambling
- Creative writing (stories, poems, scripts)
- Conservative, but cleand up text from the raw transcription (for example medical or legal memos)

*Remember that AI models does not always produce accurate results, so review and edit the output as needed.*

### Translation
Translate text or transcriptions between languages using local models.

### Assistant Chat
Interact with language models for drafting, rewriting, research and exploration.

## Technical Information

- Built with **Qt 6 (C++ / QML)**
- Cross-platform: Linux, Windows, macOS
- Typical memory usage: < 100 MB before loading models
- License: GPL3

## Credits

Developed by **Jarle Aase**, [The Last Viking LTD](https://lastviking.eu/)
© 2025
)").arg(APP_VERSION).arg(qVersion());

}

void AppEngine::setInputAudioFile(const QUrl &path)
{
    auto file_path = path.toLocalFile();

    LOG_DEBUG_N << "Setting input audio file to: " << file_path;
    transcribe_from_file_path_ = file_path;
    emit stateFlagsChanged();
}

void AppEngine::transcribeFile()
{
    startTransribeFile(transcribe_from_file_path_);
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
            [this]() {
                LOG_TRACE_N << "Chat model selection changed to: " << chat_models_.currentId();
                emit stateFlagsChanged();
            });

    // connect(&chat_models_,
    //         &AvailableModelsModel::selectedChanged,
    //         this,
    //         &AppEngine::stateFlagsChanged);

    connect(&translation_models_,
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

    connect(&doc_prepare_models_,
            &AvailableModelsModel::selectedChanged,
            this,
            &AppEngine::stateFlagsChanged);

    connect(&doc_translate_models_,
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

bool AppEngine::canPrepareForTranscribe() const
{
    const bool have_selection = live_transcribe_models_.hasSelection()
                               || post_transcribe_models_.hasSelection();

    if (transcribe_source_ == TranscribeSource::File) {
        if (transcribe_from_file_path_.isEmpty()) {
            LOG_TRACE_N << "No input file selected";
            return false;
        }
    }

    if (transcribe_source_ == TranscribeSource::Mic) {
        if (currentMic() < 0) {
            LOG_TRACE_N << "No microphone selected";
            return false;
        }
    }

    return state() == State::Idle && have_selection;
}

bool AppEngine::canPrepareForChat() const
{
    LOG_TRACE_N << "state=" << state() << ", hasSelection=" << chat_models_.hasSelection();
    return state() == State::Idle && chat_models_.hasSelection();
}

bool AppEngine::canPrepareForTranslate() const
{
    return state() == State::Idle && translation_models_.hasSelection();
}

bool AppEngine::canStart() const
{
    return state() == State::Ready;
}

bool AppEngine::canStop() const
{
    return stateIn({State::Recording, State::Processing});
}

bool AppEngine::isBusy() const
{
    return stateIn({State::Processing, State::Preparing, State::Resetting});
}

bool AppEngine::canChangeConfig() const
{
    return state() == State::Idle;
}

void AppEngine::initLogging()
{
    QSettings settings{};

    if (!settings.contains("logging/applevel")) {
        settings.setValue("logging/applevel", 4); // INFO
    }

#ifdef Q_OS_LINUX
    if (const auto level = settings.value("logging/applevel", 4).toInt()) {
        logfault::LogManager::Instance().AddHandler(
            make_unique<logfault::StreamHandler>(clog, static_cast<logfault::LogLevel>(level)));
        LOG_INFO << "Logging to console";
    }
#endif

    auto level = settings.value("logging/level", 0).toInt();
    if (level > 0) {
        if (auto path = settings.value("logging/path", "").toString().toStdString(); !path.empty()) {
            const bool prune = settings.value("logging/prune", "").toString() == "true";
            logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(path, static_cast<logfault::LogLevel>(level), prune));

            LOG_INFO << "Logging to: " << path;
        }
    }
}

void AppEngine::setMode(Mode newMode)
{
    if (mode() != newMode) {
        LOG_DEBUG_N << "Mode changed from " << mode() << " to " << newMode;
        mode_ = newMode;
        emit modeChanged();
        emit stateFlagsChanged();
        emit stateTextChanged();
    }
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

void AppEngine::setTranscribeSource(TranscribeSource source)
{
    if (transcribe_source_ != source) {
        LOG_DEBUG_N << "Transcribe source changed from " << transcribe_source_ << " to " << source;
        transcribe_source_ = source;
        emit stateFlagsChanged();
    }
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


void AppEngine::setState(State newState, const QString& text)
{
    if (state() != newState) {
        LOG_DEBUG_N << "State changed from " << state() << " to " << newState;
        state_[static_cast<size_t>(mode())] = newState;
        emit stateChanged(newState);
        emit stateFlagsChanged();
        setStateText(text);
    }
}

QCoro::Task<void> AppEngine::startPrepareForRecording()
{
    LOG_INFO_N << "Preparing for recording";
    setState(State::Preparing, tr("Preparing for transcript..."));

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

    co_await prepareTranscriptFinal();

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
    if (chat_model_ = co_await prepareGeneralModel(
        "chat-model",
        *mi,
        true // Load it
            ); chat_model_) {
        setState(State::Ready);
    } else {
        assert(state() == State::Error);
        LOG_WARN_N << "Failed to prepare chat model: " << id;
    }

    co_return;
}

QCoro::Task<void> AppEngine::startPrepareForTranslation()
{
    LOG_INFO_N << "Preparing for translation with model: " << translation_models_.currentId();
    setState(State::Preparing, tr("Preparing translation model..."));

    auto mi = ModelMgr::instance().findModelById(
        ModelKind::GENERAL,
        QString::fromUtf8(translation_models_.currentId())
        );

    if (!mi) {
        failed(tr("Translation model not found: %1").arg(translation_models_.currentId()));
        co_return;
    }
    assert(mi);
    translate_model_ = co_await prepareGeneralModel(
        "translation-model",
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


    const bool have_rewrite_step = rewrite_style_.hasSelection() && doc_prepare_models_.hasSelection();
    const bool have_translate_step = doc_translate_models_.hasSelection() && doc_translate_languages_model_.haveSelection();

    const auto whisper_models = ModelMgr::instance().availableModels(ModelKind::WHISPER, ModelInfo::Capability::Transcribe);
    const auto is_recording = transcribe_source_ == TranscribeSource::Mic;

    if (is_recording && live_transcribe_models_.hasSelection()) {
        const auto qid = QString::fromUtf8(live_transcribe_models_.currentId());

        if (rec_transcriber_ && rec_transcriber_->modelInfo().id == qid) {
            LOG_DEBUG_N << "Live transcriber model already prepared: " << qid;
        } else {
            if (rec_transcriber_ && rec_transcriber_->isLoaded()) {
                co_await rec_transcriber_->unloadModel();
                rec_transcriber_.reset();
            }
            if (auto model = ModelMgr::instance().findModelById(ModelKind::WHISPER, qid); model) {

                LOG_DEBUG_N << "Preparing live transcriber model: " << qid;

                const auto language = languageList_.at(size_t(language_index_));
                if (rec_transcriber_ = co_await prepareTranscriber(
                        "live-transcriber"s,
                        *model,
                        language.whisper_language,
                        true, // Load the live transcriber so it reacts faster when we start recording
                        false
                        ); !rec_transcriber_) {
                    co_return failed(tr("Failed to prepare live transcriber %1").arg(qid));
                }

                if (auto conversation = transcribe_conversation_) {
                    conversation->addMessage(
                        make_shared<ChatMessage>(PromptRole::Assistant,
                                                 "",
                                                 false,
                                                 tr("Live transcript").toStdString()));

                    // capture weak ptr to avoid cyclic ref
                    auto wconversation = weak_ptr<ChatConversation>(conversation);
                    connect(rec_transcriber_.get(),
                            &Model::partialTextAvailable, [this, wconversation] (const QString& text) mutable {
                        if (auto conversation = wconversation.lock()) {
                            conversation->updateLastMessage(text.toStdString());
                        }
                    });
                }

            } else {
                co_return failed(tr("Transcription model not found: %1").arg(qid));
            }
        }
    } else {
        rec_transcriber_.reset();
    }

    // same with post_transcriber_ and transcribe_post_model_name_
    if (post_transcribe_models_.hasSelection()) {
        const auto qid = QString::fromUtf8(post_transcribe_models_.currentId());

        if (post_transcriber_ && post_transcriber_->modelInfo().id == qid) {
            LOG_DEBUG_N << "Post transcriber already prepared: " << qid;
        } else {
            if (post_transcriber_ && post_transcriber_->isLoaded()) {
                co_await post_transcriber_->unloadModel();
                post_transcriber_.reset();
            }

            if (auto model = ModelMgr::instance().findModelById(ModelKind::WHISPER, qid); model) {

                LOG_DEBUG_N << "Preparing post transcriber model: " << qid;

                const auto language = languageList_.at(size_t(language_index_));
                if (post_transcriber_ = co_await prepareTranscriber(
                        "post--transcriber"s,
                        *model,
                        language.whisper_language,
                        false, // Load it later
                        false // Submit final text?
                        ); !post_transcriber_) {
                    co_return failed(tr("Failed to prepare post-transcriber %1").arg(qid));
                }

            } else {
                co_return failed(tr("Transcription model not found: %1").arg(qid));
            }
        }
    } else  {
        post_transcriber_.reset();
    }

    if (have_rewrite_step) {
        const auto qid = QString::fromUtf8(doc_prepare_models_.currentId());

        if (doc_prepare_model_ && doc_prepare_model_->modelInfo().id == qid) {
            LOG_DEBUG_N << "Document rewrite model already prepared: " << qid;
        } else {
            if (doc_prepare_model_ && doc_prepare_model_->isLoaded()) {
                co_await doc_prepare_model_->unloadModel();
                doc_prepare_model_.reset();
            }

            if (auto model = ModelMgr::instance().findModelById(ModelKind::GENERAL, qid); model) {
                LOG_DEBUG_N << "Preparing document rewrite model: " << qid;
                doc_prepare_model_ = co_await prepareGeneralModel(
                    "doc-rewrite-model",
                    *model,
                    false // Load it later
                    );
            } else {
                co_return failed(tr("Doc rewrite model not found: %1").arg(qid));
            }
        }
    } else {
        doc_prepare_model_.reset();
    }

    if (have_translate_step) {
        const auto qid = QString::fromUtf8(doc_translate_models_.currentId());

        if (doc_translate_model_ && doc_translate_model_->modelInfo().id == qid) {
            LOG_DEBUG_N << "Document translation model already prepared: " << qid;
        } else {
            if (auto model = ModelMgr::instance().findModelById(ModelKind::GENERAL, qid); model) {

                LOG_DEBUG_N << "Preparing document translation model: " << qid;
                doc_translate_model_ = co_await prepareGeneralModel(
                    "doc-translate-model",
                    *model,
                    false // Load it later
                    );
            } else {
                co_return failed(tr("Doc translate model not found: %1").arg(qid));
            }
        }
    } else {
        doc_translate_model_.reset();
    }

    setState(State::Ready);

    co_return true;
}

QCoro::Task<void> AppEngine::startPrepareForTranscribeFile()
{
    LOG_INFO_N << "Preparing for file transcription";
    setState(State::Preparing, tr("Preparing for file transcription..."));

    if (file_writer_) {
        file_writer_.reset();
    }

    if (recorder_) {
        recorder_.reset();
    }

    if (chunk_queue_) {
        chunk_queue_.reset();
    }

    // make sure pcm_file_path_ exists and is empty.
    {
        QFile f(pcm_file_path_);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.close();
        }
    }

    co_await prepareTranscriptFinal();
}

QCoro::Task<void> AppEngine::prepareTranscriptFinal()
{
    transcribe_conversation_ = make_shared<ChatConversation>("transcribe");
    transcribe_conversation_->setModel(&transcribe_messages_model_);

    const auto ok = co_await prepareTranscriberModels();
    if (!ok) {
        failed(tr("Failed to prepare for transcript"));
        co_return;
    }

    co_return;
}

QCoro::Task<void> AppEngine::startTransribeFile(const QString &path)
{
    {
        // Check if the file exists
        QFileInfo fi(path);
        if (!fi.exists() || !fi.isFile()) {
            failed(tr("Input audio file does not exist: %1").arg(path));
            co_return;
        }
    }

    setState(State::Processing, tr("Decoding audio file..."));

    AudioImport import;

    if (!co_await import.decodeMediaFile(path)) {
        failed(tr("Failed to decode audio file"));
        LOG_ERROR_N << "Failed to decode audio file: " << path.toStdString()
                    << ": " << import.errorMessage();
        co_return;
    }

    // TODO: Pass the imported format to the transcribers directly instead of
    //       converting to the PCM format used by live recording.
    const auto in = import.mono16kData();

    std::vector<int16_t> out;
    out.reserve(in.size());

    for (float s : in) {
        // clamp to [-1, 1]
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;

        // scale (use 32767 for symmetric range)
        const int v = int(std::lrint(s * 32767.0f));
        out.push_back(int16_t(std::clamp(v, -32768, 32767)));
    }

    QFile f(pcm_file_path_);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(reinterpret_cast<const char*>(out.data()),
                qint64(out.size() * sizeof(int16_t)));
        f.close();
    }

    setStateText(tr("Transcribing audio file..."));
    co_await onRecordingDone();
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

    connect(
        model.get(),
        &Model::stateChanged,
        this,
        &AppEngine::onModelChangedState);

    if (!co_await model->init(model_id)) {
        LOG_ERROR_N << "Failed to init general model: " << model_id;
        failed(tr("Failed to initialize model: %1").arg(model_id));
        co_return nullptr;
    }

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

    // Unconnected partial text signal

    // Relay partial text signals
    connect(
        transcriber.get(),
        &Model::partialTextAvailable,
        this,
        [this](const QString &text) {
            LOG_TRACE_N << "Partial text available: " << text;
            setRecordedText(text);
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

    co_await transcriber->init(model_id);

    if (loadModel) {
        co_await transcriber->loadModel();
    }

    co_return transcriber;
}

void AppEngine::onFinalRecordingTextAvailable(const QString &text)
{
    LOG_INFO_N << "Final text available: " << text.toStdString();
    setRecordedText(text);
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
    // TODO: Don't unload models if they are re-used later
    LOG_DEBUG_N << "Recording done, starting post-processing transcription if needed";

    QString final_text;
    // Each step adds their own agent messages to the conversation.

    setState(State::Processing, tr("Starting post-processing..."));

    if (rec_transcriber_) {
        LOG_DEBUG_N << "Stopping live transcriber before post-processing";
        rec_transcriber_->stopTranscribing();
        final_text = QString::fromStdString(rec_transcriber_->finalText());

        // We should only ever have a message in the conversation at this point if we had live transcription
        if (auto * last = transcribe_conversation_->back()) {
            transcribe_conversation_->updateLastMessage(final_text.toStdString());
            transcribe_conversation_->finalizeLastMessage();
        }

        co_await rec_transcriber_->stop();
        rec_transcriber_.reset();
    }

    if (post_transcriber_) {
        setStateText(tr("Running post-processing transcription..."));
        assert(post_transcriber_->haveModel());
        if (!post_transcriber_->isLoaded()) {
            co_await post_transcriber_->loadModel();
        }

        auto msg = make_shared<ChatMessage>(PromptRole::Assistant, "",
                                            false,
                                            tr("Transcript").toStdString());
        transcribe_conversation_->addMessage(msg);
        ScopedTimer timer;
        if (!co_await post_transcriber_->transcribeRecording()) {
            failed(tr("Post-processing transcription failed"));
            co_return;
        }
        msg->duration_seconds = timer.elapsed();

        final_text = QString::fromStdString(post_transcriber_->finalText());

        transcribe_conversation_->updateLastMessage(final_text.toStdString());
        transcribe_conversation_->finalizeLastMessage();
    }

    if (post_transcriber_ && post_transcriber_->isLoaded()) {
        co_await post_transcriber_ ->unloadModel();
    }

    if (doc_prepare_model_) {
        assert(rewrite_style_.hasSelection());
        LOG_DEBUG_N << "Starting document preparation rewrite";

        if (!doc_prepare_model_->isLoaded()) {
            setStateText(tr("Loading rewrite model..."));
            co_await doc_prepare_model_->loadModel();
        }

        setStateText(tr("Rewriting document..."));

        string formatted_prompt;
        {
            string fromLng;
            if (auto lngix = languageIndex(); lngix > 0) {
                fromLng = languageList_.at(size_t(language_index_)).name.toStdString();
            }
            const auto prompt = rewrite_style_.makePrompt(fromLng);

            const array<ChatMessage, 2> msgs = {ChatMessage{PromptRole::System, prompt.toStdString()},
                                          {PromptRole::User, final_text.toStdString()}};

            array<const ChatMessage*, 2> message_ptrs = {&msgs[0], &msgs[1]};

            formatted_prompt = doc_prepare_model_->modelInfo().formatPrompt(message_ptrs);

            LOG_TRACE_N << "Document rewrite formatted prompt: " << formatted_prompt;
        }

        auto msg = make_shared<ChatMessage>(PromptRole::Assistant, "",
                                            false,
                                            tr("Rewrite").toStdString());
        transcribe_conversation_->addMessage(msg);
        ScopedTimer timer;

        auto result = co_await doc_prepare_model_->prompt(std::move(formatted_prompt),
                                                          qvw::LlamaSessionCtx::Params::Balanced());

        if (!result) {
            failed("Rewrite failed.");
            co_return;
        }

        final_text = QString::fromStdString(doc_prepare_model_->finalText());

        msg->duration_seconds = timer.elapsed();
        transcribe_conversation_->updateLastMessage(final_text.toStdString());
        transcribe_conversation_->finalizeLastMessage();

        if (!(doc_translate_model_ && doc_translate_model_->modelInfo().id == doc_prepare_model_->modelInfo().id)) {
            co_await doc_prepare_model_->stop();
            doc_prepare_model_.reset();
        }
    }

    if (doc_translate_model_ && !final_text.isEmpty()) {
        assert(doc_translate_languages_model_.haveSelection());

        if (!doc_translate_model_->isLoaded()) {
            setStateText(tr("Loading translate model..."));
            co_await doc_translate_model_->loadModel();
        }

        setStateText(tr("Translating..."));

        string formatted_prompt;
        {
            const auto prompt = QString::fromUtf8(translate_document_prompt)
                                     .arg(doc_translate_languages_model_.selectedName());
            const array<ChatMessage, 2> msgs = {ChatMessage{PromptRole::System, prompt.toStdString()},
                                                {PromptRole::User, final_text.toStdString()}};

            array<const ChatMessage*, 2> message_ptrs = {&msgs[0], &msgs[1]};

            formatted_prompt = doc_translate_model_->modelInfo().formatPrompt(message_ptrs);

            LOG_TRACE_N << "Document rewrite formatted prompt: " << formatted_prompt;
        }

        auto msg = make_shared<ChatMessage>(PromptRole::Assistant, "",
                                            false,
                                            tr("Translate").toStdString());
        transcribe_conversation_->addMessage(msg);
        ScopedTimer timer;
        auto result = co_await doc_translate_model_->prompt(std::move(formatted_prompt),
                                                          qvw::LlamaSessionCtx::Params::TranslateStrict());
        msg->duration_seconds = timer.elapsed();

        if (!result) {
            failed("Translation failed.");
            co_return;
        }

        final_text = QString::fromStdString(doc_translate_model_->finalText());

        transcribe_conversation_->updateLastMessage(final_text.toStdString());
        transcribe_conversation_->finalizeLastMessage();

        co_await doc_translate_model_->stop();
        doc_translate_model_.reset();
    }

    if (doc_prepare_model_) {
        // In case we used the same model for rewrite and translation
        co_await doc_prepare_model_->stop();
        doc_prepare_model_.reset();
    }

    setRecordedText(final_text);
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

    if (doc_prepare_model_) {
        co_await doc_prepare_model_->stop();
        // reset later
        QTimer::singleShot(0, this, [this]() {
            doc_prepare_model_.reset();
        });
    }

    if (doc_translate_model_) {
        co_await doc_translate_model_->stop();
        // reset later
        QTimer::singleShot(0, this, [this]() {
            doc_translate_model_.reset();
        });
    }

    file_writer_.reset();
    recorder_.reset();
    chunk_queue_.reset();

    setRecordedText({});
    setState(State::Idle);
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
        text = rec_names.at(static_cast<int>(state()));
    }

    if (text != stateText()) {
        LOG_DEBUG_N << "State text changed to: " << text;
        state_texts_[static_cast<size_t>(mode())] = text;
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


