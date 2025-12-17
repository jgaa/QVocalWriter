#include <filesystem>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QNetworkReply>
#include <QFile>
#include <QtConcurrent/QtConcurrentRun>
#include <QFuture>

#include <qcorofuture.h>

#include <qcoro/network/qcoronetwork.h>
#include <qcoro/core/qcorosignal.h>
#include <qcoronetworkreply.h>
#include <qcoroiodevice.h>

#include "qvw/WhisperEngine.h"
#include "ModelMgr.h"
#include "ScopedTimer.h"
#include "logging.h"

using namespace std;

namespace {

using mi_t = ModelInfo;
constexpr auto all_whisper_models = std::to_array<mi_t>({
    // tiny
    { "tiny",               "ggml-tiny.bin",               mi_t::FP16,  75,  "bd577a113a864445d4c299885e0cb97d4ba92b5f" },
    { "tiny-q5_1",          "ggml-tiny-q5_1.bin",          mi_t::Q5_1,  31,  "2827a03e495b1ed3048ef28a6a4620537db4ee51" },
    { "tiny-q8_0",          "ggml-tiny-q8_0.bin",          mi_t::Q8_0,  42,  "19e8118f6652a650569f5a949d962154e01571d9" },

    // tiny.en
    { "tiny.en",            "ggml-tiny.en.bin",            mi_t::FP16,  75,  "c78c86eb1a8faa21b369bcd33207cc90d64ae9df" },
    { "tiny.en-q5_1",       "ggml-tiny.en-q5_1.bin",       mi_t::Q5_1,  31,  "3fb92ec865cbbc769f08137f22470d6b66e071b6" },
    { "tiny.en-q8_0",       "ggml-tiny.en-q8_0.bin",       mi_t::Q8_0,  42,  "802d6668e7d411123e672abe4cb6c18f12306abb" },

    // base
    { "base",               "ggml-base.bin",               mi_t::FP16, 142,  "465707469ff3a37a2b9b8d8f89f2f99de7299dac" },
    { "base-q5_1",          "ggml-base-q5_1.bin",          mi_t::Q5_1,  57,  "a3733eda680ef76256db5fc5dd9de8629e62c5e7" },
    { "base-q8_0",          "ggml-base-q8_0.bin",          mi_t::Q8_0,  78,  "7bb89bb49ed6955013b166f1b6a6c04584a20fbe" },

    // base.en
    { "base.en",            "ggml-base.en.bin",            mi_t::FP16, 142,  "137c40403d78fd54d454da0f9bd998f78703390c" },
    { "base.en-q5_1",       "ggml-base.en-q5_1.bin",       mi_t::Q5_1,  57,  "d26d7ce5a1b6e57bea5d0431b9c20ae49423c94a" },
    { "base.en-q8_0",       "ggml-base.en-q8_0.bin",       mi_t::Q8_0,  78,  "bb1574182e9b924452bf0cd1510ac034d323e948" },

    // small
    { "small",              "ggml-small.bin",              mi_t::FP16, 466,  "55356645c2b361a969dfd0ef2c5a50d530afd8d5" },
    { "small-q5_1",         "ggml-small-q5_1.bin",         mi_t::Q5_1, 181,  "6fe57ddcfdd1c6b07cdcc73aaf620810ce5fc771" },
    { "small-q8_0",         "ggml-small-q8_0.bin",         mi_t::Q8_0, 252,  "bcad8a2083f4e53d648d586b7dbc0cd673d8afad" },

    // small.en
    { "small.en",           "ggml-small.en.bin",           mi_t::FP16, 466,  "db8a495a91d927739e50b3fc1cc4c6b8f6c2d022" },
    { "small.en-q5_1",      "ggml-small.en-q5_1.bin",      mi_t::Q5_1, 181,  "20f54878d608f94e4a8ee3ae56016571d47cba34" },
    { "small.en-q8_0",      "ggml-small.en-q8_0.bin",      mi_t::Q8_0, 252,  "9d75ff4ccfa0a8217870d7405cf8cef0a5579852" },

    // small.en-tdrz
    { "small.en-tdrz",      "ggml-small.en-tdrz.bin",      mi_t::FP16, 465,  "b6c6e7e89af1a35c08e6de56b66ca6a02a2fdfa1" },

    // medium
    { "medium",             "ggml-medium.bin",             mi_t::FP16, 1500, "fd9727b6e1217c2f614f9b698455c4ffd82463b4" },
    { "medium-q5_0",        "ggml-medium-q5_0.bin",        mi_t::Q5_0, 514,  "7718d4c1ec62ca96998f058114db98236937490e" },
    { "medium-q8_0",        "ggml-medium-q8_0.bin",        mi_t::Q8_0, 785,  "e66645948aff4bebbec71b3485c576f3d63af5d6" },

    // medium.en
    { "medium.en",          "ggml-medium.en.bin",          mi_t::FP16, 1500, "8c30f0e44ce9560643ebd10bbe50cd20eafd3723" },
    { "medium.en-q5_0",     "ggml-medium.en-q5_0.bin",     mi_t::Q5_0, 514,  "bb3b5281bddd61605d6fc76bc5b92d8f20284c3b" },
    { "medium.en-q8_0",     "ggml-medium.en-q8_0.bin",     mi_t::Q8_0, 785,  "b1cf48c12c807e14881f634fb7b6c6ca867f6b38" },

    // large-v1
    { "large-v1",           "ggml-large-v1.bin",           mi_t::FP16, 2900, "b1caaf735c4cc1429223d5a74f0f4d0b9b59a299" },

    // large-v2
    { "large-v2",           "ggml-large-v2.bin",           mi_t::FP16, 2900, "0f4c8e34f21cf1a914c59d8b3ce882345ad349d6" },
    { "large-v2-q5_0",      "ggml-large-v2-q5_0.bin",      mi_t::Q5_0, 1100, "00e39f2196344e901b3a2bd5814807a769bd1630" },
    { "large-v2-q8_0",      "ggml-large-v2-q8_0.bin",      mi_t::Q8_0, 1500, "da97d6ca8f8ffbeeb5fd147f79010eeea194ba38" },

    // large-v3
    { "large-v3",           "ggml-large-v3.bin",           mi_t::FP16, 2900, "ad82bf6a9043ceed055076d0fd39f5f186ff8062" },
    { "large-v3-q5_0",      "ggml-large-v3-q5_0.bin",      mi_t::Q5_0, 1100, "e6e2ed78495d403bef4b7cff42ef4aaadcfea8de" },

    // large-v3-turbo
    { "large-v3-turbo",         "ggml-large-v3-turbo.bin",         mi_t::FP16, 1500, "4af2b29d7ec73d781377bfd1758ca957a807e941" },
    { "large-v3-turbo-q5_0",    "ggml-large-v3-turbo-q5_0.bin",    mi_t::Q5_0, 547,  "e050f7970618a659205450ad97eb95a18d69c9ee" },
    { "large-v3-turbo-q8_0",    "ggml-large-v3-turbo-q8_0.bin",    mi_t::Q8_0, 834,  "01bf15bedffe9f39d65c1b6ff9b687ea91f59e0e" }
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
    default:
        LOG_ERROR_N << "Unknown model kind requested.";
    }

    return {};
}

models_t ModelMgr::loadedModels(ModelKind kind) const noexcept
{
    models_t models;
    models.reserve(instances_.size());

    for (const auto& [modelId, inst] : instances(kind)) {
        if (inst->isLoaded()) {
            models.push_back(inst->modelInfo());
        }
    }

    return models;
}

std::optional<ModelInfo> ModelMgr::findBestModel(ModelKind kind, const QString &modelName) const noexcept
{
    const auto model_id = modelName.toStdString();
    optional<ModelInfo> rval;
    const auto models = availableModels(kind);
    for (const auto &m : models) {
        // Strip off quantization suffix for matching
        auto base_name = m.id;
        if (auto pos = base_name.find('-'); pos != std::string_view::npos) {
            base_name = base_name.substr(0, pos);
        }
        if (base_name == model_id) {
            if (rval) {
                if (m.quantization == ModelInfo::Quatization::Q5_1) {
                    rval = m;
                } else if (rval->quantization != ModelInfo::Quatization::Q5_1
                           && m.quantization == ModelInfo::Quatization::Q5_0) {
                    rval = m;
                } else if ((rval->quantization != ModelInfo::Quatization::Q5_1
                            && rval->quantization != ModelInfo::Quatization::Q5_0)
                           && rval->quantization < m.quantization) {
                    // prefer higher-quality quantization, unless we have Q5_1/Q5_0
                    rval = m;
                }
            } else {
                rval = m;
            }
        }
    }
    return rval;
}

qvw::WhisperEngine &ModelMgr::whisperEngine() {

    if (!whisper_engine_) {
        whisper_engine_ = qvw::WhisperEngine::create({});
        if (!whisper_engine_) {
            LOG_ERROR_N << "Failed to create Whisper engine instance.";
            throw std::runtime_error{"Failed to create Whisper engine instance."};
        }
    }

    assert(whisper_engine_);
    return *whisper_engine_;
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
    filesystem::path model_dir =  QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation).toStdString();
    model_dir /= dirPrefix(kind);

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
    const QString name = QString::fromUtf8(modelInfo.id);
    const QString base_url = QString::fromUtf8(modelDownloadUrlBase(kind));
    const QString url_str = base_url + QString::fromUtf8(modelInfo.filename);
    const QUrl url{url_str};

    LOG_INFO_N << "Starting download of model: kind=" << kind
                 << ", id='" << modelInfo.id << "'"
                 << ", url='" << url.toString() << "'"
                 << ", path='" << fullPath << "'";

    const bool success = co_await downloadFile(name, url, fullPath);
    if (!success) {
        LOG_ERROR_N << "Failed to download model file: " << url.toString();
        co_return false;
    }

    LOG_INFO_N << "Model file downloaded successfully: " << fullPath;
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

        emit downloadProgress(name, bytesReceived, bytesTotal);
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

    if (auto best_model = findBestModel(kind, modelId)) {
        const auto path = findModelPath(kind, *best_model);
        auto instance = std::make_shared<ModelInstance>(kind, *best_model, QString::fromUtf8(path.string()));

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
            ScopedTimer timer;
            const auto ok =  load(kind());
            LOG_DEBUG_N << "Model instance loaded in "
                         << timer.elapsed() << " seconds: "
                         << modelId()
                        << ". result=" << ok;
            return ok;
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
