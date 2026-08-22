/**
 * @file udos1715_check.cpp
 * @brief Die Pruefung des UDOS1715-/NDOS-Dateisystems (PC 1715, P8000).
 *
 * Umsetzung von `doc/design/15_dateisystempruefung.md` §10.  Dieselbe
 * Betriebssystemfamilie wie ZDOS, aber der µPD765 erreicht die Bytes hinter der
 * Daten-CRC nicht: die Verkettung steht in eigenen **Zeigersektoren** (je bis zu
 * 125 Adressen, untereinander verkettet, `FIRSTBL` im Descriptor bei `80H`).
 *
 * Was das fuer die Pruefung aendert:
 *
 * * **Die Zeigersektoren sind selbst Sektoren** und muessen mitgezaehlt werden —
 *   in der Kreuzbelegung wie im Abgleich mit dem Belegungsplan.
 * * **Die Kette ist doppelt verkettet** (`BCKZGR`/`FORZGR`), also ableitbar und
 *   spaeter reparierbar — genau wie der Sektorkontrollblock bei ZDOS.
 * * **Eine Spur ist der ganze Zylinder** (32 Sektoren, `UDOS-Sektor = (ID−1) +
 *   Kopf·16`).  Ein Record darf deshalb die **Kopf**grenze ueberschreiten (`CAT`
 *   tut es), die **Spur**grenze nicht.
 *
 * Und ein Unterschied bei den Zaehlern: bei NDOS sind **beide** echt (§3.1 des
 * Formatentwurfs), waehrend ZDOS den „belegt"-Zaehler als Festwert 2464 − frei
 * bildet.  Hier lohnt sich also auch die zweite Gegenprobe.
 *
 * @see doc/design/15_dateisystempruefung.md §10 · doc/udos1715_diskettenformat.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/check/fs_check.h"
#include "core/filesystem/udos/udos1715_fs.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr size_t kMaxJeKennung = 20;

std::string ort(UdosPointer p) {
    return "Spur " + std::to_string(p.track) + " Sektor " + std::to_string(p.sector_index);
}

/// @brief Sektorliste fuer @ref FsRepair::s — `"Spur:Index,…"` (Verabredung mit
///        `udos1715_fs.cpp`, dort am `repair`-Haken in einer Tabelle festgehalten).
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

FsCheckReport Udos1715FileSystem::check(FsCheckLevel level, bool nachladen) const {
    FsCheckReport bericht;
    bericht.level = level;
    FsFindings b(bericht);

    const uint8_t spt = secs_per_track_;
    // Buchfuehrung wie bei ZDOS: `gewollt` sind die Spuren, in die die Pruefung
    // sehen wollte, `da` die davon verfuegbaren.  Eine „Spur" sind hier BEIDE
    // Koepfe eines Zylinders (§1.1) — bekannt ist sie nur, wenn beide da sind.
    std::set<uint8_t> gewollt, da;
    auto brauche = [&](uint8_t t) {
        gewollt.insert(t);
        if (!nachladen && !(space_.trackKnown(t, 0)
                            && (phys_spt_ == spt || space_.trackKnown(t, 1)))) {
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
    auto nr = [&](UdosPointer p) {
        return static_cast<uint32_t>(p.track) * spt + p.sector_index;
    };

    // ═══ Ebene Verwaltung ════════════════════════════════════════════════════
    //
    // Die Kartenspur ist beim Mounten schon gelesen worden — sie wird hier nur noch
    // gebucht, damit die Spurzaehler stimmen.
    brauche(prof_.bitmap_track);

    bericht.schritt("udos.schritt.karte",
                    "Belegungsplan: Plausibilitaet, Geometrie und beide Zaehler");
    std::string warum;
    if (!bitmap_.looksValid(spt, tracks_, &warum))
        b.addAt("udos.karte.ungueltig", FsSeverity::Fehler, FsLayer::Verwaltung,
                "Belegungsplan",
                "Der Belegungsplan auf Spur " + std::to_string(prof_.bitmap_track)
                + " ist nicht plausibel: " + warum, prof_.bitmap_track, 0, 1);

    if (bitmap_.sectorsPerTrack() != spt)
        b.addAt("udos.karte.geometrie", FsSeverity::Fehler, FsLayer::Verwaltung,
                "Belegungsplan",
                "Der Plan nennt " + std::to_string(bitmap_.sectorsPerTrack())
                + " Sektoren je Spur, gemessen sind " + std::to_string(spt),
                prof_.bitmap_track, 0, 1);

    // Bei NDOS sind BEIDE Zaehler echt — anders als bei ZDOS.
    const FsRepair zaehler_neu{"udos.karte.zaehler.neu",
        "Frei- und Belegtzaehler auf die ausgezaehlten Werte "
        + std::to_string(bitmap_.countFree()) + " / " + std::to_string(bitmap_.countUsed())
        + " setzen", /*datenverlust*/false, /*empfohlen*/true};
    if (bitmap_.storedFree() != bitmap_.countFree())
        b.addAt("udos.karte.zaehler", FsSeverity::Warnung, FsLayer::Verwaltung,
                "Belegungsplan",
                "Der Freizaehler sagt " + std::to_string(bitmap_.storedFree())
                + ", ausgezaehlt sind " + std::to_string(bitmap_.countFree())
                + " Sektoren", prof_.bitmap_track, 0).repairs.push_back(zaehler_neu);
    if (bitmap_.storedUsed() != bitmap_.countUsed())
        b.addAt("udos.karte.zaehler", FsSeverity::Warnung, FsLayer::Verwaltung,
                "Belegungsplan",
                "Der Belegtzaehler sagt " + std::to_string(bitmap_.storedUsed())
                + ", ausgezaehlt sind " + std::to_string(bitmap_.countUsed())
                + " Sektoren", prof_.bitmap_track, 0).repairs.push_back(zaehler_neu);

    // ── Die Verzeichnisdatei ─────────────────────────────────────────────────
    bericht.schritt("udos.schritt.verzeichnisdatei",
                    "Verzeichnisdatei: Descriptor, Typ und Zeigersektoren");
    if (!brauche(prof_.directory_track)) {
        b.add("udos.verz.ungelesen", FsSeverity::Info, FsLayer::Verwaltung, "",
              "Die Verzeichnisspur ist noch nicht gelesen — geprueft wurde nichts");
        abschluss();
        bericht.sortieren();
        return bericht;
    }
    UdosFileHeader dir_hdr;
    if (!readDescriptor(directoryDescriptor(), dir_hdr)) {
        b.addAt("udos.verz.kaputt", FsSeverity::Fehler, FsLayer::Verwaltung, "DIRECTORY",
                "Der Descriptor der Verzeichnisdatei (" + ort(directoryDescriptor())
                + ") ist nicht lesbar: " + lastError(), prof_.directory_track, headOf(directoryDescriptor()),
                                   idOf(directoryDescriptor()));
        abschluss();
        bericht.sortieren();
        return bericht;
    }
    if ((dir_hdr.type_byte & 0x40) == 0)
        b.addAt("udos.verz.kaputt", FsSeverity::Fehler, FsLayer::Verwaltung, "DIRECTORY",
                "Der Descriptor der Verzeichnisdatei weist sich nicht als Typ D aus",
                prof_.directory_track, headOf(directoryDescriptor()),
                                   idOf(directoryDescriptor()));

    // Was das Dateisystem SELBST belegt — das muss im Plan stehen.  Die Systemspuren
    // (Urlader, BFOS) gehoeren NICHT dazu: ob es sie gibt, ist eine Eigenschaft der
    // Diskette, nicht des Dateisystems (bei ZDOS gilt dasselbe).
    std::set<uint32_t> eigen;
    for (uint8_t s = 1; s <= 2; ++s)
        eigen.insert(nr(UdosPointer{static_cast<uint8_t>(s - 1), prof_.bitmap_track}));
    std::vector<UdosPointer> dir_sektoren;
    if (!sectorsOfFile(directoryDescriptor(), dir_sektoren))
        b.addAt("udos.verz.kette", FsSeverity::Fehler, FsLayer::Verwaltung, "DIRECTORY",
                "Die Zeigersektoren der Verzeichnisdatei sind nicht schluessig: "
                + lastError(), prof_.directory_track, headOf(directoryDescriptor()),
                                   idOf(directoryDescriptor()));
    for (const UdosPointer& p : dir_sektoren) eigen.insert(nr(p));

    for (uint32_t s : eigen) {
        const uint8_t t = static_cast<uint8_t>(s / spt);
        const uint8_t i = static_cast<uint8_t>(s % spt);
        if (bitmap_.used(t, static_cast<uint8_t>(i + 1))) continue;
        FsFinding& f = b.addAt("udos.karte.system", FsSeverity::Gefahr, FsLayer::Verwaltung,
                "Belegungsplan",
                "Spur " + std::to_string(t) + " Sektor " + std::to_string(i)
                + " traegt das Dateisystem selbst (Belegungsplan oder Verzeichnis),"
                  " steht aber als FREI — NDOS vergibt ihn beim naechsten Schreiben",
                t, headOf(UdosPointer{i, t}), idOf(UdosPointer{i, t}));
        FsRepair rep{"udos.karte.system.sperren",
                     "Spur " + std::to_string(t) + " Sektor " + std::to_string(i)
                     + " im Belegungsplan als belegt nachtragen",
                     /*datenverlust*/false, /*empfohlen*/true};
        rep.s = liste(UdosPointer{i, t});
        f.repairs.push_back(rep);
    }

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
        bericht.schrittEntfaellt("udos.schritt.dateien",
                                 "Jede Datei: Descriptor, Zeigerkette und Datenrecords",
                                 "nur bei der Vollpruefung");
        bericht.schrittEntfaellt("udos.schritt.karte_gegen_ketten",
                                 "Plan gegen Zeigersektoren: belegte Sektoren, die als "
                                 "frei gefuehrt sind", "nur bei der Vollpruefung");
        abschluss();
        bericht.begrenzen(kMaxJeKennung);
        bericht.sortieren();
        return bericht;
    }

    // ═══ Ebene Dateien (nur Vollpruefung) ════════════════════════════════════

    bericht.schritt("udos.schritt.dateien",
                    "Jede Datei: Descriptor, Zeigerkette und Datenrecords");
    std::map<uint32_t, std::string> gehoert;
    for (uint32_t s : eigen) gehoert.emplace(s, "DIRECTORY");

    for (const UdosDirEntry& e : verz) {
        if (!brauche(e.header.track)) continue;

        UdosFileHeader hdr;
        if (!readDescriptor(e.header, hdr)) {
            b.addAt("udos.verz.eintrag_kaputt", FsSeverity::Fehler, FsLayer::Dateien, e.name,
                    "Der Verzeichniseintrag zeigt auf " + ort(e.header)
                    + ", dort steht kein brauchbarer Descriptor: " + lastError(),
                    e.header.track, headOf(e.header), idOf(e.header));
            continue;
        }
        gehoert.emplace(nr(e.header), e.name);

        // ── Der Descriptor ───────────────────────────────────────────────────
        // Die Segmentliste (Offset 40…121) endet mit `00 00 00 00`.  Laeuft sie bis
        // ans Ende durch, steht dort kein Abschluss — dann ist entweder Muell im
        // Kopfsektor, oder die Liste ist laenger, als er fassen kann.  Beides kostet
        // beim Zurueckschreiben Segmente, und eine Programmdatei ohne ihre Segmente
        // startet nicht (doc/udos_diskettenformat.md §6.3).
        if ((hdr.type_byte & 0x80) != 0 && hdr.segments.size() >= kUdosMaxSegments)
            b.addAt("udos.kopf.segmente", FsSeverity::Warnung, FsLayer::Dateien, e.name,
                    "Die Segmentliste des Descriptors hat keinen Abschluss "
                    "(00 00 00 00) — sie fuellt alle "
                    + std::to_string(kUdosMaxSegments) + " Plaetze",
                    e.header.track, headOf(e.header), idOf(e.header));
        if (hdr.typeName().empty())
            b.addAt("udos.kopf.typ", FsSeverity::Warnung, FsLayer::Dateien, e.name,
                    "Das Typbyte des Descriptors hat kein Typbit gesetzt", e.header.track, headOf(e.header), idOf(e.header));
        if (hdr.bytes_in_last > hdr.record_len)
            b.addAt("udos.kopf.letzter", FsSeverity::Warnung, FsLayer::Dateien, e.name,
                    "„Bytes im letzten Satz" " = " + std::to_string(hdr.bytes_in_last)
                    + " ist groesser als die Satzlaenge " + std::to_string(hdr.record_len),
                    e.header.track, headOf(e.header), idOf(e.header));
        if ((hdr.type_byte & 0x80) != 0 &&
            (hdr.low_addr == 0xFFFF || hdr.high_addr == 0xFFFF))
            b.addAt("udos.kopf.speicher", FsSeverity::Fehler, FsLayer::Dateien, e.name,
                    "Programmdatei ohne Speicherangabe (LOW/HIGH = FFFF) — sie laesst"
                    " sich nicht starten", e.header.track, headOf(e.header), idOf(e.header));

        if (hdr.firstbl.end() || hdr.firstbl.track >= tracks_
            || hdr.firstbl.sector_index >= spt) {
            b.addAt("ndos.firstbl", FsSeverity::Fehler, FsLayer::Dateien, e.name,
                    "FIRSTBL nennt " + (hdr.firstbl.end() ? std::string("FFFF")
                                                          : ort(hdr.firstbl))
                    + " — dort kann kein Zeigersektor liegen", e.header.track, headOf(e.header), idOf(e.header));
            continue;
        }

        // ── Die Zeigersektorkette ────────────────────────────────────────────
        std::vector<UdosPointer>          adressen;
        std::vector<Udos1715PointerBlock> bloecke;
        if (!pointerBlocks(hdr, adressen, bloecke)) {
            b.addAt("ndos.zeiger.kette", FsSeverity::Fehler, FsLayer::Dateien, e.name,
                    "Die Zeigersektorkette ist nicht lesbar: " + lastError(),
                    hdr.firstbl.track, headOf(hdr.firstbl), idOf(hdr.firstbl));
            continue;
        }

        // Vor- und Rueckwaertszeiger sind redundant und damit eine echte Gegenprobe:
        // der erste Block zeigt auf den Descriptor zurueck, jeder weitere auf seinen
        // Vorgaenger.
        UdosPointer bp = hdr.firstbl, davor = e.header;
        for (const Udos1715PointerBlock& blk : bloecke) {
            gehoert.emplace(nr(bp), e.name);
            if (!(blk.back == davor)) {
                FsFinding& f = b.addAt("ndos.zeiger.kette", FsSeverity::Warnung,
                        FsLayer::Dateien, e.name,
                        "Der Zeigersektor " + ort(bp) + " zeigt zurueck auf "
                        + (blk.back.end() ? std::string("FFFF") : ort(blk.back))
                        + ", davor liegt aber " + ort(davor), bp.track, 0);
                // Vor- und Rueckwaertszeiger sind redundant — die Reihenfolge der
                // Bloecke steht bereits fest, also sind sie ableitbar.
                FsRepair rep{"ndos.zeiger.kette.neu",
                             "Die Vor- und Rueckwaertszeiger der Zeigersektoren von '"
                             + e.name + "' aus ihrer Reihenfolge neu schreiben",
                             /*datenverlust*/false, /*empfohlen*/true};
                rep.s = e.name;
                f.repairs.push_back(rep);
            }
            davor = bp;
            bp    = blk.forward;
        }

        if (adressen.empty() || !(adressen.front() == e.header))
            b.addAt("ndos.zeiger.kette", FsSeverity::Warnung, FsLayer::Dateien, e.name,
                    "Die erste Adresse des ersten Zeigersektors muesste der Descriptor "
                    + ort(e.header) + " sein",
                    hdr.firstbl.track, headOf(hdr.firstbl), idOf(hdr.firstbl));

        const size_t daten = adressen.empty() ? 0 : adressen.size() - 1;
        if (daten != hdr.record_count) {
            FsFinding& f = b.addAt("ndos.zeiger.anzahl",
                    daten < hdr.record_count ? FsSeverity::Fehler : FsSeverity::Warnung,
                    FsLayer::Dateien, e.name,
                    "Der Descriptor sagt " + std::to_string(hdr.record_count)
                    + " Saetze an, die Zeigersektoren nennen " + std::to_string(daten)
                    + " Adressen", e.header.track, headOf(e.header), idOf(e.header));
            // Die Adressen sind die Wahrheit — sie zeigen auf wirklich vorhandene
            // Sektoren; die Satzzahl im Descriptor ist nur ihre Gegenprobe.
            FsRepair rep{"ndos.zeiger.anzahl.anpassen",
                         "Die Satzzahl im Descriptor auf " + std::to_string(daten)
                         + " setzen — so viele Adressen stehen in den Zeigersektoren",
                         /*datenverlust*/daten < hdr.record_count, /*empfohlen*/true};
            rep.a = e.header.track; rep.b = e.header.sector_index;
            rep.c = static_cast<int>(daten);
            f.repairs.push_back(rep);
        }

        // ── Die Datenrecords ─────────────────────────────────────────────────
        const uint32_t je_rec = sectorsPerRecord(hdr.record_len);
        int crc_kaputt = 0;
        UdosPointer erster_schaden{0xFF, 0xFF};

        for (size_t i = 1; i < adressen.size(); ++i) {
            const UdosPointer a = adressen[i];
            if (a.track >= tracks_ || a.sector_index >= spt) {
                b.addAt("ndos.zeiger.ausserhalb", FsSeverity::Fehler, FsLayer::Dateien, e.name,
                        "Satz " + std::to_string(i) + " nennt " + ort(a)
                        + " — das liegt ausserhalb der Diskette", e.header.track, headOf(e.header), idOf(e.header));
                continue;
            }
            // Die KOPFgrenze darf ein Record ueberschreiten (die Spur ist der ganze
            // Zylinder), die Spurgrenze nicht.
            if (a.sector_index + je_rec > spt) {
                b.addAt("ndos.satz.spurwechsel", FsSeverity::Fehler, FsLayer::Dateien, e.name,
                        "Satz " + std::to_string(i) + " beginnt bei " + ort(a)
                        + " und braucht " + std::to_string(je_rec)
                        + " Sektoren — das reicht ueber das Spurende hinaus", a.track, 0);
                continue;
            }
            if (!brauche(a.track)) continue;

            for (uint32_t k = 0; k < je_rec; ++k) {
                const UdosPointer q{static_cast<uint8_t>(a.sector_index + k), a.track};
                const auto [it, neu] = gehoert.emplace(nr(q), e.name);
                if (!neu && it->second != e.name)
                    b.addAt("ndos.zeiger.doppelt", FsSeverity::Gefahr, FsLayer::Dateien, e.name,
                            ort(q) + " gehoert sowohl zu '" + it->second + "' als auch zu '"
                            + e.name + "' — wer als zweiter schreibt, zerstoert die Daten"
                              " des ersten", q.track, 0);
                SectorData sec;
                if (!space_.readSector(q.track, headOf(q), idOf(q), sec) || !sec.ok()) {
                    if (erster_schaden.end()) erster_schaden = q;
                    ++crc_kaputt;
                }
            }
        }
        if (crc_kaputt)
            b.addAt("udos.medium.crc", FsSeverity::Fehler, FsLayer::Medium, e.name,
                    std::to_string(crc_kaputt) + " Sektor(en) dieser Datei sind nicht"
                    " lesbar oder tragen eine falsche Pruefsumme, der erste bei "
                    + ort(erster_schaden), erster_schaden.track, 0);
    }

    // ── Plan gegen Zeigersektoren ────────────────────────────────────────────
    if (bericht.vollstaendig) {
        bericht.schritt("udos.schritt.karte_gegen_ketten",
                        "Plan gegen Zeigersektoren: belegte Sektoren, die als frei "
                        "gefuehrt sind");
        std::map<std::string, std::vector<UdosPointer>> offen;
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
                    + "' stehen im Belegungsplan als FREI (der erste bei "
                    + ort(sektoren.front()) + ") — NDOS vergibt sie beim naechsten"
                      " Schreiben und zerstoert die Datei",
                    sektoren.front().track, headOf(sektoren.front()),
                                   idOf(sektoren.front()));
            FsRepair rep{"udos.karte.sektoren.sperren",
                         "Die " + std::to_string(sektoren.size())
                         + " Sektor(en) von '" + wem
                         + "' im Belegungsplan als belegt nachtragen",
                         /*datenverlust*/false, /*empfohlen*/true};
            rep.s = liste(sektoren);
            f.repairs.push_back(rep);
        }

        int verloren = 0;
        uint8_t erste_spur = 0;
        for (uint8_t t = 0; t < tracks_; ++t) {
            // Systemspuren bleiben aussen vor: Spur 0 traegt auf einer
            // Systemdiskette Urlader und BFOS, und der P8000 sperrt zusaetzlich
            // Kopf 0 der Bootspur — beides gehoert keiner Datei und ist trotzdem
            // zu Recht belegt.
            if (reservedTrack(t) || t == 0 || t == prof_.boot_track) continue;
            for (uint8_t s = 1; s <= spt; ++s) {
                if (!bitmap_.used(t, s)) continue;
                if (gehoert.count(nr(UdosPointer{static_cast<uint8_t>(s - 1), t}))) continue;
                if (verloren++ == 0) erste_spur = t;
            }
        }
        if (verloren) {
            FsFinding& f = b.addAt("udos.karte.belegt_aber_frei", FsSeverity::Warnung,
                    FsLayer::Verwaltung, "Belegungsplan",
                    std::to_string(verloren) + " Sektoren stehen als belegt, gehoeren aber"
                    " zu keiner Datei (ab Spur " + std::to_string(erste_spur)
                    + ") — verlorener Platz; dort koennten geloeschte Dateien liegen",
                    erste_spur, 0);
            // E8: der einzige Eingriff, der etwas FREIGIBT.  Gesperrt, solange auch
            // nur eine Zeigersektorkette nicht zu verfolgen war — sonst erklaerte er
            // deren Sektoren fuer frei.
            std::string blockiert;
            for (const FsFinding& g : bericht.findings) {
                if (g.severity < FsSeverity::Fehler) continue;
                if (g.id.rfind("ndos.", 0) != 0 && g.id != "udos.verz.eintrag_kaputt"
                    && g.id != "udos.verz.kette" && g.id != "udos.karte.ungueltig") continue;
                blockiert = g.id + " (" + g.object + ")";
                break;
            }
            FsRepair rep{"udos.karte.neu",
                         "Den Belegungsplan vollstaendig aus den Zeigersektoren neu"
                         " aufbauen — das gibt die " + std::to_string(verloren)
                         + " verlorenen Sektoren zurueck",
                         /*datenverlust*/false, /*empfohlen*/blockiert.empty()};
            if (!blockiert.empty()) {
                rep.gesperrt = true;
                rep.warum    = "Der Neuaufbau ist erst moeglich, wenn kein Fehler an den"
                               " Zeigersektoren mehr offen ist — offen ist " + blockiert;
            }
            f.repairs.push_back(rep);
        }
    }

    // ═══ Ebene Medium: der Reihenlauf ueber ALLES ════════════════════════════
    //
    // Wie bei ZDOS (s. `udos_check.cpp`): bis hierher ist nur geprueft, was in einer
    // Zeigersektorkette steht.  Ein schadhafter Sektor ausserhalb jeder Datei ist
    // aber die Stelle, an der geloeschte Dateien liegen (§13) und an die als
    // naechstes geschrieben wird.
    {
        int frei_kaputt = 0;
        UdosPointer erster{0xFF, 0xFF};
        for (uint8_t t = 0; t < tracks_ && t < space_.trackCount(); ++t) {
            if (!brauche(t)) continue;
            for (uint8_t i = 0; i < spt; ++i) {
                const UdosPointer p{i, t};
                if (gehoert.count(nr(p))) continue;
                // Die Spur umfasst BEIDE Seiten (§1.1) — ist die zweite Haelfte gar
                // nicht da (einseitige Diskette), gibt es diese Sektoren nicht, und
                // ihr Fehlen ist keine Auffaelligkeit.
                if (headOf(p) >= space_.format().numHeads()) continue;
                if (!space_.trackFormatted(t, headOf(p))) continue;
                SectorData sec;
                // Ein fehlender Sektor ausserhalb jeder Datei ist kein Befund —
                // Begruendung im Gegenstueck in `udos_check.cpp` (E10).
                if (!space_.readSector(t, headOf(p), idOf(p), sec)) continue;
                if (sec.ok()) continue;
                if (erster.end()) erster = p;
                ++frei_kaputt;
            }
        }
        if (frei_kaputt)
            b.addAt("udos.medium.frei_kaputt", FsSeverity::Warnung, FsLayer::Medium,
                    "Medium",
                    std::to_string(frei_kaputt) + " Sektor(en) ausserhalb jeder Datei"
                    " tragen eine falsche Pruefsumme, der erste auf Spur " + std::to_string(erster.track) + " Sektor "
                    + std::to_string(erster.sector_index) + " — verloren ist dort"
                      " nichts, aber eine geloeschte Datei waere von dort nicht mehr"
                      " zu retten",
                    erster.track, headOf(erster), idOf(erster));
    }

    abschluss();
    bericht.begrenzen(kMaxJeKennung);
    if (!bericht.vollstaendig)
        bericht.schrittEntfaellt("udos.schritt.karte_gegen_ketten",
                                 "Plan gegen Zeigersektoren: belegte Sektoren, die als "
                                 "frei gefuehrt sind",
                                 "das Speicherabbild ist unvollstaendig — der Abgleich "
                                 "braucht jede Spur");
    bericht.schrittEnde();
    bericht.sortieren();
    return bericht;
}
