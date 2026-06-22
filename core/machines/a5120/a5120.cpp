#include "a5120.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include "core/logger.h"

namespace {
// ZVE1↔ZVE2 boot handshake: ZVE2 (DMA-CPU) writes [0x03F8]=3 once it has copied
// all boot sectors into RAM (boot ROM ZVE2_DONE at 0x026B). The machine watches
// this shared RAM flag to know when to release /BUSRQ back to ZVE1.
constexpr uint16_t kZve2DoneFlagAddr = 0x03F8;
constexpr uint8_t  kZve2DoneValue    = 0x03;
}  // namespace

A5120Machine::A5120Machine()
    : zre_(bus_)
    , ops_()
    , screen_(bus_)
    , ass_(bus_)
    , afs_(bus_)
{
    disk_formats_ = FormatParser::builtinFormats();

    // ZVE1 (Haupt-CPU) lebt jetzt auf der K2526-Karte.
    // Verdrahtung mit dem Bus erfolgt im K2526-Konstruktor.
    wireBackplane();
}

void A5120Machine::wireBackplane() {
    // Register all cards on K1520 bus in slot order
    // Slot 1: OPS (K3526) — full 64KB RAM, registered first (lowest priority)
    ops_.attachToBus(bus_);

    // Slot 5: ABS (K7024) — VRAM F800H-FFFFH, overlays OPS
    screen_.attachToBus(bus_);

    // Slot 4: ZRE (K2526) — I/O ports 00H-0FH, boot ROM 0000H-03FFH
    zre_.attachToBus(bus_);

    // Slot 3: ASS (K8025) — I/O ports 50H-5FH
    bus_.registerIO(&ass_, 0x50, 16);

    // Slot 2: AFS (K5122) — I/O ports 10H-18H (9 ports)
    bus_.registerIO(&afs_, 0x10, 9);

    // Interrupt chain (physical slot order: AFS→ASS→ZRE→ABS, OPS has no IRQ)
    // ZRE BS-PIO is on the second chain via Koppelbus (lowest priority)
    bus_.setInterruptChain({&afs_, &ass_, &zre_});

    // Koppelbus wiring:
    // ZRE CTC ZC/TO outputs → Koppelbus zc_to[0..2] signals.
    // zc_to[2] is also "fest verdrahtet" back to ZRE CTC CLK/TRG[3] (channel cascade).
    // zc_to[0] feeds K8025 CTC A34 as external baud-rate clock source (W1:7 bridge).
    zre_.ctc().setZCTOCallback([this](int ch, bool lvl) {
        if (ch >= 0 && ch < 3)
            koppel_.zc_to[ch].drive(lvl);
    });

    // ZRE CTC ZC/TO[2] → ZRE CTC CLK/TRG[3] (hardwired on K2526 via Koppelbus)
    koppel_.zc_to[2].connect([this](bool lvl) {
        zre_.ctc().clkTrg(3, lvl);
    });

    // ZRE CTC ZC/TO[0] → K8025 CTC A34 external clock input (W1:7 "gezeichnete Stellung")
    koppel_.zc_to[0].connect([this](bool lvl) {
        for (int i = 0; i < 4; ++i)
            ass_.ctcA34().clkTrg(i, lvl);
    });

    // Connect keyboard to K8025 SIO A32, Channel A
    kbd_.connect(ass_.sioA32(), 0);
}

void A5120Machine::powerOn() {
    stop_.store(false);
    zre_.powerOn();
    zre_.cpuReset();
    boot_trace_count_ = 0;
    LOG_INFO("A5120", "Power on: ZVE1 Reset, Lade-ROM aktiv");

#if LOG_LEVEL >= 5
    zre_.cpu().traceCallback = [](const Z80& z) {
        LOG_TRACE("ZVE1",
            "PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X",
            z.PC, z.SP, z.AF, z.BC, z.DE, z.HL, z.IX, z.IY);
    };
#endif
}

void A5120Machine::reset() {
    stop_.store(false);
    zre_.powerOn();   // re-enable boot ROM
    zre_.cpuReset();
    boot_trace_count_ = 0;
    LOG_INFO("A5120", "Reset: ZVE1 Reset, Lade-ROM reaktiviert");
}

// ─── Snapshot / reverse-debugging support ────────────────────────────────────
// Capture/restore is exact for CPU + 64 KB RAM (see MachineSnapshot doc in a5120.h).
// Z80 register pairs are unions, so copying the 16-bit views restores all sub-bytes.
static void saveZ80(const Z80& z, A5120Machine::MachineSnapshot::Z80Regs& r) {
    r.AF=z.AF; r.BC=z.BC; r.DE=z.DE; r.HL=z.HL; r.IX=z.IX; r.IY=z.IY;
    r.PC=z.PC; r.SP=z.SP; r.AF_=z.AF_; r.BC_=z.BC_; r.DE_=z.DE_; r.HL_=z.HL_;
    r.I=z.I; r.R=z.R; r.IM=z.IM; r.IFF1=z.IFF1; r.IFF2=z.IFF2;
    r.halted=z.halted; r.cycles=z.cycles;
}
static void loadZ80(Z80& z, const A5120Machine::MachineSnapshot::Z80Regs& r) {
    z.AF=r.AF; z.BC=r.BC; z.DE=r.DE; z.HL=r.HL; z.IX=r.IX; z.IY=r.IY;
    z.PC=r.PC; z.SP=r.SP; z.AF_=r.AF_; z.BC_=r.BC_; z.DE_=r.DE_; z.HL_=r.HL_;
    z.I=r.I; z.R=r.R; z.IM=r.IM; z.IFF1=r.IFF1; z.IFF2=r.IFF2;
    z.halted=r.halted; z.cycles=r.cycles;
}

void A5120Machine::captureState(MachineSnapshot& s) const {
    std::memcpy(s.ram.data(), ops_.rawPtr(), s.ram.size());
    saveZ80(zre_.cpu(),  s.zve1);
    saveZ80(zre_.zve2(), s.zve2);
    s.rom_enabled     = zre_.isRomEnabled();
    s.busrq_active    = busrq_active_;
    s.dma_progress    = dma_saw_progress_;
    s.bus_master_zve2 = bus_master_zve2_;
    s.total_cycles    = total_cycles_;
    // Device-internal state needed for a working keyboard after a loadstate.
    // A working keyboard needs more than the keyboard SIO itself: the OS scans
    // the keyboard from its timer ISR, so the system CTC (Q302) and its BS-PIO,
    // plus the SIO's baud CTC (A34), must resume in phase too — otherwise the
    // timer-scan vs serial-latency race re-appears and keystrokes are dropped.
    // The serialise order here MUST match the deserialise order in restoreState().
    s.device_state.clear();
    zre_.ctc().serialize(s.device_state);        // K2526 Q302 system timer
    zre_.bsPio().serialize(s.device_state);      // K2526 BS-PIO
    ass_.ctcA34().serialize(s.device_state);     // K8025 baud CTC
    ass_.sioA32().serialize(s.device_state);     // K8025 keyboard/printer SIO
    kbd_.serialize(s.device_state);              // K7637 keyboard peripheral
    // Floppy controller: both PIOs, the latched control signals and — the point
    // of this — the mechanical head position (cylinder) of each drive, so disk
    // access (dir, file reads/writes) resumes with the head on the right track.
    afs_.serialize(s.device_state);              // K5122 floppy controller
}

bool A5120Machine::restoreState(const MachineSnapshot& s) {
    ops_.restore(s.ram.data());
    loadZ80(zre_.cpu(),  s.zve1);
    loadZ80(zre_.zve2(), s.zve2);
    busrq_active_    = s.busrq_active;
    dma_saw_progress_= s.dma_progress;
    bus_master_zve2_ = s.bus_master_zve2;
    total_cycles_    = s.total_cycles;
    // Reproduce the boot-ROM mapping too, so a state saved post-ROM resumes correctly
    // even into a freshly powered machine (where the ROM is still mapped at 0x0000).
    zre_.setRomMapped(s.rom_enabled);
    // Restore the keyboard subsystem (system CTC + BS-PIO + baud CTC + keyboard
    // SIO + K7637) so input works after the load. Empty for legacy (v1)
    // snapshots → the devices keep their current state. Order matches captureState().
    if (!s.device_state.empty()) {
        const uint8_t* p   = s.device_state.data();
        const uint8_t* end = p + s.device_state.size();
        zre_.ctc().deserialize(p, end);
        zre_.bsPio().deserialize(p, end);
        ass_.ctcA34().deserialize(p, end);
        ass_.sioA32().deserialize(p, end);
        kbd_.deserialize(p, end);
        afs_.deserialize(p, end);
    }
    return true;
}

// On-disk state format: magic "K1520SS" + version, a regs-size guard, then the raw
// MachineSnapshot fields. Written/read by the same build → POD memcpy is sufficient.
// A length-prefixed device_state blob holds the serialised device-internal state.
// It is parsed sequentially per chip with bounds checks, so a shorter (older) blob
// loads fine — trailing chips simply keep their current state. Versions:
//   1 = no device state, 2 = + keyboard subsystem, 3 = + K5122 floppy controller.
namespace {
const char    kStateMagicPrefix[7] = {'K','1','5','2','0','S','S'};
constexpr uint8_t kStateVersion    = 3;
}

bool A5120Machine::saveState(const std::string& path) const {
    MachineSnapshot s; captureState(s);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t regsize = (uint32_t)sizeof(MachineSnapshot::Z80Regs);
    f.write(kStateMagicPrefix, sizeof kStateMagicPrefix);
    f.write(reinterpret_cast<const char*>(&kStateVersion), 1);
    f.write(reinterpret_cast<const char*>(&regsize), sizeof regsize);
    f.write(reinterpret_cast<const char*>(s.ram.data()), s.ram.size());
    f.write(reinterpret_cast<const char*>(&s.zve1), sizeof s.zve1);
    f.write(reinterpret_cast<const char*>(&s.zve2), sizeof s.zve2);
    uint8_t flags[4] = { (uint8_t)s.rom_enabled, (uint8_t)s.busrq_active,
                         (uint8_t)s.dma_progress, (uint8_t)s.bus_master_zve2 };
    f.write(reinterpret_cast<const char*>(flags), 4);
    f.write(reinterpret_cast<const char*>(&s.total_cycles), sizeof s.total_cycles);
    // v2 tail: length-prefixed device-state blob (keyboard SIO + K7637).
    uint32_t dev_len = (uint32_t)s.device_state.size();
    f.write(reinterpret_cast<const char*>(&dev_len), sizeof dev_len);
    if (dev_len) f.write(reinterpret_cast<const char*>(s.device_state.data()), dev_len);
    return (bool)f;
}

bool A5120Machine::loadState(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[7]; f.read(magic, sizeof magic);
    if (!f || std::memcmp(magic, kStateMagicPrefix, sizeof magic) != 0) return false;
    uint8_t version = 0; f.read(reinterpret_cast<char*>(&version), 1);
    if (!f || version < 1 || version > 3) return false;
    uint32_t regsize = 0; f.read(reinterpret_cast<char*>(&regsize), sizeof regsize);
    if (!f || regsize != (uint32_t)sizeof(MachineSnapshot::Z80Regs)) return false;
    MachineSnapshot s;
    f.read(reinterpret_cast<char*>(s.ram.data()), s.ram.size());
    f.read(reinterpret_cast<char*>(&s.zve1), sizeof s.zve1);
    f.read(reinterpret_cast<char*>(&s.zve2), sizeof s.zve2);
    uint8_t flags[4]; f.read(reinterpret_cast<char*>(flags), 4);
    f.read(reinterpret_cast<char*>(&s.total_cycles), sizeof s.total_cycles);
    if (!f) return false;
    if (version >= 2) {
        uint32_t dev_len = 0; f.read(reinterpret_cast<char*>(&dev_len), sizeof dev_len);
        if (!f) return false;
        s.device_state.resize(dev_len);
        if (dev_len) f.read(reinterpret_cast<char*>(s.device_state.data()), dev_len);
        if (!f) return false;
    }
    s.rom_enabled=flags[0]; s.busrq_active=flags[1]; s.dma_progress=flags[2]; s.bus_master_zve2=flags[3];
    return restoreState(s);
}

int A5120Machine::run(int max_cycles) {
    // Drain key queue
    {
        std::lock_guard<std::mutex> lk(key_mutex_);
        while (!key_queue_.empty()) {
            auto& ev = key_queue_.front();
            if (ev.is_press)
                kbd_.keyPress(ev.keycode, ev.shift, ev.ctrl);
            else
                kbd_.keyRelease(ev.keycode);
            key_queue_.pop_front();
        }
    }

    int remaining = max_cycles;
    while (remaining > 0 && !stop_.load(std::memory_order_relaxed)) {
        // Evaluate dynamic log gates for this instruction (PC range / cycle
        // window). Cheap early-out when no gates are registered. Feed both CPU
        // PCs so a PC gate can match either ZVE1 or ZVE2.
        k1520::logging::Logger::instance().update(
            total_cycles_, zre_.cpuPC(), zre_.zve2PC());

        // Update interrupt chain
        bus_.updateInterruptChain();

        if (bus_.isWAIT()) {
            remaining--;
            total_cycles_++;
            continue;
        }

        if (bus_.isRESET()) {
            LOG_INFO("A5120", "RESET-Leitung gesetzt, ZVE1 wird zurückgesetzt");
            zre_.cpuReset();
        }

        // BUSRQ: Bus-Verriegelung ZVE1↔ZVE2 (hardware-echt, K5122-Doku §5.6.1).
        //
        // /BUSRQ entsteht in der K5122 PRO BYTE aus dem RDY des Daten-PIO: ZVE2
        // erhält den Bus, wenn ein Byte bereitliegt, holt es ab (Port 0x16) und
        // verliert den Bus wieder, bis das nächste Byte ~1 Byteperiode später
        // bereitliegt.  Während /BUSRQ aktiv ist, läuft AUSSCHLIESSLICH ZVE2; in
        // den Byte-Lücken (BUSRQ frei) läuft ZVE1.  ZVE2 verliert den Bus damit
        // automatisch, sobald es aufhört zu lesen (fertig/idle) bzw. /STR=1 zieht —
        // es braucht KEINE programm-/größenspezifische Completion-Erkennung mehr
        // ([0x03F8]=3-Watch und OUT(13H)-Hack sind entfernt).
        //
        // Ist ZVE2 in Reset oder /WAIT (Legacy-ZVE1-Byte-Poll), gibt es keine
        // zweite CPU; dmaUpdate() gibt den Bus direkt frei.
        if (bus_.isBUSRQ()) {
            if (!busrq_active_) {            // neue DMA-Runde beginnt
                busrq_active_     = true;
                dma_saw_progress_ = false;   // ein altes [0x03F8]=3 der Vorrunde ignorieren,
            }                                // bis ZVE1 es löscht (CALL 0194, 0x01B3)
            // /BUSRQ assertiert während ZVE2 noch in Reset: der 3.-Stufen-Lader
            // (0x1F36) poist ZVE2 via [0x0000]=JP-Routine + OUT(04)=0 und löst /STR
            // aus — das /BUSRQ startet ZVE2 ab PC=0 (holt das aktuelle [0x0000]),
            // bevor ZVE1 [0x0000] restauriert.
            if (zre_.isZVE2InReset()) {   // also clears any /WAIT-ZVE2
                LOG_DEBUG("A5120", "ZVE2-Start aus Reset bei /BUSRQ: PC=0 → [0x0000]");
                zre_.zve2StartFromReset();
            }
            if (!zre_.isZVE2InReset() && !zre_.isZVE2Waiting()) {
                bus_master_zve2_ = true;
                int used2 = zre_.zve2Step();
                bus_master_zve2_ = false;
                if (used2 <= 0) used2 = 1;   // Sicherung gegen Endlosschleife
                remaining     -= used2;
                total_cycles_ += used2;
                afs_.update(used2);          // Floppy-Timer (Byte-Bereitschaft, /STR-Abtastung)
                // ZVE2-Completion-Handshake [0x03F8]=3 (Boot-ROM 0x026B; Sekundär- und
                // 3.-Stufen-Lader nutzen denselben Flag).  Dies ist KEIN sektorgrößen-
                // abhängiges Signal mehr (der OUT(13H)-Hack ist entfernt), sondern der
                // programmweite „DMA fertig"-Handshake.  Nötig, weil der Chained Loader
                // (round 2) die DMA per ZEITSCHLEIFE (0x0441 DEC C;JR NZ) abwartet; unter
                // der Per-Byte-Verschränkung verschiebt sich das ZVE1:ZVE2-Tempo, sodass
                // ZVE1s Zeitschleife vor ZVE2s DMA-Ende abläuft → /STR=1 zu früh → Retry.
                // Der Flag-Watcher gibt den Bus exakt bei ZVE2-Completion frei (Transition
                // 0→3 innerhalb der Runde; das Level allein träfe ein altes =3).
                uint8_t df = bus_.memRead(kZve2DoneFlagAddr);
                if (df != kZve2DoneValue) {
                    dma_saw_progress_ = true;
                } else if (dma_saw_progress_) {
                    afs_.endDmaTransfer();
                    LOG_DEBUG("A5120", "ZVE2 DMA fertig ([0x03F8]=3): BUSRQ frei, ZVE1.PC=%04X", zre_.cpuPC());
                }
                continue;                    // Verriegelung: ZVE1 läuft NICHT während /BUSRQ
            } else {
                afs_.dmaUpdate();
                remaining--;
                total_cycles_++;
                continue;
            }
        } else {
            busrq_active_ = false;          // Bus frei → nächste Assertion ist neue Runde
        }

        // Deliver INT if CPU can accept
        if (bus_.isINT() && zre_.cpuIFF1()) {
            uint8_t vec = bus_.interruptAcknowledge();
            LOG_DEBUG("A5120", "INT zugestellt: Vektor=0x%02X PC=%04X", vec, zre_.cpuPC());
            zre_.cpuInterrupt(vec);
        }

        if (bus_.isNMI()) {
            LOG_DEBUG("A5120", "NMI zugestellt: PC=%04X", zre_.cpuPC());
            zre_.cpuNMI();
            bus_.clearNMI();
        }

        const uint16_t pc_before = zre_.cpuPC();
        int used = zre_.cpuStep();
        remaining -= used;
        total_cycles_ += used;

        // Advance floppy index pulse simulation
        afs_.update(used);

        if (boot_trace_count_ < 80) {
            LOG_DEBUG("BOOT", "step=%d PC=%04X used=%d", boot_trace_count_, pc_before, used);
            ++boot_trace_count_;
        }

        // Clock CTC on ZRE card and serial card.  Advance by the T-states the
        // instruction actually took — the CTC prescaler divides the system
        // clock, so ticking once per instruction (avg ~6 T-states) ran the
        // real-time clock and baud generators ~6x too slow.
        zre_.clockTick(used);
        ass_.clockTick(used);

        // Service the keyboard: advance the 9600-baud serial-transmit timing
        // (release any keyboard→host bytes whose transmission has completed) and
        // drain command bytes the BIOS sent to the K7637 (reset / LED control),
        // letting it return its type-code acknowledge.  The serial latency keeps
        // the type-code acks from appearing the same instant the command is sent
        // — otherwise the timer-ISR keyboard scan races the foreground LED
        // handshake for the ack and the loser reads an empty SIO (0xFF → CR),
        // which floods the keyboard buffer and drops real keystrokes at the CCP.
        kbd_.service(total_cycles_);
    }

    return max_cycles - remaining;
}

bool A5120Machine::mountDisk(int drive, const std::string& path,
                              const std::string& format_name, bool wp) {
    if (drive < 0 || drive > 3) { last_error_ = "Invalid drive"; return false; }

    auto it = std::find_if(disk_formats_.begin(), disk_formats_.end(),
                           [&](const DiskFormat& f){ return f.name == format_name; });
    if (it == disk_formats_.end()) {
        last_error_ = "Unknown format: " + format_name;
        return false;
    }

    std::lock_guard<std::mutex> lk(disk_mutex_);
    return afs_.mountDisk(drive, path, *it, wp);
}

bool A5120Machine::unmountDisk(int drive) {
    if (drive < 0 || drive > 3) return false;
    std::lock_guard<std::mutex> lk(disk_mutex_);
    return afs_.unmountDisk(drive);
}

bool A5120Machine::isDiskActive(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return afs_.isDiskActive(drive);
}

bool A5120Machine::isDiskWriteProtected(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return afs_.isDiskWriteProtected(drive);
}

bool A5120Machine::isDiskLedOn(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return afs_.isDriveLedOn(drive);
}

void A5120Machine::setDiskWriteProtect(int drive, bool wp) {
    if (drive < 0 || drive > 3) return;
    afs_.setWriteProtect(drive, wp);
}

void A5120Machine::keyPress(uint32_t kc, bool shift, bool ctrl) {
    std::lock_guard<std::mutex> lk(key_mutex_);
    key_queue_.push_back({kc, shift, ctrl, true});
}

void A5120Machine::keyRelease(uint32_t kc) {
    std::lock_guard<std::mutex> lk(key_mutex_);
    key_queue_.push_back({kc, false, false, false});
}

const uint8_t* A5120Machine::framebuffer() const {
    return screen_.getFramebuffer();
}

void A5120Machine::setDFUECallback(SerialCb cb) {
    ass_.setDFUERxCallback(std::move(cb));
}

void A5120Machine::dfueSend(uint8_t byte) {
    ass_.dfueRxByte(byte);
}

void A5120Machine::setPrinterCallback(SerialCb cb) {
    // Drain printer TX in run() or via callback — store for polling
    (void)cb;  // TODO: hook into SIO A32 ch B TX callback
}
