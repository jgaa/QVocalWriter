#pragma once

#include <QObject>
#include <QQmlComponent>

#include <qcorotask.h>

#include "AudioController.h"
#include "Queue.h"
#include "ModelInfo.h"
#include "ChatMessagesModel.h"
#include "AvailableModelsModel.h"
#include "LanguagesModel.h"
#include "RewriteStyleModel.h"

class AudioRecorder;
class AudioFileWriter;
class Transcriber;         // base class
class TranscriberWhisper;  // concrete class
class ModelMgr;
class GeneralModel;
class ChatConversation;

class AppEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QStringList languages READ languages NOTIFY languagesChanged)
    Q_PROPERTY(AvailableModelsModel *liveTranscribeModels READ liveTranscribeModels CONSTANT)
    Q_PROPERTY(AvailableModelsModel *postTranscribeModels READ postTranscribeModels CONSTANT)
    Q_PROPERTY(AvailableModelsModel *chatModels READ chatModels CONSTANT)
    Q_PROPERTY(AvailableModelsModel *docPrepareModels READ docPrepareModels CONSTANT)
    Q_PROPERTY(AvailableModelsModel *translationModels READ translationModels CONSTANT)
    Q_PROPERTY(LanguagesModel* sourceLanguages READ sourceLanguages CONSTANT)
    Q_PROPERTY(LanguagesModel* targetLanguages READ targetLanguages CONSTANT)
    Q_PROPERTY(RewriteStyleModel *rewriteStyle READ rewriteStyle CONSTANT)
    Q_PROPERTY(int languageIndex READ languageIndex WRITE setLanguageIndex NOTIFY languageIndexChanged)
    Q_PROPERTY(QString chatModelName READ chatModelName() NOTIFY chatModelNameChanged)
    Q_PROPERTY(bool canPrepare READ canPrepare NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool canPrepareforChat READ canPrepareForChat NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool canPrepareforTranslate READ canPrepareForTranslate NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool canStop READ canStop NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY stateFlagsChanged)
    Q_PROPERTY(const qreal& recordingLevel MEMBER recording_level_ NOTIFY recordingLevelChanged)
    Q_PROPERTY(const QString& recordedText MEMBER current_recorded_text_ NOTIFY recordedTextChanged)
    Q_PROPERTY(const QStringList& michrophones READ microphones() NOTIFY microphonesChanged)
    Q_PROPERTY(int currentMic READ currentMic WRITE setCurrentMic NOTIFY currentMicChanged)
    Q_PROPERTY(const QString& stateText READ stateText NOTIFY stateTextChanged)
    Q_PROPERTY(ChatMessagesModel* chatMessages READ chatMessages CONSTANT)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)

public:
    enum class State {
        Idle,
        Preparing,
        Ready,
        Recording,
        Processing,
        Done,
        Resetting,
        Error
    };
    Q_ENUM(State)

    enum class TranslationStyle {
        Default,
        Strict,
        Natural
    };

    struct Language {
        QString name; // "English"
        std::string_view whisper_language; // "en"
    };

    enum class Mode : int {
        Transcribe,
        Translate,
        Chat,
        Count // meta
    };
    Q_ENUM(Mode)

    // Keep separate states for each mode (UI pane)
    using states_t = std::array<State, static_cast<size_t>(Mode::Count)>;
    using state_text_t = std::array<QString, static_cast<size_t>(Mode::Count)>;

    Q_INVOKABLE void setLanguageIndex(int index);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void prepareForRecording();
    Q_INVOKABLE void prepareForChat();
    Q_INVOKABLE void prepareForTranslation();
    Q_INVOKABLE void chatPrompt(const QString& prompt);
    Q_INVOKABLE void translate(const QString& text);
    Q_INVOKABLE void saveTranscriptToFile(const QUrl &path);
    Q_INVOKABLE void reset();
    Q_INVOKABLE void startChatConversation(const QString& name);
    Q_INVOKABLE bool swapTranslationLanguages();
    Q_INVOKABLE static void copyTextToClipboard(const QString& text);
    Q_INVOKABLE QString aboutText() const;

    AppEngine();

    AudioController &audioController() { return audio_controller_; }
    QStringList languages()   const { return languages_; }
    QStringList microphones() const;
    int currentMic() const;
    void setCurrentMic(int index);

    QStringList translateModels() const;
    AvailableModelsModel *chatModels() {
        return &chat_models_;
    }
    AvailableModelsModel *liveTranscribeModels() {
        return &live_transcribe_models_;
    }
    AvailableModelsModel *postTranscribeModels() {
        return &post_transcribe_models_;
    }
    AvailableModelsModel *translationModels() {
        return &translation_models_;
    }
    LanguagesModel* sourceLanguages() {
        return &source_languages_model_;
    }
    LanguagesModel* targetLanguages() {
        return &target_languages_model_;
    }
    AvailableModelsModel* docPrepareModels() {
        return &doc_prepare_models_;;
    }
    RewriteStyleModel* rewriteStyle() {
        return &rewrite_style_;
    }

    int  languageIndex() const { return language_index_; }
    QString transcribeModelName() const { return transcribe_model_name_;}
    // void setTranscribeModelName(const QString& name);
    // void setTranscribePostModelName(const QString& name);
    QString transcribePostModelName() const { return transcribe_post_model_name_;}
    QString chatModelName() const;
    std::string getChatSystemPrompt() const;


    bool canPrepare() const;
    bool canPrepareForChat() const;
    bool canPrepareForTranslate() const;
    bool canStart()   const;
    bool canStop()    const;
    bool isBusy()     const;

    template <typename T>
    constexpr bool stateIn(std::initializer_list<T> list) const noexcept
    {
        const auto s = state();
        for (auto v : list)
            if (v == s)
                return true;
        return false;
    }

    ChatMessagesModel* chatMessages() { return &chat_messages_model_; }

    static void initLogging();

signals:
    void stateChanged(AppEngine::State newState);
    void languageIndexChanged(int newIndex);
    void transcribeModelNameChanged();
    void postTranscribeModelNameChanged();
    void chatModelNameChanged();
    void stateFlagsChanged();
    void partialTextAvailable(const QString &text);
    void finalTextAvailable(const QString &text);
    void errorOccurred(const QString &message);
    //void downloadProgress(QString name, qint64 bytesReceived, qint64 bytesTotal);
    void downloadProgressRatio(const QString& name, double ratio); // 0..1
    void recordingLevelChanged();
    void recordedTextChanged();
    void microphonesChanged();
    void currentMicChanged();
    void stateTextChanged();
    void languagesChanged();
    void translationAvailable(const QString& text);
    void modeChanged();

private:
    Mode mode() const { return mode_; }
    void setMode(Mode newMode);
    State state() const { return state_[static_cast<size_t>(mode())]; }
    void setState(State newState);
    void createPipelineIfNeeded();
    QCoro::Task<void> startPrepareForRecording();
    QCoro::Task<void> startPrepareForChat(QString modelName);
    QCoro::Task<void> startPrepareForTranslation();
    QCoro::Task<bool> prepareTranscriberModels();
    QCoro::Task<std::shared_ptr<Transcriber>> prepareTranscriber(std::string name,
                                                           ModelInfo modelInfo,
                                                           std::string_view language,
                                                           bool loadModel,
                                                           bool submitFilalText);

    QCoro::Task<std::shared_ptr<GeneralModel>> prepareGeneralModel(std::string name,
                                                                 ModelInfo modelInfo,
                                                                 bool loadModel);
    void onFinalRecordingTextAvailable(const QString &text);
    QCoro::Task<void> transcribeChunks();
    QCoro::Task<void> onRecordingDone();
    bool failed(const QString& why);
    QCoro::Task<void> doReset();
    QString stateText() const {
        return state_texts_[static_cast<size_t>(mode())];
    }
    void setStateText(QString text = {});
    void prepareLanguages();
    QCoro::Task<bool> sendChatPrompt(const QString& prompt);
    QCoro::Task<bool> sendTranslatePrompt(const QString& prompt);
    void prepareAvailableModels();
    void setRecordedText(const QString text);

    ChatMessagesModel chat_messages_model_;
    AvailableModelsModel chat_models_{ModelKind::GENERAL, "chat_model.selected"};
    AvailableModelsModel translation_models_{ModelKind::GENERAL, "translation_model.selected"};
    AvailableModelsModel live_transcribe_models_{ModelKind::WHISPER, "transcribe_model.live.selected"};
    AvailableModelsModel post_transcribe_models_{ModelKind::WHISPER, "transcribe_model.post.selected"};
    AvailableModelsModel doc_prepare_models_{ModelKind::GENERAL, "doc_prepare_model.selected"};
    LanguagesModel source_languages_model_{"translate.source_language"};
    LanguagesModel target_languages_model_{"translate.target_language"};
    RewriteStyleModel rewrite_style_{"transcribe.doc.rewrite_style"};
    std::shared_ptr<ChatConversation> chat_conversation_; // the current conversation
    AudioController audio_controller_;
    states_t state_{State::Idle, State::Idle, State::Idle};
    state_text_t state_texts_{QString("Idle"), QString("Idle"), QString("Idle")};
    QStringList languages_;
    QStringList transcribe_model_sizes_;
    int language_index_{0}; // Auto
    QString transcribe_model_name_{};
    QString transcribe_post_model_name_{};
    //QString chat_model_name_{};
    QString pcm_file_path_;
    std::shared_ptr<chunk_queue_t> chunk_queue_;
    std::shared_ptr<AudioRecorder> recorder_;
    std::shared_ptr<AudioFileWriter> file_writer_;
    std::shared_ptr<Transcriber> rec_transcriber_;
    std::shared_ptr<Transcriber> post_transcriber_;
    std::shared_ptr<ModelMgr> model_mgr_;
    std::shared_ptr<GeneralModel> chat_model_;
    std::shared_ptr<GeneralModel> translate_model_;
    std::shared_ptr<GeneralModel> doc_prepare_model_;
    qreal recording_level_{};
    QString current_recorded_text_;
    QList<Language> languageList_;
    Mode mode_{Mode::Transcribe};
};

std::ostream& operator << (std::ostream& os, AppEngine::State state);
std::ostream& operator << (std::ostream& os, AppEngine::Mode mode);
