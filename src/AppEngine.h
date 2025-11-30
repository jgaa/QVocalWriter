#pragma once

#include <QObject>
#include <QQmlComponent>

#include "AudioController.h"

class AudioRecorder;
class ChunkQueue;
class AudioFileWriter;
class Transcriber;         // base class
class TranscriberWhisper;  // concrete class

class AppEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(RecordingState recordingState READ recordingState NOTIFY recordingStateChanged)
    Q_PROPERTY(QStringList languages READ languages CONSTANT)
    Q_PROPERTY(QStringList modelSizes READ modelSizes CONSTANT)
    Q_PROPERTY(int languageIndex READ languageIndex WRITE setLanguageIndex NOTIFY languageIndexChanged)
    Q_PROPERTY(int modelIndex READ modelIndex WRITE setModelIndex NOTIFY modelIndexChanged)
    Q_PROPERTY(bool canPrepare READ canPrepare NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY stateFlagsChanged)
    Q_PROPERTY(bool canStop READ canStop NOTIFY stateFlagsChanged)

public:
    enum RecordingState {
        Idle,
        Preparing,
        Ready,
        Recording,
        Processing,
        Finishing,
        DoneRecording,
        Error
    };
    Q_ENUM(RecordingState)

    Q_INVOKABLE void setModelIndex(int index);
    Q_INVOKABLE void setLanguageIndex(int index);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void prepare();

    AppEngine();

    AudioController &audioController() { return audio_controller_; }
    QStringList languages()   const { return languages_; }
    QStringList modelSizes()  const { return model_sizes_; }

    int  languageIndex() const { return language_index_; }
    int  modelIndex()    const { return model_index_;    }

    bool canPrepare() const;
    bool canStart()   const;
    bool canStop()    const;

signals:
    void recordingStateChanged(RecordingState newState);
    void languageIndexChanged(int newIndex);
    void modelIndexChanged(int newIndex);
    void stateFlagsChanged();
    void partialTextAvailable(const QString &text);
    void errorOccurred(const QString &message);
    void modelDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
    RecordingState recordingState() const { return recording_state_; }
    void setRecordingState(RecordingState newState);
    void createPipelineIfNeeded();
    void prepareTranscriber();
    void onTranscriberPrepared(bool ok, const QString &errorText);

    AudioController audio_controller_;
    RecordingState recording_state_ = Idle;
    QStringList languages_;
    QStringList model_sizes_;
    int language_index_{0}; // Auto
    int model_index_{};
    QString pcm_file_path_;
    std::shared_ptr<ChunkQueue> chunk_queue_;
    std::shared_ptr<AudioRecorder> recorder_;
    std::shared_ptr<AudioFileWriter> file_writer_;
    std::shared_ptr<TranscriberWhisper> transcriber_;
};
