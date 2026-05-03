/**
 * @file floppy.cpp
 * @brief Implementierung der Disketten-Image-Verwaltung fuer den Robotron A5120.
 *
 * Implementiert das Laden, Speichern und den sektorweisen Zugriff auf
 * Disketten-Images. Unterstuetzt das A5120-Standardformat (800 KB, DD, DS)
 * sowie das Mounten von Host-Verzeichnissen als virtuelle CP/M-Laufwerke.
 *
 * @section image_layout Image-Layout im Speicher
 *
 * Das Disketten-Image wird als lineares Byte-Array verwaltet. Die Adressierung
 * erfolgt ueber logische Spur- und Sektornummern:
 *
 * @code
 *   Byte-Offset = logSpur * 40 * 128 + logSektor * 128
 *
 *   logSpur 0-79:    Physische Spuren 0-79, Seite 0
 *   logSpur 80-159:  Physische Spuren 0-79, Seite 1
 *   logSektor 0-39:  40 logische 128-Byte-Sektoren pro Spur
 * @endcode
 *
 * @section dir_fs CP/M-Dateisystem-Aufbau
 *
 * Beim Laden eines Verzeichnisses (loadFromDirectory) wird ein vollstaendiges
 * CP/M-Dateisystem aufgebaut:
 *
 * - **Bloecke 0-2**: Directory (192 Eintraege a 32 Bytes = 6144 Bytes)
 * - **Bloecke 3-399**: Datenbereich (je 2048 Bytes)
 * - **Blocknummern**: 16-Bit (da DSM=399 >= 256), 8 pro Directory-Eintrag
 * - **Extents**: Mit EXM=0 deckt jeder Eintrag 128 Records = 16 KB ab.
 *   Dateien groesser als 16 KB benoetigen mehrere Directory-Eintraege.
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
#include "floppy.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <filesystem>

/// Statische Definition der Interleave-Tabelle (siehe floppy.h).
constexpr int FloppyDisk::XLAT26[];

/**
 * @brief Konstruktor - erzeugt ein leeres, nicht geladenes FloppyDisk-Objekt.
 *
 * Initialisiert die Formatparameter auf das A5120-Standardformat
 * (40 logische Sektoren pro Spur, 160 logische Spuren = 800 KB DS).
 */
FloppyDisk::FloppyDisk()
    : loaded_(false), writeProtected_(false), modified_(false),
      sectorsPerTrack_(CPM_SECTORS_PER_TRACK), totalTracks_(TRACKS * SIDES),
      sylFormat_(false) {
}

/**
 * @brief Laedt ein Disketten-Image aus einer Datei.
 *
 * Die Datei wird komplett in den Speicher gelesen. Das Format wird
 * anhand der Dateigroesse bestimmt:
 *
 * | Groesse     | Format                                     | Spuren | Sektoren |
 * |-------------|--------------------------------------------|---------|-----------|
 * | 819200      | A5120 Standard (DD, DS, 80 Spuren)         | 160    | 40       |
 * | 409600      | Einseitig (DD, SS, 80 Spuren)              | 80     | 40       |
 * | <= 256256   | SSSD (z.B. 77 Spuren, 26 Sektoren, 128 B)  | auto   | 26       |
 * | sonstige    | Generisch (128-Byte-Sektoren)              | auto   | 40       |
 *
 * @param filename Pfad zur Image-Datei
 * @return true bei Erfolg, false wenn die Datei nicht geoeffnet werden kann
 */
bool FloppyDisk::loadImage(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    image_.resize(fileSize);
    file.read(reinterpret_cast<char*>(image_.data()), fileSize);

    if (!file.good() && !file.eof()) return false;

    // Determine format based on file size
    if (fileSize == 819200) {
        // Standard 800KB: 80 tracks * 2 sides * 5 sectors * 1024 bytes
        sectorsPerTrack_ = CPM_SECTORS_PER_TRACK; // 40 logical
        totalTracks_ = TRACKS * SIDES;
    } else if (fileSize == 409600) {
        // Single sided 400KB
        sectorsPerTrack_ = CPM_SECTORS_PER_TRACK;
        totalTracks_ = TRACKS;
    } else if (fileSize <= 256256) {
        // SSSD: 77 tracks * 26 * 128 or similar
        sectorsPerTrack_ = 26;
        totalTracks_ = fileSize / (26 * 128);
    } else {
        // Generic: assume 128-byte sectors
        sectorsPerTrack_ = CPM_SECTORS_PER_TRACK;
        totalTracks_ = fileSize / (CPM_SECTORS_PER_TRACK * CPM_SECTOR_SIZE);
        if (totalTracks_ == 0) totalTracks_ = 1;
    }

    filename_ = filename;
    loaded_ = true;
    modified_ = false;

    // Detect CPA780 SYL format: mixed track geometry
    // System tracks (0-2): 26 × 128 bytes each (3328 bytes)
    // Data   tracks (3+):   5 × 1024 bytes each (5120 bytes)
    sylFormat_ = (image_.size() >= 3 &&
                  image_[0] == 0x53 && image_[1] == 0x59 && image_[2] == 0x4C);
    if (sylFormat_) {
        sectorsPerTrack_ = CPM_SECTORS_PER_TRACK;  // 40 logical sectors for data tracks
        size_t dataBytes  = image_.size() - (size_t)SYL_SYS_TRACKS * SYL_SYS_TRACK_SIZE;
        int    dataTracks = static_cast<int>(dataBytes / SYL_DATA_TRACK_SIZE);
        totalTracks_ = SYL_SYS_TRACKS + dataTracks;
    }

    return true;
}

/**
 * @brief Speichert das Disketten-Image in eine Datei.
 *
 * Schreibt das gesamte interne Byte-Array (image_) binaer in die Datei.
 *
 * @param filename Pfad zur Zieldatei
 * @return true bei Erfolg
 */
bool FloppyDisk::saveImage(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(image_.data()), image_.size());
    return file.good();
}

/**
 * @brief Erzeugt ein leeres, formatiertes 800-KB-Disketten-Image.
 *
 * Das gesamte Image wird mit E5h gefuellt, dem CP/M-Standardwert
 * fuer leere/formatierte Sektoren. Das Image wird sofort auf
 * die Festplatte geschrieben.
 *
 * @param filename Pfad zur Zieldatei
 * @return true bei Erfolg
 */
bool FloppyDisk::createBlank(const std::string& filename) {
    image_.resize(DISK_SIZE, 0xE5); // E5 = CP/M empty marker
    filename_ = filename;
    loaded_ = true;
    modified_ = true;
    return saveImage(filename);
}

/**
 * @brief Laedt ein Host-Verzeichnis als virtuelles CP/M-Laufwerk.
 *
 * Erzeugt ein leeres 800-KB-Image (gefuellt mit E5h) und baut ein
 * vollstaendiges CP/M-Dateisystem darin auf. Alle regulaeren Dateien
 * im angegebenen Verzeichnis werden eingelesen und in das Image
 * geschrieben.
 *
 * **Ablauf:**
 * 1. Leeres Image erzeugen (819200 Bytes, E5h-gefuellt)
 * 2. Dateien aus dem Verzeichnis einlesen
 * 3. Fuer jede Datei:
 *    a. Dateiname in CP/M 8.3-Grossbuchstaben-Format konvertieren
 *    b. Dateiinhalt in Bloecke schreiben (ab Block 3, nach dem Directory)
 *    c. Directory-Eintraege erzeugen (32 Bytes pro Eintrag)
 * 4. Directory in die Bloecke 0-2 schreiben
 *
 * **Directory-Eintrag-Format** (32 Bytes):
 * @code
 *   Byte  0:      User-Nummer (0)
 *   Byte  1-8:    Dateiname (8 Zeichen, Leerzeichen-aufgefuellt)
 *   Byte  9-11:   Erweiterung (3 Zeichen, Leerzeichen-aufgefuellt)
 *   Byte  12:     EX - Extent-Nummer (Low 5 Bits)
 *   Byte  13:     S1 - reserviert (0)
 *   Byte  14:     S2 - Extent-Nummer (High 6 Bits)
 *   Byte  15:     RC - Record-Count (0-128 Records in diesem Extent)
 *   Byte  16-31:  Allokationsmap (8x 16-Bit Blocknummern, Little-Endian)
 * @endcode
 *
 * Bei Dateien groesser als 16 KB (128 Records * 128 Bytes) werden
 * mehrere Directory-Eintraege mit aufsteigenden Extent-Nummern angelegt.
 * Jeder Eintrag kann maximal 8 Bloecke (= 16 KB) referenzieren.
 *
 * @param dirPath Pfad zum Host-Verzeichnis
 * @return true bei Erfolg, false wenn Verzeichnis nicht existiert
 */
bool FloppyDisk::loadFromDirectory(const std::string& dirPath) {
    // Create a blank disk image and populate it with files from the directory
    image_.resize(DISK_SIZE, 0xE5);
    loaded_ = true;
    modified_ = true;
    sectorsPerTrack_ = CPM_SECTORS_PER_TRACK;
    totalTracks_ = TRACKS * SIDES;

    namespace fs = std::filesystem;
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return false;

    // DPB parameters matching the original CPA BIOS (from bios.prn DPBA at D06F):
    //   SPT=40, BSH=4, BLM=0F, EXM=0, DSM=399, DRM=191
    //   AL0=E0, AL1=00, CKS=48, OFF=0
    // With DSM=399 >= 256: 16-bit block numbers, 8 per directory entry
    // With EXM=0: each directory entry covers 1 extent = 128 records = 16KB
    // Block size = 2KB (BSH=4), directory uses first 3 blocks (AL0=E0)
    constexpr int blockSize = 2048;
    constexpr int maxDirEntries = 192;     // DRM=191
    constexpr int dirBlocks = 3;           // AL0=0xE0 -> blocks 0,1,2
    constexpr int blocksPerEntry = 8;      // 16-bit block numbers -> 8 per entry
    constexpr int systemTrackOffset = 0;   // OFF=0 (no system tracks)
    constexpr int recsPerExtent = 128;     // EXM=0: 1 extent = 128 records

    int dataStartOffset = systemTrackOffset * sectorsPerTrack_ * CPM_SECTOR_SIZE;

    // Read files from directory
    struct FileEntry {
        std::string name;
        std::vector<uint8_t> content;
    };
    std::vector<FileEntry> files;

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file) continue;
        FileEntry fe;
        fe.name = entry.path().filename().string();
        fe.content.assign(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        files.push_back(std::move(fe));
    }

    // Allocate blocks to files and build directory entries
    uint8_t directory[maxDirEntries * 32];
    memset(directory, 0xE5, sizeof(directory));
    int dirEntry = 0;
    int currentBlock = dirBlocks; // First data block after directory

    for (const auto& fe : files) {
        if (dirEntry >= maxDirEntries) break;

        // Parse filename to 8.3 uppercase format
        std::string fname = fe.name;
        std::string base, ext;
        auto dot = fname.rfind('.');
        if (dot != std::string::npos) {
            base = fname.substr(0, dot);
            ext = fname.substr(dot + 1);
        } else {
            base = fname;
        }
        for (auto& c : base) c = toupper(c);
        for (auto& c : ext) c = toupper(c);
        while (base.size() < 8) base += ' ';
        while (ext.size() < 3) ext += ' ';
        base = base.substr(0, 8);
        ext = ext.substr(0, 3);

        int totalRecords = (fe.content.size() + CPM_SECTOR_SIZE - 1) / CPM_SECTOR_SIZE;
        if (totalRecords == 0) totalRecords = 1; // Minimum 1 record for empty files
        int blocksNeeded = (fe.content.size() + blockSize - 1) / blockSize;
        if (blocksNeeded == 0) blocksNeeded = 1;

        // Write file data to image
        for (int b = 0; b < blocksNeeded; b++) {
            int block = currentBlock + b;
            int imgOffset = dataStartOffset + block * blockSize;
            int srcOffset = b * blockSize;
            int bytesToWrite = std::min((int)fe.content.size() - srcOffset, blockSize);
            if (bytesToWrite > 0 && imgOffset + bytesToWrite <= (int)image_.size()) {
                memcpy(&image_[imgOffset], &fe.content[srcOffset], bytesToWrite);
            }
        }

        // Create directory entries for this file
        // Each entry covers up to 8 blocks (16-bit pointers, DSM >= 256)
        // With EXM=0: each entry covers 1 extent = 128 records = 16KB
        int blocksAllocated = 0;
        int recordsAllocated = 0;

        while (blocksAllocated < blocksNeeded && dirEntry < maxDirEntries) {
            uint8_t* dir = &directory[dirEntry * 32];
            int blocksThisEntry = std::min(blocksPerEntry, blocksNeeded - blocksAllocated);
            int recsThisEntry = std::min(totalRecords - recordsAllocated, recsPerExtent);

            dir[0] = 0;  // User 0
            memcpy(&dir[1], base.c_str(), 8);
            memcpy(&dir[9], ext.c_str(), 3);

            // Extent number: with EXM=0, each dir entry = 1 extent
            int extentNum = recordsAllocated / recsPerExtent;
            dir[12] = extentNum & 0x1F;  // EX (low 5 bits of extent)
            dir[13] = 0;                 // S1
            dir[14] = (extentNum >> 5) & 0x3F;  // S2 (high bits of extent)

            // RC: record count in this extent
            if (blocksAllocated + blocksThisEntry < blocksNeeded) {
                // Not the last entry - full extent
                dir[15] = 128;
            } else {
                // Last entry - remaining records
                int rem = (totalRecords - recordsAllocated);
                dir[15] = (rem > 128) ? 128 : rem;
            }

            // Allocation map: 16-bit block numbers (8 entries, DSM >= 256)
            for (int i = 0; i < blocksThisEntry; i++) {
                int blk = currentBlock + blocksAllocated + i;
                dir[16 + i * 2] = blk & 0xFF;
                dir[16 + i * 2 + 1] = (blk >> 8) & 0xFF;
            }
            // Remaining entries stay 0 (no block allocated)
            for (int i = blocksThisEntry; i < blocksPerEntry; i++) {
                dir[16 + i * 2] = 0;
                dir[16 + i * 2 + 1] = 0;
            }

            blocksAllocated += blocksThisEntry;
            recordsAllocated += recsThisEntry;
            dirEntry++;
        }

        currentBlock += blocksNeeded;
    }

    // Write directory to disk (blocks 0, 1, 2, right at start since OFF=0)
    int dirSize = std::min((int)sizeof(directory), dirBlocks * blockSize);
    memcpy(&image_[dataStartOffset], directory, dirSize);

    return true;
}

/**
 * @brief Rechnet logische Sektoradressen in physische um.
 *
 * Das A5120-Format verwendet 5 physische Sektoren a 1024 Bytes pro Spur,
 * waehrend CP/M mit 40 logischen Sektoren a 128 Bytes arbeitet.
 * Diese Funktion berechnet den physischen Sektor und den Byte-Offset
 * innerhalb des physischen Sektors.
 *
 * Fuer doppelseitige Disketten (totalTracks_ > 80):
 * - physSide = logTrack / 80 (0 oder 1)
 * - physTrack = logTrack % 80 (0-79)
 *
 * Fuer einseitige Disketten:
 * - physSide = 0
 * - physTrack = logTrack (0-79)
 *
 * Physischer Sektor und Offset:
 * @code
 *   byteOffset = logSector * 128
 *   physSector = byteOffset / 1024   (0-4)
 *   offset     = byteOffset % 1024   (0-896 in 128er-Schritten)
 * @endcode
 */
void FloppyDisk::logicalToPhysical(int logTrack, int logSector,
                                    int& physTrack, int& physSide,
                                    int& physSector, int& offset) const {
    // For A5120 800KB format:
    // Logical tracks 0-79 = side 0, tracks 80-159 = side 1
    // Each logical track has 40 sectors of 128 bytes
    // Physical: 5 sectors of 1024 bytes per track/side

    if (totalTracks_ > TRACKS) {
        // Double-sided
        physSide = logTrack / TRACKS;
        physTrack = logTrack % TRACKS;
    } else {
        physSide = 0;
        physTrack = logTrack;
    }

    // Map logical sector (0-39) to physical sector + offset
    int byteOffset = logSector * CPM_SECTOR_SIZE;
    physSector = byteOffset / SECTOR_SIZE;
    offset = byteOffset % SECTOR_SIZE;
}

/**
 * @brief Liest einen 128-Byte-Sektor vom Disketten-Image.
 *
 * Berechnet den Byte-Offset im internen Image-Array:
 * @code
 *   offset = track * 40 * 128 + sector * 128
 * @endcode
 *
 * Falls der berechnete Offset ueber die Image-Groesse hinausgeht,
 * wird der Zielpuffer mit E5h gefuellt (leerer/formatierter Sektor)
 * und true zurueckgegeben — dies tritt auf, wenn nicht alle Spuren
 * im Image vorhanden sind.
 *
 * @return true bei Erfolg, false bei ungueltigen Parametern oder
 *         nicht geladenem Image
 */
bool FloppyDisk::readSector(int track, int sector, uint8_t* buffer) const {
    if (!loaded_) return false;
    if (track < 0 || track >= totalTracks_) return false;

    size_t imageOffset;
    if (sylFormat_) {
        if (track < SYL_SYS_TRACKS) {
            // Systemspur: 26 Sektoren × 128 Bytes
            if (sector < 0 || sector >= SYL_SYS_TRACK_SECTORS) return false;
            imageOffset = (size_t)track * SYL_SYS_TRACK_SIZE
                        + (size_t)sector * CPM_SECTOR_SIZE;
        } else {
            // Datenspur: 40 logische Sektoren × 128 Bytes (= 5 × 1024 Bytes)
            if (sector < 0 || sector >= sectorsPerTrack_) return false;
            imageOffset = (size_t)SYL_SYS_TRACKS * SYL_SYS_TRACK_SIZE
                        + (size_t)(track - SYL_SYS_TRACKS) * SYL_DATA_TRACK_SIZE
                        + (size_t)sector * CPM_SECTOR_SIZE;
        }
    } else {
        if (sector < 0 || sector >= sectorsPerTrack_) return false;
        imageOffset = (size_t)track * sectorsPerTrack_ * CPM_SECTOR_SIZE
                   + (size_t)sector * CPM_SECTOR_SIZE;
    }

    if (imageOffset + CPM_SECTOR_SIZE > image_.size()) {
        memset(buffer, 0xE5, CPM_SECTOR_SIZE);
        return true;
    }

    memcpy(buffer, &image_[imageOffset], CPM_SECTOR_SIZE);
    return true;
}

/**
 * @brief Schreibt einen 128-Byte-Sektor in das Disketten-Image.
 *
 * Falls der berechnete Offset ueber die aktuelle Image-Groesse
 * hinausgeht, wird das Image automatisch vergroessert und der
 * neue Bereich mit E5h gefuellt.
 *
 * Setzt modified_ = true, damit der Aufrufer erkennen kann,
 * dass das Image gespeichert werden sollte.
 *
 * @return true bei Erfolg, false wenn schreibgeschuetzt, nicht
 *         geladen, oder Spur/Sektor ungueltig
 */
bool FloppyDisk::writeSector(int track, int sector, const uint8_t* buffer) {
    if (!loaded_ || writeProtected_) return false;
    if (track < 0 || track >= totalTracks_) return false;

    size_t imageOffset;
    if (sylFormat_) {
        if (track < SYL_SYS_TRACKS) {
            if (sector < 0 || sector >= SYL_SYS_TRACK_SECTORS) return false;
            imageOffset = (size_t)track * SYL_SYS_TRACK_SIZE
                        + (size_t)sector * CPM_SECTOR_SIZE;
        } else {
            if (sector < 0 || sector >= sectorsPerTrack_) return false;
            imageOffset = (size_t)SYL_SYS_TRACKS * SYL_SYS_TRACK_SIZE
                        + (size_t)(track - SYL_SYS_TRACKS) * SYL_DATA_TRACK_SIZE
                        + (size_t)sector * CPM_SECTOR_SIZE;
        }
    } else {
        if (sector < 0 || sector >= sectorsPerTrack_) return false;
        imageOffset = (size_t)track * sectorsPerTrack_ * CPM_SECTOR_SIZE
                   + (size_t)sector * CPM_SECTOR_SIZE;
    }

    if (imageOffset + CPM_SECTOR_SIZE > image_.size()) {
        image_.resize(imageOffset + CPM_SECTOR_SIZE, 0xE5);
    }

    memcpy(&image_[imageOffset], buffer, CPM_SECTOR_SIZE);
    modified_ = true;
    return true;
}

/**
 * @brief Prueft ob Sektor 0 das SYL-Systemlader-Kennzeichen traegt.
 */
bool FloppyDisk::isSylDisk() const {
    return sylFormat_;
}

/**
 * @brief Liest eine benannte Datei aus dem CP/M-Dateisystem der Diskette.
 *
 * CPA-DPB: SPT=40, BSH=4 (16 Sektoren/Block = 2048 Bytes),
 * DSM=399 (>= 256 → 16-Bit Blocknummern), DRM=191, AL0=E0h.
 */
std::vector<uint8_t> FloppyDisk::readCpmFile(const std::string& name8,
                                              const std::string& ext3,
                                              int off) const {
    if (!loaded_) return {};

    const int spt       = 40;         // Sektoren pro Spur
    const int bsh       = 4;          // Block-Shift (2^4 = 16 Sektoren/Block)
    const int blkSec    = 1 << bsh;   // 16 Sektoren pro Block
    const bool big      = true;        // DSM = 399 >= 256 => 16-Bit Blocknummern
    const int alPerExt  = big ? 8 : 16; // Eintraege im AL-Feld pro Extent

    // Verzeichnis: Bloecke 0,1,2 (AL0=E0h → Bits 7,6,5 → Bloecke 0,1,2)
    // Startsektor: off * spt
    const int dirStartSec = off * spt;

    // Hilfsfunktion: Sektor (linear) → Spur + Sektornummer
    auto readLinSector = [&](int linSec, uint8_t* buf) -> bool {
        int track  = linSec / spt;
        int sector = linSec % spt;
        return readSector(track, sector, buf);
    };

    // Alle Directory-Eintraege (Bloecke 0-2 = 48 Sektoren = 192 Eintraege)
    // Extents nach Extent-Nummer sammeln
    struct Extent {
        int  extNum;
        int  rc;        // Record Count
        int  al[8];     // Block numbers (max 8 für big-disc)
    };
    std::vector<Extent> extents;

    uint8_t sec[128];
    for (int s = 0; s < 3 * blkSec; s++) {
        if (!readLinSector(dirStartSec + s, sec)) continue;

        for (int e = 0; e < 4; e++) {
            const uint8_t* de = sec + e * 32;
            if (de[0] != 0x00) continue;  // Nur User 0, kein gelöschter Eintrag

            // Dateiname/Extension vergleichen (Attributbits ignorieren, Bit 0-6)
            bool match = true;
            for (int i = 0; i < 8 && match; i++)
                if ((de[1+i] & 0x7F) != (uint8_t)name8[i]) match = false;
            for (int i = 0; i < 3 && match; i++)
                if ((de[9+i] & 0x7F) != (uint8_t)ext3[i]) match = false;
            if (!match) continue;

            Extent ex;
            ex.extNum = de[12];  // Xl (low extent byte)
            ex.rc     = de[15];  // Record count in this extent
            for (int i = 0; i < alPerExt; i++) {
                if (big) {
                    ex.al[i] = de[16 + i*2] | (de[17 + i*2] << 8);
                } else {
                    ex.al[i] = de[16 + i];
                }
            }
            extents.push_back(ex);
        }
    }

    if (extents.empty()) return {};

    // Extents nach Extent-Nummer sortieren
    std::sort(extents.begin(), extents.end(),
              [](const Extent& a, const Extent& b){ return a.extNum < b.extNum; });

    // Dateidaten aus Blöcken lesen
    std::vector<uint8_t> fileData;
    for (const auto& ex : extents) {
        int blocksLeft = alPerExt;
        // Letzter Extent: rc gibt Anzahl 128-Byte-Records → ggf. kürzen
        bool lastExtent = (&ex == &extents.back());
        int  recordsLeft = lastExtent ? ex.rc : 128; // 128 Records = voller Extent (16KB)

        for (int ai = 0; ai < blocksLeft && recordsLeft > 0; ai++) {
            int blkNum = ex.al[ai];
            if (blkNum == 0) continue;  // Not allocated (sparse)

            // Erster logischer Sektor dieses Blocks
            int startSec = dirStartSec + blkNum * blkSec;

            for (int bs = 0; bs < blkSec && recordsLeft > 0; bs++, recordsLeft--) {
                if (!readLinSector(startSec + bs, sec)) {
                    memset(sec, 0x1A, 128); // EOF marker falls Lesefehler
                }
                fileData.insert(fileData.end(), sec, sec + 128);
            }
        }
    }

    return fileData;
}
