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
#include <array>
#include <vector>
#include <functional>

class A5120Machine {
public:
    /**
     * @brief Laufzeit-Konfiguration der Maschine (per C-API / später GUI / Config-Datei).
     *
     * Default = A5120-Standard-Bürokonfiguration: 4× 5,25"-MFM-Laufwerke (K5601).
     * Wird über die C-API (`k1520_create_configured`) oder direkt (Tools) gesetzt.
     */
    struct Config {
        /// DriveProfile-Namen je K5122-Slot (siehe builtinDriveProfile). Default: 4× K5601.
        std::array<std::string, 4> drive_profiles = {"K5601", "K5601", "K5601", "K5601"};
    };

    /** @brief Construct with the default configuration (4× 5,25"-MFM, K5601). */
    A5120Machine();
    /** @brief Construct and wire a full A5120 machine instance.
     *  @param cfg Laufwerksbestückung etc. (per C-API/GUI/Config-Datei). */
    explicit A5120Machine(const Config& cfg);
    ~A5120Machine() = default;

    // Lifecycle
    /** @brief Power-on sequence with bootstrap ROM enabled. */
    void powerOn();
    /** @brief Reset sequence with bootstrap ROM re-enabled. */
    void reset();
    /** @brief Request emulation stop (breaks out of run() after the current instr). */
    void stop() { stop_.store(true); }
    /** @brief Clear a pending stop request so the next run() proceeds (debugger resume). */
    void clearStop() { stop_.store(false); }
    /** @brief ZVE1 (main CPU) total executed clock cycles — monotonic timeline for tools. */
    uint64_t cpuCycles() const { return zre_.cpu().cycles; }

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
    /** @brief Mutable ZVE1 core for debuggers (register edit, flag inspection). */
    Z80& cpuDebug() { return zre_.cpu(); }
    /** @brief Mutable ZVE2 core for debuggers (register edit, flag inspection). */
    Z80& zve2Debug() { return zre_.zve2(); }

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
    /** @brief K5122 floppy-controller state snapshot (debugger `dev` command). */
    K5122::DebugState k5122State() const { return afs_.debugState(); }
    /** @brief K2526 system-CTC state snapshot (debugger `dev ctc`). */
    Z80CTC::DebugState ctcState()   { return zre_.ctc().debugState(); }
    /** @brief K2526 BS-PIO state snapshot (debugger `dev pio`). */
    Z80PIO::DebugState bsPioState() { return zre_.bsPio().debugState(); }
    /** @brief K8025 keyboard/printer SIO (A32) state snapshot (debugger `dev sio`). */
    Z80SIO::DebugState kbdSioState()  { return ass_.sioA32().debugState(); }
    /** @brief K8025 DFÜ SIO (A33) state snapshot (debugger `dev sio2`). */
    Z80SIO::DebugState dfueSioState() { return ass_.sioA33().debugState(); }
    /** @brief Which CPU is the current bus master — true=ZVE2, false=ZVE1. Valid inside a
     *  bus-trace callback to attribute a memory/IO access to the CPU that issued it. */
    bool     busMasterIsZVE2() const { return bus_master_zve2_; }
    /** @brief PC of the current bus-master CPU (ZVE2's PC during a ZVE2 step, else ZVE1's). */
    uint16_t busMasterPC() const { return bus_master_zve2_ ? zre_.zve2PC() : zre_.cpuPC(); }
    /** @brief Install a per-instruction trace callback on ZVE2. */
    void setZVE2TraceCallback(std::function<void(const Z80&)> cb) {
        zre_.setZVE2TraceCallback(std::move(cb));
    }

    // Debug
    std::string lastError() const { return last_error_; }

    // ─── Snapshot / reverse-debugging support ──────────────────────────────────
    /**
     * @brief Reproducible machine snapshot for the debugger (snap/restore, reverse-step).
     *
     * Contains the full 64 KB main RAM, both Z80 register files (registers only —
     * the access callbacks are *not* part of the snapshot), the run-loop coordination
     * flags, the boot-ROM mapping, the keyboard subsystem (CTC/BS-PIO/baud-CTC/SIO/
     * K7637) and the floppy controller (K5122 PIOs + per-drive head position). So a
     * restore resumes with a working keyboard AND disk access (head on the right
     * track). NOT captured: the mounted disk images themselves (mounted separately)
     * and the K7024 screen VRAM. Restoring in the middle of an active DMA / timer
     * phase may still drift once execution resumes.
     */
    struct MachineSnapshot {
        struct Z80Regs {
            uint16_t AF=0,BC=0,DE=0,HL=0,IX=0,IY=0,PC=0,SP=0;
            uint16_t AF_=0,BC_=0,DE_=0,HL_=0;
            uint8_t  I=0,R=0,IM=0;
            bool     IFF1=false,IFF2=false,halted=false;
            uint64_t cycles=0;
        };
        std::array<uint8_t,65536> ram{};
        Z80Regs  zve1, zve2;
        bool     rom_enabled    = false;
        bool     busrq_active   = false;
        bool     dma_progress   = false;
        bool     bus_master_zve2= false;
        uint64_t total_cycles   = 0;
        // Serialised device-internal state (keyboard SIO + K7637). Empty when a
        // legacy (v1) snapshot without device state is restored. Captured so a
        // loadstate resumes with a working keyboard. See captureState().
        std::vector<uint8_t> device_state;
    };
    /** @brief Capture the current machine state into @p s. */
    void captureState(MachineSnapshot& s) const;
    /**
     * @brief Restore a previously captured snapshot (RAM + both CPUs + ROM mapping).
     *
     * Also reproduces the boot-ROM mapping, so a state saved post-ROM resumes
     * correctly even into a freshly powered machine. The keyboard subsystem
     * (system CTC + BS-PIO + baud CTC + keyboard SIO + K7637) and the floppy
     * controller (K5122 PIOs + per-drive head position) ARE captured/restored, so
     * keyboard input AND disk access work after a loadstate. Not captured: the
     * mounted disk images and the K7024 screen VRAM.
     * @return always true (the snapshot is fully applied).
     */
    bool restoreState(const MachineSnapshot& s);

    /**
     * @brief Save the current machine state to a file (captureState + binary serialise).
     *
     * Lets a host tool boot once, run to a point of interest and persist that state,
     * then resume cheaply later via loadState() — instead of re-booting each time.
     * Format: 8-byte magic+version, then RAM + both register files + flags. Same
     * RAM+CPU scope (and ROM-mapping caveat) as captureState/restoreState.
     * @return false if the file cannot be written.
     */
    bool saveState(const std::string& path) const;
    /**
     * @brief Load a machine state previously written by saveState().
     * @return true if applied; false if the file is missing or not a valid state file
     *         (then the machine is unchanged).
     */
    bool loadState(const std::string& path);

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
    bool bus_master_zve2_  = false;   // which CPU is currently stepping (for bus-trace
                                      // attribution): true while ZVE2 steps, false for ZVE1

    // Monotonic cycle counter across all run() calls, fed to the Logger's gate
    // evaluation (cycle windows) once per instruction.
    uint64_t total_cycles_ = 0;

    std::string last_error_;
};
