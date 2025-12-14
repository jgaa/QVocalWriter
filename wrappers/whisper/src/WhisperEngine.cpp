
#include <atomic>
#include <format>
#include <map>
#include <memory>
#include <thread>

#include "qvw/WhisperEngine.h"

#include <whisper.h>

using namespace std;

namespace qvw {

namespace {

class WhisperImpl;
class WhisperCtxImpl;

class WhisperSessionCtxImpl final : public WhisperSessionCtx {
public:
    WhisperSessionCtxImpl(shared_ptr<WhisperCtxImpl> modelCtx, whisper_state *state);
    void setOnPartialTextCallback(std::function<void (const std::string &)> callback) override {
        on_partial_text_callback_ = std::move(callback);
    }

    string getFullTextResult() const override {
        return final_text_;
    }

    bool whisperFull(std::span<const float> data, const WhisperFullParams &params) override;

private:
    shared_ptr<WhisperCtxImpl> model_ctx_;
    whisper_state *state_{nullptr};
    std::string final_text_;
    std::function<void (const std::string &)> on_partial_text_callback_;
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

    shared_ptr<SessionCtx> newSession() override {
        assert(ctx_ != nullptr);

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
    WhisperImpl() {

    }

    ~WhisperImpl() override {

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
        return clearError();
    }

    string lastError() const noexcept override {
        return error_;
    }

    shared_ptr<ModelCtx> load(const string &modelId, const filesystem::path &modelPath, const EngineLoadParams &params) override {
        whisper_context_params cparams = whisper_context_default_params();

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
        if (auto *wp = dynamic_cast<const WhisperEngineLoadParams*>(&params)) {
            cparams.use_gpu = wp->use_gpu;
            cparams.flash_attn = wp->flash_attn;
            cparams.gpu_device = wp->gpu_device;
        }

        if (auto *ctx = whisper_init_from_file_with_params_no_state(modelPath.c_str(), cparams)) {
            auto modelCtx = make_shared<WhisperCtxImpl>(*this, modelId, ctx);
            num_loaded_models_++;

            return modelCtx; // When the shared pointer goes out of scope, the model context is unloaded
        }


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
    assert(model_ctx_ != nullptr);
    assert(state_ != nullptr);
}

bool WhisperSessionCtxImpl::whisperFull(std::span<const float> data, const WhisperFullParams &params) {
    auto p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    p.print_progress   = false;
    p.print_realtime   = false;
    p.print_timestamps = true;
    p.offset_ms = 0;  // or leave default

    if (const auto thds = std::thread::hardware_concurrency(); thds > 4) {
        if (thds > 32) {
            p.n_threads = static_cast<int>(thds -4);
        } else {
            p.n_threads = static_cast<int>(thds -1);
        }
    }

    if (params.threads > 0) {
        p.n_threads = params.threads;
    }

    if (!params.language.empty()) {
        p.language = params.language.c_str();
    }

    auto rc = whisper_full_with_state(model_ctx_->ctx(), state_, p, data.data(), static_cast<int>(data.size()));
    if (rc != 0) {
        return false;
    }

    return true;
}

} // anon ns



} // ns

