/**
 * @file sector_space.h
 * @brief SectorSpace — die Sicht der Dateisysteme auf ein @ref DiskMedium.
 *
 * Verbirgt Container (`.img`/`.hfe`/`.dmk`), Aufzeichnungsverfahren und Spurgeometrie und
 * bietet genau die zwei Adressierungen, die die beiden unterstuetzten Dateisysteme brauchen:
 *
 *   - **physisch** `(Zylinder, Kopf, Sektor-ID)` — so denkt UDOS/ZDOS (Zeiger = Spur +
 *     Sektorindex, `doc/udos_diskettenformat.md` §1.2);
 *   - **linear** als fortlaufender Bytestrom — so denkt CP/M (Bloecke ab einem Offset).
 *
 * Die lineare Reihenfolge ist **exakt** die des `.img`-Codecs (Zylinder aussen, Kopf innen,
 * Sektoren nach aufsteigender ID, @ref ImgCodec::sectorOffset).  Damit gilt fuer jede
 * `.img`-faehige Diskette `SectorSpace::read(off,…) ≡ Datei-Offset off`, und die
 * Byte-Offsets aus den `diskdefs` von cpmtools sind unmittelbar uebertragbar.
 *
 * Geschrieben wird **immer nur ins Medium**, nie in die Datei — das erledigt
 * @ref DiskImage::flush.  Ein abgebrochener Kopiervorgang hinterlaesst deshalb keine halb
 * geschriebene Datei.
 *
 * @see doc/design/13_k1520disktool.md §5
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/peripherals/floppy_drive/disk_format.h"
#include "core/peripherals/floppy_drive/disk_medium.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * @struct SectorData
 * @brief Ein gelesener Sektor: Nutzdaten, Nachspann und der Zustand beider CRCs.
 */
struct SectorData {
    std::vector<uint8_t> data;               ///< Nutzbytes (Sektorgroesse laut ID-Feld)
    std::vector<uint8_t> tail;               ///< Bytes hinter der Daten-CRC (UDOS-Kontrollblock)
    bool id_crc_ok   = true;
    bool data_crc_ok = true;

    /// @brief true, wenn beide CRCs stimmen.
    bool ok() const { return id_crc_ok && data_crc_ok; }
};

/**
 * @class SectorSpace
 * @brief Sektorweiser Zugriff auf ein Medium — physisch und linear.
 *
 * Haelt eine Referenz auf das Medium (keine Kopie).  Geparste Spuren werden
 * zwischengespeichert und ueber @ref DiskMedium::revision verworfen, sobald irgendjemand
 * das Medium veraendert hat.
 */
class SectorSpace {
public:
    /// @brief Kopf-Filter „alle Koepfe" — die uebliche CP/M-Sicht auf die ganze Diskette.
    static constexpr uint8_t kAllHeads = 0xFF;

    /**
     * @param medium      Medium (muss den SectorSpace ueberleben)
     * @param fmt         Geometrie; bestimmt Spurlaengen und damit die lineare Adressierung
     * @param head_filter kAllHeads = ganze Diskette; sonst nur dieser Kopf (UDOS: eine Seite)
     */
    SectorSpace(DiskMedium& medium, const DiskFormat& fmt, uint8_t head_filter = kAllHeads);

    const DiskFormat& format()     const { return fmt_; }
    uint8_t           headFilter() const { return head_filter_; }

    // ─── physisch ────────────────────────────────────────────────────────────

    /// @brief Sektor lesen.  false = Spur/ID nicht vorhanden (CRC-Fehler sind KEIN Fehlschlag,
    ///        sondern stehen in @ref SectorData::id_crc_ok / data_crc_ok).
    bool readSector(uint8_t cyl, uint8_t head, uint8_t id, SectorData& out) const;

    /**
     * @brief Sektor schreiben (Datenfeld + optional Nachspann), CRC wird neu gerechnet.
     * @param tail leer = vorhandene Bytes hinter der CRC bleiben stehen
     * @return false ohne jede Aenderung (Spur/ID fehlt, Laenge passt nicht, Marke im Weg)
     */
    bool writeSector(uint8_t cyl, uint8_t head, uint8_t id,
                     const std::vector<uint8_t>& data,
                     const std::vector<uint8_t>& tail = {});

    // ─── Spuren des Raums (Dateisysteme brauchen die Reihenfolge) ────────────

    /**
     * @struct TrackRef
     * @brief Eine Spur des Raums in Layout-Reihenfolge.
     *
     * Die Dateisysteme rechnen ihre Adressen selbst in Spuren um — CP/M, weil der
     * **Sektorversatz** (skew) je Spur gilt und nur das Dateisystem ihn kennt; UDOS,
     * weil ein Satz nie eine Spurgrenze ueberschreitet.
     */
    struct TrackRef {
        uint8_t  cyl = 0, head = 0;
        uint64_t start = 0;        ///< Byte-Offset im linearen Raum
        uint64_t bytes = 0;        ///< Nutzbytes der Spur
        uint8_t  sectors = 0;
        uint16_t sector_size = 0;
        uint8_t  first_id = 1;
    };

    size_t   trackCount() const { return slots_.size(); }
    TrackRef trackAt(size_t i) const;
    /// @brief Index einer Spur in der Layout-Reihenfolge; -1 = nicht im Raum.
    int      trackIndexOf(uint8_t cyl, uint8_t head) const { return slotOf(cyl, head); }

    /// @brief Sektorgroesse dieser Spur laut Format (0 = Spur gehoert nicht zum Raum).
    uint16_t sectorSize(uint8_t cyl, uint8_t head) const;
    /// @brief Sektoren je Spur laut Format (0 = Spur gehoert nicht zum Raum).
    uint8_t  sectorsPerTrack(uint8_t cyl, uint8_t head) const;
    /// @brief Erste Sektor-ID dieser Spur laut Format (IBM-ueblich 1).
    uint8_t  firstSectorId(uint8_t cyl, uint8_t head) const;
    /// @brief Traegt die Spur ueberhaupt Adressmarken? (false = unformatiert)
    bool     trackFormatted(uint8_t cyl, uint8_t head) const;

    /**
     * @brief Ist die Spur schon bekannt? — **laedt NICHT nach**.
     *
     * Nur an einer physischen Diskette je @c false; bei einer Datei ist jede Spur
     * bekannt.  Damit kann eine Anzeige entscheiden, ob ein Zugriff sie warten liesse
     * (@ref FileSystem::detailsReady).
     */
    bool     trackKnown(uint8_t cyl, uint8_t head) const;

    // ─── linear ──────────────────────────────────────────────────────────────

    /// @brief Nutzbytes aller zum Raum gehoerenden Spuren.
    uint64_t size() const { return total_bytes_; }

    /// @brief Byte-Offset des ersten Sektors einer Spur; -1 = Spur nicht im Raum.
    int64_t  offsetOf(uint8_t cyl, uint8_t head) const;
    /// @brief Byte-Offset eines Sektors; -1 = Spur/ID nicht im Raum.
    int64_t  offsetOf(uint8_t cyl, uint8_t head, uint8_t id) const;

    /// @brief @p n Bytes ab @p offset lesen (ueber Sektor- und Spurgrenzen hinweg).
    bool read(uint64_t offset, uint8_t* dst, size_t n) const;
    /// @brief @p n Bytes ab @p offset schreiben (Lesen-Aendern-Schreiben je Sektor).
    bool write(uint64_t offset, const uint8_t* src, size_t n);

    const std::string& lastError() const { return last_error_; }

private:
    /// @brief Eine Spur des linearen Raums.
    struct Slot {
        uint8_t  cyl   = 0;          ///< LOGISCHE Spur (so, wie das Dateisystem zaehlt)
        uint8_t  phys  = 0;          ///< physischer Zylinder im Medium (`DiskFormat::step`)
        uint8_t  head  = 0;
        uint64_t start = 0;          ///< Byte-Offset des Spuranfangs im linearen Raum
        uint64_t bytes = 0;          ///< Nutzbytes der Spur
        const TrackFormat* tf = nullptr;
    };

    /// @brief Slot-Index einer Spur; -1 = nicht im Raum.
    int slotOf(uint8_t cyl, uint8_t head) const;
    /// @brief Slot-Index zu einem linearen Offset; -1 = ausserhalb.
    int slotAt(uint64_t offset) const;

    /// @brief Geparste Spur aus dem Zwischenspeicher (parst bei Bedarf).
    const std::vector<LogicalSector>& sectors(int slot) const;

    DiskMedium&       medium_;
    const DiskFormat& fmt_;
    uint8_t           head_filter_;

    std::vector<Slot> slots_;
    uint64_t          total_bytes_ = 0;

    mutable std::vector<std::vector<LogicalSector>> cache_;
    mutable std::vector<uint8_t>                    cache_gueltig_;
    mutable uint64_t                                cache_revision_ = 0;
    mutable std::string                             last_error_;
};
