#pragma once
#ifndef LOG_H
#define LOG_H

#include "internal.h"

// ── Log levels ────────────────────────────────────────────────────────────────
enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3, Off = 4 };

// ── Runtime log sink ─────────────────────────────────────────────────────────
struct LogConfig {
    LogLevel  min_level   = LogLevel::Debug;
    FILE*     file        = nullptr;   // optional file output (set via logSetFile)
    bool      timestamps  = true;
    bool      to_stderr   = true;
#ifdef _WIN32
    bool      debugger    = true;      // OutputDebugString on Windows
#endif
};

inline LogConfig& logGetConfig() { static LogConfig cfg; return cfg; }

inline void logSetFile(FILE* f)            { logGetConfig().file      = f;    }
inline void logSetLevel(LogLevel lv)       { logGetConfig().min_level = lv;   }
inline void logSetTimestamps(bool on)      { logGetConfig().timestamps = on;  }

inline void logWrite(LogLevel level, const char* prefix, const char* fmt, ...) {
    LogConfig& cfg = logGetConfig();
    if (level < cfg.min_level) return;

    char msg[1024];
    va_list ap; va_start(ap, fmt);
    rt_vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char line[1152];
    if (cfg.timestamps) {
#ifdef _WIN32
        SYSTEMTIME st;
        GetLocalTime(&st);
        char ts[32];
        rt_snprintf(ts, sizeof(ts), "%02d:%02d:%02d", (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
#else
        const char* ts = "00:00:00";
#endif
        rt_snprintf(line, sizeof(line), "[%s]%s %s\n", ts, prefix, msg);
    } else {
        rt_snprintf(line, sizeof(line), "%s %s\n", prefix, msg);
    }

    if (cfg.to_stderr)  fputs(line, stderr);
    if (cfg.file)       fputs(line, cfg.file);
#ifdef _WIN32
    if (cfg.debugger)   OutputDebugStringA(line);
#endif
}

// ── Macros ────────────────────────────────────────────────────────────────────
#define LOG_DEBUG(msg, ...) logWrite(LogLevel::Debug, "[DEBUG]", msg, ##__VA_ARGS__)
#define LOG_INFO(msg,  ...) logWrite(LogLevel::Info,  "[INFO] ", msg, ##__VA_ARGS__)
#define LOG_WARN(msg,  ...) logWrite(LogLevel::Warn,  "[WARN] ", msg, ##__VA_ARGS__)
#define LOG_ERROR(msg, ...) logWrite(LogLevel::Error, "[ERROR]", msg, ##__VA_ARGS__)

#endif
