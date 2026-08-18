/**
 * @file geometry_probe.h
 * @brief Geometrie eines Mediums MESSEN und gegen den Formatkatalog abgleichen.
 *
 * Stufe 1 der Formaterkennung des k1520DiskTool (doc/design/13_k1520disktool.md §12.1):
 * `.hfe` und `.dmk` sind selbstbeschreibend — nach dem Laden liegt das ganze Medium vor,
 * und @ref TrackCodec::parseTrack liefert je Spur die **tatsaechlichen** Sektor-IDs,
 * Sektorgroessen und das Verfahren.  Daraus entsteht ein gemessener Spurbereichsplan, der
 * mit jedem @ref DiskFormat des Katalogs verglichen wird.
 *
 * Kein Treffer heisst **Abbruch mit Diagnose**, nicht „irgendein Format nehmen":
 * @ref GeometryProbe::describe formatiert die Messung so, dass sie zugleich als Vorlage
 * fuer einen fehlenden Katalogeintrag taugt.
 *
 * @see doc/design/13_k1520disktool.md §12
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/peripherals/floppy_drive/disk_format.h"
#include "core/peripherals/floppy_drive/disk_medium.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @struct MeasuredTrack
 * @brief Was auf einer Spur tatsaechlich steht.
 */
struct MeasuredTrack {
    uint8_t  cyl        = 0;
    uint8_t  head       = 0;
    bool     formatted  = false;   ///< traegt Adressmarken
    uint8_t  sectors    = 0;       ///< Anzahl gefundener Sektoren (mit Doppelgaengern)
    /// @brief Anzahl **verschiedener** Sektor-IDs; 0 = nicht ermittelt.
    ///
    /// Meist gleich @ref sectors.  Eine Spur, die in einem Zug ueber die Umdrehung
    /// hinaus beschrieben wurde, traegt am Ende ein zweites Exemplar ihrer ersten
    /// Sektoren — die SCP1700-Bootspur des A7100 ist so eine (16 Sektoren, 19
    /// Adressmarken).  Fuer den Abgleich mit einem Format zaehlt diese Zahl: der
    /// Treiber liest ueber die ID, ein Doppelgaenger bringt keinen Platz.
    /// Zugriff ueber @ref uniqueSectors, nicht direkt.
    uint8_t  unique_sectors = 0;

    /// @brief Verschiedene Sektor-IDs — mit Rueckfall auf @ref sectors.
    uint8_t uniqueSectors() const { return unique_sectors ? unique_sectors : sectors; }
    uint16_t sector_size= 0;       ///< einheitliche Sektorgroesse (0 = uneinheitlich)
    uint8_t  first_id   = 0;       ///< kleinste Sektor-ID
    /// @brief Spurnummer aus dem ID-FELD.  Bei Doppelschritt-Disketten ist sie die
    ///        LOGISCHE Spur und weicht deshalb vom physischen Zylinder ab
    ///        (physisch c4h0 traegt `cyl=2`) — das ist das Erkennungsmerkmal.
    uint8_t  id_cyl     = 0;
    bool     ids_dense  = false;   ///< IDs bilden lueckenlos first_id … first_id+sectors-1
    Encoding encoding   = Encoding::MFM;
    uint16_t crc_errors = 0;       ///< Sektoren mit ID- oder Daten-CRC-Fehler
};

/**
 * @struct GeometryMatch
 * @brief Ergebnis des Abgleichs einer Messung mit EINEM Format.
 */
struct GeometryMatch {
    const DiskFormat* format = nullptr;
    bool        ok        = false;
    std::string reason;              ///< bei ok == false: warum nicht
    uint16_t    empty_tracks = 0;    ///< tolerierte unformatierte Spuren (am Ende)
    uint16_t    gap_tracks   = 0;    ///< unformatierte Spuren ZWISCHEN beschriebenen
    uint16_t    stray_tracks = 0;    ///< beschriebene Spuren HINTER dem Format (Altbestand)
    uint16_t    defect_tracks= 0;    ///< Spuren mit zu WENIGEN Sektoren (Schaden)
    uint16_t    crc_errors   = 0;    ///< Summe ueber alle Spuren
    uint16_t    slack_cyls   = 0;    ///< ungenutzte Zylinder des Formats (kleiner = besser)

    /// @brief Auffaelligkeiten im Klartext ("" = makellos) — gehoert in die Anzeige.
    std::string remarks() const;
};

/**
 * @namespace GeometryProbe
 * @brief Messen, Abgleichen, Beschreiben.
 */
namespace GeometryProbe {

/// @brief Alle Spuren des Mediums vermessen (Reihenfolge: Zylinder aussen, Kopf innen).
std::vector<MeasuredTrack> measure(const DiskMedium& medium);

/**
 * @brief Nur die angegebenen Zylinder vermessen — fuer teuer nachladende Medien.
 *
 * Am echten Laufwerk kostet jede Spur 0,5–0,8 s; eine Vollmessung von 160 Spuren
 * braucht anderthalb Minuten, obwohl die Katalogformate sich an **wenigen** Stellen
 * unterscheiden.  Nachgerechnet ueber alle Formatpaare des Katalogs trennen acht
 * Spuren alles, was ueberhaupt trennbar ist; die mit Abstand wichtigste ist
 * **Zylinder 3** — dieselbe, die das CP/A-BIOS liest (@c dlgint,
 * doc/cpa_format_detection.md).
 *
 * @warning Das Ergebnis ist eine Aussage ueber die **gemessenen** Spuren.  Die Regeln
 *          in @ref match zaehlen Luecken, Altbestand und Schadstellen nur unter ihnen;
 *          @ref synthesize braucht weiterhin die Vollmessung, denn es leitet die
 *          Geometrie aus dem lueckenlosen Bild ab.
 */
std::vector<MeasuredTrack> measureTracks(
    const DiskMedium& medium,
    const std::vector<std::pair<uint8_t, uint8_t>>& tracks);

/**
 * @brief Die **Spuren** (Zylinder + Kopf), die zum Unterscheiden der Formate genuegen.
 *
 * Acht Stueck, aus dem Katalog ausgerechnet — nicht acht Zylinder auf beiden Seiten:
 * Kopf 1 traegt genau eine Sonde (Zylinder 0), und die beantwortet nur die Frage
 * ein- oder zweiseitig.  Alles Weitere entscheidet sich auf Kopf 0.
 *
 * | Sonde | trennt zusaetzlich |
 * |-------|--------------------|
 * | 3/0   | 1606 Formatpaare (die CP/A-Sonde `dlgint`) |
 * | 0/1   | 99  |
 * | 78/0  | 35  (40 oder 80 Spuren) |
 * | 0/0   | 15  |
 * | 2/0, 40/0, 1/0, 77/0 | je 1–4 |
 *
 * Die ungeraden Zylinder sind Pflicht, nicht Beiwerk: ohne sie faende die
 * Doppelschritt-Regel ihre Luecken nicht und jedes `step: 2`-Format fiele durch.
 */
std::vector<std::pair<uint8_t, uint8_t>> probeTracks(uint8_t num_cyls,
                                                     uint8_t num_heads);

/**
 * @brief **Stichprobe** eines teuer nachladenden Mediums — sucht sich ihre Spuren selbst.
 *
 * Anders als @ref measureTracks ist die Auswahl nicht fest, sondern haengt vom
 * Gemessenen ab.  Das ist noetig, weil die Regeln in @ref match nicht nur die
 * einzelnen Spuren bewerten, sondern die **Ausdehnung** der Diskette: ein Format,
 * das mehr Zylinder deklariert als beschrieben sind, faellt durch.  Wer fest bei
 * Zylinder 77 sondiert und dort Altbestand einer kuerzeren Formatierung findet,
 * haelt eine 77-Spur-Diskette fuer 41 Spuren lang und waehlt ein zu kleines Format.
 *
 * Ablauf: Systemspuren (0–3) → **binaere Suche nach dem letzten beschriebenen
 * Zylinder** (~log2(n) Spuren, dabei immer zwei benachbarte pruefen, sonst halbiert
 * sie eine Doppelschritt-Diskette) → Spuren am gefundenen Ende, auch auf Kopf 1 →
 * eine Sonde in der Mitte.  Zusammen ~15 statt 160 Spuren.
 *
 * @warning Die Zaehlungen des Ergebnisses (`stray_tracks` …) sind Aussagen ueber die
 *          angesehenen Spuren.  Wer damit @ref match ruft, setzt @c stichprobe.
 */
std::vector<MeasuredTrack> measureSample(const DiskMedium& medium);

/// @brief Hoechster Zylinder mit Adressmarken; -1, wenn das Medium unformatiert ist.
int lastFormattedCylinder(const std::vector<MeasuredTrack>& tracks);

/**
 * @brief Passt die Messung zu @p fmt?
 *
 * Regeln (doc/design/13_k1520disktool.md §12.1):
 *  1. jede **formatierte** Spur muss von einem Spurbereich abgedeckt sein und in
 *     Sektorgroesse, erster ID und Verfahren uebereinstimmen;
 *  2. unformatierte Spuren **am Ende** werden toleriert (echte Abbilder tragen oft ein
 *     bis drei leere Zusatzspuren) — sie zaehlen als @ref empty_tracks.  Unformatierte
 *     Spuren **zwischen** beschriebenen (@ref gap_tracks) sind dagegen ein Ausschluss:
 *     so sieht eine **Doppelschritt**-Diskette aus (nur jeder zweite Zylinder
 *     beschrieben, §3.4-Geometrien T/U des CP/A-FORMAT).  Ohne diese Unterscheidung
 *     passt jedes 80-Zylinder-Format auf eine 40-Spur-Doppelschritt-Diskette, weil die
 *     40 Luecken als „leer" durchgingen — und das Dateisystem laese anschliessend
 *     Datenmuell.
 *  3. beschriebene Spuren **hinter** dem Format sind Altbestand einer frueheren
 *     Formatierung (@ref stray_tracks) und disqualifizieren nicht: das Dateisystem
 *     fasst sie nie an.  Der Referenzdatentraeger `udos_boot_scp.hfe` traegt genau
 *     das — 77 UDOS-Spuren, dahinter drei Spuren 9×512 aus einem frueheren Leben;
 *  4. **zu wenige** Sektoren auf einer Spur sind ein Schaden (@ref defect_tracks), kein
 *     anderes Format — zu VIELE dagegen schon;
 *  5. einzelne CRC-Fehler disqualifizieren nicht — sie werden gezaehlt und gemeldet;
 *  6. ein Format mit mehr Koepfen als das Medium passt nie (kein DS-Format auf einer
 *     einseitigen Diskette).
 */
GeometryMatch match(const std::vector<MeasuredTrack>& tracks, const DiskFormat& fmt,
                    bool stichprobe = false);

/**
 * @brief Alle passenden Formate, bestes zuerst.
 *
 * Rangfolge: wenige ungenutzte Zylinder vor vielen, dann wenige tolerierte Leerspuren.
 * Mehrere gleich gute Treffer sind **normal** — `formats.yaml` enthaelt geometrisch
 * identische Eintraege (`cpa640` ≡ `k5601_16x256`).  Welches Dateisystem darauf liegt,
 * entscheidet Stufe 2.
 */
std::vector<GeometryMatch> matchAll(const std::vector<MeasuredTrack>& tracks,
                                    const std::vector<DiskFormat>& formats,
                                    bool stichprobe = false);

/// @brief Messung als Klartext — Grundlage der Fehlermeldung „passt zu keinem Format".
std::string describe(const std::vector<MeasuredTrack>& tracks);

/**
 * @brief Aus der Messung ein **namenloses** @ref DiskFormat bauen (Name `(gemessen)`).
 *
 * Der letzte Rueckfall, wenn keine Katalogsgeometrie passt: statt die Diskette
 * abzuweisen, wird beschrieben, was tatsaechlich daraufsteht — dieselbe Umformung, die
 * @ref describe fuer den Menschen macht, nur als Datenstruktur.  Zusammen mit der
 * CP/A-Regel (@ref CpaDpbRule) laesst sich eine fremde Diskette damit **lesen**, ohne
 * dass jemand vorher einen Katalogeintrag schreibt.
 *
 * Die Spurbereiche werden korrekt als RECHTECKE gebildet: erst Zylinder mit gleichem
 * Kopf-Muster zusammenfassen, dann darin die Koepfe — sonst bekaeme eine gemischte
 * Geometrie (cpa780: c0h0/c0h1/c1h0 sind 128 B, c1h1 schon 1024 B) einen Bereich, den
 * es gar nicht gibt.  Ein erkanntes Luecken-Muster wird als `step: 2` ausgedrueckt.
 *
 * @param why  Grund, wenn nichts herauskommt
 * @return leer, wenn die Diskette gar nicht beschreibbar ist: keine formatierte Spur,
 *         uneinheitliche Sektorgroessen auf einer Spur, oder Luecken MITTENDRIN, die
 *         kein Doppelschritt sind (dann waere der lineare Sektorraum locherig und
 *         jedes Dateisystem darueber Zufall).
 *
 * @warning Ein so gelesener Datentraeger ist **schreibgeschuetzt** — die Geometrie ist
 *          geraten, nicht belegt (@ref DiskVolume::open).
 */
std::optional<DiskFormat> synthesize(const std::vector<MeasuredTrack>& tracks,
                                     std::string* why);

}  // namespace GeometryProbe
