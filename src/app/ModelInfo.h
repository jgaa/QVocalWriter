#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <ctime>

enum class PromptRole {
    System,
    User,
    Assistant
};

struct ChatMessage {
    PromptRole role;
    std::string content;
    bool completed{true}; // false during assistants partial updates
    time_t timestamp{time(nullptr)};
    double duration_seconds{0.0}; // models time to generate a full response
};

using messages_view_t = std::span<const ChatMessage * const>;

struct ModelInfo {
    enum class PromptStyle {
        None,
        Llama3,
        ChatML,
        Mistral,
        Raw
    };

    enum Quatization {
        Q_Unknown,
        Q4_0,
        Q4_1,
        Q5_0,
        Q5_1,
        Q8_0,
        FP16,
        FP32,
    };

    enum Capability : uint32_t {
        None        = 0,
        Chat        = 1 << 0,
        Rewrite     = 1 << 1,
        Translate   = 1 << 2,
        Transcribe  = 1 << 3,
    };


    std::string formatPrompt(messages_view_t messages) const;

    std::string_view name;
    std::string_view id;
    PromptStyle prompt_style{PromptStyle::None};
    std::string_view filename;
    Quatization quantization{};
    size_t size_mb{};   // approximate in megabytes
    std::string_view sha;
    uint32_t capabilities{};
    std::string_view download_url; // If it ends with '/', the file name is appended for download
};

enum class ModelKind {
    WHISPER,
    GENERAL
};

using model_list_t = std::span<const ModelInfo>; // NB: Non owning
using models_t = std::vector<const ModelInfo *>;
