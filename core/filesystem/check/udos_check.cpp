/**
 * @file udos_check.cpp
 * @brief Die Pruefung des UDOS-/ZDOS-Dateisystems (A5120).
 *
 * Umsetzung von `doc/design/15_dateisystempruefung.md` §9.
 *
 * ### Warum hier mehr moeglich ist als bei CP/M
 * UDOS fuehrt **zwei unabhaengige Darstellungen derselben Wahrheit**: einen
 * gespeicherten Belegungsplan (Spur 23) und eine **selbsttragende** Verkettung in
 * den vier Bytes hinter jeder Daten-CRC.  Genau die Konstellation, in der eine
 * Pruefung etwas beweisen kann statt nur zu vermuten.
 *
 * Und der Plan ist die einzige Instanz, die den freien Platz kennt
 * (`doc/udos_diskettenformat.md` §4.2).  Steht dort ein Sektor als frei, der zu
 * einer Datei gehoert, vergibt UDOS ihn beim naechsten Schreiben weiter — das ist
 * @ref FsSeverity::Gefahr im Wortsinn: die Datei ist noch heil, und der naechste
 * Schreibvorgang zerstoert sie.
 *
 * ### Was NICHT geprueft wird, und warum
 * **Die Systemspuren 0–2 und die Bootspur sind Sitte, nicht Struktur.**  An echten
 * Datentraegern gemessen: `udos_boot_scp.hfe` Seite 0 hat auf Spur 0 nur die
 * Sektoren 1–3 belegt und auf Spur 1 die Sektoren 1–6 und 17–24; Seite 1 von
 * `udos_ds77_k5601_fremdsync.hfe` hat die Spuren 0–2 **voellig frei** (eine reine
 * Datenseite ohne Urlader).  Ein Pruefer, der dort „muss belegt sein" verlangt,
 * meldet auf gesunden Disketten Fehler — und wird zu Recht weggeklickt (E10).
 * Geprueft wird deshalb nur, was sich ABLEITEN laesst: die Karte muss ihre eigenen
 * Sektoren und die der Verzeichnisdatei als belegt fuehren.
 *
 * @see doc/design/15_dateisystempruefung.md §9 · doc/udos_diskettenformat.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/check/fs_check.h"
#include "core/filesystem/udos/udos_fs.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr size_t kSector = 128;
/// @brief Hoechstens so viele Befunde je Kennung — der Rest wird zusammengezogen.
constexpr size_t kMaxJeKennung = 20;

/// @brief Sektor 1 der Kartenspur — dort faengt die Belegungskarte an (§4).
///
/// Jeder Befund traegt seinen Ort so genau, wie er ihn kennt: der Sprung in den
/// Diskeditor (E9) soll den Sektor aufschlagen, den der Befundtext NENNT, nicht nur
/// die Spur.  Wo nur die Spur bekannt ist, bleibt der Sektor -1.
constexpr int kKarteSektor = 1;

std::string ort(UdosPointer p) {
    return "Spur " + std::to_string(p.track) + " Sektor " + std::to_string(p.sectorId());
}

/// @brief Sektorliste fuer @ref FsRepair::s — `"Spur:Index,…"`, Index 0-basiert.
///        Das Format ist die Verabredung mit `udos_fs.cpp`; sie ist dort am
///        `repair`-Haken in einer Tabelle festgehalten.
std::string liste(const std::vector<UdosPointer>& ps) {
    std::string s;
    for (const UdosPointer& p : ps) {
        if (!s.empty()) s += ',';
        s += std::to_string(p.track) + ':' + std::to_string(p.sector_index);
    }
    return s;
}

std::string liste(UdosPointer p) { return liste(std::vector<UdosPointer>{p}); }

}  // namespace

FsCheckReport UdosFileSystem::check(FsCheckLevel level, bool nachladen) const {
    FsCheckReport bericht;
    bericht.level = level;
    FsFindings b(bericht);

    const uint8_t spt = secs_per_track_;
    // Buchfuehrung ueber die Spuren: `gewollt` sind die, in die die Pruefung sehen
    // wollte, `da` die davon verfuegbaren.  Eine Spur, die an einer physischen
    // Diskette noch nicht gelesen ist, wird uebersprungen — die Pruefung holt sie
    // nicht von sich aus (E2), sagt es aber (E8).
    std::set<uint8_t> gewollt, da;
    auto brauche = [&](uint8_t t) {
        gewollt.insert(t);
        if (!nachladen && !space_.trackKnown(t, head_)) {
            bericht.vollstaendig = false;
            return false;
        }
        da.insert(t);
        return true;
    };
    // Vor JEDEM Verlassen: die Spurzaehler festschreiben.  Sie bedeuten „gewollt"
    // und „davon verfuegbar" — an vier Stellen von Hand gesetzt liefen sie
    // auseinander.
    auto abschluss = [&] {
        bericht.spuren_gelesen = static_cast<int>(da.size());
        bericht.spuren_gesamt  = static_cast<int>(gewollt.size());
    };
    // Eindeutige Nummer eines Sektors — Schluessel aller Mengen dieser Pruefung.
    auto nr = [&](UdosPointer p) {
        return static_cast<uint32_t>(p.track) * spt + p.sector_index;
    };

    // ═══ Ebene Verwaltung ════════════════════════════════════════════════════
    //
    // Die Kartenspur ist beim Mounten schon gelesen worden — sie wird hier nur noch
    // gebucht, damit die Spurzaehler stimmen.
    brauche(prof_.bitmap_track);

    // ── Die Karte selbst ─────────────────────────────────────────────────────
    std::string warum;
    if (!bitmap_.looksValid(spt, static_cast<uint8_t>(space_.trackCount()), &warum))
        b.addAt("udos.karte.ungueltig", FsSeverity::Fehler, FsLayer::Verwaltung,
                "Belegungskarte",
                "Die Belegungskarte auf Spur " + std::to_string(prof_.bitmap_track)
                + " ist nicht plausibel: " + warum,
                prof_.bitmap_track, head_, kKarteSektor);

    if (bitmap_.sectorsPerTrack() != spt)
        b.addAt("udos.karte.geometrie", FsSeverity::Fehler, FsLayer::Verwaltung,
                "Belegungskarte",
                "Die Karte nennt " + std::to_string(bitmap_.sectorsPerTrack())
                + " Sektoren je Spur, gemessen sind " + std::to_string(spt),
                prof_.bitmap_track, head_, kKarteSektor);
    if (bitmap_.trackCount() > space_.trackCount())
        b.addAt("udos.karte.geometrie", FsSeverity::Fehler, FsLayer::Verwaltung,
                "Belegungskarte",
                "Die Karte nennt " + std::to_string(bitmap_.trackCount())
                + " Spuren, die Seite hat " + std::to_string(space_.trackCount()),
                prof_.bitmap_track, head_, kKarteSektor);

    // §4.2: Der gespeicherte Freizaehler ist eine GEGENPROBE, nicht die Wahrheit —
    // massgeblich sind die Bits.  Der „belegt"-Zaehler bleibt aussen vor: ZDOS
    // bildet ihn als Festwert 2464 − frei (Konstante aus FORMATPC.MAC), er sagt
    // ueber die Diskette nichts aus.
    const int frei = bitmap_.countFree();
    if (bitmap_.storedFree() != frei) {
        FsFinding& f = b.addAt("udos.karte.zaehler", FsSeverity::Warnung, FsLayer::Verwaltung,
                "Belegungskarte",
                "Der gespeicherte Freizaehler sagt " + std::to_string(bitmap_.storedFree())
                + ", ausgezaehlt sind " + std::to_string(frei) + " Sektoren",
                prof_.bitmap_track, head_, kKarteSektor);
        // Der harmloseste Eingriff ueberhaupt: die Bits sind die Wahrheit, der
        // Zaehler nur ihre Gegenprobe (§4.2).
        f.repairs.push_back(FsRepair{"udos.karte.zaehler.neu",
            "Freizaehler auf den ausgezaehlten Wert " + std::to_string(frei) + " setzen",
            /*datenverlust*/false, /*empfohlen*/true});
    }

    // ── Die Verzeichnisdatei ─────────────────────────────────────────────────
    //
    // Sie ist der einzige feste Einstiegspunkt (§5).  Was hier bricht, macht die
    // ganze Seite unlesbar — deshalb steht sie noch in der Schnellpruefung.
    if (!brauche(prof_.directory_track)) {
        b.add("udos.verz.ungelesen", FsSeverity::Info, FsLayer::Verwaltung, "",
              "Die Verzeichnisspur ist noch nicht gelesen — geprueft wurde nichts");
        abschluss();
        bericht.sortieren();
        return bericht;
    }
    UdosFileHeader dir_hdr;
    if (!readHeader(directoryHeader(), dir_hdr)) {
        b.addAt("udos.verz.kaputt", FsSeverity::Fehler, FsLayer::Verwaltung, "DIRECTORY",
                "Der Kopfsektor der Verzeichnisdatei (" + ort(directoryHeader())
                + ") ist nicht lesbar: " + lastError(),
                prof_.directory_track, head_, directoryHeader().sectorId());
        abschluss();
        bericht.sortieren();
        return bericht;
    }
    if ((dir_hdr.type_byte & 0x40) == 0)
        b.addAt("udos.verz.kaputt", FsSeverity::Fehler, FsLayer::Verwaltung, "DIRECTORY",
                "Der Kopfsektor der Verzeichnisdatei weist sich nicht als Typ D aus",
                prof_.directory_track, head_, directoryHeader().sectorId());

    // Alle Sektoren, die das Dateisystem SELBST braucht — sie MUESSEN in der Karte
    // stehen.  Das ist der ableitbare Teil; die Systemspuren sind es nicht (s. o.).
    std::set<uint32_t> eigen;
    for (uint8_t s = 1; s <= 3 && s <= spt; ++s)
        eigen.insert(nr(UdosPointer{static_cast<uint8_t>(s - 1), prof_.bitmap_track}));
    eigen.insert(nr(directoryHeader()));

    std::vector<UdosPointer> dir_kette;
    if (!recordChain(dir_hdr, dir_kette))
        b.addAt("udos.verz.kette", FsSeverity::Fehler, FsLayer::Verwaltung, "DIRECTORY",
                "Die Satzkette der Verzeichnisdatei bricht ab: " + lastError(),
                prof_.directory_track, head_, directoryHeader().sectorId());
    else if (dir_kette.size() != dir_hdr.record_count)
        b.addAt("udos.verz.kette", FsSeverity::Fehler, FsLayer::Verwaltung, "DIRECTORY",
                "Die Verzeichnisdatei sagt " + std::to_string(dir_hdr.record_count)
                + " Saetze an, die Kette hat " + std::to_string(dir_kette.size()),
                prof_.directory_track, head_, directoryHeader().sectorId());

    const uint32_t dir_je_satz = std::max<uint32_t>(1u, dir_hdr.record_len / kSector);
    for (const UdosPointer& satz : dir_kette)
        for (uint32_t k = 0; k < dir_je_satz; ++k)
            eigen.insert(nr(UdosPointer{static_cast<uint8_t>(satz.sector_index + k),
                                        satz.track}));

    for (uint32_t s : eigen) {
        const uint8_t t = static_cast<uint8_t>(s / spt);
        const uint8_t i = static_cast<uint8_t>(s % spt);
        if (bitmap_.used(t, static_cast<uint8_t>(i + 1))) continue;
        FsFinding& f = b.addAt("udos.karte.system", FsSeverity::Gefahr, FsLayer::Verwaltung,
                "Belegungskarte",
                "Spur " + std::to_string(t) + " Sektor " + std::to_string(i + 1)
                + " traegt das Dateisystem selbst (Belegungskarte oder Verzeichnis),"
                  " steht aber als FREI — UDOS vergibt ihn beim naechsten Schreiben",
                t, head_, i + 1);
        FsRepair rep{"udos.karte.system.sperren",
                     "Spur " + std::to_string(t) + " Sektor " + std::to_string(i + 1)
                     + " in der Belegungskarte als belegt nachtragen",
                     /*datenverlust*/false, /*empfohlen*/true};
        rep.s = liste(UdosPointer{i, t});
        f.repairs.push_back(rep);
    }

    // ── Die Verzeichniseintraege ─────────────────────────────────────────────
    const std::vector<UdosDirEntry> verz = directory();
    std::map<std::string, int> namen;
    for (const UdosDirEntry& e : verz) {
        if (++namen[e.name] == 2)
            b.add("udos.verz.name", FsSeverity::Warnung, FsLayer::Verwaltung, e.name,
                  "Der Name '" + e.name + "' steht mehrfach im Verzeichnis");
        std::string w;
        if (!validName(e.name, &w))
            b.add("udos.verz.name", FsSeverity::Warnung, FsLayer::Verwaltung, e.name,
                  "'" + e.name + "': " + w);
    }

    if (level == FsCheckLevel::Schnell) {
        abschluss();
        bericht.begrenzen(kMaxJeKennung);
        bericht.sortieren();
        return bericht;
    }

    // ═══ Ebene Dateien (nur Vollpruefung) ════════════════════════════════════

    // Jeder Sektor, der zu einer Datei gehoert — Schluessel fuer Kreuzbelegung und
    // fuer den Abgleich mit der Karte.
    std::map<uint32_t, std::string> gehoert;
    for (uint32_t s : eigen) gehoert.emplace(s, "DIRECTORY");

    for (const UdosDirEntry& e : verz) {
        if (!brauche(e.header.track)) continue;

        UdosFileHeader hdr;
        if (!readHeader(e.header, hdr)) {
            FsFinding& f = b.addAt("udos.verz.eintrag_kaputt", FsSeverity::Fehler,
                    FsLayer::Dateien, e.name,
                    "Der Verzeichniseintrag zeigt auf " + ort(e.header)
                    + ", dort steht kein brauchbarer Kopfsektor: " + lastError(),
                    e.header.track, head_, e.header.sectorId());
            // Der Eintrag verweist ins Leere.  Herausschneiden macht das Verzeichnis
            // wieder stimmig; die Daten dahinter waren ohnehin nicht erreichbar und
            // bleiben fuer die Wiederherstellung liegen (§13).
            FsRepair rep{"udos.verz.eintrag.entfernen",
                         "Den Verzeichniseintrag '" + e.name + "' entfernen",
                         /*datenverlust*/true, /*empfohlen*/true};
            rep.s = e.name;
            f.repairs.push_back(rep);
            continue;
        }
        gehoert.emplace(nr(e.header), e.name);

        // ── Der Kopfsektor ───────────────────────────────────────────────────
        if (!(hdr.directory_sector == e.record)) {
            FsFinding& f = b.addAt("udos.kopf.rueckzeiger", FsSeverity::Warnung,
                    FsLayer::Dateien, e.name,
                    "Der Rueckwaertszeiger des Kopfsektors nennt " + ort(hdr.directory_sector)
                    + ", der Eintrag steht aber in " + ort(e.record),
                    e.header.track, head_, e.header.sectorId());
            FsRepair rep{"udos.kopf.rueckzeiger.neu",
                         "Den Rueckwaertszeiger des Kopfsektors auf " + ort(e.record)
                         + " setzen", /*datenverlust*/false, /*empfohlen*/true};
            rep.a = e.header.track; rep.b = e.header.sector_index;
            rep.c = e.record.track; rep.d = e.record.sector_index;
            f.repairs.push_back(rep);
        }
        // Die Segmentliste (Offset 40…121) endet mit `00 00 00 00`.  Laeuft sie bis
        // ans Ende durch, steht dort kein Abschluss — dann ist entweder Muell im
        // Kopfsektor, oder die Liste ist laenger, als er fassen kann.  Beides kostet
        // beim Zurueckschreiben Segmente, und eine Programmdatei ohne ihre Segmente
        // startet nicht (doc/udos_diskettenformat.md §6.3).
        if ((hdr.type_byte & 0x80) != 0 && hdr.segments.size() >= kUdosMaxSegments)
            b.addAt("udos.kopf.segmente", FsSeverity::Warnung, FsLayer::Dateien, e.name,
                    "Die Segmentliste des Kopfsektors hat keinen Abschluss "
                    "(00 00 00 00) — sie fuellt alle "
                    + std::to_string(kUdosMaxSegments) + " Plaetze",
                    e.header.track, head_, e.header.sectorId());
        if (hdr.typeName().empty())
            b.addAt("udos.kopf.typ", FsSeverity::Warnung, FsLayer::Dateien, e.name,
                    "Das Typbyte des Kopfsektors hat kein Typbit gesetzt",
                    e.header.track, head_, e.header.sectorId());
        if (hdr.bytes_in_last > hdr.record_len)
            b.addAt("udos.kopf.letzter", FsSeverity::Warnung, FsLayer::Dateien, e.name,
                    "„Bytes im letzten Satz" " = " + std::to_string(hdr.bytes_in_last)
                    + " ist groesser als die Satzlaenge " + std::to_string(hdr.record_len),
                    e.header.track, head_, e.header.sectorId());
        // §14: Der Lader traegt LOW/HIGH in die Nukleusvariablen und laesst den
        // Speicher zuteilen.  Steht dort FFFF, weist UDOS die Datei beim Starten mit
        // MEMORY PROTECT VIOLATION ab — die Datei ist also lesbar, aber unbrauchbar.
        if ((hdr.type_byte & 0x80) != 0) {
            if (hdr.low_addr == 0xFFFF || hdr.high_addr == 0xFFFF)
                b.addAt("udos.kopf.speicher", FsSeverity::Fehler, FsLayer::Dateien, e.name,
                        "Programmdatei ohne Speicherangabe (LOW/HIGH = FFFF) — UDOS"
                        " weist sie beim Starten mit MEMORY PROTECT VIOLATION ab",
                        e.header.track, head_, e.header.sectorId());
            else if (hdr.high_addr < hdr.low_addr)
                b.addAt("udos.kopf.speicher", FsSeverity::Fehler, FsLayer::Dateien, e.name,
                        "HIGH ADDRESS liegt vor LOW ADDRESS",
                        e.header.track, head_, e.header.sectorId());
        }

        // ── Die Satzkette ────────────────────────────────────────────────────
        const uint32_t je_satz = std::max<uint32_t>(1u, hdr.record_len / kSector);
        std::set<uint32_t>  besucht;
        // Der Rueckwaertszeiger des ERSTEN Satzes nennt den Kopfsektor, nicht das
        // Kettenende — die Kette beginnt beim Kopf (§1.2).  Mit FFFF als Startwert
        // meldete die Pruefung jede gesunde Datei der Referenzdiskette.
        UdosPointer p = hdr.first_record, vorher = e.header;
        uint32_t    saetze = 0;
        int         crc_kaputt = 0, ohne_nachspann = 0;
        UdosPointer erster_schaden{0xFF, 0xFF};
        bool        abgebrochen = false;

        while (!p.end()) {
            if (p.track >= tracks_ || p.sector_index >= spt) {
                b.addAt("udos.kette.ausserhalb", FsSeverity::Fehler, FsLayer::Dateien, e.name,
                        "Satz " + std::to_string(saetze + 1) + " zeigt auf " + ort(p)
                        + " — das liegt ausserhalb der Diskette ("
                        + std::to_string(tracks_) + " Spuren à " + std::to_string(spt) + ")",
                        e.header.track, head_, e.header.sectorId());
                abgebrochen = true;
                break;
            }
            if (!besucht.insert(nr(p)).second) {
                b.addAt("udos.kette.zyklus", FsSeverity::Fehler, FsLayer::Dateien, e.name,
                        "Die Satzkette laeuft im Kreis — bei " + ort(p)
                        + " schliesst sich die Schleife (nach " + std::to_string(saetze)
                        + " Saetzen)", p.track, head_, p.sectorId());
                abgebrochen = true;
                break;
            }
            if (!brauche(p.track)) { abgebrochen = true; break; }

            // Ein Satz belegt `Satzlaenge/128` physisch aufeinanderfolgende Sektoren
            // DERSELBEN Spur (§7) — er darf die Spurgrenze nicht ueberschreiten.
            if (p.sector_index + je_satz > spt) {
                b.addAt("udos.kette.spurwechsel", FsSeverity::Fehler, FsLayer::Dateien, e.name,
                        "Satz " + std::to_string(saetze + 1) + " beginnt bei " + ort(p)
                        + " und braucht " + std::to_string(je_satz)
                        + " Sektoren — das reicht ueber das Spurende hinaus",
                        p.track, head_, p.sectorId());
                abgebrochen = true;
                break;
            }

            UdosPointer back{0xFF, 0xFF}, fwd{0xFF, 0xFF};
            for (uint32_t k = 0; k < je_satz; ++k) {
                const UdosPointer q{static_cast<uint8_t>(p.sector_index + k), p.track};
                SectorData sec;
                if (!space_.readSector(q.track, head_, q.sectorId(), sec)) {
                    if (erster_schaden.end()) erster_schaden = q;
                    ++crc_kaputt;
                    continue;
                }
                if (!sec.ok()) {
                    if (erster_schaden.end()) erster_schaden = q;
                    ++crc_kaputt;
                }
                if (sec.tail.size() < 4) ++ohne_nachspann;
                else if (k == 0) {
                    back = UdosPointer::fromBytes(sec.tail.data());
                    fwd  = UdosPointer::fromBytes(sec.tail.data() + 2);
                }
                const auto [it, neu] = gehoert.emplace(nr(q), e.name);
                if (!neu && it->second != e.name)
                    b.addAt("udos.kette.doppelt", FsSeverity::Gefahr, FsLayer::Dateien, e.name,
                            ort(q) + " gehoert sowohl zu '" + it->second + "' als auch zu '"
                            + e.name + "' — wer als zweiter schreibt, zerstoert die Daten"
                              " des ersten", q.track, head_, q.sectorId());
            }

            // Die Kette ist DOPPELT verkettet; der Rueckwaertszeiger ist damit
            // ableitbar und eine echte Gegenprobe (und spaeter reparierbar).
            if (!(back == vorher)) {
                FsFinding& f = b.addAt("udos.kette.rueckwaerts", FsSeverity::Warnung,
                        FsLayer::Dateien, e.name,
                        "Satz " + std::to_string(saetze + 1) + " bei " + ort(p)
                        + " zeigt zurueck auf " + (back.end() ? std::string("das Kettenende")
                                                              : ort(back))
                        + ", davor liegt aber " + ort(vorher),
                        p.track, head_, p.sectorId());
                // Die Kette ist doppelt verkettet — der Rueckwaertszeiger ist damit
                // ableitbar.  Angefasst wird nur der Nachspann.
                FsRepair rep{"udos.kette.rueckwaerts.neu",
                             "Den Rueckwaertszeiger von " + ort(p) + " auf " + ort(vorher)
                             + " setzen", /*datenverlust*/false, /*empfohlen*/true};
                rep.a = p.track;      rep.b = p.sector_index;
                rep.c = vorher.track; rep.d = vorher.sector_index;
                f.repairs.push_back(rep);
            }
            vorher = p;
            p      = fwd;
            ++saetze;
            if (saetze > static_cast<uint32_t>(tracks_) * spt) { abgebrochen = true; break; }
        }

        if (!abgebrochen && saetze != hdr.record_count) {
            FsFinding& f = b.addAt("udos.kette.bruch",
                    saetze < hdr.record_count ? FsSeverity::Fehler : FsSeverity::Warnung,
                    FsLayer::Dateien, e.name,
                    "Der Kopfsektor sagt " + std::to_string(hdr.record_count)
                    + " Saetze an, die Kette hat " + std::to_string(saetze),
                    e.header.track, head_, e.header.sectorId());
            // Kuerzen macht die Datei wieder in sich stimmig — sie ist danach kuerzer.
            // Ohne einen einzigen erreichbaren Satz gibt es nichts zu kuerzen; dann
            // gehoert der Eintrag entfernt, und das schlaegt `eintrag_kaputt` vor.
            if (saetze > 0) {
                FsRepair rep{"udos.kette.kuerzen",
                             "Die Datei auf die erreichbaren " + std::to_string(saetze)
                             + " Saetze kuerzen (letzter Satz " + ort(vorher) + ")",
                             /*datenverlust*/true, /*empfohlen*/true};
                rep.a = e.header.track; rep.b = e.header.sector_index;
                rep.c = static_cast<int>(saetze);
                rep.s = liste(vorher);
                f.repairs.push_back(rep);
            }
        }
        if (crc_kaputt)
            b.addAt("udos.medium.crc", FsSeverity::Fehler, FsLayer::Medium, e.name,
                    std::to_string(crc_kaputt) + " Sektor(en) dieser Datei sind nicht"
                    " lesbar oder tragen eine falsche Pruefsumme, der erste bei "
                    + ort(erster_schaden),
                    erster_schaden.track, head_, erster_schaden.sectorId());
        if (ohne_nachspann)
            b.addAt("udos.medium.nachspann", FsSeverity::Gefahr, FsLayer::Medium, e.name,
                    std::to_string(ohne_nachspann) + " Sektor(en) dieser Datei haben"
                    " keinen Sektorkontrollblock hinter der Daten-CRC — dort laesst sich"
                    " die Verkettung nicht ablegen",
                    e.header.track, head_, e.header.sectorId());
    }

    // ── Karte gegen Ketten ───────────────────────────────────────────────────
    //
    // Der wichtigste Abgleich des ganzen Dateisystems.  Die eine Richtung ist
    // Gefahr (die Datei ist heil und wird beim naechsten Schreiben ueberschrieben),
    // die andere nur verlorener Platz.
    if (bericht.vollstaendig) {
        std::map<std::string, std::vector<UdosPointer>> offen;   // Datei → ihre Sektoren
        for (const auto& [s, wem] : gehoert) {
            const uint8_t t = static_cast<uint8_t>(s / spt);
            const uint8_t i = static_cast<uint8_t>(s % spt);
            if (bitmap_.used(t, static_cast<uint8_t>(i + 1))) continue;
            offen[wem].push_back(UdosPointer{i, t});
        }
        for (const auto& [wem, sektoren] : offen) {
            FsFinding& f = b.addAt("udos.karte.frei_aber_belegt", FsSeverity::Gefahr,
                    FsLayer::Dateien, wem,
                    std::to_string(sektoren.size()) + " Sektor(en) von '" + wem
                    + "' stehen in der Belegungskarte als FREI (der erste bei "
                    + ort(sektoren.front()) + ") — UDOS vergibt sie beim naechsten"
                      " Schreiben und zerstoert die Datei",
                    sektoren.front().track, head_, sektoren.front().sectorId());
            // Nachtragen nimmt nur und gibt nie — der Eingriff kann unter keinen
            // Umstaenden Daten freigeben und ist deshalb auch bei unvollstaendigem
            // Wissen erlaubt (§9.3).
            FsRepair rep{"udos.karte.sektoren.sperren",
                         "Die " + std::to_string(sektoren.size())
                         + " Sektor(en) von '" + wem
                         + "' in der Belegungskarte als belegt nachtragen",
                         /*datenverlust*/false, /*empfohlen*/true};
            rep.s = liste(sektoren);
            f.repairs.push_back(rep);
        }

        // Die Gegenrichtung: belegt, aber in keiner Kette.  Die Systemspuren bleiben
        // aussen vor — dort liegen Urlader und Bootabbild, die keiner Datei gehoeren
        // und trotzdem zu Recht belegt sind (§8.6).
        int verloren = 0;
        uint8_t erste_spur = 0;
        for (uint8_t t = 0; t < tracks_ && t < space_.trackCount(); ++t) {
            if (reservedTrack(t)) continue;
            for (uint8_t s = 1; s <= spt; ++s) {
                if (!bitmap_.used(t, s)) continue;
                if (gehoert.count(nr(UdosPointer{static_cast<uint8_t>(s - 1), t}))) continue;
                if (verloren++ == 0) erste_spur = t;
            }
        }
        if (verloren) {
            FsFinding& f = b.addAt("udos.karte.belegt_aber_frei", FsSeverity::Warnung,
                    FsLayer::Verwaltung, "Belegungskarte",
                    std::to_string(verloren) + " Sektoren stehen als belegt, gehoeren aber"
                    " zu keiner Datei (ab Spur " + std::to_string(erste_spur)
                    + ") — verlorener Platz; er laesst sich zurueckgewinnen, und dort"
                      " koennten geloeschte Dateien liegen",
                    erste_spur, head_);
            // Der wertvollste Eingriff ueberhaupt — und der einzige, der etwas
            // FREIGIBT.  Deshalb E8: er setzt voraus, dass die Ketten vollstaendig
            // gelesen und in sich heil sind; sonst erklaerte er die ungelesene
            // Haelfte fuer frei.  Was ihn sperrt, sind genau die Befunde, die
            // besagen „eine Kette liess sich nicht verfolgen" — ein falsches
            // Kartenbit (frei_aber_belegt) gehoert nicht dazu, das ist ja der
            // Schaden, den er behebt.
            std::string blockiert;
            for (const FsFinding& g : bericht.findings) {
                if (g.severity < FsSeverity::Fehler) continue;
                if (g.id.rfind("udos.kette.", 0) != 0 && g.id != "udos.verz.eintrag_kaputt"
                    && g.id != "udos.verz.kette" && g.id != "udos.karte.ungueltig") continue;
                blockiert = g.id + " (" + g.object + ")";
                break;
            }
            FsRepair rep{"udos.karte.neu",
                         "Den Belegungsplan vollstaendig aus den Ketten neu aufbauen —"
                         " das gibt die " + std::to_string(verloren)
                         + " verlorenen Sektoren zurueck",
                         /*datenverlust*/false, /*empfohlen*/blockiert.empty()};
            if (!blockiert.empty()) {
                rep.gesperrt = true;
                rep.warum    = "Der Neuaufbau ist erst moeglich, wenn kein Kettenfehler"
                               " mehr offen ist — offen ist " + blockiert;
            }
            f.repairs.push_back(rep);
        }
    }

    abschluss();
    bericht.begrenzen(kMaxJeKennung);
    bericht.sortieren();
    return bericht;
}
