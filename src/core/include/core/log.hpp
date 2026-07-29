#pragma once

// Logging that works in every build target, including the headless server.
// Intentionally not SDL_Log: the server links no SDL at all.

namespace core {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Off   = 5,
};

/// Messages below this level are dropped. Defaults to Info, or Trace when the
/// GAME_LOG_LEVEL environment variable says so (trace/debug/info/warn/error).
void log_set_level(LogLevel level);
LogLevel log_current_level();

/// Prefix printed on every line, e.g. "server" or "client". Max 15 chars.
void log_set_tag(const char* tag);

#if defined(__GNUC__) || defined(__clang__)
#define GAME_PRINTF_LIKE(fmt_index, first_arg) \
    __attribute__((format(printf, fmt_index, first_arg)))
#else
#define GAME_PRINTF_LIKE(fmt_index, first_arg)
#endif

void log_write(LogLevel level, const char* fmt, ...) GAME_PRINTF_LIKE(2, 3);

}  // namespace core

#define LOG_TRACE(...) ::core::log_write(::core::LogLevel::Trace, __VA_ARGS__)
#define LOG_DEBUG(...) ::core::log_write(::core::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::core::log_write(::core::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...)  ::core::log_write(::core::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::core::log_write(::core::LogLevel::Error, __VA_ARGS__)
