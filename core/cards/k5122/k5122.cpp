// core/cards/k5122/k5122.cpp
// K5122 AFS – Anschlusssteuerung Floppy-disk Speicher
//
// Two Z80 PIOs handle drive control signals and byte-level data transfer.
// An 8212 equivalent (port 0x18) selects one of up to 4 drives.
//
// Ctrl Port A bit layout (CPU output → drive):
//   bit 0  /WE   – Write Enable (active low)
//   bit 1  MK    – Mark
//   bit 2  /FR   – Fault Reset (active low)
//   bit 3  /STR  – Start / Strobe (active low, edge-triggered)
//   bit 4  MK1   – Mark 1
//   bit 5  MR/SD – Step direction / side select (0 = inward / side 1)
//   bit 6  /HL   – Head Load (active low)
//   bit 7  /ST   – Step pulse (active low, edge-triggered)
//
// Ctrl Port B bit layout (drive → CPU input):
//   bit 0  /RDYL – Ready (active low, 0 = drive ready)
//   bit 1  MKE   – Index mark detected
//   bit 2  /HF   – High Frequency indicator (output, 1 = FM/5")
//   bit 3  PRE   – Pre-compensation (output)
//   bit 4  /FA   – Fault Adapter (active low; 1 = no fault)
//   bit 5  /WP   – Write Protect (active low, 0 = protected)
//   bit 6  /FW   – Track 0 (active low, 0 = at track 0)
//   bit 7  /TO   – Timeout (active low, 1 = no timeout)

#include "k5122.h"
#include <algorithm>
#include <cassert>

// ─── Construction ─────────────────────────────────────────────────────────────

K5122::K5122(K1520Bus& /*bus*/) {
    // Pre-populate the ctrl PIO Port B with "no drive mounted" status so that
    // the very first read of port 0x12 returns something sensible.
    updateStatusPortB();
}

// ─── BusDevice ────────────────────────────────────────────────────────────────

uint8_t K5122::ioRead(uint8_t port) {
    if (port >= 0x10 && port <= 0x13) {
        return ctrl_pio_.ioRead(port - 0x10);
    }
    if (port >= 0x14 && port <= 0x17) {
        // Intercept data Port B reads (0x16) to stream sector data to the CPU.
        if (port == 0x16 && transferring_ && !write_mode_) {
            if (buf_pos_ < sector_buf_.size()) {
                return sector_buf_[buf_pos_++];
            }
            // All bytes delivered – transfer complete.
            transferring_ = false;
            return 0xFF;
        }
        return data_pio_.ioRead(port - 0x14);
    }
    // Port 0x18 is write-only (8212 drive-select latch).
    return 0xFF;
}

void K5122::ioWrite(uint8_t port, uint8_t data) {
    if (port >= 0x10 && port <= 0x13) {
        ctrl_pio_.ioWrite(port - 0x10, data);
        // Mirror ctrl Port A data writes to our handler regardless of PIO mode.
        if (port == 0x10) {
            handleCtrlPortAWrite(data);
        }
    } else if (port >= 0x14 && port <= 0x17) {
        data_pio_.ioWrite(port - 0x14, data);
        if (port == 0x14) {
            handleDataPortAWrite(data);
        }
    } else if (port == 0x18) {
        // 8212 drive-select: simplified to bits [1:0].
        selected_drive_ = data & 0x03;
        updateStatusPortB();
    }
}

// ─── InterruptSlave ──────────────────────────────────────────────────────────
// Daisy-chain order: IEI → ctrl_pio_ → data_pio_ → IEO (K5122 output).

void K5122::setIEI(bool iei) {
    iei_in_ = iei;
    ctrl_pio_.setIEI(iei);
    data_pio_.setIEI(ctrl_pio_.getIEO());
}

bool K5122::getIEO() const {
    return data_pio_.getIEO();
}

bool K5122::hasInterrupt() const {
    return ctrl_pio_.hasInterrupt() || data_pio_.hasInterrupt();
}

uint8_t K5122::getVector() const {
    if (ctrl_pio_.hasInterrupt()) return ctrl_pio_.getVector();
    if (data_pio_.hasInterrupt()) return data_pio_.getVector();
    return 0xFF;
}

// ─── Disk management ─────────────────────────────────────────────────────────

bool K5122::mountDisk(int drive, const std::string& path,
                      const DiskFormat& fmt, bool write_protect) {
    if (drive < 0 || drive > 3) return false;
    bool ok = drives_[drive].mount(path, fmt, write_protect);
    if (ok && drive == selected_drive_) {
        updateStatusPortB();
    }
    return ok;
}

bool K5122::unmountDisk(int drive) {
    if (drive < 0 || drive > 3) return false;
    drives_[drive].unmount();
    if (drive == selected_drive_) {
        updateStatusPortB();
    }
    return true;
}

bool K5122::isDiskActive(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return drives_[drive].isMounted();
}

bool K5122::isDiskWriteProtected(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return drives_[drive].isWriteProtect();
}

void K5122::setWriteProtect(int drive, bool wp) {
    if (drive < 0 || drive > 3) return;
    drives_[drive].setWriteProtect(wp);
    if (drive == selected_drive_) {
        updateStatusPortB();
    }
}

// ─── Private: ctrl Port A handler ────────────────────────────────────────────

void K5122::handleCtrlPortAWrite(uint8_t data) {
    // ── Step pulse: /ST (bit 7) falling edge ─────────────────────────────────
    if ((prev_ctrl_a_ & 0x80) && !(data & 0x80)) {
        // MR/SD (bit 5): 0 = inward (toward higher cylinder), 1 = outward.
        step_dir_in_ = !(data & 0x20);
        doStep();
    }

    // ── Start strobe: /STR (bit 3) falling edge ───────────────────────────────
    if ((prev_ctrl_a_ & 0x08) && !(data & 0x08)) {
        bool is_write = !(data & 0x01);  // /WE bit 0: 0 = write mode
        if (is_write) {
            doWriteSector();
        } else {
            doReadSector();
        }
    }

    // Update head/side select (bit 5 doubles as side select when not stepping).
    current_head_ = (data >> 5) & 0x01;

    prev_ctrl_a_ = data;
    updateStatusPortB();
}

// ─── Private: data Port A handler ────────────────────────────────────────────

void K5122::handleDataPortAWrite(uint8_t data) {
    // Accumulate write data regardless of mode; doWriteSector() will consume it.
    sector_buf_.push_back(data);
    buf_pos_ = 0;   // reset read pointer on new write data
}

// ─── Private: status ─────────────────────────────────────────────────────────

void K5122::updateStatusPortB() {
    // Compose Port B status byte (active-low signals are 0 when active).
    //
    // Default (drive not mounted): everything inactive / no fault.
    //   /TO=1, /FW=1, /WP=1, /FA=1, PRE=0, /HF=1, MKE=0, /RDYL=1
    //   = 0b11110101 = 0xF5
    uint8_t s = 0xF5;

    FloppyDrive& drv = drives_[selected_drive_];
    if (drv.isMounted()) {
        s &= ~(1u << 0);            // /RDYL = 0 (ready)
        if (drv.currentCylinder() == 0)
            s &= ~(1u << 6);        // /FW = 0 (at track 0)
        if (drv.isWriteProtect())
            s &= ~(1u << 5);        // /WP = 0 (write-protected)
    }

    // Push status into the ctrl PIO Port B input latch so the CPU can read
    // it via ioRead(0x12) → ctrl_pio_.ioRead(2) → readDataCPU(portb_).
    ctrl_pio_.portBWrite(s);
}

// ─── Private: floppy operations ──────────────────────────────────────────────

void K5122::doStep() {
    FloppyDrive& drv = drives_[selected_drive_];
    if (!drv.isMounted()) return;

    drv.step(step_dir_in_);
    current_cyl_[selected_drive_] = drv.currentCylinder();
}

void K5122::doReadSector() {
    FloppyDrive& drv = drives_[selected_drive_];
    if (!drv.isMounted()) return;

    sector_buf_ = drv.readSector(current_cyl_[selected_drive_],
                                 current_head_,
                                 current_sector_);
    buf_pos_      = 0;
    transferring_ = true;
    write_mode_   = false;
}

void K5122::doWriteSector() {
    FloppyDrive& drv = drives_[selected_drive_];
    if (!drv.isMounted()) return;
    if (drv.isWriteProtect()) return;
    if (sector_buf_.empty()) return;

    drv.writeSector(current_cyl_[selected_drive_],
                    current_head_,
                    current_sector_,
                    sector_buf_);

    sector_buf_.clear();
    buf_pos_      = 0;
    transferring_ = false;
    write_mode_   = false;
}
