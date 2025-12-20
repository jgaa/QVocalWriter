#pragma once

#include <functional>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string_view>
#include <array>
#include <format>
#include <chrono>

namespace logfault_fwd {

// Same as logfault::LogLevel
enum class Level { NONE, ERROR, WARN, NOTICE, INFO, DEBUG, TRACE };

struct SourceLoc {
    const char* file{};
    int line{};
    const char* func{};
};

// The callback type for log forwarding to the actual logger used by the app (e.g., logfault)
using logfault_callback_t = std::function<void(Level lvl, SourceLoc loc, std::string_view msg, std::string_view tag)>;

inline std::string_view to_name(Level l) {
    constexpr static auto names = std::to_array<std::string_view>({
        ""/* None*/, "ERROR", "WARN", "NOTICE", "INFO", "DEBUG", "TRACE"
    });

    const auto idx = static_cast<size_t>(l);
    return names.at(idx);
}

} // ns

template <>
struct std::formatter<logfault_fwd::Level> : std::formatter<std::string_view> {
    auto format(logfault_fwd::Level l, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_name(l), ctx);
    }
};


namespace logfault_fwd {

struct Instance {
    logfault_callback_t cb;
    std::string tag;
    std::atomic<Level> level{Level::DEBUG};
};

inline Instance& instance() {
    static Instance s;
    return s;
}

inline void setCallback(logfault_callback_t cb, std::string_view tag) noexcept {
    auto& i = instance();
    i.tag = tag;
    i.cb = std::move(cb);
}

inline void setLevel(Level lvl) noexcept {
    instance().level = lvl;
}

inline Level level() noexcept {
    return instance().level.load(std::memory_order_relaxed);
}

class Log {
public:
    Log(Level lvl, SourceLoc loc) : lvl_(lvl), loc_(loc) {}
    ~Log() { flush(); }

    std::ostream& Line() { return ss_; }

private:
    void flush() noexcept {
        const auto msg = ss_.str();
        if (msg.empty()) return;

        if (auto& cb = instance().cb) {
            cb(lvl_, loc_, msg, instance().tag);
            return;
        }

        const auto now = std::chrono::system_clock::now();
        // add now as iso date/time
        std::clog << format("{:%FT%T} [{}] {} {}", now, lvl_, instance().tag, msg) << std::endl;
    }

    Level lvl_;
    SourceLoc loc_;
    std::ostringstream ss_;
};

#ifdef _LOGFAULT_H
static inline void forward_to_logfault(logfault_fwd::Level lvl,
                                logfault_fwd::SourceLoc loc,
                                std::string_view msg,
                                std::string_view tag) {

    const auto lf_level = static_cast<logfault::LogLevel>(lvl);
    if ( ::logfault::LogManager::Instance().IsRelevant(lf_level)) {
        ::logfault::Log(lf_level, loc.file, loc.line, loc.func).Line() << tag << ' '<< msg;
    }
}
#endif

} // namespace logfault_fwd

#if defined(LOGFAULT_FWD_ENABLE_LOGGING) && LOGFAULT_FWD_ENABLE_LOGGING

#if defined(__GNUC__) || defined(__clang__)
#define LOGFAULT_FWD_FUNC __PRETTY_FUNCTION__
#else
#define LOGFAULT_FWD_FUNC __func__
#endif

#define LOGFAULT_FWD_RELEVANT(lvl) \
    (lvl <= logfault_fwd::level())

#define LOG_ERROR  LOGFAULT_FWD_RELEVANT(logfault_fwd::Level::ERROR) && logfault_fwd::Log(logfault_fwd::Level::ERROR,{}).Line()
#define LOG_WARN   LOGFAULT_FWD_RELEVANT(logfault_fwd::Level::WARN) && logfault_fwd::Log(logfault_fwd::Level::WARN,{}).Line()
#define LOG_INFO   LOGFAULT_FWD_RELEVANT(logfault_fwd::Level::INFO) && logfault_fwd::Log(logfault_fwd::Level::INFO,{}).Line()
#define LOG_DEBUG  LOGFAULT_FWD_RELEVANT(logfault_fwd::Level::DEBUG) && logfault_fwd::Log(logfault_fwd::Level::DEBUG,{}).Line()
#define LOG_TRACE  LOGFAULT_FWD_RELEVANT(logfault_fwd::Level::TRACE) && logfault_fwd::Log(logfault_fwd::Level::TRACE,{}).Line()

#define LOG_ERROR_N  LOGFAULT_FWD_RELEVANT(logfault_fwd::Level::ERROR) && logfault_fwd::Log(logfault_fwd::Level::ERROR,  {__FILE__, __LINE__, LOGFAULT_FWD_FUNC}).Line()
#define LOG_WARN_N   LOGFAULT_FWD_RELEVANT(logfault_fwd::Level::WARN) && logfault_fwd::Log(logfault_fwd::Level::WARN,    {__FILE__, __LINE__, LOGFAULT_FWD_FUNC}).Line()
#define LOG_INFO_N   LOGFAULT_FWD_RELEVANT(logfault_fwd::Level::INFO) && logfault_fwd::Log(logfault_fwd::Level::INFO,    {__FILE__, __LINE__, LOGFAULT_FWD_FUNC}).Line()
#define LOG_DEBUG_N  LOGFAULT_FWD_RELEVANT(logfault_fwd::Level::DEBUG) && logfault_fwd::Log(logfault_fwd::Level::DEBUG,   {__FILE__, __LINE__, LOGFAULT_FWD_FUNC}).Line()
#define LOG_TRACE_N  LOGFAULT_FWD_RELEVANT(logfault_fwd::Level::TRACE) && logfault_fwd::Log(logfault_fwd::Level::TRACE,   {__FILE__, __LINE__, LOGFAULT_FWD_FUNC}).Line()

#endif // LOGFAULT_FWD_ENABLE_LOGGING

