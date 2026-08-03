/**
 * @file disk_image.h
 * @brief DiskImage – abstraktes Disketten-Backend (liefert/nimmt fertige TrackImages).
 *
 * Der neue Floppy-Controller (@ref K5122) sieht NUR @ref TrackImage — nie Sektoren,
 * Offsets oder Dateiformat.  Hinter dieser Schnittstelle stehen austauschbare Backends:
 *   - @ref RawSectorImage — bestehendes .img + Geometriebeschreibung (synthetisiert den Track);
 *   - HfeImage (Folgearbeit) — .hfe mit echtem MFM/FM-Bitstrom pro Spur.
 *
 * @see doc/design/07_k5122_afs.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "track_image.h"
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
     * @return DiskImage oder nullptr bei Fehler/unbekanntem Format
     */
    static std::unique_ptr<DiskImage> open(const std::string& path,
                                           std::optional<DiskFormat> fmt,
                                           bool write_protect,
                                           Encoding raw_encoding = Encoding::MFM);

    /**
     * @brief Legt eine NEUE, GÜLTIG FORMATIERTE, leere Image-Datei an und öffnet sie (Fabrik).
     *
     * Das Ergebnis ist eine formatierte Leerdiskette (echte IDAM/DATA-Felder mit CRC,
     * Nutzdaten = `0xE5`) — direkt les-/beschreibbar UND durch FORMAT.COM neu formatierbar.
     * Kein gap-leeres Template mehr (das beim Lesen/Formatieren hängen konnte).
     *
     * Der Dateityp wird an der Endung erkannt; @p fmt ist für BEIDE Typen ERFORDERLICH
     * (bestimmt Geometrie und Sektorlayout):
     *   - `.hfe` → HFE-v1: je Spur ein per @ref TrackCodec::buildTrack synthetisierter,
     *     in @p enc kodierter Bitzellen-Strom (Header trägt das Verfahren).
     *   - sonst (`.img`) → rohes Sektorimage in Format-Größe, mit `0xE5` gefüllt.
     *
     * Eine vorhandene Datei am @p path wird überschrieben.
     *
     * @param path          Zielpfad (Endung `.hfe` → HFE, sonst Raw/.img)
     * @param fmt           DiskFormat (Pflicht; Geometrie + Sektorlayout)
     * @param write_protect Schreibschutz des zurückgegebenen Images
     * @param enc           Aufzeichnungsverfahren (MFM = 5,25″/8″-DD, FM = 8″-SD)
     * @return DiskImage oder nullptr bei Fehler (fehlendes @p fmt, Schreibfehler)
     */
    static std::unique_ptr<DiskImage> create(const std::string& path,
                                             std::optional<DiskFormat> fmt,
                                             bool write_protect,
                                             Encoding enc = Encoding::MFM);
};
