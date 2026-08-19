/**
 * @file udos_recover.cpp
 * @brief Rettung geloeschter UDOS-/ZDOS-Dateien (A5120) — **nur lesend**.
 *
 * Umsetzung von `doc/design/15_dateisystempruefung.md` §13, Gegenstueck zu
 * `cpm_recover.cpp` — und aus demselben Grund eine eigene Datei neben
 * `udos_check.cpp`: dort steht, was falsch ist, hier, was noch da ist.  Methoden von
 * @ref UdosFileSystem sind es trotzdem; ohne die Innenansicht (Kontrollblock,
 * Satzkette, Belegungskarte) ginge hier nichts.
 *
 * ### Was ein Loeschen bei UDOS uebriglaesst
 * `erase` tut genau zwei Dinge: es schneidet den Verzeichniseintrag heraus (die
 * folgenden ruecken nach, `FF` wird nachgezogen) und loescht die Bits der Datei in
 * der Belegungskarte.  **Kopfsektor, alle Saetze und die vollstaendige Verkettung in
 * den Kontrollbloecken bleiben unangetastet.**  Erhalten bleiben damit auch alle
 * Angaben, die eine Datei ausmachen: Typ, Eigenschaften (W E L S R F), ENTRY,
 * Satzlaenge, Blocklaenge, Speichersegmente, LOW/HIGH/STACK und beide Datumsvermerke.
 *
 * Verloren ist **allein der Name** — er stand nur im Verzeichnis.
 *
 * ### Warum hier nur gerettet und nicht wiederhergestellt wird
 * Auf der Diskette wieder einzutragen hiesse: Sektoren in der Karte belegen, einen
 * Verzeichniseintrag anlegen, den Rueckwaertszeiger des Kopfsektors umbiegen — drei
 * Schreibzugriffe auf einen Datentraeger, den man fuer eine Rettung gerade **nicht**
 * anfassen will, und das fuer eine Datei, deren Name ohnehin frei erfunden werden
 * muss.  Der Weg zurueck ist deshalb ein anderer und geht ueber Wege, die es schon
 * gibt:
 *
 *   1. Fund in den Ordner retten (`recover --to`, oder der Suchdialog).
 *   2. Der Datei dort den passenden Namen geben.
 *   3. Mit `put` wieder einspielen — das Beiblatt `udos-dateiangaben.txt` bringt Typ,
 *      Eigenschaften, ENTRY, Satzlaenge und Segmente von selbst mit
 *      (@ref UdosFileSystem::recoverEntry fuellt es).
 *
 * @ref FsRecoverFind::wiederherstellbar ist bei UDOS deshalb **immer** false, und
 * @ref FsRecoverFind::warum_nicht nennt diesen Weg.  (Bei CP/M gibt es das
 * Zurueckschreiben weiterhin: dort ist es ein einziges Byte, und der Name stimmt.)
 *
 * ### Was gesucht wird
 * 1. **Kopfsektoren** — ueber ihre Signatur (§13.1), nicht ueber einen Rest im
 *    Verzeichnis.  Von dort aus laesst sich die ganze Datei wieder lesen.
 * 2. **Rohbereiche** (nur @c FsRecoverLevel::Oberflaeche) — Laeufe von Sektoren mit
 *    Inhalt, die zu keiner lebenden Datei und zu keinem Kopfsektor-Fund gehoeren:
 *    die Bruchstuecke ohne Struktur.
 *
 * ### Zwei Suchtiefen — hier ein anderer Schnitt als bei CP/M
 * Bei CP/M ist die billige Suche billig, weil alles Gesuchte im Verzeichnis steht.
 * Bei UDOS steht dort **nichts** mehr; beide Tiefen muessen die Datenspuren ansehen.
 * Der Unterschied ist, WELCHE Sektoren als Kandidat gelten:
 *
 * * @c Verzeichnis — nur Sektoren, die die Karte als **frei** meldet, und keine
 *   Rohbereiche.  Das ist der Normalfall „ich habe eben etwas geloescht".
 * * @c Oberflaeche — jeder Sektor, dazu die Rohbereiche.  Das findet zusaetzlich
 *   Kopfsektoren, die als belegt gefuehrt werden, aber in keinem Verzeichnis mehr
 *   stehen (verlorener Platz, ueberschriebenes Verzeichnis).
 *
 * ### Die Systemspuren werden NICHT uebersprungen
 * Der naheliegende Griff — „auf Spur 0–2, 21, 22, 23 legt UDOS keine Datei an, also
 * gar nicht erst hinsehen" — ist **falsch**, und zwar an einer echten Diskette
 * nachgewiesen: `NOTE.TO.SD` der Referenzdiskette (`udos_boot_scp.hfe`, Seite 1) hat
 * ihren Kopfsektor auf **Spur 21**, und Seite 1 traegt auf Spur 22 eine weitere
 * gewoehnliche Datei.  Seite 1 ist eine reine Datenseite ohne Urlader; dort ist die
 * „Systemspur" nichts als eine Spur.  Es ist dieselbe Lehre, die schon `udos_check.cpp`
 * gezogen hat: **die Systemspuren sind Sitte, nicht Struktur.**  Nur `allocSectors`
 * dieser Umsetzung meidet sie — das echte UDOS tat es nicht.
 *
 * Ausgeschlossen wird deshalb nicht die Spur, sondern nur, was die Belegungskarte im
 * Bootbereich als BELEGT fuehrt und keiner Datei gehoert: Urlader, Nukleus und
 * Bootabbild.  Auf einer Systemseite haelt das den Nukleus aus der Fundliste heraus,
 * auf einer Datenseite steht dort nichts im Weg.
 *
 * @see doc/design/15_dateisystempruefung.md §13 · doc/udos_diskettenformat.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/check/fs_recover.h"
#include "core/filesystem/udos/udos_fs.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr size_t kSector = 128;
/// @brief Hoechstens so viele Funde — eine Liste, die niemand mehr liest, ist genau
///        dann wertlos, wenn sie am noetigsten waere (wie bei CP/M).
constexpr size_t kMaxFunde = 200;

/// @brief Der Weg zurueck auf die Diskette, im Klartext — er steht in jedem Fund.
constexpr const char* kWegZurueck =
    "UDOS fuehrt den Dateinamen nur im Verzeichnis; er ist beim Loeschen"
    " verlorengegangen. Der Fund laesst sich herausholen, benennen und mit `put`"
    " wieder einspielen — Typ, Eigenschaften und Segmente bringt das Beiblatt"
    " udos-dateiangaben.txt mit.";

std::string ort(UdosPointer p) {
    return "Spur " + std::to_string(p.track) + " Sektor " + std::to_string(p.sectorId());
}

/// @brief Sechs Byte druckbares ASCII?  (die beiden Datums-/Versionsvermerke)
bool druckbar6(const uint8_t* p) {
    for (int i = 0; i < 6; ++i)
        if (p[i] < 0x20 || p[i] >= 0x7F) return false;
    return true;
}

/**
 * @brief Die Signatur eines Kopfsektors (§13.1) — der einzige Filter gegen Fehltreffer.
 *
 * Hier gilt E10 doppelt: **ein erfundener Fund ist schlimmer als ein verpasster.**
 * Wer einen Datensektor als Kopfsektor ausgibt, bietet dem Anwender Muell unter einem
 * Dateinamen an — und der glaubt es.  Deshalb wird ALLES verlangt, was der
 * Schreibpfad (`UdosFileSystem::write`) setzt und was sich nachrechnen laesst: sechs
 * Nullbytes, genau ein Typbit, eine moegliche Satzlaenge und Satzzahl, beide Zeiger im
 * Bereich, die festen Trennmarken `FF 00` und zwei druckbare Vermerke.
 *
 * @param d       mindestens 128 Byte Sektordaten
 * @param spt     Sektoren je Spur
 * @param tracks  Spuren dieser Seite
 */
bool sieht_aus_wie_kopfsektor(const std::vector<uint8_t>& d, uint8_t spt, uint8_t tracks) {
    if (d.size() < kSector) return false;
    for (size_t i = 0; i < 6; ++i) if (d[i] != 0x00) return false;

    if (udosTypeName(d[12]).empty()) return false;          // kein Typbit
    // Genau EIN Typbit: 30H waere „D und A zugleich" und gibt es nicht.
    const uint8_t typbits = static_cast<uint8_t>(d[12] & 0xF0);
    if ((typbits & static_cast<uint8_t>(typbits - 1)) != 0) return false;

    const uint16_t satzlen = static_cast<uint16_t>(d[15] | (d[16] << 8));
    if (satzlen == 0 || satzlen % kSector != 0 || satzlen > 4096) return false;
    if (satzlen / kSector > spt) return false;               // ein Satz passt in keine Spur

    const uint32_t sektoren = static_cast<uint32_t>(tracks) * spt;
    const uint16_t saetze   = static_cast<uint16_t>(d[13] | (d[14] << 8));
    if (saetze == 0 || saetze > sektoren) return false;

    // Erster und letzter Satz muessen auf der Diskette liegen.  FF FF ist hier NICHT
    // zulaessig: eine Datei ohne ersten Satz gibt es nicht (record_count > 0).
    for (size_t off : {size_t{8}, size_t{10}}) {
        const UdosPointer p = UdosPointer::fromBytes(d.data() + off);
        if (p.track >= tracks || p.sector_index >= spt) return false;
    }

    if (d[30] != 0xFF || d[31] != 0x00) return false;
    if (d[38] != 0xFF || (d[39] != 0x00 && d[39] != 0xFF)) return false;
    return druckbar6(d.data() + 24) && druckbar6(d.data() + 32);
}

}  // namespace

// ─── Suchlauf ────────────────────────────────────────────────────────────────

FsRecoverReport UdosFileSystem::recoverScan(FsRecoverLevel level, bool nachladen) const {
    FsRecoverReport bericht;
    bericht.level = level;

    const uint8_t spt = secs_per_track_;
    const uint8_t tracks = static_cast<uint8_t>(
        std::min<size_t>(tracks_, space_.trackCount()));

    // Spurbuchfuehrung wie in `udos_check.cpp`: `gewollt` sind die Spuren, in die der
    // Suchlauf sehen wollte, `da` die davon verfuegbaren.  An einer physischen
    // Diskette holt er keine von sich aus (E2), sagt es aber (E8) — sonst hiesse
    // „nichts gefunden" viel zu viel.
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
    auto abschluss = [&] {
        bericht.spuren_gelesen = static_cast<int>(da.size());
        bericht.spuren_gesamt  = static_cast<int>(gewollt.size());
        bericht.sortieren();
    };
    auto nr = [&](UdosPointer p) {
        return static_cast<uint32_t>(p.track) * spt + p.sector_index;
    };

    // ── 1. Was LEBT?  Sektoren und Namen ─────────────────────────────────────
    //
    // Ohne das Verzeichnis ist kein Fund zu bewerten: „gehoert dieser Sektor
    // inzwischen einer anderen Datei?" ist die Frage, an der die Guete haengt.
    // Also ohne Verzeichnisspur keine Suche.
    if (!brauche(prof_.directory_track)) { abschluss(); return bericht; }
    brauche(prof_.bitmap_track);

    std::map<uint32_t, std::string> lebend;      // Sektor → Datei, die ihn hat
    std::set<std::string>           lebt_name;

    for (uint8_t s = 1; s <= 3 && s <= spt; ++s)
        lebend.emplace(nr(UdosPointer{static_cast<uint8_t>(s - 1), prof_.bitmap_track}),
                       "Belegungskarte");

    auto ketteMerken = [&](const UdosFileHeader& hdr, const std::string& wem) {
        const uint32_t je_satz = std::max<uint32_t>(1u, hdr.record_len / kSector);
        std::vector<UdosPointer> kette;
        if (!recordChain(hdr, kette)) return;
        for (const UdosPointer& satz : kette)
            for (uint32_t k = 0; k < je_satz && satz.sector_index + k < spt; ++k)
                lebend.emplace(nr(UdosPointer{static_cast<uint8_t>(satz.sector_index + k),
                                              satz.track}), wem);
    };

    UdosFileHeader dir_hdr;
    const bool dir_ok = readHeader(directoryHeader(), dir_hdr);
    if (dir_ok) {
        lebend.emplace(nr(directoryHeader()), "DIRECTORY");
        ketteMerken(dir_hdr, "DIRECTORY");
    }
    for (const UdosDirEntry& e : directory()) {
        lebt_name.insert(e.name);
        if (!brauche(e.header.track)) continue;
        UdosFileHeader hdr;
        if (!readHeader(e.header, hdr)) continue;
        lebend.emplace(nr(e.header), e.name);
        ketteMerken(hdr, e.name);
    }

    // Der Bootbereich (§8.6): belegte Sektoren auf den Systemspuren, die zu keiner
    // Datei gehoeren, sind Urlader/Nukleus/Bootabbild — sie sind zu Recht belegt und
    // trotzdem in keiner Kette.  `emplace` ueberschreibt nichts: wo eine echte Datei
    // liegt (Datenseite!), behaelt sie ihren Namen.
    for (uint8_t t : {static_cast<uint8_t>(0), static_cast<uint8_t>(1),
                      static_cast<uint8_t>(2), prof_.boot_track}) {
        if (t >= tracks || !da.count(t)) continue;
        for (uint8_t s = 1; s <= spt; ++s)
            if (bitmap_.used(t, s))
                lebend.emplace(nr(UdosPointer{static_cast<uint8_t>(s - 1), t}),
                               "Systembereich");
    }

    // ── 2. Namensreste im Verzeichnis (§13, Kasten) ──────────────────────────
    //
    // `removeDirEntry` schiebt die folgenden Eintraege nach vorn und zieht `FF` nach
    // — der geloeschte Name ist damit ueberschrieben.  Fremde UDOS-Auspraegungen
    // kompaktieren moeglicherweise nicht; deshalb wird HINTER dem Endebyte
    // weitergelesen, so lange dort noch etwas wie ein Eintrag aussieht.  Was sich
    // findet, ist ein **Vorschlag** und kommt nie in @ref FsRecoverFind::name.
    std::map<uint32_t, std::string> namensrest;   // Kopfsektor → moeglicher Name
    if (dir_ok) {
        std::vector<UdosPointer> dir_kette;
        if (recordChain(dir_hdr, dir_kette)) {
            const uint32_t je_satz = std::max<uint32_t>(1u, dir_hdr.record_len / kSector);
            for (const UdosPointer& satz : dir_kette) {
                std::vector<uint8_t> roh;
                for (uint32_t k = 0; k < je_satz && satz.sector_index + k < spt; ++k) {
                    std::vector<uint8_t> d;
                    UdosPointer b, f;
                    if (!readSector(UdosPointer{static_cast<uint8_t>(satz.sector_index + k),
                                                satz.track}, d, b, f)) break;
                    roh.insert(roh.end(), d.begin(), d.end());
                }
                size_t i = 0;
                bool hinterm_ende = false;
                while (i + 3 <= roh.size()) {
                    if (roh[i] == 0xFF) { hinterm_ende = true; ++i; continue; }
                    const uint8_t n = static_cast<uint8_t>(roh[i] & 0x3F);
                    if (n == 0 || i + 3 + n > roh.size()) break;
                    const std::string name(reinterpret_cast<const char*>(roh.data() + i + 1), n);
                    const UdosPointer kopf = UdosPointer::fromBytes(roh.data() + i + 1 + n);
                    if (!validName(name, nullptr)) break;
                    if (kopf.track >= tracks || kopf.sector_index >= spt) break;
                    // Nur, was NICHT mehr im Verzeichnis steht — ein lebender Eintrag
                    // waere kein Rest, sondern die Wahrheit.
                    if (hinterm_ende && !lebend.count(nr(kopf)))
                        namensrest.emplace(nr(kopf), name);
                    i += 3 + n;
                }
            }
        }
    }

    // ── 3. Kandidaten: Sektoren mit der Signatur eines Kopfsektors ───────────
    std::set<uint32_t> vergeben;     // gehoert schon zu einem Fund
    int laufende_nummer = 0;

    for (uint8_t t = 0; t < tracks && bericht.funde.size() < kMaxFunde; ++t) {
        if (!brauche(t)) continue;

        for (uint8_t i = 0; i < spt && bericht.funde.size() < kMaxFunde; ++i) {
            const UdosPointer kopf{i, t};
            if (lebend.count(nr(kopf)) || vergeben.count(nr(kopf))) continue;
            // Die billige Tiefe sieht nur in die FREIEN Sektoren — dort liegt, was
            // eben geloescht wurde.  Der Rest ist die Sache der Oberflaechensuche.
            if (level == FsRecoverLevel::Verzeichnis && bitmap_.used(t, kopf.sectorId()))
                continue;

            std::vector<uint8_t> d;
            UdosPointer back, fwd;
            if (!readSector(kopf, d, back, fwd)) continue;
            if (!sieht_aus_wie_kopfsektor(d, spt, tracks)) continue;

            UdosFileHeader hdr;
            if (!readHeader(kopf, hdr)) continue;   // prueft die Satzlaenge noch einmal

            // ── Die Kette verfolgen ──────────────────────────────────────────
            const uint32_t je_satz = std::max<uint32_t>(1u, hdr.record_len / kSector);
            std::vector<UdosPointer> saetze;
            std::set<uint32_t>       besucht;
            std::vector<std::string> streit, vorbehalt;
            UdosPointer q = hdr.first_record, vorher = kopf;
            int  crc_kaputt = 0, ohne_nachspann = 0, rueckwaerts_falsch = 0;
            std::string abbruch;

            while (!q.end() && saetze.size() <= static_cast<size_t>(tracks) * spt) {
                if (q.track >= tracks || q.sector_index >= spt) {
                    abbruch = "die Kette zeigt hinter Satz " + std::to_string(saetze.size())
                            + " auf Spur " + std::to_string(q.track) + " Sektor "
                            + std::to_string(q.sectorId()) + " — das gibt es nicht";
                    break;
                }
                if (!besucht.insert(nr(q)).second) {
                    abbruch = "die Kette laeuft bei " + ort(q) + " im Kreis";
                    break;
                }
                if (q.sector_index + je_satz > spt) {
                    abbruch = "Satz " + std::to_string(saetze.size() + 1) + " bei " + ort(q)
                            + " braucht " + std::to_string(je_satz)
                            + " Sektoren und reicht ueber das Spurende hinaus";
                    break;
                }
                if (!brauche(q.track)) {
                    abbruch = "Spur " + std::to_string(q.track) + " ist noch nicht gelesen";
                    break;
                }

                // Gehoert ein Sektor dieses Satzes inzwischen einer lebenden Datei?
                // Dann ist ab hier nichts mehr zu holen: der Inhalt ist ueberschrieben
                // und die Kette dahinter zeigt in fremdes Gebiet.
                bool fremd = false;
                for (uint32_t k = 0; k < je_satz; ++k) {
                    const auto it = lebend.find(
                        nr(UdosPointer{static_cast<uint8_t>(q.sector_index + k), q.track}));
                    if (it == lebend.end()) continue;
                    streit.push_back("Satz " + std::to_string(saetze.size() + 1) + " liegt auf "
                                     + ort(q) + ", das gehoert jetzt zu '" + it->second + "'");
                    fremd = true;
                    break;
                }
                if (fremd) break;

                UdosPointer b2{0xFF, 0xFF}, f2{0xFF, 0xFF};
                for (uint32_t k = 0; k < je_satz; ++k) {
                    const UdosPointer r{static_cast<uint8_t>(q.sector_index + k), q.track};
                    SectorData sec;
                    if (!space_.readSector(r.track, head_, r.sectorId(), sec)
                        || sec.data.size() < kSector) { ++crc_kaputt; continue; }
                    if (!sec.ok()) ++crc_kaputt;
                    if (sec.tail.size() < 4) { ++ohne_nachspann; continue; }
                    if (k == 0) {
                        b2 = UdosPointer::fromBytes(sec.tail.data());
                        f2 = UdosPointer::fromBytes(sec.tail.data() + 2);
                    }
                }
                // Die Kette ist DOPPELT verkettet — der Rueckwaertszeiger ist eine
                // echte Gegenprobe und damit ein Beleg fuer die Guete.
                if (!(b2 == vorher)) ++rueckwaerts_falsch;

                saetze.push_back(q);
                vorher = q;
                q = f2;
            }

            const bool ganz = abbruch.empty() && streit.empty()
                           && saetze.size() == hdr.record_count;

            FsRecoverFind f;
            f.d = 0;                       // Kopfsektor-Fund
            f.a = t;                       // Kopfsektor: Spur …
            f.b = i;                       //             … und Sektorindex
            f.c = static_cast<int>(saetze.size());
            vergeben.insert(nr(kopf));
            // Der Kopfsektor steht als erster in der Sektorliste — er ist der Ort,
            // an dem der Anwender ablesen kann, WAS er da gefunden hat (Typ, ENTRY,
            // Segmente); die Saetze danach zeigen den Inhalt (§13.3b).
            f.orte.push_back(FsRecoverOrt{kopf.track, head_, kopf.sectorId()});
            for (const UdosPointer& satz : saetze) {
                f.teile.push_back(satz.track * 256 + satz.sector_index);
                for (uint32_t k = 0; k < je_satz && satz.sector_index + k < spt; ++k) {
                    const UdosPointer r{static_cast<uint8_t>(satz.sector_index + k),
                                        satz.track};
                    vergeben.insert(nr(r));
                    if (f.orte.size() < kFsRecoverMaxOrte)
                        f.orte.push_back(FsRecoverOrt{r.track, head_, r.sectorId()});
                }
            }
            f.type   = hdr.typeName();
            f.origin = "Kopfsektor " + ort(kopf);
            f.cyl    = t;
            f.head   = head_;
            f.sector_index = kopf.sectorId();
            f.size   = ganz ? hdr.length()
                            : static_cast<uint64_t>(saetze.size()) * hdr.record_len;

            // Der Name ist fort; was hier steht, ist ein Vorschlag (§13, Kasten).
            char ersatz[24];
            std::snprintf(ersatz, sizeof ersatz, "GERETTET.%03d", ++laufende_nummer);
            const auto rest = namensrest.find(nr(kopf));
            if (rest != namensrest.end() && !lebt_name.count(rest->second)) {
                f.vorschlag = rest->second;
                vorbehalt.push_back("Der Name '" + rest->second
                                    + "' ist ein Rest im Verzeichnis, keine gesicherte"
                                      " Angabe");
            } else {
                f.vorschlag = ersatz;
            }

            if (!abbruch.empty()) vorbehalt.insert(vorbehalt.begin(), abbruch);
            if (!ganz && streit.empty())
                vorbehalt.push_back("Der Kopfsektor sagt " + std::to_string(hdr.record_count)
                                    + " Saetze an, erreichbar sind "
                                    + std::to_string(saetze.size()));
            if (crc_kaputt)
                vorbehalt.push_back(std::to_string(crc_kaputt)
                                    + " Sektor(en) sind nicht lesbar oder tragen eine"
                                      " falsche Pruefsumme");
            if (ohne_nachspann)
                vorbehalt.push_back(std::to_string(ohne_nachspann)
                                    + " Sektor(en) haben keinen Sektorkontrollblock");
            if (rueckwaerts_falsch)
                vorbehalt.push_back(std::to_string(rueckwaerts_falsch)
                                    + " Rueckwaertszeiger passen nicht zum Vorgaenger");

            fsRecoverBelege(f.detail, streit, "Saetze");
            fsRecoverBelege(f.detail, vorbehalt, "Vorbehalte");

            f.quality = !ganz                    ? FsRecoverQuality::Bruchstueck
                      : (crc_kaputt || ohne_nachspann || rueckwaerts_falsch)
                                                 ? FsRecoverQuality::Wahrscheinlich
                                                 : FsRecoverQuality::Sicher;

            // Bei UDOS gibt es kein Zurueckschreiben (s. Dateikopf) — der Weg zurueck
            // fuehrt ueber `put`, und der Fund sagt das auch.
            f.wiederherstellbar = false;
            f.warum_nicht       = kWegZurueck;

            bericht.funde.push_back(std::move(f));
        }
    }

    // ── 4. Rohbereiche: Inhalt ohne Kopfsektor (nur die Oberflaechensuche) ───
    //
    // Die Laeufe enden an der SPURGRENZE.  Ein Lauf darueber hinaus liesse sich nicht
    // mehr benennen: `fragment_c12h0_s20-s4` naenne eine zweite Sektornummer, die auf
    // einer anderen Spur liegt (bei CP/M dieselbe Ueberlegung, dort mit Bloecken,
    // §13.3).  Es ist auch die Koernung des Dateisystems selbst — ein UDOS-Satz
    // ueberschreitet die Spurgrenze nie (§7).
    if (level == FsRecoverLevel::Oberflaeche) {
        std::vector<UdosPointer> lauf;
        std::vector<uint8_t>     lauf_daten;

        auto laufAbschliessen = [&] {
            if (lauf.empty()) return;
            if (bericht.funde.size() >= kMaxFunde) { lauf.clear(); lauf_daten.clear(); return; }
            FsRecoverFind f;
            f.d       = 1;                                   // Rohbereich
            f.a       = lauf.front().track;
            f.b       = lauf.front().sector_index;
            f.c       = static_cast<int>(lauf.size());
            f.quality = FsRecoverQuality::Bruchstueck;       // ohne Struktur nie mehr
            f.size    = static_cast<uint64_t>(lauf.size()) * kSector;
            f.type    = fsRecoverEinordnung(lauf_daten);
            for (const UdosPointer& p : lauf) {
                f.teile.push_back(p.track * 256 + p.sector_index);
                if (f.orte.size() < kFsRecoverMaxOrte)
                    f.orte.push_back(FsRecoverOrt{p.track, head_, p.sectorId()});
            }
            f.cyl     = lauf.front().track;
            f.head    = head_;
            f.sector_index = lauf.front().sectorId();

            char wo[56];
            std::snprintf(wo, sizeof wo, "fragment_c%dh%d_s%u-s%u.bin", f.cyl, f.head,
                          static_cast<unsigned>(lauf.front().sectorId()),
                          static_cast<unsigned>(lauf.back().sectorId()));
            f.vorschlag = wo;
            f.origin    = "Bereich ohne Kopfsektor, " + ort(lauf.front())
                        + (lauf.size() > 1 ? "…" + std::to_string(lauf.back().sectorId()) : "");
            f.detail    = "Kein Kopfsektor — Name, Typ und Laenge sind unbekannt;"
                          " gerettet wird der Rohinhalt (" + f.type + ")";
            f.warum_nicht = "Ein Rohbereich hat keinen Kopfsektor, aus dem sich eine Datei"
                            " bauen liesse — herausholen laesst er sich aber";
            bericht.funde.push_back(std::move(f));
            lauf.clear();
            lauf_daten.clear();
        };

        for (uint8_t t = 0; t < tracks; ++t) {
            if (!da.count(t)) { laufAbschliessen(); continue; }
            for (uint8_t i = 0; i < spt; ++i) {
                const UdosPointer p{i, t};
                if (lebend.count(nr(p)) || vergeben.count(nr(p))) { laufAbschliessen(); continue; }

                std::vector<uint8_t> d;
                UdosPointer back, fwd;
                if (!readSector(p, d, back, fwd) || fsRecoverFuellmuster(d)) {
                    laufAbschliessen();
                    continue;
                }
                lauf.push_back(p);
                if (lauf_daten.size() < 4096)
                    lauf_daten.insert(lauf_daten.end(), d.begin(), d.end());
            }
            laufAbschliessen();          // die Spurgrenze schliesst den Lauf ab
        }
        laufAbschliessen();
    }

    abschluss();
    return bericht;
}

// ─── Lesen ───────────────────────────────────────────────────────────────────

bool UdosFileSystem::recoverRead(const FsRecoverFind& f, std::vector<uint8_t>& out) const {
    out.clear();

    // ── Rohbereich: die Sektoren des Laufs, mehr ist darueber nicht bekannt ──
    if (f.d == 1) {
        for (int teil : f.teile) {
            const UdosPointer p{static_cast<uint8_t>(teil & 0xFF),
                                static_cast<uint8_t>((teil >> 8) & 0xFF)};
            std::vector<uint8_t> d;
            UdosPointer back, fwd;
            if (!readSector(p, d, back, fwd)) { out.insert(out.end(), kSector, 0xE5); continue; }
            out.insert(out.end(), d.begin(), d.end());
        }
        if (out.empty()) return fail("Von diesem Bereich ist kein Sektor lesbar");
        return true;
    }

    const UdosPointer kopf{static_cast<uint8_t>(f.b), static_cast<uint8_t>(f.a)};

    // Frisch vom Medium, nicht aus dem Zettel von vorhin: zwischen Suchlauf und
    // Rettung kann geschrieben worden sein (wie `CpmFileSystem::recoverRead`).
    UdosFileHeader hdr;
    if (!readHeader(kopf, hdr)) return false;

    const uint32_t je_satz = std::max<uint32_t>(1u, hdr.record_len / kSector);
    const uint32_t grenze  = f.c > 0 ? static_cast<uint32_t>(f.c) : hdr.record_count;

    std::set<uint32_t>  besucht;
    UdosPointer q = hdr.first_record;
    uint32_t    gelesen = 0;

    while (!q.end() && gelesen < grenze) {
        if (q.track >= tracks_ || q.sector_index + je_satz > secs_per_track_) break;
        if (!besucht.insert(static_cast<uint32_t>(q.track) * secs_per_track_
                            + q.sector_index).second) break;

        UdosPointer weiter{};   // FF FF — bleibt stehen, wenn der Satz unlesbar ist
        for (uint32_t k = 0; k < je_satz; ++k) {
            const UdosPointer r{static_cast<uint8_t>(q.sector_index + k), q.track};
            std::vector<uint8_t> d;
            UdosPointer b, fw;
            if (!readSector(r, d, b, fw)) {
                // Fehlendes wird aufgefuellt, damit die Offsets des uebrigen stimmen
                // (§13.3) — was fehlt, steht im Beiblatt.
                out.insert(out.end(), kSector, 0xE5);
                continue;
            }
            if (k == 0) weiter = fw;
            out.insert(out.end(), d.begin(), d.end());
        }
        q = weiter;
        ++gelesen;
    }

    if (out.empty()) return fail("Von diesem Fund ist kein einziger Satz lesbar");

    // Kuerzen nur bei vollstaendiger Kette — bei einem Bruchstueck ist die Laenge aus
    // dem Kopfsektor eine Angabe ueber eine Datei, die so nicht mehr da ist.
    // Sonst gilt dieselbe Ausnahme wie in `readChain`: bei einer PROGRAMMdatei reicht
    // das Speicherabbild ueber das logische Dateiende hinaus, und wer es abschneidet,
    // kann sie nicht mehr zurueckspielen (doc/udos_diskettenformat.md §14.4).
    if (gelesen == hdr.record_count) {
        const uint64_t laenge = hdr.length();
        const bool programm = (hdr.type_byte == 0x80 || hdr.type_byte == 0x81);
        if (!(programm && hdr.segment_len > laenge) && out.size() > laenge)
            out.resize(static_cast<size_t>(laenge));
    }
    return true;
}

// ─── Kein Zurueckschreiben — aber eine brauchbare Absage ─────────────────────

bool UdosFileSystem::recoverRestore(const FsRecoverFind& f, const std::string& name) {
    (void)f; (void)name;
    return fail(kWegZurueck);
}

// ─── Angaben fuer das Beiblatt ───────────────────────────────────────────────

bool UdosFileSystem::recoverEntry(const FsRecoverFind& f, FileEntry& out) const {
    if (f.d != 0) return false;              // ein Rohbereich hat keinen Kopfsektor
    UdosFileHeader hdr;
    if (!readHeader(UdosPointer{static_cast<uint8_t>(f.b), static_cast<uint8_t>(f.a)}, hdr))
        return false;
    out.name   = f.name.empty() ? f.vorschlag : f.name;
    out.volume = f.volume;
    udosKopfInEintrag(hdr, out);
    return true;
}
