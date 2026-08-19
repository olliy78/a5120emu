/**
 * @file cpm_check.cpp
 * @brief Die Pruefung des CP/M-Dateisystems (CP/A, SCPX, SCP1700).
 *
 * Umsetzung von `doc/design/15_dateisystempruefung.md` §7.  Sie steht bewusst NICHT
 * in `cpm_fs.cpp`: dort ist der Betrieb zu Hause, hier die Diagnose.  Es ist trotzdem
 * eine Methode von @ref CpmFileSystem — sie braucht die Innenansicht (Blockgroesse,
 * Verzeichnisbloecke, Versatztabelle), und niemand ausserhalb soll sie bekommen.
 *
 * ### Die Eigenheit, die alles praegt
 * **CP/M fuehrt keinen gespeicherten Belegungsplan.**  @ref CpmFileSystem::allocationMap
 * baut ihn bei jedem Mounten aus dem Verzeichnis neu.  Damit sind die beiden
 * klassischen `fsck`-Befunde „Block belegt, gehoert aber niemandem" und „Block gehoert
 * einer Datei, ist aber frei" **strukturell unmoeglich** — es gibt nichts, womit man
 * das Verzeichnis abgleichen koennte.  Umgekehrt kann CP/M etwas, das UDOS nicht kann:
 * **denselben Block in zwei Dateien**, und das ohne jede Warnung des Betriebssystems.
 * Das ist hier der eine Befund mit Schwere @c Gefahr.
 *
 * Weil ausserdem alles, was eine Datei ausmacht, IM Verzeichnis steht, faellt bei CP/M
 * die Ebene @ref FsLayer::Dateien fast vollstaendig mit der Ebene
 * @ref FsLayer::Verwaltung zusammen.  Was die Vollpruefung zusaetzlich bringt, ist
 * deshalb die **Medienebene**: jeden Sektor des Datenbereichs anfassen und einen
 * CRC-Fehler auf die Datei zurueckrechnen, der er gehoert.
 *
 * @see doc/design/15_dateisystempruefung.md §7
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/check/fs_check.h"
#include "core/filesystem/cpm/cpm_fs.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

namespace {

constexpr uint32_t kRecordBytes = 128;

/// @brief "NAME    TYP" aus einem rohen Platz, ohne die Attributhochbits.
std::string rohName(const uint8_t* p) {
    std::string name, typ;
    for (int i = 0; i < 8; ++i) name += static_cast<char>(p[1 + i] & 0x7F);
    for (int i = 0; i < 3; ++i) typ  += static_cast<char>(p[9 + i] & 0x7F);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!typ.empty()  && typ.back()  == ' ') typ.pop_back();
    return typ.empty() ? name : name + "." + typ;
}

/// @brief Bezeichnung eines Platzes fuer die Spalte „Ort" des Berichts.
std::string platz(int i) { return "Platz " + std::to_string(i); }

/// @brief Bezeichnung einer Datei mit Nutzerbereich ("3:TEST.COM").
std::string datei(int user, const std::string& name) {
    return user == 0 ? name : std::to_string(user) + ":" + name;
}

/**
 * @brief Wozu dient ein Nutzerbyte jenseits von 0…15?
 *
 * CP/M 3 und die Zeitstempelerweiterungen benutzen denselben Verzeichnisbereich mit
 * eigenen Kennungen.  Dieses Werkzeug wertet sie nicht aus (@ref CpmFileSystem
 * ueberspringt sie, `user > 15`) — sie sind aber **kein Schaden**, und ein `fsck`,
 * das sie als Fehler meldet, wird zu Recht weggeklickt.
 * @return Klartext oder "" fuer „unbekannt".
 */
const char* sonderplatz(uint8_t user) {
    if (user >= 16 && user <= 31) return "Kennwortsatz (CP/M 3)";
    if (user == 0x20)             return "Datentraegeretikett";
    if (user == 0x21)             return "Zeitstempelsatz";
    return "";
}

// ─── Reparaturvorschlaege (§8) ───────────────────────────────────────────────
//
// Die Parameter sind namenlos; ihre Bedeutung steht in der Tabelle am
// `repair`-Haken in `cpm_fs.h`.  Sie hier zu bauen statt an jeder Fundstelle haelt
// Kennung, Text und Parameter an EINER Stelle zusammen.

/// @brief Nutzerbyte auf 0xE5 — der Rest des Platzes bleibt lesbar (§13).
FsRepair platz_freigeben(int index) {
    FsRepair r{"cpm.platz.freigeben",
               "Verzeichnisplatz " + std::to_string(index) + " freigeben (Nutzerbyte"
               " 0xE5).  Der Eintrag bleibt lesbar und ist wiederherstellbar",
               /*datenverlust*/true, /*empfohlen*/true};
    r.a = index;
    return r;
}

/// @brief Unbrauchbare Blockzeiger nullen, ab dort abschneiden und `RC` nachziehen.
///
/// Abgeschnitten wird, weil ein Loch mitten in der Zeigerliste jeden folgenden
/// Satz verschoebe — der Extent lieferte danach falsche Daten aus, statt weniger.
FsRepair zeiger_streichen(int index) {
    FsRepair r{"cpm.zeiger.streichen",
               "Platz " + std::to_string(index) + " ab dem ersten unbrauchbaren"
               " Blockzeiger abschneiden und die Satzzahl nachziehen",
               /*datenverlust*/true, /*empfohlen*/true};
    r.a = index;
    return r;
}

}  // namespace

FsCheckReport CpmFileSystem::check(FsCheckLevel level, bool nachladen) const {
    FsCheckReport bericht;
    bericht.level = level;
    FsFindings b(bericht);

    const int start = space_.trackIndexOf(prof_.data_cyl, prof_.data_head);
    if (start < 0) {
        b.add("cpm.bereich.fehlt", FsSeverity::Fehler, FsLayer::Verwaltung, "",
              "Der Datenbereich c" + std::to_string(prof_.data_cyl) + "h"
              + std::to_string(prof_.data_head) + " liegt nicht im Sektorraum");
        return bericht;
    }

    const uint64_t track_bytes = static_cast<uint64_t>(secs_per_track_) * sector_size_;
    const size_t   tracks      = space_.trackCount() - static_cast<size_t>(start);

    // Umkehrung der Versatztabelle: physischer Index → logischer.  Gebraucht wird
    // sie, um von einem schadhaften Sektor auf den Byte-Offset und damit auf die
    // Datei zu kommen, der er gehoert.
    std::vector<uint8_t> zurueck(secs_per_track_, 0);
    for (uint16_t i = 0; i < secs_per_track_; ++i) zurueck[skew_tab_[i]] = static_cast<uint8_t>(i);

    // Was die Pruefung anfasst — bei Schnell nur die Spuren des Verzeichnisses.
    const uint64_t dir_bytes = static_cast<uint64_t>(prof_.dir_entries) * 32;
    const size_t   dir_tracks =
        static_cast<size_t>((dir_bytes + track_bytes - 1) / track_bytes);
    const size_t   bis = (level == FsCheckLevel::Voll) ? tracks
                                                       : std::min(dir_tracks, tracks);

    // Gezaehlt wird, was DIESE Prueftiefe ansehen will — nicht, was die Diskette
    // hat.  Sonst meldete eine vollstaendige Schnellpruefung „1 von 156 Spuren
    // angesehen" und saehe unvollstaendig aus, obwohl sie alles hat, was sie
    // braucht.  Bei der Vollpruefung kommen die Systemspuren hinzu.
    bericht.spuren_gesamt = static_cast<int>(level == FsCheckLevel::Voll
                                             ? space_.trackCount() : bis);

    // ── Ist ueberhaupt zu lesen, was zu lesen ist? ───────────────────────────
    //
    // An einer physischen Diskette darf die Pruefung nicht von sich aus Spuren
    // holen (E2).  Fehlt eine, wird sie uebersprungen und der Bericht sagt es —
    // „ohne Befund" hiesse sonst zu viel (E8).
    std::vector<bool> spur_da(bis, true);
    for (size_t i = 0; i < bis; ++i) {
        const SectorSpace::TrackRef t = space_.trackAt(static_cast<size_t>(start) + i);
        if (nachladen || space_.trackKnown(t.cyl, t.head)) { ++bericht.spuren_gelesen; continue; }
        spur_da[i]            = false;
        bericht.vollstaendig  = false;
    }
    if (!spur_da.empty() && !spur_da[0]) {
        // Ohne die erste Spur gibt es kein Verzeichnis und damit gar keine Pruefung.
        b.add("cpm.verz.ungelesen", FsSeverity::Info, FsLayer::Verwaltung, "",
              "Die Verzeichnisspur ist noch nicht gelesen — geprueft wurde nichts");
        return bericht;
    }

    // ── Ebene Medium: die Sektoren des Verzeichnisses (immer) ────────────────
    //
    // Ein Verzeichnissektor mit falscher CRC erklaert jeden Muell, der danach im
    // Verzeichnis steht.  Deshalb kommt er VOR die Auswertung.
    // Sektoren eines BYTE-Bereichs des Dateisystems pruefen.  Byte, nicht Spur: das
    // Verzeichnis endet mitten in einer Spur (bei cpa780 nach 4096 von 5120 Byte),
    // und die Schnellpruefung darf nicht ueber sein Ende hinausschauen — sonst
    // meldete sie einen Schaden an Dateidaten, ohne die Datei benennen zu koennen.
    auto sektorenPruefen = [&](uint64_t von_byte, uint64_t bis_byte, bool mit_datei) {
        // Blocknummer → Datei, damit ein schadhafter Sektor einen Namen bekommt.
        std::map<uint16_t, std::string> gehoert;
        if (mit_datei)
            for (const CpmDirEntry& d : directory()) {
                if (d.free() || d.user > 15) continue;
                for (uint16_t blk : d.blocks)
                    if (blk != 0 && blk < total_blocks_)
                        gehoert.emplace(blk, datei(d.user, d.name));
            }

        const size_t von_spur = static_cast<size_t>(von_byte / track_bytes);
        const size_t bis_spur = std::min(
            bis, static_cast<size_t>((bis_byte + track_bytes - 1) / track_bytes));

        SectorData sec;
        for (size_t i = von_spur; i < bis_spur; ++i) {
            if (i < spur_da.size() && !spur_da[i]) continue;
            const SectorSpace::TrackRef t = space_.trackAt(static_cast<size_t>(start) + i);

            if (!space_.trackFormatted(t.cyl, t.head)) {
                b.addAt("cpm.medium.unformatiert", FsSeverity::Fehler, FsLayer::Medium,
                        "Spur " + std::to_string(t.cyl),
                        "Spur c" + std::to_string(t.cyl) + "h" + std::to_string(t.head)
                        + " traegt keine Adressmarken, gehoert aber zum Dateisystem",
                        t.cyl, t.head);
                continue;
            }

            for (uint16_t k = 0; k < secs_per_track_; ++k) {
                const uint8_t id = static_cast<uint8_t>(t.first_id + k);
                const uint64_t off = i * track_bytes
                                   + static_cast<uint64_t>(zurueck[k]) * sector_size_;
                if (off < von_byte || off >= bis_byte) continue;
                const uint16_t blk = static_cast<uint16_t>(off / prof_.block_size);
                const auto     it  = gehoert.find(blk);
                const std::string wem =
                    it != gehoert.end() ? it->second
                                        : (blk < dir_blocks_ ? std::string("Verzeichnis")
                                                             : std::string());

                if (!space_.readSector(t.cyl, t.head, id, sec)) {
                    b.addAt("cpm.medium.fehlt", FsSeverity::Fehler, FsLayer::Medium,
                            wem.empty() ? ("c" + std::to_string(t.cyl)) : wem,
                            "Sektor " + std::to_string(id) + " auf c" + std::to_string(t.cyl)
                            + "h" + std::to_string(t.head) + " fehlt"
                            + (wem.empty() ? " (kein Block einer Datei)" : " — " + wem
                               + " liegt darauf"),
                            t.cyl, t.head);
                    continue;
                }
                if (!sec.ok()) {
                    const bool belegt = !wem.empty();
                    b.addAt("cpm.medium.crc",
                            belegt ? FsSeverity::Fehler : FsSeverity::Warnung,
                            FsLayer::Medium, belegt ? wem : ("c" + std::to_string(t.cyl)),
                            std::string(sec.id_crc_ok ? "Daten-CRC" : "ID-CRC")
                            + " von Sektor " + std::to_string(id) + " auf c"
                            + std::to_string(t.cyl) + "h" + std::to_string(t.head)
                            + " stimmt nicht"
                            + (belegt ? " — Satz "
                                        + std::to_string((off % prof_.block_size) / kRecordBytes
                                                         + 1)
                                        + " von " + wem + " liegt darauf"
                                      : " (freier Bereich)"),
                            t.cyl, t.head);
                }
            }
        }
    };
    sektorenPruefen(0, dir_bytes, false);

    // ── Ebene Verwaltung: das Verzeichnis ────────────────────────────────────

    const int fuell = directoryFill();
    if (fuell >= 0 && fuell != 0xE5) {
        char hex[8];
        std::snprintf(hex, sizeof hex, "0x%02X", static_cast<unsigned>(fuell));
        b.add("cpm.dir.fuellbyte", FsSeverity::Warnung, FsLayer::Verwaltung, "Verzeichnis",
              std::string("Der Verzeichnisbereich besteht durchgehend aus ") + hex
              + " — die Diskette ist formatiert, aber nie eingerichtet worden (fuer CP/M"
                " ist nur 0xE5 ein freier Platz)");
        bericht.sortieren();
        return bericht;      // alles Weitere waere Rauschen ueber Fuellbytes
    }

    std::vector<uint8_t> roh;
    if (!directoryRaw(roh)) {
        b.add("cpm.verz.unlesbar", FsSeverity::Fehler, FsLayer::Verwaltung, "Verzeichnis",
              "Der Verzeichnisbereich ist nicht lesbar: " + lastError());
        bericht.sortieren();
        return bericht;
    }

    const std::vector<CpmDirEntry> verz = directory();

    // Geloeschte Plaetze: Nutzerbyte 0xE5, aber NICHT alle 32 Byte — so sieht ein nie
    // benutzter Platz nach mkfs aus.  Das ist der Vorrat der Wiederherstellung.
    int geloescht = 0, sonder = 0;
    std::string sonder_art;

    // Extents je Datei einsammeln (der Name ueberlebt bei CP/M, das Gruppieren ist
    // also verlaesslich) und Blockbelegung mitzaehlen.
    std::map<std::pair<int, std::string>, std::vector<const CpmDirEntry*>> nach_datei;
    // Je Block: wer ihn beansprucht — Klartext UND Verzeichnisplatz.  Ohne den Platz
    // liesse sich die Kreuzbelegung benennen, aber nicht aufloesen.
    std::map<uint16_t, std::vector<std::pair<std::string, int>>> blockbesitzer;
    std::set<std::tuple<int, std::string, int>> gesehen;   // (user, name, extent)

    for (const CpmDirEntry& d : verz) {
        const uint8_t* p = roh.data() + static_cast<size_t>(d.index) * 32;

        if (d.free()) {
            bool alles_e5 = true;
            for (int k = 1; k < 32; ++k) if (p[k] != 0xE5) { alles_e5 = false; break; }
            if (!alles_e5) ++geloescht;
            continue;
        }

        if (d.user > 15) {
            const char* art = sonderplatz(d.user);
            if (*art) { ++sonder; if (sonder_art.empty()) sonder_art = art; continue; }
            char hex[8];
            std::snprintf(hex, sizeof hex, "0x%02X", static_cast<unsigned>(d.user));
            FsFinding& f = b.add("cpm.dir.user", FsSeverity::Fehler, FsLayer::Verwaltung,
                  platz(d.index),
                  std::string("Nutzerbyte ") + hex
                  + " ist weder ein Nutzerbereich (0…15) noch 0xE5 (frei) noch ein"
                    " bekannter Sondersatz");
            f.repairs.push_back(platz_freigeben(d.index));
            continue;
        }

        // ── Name ─────────────────────────────────────────────────────────────
        bool steuerzeichen = false, klein = false;
        char trenner = 0;
        static const std::string verboten = " <>.,;:=?*[]%|()/\\";
        bool leer = true;
        for (int k = 1; k <= 11; ++k) {
            const uint8_t u = static_cast<uint8_t>(p[k] & 0x7F);
            if (u < 0x20 || u == 0x7F) { steuerzeichen = true; continue; }
            if (u != ' ') leer = false;
            const char c = static_cast<char>(u);
            if (c >= 'a' && c <= 'z') klein = true;
            // Das Leerzeichen ist Fuellzeichen und darf nicht als Trenner zaehlen.
            if (c != ' ' && verboten.find(c) != std::string::npos && !trenner) trenner = c;
        }
        if (steuerzeichen || leer)
            b.add("cpm.dir.name", FsSeverity::Fehler, FsLayer::Verwaltung, platz(d.index),
                  leer ? "Der Platz ist belegt, traegt aber keinen Namen"
                       : "Der Name enthaelt Steuerzeichen — der Platz ist vermutlich Muell")
             .repairs.push_back(platz_freigeben(d.index));
        else if (klein || trenner)
            b.add("cpm.dir.name", FsSeverity::Warnung, FsLayer::Verwaltung, platz(d.index),
                  "'" + d.name + "': "
                  + (klein ? std::string("CP/M-Namen sind grossgeschrieben")
                           : std::string("'") + trenner + "' ist ein CP/M-Trennzeichen "
                             "und im Namen nicht eingebbar"));

        // ── Extents und Saetze ───────────────────────────────────────────────
        if (!gesehen.insert({d.user, d.name, d.extent}).second)
            b.add("cpm.dir.doppelt", FsSeverity::Fehler, FsLayer::Verwaltung, platz(d.index),
                  "Extent " + std::to_string(d.extent) + " von "
                  + datei(d.user, d.name) + " steht mehrfach im Verzeichnis");

        if (d.records > 128) {
            FsRepair rep{"cpm.rc.anpassen",
                         "Die Satzzahl auf 128 setzen — mehr traegt ein Extent nicht",
                         /*datenverlust*/false, /*empfohlen*/true};
            rep.a = d.index;
            rep.b = 128;
            b.add("cpm.dir.rc", FsSeverity::Warnung, FsLayer::Verwaltung, platz(d.index),
                  datei(d.user, d.name) + ": Satzzahl " + std::to_string(d.records)
                  + " ist groesser als die 128 Saetze eines Extents").repairs.push_back(rep);
        }

        int belegte = 0;
        bool luecke = false, nach_null = false;
        for (uint16_t blk : d.blocks) {
            if (blk == 0) { nach_null = true; continue; }
            if (nach_null) luecke = true;
            ++belegte;
            if (blk >= total_blocks_) {
                b.add("cpm.block.ausserhalb", FsSeverity::Fehler, FsLayer::Verwaltung,
                      platz(d.index),
                      datei(d.user, d.name) + ": Blockzeiger " + std::to_string(blk)
                      + " liegt hinter dem Datenbereich (" + std::to_string(total_blocks_)
                      + " Bloecke) — meist das falsche Dateisystemprofil")
                 .repairs.push_back(zeiger_streichen(d.index));
                continue;
            }
            if (blk < dir_blocks_)
                b.add("cpm.block.verzeichnis", FsSeverity::Gefahr, FsLayer::Verwaltung,
                      platz(d.index),
                      datei(d.user, d.name) + " beansprucht Block " + std::to_string(blk)
                      + ", der zum VERZEICHNIS gehoert — ein Schreibvorgang darauf"
                        " zerstoert das Verzeichnis")
                 .repairs.push_back(zeiger_streichen(d.index));
            blockbesitzer[blk].push_back({datei(d.user, d.name) + " Extent "
                                          + std::to_string(d.extent), d.index});
        }
        if (luecke)
            b.add("cpm.block.luecke", FsSeverity::Warnung, FsLayer::Verwaltung, platz(d.index),
                  datei(d.user, d.name) + ", Extent " + std::to_string(d.extent)
                  + ": auf einen leeren Blockzeiger folgt wieder ein belegter");
        if (belegte == 0 && d.records > 0)
            b.add("cpm.dir.leer", FsSeverity::Fehler, FsLayer::Verwaltung, platz(d.index),
                  datei(d.user, d.name) + ", Extent " + std::to_string(d.extent)
                  + ": " + std::to_string(d.records)
                  + " Saetze angesagt, aber kein einziger Block genannt");

        nach_datei[{d.user, d.name}].push_back(&d);
    }

    // ── Kreuzbelegung: derselbe Block in zwei Dateien ────────────────────────
    for (const auto& [blk, wer] : blockbesitzer) {
        if (wer.size() < 2) continue;
        std::string liste;
        for (const auto& [w, ignoriert] : wer) liste += (liste.empty() ? "" : ", ") + w;
        FsFinding& f = b.add("cpm.block.doppelt", FsSeverity::Gefahr, FsLayer::Verwaltung,
              "Block " + std::to_string(blk),
              "Block " + std::to_string(blk) + " wird von " + std::to_string(wer.size())
              + " Eintraegen beansprucht (" + liste
              + ") — wer als zweiter schreibt, zerstoert die Daten des ersten");
        // Der Block bleibt bei der Datei mit dem KLEINEREN Verzeichnisindex; jede
        // weitere bekommt dort einen Nullzeiger.  Das ist der verlustbehaftete, aber
        // eindeutige Weg; `cpm.kreuz.kopieren` (§8) kommt spaeter.
        for (size_t k = 1; k < wer.size(); ++k) {
            FsRepair rep{"cpm.kreuz.erstem_lassen",
                         "Block " + std::to_string(blk) + " bei '" + wer.front().first
                         + "' lassen und '" + wer[k].first + "' ab dort abschneiden",
                         /*datenverlust*/true, /*empfohlen*/k == 1};
            rep.a = wer[k].second;
            rep.b = blk;
            f.repairs.push_back(rep);
        }
    }

    // ── Fehlende Extents ─────────────────────────────────────────────────────
    //
    // Bewusst vorsichtig: gemeldet wird nur ein Loch, das GROESSER ist, als ein
    // Verzeichnisplatz ueberbruecken kann, und ein Anfang jenseits des ersten
    // Platzes.  Wie ein fremdes BDOS die Extentnummern innerhalb eines Platzes
    // zaehlt, ist nicht ueberall gleich — daraus einen Fehler zu machen brachte
    // Falschmeldungen auf gesunden Disketten (E10).
    for (const auto& [schluessel, teile] : nach_datei) {
        std::vector<int> ext;
        for (const CpmDirEntry* d : teile) ext.push_back(d->extent);
        std::sort(ext.begin(), ext.end());
        const int schritt = static_cast<int>(ext_per_entry_);

        if (ext.front() >= schritt)
            b.add("cpm.dir.extentluecke", FsSeverity::Fehler, FsLayer::Verwaltung,
                  datei(schluessel.first, schluessel.second),
                  "Die Datei beginnt erst bei Extent " + std::to_string(ext.front())
                  + " — ihr Anfang fehlt");
        for (size_t k = 1; k < ext.size(); ++k)
            if (ext[k] - ext[k - 1] > schritt)
                b.add("cpm.dir.extentluecke", FsSeverity::Fehler, FsLayer::Verwaltung,
                      datei(schluessel.first, schluessel.second),
                      "Zwischen Extent " + std::to_string(ext[k - 1]) + " und "
                      + std::to_string(ext[k]) + " fehlen Verzeichnisplaetze");
    }

    if (sonder)
        b.add("cpm.dir.sonderplatz", FsSeverity::Info, FsLayer::Verwaltung, "Verzeichnis",
              std::to_string(sonder) + " Verzeichnisplaetze tragen eine Sonderfunktion ("
              + sonder_art + ") und werden von diesem Werkzeug nicht ausgewertet");
    if (geloescht)
        b.add("cpm.dir.geloescht", FsSeverity::Info, FsLayer::Verwaltung, "Verzeichnis",
              std::to_string(geloescht) + " freie Verzeichnisplaetze tragen noch einen"
              " lesbaren Eintrag — dort stehen geloeschte Dateien, die sich"
              " wiederherstellen lassen");

    // ── Ebene Medium: der Datenbereich (nur Vollpruefung) ────────────────────
    if (level == FsCheckLevel::Voll)
        sektorenPruefen(dir_bytes, data_bytes_, true);

    // ── Ebene Medium: die Systemspuren (nur Vollpruefung) ────────────────────
    //
    // Sie gehoeren keinem Dateisystem — das Lade-ROM liest Spur 0 blind ein, bevor
    // es irgendein Dateisystem gibt (§13a).  Ein Schaden dort kostet trotzdem etwas,
    // naemlich die Bootfaehigkeit, und niemand sonst sieht ihn: die Erkennung meldet
    // „1 Sektor mit CRC-Fehler" ohne zu sagen, wo.  Eigene Rechnung, weil die
    // Systemspuren eine ANDERE Geometrie haben duerfen als der Datenbereich
    // (cpa780: drei 128-B-Seiten, dann 1024 B).
    if (level == FsCheckLevel::Voll && start > 0) {
        for (int i = 0; i < start; ++i) {
            const SectorSpace::TrackRef t = space_.trackAt(static_cast<size_t>(i));
            if (!nachladen && !space_.trackKnown(t.cyl, t.head)) {
                bericht.vollstaendig = false;
                continue;
            }
            ++bericht.spuren_gelesen;
            if (!space_.trackFormatted(t.cyl, t.head)) {
                b.addAt("cpm.medium.systemspur", FsSeverity::Warnung, FsLayer::Medium,
                        "Systemspur", "Systemspur c" + std::to_string(t.cyl) + "h"
                        + std::to_string(t.head) + " traegt keine Adressmarken —"
                          " die Diskette ist so nicht bootfaehig",
                        t.cyl, t.head);
                continue;
            }
            // Physisch durchgehen, nicht die erwarteten IDs durchlesen: eine
            // Systemspur darf eine ID doppelt tragen, und dann ist womoeglich
            // gerade die zweite Aufnahme die schadhafte.
            const std::string wo = "c" + std::to_string(t.cyl) + "h" + std::to_string(t.head);
            std::vector<bool> da(t.sectors, false);
            for (const LogicalSector& ls : space_.trackSectors(t.cyl, t.head)) {
                if (ls.id >= t.first_id && ls.id < t.first_id + t.sectors)
                    da[static_cast<size_t>(ls.id - t.first_id)] = true;
                if (ls.id_crc_ok && ls.data_crc_ok) continue;
                b.addAt("cpm.medium.systemspur", FsSeverity::Warnung, FsLayer::Medium,
                        "Systemspur",
                        std::string(ls.id_crc_ok ? "Daten-CRC" : "ID-CRC")
                        + " von Sektor " + std::to_string(ls.id) + " der Systemspur " + wo
                        + " stimmt nicht — die Diskette bootet moeglicherweise nicht",
                        t.cyl, t.head);
            }
            for (uint8_t k = 0; k < t.sectors; ++k)
                if (!da[k])
                    b.addAt("cpm.medium.systemspur", FsSeverity::Warnung, FsLayer::Medium,
                            "Systemspur",
                            "Sektor " + std::to_string(t.first_id + k) + " der Systemspur "
                            + wo + " fehlt", t.cyl, t.head);
        }
    }

    bericht.sortieren();
    return bericht;
}
