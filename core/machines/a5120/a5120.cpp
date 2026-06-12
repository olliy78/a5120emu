#include "a5120.h"
#include <algorithm>
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

        // BUSRQ: the K5122 has a DMA transfer in progress and ZVE2 is driving it.
        //
        // On real K2526 hardware ZVE1 and ZVE2 run *concurrently*; /BUSRQ only
        // gates the few bus cycles ZVE2 actually needs, not whole routines. We
        // approximate that by interleaving: step ZVE2, then fall through and also
        // step ZVE1 this iteration. This ordering matters — ZVE1 must finish
        // CALL 0194 (whose tail writes [0x03F8]=0 at 0x01B3) and settle into its
        // poll loop at 0x0168 *before* ZVE2 writes [0x03F8]=3 at 0x026B; otherwise
        // ZVE1's late =0 would clobber ZVE2's =3 and the boot would hang.
        //
        // When ZVE2 is held in reset or /WAIT (legacy ZVE1 byte-poll boot path),
        // there is no second CPU to run, so dmaUpdate() releases the bus directly.
        if (bus_.isBUSRQ()) {
            if (!busrq_active_) {            // a new DMA round just started
                busrq_active_     = true;
                dma_saw_progress_ = false;   // arm: ignore a stale [0x03F8]=3 from
            }                                //      the previous round until ZVE1
                                             //      clears it (CALL 0194, 0x01B3)
            // /BUSRQ asserted while ZVE2 is still reset: the OS-loader's data-area
            // read path (3rd stage @0x1F36) poises ZVE2 by installing its DMA
            // routine at [0x0000] and resetting ZVE2 (OUT(04)=0x00), then triggers
            // /STR — the /BUSRQ is what runs ZVE2 from PC=0 (fetching the current
            // [0x0000]).  Start it here so it fetches the routine before ZVE1
            // restores [0x0000].  (Path-1/secondary rounds start ZVE2 explicitly
            // with bit0=1, so they are not in reset when /BUSRQ asserts.)
            if (zre_.isZVE2InReset()) {   // also clears any /WAIT-ZVE2
                LOG_DEBUG("A5120", "ZVE2-Start aus Reset bei /BUSRQ: PC=0 → [0x0000]");
                zre_.zve2StartFromReset();
            }
            if (!zre_.isZVE2InReset() && !zre_.isZVE2Waiting()) {
                bus_master_zve2_ = true;
                zre_.zve2Step();
                bus_master_zve2_ = false;
                // ZVE2 signals completion by writing [0x03F8]=3 (boot ROM 0x026B).
                // Detect the *transition* to 3 within this round, not the level:
                // a multi-sector chain re-runs the DMA, and [0x03F8] still holds 3
                // from the previous round when the next one starts.
                uint8_t df = bus_.memRead(kZve2DoneFlagAddr);
                if (df != kZve2DoneValue) {
                    dma_saw_progress_ = true;            // ZVE1 cleared it → armed
                } else if (dma_saw_progress_) {
                    LOG_DEBUG("A5120", "endDmaTransfer: ZVE1.PC=%04X ZVE2.PC=%04X",
                              zre_.cpuPC(), zre_.zve2PC());
                    afs_.endDmaTransfer();               // 0→3 this round: done
                    LOG_DEBUG("A5120", "endDmaTransfer fertig: ZVE1.PC=%04X", zre_.cpuPC());
                }
                // Fall through: also step ZVE1 concurrently (see comment above).
            } else {
                afs_.dmaUpdate();
                remaining--;
                total_cycles_++;
                continue;
            }
        } else {
            busrq_active_ = false;          // bus idle → next assert is a new round
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

        // Clock CTC on ZRE card and serial card
        zre_.clockTick();
        ass_.clockTick();

        // Service the keyboard: drain command bytes the BIOS sent to the K7637
        // (reset / LED control) and let it return its type-code acknowledge.
        // Without this the BIOS keyboard detection never sees the K7637 answer,
        // mis-detects a parallel K7606, and no key ever reaches the OS.
        kbd_.processTxCommands();
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
