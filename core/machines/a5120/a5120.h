/**
 * @file a5120.h
 * @brief Top-level machine integration for the Robotron A5120 configuration.
 *
 * This class wires CPU, bus, cards, and peripherals into one runnable machine
 * instance and provides thread-safe methods used by the C API and Python GUI.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/bus/koppelbus.h"
#include "core/cards/k2526/k2526.h"
#include "core/cards/k3526/k3526.h"
#include "core/cards/k7024/k7024.h"
#include "core/cards/k8025/k8025.h"
#include "core/cards/k5122/k5122.h"
#include "core/peripherals/k7637/k7637.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include <atomic>
#include <mutex>
#include <string>
#include <deque>
#include <functional>

class A5120Machine {
public:
    /** @brief Construct and wire a full A5120 machine instance. */
    A5120Machine();
    ~A5120Machine() = default;

    // Lifecycle
    /** @brief Power-on sequence with bootstrap ROM enabled. */
    void powerOn();
    /** @brief Reset sequence with bootstrap ROM re-enabled. */
    void reset();
    /** @brief Request emulation stop. */
    void stop() { stop_.store(true); }

    // Run up to max_cycles CPU cycles. Returns cycles actually executed.
    /** @brief Execute up to max_cycles CPU cycles and return consumed cycles. */
    int  run(int max_cycles);

    // Disk management (thread-safe)
    bool mountDisk(int drive, const std::string& path,
                   const std::string& format_name, bool write_protect);
    bool unmountDisk(int drive);
    bool isDiskActive(int drive) const;
    bool isDiskWriteProtected(int drive) const;
    /** @brief Return transient drive activity LED state for GUI display. */
    bool isDiskLedOn(int drive) const;
    void setDiskWriteProtect(int drive, bool wp);

    // Keyboard (enqueued thread-safely, consumed in run())
    void keyPress(uint32_t qt_keycode, bool shift, bool ctrl);
    void keyRelease(uint32_t qt_keycode);

    // Framebuffer
    const uint8_t* framebuffer() const;
    int  fbWidth()  const { return 640; }
    int  fbHeight() const { return 288; }
    bool fbDirty()  const { return screen_.fbDirty(); }
    void fbClearDirty()   { screen_.fbClearDirty(); }

    // Console (CLI) mode
    void setConsoleMode(bool on) { screen_.setConsoleMode(on); }
    bool consolePoll(int& x, int& y, char& ch) {
        return screen_.pollTextChange(x, y, ch);
    }

    // Serial callbacks (DFÜ, printer)
    using SerialCb = std::function<void(uint8_t)>;
    void setDFUECallback(SerialCb cb);
    void setPrinterCallback(SerialCb cb);
    void dfueSend(uint8_t byte);

    // Debug bus passthrough helpers.
    /** @brief Read memory through the machine bus for diagnostics. */
    uint8_t memReadDebug(uint16_t addr) { return bus_.memRead(addr); }
    /** @brief Write memory through the machine bus for diagnostics. */
    void memWriteDebug(uint16_t addr, uint8_t data) { bus_.memWrite(addr, data); }
    /** @brief Read I/O port through the machine bus for diagnostics. */
    uint8_t ioReadDebug(uint8_t port) { return bus_.ioRead(port); }
    /** @brief Install a bus trace callback (io, is_read, addr, data). */
    void setBusTrace(K1520Bus::BusTrace cb) { bus_.setTraceCallback(std::move(cb)); }
    /** @brief Current PC of the ZVE1 (main Z80). */
    uint16_t cpuPC() const { return zre_.cpuPC(); }
    uint16_t cpuSP() const { return zre_.cpu().SP; }
    uint16_t cpuAF() const { return zre_.cpu().AF; }
    uint16_t cpuBC() const { return zre_.cpu().BC; }
    uint16_t cpuDE() const { return zre_.cpu().DE; }
    uint16_t cpuHL() const { return zre_.cpu().HL; }
    bool     isRomEnabled() const { return zre_.isRomEnabled(); }

    /** @brief Install a per-instruction trace callback on ZVE1. */
    void setCpuTraceCallback(std::function<void(const Z80&)> cb) {
        zre_.cpu().traceCallback = std::move(cb);
    }

    // Debug
    std::string lastError() const { return last_error_; }

private:
    void wireBackplane();

    struct KeyEvent { uint32_t keycode; bool shift, ctrl, is_press; };

    K1520Bus      bus_;
    Koppelbus     koppel_;

    K2526         zre_;       // slot 4: CPU card (enthält ZVE1)
    K3526         ops_;       // slot 1: RAM
    K7024         screen_;    // slot 5: video
    K8025         ass_;       // slot 3: serial
    K5122         afs_;       // slot 2: floppy

    K7637         kbd_;

    std::vector<DiskFormat> disk_formats_;

    std::atomic<bool>  stop_{false};

    mutable std::mutex disk_mutex_;
    mutable std::mutex key_mutex_;
    std::deque<KeyEvent> key_queue_;

    int boot_trace_count_ = 0;

    std::string last_error_;
};
