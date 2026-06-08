/**
 * @file logger.h
 * @brief Logging system for the K1520 emulator with a compile-time ceiling and
 *        a runtime-adjustable, dynamically gated output level.
 *
 * Two independent dimensions control whether a log line is emitted:
 *
 *  1. **Compile-time ceiling** (`LOG_LEVEL`, default INFO).  Call sites above the
 *     ceiling are removed by the preprocessor → zero overhead.  Build the
 *     diagnostic targets (boot_trace, the LOG_LEVEL=5 `build_trace/` dir) with
 *     `-DLOG_LEVEL=5` so every call site is compiled in and can be enabled at
 *     runtime.  Release builds keep a low ceiling for zero cost.
 *       -DLOG_LEVEL=0: OFF   1: ERROR  2: WARN  3: INFO  4: DEBUG  5: TRACE
 *
 *  2. **Runtime effective level**, evaluated cheaply on every macro via
 *     `Logger::shouldLog()` (an atomic int read — when the level is off the
 *     printf arguments are not even evaluated).  The effective level is the max
 *     of three sources:
 *       - the **base level** (`setBaseLevel`, default = compile ceiling),
 *       - any active **scoped boost** (RAII `LogScope` / `K1520_LOG_BOOST`),
 *       - any firing **gate** (a PC range or a cycle window, see below).
 *
 * Dynamic gating lets you run quietly (e.g. base = ERROR) and raise the level
 * only where it matters, so trace files stay small and focused:
 *
 *   auto& lg = k1520::logging::Logger::instance();
 *   lg.setBaseLevel(Level::ERROR);
 *   lg.addPCGate(0x1F7D, 0x2060, Level::TRACE);   // boost while PC is in the
 *                                                 // 3rd-stage read routine
 *   lg.addCycleGate(40'000'000, 41'000'000, Level::DEBUG);  // …or in a window
 *
 * The emulator run loop calls `Logger::instance().update(cycle, pc1, pc2)` once
 * per instruction; when no gates are registered this is a single early-out.
 *
 * Usage of the emit macros is unchanged:
 *   LOG_ERROR("BUS", "Failed to mount disk: %s", path);
 *   LOG_TRACE("Z80", "PC=0x%04X", pc);
 */

#pragma once

#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <atomic>
#include <string>
#include <vector>

// Define LOG_LEVEL (compile-time ceiling) if not set; default is INFO.
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

/** @brief Sentinel for "no PC" (used for the optional second CPU PC). */
constexpr uint32_t kNoPC = 0xFFFFFFFFu;

/**
 * @class Logger
 * @brief Central logging interface with runtime level + dynamic gating.
 *
 * Singleton; get the instance via Logger::instance().  The fast path
 * (shouldLog) reads a static atomic cache so emit macros stay cheap.
 */
class Logger {
public:
    static Logger& instance();

    // ── Fast path ────────────────────────────────────────────────────────────
    /**
     * @brief True if @p level is currently emitted (runtime effective level).
     * Inline, lock-free; evaluated by every LOG_* macro before formatting.
     */
    static bool shouldLog(Level level) {
        return static_cast<int>(level) <= s_effective_level.load(std::memory_order_relaxed);
    }

    /** @brief Current effective level (max of base, scopes and firing gates). */
    static Level effectiveLevel() {
        return static_cast<Level>(s_effective_level.load(std::memory_order_relaxed));
    }

    // ── Emit ─────────────────────────────────────────────────────────────────
    /**
     * @brief Log a message at @p level with source location.
     * @param level    Log level
     * @param category Category/subsystem (e.g. "BUS", "K7024", "Z80")
     * @param file     Source file name (__FILE__)
     * @param line     Source line number (__LINE__)
     * @param format   Printf-style format string
     */
    void log(Level level, const char* category,
             const char* file, int line,
             const char* format, ...);

    // ── Output sink ────────────────────────────────────────────────────────────
    /**
     * @brief Set the output file path. Use "-" for stderr (default).
     * @return true if successful, false if the file could not be opened.
     */
    bool setOutputFile(const std::string& path);
    /** @brief Close output file if open (reverts to stderr). */
    void closeOutputFile();

    // ── Runtime base level ─────────────────────────────────────────────────────
    /** @brief Set the runtime base (floor) level; recomputes effective level. */
    void setBaseLevel(Level level);
    /** @brief Current base level. */
    Level baseLevel() const { return base_level_; }

    // ── Dynamic gates ──────────────────────────────────────────────────────────
    /**
     * @brief Raise the effective level to @p level while either CPU PC is in
     *        the inclusive range [lo, hi].  Repeatable; gates OR together.
     */
    void addPCGate(uint32_t lo, uint32_t hi, Level level);
    /**
     * @brief Raise the effective level to @p level while the cycle counter is
     *        in the inclusive window [from, to].  Repeatable; gates OR together.
     */
    void addCycleGate(uint64_t from, uint64_t to, Level level);
    /** @brief Remove all PC and cycle gates. */
    void clearGates();
    /** @brief True if any gate is registered (drives update()'s early-out). */
    bool hasGates() const { return !pc_gates_.empty() || !cycle_gates_.empty(); }

    /**
     * @brief Per-instruction hook from the run loop: evaluates gates for the
     *        current cycle and CPU PC(s) and updates the effective level.
     *        Cheap no-op when no gates are registered.
     * @param cycle Absolute cycle counter.
     * @param pc1   Primary CPU PC (ZVE1).
     * @param pc2   Optional secondary CPU PC (ZVE2); kNoPC if unused.
     */
    void update(uint64_t cycle, uint32_t pc1, uint32_t pc2 = kNoPC);

    // ── Scoped boost (RAII) ────────────────────────────────────────────────────
    /** @brief Push a scoped boost level; restored by popScope(). LIFO. */
    void pushScope(Level level);
    /** @brief Pop the most recent scoped boost. */
    void popScope();

    /** @brief Parse "off|error|warn|info|debug|trace" or "0".."5". */
    static Level levelFromString(const std::string& s, Level fallback = Level::INFO);
    static const char* levelName(Level level);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void recompute();   // effective = max(base, scopes, gate_level_)

    struct PCGate    { uint32_t lo, hi; Level level; };
    struct CycleGate { uint64_t from, to; Level level; };

    FILE* output_file_ = nullptr;
    bool  owns_file_   = false;

    Level base_level_  = static_cast<Level>(LOG_LEVEL);
    Level gate_level_  = Level::OFF;   // contribution of currently-firing gates
    std::vector<Level>     scope_stack_;
    std::vector<PCGate>    pc_gates_;
    std::vector<CycleGate> cycle_gates_;

    // Effective level cache for the lock-free shouldLog() fast path.
    static std::atomic<int> s_effective_level;
};

/**
 * @class LogScope
 * @brief RAII helper: raise the effective level on entry, restore on exit.
 *        Use at the top of a function/block to trace just that region.
 */
class LogScope {
public:
    explicit LogScope(Level level) { Logger::instance().pushScope(level); }
    ~LogScope() { Logger::instance().popScope(); }
    LogScope(const LogScope&) = delete;
    LogScope& operator=(const LogScope&) = delete;
};

} // namespace k1520::logging

// Convenience macros - these are only defined if logging is enabled
// Helper: strip path prefix from __FILE__ at compile time to keep log output compact
#ifdef __linux__
#  define LOG_FILENAME ((__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__))
#else
#  define LOG_FILENAME __FILE__
#endif

// Internal: emit only if both the compile ceiling (handled by the #if below)
// and the runtime effective level allow it. The shouldLog() guard short-circuits
// before the variadic arguments are evaluated, so disabled logs cost ~one load.
#define K1520_LOG_EMIT(LVL, category, fmt, ...)                                   \
    do {                                                                          \
        if (::k1520::logging::Logger::shouldLog(LVL))                             \
            ::k1520::logging::Logger::instance().log(                             \
                LVL, category, LOG_FILENAME, __LINE__, fmt, ##__VA_ARGS__);       \
    } while (0)

#if LOG_LEVEL >= 1
#define LOG_ERROR(category, fmt, ...) K1520_LOG_EMIT(::k1520::logging::Level::ERROR, category, fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(category, fmt, ...) (void)0
#endif

#if LOG_LEVEL >= 2
#define LOG_WARN(category, fmt, ...) K1520_LOG_EMIT(::k1520::logging::Level::WARN, category, fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(category, fmt, ...) (void)0
#endif

#if LOG_LEVEL >= 3
#define LOG_INFO(category, fmt, ...) K1520_LOG_EMIT(::k1520::logging::Level::INFO, category, fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(category, fmt, ...) (void)0
#endif

#if LOG_LEVEL >= 4
#define LOG_DEBUG(category, fmt, ...) K1520_LOG_EMIT(::k1520::logging::Level::DEBUG, category, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(category, fmt, ...) (void)0
#endif

#if LOG_LEVEL >= 5
#define LOG_TRACE(category, fmt, ...) K1520_LOG_EMIT(::k1520::logging::Level::TRACE, category, fmt, ##__VA_ARGS__)
#else
#define LOG_TRACE(category, fmt, ...) (void)0
#endif

// RAII scoped boost: raise the effective level for the rest of the enclosing
// scope (e.g. a function), then restore. Compiled out below the ceiling.
#if LOG_LEVEL >= 1
#define K1520_LOG_BOOST(LVL) ::k1520::logging::LogScope _k1520_log_scope_##__LINE__(LVL)
#else
#define K1520_LOG_BOOST(LVL) (void)0
#endif
