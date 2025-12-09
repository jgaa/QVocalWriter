#pragma once

#include <QObject>
#include <QQmlComponent>

#include <qcorotask.h>

#include "AudioController.h"
#include "Queue.h"

class AudioRecorder;
class AudioFileWriter;
class Transcriber;         // base class
class TranscriberWhisper;  // concrete class
class ModelMgr;

class AppEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(RecordingState recordingState READ recordingState NOTIFY recordingStateChanged)
    Q_PROPERTY(QStringList languages READ languages CONSTANT)
    Q_PROPERTY(QStringList modelSizes READ modelSizes CONSTANT)
    Q_PROPERTY(int languageIndex READ languageIndex WRITE setLanguageIndex NOTIFY languageIndexChanged)
    Q_PROPERTY(int modelIndex READ modelIndex WRITE setModelIndex NOTIFY modelIndexChanged)
    Q_PROPERTY(int postModelIndex READ postModelIndex WRITE setPostModelIndex NOTIFY postModelIndexChanged)
    Q_PROPERTY(bool canPrepare READ canPrepare NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool canStop READ canStop NOTIFY stateFlagsChanged)
    Q_PROPERTY(const qreal& recordingLevel MEMBER recording_level_ NOTIFY recordingLevelChanged)
    Q_PROPERTY(const QString& recordedText MEMBER current_recorded_text_ NOTIFY recordedTextChanged)
    Q_PROPERTY(const QStringList& michrophones READ microphones() NOTIFY microphonesChanged)
    Q_PROPERTY(int currentMic READ currentMic WRITE setCurrentMic NOTIFY currentMicChanged)

public:
    enum RecordingState {
        Idle,
        Preparing,
        Ready,
        Recording,
        Processing,
        Done,
        Resetting,
        Error
    };
    Q_ENUM(RecordingState)

    Q_INVOKABLE void setModelIndex(int index);
    Q_INVOKABLE void setPostModelIndex(int index);
    Q_INVOKABLE void setLanguageIndex(int index);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void prepareForRecording();
    Q_INVOKABLE void saveTranscriptToFile(const QUrl &path);
    Q_INVOKABLE void reset();

    AppEngine();

    AudioController &audioController() { return audio_controller_; }
    QStringList languages()   const { return languages_; }
    QStringList modelSizes()  const { return model_sizes_; }
    QStringList microphones() const;
    int currentMic() const;
    void setCurrentMic(int index);

    int  languageIndex() const { return language_index_; }
    int  modelIndex()    const { return model_index_;    }
    int  postModelIndex()const { return post_model_index_;}

    bool canPrepare() const;
    bool canStart()   const;
    bool canStop()    const;

signals:
    void recordingStateChanged(RecordingState newState);
    void languageIndexChanged(int newIndex);
    void modelIndexChanged(int newIndex);
    void postModelIndexChanged(int newIndex);
    void stateFlagsChanged();
    void partialTextAvailable(const QString &text);
    void errorOccurred(const QString &message);
    void downloadProgress(QString name, qint64 bytesReceived, qint64 bytesTotal);
    void recordingLevelChanged();
    void recordedTextChanged();
    void microphonesChanged();
    void currentMicChanged();

private:
    RecordingState recordingState() const { return recording_state_; }
    void setRecordingState(RecordingState newState);
    void createPipelineIfNeeded();
    QCoro::Task<void> startPrepareForRecording();
    QCoro::Task<bool> prepareTranscriberModels();
    QCoro::Task<std::shared_ptr<Transcriber>> prepareModel(std::string_view modelId, std::string_view language, bool loadModel, bool submitFilalText);
    void onFinalRecordingTextAvailable(const QString &text);
    QCoro::Task<void> transcribeChunks();
    QCoro::Task<void> onRecordingDone();
    bool failed(const QString& why);
    QCoro::Task<void> doReset();

    AudioController audio_controller_;
    RecordingState recording_state_ = Idle;
    QStringList languages_;
    QStringList model_sizes_;
    int language_index_{0}; // Auto
    int model_index_{};
    int post_model_index_{};
    QString pcm_file_path_;
    std::shared_ptr<chunk_queue_t> chunk_queue_;
    std::shared_ptr<AudioRecorder> recorder_;
    std::shared_ptr<AudioFileWriter> file_writer_;
    std::shared_ptr<Transcriber> rec_transcriber_;
    std::shared_ptr<Transcriber> post_transcriber_;
    std::shared_ptr<ModelMgr> model_mgr_;
    qreal recording_level_{};
    QString current_recorded_text_;
};

std::ostream& operator << (std::ostream& os, AppEngine::RecordingState state);
