#include "core/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/time.hpp"

namespace core {
namespace {

LogLevel g_level = LogLevel::Info;
char g_tag[16] = "game";
bool g_level_initialised = false;

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Off:   return "OFF  ";
    }
    return "?????";
}

void init_level_from_env() {
    g_level_initialised = true;
    const char* env = std::getenv("GAME_LOG_LEVEL");
    if (env == nullptr) {
        return;
    }
    if (std::strcmp(env, "trace") == 0) {
        g_level = LogLevel::Trace;
    } else if (std::strcmp(env, "debug") == 0) {
        g_level = LogLevel::Debug;
    } else if (std::strcmp(env, "info") == 0) {
        g_level = LogLevel::Info;
    } else if (std::strcmp(env, "warn") == 0) {
        g_level = LogLevel::Warn;
    } else if (std::strcmp(env, "error") == 0) {
        g_level = LogLevel::Error;
    } else if (std::strcmp(env, "off") == 0) {
        g_level = LogLevel::Off;
    }
}

}  // namespace

void log_set_level(LogLevel level) {
    g_level = level;
    g_level_initialised = true;
}

LogLevel log_current_level() {
    if (!g_level_initialised) {
        init_level_from_env();
    }
    return g_level;
}

void log_set_tag(const char* tag) {
    if (tag == nullptr) {
        return;
    }
    std::snprintf(g_tag, sizeof(g_tag), "%s", tag);
}

void log_write(LogLevel level, const char* fmt, ...) {
    if (static_cast<int>(level) < static_cast<int>(log_current_level())) {
        return;
    }

    // Warnings and errors go to stderr so a server run can be piped with
    // stdout carrying only the interesting lines.
    std::FILE* out = (level >= LogLevel::Warn) ? stderr : stdout;

    std::fprintf(out, "[%8.3f] %-6s %-6s ", uptime_seconds(), g_tag,
                 level_name(level));

    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(out, fmt, args);
    va_end(args);

    std::fputc('\n', out);

    // Always flush. When stdout is a pipe or a file — which is exactly how the
    // server is run in production — it is fully buffered, so without this the log
    // stays invisible until the process exits cleanly. A server killed by a signal
    // would lose every line explaining why it was misbehaving. Line buffering via
    // setvbuf is not an option: Windows treats _IOLBF as full buffering.
    std::fflush(out);
}

}  // namespace core
