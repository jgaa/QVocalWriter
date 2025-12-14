#pragma once

#include <span>
#include <string_view>
#include <vector>

struct ModelInfo {
    enum Quatization {
        Q4_0,
        Q4_1,
        Q5_0,
        Q5_1,
        Q8_0,
        FP16,
        FP32,
    };
    std::string_view id;
    std::string_view filename;
    Quatization quantization{};
    size_t size_mb{};   // approximate in megabytes
    std::string_view sha;
};

using model_list_t = std::span<const ModelInfo>; // NB: Non owning
using models_t = std::vector<ModelInfo>;
