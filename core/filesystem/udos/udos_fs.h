/**
 * @file udos_fs.h
 * @brief UDOS-/ZDOS-Dateisystem (UDOS 1526 / 4.x) — eine Diskettenseite.
 *
 * Vollstaendig spezifiziert in **`doc/udos_diskettenformat.md`** (an einer echten,
 * fehlerfrei eingelesenen Diskette gemessen).  Die drei Eigenheiten, die dieses
 * Dateisystem von CP/M unterscheiden und die ganze Umsetzung praegen:
 *
 * 1. **Die Verkettung steht hinter der Daten-CRC.**  Je Sektor folgen dort 4 Bytes:
 *    Rueckwaerts- und Vorwaertszeiger (@ref UdosPointer).  Ein rohes Sektorabbild
 *    (`.img`) verliert sie und damit das gesamte Dateisystem — deshalb sind
 *    UDOS-Profile nie `.img`-faehig.  Geliefert werden sie von
 *    @ref SectorData::tail (aus @ref TrackCodec::parseTrack).
 * 2. **Jede Seite ist ein eigener Datentraeger** (§2): eigene Belegungskarte, eigenes
 *    Verzeichnis, eigener Name.  Diese Klasse bedient GENAU EINE Seite; die Diskette
 *    als Ganzes setzt zwei davon nebeneinander (`Side0/`, `Side1/`).
 * 3. **Zuteilungseinheit ist der Satz, nicht der Sektor** (§7): ein Satz belegt
 *    `Satzlaenge/128` physisch aufeinanderfolgende Sektoren **derselben Spur**, alle
 *    mit demselben Kontrollblock; die Zeiger adressieren den **ersten** Sektor des
 *    Vorgaenger- bzw. Nachfolgesatzes.
 *
 * Das Verzeichnis ist selbst eine gewoehnliche Datei namens `DIRECTORY` (Typ `D`),
 * deren Kopfsektor auf Spur 22 Sektor 1 liegt — der einzige feste Einstiegspunkt.
 *
 * @see doc/udos_diskettenformat.md
 * @see doc/design/13_k1520disktool.md §8
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/filesystem/file_system.h"
#include "core/filesystem/fs_profile.h"
#include "core/filesystem/sector_space.h"
#include "core/filesystem/udos/udos_bitmap.h"

#include <memory>
#include <string>
#include <vector>

/**
 * @struct UdosPointer
 * @brief Zeiger auf einen Sektor: Sektorindex (0-basiert) + Spur.  `FF FF` = Kettenende.
 * @see doc/udos_diskettenformat.md §1.2
 */
struct UdosPointer {
    uint8_t sector_index = 0xFF;   ///< 0 = Sektor-ID 1
    uint8_t track        = 0xFF;

    bool    end() const { return sector_index == 0xFF && track == 0xFF; }
    uint8_t sectorId() const { return static_cast<uint8_t>(sector_index + 1); }

    static UdosPointer fromBytes(const uint8_t* p) { return {p[0], p[1]}; }
    bool operator==(const UdosPointer& o) const {
        return sector_index == o.sector_index && track == o.track;
    }
};

/**
 * @struct UdosFileHeader
 * @brief Der 128-B-Kopfsektor einer Datei (§6) — nur die belegten Felder.
 */
struct UdosFileHeader {
    UdosPointer directory_sector;   ///< Rueckwaertszeiger: Verzeichnissektor mit dem Eintrag
    UdosPointer first_record;       ///< Vorwaertszeiger: erster Satz
    UdosPointer last_record;
    uint8_t     type_byte    = 0;   ///< 80=P · 81=P1 · 40=D · 20=A · 10=B
    uint16_t    record_count = 0;
    uint16_t    record_len   = 128;
    uint8_t     properties   = 0;   ///< 80=W · 40=E · 20=L · 10=S · 08=R · 04=F
    uint16_t    entry_addr   = 0;
    uint16_t    bytes_in_last= 0;
    std::string created;            ///< 6 ASCII: "JJMMTT" ODER ein Versionstext ("V 4.3 ")
    std::string modified;           ///< 6 ASCII "JJMMTT"

    /// @brief Dateilaenge nach §7.1.
    uint64_t length() const;
    /// @brief Typkuerzel ("A"/"P"/"P1"/"B"/"D"), "" bei unbekanntem Byte.
    std::string typeName() const;
    /// @brief Eigenschaften als Buchstaben in der Reihenfolge W E L S R F.
    std::string propertyLetters() const;
};

/**
 * @struct UdosDirEntry
 * @brief Ein Eintrag der Verzeichnisdatei (§5): Flagbyte, Name, Zeiger auf den Kopfsektor.
 */
struct UdosDirEntry {
    std::string name;
    bool        secret = false;      ///< Bit 7 des Flagbytes — Spiegel der S-Eigenschaft
    UdosPointer header;
    uint32_t    record_index = 0;    ///< in welchem Satz der Verzeichnisdatei er steht
    uint32_t    offset       = 0;    ///< Byte-Offset innerhalb dieses Satzes
};

/**
 * @class UdosFileSystem
 * @brief Eine UDOS-Seite: auflisten und lesen.
 */
class UdosFileSystem : public FileSystem {
public:
    /**
     * @param space Sektorraum, der **nur diese Seite** umfasst
     *              (@ref SectorSpace mit Kopf-Filter)
     * @param prof  Dateisystemprofil (`type: udos`)
     * @param head  physische Kopfnummer dieser Seite
     * @param err   Grund, wenn nullptr zurueckkommt
     */
    static std::unique_ptr<UdosFileSystem> mount(SectorSpace& space, const FsProfile& prof,
                                                 uint8_t head, std::string& err);

    std::vector<FileEntry> list() const override;
    bool   read (const std::string& name, std::vector<uint8_t>& out) override;
    bool   write(const std::string& name, const std::vector<uint8_t>& data,
                 const WriteOptions& opt) override;
    bool   erase(const std::string& name) override;
    bool   wouldFit(const std::vector<PlannedFile>& files, FitReport& out) const override;
    bool   mkfs() override;
    FsInfo info() const override;

    // ─── Innenansicht (Diagnose, Tests) ──────────────────────────────────────

    const UdosBitmap& bitmap() const { return bitmap_; }
    /// @brief Kopfsektor der Verzeichnisdatei — der feste Einstiegspunkt (§5).
    UdosPointer directoryHeader() const {
        return UdosPointer{0, prof_.directory_track};
    }
    /// @brief Alle Verzeichniseintraege, roh.
    std::vector<UdosDirEntry> directory() const;
    /// @brief Kopfsektor einer Datei lesen.
    bool readHeader(UdosPointer p, UdosFileHeader& out) const;
    /// @brief Sektorkette einer Datei — die Anfangszeiger aller Saetze.
    bool recordChain(const UdosFileHeader& hdr, std::vector<UdosPointer>& out) const;

    /// @brief Positivprobe der Erkennung: sieht diese Seite nach UDOS aus?
    static bool looksLikeUdos(const SectorSpace& space, const FsProfile& prof,
                              uint8_t head, std::string* why);

private:
    UdosFileSystem(SectorSpace& space, const FsProfile& prof, uint8_t head);

    /// @brief Sektor samt Kontrollblock lesen.
    bool readSector(UdosPointer p, std::vector<uint8_t>& data,
                    UdosPointer& back, UdosPointer& fwd) const;
    /// @brief Inhalt einer Datei ab ihrem Kopfsektor zusammensetzen.
    bool readChain(const UdosFileHeader& hdr, std::vector<uint8_t>& out) const;

    SectorSpace& space_;
    FsProfile    prof_;
    uint8_t      head_ = 0;
    UdosBitmap   bitmap_;
    uint8_t      secs_per_track_ = 26;
    uint8_t      tracks_         = 77;
};
