#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <chrono>
#include <span>
#include <optional>


#include <QObject>
#include <QThread>
#include <QNetworkAccessManager>
#include <QFuture>

#include <qcorotask.h>
#include <qcoro/core/qcorofuture.h>

#include "Queue.h"
#include "ModelMgr.h"


/*! Base-class for model handling
 *
 *  The actual LLM models are handled by ModelMgr, and this class
 *  works with contexts for specific models.
 *
 *  Each instance or a Model has it's own worker thread for interacting with the
 *  model engine (whisper.cpp, llama.cpp, etc).
 *
 *  The interaction with the context is handled by subclasses that implement
 *  the pure virtual methods.
 */

class Model : public QObject
{
    Q_OBJECT

public:
    struct Config {
        std::string model_name;
        std::string from_language;
        bool submit_filal_text{true};
    };

    // Command types for worker thread
    enum class CmdType {
        CREATE_CONTEXT,
        COMMAND,
        EXIT
    };

    class Operation {
    public:
        using fn_t = std::function<bool()> ;
        Operation(CmdType type = CmdType::COMMAND)
            : type_{type} {}

        Operation(fn_t && fn, CmdType type = CmdType::COMMAND)
            : type_{type}, fn_{std::move(fn)}  {}

        ~Operation() {
            setResult(false); // If not set to true, default to false.
        }

        Operation(const Operation&) = delete;
        Operation& operator=(const Operation&) = delete;
        Operation(Operation&&) = delete;
        Operation& operator=(Operation&&) = delete;

        CmdType op() const noexcept { return type_; }

        virtual void execute() noexcept;

        void setResult(bool ok) {
            std::call_once(promise_set_, [this, ok]() {
                promise_.start();
                promise_.addResult(ok);
                promise_.finish();
            });
        }

        QFuture<bool> future() noexcept {
            return promise_.future();
        }

    private:
        QPromise<bool> promise_;
        std::once_flag promise_set_;
        CmdType type_;
        fn_t fn_;
        std::chrono::steady_clock::time_point timestamp_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point timestamp_start_;
        std::chrono::steady_clock::time_point timestamp_end_;
    };

    using cmd_queue_t = Queue<std::unique_ptr<Operation>>;

    enum class State {
        CREATED,
        RUNNING,
        PREPARING,
        LOADING,
        LOADED,
        READY,
        WORKING,
        STOPPING,
        DONE,
        ERROR
    };

    Model(std::unique_ptr<Config> &&config);
    virtual ~Model();

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    Model(const Model&&) = delete;
    Model& operator=(const Model&&) = delete;

    virtual ModelKind kind() const noexcept  = 0;
    QCoro::Task<bool> init(const QString &modelId);
    QCoro::Task<bool> loadModel();
    //QCoro::Task<bool> unloadModel();

    void cancel();
    void reset();
    QCoro::Task<void> stop();

    bool cancelled() const noexcept { return state() >= State::STOPPING;}
    bool haveModel() const noexcept { return model_instance_ != nullptr; }
    State state() const noexcept {return state_.load();}

    const Config& config() const noexcept {
        assert(config_);
        return *config_;
    }

    std::shared_ptr<ModelInstanceBase> modelInstance() const noexcept {
        return model_instance_;
    }

    const auto& worker() const noexcept {
        return worker_;
    }

protected:
    void setState(State state);
    bool failed(QString message);
    void enqueueCommand(std::unique_ptr<Operation> && op);

    virtual bool createContextImpl() = 0;

signals:
    void partialTextAvailable(const QString &text);
    void finalTextAvailable(const QString &text);
    void modelDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void modelReady();
    void errorOccurred(const QString &message);
    void stateChanged();
    void stopped();

private:
    void run() noexcept;

    std::unique_ptr<Config> config_;
    std::shared_ptr<ModelInstanceBase> model_instance_;
    std::atomic<State> state_{State::CREATED};
    std::optional<std::jthread> worker_;
    cmd_queue_t cmd_queue_;
    std::mutex mutex_;
    std::atomic_bool have_context_{false};
    std::atomic_bool is_loaded_{false};
};


std::ostream& operator << (std::ostream& os, Model::State state);
std::ostream& operator<<(std::ostream &os, const Model::Operation& op);
