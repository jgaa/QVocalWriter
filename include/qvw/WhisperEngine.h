#pragma once

#include <span>

#include "EngineBase.h"

struct whisper_context;

#if defined(_WIN32)
#if defined(QVW_WHISPER_WRAP_BUILD)
#define QVW_WHISPER_WRAP_API __declspec(dllexport)
#else
#define QVW_WHISPER_WRAP_API __declspec(dllimport)
#endif
#else
#define QVW_WHISPER_WRAP_API __attribute__((visibility("default")))
#endif

extern "C" __attribute__((visibility("default"))) __attribute__((used))
int qvw_whisper_wrap_abi_version() { return 1; }

namespace qvw {

class WhisperEngine;

struct WhisperEngineLoadParams : public EngineLoadParams {
    bool use_gpu{};
    bool flash_attn{};
    int gpu_device{};
};

/*! Session context for Whisper model sessions.
 *
 *  This is ment to be used for processing related audio data.
 *
 *  If you have multiple audio streams to process, create a new session for each.
 */
class QVW_WHISPER_WRAP_API WhisperSessionCtx : public SessionCtx {
public:
    struct WhisperFullParams {
        std::string language; // empty for auto
        int threads{-1}; // -1 for using default
    };

    WhisperSessionCtx() = default;
    virtual ~WhisperSessionCtx() = default;

    /*! Processes the full audio data using the Whisper model.
     *
     * @param data Audio data as a span of floats.
     * @param params Parameters for the Whisper processing.
     * @return True if processing was successful, false otherwise.
     */
    virtual bool whisperFull(std::span<const float> data, const WhisperFullParams& params) = 0;
};

/*! Context for a loaded Whisper model.
 *
 */
class QVW_WHISPER_WRAP_API WhisperCtx : public ModelCtx {
public:
    virtual whisper_context *ctx() noexcept;
    virtual const whisper_context *ctx() const noexcept;
};

/*! Whisper engine interface
 *
 */
class QVW_WHISPER_WRAP_API WhisperEngine : public EngineBase {
public:

    struct WhisperCreateParams{};


    /*! Creates a new Whisper engine instance.
     *
     * @param params Parameters for creating the engine.
     * @return Shared pointer to the new Whisper engine instance.
     */
    static QVW_WHISPER_WRAP_API std::shared_ptr<WhisperEngine> create(const WhisperCreateParams& params);
};


} // ns
