/**
 * @file k5122.cpp
 * @brief K5122 AFS – Anschlusssteuerung Floppy-disk Speicher (floppy controller card) implementation.
 *
 * Implements the floppy disk controller for the Robotron K1520/A5120 system.
 * Two Z80 PIOs handle drive control signals and byte-level data transfer;
 * an 8212-equivalent chip at port 0x18 selects one of up to four drives.
 *
 * Ctrl PIO Port A bit layout (CPU output → drive):
 * @code
 *   bit 0  /WE   – Write Enable (active low)
 *   bit 1  MK    – Mark
 *   bit 2  /FR   – Fault Reset (active low)
 *   bit 3  /STR  – Start / Strobe (active low, edge-triggered)
 *   bit 4  MK1   – Mark 1
 *   bit 5  MR/SD – Step direction / side select (0 = inward / side 1)
 *   bit 6  /HL   – Head Load (active low)
 *   bit 7  /ST   – Step pulse (active low, edge-triggered)
 * @endcode
 *
 * Ctrl PIO Port B bit layout (drive → CPU input):
 * @code
 *   bit 0  /RDYL – Ready (active low, 0 = drive ready)
 *   bit 1  MKE   – Index mark detected
 *   bit 2  /HF   – High Frequency indicator (output, 1 = FM/5")
 *   bit 3  PRE   – Pre-compensation (output)
 *   bit 4  /FA   – Fault Adapter (active low; 1 = no fault)
 *   bit 5  /WP   – Write Protect (active low, 0 = protected)
 *   bit 6  /FW   – Fault (active low, 1 = no fault)
 *   bit 7  /TO   – Track 0 (active low, 0 = at track 0)
 * @endcode
 *
 * @see k5122.h
 * @see doc/design/07_k5122_afs.md
 * @author Olaf Krieger
 * @date 2024–2025
 * @license MIT License
 */

#include "k5122.h"
#include <algorithm>
#include <cassert>
#include "core/logger.h"

// ─── Construction ─────────────────────────────────────────────────────────────

/**
 * @brief Construct a K5122 floppy controller and initialise drive status.
 *
 * Pre-populates the ctrl PIO Port B with the "no drive mounted" status byte
 * (0xF5) so that the first read of port 0x12 returns a sensible value before
 * any disk is mounted.
 *
 * @param bus K1520 system bus reference (used for BUSRQ/BUSAK DMA protocol)
 */
K5122::K5122(K1520Bus& bus) : bus_(bus) {
    // Pre-populate the ctrl PIO Port B with "no drive mounted" status so that
    // the very first read of port 0x12 returns something sensible.
    updateStatusPortB();
}

// ─── BusDevice ────────────────────────────────────────────────────────────────

/**
 * @brief Handle an IN instruction to a K5122 I/O port.
 *
 * Dispatches to the ctrl PIO (0x10–0x13), data PIO (0x14–0x17), or returns
 * 0xFF for unknown ports.  Special handling for port 0x16 (data Port B):
 * during an active read transfer, returns the next byte from sector_buf_ and
 * auto-releases BUSRQ after the last byte is consumed.
 *
 * @param port Port address (0x10–0x18)
 * @return Data byte from the selected sub-device or sector buffer
 */
uint8_t K5122::ioRead(uint8_t port) {
    uint8_t result = 0xFF;

    if (port >= 0x10 && port <= 0x13) {
        result = ctrl_pio_.ioRead(port - 0x10);
        LOG_DEBUG("K5122", "CTRL PIO read  port=0x%02X (sub=%u) => 0x%02X  [%s]",
                  port, port - 0x10, result,
                  port == 0x10 ? "CtrlA-data" : port == 0x11 ? "CtrlA-ctrl" :
                  port == 0x12 ? "CtrlB-data" : "CtrlB-ctrl");
    } else if (port >= 0x14 && port <= 0x17) {
        // Intercept data Port B reads (0x16) to stream the rotating track to the
        // CPU/DMA. The buffer holds the whole track (built by doReadSector); we
        // stream it and wrap at the end, because the disk keeps spinning. BUSRQ
        // is held throughout — it is released only when ZVE2 signals completion
        // ([0x03F8]=3), detected by A5120Machine, which then calls endDmaTransfer().
        if (port == 0x16 && transferring_ && !write_mode_) {
            // Stream the current address-mark field; pad with gap bytes on over-read.
            // The reader advances to the next field by pulsing MK (see advanceField()).
            if (field_buf_.empty()) {
                result = 0xFF;
            } else if (field_pos_ < field_buf_.size()) {
                result = field_buf_[field_pos_++];
                LOG_TRACE("K5122", "Feld[%zu/%zu] S%zu %s => 0x%02X",
                          field_pos_ - 1, field_buf_.size(), field_sector_,
                          field_is_data_ ? "DATA" : "IDAM", result);
            } else {
                result = kGapByte;   // over-read past the field → MFM gap filler
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

/**
 * @brief Handle an OUT instruction to a K5122 I/O port.
 *
 * Dispatches to the ctrl PIO (0x10–0x13), data PIO (0x14–0x17), or the 8212
 * drive-select (0x18).  Writing to ctrl Port A (0x10) additionally triggers
 * handleCtrlPortAWrite() for step/strobe edge detection.  Writing to data Port
 * A (0x14) additionally triggers handleDataPortAWrite() to accumulate write data.
 *
 * @param port Port address (0x10–0x18)
 * @param data Byte written by CPU
 */
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
        } else if (port == 0x11) {
            LOG_DEBUG("K5122", "CTRL PIO write port=0x11 data=0x%02X [CtrlA-ctrl] busrq=%d",
                      data, bus_.isBUSRQ() ? 1 : 0);
        } else {
            LOG_DEBUG("K5122", "CTRL PIO write port=0x%02X data=0x%02X [%s]",
                      port, data, port_name);
        }
        ctrl_pio_.ioWrite(port - 0x10, data);
        // Mirror ctrl Port A data writes to our handler regardless of PIO mode.
        if (port == 0x10) {
            handleCtrlPortAWrite(data);
        }
        // Track-end: the loader's ZVE2 routine disables the ctrl-PIO Port B
        // interrupt (OUT(13H),03H at 0x069C) the moment it has read all sectors of
        // a track and falls into its idle loop L0696.  On real hardware /STR=1 then
        // suppresses /BUSRQ and ZVE2 loses the bus; in our continuously-stepped
        // model ZVE2 would instead spin in L0696 and corrupt the handshake variables
        // ([07F8..07FC] via INIR).  Release /BUSRQ here so ZVE1 takes over, processes
        // the track, seeks and resets ZVE2 for the next track.
        if (port == 0x13 && data == 0x03 && transferring_ && bus_.isBUSRQ()) {
            transferring_ = false;
            bus_.releaseBUSRQ();
            LOG_DEBUG("K5122", "Track-Ende (ZVE2 L0696): BUSRQ freigegeben, ZVE1 übernimmt");
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
        // 8212 drive-select: bits [3:0] = /SELx, active-low one-hot
        // 0xEE → lower nibble 0xE = 1110, bit0=0 → drive 0
        // 0xDD → lower nibble 0xD = 1101, bit1=0 → drive 1
        // 0xBB → lower nibble 0xB = 1011, bit2=0 → drive 2
        // 0x77 → lower nibble 0x7 = 0111, bit3=0 → drive 3
        uint8_t sel = ~data & 0x0F;
        selected_drive_ = (sel == 0) ? 0
                        : (sel & 0x01) ? 0
                        : (sel & 0x02) ? 1
                        : (sel & 0x04) ? 2
                        : 3;
        LOG_INFO("K5122", "8212 drive-select write=0x%02X => D%d selected",
                 data, selected_drive_);
        updateStatusPortB();
    } else {
        LOG_WARN("K5122", "ioWrite unknown port=0x%02X data=0x%02X", port, data);
    }
}

// ─── InterruptSlave ──────────────────────────────────────────────────────────
// Daisy-chain order: IEI → ctrl_pio_ → data_pio_ → IEO (K5122 output).

/**
 * @brief Set /IEI from the upstream interrupt chain and propagate internally.
 *
 * Daisy-chain order: IEI → ctrl_pio_ → data_pio_ → IEO out.
 *
 * @param iei true when the upstream device passes the interrupt enable signal
 */
void K5122::setIEI(bool iei) {
    iei_in_ = iei;
    ctrl_pio_.setIEI(iei);
    data_pio_.setIEI(ctrl_pio_.getIEO());
}

/**
 * @brief Return /IEO to pass to the downstream device in the daisy chain.
 *
 * Reflects data_pio_.getIEO(), which is false when either PIO has a pending
 * interrupt that blocks further propagation.
 *
 * @return true to pass the enable signal downstream; false if chain is blocked
 */
bool K5122::getIEO() const {
    return data_pio_.getIEO();
}

/**
 * @brief Check whether ctrl_pio_ or data_pio_ has a pending interrupt.
 * @return true if either PIO requires CPU attention
 */
bool K5122::hasInterrupt() const {
    return ctrl_pio_.hasInterrupt() || data_pio_.hasInterrupt();
}

/**
 * @brief Return the interrupt vector from the highest-priority PIO.
 *
 * ctrl_pio_ has priority over data_pio_ (it is first in the daisy chain).
 *
 * @return 8-bit interrupt vector, or 0xFF if no interrupt is pending
 */
uint8_t K5122::getVector() const {
    if (ctrl_pio_.hasInterrupt()) return ctrl_pio_.getVector();
    if (data_pio_.hasInterrupt()) return data_pio_.getVector();
    return 0xFF;
}

void K5122::onRETI() {
    ctrl_pio_.onRETI();
    data_pio_.onRETI();
}

// ─── Disk management ─────────────────────────────────────────────────────────

/**
 * @brief Mount a disk image on a drive.
 *
 * Opens @p path with the given geometry @p fmt and associates it with @p drive.
 * If @p drive is the currently selected drive, ctrl PIO Port B status is
 * updated immediately so the CPU sees the drive as ready.
 *
 * @param drive         Drive number 0–3
 * @param path          Path to disk image file
 * @param fmt           Disk geometry and sector format
 * @param write_protect true to prevent writes to the image (default false)
 * @return true on success; false if the file is not found or @p drive is invalid
 */
bool K5122::mountDisk(int drive, const std::string& path,
                      const DiskFormat& fmt, bool write_protect) {
    if (drive < 0 || drive > 3) return false;
    bool ok = drives_[drive].mount(path, fmt, write_protect);
    if (ok && drive == selected_drive_) {
        updateStatusPortB();
    }
    return ok;
}

/**
 * @brief Unmount the disk image from a drive.
 *
 * Marks the drive as empty and updates ctrl PIO Port B if @p drive is
 * currently selected.
 *
 * @param drive Drive number 0–3
 * @return true on success; false if @p drive is out of range
 */
bool K5122::unmountDisk(int drive) {
    if (drive < 0 || drive > 3) return false;
    drives_[drive].unmount();
    if (drive == selected_drive_) {
        updateStatusPortB();
    }
    return true;
}

/**
 * @brief Check whether a disk image is mounted on a drive.
 * @param drive Drive number 0–3
 * @return true if a disk is currently mounted; false if drive is empty or index invalid
 */
bool K5122::isDiskActive(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return drives_[drive].isMounted();
}

/**
 * @brief Query the simulated drive activity LED state.
 *
 * Returns true for 180 ms after any step, read, or write operation on
 * @p drive.  The time is measured from the last call to markDriveAccess().
 *
 * @param drive Drive number 0–3
 * @return true if the drive LED should be shown as on
 */
bool K5122::isDriveLedOn(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return std::chrono::steady_clock::now() < led_until_[static_cast<size_t>(drive)];
}

/**
 * @brief Check the write-protect status of a mounted disk.
 * @param drive Drive number 0–3
 * @return true if the disk is write-protected; false if writable or index invalid
 */
bool K5122::isDiskWriteProtected(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return drives_[drive].isWriteProtect();
}

/**
 * @brief Set the write-protect flag on a mounted disk.
 *
 * Updates ctrl PIO Port B (bit 5 = /WP) immediately if @p drive is the
 * currently selected drive.
 *
 * @param drive Drive number 0–3
 * @param wp    true to enable write protection; false to allow writes
 */
void K5122::setWriteProtect(int drive, bool wp) {
    if (drive < 0 || drive > 3) return;
    drives_[drive].setWriteProtect(wp);
    if (drive == selected_drive_) {
        updateStatusPortB();
    }
}

// ─── Private: ctrl Port A handler ────────────────────────────────────────────

/**
 * @brief Decode and act on a write to ctrl Port A (port 0x10).
 *
 * Detects two falling-edge signals:
 *
 * - `/ST` (bit 7): Step pulse.  Calls doStep() to advance or retract the
 *   head by one cylinder.  Direction from MR/SD (bit 5).
 *
 * - `/STR` (bit 3): Start / Strobe.
 *   - If BUSRQ is already held (ZVE2 committing a write): calls doWriteSector()
 *     and releases BUSRQ.
 *   - Otherwise (ZVE1 triggering a new transfer): for read, calls doReadSector()
 *     and fills sector_buf_; for write, clears sector_buf_ for ZVE2 to fill.
 *     Then asserts BUSRQ.
 *
 * Also updates current_head_ from bit 5 (MR/SD) and refreshes ctrl Port B status.
 *
 * @param data Byte written to ctrl PIO Port A
 */
void K5122::handleCtrlPortAWrite(uint8_t data) {
    // ── Step pulse: /ST (bit 7) falling edge ─────────────────────────────────
    if ((prev_ctrl_a_ & 0x80) && !(data & 0x80)) {
        // MR/SD (bit 5) step direction. Polarity from the boot ROM's own usage:
        //   bit5=0 → outward (toward track 0): ROM drive-detect seeks track 0 with
        //            0x09 (0000 1001, bit5=0).
        //   bit5=1 → inward (toward higher cylinder): the loaded boot record steps
        //            to cylinder 1 with 0x29 (0010 1001, bit5=1).
        step_dir_in_ = (data & 0x20) != 0;
        doStep();
    }

    // ── Start strobe: /STR (bit 3) falling edge ───────────────────────────────
    // ZVE1 context: assert /BUSRQ so ZVE2 (or dmaUpdate fallback) can take over.
    //   READ  (/WE=1): fill sector_buf_ immediately; ZVE2 drains it via INIR
    //                  on port 0x16; last byte auto-releases BUSRQ.
    //   WRITE (/WE=0): clear sector_buf_ for ZVE2 to fill via OTIR on port 0x14;
    //                  ZVE2 triggers commit by writing /STR again while holding bus.
    // ZVE2 context (bus already acquired): commit a completed write and release bus.
    if ((prev_ctrl_a_ & 0x08) && !(data & 0x08)) {
        bool is_write = !(data & 0x01);  // /WE=0 means write

        if (bus_.isBUSRQ()) {
            if (is_write) {
                // ZVE2 write-commit: OTIR finished, commit sector and release bus.
                doWriteSector();
                dma_pending_ = false;
                bus_.releaseBUSRQ();
                LOG_DEBUG("K5122", "/STR ZVE2-Commit SCHREIBEN abgeschlossen, BUSRQ freigegeben");
            } else {
                // ZVE2 read-refresh: /STR re-strobed at a track boundary — reload
                // the rotating track and present sector 0's IDAM field again.  The
                // mid-sector field advances (IDAM→DATA→next IDAM) are driven by the
                // MK strobe (see below), not by /STR.
                doReadSector();
                LOG_DEBUG("K5122", "/STR ZVE2-Lese-Refresh: Spur neu geladen, BUSRQ gehalten");
            }
        } else {
            // ZVE1 triggering a new DMA transfer
            dma_is_write_ = is_write;
            if (!is_write) {
                // The MR/SD bit (bit 5) of THIS start word is the side-select for
                // the read.  Latch the head *before* building the track stream so
                // the IDAM headers carry the correct side.  Previously current_head_
                // was only updated after doReadSector() (below), so a read started
                // by 0xA1 (bit5=1 → head 0) still streamed the stale head from the
                // preceding control word — the loader's ZVE2 routine then rejected
                // every IDAM (head mismatch) and span forever in its retry loop.
                current_head_ = (data & 0x20) ? 0 : 1;
                doReadSector();   // fill sector_buf_ immediately for ZVE2 INIR
            } else {
                sector_buf_.clear();  // ZVE2 will fill via port 0x14 writes
                buf_pos_ = 0;
            }
            dma_pending_ = true;
            bus_.assertBUSRQ();
            LOG_DEBUG("K5122", "/STR Flanke: BUSRQ gesetzt, DMA %s ausstehend",
                      is_write ? "SCHREIBEN" : "LESEN");
        }
    }

    // ── MK strobe: ctrl Port A bit1 rising edge (0xB5→0x87) ──────────────────
    // Both the boot ROM (0x0224/0x022D, 0x0249/0x025F) and the loaded bootloader's
    // ZVE2 routine (0x066E/0x0670, 0x06B9/0x06BB) pulse MK to re-synchronise the
    // data separator to the next address mark.  Each rising edge advances the
    // presented field: IDAM → DATA (same sector) → IDAM of the next sector.
    if (transferring_ && !write_mode_ && !(prev_ctrl_a_ & 0x02) && (data & 0x02))
        advanceField();

    // Update head/side select (bit 5 doubles as side select when not stepping).
    // MR/SD=0 → side 1 (head 1); MR/SD=1 → side 0 (head 0).
    current_head_ = (data & 0x20) ? 0 : 1;

    prev_ctrl_a_ = data;
    updateStatusPortB();
}

// ─── Private: data Port A handler ────────────────────────────────────────────

/**
 * @brief Accumulate one byte into the sector write buffer.
 *
 * Called when the CPU writes to data Port A (port 0x14).  Bytes are appended
 * to sector_buf_ for later commit by doWriteSector().  The read pointer
 * buf_pos_ is reset to 0 on each call so a subsequent read would start from
 * the beginning of the buffer (although reads and writes do not mix in
 * normal operation).
 *
 * @param data Byte from the CPU to append to the sector buffer
 */
void K5122::handleDataPortAWrite(uint8_t data) {
    // Accumulate write data regardless of mode; doWriteSector() will consume it.
    sector_buf_.push_back(data);
    buf_pos_ = 0;   // reset read pointer on new write data
}

// ─── Private: status ─────────────────────────────────────────────────────────

/**
 * @brief Compose the drive status byte and inject it into ctrl PIO Port B.
 *
 * Builds the Port B status byte for the currently selected drive:
 * @code
 *   Default (not mounted): 0xF5  = /TO=1, /FW=1, /WP=1, /FA=1, PRE=0, /HF=1, MKE=0, /RDYL=1
 *   Drive mounted:         /RDYL cleared (bit 0 = 0)
 *   At track 0:            /FW   cleared (bit 6 = 0)
 *   Write-protected:       /WP   cleared (bit 5 = 0)
 * @endcode
 *
 * The composed byte is pushed into ctrl_pio_.portBWrite() so the CPU can
 * read it via IN port 0x12.  Called after every drive-state change.
 */
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
            s &= ~(1u << 7);        // /TO = /TRACK 00 = 0 (head at track 0).
                                    // The boot drive-detect (ROM 0x0110: IN 12H;
                                    // RLCA; JR NC) proceeds only when bit 7 is clear.
        if (drv.isWriteProtect())
            s &= ~(1u << 5);        // /WP = 0 (write-protected)
        // bit 6 (/FW = /FAULT) stays 1: no drive fault is modelled.
    }

    // Push status into the ctrl PIO Port B input latch so the CPU can read
    // it via ioRead(0x12) → ctrl_pio_.ioRead(2) → readDataCPU(portb_).
    ctrl_pio_.portBWrite(s);
}

// ─── Private: floppy operations ──────────────────────────────────────────────

/**
 * @brief Advance (or retract) the head of the selected drive by one cylinder.
 *
 * Direction is determined by step_dir_in_ (derived from MR/SD bit 5 of ctrl
 * Port A): true = step inward (toward higher cylinder numbers), false = outward.
 * No-op if no disk is mounted in the selected drive.
 * Updates current_cyl_[selected_drive_] and records the access for LED simulation.
 */
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

/**
 * @brief Fill sector_buf_ with the whole current track as a continuous read stream.
 *
 * The real K5122 streams the rotating track to the CPU/DMA; the ZVE2 DMA-CPU
 * scans that stream for each sector's IDAM and copies the data.  We model this
 * by concatenating every sector of the current (cylinder, head) track, in
 * sector-ID order, as a sequence of 136-byte blocks:
 *
 * @code
 *   byte 0       0x00  sync / gap                 (ROM reads & discards, addr 0x0261)
 *   byte 1       0xFE  IDAM address mark          (ROM CP B' = 0xFE, addr 0x020B)
 *   byte 2       cyl   cylinder                   (ROM CP E', addr 0x0210)
 *   byte 3       head  head / side                (ROM CP D', addr 0x0215)
 *   byte 4       sec   sector ID (1-based)        (ROM CP L', addr 0x021A)
 *   byte 5       size  size code (128→0 …)        (ROM CP H', addr 0x021F)
 *   byte 6..7    0x00  IDAM CRC / gap (2 bytes)   (ROM dummy reads, addr 0x022F, 0x0239)
 *   byte 8..135  data  sector payload (128 bytes) (ROM INI/INIR, addr 0x023C…0x0242)
 *   byte 136..137 0x00 data CRC (2 bytes)         (ROM trailing INI, addr 0x0245, 0x0247)
 * @endcode
 *
 * The block is **138 bytes**, which is exactly how many bytes the boot ROM reads
 * per sector: 6 header + 2 IDAM-CRC + 128 data + 2 data-CRC.  Getting this count
 * right is critical — a continuous stream must align each sector's IDAM with the
 * ROM's read sequence.  With a short block the stream drifts by the deficit per
 * sector and the IDAM search never matches sectors 2…N.
 *
 * buf_pos_ is reset to 0; ioRead(0x16) streams the buffer and wraps at the end
 * (the disk keeps spinning).  No-op if no disk is mounted.
 */
void K5122::doReadSector() {
    FloppyDrive& drv = drives_[selected_drive_];
    if (!drv.isMounted()) {
        LOG_WARN("K5122", "READ requested but D%d not mounted!", selected_drive_);
        return;
    }

    const uint8_t cyl  = current_cyl_[selected_drive_];
    const uint8_t head = current_head_;
    const TrackFormat* tf = drv.format().findTrack(cyl, head);
    const uint8_t nsec = (tf && tf->secs_per_track > 0) ? tf->secs_per_track : 1;

    // Size code from bytes/sector (128→0, 256→1, 512→2, 1024→3).
    uint8_t size_code = 0;
    if (tf) {
        uint16_t bps = tf->bytes_per_sec;
        while (bps > 128 && size_code < 3) { bps >>= 1; ++size_code; }
    }

    markDriveAccess(selected_drive_);
    LOG_INFO("K5122", ">>> READ  D%d C=%u H=%u (track: %u sectors, size_code=%u)",
              selected_drive_, static_cast<unsigned>(cyl),
              static_cast<unsigned>(head), static_cast<unsigned>(nsec), size_code);

    sector_buf_.clear();
    sector_buf_.reserve(static_cast<size_t>(nsec) * 138u);
    for (uint8_t sec = 1; sec <= nsec; ++sec) {
        auto data = drv.readSector(cyl, head, sec);
        sector_buf_.push_back(0xA1);       // sync / address-mark prefix (loader skips 0xA1)
        sector_buf_.push_back(0xFE);       // IDAM address mark
        sector_buf_.push_back(cyl);        // cylinder
        sector_buf_.push_back(head);       // head / side
        sector_buf_.push_back(sec);        // sector ID (1-based, matches ROM L')
        sector_buf_.push_back(size_code);  // size code
        sector_buf_.push_back(0x00);       // IDAM CRC / gap byte 1 (ROM discards)
        sector_buf_.push_back(0x00);       // IDAM CRC / gap byte 2 (ROM discards)
        sector_buf_.insert(sector_buf_.end(), data.begin(), data.end());
        sector_buf_.push_back(0x00);       // data CRC byte 1 (ROM trailing INI 0x0245)
        sector_buf_.push_back(0x00);       // data CRC byte 2 (ROM trailing INI 0x0247)
    }

    buf_pos_       = 0;
    transferring_  = true;
    write_mode_    = false;
    // Present the first sector's IDAM field; MK strobes advance from here.
    field_sector_  = 0;
    field_is_data_ = false;
    buildField();

    LOG_DEBUG("K5122", "    track stream: %u sectors, %zu bytes total",
              static_cast<unsigned>(nsec), sector_buf_.size());
}

/**
 * @brief Write the accumulated sector_buf_ to the selected drive.
 *
 * Commits the write buffer to the disk image at the current cylinder, head,
 * and sector position.  After a successful write, current_sector_ is advanced
 * (wrapping at secs_per_track), sector_buf_ is cleared, and the transfer
 * state is reset.
 *
 * Early-exit conditions (logs a warning):
 * - No disk mounted in the selected drive
 * - Drive is write-protected
 * - sector_buf_ is empty (no data accumulated)
 */
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

/**
 * @brief Record the time of the most recent disk access to drive the LED simulation.
 *
 * Sets led_until_[drive] to now + led_hold_time_ (180 ms).  isDriveLedOn()
 * returns true until this deadline passes.
 *
 * @param drive Drive number 0–3 (out-of-range values are silently ignored)
 */
void K5122::markDriveAccess(int drive) {
    if (drive < 0 || drive > 3) return;
    led_until_[static_cast<size_t>(drive)] =
        std::chrono::steady_clock::now() + led_hold_time_;
}

// ─── Address-mark field streaming (MK-strobe driven) ──────────────────────────

namespace {
/**
 * @brief Replicates the loaded bootloader's per-sector CRC-16 (sub_0407 @0x0407).
 *
 * The loader verifies every loaded sector by computing this CRC over its 128 data
 * bytes (LD E,[0677]=0x80) and comparing the result (B=high, C=low) against the two
 * CRC bytes the K5122 streamed after the data (table at [0x0770]).  The synthesised
 * DATA field must therefore carry the *correct* CRC or the loader halts at its error
 * handler (0x0605).  Start word BF84H is the normal boot path ([0x03FD]=0x87, bit1=1;
 * the alternate E295H path is bit1=0).  Byte-for-byte translation of the Z80 routine.
 */
uint16_t loaderCrc16(const uint8_t* data, size_t n, uint8_t b, uint8_t c) {
    auto ror = [](uint8_t x, int k) -> uint8_t {
        return static_cast<uint8_t>((x >> k) | (x << (8 - k)));
    };
    for (size_t i = 0; i < n; ++i) {
        uint8_t a = data[i] ^ b;        // A = byte XOR B
        b = a;                          // t1
        a = ror(a, 4) & 0x0F;
        a ^= b;
        b = a;                          // t2
        a = ror(a, 3);
        uint8_t d = a;
        a = (a & 0x1F) ^ c;
        c = a;
        a = ror(d, 1) & 0xF0;
        a ^= c;
        c = a;
        a = (d & 0xE0) ^ b;
        b = c;
        c = a;
    }
    return static_cast<uint16_t>((b << 8) | c);  // B = high CRC byte, C = low
}
}  // namespace

void K5122::buildField() {
    field_buf_.clear();
    field_pos_ = 0;
    if (sector_buf_.empty()) return;

    const size_t nsec  = sector_buf_.size() / kSectorBlock;
    if (nsec == 0) return;
    const size_t start = (field_sector_ % nsec) * kSectorBlock;
    // 138-byte IDAM-field block layout: [A1 FE cyl head sec size][gap gap]<128 data>[CRC CRC]
    const uint8_t cyl  = sector_buf_[start + 2];
    const uint8_t head = sector_buf_[start + 3];
    const uint8_t sec  = sector_buf_[start + 4];
    const uint8_t size = sector_buf_[start + 5];

    if (!field_is_data_) {
        // IDAM field: address mark + header, then gap.  The reader (ROM or loader)
        // stops after as many bytes as it wants and pulses MK to advance.
        field_buf_ = {0xA1, 0xFE, cyl, head, sec, size};
        field_buf_.insert(field_buf_.end(), 18, kGapByte);
    } else {
        // DATA field: address mark + 128 data bytes + CRC, then gap.
        field_buf_ = {0xA1, 0xFB};
        const size_t data_off = start + 8;
        for (size_t i = 0; i < 128; ++i) field_buf_.push_back(sector_buf_[data_off + i]);
        // Real CRC-16 over the 128 data bytes so the loader's per-sector verify
        // (CALL 0x0407 → CP B / CP C) passes.  Start word BF84H = normal boot path.
        const uint16_t crc = loaderCrc16(&sector_buf_[data_off], 128, 0xBF, 0x84);
        field_buf_.push_back(static_cast<uint8_t>(crc >> 8));    // CRC high (→ CP B)
        field_buf_.push_back(static_cast<uint8_t>(crc & 0xFF));  // CRC low  (→ CP C)
        field_buf_.insert(field_buf_.end(), 8, kGapByte);
    }
}

void K5122::advanceField() {
    if (sector_buf_.empty()) return;
    const size_t nsec = sector_buf_.size() / kSectorBlock;
    if (nsec == 0) return;

    if (!field_is_data_) {
        field_is_data_ = true;                          // IDAM → DATA (same sector)
    } else {
        field_is_data_ = false;                         // DATA → next sector's IDAM
        field_sector_  = (field_sector_ + 1) % nsec;
    }
    buildField();
    LOG_TRACE("K5122", "MK-Flanke: Feld → Sektor %zu %s", field_sector_,
              field_is_data_ ? "DATA" : "IDAM");
}

// ─── Index pulse simulation ───────────────────────────────────────────────────

void K5122::update(int cycles) {
    if (!drives_[selected_drive_].isMounted()) return;

    index_cycle_acc_ += cycles;
    if (index_cycle_acc_ < kIndexPeriodCycles) return;
    index_cycle_acc_ -= kIndexPeriodCycles;

    // Pulse /ASTB low (index pulse) then high again.
    // The falling edge (true→false) fires the ctrl PIO Port A interrupt.
    ctrl_pio_.setASTB(false);   // falling edge → interrupt pending
    ctrl_pio_.setASTB(true);    // return high (pulse complete)
    LOG_TRACE("K5122", "Index pulse: ctrl PIO Port A /ASTB pulsed");
}

/**
 * @brief Perform the pending DMA transfer and release /BUSRQ (ZVE2 fallback).
 *
 * Called by the A5120Machine run loop while /BUSRQ is asserted and ZVE2 is in
 * reset (boot-ROM phase).  Behaviour depends on the direction of the pending
 * transfer:
 *
 * - **Write**: calls doWriteSector() to commit sector_buf_ to the disk image.
 * - **Read**: sector_buf_ was already filled by doReadSector() in
 *   handleCtrlPortAWrite(); ZVE1 (boot ROM, byte-polling via IN A,(16h))
 *   drains the buffer after BUSRQ is released.
 *
 * After executing the transfer, clears dma_pending_ and calls
 * bus_.releaseBUSRQ() so ZVE1 can resume.  No-op if dma_pending_ is false.
 */
void K5122::dmaUpdate() {
    if (!dma_pending_) return;

    if (dma_is_write_) {
        doWriteSector();
    }
    // For read: sector_buf_ was already filled in handleCtrlPortAWrite;
    // ZVE1 (Boot ROM byte-polling via IN A,(16h)) will drain it after resume.

    dma_pending_ = false;
    bus_.releaseBUSRQ();
    LOG_DEBUG("K5122", "dmaUpdate (ZVE2-Fallback): BUSRQ freigegeben (%s)",
              dma_is_write_ ? "SCHREIBEN abgeschlossen" : "LESEN: ZVE1 pollt selbst");
}

/**
 * @brief End an active read-DMA transfer and release /BUSRQ.
 *
 * Called by A5120Machine when it observes the ZVE2 DMA-CPU signalling
 * completion (boot ROM writes [0x03F8]=3 at 0x026B after copying all sectors).
 * The K5122 cannot see that memory write itself, so the machine arbitrates the
 * handover: it detects completion and calls this to clear the transfer state
 * and hand the bus back to ZVE1.  No-op if /BUSRQ is not held.
 *
 * ZVE2's completion code (ROM 0x0267) writes OUT(11H,03H) which is an interrupt
 * control word with IE=0, disabling ctrl_pio_ Port A interrupt (Vektor 0xBA,
 * Index-Puls-ISR).  The bootloader at 0x0437 reprogrammes the vector to 0x62
 * (OUT(11H),0x62 — a vector word, not a control word) but does not re-enable IE.
 * We restore IE here so that subsequent index pulses trigger the Timer-ISR
 * (Vektor 0x62 → ISR 0x060E) which the bootloader waiting loop at 0x052A
 * requires to make progress.
 *
 * 0x83 = 1000_0011: interrupt control word (bits[3:0]≠1111, bit0=1),
 * IE=1 (bit7), OR mode (bit6=0), active LOW (bit5=0), no mask follows (bit4=0).
 */
void K5122::endDmaTransfer() {
    if (!bus_.isBUSRQ()) return;
    transferring_ = false;
    dma_pending_  = false;
    bus_.releaseBUSRQ();
    // Restore ctrl_pio_ Port A interrupt enable (cleared by ZVE2 OUT(11H,03H)).
    ctrl_pio_.ioWrite(1, 0x83);
    LOG_DEBUG("K5122", "endDmaTransfer: ZVE2 DMA fertig, BUSRQ freigegeben, ctrl-PIO Port-A IE wiederhergestellt");
}
