/**
 * @file logger.cpp
 * @brief Implementation of the K1520 emulator logging system.
 *
 * Thread-safe emission (mutex around the sink); a lock-free atomic cache for the
 * effective level so the shouldLog() fast path is cheap. Runtime base level plus
 * dynamic PC/cycle gates and RAII scoped boosts adjust the effective level.
 */

#include "logger.h"
#include <ctime>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <mutex>

namespace k1520::logging {

// Effective-level cache (read by shouldLog on the hot path). Initialised to the
// compile ceiling so default behaviour is unchanged until setBaseLevel/gates.
std::atomic<int> Logger::s_effective_level{LOG_LEVEL};

Logger& Logger::instance() {
    static Logger instance_;
    return instance_;
}

Logger::Logger() : output_file_(stderr), owns_file_(false) {
    recompute();
}

Logger::~Logger() {
    closeOutputFile();
}

void Logger::log(Level level, const char* category,
                 const char* file, int line,
                 const char* format, ...) {
#if LOG_LEVEL == 0
    (void)level; (void)category; (void)file; (void)line; (void)format;
    return;
#else
    // Defensive: callers normally gate via shouldLog() in the macro, but a direct
    // call must still honour the runtime effective level.
    if (!shouldLog(level)) {
        return;
    }

    static std::mutex log_mutex;
    std::lock_guard<std::mutex> guard(log_mutex);

    FILE* out = output_file_ ? output_file_ : stderr;

    // Format timestamp with milliseconds
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    std::tm* local_time = std::localtime(&ts.tv_sec);
    char time_buffer[32];
    std::strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", local_time);
    char full_time[48];
    std::snprintf(full_time, sizeof(full_time), "%s.%03ld", time_buffer, ts.tv_nsec / 1000000L);

    // Print header: time | level | category | file:line |
    std::fprintf(out, "[%s] %s [%-8s] %s:%d | ",
                 full_time, levelName(level), category, file, line);

    // Print user message
    va_list args;
    va_start(args, format);
    std::vfprintf(out, format, args);
    va_end(args);

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

// ── Effective level computation ────────────────────────────────────────────────

void Logger::recompute() {
    int eff = static_cast<int>(base_level_);
    for (Level s : scope_stack_)
        eff = std::max(eff, static_cast<int>(s));
    eff = std::max(eff, static_cast<int>(gate_level_));
    // Never exceed the compile ceiling — call sites above it do not exist.
    eff = std::min(eff, static_cast<int>(LOG_LEVEL));
    s_effective_level.store(eff, std::memory_order_relaxed);
}

void Logger::setBaseLevel(Level level) {
    base_level_ = level;
    recompute();
}

void Logger::addPCGate(uint32_t lo, uint32_t hi, Level level) {
    if (lo > hi) std::swap(lo, hi);
    pc_gates_.push_back({lo, hi, level});
}

void Logger::addCycleGate(uint64_t from, uint64_t to, Level level) {
    if (from > to) std::swap(from, to);
    cycle_gates_.push_back({from, to, level});
}

void Logger::clearGates() {
    pc_gates_.clear();
    cycle_gates_.clear();
    gate_level_ = Level::OFF;
    recompute();
}

void Logger::update(uint64_t cycle, uint32_t pc1, uint32_t pc2) {
    if (pc_gates_.empty() && cycle_gates_.empty())
        return;   // hot-path early-out: nothing to evaluate

    int g = static_cast<int>(Level::OFF);
    for (const auto& gate : pc_gates_) {
        const bool hit = (pc1 >= gate.lo && pc1 <= gate.hi) ||
                         (pc2 != kNoPC && pc2 >= gate.lo && pc2 <= gate.hi);
        if (hit) g = std::max(g, static_cast<int>(gate.level));
    }
    for (const auto& gate : cycle_gates_) {
        if (cycle >= gate.from && cycle <= gate.to)
            g = std::max(g, static_cast<int>(gate.level));
    }

    const Level newgl = static_cast<Level>(g);
    if (newgl != gate_level_) {       // only recompute on a transition
        gate_level_ = newgl;
        recompute();
    }
}

void Logger::pushScope(Level level) {
    scope_stack_.push_back(level);
    recompute();
}

void Logger::popScope() {
    if (!scope_stack_.empty())
        scope_stack_.pop_back();
    recompute();
}

// ── Helpers ─────────────────────────────────────────────────────────────────────

const char* Logger::levelName(Level level) {
    switch (level) {
        case Level::ERROR: return "ERROR";
        case Level::WARN:  return "WARN ";
        case Level::INFO:  return "INFO ";
        case Level::DEBUG: return "DEBUG";
        case Level::TRACE: return "TRACE";
        case Level::OFF:   return "OFF  ";
    }
    return "?????";
}

Level Logger::levelFromString(const std::string& s, Level fallback) {
    std::string t;
    t.reserve(s.size());
    for (char c : s) t.push_back(static_cast<char>(std::tolower(c)));

    if (t == "off"   || t == "0") return Level::OFF;
    if (t == "error" || t == "1") return Level::ERROR;
    if (t == "warn"  || t == "2") return Level::WARN;
    if (t == "info"  || t == "3") return Level::INFO;
    if (t == "debug" || t == "4") return Level::DEBUG;
    if (t == "trace" || t == "5") return Level::TRACE;
    return fallback;
}

} // namespace k1520::logging
