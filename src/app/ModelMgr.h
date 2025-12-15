#pragma once

#include <filesystem>

#include <QObject>
#include <QQmlEngine>
#include <QNetworkReply>

#include <qcorotask.h>

#include "qvw/EngineBase.h"
#include "ModelInfo.h"

/*! Model Manager

 Manages instances of different models (e.g., Whisper, General NLP models).
 Provides loading/unloading and access to model instances.

 Signals:
 - modelDownloadProgress(qint64 bytesReceived, qint64 bytesTotal): Emitted during model download.
 - modelReady(const QString& modelId): Emitted when a model is ready for use.
 - stateChanged(): Emitted when the state of the manager changes.
*/

enum class ModelKind {
    WHISPER,
    GENERAL
};


// Instance of a model
class ModelInstance : public QObject{
    Q_OBJECT
public:
    ModelInstance(const ModelInfo& modelInfo, QString fullPath, QObject *parent = nullptr)
        : QObject(parent), full_path_{fullPath}, model_info_{modelInfo} {}

    virtual ModelKind kind() const noexcept = 0;

    bool isLoaded() const noexcept {
        return loaded_count_ > 0;
    }

    QCoro::Task<bool> load() noexcept;
    QCoro::Task<bool> unload() noexcept;
    bool unloadNow() noexcept;

    const ModelInfo& modelInfo() const noexcept {
        return model_info_;
    }

    const QString & modelId() const noexcept {
        return model_id_;
    }

    const auto path() const noexcept {
        return full_path_;
    }

signals:
    void partialTextAvailable(const QString &text);
    void finalTextAvailable(const QString &text);
    void modelReady();

// protected:
//     virtual bool loadImpl() noexcept = 0;
//     virtual bool unloadImpl() noexcept = 0;
//     virtual void *ctx() = 0 ;
//     virtual bool haveCtx() const noexcept = 0 ;

private:
    QString full_path_;
    int loaded_count_{};
    ModelInfo model_info_;
    QString model_id_{QString::fromUtf8(model_info_.id)};
    std::shared_ptr<qvw::ModelCtx> model_ctx_;
};

class ReplyEventProxy : public QObject {
    Q_OBJECT
public:
    enum class Event {
        ReadyRead,
        Finished,
        Error
    };
    Q_ENUM(Event)

    explicit ReplyEventProxy(QNetworkReply *reply, QObject *parent = nullptr)
        : QObject(parent)
    {
        // readyRead
        connect(reply, &QNetworkReply::readyRead,
                this, [this] {
                    emit event(Event::ReadyRead);
                });

        // finished
        connect(reply, &QNetworkReply::finished,
                this, [this] {
                    emit event(Event::Finished);
                });

        // errorOccurred (Qt 5/6 name is errorOccurred; if you're on Qt5 < 5.15, adjust)
        connect(reply, &QNetworkReply::errorOccurred,
                this, [this](QNetworkReply::NetworkError) {
                    emit event(Event::Error);
                });
    }

signals:
    void event(ReplyEventProxy::Event ev);
};



class ModelMgr : public QObject
{
    Q_OBJECT

public:
    using model_ctx_t = std::shared_ptr<ModelInstance>;

    explicit ModelMgr(QObject *parent = nullptr);

    static ModelMgr& instance() {
        assert(self_);
        return *self_;
    }

    /*! Get or create an instance of the specified model.

     If the model is not already instatiated, it will be downloaded and made available.

     \param kind The kind of model (e.g., WHISPER, GENERAL).
     \param modelId The identifier of the model to load.
     \return A coro-task that resolves to a shared pointer to the model instance.
    */
    QCoro::Task<model_ctx_t> getInstance(ModelKind kind, const QString& modelId) noexcept;

    model_list_t availableModels(ModelKind kind) const noexcept;
    models_t loadedModels(ModelKind kind) const noexcept;
    std::optional<ModelInfo> findBestModel(ModelKind kind, const QString& modelName) const noexcept;

signals:
    void downloadProgress(QString name, qint64 bytesReceived, qint64 bytesTotal);
    void modelReady(const QString& modelId);
    void stateChanged();

private:
    using instances_map_t = std::map<QString /* model id */, std::shared_ptr<ModelInstance>>;
    QCoro::Task<bool> makeAvailable(ModelKind kind, const ModelInfo& modelInfo) noexcept;
    std::filesystem::path findModelPath(ModelKind kind, const ModelInfo &modelInfo) const;
    QCoro::Task<bool> downloadModel(ModelKind kind, const ModelInfo &modelInfo, const QString& fullPath) noexcept;
    QCoro::Task<bool> downloadFile(const QString& name, const QUrl& url, const QString& fullPath) noexcept;

    instances_map_t& instances(ModelKind kind) {
        return instances_.at(static_cast<size_t>(kind));
    }

    const instances_map_t& instances(ModelKind kind) const {
        return instances_.at(static_cast<size_t>(kind));
    }

    QNetworkAccessManager *nam_{};
    std::array<instances_map_t, 2 /* Kind */> instances_;
    static ModelMgr *self_;
};

std::ostream& operator<<(std::ostream &os, ModelKind kind);
