/**
 * @file raw_sector_image.h
 * @brief RawSectorImage – DiskImage-Backend für .img (reine Sektor-Nutzdaten) + DiskFormat.
 *
 * Kapselt das bestehende .img-Verhalten: Sektor-Nutzdaten an Offsets, beschrieben durch
 * ein @ref DiskFormat (mit asymmetrischer cpa780-Geometrie).  readTrack() liest die
 * Sektoren einer (cyl, head)-Spur und materialisiert über @ref TrackCodec::buildTrack
 * eine vollständige @ref TrackImage mit echten Marken und CRCs; writeTrack() parst die
 * Spur zurück (@ref TrackCodec::parseTrack) und schreibt die Sektoren an ihre Offsets.
 *
 * Das Offset-/Interleave-Modell (Spuren verschränkt: cyl0/A, cyl0/B, cyl1/A, …) ist
 * 1:1 aus dem alten FloppyDrive übernommen, damit das Verhalten unverändert bleibt.
 *
 * @see doc/refactoring_floppy_emulator.md §6.1
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "disk_image.h"
#include "format_parser.h"
#include "track_image.h"
#include <cstdint>
#include <string>
#include <vector>

/**
 * @class RawSectorImage
 * @brief .img + DiskFormat als TrackImage-lieferndes Backend (MFM; FM via @p enc).
 */
class RawSectorImage : public DiskImage {
public:
    /**
     * @brief Öffnet ein .img-File und bindet es an ein DiskFormat.
     * @param path          Pfad zur .img-Datei (muss existieren)
     * @param fmt           Geometrie/Sektorbelegung
     * @param write_protect Schreibschutz
     * @param enc           Aufzeichnungsverfahren der Tracks (Default MFM)
     */
    RawSectorImage(const std::string& path, DiskFormat fmt,
                   bool write_protect, Encoding enc = Encoding::MFM);

    /// @brief true, wenn die Datei geöffnet werden konnte.
    bool isOpen() const { return is_open_; }

    DiskGeometry geometry() const override;
    bool         writable() const override { return is_open_ && !write_protect_; }
    TrackImage   readTrack(uint8_t cyl, uint8_t head) override;
    bool         writeTrack(uint8_t cyl, uint8_t head, const TrackImage& track) override;
    bool         flush() override { return true; }   ///< Raw schreibt synchron
    const char*  lastError() const override { return last_error_.c_str(); }

private:
    /// @brief Byte-Offset von Sektor (cyl, head, id) in der .img (1-basiertes id), oder -1.
    int64_t sectorOffset(uint8_t cyl, uint8_t head, uint8_t sector_id) const;

    std::string  path_;
    DiskFormat   fmt_;
    bool         write_protect_ = false;
    Encoding     enc_           = Encoding::MFM;
    bool         is_open_       = false;
    std::string  last_error_;
};
