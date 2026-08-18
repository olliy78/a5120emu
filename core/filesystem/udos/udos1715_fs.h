/**
 * @file udos1715_fs.h
 * @brief UDOS1715 / **NDOS** — das Dateisystem des PC 1715.
 *
 * Vollstaendig spezifiziert in **`doc/udos1715_diskettenformat.md`**, gemessen an einer
 * echten Systemdiskette und belegt durch das Handbuch, das auf dieser Diskette selbst
 * liegt (`doc/original_docs/UDOS1715_Systemhandbuch.txt`).
 *
 * Es ist dieselbe Betriebssystemfamilie wie @ref UdosFileSystem (ZDOS auf dem A5120) —
 * Verzeichnisaufbau, Descriptorfelder, Typ- und Eigenschaftsbytes sind bitgleich.  Der
 * Unterschied kommt vom **µPD765**-Controller des PC 1715: der kann nur ganze
 * IBM-Sektoren lesen und schreiben und erreicht die vier Bytes hinter der Daten-CRC
 * nicht, in denen ZDOS seine Verkettung fuehrt.  Daraus folgen die drei Eigenheiten,
 * die diese Umsetzung praegen:
 *
 * 1. **Zeigersektoren statt Kontrollbloecke** (§6).  Zu jeder Datei gehoeren ein oder
 *    mehrere 256-B-Sektoren, die nichts als die Adressen ihrer Datenrecords enthalten
 *    (125 Stueck je Sektor, untereinander verkettet).  Der Descriptor zeigt bei Offset
 *    `80H` auf den ersten (`FIRSTBL`).
 * 2. **Die Spur ist der ganze Zylinder** (§1.1).  BFOS fasst die 16 Sektoren beider
 *    Seiten zu einer Spur von 32 Sektoren zusammen:
 *    `UDOS-Sektor = (ID − 1) + Kopf · 16`.  Eine UDOS1715-Diskette ist deshalb **ein**
 *    Datentraeger und nicht — wie bei ZDOS — zwei getrennte Seiten.
 * 3. **`.img` ist moeglich** (§8).  Es steht nichts ausserhalb der Sektordatenfelder.
 *
 * @see doc/udos1715_diskettenformat.md
 * @see doc/udos_diskettenformat.md (ZDOS — die gemeinsame Herkunft)
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/filesystem/file_system.h"
#include "core/filesystem/fs_profile.h"
#include "core/filesystem/sector_space.h"
#include "core/filesystem/udos/udos_bitmap.h"
#include "core/filesystem/udos/udos_fs.h"   // UdosPointer, UdosFileHeader, UdosDirEntry

#include <memory>
#include <string>
#include <vector>

/// @brief Sektorgroesse von UDOS1715 — 100H Byte, festgelegt vom Dateisystem (§1).
inline constexpr size_t kUdos1715Sector = 256;
/// @brief Adressen je Zeigersektor: 250 Byte / 2 (§6).
inline constexpr size_t kUdos1715PointersPerBlock = 125;

/**
 * @struct Udos1715PointerBlock
 * @brief Ein Zeigersektor: Adressliste + Verkettung (§6).
 */
struct Udos1715PointerBlock {
    std::vector<UdosPointer> entries;   ///< bis zu 125; im ERSTEN Block ist [0] der Descriptor
    UdosPointer              back;      ///< BCKZGR (FCH) — voriger Block bzw. der Descriptor
    UdosPointer              forward;   ///< FORZGR (FEH) — naechster Block, FFFF = letzter
};

/**
 * @class Udos1715FileSystem
 * @brief Eine UDOS1715-Diskette (beide Seiten zusammen = ein Datentraeger).
 */
class Udos1715FileSystem : public FileSystem {
public:
    /**
     * @param space Sektorraum ueber die GANZE Diskette (@ref SectorSpace::kAllHeads) —
     *              die Spur umfasst beide Seiten
     * @param prof  Dateisystemprofil (`type: udos1715`)
     * @param err   Grund, wenn nullptr zurueckkommt
     */
    static std::unique_ptr<Udos1715FileSystem> mount(SectorSpace& space,
                                                     const FsProfile& prof,
                                                     std::string& err);

    /// @brief **Neues** Dateisystem auf einer formatierten Leerdiskette anlegen.
    static std::unique_ptr<Udos1715FileSystem> format(SectorSpace& space,
                                                      const FsProfile& prof,
                                                      const std::string& label,
                                                      std::string& err);

    std::vector<FileEntry> list() const override;
    std::vector<FileEntry> listNames() const override;
    bool detailsReady(const FileEntry& e) const override;
    bool loadDetails(FileEntry& e) const override;
    bool   read (const std::string& name, std::vector<uint8_t>& out) override;
    bool   write(const std::string& name, const std::vector<uint8_t>& data,
                 const WriteOptions& opt) override;
    bool   erase(const std::string& name) override;
    using  FileSystem::setAttributes;
    bool   setAttributes(const std::string& name, const UdosAttrs& a) override;
    bool   wouldFit(const std::vector<PlannedFile>& files, FitReport& out) const override;
    bool   mkfs() override;
    FsInfo info() const override;

    // ─── Innenansicht (Diagnose, Tests) ──────────────────────────────────────

    const UdosBitmap& bitmap() const { return bitmap_; }
    /// @brief Descriptor der Verzeichnisdatei — Spur 16H Sektor 00, der einzige feste
    ///        Einstiegspunkt (§2).
    UdosPointer directoryDescriptor() const { return UdosPointer{0, prof_.directory_track}; }
    std::vector<UdosDirEntry> directory() const;
    bool readDescriptor(UdosPointer p, UdosFileHeader& out) const;
    /// @brief Die Zeigersektorkette einer Datei, roh.
    bool pointerBlocks(const UdosFileHeader& hdr, std::vector<UdosPointer>& adressen,
                       std::vector<Udos1715PointerBlock>& bloecke) const;
    /// @brief Nur die Datenrecordadressen (ohne den Descriptor an Stelle 0).
    bool recordChain(const UdosFileHeader& hdr, std::vector<UdosPointer>& out) const;

    /// @brief Positivprobe der Erkennung: sieht das nach UDOS1715 aus?
    static bool looksLikeUdos1715(const SectorSpace& space, const FsProfile& prof,
                                  std::string* why);

    /// @brief Gueltiger Name?  Bis 32 Zeichen, **muss mit einem Buchstaben beginnen**
    ///        (Handbuch §3.1).
    static bool validName(const std::string& name, std::string* why);

    /// @brief Spuren, die ein Werkzeug nie beschreiben darf: 16H und 17H — und auf
    ///        einer Systemdiskette Spur 0 (§7.5).
    bool reservedTrack(uint8_t track) const;

    /// @brief Sektoren je Spur, wie das Dateisystem zaehlt (32 beidseitig, 16 einseitig).
    uint8_t sectorsPerTrack() const { return secs_per_track_; }
    uint8_t trackCount()      const { return tracks_; }

private:
    Udos1715FileSystem(SectorSpace& space, const FsProfile& prof);

    // ─── Adressumrechnung (§1.1) — die eine Stelle, durch die alles geht ─────
    uint8_t headOf(UdosPointer p) const {
        return static_cast<uint8_t>(p.sector_index >= phys_spt_ ? 1 : 0);
    }
    uint8_t idOf(UdosPointer p) const {
        return static_cast<uint8_t>(p.sector_index % phys_spt_ + 1);
    }
    bool inRange(UdosPointer p) const {
        return p.track < tracks_ && p.sector_index < secs_per_track_;
    }

    bool readSector (UdosPointer p, std::vector<uint8_t>& data) const;
    bool writeSector(UdosPointer p, const std::vector<uint8_t>& data);

    /// @brief Zeigersektor lesen bzw. schreiben (§6).
    bool readPointerBlock (UdosPointer p, Udos1715PointerBlock& out) const;
    bool writePointerBlock(UdosPointer p, const Udos1715PointerBlock& b);

    /// @brief Inhalt einer Datei ab ihrem Descriptor zusammensetzen.
    bool readChain(const UdosFileHeader& hdr, std::vector<uint8_t>& out) const;

    /// @brief Sektoren je Record (mindestens 1; Recordlaenge 80H nutzt den halben).
    static uint32_t sectorsPerRecord(uint16_t record_len) {
        return record_len <= kUdos1715Sector
             ? 1u : static_cast<uint32_t>(record_len / kUdos1715Sector);
    }

    // ─── Belegung ────────────────────────────────────────────────────────────
    bool allocSectors(uint32_t n, std::vector<UdosPointer>& out);
    bool allocRecords(uint32_t records, uint32_t sec_je_record,
                      std::vector<UdosPointer>& out);
    void freeSectors(const std::vector<UdosPointer>& v);
    bool saveBitmap();

    /// @brief Alle Sektoren, die eine Datei belegt (Descriptor + Zeigersektoren + Daten).
    bool sectorsOfFile(UdosPointer descriptor, std::vector<UdosPointer>& out) const;

    // ─── Verzeichnis ─────────────────────────────────────────────────────────
    bool appendDirEntry(const std::string& name, bool secret, UdosPointer descriptor,
                        UdosPointer& dir_record);
    bool removeDirEntry(const UdosDirEntry& e);
    /// @brief Die Datei DIRECTORY um einen Satz verlaengern (§4).
    bool growDirectory(UdosPointer& neuer_satz);
    /// @brief Eine Adresse an die Zeigersektorkette einer Datei anhaengen; legt bei
    ///        Bedarf einen weiteren Zeigersektor an.
    bool appendToPointerChain(const UdosFileHeader& hdr, UdosPointer neue_adresse);

    SectorSpace& space_;
    FsProfile    prof_;
    UdosBitmap   bitmap_;
    std::string  label_ = "K1520.DISK";
    uint8_t      phys_spt_       = 16;   ///< physische Sektoren je SEITE
    uint8_t      secs_per_track_ = 32;   ///< logische Sektoren je Spur (= Zylinder)
    uint8_t      tracks_         = 80;
};
