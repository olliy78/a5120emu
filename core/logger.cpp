/**
 * @file logger.cpp
 * @brief Implementation of the K1520 emulator logging system.
 * 
 * Provides thread-safe logging with compile-time level control.
 */

#include "logger.h"
#include <ctime>
#include <mutex>

namespace k1520::logging {

Logger& Logger::instance() {
    static Logger instance_;
    return instance_;
}

Logger::Logger() : output_file_(stderr), owns_file_(false) {}

Logger::~Logger() {
    closeOutputFile();
}

void Logger::log(Level level, const char* category, const char* format, ...) {
#if LOG_LEVEL == 0
    (void)level;
    (void)category;
    (void)format;
    return;
#else
    if (static_cast<int>(level) > LOG_LEVEL) {
        return;
    }

    static std::mutex log_mutex;
    std::lock_guard<std::mutex> guard(log_mutex);

    FILE* out = output_file_ ? output_file_ : stderr;

    // Format timestamp
    std::time_t now = std::time(nullptr);
    std::tm* local_time = std::localtime(&now);
    char time_buffer[32];
    std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", local_time);

    // Level string
    const char* level_str = "?????";
    switch (level) {
        case Level::ERROR: level_str = "ERROR"; break;
        case Level::WARN:  level_str = "WARN "; break;
        case Level::INFO:  level_str = "INFO "; break;
        case Level::DEBUG: level_str = "DEBUG"; break;
        case Level::TRACE: level_str = "TRACE"; break;
        case Level::OFF:   level_str = "OFF  "; break;
    }

    // Print header
    std::fprintf(out, "[%s] %-5s [%-8s] ", time_buffer, level_str, category);

    // Print user message
    va_list args;
    va_start(args, format);
    std::vfprintf(out, format, args);
    va_end(args);

    // Newline
    std::fputc('\n', out);
    std::fflush(out);
#endif
}

bool Logger::setOutputFile(const std::string& path) {
    if (path == "-") {
        closeOutputFile();
        output_file_ = stderr;
        owns_file_ = false;
        return true;
    }

    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) {
        return false;
    }

    closeOutputFile();
    output_file_ = f;
    owns_file_ = true;
    return true;
}

void Logger::closeOutputFile() {
    if (output_file_ && owns_file_) {
        std::fclose(output_file_);
        output_file_ = nullptr;
        owns_file_ = false;
    }
}

} // namespace k1520::logging
