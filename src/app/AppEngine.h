#pragma once

#include <QObject>
#include <QQmlComponent>

#include <qcorotask.h>

#include "AudioController.h"
#include "Queue.h"
#include "ModelInfo.h"
#include "ChatMessagesModel.h"
#include "AvailableModelsModel.h"

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
    Q_PROPERTY(QStringList transcribeModels READ transcribeModels NOTIFY languageIndexChanged)
    Q_PROPERTY(QStringList translateModels READ translateModels CONSTANT)
    Q_PROPERTY(QStringList documentModels READ documentModels CONSTANT)
    Q_PROPERTY(AvailableModelsModel *chatModels READ chatModels CONSTANT)

    Q_PROPERTY(int languageIndex READ languageIndex WRITE setLanguageIndex NOTIFY languageIndexChanged)
    Q_PROPERTY(QString transcribeModelName READ transcribeModelName() WRITE setTranscribeModelName NOTIFY transcribeModelNameChanged)
    Q_PROPERTY(QString transcribePostModelName READ transcribePostModelName() WRITE setTranscribePostModelName NOTIFY postTranscribeModelNameChanged)
    Q_PROPERTY(QString chatModelName READ chatModelName() NOTIFY chatModelNameChanged)
    Q_PROPERTY(bool canPrepare READ canPrepare NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool canPrepareforChat READ canPrepareForChat NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool canStop READ canStop NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY stateFlagsChanged)
    Q_PROPERTY(const qreal& recordingLevel MEMBER recording_level_ NOTIFY recordingLevelChanged)
    Q_PROPERTY(const QString& recordedText MEMBER current_recorded_text_ NOTIFY recordedTextChanged)
    Q_PROPERTY(const QStringList& michrophones READ microphones() NOTIFY microphonesChanged)
    Q_PROPERTY(int currentMic READ currentMic WRITE setCurrentMic NOTIFY currentMicChanged)
    Q_PROPERTY(const QString& stateText MEMBER state_text_ NOTIFY stateTextChanged)
    Q_PROPERTY(ChatMessagesModel* chatMessages READ chatMessages CONSTANT)

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

    struct Language {
        QString name; // "English"
        std::string_view whisper_language; // "en"
    };

    // Q_INVOKABLE void setModelName(const QString& name);
    // Q_INVOKABLE void setPostModelName(const QString& name);
    Q_INVOKABLE void setLanguageIndex(int index);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void prepareForRecording();
    Q_INVOKABLE void prepareForChat();
    Q_INVOKABLE void chatPrompt(const QString& prompt);
    Q_INVOKABLE void saveTranscriptToFile(const QUrl &path);
    Q_INVOKABLE void reset();
    Q_INVOKABLE void startChatConversation(const QString& name);

    AppEngine();

    AudioController &audioController() { return audio_controller_; }
    QStringList languages()   const { return languages_; }
    QStringList transcribeModels()  const;
    QStringList microphones() const;
    int currentMic() const;
    void setCurrentMic(int index);

    QStringList translateModels() const;
    QStringList documentModels() const;
    AvailableModelsModel *chatModels() {
        return &chat_models_;
    }

    int  languageIndex() const { return language_index_; }
    QString transcribeModelName() const { return transcribe_model_name_;}
    void setTranscribeModelName(const QString& name);
    void setTranscribePostModelName(const QString& name);
    QString transcribePostModelName() const { return transcribe_post_model_name_;}
    QString chatModelName() const;
    std::string getChatSystemPrompt() const;

    int modelIndex(const QString& name) const;

    bool canPrepare() const;
    bool canPrepareForChat() const;
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

private:
    State state() const { return state_; }
    void setState(State newState);
    void createPipelineIfNeeded();
    QCoro::Task<void> startPrepareForRecording();
    QCoro::Task<void> startPrepareForChat(QString modelName);
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
    void setStateText(QString text = {});
    void prepareLanguages();
    QCoro::Task<bool> sendChatPrompt(const QString& prompt);
    void prepareAvailableModels();

    ChatMessagesModel chat_messages_model_;
    AvailableModelsModel chat_models_{ModelKind::GENERAL, "chat_model.selected"};
    std::shared_ptr<ChatConversation> chat_conversation_; // the current conversation
    AudioController audio_controller_;
    State state_{State::Idle};
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
    qreal recording_level_{};
    QString current_recorded_text_;
    QString state_text_;
    QList<Language> languageList_;
};

std::ostream& operator << (std::ostream& os, AppEngine::State state);
