/**
 * @file disk_image.h
 * @brief DiskImage – abstraktes Disketten-Backend (liefert/nimmt fertige TrackImages).
 *
 * Der neue Floppy-Controller (@ref K5122) sieht NUR @ref TrackImage — nie Sektoren,
 * Offsets oder Dateiformat.  Hinter dieser Schnittstelle stehen austauschbare Backends:
 *   - @ref RawSectorImage — bestehendes .img + Geometriebeschreibung (synthetisiert den Track);
 *   - HfeImage (Folgearbeit) — .hfe mit echtem MFM/FM-Bitstrom pro Spur.
 *
 * @see doc/refactoring_floppy_emulator.md §6
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "track_image.h"
#include "track_codec.h"     // TrackLayout
#include "format_parser.h"   // DiskFormat
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

/**
 * @struct DiskGeometry
 * @brief Geometrieabfrage für UI/Validierung.  Self-describing-Backends (HFE) füllen
 *        das aus der Datei, @ref RawSectorImage aus dem DiskFormat.
 */
struct DiskGeometry {
    uint8_t  num_cyls  = 0;
    uint8_t  num_heads = 0;
    bool     uniform   = false;   ///< true = alle Spuren gleich (Info für UI)
    Encoding encoding  = Encoding::MFM;  ///< vorherrschendes Verfahren der Diskette
};

/**
 * @class DiskImage
 * @brief Abstraktes Disketten-Image.  Liefert/nimmt fertige TrackImages pro (cyl, head).
 */
class DiskImage {
public:
    virtual ~DiskImage() = default;

    /// @brief Geometrie (für UI/Mount-Validierung).
    virtual DiskGeometry geometry() const = 0;
    /// @brief true, wenn schreibbar (nicht WP, nicht read-only-Backend).
    virtual bool writable() const = 0;

    /**
     * @brief Decodierte Spur (cyl, head) als TrackImage.
     * @return leeres TrackImage, wenn die Spur nicht existiert.
     */
    virtual TrackImage readTrack(uint8_t cyl, uint8_t head) = 0;

    /**
     * @brief Geänderte Spur zurückschreiben.
     *
     * Raw: Track parsen → Sektoren → .img.  HFE: Track → Bitstrom → Datei.
     * @return false bei Write-Protect oder Fehler.
     */
    virtual bool writeTrack(uint8_t cyl, uint8_t head, const TrackImage& track) = 0;

    /// @brief Persistiert ausstehende Änderungen (No-op bei synchronem Schreiben).
    virtual bool flush() = 0;

    /// @brief Letzte Fehlermeldung (für Mount-Diagnose).
    virtual const char* lastError() const { return ""; }

    /**
     * @brief Fabrik mit Format-Sniffing.
     *
     * Erkennt HFE an der Signatur "HXCPICFE"/"HXCHFEV3" (Folgearbeit), sonst Raw/.img
     * (braucht @p fmt).  @p fmt nur für nicht-self-describing Backends nötig.
     *
     * @param path          Pfad zur Image-Datei
     * @param fmt           DiskFormat (nur Raw); std::nullopt für self-describing
     * @param write_protect Schreibschutz
     * @param layout        Track-Layout für das Raw-Backend (Default IbmStandard).
     *                      HFE-Dateien sind self-describing und ignorieren diesen Parameter.
     * @return DiskImage oder nullptr bei Fehler/unbekanntem Format
     */
    static std::unique_ptr<DiskImage> open(const std::string& path,
                                           std::optional<DiskFormat> fmt,
                                           bool write_protect,
                                           TrackLayout layout = TrackLayout::IbmStandard);
};
