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
#include "core/logger.h"

// ─── Construction ─────────────────────────────────────────────────────────────

K5122::K5122(K1520Bus& bus) : bus_(bus) {
    // Pre-populate the ctrl PIO Port B with "no drive mounted" status so that
    // the very first read of port 0x12 returns something sensible.
    updateStatusPortB();
}

// ─── BusDevice ────────────────────────────────────────────────────────────────

uint8_t K5122::ioRead(uint8_t port) {
    uint8_t result = 0xFF;

    if (port >= 0x10 && port <= 0x13) {
        result = ctrl_pio_.ioRead(port - 0x10);
        LOG_DEBUG("K5122", "CTRL PIO read  port=0x%02X (sub=%u) => 0x%02X  [%s]",
                  port, port - 0x10, result,
                  port == 0x10 ? "CtrlA-data" : port == 0x11 ? "CtrlA-ctrl" :
                  port == 0x12 ? "CtrlB-data" : "CtrlB-ctrl");
    } else if (port >= 0x14 && port <= 0x17) {
        // Intercept data Port B reads (0x16) to stream sector data to the CPU.
        if (port == 0x16 && transferring_ && !write_mode_) {
            if (buf_pos_ < sector_buf_.size()) {
                result = sector_buf_[buf_pos_++];
                LOG_TRACE("K5122", "DATA byte[%zu/%zu] => 0x%02X",
                          buf_pos_ - 1, sector_buf_.size(), result);
            } else {
                transferring_ = false;
                result = 0xFF;
                // Advance to next sector on track (wraps at secs_per_track).
                const FloppyDrive& drv = drives_[selected_drive_];
                if (drv.isMounted()) {
                    const TrackFormat* tf = drv.format().findTrack(
                        current_cyl_[selected_drive_], current_head_);
                    if (tf && tf->secs_per_track > 0) {
                        current_sector_ = (current_sector_ % tf->secs_per_track) + 1;
                    }
                }
                LOG_DEBUG("K5122", "DATA transfer complete (%zu bytes), next sector=%u",
                          sector_buf_.size(), current_sector_);
            }
        } else {
            result = data_pio_.ioRead(port - 0x14);
            LOG_DEBUG("K5122", "DATA PIO read  port=0x%02X (sub=%u) => 0x%02X",
                      port, port - 0x14, result);
        }
    } else {
        LOG_WARN("K5122", "ioRead unknown port=0x%02X", port);
    }

    return result;
}

void K5122::ioWrite(uint8_t port, uint8_t data) {
    if (port >= 0x10 && port <= 0x13) {
        const char* port_name =
            port == 0x10 ? "CtrlA-data" : port == 0x11 ? "CtrlA-ctrl" :
            port == 0x12 ? "CtrlB-data" : "CtrlB-ctrl";
        if (port == 0x10) {
            // Decode ctrl Port A signal bits for readability
            LOG_DEBUG("K5122",
                "CTRL PortA write 0x%02X  /ST=%d /HL=%d MK=%d /STR=%d /FR=%d MR/SD=%d MK1=%d /WE=%d",
                data,
                (data >> 7) & 1, (data >> 6) & 1, (data >> 5) & 1, (data >> 3) & 1,
                (data >> 2) & 1, (data >> 1) & 1, (data >> 4) & 1, data & 1);
        } else {
            LOG_DEBUG("K5122", "CTRL PIO write port=0x%02X data=0x%02X [%s]",
                      port, data, port_name);
        }
        ctrl_pio_.ioWrite(port - 0x10, data);
        // Mirror ctrl Port A data writes to our handler regardless of PIO mode.
        if (port == 0x10) {
            handleCtrlPortAWrite(data);
        }
    } else if (port >= 0x14 && port <= 0x17) {
        const char* port_name =
            port == 0x14 ? "DataA-data" : port == 0x15 ? "DataA-ctrl" :
            port == 0x16 ? "DataB-data" : "DataB-ctrl";
        LOG_DEBUG("K5122", "DATA PIO write port=0x%02X data=0x%02X [%s]",
                  port, data, port_name);
        data_pio_.ioWrite(port - 0x14, data);
        if (port == 0x14) {
            handleDataPortAWrite(data);
        }
    } else if (port == 0x18) {
        // 8212 drive-select: bits [7:4]=/LCKx bits [3:0]=/SELx (simplified)
        selected_drive_ = data & 0x03;
        LOG_INFO("K5122", "8212 drive-select write=0x%02X => D%d selected",
                 data, selected_drive_);
        updateStatusPortB();
    } else {
        LOG_WARN("K5122", "ioWrite unknown port=0x%02X data=0x%02X", port, data);
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

bool K5122::isDriveLedOn(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return std::chrono::steady_clock::now() < led_until_[static_cast<size_t>(drive)];
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
    // Real hardware: /STR=0 enables /BUSRQ generation. ZVE2 (DMA-CPU) takes
    // over the bus and performs the sector transfer autonomously.
    // Emulation: assert /BUSRQ immediately; defer the actual sector
    // read/write to dmaUpdate(), which is called by the run loop while
    // ZVE1 is suspended (BUSRQ asserted).
    if ((prev_ctrl_a_ & 0x08) && !(data & 0x08)) {
        dma_is_write_ = !(data & 0x01);  // /WE bit 0: 0 = write mode
        dma_pending_  = true;
        bus_.assertBUSRQ();
        LOG_DEBUG("K5122", "/STR Flanke: BUSRQ gesetzt, DMA %s ausstehend",
                  dma_is_write_ ? "SCHREIBEN" : "LESEN");
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
    markDriveAccess(selected_drive_);
    LOG_TRACE("K5122", "STEP D%d dir=%s cyl=%u",
              selected_drive_, step_dir_in_ ? "in" : "out",
              static_cast<unsigned>(drv.currentCylinder()));
    current_cyl_[selected_drive_] = drv.currentCylinder();
}

void K5122::doReadSector() {
    FloppyDrive& drv = drives_[selected_drive_];
    if (!drv.isMounted()) {
        LOG_WARN("K5122", "READ requested but D%d not mounted!", selected_drive_);
        return;
    }

    markDriveAccess(selected_drive_);
    LOG_INFO("K5122", ">>> READ  D%d C=%u H=%u S=%u",
              selected_drive_,
              static_cast<unsigned>(current_cyl_[selected_drive_]),
              static_cast<unsigned>(current_head_),
              static_cast<unsigned>(current_sector_));

    sector_buf_ = drv.readSector(current_cyl_[selected_drive_],
                                 current_head_,
                                 current_sector_);
    buf_pos_      = 0;
    transferring_ = true;
    write_mode_   = false;

    LOG_DEBUG("K5122", "    sector data: %zu bytes, first=0x%02X 0x%02X 0x%02X 0x%02X...",
              sector_buf_.size(),
              sector_buf_.size() > 0 ? sector_buf_[0] : 0xFF,
              sector_buf_.size() > 1 ? sector_buf_[1] : 0xFF,
              sector_buf_.size() > 2 ? sector_buf_[2] : 0xFF,
              sector_buf_.size() > 3 ? sector_buf_[3] : 0xFF);
}

void K5122::doWriteSector() {
    FloppyDrive& drv = drives_[selected_drive_];
    if (!drv.isMounted()) {
        LOG_WARN("K5122", "WRITE requested but D%d not mounted!", selected_drive_);
        return;
    }
    if (drv.isWriteProtect()) {
        LOG_WARN("K5122", "WRITE aborted: D%d is write-protected", selected_drive_);
        return;
    }
    if (sector_buf_.empty()) {
        LOG_WARN("K5122", "WRITE aborted: sector buffer empty");
        return;
    }

    markDriveAccess(selected_drive_);
    LOG_INFO("K5122", ">>> WRITE D%d C=%u H=%u S=%u bytes=%u",
              selected_drive_,
              static_cast<unsigned>(current_cyl_[selected_drive_]),
              static_cast<unsigned>(current_head_),
              static_cast<unsigned>(current_sector_),
              static_cast<unsigned>(sector_buf_.size()));

    drv.writeSector(current_cyl_[selected_drive_],
                    current_head_,
                    current_sector_,
                    sector_buf_);

    // Advance sector after write, same as after read.
    {
        const TrackFormat* tf = drv.format().findTrack(
            current_cyl_[selected_drive_], current_head_);
        if (tf && tf->secs_per_track > 0)
            current_sector_ = (current_sector_ % tf->secs_per_track) + 1;
    }

    sector_buf_.clear();
    buf_pos_      = 0;
    transferring_ = false;
    write_mode_   = false;
}

void K5122::markDriveAccess(int drive) {
    if (drive < 0 || drive > 3) return;
    led_until_[static_cast<size_t>(drive)] =
        std::chrono::steady_clock::now() + led_hold_time_;
}

// ─── DMA-Update (wird vom Run-Loop aufgerufen während BUSRQ aktiv ist) ────────
//
// Führt den ausstehenden Sektortransfer durch (Lesen oder Schreiben) und
// gibt den Bus durch releaseBUSRQ() wieder frei. ZVE1 darf danach weiter
// arbeiten. Auf echter Hardware würde ZVE2 diese Aufgabe übernehmen.

void K5122::dmaUpdate() {
    if (!dma_pending_) return;

    if (dma_is_write_) {
        doWriteSector();
    } else {
        doReadSector();
    }

    dma_pending_ = false;
    bus_.releaseBUSRQ();
    LOG_DEBUG("K5122", "dmaUpdate: DMA-Transfer abgeschlossen, BUSRQ freigegeben");
}
