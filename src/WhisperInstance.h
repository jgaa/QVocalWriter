#pragma once

#include <memory>

#include "ModelMgr.h"
#include <whisper.h>

class WhisperInstance final : public ModelInstanceBase
{
public:
    WhisperInstance(const ModelInfo& modelInfo, const QString& fullPath);
    ~WhisperInstance();

    ModelKind kind() const noexcept override { return ModelKind::WHISPER;}

    auto *whisperCtx() noexcept {
        assert(shared_ctx_);
        return shared_ctx_.get();
    }

    const auto *whisperCtx() const noexcept {
        assert(shared_ctx_);
        return shared_ctx_.get();
    }

    bool haveWhisperCtx() const noexcept {
        return shared_ctx_ != nullptr;
    }

    std::shared_ptr<whisper_state> newState();

protected:
    bool loadImpl() noexcept override;
    bool unloadImpl() noexcept override;

    void *ctx() override {
        return shared_ctx_.get();
    }

    bool haveCtx() const noexcept override {
        return shared_ctx_ != nullptr;
    }

private:
    std::shared_ptr<whisper_context> shared_ctx_;
};
