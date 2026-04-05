/**
 * @file cpm_bdos.h
 * @brief CP/M 2.2 BDOS-Emulation mit direktem Host-Dateisystem-Zugriff.
 *
 * Implementiert die CP/M BDOS-Schnittstelle (Aufruf ueber CALL 0005h)
 * als C++-Klasse, die Dateioperationen direkt auf das Host-Dateisystem
 * abbildet. Wird von cparun verwendet, um .com-Programme ohne
 * Disketten-Image-Emulation auszufuehren.
 *
 * Unterstuetzte BDOS-Funktionen:
 * - Konsole: Eingabe (1), Ausgabe (2), Direct I/O (6), String (9),
 *   Zeilenpuffer (10), Status (11)
 * - Dateien: Open (15), Close (16), Search (17/18), Delete (19),
 *   Read/Write Sequential (20/21), Make (22), Rename (23),
 *   Read/Write Random (33/34/40), File Size (35), Set Random (36)
 * - System: Reset (0/13), Version (12), Select Disk (14),
 *   Get Disk (25), Set DMA (26), User Code (32)
 *
 * @author Olaf Krieger
 * @license MIT License
 */
#pragma once
#include "z80.h"
#include "memory.h"
#include <string>
#include <map>
#include <vector>
#include <cstdio>

/**
 * @class CpmBdos
 * @brief CP/M 2.2 BDOS-Emulation mit Host-Dateisystem-Zugriff.
 *
 * Faengt CALL 0005h (BDOS) per HALT-Trap ab und implementiert die
 * CP/M-Datei- und Konsolenfunktionen direkt in C++. FCB-Dateinamen
 * werden in Host-Dateinamen konvertiert (Grossbuchstaben → Kleinbuchstaben,
 * case-insensitive Suche im Arbeitsverzeichnis).
 *
 * Speicherlayout:
 * - 0x0000: JP WBOOT_TRAP (Warmstart-Vektor)
 * - 0x0005: JP BDOS_TRAP  (BDOS-Einsprung)
 * - 0x005C: FCB1 (erster Dateiname aus Kommandozeile)
 * - 0x006C: FCB2 (zweiter Dateiname)
 * - 0x0080: Kommando-Tail (Laengenbyte + Text + CR)
 * - 0x0100: .com-Programm
 * - 0xF000: HALT (BDOS-Trap)
 * - 0xF003: HALT (Warmstart-Trap)
 */
class CpmBdos {
public:
    static constexpr uint16_t BDOS_TRAP  = 0xF000;
    static constexpr uint16_t WBOOT_TRAP = 0xF003;

    CpmBdos(Z80& cpu, Memory& mem, const std::string& workDir);
    ~CpmBdos();

    /// Richtet Page Zero ein (Sprungvektoren, HALT-Traps).
    void setup();

    /// Laedt eine .com-Datei in den Speicher ab 0x0100.
    bool loadCom(const std::string& name);

    /// Parst die Kommandozeile in FCB1, FCB2 und Kommando-Tail.
    void parseCommandLine(const std::string& args);

    /// Hauptschleife: fuehrt Z80-Code aus bis Programmende. Gibt Exit-Code zurueck.
    int run();

private:
    Z80& cpu_;
    Memory& mem_;
    std::string workDir_;
    uint16_t dmaAddr_;
    int currentDrive_;
    int currentUser_;
    bool running_;

    /// Offene Datei: FILE-Handle und Host-Pfad.
    struct OpenFile {
        FILE* fp;
        std::string path;
    };
    std::map<uint16_t, OpenFile> openFiles_;

    /// Suchergebnisse fuer BDOS 17/18 (Search First/Next).
    std::vector<std::string> searchResults_;
    size_t searchIndex_;

    // Hilfsfunktionen
    std::string fcbToName(uint16_t addr);
    std::string findFileCI(const std::string& name);
    std::string hostPath(const std::string& name);
    void nameToFcb(const std::string& name, uint8_t* fcb);
    void parseFcb(const std::string& token, uint16_t addr);
    bool matchPattern(const uint8_t* pat, const uint8_t* name);

    /// BDOS-Dispatch: liest Funktionsnummer aus C, fuehrt aus. false = Programmende.
    bool dispatch();

    // BDOS-Funktionsimplementierungen
    uint8_t bdosOpenFile();
    uint8_t bdosCloseFile();
    uint8_t bdosSearchFirst();
    uint8_t bdosSearchNext();
    uint8_t bdosDeleteFile();
    uint8_t bdosReadSeq();
    uint8_t bdosWriteSeq();
    uint8_t bdosMakeFile();
    uint8_t bdosRenameFile();
    uint8_t bdosReadRand();
    uint8_t bdosWriteRand();
    void bdosFileSize();
    void bdosSetRandRec();
};
