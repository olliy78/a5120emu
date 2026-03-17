/**
 * @file cpa_bios.h
 * @brief CPA/CP/M 2.2 BIOS-Emulation fuer den Robotron A5120 Emulator.
 *
 * Diese Datei enthaelt die Klassendefinition fuer CpaBios, die zentrale
 * Emulationsschicht zwischen dem emulierten Z80/U880-Prozessor und der
 * PC-Hardware. Sie emuliert das CPA-BIOS (CP/M-kompatibles Betriebssystem
 * der VEB Robotron), den Floppydisk-Controller der Baugruppe K5122 sowie
 * die Bildschirmausgabe der Bildschirmkarte K7027.
 *
 * @section trap_arch HALT-Trap-Architektur
 *
 * Die Emulation verwendet eine "Trap"-Technik, um BIOS-Aufrufe abzufangen:
 *
 * 1. Das originale CPA-BIOS wird aus der Datei \@os.com in den Z80-Speicher
 *    geladen und an die endgueltigen Adressen (ab D000h) reloziert.
 *
 * 2. Die 17 Einsprungpunkte der BIOS-Sprungtabelle (BOOT, WBOOT, CONST,
 *    CONIN, CONOUT, LIST, PUNCH, READER, HOME, SELDSK, SETTRK, SETSEC,
 *    SETDMA, READ, WRITE, LISTST, SECTRAN) werden durch HALT-Opcodes (76h)
 *    ersetzt. Alle uebrigen BIOS-Daten (Versionstring "CPAB6", DPH/DPB-
 *    Tabellen, ISR-Code, etc.) bleiben im Z80-Speicher erhalten.
 *
 * 3. Wenn der emulierte Z80 eine BIOS-Funktion per CALL aufruft und auf
 *    den HALT-Opcode trifft, stoppt die CPU. Die Emulationsschleife in
 *    run() erkennt dies, identifiziert anhand der PC-Adresse die gewuenschte
 *    BIOS-Funktion und ruft die entsprechende C++-Implementierung auf.
 *
 * 4. Zusaetzlich wird die CPA-Erweiterung cpmx21 (diskio) abgefangen,
 *    die von Programmen wie format.com fuer physische Disk-Operationen
 *    verwendet wird.
 *
 * @section fdc_arch Floppydisk-Emulation (Baugruppe K5122)
 *
 * Die Baugruppe K5122 ist die Floppydisk-Controllerkarte des A5120.
 * Sie wird ueber zwei Z80-PIOs angesteuert:
 * - Steuer-PIO (Ports 10h-13h): Kopfsteuerung, Schreibfreigabe, Schrittmotor
 * - Daten-PIO (Ports 14h-17h): Lese-/Schreibdaten
 * - Selektions-Latch (Port 18h): Laufwerksauswahl
 *
 * Die CPU-Karte K2526 enthaelt zwei Z80/U880-Prozessoren:
 * - CPU1 (Hauptprozessor): Fuehrt CPA/CP/M und Anwendungsprogramme aus
 * - CPU2 (Disk-Prozessor): Uebernimmt das byteweise Schreiben/Lesen von
 *   MFM-kodierten Spurdaten mit exaktem 32us-Timing pro Byte (250 kbit/s DD).
 *   Die Firmware fuer CPU2 befindet sich im EPROM der K2526-Baugruppe.
 *
 * Waehrend CPU2 eine Spur schreibt, parkt CPU1 in einer JR $-Schleife
 * (Opcode 18h FEh = Sprung auf sich selbst). Der Emulator erkennt dieses
 * Muster und fuehrt die Formatierung direkt durch (siehe run()).
 *
 * @section crt_arch Bildschirmemulation (Baugruppe K7027)
 *
 * Die Bildschirmkarte K7027 stellt einen 80x24-Zeichen-Textbildschirm
 * bereit. Die Emulation arbeitet mit einem Bildschirm-Puffer im
 * Z80-Speicher (ab F800h) und unterstuetzt:
 * - Direkte Zeichenausgabe an der Cursorposition
 * - Steuerzeichen: HOME (01h), BS (08h), LF (0Ah), FF (0Ch), CR (0Dh)
 * - Bildschirmoperationen: Loeschen (0Ch), Scrollen, Zeile einfuegen/loeschen
 * - Cursor-Steuerung: Sichtbar/Unsichtbar, Positionierung via ESC-Sequenzen
 * - ESC-Sequenzen: ESC = row col (ADM3A-kompatibel) fuer Cursorpositionierung
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
#pragma once
#include "z80.h"
#include "memory.h"
#include "terminal.h"
#include "floppy.h"
#include <string>
#include <vector>

/**
 * @class CpaBios
 * @brief Emuliert das CPA-BIOS und die Hardware des Robotron A5120.
 *
 * Diese Klasse bildet die zentrale Bruecke zwischen der emulierten Z80-CPU
 * und der realen PC-Hardware. Sie implementiert:
 *
 * - **BIOS-Trap-Mechanismus**: HALT-Opcodes an den 17 BIOS-Einsprungpunkten
 *   fangen Aufrufe ab und leiten sie an C++-Implementierungen weiter.
 *   Das originale BDOS laeuft nativ im Z80-Emulator.
 *
 * - **Floppydisk-Controller K5122**: Emulation der zwei Z80-PIOs fuer
 *   Kopfsteuerung und Datentransfer. Das Spur-Formatieren wird durch
 *   Erkennung der JR $-Schleife (CPU2-Emulation der K2526-Karte) realisiert.
 *
 * - **Bildschirmkarte K7027**: 80x24-Zeichen-Textmodus mit Cursor,
 *   Steuerzeichen und ESC-Sequenzen (ADM3A-kompatibel).
 *
 * - **CPA-Erweiterungen**: diskio (cpmx21) fuer physische Disk-Transfers,
 *   Stub-Eintraege fuer Timer- und Monitor-Erweiterungen.
 *
 * @note Die Klasse laedt das originale CPA-BIOS aus \@os.com und behaelt
 *       dessen Datenstrukturen (DPH, DPB, ISR-Code) im Z80-Speicher.
 *       Nur die Sprungtabellen-Eintraege werden durch HALT-Traps ersetzt.
 */
class CpaBios {
public:
    /// @name CP/M-Speicherlayout (entsprechend \@os.com)
    /// @{
    static constexpr uint16_t TPA_START     = 0x0100;  ///< Beginn des Transient Program Area
    static constexpr uint16_t IOBYTE_ADDR   = 0x0003;  ///< I/O-Byte (Geraetezuordnung)
    static constexpr uint16_t CDISK_ADDR    = 0x0004;  ///< Aktuelles Laufwerk (0=A, 1=B, ...)
    static constexpr uint16_t BDOS_ENTRY    = 0x0005;  ///< BDOS-Einsprungadresse (JP bdos+6)
    /// @}

    /// @name CPA-spezifische Speicherstellen
    /// @{
    static constexpr uint16_t CPMEXT_ADDR   = 0x004E;  ///< Zeiger auf CPA-Erweiterungstabelle (cpmx00)
    /// @}

    /// @name BIOS-Adressen (werden zur Laufzeit aus \@os.com bestimmt)
    /// @{
    uint16_t ccpBase;    ///< Basisadresse des CCP (Console Command Processor), typisch BA00h
    uint16_t bdosBase;   ///< Basisadresse des BDOS (Basic Disk Operating System), typisch C200h
    uint16_t biosBase;   ///< Basisadresse des BIOS, typisch D000h
    /// @}

    /**
     * @brief Nummern der 17 Standard-BIOS-Funktionen.
     *
     * Jede Funktion belegt 3 Bytes in der BIOS-Sprungtabelle (JP addr).
     * Der Emulator ersetzt diese durch HALT + 2x NOP und erkennt den
     * Funktionsindex aus der HALT-Adresse: func = (haltAddr - biosBase) / 3.
     */
    enum BiosFunc {
        BIOS_BOOT = 0,      ///< Kaltstart: Gesamte Initialisierung
        BIOS_WBOOT,         ///< Warmstart: Zurueck zum CCP-Kommandoprompt
        BIOS_CONST,         ///< Konsolenstatus: 0xFF = Taste bereit, 0x00 = keine
        BIOS_CONIN,         ///< Konsoleneingabe: Wartet auf Tastendruck, gibt ASCII zurueck
        BIOS_CONOUT,        ///< Konsolenausgabe: Zeichen an Bildschirm (K7027-Emulation)
        BIOS_LIST,          ///< Druckerausgabe
        BIOS_PUNCH,         ///< Lochstreifenstanzer-Ausgabe
        BIOS_READER,        ///< Lochstreifenleser-Eingabe
        BIOS_HOME,          ///< Diskettenkopf auf Spur 0 positionieren
        BIOS_SELDSK,        ///< Laufwerk auswaehlen, gibt DPH-Adresse in HL zurueck
        BIOS_SETTRK,        ///< Spurnummer setzen (BC = Spur)
        BIOS_SETSEC,        ///< Sektornummer setzen (C = Sektor)
        BIOS_SETDMA,        ///< DMA-Adresse setzen (BC = Adresse)
        BIOS_READ,          ///< Sektor lesen: 128 Bytes von Disk an DMA-Adresse
        BIOS_WRITE,         ///< Sektor schreiben: 128 Bytes von DMA-Adresse auf Disk
        BIOS_LISTST,        ///< Druckerstatus: 0xFF = bereit
        BIOS_SECTRAN,       ///< Sektornummer uebersetzen (Interleave-Tabelle)
        BIOS_COUNT           ///< Anzahl der BIOS-Funktionen (17)
    };

    /**
     * @brief Konstruktor.
     * @param cpu  Referenz auf die emulierte Z80/U880-CPU
     * @param mem  Referenz auf den 64KB-Speicher
     * @param term Referenz auf das SDL-Terminal fuer Bildschirmausgabe und Tastatur
     */
    CpaBios(Z80& cpu, Memory& mem, Terminal& term);

    /// @name Laufwerksverwaltung
    /// @{

    /**
     * @brief Haengt ein Disketten-Image an ein Laufwerk an.
     * @param drive Laufwerksnummer (0=A, 1=B, 2=C, 3=D)
     * @param imagePath Pfad zur .img-Datei (800 KB raw image)
     * @return true bei Erfolg
     */
    bool mountDisk(int drive, const std::string& imagePath);

    /**
     * @brief Laedt den Inhalt eines Host-Verzeichnisses als virtuelles Laufwerk.
     *
     * Die Dateien im Verzeichnis werden in das CP/M-Dateisystem-Format
     * konvertiert und als virtuelles Disketten-Image bereitgestellt.
     *
     * @param drive Laufwerksnummer (0-3)
     * @param dirPath Pfad zum Host-Verzeichnis
     * @return true bei Erfolg
     */
    bool mountDirectory(int drive, const std::string& dirPath);

    /**
     * @brief Haengt ein Laufwerk aus. Speichert ein modifiziertes Image automatisch.
     * @param drive Laufwerksnummer (0-3)
     */
    void unmountDisk(int drive);
    /// @}

    /// @name Boot-Funktionen
    /// @{

    /**
     * @brief Startet das System von einer \@os.com-Datei.
     *
     * Laedt \@os.com an Adresse 0100h, fuehrt die Relokation durch
     * (CCP nach BA00h, BDOS nach C200h, BIOS nach D000h), installiert
     * die HALT-Traps und startet den CCP.
     *
     * Die Relokationsparameter werden direkt aus dem Loader-Code in der
     * Datei gelesen (LD DE,dest bei Offset 6Ch, LD HL,end bei 6Fh,
     * JP biosEntry bei 7Ah).
     *
     * Nach cpu_.reset() werden I=F7h und IM=2 gesetzt, damit das
     * IM2-Interruptsystem fuer die FDC-PIO-Interrupts funktioniert.
     *
     * @param osPath Pfad zur \@os.com-Datei
     * @return true bei Erfolg
     */
    bool bootFromFile(const std::string& osPath);

    /**
     * @brief Startet das System von einem Disketten-Image.
     *
     * Liest die System-Spuren (0 und 1) und fuehrt danach die gleiche
     * Relokation wie bootFromFile() durch.
     *
     * @param drive Laufwerksnummer (0-3)
     * @return true bei Erfolg
     */
    bool bootFromDisk(int drive);
    /// @}

    /// @name BIOS-Trap-Behandlung
    /// @{

    /**
     * @brief Behandelt einen BIOS-Trap (ausgeloest durch HALT-Opcode).
     *
     * Wird aufgerufen, wenn die Z80-CPU auf einen HALT-Opcode an einer
     * BIOS-Sprungtabellen-Adresse oder der cpmx21-Adresse trifft.
     *
     * Der Ablauf ist:
     * 1. Aus der HALT-Adresse (PC-1) wird die BIOS-Funktionsnummer berechnet
     * 2. Die entsprechende C++-Implementierung wird aufgerufen
     * 3. Die Ruecksprungadresse wird vom Stack geholt (cpu_.pop())
     * 4. Ausnahme: BOOT und WBOOT setzen PC direkt, kein pop()
     *
     * @param addr Aktuelle PC-Adresse (HALT-Adresse + 1, da PC nach HALT weiterrueckt)
     * @return true wenn der Trap behandelt wurde, false wenn die Adresse
     *         nicht zu einer bekannten BIOS-Funktion gehoert
     */
    bool handleBiosTrap(uint16_t addr);

    /**
     * @brief Behandelt einen BDOS-Trap (derzeit nicht verwendet).
     *
     * Das originale BDOS laeuft nativ im Z80-Emulator. Diese Methode
     * existiert als Erweiterungspunkt, falls einzelne BDOS-Aufrufe
     * kuenftig abgefangen werden sollen.
     *
     * @return Immer false (kein Trap behandelt)
     */
    bool handleBdosTrap();

    /**
     * @brief Installiert HALT-Traps an den BIOS-Einsprungpunkten.
     *
     * Diese Methode fuehrt folgende Schritte aus:
     * 1. Schreibt HALT + 2x NOP an die 17 BIOS-Sprungtabellen-Eintraege
     *    (biosBase + 0*3 bis biosBase + 16*3)
     * 2. Richtet die Page-Zero-Eintraege ein:
     *    - 0000h: JP zum WBOOT-Trap
     *    - 0003h: IOBYTE = 00h
     *    - 0004h: Aktuelles Laufwerk
     *    - 0005h: JP zum BDOS-Einsprung (bdosBase + 6)
     *    - 004Eh: Zeiger auf CPA-Erweiterungstabelle cpmx00
     * 3. Installiert einen HALT-Trap fuer cpmx21 (diskio-Erweiterung)
     * 4. Setzt RET (C9h) fuer unbenutzte CPA-Erweiterungen (Timer, Monitor)
     * 5. Initialisiert DPH/DPB-Strukturen (buildDiskParams())
     * 6. Setzt die IM2-Interruptvektortabelle (F7E8h/F7EAh) auf itimeo
     * 7. Loescht den Bildschirmpuffer (K7027-Emulation)
     */
    void installTraps();
    /// @}

    /**
     * @brief Haupt-Emulationsschleife.
     *
     * Fuehrt Z80-Instruktionen im 50-Hz-Takt aus (~50000 Zyklen/Frame bei
     * 2.5 MHz). Erkennt und behandelt:
     * - BIOS-Traps (HALT an Sprungtabellen-Adressen)
     * - Warmstart-Ausloesung (JP 0000h)
     * - CPU2-Formatierschleife (JR $ + Schreibmodus + PIO-Interrupt)
     * - Index-Puls-Interrupts fuer die FDC-PIO-Synchronisation
     */
    void run();

    /**
     * @brief Aktualisiert die SDL-Bildschirmausgabe.
     *
     * Liest den Bildschirmpuffer ab Memory::SCREEN_START (F800h),
     * setzt temporaer ein Cursor-Marker-Bit (Bit 7) an der aktuellen
     * Cursorposition und uebertraegt den Puffer an das SDL-Terminal.
     */
    void updateDisplay();

    /**
     * @brief Periodischer Timer-Tick (wird aus der Hauptschleife aufgerufen).
     *
     * Aktualisiert den Bildschirm alle 50 Ticks (~50 Hz).
     */
    void tick();

    /// @name Floppydisk-Controller-Emulation (Baugruppe K5122)
    /// @{

    /**
     * @brief Liest einen FDC-PIO-Port (Ports 10h-18h).
     *
     * Die Baugruppe K5122 verwendet zwei Z80-PIOs:
     * - Port 10h: Steuer-PIO A Daten (Ruecklesen des Control-Ausgangs)
     * - Port 12h: Steuer-PIO B Status (Laufwerksstatus, aktiv-low):
     *   - Bit 0: /RDY  (0=bereit)
     *   - Bit 1: /MKE  (1=keine Marke gefunden)
     *   - Bit 4: /FA   (1=kein AMF-Fehler)
     *   - Bit 5: /WP   (1=nicht schreibgeschuetzt)
     *   - Bit 6: /FW   (1=kein Schreibfehler)
     *   - Bit 7: /T0   (0=auf Spur 0, 1=nicht auf Spur 0)
     * - Port 16h: Daten-PIO B Lesedaten
     *
     * @param port I/O-Portadresse (10h-18h)
     * @return Gelesener Wert
     */
    uint8_t fdcReadPort(uint8_t port);

    /**
     * @brief Schreibt auf einen FDC-PIO-Port (Ports 10h-18h).
     *
     * Emuliert die Portausgaben der Baugruppe K5122:
     * - Port 10h: Steuer-PIO A Daten (Kopfsteuerung):
     *   - Bit 0: /WE   (0=Schreibfreigabe, aktiv-low)
     *   - Bit 2: /SIDE (0=Seite 1, 1=Seite 0, aktiv-low)
     *   - Bit 5: SD    (0=Step-Out Richtung Spur 0, 1=Step-In)
     *   - Bit 7: /ST   (Schrittimpuls, 0→1 Flanke = Schritt ausfuehren)
     * - Port 11h: Steuer-PIO A Steuerregister:
     *   - Bit 0=0: Interruptvektor setzen
     *   - Low-Nibble 03h: Interrupt ein/aus (Bit 7: 1=ein)
     * - Port 14h: Daten-PIO A Schreibdaten (zum Track-Puffer)
     * - Port 18h: Selektions-Latch (Bits 7..4 waehlen Laufwerk 3..0, aktiv-low)
     *
     * @param port I/O-Portadresse (10h-18h)
     * @param val  Zu schreibender Wert
     */
    void fdcWritePort(uint8_t port, uint8_t val);
    /// @}

    /// @name Logging und Automatisierung
    /// @{
    void setConsoleLog(FILE* f) { consoleLog_ = f; }  ///< Setzt die Datei fuer Konsolenausgabe-Mitschnitt
    void setDiagLog(FILE* f) { diagLog_ = f; }        ///< Setzt die Datei fuer Diagnose-Log (Standard: stderr)
    void setAutoExit(bool enable) { autoExit_ = enable; } ///< Aktiviert automatisches Beenden nach Kommandoausfuehrung
    void setExecCommand(const std::string& cmd) { execCommand_ = cmd; } ///< Setzt das automatisch auszufuehrende Kommando
    /// @}

private:
    Z80& cpu_;        ///< Emulierte Z80/U880-CPU (Baugruppe K2526, CPU1)
    Memory& mem_;     ///< 64KB Z80-Speicher
    Terminal& term_;  ///< SDL-Terminal fuer Bildschirm und Tastatur

    /// @name Logischer Disk-Zustand (BIOS-Ebene)
    /// @{
    FloppyDisk disks_[4];  ///< Laufwerke A: bis D: mit je 800 KB (80 Spuren, 2 Seiten, DD)
    int currentDisk_;      ///< Aktuell gewaehltes Laufwerk (0-3)
    int currentTrack_;     ///< Aktuelle logische Spur (vom BDOS gesetzt via SETTRK)
    int currentSector_;    ///< Aktueller logischer Sektor (vom BDOS gesetzt via SETSEC)
    uint16_t dmaAddr_;     ///< DMA-Adresse fuer Sektor-Transfers (Standard: 0080h)
    /// @}

    /// @name DPH-Adressen (Disk Parameter Header) fuer jedes Laufwerk
    /// @{
    uint16_t dphAddr_[4];  ///< Zeiger auf DPH im Z80-Speicher (aus dem originalen BIOS)
    /// @}

    /**
     * @brief CPA-Sektor-Interleave-Tabelle (26 Eintraege).
     *
     * Ordnet logische Sektornummern physischen Sektoren zu.
     * Die Verschachtelung reduziert die Zugriffszeit bei sequentiellen Lesevorgaengen,
     * da der naechste logische Sektor erst nach einer Teilrotation der Diskette kommt.
     */
    static const uint8_t sectorXlat_[26];

    /**
     * @brief HALT-Opcode (76h), verwendet als Trap-Mechanismus.
     *
     * Wenn die Z80-CPU einen HALT-Opcode ausfuehrt, stoppt sie und setzt
     * das halted-Flag. Die Emulationsschleife in run() erkennt dies und
     * prueft, ob die HALT-Adresse zu einem BIOS-Einsprungpunkt gehoert.
     * Ist dies der Fall, wird die entsprechende C++-Funktion aufgerufen
     * statt den Original-BIOS-Code auszufuehren.
     */
    static constexpr uint8_t TRAP_OPCODE = 0x76;

    /**
     * @brief Adresse des cpmx21-Traps (diskio-Erweiterung).
     *
     * Die CPA-Erweiterung diskio (cpmx21 bei biosBase + 129h) wird von
     * Programmen wie format.com fuer physische Disk-Operationen (Seek,
     * Sektor-I/O) verwendet. Auch hier wird ein HALT-Trap installiert.
     */
    uint16_t cpmx21Addr_;

    /**
     * @name FDC-Emulationszustand (Baugruppe K5122, PIO-basiert)
     *
     * Der A5120 steuert den Floppydisk-Controller K5122 ueber zwei Z80-PIOs:
     *
     * **Steuer-PIO** (Ports 10h-13h):
     * - Port A (10h, Ausgang): Kopfsteuerung mit aktiv-low Signalen
     *   - Bit 0: /WE   (0=Schreibfreigabe)
     *   - Bit 2: /SIDE (0=Seite 1, 1=Seite 0)
     *   - Bit 5: SD    (0=Step-Out Richtung Spur 0, 1=Step-In)
     *   - Bit 7: /ST   (Schrittimpuls, steigende Flanke 0→1 = Schritt)
     * - Port B (12h, Eingang): Laufwerksstatus (aktiv-low)
     *   - Bit 0: /RDY  (0=Laufwerk bereit)
     *   - Bit 5: /WP   (0=schreibgeschuetzt)
     *   - Bit 7: /T0   (0=Kopf auf Spur 0)
     *
     * **Daten-PIO** (Ports 14h-17h):
     * - Port A (14h): Schreibdaten zum Controller
     * - Port B (16h): Lesedaten vom Controller
     *
     * **Selektions-Latch** (Port 18h):
     * - Bits 7..4: Laufwerk 3..0 (aktiv-low, je 1 Bit)
     *
     * @{
     */
    uint8_t fdcDriveSelect_;    ///< Aktuell selektiertes Laufwerk (0-3)
    int fdcTrack_;              ///< Aktuelle physische Kopfposition (Spur 0-79)
    int fdcSide_;               ///< Aktuelle Diskettenseite (0 oder 1)
    bool fdcAtTrack0_;          ///< true wenn Kopf auf Spur 0 steht
    uint8_t fdcControlA_;       ///< Letzter geschriebener Wert an Steuer-PIO A (Port 10h)
    bool fdcWriteMode_;         ///< true wenn Schreibfreigabe aktiv (/WE=0)
    std::vector<uint8_t> fdcTrackBuf_; ///< Puffer fuer geschriebene Spurdaten (MFM-Rohdaten)
    int fdcWriteCount_;         ///< Anzahl geschriebener Bytes in der aktuellen Operation
    /// @}

    /**
     * @name PIO-Interrupt-Zustand (fuer Index-Puls / Format-Synchronisation)
     *
     * Die K5122-Baugruppe generiert Interrupts ueber die PIO, insbesondere
     * den Indexpuls-Interrupt bei jeder Diskettenumdrehung (~300 RPM = 200ms).
     * Der Emulator erzeugt diese Interrupts zyklisch im IM2-Modus:
     *   Vektoradresse = (I << 8) | (fdcPioAVector_ & 0xFE)
     *   ISR-Adresse = mem[Vektoradresse] | (mem[Vektoradresse+1] << 8)
     * @{
     */
    uint8_t fdcPioAVector_;     ///< PIO-A-Interruptvektor (Low-Byte, gesetzt ueber Port 11h)
    bool fdcPioAIntEnabled_;    ///< true wenn PIO-A-Interrupts aktiviert (Port 11h, Bit 7)
    uint64_t fdcIndexCycles_;   ///< Zyklenzaehler fuer Indexpuls-Timing
    static constexpr int INDEX_PULSE_CYCLES = 50000; ///< Zyklen pro Umdrehung (~20ms bei 2.5 MHz)
    bool fdcFormatDone_;        ///< Markiert, dass aktuelle Spur formatiert wurde (CPU2-Emulation)
    /// @}

    /**
     * @name CRT-Zustand (Bildschirmkarte K7027)
     *
     * Die Bildschirmkarte K7027 wird ueber einen Speicherpuffer emuliert.
     * Der Puffer beginnt bei Memory::SCREEN_START (F800h) und umfasst
     * 80 * 24 = 1920 Zeichen. Die Cursorposition wird als absolute
     * Speicheradresse innerhalb dieses Puffers verwaltet.
     * @{
     */
    uint16_t cursorPos_;    ///< Aktuelle Cursorposition (absolute Adresse ab F800h)
    bool cursorVisible_;    ///< true wenn Cursor sichtbar (Steuercode 02h ein, 03h aus)
    uint8_t escapeState_;   ///< Zustandsmaschine fuer ESC-Sequenzen (0=normal, 1=ESC, 2=Zeile, 3=Spalte)
    uint8_t escapeRow_;     ///< Zwischengespeicherte Zeile waehrend ESC-Sequenz-Verarbeitung
    /// @}

    /// @name Timer und Laufzeitkontrolle
    /// @{
    uint64_t tickCount_;   ///< Fortlaufender Tick-Zaehler
    bool running_;         ///< true solange die Emulation laeuft
    /// @}

    /// @name Logging und Automatisierung
    /// @{
    FILE* consoleLog_;       ///< Ausgabedatei fuer Konsolen-Mitschnitt (nullptr = aus)
    FILE* diagLog_;          ///< Ausgabedatei fuer Diagnose-Log (nullptr = stderr)
    bool autoExit_;          ///< Automatisch beenden wenn Exec-Kommando fertig
    std::string execCommand_; ///< Automatisch auszufuehrendes Kommando nach dem Boot
    bool execDone_;          ///< Alle injizierten Tasten wurden verbraucht
    /// @}

    /// @name Interne BIOS-Funktionen
    /// Jede Methode entspricht einer BIOS-Sprungtabellen-Funktion.
    /// @{
    void biosBoot();                              ///< BIOS 0: Kaltstart
    void biosWboot();                             ///< BIOS 1: Warmstart (zurueck zum CCP)
    uint8_t biosConst();                          ///< BIOS 2: Konsolenstatus (0xFF/0x00)
    uint8_t biosConin();                          ///< BIOS 3: Zeichen von Tastatur lesen (blockierend)
    void biosConout(uint8_t ch);                  ///< BIOS 4: Zeichen auf Bildschirm ausgeben (K7027)
    void biosList(uint8_t ch);                    ///< BIOS 5: Zeichen an Drucker ausgeben
    uint8_t biosPunch(uint8_t ch);                ///< BIOS 6: Lochstreifenstanzer
    uint8_t biosReader();                         ///< BIOS 7: Lochstreifenleser (gibt 1Ah/EOF zurueck)
    void biosHome();                              ///< BIOS 8: Kopf auf Spur 0
    uint16_t biosSeldsk(uint8_t disk, uint8_t logged); ///< BIOS 9: Laufwerk waehlen → DPH
    void biosSettrk(uint16_t track);              ///< BIOS 10: Spurnummer setzen
    void biosSetsec(uint8_t sector);              ///< BIOS 11: Sektornummer setzen
    void biosSetdma(uint16_t addr);               ///< BIOS 12: DMA-Adresse setzen
    uint8_t biosRead();                           ///< BIOS 13: 128-Byte-Sektor lesen
    uint8_t biosWrite(uint8_t type);              ///< BIOS 14: 128-Byte-Sektor schreiben
    uint8_t biosListst();                         ///< BIOS 15: Druckerstatus (immer 0xFF)
    uint16_t biosSectran(uint16_t sector, uint16_t table); ///< BIOS 16: Sektornummer uebersetzen
    /// @}

    /// @name CRT-Zeichenverarbeitung (entsprechend bioscrt.mac der Baugruppe K7027)
    /// @{

    /**
     * @brief Verarbeitet ein einzelnes Zeichen fuer die Bildschirmausgabe.
     *
     * Implementiert die Steuercode-Verarbeitung der K7027-Bildschirmkarte:
     * - 00h: NOP (ignoriert)
     * - 01h: HOME (Cursor links oben)
     * - 02h: Cursor einschalten
     * - 03h: Cursor ausschalten
     * - 07h/87h: BELL (Signalton)
     * - 08h: Backspace (Cursor eine Position zurueck)
     * - 0Ah: Line Feed (Cursor eine Zeile nach unten, ggf. Scrollen)
     * - 0Ch: Form Feed (Bildschirm loeschen)
     * - 0Dh: Carriage Return (Cursor an Zeilenanfang)
     * - 14h: Loeschen bis Bildschirmende
     * - 15h: Cursor rechts
     * - 16h: Loeschen bis Zeilenende
     * - 17h: Zeile einfuegen (nachfolgende Zeilen nach unten schieben)
     * - 18h: Zeile loeschen + Cursor an Zeilenanfang
     * - 19h: Zeile entfernen (nachfolgende Zeilen nach oben schieben)
     * - 1Ah/1Ch: Cursor hoch
     * - 1Bh: ESC (Beginn einer Escape-Sequenz)
     * - 7Fh: Delete (Zeichen an Cursorposition durch Leerzeichen ersetzen)
     * - 20h-7Eh: Druckbare Zeichen (an Cursorposition schreiben)
     * - 84h-86h: Videoattribute (K7027-spezifisch)
     *
     * ESC-Sequenzen (ADM3A-kompatibel):
     * - ESC = row col: Cursor auf Zeile/Spalte positionieren (jeweils + 20h)
     * - ESC Y row col: Alternative Cursorpositionierung
     *
     * @param ch Das auszugebende Zeichen oder Steuerzeichen
     */
    void crtPutChar(uint8_t ch);

    /** @brief Scrollt den Bildschirm um eine Zeile nach oben. Die unterste Zeile wird geloescht. */
    void crtScroll();

    /** @brief Loescht den gesamten Bildschirm und setzt den Cursor nach links oben. */
    void crtClearScreen();

    /** @brief Loescht alle Zeichen von der aktuellen Cursorposition bis zum Zeilenende. */
    void crtClearToEol();

    /** @brief Setzt den Cursor auf die HOME-Position (Zeile 0, Spalte 0). */
    void crtCursorHome();

    /**
     * @brief Setzt den Cursor auf eine bestimmte Bildschirmposition.
     * @param row Zeile (0-23, wird auf gueltigen Bereich begrenzt)
     * @param col Spalte (0-79, wird auf gueltigen Bereich begrenzt)
     */
    void crtSetCursor(int row, int col);
    /// @}

    /// @name Disk-Parameter und physische Disk-Operationen
    /// @{

    /**
     * @brief Initialisiert die DPH/DPB-Strukturen fuer alle Laufwerke.
     *
     * Verwendet die originalen DPH/DPB-Strukturen aus dem geladenen BIOS-Code.
     * Das CPA-BIOS enthaelt zusaetzliche Erweiterungsfelder (dpbtyp, dpbflg,
     * dpbslc, dpbdnr, dpbptr), die von Programmen wie format.com gelesen
     * werden, um den Laufwerkstyp zu bestimmen.
     *
     * dpbtyp-Bitfelder:
     * - Bit 0: 1=DS (doppelseitig)
     * - Bit 3: 1=DD (doppelte Dichte)
     * - Bit 4: 1=5.25" (sonst 8")
     * - Bit 5: 1=80 Spuren (sonst 40 oder 77)
     */
    void buildDiskParams();

    /**
     * @brief Parst geschriebene MFM-Spurdaten und extrahiert Sektoren.
     *
     * Sucht im fdcTrackBuf_ nach ID-Adressmarken (A1 A1 A1 FE) und
     * Datenmarken (A1 A1 A1 FB), um die formatierten Sektoren zu
     * identifizieren und auf das Disketten-Image zu schreiben.
     *
     * Physische 1024-Byte-Sektoren werden in je 8 logische 128-Byte-
     * CP/M-Sektoren aufgeteilt.
     */
    void fdcParseTrackData();

    /**
     * @brief Emuliert CPU2 der K2526-Baugruppe: Formatiert eine komplette Spur.
     *
     * Auf der realen Hardware uebernimmt CPU2 (der zweite Z80 auf der
     * K2526-CPU-Karte) das byteweise Schreiben des MFM-Spurformats
     * mit exaktem 32us-Timing pro Byte. Der Emulator ruft diese Funktion
     * auf, wenn die JR $-Schleife erkannt wird, und fuellt alle 26
     * logischen 128-Byte-Sektoren der Spur mit E5h (Standard-Fuellbyte).
     *
     * @param drive Laufwerksnummer (0-3)
     * @param track Physische Spurnummer (0-79)
     * @param side  Diskettenseite (0 oder 1)
     */
    void fdcFormatTrack(int drive, int track, int side);
    /// @}

    /**
     * @brief Behandelt den CPA-Erweiterungsaufruf cpmx21 (diskio).
     *
     * Wird von format.com und anderen Programmen fuer physische
     * Disk-Operationen (Seek, Sektor-I/O) verwendet.
     *
     * Schnittstelle (aus biosdsk.mac):
     * - HL zeigt auf einen CDB (Command Descriptor Block):
     *   - +0: cdbfl   Flags (Bit 2: Schreiben, Bit 5: Kopf abheben)
     *   - +1: cdbdev  Logische Geraetenummer (0..3)
     *   - +2: cdbtrk  Spurnummer
     *   - +3: cdbsid  Seitennummer
     *   - +4: cdbsec  Sektornummer
     *   - +5: cdbslc  Sektorlaengen-Code (0=128, 1=256, 2=512, 3=1024)
     *   - +6: cdbsnb  Anzahl zu transferierender Sektoren (0 = nur positionieren)
     *   - +7/+8: cdbdma Transferadresse (Little-Endian)
     * - Rueckgabe: A=0 (Z-Flag) bei Erfolg, A!=0 (NZ) bei Fehler
     */
    void biosDiskio();

    /**
     * @brief Diagnosemeldung im printf-Format ausgeben.
     *
     * Schreibt nach diagLog_ (falls gesetzt) oder stderr.
     * @param fmt printf-Formatstring
     */
    void diagf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};
