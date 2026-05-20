/**
 * @file k5122.h
 * @brief K5122 AFS – Anschlusssteuerung Floppy-disk Speicher (floppy controller card).
 *
 * Emulates the K5122 floppy disk controller card for the Robotron K1520/A5120 system.
 * The physical card uses two Z80 PIOs for command/status and data transfer,
 * plus one 8212 chip for drive selection.
 *
 * I/O port assignment (base 0x10):
 * @code
 *   0x10 R/W  Ctrl PIO Port A data  – drive control signals (CPU output)
 *   0x11 R/W  Ctrl PIO Port A ctrl  – PIO control register
 *   0x12 R/W  Ctrl PIO Port B data  – drive status signals (CPU input)
 *   0x13 R/W  Ctrl PIO Port B ctrl  – PIO control register
 *   0x14 R/W  Data PIO Port A data  – write data (byte-by-byte from CPU)
 *   0x15 R/W  Data PIO Port A ctrl
 *   0x16 R/W  Data PIO Port B data  – read data  (byte-by-byte to CPU)
 *   0x17 R/W  Data PIO Port B ctrl
 *   0x18 W    8212 drive select     – bits [1:0] = drive number 0–3
 * @endcode
 *
 * Ctrl PIO Port A bit layout (CPU output → drive):
 * @code
 *   bit 0  /WE   – Write Enable (active low)
 *   bit 1  MK    – Mark
 *   bit 2  /FR   – Fault Reset (active low)
 *   bit 3  /STR  – Start/Strobe (active low, edge-triggered)
 *   bit 4  MK1   – Mark 1
 *   bit 5  MR/SD – Step direction / side select (0 = inward / side 1)
 *   bit 6  /HL   – Head Load (active low)
 *   bit 7  /ST   – Step pulse (active low, edge-triggered)
 * @endcode
 *
 * Ctrl PIO Port B bit layout (drive → CPU status input):
 * @code
 *   bit 0  /RDYL – Ready (active low, 0 = drive ready)
 *   bit 1  MKE   – Index mark detected
 *   bit 2  /HF   – High Frequency indicator (FM/5")
 *   bit 3  PRE   – Pre-compensation enable
 *   bit 4  /FA   – Fault Adapter (active low; 1 = no fault)
 *   bit 5  /WP   – Write Protect (active low, 0 = protected)
 *   bit 6  /FW   – Track 0 (active low, 0 = at track 0)
 *   bit 7  /TO   – Timeout (active low, 1 = no timeout)
 * @endcode
 *
 * Simplified (non-DMA) sector transfer model:
 *   Read:  CPU asserts /STR (Port A bit 3 falling edge) → doReadSector() fills
 *          internal buffer from disk image; CPU drains buffer by reading port 0x16
 *          byte-by-byte.  After the last byte, current_sector_ auto-advances.
 *   Write: CPU writes bytes to port 0x14 to fill the buffer, then asserts /STR
 *          with /WE=0 → doWriteSector() commits buffer to disk image.
 *          current_sector_ auto-advances after the write.
 *
 * @note The real K5122 uses BUSRQ/BUSACK to perform DMA transfers.  This
 *       emulation replaces DMA with a simpler byte-polling model compatible
 *       with the A5120 BIOS sector-transfer routines.
 *
 * @see doc/design/07_k5122_afs.md
 * @author Olaf Krieger
 * @date 2024–2025
 * @license MIT License
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_pio.h"
#include "core/peripherals/floppy_drive/floppy_drive.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include <vector>
#include <string>
#include <cstdint>
#include <array>
#include <chrono>

/**
 * @class K5122
 * @brief Emulation of the K5122 AFS (Anschlusssteuerung Floppy-disk Speicher) card.
 *
 * Features:
 * - Controls up to 4 floppy drives via two Z80 PIOs and one 8212 chip
 * - Ctrl PIO: drive control signals (step, head-load, strobe) and status readback
 * - Data PIO: byte-oriented sector data transfer (read and write)
 * - Drive-select via 8212 (port 0x18, bits [1:0])
 * - LED activity simulation per drive (isDriveLedOn())
 * - InterruptSlave: IEI → ctrl_pio_ → data_pio_ → IEO daisy-chain
 */
class K5122 : public BusDevice, public InterruptSlave {
public:
    /**
     * @brief Construct a K5122 floppy controller.
     * @param bus K1520 system bus reference (used for BUSRQ/BUSAK DMA protocol)
     */
    explicit K5122(K1520Bus& bus);

    // ─── BusDevice interface (I/O ports 0x10–0x18) ────────────────────────

    /**
     * @brief Handle an IN instruction to a K5122 I/O port.
     *
     * Dispatches to ctrl PIO (0x10–0x13), data PIO (0x14–0x17), or warns
     * on unknown port.  Reading data Port B (0x16) during an active read
     * transfer returns the next byte from the sector buffer.
     *
     * @param port Port address (0x10–0x18)
     * @return Data byte from the selected sub-device or sector buffer
     */
    uint8_t     ioRead(uint8_t port) override;

    /**
     * @brief Handle an OUT instruction to a K5122 I/O port.
     *
     * Dispatches to ctrl PIO (0x10–0x13), data PIO (0x14–0x17), or 8212
     * drive-select (0x18).  Writing to ctrl Port A (0x10) triggers step and
     * strobe detection; writing to data Port A (0x14) accumulates write data.
     *
     * @param port Port address (0x10–0x18)
     * @param data Byte written by CPU
     */
    void        ioWrite(uint8_t port, uint8_t data) override;

    /**
     * @brief Return the device name.
     * @return "K5122"
     */
    const char* deviceName() const override { return "K5122"; }

    // ─── InterruptSlave interface (ctrl_pio → data_pio daisy-chain) ────────

    /**
     * @brief Set /IEI from the upstream interrupt chain.
     *
     * Propagates IEI through the internal ctrl_pio_ → data_pio_ chain.
     *
     * @param iei true when upstream allows this card to interrupt
     */
    void    setIEI(bool iei) override;

    /**
     * @brief Get /IEO to pass to the downstream device.
     * @return true to pass interrupt downstream, false if data_pio_ has IEO=false
     */
    bool    getIEO() const override;

    /**
     * @brief Check whether this card has a pending interrupt.
     * @return true if ctrl_pio_ or data_pio_ has a pending interrupt
     */
    bool    hasInterrupt() const override;

    /**
     * @brief Return the active interrupt vector from the highest-priority PIO.
     * @return 8-bit interrupt vector, or 0xFF if none
     */
    uint8_t getVector() const override;

    // ─── Disk management ───────────────────────────────────────────────────

    /**
     * @brief Mount a disk image on a drive.
     *
     * Opens the file at @p path and associates it with @p drive.
     * Updates the ctrl PIO Port B status register immediately.
     *
     * @param drive   Drive number 0–3
     * @param path    Path to disk image file
     * @param fmt     Disk geometry and sector format
     * @param write_protect  true to prevent writes to the image
     * @return true on success, false if file not found or drive index invalid
     */
    bool mountDisk(int drive, const std::string& path,
                   const DiskFormat& fmt, bool write_protect = false);

    /**
     * @brief Unmount the disk image from a drive.
     *
     * Marks the drive as empty and updates the status register.
     *
     * @param drive Drive number 0–3
     * @return true on success, false if drive index is invalid
     */
    bool unmountDisk(int drive);

    /**
     * @brief Check whether a disk image is mounted.
     * @param drive Drive number 0–3
     * @return true if a disk is mounted in the drive
     */
    bool isDiskActive(int drive) const;

    /**
     * @brief Check the write-protect status of a mounted disk.
     * @param drive Drive number 0–3
     * @return true if the disk is write-protected
     */
    bool isDiskWriteProtected(int drive) const;

    /**
     * @brief Set write-protect flag on a mounted disk.
     *
     * Updates the ctrl PIO Port B status bit immediately.
     *
     * @param drive Drive number 0–3
     * @param wp    true to enable write protection
     */
    void setWriteProtect(int drive, bool wp);

    /**
     * @brief Query the simulated drive activity LED.
     *
     * Returns true for 180 ms after each step, read, or write operation.
     * Used by the UI to show disk activity.
     *
     * @param drive Drive number 0–3
     * @return true if the LED should be shown as on
     */
    bool isDriveLedOn(int drive) const;

    /**
     * @brief Direct access to a drive object (for tests and C-API wrappers).
     * @param idx Drive index 0–3
     * @return Reference to the FloppyDrive object
     */
    FloppyDrive& drive(int idx) { return drives_[idx]; }

    /**
     * @brief Perform the pending DMA transfer and release /BUSRQ.
     *
     * Called by the A5120Machine run loop while /BUSRQ is asserted.
     * Executes the deferred sector read or write that was triggered by
     * the /STR edge, then calls bus_.releaseBUSRQ() so ZVE1 can resume.
     *
     * On real hardware ZVE2 would perform this transfer autonomously.
     * In this emulation the transfer is performed synchronously here and
     * the sector data is placed in sector_buf_ for subsequent reads via
     * port 0x16 (byte-polling code) or written to the disk image (write).
     *
     * No-op if no DMA transfer is pending.
     */
    void dmaUpdate();

private:
    // ─── PIO signal handlers ────────────────────────────────────────────────

    /**
     * @brief Decode and act on a write to ctrl Port A (port 0x10).
     *
     * Detects falling edges on /ST (bit 7) to perform a head step and on
     * /STR (bit 3) to trigger a sector read or write.  Also tracks the
     * current head/side (bit 5 = MR/SD).
     *
     * @param data Byte written to ctrl PIO Port A
     */
    void handleCtrlPortAWrite(uint8_t data);

    /**
     * @brief Accumulate a byte into the sector write buffer.
     *
     * Called when the CPU writes to data Port A (port 0x14).  Bytes are
     * stored in sector_buf_; doWriteSector() flushes them to the image.
     *
     * @param data Byte from CPU to add to the sector buffer
     */
    void handleDataPortAWrite(uint8_t data);

    /**
     * @brief Push current drive status into ctrl PIO Port B input latch.
     *
     * Composes the status byte from the selected drive's mounted/WP/track-0
     * state and calls ctrl_pio_.portBWrite() so the CPU can read it via
     * IN port 0x12.  Called after every drive-state change.
     */
    void updateStatusPortB();

    // ─── Low-level floppy operations ────────────────────────────────────────

    /**
     * @brief Advance (or retract) the head by one cylinder.
     *
     * Direction is determined by MR/SD bit (bit 5 of ctrl Port A):
     * 0 = step inward (toward higher cylinder), 1 = step outward.
     * No-op if no disk is mounted in the selected drive.
     */
    void doStep();

    /**
     * @brief Read one sector from the selected drive into sector_buf_.
     *
     * Uses current_cyl_, current_head_, and current_sector_ to locate the
     * sector in the disk image.  Sets transferring_=true and write_mode_=false.
     * No-op if no disk is mounted.
     */
    void doReadSector();

    /**
     * @brief Write sector_buf_ to the selected drive.
     *
     * Commits the accumulated write buffer to the disk image at
     * current_cyl_, current_head_, current_sector_, then advances
     * current_sector_ (wrapping at secs_per_track).
     * No-op if no disk is mounted, drive is write-protected, or buffer is empty.
     */
    void doWriteSector();

    /**
     * @brief Record the time of the most recent drive access for LED simulation.
     * @param drive Drive number 0–3
     */
    void markDriveAccess(int drive);

    // ─── Hardware objects ────────────────────────────────────────────────────
    K1520Bus&   bus_;                        ///< K1520 Systembus (für BUSRQ/BUSAK)
    Z80PIO      ctrl_pio_{"K5122-CTRL"};    ///< Steuer-PIO: ports 0x10–0x13
    Z80PIO      data_pio_{"K5122-DATA"};    ///< Daten-PIO:  ports 0x14–0x17
    FloppyDrive drives_[4];                 ///< Up to 4 floppy drives

    // ─── Controller state ────────────────────────────────────────────────────
    int     selected_drive_ = 0;            ///< Currently selected drive (8212 output)
    uint8_t current_cyl_[4] = {};           ///< Current cylinder number per drive
    uint8_t current_head_   = 0;            ///< Currently active head/side (0 or 1)
    uint8_t current_sector_ = 1;            ///< Current sector ID (1-based, auto-advances)
    bool    step_dir_in_    = true;         ///< true = step toward higher cylinder number
    uint8_t prev_ctrl_a_    = 0xFF;         ///< Previous ctrl Port A value (edge detection)

    // ─── Sector transfer buffer ──────────────────────────────────────────────
    std::vector<uint8_t> sector_buf_;       ///< Sector data for current read/write transfer
    size_t               buf_pos_      = 0; ///< Read index into sector_buf_
    bool                 transferring_ = false; ///< true while a read transfer is in progress
    bool                 write_mode_   = false; ///< true during a write transfer

    // ─── DMA state machine ───────────────────────────────────────────────────
    bool dma_pending_  = false;  ///< true: /BUSRQ asserted, DMA transfer waiting
    bool dma_is_write_ = false;  ///< true = write sector, false = read sector

    // ─── Interrupt state ─────────────────────────────────────────────────────
    bool    iei_in_ = false;                ///< Last IEI value from upstream chain

    // ─── LED simulation ──────────────────────────────────────────────────────
    std::array<std::chrono::steady_clock::time_point, 4> led_until_{};  ///< LED-on deadline per drive
    std::chrono::milliseconds led_hold_time_{180};  ///< Duration to keep LED on after access
};
