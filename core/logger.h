/**
 * @file logger.h
 * @brief Logging system for the K1520 emulator with compile-time level control.
 * 
 * Provides a lightweight logging framework that can be completely disabled at
 * compile time with zero overhead. Log levels can be controlled via compiler flags:
 * - `-DLOG_LEVEL=0`: OFF (no logging)
 * - `-DLOG_LEVEL=1`: ERROR
 * - `-DLOG_LEVEL=2`: WARN
 * - `-DLOG_LEVEL=3`: INFO (default)
 * - `-DLOG_LEVEL=4`: DEBUG
 * - `-DLOG_LEVEL=5`: TRACE
 * 
 * Usage:
 *   LOG_ERROR("Failed to mount disk: %s", path);
 *   LOG_DEBUG("CPU PC=0x%04X AF=0x%04X", pc, af);
 *   LOG_TRACE("Bus read at 0x%04X = 0x%02X", addr, data);
 * 
 * When compiled with LOG_LEVEL=0, all logging code is eliminated by preprocessor.
 */

#pragma once

#include <cstdio>
#include <cstdarg>
#include <string>

// Define LOG_LEVEL if not set; default is INFO
#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

// Ensure LOG_LEVEL is a valid value
static_assert(LOG_LEVEL >= 0 && LOG_LEVEL <= 5, "LOG_LEVEL must be 0-5");

namespace k1520::logging {

enum class Level {
    OFF   = 0,
    ERROR = 1,
    WARN  = 2,
    INFO  = 3,
    DEBUG = 4,
    TRACE = 5,
};

/**
 * @class Logger
 * @brief Central logging interface for the K1520 emulator.
 * 
 * Singleton pattern. Get instance via Logger::instance().
 */
class Logger {
public:
    static Logger& instance();

    /**
     * @brief Log a message at the specified level with source location.
     * @param level    Log level
     * @param category Category/subsystem (e.g. "BUS", "K7024", "Z80")
     * @param file     Source file name (__FILE__)
     * @param line     Source line number (__LINE__)
     * @param format   Printf-style format string
     * @param ...      Variable arguments
     */
    void log(Level level, const char* category,
             const char* file, int line,
             const char* format, ...);

    /**
     * @brief Set the output file path. Use "-" for stderr (default).
     * @param path File path or "-" for stderr
     * @return true if successful, false if file could not be opened
     */
    bool setOutputFile(const std::string& path);

    /**
     * @brief Close output file if open (reverts to stderr).
     */
    void closeOutputFile();

    /**
     * @brief Check if a given level is enabled (for conditional logging).
     * @param level Log level to check
     * @return true if this level will be logged
     */
    static constexpr bool isEnabled(Level level) {
        return static_cast<int>(level) <= LOG_LEVEL;
    }

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    FILE* output_file_ = nullptr;
    bool  owns_file_   = false;
};

} // namespace k1520::logging

// Convenience macros - these are only defined if logging is enabled
// Helper: strip path prefix from __FILE__ at compile time to keep log output compact
#ifdef __linux__
#  define LOG_FILENAME ((__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__))
#else
#  define LOG_FILENAME __FILE__
#endif

#if LOG_LEVEL >= 1
#define LOG_ERROR(category, fmt, ...) \
    ::k1520::logging::Logger::instance().log( \
        ::k1520::logging::Level::ERROR, category, LOG_FILENAME, __LINE__, fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(category, fmt, ...) (void)0
#endif

#if LOG_LEVEL >= 2
#define LOG_WARN(category, fmt, ...) \
    ::k1520::logging::Logger::instance().log( \
        ::k1520::logging::Level::WARN, category, LOG_FILENAME, __LINE__, fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(category, fmt, ...) (void)0
#endif

#if LOG_LEVEL >= 3
#define LOG_INFO(category, fmt, ...) \
    ::k1520::logging::Logger::instance().log( \
        ::k1520::logging::Level::INFO, category, LOG_FILENAME, __LINE__, fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(category, fmt, ...) (void)0
#endif

#if LOG_LEVEL >= 4
#define LOG_DEBUG(category, fmt, ...) \
    ::k1520::logging::Logger::instance().log( \
        ::k1520::logging::Level::DEBUG, category, LOG_FILENAME, __LINE__, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(category, fmt, ...) (void)0
#endif

#if LOG_LEVEL >= 5
#define LOG_TRACE(category, fmt, ...) \
    ::k1520::logging::Logger::instance().log( \
        ::k1520::logging::Level::TRACE, category, LOG_FILENAME, __LINE__, fmt, ##__VA_ARGS__)
#else
#define LOG_TRACE(category, fmt, ...) (void)0
#endif
