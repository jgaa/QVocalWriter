
#include <atomic>
#include <format>
#include <map>
#include <memory>
#include <thread>

#include "qvw/WhisperEngine.h"
#include "qvw/log_wrapper.h"

#include <whisper.h>

using namespace std;

namespace qvw {

namespace {

class WhisperImpl;
class WhisperCtxImpl;

void whisperLogger(ggml_log_level level, const char *msg, void *) {
    string_view message(msg);
    message = message.substr(0, message.empty() ? 0 : message.size() -1);

    switch(level) {
    case GGML_LOG_LEVEL_ERROR:
        LOG_ERROR << "[whisper] " << message;
        break;
    case GGML_LOG_LEVEL_WARN:
        LOG_WARN << "[whisper] " << message;
        break;
    case GGML_LOG_LEVEL_INFO:
        LOG_INFO << "[whisper] " << message;
        break;
    case GGML_LOG_LEVEL_DEBUG:
        LOG_DEBUG << "[whisper] " << message;
        break;
    case GGML_LOG_LEVEL_CONT:
        LOG_TRACE << "[whisper] " << message;
        break;
    case GGML_LOG_LEVEL_NONE:
        // no logging
        break;
    }
}

class WhisperSessionCtxImpl final : public WhisperSessionCtx {
public:
    WhisperSessionCtxImpl(shared_ptr<WhisperCtxImpl> modelCtx, whisper_state *state);
    void setOnPartialTextCallback(std::function<void (const std::string &)> callback) override {
        on_partial_text_callback_ = std::move(callback);
    }

    string getFullTextResult() const override {
        return final_text_;
    }

    bool whisperFull(std::span<const float> data, const WhisperFullParams &params, Transcript& out) override;

private:
    shared_ptr<WhisperCtxImpl> model_ctx_;
    whisper_state *state_{nullptr};
    std::string final_text_;
    std::function<void (const std::string &)> on_partial_text_callback_;

    // SessionCtx interface
};


class WhisperCtxImpl final : public WhisperCtx, public enable_shared_from_this<WhisperCtxImpl> {
public:
    WhisperCtxImpl(WhisperImpl& engine, string_view modelId, whisper_context *ctx)
        : engine_{engine}, model_id_{modelId}, ctx_{ctx}
    {
        assert(ctx_ != nullptr);
    }

    ~WhisperCtxImpl() override;

    // ModelCtx interface
public:
    string info() const noexcept override;

    EngineBase &engine() noexcept override;

    const EngineBase &engine() const noexcept override;

    WhisperImpl& wengine() noexcept {
        return engine_;
    }

    const WhisperImpl& wengine() const noexcept {
        return engine_;
    }

    const string &modelId() const noexcept override {
        return model_id_;
    }

    std::shared_ptr<WhisperSessionCtx> createWhisperSession() override {
        assert(ctx_ != nullptr);

        LOG_DEBUG << "Creating new Whisper session for model " << model_id_;

        if (auto state = whisper_init_state(ctx_)) {
            auto s = make_shared<WhisperSessionCtxImpl>(shared_from_this(), state);
            return s;
        }

        return {};
    }

    whisper_context *ctx() noexcept override {
        return ctx_;
    };

    const whisper_context *ctx() const noexcept override {
        return ctx_;
    };

private:
    WhisperImpl& engine_;
    const std::string model_id_;
    whisper_context *ctx_{nullptr};
};

class WhisperImpl final : public WhisperEngine {
public:
    WhisperImpl(const WhisperCreateParams& params)
    {
        LOG_DEBUG << "Creating Whisper engine";

        whisper_log_set(whisperLogger, nullptr);
    }

    ~WhisperImpl() override {
        LOG_DEBUG << "Destroying Whisper engine with " << num_loaded_models_ << " loaded models";
    }

    int numLoadedModels() const noexcept override {
        return num_loaded_models_.load();
    }

    // EngineBase interface
    string version() const noexcept override {
        string_view v;
        if (const auto p = whisper_version()) {
            v = p;
        }

        return format("whisper.cpp version {}", v);
    }

    bool init() override {
        // No specific init for whisper.cpp

        LOG_INFO << "Whisper engine initialized";
        return clearError();
    }

    string lastError() const noexcept override {
        return error_;
    }

    shared_ptr<ModelCtx> load(const string &modelId, const filesystem::path &modelPath, const EngineLoadParams &params) override {
        WhisperEngineLoadParams wp;
        if (auto *wparams = dynamic_cast<const WhisperEngineLoadParams*>(&params)) {
            wp.use_gpu = wparams->use_gpu;
            wp.flash_attn = wparams->flash_attn;
            wp.gpu_device = wparams->gpu_device;
        }

        return loadWhisper(modelId, modelPath, wp);
    }

    std::shared_ptr<WhisperCtx> loadWhisper(const std::string &modelId,
                                            const std::filesystem::path &modelPath,
                                            const WhisperEngineLoadParams &params) override {
        whisper_context_params cparams = whisper_context_default_params();

        LOG_DEBUG << "Loading Whisper model " << modelId << " from " << modelPath;

        // Set default params
        // Optionally modify defaults:
        cparams.use_gpu = false;          // TODO: Make optional later
        cparams.flash_attn = false;       // TODO: Make optional later (require gpu)
        cparams.gpu_device = 0;           // ignored for CPU mode

        // DTW features are advanced; keep disabled for now
        cparams.dtw_token_timestamps = false;
        cparams.dtw_aheads_preset = WHISPER_AHEADS_NONE;
        cparams.dtw_n_top = 0;            // default means let whisper choose


        // Override params from arg

        cparams.use_gpu = params.use_gpu;
        cparams.flash_attn = params.flash_attn;
        cparams.gpu_device = params.gpu_device;

        if (auto *ctx = whisper_init_from_file_with_params_no_state(modelPath.c_str(), cparams)) {
            auto modelCtx = make_shared<WhisperCtxImpl>(*this, modelId, ctx);
            num_loaded_models_++;

            return modelCtx; // When the shared pointer goes out of scope, the model context is unloaded
        }

        LOG_ERROR << "Failed to load Whisper model from " << modelPath;
        setError(format("Failed to load Whisper model from {}", modelPath.string()));
        return {};
    }

    void onModelUnloaded() {
        --num_loaded_models_;
    }

private:

    // Returns true if msg is empty
    bool setError(string msg) {
        error_ = std::move(msg);
        return error_.empty();
    }

    bool clearError() {
        error_.clear();
        return true;
    }

    string error_;
    atomic_int num_loaded_models_{0};
};

WhisperCtxImpl::~WhisperCtxImpl() {
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
        engine_.onModelUnloaded();
    }
}

string WhisperCtxImpl::info() const noexcept
{
    return format("{}, model={}", wengine().version(), modelId());
}

EngineBase &WhisperCtxImpl::engine() noexcept {
    return engine_;
}

const EngineBase &WhisperCtxImpl::engine() const noexcept
{
    return engine_;
}

WhisperSessionCtxImpl::WhisperSessionCtxImpl(shared_ptr<WhisperCtxImpl> modelCtx, whisper_state *state)
    : model_ctx_{std::move(modelCtx)}, state_{state}
{
    LOG_DEBUG << "Created Whisper session context";
    assert(model_ctx_ != nullptr);
    assert(state_ != nullptr);
}

bool WhisperSessionCtxImpl::whisperFull(std::span<const float> data, const WhisperFullParams &params, Transcript& out) {
    auto p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    LOG_TRACE << "Starting full whisper processing with "
              << (params.language.empty() ? "auto-detect language" : format("language='{}'", params.language))
              << ", threads=" << params.threads;

    if (params.max_len.has_value()) {
        p.max_len = params.max_len.value();
    }
    if (params.token_timestamps.has_value()) {
        p.token_timestamps = params.token_timestamps.value();
    }
    if (params.no_context.has_value()) {
        p.no_context = params.no_context.value();
    }
    if (params.single_segment.has_value()) {
        p.single_segment = params.single_segment.value();
    }
    if (params.print_progress.has_value()) {
        p.print_progress = params.print_progress.value();
    }
    if (params.print_timestamps.has_value()) {
        p.print_timestamps = params.print_timestamps.value();
    }
    if (params.print_realtime.has_value()) {
        p.print_realtime = params.print_realtime.value();
    }
    // Set number of threads

    if (params.threads > 0) {
        p.n_threads = params.threads;
    } else {
        if (const auto thds = std::thread::hardware_concurrency(); thds > 4) {
            if (thds > 32) {
                p.n_threads = static_cast<int>(thds -4);
            } else if (thds > 4) {
                p.n_threads = static_cast<int>(thds -1);
            } else {
                p.n_threads = static_cast<int>(thds);
            }
        } else {
            p.n_threads = 4;
        }
    }

    if (!params.language.empty()) {
        p.language = params.language.c_str();
    }

    LOG_TRACE << "Whisper full params: "
                << "language='" << (p.language ? p.language : "auto") << "', "
                << "n_threads=" << p.n_threads << ", "
                << "max_len=" << p.max_len << ", "
                << "token_timestamps=" << p.token_timestamps << ", "
                << "no_context=" << p.no_context << ", "
                << "single_segment=" << p.single_segment << ", "
                << "print_progress=" << p.print_progress << ", "
                << "print_timestamps=" << p.print_timestamps << ", "
                << "print_realtime=" << p.print_realtime;

    auto rc = whisper_full_with_state(model_ctx_->ctx(), state_, p, data.data(), static_cast<int>(data.size()));
    if (rc != 0) {
        return false;
    }

    // Handle transcript output
    out.segments.clear();
    out.full_text.clear();

    const int n = whisper_full_n_segments_from_state(state_);
    out.segments.reserve(std::max(0, n));

    for (int i = 0; i < n; ++i) {
        Segment seg{};
        seg.t0_ms = whisper_full_get_segment_t0_from_state(state_, i) * 10; // whisper uses 10ms units
        seg.t1_ms = whisper_full_get_segment_t1_from_state(state_, i) * 10;

        const char* txt = whisper_full_get_segment_text_from_state(state_, i);
        if (txt) {
            seg.text.assign(txt);
            out.full_text += seg.text;
        }

        // seg.avg_logprob = whisper_full_get_segment_avg_logprob_from_state(state_, i);
        seg.no_speech_prob = whisper_full_get_segment_no_speech_prob_from_state(state_, i);
        out.segments.push_back(std::move(seg));
    }

    // Optional: expose the language you used / detected
    // If you forced language, you already know it:
    if (!params.language.empty()) {
        out.language = params.language;
    } else {
        // Some builds expose language id; if available, map it to a string here.
        // Otherwise leave empty or set "auto".
        out.language.clear();
    }

    return true;
}

} // anon ns

std::shared_ptr<WhisperEngine> WhisperEngine::create(const WhisperCreateParams &params)
{
    LOG_DEBUG << "Creating Whisper engine instance";
    return make_shared<WhisperImpl>(params);
}

WhisperCtx::WhisperCtx() {}

WhisperCtx::~WhisperCtx() {}

WhisperSessionCtx::WhisperSessionCtx() {}
WhisperSessionCtx::~WhisperSessionCtx() {}


WhisperEngine::WhisperEngine() {}

WhisperEngine::~WhisperEngine() {}

} // ns

