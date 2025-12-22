#include "ModelInfo.h"

#include <array>
#include <format>
#include <sstream>
#include <stdexcept>

namespace {

constexpr auto role_names = std::to_array<std::string_view>({
    "system",     // PromptRole::System
    "user",       // PromptRole::User
    "assistant"   // PromptRole::Assistant
});

constexpr auto role_labels = std::to_array<std::string_view>({
    "System",     // PromptRole::System
    "User",       // PromptRole::User
    "Assistant"   // PromptRole::Assistant
});

inline std::string_view role_name(PromptRole role) {
    return role_names.at(static_cast<size_t>(role));
}

inline std::string_view role_label(PromptRole role) {
    return role_labels.at(static_cast<size_t>(role));
}

inline bool has_system_first(messages_view_t messages) {
    return !messages.empty() && messages.front()->role == PromptRole::System;
}

inline messages_view_t without_system(messages_view_t messages) {
    return has_system_first(messages) ? messages.subspan(1) : messages;
}

inline bool ends_with_assistant(messages_view_t messages) {
    return !messages.empty() && messages.back()->role == PromptRole::Assistant;
}

} // namespace

std::string ModelInfo::formatPrompt(messages_view_t messages) const {
    if (prompt_style == PromptStyle::None) {
        throw std::invalid_argument("ModelInfo::formatPrompt(): PromptStyle::None is invalid for LLM models");
    }

    // Note: empty message list is allowed (you might use a system-only default elsewhere),
    // but most styles will just produce a minimal "assistant start" or empty prompt.
    const bool open_assistant_turn = !ends_with_assistant(messages);

    std::ostringstream oss;

    switch (prompt_style) {
    case PromptStyle::Llama3: {
        // Llama 3.x format:
        // <|begin_of_text|>
        // <|start_header_id|>system<|end_header_id|>\n...<|eot_id|>
        // <|start_header_id|>user<|end_header_id|>\n...<|eot_id|>
        // ...
        // <|start_header_id|>assistant<|end_header_id|>\n
        static constexpr std::string_view BOT = "<|begin_of_text|>";
        static constexpr std::string_view SHS = "<|start_header_id|>";
        static constexpr std::string_view SHE = "<|end_header_id|>\n";
        static constexpr std::string_view EOT = "<|eot_id|>";

        oss << BOT;

        // If first message is system, write it first (as required)
        if (has_system_first(messages)) {
            oss << std::format("{}system{}{}{}",
                               SHS, SHE,
                               messages.front()->content,
                               EOT);
        }

        // Then write remaining messages
        for (const auto & m : without_system(messages)) {
            oss << std::format("{}{}{}{}{}",
                               SHS, role_name(m->role), SHE,
                               m->content,
                               EOT);
        }

        if (open_assistant_turn) {
            oss << std::format("{}assistant{}", SHS, SHE);
        }
        break;
    }

    case PromptStyle::ChatML: {
        // ChatML-ish format:
        // <|im_start|>system\n...\n<|im_end|>\n
        // <|im_start|>user\n...\n<|im_end|>\n
        // ...
        // <|im_start|>assistant\n
        static constexpr std::string_view IMS = "<|im_start|>";
        static constexpr std::string_view IME = "<|im_end|>";

        if (has_system_first(messages)) {
            oss << std::format("{}system\n{}\n{}\n",
                               IMS, messages.front()->content, IME);
        }

        for (const auto & m : without_system(messages)) {
            oss << std::format("{}{}\n{}\n{}\n",
                               IMS, role_name(m->role),
                               m->content,
                               IME);
        }

        if (open_assistant_turn) {
            oss << std::format("{}assistant\n", IMS);
        }
        break;
    }

    case PromptStyle::Mistral: {
        // Common Mistral instruct:
        // <s>[INST] (system?) user [/INST] assistant </s>
        // <s>[INST] user2 [/INST] assistant2 </s>
        //
        // If the last turn is user and we want the model to answer:
        // end with "<s>[INST] ... [/INST]" (no assistant text, no </s> required).
        //
        // We support:
        // - Optional system as first message
        // - Any number of user/assistant turns (best if alternating)
        const std::string_view sys = has_system_first(messages) ? messages.front()->content : std::string_view{};
        const auto msgs = without_system(messages);

        bool first_inst = true;

        for (size_t i = 0; i < msgs.size(); ) {
            const auto & m = msgs[i];

            if (m->role != PromptRole::User) {
                // Be forgiving: if an assistant message appears without a user opener,
                // include it as plain text so you don't silently drop history.
                oss << m->content << "\n";
                ++i;
                continue;
            }

            // open an [INST] block
            if (first_inst) {
                if (!sys.empty()) {
                    oss << std::format("<s>[INST] {}\n\n{} [/INST]",
                                       sys, m->content);
                } else {
                    oss << std::format("<s>[INST] {} [/INST]", m->content);
                }
                first_inst = false;
            } else {
                oss << std::format("<s>[INST] {} [/INST]", m->content);
            }

            // If next is assistant, append it and close the sequence
            if (i + 1 < msgs.size() && msgs[i + 1]->role == PromptRole::Assistant) {
                oss << std::format(" {} </s>", msgs[i + 1]->content);
                i += 2;
            } else {
                // No assistant follows:
                // - If we want a model answer: leave it open (no assistant text)
                // - If not: close it anyway
                if (!open_assistant_turn) {
                    oss << " </s>";
                }
                ++i;
            }
        }

        // Edge case: empty msgs but system exists. Nothing to format.
        // You might choose to open a minimal [INST] with system only,
        // but most apps always have at least a user message for generation.
        break;
    }

    case PromptStyle::Raw: {
        // Human-readable fallback for "completion-ish" models
        // System: ...
        // User: ...
        // Assistant: ...
        if (has_system_first(messages)) {
            oss << std::format("{}: {}\n\n",
                               role_label(PromptRole::System),
                               messages.front()->content);
        }

        for (const auto & m : without_system(messages)) {
            oss << std::format("{}: {}\n\n",
                               role_label(m->role),
                               m->content);
        }

        if (open_assistant_turn) {
            oss << std::format("{}: ", role_label(PromptRole::Assistant));
        }
        break;
    }

    case PromptStyle::None:
        // handled above
        break;
    }

    return oss.str();
}
