#pragma once

#include <optional>
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

namespace qvw {

class WhisperEngine;

struct WhisperEngineLoadParams : public EngineLoadParams {
    bool use_gpu{};
    bool flash_attn{};
    int gpu_device{};
    int threads{-1};
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
        std::optional<int> max_len;
        std::optional<int> offset_ms;
        std::optional<bool> token_timestamps;
        std::optional<bool> no_context;
        std::optional<bool> single_segment;
        std::optional<bool> print_progress;
        std::optional<bool> print_timestamps;
        std::optional<bool> print_realtime;
    };

    struct Segment {
        int64_t t0_ms = 0;
        int64_t t1_ms = 0;
        std::string text;

        // optional extras
        float avg_logprob = 0.0f;
        float no_speech_prob = 0.0f;
        int   speaker = -1;                 // if you ever add diarization
    };

    struct Transcript {
        std::vector<Segment> segments;
        std::string full_text;              // convenience (can be derived)
        std::string language;               // detected or forced
    };

    WhisperSessionCtx();
    virtual ~WhisperSessionCtx();

    /*! Processes the full audio data using the Whisper model.
     *
     * @param data Audio data as a span of floats.
     * @param params Parameters for the Whisper processing.
     * @param out Output transcript structure to hold the results.
     * @return True if processing was successful, false otherwise.
     */
    virtual bool whisperFull(std::span<const float> data,
                             const WhisperFullParams& params,
                             Transcript& out) = 0;

};

/*! Context for a loaded Whisper model.
 *
 */
class QVW_WHISPER_WRAP_API WhisperCtx : public ModelCtx {
public:
    WhisperCtx();
    virtual ~WhisperCtx();

    virtual whisper_context *ctx() noexcept = 0;
    virtual const whisper_context *ctx() const noexcept = 0;
};

/*! Whisper engine interface
 *
 */
class QVW_WHISPER_WRAP_API WhisperEngine : public EngineBase {
public:
    QVW_WHISPER_WRAP_API WhisperEngine();
    QVW_WHISPER_WRAP_API virtual ~WhisperEngine();

    struct WhisperCreateParams{};


    /*! Creates a new Whisper engine instance.
     *
     * @param params Parameters for creating the engine.
     * @return Shared pointer to the new Whisper engine instance.
     */
    static QVW_WHISPER_WRAP_API std::shared_ptr<WhisperEngine> create(const WhisperCreateParams& params);

    virtual std::shared_ptr<WhisperCtx> loadWhisper(const std::string& modelId,
                                                    const std::filesystem::path& modelPath,
                                                    const WhisperEngineLoadParams& params) = 0;
};



} // ns
