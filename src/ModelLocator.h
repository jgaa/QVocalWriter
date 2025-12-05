#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <filesystem>
#include <cstdint>

namespace qvokal {

namespace fs = std::filesystem;

/// Hints when searching / scoring models.
struct ModelSearchConfig {
    /// If true, search default known locations (HF cache, LM Studio, etc.)
    bool include_default_paths = true;

    /// Additional search roots (recursively scanned).
    std::vector<fs::path> extra_search_paths;

    /// File extensions (lowercase) to consider as model files.
    /// You can override this; by default we include gguf + whisper/bin-style.
    std::vector<std::string> model_extensions = {
        ".gguf", ".bin", ".pt", ".onnx"
    };

    /// Quantization preference, highest priority first.
    /// We match these as case-insensitive substrings in filenames.
    /// Example: { "q4_k", "q5_k", "q8_0", "fp16", "f16" }
    std::vector<std::string> quantization_preference = {
        "q4_k", "q4_0",
        "q5_k", "q5_0",
        "q8_0",
        "fp16", "f16"
    };

    /// If true, when quantization priority is tied, prefer smaller file size.
    bool prefer_smaller_when_equal_quant = true;

    /// If true, base-name matching is case-insensitive.
    bool case_insensitive_match = true;

    /// If true, we treat a candidate as match if it contains the base name
    /// as a substring (not only prefix).
    bool allow_substring_match = true;
};

/// A discovered model candidate on disk.
struct DiscoveredModel {
    std::string base_name;               ///< filename without extension
    fs::path   path;                     ///< full filesystem path
    std::string extension;               ///< file extension (lowercase, with dot)
    std::string quantization_hint;       ///< e.g. "q4_k", "q5_1", "fp16"
    std::uintmax_t size_bytes = 0;       ///< 0 if unknown
};

/// Result of a lookup
struct ModelLookupResult {
    /// Best candidate path if found.
    std::optional<DiscoveredModel> best_match;

    /// All candidates that matched the base name (for debugging/inspection).
    std::vector<DiscoveredModel> all_matches;
};

class ModelLocator {
public:
    explicit ModelLocator(ModelSearchConfig config = {});

    /// Re-scan all search paths and return every model we recognize.
    /// NOTE: This may be expensive; you might want to cache results.
    std::vector<DiscoveredModel> list_all_models() const;

    /// Find the best matching model for a given base model name.
    /// Example base names:
    ///   - "ggml-base.en"
    ///   - "base.en"
    ///   - "Llama-3-8B-Instruct"
    ModelLookupResult find_model(std::string_view base_model_name) const;

    /// Convenience helper: true if we can find *any* model for base_model_name.
    bool model_exists(std::string_view base_model_name) const {
        return find_model(base_model_name).best_match.has_value();
    }

    /// Expose effective search paths (defaults + extra).
    std::vector<fs::path> effective_search_paths() const;

private:
    ModelSearchConfig config_;

    std::vector<fs::path> build_default_search_paths() const;
    std::vector<DiscoveredModel> scan_paths(const std::vector<fs::path>& roots) const;

    static bool has_allowed_extension(const fs::path& p,
                                      const std::vector<std::string>& exts);

    static std::string to_lower(std::string s);
    static bool icontains(std::string_view haystack, std::string_view needle);
    static bool istarts_with(std::string_view text, std::string_view prefix);

    static std::string extract_quantization_hint(std::string_view filename_no_ext);
    static int quantization_score(std::string_view quant_hint,
                                  const std::vector<std::string>& preference);

    static DiscoveredModel make_discovered(const fs::directory_entry& de);
};

} // namespace qvokal
