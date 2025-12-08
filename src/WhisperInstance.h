#pragma once

#include "ModelMgr.h"
#include <whisper.h>

class WhisperInstance final : public ModelInstanceBase
{
public:
    WhisperInstance(const ModelInfo& modelInfo, const QString& fullPath);
    ~WhisperInstance();

    ModelKind kind() const noexcept override { return ModelKind::WHISPER;}

    auto *whisperCtx() noexcept {
        return shared_ctx_;
    }

    const auto *whisperCtx() const noexcept {
        return shared_ctx_;
    }

    std::shared_ptr<whisper_state> newState();

protected:
    bool loadImpl() noexcept override;
    bool unloadImpl() noexcept override;

    void *ctx() override {
        return shared_ctx_;
    }

    bool haveCtx() const noexcept override {
        return shared_ctx_ != nullptr;
    }

private:
    whisper_context *shared_ctx_{};
};
