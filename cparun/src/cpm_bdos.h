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

    /**
     * @brief Richtet Page Zero und HALT-Traps ein.
     *
     * Schreibt Sprungvektoren fuer Warmstart (0x0000) und BDOS (0x0005)
     * sowie HALT-Opcodes an den Trap-Adressen in den Z80-Speicher.
     */
    void setup();

    /**
     * @brief Laedt eine .com-Datei in den Z80-Speicher ab 0x0100.
     *
     * Sucht die Datei case-insensitiv im Arbeitsverzeichnis.
     * Haengt ".com" an, falls die Erweiterung fehlt.
     * Initialisiert CPU-Register und legt 0x0000 als Warmstart-Ruecksprung
     * auf den Stack.
     *
     * @param name Programmname, mit oder ohne ".com"-Erweiterung.
     * @return true bei Erfolg, false wenn die Datei nicht gefunden wurde.
     */
    bool loadCom(const std::string& name);

    /**
     * @brief Parst die Kommandozeile in FCB1, FCB2 und Kommando-Tail.
     *
     * Konvertiert den Text nach Grossbuchstaben (CP/M-Konvention) und
     * schreibt die ersten zwei Dateinamen-Token in die FCBs bei 0x005C
     * und 0x006C. Der gesamte Text wird als Kommando-Tail ab 0x0080
     * abgelegt (Laengenbyte + " ARGS..." + CR).
     *
     * @param args Argumentstring (alles hinter dem Programmnamen).
     */
    void parseCommandLine(const std::string& args);

    /**
     * @brief Fuehrt die Z80-Emulation aus, bis das Programm endet.
     *
     * Laeuft in einer Schleife, bis ein HALT-Opcode an einer Trap-Adresse
     * oder ein WBOOT_TRAP ausgeloest wird. BDOS-Aufrufe werden per
     * dispatch() in C++ behandelt.
     *
     * @return 0 bei normalem Programmende, 1 bei unerwartetem HALT.
     */
    int run();

private:
    Z80& cpu_;           ///< Referenz auf die Z80-CPU-Instanz.
    Memory& mem_;        ///< Referenz auf den 64-KB-Adressraum.
    std::string workDir_; ///< Arbeitsverzeichnis fuer Dateioperationen auf dem Host.
    uint16_t dmaAddr_;   ///< Aktuelle DMA-Transferadresse (BDOS 26, Standard: 0x0080).
    int currentDrive_;   ///< Aktuell selektiertes Laufwerk (0=A, 1=B, ...).
    int currentUser_;    ///< Aktueller User-Bereich (0-15, BDOS 32).
    bool running_;       ///< true solange die Emulationsschleife laeuft.

    /// @brief Offene Datei: stdio-Handle und vollstaendiger Host-Pfad.
    struct OpenFile {
        FILE* fp;        ///< Geoeffnetes stdio-Handle (nullptr wenn geschlossen).
        std::string path; ///< Absoluter Pfad der Datei auf dem Host.
    };
    /// Abbildung FCB-Adresse → geoeffnete Datei.
    std::map<uint16_t, OpenFile> openFiles_;

    /// Zwischengespeicherte Trefferliste fuer BDOS 17/18 (Search First/Next).
    std::vector<std::string> searchResults_;
    size_t searchIndex_; ///< Index des naechsten zurueckzugebenden Treffers.

    // =========================================================================
    /// @name Hilfsfunktionen
    // =========================================================================
    /// @{

    /**
     * @brief Liest einen FCB aus dem Z80-Speicher und gibt den Dateinamen zurueck.
     *
     * Konvertiert das CP/M 8+3-Format (Grossbuchstaben, Leerzeichen-gefuellt)
     * in einen Host-Dateinamen in Kleinbuchstaben, z.B. "BIOS    MAC" → "bios.mac".
     * Bit 7 jedes Zeichens (Dateiattribute) wird maskiert.
     *
     * @param addr FCB-Startadresse im Z80-Speicher (Byte 0 = Laufwerk).
     * @return Dateiname in Kleinbuchstaben oder leerer String bei leerem FCB.
     */
    std::string fcbToName(uint16_t addr);

    /**
     * @brief Sucht eine Datei case-insensitiv im Arbeitsverzeichnis.
     *
     * Vergleicht den gesuchten Namen mit allen Eintraegen im Verzeichnis
     * ohne Ruecksicht auf Gross-/Kleinschreibung (z.B. fuer "BIOS.MAC"
     * wird auch "bios.mac" gefunden).
     *
     * @param name Gesuchter Dateiname (beliebige Gross-/Kleinschreibung).
     * @return Tatsaechlicher Dateiname im Verzeichnis oder leerer String.
     */
    std::string findFileCI(const std::string& name);

    /**
     * @brief Kombiniert Arbeitsverzeichnis und Dateiname zu einem Host-Pfad.
     * @param name Dateiname (relativ zum Arbeitsverzeichnis).
     * @return Vollstaendiger Pfad (workDir_ + "/" + name).
     */
    std::string hostPath(const std::string& name);

    /**
     * @brief Konvertiert einen Host-Dateinamen in 11-Byte FCB-Format.
     *
     * Schreibt Dateiname (8 Bytes) und Erweiterung (3 Bytes) in Grossbuchstaben,
     * Leerzeichen-aufgefuellt, in den Zielpuffer (Byte 0 = User, bleibt 0).
     *
     * @param name Host-Dateiname (z.B. "bios.mac").
     * @param fcb  Zielpuffer, mindestens 12 Bytes (Byte 0=User, 1-11=Name+Ext).
     */
    void nameToFcb(const std::string& name, uint8_t* fcb);

    /**
     * @brief Parst einen Dateinamen-Token und schreibt ihn als FCB in den Z80-Speicher.
     *
     * Token-Format: [d:]dateiname[.erweiterung]
     * Optionaler Laufwerksbuchstabe (A:-P:), Name (max. 8 Zeichen),
     * Erweiterung (max. 3 Zeichen). Alles wird nach Grossbuchstaben konvertiert.
     *
     * @param token Dateinamen-Token aus der Kommandozeile.
     * @param addr  Zieladresse des FCBs im Z80-Speicher.
     */
    void parseFcb(const std::string& token, uint16_t addr);

    /**
     * @brief Vergleicht ein FCB-Namensmuster mit einem FCB-Namen (Wildcard-Unterstuetzung).
     *
     * Vergleicht jeweils 11 Bytes (Name + Erweiterung). Ein '?' im Muster
     * passt auf jedes beliebige Zeichen. Bit 7 wird in beiden Puffern maskiert.
     *
     * @param pat  11-Byte Muster (Bytes 1-11 eines FCBs).
     * @param name 11-Byte Dateiname (Bytes 1-11 eines FCBs).
     * @return true wenn das Muster auf den Namen passt.
     */
    bool matchPattern(const uint8_t* pat, const uint8_t* name);

    /**
     * @brief Dispatcht einen BDOS-Aufruf anhand der Funktionsnummer in Register C.
     *
     * Liest den Funktionscode aus cpu_.C und fuehrt die entsprechende
     * BDOS-Funktion aus. Schreibt das Ergebnis in cpu_.A und cpu_.L.
     *
     * @return true um die Emulation fortzusetzen, false zum Beenden (Funktion 0).
     */
    bool dispatch();

    /// @}

    // =========================================================================
    /// @name BDOS-Dateioperationen
    // =========================================================================
    /// @{

    /** @brief BDOS 15 – Datei oeffnen. @return 0 bei Erfolg, 0xFF wenn nicht gefunden. */
    uint8_t bdosOpenFile();
    /** @brief BDOS 16 – Datei schliessen. @return 0 bei Erfolg. */
    uint8_t bdosCloseFile();
    /**
     * @brief BDOS 17 – Erster Treffer bei Verzeichnissuche.
     *
     * Scannt das Arbeitsverzeichnis nach dem FCB-Muster (Wildcards erlaubt)
     * und schreibt den ersten Treffer als 32-Byte-Verzeichniseintrag an die DMA-Adresse.
     *
     * @return 0 bei Treffer (Eintrag bei DMA-Offset 0), 0xFF wenn nicht gefunden.
     */
    uint8_t bdosSearchFirst();
    /** @brief BDOS 18 – Naechster Treffer. @return 0 bei Treffer, 0xFF wenn keine weiteren. */
    uint8_t bdosSearchNext();
    /** @brief BDOS 19 – Datei loeschen (Wildcards erlaubt). @return 0 bei Erfolg, 0xFF bei Fehler. */
    uint8_t bdosDeleteFile();
    /** @brief BDOS 20 – Sequentiell lesen (128 Bytes → DMA). @return 0 bei Erfolg, 1 bei EOF. */
    uint8_t bdosReadSeq();
    /** @brief BDOS 21 – Sequentiell schreiben (128 Bytes von DMA). @return 0 bei Erfolg, 5 bei Fehler. */
    uint8_t bdosWriteSeq();
    /** @brief BDOS 22 – Datei erstellen. @return 0 bei Erfolg, 0xFF bei Fehler. */
    uint8_t bdosMakeFile();
    /** @brief BDOS 23 – Datei umbenennen (neuer Name ab FCB+16). @return 0 bei Erfolg, 0xFF bei Fehler. */
    uint8_t bdosRenameFile();
    /** @brief BDOS 33 – Wahlfreier Lesezugriff (Position aus R0/R1). @return 0/1/6. */
    uint8_t bdosReadRand();
    /** @brief BDOS 34/40 – Wahlfreier Schreibzugriff (Position aus R0/R1). @return 0/5/6. */
    uint8_t bdosWriteRand();
    /** @brief BDOS 35 – Dateigroesse berechnen; schreibt Record-Anzahl nach R0/R1/R2. */
    void bdosFileSize();
    /** @brief BDOS 36 – Wahlfreie Position aus sequentiellem FCB-Zustand berechnen. */
    void bdosSetRandRec();

    /// @}
};
