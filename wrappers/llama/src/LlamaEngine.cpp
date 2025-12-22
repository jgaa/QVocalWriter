#define LOGFAULT_FWD_ENABLE_LOGGING 1

#include <atomic>
#include <cassert>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "qvw/LlamaEngine.h"
#include "qvw/log_wrapper.h"

#include <llama.h>

using namespace std;

namespace qvw {

namespace {

// -------------------------
// Logging bridge
// -------------------------
static void llamaLogger(ggml_log_level level, const char * text, void *) {
    string_view msg(text ? text : "");
    if (!msg.empty() && msg.back() == '\n') {
        msg = msg.substr(0, msg.size() - 1);
    }

    switch (level) {
    case GGML_LOG_LEVEL_ERROR: LOG_ERROR << "[llama] " << msg; break;
    case GGML_LOG_LEVEL_WARN:  LOG_WARN  << "[llama] " << msg; break;
    case GGML_LOG_LEVEL_INFO:  LOG_INFO  << "[llama] " << msg; break;
    case GGML_LOG_LEVEL_DEBUG: LOG_DEBUG << "[llama] " << msg; break;
    case GGML_LOG_LEVEL_CONT:  LOG_TRACE << "[llama] " << msg; break;
    case GGML_LOG_LEVEL_NONE:  break;
    }
}

static std::string tokenToPiece(const llama_vocab * vocab, llama_token tok) {
    // small stack buffer for common cases
    array<char, 64> tmp{};

    // lstrip = 0 (don’t strip), special = false (don’t render special tokens)
    int32_t n = llama_token_to_piece(vocab, tok, tmp.data(), tmp.size(), 0, false);

    if (n >= 0) {
        return std::string(tmp.data(), tmp.data() + n);
    }

    // need a bigger buffer; n is negative, -n is required length
    const int32_t need = -n;
    std::string out;
    out.resize((size_t)need);

    n = llama_token_to_piece(vocab, tok, out.data(), need, 0, false);
    if (n < 0) {
        // if this happens something is inconsistent; return empty to be safe
        return {};
    }

    out.resize((size_t)n);
    return out;
}


class LlamaImpl;
class LlamaCtxImpl;

// -------------------------
// Session implementation
// (owns its own llama_context)
// -------------------------
class LlamaSessionCtxImpl final : public LlamaSessionCtx {
public:
    explicit LlamaSessionCtxImpl(shared_ptr<LlamaCtxImpl> modelCtx);

    ~LlamaSessionCtxImpl() override {
        if (ctx_) {
            llama_free(ctx_);
            ctx_ = nullptr;
        }
    }

    void setOnPartialTextCallback(function<void(const string&)> cb) override {
        on_partial_text_callback_ = std::move(cb);
    }

    string getFullTextResult() const override {
        return final_text_;
    }

    bool promptImpl(string_view text, const Params & params) override;

    void setOnPartialTextCallbackImpl(std::function<void (const std::string &)> on_partial_text_callback) override {
        on_partial_text_callback_ = std::move(on_partial_text_callback);
    }

    std::string getFullTextResultImpl() const override {
        return final_text_;
    }

    bool resetContext();


private:
    bool appendAndCallback(string_view piece);
    bool evalTokens(span<const llama_token> toks);

    shared_ptr<LlamaCtxImpl> model_ctx_;

    llama_context * ctx_{nullptr};
    llama_model * model_{nullptr};
    const llama_vocab * vocab_{nullptr};

    int32_t n_past_{0};
    int32_t last_logits_idx_ = 0;

    string final_text_;
    function<void(const string&)> on_partial_text_callback_;
};

// -------------------------
// Model context implementation
// (owns llama_model; sessions create contexts)
// -------------------------
class LlamaCtxImpl final : public LlamaCtx, public enable_shared_from_this<LlamaCtxImpl> {
public:
    LlamaCtxImpl(LlamaImpl & engine,
                 string modelId,
                 llama_model * model,
                 int threads,
                 int ctx_size)
        : engine_(engine)
        , model_id_(std::move(modelId))
        , model_(model)
        , threads_(threads)
        , ctx_size_(ctx_size) {
        assert(model_);
    }

    ~LlamaCtxImpl() override;

    // ModelCtx
    string info() const noexcept override;
    EngineBase & engine() noexcept override;
    const EngineBase & engine() const noexcept override;
    const string & modelId() const noexcept override { return model_id_; }

    // LlamaCtx
    shared_ptr<LlamaSessionCtx> createLlamaSession() override {
        return make_shared<LlamaSessionCtxImpl>(shared_from_this());
    }

    // internal accessors
    llama_model * model() noexcept { return model_; }
    const llama_model * model() const noexcept { return model_; }
    int threads() const noexcept { return threads_; }
    int ctxSize() const noexcept { return ctx_size_; }

private:
    LlamaImpl & engine_;
    string model_id_;

    llama_model * model_{nullptr};

    int threads_{EngineBase::getThreads()};
    int ctx_size_{4096};
};

// -------------------------
// Engine implementation
// -------------------------
class LlamaImpl final : public LlamaEngine {
public:
    explicit LlamaImpl(const LlamaCreateParams &) {
        llama_log_set(llamaLogger, nullptr);
    }

    ~LlamaImpl() override {
        // If you call llama_backend_init(), you should call llama_backend_free().
        // Safe to call even if init failed? In practice, call only if we set backend_inited_.
        if (backend_inited_) {
            llama_backend_free();
        }
    }

    string version() const override {
        if (const auto * p = llama_print_system_info()) {
            return p;
        }

        return "llama.cpp (version unknown)";
    }

    bool init() override {
        clearError();

        // One-time backend init.
        // NUMA init is optional; leave it at defaults unless you have a reason to enable.
        llama_backend_init();
        backend_inited_ = true;

        LOG_INFO << "Llama backend initialized";
        return true;
    }

    string lastError() const noexcept override {
        return error_;
    }

    shared_ptr<ModelCtx> load(const string & modelId,
                              const filesystem::path & modelPath,
                              const EngineLoadParams & params) override
    {
        LlamaEngineLoadParams lp;
        if (auto * p = dynamic_cast<const LlamaEngineLoadParams *>(&params)) {
            lp = *p;
        }
        return loadLlama(modelId, modelPath, lp);
    }

    int numLoadedModels() const noexcept override {
        return num_loaded_models_.load();
    }

    shared_ptr<LlamaCtx> loadLlama(const string & modelId,
                                   const filesystem::path & modelPath,
                                   const LlamaEngineLoadParams & params) override
    {
        clearError();

        LOG_DEBUG << "Loading Llama model " << modelId << " from " << modelPath;

        auto mparams = llama_model_default_params();
        // Keep wrapper CPU-only by default; if you later enable GPU variants, map params here.
        // mparams.n_gpu_layers = params.n_gpu_layers;

        llama_model * model = llama_model_load_from_file(modelPath.c_str(), mparams);
        if (!model) {
            setError(format("Failed to load llama model from {}", modelPath.string()));
            LOG_ERROR << lastError();
            return {};
        }

        const int threads = EngineBase::getThreads(params.threads);
        const int ctx_size = (params.ctx_size > 0) ? params.ctx_size : 4096;

        auto ctx = make_shared<LlamaCtxImpl>(*this, modelId, model, threads, ctx_size);
        ++num_loaded_models_;
        return ctx;
    }

    void onModelUnloaded() noexcept {
        --num_loaded_models_;
    }

    void setLogger(logfault_fwd::logfault_callback_t cb) override {
        logfault_fwd::setCallback(std::move(cb), "LlamaEngine");
    }

private:
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
    bool backend_inited_{false};
};

// -------------------------
// LlamaCtxImpl dtor + info
// -------------------------
LlamaCtxImpl::~LlamaCtxImpl() {
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
    engine_.onModelUnloaded();
}

string LlamaCtxImpl::info() const noexcept {
    return format("{}, model={}", engine_.version(), modelId());
}

EngineBase & LlamaCtxImpl::engine() noexcept { return engine_; }
const EngineBase & LlamaCtxImpl::engine() const noexcept { return engine_; }

// -------------------------
// LlamaSessionCtxImpl
// -------------------------
LlamaSessionCtxImpl::LlamaSessionCtxImpl(shared_ptr<LlamaCtxImpl> modelCtx)
    : model_ctx_(std::move(modelCtx))
    , model_(model_ctx_->model()) {

    assert(model_);

    vocab_ = llama_model_get_vocab(model_);
    assert(vocab_);

    if (!resetContext()) {
        throw runtime_error("Failed to create llama_context");
    }
}

bool LlamaSessionCtxImpl::appendAndCallback(string_view piece) {
    final_text_.append(piece);
    if (on_partial_text_callback_) {
        on_partial_text_callback_(final_text_);
    }
    return true;
}

bool LlamaSessionCtxImpl::evalTokens(span<const llama_token> toks) {
    if (toks.empty()) return true;

    llama_batch batch = llama_batch_init((int32_t)toks.size(), /*embd*/ 0, /*n_seq_max*/ 1);
    batch.n_tokens = (int32_t)toks.size();

    for (int32_t i = 0; i < batch.n_tokens; ++i) {
        batch.token[i]     = toks[(size_t)i];
        batch.pos[i]       = n_past_ + i;

        batch.n_seq_id[i]  = 1;
        batch.seq_id[i][0] = 0;

        batch.logits[i]    = (i == batch.n_tokens - 1) ? 1 : 0;
    }

    // We requested logits for the last token in this batch:
    last_logits_idx_ = batch.n_tokens - 1;

    const int rc = llama_decode(ctx_, batch);
    llama_batch_free(batch);

    if (rc != 0) {
        LOG_ERROR << "llama_decode failed rc=" << rc;
        return false;
    }

    n_past_ += (int32_t)toks.size();
    return true;
}


bool LlamaSessionCtxImpl::promptImpl(string_view text, const Params & params) {
    final_text_.clear();

    if (!params.continue_conversation) {
        // Fresh start: we must reset BOTH n_past_ and KV cache.
        // Your llama.h doesn't expose kv_cache_clear, so recreate the context.
        if (!resetContext()) {
            LOG_ERROR_N << "Failed to reset context for new conversation";
            return false;
        }
        LOG_DEBUG_N << "Starting new conversation.";
    } else {
        // Continue: keep ctx_ + KV cache and DO NOT reset n_past_.
        // The caller must provide only the "delta" (new user message + assistant header),
        // not the whole conversation.
        LOG_DEBUG_N << "Continuing conversation, n_past=" << n_past_;
    }

    LOG_DEBUG << "Prompting Llama model with text: " << text;

    // ---- tokenize prompt (vocab-based API) ----
    vector<llama_token> prompt_tokens(text.size() + 8);

    LOG_DEBUG << "Tokenizing prompt text of size " << text.size();
    int32_t n_prompt = llama_tokenize(
        vocab_,
        text.data(),
        (int32_t)text.size(),
        prompt_tokens.data(),
        (int32_t)prompt_tokens.size(),
        /*add_special*/ false,
        /*parse_special*/ true
        );

    if (n_prompt < 0) {
        LOG_ERROR << "llama_tokenize failed";
        return false;
    }
    prompt_tokens.resize((size_t)n_prompt);

    LOG_DEBUG << "Tokenized prompt into " << n_prompt << " tokens";
    if (!evalTokens(prompt_tokens)) {
        return false;
    }

    LOG_DEBUG << "Evaluated prompt tokens, n_past=" << n_past_;
    // ---- sampler chain (current API) ----
    auto sparams = llama_sampler_chain_default_params();
    llama_sampler * smpl = llama_sampler_chain_init(sparams);

    // Put penalties early (so they affect candidates before truncation)
    llama_sampler_chain_add(smpl, llama_sampler_init_penalties(
                                      /*penalty_last_n*/ -1,
                                      /*penalty_repeat*/ params.repeat_penalty,
                                      /*penalty_freq*/   0.0f,
                                      /*penalty_present*/0.0f
                                      ));

    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(params.top_k));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(params.top_p, /*min_keep*/ 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(params.temperature));

    // *** REQUIRED: terminal sampler that chooses the token ***
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(/*seed*/ 1234));
    // (or for debugging: llama_sampler_init_greedy())

    llama_sampler_reset(smpl);

    const llama_vocab * vocab = llama_model_get_vocab(model_);

    // generation loop
    LOG_DEBUG << "Starting generation loop for up to " << params.max_tokens << " tokens";
    for (int i = 0; i < params.max_tokens; ++i) {
        // b7444 supports idx = -1 meaning "last logits in batch"
        llama_token id = llama_sampler_sample(smpl, ctx_, -1);

        // IMPORTANT for penalties/repetition tracking:
        llama_sampler_accept(smpl, id);

        if (llama_vocab_is_eog(vocab, id)) {
            break;
        }

        const auto piece = tokenToPiece(vocab, id);
        LOG_TRACE << "Sampled token id=" << id << " piece=\"" << piece << "\"";
        final_text_ += piece;
        if (on_partial_text_callback_) {
            on_partial_text_callback_(piece);   // emit the *piece*, not the whole final_text_
        }

        const llama_token toks[1] = { id };
        if (!evalTokens(std::span{toks, 1})) break;
    }

    LOG_DEBUG << "Generation loop complete, total n_past=" << n_past_;
    llama_sampler_free(smpl);

    return true;
}

bool LlamaSessionCtxImpl::resetContext() {
    LOG_DEBUG_N << "Resetting llama_context for new session";
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }

    auto cparams = llama_context_default_params();
    cparams.n_ctx = model_ctx_->ctxSize();

    if (model_ctx_->threads() > 0) {
        cparams.n_threads = model_ctx_->threads();
        cparams.n_threads_batch = model_ctx_->threads();
    }

    ctx_ = llama_init_from_model(model_, cparams);
    if (!ctx_) {
        LOG_ERROR << "Failed to recreate llama_context";
        return false;
    }

    n_past_ = 0;
    last_logits_idx_ = 0;
    return true;
}

} // namespace

// -------------------------
// Public factories + base ctors
// -------------------------
shared_ptr<LlamaEngine> LlamaEngine::create(const LlamaCreateParams & params) {
    return make_shared<LlamaImpl>(params);
}

LlamaEngine::LlamaEngine() = default;
LlamaEngine::~LlamaEngine() = default;

LlamaCtx::LlamaCtx() = default;
LlamaCtx::~LlamaCtx() = default;

LlamaSessionCtx::LlamaSessionCtx() = default;
LlamaSessionCtx::~LlamaSessionCtx() = default;

void LlamaSessionCtx::setOnPartialTextCallback(std::function<void(const std::string&)> callback) {
    dynamic_cast<LlamaSessionCtxImpl&>(*this).setOnPartialTextCallbackImpl(std::move(callback));
}

std::string LlamaSessionCtx::getFullTextResult() const {
    return dynamic_cast<const LlamaSessionCtxImpl&>(*this).getFullTextResultImpl();
}

// Llama API
bool LlamaSessionCtx::prompt(std::string_view text, const Params& params) {
    return dynamic_cast<LlamaSessionCtxImpl&>(*this).promptImpl(text, params);
}

} // namespace qvw
