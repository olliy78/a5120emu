/**
 * @file hfe_image.h
 * @brief HfeImage – DiskImage-Backend für HFE v1 ("HXCPICFE"), MFM und FM, lesend+schreibend.
 *
 * Liest/schreibt HxC-Floppy-Emulator-Images (HFE v1).  readTrack() de-interleavet die
 * seitenverschränkten 256-Byte-Blöcke einer Spur und decodiert den Bitzellen-Strom über
 * @ref BitCodec zu einer fertigen @ref TrackImage (mit echten Marken/CRCs aus dem Medium);
 * writeTrack() encodiert die Spur zurück und überschreibt die Blöcke in-place (die
 * Bitzellenlänge bleibt erhalten, daher ändern sich Header und LUT nicht).  Im Gegensatz
 * zu @ref RawSectorImage ist HFE **self-describing**: Geometrie und Verfahren stehen im
 * Header, ein @ref DiskFormat ist nicht nötig.
 *
 * Format (Auszug, alle Felder little-endian):
 * @code
 *   Header (Block 0, 512 B): "HXCPICFE", formatrev=0, n_track, n_side,
 *       track_encoding(0=MFM,2=FM), bitrate[kbit/s], rpm, iface, dnu, track_list_offset,
 *       write_allowed, single_step, …  (Rest 0xFF)
 *   Track-LUT (track_list_offset*512): je Zylinder { u16 offset[512-B-Blöcke]; u16 len[Byte, beide Seiten] }
 *   Spurdaten (ab offset*512): 256-B-Blöcke seitenverschränkt [S0][S1][S0][S1]… , Zellen LSB-first
 * @endcode
 *
 * @see doc/refactoring_floppy_emulator.md §6.2 / §7
 * @see https://hxc2001.com/floppy_drive_emulator/HFE-file-format.html
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "disk_image.h"
#include "track_image.h"
#include <cstdint>
#include <string>
#include <vector>

/**
 * @class HfeImage
 * @brief HFE-v1-Backend (self-describing; MFM/FM; lesend und schreibend).
 */
class HfeImage : public DiskImage {
public:
    /**
     * @brief Öffnet eine HFE-Datei und liest Header + Track-LUT ein.
     * @param path          Pfad zur .hfe-Datei
     * @param write_protect Schreibschutz (zusätzlich zu HFE write_allowed)
     */
    HfeImage(const std::string& path, bool write_protect);

    /// @brief true, wenn Datei + Header gültig gelesen wurden.
    bool isOpen() const { return is_open_; }

    DiskGeometry geometry() const override;
    bool         writable() const override { return is_open_ && hfe_write_allowed_ && !write_protect_; }
    TrackImage   readTrack(uint8_t cyl, uint8_t head) override;
    bool         writeTrack(uint8_t cyl, uint8_t head, const TrackImage& track) override;
    bool         flush() override;
    const char*  lastError() const override { return last_error_.c_str(); }

private:
    /// @brief Ein Track-LUT-Eintrag (Startblock + Bytelänge beider Seiten).
    struct TrackEntry { uint16_t offset_blocks = 0; uint16_t len_bytes = 0; };

    /// @brief De-interleavet die HFE-Spurseiten-Bytes für (cyl, head).
    std::vector<uint8_t> readSideBytes(uint8_t cyl, uint8_t head, uint32_t& bitcells_out);
    /// @brief Schreibt die Spurseiten-Bytes für (cyl, head) zurück (in-place).
    bool writeSideBytes(uint8_t cyl, uint8_t head, const std::vector<uint8_t>& side);

    std::string  path_;
    bool         write_protect_     = false;
    bool         is_open_           = false;
    std::string  last_error_;

    // ── Header-Felder ──
    uint8_t   num_tracks_       = 0;
    uint8_t   num_sides_        = 0;
    Encoding  encoding_         = Encoding::MFM;
    uint16_t  bitrate_          = 250;
    uint16_t  rpm_              = 0;
    uint16_t  track_list_block_ = 1;
    bool      hfe_write_allowed_ = false;

    std::vector<TrackEntry>   lut_;     ///< Track-LUT (ein Eintrag je Zylinder)
    std::vector<uint8_t>      file_;    ///< komplette Datei im Speicher (für In-place-Write)
    bool                      dirty_ = false;
};
