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

    // ─── ZVE2 (DMA-CPU) diagnostics ──────────────────────────────────────────
    /** @brief Current PC of the ZVE2 (DMA Z80). */
    uint16_t zve2PC() const { return zre_.zve2PC(); }
    uint16_t zve2SP() const { return zre_.zve2().SP; }
    uint16_t zve2AF() const { return zre_.zve2().AF; }
    uint16_t zve2BC() const { return zre_.zve2().BC; }
    uint16_t zve2DE() const { return zre_.zve2().DE; }
    uint16_t zve2HL() const { return zre_.zve2().HL; }
    /** @brief True while ZVE2 is held in reset (port 04H bit0=0). */
    bool     isZVE2InReset() const { return zre_.isZVE2InReset(); }
    /** @brief True while ZVE2 is stalled by /WAIT-ZVE2 (BS-PIO B3=0). */
    bool     isZVE2Waiting() const { return zre_.isZVE2Waiting(); }
    /** @brief True while /BUSRQ is asserted (ZVE2/DMA owns the bus). */
    bool     isBUSRQ() const { return bus_.isBUSRQ(); }
    /** @brief Install a per-instruction trace callback on ZVE2. */
    void setZVE2TraceCallback(std::function<void(const Z80&)> cb) {
        zre_.setZVE2TraceCallback(std::move(cb));
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
    K5122         afs_;       // slot 2: floppy (formatagnostischer Streaming-Controller)

    K7637         kbd_;

    std::vector<DiskFormat> disk_formats_;

    std::atomic<bool>  stop_{false};

    mutable std::mutex disk_mutex_;
    mutable std::mutex key_mutex_;
    std::deque<KeyEvent> key_queue_;

    int boot_trace_count_ = 0;

    // ZVE2 DMA completion tracking (see run()): a chained bootloader re-runs the
    // DMA, and the boot ROM's [0x03F8] done-flag still holds 3 from the previous
    // round when the next starts. We therefore detect the 0→3 transition per
    // round, not the level.
    bool busrq_active_     = false;   // /BUSRQ was asserted last iteration
    bool dma_saw_progress_ = false;   // [0x03F8] observed != 3 since this round began

    // Monotonic cycle counter across all run() calls, fed to the Logger's gate
    // evaluation (cycle windows) once per instruction.
    uint64_t total_cycles_ = 0;

    std::string last_error_;
};
