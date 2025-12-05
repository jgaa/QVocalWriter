
#include <array>
#include <string_view>
#include <filesystem>

#include <QDir>
#include <QStandardPaths>
#include <QNetworkReply>
#include <QEventLoop>

#include "Model.h"

#include "logging.h"

using namespace std;


std::ostream& operator << (std::ostream& os, Model::State state) {
    constexpr auto states = to_array<string_view>({
        "CREATED",
        "RUNNING",
        "DOWNLOADING",
        "LOADING",
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
        "DOWNLOAD_MODEL",
        "LOAD_MODEL",
        "CREATE_CONTEXT",
        "COMMAND",
        "EXIT"
    });

    return os << cmds.at(static_cast<size_t>(cmd));
}

std::ostream& operator<<(std::ostream &os, Model::Operation op) {
    return os << op.op();
}

Model::Model(const Model::Config *config)
    : config_(config)
{
    assert(config_);
    worker_ = std::jthread([this] { run(); });
}

Model::~Model()
{
    stop();
}

void Model::prepareModel()
{
    if (auto model = findBestModel()) {
        LOG_INFO_N << "Preparing model: " << model->id;
        model_path_ = findModelPath(model.value());
        setModel(model.value());

        // See if the model is already downloaded
        if (QFile::exists(model_path_)) {
            LOG_INFO_N << "Model file exists at: " << model_path_;
            have_model_ = true;
            return;
        }

        LOG_INFO_N << "Model file '" << model->filename << "' not found locally, downloading...";
        setState(State::DOWNLOADING);
        enqueueCommand(std::make_unique<Operation>(CmdType::DOWNLOAD_MODEL));
        return;
    }

    failed("Could not find suitable model for name: " + QString::fromStdString(config().model_name));
}

void Model::loadModel()
{
    assert(have_model_);
    setState(State::LOADING);
    enqueueCommand(std::make_unique<Operation>(CmdType::LOAD_MODEL));
}

void Model::setState(State state)
{
    if(state_ != state) {
        LOG_DEBUG_N << "Model state changed from " << state_ << " to " << state;
        state_ = state;
        emit stateChanged();
    }
}

void Model::failed(QString message)
{
    LOG_WARN_N << "Model failed: " << message.toStdString();
    setState(State::ERROR);
    emit errorOccurred(message);
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
                case CmdType::DOWNLOAD_MODEL:
                    downloadModel(model());
                    break;
                case CmdType::LOAD_MODEL:
                    loadModelImpl();
                    break;
                case CmdType::CREATE_CONTEXT:
                    createContextImpl();
                    break;
                case CmdType::COMMAND:
                    onCommand(*op);
                    break;
                case CmdType::EXIT:
                    LOG_DEBUG_N << "Exit command received, stopping model...";
                    setState(State::STOPPING);
                    stopImpl();
                    break;
                default:
                    LOG_WARN_N << "Unknown command received.";
                    break;
            }
        } catch (const exception& ex) {
            failed(tr("Caught exception in command loop: %1").arg(ex.what()));
        }
    }
}

std::optional<ModelInfo> Model::findBestModel()
{
    optional<ModelInfo> rval;
    const auto models = config().models;
    for (const auto &m : models) {
        // Strip off quantization suffix for matching
        auto base_name = m.id;
        if (auto pos = base_name.find('-'); pos != std::string_view::npos) {
            base_name = base_name.substr(0, pos);
        }
        if (base_name == config().model_name) {
            if (rval) {
                if (m.quantization == ModelInfo::Quatization::Q5_1) {
                    rval = m;
                } else if (rval->quantization != ModelInfo::Quatization::Q5_1
                           && m.quantization == ModelInfo::Quatization::Q5_0) {
                    rval = m;
                } else if ((rval->quantization != ModelInfo::Quatization::Q5_1
                            && rval->quantization != ModelInfo::Quatization::Q5_1)
                           && rval->quantization < m.quantization) {
                    // prefer higher-quality quantization, unless we have Q5_1/Q5_0
                    rval = m;
                }
            } else {
                rval = m;
            }
        }
    }

    LOG_TRACE_N  << "Best model lookup for '" << config().model_name
                 << "' found: "
                 << (rval ? rval->id : "none");

    return rval;
}

QString Model::findModelPath(const ModelInfo &modelInfo) const
{
    filesystem::path model_dir =  QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation).toStdString();
    model_dir /= modelPathPostfix();

    if (!filesystem::is_directory(model_dir)) {
        LOG_INFO_N << "Creating model directory: " << model_dir;
        filesystem::create_directories(model_dir);
    }

    const auto file_path = model_dir / modelInfo.filename;
    return QString::fromStdString(file_path.string());
}

void Model::downloadModel(const ModelInfo &model)
{
    if (!nam_) {
        nam_ = new QNetworkAccessManager(this);
    }

    const QUrl url{QStringLiteral("%1/%1").arg(config().url_base).arg(model.filename)};

    LOG_INFO_N << "Downloading model " << model.id << " from " << url.toString();

    QNetworkRequest req(url);
    auto reply = nam_->get(req);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::downloadProgress,
                     [this] (qint64 bytesReceived, qint64 bytesTotal) {

                         LOG_TRACE_N << "Model download progress: "
                                     << bytesReceived << " / " << bytesTotal;

                         emit modelDownloadProgress(bytesReceived, bytesTotal);
                     });

    QObject::connect(reply, &QNetworkReply::finished,
                     &loop, &QEventLoop::quit);

    loop.exec(); // blocks this thread until finished

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        failed(tr("Failed to download model: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray data = reply->readAll();
    assert(!model_path_.isEmpty());
    {
        QFile out(model_path_);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            failed(tr("Failed to save model to %1").arg(model_path_));
            return;
        }
        out.write(data);
        out.close();
    }

    emit modelReady();
    have_model_ = true;
}
