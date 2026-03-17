/**
 * @file cpa_bios.cpp
 * @brief Implementierung der CPA-BIOS-Emulation fuer den Robotron A5120.
 *
 * Diese Datei implementiert die Emulation des CPA-BIOS (CP/M-kompatibles
 * Betriebssystem der DDR), der Floppydisk-Steuerung (Baugruppe K5122),
 * der CPU2-Formatierungs-Emulation (Baugruppe K2526) und der
 * Bildschirmausgabe (Baugruppe K7027).
 *
 * @section trap_detail Detaillierte Beschreibung des Trap-Mechanismus
 *
 * Das CPA-BIOS wird aus der Datei \@os.com geladen, die einen
 * selbst-relozierenden Loader und den kompletten CPA-Systemkern
 * (CCP + BDOS + BIOS) enthaelt. Nach der Relokation befinden sich:
 * - CCP  bei BA00h (Console Command Processor, 2048 Bytes)
 * - BDOS bei C200h (Basic Disk Operating System, 3584 Bytes)
 * - BIOS bei D000h (Basic I/O System, ~9300 Bytes)
 *
 * Die BIOS-Sprungtabelle besteht aus 17 JP-Instruktionen (je 3 Bytes),
 * die normalerweise zu den jeweiligen Handler-Routinen springen.
 * Der Emulator ersetzt diese JP-Instruktionen durch HALT-Opcodes:
 *
 * @code
 *   D000h: HALT (76h) + NOP NOP   ; war: JP biosBoot
 *   D003h: HALT (76h) + NOP NOP   ; war: JP biosWboot
 *   D006h: HALT (76h) + NOP NOP   ; war: JP biosConst
 *   ...
 *   D030h: HALT (76h) + NOP NOP   ; war: JP biosSectran
 * @endcode
 *
 * Wenn der Z80-Emulator einen HALT-Opcode ausfuehrt:
 * 1. Z80::step() setzt cpu_.halted = true und PC zeigt auf HALT+1
 * 2. Die run()-Schleife erkennt halted == true
 * 3. Sie berechnet haltAddr = PC - 1 und prueft:
 *    - Liegt haltAddr im Bereich [biosBase, biosBase + 17*3)?
 *    - Ist (haltAddr - biosBase) durch 3 teilbar?
 *    - Oder ist es die cpmx21-Adresse (diskio-Erweiterung)?
 * 4. Falls ja: handleBiosTrap() identifiziert die Funktionsnummer
 *    und ruft die C++-Implementierung auf
 * 5. Die Ruecksprungadresse (die der CALL auf den Stack gelegt hat)
 *    wird mit cpu_.pop() geholt und in PC geschrieben
 *
 * Das originale BDOS laeuft unveraendert im Z80-Emulator. Nur das BIOS
 * wird durch C++-Code ersetzt. Die BIOS-Daten nach der Sprungtabelle
 * (Versionstring "CPAB6", DPH/DPB-Tabellen, ISR-Code, Variablen)
 * bleiben im Z80-Speicher erhalten und werden von Programmen gelesen.
 *
 * @section cpu2_detail CPU2-Emulation (K2526 Dual-CPU-Architektur)
 *
 * Die CPU-Karte K2526 besitzt zwei Z80/U880-Prozessoren:
 * - CPU1: Hauptprozessor fuer CPA/CP/M und Anwendungen
 * - CPU2: Dedizierter Disk-Prozessor mit eigenem EPROM
 *
 * Bei Formatier-Operationen parkt CPU1 in einer JR $-Schleife
 * (Opcode 18h FEh = JR -2, d.h. Sprung auf sich selbst), waehrend
 * CPU2 die MFM-kodierten Spurdaten mit exaktem 32us-Timing pro Byte
 * (250 kbit/s Double Density) byteweise ueber die K5122-Baugruppe
 * auf die Diskette schreibt.
 *
 * Der Emulator erkennt dieses Muster (JR $ + Schreibmodus + PIO-Interrupt
 * aktiv) und fuehrt die Formatierung direkt durch:
 * 1. Alle 26 logischen Sektoren der Spur werden mit E5h gefuellt
 * 2. Der JR $-Displacement wird von FEh auf 00h gepatcht, sodass
 *    CPU1 die Schleife verlaesst und den nachfolgenden Cleanup-Code
 *    ausfuehrt (SP-Wiederherstellung, PIO-Interrupt deaktivieren)
 * 3. Das format.com-Programm stellt den Displacement vor jedem
 *    neuen Spur-Aufruf wieder auf FEh zurueck (Selbstmodifikation
 *    bei Adresse 1CEEh)
 *
 * @section crt_detail Bildschirmausgabe (K7027)
 *
 * Die Bildschirmkarte K7027 stellt einen 80x24-Zeichen-Textbildschirm
 * zur Verfuegung. In der Emulation wird der Bildschirmpuffer als
 * 1920-Byte-Array im Z80-Speicher ab F800h verwaltet. Jedes Byte
 * entspricht einem ASCII-Zeichen auf dem Bildschirm. Die Zeichenausgabe
 * (BIOS-Funktion CONOUT) wird ueber crtPutChar() abgewickelt, die eine
 * vollstaendige Zustandsmaschine fuer ESC-Sequenzen und Steuerzeichen
 * implementiert.
 *
 * @author Olaf Krieger
 * @license MIT License
 *
 * Copyright (c) Olaf Krieger
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "cpa_bios.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <chrono>
#include <thread>

/**
 * @brief CPA-Sektor-Interleave-Tabelle.
 *
 * Die 26 Eintraege bilden logische Sektornummern auf physische Positionen ab.
 * Diese Verschachtelung sorgt dafuer, dass aufeinanderfolgende logische
 * Sektoren physisch ueber die Spur verteilt sind, sodass nach dem Lesen
 * eines Sektors genug Zeit bleibt, die Daten zu verarbeiten, bevor der
 * naechste logische Sektor unter dem Lesekopf vorbeikommt.
 */
const uint8_t CpaBios::sectorXlat_[26] = {
    1,7,13,19,25,5,11,17,23,3,9,15,21,2,8,14,20,26,6,12,18,24,4,10,16,22
};

/**
 * @brief Konstruktor - initialisiert alle Emulationszustaende.
 *
 * Setzt die FDC-Steuerregister auf Ruhezustand (fdcControlA_ = 0xFF,
 * d.h. alle aktiv-low Signale inaktiv), den PIO-Interruptvektor auf
 * den BIOS-Standard (0xE8) und die DMA-Adresse auf den CP/M-Standard
 * (0080h = Default-DMA-Puffer in der Page Zero).
 */
CpaBios::CpaBios(Z80& cpu, Memory& mem, Terminal& term)
    : cpu_(cpu), mem_(mem), term_(term),
      currentDisk_(0), currentTrack_(0), currentSector_(0), dmaAddr_(0x0080),
      cursorPos_(Memory::SCREEN_START), cursorVisible_(true),
      escapeState_(0), escapeRow_(0),
      tickCount_(0), running_(false),
      consoleLog_(nullptr), diagLog_(nullptr),
      autoExit_(false), execDone_(false),
      ccpBase(0), bdosBase(0), biosBase(0),
      cpmx21Addr_(0),
      fdcDriveSelect_(0), fdcTrack_(0), fdcSide_(0), fdcAtTrack0_(true),
      fdcControlA_(0xFF), fdcWriteMode_(false), fdcWriteCount_(0),
      fdcPioAVector_(0xE8), fdcPioAIntEnabled_(false), fdcIndexCycles_(0),
      fdcFormatDone_(false) {
    memset(dphAddr_, 0, sizeof(dphAddr_));
}

/// @copydoc CpaBios::diagf
void CpaBios::diagf(const char* fmt, ...) {
    FILE* out = diagLog_ ? diagLog_ : stderr;
    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);
    fflush(out);
}

/// @copydoc CpaBios::mountDisk
bool CpaBios::mountDisk(int drive, const std::string& imagePath) {
    if (drive < 0 || drive >= 4) return false;
    return disks_[drive].loadImage(imagePath);
}

/// @copydoc CpaBios::mountDirectory
bool CpaBios::mountDirectory(int drive, const std::string& dirPath) {
    if (drive < 0 || drive >= 4) return false;
    return disks_[drive].loadFromDirectory(dirPath);
}

/// @copydoc CpaBios::unmountDisk
void CpaBios::unmountDisk(int drive) {
    if (drive < 0 || drive >= 4) return;
    if (disks_[drive].isModified()) {
        // Auto-save modified images
        disks_[drive].saveImage("");
    }
}

/**
 * @brief Startet das System von einer \@os.com-Datei.
 *
 * Die Datei \@os.com hat folgende Struktur:
 * - Offset 00h-7Fh: Selbst-relozierender Loader
 * - Offset 80h+:    CCP + BDOS + BIOS (fuer Endadressen assembliert)
 *
 * Der Loader fuehrt normalerweise folgende Operationen aus:
 * @code
 *   LD DE, BA00h    ; Zieladresse
 *   LD HL, F456h    ; Endadresse
 *   SBC HL, DE      ; HL = 3A56h = Laenge
 *   LD BC, HL       ; BC = Laenge
 *   POP HL          ; HL = 0180h = Quelle (CCP-Beginn im geladenen Image)
 *   LDIR            ; CCP+BDOS+BIOS nach BA00h kopieren
 *   JP D000h        ; Sprung zum BIOS-Kaltstart
 * @endcode
 *
 * Der Emulator liest die Relokationsparameter direkt aus dem Loader-Code
 * und fuehrt die Kopie selbst durch, anstatt den Loader im Z80 laufen
 * zu lassen. Danach werden die HALT-Traps installiert und der CCP gestartet.
 *
 * @note Nach cpu_.reset() muessen I=F7h und IM=2 gesetzt werden,
 *       da reset() diese Werte auf 0 zuruecksetzt. Ohne I=F7h
 *       wuerden IM2-Interrupts in den Bereich 00xxh dispatchen
 *       statt nach F7xxh, was zu Abstuerzern fuehrt.
 */
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

    diagf("OS relocation: %04X -> %04X, length %04X, BIOS at %04X\n",
           source, dest, length, biosEntry);

    // Do the relocation ourselves (instead of running the loader)
    for (uint16_t i = 0; i < length; i++) {
        mem_.write(dest + i, mem_.read(source + i));
    }

    // Set up addresses
    ccpBase = dest;                       // BA00h
    bdosBase = ccpBase + 0x800;           // C200h
    biosBase = biosEntry;                 // D000h

    // Save CCP code for warm boot restoration (transient programs may overwrite it)
    ccpImage_.resize(0x800);
    for (int i = 0; i < 0x800; i++)
        ccpImage_[i] = mem_.read(ccpBase + i);

    // Install our BIOS traps over the real BIOS jump table
    installTraps();

    // Set CPU start point to CCP
    cpu_.reset();
    cpu_.PC = ccpBase;
    cpu_.SP = ccpBase;

    // Set up Z80 interrupt mode 2 and I register (must be after cpu_.reset())
    cpu_.I = 0xF7;
    cpu_.IM = 2;
    cpu_.IFF1 = true;
    cpu_.IFF2 = true;

    return true;
}

/**
 * @brief Startet das System von einem Disketten-Image.
 *
 * Liest die beiden System-Spuren (Spur 0 und 1, je 26 Sektoren a 128 Bytes)
 * in den TPA-Bereich ab 0100h. Der Inhalt entspricht dann dem einer
 * \@os.com-Datei, sodass die gleiche Relokation wie in bootFromFile()
 * durchgefuehrt werden kann.
 *
 * @param drive Laufwerksnummer (0-3)
 * @return true bei Erfolg, false wenn Laufwerk ungueltig oder nicht geladen
 */
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

    // Save CCP code for warm boot restoration (transient programs may overwrite it)
    ccpImage_.resize(0x800);
    for (int i = 0; i < 0x800; i++)
        ccpImage_[i] = mem_.read(ccpBase + i);

    installTraps();

    cpu_.reset();
    cpu_.PC = ccpBase;
    cpu_.SP = ccpBase;

    // Set up Z80 interrupt mode 2 and I register (must be after cpu_.reset())
    cpu_.I = 0xF7;
    cpu_.IM = 2;
    cpu_.IFF1 = true;
    cpu_.IFF2 = true;

    return true;
}

/**
 * @brief Installiert HALT-Traps und richtet die Systemumgebung ein.
 *
 * Diese zentrale Initialisierungsfunktion fuehrt folgende Schritte aus:
 *
 * **1. BIOS-Sprungtabelle ersetzen (D000h-D032h)**
 *
 * Die 17 BIOS-Einsprungpunkte (je 3 Bytes: JP addr) werden durch
 * HALT + NOP + NOP ersetzt. Alle anderen BIOS-Daten bleiben erhalten:
 * - Versionstring "CPAB6" bei biosBase+33h
 * - DPH-Tabellen bei biosBase+3Fh/4Fh/5Fh
 * - DPB-Tabellen mit CPA-Erweiterungsfeldern
 * - ISR-Code (itimeo bei biosBase+11ADh)
 * - Variablen und Stack-Bereiche
 *
 * **2. Page Zero einrichten (0000h-007Fh)**
 * @code
 *   0000h: JP biosBase+3    ; Warmstart-Einsprung
 *   0003h: 00              ; IOBYTE
 *   0004h: currentDisk_     ; Aktuelles Laufwerk
 *   0005h: JP bdosBase+6    ; BDOS-Einsprung
 *   004Eh: cpmx00 (word)    ; CPA-Erweiterungstabelle
 * @endcode
 *
 * **3. CPA-Erweiterungen (cpmx00 bei biosBase+108h)**
 *
 * Die CPA-Erweiterungstabelle enthaelt 11 Eintraege a 3 Bytes.
 * Nur cpmx21 (diskio, bei cpmx00+21h) wird mit einem HALT-Trap
 * versehen. Alle anderen Eintraege (Monitor, Timer, etc.) werden
 * mit RET (C9h) gestubbt, damit sie nicht abstuerzen.
 *
 * **4. IM2-Interruptvektortabelle (F7E8h/F7EAh)**
 *
 * Die Vektortabelle fuer die Disk-PIO-Interrupts wird initialisiert:
 * - F7E8h (ivdsk1, Index-Puls): Zeigt auf itimeo (biosBase+11ADh)
 * - F7EAh (ivdsk2, Marke/Fehler): Zeigt auf itimeo
 *
 * Format.com sichert und restauriert diese Vektoren, wenn es seine
 * eigenen ISR-Handler installiert.
 *
 * **5. Bildschirmpuffer loeschen**
 *
 * Der Bildschirmspeicher der K7027-Karte (ab F800h, 1920 Bytes)
 * wird mit Leerzeichen gefuellt.
 */
void CpaBios::installTraps() {
    // Place HALT instructions only at the BIOS jump table entries (first 17*3 bytes)
    // Preserve all original BIOS data after the jump table (version string, DPH/DPB, etc.)
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

    // Set up CPA extension table pointer at 0x004E
    // The original BIOS stores cpmx00 address here during cold boot
    // cpmx00 is at biosBase + 0x108 (from bios.prn: D108 for biosBase=D000)
    uint16_t cpmx00 = biosBase + 0x108;
    mem_.write(CPMEXT_ADDR, cpmx00 & 0xFF);
    mem_.write(CPMEXT_ADDR + 1, (cpmx00 >> 8) & 0xFF);

    // Install HALT trap at cpmx21 (diskio extension) so we can intercept it
    cpmx21Addr_ = cpmx00 + 0x21;  // biosBase + 0x129
    mem_.write(cpmx21Addr_, TRAP_OPCODE);
    mem_.write(cpmx21Addr_ + 1, 0x00);
    mem_.write(cpmx21Addr_ + 2, 0x00);

    // Stub other extension entries with RET so they don't crash if called
    // cpmx00: monitor call → RET (already has RET/C9 in original BIOS)
    // cpmx03: tim5on → RET
    // cpmx06: tim5of → RET
    // cpmx09: tim1on → RET
    // cpmx0c: tim1of → RET
    // cpmx18: delsps → RET
    // cpmx1b: delspr → RET
    // cpmx1e: crtuml → RET
    static const int extStubs[] = { 0x03, 0x06, 0x09, 0x0C, 0x0F, 0x12, 0x15, 0x18, 0x1B, 0x1E };
    for (int off : extStubs) {
        mem_.write(cpmx00 + off, 0xC9); // RET
    }

    // Build DPH/DPB structures in safe area (0xF500+), without overwriting BIOS data
    // This preserves the original BIOS version string at biosBase+0x33 ("CPAB6")
    // and all other BIOS tables that programs like format.com may inspect
    buildDiskParams();

    // Log the BIOS version string area for diagnostics
    diagf("BIOS version string at %04X: ", biosBase + 0x33);
    for (int i = 0; i < 10; i++) {
        uint8_t ch = mem_.read(biosBase + 0x33 + i);
        diagf("%02X(%c) ", ch, (ch >= 0x20 && ch < 0x7F) ? ch : '.');
    }
    diagf("\n");

    // Initialize the interrupt vector table at F7E8 (ivdsk1 = index pulse)
    // and F7EA (ivdsk2 = mark/fault) with the BIOS itimeo handler address.
    // The itimeo handler is at biosBase + 0x11AD (E1AD for biosBase=D000).
    // Format.com saves and restores these vectors when it installs its own handlers.
    uint16_t itimeoAddr = biosBase + 0x11AD;  // From bios.prn: E1AD
    uint16_t ivdsk1 = 0xF7E8;
    uint16_t ivdsk2 = 0xF7EA;
    mem_.write(ivdsk1, itimeoAddr & 0xFF);
    mem_.write(ivdsk1 + 1, (itimeoAddr >> 8) & 0xFF);
    mem_.write(ivdsk2, itimeoAddr & 0xFF);
    mem_.write(ivdsk2 + 1, (itimeoAddr >> 8) & 0xFF);

    // Initialize itimeo's stack and variables
    // intsp (at biosBase + 0x14F8) - interrupt SP save area
    // intstk (at biosBase + 0x200C) - interrupt stack
    // fl.zto (at biosBase + 0x11B6) - timeout counter
    // pretx displacement (at biosBase + 0x11BE) - set to default (jr pret3)
    // These are already present in the loaded BIOS code, just ensure fl.zto is 0
    mem_.write(biosBase + 0x11B6, 0);  // fl.zto = 0

    // Set PIO A interrupt vector to 0xE8 (matching BIOS cold boot setup)
    fdcPioAVector_ = 0xE8;

    // Clear screen buffer
    mem_.fill(Memory::SCREEN_START, Memory::SCREEN_START + Memory::SCREEN_CHARS - 1, ' ');
    cursorPos_ = Memory::SCREEN_START;
}

/**
 * @brief Initialisiert die DPH/DPB-Strukturen aus dem originalen BIOS.
 *
 * Verwendet die Disk Parameter Header (DPH) und Disk Parameter Blocks
 * (DPB) direkt aus dem geladenen BIOS-Code. Diese enthalten die
 * CPA-spezifischen Erweiterungsfelder:
 *
 * Standardmaessige CP/M-DPB-Felder (15 Bytes):
 * @code
 *   +0,+1: SPT   Sektoren pro Spur (26 fuer A5120)
 *   +2:    BSH   Block-Shift (4 = 2KB Bloecke)
 *   +3:    BLM   Block-Maske (0Fh)
 *   +4:    EXM   Extent-Maske
 *   +5,+6: DSM   Gesamtanzahl Bloecke - 1
 *   +7,+8: DRM   Verzeichniseintraege - 1
 *   +9,+10: AL0/AL1  Verzeichnis-Allokationsbitmap
 *   +11,+12: CKS  Pruefsummenvektor-Groesse
 *   +13,+14: OFF  Systemspuren (Offset)
 * @endcode
 *
 * CPA-Erweiterungsfelder (ab Offset 15):
 * @code
 *   +15: dpbflg  Flags
 *   +16: dpbslc  Sektorlaengen-Code (3=1024 Bytes)
 *   +22: dpbptr  Spuren pro Seite
 *   +24: dpbtyp  Laufwerkstyp-Bits:
 *                Bit 0: DS (doppelseitig)
 *                Bit 3: DD (doppelte Dichte)
 *                Bit 4: 5.25" Format
 *                Bit 5: 80 Spuren
 * @endcode
 *
 * DPH-Layout (aus dem BIOS bei biosBase+3Fh/4Fh/5Fh):
 * @code
 *   dphA = biosBase + 03Fh (Laufwerk A:)
 *   dphB = biosBase + 04Fh (Laufwerk B:)
 *   dphC = biosBase + 05Fh (Laufwerk C:)
 * @endcode
 *
 * @note Laufwerk D: hat keinen DPH-Offset und gibt 0 zurueck (nicht konfiguriert).
 */
void CpaBios::buildDiskParams() {
    // Use the ORIGINAL DPH/DPB structures from the loaded BIOS code.
    //
    // The loaded @os.com BIOS contains complete DPH and DPB data including
    // CPA extension fields (dpbtyp, dpbflg, dpbslc, dpbdnr, dpbptr, etc.)
    // that programs like format.com read to determine drive types.
    //
    // BIOS memory layout (from bios.prn):
    //   biosBase+0x3F (D03Fh) = dphA  -> DPB at biosBase+0x6F (dpbtyp=0x39)
    //   biosBase+0x4F (D04Fh) = dphB  -> DPB at biosBase+0xA2 (dpbtyp=0x39)
    //   biosBase+0x5F (D05Fh) = dphC  -> DPB at biosBase+0xD5 (dpbtyp=0x39)
    //   biosBase+0x177A (E77Ah) = dphatb (DPH address table, 13 entries)
    //   biosBase+0x200C (F00Ch) = dirbuf (shared directory buffer)
    //
    // dpbtyp=0x39 means: DD, DS, 5.25", 80-track (standard A5120 config)
    //
    // We read the DPH addresses directly from the original code.

    static const uint16_t dphOffsets[4] = { 0x3F, 0x4F, 0x5F, 0x00 };

    for (int d = 0; d < 4; d++) {
        if (dphOffsets[d] != 0) {
            dphAddr_[d] = biosBase + dphOffsets[d];

            // Read the DPB pointer from DPH+10 and log the drive type
            uint16_t dph = dphAddr_[d];
            uint16_t dpbAddr = mem_.read(dph + 10) | (mem_.read(dph + 11) << 8);
            if (dpbAddr != 0) {
                uint8_t dpbtyp = mem_.read(dpbAddr + 24);
                uint8_t dpbflg = mem_.read(dpbAddr + 15);
                uint8_t dpbptr = mem_.read(dpbAddr + 22);
                uint8_t dpbslc = mem_.read(dpbAddr + 16);
                diagf("Drive %c: DPH=%04X DPB=%04X dpbtyp=%02X dpbflg=%02X "
                      "dpbptr=%d dpbslc=%d (%s %s %s %d-trk)\n",
                      'A' + d, dph, dpbAddr, dpbtyp, dpbflg, dpbptr, dpbslc,
                      (dpbtyp & 0x10) ? "5.25\"" : "8\"",
                      (dpbtyp & 0x01) ? "DS" : "SS",
                      (dpbtyp & 0x08) ? "DD" : "SD",
                      (dpbtyp & 0x20) ? 80 : ((dpbptr == 77) ? 77 : 40));
            }
        } else {
            dphAddr_[d] = 0;
        }
    }
}

/**
 * @brief Behandelt einen BIOS-Trap.
 *
 * Wird aufgerufen, wenn die Z80-CPU auf einem HALT-Opcode stehenbleibt.
 * PC zeigt zu diesem Zeitpunkt bereits auf HALT+1 (Z80 inkrementiert PC
 * nach dem Ausfuehren des HALT-Opcodes).
 *
 * Ablauf:
 * 1. haltAddr = addr - 1 (die tatsaechliche HALT-Adresse)
 * 2. Pruefen ob haltAddr == cpmx21Addr_ (diskio-Erweiterung)
 * 3. Pruefen ob haltAddr im BIOS-Sprungtabellen-Bereich liegt
 *    und durch 3 teilbar ist (jeder Eintrag = 3 Bytes)
 * 4. Funktionsnummer berechnen: func = (haltAddr - biosBase) / 3
 * 5. Entsprechende C++-Methode aufrufen
 * 6. Registerergebnisse in Z80-CPU-Register schreiben (z.B. A, HL)
 * 7. Ruecksprungadresse vom Stack holen und in PC setzen
 *
 * Sonderfaelle BOOT und WBOOT: Diese setzen PC direkt (auf ccpBase)
 * und holen keine Ruecksprungadresse vom Stack, da sie nicht per
 * RET zurueckkehren sondern den CCP direkt starten.
 *
 * @param addr Aktuelle PC-Adresse (HALT-Adresse + 1)
 * @return true wenn Trap behandelt, false wenn Adresse nicht erkannt
 */
bool CpaBios::handleBiosTrap(uint16_t addr) {
    // addr is cpu_.PC which is HALT_address + 1 (PC advanced past HALT)
    uint16_t haltAddr = addr - 1;

    // Check for cpmx21 (diskio extension) trap
    if (cpmx21Addr_ != 0 && haltAddr == cpmx21Addr_) {
        cpu_.halted = false;
        diagf("DISKIO: HL=%04X flags=%02X dev=%d trk=%d side=%d sec=%d cnt=%d dma=%04X\n",
              cpu_.HL, mem_.read(cpu_.HL), mem_.read(cpu_.HL+1), mem_.read(cpu_.HL+2),
              mem_.read(cpu_.HL+3), mem_.read(cpu_.HL+4), mem_.read(cpu_.HL+6),
              mem_.read(cpu_.HL+7) | (mem_.read(cpu_.HL+8) << 8));
        biosDiskio();
        cpu_.PC = cpu_.pop();
        return true;
    }

    if (haltAddr < biosBase || haltAddr >= biosBase + BIOS_COUNT * 3) return false;
    if ((haltAddr - biosBase) % 3 != 0) return false;

    int func = (haltAddr - biosBase) / 3;
    cpu_.halted = false; // Resume from HALT

    switch (func) {
        case BIOS_BOOT:
            biosBoot();
            return true; // Boot sets PC directly, don't pop return address
        case BIOS_WBOOT:
            diagf("BIOS WBOOT trap at %04X, C=%02X\n", haltAddr, cpu_.C);
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
            diagf("BIOS HOME\n");
            biosHome();
            break;
        case BIOS_SELDSK: {
            uint16_t dph = biosSeldsk(cpu_.C, cpu_.E);
            cpu_.HL = dph;
            break;
        }
        case BIOS_SETTRK:
            diagf("BIOS SETTRK BC=%04X\n", cpu_.BC);
            biosSettrk(cpu_.BC);
            break;
        case BIOS_SETSEC:
            diagf("BIOS SETSEC C=%02X\n", cpu_.C);
            biosSetsec(cpu_.C);
            break;
        case BIOS_SETDMA:
            diagf("BIOS SETDMA BC=%04X\n", cpu_.BC);
            biosSetdma(cpu_.BC);
            break;
        case BIOS_READ:
            cpu_.A = biosRead();
            diagf("BIOS READ -> %02X (disk=%d trk=%d sec=%d dma=%04X)\n",
                   cpu_.A, currentDisk_, currentTrack_, currentSector_, dmaAddr_);
            break;
        case BIOS_WRITE:
            diagf("BIOS WRITE C=%02X (disk=%d trk=%d sec=%d dma=%04X)\n",
                   cpu_.C, currentDisk_, currentTrack_, currentSector_, dmaAddr_);
            cpu_.A = biosWrite(cpu_.C);
            break;
        case BIOS_LISTST:
            cpu_.A = biosListst();
            break;
        case BIOS_SECTRAN: {
            uint16_t result = biosSectran(cpu_.BC, cpu_.DE);
            cpu_.HL = result;
            diagf("BIOS SECTRAN BC=%04X DE=%04X -> %04X\n", cpu_.BC, cpu_.DE, result);
            break;
        }
        default:
            return false;
    }

    // Return from BIOS call (RET) - pop return address pushed by caller's CALL
    cpu_.PC = cpu_.pop();
    return true;
}

/**
 * @brief BDOS-Trap-Handler (derzeit nicht verwendet).
 *
 * Das originale BDOS laeuft nativ im Z80-Emulator und wird nicht
 * abgefangen. Diese Methode dient als Erweiterungspunkt fuer den
 * Fall, dass einzelne BDOS-Funktionen (z.B. Datei-I/O) kuenftig
 * optimiert oder ueberwacht werden sollen.
 */
bool CpaBios::handleBdosTrap() {
    // Not needed - we let the original BDOS code run natively
    // This is only used if we need to intercept specific BDOS calls
    return false;
}

// ============================================================================
// BIOS-Funktionsimplementierungen
//
// Jede Methode entspricht einer der 17 standardmaessigen CP/M-BIOS-Funktionen.
// Die Funktionen werden aufgerufen, wenn der Z80 auf den HALT-Trap an der
// jeweiligen Sprungtabellen-Adresse trifft.
// ============================================================================

/**
 * @brief BIOS 0: Kaltstart.
 *
 * Initialisiert das IOBYTE (0003h = 00h), setzt das aktuelle Laufwerk
 * auf A: und die DMA-Adresse auf den Standard (0080h). Loescht den
 * Bildschirm der K7027-Karte und fuehrt dann einen Warmstart aus.
 */
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

/**
 * @brief BIOS 1: Warmstart (Zurueck zum CCP-Kommandoprompt).
 *
 * Stellt die Page-Zero-Eintraege wieder her (Warmstart-JP bei 0000h,
 * BDOS-Einsprung bei 0005h, aktuelles Laufwerk bei 0004h).
 *
 * Setzt das Interruptsystem zurueck (I=F7h, IM=2, EI), wie es
 * auch das originale BIOS-Makro "warmin" tut. Register C wird
 * auf das aktuelle Laufwerk gesetzt, da der CPA-CCP beim Einstieg
 * C = (User << 4) | Laufwerk erwartet.
 *
 * Springt direkt zum CCP (PC = ccpBase) ohne Ruecksprungadresse
 * auf dem Stack.
 */
void CpaBios::biosWboot() {
    // Warm boot - reload CCP and re-enter command loop
    // Transient programs (format.com etc.) may have overwritten the CCP area
    // through stack usage, so we must restore it from the saved copy.

    // Restore CCP code from saved image
    if (!ccpImage_.empty()) {
        uint16_t ccpLen = static_cast<uint16_t>(ccpImage_.size());
        mem_.load(ccpBase, ccpImage_.data(), ccpLen);
    }

    // Restore page zero
    mem_.write(0x0000, 0xC3);
    mem_.write(0x0001, (biosBase + 3) & 0xFF);
    mem_.write(0x0002, ((biosBase + 3) >> 8) & 0xFF);
    mem_.write(BDOS_ENTRY, 0xC3);
    mem_.write(0x0006, (bdosBase + 6) & 0xFF);
    mem_.write(0x0007, ((bdosBase + 6) >> 8) & 0xFF);
    mem_.write(CDISK_ADDR, currentDisk_);

    // Restore IM2 and I register (matching BIOS warmin macro)
    cpu_.I = 0xF7;
    cpu_.IM = 2;
    cpu_.IFF1 = true;
    cpu_.IFF2 = true;

    // Set C = current disk (CPA CCP expects C = user<<4 | drive on entry)
    // The original BIOS warmin macro loads this from 'defaul'
    cpu_.C = currentDisk_;

    // Jump directly to CCP (no return address on stack)
    cpu_.SP = ccpBase;
    cpu_.PC = ccpBase;
}

/**
 * @brief BIOS 2: Konsolenstatus abfragen.
 *
 * Fragt das SDL-Terminal, ob eine Taste im Eingabepuffer bereitsteht.
 * @return 0xFF wenn Taste bereit, 0x00 wenn keine Taste
 */
uint8_t CpaBios::biosConst() {
    term_.pollEvents();
    return term_.keyAvailable() ? 0xFF : 0x00;
}

/**
 * @brief BIOS 3: Zeichen von der Tastatur lesen (blockierend).
 *
 * Wartet in einer Schleife auf einen Tastendruck. Waehrend des Wartens
 * wird der Bildschirm mit ~50 Hz aktualisiert und SDL-Events abgefragt.
 *
 * Im Auto-Exit-Modus (-exec Kommandozeilenoption) werden zunaechst
 * die injizierten Tasten zurueckgegeben. Wenn alle Tasten verbraucht
 * sind (execDone_ == true), beendet die Methode die Emulation und
 * gibt 1Ah (EOF) zurueck.
 *
 * @return ASCII-Code des gedrueckten Zeichens, oder 1Ah bei Abbruch
 */
uint8_t CpaBios::biosConin() {
    // In auto-exit mode, stop when all injected keys are consumed
    if (autoExit_ && execDone_ && !term_.keyAvailable()) {
        diagf("Auto-exit: command completed, no more keys\n");
        running_ = false;
        return 0x1A; // EOF
    }

    // Wait for key press
    auto lastDisplay = std::chrono::steady_clock::now();
    while (!term_.shouldQuit()) {
        term_.pollEvents();
        if (term_.keyAvailable()) {
            uint8_t key = term_.getKey();
            // Track when all injected keys are consumed
            if (autoExit_ && !execDone_ && !term_.keyAvailable()) {
                execDone_ = true;
            }
            return key;
        }
        // In auto-exit mode, don't wait for keyboard if no keys available
        if (autoExit_ && execDone_) {
            diagf("Auto-exit: waiting for input, no more keys\n");
            running_ = false;
            return 0x1A;
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

/**
 * @brief BIOS 4: Zeichen auf den Bildschirm (K7027) ausgeben.
 *
 * Leitet das Zeichen an die CRT-Emulation (crtPutChar()) weiter und
 * schreibt es optional in die Konsolen-Logdatei.
 *
 * @param ch Das auszugebende Zeichen (einschliesslich Steuerzeichen)
 */
void CpaBios::biosConout(uint8_t ch) {
    crtPutChar(ch);
    if (consoleLog_) {
        fputc(ch, consoleLog_);
        if (ch == '\n' || ch == '\r') fflush(consoleLog_);
    }
}

/** @brief BIOS 5: Zeichen an den Drucker ausgeben (nicht implementiert). */
void CpaBios::biosList(uint8_t ch) {
    // List device - just echo to stdout for now
    if (ch >= 0x20 && ch < 0x7F) {
        // Could log to a file
    }
}

/** @brief BIOS 6: Lochstreifenstanzer (nicht implementiert, gibt 0 zurueck). */
uint8_t CpaBios::biosPunch(uint8_t ch) {
    return 0;
}

/** @brief BIOS 7: Lochstreifenleser (gibt immer 1Ah/EOF zurueck). */
uint8_t CpaBios::biosReader() {
    return 0x1A; // EOF
}

/** @brief BIOS 8: Diskettenkopf auf Spur 0 positionieren. */
void CpaBios::biosHome() {
    currentTrack_ = 0;
}

/**
 * @brief BIOS 9: Laufwerk auswaehlen.
 *
 * Prueft ob das Laufwerk gueltig (0-3) und ein Disketten-Image geladen ist.
 * Bei Erfolg wird currentDisk_ und fdcDriveSelect_ aktualisiert und die
 * DPH-Adresse zurueckgegeben. Bei Fehler gibt die Funktion 0 zurueck,
 * was dem BDOS einen "Select Error" signalisiert.
 *
 * @param disk   Laufwerksnummer (0=A, 1=B, 2=C, 3=D)
 * @param logged 0=erstmaliger Zugriff (Disk muss geladen sein),
 *               !=0=bereits angemeldetes Laufwerk
 * @return DPH-Adresse im Z80-Speicher, oder 0 bei Fehler
 */
uint16_t CpaBios::biosSeldsk(uint8_t disk, uint8_t logged) {
    if (disk >= 4) {
        diagf("SELDSK(%d, %d) -> 0 (invalid drive)\n", disk, logged);
        return 0; // Error: invalid drive
    }

    // Check if drive has a mounted disk
    if (!disks_[disk].isLoaded() && logged == 0) {
        diagf("SELDSK(%d, %d) -> 0 (not loaded)\n", disk, logged);
        return 0; // No disk
    }

    currentDisk_ = disk;
    fdcDriveSelect_ = disk; // Also update FDC drive select for CPU2 format
    diagf("SELDSK(%d, %d) -> %04X\n", disk, logged, dphAddr_[disk]);
    return dphAddr_[disk];
}

/** @brief BIOS 10: Spurnummer setzen (aus BC-Register). */
void CpaBios::biosSettrk(uint16_t track) {
    currentTrack_ = track;
}

/** @brief BIOS 11: Sektornummer setzen (aus C-Register). */
void CpaBios::biosSetsec(uint8_t sector) {
    currentSector_ = sector;
}

/** @brief BIOS 12: DMA-Adresse fuer den naechsten Sektor-Transfer setzen. */
void CpaBios::biosSetdma(uint16_t addr) {
    dmaAddr_ = addr;
}

/**
 * @brief BIOS 13: Einen 128-Byte-Sektor von der Diskette lesen.
 *
 * Liest den zuvor mit SETTRK/SETSEC/SELDSK eingestellten Sektor vom
 * Disketten-Image und kopiert die 128 Bytes an die gesetzte DMA-Adresse.
 *
 * @return 0 bei Erfolg, 1 bei Fehler (ungueltig/nicht geladen/Lesefehler)
 */
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

/**
 * @brief BIOS 14: Einen 128-Byte-Sektor auf die Diskette schreiben.
 *
 * Liest 128 Bytes von der DMA-Adresse und schreibt sie in den zuvor
 * mit SETTRK/SETSEC/SELDSK eingestellten Sektor des Disketten-Images.
 *
 * @param type Schreibtyp (0=normal, 1=Verzeichnis, 2=erster Sektor eines
 *             neuen Blocks - wird in der Emulation nicht unterschieden)
 * @return 0 bei Erfolg, 1 bei Fehler
 */
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

/** @brief BIOS 15: Druckerstatus (gibt immer 0xFF = bereit zurueck). */
uint8_t CpaBios::biosListst() {
    return 0xFF; // Always ready
}

/**
 * @brief BIOS 16: Sektornummer mittels Interleave-Tabelle uebersetzen.
 *
 * Wenn eine Uebersetzungstabelle angegeben ist (table != 0), wird die
 * logische Sektornummer ueber die Tabelle in eine physische Sektornummer
 * uebersetzt. Das BDOS ruft diese Funktion bei jedem Sektorzugriff auf.
 *
 * @param sector Logische Sektornummer (0-basiert)
 * @param table  Adresse der Uebersetzungstabelle im Z80-Speicher (0 = keine)
 * @return Physische Sektornummer
 */
uint16_t CpaBios::biosSectran(uint16_t sector, uint16_t table) {
    if (table == 0) {
        // No translation
        return sector;
    }

    // Read translated sector from table
    return mem_.read(table + sector);
}

// ============================================================================
// CRT-Emulation (Bildschirmkarte K7027)
//
// Die K7027 stellt einen 80x24-Zeichen-Textbildschirm bereit. Der
// Bildschirmspeicher wird im Z80-Adressraum ab F800h emuliert (1920 Bytes).
// Die Cursorposition wird als absolute Adresse innerhalb dieses Puffers
// verwaltet. Steuerzeichen und ESC-Sequenzen werden entsprechend dem
// Original-BIOS-Code (bioscrt.mac) verarbeitet.
// ============================================================================

/**
 * @brief Verarbeitet ein Zeichen fuer die K7027-Bildschirmausgabe.
 *
 * Implementiert eine vollstaendige Zustandsmaschine fuer die
 * Zeichenausgabe auf dem A5120-Bildschirm:
 *
 * **ESC-Sequenz-Verarbeitung** (Zustandsmaschine escapeState_):
 * - Zustand 0: Normaler Modus
 * - Zustand 1: ESC empfangen, warte auf Kommando ('=' oder 'Y')
 * - Zustand 2: Cursorpositionierung, warte auf Zeilenangabe (Wert + 20h)
 * - Zustand 3: Cursorpositionierung, warte auf Spaltenangabe (Wert + 20h)
 *
 * **Steuerzeichen** (00h-1Fh):
 * Siehe crtPutChar()-Doku im Header fuer die vollstaendige Tabelle.
 *
 * **Druckbare Zeichen** (20h-7Eh):
 * Werden an der aktuellen Cursorposition in den Bildschirmpuffer
 * geschrieben. Der Cursor rueckt um eine Position weiter. Am
 * Bildschirmende wird gescrollt.
 *
 * **Videoattribute** (84h-86h):
 * K7027-spezifische Attribute werden als Zeichen mit geloeschtem
 * Bit 7 geschrieben.
 */

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
            // Characters 0x84-0x87 are video attributes on K7027
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

/**
 * @brief Scrollt den Bildschirm um eine Zeile nach oben.
 *
 * Kopiert jede Zeile (80 Zeichen) um eine Position nach oben.
 * Die unterste Zeile (Zeile 23) wird mit Leerzeichen gefuellt.
 * Diese Funktion wird aufgerufen, wenn der Cursor das untere
 * Bildschirmende ueberschreitet (Line Feed oder Zeichenausgabe
 * in der letzten Zeile/Spalte).
 */
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

/** @brief Loescht den gesamten Bildschirm (alle 1920 Bytes auf Leerzeichen) und setzt Cursor auf HOME. */
void CpaBios::crtClearScreen() {
    mem_.fill(Memory::SCREEN_START, Memory::SCREEN_START + Memory::SCREEN_CHARS - 1, ' ');
    cursorPos_ = Memory::SCREEN_START;
}

/** @brief Loescht alle Zeichen von der Cursorposition bis zum Ende der aktuellen Zeile. */
void CpaBios::crtClearToEol() {
    int col = (cursorPos_ - Memory::SCREEN_START) % Memory::SCREEN_COLS;
    int row = (cursorPos_ - Memory::SCREEN_START) / Memory::SCREEN_COLS;
    for (int c = col; c < Memory::SCREEN_COLS; c++) {
        mem_.write(Memory::SCREEN_START + row * Memory::SCREEN_COLS + c, ' ');
    }
}

/** @brief Setzt den Cursor auf die HOME-Position (F800h = Zeile 0, Spalte 0). */
void CpaBios::crtCursorHome() {
    cursorPos_ = Memory::SCREEN_START;
}

/**
 * @brief Setzt den Cursor auf eine bestimmte Zeile und Spalte.
 *
 * Die Koordinaten werden auf den gueltigen Bereich begrenzt
 * (0-23 fuer Zeilen, 0-79 fuer Spalten) und in eine absolute
 * Speicheradresse umgerechnet: addr = F800h + row * 80 + col.
 */
void CpaBios::crtSetCursor(int row, int col) {
    if (row < 0) row = 0;
    if (row >= Memory::SCREEN_ROWS) row = Memory::SCREEN_ROWS - 1;
    if (col < 0) col = 0;
    if (col >= Memory::SCREEN_COLS) col = Memory::SCREEN_COLS - 1;
    cursorPos_ = Memory::SCREEN_START + row * Memory::SCREEN_COLS + col;
}

/**
 * @brief Aktualisiert die SDL-Bildschirmanzeige.
 *
 * Setzt temporaer Bit 7 des Zeichens an der Cursorposition,
 * um den Cursor sichtbar zu machen (invertierte Darstellung
 * im SDL-Renderer). Nach dem Bildschirmupdate wird das Bit
 * wieder zurueckgesetzt, damit der Bildschirmpuffer sauber bleibt.
 */
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

// ============================================================================
// FDC-PIO-Port-Emulation (Baugruppe K5122, Ports 10h-18h)
//
// Die Floppydisk-Controllerkarte K5122 des A5120 wird ueber zwei Z80-PIOs
// angesteuert (insgesamt 9 I/O-Ports). Die Steuer-PIO (10h-13h) kontrolliert
// den Schreib-/Lesekopf und die Laufwerksmechanik, die Daten-PIO (14h-17h)
// uebertraegt die eigentlichen Lese-/Schreibdaten. Der Selektions-Latch
// (Port 18h) waehlt das aktive Laufwerk aus.
//
// Die PIO erzeugt Interrupts im Z80-IM2-Modus fuer:
// - Indexpuls (eine Umdrehung der Diskette, ~200ms bei 300 RPM)
// - Adressmarken-Erkennung (MFM-Sync-Muster)
//
// Der Interruptvektor wird ueber Port 11h programmiert. Im IM2-Modus
// berechnet die Z80-CPU die ISR-Adresse als:
//   vectorAddr = (I << 8) | (vector & 0xFE)
//   isrAddr = mem[vectorAddr] | (mem[vectorAddr+1] << 8)
// ============================================================================

/**
 * @brief Liest einen FDC-PIO-Port.
 *
 * Emulierte Ports:
 * - **Port 10h** (Steuer-PIO A Daten): Gibt den zuletzt geschriebenen Wert
 *   zurueck (Readback des Ausgangsregisters).
 * - **Port 12h** (Steuer-PIO B Status): Gibt den Laufwerksstatus zurueck.
 *   Alle Signale sind aktiv-low (0 = aktiv, 1 = inaktiv):
 *   - Bit 0: /RDY = 0 (Laufwerk immer bereit)
 *   - Bit 1: /MKE = 1 (keine Marke gefunden)
 *   - Bit 4: /FA  = 1 (kein AMF-Fehler)
 *   - Bit 5: /WP  = 1 (nicht schreibgeschuetzt)
 *   - Bit 6: /FW  = 1 (kein Schreibfehler)
 *   - Bit 7: /T0  = 0 wenn Kopf auf Spur 0 (fdcAtTrack0_)
 * - **Port 16h** (Daten-PIO B Lesedaten): Gibt E5h zurueck (Fuellbyte).
 *
 * Alle anderen Ports geben FFh zurueck.
 */

uint8_t CpaBios::fdcReadPort(uint8_t port) {
    switch (port) {
        case 0x10:
            // Control PIO A data - return last written value
            return fdcControlA_;
        case 0x12: {
            // Control PIO B - status (active-low signals)
            //   Bit 0: /RDY  0=ready, 1=not ready
            //   Bit 1: /MKE  1=no mark found
            //   Bit 4: /FA   1=no AMF error
            //   Bit 5: /WP   1=not write protected
            //   Bit 6: /FW   1=no write error
            //   Bit 7: /T0   0=at track 0, 1=not at track 0
            uint8_t status = 0x00;  // Bit 0=0: drive ready
            status |= 0x02;        // Bit 1=1: no mark found
            status |= 0x10;        // Bit 4=1: no AMF error
            status |= 0x20;        // Bit 5=1: not write protected
            status |= 0x40;        // Bit 6=1: no write error
            if (!fdcAtTrack0_) status |= 0x80; // Bit 7: 0=track0, 1=not track0
            return status;
        }
        case 0x16:
            // Data PIO B - read data (return 0xE5 for empty/formatted sectors)
            return 0xE5;
        default:
            return 0xFF;
    }
}

/**
 * @brief Schreibt auf einen FDC-PIO-Port.
 *
 * **Port 10h - Steuer-PIO A Daten (Kopfsteuerung):**
 *
 * Emuliert die mechanischen Funktionen des Floppylaufwerks:
 * - **Schrittmotor**: Erkennt die steigende Flanke von /ST (Bit 7: 0→1).
 *   Bei jeder Flanke wird je nach SD-Bit (Bit 5) ein Schritt in Richtung
 *   hoehere Spurnummern (Step-In, SD=1) oder Spur 0 (Step-Out, SD=0)
 *   ausgefuehrt. Die Spurposition wird auf den Bereich 0-79 begrenzt.
 * - **Seitenauswahl**: /SIDE (Bit 2, aktiv-low): 0 = Seite 1, 1 = Seite 0.
 * - **Schreibfreigabe**: /WE (Bit 0, aktiv-low): Steigende Flanke
 *   (0→1) startet den Schreibmodus und initialisiert den Track-Puffer.
 *   Fallende Flanke (1→0) beendet den Schreibvorgang und loest
 *   fdcParseTrackData() aus.
 *
 * **Port 11h - Steuer-PIO A Steuerregister:**
 *
 * Verarbeitet Z80-PIO-Steuerbefehle:
 * - Bit 0 = 0: Setzt den Interruptvektor (val wird als Low-Byte gespeichert)
 * - Low-Nibble = 03h: Interrupt ein/aus schalten (Bit 7: 1=einschalten)
 * - Andere Steuerbefehle (Mode Set, Richtung): Werden ignoriert
 *
 * **Port 14h - Daten-PIO A Schreibdaten:**
 *
 * Im Schreibmodus wird jedes geschriebene Byte an den Track-Puffer
 * (fdcTrackBuf_) angehaengt. Nach Beendigung des Schreibvorgangs
 * werden die gepufferten Daten von fdcParseTrackData() ausgewertet.
 *
 * **Port 18h - Selektions-Latch:**
 *
 * Waehlt das aktive Laufwerk aus. Die Bits 7..4 entsprechen den
 * Laufwerken 3..0 (aktiv-low). Jeweils nur ein Bit darf 0 sein.
 */
void CpaBios::fdcWritePort(uint8_t port, uint8_t val) {
    switch (port) {
        case 0x10: {
            // Control PIO A data - drive control output
            // Bit 0: /WE   0=write enable, 1=write disable
            // Bit 2: /SIDE 0=side 1, 1=side 0
            // Bit 5: SD    0=step out (towards track 0), 1=step in
            // Bit 7: /ST   0=step pulse active, 1=inactive
            uint8_t oldCtl = fdcControlA_;
            fdcControlA_ = val;

            // Detect step pulse: rising edge of /ST (bit 7: 0→1)
            if ((val & 0x80) && !(oldCtl & 0x80)) {
                if (val & 0x20) {
                    // SD=1: Step in (towards higher tracks)
                    if (fdcTrack_ < 79) fdcTrack_++;
                } else {
                    // SD=0: Step out (towards track 0)
                    if (fdcTrack_ > 0) fdcTrack_--;
                }
                fdcAtTrack0_ = (fdcTrack_ == 0);
            }

            // Side select: /SIDE bit 2 (active low: 0=side 1, 1=side 0)
            fdcSide_ = (val & 0x04) ? 0 : 1;

            // Write enable: /WE bit 0 (active low: 0=write enabled)
            bool writeEnable = !(val & 0x01);
            if (writeEnable && !fdcWriteMode_) {
                // Start of write/format operation
                fdcWriteMode_ = true;
                fdcTrackBuf_.clear();
                fdcWriteCount_ = 0;
                fdcFormatDone_ = false;
            } else if (!writeEnable && fdcWriteMode_) {
                // End of write/format operation - parse and commit track data
                fdcWriteMode_ = false;
                if (!fdcTrackBuf_.empty()) {
                    fdcParseTrackData();
                }
            }
            break;
        }
        case 0x11: {
            // Control PIO A control register
            // Z80 PIO control word programming:
            //   Bit 0 = 0: set interrupt vector (val is vector low byte)
            //   Low nibble = 0x03: interrupt enable/disable (bit 7: 1=enable)
            //   Low nibble = 0x0F: set I/O direction
            //   Low nibble = 0x07: interrupt control word
            if ((val & 0x01) == 0) {
                // Set interrupt vector (low byte)
                fdcPioAVector_ = val;
            } else if ((val & 0x0F) == 0x03) {
                // Interrupt enable/disable
                fdcPioAIntEnabled_ = (val & 0x80) != 0;
            }
            // Other control words (mode set, direction) - ignore for emulation
            break;
        }
        case 0x14:
            // Data PIO A - write data byte
            if (fdcWriteMode_) {
                fdcTrackBuf_.push_back(val);
                fdcWriteCount_++;
            }
            break;
        case 0x15:
            // Data PIO A control - ignore
            break;
        case 0x13:
            // Control PIO B control - ignore
            break;
        case 0x17:
            // Data PIO B control - ignore
            break;
        case 0x18:
            // Select latch - bits 7..4 select drives 3..0 (active low)
            fdcDriveSelect_ = 0;
            if (!(val & 0x10)) fdcDriveSelect_ = 0;
            else if (!(val & 0x20)) fdcDriveSelect_ = 1;
            else if (!(val & 0x40)) fdcDriveSelect_ = 2;
            else if (!(val & 0x80)) fdcDriveSelect_ = 3;
            break;
        default:
            break;
    }
}

/**
 * @brief Parst MFM-kodierte Spurdaten und extrahiert formatierte Sektoren.
 *
 * Wird aufgerufen, wenn der Schreibmodus beendet wird (/WE: 0→1).
 * Die im fdcTrackBuf_ gesammelten Rohdaten werden nach dem
 * MFM-Adressmarken-Schema durchsucht:
 *
 * **ID-Feld** (Sektoridentifikation):
 * @code
 *   A1 A1 A1 FE   ; Sync + ID-Adressmarke
 *   TT             ; Spurnummer
 *   SS             ; Seitennummer
 *   NN             ; Sektornummer (1-basiert)
 *   SC             ; Sektorlaengen-Code (0=128, 1=256, 2=512, 3=1024)
 *   CC CC          ; CRC
 * @endcode
 *
 * **Datenfeld** (Sektorinhalt):
 * @code
 *   A1 A1 A1 FB   ; Sync + Daten-Adressmarke
 *   DD ... DD      ; Sektordaten (128/256/512/1024 Bytes je nach SC)
 *   CC CC          ; CRC
 * @endcode
 *
 * Fuer das A5120-Format mit 1024-Byte-Sektoren (SC=3) werden die
 * physischen Sektoren in je 8 logische 128-Byte-CP/M-Sektoren
 * aufgeteilt und an die korrekte Position im Disketten-Image
 * geschrieben.
 *
 * Die logische Spurnummer errechnet sich aus:
 * logTrack = physTrack * 2 + seite
 */
void CpaBios::fdcParseTrackData() {
    // Parse the written track data to extract formatted sectors
    // The track data contains: gap bytes, sync, address marks, sector ID, data
    // We look for address mark 0xFE (ID field) followed by sector ID,
    // then data mark 0xFB followed by sector data

    int drive = fdcDriveSelect_;
    if (drive < 0 || drive >= 4 || !disks_[drive].isLoaded()) return;

    int logTrack = fdcTrack_ * 2 + fdcSide_; // Convert to logical track number
    const auto& buf = fdcTrackBuf_;
    size_t len = buf.size();

    for (size_t i = 0; i + 4 < len; i++) {
        // Look for ID address mark pattern: 0xA1 0xA1 0xA1 0xFE
        if (buf[i] == 0xA1 && i + 1 < len && buf[i+1] == 0xA1 &&
            i + 2 < len && buf[i+2] == 0xA1 && i + 3 < len && buf[i+3] == 0xFE) {
            // Read sector ID: track, side, sector, size code
            if (i + 7 >= len) break;
            uint8_t secNum = buf[i + 6]; // Sector number
            uint8_t sizeCode = buf[i + 7]; // 0=128, 1=256, 2=512, 3=1024
            int secSize = 128 << sizeCode;

            // Now look for data mark: 0xA1 0xA1 0xA1 0xFB
            for (size_t j = i + 10; j + 3 + secSize < len; j++) {
                if (buf[j] == 0xA1 && buf[j+1] == 0xA1 &&
                    buf[j+2] == 0xA1 && buf[j+3] == 0xFB) {
                    // Found data field - write sector data to disk image
                    size_t dataStart = j + 4;
                    if (dataStart + secSize > len) break;

                    // For 1024-byte physical sectors, write 8 logical 128-byte sectors
                    int logSectors = secSize / 128;
                    int baseSector = (secNum - 1) * logSectors; // Convert to 0-based logical sector

                    for (int ls = 0; ls < logSectors && baseSector + ls < FloppyDisk::CPM_SECTORS_PER_TRACK; ls++) {
                        disks_[drive].writeSector(logTrack, baseSector + ls,
                            &buf[dataStart + ls * 128]);
                    }

                    i = j + 4 + secSize; // Skip past this sector's data
                    break;
                }
            }
        }
    }
}

/**
 * @brief Emuliert CPU2 der K2526-Karte: Formatiert eine komplette Spur.
 *
 * Auf der realen Hardware uebernimmt CPU2 (der zweite Z80/U880 auf der
 * CPU-Karte K2526) das byteweise Schreiben des MFM-Spurformats. CPU2
 * hat ein eigenes EPROM mit der Firmware fuer das exakte 32us-Timing
 * pro Byte (250 kbit/s Double Density). Waehrend CPU2 arbeitet, wartet
 * CPU1 in einer JR $-Schleife.
 *
 * Der Emulator kann CPU2 nicht im Detail emulieren, da das EPROM
 * nicht vorliegt. Stattdessen wird die Formatierung auf hoeherer
 * Ebene durchgefuehrt:
 *
 * A5120-Diskettenformat:
 * - 80 Spuren (physisch, 0-79)
 * - 2 Seiten (0 und 1)
 * - 5 physische Sektoren pro Spur (1024 Bytes)
 * - = 26 logische Sektoren pro Spur (128 Bytes)
 * - Gesamtkapazitaet: 80 * 2 * 26 * 128 = 532480 Bytes Nutzkapazitaet
 *   (800 KB Rohkapazitaet mit System-Spuren)
 *
 * Alle logischen Sektoren werden mit E5h gefuellt (Standard-Fuellbyte
 * fuer formatierte, leere Disketten unter CP/M).
 *
 * @param drive Laufwerksnummer (0-3)
 * @param track Physische Spurnummer (0-79)
 * @param side  Diskettenseite (0 oder 1)
 */
void CpaBios::fdcFormatTrack(int drive, int track, int side) {
    // Emulate what CPU2 on the K2526 CPU card does during format:
    // A5120 format: 5 physical sectors per track, 1024 bytes each
    // = 40 logical sectors per track, 128 bytes each
    // Fill all sectors with 0xE5 (standard format fill byte)

    if (drive < 0 || drive >= 4 || !disks_[drive].isLoaded()) return;

    int logTrack = track * 2 + side;
    uint8_t sectorBuf[128];
    memset(sectorBuf, 0xE5, 128);

    for (int s = 0; s < FloppyDisk::CPM_SECTORS_PER_TRACK; s++) {
        disks_[drive].writeSector(logTrack, s, sectorBuf);
    }

    diagf("FDC: Formatted drive %c: track %d side %d\n",
           'A' + drive, track, side);
}

// ============================================================================
// CPA-Erweiterung: diskio (cpmx21)
// ============================================================================

/**
 * @brief Behandelt den CPA-Erweiterungsaufruf diskio (cpmx21).
 *
 * diskio ist die CPA-spezifische Erweiterung fuer physische
 * Disk-Transfers, die ueber die Erweiterungstabelle (cpmx00+21h)
 * aufgerufen wird. Das Standard-BIOS verwendet blockweise
 * 128-Byte-Transfers ueber READ/WRITE, waehrend diskio direkte
 * physische Operationen erlaubt (Seek, Mehrsektortransfer).
 *
 * Wird hauptsaechlich von format.com verwendet fuer:
 * - Kopfpositionierung (Seek) auf bestimmte Spur/Seite
 * - Mehrsektortransfers (mehrere 128-Byte-Sektoren in einem Aufruf)
 * - Kopf-Abheben (diofhd-Flag) nach Operationsende
 *
 * **CDB-Struktur** (Command Descriptor Block, HL zeigt darauf):
 * @code
 *   Offset  Feld    Beschreibung
 *   +0      cdbfl   Flags: Bit 2=Schreiben, Bit 5=Kopf abheben
 *   +1      cdbdev  Logische Geraetenummer (0..3)
 *   +2      cdbtrk  Spurnummer (0..79)
 *   +3      cdbsid  Seitennummer (0 oder 1)
 *   +4      cdbsec  Sektornummer (0-basiert)
 *   +5      cdbslc  Sektorlaengen-Code (0=128, 1=256, 2=512, 3=1024)
 *   +6      cdbsnb  Anzahl Sektoren (0 = nur positionieren)
 *   +7/+8   cdbdma  Transferadresse (Little-Endian)
 * @endcode
 *
 * Bei cdbsnb=0 wird nur ein Seek ausgefuehrt (Kopf positionieren,
 * FDC-Zustand aktualisieren). Bei cdbsnb>0 werden die angegebenen
 * Sektoren gelesen oder geschrieben.
 *
 * **Rueckgabe** (in Z80-Registern):
 * - A=0 + Z-Flag: Erfolg
 * - A='D' + NZ: Geraet nicht gefunden
 */
void CpaBios::biosDiskio() {
    // diskio is the CPA physical disk transfer extension (cpmx21)
    // Replicates the BIOS diskio handler in biosdsk.mac:
    //   - Reads the CDB (Command Descriptor Block) from HL
    //   - Looks up the DPB for the logical device
    //   - Converts logical track/sector to physical track/side/sector
    //   - Performs the sector transfer
    //
    // CDB layout (from biosdsk.mac):
    //   +0: cdbfl   - flags (bit definitions: diofps=6, diofhd=5,
    //                 dioftr=4, dioffm=3, diofwr=2, diofid=1)
    //   +1: cdbdev  - logical device number (0..3)
    //   +2: cdbtrk  - logical track number
    //   +3: cdbsid  - side (only used when diofps=1)
    //   +4: cdbsec  - sector number (1-based)
    //   +5: cdbslc  - sector length code (0=128, 1=256, 2=512, 3=1024)
    //   +6: cdbsnb  - sectors to transfer (0 = just seek/position)
    //   +7,+8: cdbdma - transfer address (little-endian)

    uint16_t hl = cpu_.HL;
    uint8_t flags = mem_.read(hl + 0);   // cdbfl

    // diofhd (bit 5): head up/disengage
    if (flags & 0x20) {
        cpu_.A = 0;
        cpu_.F |= 0x40;
        return;
    }

    uint8_t device = mem_.read(hl + 1);  // cdbdev
    uint8_t cdbTrack = mem_.read(hl + 2); // cdbtrk (logical)
    uint8_t cdbSide  = mem_.read(hl + 3); // cdbsid
    uint8_t cdbSec   = mem_.read(hl + 4); // cdbsec (1-based)
    uint8_t slc      = mem_.read(hl + 5); // cdbslc
    uint8_t count    = mem_.read(hl + 6); // cdbsnb
    uint16_t addr = mem_.read(hl + 7) | (mem_.read(hl + 8) << 8);

    uint8_t drive = device;

    if (drive < 4) {
        fdcDriveSelect_ = drive;
    }

    if (count == 0) {
        // Seek only - compute physical track/side from logical track
        // just like the BIOS does (needed for CPU2 format which reads fdcTrack_/fdcSide_)
        int seekTrack = cdbTrack;
        int seekSide = 0;
        bool diofps_seek = (flags & 0x40) != 0;
        if (!diofps_seek && drive < 4 && dphAddr_[drive] != 0) {
            uint16_t dph = dphAddr_[drive];
            uint16_t dpbAddr = mem_.read(dph + 10) | (mem_.read(dph + 11) << 8);
            if (dpbAddr != 0) {
                uint8_t dpbflg = mem_.read(dpbAddr + 15);
                bool fds  = (dpbflg >> 5) & 1;
                bool f86  = (dpbflg >> 4) & 1;
                if (f86) {
                    uint8_t dpbtrk = mem_.read(dpbAddr + 20);
                    int halfTracks = dpbtrk / 2;
                    int a = cdbTrack - halfTracks;
                    if (a >= 0) {
                        bool fsv = (dpbflg >> 3) & 1;
                        if (!fsv) { a = (~a & 0xFF); a = (a + halfTracks) & 0xFF; }
                        seekTrack = a;
                        seekSide = 1;
                    }
                } else if (fds) {
                    seekSide = cdbTrack & 1;
                    seekTrack = cdbTrack >> 1;
                }
            }
        } else if (diofps_seek) {
            seekSide = cdbSide;
        }
        fdcTrack_ = seekTrack;
        fdcSide_ = seekSide;
        fdcAtTrack0_ = (seekTrack == 0);
        cpu_.A = 0;
        cpu_.F |= 0x40;
        return;
    }

    if (drive >= 4 || !disks_[drive].isLoaded()) {
        cpu_.A = 'D';
        cpu_.F &= ~0x40;
        return;
    }

    // --- Logical to physical conversion (biosdsk.mac diskio handler) ---
    // Look up the DPB through the DPH for this drive
    int physTrack = cdbTrack;
    int physSide = 0;
    int physSec = (cdbSec > 0) ? cdbSec - 1 : 0; // 1-based -> 0-based

    bool diofps = (flags & 0x40) != 0; // bit 6: already physical?

    if (!diofps && drive < 4 && dphAddr_[drive] != 0) {
        uint16_t dph = dphAddr_[drive];
        uint16_t dpbAddr = mem_.read(dph + 10) | (mem_.read(dph + 11) << 8);

        if (dpbAddr != 0) {
            uint8_t dpbflg = mem_.read(dpbAddr + 15);
            uint8_t dpbsn0 = mem_.read(dpbAddr + 19);
            uint8_t dpbtrk = mem_.read(dpbAddr + 20);
            uint8_t dpbofs = mem_.read(dpbAddr + 13); // system track offset
            bool fds  = (dpbflg >> 5) & 1; // dpbfds: double-sided
            bool f86  = (dpbflg >> 4) & 1; // dpbf86: continuation mode

            int d = cdbTrack;  // logical track
            int c = physSec;   // sector (0-based)

            if (f86) {
                // Continuation mode: tracks > half go to back side
                int halfTracks = dpbtrk / 2;
                int a = d - halfTracks;
                if (a >= 0) {
                    // Back side
                    bool fsv = (dpbflg >> 3) & 1; // dpbfsv (shares bit with dpbfsm)
                    if (!fsv) {
                        // innen nach aussen: reverse track numbering
                        a = ~a & 0xFF;           // CPL
                        a = (a + halfTracks) & 0xFF; // ADD A,B
                    }
                    d = a;
                    c = (c + dpbsn0) & 0xFF;
                    physSide = 1;
                }
                // else: front side, d unchanged
            } else if (fds) {
                // Standard DS: odd logical tracks on back side
                physSide = d & 1;
                d = d >> 1; // SRL D
                if (physSide) {
                    c = (c + dpbsn0) & 0xFF;
                }
            }
            // else: single-sided, no conversion

            physTrack = d;

            // Sector translation via dpbstr table (biosdsk.mac "diossp" section)
            // For system tracks, sector numbers are already physical
            uint16_t dpbstrAddr = dpbAddr + 25; // dpbstr offset
            if (cdbTrack >= dpbofs) {
                // Non-system track: look up in sector translate table
                // physSec = dpbstr[c] (the table contains 1-based phys sector numbers)
                // For DD format the table is sequential: 1,2,3,...
                physSec = mem_.read(dpbstrAddr + c);
                // dpbstr values are 1-based physical sector numbers;
                // we need 0-based record indices for our image layout
                if (physSec > 0) physSec--;
            }
            // else: system track — sector number used directly (already 0-based)
        }
    } else if (diofps) {
        // Already physical: use cdbsid as side
        physSide = cdbSide;
        // physSec is already 0-based from cdbSec-1
    }

    // Compute logical track in disk image (interleaved: track*2+side)
    int logTrack = physTrack * 2 + physSide;

    fdcTrack_ = physTrack;
    fdcSide_ = physSide;
    fdcAtTrack0_ = (physTrack == 0);

    bool isWrite = (flags & 0x04) != 0; // diofwr = bit 2

    // Physical sector size from cdbslc
    if (slc > 3) slc = 3;
    int recsPerSector = 1 << slc;       // 128-byte records per physical sector
    int physSectorSize = 128 * recsPerSector;

    for (int i = 0; i < count; i++) {
        int startRecord = (physSec + i) * recsPerSector;
        for (int r = 0; r < recsPerSector; r++) {
            int record = startRecord + r;
            if (record >= FloppyDisk::CPM_SECTORS_PER_TRACK) break;
            int memOffset = i * physSectorSize + r * 128;
            if (isWrite) {
                uint8_t sectorBuf[128];
                for (int b = 0; b < 128; b++)
                    sectorBuf[b] = mem_.read(addr + memOffset + b);
                disks_[drive].writeSector(logTrack, record, sectorBuf);
            } else {
                uint8_t sectorBuf[128];
                disks_[drive].readSector(logTrack, record, sectorBuf);
                mem_.load(addr + memOffset, sectorBuf, 128);
            }
        }
    }

    cpu_.A = 0;
    cpu_.F |= 0x40;
}

/** @brief Periodischer Timer-Tick. Aktualisiert den Bildschirm alle 50 Ticks. */
void CpaBios::tick() {
    tickCount_++;
    if (tickCount_ % 50 == 0) { // ~50Hz display update
        updateDisplay();
    }
}

/**
 * @brief Haupt-Emulationsschleife.
 *
 * Fuehrt Z80-Instruktionen in Frames von ~50000 Zyklen aus (2.5 MHz / 50 Hz).
 * Nach jedem Frame werden SDL-Events abgefragt, der Bildschirm aktualisiert
 * und die Timing-Kontrolle durchgefuehrt (20ms Zielzeit pro Frame = 50 Hz).
 *
 * **Reihenfolge der Pruefungen innerhalb jedes Frames:**
 *
 * 1. **HALT-Traps**: Wenn cpu_.halted == true, wird geprueft ob die
 *    HALT-Adresse zu einem BIOS-Einsprungpunkt gehoert. Falls ja,
 *    wird handleBiosTrap() aufgerufen. Andernfalls wird der HALT als
 *    echter HALT behandelt (z.B. Warten auf Interrupt).
 *
 * 2. **Warmstart-Erkennung**: Wenn PC == 0000h, wird biosWboot() aufgerufen.
 *    Dies entspricht einem JP 0000h im Z80-Programm.
 *
 * 3. **CPU2-Formatierschleife** (K2526-Emulation): Wenn folgende drei
 *    Bedingungen gleichzeitig erfuellt sind:
 *    - fdcWriteMode_ == true (Schreibfreigabe /WE aktiv)
 *    - fdcPioAIntEnabled_ == true (PIO-Interrupts eingeschaltet)
 *    - Aktuelle Instruktion ist JR $ (Opcode 18h FEh)
 *    Dann wird die CPU2-Formatierung emuliert:
 *    a) fdcFormatTrack() fuellt alle Sektoren mit E5h
 *    b) Der JR-Displacement wird von FEh auf 00h gepatcht
 *       (macht aus JR -2 ein JR +0 = Durchfallen)
 *    c) PIO-Interrupts werden deaktiviert
 *    d) fdcFormatDone_ wird auf true gesetzt
 *    CPU1 faellt dann durch zum Cleanup-Code:
 *    @code
 *      LD B, 00h        ; B=0 (Erfolg)
 *      LD HL, 0000h     ; Status
 *      LD SP, (nnnn)    ; SP-Wiederherstellung (selbstmodifiziert)
 *      OUT (11h), 03h   ; PIO-Interrupts deaktivieren
 *    @endcode
 *
 * 4. **Index-Puls-Interrupts**: Wenn PIO-Interrupts aktiviert und die
 *    CPU Interrupts annimmt (IFF1=true), wird alle INDEX_PULSE_CYCLES
 *    Zyklen (~20ms/50000 Zyklen) ein IM2-Interrupt mit dem PIO-A-Vektor
 *    ausgeloest. Dies simuliert die Diskettenumdrehung.
 *
 * 5. **Normale Instruktionsausfuehrung**: cpu_.step() fuehrt eine
 *    Z80-Instruktion aus und gibt die verbrauchten Taktzyklen zurueck.
 */
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
                if ((haltAddr >= biosBase && haltAddr < biosBase + BIOS_COUNT * 3
                    && (haltAddr - biosBase) % 3 == 0)
                    || (cpmx21Addr_ != 0 && haltAddr == cpmx21Addr_)) {
                    if (!handleBiosTrap(cpu_.PC)) {
                        diagf("Unhandled BIOS trap at %04X\n", haltAddr);
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
                diagf("Warm boot triggered (JP 0x0000), C=%02X\n", cpu_.C);
                biosWboot();
                continue;
            }

            // K2526 CPU2 emulation: detect JR $ (self-loop) during FDC format
            // In the original A5120, CPU1 parks in a JR $ self-loop while
            // CPU2 handles byte-level track writing. The format code at 1D21 is:
            //   OUT (10h), B4h  → enable write mode
            //   OUT (11h), 83h  → enable PIO interrupts
            //   JR $            → self-loop at 1D21 (18 FE)
            //   LD B, 00h       → cleanup: B=0 (success)
            //   LD HL, 0000h    → cleanup: HL status
            //   LD SP, saved    → cleanup: restore SP (self-modified by ED 73 29 1D)
            //   OUT (11h), 03h  → cleanup: disable PIO interrupts
            //   ...             → return to caller
            //
            // We emulate CPU2 by formatting the track directly, then break the
            // JR $ loop by patching the displacement from 0xFE to 0x00 so the
            // CPU falls through to the cleanup code. The cleanup code at 1CEE
            // restores 0xFE on each call, so the patch is safe for multiple tracks.
            if (fdcWriteMode_ && fdcPioAIntEnabled_ && !fdcFormatDone_) {
                uint8_t opcode = mem_.read(cpu_.PC);
                uint8_t operand = mem_.read(cpu_.PC + 1);
                if (opcode == 0x18 && operand == 0xFE) {
                    // CPU is in JR $ self-loop with FDC write active + PIO interrupt enabled
                    diagf("CPU2 format: drive=%d track=%d side=%d PC=%04X SP=%04X\n",
                          fdcDriveSelect_, fdcTrack_, fdcSide_, cpu_.PC, cpu_.SP);

                    // Emulate CPU2: format the current track
                    fdcFormatTrack(fdcDriveSelect_, fdcTrack_, fdcSide_);

                    // Break the JR $ loop by changing displacement from 0xFE to 0x00
                    // This makes JR $ (jump -2) into JR +0 (fall through to next instruction)
                    mem_.write(cpu_.PC + 1, 0x00);

                    // Disable PIO interrupts to prevent ISR spam while cleanup runs
                    fdcPioAIntEnabled_ = false;
                    fdcFormatDone_ = true;

                    frameCycles += 100;
                    continue;
                }
            }

            // Generate index pulse interrupts for FDC PIO
            // The disk rotates at ~300 RPM = one index pulse every ~200ms
            // We fire the interrupt every INDEX_PULSE_CYCLES to simulate rotation
            if (fdcPioAIntEnabled_ && cpu_.IFF1) {
                fdcIndexCycles_++;
                if (fdcIndexCycles_ >= INDEX_PULSE_CYCLES) {
                    fdcIndexCycles_ = 0;
                    // Fire IM2 interrupt with PIO A vector
                    cpu_.interrupt(fdcPioAVector_);
                    continue;
                }
            } else {
                fdcIndexCycles_ = 0;
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
