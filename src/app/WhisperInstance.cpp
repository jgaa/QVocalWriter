
#include "WhisperInstance.h"
#include "ScopedTimer.h"

#include "logging.h"
using namespace std;

WhisperInstance::WhisperInstance(const ModelInfo &modelInfo, const QString& fullPath)
    : ModelInstance(modelInfo, fullPath)
{
}

WhisperInstance::~WhisperInstance()
{
    unloadImpl();
}

std::shared_ptr<whisper_state> WhisperInstance::newState()
{
    assert(shared_ctx_ != nullptr);

    whisper_state *state = whisper_init_state(whisperCtx());
    if (!state) {
        LOG_ERROR_N << "Failed to create new Whisper state";
        return nullptr;
    }

    return std::shared_ptr<whisper_state>(state, [this](whisper_state *s) {
        LOG_TRACE_N << "Freeing Whisper state";
        whisper_free_state(s);
    });
}

bool WhisperInstance::loadImpl() noexcept
{
    assert(shared_ctx_ == nullptr);

    const std::string full_path = path().toStdString();
    LOG_DEBUG_N << "Loading Whisper model from " << path();

    whisper_context_params cparams = whisper_context_default_params();

    // Optionally modify defaults:
    cparams.use_gpu = false;          // TODO: Make optional later
    cparams.flash_attn = false;       // TODO: Make optional later (require gpu)
    cparams.gpu_device = 0;           // ignored for CPU mode

    // DTW features are advanced; keep disabled for now
    cparams.dtw_token_timestamps = false;
    cparams.dtw_aheads_preset = WHISPER_AHEADS_NONE;
    cparams.dtw_n_top = 0;            // default means let whisper choose

    const ScopedTimer timer;
    if (auto *ctx = whisper_init_from_file_with_params_no_state(full_path.c_str(), cparams)) {
        shared_ctx_ = std::shared_ptr<whisper_context>(ctx, [](whisper_context *c) {
            LOG_TRACE_N << "Freeing Whisper model context";
            whisper_free(c);
            LOG_TRACE_N << "Whisper model context freed";
        });
    }

    if (!shared_ctx_) {
        LOG_ERROR_N << "Failed to load Whisper model from " << path();
        return false;
    }

    LOG_DEBUG_N << "Model loaded in " << timer.elapsed() << " seconds";
    return true;
}

bool WhisperInstance::unloadImpl() noexcept
{
    if (shared_ctx_) {
        shared_ctx_.reset();
    }
    return true;
}
