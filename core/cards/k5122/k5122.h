#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_pio.h"
#include "core/peripherals/floppy_drive/floppy_drive.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include <vector>
#include <string>
#include <cstdint>

// ─── K5122 AFS – Anschlusssteuerung Floppy-disk Speicher ─────────────────────
//
// Controls up to 4 floppy drives via two Z80 PIOs and one 8212 chip:
//
//  Steuer-PIO (ctrl_pio_)  – ports 0x10–0x13
//    Port A output: control signals (/WE, MK, /FR, /STR, MK1, MR/SD, /HL, /ST)
//    Port B mixed:  status signals  (/RDYL, MKE, /HF, PRE, /FA, /WP, /FW, /TO)
//
//  Daten-PIO (data_pio_)   – ports 0x14–0x17
//    Port A output: write data (byte-by-byte sector transfer)
//    Port B input:  read data  (byte-by-byte sector transfer)
//
//  8212 drive-select chip  – port 0x18
//    bits [1:0]: drive number 0–3 (simplified: data & 0x03)
//
// Simplified sector-transfer model (no DMA/BUSRQ):
//   Read:  /STR pulse (ctrl Port A bit 3 = 0) → doReadSector() fills sector_buf_;
//          CPU drains sector_buf_ by reading port 0x16 (data Port B) byte-by-byte.
//   Write: CPU fills sector_buf_ by writing to port 0x14 (data Port A) byte-by-byte,
//          then asserts /STR with /WE=0 → doWriteSector() commits to image.

class K5122 : public BusDevice, public InterruptSlave {
public:
    explicit K5122(K1520Bus& bus);

    // BusDevice: I/O ports 0x10–0x18
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "K5122"; }

    // InterruptSlave (daisy-chain: IEI → ctrl_pio → data_pio → IEO)
    void    setIEI(bool) override;
    bool    getIEO() const override;
    bool    hasInterrupt() const override;
    uint8_t getVector() const override;

    // ── Disk management ──────────────────────────────────────────────────────
    // Mount a disk image using the given format.  Returns false on failure
    // (file not found, bad format, drive index out of range).
    bool mountDisk(int drive, const std::string& path,
                   const DiskFormat& fmt, bool write_protect = false);
    bool unmountDisk(int drive);

    // isDiskActive: true when a disk image is mounted in the drive.
    bool isDiskActive(int drive) const;
    bool isDiskWriteProtected(int drive) const;
    void setWriteProtect(int drive, bool wp);

    // Direct access for tests / C-API wrappers.
    FloppyDrive& drive(int idx) { return drives_[idx]; }

private:
    // ── PIO signal handlers ─────────────────────────────────────────────────
    // Called from ioWrite when the CPU writes to ctrl Port A data (0x10).
    void handleCtrlPortAWrite(uint8_t data);
    // Called from ioWrite when the CPU writes to data Port A data (0x14).
    void handleDataPortAWrite(uint8_t data);

    // Push current drive status into ctrl_pio_ Port B input latch.
    void updateStatusPortB();

    // ── Low-level floppy operations ─────────────────────────────────────────
    void doStep();
    void doReadSector();
    void doWriteSector();

    // ── Hardware objects ────────────────────────────────────────────────────
    Z80PIO      ctrl_pio_{"K5122-CTRL"};    // ports 0x10–0x13 (Steuer-PIO)
    Z80PIO      data_pio_{"K5122-DATA"};    // ports 0x14–0x17 (Daten-PIO)
    FloppyDrive drives_[4];

    // ── Controller state ────────────────────────────────────────────────────
    int     selected_drive_ = 0;
    uint8_t current_cyl_[4] = {};          // current cylinder per drive
    uint8_t current_head_   = 0;           // currently selected head/side
    uint8_t current_sector_ = 1;           // current sector id (1-based)
    bool    step_dir_in_    = true;        // true = toward higher cylinder
    uint8_t prev_ctrl_a_    = 0xFF;        // previous ctrl Port A value

    // ── Sector transfer buffer ──────────────────────────────────────────────
    std::vector<uint8_t> sector_buf_;
    size_t               buf_pos_      = 0;
    bool                 transferring_ = false;
    bool                 write_mode_   = false;

    // ── Interrupt state ─────────────────────────────────────────────────────
    bool    iei_in_ = false;
};
