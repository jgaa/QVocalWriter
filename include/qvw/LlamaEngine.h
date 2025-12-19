#pragma once

#include <optional>
#include <span>
#include "EngineBase.h"

#if defined(_WIN32)
#if defined(QVW_LLAMA_WRAP_BUILD)
#define QVW_LLAMA_WRAP_API __declspec(dllexport)
#else
#define QVW_LLAMA_WRAP_API __declspec(dllimport)
#endif
#else
#define QVW_LLAMA_WRAP_API __attribute__((visibility("default")))
#endif

namespace qvw {

class LlamaEngine;

struct LlamaEngineLoadParams : public EngineLoadParams {
    int threads{-1};
    int ctx_size{4096};        // typical default; tune as you like
    int n_gpu_layers{0};       // keep 0 for CPU-only wrapper
    bool flash_attn{false};    // optional
};

class QVW_LLAMA_WRAP_API LlamaSessionCtx : public SessionCtx {
public:
    struct Params {
        int   max_tokens{256};
        float temperature{0.7f};
        int   top_k{40};
        float top_p{0.95f};
        float repeat_penalty{1.1f};

        std::vector<std::string> stop;

        // -------------------------
        // Common presets
        // -------------------------

        /// Balanced assistant-style output (default)
        static Params Balanced() {
            return Params{};
        }

        /// Translation (faithful, low creativity)
        static Params Translate(int maxTokens = 512) {
            Params p;
            p.max_tokens     = maxTokens;
            p.temperature    = 0.25f;
            p.top_k          = 30;
            p.top_p          = 0.9f;
            p.repeat_penalty = 1.1f;
            return p;
        }

        /// Deterministic / factual / cleanup tasks
        /// (summaries, transcription cleanup, code, Q&A)
        static Params Deterministic(int maxTokens = 256) {
            Params p;
            p.max_tokens     = maxTokens;
            p.temperature    = 0.2f;
            p.top_k          = 20;
            p.top_p          = 0.9f;
            p.repeat_penalty = 1.1f;
            return p;
        }

        /// Short, precise answers (commands, confirmations)
        static Params ShortAnswer() {
            Params p;
            p.max_tokens     = 64;
            p.temperature    = 0.3f;
            p.top_k          = 20;
            p.top_p          = 0.9f;
            p.repeat_penalty = 1.1f;
            return p;
        }

        /// General chat / assistant (slightly more creative)
        static Params Chat(int maxTokens = 256) {
            Params p;
            p.max_tokens     = maxTokens;
            p.temperature    = 0.7f;
            p.top_k          = 40;
            p.top_p          = 0.95f;
            p.repeat_penalty = 1.1f;
            return p;
        }

        /// Creative writing (blog posts, stories, brainstorming)
        static Params Creative(int maxTokens = 512) {
            Params p;
            p.max_tokens     = maxTokens;
            p.temperature    = 1.0f;
            p.top_k          = 100;
            p.top_p          = 0.98f;
            p.repeat_penalty = 1.05f;
            return p;
        }

        /// Very strict / near-greedy decoding
        /// Useful for testing or reproducibility
        static Params Greedy(int maxTokens = 256) {
            Params p;
            p.max_tokens     = maxTokens;
            p.temperature    = 0.0f;
            p.top_k          = 1;
            p.top_p          = 1.0f;
            p.repeat_penalty = 1.0f;
            return p;
        }
    };


    LlamaSessionCtx();
    ~LlamaSessionCtx() override;

    // SessionCtx
    QVW_LLAMA_WRAP_API void setOnPartialTextCallback(std::function<void(const std::string&)>) override;
    QVW_LLAMA_WRAP_API std::string getFullTextResult() const override;

    // Llama API
    QVW_LLAMA_WRAP_API bool prompt(std::string_view text, const Params& params);

protected:
    virtual void setOnPartialTextCallbackImpl(std::function<void(const std::string&)>) = 0;
    virtual std::string getFullTextResultImpl() const = 0;

    // Llama API
    virtual bool promptImpl(std::string_view text, const Params& params) = 0;
};

class QVW_LLAMA_WRAP_API LlamaCtx : public ModelCtx {
public:
    LlamaCtx();
    ~LlamaCtx() override;
};

class QVW_LLAMA_WRAP_API LlamaEngine : public EngineBase {
public:
    QVW_LLAMA_WRAP_API LlamaEngine();
    QVW_LLAMA_WRAP_API ~LlamaEngine() override;

    struct LlamaCreateParams {};

    static QVW_LLAMA_WRAP_API std::shared_ptr<LlamaEngine> create(const LlamaCreateParams& params);

    virtual std::shared_ptr<LlamaCtx> loadLlama(const std::string& modelId,
                                                const std::filesystem::path& modelPath,
                                                const LlamaEngineLoadParams& params) = 0;
};

} // namespace qvw
