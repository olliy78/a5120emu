// cpa_bios.cpp - CPA BIOS Emulation Implementation
#include "cpa_bios.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <chrono>
#include <thread>

// CPA sector interleave table (26 sectors)
const uint8_t CpaBios::sectorXlat_[26] = {
    1,7,13,19,25,5,11,17,23,3,9,15,21,2,8,14,20,26,6,12,18,24,4,10,16,22
};

CpaBios::CpaBios(Z80& cpu, Memory& mem, Terminal& term)
    : cpu_(cpu), mem_(mem), term_(term),
      currentDisk_(0), currentTrack_(0), currentSector_(0), dmaAddr_(0x0080),
      cursorPos_(Memory::SCREEN_START), cursorVisible_(true),
      escapeState_(0), escapeRow_(0),
      tickCount_(0), running_(false),
      ccpBase(0), bdosBase(0), biosBase(0) {
    memset(dphAddr_, 0, sizeof(dphAddr_));
}

bool CpaBios::mountDisk(int drive, const std::string& imagePath) {
    if (drive < 0 || drive >= 4) return false;
    return disks_[drive].loadImage(imagePath);
}

bool CpaBios::mountDirectory(int drive, const std::string& dirPath) {
    if (drive < 0 || drive >= 4) return false;
    return disks_[drive].loadFromDirectory(dirPath);
}

void CpaBios::unmountDisk(int drive) {
    if (drive < 0 || drive >= 4) return;
    if (disks_[drive].isModified()) {
        // Auto-save modified images
        disks_[drive].saveImage("");
    }
}

bool CpaBios::bootFromFile(const std::string& osPath) {
    // Load @os.com at TPA (0x0100)
    if (!mem_.loadFile(osPath, TPA_START)) {
        fprintf(stderr, "Error: Cannot load %s\n", osPath.c_str());
        return false;
    }

    // @os.com structure (from binary analysis):
    //   File offset 0x00-0x7F: Self-relocating loader
    //   File offset 0x80+:     CCP + BDOS + BIOS (assembled for final addresses)
    //
    // The loader performs:
    //   LD DE, BA00h    ; destination
    //   LD HL, F456h    ; end address
    //   SBC HL, DE      ; HL = 3A56h = length
    //   LD BC, HL       ; BC = length
    //   POP HL          ; HL = 0180h = source (CCP start in loaded image)
    //   LDIR            ; copy CCP+BDOS+BIOS to BA00h
    //   JP D000h        ; jump to BIOS cold boot
    //
    // Memory layout after relocation:
    //   CCP:   BA00h - C1FFh (0x800 bytes)
    //   BDOS:  C200h - CFFFh (0xE00 bytes)
    //   BIOS:  D000h - F455h

    // Read relocation parameters from the loader code in the file
    // At file offset 0x6C (memory 0x016C): LD DE, <dest>
    // At file offset 0x6F (memory 0x016F): LD HL, <end>
    // At file offset 0x7A (memory 0x017A): JP <biosentry>
    uint16_t dest = mem_.read(0x016D) | (mem_.read(0x016E) << 8);        // BA00h
    uint16_t endAddr = mem_.read(0x0170) | (mem_.read(0x0171) << 8);     // F456h
    uint16_t biosEntry = mem_.read(0x017B) | (mem_.read(0x017C) << 8);   // D000h
    uint16_t length = endAddr - dest;
    uint16_t source = 0x0180; // CCP starts at file offset 0x80

    fprintf(stderr, "OS relocation: %04X -> %04X, length %04X, BIOS at %04X\n",
            source, dest, length, biosEntry);

    // Do the relocation ourselves (instead of running the loader)
    for (uint16_t i = 0; i < length; i++) {
        mem_.write(dest + i, mem_.read(source + i));
    }

    // Set up addresses
    ccpBase = dest;                       // BA00h
    bdosBase = ccpBase + 0x800;           // C200h
    biosBase = biosEntry;                 // D000h

    // Install our BIOS traps over the real BIOS jump table
    installTraps();

    // Set CPU start point to CCP
    cpu_.reset();
    cpu_.SP = ccpBase;
    cpu_.PC = ccpBase;

    return true;
}

bool CpaBios::bootFromDisk(int drive) {
    if (drive < 0 || drive >= 4 || !disks_[drive].isLoaded()) return false;

    // Read system tracks (tracks 0 and 1) into TPA area at 0x0100
    // This gives us the same @os.com content as a file boot
    uint8_t sector[128];
    uint16_t loadAddr = TPA_START;

    for (int t = 0; t < FloppyDisk::SYSTEM_TRACKS; t++) {
        for (int s = 0; s < FloppyDisk::CPM_SECTORS_PER_TRACK; s++) {
            if (disks_[drive].readSector(t, s, sector)) {
                mem_.load(loadAddr, sector, 128);
                loadAddr += 128;
            }
        }
    }

    // Now do the same relocation as bootFromFile
    uint16_t dest = mem_.read(0x016D) | (mem_.read(0x016E) << 8);
    uint16_t endAddr = mem_.read(0x0170) | (mem_.read(0x0171) << 8);
    uint16_t biosEntry = mem_.read(0x017B) | (mem_.read(0x017C) << 8);
    uint16_t length = endAddr - dest;
    uint16_t source = 0x0180;

    for (uint16_t i = 0; i < length; i++) {
        mem_.write(dest + i, mem_.read(source + i));
    }

    ccpBase = dest;
    bdosBase = ccpBase + 0x800;
    biosBase = biosEntry;
    currentDisk_ = drive;

    installTraps();

    cpu_.reset();
    cpu_.SP = ccpBase;
    cpu_.PC = ccpBase;

    return true;
}

void CpaBios::installTraps() {
    // Place HALT instructions directly at each BIOS jump table entry
    // Each entry is 3 bytes: HALT + NOP + NOP (replacing JP xxxx)
    // When CPU halts, we detect the address and handle the function
    for (int i = 0; i < BIOS_COUNT; i++) {
        uint16_t entryAddr = biosBase + i * 3;
        mem_.write(entryAddr, TRAP_OPCODE);  // HALT
        mem_.write(entryAddr + 1, 0x00);     // NOP
        mem_.write(entryAddr + 2, 0x00);     // NOP
    }

    // Set up page zero
    // 0x0000: JP to BIOS warm boot entry
    mem_.write(0x0000, 0xC3);
    mem_.write(0x0001, (biosBase + 3) & 0xFF);
    mem_.write(0x0002, ((biosBase + 3) >> 8) & 0xFF);

    // 0x0003: IOBYTE
    mem_.write(IOBYTE_ADDR, 0x00);

    // 0x0004: Current disk
    mem_.write(CDISK_ADDR, currentDisk_);

    // 0x0005: JP to BDOS entry (BDOS base + 6)
    mem_.write(BDOS_ENTRY, 0xC3);
    mem_.write(0x0006, (bdosBase + 6) & 0xFF);
    mem_.write(0x0007, ((bdosBase + 6) >> 8) & 0xFF);

    // Build DPH/DPB structures in BIOS area (after jump table)
    buildDiskParams();

    // Clear screen buffer
    mem_.fill(Memory::SCREEN_START, Memory::SCREEN_START + Memory::SCREEN_CHARS - 1, ' ');
    cursorPos_ = Memory::SCREEN_START;
}

void CpaBios::buildDiskParams() {
    // Build DPH (Disk Parameter Header) and DPB (Disk Parameter Block)
    // Place them after the BIOS jump table (17 entries × 3 bytes = 51 bytes)
    uint16_t base = biosBase + BIOS_COUNT * 3;

    // Write sector translate table (no translation for 40-sector format)

    // DPB for 800KB disk (A5120 standard): 
    // SPT=40 (logical 128-byte sectors per track for 5*1024=5120 bytes/track)
    // BSH=4, BLM=0F, EXM=1, DSM=194, DRM=127, AL0=C0, AL1=00
    // CKS=32, OFF=2
    uint16_t dpbAddr = base;
    // SPT
    mem_.write(dpbAddr + 0, 40);        // SPT low
    mem_.write(dpbAddr + 1, 0);         // SPT high
    // BSH
    mem_.write(dpbAddr + 2, 4);
    // BLM
    mem_.write(dpbAddr + 3, 0x0F);
    // EXM
    mem_.write(dpbAddr + 4, 1);
    // DSM (194 = disk size - 1 in blocks)
    mem_.write(dpbAddr + 5, 194);
    mem_.write(dpbAddr + 6, 0);
    // DRM (127 = dir entries - 1)
    mem_.write(dpbAddr + 7, 127);
    mem_.write(dpbAddr + 8, 0);
    // AL0, AL1
    mem_.write(dpbAddr + 9, 0xC0);
    mem_.write(dpbAddr + 10, 0x00);
    // CKS
    mem_.write(dpbAddr + 11, 32);
    mem_.write(dpbAddr + 12, 0);
    // OFF (system tracks)
    mem_.write(dpbAddr + 13, 2);
    mem_.write(dpbAddr + 14, 0);

    uint16_t dpbSize = 15;
    uint16_t afterDpb = dpbAddr + dpbSize;

    // Directory buffer (128 bytes)
    uint16_t dirBufAddr = afterDpb;
    afterDpb += 128;

    // For each drive, create DPH
    for (int d = 0; d < 4; d++) {
        dphAddr_[d] = afterDpb;
        uint16_t dph = afterDpb;

        // XLT pointer (0 = no translation for 40-sector format)
        mem_.write(dph + 0, 0);  // No sector translation
        mem_.write(dph + 1, 0);

        // Scratch areas (set to 0)
        for (int i = 2; i <= 7; i++) mem_.write(dph + i, 0);

        // DIRBF
        mem_.write(dph + 8, dirBufAddr & 0xFF);
        mem_.write(dph + 9, (dirBufAddr >> 8) & 0xFF);

        // DPB
        mem_.write(dph + 10, dpbAddr & 0xFF);
        mem_.write(dph + 11, (dpbAddr >> 8) & 0xFF);

        // CSV (check vector - 32 bytes per drive)
        uint16_t csvAddr = afterDpb + 16;
        mem_.write(dph + 12, csvAddr & 0xFF);
        mem_.write(dph + 13, (csvAddr >> 8) & 0xFF);

        // ALV (allocation vector - ceil(DSM/8)+1 bytes)
        uint16_t alvAddr = csvAddr + 32;
        mem_.write(dph + 14, alvAddr & 0xFF);
        mem_.write(dph + 15, (alvAddr >> 8) & 0xFF);

        afterDpb = alvAddr + 32; // 25 bytes needed for 194 blocks
    }
}

bool CpaBios::handleBiosTrap(uint16_t addr) {
    // addr is cpu_.PC which is HALT_address + 1 (PC advanced past HALT)
    uint16_t haltAddr = addr - 1;
    if (haltAddr < biosBase || haltAddr >= biosBase + BIOS_COUNT * 3) return false;
    if ((haltAddr - biosBase) % 3 != 0) return false;

    int func = (haltAddr - biosBase) / 3;
    cpu_.halted = false; // Resume from HALT

    switch (func) {
        case BIOS_BOOT:
            biosBoot();
            return true; // Boot sets PC directly, don't pop return address
        case BIOS_WBOOT:
            biosWboot();
            return true; // Wboot sets PC directly, don't pop return address
        case BIOS_CONST:
            cpu_.A = biosConst();
            break;
        case BIOS_CONIN:
            cpu_.A = biosConin();
            break;
        case BIOS_CONOUT:
            biosConout(cpu_.C);
            break;
        case BIOS_LIST:
            biosList(cpu_.C);
            break;
        case BIOS_PUNCH:
            cpu_.A = biosPunch(cpu_.C);
            break;
        case BIOS_READER:
            cpu_.A = biosReader();
            break;
        case BIOS_HOME:
            biosHome();
            break;
        case BIOS_SELDSK: {
            uint16_t dph = biosSeldsk(cpu_.C, cpu_.E);
            cpu_.HL = dph;
            break;
        }
        case BIOS_SETTRK:
            biosSettrk(cpu_.BC);
            break;
        case BIOS_SETSEC:
            biosSetsec(cpu_.C);
            break;
        case BIOS_SETDMA:
            biosSetdma(cpu_.BC);
            break;
        case BIOS_READ:
            cpu_.A = biosRead();
            break;
        case BIOS_WRITE:
            cpu_.A = biosWrite(cpu_.C);
            break;
        case BIOS_LISTST:
            cpu_.A = biosListst();
            break;
        case BIOS_SECTRAN: {
            uint16_t result = biosSectran(cpu_.BC, cpu_.DE);
            cpu_.HL = result;
            break;
        }
        default:
            return false;
    }

    // Return from BIOS call (RET) - pop return address pushed by caller's CALL
    cpu_.PC = cpu_.pop();
    return true;
}

bool CpaBios::handleBdosTrap() {
    // Not needed - we let the original BDOS code run natively
    // This is only used if we need to intercept specific BDOS calls
    return false;
}

// --- BIOS Function Implementations ---

void CpaBios::biosBoot() {
    // Cold boot - initialize everything
    mem_.write(IOBYTE_ADDR, 0x00);
    mem_.write(CDISK_ADDR, 0x00);
    currentDisk_ = 0;
    currentTrack_ = 0;
    currentSector_ = 0;
    dmaAddr_ = 0x0080;

    crtClearScreen();
    biosWboot();
}

void CpaBios::biosWboot() {
    // Warm boot - re-enter CCP command loop

    // Restore page zero
    mem_.write(0x0000, 0xC3);
    mem_.write(0x0001, (biosBase + 3) & 0xFF);
    mem_.write(0x0002, ((biosBase + 3) >> 8) & 0xFF);
    mem_.write(BDOS_ENTRY, 0xC3);
    mem_.write(0x0006, (bdosBase + 6) & 0xFF);
    mem_.write(0x0007, ((bdosBase + 6) >> 8) & 0xFF);
    mem_.write(CDISK_ADDR, currentDisk_);

    // Jump directly to CCP (no return address on stack)
    cpu_.SP = ccpBase;
    cpu_.PC = ccpBase;
}

uint8_t CpaBios::biosConst() {
    term_.pollEvents();
    return term_.keyAvailable() ? 0xFF : 0x00;
}

uint8_t CpaBios::biosConin() {
    // Wait for key press
    auto lastDisplay = std::chrono::steady_clock::now();
    while (!term_.shouldQuit()) {
        term_.pollEvents();
        if (term_.keyAvailable()) {
            return term_.getKey();
        }
        // Update display at ~50Hz while waiting for input
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDisplay).count() >= 20) {
            updateDisplay();
            lastDisplay = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    running_ = false;
    return 0x1A; // EOF
}

void CpaBios::biosConout(uint8_t ch) {
    crtPutChar(ch);
}

void CpaBios::biosList(uint8_t ch) {
    // List device - just echo to stdout for now
    if (ch >= 0x20 && ch < 0x7F) {
        // Could log to a file
    }
}

uint8_t CpaBios::biosPunch(uint8_t ch) {
    return 0;
}

uint8_t CpaBios::biosReader() {
    return 0x1A; // EOF
}

void CpaBios::biosHome() {
    currentTrack_ = 0;
}

uint16_t CpaBios::biosSeldsk(uint8_t disk, uint8_t logged) {
    if (disk >= 4) return 0; // Error: invalid drive

    // Check if drive has a mounted disk
    if (!disks_[disk].isLoaded() && logged == 0) {
        return 0; // No disk
    }

    currentDisk_ = disk;
    return dphAddr_[disk];
}

void CpaBios::biosSettrk(uint16_t track) {
    currentTrack_ = track;
}

void CpaBios::biosSetsec(uint8_t sector) {
    currentSector_ = sector;
}

void CpaBios::biosSetdma(uint16_t addr) {
    dmaAddr_ = addr;
}

uint8_t CpaBios::biosRead() {
    if (currentDisk_ < 0 || currentDisk_ >= 4) return 1;
    if (!disks_[currentDisk_].isLoaded()) return 1;

    uint8_t sector[128];
    if (!disks_[currentDisk_].readSector(currentTrack_, currentSector_, sector)) {
        return 1; // Error
    }

    // Copy to DMA buffer
    mem_.load(dmaAddr_, sector, 128);
    return 0; // Success
}

uint8_t CpaBios::biosWrite(uint8_t type) {
    if (currentDisk_ < 0 || currentDisk_ >= 4) return 1;
    if (!disks_[currentDisk_].isLoaded()) return 1;

    uint8_t sector[128];
    for (int i = 0; i < 128; i++) {
        sector[i] = mem_.read(dmaAddr_ + i);
    }

    if (!disks_[currentDisk_].writeSector(currentTrack_, currentSector_, sector)) {
        return 1; // Error
    }

    return 0; // Success
}

uint8_t CpaBios::biosListst() {
    return 0xFF; // Always ready
}

uint16_t CpaBios::biosSectran(uint16_t sector, uint16_t table) {
    if (table == 0) {
        // No translation
        return sector;
    }

    // Read translated sector from table
    return mem_.read(table + sector);
}

// --- CRT Emulation ---

void CpaBios::crtPutChar(uint8_t ch) {
    // Handle escape sequences (ADM3A compatible for CPA)
    if (escapeState_ > 0) {
        if (escapeState_ == 1) {
            // First char after ESC: could be '=' for cursor positioning
            if (ch == '=' || ch == 'Y') {
                escapeState_ = 2;
                return;
            }
            // Other escape sequences - ignore for now
            escapeState_ = 0;
            return;
        }
        if (escapeState_ == 2) {
            // Row (biased by 0x20)
            escapeRow_ = ch - 0x20;
            escapeState_ = 3;
            return;
        }
        if (escapeState_ == 3) {
            // Column (biased by 0x20)
            uint8_t col = ch - 0x20;
            crtSetCursor(escapeRow_, col);
            escapeState_ = 0;
            return;
        }
    }

    // Control characters
    switch (ch) {
        case 0x00: // NOP
            break;
        case 0x01: // HOME
            crtCursorHome();
            break;
        case 0x02: // Cursor ON
            cursorVisible_ = true;
            break;
        case 0x03: // Cursor OFF
            cursorVisible_ = false;
            break;
        case 0x07: // BELL
        case 0x87: // BELL (alternate)
            term_.bell();
            break;
        case 0x08: // Backspace
            if (cursorPos_ > Memory::SCREEN_START) {
                cursorPos_--;
            }
            break;
        case 0x0A: // Line feed
            cursorPos_ += Memory::SCREEN_COLS;
            if (cursorPos_ >= Memory::SCREEN_START + Memory::SCREEN_CHARS) {
                cursorPos_ -= Memory::SCREEN_COLS;
                crtScroll();
            }
            break;
        case 0x0C: // Clear screen
            crtClearScreen();
            break;
        case 0x0D: // Carriage return
            cursorPos_ = Memory::SCREEN_START +
                ((cursorPos_ - Memory::SCREEN_START) / Memory::SCREEN_COLS) * Memory::SCREEN_COLS;
            break;
        case 0x14: // Clear to end of screen
            for (uint16_t i = cursorPos_; i < Memory::SCREEN_START + Memory::SCREEN_CHARS; i++) {
                mem_.write(i, ' ');
            }
            break;
        case 0x15: // Cursor right
            cursorPos_++;
            if (cursorPos_ >= Memory::SCREEN_START + Memory::SCREEN_CHARS) {
                cursorPos_ = Memory::SCREEN_START + Memory::SCREEN_CHARS - 1;
            }
            break;
        case 0x16: // Clear to end of line
            crtClearToEol();
            break;
        case 0x17: // Insert line
        {
            int row = (cursorPos_ - Memory::SCREEN_START) / Memory::SCREEN_COLS;
            // Move lines down
            for (int r = Memory::SCREEN_ROWS - 1; r > row; r--) {
                for (int c = 0; c < Memory::SCREEN_COLS; c++) {
                    mem_.write(Memory::SCREEN_START + r * Memory::SCREEN_COLS + c,
                              mem_.read(Memory::SCREEN_START + (r-1) * Memory::SCREEN_COLS + c));
                }
            }
            // Clear current line
            for (int c = 0; c < Memory::SCREEN_COLS; c++) {
                mem_.write(Memory::SCREEN_START + row * Memory::SCREEN_COLS + c, ' ');
            }
            break;
        }
        case 0x18: // Clear line + cursor to start
        {
            int row = (cursorPos_ - Memory::SCREEN_START) / Memory::SCREEN_COLS;
            for (int c = 0; c < Memory::SCREEN_COLS; c++) {
                mem_.write(Memory::SCREEN_START + row * Memory::SCREEN_COLS + c, ' ');
            }
            cursorPos_ = Memory::SCREEN_START + row * Memory::SCREEN_COLS;
            break;
        }
        case 0x19: // Delete line
        {
            int row = (cursorPos_ - Memory::SCREEN_START) / Memory::SCREEN_COLS;
            // Move lines up
            for (int r = row; r < Memory::SCREEN_ROWS - 1; r++) {
                for (int c = 0; c < Memory::SCREEN_COLS; c++) {
                    mem_.write(Memory::SCREEN_START + r * Memory::SCREEN_COLS + c,
                              mem_.read(Memory::SCREEN_START + (r+1) * Memory::SCREEN_COLS + c));
                }
            }
            // Clear last line
            for (int c = 0; c < Memory::SCREEN_COLS; c++) {
                mem_.write(Memory::SCREEN_START + (Memory::SCREEN_ROWS - 1) * Memory::SCREEN_COLS + c, ' ');
            }
            break;
        }
        case 0x1A: // Cursor up
            if (cursorPos_ >= Memory::SCREEN_START + Memory::SCREEN_COLS) {
                cursorPos_ -= Memory::SCREEN_COLS;
            }
            break;
        case 0x1B: // ESC - start escape sequence
            escapeState_ = 1;
            break;
        case 0x1C: // Cursor up (ADM3A alternate)
            if (cursorPos_ >= Memory::SCREEN_START + Memory::SCREEN_COLS) {
                cursorPos_ -= Memory::SCREEN_COLS;
            }
            break;
        case 0x7F: // Delete
            mem_.write(cursorPos_, ' ');
            break;
        default:
            if (ch >= 0x20 && ch < 0x7F) {
                // Normal printable character
                mem_.write(cursorPos_, ch);
                cursorPos_++;
                if (cursorPos_ >= Memory::SCREEN_START + Memory::SCREEN_CHARS) {
                    cursorPos_ -= Memory::SCREEN_COLS;
                    crtScroll();
                }
            }
            // Characters 0x84-0x87 are video attributes on K7024
            // They just write the character code directly
            else if (ch >= 0x84 && ch <= 0x86) {
                mem_.write(cursorPos_, ch & 0x7F);
                cursorPos_++;
                if (cursorPos_ >= Memory::SCREEN_START + Memory::SCREEN_CHARS) {
                    cursorPos_ -= Memory::SCREEN_COLS;
                    crtScroll();
                }
            }
            break;
    }
}

void CpaBios::crtScroll() {
    // Scroll screen up by one line
    for (int r = 0; r < Memory::SCREEN_ROWS - 1; r++) {
        for (int c = 0; c < Memory::SCREEN_COLS; c++) {
            mem_.write(Memory::SCREEN_START + r * Memory::SCREEN_COLS + c,
                      mem_.read(Memory::SCREEN_START + (r+1) * Memory::SCREEN_COLS + c));
        }
    }
    // Clear bottom line
    for (int c = 0; c < Memory::SCREEN_COLS; c++) {
        mem_.write(Memory::SCREEN_START + (Memory::SCREEN_ROWS - 1) * Memory::SCREEN_COLS + c, ' ');
    }
}

void CpaBios::crtClearScreen() {
    mem_.fill(Memory::SCREEN_START, Memory::SCREEN_START + Memory::SCREEN_CHARS - 1, ' ');
    cursorPos_ = Memory::SCREEN_START;
}

void CpaBios::crtClearToEol() {
    int col = (cursorPos_ - Memory::SCREEN_START) % Memory::SCREEN_COLS;
    int row = (cursorPos_ - Memory::SCREEN_START) / Memory::SCREEN_COLS;
    for (int c = col; c < Memory::SCREEN_COLS; c++) {
        mem_.write(Memory::SCREEN_START + row * Memory::SCREEN_COLS + c, ' ');
    }
}

void CpaBios::crtCursorHome() {
    cursorPos_ = Memory::SCREEN_START;
}

void CpaBios::crtSetCursor(int row, int col) {
    if (row < 0) row = 0;
    if (row >= Memory::SCREEN_ROWS) row = Memory::SCREEN_ROWS - 1;
    if (col < 0) col = 0;
    if (col >= Memory::SCREEN_COLS) col = Memory::SCREEN_COLS - 1;
    cursorPos_ = Memory::SCREEN_START + row * Memory::SCREEN_COLS + col;
}

void CpaBios::updateDisplay() {
    // Set cursor marker (bit 7) at current position
    if (cursorVisible_) {
        uint8_t ch = mem_.read(cursorPos_);
        mem_.write(cursorPos_, ch | 0x80);
    }

    term_.updateScreen(mem_.screenBuffer(), Memory::SCREEN_ROWS, Memory::SCREEN_COLS);

    // Remove cursor marker
    if (cursorVisible_) {
        uint8_t ch = mem_.read(cursorPos_);
        mem_.write(cursorPos_, ch & 0x7F);
    }
}

void CpaBios::tick() {
    tickCount_++;
    if (tickCount_ % 50 == 0) { // ~50Hz display update
        updateDisplay();
    }
}

void CpaBios::run() {
    running_ = true;
    auto lastUpdate = std::chrono::steady_clock::now();
    constexpr int CYCLES_PER_FRAME = 2500000 / 50; // 2.5 MHz / 50 Hz = 50000 cycles per frame

    while (running_ && !term_.shouldQuit()) {
        int frameCycles = 0;

        while (frameCycles < CYCLES_PER_FRAME) {
            // Check if halted (BIOS trap or real HALT)
            if (cpu_.halted) {
                uint16_t haltAddr = cpu_.PC - 1;
                if (haltAddr >= biosBase && haltAddr < biosBase + BIOS_COUNT * 3
                    && (haltAddr - biosBase) % 3 == 0) {
                    if (!handleBiosTrap(cpu_.PC)) {
                        fprintf(stderr, "Unhandled BIOS trap at %04X\n", haltAddr);
                        running_ = false;
                        break;
                    }
                    continue;
                } else {
                    // Real HALT (waiting for interrupt) - just idle
                    cpu_.halted = false;
                    frameCycles += 4;
                    continue;
                }
            }

            // Check for warm boot (JP 0x0000)
            if (cpu_.PC == 0x0000) {
                biosWboot();
                continue;
            }

            frameCycles += cpu_.step();
        }

        // Update display
        term_.pollEvents();
        updateDisplay();

        // Timing control - maintain ~50Hz frame rate
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - lastUpdate);
        auto targetFrame = std::chrono::microseconds(20000); // 20ms = 50Hz
        if (elapsed < targetFrame) {
            std::this_thread::sleep_for(targetFrame - elapsed);
        }
        lastUpdate = std::chrono::steady_clock::now();
    }
}
