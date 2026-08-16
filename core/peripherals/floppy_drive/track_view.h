/**
 * @file track_view.h
 * @brief TrackView — eine Spur als lückenlose Folge zeichenbarer Abschnitte.
 *
 * Für die **Anschauung**, nicht für den Betrieb: der Diskeditor des k1520DiskTool
 * zeichnet jede Spur als Ring und braucht dafür, was wo liegt — Sektor, Gap oder
 * unformatiert, jeweils mit Anfang und Ende als Bruchteil einer Umdrehung.
 *
 * ### Warum die Winkel ohne Drehzahl auskommen
 * Eine @ref TrackImage **ist** genau eine Umdrehung.  Der Winkel eines Bytes ist damit
 * schlicht `Position / Spurlänge` — unabhängig von Bitrate und Drehzahl, die der
 * HFE-Kopf zwar nennt, die aber nur die *Zeitachse* skalieren.  Bei `.img` gibt es
 * keine echte Winkelinformation; dort liegen die Sektoren gleichmäßig, weil der
 * Container es nicht besser weiß (@ref ImgCodec baut die Spur mit
 * @ref TrackCodec::buildTrack).
 *
 * ### Gap und „unformatiert" sind zwei verschiedene Dinge
 * * **unformatiert** — die Spur trägt keine einzige Adressmarke: entweder gar keine
 *   Bytes (die Spur existiert nicht) oder markenloser Gap-Fluss, wie ihn
 *   `DiskImage::createBlank` schreibt.  Ein Gast kann sie formatieren.
 * * **Gap** — auf einer formatierten Spur alles, was zwischen den Sektorfeldern liegt
 *   (Sync, Lücken, IAM).  Ein Sektor reicht dabei vom Beginn seiner Sync-Gruppe bis
 *   hinter seine Daten-CRC.
 *
 * @see doc/design/13_k1520disktool.md §19
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "track_codec.h"
#include "track_image.h"

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @struct TrackSpan
 * @brief Ein Abschnitt der Spur — Sektor, Gap oder unformatiert.
 */
struct TrackSpan {
    enum class Kind : uint8_t { Unformatted = 0, Gap = 1, Sector = 2 };

    Kind   kind  = Kind::Gap;
    double start = 0.0;   ///< Anfang als Bruchteil der Umdrehung (0 = Index, 12 Uhr)
    double end   = 1.0;   ///< Ende, immer > @ref start (kein Umlauf: s. u.)

    // ── nur bei @ref Kind::Sector ────────────────────────────────────────────
    int      index    = -1;   ///< laufende Nummer in der Spur (Zugriffsschlüssel)
    uint8_t  cyl      = 0;    ///< Zylinder AUS DEM ID-FELD (kann von der Lage abweichen)
    uint8_t  head     = 0;
    uint8_t  id       = 0;
    uint16_t size     = 0;    ///< Bytes im Datenfeld
    bool     id_crc_ok   = false;
    bool     data_crc_ok = false;
    bool     deleted     = false;  ///< Datenmarke 0xF8 statt 0xFB
    /**
     * @brief Traegt das Datenfeld nichts Unterscheidbares — alle Bytes gleich?
     *
     * So sieht ein Sektor aus, der zwar **formatiert**, aber nie beschrieben wurde:
     * das Formatieren fuellt ihn mit einem Fuellbyte (CP/M 0xE5, andere 0x00/0xFF).
     * Statt auf einen bestimmten Wert zu pruefen, zaehlt die Gleichfoermigkeit —
     * welches Byte ein Format benutzt, ist dessen Sache.
     *
     * **Nur das Datenfeld.**  Der UDOS-Sektorkontrollblock hinter der Daten-CRC
     * (@ref LogicalSector::tail) bleibt aussen vor: er traegt die Dateiverkettung
     * und ist auch auf einer leeren Diskette belegt — mitgezaehlt saehe dort kein
     * Sektor mehr leer aus.
     *
     * @note Eine Datei aus lauter gleichen Bytes gilt hier ebenfalls als leer.  Das
     *       ist hingenommen: die Anzeige sagt „nichts Unterscheidbares", und genau
     *       das trifft dann ja zu.
     */
    bool     blank       = false;

    /// @brief Gilt der Sektor als lesbar?  Beide CRCs müssen stimmen.
    bool ok() const { return id_crc_ok && data_crc_ok; }
};

/**
 * @struct TrackView
 * @brief Eine ganze Spur: Kopfangaben + lückenlose Abschnittsfolge.
 *
 * Die Abschnitte decken `[0,1)` **lückenlos und überschneidungsfrei** ab, damit die
 * Darstellung nicht raten muss und ein Treffertest über den Winkel eindeutig ist.
 * Ein Sektor, der über den Index läuft (Anfang kurz vor 1, Ende hinter 0), wird an
 * der Nahtstelle in ZWEI Abschnitte mit derselben @ref TrackSpan::index geteilt —
 * sonst wäre `start < end` nicht zu halten.
 */
struct TrackView {
    bool     exists    = false;  ///< false = die Spur gibt es in dieser Geometrie nicht
    bool     formatted = false;  ///< false = keine einzige Adressmarke
    Encoding encoding  = Encoding::MFM;
    size_t   bytes     = 0;      ///< Spurlänge (eine Umdrehung)
    int      sectors   = 0;      ///< Anzahl Sektoren (ohne die Naht-Teilstücke)
    std::vector<TrackSpan> spans;
};

/// @brief Eine Spur in ihre zeichenbaren Abschnitte zerlegen.
TrackView scanTrack(const TrackImage& track);
