#include "ModelLocator.h"

#include <cstdlib>
#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <iostream> // optional for debugging

namespace qvokal {

ModelLocator::ModelLocator(ModelSearchConfig config)
    : config_(std::move(config))
{}

// --- String helpers ---------------------------------------------------------

std::string ModelLocator::to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

bool ModelLocator::icontains(std::string_view haystack, std::string_view needle) {
    auto h = std::string(haystack);
    auto n = std::string(needle);
    std::transform(h.begin(), h.end(), h.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return h.find(n) != std::string::npos;
}

bool ModelLocator::istarts_with(std::string_view text, std::string_view prefix) {
    if (prefix.size() > text.size()) return false;
    auto t = std::string(text.substr(0, prefix.size()));
    auto p = std::string(prefix);
    std::transform(t.begin(), t.end(), t.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    std::transform(p.begin(), p.end(), p.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return t == p;
}

// --- Quantization heuristics -----------------------------------------------

std::string ModelLocator::extract_quantization_hint(std::string_view filename_no_ext) {
    // Crude but effective: look for common substrings.
    // You can extend this as needed.
    static const char* patterns[] = {
        "q2", "q3", "q4_0", "q4_1", "q4_k", "q5_0", "q5_1", "q5_k",
        "q6", "q8_0", "int4", "int8", "fp16", "f16", "fp32"
    };

    std::string lower = to_lower(std::string(filename_no_ext));
    for (auto* pat : patterns) {
        if (lower.find(pat) != std::string::npos) {
            return pat;
        }
    }
    return {}; // unknown / unquantized
}

int ModelLocator::quantization_score(std::string_view quant_hint,
                                     const std::vector<std::string>& preference) {
    if (quant_hint.empty())
        return -1; // lowest priority if we explicitly care about quantization

    for (std::size_t i = 0; i < preference.size(); ++i) {
        if (icontains(quant_hint, preference[i])) {
            // higher score = better
            return static_cast<int>(preference.size() - i);
        }
    }

    // Not found in preference list: neutral
    return 0;
}

// --- Filesystem scanning ----------------------------------------------------

bool ModelLocator::has_allowed_extension(const fs::path& p,
                                         const std::vector<std::string>& exts) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

DiscoveredModel ModelLocator::make_discovered(const fs::directory_entry& de) {
    DiscoveredModel dm;
    dm.path = de.path();
    dm.extension = to_lower(dm.path.extension().string());

    dm.base_name = dm.path.stem().string(); // filename without extension
    dm.quantization_hint = extract_quantization_hint(dm.base_name);

    std::error_code ec;
    dm.size_bytes = de.file_size(ec);
    if (ec) {
        dm.size_bytes = 0;
    }
    return dm;
}

// Default search roots based on OS + common tools.
std::vector<fs::path> ModelLocator::build_default_search_paths() const {
    std::vector<fs::path> paths;

    auto getenv_path = [](const char* name) -> std::optional<fs::path> {
        if (const char* v = std::getenv(name)) {
            if (*v != '\0') return fs::path(v);
        }
        return std::nullopt;
    };

    auto home         = getenv_path("HOME").value_or(fs::path{});
    auto localAppData = getenv_path("LOCALAPPDATA").value_or(fs::path{});
    auto appData      = getenv_path("APPDATA").value_or(fs::path{});
    auto userProfile  = getenv_path("USERPROFILE").value_or(fs::path{});

    // HuggingFace override vars if present
    if (auto hf_home = getenv_path("HF_HOME")) {
        paths.push_back(*hf_home / "hub");
    }
    if (auto hf_download = getenv_path("HF_HUB_DOWNLOAD_DIR")) {
        paths.push_back(*hf_download);
    }

#ifdef __linux__
    if (!home.empty()) {
        paths.push_back(home / ".cache/whisper");
        paths.push_back(home / ".cache/huggingface/hub");
        paths.push_back(home / ".cache/torch/hub");
        paths.push_back(home / ".cache/lm-studio/models");
        paths.push_back(home / ".local/share/nomic.ai/GPT4All/models");
        paths.push_back(home / ".ollama/models");
    }
#elif defined(__APPLE__)
    if (!home.empty()) {
        paths.push_back(home / "Library/Caches/whisper");
        paths.push_back(home / "Library/Caches/huggingface/hub");
        paths.push_back(home / "Library/Caches/torch/hub");
        paths.push_back(home / "Library/Application Support/LM Studio/models");
        paths.push_back(home / "Library/Application Support/nomic.ai/GPT4All/models");
        paths.push_back(home / ".ollama/models");
    }
#elif defined(_WIN32)
    if (!localAppData.empty()) {
        paths.push_back(localAppData / "whisper");
        paths.push_back(localAppData / "huggingface/hub");
        paths.push_back(localAppData / "torch/hub");
    }
    if (!appData.empty()) {
        paths.push_back(appData / "LM Studio/models");
        paths.push_back(appData / "nomic.ai/GPT4All/models");
    }
    if (!userProfile.empty()) {
        paths.push_back(userProfile / ".ollama/models");
    }
#endif

    return paths;
}

std::vector<fs::path> ModelLocator::effective_search_paths() const {
    std::vector<fs::path> result;
    if (config_.include_default_paths) {
        auto defaults = build_default_search_paths();
        result.insert(result.end(), defaults.begin(), defaults.end());
    }
    result.insert(result.end(),
                  config_.extra_search_paths.begin(),
                  config_.extra_search_paths.end());
    return result;
}

std::vector<DiscoveredModel> ModelLocator::scan_paths(
        const std::vector<fs::path>& roots) const
{
    std::vector<DiscoveredModel> result;

    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
            continue;
        }

        try {
            for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
                if (ec) break;
                const auto& entry = *it;
                if (!entry.is_regular_file()) continue;

                if (!has_allowed_extension(entry.path(), config_.model_extensions))
                    continue;

                result.push_back(make_discovered(entry));
            }
        } catch (const std::exception&) {
            // Ignore scanning errors for this root
        }
    }

    return result;
}

// --- Public API -------------------------------------------------------------

std::vector<DiscoveredModel> ModelLocator::list_all_models() const {
    auto roots = effective_search_paths();
    return scan_paths(roots);
}

ModelLookupResult ModelLocator::find_model(std::string_view base_model_name) const {
    ModelLookupResult res;

    if (base_model_name.empty()) {
        return res;
    }

    auto roots = effective_search_paths();
    auto all   = scan_paths(roots);

    // Filter by base name
    std::string base = std::string(base_model_name);
    std::string base_lower = to_lower(base);

    for (const auto& dm : all) {
        std::string candidate = dm.base_name;
        std::string candidate_lower = to_lower(candidate);

        bool match = false;
        if (config_.case_insensitive_match) {
            if (config_.allow_substring_match) {
                match = (candidate_lower.find(base_lower) != std::string::npos);
            } else {
                match = istarts_with(candidate_lower, base_lower);
            }
        } else {
            if (config_.allow_substring_match) {
                match = (candidate.find(base) != std::string::npos);
            } else {
                match = candidate.rfind(base, 0) == 0; // prefix
            }
        }

        if (match) {
            res.all_matches.push_back(dm);
        }
    }

    if (res.all_matches.empty()) {
        return res;
    }

    // Choose best based on quantization preference and size
    const auto& pref = config_.quantization_preference;
    auto best = res.all_matches.front();
    int best_score = quantization_score(best.quantization_hint, pref);

    for (std::size_t i = 1; i < res.all_matches.size(); ++i) {
        const auto& cand = res.all_matches[i];
        int score = quantization_score(cand.quantization_hint, pref);

        if (score > best_score) {
            best = cand;
            best_score = score;
        } else if (score == best_score && config_.prefer_smaller_when_equal_quant) {
            if (cand.size_bytes != 0 && best.size_bytes != 0 &&
                cand.size_bytes < best.size_bytes) {
                best = cand;
                best_score = score;
            }
        }
    }

    res.best_match = best;
    return res;
}

} // namespace qvokal
