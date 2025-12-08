
#include <array>
#include <string_view>

#include <QDir>
#include <QStandardPaths>
#include <QNetworkReply>
#include <QEventLoop>

#include <qcorosignal.h>

#include "Model.h"
#include "logging.h"

using namespace std;


std::ostream& operator << (std::ostream& os, Model::State state) {
    constexpr auto states = to_array<string_view>({
        "CREATED",
        "RUNNING",
        "PREPARING",
        "LOADING",
        "LOADED",
        "READY",
        "WORKING",
        "STOPPING",
        "DONE",
        "ERROR"
    });

    return os << states.at(static_cast<size_t>(state));
}

std::ostream& operator<<(std::ostream &os, Model::CmdType cmd) {
    constexpr auto cmds = to_array<string_view>({
        "CREATE_CONTEXT",
        "COMMAND",
        "EXIT"
    });

    return os << cmds.at(static_cast<size_t>(cmd));
}

std::ostream& operator<<(std::ostream &os, const Model::Operation& op) {
    return os << op.op();
}

Model::Model(std::unique_ptr<Model::Config> && config)
    : config_{std::move(config)}
{
    assert(config_);
    worker_ = std::jthread([this] { run(); });
}

Model::~Model()
{
    stop();

    if (is_loaded_) {
        if (model_instance_) {
            model_instance_->unloadNow(); // non-coroutine version
        }
        is_loaded_ = false;
    }
}

QCoro::Task<bool> Model::init(const QString &modelId)
{
    assert(!haveModel());
    setState(State::PREPARING);
    model_instance_ = co_await ModelMgr::instance().getInstance(kind(), modelId);
    if (model_instance_ == nullptr) {
        failed(tr("Failed to get model instance for id: %1").arg(modelId));
        co_return false;
    }

    setState(State::READY);
    co_return true;
}

QCoro::Task<bool> Model::loadModel()
{
    assert(haveModel());
    assert(!is_loaded_);

    setState(State::LOADING);
    const bool ok = co_await model_instance_->load();
    if (!ok) {
        failed(tr("Failed to load model: %1").arg(model_instance_->modelId()));
        co_return false;
    }

    if (!createContextImpl()) {
        failed(tr("Failed to create context on the model"));
        co_return false;
    }

    // Relay signals
    connect(model_instance_.get(), &ModelInstanceBase::modelReady,
            this, &Model::modelReady);
    connect(model_instance_.get(), &ModelInstanceBase::partialTextAvailable,
            this, &Model::partialTextAvailable);

    if (config().submit_filal_text) {
        connect(model_instance_.get(), &ModelInstanceBase::finalTextAvailable,
                this, &Model::finalTextAvailable);
    }

    setState(State::LOADED);
    is_loaded_ = true;
    co_return true;
}

QCoro::Task<void> Model::stop()
{
    if(state() < State::STOPPING) {
        LOG_DEBUG_N << "Stopping model...";
        enqueueCommand(std::make_unique<Operation>(CmdType::EXIT));
    }

    // Wait for the stopped signal
    LOG_DEBUG_N << "Waiting for model to stop...";
    co_await qCoro(this, &Model::stopped);

    // Join the thread
    if (worker_ && worker_->joinable()) {
        LOG_DEBUG_N << "Waiting for model worker thread to join...";
        worker_->join();
        LOG_DEBUG_N << "Model worker thread joined.";
    }

    co_return;
}

void Model::setState(State state)
{
    if(state_ != state) {
        LOG_DEBUG_N << "Model state for "
                    << config().model_name
                    <<" changed from " << state_ << " to " << state;
        state_ = state;
        emit stateChanged();
    }
}

bool Model::failed(QString message)
{
    LOG_WARN_N << "Model failed: " << message.toStdString();
    setState(State::ERROR);
    emit errorOccurred(message);
    return false;
}

void Model::enqueueCommand(std::unique_ptr<Operation> &&op)
{
    assert(op);
    LOG_TRACE_N << "Enqueue command: " << *op;
    cmd_queue_.push(std::move(op));
}

void Model::run() noexcept
{
    while(state() < State::STOPPING) {
        if (have_context_) {
            setState(State::READY);
        } else {
            setState(State::RUNNING);
        }

        LOG_DEBUG_N << "waiting for command...";
        cmd_queue_t::type_t op;
        cmd_queue_.pop(op);
        if(!op) {
            LOG_ERROR_N << "No command received, exiting...";
            break;
        }

        LOG_DEBUG_N << "Processing command: " << *op;

        const auto op_type = op->op();
        try {
            switch(op_type) {
            case CmdType::CREATE_CONTEXT: {
                    const auto result = createContextImpl();
                    op->setResult(result);
                } break;
                case CmdType::COMMAND:
                    // Execute will set the result
                    op->execute();
                    break;
                case CmdType::EXIT:
                    LOG_DEBUG_N << "Exit command received, stopping model...";
                    setState(State::STOPPING);

                    // TODO: Implement and set the correct restlt
                    //stopImpl();
                    op->setResult(true);
                    break;
                default:
                    LOG_WARN_N << "Unknown command received.";
                    op->setResult(false);
                    break;
            }
        } catch (const exception& ex) {
            failed(tr("Caught exception in command loop: %1").arg(ex.what()));
        }
    }

    emit stopped();
}


void Model::Operation::execute() noexcept
{
    try {
        if (fn_) {
            const bool res = fn_();
            setResult(res);
            return;
        }

        setResult(true);
    } catch (const exception& ex) {
        setResult(false);
        LOG_WARN_N << "Exception during operation execution: " << ex.what();
    }
}
