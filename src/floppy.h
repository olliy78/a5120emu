/**
 * @file floppy.h
 * @brief Disketten-Image-Verwaltung fuer den Robotron A5120 Emulator.
 *
 * Diese Datei enthaelt die Klassendefinition fuer FloppyDisk, die das
 * Laden, Speichern und den sektorweisen Zugriff auf Disketten-Images
 * im A5120-Format realisiert.
 *
 * @section disk_format A5120-Diskettenformat
 *
 * Der Robotron A5120 verwendet 5.25"-Disketten im Double-Density-Format
 * (MFM-Kodierung, 250 kbit/s) mit folgenden Parametern:
 *
 * | Parameter              | Wert                             |
 * |------------------------|----------------------------------|
 * | Spuren (physisch)      | 80 (0-79)                        |
 * | Seiten                 | 2 (doppelseitig)                 |
 * | Sektoren pro Spur      | 5 physische (1024 Bytes)         |
 * | Logische Sektoren      | 40 pro Spur (128 Bytes, CP/M)    |
 * | Rohkapazitaet          | 80 * 2 * 5 * 1024 = 819200 Bytes |
 * | System-Spuren          | 2 (fuer CPA/CP/M-Bootloader)     |
 *
 * Das Disketten-Image wird als lineares Byte-Array gespeichert:
 * - Logische Spuren 0-79: Physische Spuren 0-79, Seite 0
 * - Logische Spuren 80-159: Physische Spuren 0-79, Seite 1
 * - Innerhalb jeder Spur: 40 logische Sektoren a 128 Bytes
 *
 * @section interleave Sektor-Interleave
 *
 * Die CPA-Interleave-Tabelle (XLAT26) ordnet logische Sektornummern
 * physischen Positionen zu. Die Verschachtelung stellt sicher, dass
 * aufeinanderfolgende logische Sektoren physisch ueber die Spur verteilt
 * sind, sodass nach dem Lesen eines Sektors genuegend Zeit fuer die
 * Datenverarbeitung bleibt, bevor der naechste logische Sektor unter
 * dem Lesekopf vorbeikommt.
 *
 * @section dir_mount Verzeichnis-basiertes Mounten
 *
 * Die Methode loadFromDirectory() erlaubt es, ein Host-Verzeichnis als
 * virtuelle Diskette zu mounten. Dabei wird ein leeres Disketten-Image
 * erzeugt und mit den Dateien aus dem Verzeichnis befuellt. Das CP/M-
 * Dateisystem (Directory-Eintraege, Allokationsmap) wird dabei korrekt
 * aufgebaut, einschliesslich 16-Bit-Blocknummern (DSM >= 256) und
 * Extent-Verwaltung.
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
#include <cstdint>
#include <string>
#include <vector>

/**
 * @class FloppyDisk
 * @brief Verwaltet ein Disketten-Image im A5120-Format.
 *
 * Unterstuetzt folgende Operationen:
 * - Laden und Speichern von Raw-Disketten-Images (800 KB, 400 KB, SSSD)
 * - Erzeugen leerer, mit E5h gefuellter Images (CP/M-Standard)
 * - Laden eines Host-Verzeichnisses als virtuelles CP/M-Dateisystem
 * - Sektorweises Lesen und Schreiben (128-Byte CP/M-Sektoren)
 * - Umrechnung zwischen logischen und physischen Sektoradressen
 *
 * Das Image wird intern als lineares Byte-Array (std::vector) verwaltet.
 * Die Adressierung erfolgt ueber logische Spurnummern (0-159) und
 * logische Sektornummern (0-39), die direkt in einen Byte-Offset im
 * Array umgerechnet werden.
 */
class FloppyDisk {
public:
    /// @name A5120-Diskettenformat-Konstanten
    /// @{
    static constexpr int TRACKS = 80;              ///< Physische Spuren pro Seite (0-79)
    static constexpr int SIDES = 2;                ///< Anzahl Diskettenseiten (doppelseitig)
    static constexpr int SECTORS_PER_TRACK = 5;    ///< Physische Sektoren pro Spur (1024 Bytes)
    static constexpr int SECTOR_SIZE = 1024;       ///< Physische Sektorgroesse in Bytes
    static constexpr int CPM_SECTOR_SIZE = 128;    ///< Logische CP/M-Sektorgroesse in Bytes
    static constexpr int CPM_SECTORS_PER_TRACK = 40; ///< Logische Sektoren pro Spur (5*1024/128)
    static constexpr int DISK_SIZE = TRACKS * SIDES * SECTORS_PER_TRACK * SECTOR_SIZE; ///< Gesamtgroesse: 819200 Bytes
    static constexpr int SYSTEM_TRACKS = 2;        ///< Reservierte System-Spuren fuer CPA-Bootloader
    /// @}

    /**
     * @brief CPA-Sektor-Interleave-Tabelle (26 Eintraege).
     *
     * Standardmaessige CPA-Verschachtelung:
     * @code
     *   Logisch:   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 ...
     *   Physisch:  1  7 13 19 25  5 11 17 23  3  9 15 21  2  8 14 ...
     * @endcode
     */
    static constexpr int XLAT26[] = {
        1,7,13,19,25,5,11,17,23,3,9,15,21,2,8,14,20,26,6,12,18,24,4,10,16,22
    };

    FloppyDisk();

    /// @name Image-Verwaltung
    /// @{

    /**
     * @brief Laedt ein Disketten-Image aus einer Datei.
     *
     * Erkennt das Format anhand der Dateigroesse:
     * - 819200 Bytes: Standard 800 KB (80 Spuren, 2 Seiten, DD)
     * - 409600 Bytes: Einseitig 400 KB (80 Spuren, 1 Seite)
     * - <= 256256 Bytes: SSSD (77 Spuren, 26 Sektoren, 128 Bytes)
     * - Sonstige: Generisch (128-Byte-Sektoren)
     *
     * @param filename Pfad zur Image-Datei
     * @return true bei Erfolg
     */
    bool loadImage(const std::string& filename);

    /**
     * @brief Speichert das Disketten-Image in eine Datei.
     * @param filename Pfad zur Zieldatei
     * @return true bei Erfolg
     */
    bool saveImage(const std::string& filename) const;

    /**
     * @brief Erzeugt ein leeres, formatiertes Disketten-Image.
     *
     * Fuellt das gesamte Image mit E5h (CP/M-Standard fuer leere/
     * formatierte Sektoren) und speichert es sofort in die angegebene Datei.
     *
     * @param filename Pfad zur Zieldatei
     * @return true bei Erfolg
     */
    bool createBlank(const std::string& filename);

    /**
     * @brief Laedt den Inhalt eines Host-Verzeichnisses als virtuelles CP/M-Laufwerk.
     *
     * Erzeugt ein leeres 800-KB-Image und befuellt es mit den Dateien
     * aus dem angegebenen Verzeichnis. Die CP/M-Dateisystem-Strukturen
     * (Directory-Eintraege, Allokationsmap) werden korrekt erzeugt.
     *
     * CP/M-Dateisystem-Parameter (aus CPA-BIOS DPB):
     * @code
     *   SPT = 40     Sektoren pro Spur
     *   BSH = 4      Block-Shift (2^4 * 128 = 2048 Bytes/Block)
     *   BLM = 0Fh    Block-Maske
     *   EXM = 0      Jeder Directory-Eintrag = 1 Extent = 128 Records = 16 KB
     *   DSM = 399    Gesamtanzahl Bloecke - 1 (=> 16-Bit Blocknummern)
     *   DRM = 191    Max. Verzeichniseintraege - 1 (192 Eintraege)
     *   AL0 = E0h    Directory belegt Bloecke 0, 1, 2
     *   OFF = 0      Keine System-Spuren-Reserve
     * @endcode
     *
     * Dateinamen werden in das CP/M 8.3-Grossbuchstaben-Format konvertiert.
     *
     * @param dirPath Pfad zum Host-Verzeichnis
     * @return true bei Erfolg
     */
    bool loadFromDirectory(const std::string& dirPath);
    /// @}

    /// @name Sektorzugriff (128-Byte logische CP/M-Sektoren)
    /// @{

    /**
     * @brief Liest einen 128-Byte-Sektor vom Disketten-Image.
     *
     * @param track  Logische Spurnummer (0-79 = Seite 0, 80-159 = Seite 1)
     * @param sector Logische Sektornummer (0-39)
     * @param buffer Zielpuffer (mindestens 128 Bytes)
     * @return true bei Erfolg, false bei Fehler oder ausserhalb des Images
     *         (bei Zugriff ausserhalb wird der Puffer mit E5h gefuellt)
     */
    bool readSector(int track, int sector, uint8_t* buffer) const;

    /**
     * @brief Schreibt einen 128-Byte-Sektor in das Disketten-Image.
     *
     * Falls der Zugriff ueber die aktuelle Image-Groesse hinausgeht,
     * wird das Image automatisch vergroessert (mit E5h aufgefuellt).
     *
     * @param track  Logische Spurnummer (0-159)
     * @param sector Logische Sektornummer (0-39)
     * @param buffer Quellpuffer (128 Bytes)
     * @return true bei Erfolg, false wenn schreibgeschuetzt oder nicht geladen
     */
    bool writeSector(int track, int sector, const uint8_t* buffer);
    /// @}

    /**
     * @brief Rechnet logische Sektoradressen in physische um.
     *
     * Fuer das A5120 800-KB-Format:
     * - Logische Spuren 0-79 = Seite 0, Spuren 80-159 = Seite 1
     * - Jede logische Spur hat 40 Sektoren a 128 Bytes
     * - Physisch: 5 Sektoren a 1024 Bytes pro Spur/Seite
     *
     * @param logTrack   Logische Spurnummer (0-159)
     * @param logSector  Logische Sektornummer (0-39)
     * @param[out] physTrack  Physische Spurnummer (0-79)
     * @param[out] physSide   Diskettenseite (0 oder 1)
     * @param[out] physSector Physische Sektornummer (0-4)
     * @param[out] offset     Byte-Offset innerhalb des physischen Sektors (0-895)
     */
    void logicalToPhysical(int logTrack, int logSector,
                           int& physTrack, int& physSide, int& physSector, int& offset) const;

    /// @name Zustandsabfragen
    /// @{
    bool isLoaded() const { return loaded_; }               ///< Prueft ob ein Image geladen ist
    bool isWriteProtected() const { return writeProtected_; } ///< Prueft auf Schreibschutz
    void setWriteProtected(bool wp) { writeProtected_ = wp; } ///< Setzt den Schreibschutz
    bool isModified() const { return modified_; }            ///< Prueft ob das Image veraendert wurde
    /// @}

    /** @brief Direkter Zugriff auf die Rohdaten des Disketten-Images. */
    const std::vector<uint8_t>& data() const { return image_; }

private:
    std::vector<uint8_t> image_;   ///< Rohdaten des Disketten-Images (lineares Byte-Array)
    bool loaded_;                  ///< true wenn ein Image geladen ist
    bool writeProtected_;          ///< true wenn Schreibschutz aktiv
    bool modified_;                ///< true wenn das Image seit dem Laden veraendert wurde
    std::string filename_;         ///< Dateipfad des geladenen Images (fuer saveImage)
    int sectorsPerTrack_;          ///< Logische Sektoren pro Spur (40 fuer A5120)
    int totalTracks_;              ///< Gesamtanzahl logischer Spuren (160 fuer 800 KB DS)
};
