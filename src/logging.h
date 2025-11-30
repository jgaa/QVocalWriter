#pragma once

#define LOGFAULT_USE_TID_AS_NAME 1

#include "logfault/logfault.h"

#define LOG_ERROR   LFLOG_ERROR
#define LOG_WARN    LFLOG_WARN
#define LOG_INFO    LFLOG_INFO
#define LOG_DEBUG   LFLOG_DEBUG
#define LOG_TRACE   LFLOG_TRACE

#define LOG_ERROR_N   LFLOG_ERROR_EX
#define LOG_WARN_N    LFLOG_WARN_EX
#define LOG_INFO_N    LFLOG_INFO_EX
#define LOG_DEBUG_N   LFLOG_DEBUG_EX
#define LOG_TRACE_N   LFLOG_TRACE_EX

#define LOG_ERROR_EX(...)   LOGFAULT_LOG_EX__(logfault::LogLevel::ERROR __VA_OPT__(, __VA_ARGS__))
#define LOG_WARN_EX(...)    LOGFAULT_LOG_EX__(logfault::LogLevel::WARN __VA_OPT__(, __VA_ARGS__))
#define LOG_NOTICE_EX(...)  LOGFAULT_LOG_EX__(logfault::LogLevel::NOTICE __VA_OPT__(, __VA_ARGS__))
#define LOG_INFO_EX(...)    LOGFAULT_LOG_EX__(logfault::LogLevel::INFO __VA_OPT__(, __VA_ARGS__))
#define LOG_DEBUG_EX(...)   LOGFAULT_LOG_EX__(logfault::LogLevel::DEBUGGING __VA_OPT__(, __VA_ARGS__))
#define LOG_TRACE_EX(...)   LOGFAULT_LOG_EX__(logfault::LogLevel::TRACE __VA_OPT__(, __VA_ARGS__))

