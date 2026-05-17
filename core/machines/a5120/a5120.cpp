#include "a5120.h"
#include <algorithm>
#include "core/logger.h"

A5120Machine::A5120Machine()
    : zre_(bus_)
    , ops_()
    , screen_(bus_)
    , ass_(bus_)
    , afs_(bus_)
{
    disk_formats_ = FormatParser::builtinFormats();

    // Wire CPU callbacks to bus
    cpu_.readByte  = [this](uint16_t a)           { return bus_.memRead(a); };
    cpu_.writeByte = [this](uint16_t a, uint8_t d){ bus_.memWrite(a, d); };
    cpu_.readPort  = [this](uint16_t p)           { return bus_.ioRead(p & 0xFF); };
    cpu_.writePort = [this](uint16_t p, uint8_t d){ bus_.ioWrite(p & 0xFF, d); };
    cpu_.retiCallback = [this]()                  { bus_.signalRETI(); };

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

    // Koppelbus wiring: CTC ZC/TO[2] → CTC channel 3 clock
    koppel_.zc_to[2].connect([this](bool lvl) {
        zre_.ctc().clkTrg(3, lvl);
    });

    // Connect keyboard to K8025 SIO A32, Channel A
    kbd_.connect(ass_.sioA32(), 0);
}

void A5120Machine::powerOn() {
    stop_.store(false);
    zre_.powerOn();
    cpu_.reset();
    boot_trace_count_ = 0;
    LOG_INFO("A5120", "Power on: CPU reset, boot ROM enabled");

    // Set up Z80 instruction trace callback at TRACE level (LOG_LEVEL=5).
    // Captures PC and all registers before each instruction.
#if LOG_LEVEL >= 5
    cpu_.traceCallback = [](const Z80& z) {
        LOG_TRACE("ZVE1",
            "PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X",
            z.PC, z.SP, z.AF, z.BC, z.DE, z.HL, z.IX, z.IY);
    };
#endif
}

void A5120Machine::reset() {
    stop_.store(false);
    zre_.powerOn();   // re-enable boot ROM
    cpu_.reset();
    boot_trace_count_ = 0;
    LOG_INFO("A5120", "Reset: CPU reset, boot ROM re-enabled");
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
        // Update interrupt chain
        bus_.updateInterruptChain();

        if (bus_.isWAIT()) {
            // External WAIT line holds the CPU. Consume no instruction and let
            // peripherals progress on the next loop iteration.
            continue;
        }

        if (bus_.isRESET()) {
            LOG_INFO("A5120", "RESET line asserted by bus, resetting CPU");
            cpu_.reset();
        }

        // Deliver INT if CPU can accept
        if (bus_.isINT() && cpu_.IFF1) {
            uint8_t vec = bus_.interruptAcknowledge();
            cpu_.interrupt(vec);
        }

        if (bus_.isNMI()) {
            cpu_.nmi();
        }

        const uint16_t pc_before = cpu_.PC;
        int used = cpu_.step();
        remaining -= used;

        if (boot_trace_count_ < 80) {
            LOG_DEBUG("BOOT", "step=%d PC=%04X used=%d", boot_trace_count_, pc_before, used);
            ++boot_trace_count_;
        }

        // Clock CTC on ZRE card
        zre_.clockTick();
        ass_.clockTick();
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
