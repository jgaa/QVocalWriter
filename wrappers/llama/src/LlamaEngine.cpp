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
    case GGML_LOG_LEVEL_ERROR: LOG_ERROR_N << "[llama] " << msg; break;
    case GGML_LOG_LEVEL_WARN:  LOG_WARN  << "[llama] " << msg; break;
    case GGML_LOG_LEVEL_INFO:  LOG_INFO  << "[llama] " << msg; break;
    case GGML_LOG_LEVEL_DEBUG: LOG_DEBUG_N << "[llama] " << msg; break;
    case GGML_LOG_LEVEL_CONT:  LOG_TRACE_N << "[llama] " << msg; break;
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

static std::string_view tailView(string_view s, size_t max_tail = 32) {
    const size_t n = s.size();
    const size_t start = (n > max_tail) ? (n - max_tail) : 0;
    return std::string_view{s}.substr(start);
}

bool shouldFlushNow(std::string_view last_piece, string_view buffer) {
    // If the new piece contains paragraph breaks, flush immediately
    if (last_piece.find("\n\n") != std::string_view::npos) return true;

    const auto tail = tailView(buffer, 32);

    // Paragraph at end
    if (tail.size() >= 2 && tail.ends_with("\n\n")) return true;

    // Code fence boundary at end
    if (tail.size() >= 3 && tail.ends_with("```")) return true;

    // Sentence endings (allow space or newline)
    if (tail.size() >= 2) {
        const char a = tail[tail.size() - 2];
        const char b = tail[tail.size() - 1];
        if ((a == '.' || a == '!' || a == '?') && (b == ' ' || b == '\n')) return true;
    }

    // Ellipsis (unicode) cases if you care:
    if (tail.ends_with("… ") ||  tail.ends_with("…\n")) {
        return true;
    }

    return false;
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

    int ctx_size_override_{0}; // 0 => use model_ctx_->ctxSize()
    shared_ptr<LlamaCtxImpl> model_ctx_;

    llama_context * ctx_{nullptr};
    llama_model * model_{nullptr};
    const llama_vocab * vocab_{nullptr};

    int32_t n_past_{0};
    int32_t last_logits_idx_ = 0;

    string final_text_;
    string partial_text_;
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

        LOG_DEBUG_N << "Loading Llama model " << modelId << " from " << modelPath;

        auto mparams = llama_model_default_params();
        // Keep wrapper CPU-only by default; if you later enable GPU variants, map params here.
        // mparams.n_gpu_layers = params.n_gpu_layers;

        llama_model * model = llama_model_load_from_file(modelPath.c_str(), mparams);
        if (!model) {
            setError(format("Failed to load llama model from {}", modelPath.string()));
            LOG_ERROR_N << lastError();
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

    void setLogger(logfault_fwd::logfault_callback_t cb, logfault_fwd::Level level) override {
        logfault_fwd::setCallback(std::move(cb), "LlamaEngine");
        logfault_fwd::setLevel(level);
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
        if (shouldFlushNow(piece, final_text_)) {
            LOG_TRACE_N << "Flushing partial text callback, final_text_=\"" << final_text_ << "\"";
            on_partial_text_callback_(final_text_);
        }
    }
    return true;
}

bool LlamaSessionCtxImpl::evalTokens(span<const llama_token> toks) {
    if (toks.empty()) return true;

    const int32_t n_batch = llama_n_batch(ctx_);
    if (n_batch <= 0) {
        LOG_ERROR_N << "llama_n_batch(ctx_) returned " << n_batch;
        return false;
    }

    int32_t offset = 0;
    while (offset < (int32_t)toks.size()) {
        const int32_t n_this = std::min<int32_t>(n_batch, (int32_t)toks.size() - offset);

        llama_batch batch = llama_batch_init(n_this, /*embd*/ 0, /*n_seq_max*/ 1);
        batch.n_tokens = n_this;

        for (int32_t i = 0; i < n_this; ++i) {
            batch.token[i]     = toks[(size_t)offset + (size_t)i];
            batch.pos[i]       = n_past_ + i;

            batch.n_seq_id[i]  = 1;
            batch.seq_id[i][0] = 0;

            batch.logits[i]    = (i == n_this - 1) ? 1 : 0;
        }

        last_logits_idx_ = n_this - 1;

        const int rc = llama_decode(ctx_, batch);
        llama_batch_free(batch);

        if (rc != 0) {
            LOG_ERROR_N << "llama_decode failed rc=" << rc;
            return false;
        }

        n_past_ += n_this;
        offset += n_this;
    }

    return true;
}

bool LlamaSessionCtxImpl::promptImpl(string_view text, const Params & params) {
    final_text_.clear();
    partial_text_.clear();

    // NOTE:
    // - Auto-resize + retry is safe only for "fresh start" prompts.
    // - If continue_conversation=true, we must not reset context / retry,
    //   otherwise we'd duplicate history and break state.
    const bool can_retry = !params.continue_conversation;

    if (!params.continue_conversation) {
        // Always start from a clean context for this prompt.
        if (!resetContext()) {
            LOG_ERROR_N << "Failed to reset context for new conversation";
            return false;
        }
        LOG_DEBUG_N << "Starting new conversation.";
    } else {
        LOG_DEBUG_N << "Continuing conversation, n_past=" << n_past_;
    }

    LOG_DEBUG_N << "Prompting Llama model with text bytes=" << text.size();

    // -------------------------
    // 1) Tokenize prompt once (vocab-based API)
    // -------------------------
    std::vector<llama_token> prompt_tokens(text.size() + 8);

    int32_t n_prompt = llama_tokenize(
        vocab_,
        text.data(),
        (int32_t)text.size(),
        prompt_tokens.data(),
        (int32_t)prompt_tokens.size(),
        /*add_special*/ false,
        /*parse_special*/ true
        );

    // In newer llama.cpp, a negative return often means "buffer too small" and -n is required
    if (n_prompt < 0) {
        const int32_t need = -n_prompt;
        prompt_tokens.resize((size_t)need);

        n_prompt = llama_tokenize(
            vocab_,
            text.data(),
            (int32_t)text.size(),
            prompt_tokens.data(),
            (int32_t)prompt_tokens.size(),
            /*add_special*/ false,
            /*parse_special*/ true
            );
    }

    if (n_prompt < 0) {
        LOG_ERROR_N << "llama_tokenize failed (n_prompt=" << n_prompt << ")";
        return false;
    }

    prompt_tokens.resize((size_t)n_prompt);
    LOG_DEBUG_N << "Tokenized prompt into " << n_prompt << " tokens";

    // -------------------------
    // 2) Retry loop: scale output + ctx if result looks partial
    // -------------------------
    auto roundUp = [](int v, int multiple) -> int {
        if (multiple <= 0) return v;
        return ((v + multiple - 1) / multiple) * multiple;
    };

    // Default output budget = params.max_tokens.
    // For cleanup/formatting, 256 is *far* too small; if user kept default, scaling will kick in.
    int target_out = params.max_tokens > 0 ? params.max_tokens : 256;

    // Safety margins
    const int ctx_margin        = 128;    // extra room beyond prompt+out
    const int room_margin       = 16;     // keep a little headroom in generation
    const int max_attempts      = can_retry ? 6 : 1;

    // Hard caps (tune as you like)
    const int hard_max_out      = 16384;  // maximum output tokens we'll try for auto-scaling
    const int hard_max_ctx      = 131072; // maximum context we'll request (prevents runaway)

    std::string last_stop_reason = "unknown";

    for (int attempt = 0; attempt < max_attempts; ++attempt) {

        // If we can retry, we always run from a clean context each attempt
        if (can_retry) {
            // Compute required n_ctx for (prompt + target_out + margin)
            int want_ctx = (int)prompt_tokens.size() + target_out + ctx_margin;
            want_ctx = std::min(want_ctx, hard_max_ctx);
            want_ctx = roundUp(want_ctx, 256);

            // Apply override and recreate context
            ctx_size_override_ = std::max(want_ctx, model_ctx_->ctxSize());
            if (!resetContext()) {
                LOG_ERROR_N << "Failed to recreate context for attempt=" << attempt
                          << " n_ctx=" << ctx_size_override_;
                return false;
            }
        }

        // Prefill prompt tokens
        if (!evalTokens(prompt_tokens)) {
            LOG_ERROR_N << "Failed to eval prompt tokens";
            return false;
        }

        // Determine how many tokens we can actually generate in this ctx
        const int n_ctx = llama_n_ctx(ctx_);
        const int room  = std::max(0, n_ctx - n_past_ - 1);

        // Use target_out, but never exceed room (minus margin)
        int effective_max_tokens = target_out;
        effective_max_tokens = std::min(effective_max_tokens, std::max(0, room - room_margin));

        LOG_DEBUG_N << "Starting generation attempt=" << attempt
                  << " for up to " << effective_max_tokens
                  << " tokens (target_out=" << target_out
                  << ", params.max_tokens=" << params.max_tokens
                  << ", n_ctx=" << n_ctx
                  << ", n_past=" << n_past_
                  << ", room=" << room << ")";

        // Fresh output buffers each attempt (important when retrying)
        final_text_.clear();
        partial_text_.clear();

        // Build sampler chain (fresh per attempt!)
        auto sparams = llama_sampler_chain_default_params();
        llama_sampler * smpl = llama_sampler_chain_init(sparams);

        llama_sampler_chain_add(smpl, llama_sampler_init_penalties(
                                          /*penalty_last_n*/ -1,
                                          /*penalty_repeat*/ params.repeat_penalty,
                                          /*penalty_freq*/   0.0f,
                                          /*penalty_present*/0.0f));

        llama_sampler_chain_add(smpl, llama_sampler_init_top_k(params.top_k));
        llama_sampler_chain_add(smpl, llama_sampler_init_top_p(params.top_p, /*min_keep*/ 1));
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(params.temperature));

        // Terminal sampler that chooses token
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(/*seed*/ 1234));
        // (or llama_sampler_init_greedy() for debugging)

        llama_sampler_reset(smpl);

        const llama_vocab * vocab = llama_model_get_vocab(model_);

        bool saw_eog = false;
        int generated = 0;
        std::string stop_reason = "unknown";

        // -------------------------
        // Generation loop
        // -------------------------
        for (int i = 0; i < effective_max_tokens; ++i) {
            llama_token id = llama_sampler_sample(smpl, ctx_, -1);
            llama_sampler_accept(smpl, id);

            if (llama_vocab_is_eog(vocab, id)) {
                saw_eog = true;
                stop_reason = "eog";
                break;
            }

            const auto piece = tokenToPiece(vocab, id);
            if (!appendAndCallback(piece)) {
                stop_reason = "callback_failed";
                break;
            }
            ++generated;

            const llama_token toks[1] = { id };
            if (!evalTokens(std::span{toks, 1})) {
                stop_reason = "eval_failed";
                break;
            }
        }

        if (stop_reason == "unknown" && generated >= effective_max_tokens) {
            // We hit the generation cap or ran out of room
            stop_reason = "max_tokens";
        }

        llama_sampler_free(smpl);

        last_stop_reason = stop_reason;

        LOG_INFO << "LlamaEngine Generation stopped: " << stop_reason
                 << " generated=" << generated
                 << " effective_max_tokens=" << effective_max_tokens
                 << " n_past=" << n_past_
                 << " n_ctx=" << llama_n_ctx(ctx_);

        // Success condition: model ended on its own
        if (saw_eog) {
            return true;
        }

        // Hard failures should not be retried
        if (stop_reason == "eval_failed" || stop_reason == "callback_failed") {
            return (stop_reason != "eval_failed"); // conservative: eval_failed => false
        }

        // If we cannot retry (continue_conversation) return best effort
        if (!can_retry) {
            return true;
        }

        // If we got here, we likely returned a partial result. Scale up and retry.
        if (target_out >= hard_max_out) {
            LOG_WARN_N << "Reached hard_max_out=" << hard_max_out
                     << " without EOG. Returning best effort partial result.";
            return true;
        }

        // Scale up (doubling works well; you can do 1.5x if you want)
        target_out = std::min(hard_max_out, target_out * 2);
    }

    // If we exhausted attempts, return best effort
    LOG_WARN_N << "Exhausted retry attempts. Last stop reason: " << last_stop_reason;
    return true;
}


bool LlamaSessionCtxImpl::resetContext() {
    LOG_DEBUG_N << "Resetting llama_context for new session";
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }

    auto cparams = llama_context_default_params();
    const int desired_ctx = (ctx_size_override_ > 0) ? ctx_size_override_ : model_ctx_->ctxSize();
    cparams.n_ctx = desired_ctx;

    // Optional, but nice: pick a sane batch default for large ctx
    // (you still won’t crash because evalTokens chunks)
    cparams.n_batch = std::min(desired_ctx, 2048);

    if (model_ctx_->threads() > 0) {
        cparams.n_threads = model_ctx_->threads();
        cparams.n_threads_batch = model_ctx_->threads();
    }

    ctx_ = llama_init_from_model(model_, cparams);
    if (!ctx_) {
        LOG_ERROR_N << "Failed to recreate llama_context";
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
