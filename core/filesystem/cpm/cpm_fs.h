/**
 * @file cpm_fs.h
 * @brief CP/M-2.2-Dateisystem (CP/A, SCPX) — auflisten und lesen.
 *
 * Deckt die Teilmenge von `cpmtools` ab, die das k1520DiskTool braucht — bewusst OHNE
 * Passwoerter, Datentraegeretikett, CP/M-3-Zeitstempel und `libdsk`.
 *
 * ### Modell
 * Ab @ref FsProfile::data_cyl / data_head zerfaellt der Sektorraum in Bloecke à
 * `block_size`.  Die ersten `dir_entries × 32 / block_size` Bloecke sind das Verzeichnis;
 * ein Eintrag ist 32 Byte:
 * @code
 *   US NAME(8) TYP(3) EX S1 S2 RC  AL(16)
 *   │  │       │      │  │  │  │   └─ Blocknummern: 16×8 Bit, oder 8×16 Bit ab 256 Bloecken
 *   │  │       │      │  │  │  └──── Saetze im letzten Extent (à 128 B)
 *   │  │       │      └──┴───────── Extent-Nummer (EX + S2×32)
 *   │  │       └── Hochbits von TYP[0..2] = R/O, SYS, ARCHIV
 *   └── Nutzerbereich 0…15, 0xE5 = frei
 * @endcode
 *
 * ### Sektorversatz (skew)
 * Die Uebersetzung logischer → physischer Sektor passiert **hier**, nicht im
 * @ref SectorSpace: nur das Dateisystem kennt den Versatz.  Alle heute bekannten
 * CP/A-/SCPX-Formate benutzen `skew: 0`; die Tabelle existiert trotzdem, damit
 * Fremdformate nachruestbar sind.
 *
 * @see doc/design/13_k1520disktool.md §7
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/filesystem/file_system.h"
#include "core/filesystem/fs_profile.h"
#include "core/filesystem/sector_space.h"

#include <memory>

/**
 * @struct CpmDirEntry
 * @brief Ein roher 32-Byte-Verzeichniseintrag, bereits zerlegt.
 */
struct CpmDirEntry {
    int      index    = 0;      ///< Position im Verzeichnis (0-basiert)
    uint8_t  user     = 0;      ///< 0xE5 = frei
    std::string name;           ///< "NAME.TYP" (ohne Attributbits)
    int      extent   = 0;      ///< EX + S2×32
    uint8_t  records  = 0;      ///< RC
    bool     read_only= false;
    bool     system   = false;
    bool     archived = false;
    std::vector<uint16_t> blocks;   ///< AL, 0 = nicht belegt

    bool free() const { return user == 0xE5; }
};

/**
 * @class CpmFileSystem
 * @brief CP/M-Volume ueber einem @ref SectorSpace.
 */
class CpmFileSystem : public FileSystem {
public:
    /**
     * @brief Volume aufsetzen und die Geometrie pruefen.
     * @param space  Sektorraum der ganzen Diskette (CP/M nutzt beide Seiten)
     * @param prof   Dateisystemprofil (`type: cpm`)
     * @param err    Grund, wenn nullptr zurueckkommt
     */
    static std::unique_ptr<CpmFileSystem> mount(SectorSpace& space, const FsProfile& prof,
                                                std::string& err);

    std::vector<FileEntry> list() const override;
    bool   read (const std::string& name, std::vector<uint8_t>& out) override;
    bool   write(const std::string& name, const std::vector<uint8_t>& data,
                 const WriteOptions& opt) override;
    bool   erase(const std::string& name) override;
    bool   wouldFit(const std::vector<PlannedFile>& files, FitReport& out) const override;
    bool   mkfs() override;
    FsInfo info() const override;

    /// @brief Ist @p name ein gueltiger CP/M-Name (8.3, Grossschrift, ohne Sonderzeichen)?
    ///        Liefert bei false den Grund in @p why.
    static bool validName(const std::string& name, std::string* why);
    /// @brief Linux-Dateiname → CP/M-Name (Grossschrift, auf 8.3 gekuerzt).
    static std::string toCpmName(const std::string& linux_name);

    // ─── Innenansicht (Diagnose, Tests) ──────────────────────────────────────

    /// @brief Alle Verzeichnisplaetze, auch freie und fortgesetzte Extents.
    std::vector<CpmDirEntry> directory() const;

    uint32_t blockSize()      const { return prof_.block_size; }
    uint32_t totalBlocks()    const { return total_blocks_; }
    uint32_t directoryBlocks()const { return dir_blocks_; }
    bool     wideBlockPtr()   const { return wide_ptr_; }
    uint64_t dataStart()      const { return data_start_; }

    /// @brief Belegungskarte aus dem Verzeichnis rekonstruiert (true = belegt).
    ///        CP/M speichert sie nirgends — sie wird bei jedem Mounten neu gebaut.
    std::vector<bool> allocationMap() const;

private:
    CpmFileSystem(SectorSpace& space, const FsProfile& prof);

    /// @brief Byte-Offset im Datenbereich → Sektor lesen (mit Versatz).
    bool readAt(uint64_t offset, uint8_t* dst, size_t n) const;
    /// @brief Byte-Offset im Datenbereich → Sektor schreiben (mit Versatz).
    bool writeAt(uint64_t offset, const uint8_t* src, size_t n);
    /// @brief Einen 32-B-Verzeichnisplatz zurueckschreiben.
    bool writeDirEntry(int slot, const uint8_t* raw32);
    /// @brief Einen Block in @p out anhaengen.
    bool readBlock(uint16_t block, std::vector<uint8_t>& out) const;
    /// @brief Verzeichnisbytes (dir_entries × 32).
    bool readDirectory(std::vector<uint8_t>& out) const;

    /// @brief Versatztabelle: logischer → physischer Sektorindex.
    void buildSkewTable();

    SectorSpace& space_;
    FsProfile    prof_;

    uint64_t data_start_   = 0;    ///< Byte-Offset des Datenbereichs im Sektorraum
    uint64_t data_bytes_   = 0;
    uint32_t total_blocks_ = 0;
    uint32_t dir_blocks_   = 0;
    bool     wide_ptr_     = false;///< 16-Bit-Blockzeiger (ab 256 Bloecken)
    uint8_t  ptrs_per_entry_ = 16; ///< AL-Eintraege je Verzeichnisplatz (16 oder 8)
    uint32_t ext_per_entry_  = 1;  ///< logische Extents je Verzeichnisplatz

    // Datenbereich als Spurfolge (fuer den Sektorversatz).
    uint16_t secs_per_track_ = 0;
    uint16_t sector_size_    = 0;
    std::vector<uint8_t> skew_tab_;   ///< logischer Index → physischer Index
};
