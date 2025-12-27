#include <filesystem>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QNetworkReply>
#include <QFile>
#include <QtConcurrent/QtConcurrentRun>
#include <QFuture>
#include <QSettings>

#include <qcorofuture.h>

#include <qcoro/network/qcoronetwork.h>
#include <qcoro/core/qcorosignal.h>
#include <qcoronetworkreply.h>
#include <qcoroiodevice.h>

#include "logging.h"

#include "qvw/WhisperEngine.h"
#include "qvw/LlamaEngine.h"
#include "ModelMgr.h"
#include "ScopedTimer.h"


using namespace std;

namespace {

using mi_t = ModelInfo;
constexpr auto all_whisper_models = std::to_array<mi_t>({
    // Whisper
    {"tiny", "tiny-q5_1", ModelInfo::PromptStyle::None, "ggml-tiny-q5_1.bin", mi_t::Q5_1, 31,
     "2827a03e495b1ed3048ef28a6a4620537db4ee51", mi_t::Transcribe,
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"},
    {"tiny-en", "tiny.en-q5_1", ModelInfo::PromptStyle::None, "ggml-tiny.en-q5_1.bin", mi_t::Q5_1, 31,
     "3fb92ec865cbbc769f08137f22470d6b66e071b6", mi_t::Transcribe,
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"},
    {"base", "base-q5_1", ModelInfo::PromptStyle::None, "ggml-base-q5_1.bin", mi_t::Q5_1, 57,
     "a3733eda680ef76256db5fc5dd9de8629e62c5e7", mi_t::Transcribe,
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"},
    {"base-en", "base.en-q5_1", ModelInfo::PromptStyle::None, "ggml-base.en-q5_1.bin", mi_t::Q5_1, 57,
     "d26d7ce5a1b6e57bea5d0431b9c20ae49423c94a", mi_t::Transcribe,
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"},
    {"small", "small-q5_1", ModelInfo::PromptStyle::None, "ggml-small-q5_1.bin", mi_t::Q5_1, 181,
     "6fe57ddcfdd1c6b07cdcc73aaf620810ce5fc771", mi_t::Transcribe,
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"},
    {"small-en", "small.en-q5_1", ModelInfo::PromptStyle::None, "ggml-small.en-q5_1.bin", mi_t::Q5_1, 181,
     "20f54878d608f94e4a8ee3ae56016571d47cba34", mi_t::Transcribe,
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"},
    {"medium", "medium-q5_0", ModelInfo::PromptStyle::None, "ggml-medium-q5_0.bin", mi_t::Q5_0, 514,
     "7718d4c1ec62ca96998f058114db98236937490e", mi_t::Transcribe,
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"},
    {"medium-en", "medium.en-q5_0", ModelInfo::PromptStyle::None, "ggml-medium.en-q5_0.bin", mi_t::Q5_0, 514,
     "bb3b5281bddd61605d6fc76bc5b92d8f20284c3b", mi_t::Transcribe,
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"},
    {"large", "large-v3-q5_0", ModelInfo::PromptStyle::None, "ggml-large-v3-q5_0.bin", mi_t::Q5_0, 1100,
     "e6e2ed78495d403bef4b7cff42ef4aaadcfea8de", mi_t::Transcribe,
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"},
    {"turbo", "large-v3-turbo-q4_0", ModelInfo::PromptStyle::None, "ggml-large-v3-turbo-q5_0.bin", mi_t::Q5_0,
     547, "e050f7970618a659205450ad97eb95a18d69c9ee", mi_t::Transcribe,
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"},
    {"turbo-best", "large-v3-turbo-q8_0", ModelInfo::PromptStyle::None, "ggml-large-v3-turbo-q5_0.bin",
     mi_t::Q8_0, 547, "e050f7970618a659205450ad97eb95a18d69c9ee",
     mi_t::Transcribe,
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/"},
});

constexpr auto all_llama_models = std::to_array<mi_t>({
    // ---- Lite (≈8 GB RAM laptops) ----
    {
        "lite",                                    // user-facing name
        "qwen2.5-3b-instruct-q4_k_m",
        ModelInfo::PromptStyle::ChatML,
        "Qwen2.5-3B-Instruct-Q4_K_M.gguf",
        mi_t::Q4_0,                                // map K-quants into your buckets as you prefer
        1930,
        "",
        mi_t::Chat | mi_t::Rewrite | mi_t::Translate,
        "https://huggingface.co/bartowski/Qwen2.5-3B-Instruct-GGUF/resolve/main/"
    },

    // ---- Balanced (≈16–32 GB) ----
    {
        "balanced",
        "llama3.1-8b-instruct-q4_k_m",
        ModelInfo::PromptStyle::Llama3,
        "Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",
        mi_t::Q4_0,
        4920,
        "",
        mi_t::Chat | mi_t::Rewrite,
        "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/main/"
    },
    {
        "balanced-translate",
        "qwen2.5-7b-instruct-q4_k_m",
        ModelInfo::PromptStyle::ChatML,
        "Qwen2.5-7B-Instruct-Q4_K_M.gguf",
        mi_t::Q4_0,
        4200,
        "",
        mi_t::Chat | mi_t::Rewrite | mi_t::Translate,
        "https://huggingface.co/bartowski/Qwen2.5-7B-Instruct-GGUF/resolve/main/"
    },

    // ---- Pro (≈32–64 GB) ----
    {
        "pro",
        "mistral-small-instruct-2409-q4_k_m",
        ModelInfo::PromptStyle::Mistral,
        "Mistral-Small-Instruct-2409-Q4_K_M.gguf",
        mi_t::Q4_0,
        13340,
        "",
        mi_t::Chat | mi_t::Rewrite,
        "https://huggingface.co/bartowski/Mistral-Small-Instruct-2409-GGUF/resolve/main/"
    },
    {
        "pro-translate",
        "qwen2.5-14b-instruct-q4_k_m",
        ModelInfo::PromptStyle::ChatML,
        "Qwen2.5-14B-Instruct-Q4_K_M.gguf",
        mi_t::Q4_0,
        8990,
        "",
        mi_t::Chat | mi_t::Rewrite | mi_t::Translate,
        "https://huggingface.co/bartowski/Qwen2.5-14B-Instruct-GGUF/resolve/main/"
    },

    // ---- Workstation (≈64–128 GB / strong GPU) ----
    {
        "workstation",
        "llama3.1-70b-instruct-q4_k_m",
        ModelInfo::PromptStyle::Llama3,
        "Meta-Llama-3.1-70B-Instruct-Q4_K_M.gguf",
        mi_t::Q4_0,
        42520,
        "",
        mi_t::Chat | mi_t::Rewrite,
        "https://huggingface.co/bartowski/Meta-Llama-3.1-70B-Instruct-GGUF/resolve/main/"
    },
    {
        "workstation-translate",
        "qwen2.5-32b-instruct-q4_k_m",
        ModelInfo::PromptStyle::ChatML,
        "Qwen2.5-32B-Instruct-Q4_K_M.gguf",
        mi_t::Q4_0,
        19850,
        "",
        mi_t::Chat | mi_t::Rewrite | mi_t::Translate,
        "https://huggingface.co/bartowski/Qwen2.5-32B-Instruct-GGUF/resolve/main/"
    },

    // ---- Extreme (≈128+ GB / “use all the RAM”) ----
    // Higher-quality (still single-file) 70B option:
    {
        "extreme",
        "llama3.1-70b-instruct-q5_k_s",
        ModelInfo::PromptStyle::Llama3,
        "Meta-Llama-3.1-70B-Instruct-Q5_K_S.gguf",
        mi_t::Q5_1,
        48660,
        "",
        mi_t::Chat | mi_t::Rewrite,
        "https://huggingface.co/bartowski/Meta-Llama-3.1-70B-Instruct-GGUF/resolve/main/"
    },
    {
        "extreme-translate",
        "qwen2.5-72b-instruct-q4_k_m",
        ModelInfo::PromptStyle::ChatML,
        "Qwen2.5-72B-Instruct-Q4_K_M.gguf",
        mi_t::Q4_0,
        47420,
        "",
        mi_t::Chat | mi_t::Rewrite | mi_t::Translate,
        "https://huggingface.co/bartowski/Qwen2.5-72B-Instruct-GGUF/resolve/main/"
    },

    //  “heavy MoE” (good writing/chat; not primarily translation) ----
    {
        "heavy-moe",
        "mixtral-8x7b-instruct-v0.1-q4_k_m",
        ModelInfo::PromptStyle::Mistral,
        "mixtral-8x7b-instruct-v0.1.Q4_K_M.gguf",
        mi_t::Q4_0,
        26400,
        "",
        mi_t::Chat | mi_t::Rewrite,
        "https://huggingface.co/TheBloke/Mixtral-8x7B-Instruct-v0.1-GGUF/resolve/main/"
    },

    {
        "heavy-moe-deepseek",
        "mixtral-8x7b-instruct-v0.1-deepseek-q4_k_m",
        ModelInfo::PromptStyle::Mistral,
        "mixtral-8x7b-instruct-v0.1-deepseek.Q4_K_M.gguf",
        mi_t::Q4_0,
        26400,
        "",
        mi_t::Chat | mi_t::Rewrite,
        "https://huggingface.co/deepseekr/Mixtral-8x7B-Instruct-v0.1-Deepseek-GGUF/resolve/main/"
    },

    {
        "heavy-moe-uncensored",
        "mixtral-8x7b-instruct-v0.1-uncensored-q4_k_m",
        ModelInfo::PromptStyle::Mistral,
        "mixtral-8x7b-instruct-v0.1-uncensored.Q4_K_M.gguf",
        mi_t::Q4_0,
        26400,
        "",
        mi_t::Chat | mi_t::Rewrite,
        "https://huggingface.co/bartowski/Mixtral-8x7B-Instruct-v0.1-Uncensored-GGUF/resolve/main/"
    }
});

string_view dirPrefix(ModelKind kind) {
    constexpr auto prefixes = std::to_array<const char*>({
        "whisper_models",
        "general_models"
    });

    return prefixes.at(static_cast<size_t>(kind));
}

string_view modelDownloadUrlBase(ModelKind kind) {
    constexpr auto urls = std::to_array<const char*>({
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/",
        ""
    });

    return urls.at(static_cast<size_t>(kind));
}

} // anon ns

std::ostream& operator<<(std::ostream &os, ModelKind kind) {
    constexpr auto names = std::to_array<const char*>({
        "WHISPER",
        "GENERAL"
    });
    return os << names.at(static_cast<size_t>(kind));
}

ModelMgr *ModelMgr::self_{};

ModelMgr::ModelMgr(QObject *parent)
{
    assert(!self_);
    self_ = this;
}

model_list_t ModelMgr::availableModels(ModelKind kind) const noexcept
{
    switch(kind) {
    case ModelKind::WHISPER:
        return all_whisper_models;
    case ModelKind::GENERAL:
        return all_llama_models;
    default:
        LOG_ERROR_N << "Unknown model kind requested.";
    }

    return {};
}

models_t ModelMgr::availableModels(ModelKind kind, ModelInfo::Capability purpose) const noexcept
{
    models_t models;
    models.reserve(instances_.size());

    for (const auto& m : availableModels(kind)) {
        if ((m.capabilities & purpose) == purpose) {
            models.push_back(&m);
        }
    }

    LOG_TRACE_N << "Found " << models.size()
                << " models for kind=" << kind
                << " and purpose=" << purpose;
    return models;
}

models_t ModelMgr::loadedModels(ModelKind kind) const noexcept
{
    models_t models;
    models.reserve(instances_.size());

    for (const auto& [modelId, inst] : instances(kind)) {
        if (inst->isLoaded()) {
            models.push_back(&inst->modelInfo());
        }
    }

    return models;
}

std::optional<ModelInfo> ModelMgr::findModelById(ModelKind kind, const QString &modelName) const noexcept
{
    const auto model_id = modelName.toStdString();
    optional<ModelInfo> rval;
    const auto models = availableModels(kind);

    for (const auto &m : models) {
        if (m.id == model_id) {
            return m;
        }
    }
    LOG_WARN_N << "No GENERAL model found matching id='" << model_id << "'";
    return std::nullopt;
}

std::optional<ModelInfo> ModelMgr::findModelByName(ModelKind kind, const QString &name) const noexcept
{
    const auto mname = name.toStdString();
    const auto models = availableModels(kind);

    for (const auto &m : models) {
        if (m.name == name && m.id.empty() == false) {
            return m;
        }
    }

    LOG_WARN_N << "No GENERAL model found matching name='" << name << "'";
    return std::nullopt;
}

qvw::WhisperEngine &ModelMgr::whisperEngine() {

    if (!whisper_engine_) {
        whisper_engine_ = qvw::WhisperEngine::create({});
        if (!whisper_engine_) {
            LOG_ERROR_N << "Failed to create Whisper engine instance.";
            throw std::runtime_error{"Failed to create Whisper engine instance."};
        }

        whisper_engine_->setLogger(logfault_fwd::forward_to_logfault,
                                   static_cast<logfault_fwd::Level>(
                                       ::logfault::LogManager::Instance().GetLoglevel()));
    }

    assert(whisper_engine_);
    return *whisper_engine_;
}

qvw::LlamaEngine &ModelMgr::llamaEngine()
{
    if (!llama_engine_) {
        llama_engine_ = qvw::LlamaEngine::create({});
        if (!llama_engine_) {
            LOG_ERROR_N << "Failed to create Llama engine instance.";
            throw std::runtime_error{"Failed to create Llama engine instance."};
        }

        llama_engine_->setLogger(logfault_fwd::forward_to_logfault,
                                 static_cast<logfault_fwd::Level>(
                                     ::logfault::LogManager::Instance().GetLoglevel()));
    }

    assert(llama_engine_);
    return *llama_engine_;
}

bool ModelMgr::isDownloaded(ModelKind kind, const ModelInfo &mi) const
{
    const auto model_path = findModelPath(kind, mi);
    return filesystem::exists(model_path);
}

QCoro::Task<bool> ModelMgr::makeAvailable(ModelKind kind, const ModelInfo &modelInfo) noexcept
{
    const auto model_path = findModelPath(kind, modelInfo);

    LOG_DEBUG_N << "Making model available: kind=" << kind
                 << ", id='" << modelInfo.id << "'"
                 << ", path='" << model_path << "'";

    // Check if the model file already exists
    if (filesystem::exists(model_path)) {
        LOG_DEBUG_N << "Model file already exists on disk: " << model_path;
        co_return true;
    }

    // Download the model file
    if (!co_await downloadModel(kind, modelInfo, QString::fromUtf8(model_path.string()))) {
        co_return false;
    }

    co_return true;
}

std::filesystem::path ModelMgr::findModelPath(ModelKind kind, const ModelInfo &modelInfo) const
{
    auto base = QSettings{}.value("models/path", "").toString().trimmed();
    if (base.isEmpty()) {
        // This is supposed to be set in main(). This is a fallback.
        LOG_WARN_N << "Model path not set in settings; using default.";
        base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/models";
    }

    filesystem::path model_dir = base.toStdString();
    model_dir /= dirPrefix(kind);

    LOG_TRACE_N << "Base model directory: " << model_dir;
    if (!filesystem::is_directory(model_dir)) {
        LOG_INFO_N << "Creating model directory: " << model_dir;
        filesystem::create_directories(model_dir);
    }

    const auto file_path = model_dir / modelInfo.filename;
    return file_path;
}

QCoro::Task<bool> ModelMgr::downloadModel(ModelKind kind, const ModelInfo &modelInfo, const QString& fullPath) noexcept
{
    // Construct download URL
    const QString id = QString::fromUtf8(modelInfo.id);
    QString surl{QString::fromUtf8(modelInfo.download_url)};

    if (modelInfo.download_url.ends_with('/')) {
        surl += QString::fromUtf8(modelInfo.filename);
    }

    const QUrl url{surl};

    LOG_INFO_N << "Starting download of model: kind=" << kind
                 << ", id='" << modelInfo.id << "'"
                 << ", url='" << url.toString() << "'"
                 << ", path='" << fullPath << "'";

    const bool success = co_await downloadFile(id, url, fullPath);
    if (!success) {
        LOG_ERROR_N << "Failed to download model file: " << url.toString();
        co_return false;
    }

    LOG_INFO_N << "Model file downloaded successfully: " << fullPath;
    emit modelDownloaded(kind, string{modelInfo.id});
    co_return true;
}

QCoro::Task<bool> ModelMgr::downloadFile(const QString& name,
                                         const QUrl &url,
                                         const QString &fullPath) noexcept
{
    if (!nam_) {
        nam_ = new QNetworkAccessManager(this);
    }

    LOG_DEBUG_N << "Downloading file from URL: " << url.toString()
                << " to path: " << fullPath;

    const QString tmpPath = fullPath + ".part";

    QNetworkRequest request{url};
    QNetworkReply *reply = nam_->get(request);

    // Ensure reply gets deleted and temp file cleaned up on early exit
    const auto guard = qScopeGuard([reply, tmpPath] {
        if (reply) {
            reply->deleteLater();
        }
        if (QFile::exists(tmpPath)) {
            LOG_DEBUG_N << "Removing temporary file: " << tmpPath;
            QFile::remove(tmpPath);
        }
    });

    QFile out(tmpPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        reply->abort();
        co_return false;
    }

    bool writeError = false;

    QObject::connect(reply, &QNetworkReply::downloadProgress,
                     [this, url, name](qint64 bytesReceived, qint64 bytesTotal){

        if (bytesTotal > 0) {
            const double ratio = static_cast<double>(bytesReceived) / static_cast<double>(bytesTotal);
            emit downloadProgressRatio(name, ratio);
        }
    });

    // Our “any-of-these-signals” proxy
    ReplyEventProxy proxy{reply, reply};  // parent = reply for lifetime tying

    auto drainToFile = [&](QNetworkReply *r) {
        while (r->bytesAvailable() > 0) {
            QByteArray chunk = r->read(64 * 1024); // 64 KiB
            if (chunk.isEmpty()) {
                break;
            }

            const qint64 written = out.write(chunk);
            if (written != chunk.size()) {
                writeError = true;
                r->abort();
                break;
            }
        }
    };

    // Main loop: wait for *any* event, act based on what happened
    while (true) {
        //LOG_TRACE_N << "Waiting for download events...";
        // First drain whatever we already have
        drainToFile(reply);
        if (writeError) {
            LOG_ERROR_N << "Disk write error during file downloading "
                        << url.toString();
            co_return false;
        }

        // If we're done (or in error) and there's nothing left, break
        if (reply->isFinished() && reply->bytesAvailable() == 0) {
            LOG_TRACE_N << "Download finished.";
            break;
        }

        if (reply->error() != QNetworkReply::NoError) {
            // errorOccurred may have fired already
            LOG_ERROR_N << "Download error detected: "
                        << reply->errorString();
            co_return false;
        }

        // Wait for next relevant signal
        const auto ev =
            co_await qCoro(&proxy, &ReplyEventProxy::event);

        switch (ev) {
        case ReplyEventProxy::Event::ReadyRead:
            // loop will drain on next iteration
            break;

        case ReplyEventProxy::Event::Finished:
            // Finished – there might still be some bytes buffered
            drainToFile(reply);
            // break out on next loop check
            break;

        case ReplyEventProxy::Event::Error:
            LOG_ERROR_N << "Download error signaled: "
                        << reply->errorString();
            co_return false;

            drainToFile(reply);   // optional; often nothing left
            break;
        }
    }

    out.flush();
    out.close();

    if (writeError) {
        QFile::remove(tmpPath);
        co_return false;
    }

    // Network error?
    if (reply->error() != QNetworkReply::NoError) {
        LOG_ERROR_N << "Network error during file downloading "
                    << url.toString()
                    << ": "
                    << reply->errorString();
        co_return false;
    }

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus < 200 || httpStatus >= 300) {
        LOG_ERROR_N << "Download error during file downloading "
                    << url.toString()
                    << ": HTTP status "
                    << httpStatus;
        co_return false;
    }

    if (!QFile::rename(tmpPath, fullPath)) {
        LOG_ERROR_N << "Failed to rename temporary file "
                    << tmpPath
                    << " to final path "
                    << fullPath;
        co_return false;
    }

    LOG_DEBUG_N << "File downloaded successfully: " << fullPath;
    co_return true;
}

QCoro::Task<ModelMgr::model_ctx_t> ModelMgr::getInstance(ModelKind kind, const QString &modelId) noexcept
{
    LOG_DEBUG_N << "Requesting model instance: kind=" << kind
                 << ", id='" << modelId << "'";

    auto& inst_map = instances(kind);

    // See if the model is already available
    {
        auto it = inst_map.find(modelId);
        if (it != inst_map.end()) {
            LOG_DEBUG_N << "Found existing model instance for id='" << modelId << "'";
            assert(it->second);
            LOG_DEBUG_N << "Returning existing model instance. Loaded=" << it->second->isLoaded();

            // Weird behaviour here. `co_return it->second` empties it->second (calling `std::move()`?)
            // The brackets force a copy of the original shared_pointer instance.
            co_return {it->second};
        }
    }

    if (auto model = findModelById(kind, modelId)) {
        const auto path = findModelPath(kind, *model);
        auto instance = std::make_shared<ModelInstance>(kind, *model, QString::fromUtf8(path.string()));

        if (!co_await makeAvailable(kind, instance->modelInfo())) {
            LOG_ERROR_N << "Failed to make model available: id='" << modelId << "'";
            co_return nullptr;
        }

        // Assert leftovers from finding the co_return problem above.
        assert(instance);
        assert(!instances(kind).contains(modelId));
        inst_map[modelId] = instance;
        assert(instances(kind).contains(modelId));
        assert(inst_map[modelId] != nullptr);

        co_return instance;
    }

    co_return nullptr;
}

QCoro::Task<bool> ModelInstance::load() noexcept
{
    if (++loaded_count_ == 1) {
        auto ok = co_await  QtConcurrent::run([this]() -> bool {
            LOG_DEBUG_N << "Loading model instance: " << modelId();
            const ScopedTimer timer;
            try {
                const auto ok =  load(kind());
                LOG_DEBUG_N << "Model instance loaded in "
                             << timer.elapsed() << " seconds: "
                             << modelId()
                            << ". result=" << ok;
                return ok;
            } catch (const std::exception &e) {
                LOG_ERROR_N << "Exception during model loading: " << e.what();
                return false;
            }
        });
        co_return ok;
    }

    co_return true;
}

QCoro::Task<bool> ModelInstance::unload() noexcept
{
    LOG_TRACE_N << "Unloading model instance: " << modelId()
                << ". current load count=" << loaded_count_;

    assert(loaded_count_ > 0);
    if (--loaded_count_ ==0) {
        co_return co_await  QtConcurrent::run([this]() -> bool {
            return unloadNow();
        });
    }

    co_return true;
}

bool ModelInstance::unloadNow() noexcept
{
    LOG_DEBUG_N << "Unloading model instance: " << modelId();

    ScopedTimer timer;
    try {
        model_ctx_.reset();
        loaded_count_ = 0;
    } catch (const std::exception &e) {
        LOG_ERROR_N << "Exception during model context reset: " << e.what();
        return false;
    }

    LOG_DEBUG_N << "Model instance unloaded in "
                << timer.elapsed() << " seconds: "
                << modelId();
    return true;
}

bool ModelInstance::load(ModelKind kind)
{
    switch (kind) {
    case ModelKind::WHISPER: {
        return loadWhisper();
    }
    case ModelKind::GENERAL: {
        return loadLlama();
    }
    default:
        LOG_ERROR_N << "Unknown model kind requested for loading.";
        return false;
    }
}

bool ModelInstance::loadWhisper()
{

    auto& wengine = ModelMgr::instance().whisperEngine();
    const filesystem::path path = full_path_.toStdString();
    qvw::WhisperEngineLoadParams params;
    ScopedTimer timer;
    LOG_DEBUG_N << "Loading Whisper model \"" << modelId() << "\" from path: " << full_path_;
    model_ctx_ = wengine.loadWhisper(modelId().toStdString(), path, params);
    if (!model_ctx_) {
        LOG_ERROR_N << "Failed to load Whisper model from path: " << full_path_;
        return false;
    }
    LOG_INFO_N << "Whisper model \"" << modelId() << "\" loaded in "
                << timer.elapsed() << " seconds from path: " << full_path_;
    emit modelReady();
    return true;
}

bool ModelInstance::loadLlama()
{
    auto& llama_engine = ModelMgr::instance().llamaEngine();
    const filesystem::path path = full_path_.toStdString();
    qvw::LlamaEngineLoadParams params;
    ScopedTimer timer;
    LOG_DEBUG_N << "Loading Llama model \"" << modelId() << "\" from path: " << full_path_;
    model_ctx_ = llama_engine.loadLlama(modelId().toStdString(), path, params);
    if (!model_ctx_) {
        LOG_ERROR_N << "Failed to load Llama model from path: " << full_path_;
        return false;
    }
    LOG_INFO_N << "Llama model \"" << modelId() << "\" loaded in "
                << timer.elapsed() << " seconds from path: " << full_path_;
    emit modelReady();
    return true;
}

ReplyEventProxy::ReplyEventProxy(QNetworkReply *reply, QObject *parent)
    : QObject(parent)
{
    // readyRead
    connect(reply, &QNetworkReply::readyRead,
            this, [this] {
                //LOG_TRACE_N << "ReplyEventProxy: readyRead signaled.";
                emit event(Event::ReadyRead);
            });

    // finished
    connect(reply, &QNetworkReply::finished,
            this, [this] {
                LOG_TRACE_N << "ReplyEventProxy: finished signaled.";
                emit event(Event::Finished);
            });

    // errorOccurred (Qt 5/6 name is errorOccurred; if you're on Qt5 < 5.15, adjust)
    connect(reply, &QNetworkReply::errorOccurred,
            this, [this](QNetworkReply::NetworkError) {
                LOG_TRACE_N << "ReplyEventProxy: errorOccurred signaled.";
                emit event(Event::Error);
            });

    connect(reply, &QNetworkReply::destroyed,
            this, [this] {
                LOG_TRACE_N << "ReplyEventProxy: reply destroyed, cleaning up.";
                this->deleteLater();
            });

    connect(reply, &QNetworkReply::aboutToClose,
            this, [this] {
                LOG_TRACE_N << "ReplyEventProxy: reply about to close, cleaning up.";
                this->deleteLater();
            });
}
