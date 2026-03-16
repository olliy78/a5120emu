// cpa_bios.h - CPA (CP/M 2.2) BIOS Emulation for A5120
// This intercepts BIOS calls and provides hardware emulation
#pragma once
#include "z80.h"
#include "memory.h"
#include "terminal.h"
#include "floppy.h"
#include <string>
#include <vector>

class CpaBios {
public:
    // CP/M memory layout (matching @os.com layout)
    static constexpr uint16_t TPA_START     = 0x0100;
    static constexpr uint16_t IOBYTE_ADDR   = 0x0003;
    static constexpr uint16_t CDISK_ADDR    = 0x0004;
    static constexpr uint16_t BDOS_ENTRY    = 0x0005;

    // These are determined from the loaded @os.com
    uint16_t ccpBase;
    uint16_t bdosBase;
    uint16_t biosBase;

    // BIOS function numbers
    enum BiosFunc {
        BIOS_BOOT = 0,      // Cold boot
        BIOS_WBOOT,         // Warm boot
        BIOS_CONST,         // Console status
        BIOS_CONIN,          // Console input
        BIOS_CONOUT,         // Console output
        BIOS_LIST,           // List output
        BIOS_PUNCH,          // Punch output
        BIOS_READER,         // Reader input
        BIOS_HOME,           // Home disk
        BIOS_SELDSK,         // Select disk
        BIOS_SETTRK,         // Set track
        BIOS_SETSEC,         // Set sector
        BIOS_SETDMA,         // Set DMA
        BIOS_READ,           // Read sector
        BIOS_WRITE,          // Write sector
        BIOS_LISTST,         // List status
        BIOS_SECTRAN,        // Sector translate
        BIOS_COUNT
    };

    CpaBios(Z80& cpu, Memory& mem, Terminal& term);

    // Disk management
    bool mountDisk(int drive, const std::string& imagePath);
    bool mountDirectory(int drive, const std::string& dirPath);
    void unmountDisk(int drive);

    // Boot from @os.com file (loaded at 0x100)
    bool bootFromFile(const std::string& osPath);

    // Boot from disk image
    bool bootFromDisk(int drive);

    // Handle BIOS trap (called when CPU hits BIOS entry point)
    bool handleBiosTrap(uint16_t addr);

    // Handle BDOS trap (BDOS function 0-40)
    bool handleBdosTrap();

    // Install BIOS traps in memory
    void installTraps();

    // Run emulation loop
    void run();

    // CRT emulation (for screen buffer mode)
    void updateDisplay();

    // Periodic timer tick (call from main loop)
    void tick();

private:
    Z80& cpu_;
    Memory& mem_;
    Terminal& term_;

    // Disk state
    FloppyDisk disks_[4]; // A: B: C: D:
    int currentDisk_;
    int currentTrack_;
    int currentSector_;
    uint16_t dmaAddr_;

    // DPH (Disk Parameter Header) addresses for each drive
    uint16_t dphAddr_[4];

    // Sector translation table (CPA interleave)
    static const uint8_t sectorXlat_[26];

    // BIOS trap mechanism: HALT instructions placed directly at BIOS jump table entries
    static constexpr uint8_t TRAP_OPCODE = 0x76;  // HALT used as trap

    // CRT state for screen buffer emulation
    uint16_t cursorPos_;
    bool cursorVisible_;
    uint8_t escapeState_;
    uint8_t escapeRow_;

    // Timer
    uint64_t tickCount_;
    bool running_;

    // Internal BIOS functions
    void biosBoot();
    void biosWboot();
    uint8_t biosConst();
    uint8_t biosConin();
    void biosConout(uint8_t ch);
    void biosList(uint8_t ch);
    uint8_t biosPunch(uint8_t ch);
    uint8_t biosReader();
    void biosHome();
    uint16_t biosSeldsk(uint8_t disk, uint8_t logged);
    void biosSettrk(uint16_t track);
    void biosSetsec(uint8_t sector);
    void biosSetdma(uint16_t addr);
    uint8_t biosRead();
    uint8_t biosWrite(uint8_t type);
    uint8_t biosListst();
    uint16_t biosSectran(uint16_t sector, uint16_t table);

    // CRT character processing (matching A5120 bioscrt.mac)
    void crtPutChar(uint8_t ch);
    void crtScroll();
    void crtClearScreen();
    void crtClearToEol();
    void crtCursorHome();
    void crtSetCursor(int row, int col);

    // Build DPH/DPB structures in memory
    void buildDiskParams();
};
