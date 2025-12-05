#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <chrono>
#include <span>

#include "Queue.h"

#include <QObject>
#include <QThread>
#include <QNetworkAccessManager>

#include "ModelLocator.h"

struct ModelInfo {
    enum Quatization {
        Q4_0,
        Q4_1,
        Q5_0,
        Q5_1,
        Q8_0,
        FP16,
        FP32,
    };
    std::string_view id;
    std::string_view filename;
    Quatization quantization{};
    size_t size_mb{};   // approximate in megabytes
    std::string_view sha;
};

using model_list_t = std::span<const ModelInfo>;

class Model : public QObject
{
    Q_OBJECT

public:
    struct Config {
        std::string model_name;
        model_list_t models;
        bool submit_filal_text{true};
        std::string url_base{}; // https://huggingface.co/ggerganov/whisper.cpp/resolve/main
    };

    // Command types for worker thread
    enum class CmdType {
        DOWNLOAD_MODEL,
        LOAD_MODEL,
        CREATE_CONTEXT,
        COMMAND,
        EXIT
    };

    class Operation {
    public:
        Operation(CmdType type = CmdType::COMMAND) : type_(type) {}
        virtual ~Operation() = default;

        CmdType op() const noexcept { return type_; }

    private:
        CmdType type_;
        std::chrono::steady_clock::time_point timestamp_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point timestamp_start_;
        std::chrono::steady_clock::time_point timestamp_end_;
    };

    using cmd_queue_t = Queue<std::unique_ptr<Operation>>;

    enum class State {
        CREATED,
        RUNNING,
        DOWNLOADING,
        LOADING,
        READY,
        WORKING,
        STOPPING,
        DONE,
        ERROR
    };

    Model(const Config* config);
    virtual ~Model();

    QList<ModelInfo> listAvailableModels() const;
    void prepareModel();
    void loadModel();

    void cancel();
    void reset();
    void stop(); // blocking on thread join

    bool cancelled() const noexcept { return state() >= State::STOPPING;}
    bool haveModel() const noexcept { return have_model_; }
    ModelInfo model() const noexcept {
        std::lock_guard lock{mutex_};
        assert(model_.has_value());
        return model_.value();
    }
    State state() const noexcept {return state_.load();}

    const Config& config() const noexcept {
        assert(config_);
        return *config_;
    }

protected:
    void setState(State state);
    void failed(QString message);
    void enqueueCommand(std::unique_ptr<Operation> && op);

    virtual void loadModelImpl() = 0;
    //virtual void unloadModelImpl() = 0;
    virtual void createContextImpl() = 0;
    virtual void onCommand(const Operation& op) = 0;
    virtual void stopImpl() = 0;
    virtual std::string_view modelPathPostfix() const noexcept = 0;

signals:
    void partialTextAvailable(const QString &text);
    void finalTextAvailable(const QString &text);
    void modelDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void modelReady();
    void errorOccurred(const QString &message);
    void stateChanged();

private:
    void run() noexcept;
    std::optional<ModelInfo> findBestModel();
    QString findModelPath(const ModelInfo &modelInfo) const;
    void downloadModel(const ModelInfo &model);

    void setModel(ModelInfo model) noexcept {
        std::lock_guard lock{mutex_};
        model_ = model;
    }

    const Config *config_{};
    std::atomic<State> state_{State::CREATED};
    std::optional<std::jthread> worker_;
    cmd_queue_t cmd_queue_;
    QString model_path_;
    std::optional<ModelInfo> model_;
    QNetworkAccessManager *nam_ = nullptr;
    std::mutex mutex_;
    std::atomic_bool have_model_{false};
    std::atomic_bool have_context_{false};
};


std::ostream& operator << (std::ostream& os, Model::State state);
std::ostream& operator<<(std::ostream &os, Model::Operation op);
