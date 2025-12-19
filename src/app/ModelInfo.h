#pragma once

#include <span>
#include <string_view>
#include <vector>
#include <cstdint>

struct ModelInfo {
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

    std::string_view name;
    std::string_view id;
    std::string_view filename;
    Quatization quantization{};
    size_t size_mb{};   // approximate in megabytes
    std::string_view sha;
    uint32_t capabilities{};
    std::string_view download_url; // If it ends with '/', the file name is appended for download
};

using model_list_t = std::span<const ModelInfo>; // NB: Non owning
using models_t = std::vector<ModelInfo>;
