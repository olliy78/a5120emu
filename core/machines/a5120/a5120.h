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
#include "core/peripherals/floppy_drive/disk_format.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
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
    /**
     * @brief Maschinenweite Taktuhr — Takte BEIDER CPUs (und der DMA-Wartetakte).
     *
     * Im Gegensatz zu cpuCycles() (nur ZVE1) kriecht sie nicht, während ZVE2 den Bus
     * hält und ZVE1 geparkt ist.  Werkzeuge, die "laufe N Takte" anbieten, sollten
     * darauf laufen — sonst wirkt ein ZVE2-lastiger Abschnitt wie ein Hänger.
     */
    uint64_t machineCycles() const { return total_cycles_; }

    // Run up to max_cycles CPU cycles. Returns cycles actually executed.
    /** @brief Execute up to max_cycles CPU cycles and return consumed cycles. */
    int  run(int max_cycles);

    // Disk management (thread-safe)
    bool mountDisk(int drive, const std::string& path,
                   const std::string& format_name, bool write_protect);
    /**
     * @brief Legt eine NEUE, GÜLTIG FORMATIERTE, leere Diskette an und mountet sie.
     *
     * Endung `.hfe` → formatiertes HFE (echte IDAM/DATA/CRC, Daten 0xE5); sonst `.img`
     * → 0xE5-Sektorimage.  @p format_name bestimmt die Geometrie; ist er LEER, wird das
     * laufwerkstyp-spezifische Standardformat des Slots gewählt (K5601→cpa800,
     * K5600.10→200K, K5600.20→400K, MF3200→308K/FM, MF6400→616K).  Das Aufzeichnungs-
     * verfahren folgt aus dem DriveProfile (reines FM-Laufwerk → FM, sonst MFM).
     * Überschreibt eine vorhandene Datei.  @see DiskImage::create
     */
    bool createDisk(int drive, const std::string& path,
                    const std::string& format_name, bool write_protect);

    /**
     * @brief Name des laufwerkstyp-spezifischen Standardformats für einen Slot.
     *
     * Das Format, das @ref createDisk bei leerem @p format_name wählt (K5601→cpa800,
     * K5600.10→200K, …).  Leerer String, wenn der Slot unbestückt ist.
     */
    std::string defaultFormatName(int drive) const;

    /**
     * @brief Formatnamen, die geometrisch auf das Laufwerk dieses Slots passen.
     *
     * Ein eingebautes Format passt, wenn seine Spurzahl ≤ profile.num_cyls und seine
     * Kopfzahl ≤ profile.num_heads ist (das Aufzeichnungsverfahren leitet createDisk
     * aus dem Laufwerk ab).  Das Standardformat des Slots steht an erster Stelle.
     * Leere Liste bei unbestücktem Slot.  Für die GUI-Formatauswahl.
     */
    std::vector<std::string> compatibleFormats(int drive) const;

    /** @brief Klartextbeschreibung eines Katalogformats (leer, wenn unbekannt). */
    std::string formatDescription(const std::string& format_name) const;

    /** @brief Geladener Formatkatalog (Diagnose: Quelldateien, übersprungene Formate). */
    const FormatCatalog& formatCatalog() const { return disk_formats_; }

    bool unmountDisk(int drive);
    bool isDiskActive(int drive) const;
    bool isDiskWriteProtected(int drive) const;
    /** @brief Return drive activity LED state (select OR motor) for GUI display. */
    bool isDiskLedOn(int drive) const;
    /** @brief Return the drive's spindle-motor state (/LCK from the 8212, port 0x18). */
    bool isMotorOn(int drive) const;
    /** @brief Return whether the read/write head is loaded (/HL, ctrl port A bit6). */
    bool isHeadLoaded() const;
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
    /** @brief K5122 Steuer-PIO (Ports 0x10–0x13) snapshot (debugger `dev pio k5122ctrl`). */
    Z80PIO::DebugState k5122CtrlPioState() const { return afs_.ctrlPio().debugState(); }
    /** @brief K5122 Daten-PIO (Ports 0x14–0x17) snapshot (debugger `dev pio k5122data`). */
    Z80PIO::DebugState k5122DataPioState() const { return afs_.dataPio().debugState(); }
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

    // ─── Interrupt-Diagnose (Debugger `ivt`, `bint`, `itrace`) ────────────────
    /**
     * @brief Eine einzelne Interruptquelle der Daisy-Chain (PIO-Port, CTC-Kanal, SIO-Kanal).
     *
     * Zusammen ergeben sie die Vektor-Landkarte, die ein Fremd-OS mit eigener
     * IM-2-Tabelle beim Hochlauf programmiert: Wer darf einen Interrupt auslösen,
     * mit welchem Vektor — und zeigt der zugehörige Tabelleneintrag ins Leere?
     */
    struct IntSource {
        std::string device;          ///< z.B. "K5122 ctrl-PIO A"
        uint8_t     vector  = 0xFF;  ///< programmierter Vektor (Basis bei SIO)
        bool        exact   = true;  ///< false: SIO — Subtyp-Bits variieren je Anlass
        bool        ie      = false; ///< Interrupterzeugung freigegeben
        bool        pending = false; ///< Anforderung steht an
        bool        ius     = false; ///< in Bedienung (IUS)
        bool        iei     = false; ///< von der Chain freigegeben
        int         chain   = 0;     ///< Position in der Daisy-Chain (0 = höchste Priorität)
    };
    /** @brief Alle Interruptquellen der Daisy-Chain in Prioritätsreihenfolge. */
    std::vector<IntSource> interruptSources() const;

    /** @brief Letzte Interrupt-Quittung des Busses (Vektor + Quellgerät, `SPURIOUS`-Fall). */
    const K1520Bus::IntAck& lastIntAck() const { return bus_.lastIntAck(); }

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
     * track), and the K7024 screen VRAM (so `screen`/framebuffer are correct after
     * a restore). NOT captured: the mounted disk images themselves (mounted
     * separately). Restoring in the middle of an active DMA / timer
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
        // Serialised device-internal state (keyboard SIO + K7637 + K5122 floppy +
        // K7024 VRAM). Empty when a legacy (v1) snapshot without device state is
        // restored. Captured so a loadstate resumes with a working keyboard, disk
        // access and a correct screen. See captureState().
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
     * controller (K5122 PIOs + per-drive head position) plus the K7024 screen VRAM
     * ARE captured/restored, so keyboard input, disk access AND the screen work
     * after a loadstate. Not captured: the mounted disk images (mounted separately).
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

    /** @brief Systemweiter /RESET (ZVE1 + alle peripheren Bausteine); s. .cpp. */
    void resetHardware();

    struct KeyEvent { uint32_t keycode; bool shift, ctrl, is_press; };

    K1520Bus      bus_;
    Koppelbus     koppel_;

    K2526         zre_;       // slot 4: CPU card (enthält ZVE1)
    K3526         ops_;       // slot 1: RAM
    K7024         screen_;    // slot 5: video
    K8025         ass_;       // slot 3: serial
    K5122         afs_;       // slot 2: floppy (formatagnostischer Streaming-Controller)

    K7637         kbd_;

    FormatCatalog disk_formats_;                  // aus data/formats.yaml (§8.6)
    std::array<DriveProfile, 4> drive_profiles_;  // Bestückung je Slot (für create-Default)

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
    bool prev_floppy_int_  = false;   // letzter K5122-Interruptzustand (zeitgetriebene
                                      // Index-IRQ) → Flankenerkennung markiert Chain dirty
    bool bus_master_zve2_  = false;   // which CPU is currently stepping (for bus-trace
                                      // attribution): true while ZVE2 steps, false for ZVE1

    // ── Os-gated „gehaltener Bus" für den SCPX-Laufzeit-Read (doc/analyse_scpx_com_load.md §9.4b) ──
    // Der Boot nutzt die Per-Byte-/BUSRQ-Drossel (getunt, s. project_per_byte_busrq_model);
    // die Laufzeit-.COM-Reads scheitern aber daran, weil ZVE1 in den Byte-Lücken mitläuft und
    // (1) den Kopf mit-steppt, (2) den Matcher-Mini-Stack per INT korrumpiert, (3) den
    // Warmstart-Vektor [0x0000] verfrüht restauriert.  Sobald der interaktive Prompt (ZVE1 @E079)
    // das erste Mal erreicht ist (os_running_), wird der Laufzeit-Read wie echte HW gefahren:
    // /BUSRQ über den GANZEN Transfer halten (nur ZVE2 liest), ausgelöst am read-eindeutigen
    // Poll-Wait (ZVE1 @E8B5 mit [EC0B]==E8B5 und aktivem Lese-Transfer), beendet, sobald ZVE2
    // das Ergebnis über [EC0B] signalisiert.  Während des Boots (os_running_==false) inaktiv →
    // keine Boot-Regression.
    bool os_running_       = false;   // ZVE1 hat den interaktiven Prompt (E079) erreicht
    bool held_read_active_ = false;   // gehaltener Laufzeit-Read läuft (ZVE1 eingefroren)
    // No-Progress-Watchdog für den gehaltenen Read: findet ZVE2 sein Sektor-IDAM nicht
    // (z. B. FM-Probe 0x87 auf einer MFM-Spur → Matcher E9C8 re-armt endlos), signalisiert es
    // NIE [EC0B]≠E8B5 → ZVE1 bliebe ewig @E8B5 eingefroren.  Auf echter HW terminiert hier der
    // Index-Interrupt (Record-not-found → [EC0B]=E998, FM/MFM-Retry).  Der wird beim gehaltenen
    // Read aber nie zugestellt (ZVE1 eingefroren, INT-Zustellung übersprungen).  Nach ~2 Umdrehungen
    // ohne Fortschritt geben wir daher den Bus frei (und sperren die Wieder-Engage bis [EC0B]
    // wechselt), sodass ZVE1 läuft, seinen Index-ISR nimmt und der Timeout/Retry greift — wie beim
    // Boot.  Verhindert das Einfrieren bei der FM/MFM-Erkennung einer nicht passenden Diskette.
    long long held_read_cycles_   = 0;      // Takte seit Engage des gehaltenen Reads
    bool      held_read_watchdog_ = false;  // Watchdog ausgelöst → Bus frei, kein Re-Engage bis Fortschritt

    // Monotonic cycle counter across all run() calls, fed to the Logger's gate
    // evaluation (cycle windows) once per instruction.
    uint64_t total_cycles_ = 0;

    std::string last_error_;
};
