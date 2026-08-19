/**
 * @file udos1715_recover.cpp
 * @brief Rettung geloeschter UDOS1715-/NDOS-Dateien (PC 1715, P8000) — **nur lesend**.
 *
 * Umsetzung von `doc/design/15_dateisystempruefung.md` §13, Schwester von
 * `udos_recover.cpp`.  Das Verfahren ist dasselbe — der Descriptor ueberlebt das
 * Loeschen vollstaendig, allein der Name ist fort —, die Kette liegt hier aber in
 * eigenen **Zeigersektoren** statt im Nachspann hinter der Daten-CRC: der µPD765 des
 * PC 1715 kommt dort nicht hin (`doc/udos1715_diskettenformat.md` §6).
 *
 * Drei Unterschiede, die man beim Lesen im Kopf haben muss:
 *
 * 1. **Ein Datentraeger, nicht zwei Seiten.**  BFOS fasst beide Seiten zu einer Spur
 *    von 32 Sektoren zusammen (§1.1); es gibt hier keine Kopfnummer im Fund, der
 *    Sektorindex traegt sie schon.
 * 2. **Die Kette haengt an `FIRSTBL`** (Descriptor-Offset 80H).  Reisst sie, ist der
 *    Fund ein Bruchstueck — anders als bei ZDOS gibt es keine zweite, ableitbare
 *    Richtung, aus der sich das Fehlende rekonstruieren liesse.
 * 3. **Der erste Eintrag des ersten Zeigersektors ist der Descriptor selbst** (§6);
 *    `recordChain` schneidet ihn schon ab, `sectorsOfFile` liefert alles zusammen.
 *
 * Wie bei ZDOS gibt es **kein Zurueckschreiben** auf die Diskette: der Weg zurueck
 * fuehrt ueber Retten, Benennen und `put`; das Beiblatt `udos-dateiangaben.txt`
 * bringt Typ, Eigenschaften, ENTRY, Recordlaenge und Segmente mit.  Die ausfuehrliche
 * Begruendung steht im Kopf von `udos_recover.cpp`.
 *
 * @see doc/design/15_dateisystempruefung.md §13 · doc/udos1715_diskettenformat.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/check/fs_recover.h"
#include "core/filesystem/udos/udos1715_fs.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr size_t kSector = kUdos1715Sector;   // 256
constexpr size_t kMaxFunde = 200;

/// @brief Offset des Zeigers auf den ersten Zeigersektor im Descriptor (`FIRSTBL`).
constexpr size_t kFirstbl = 0x80;

constexpr const char* kWegZurueck =
    "NDOS fuehrt den Dateinamen nur im Verzeichnis; er ist beim Loeschen"
    " verlorengegangen. Der Fund laesst sich herausholen, benennen und mit `put`"
    " wieder einspielen — Typ, Eigenschaften und Segmente bringt das Beiblatt"
    " udos-dateiangaben.txt mit.";

std::string ort(UdosPointer p) {
    return "Spur " + std::to_string(p.track) + " Sektor " + std::to_string(p.sector_index);
}

bool druckbar6(const uint8_t* p) {
    for (int i = 0; i < 6; ++i)
        if (p[i] < 0x20 || p[i] >= 0x7F) return false;
    return true;
}

/// @brief Sechs Byte, die ein Vermerk sein koennen: druckbar ODER ganz leer.
///
/// Der PC 1715 laesst den Aenderungsvermerk **unbeschrieben** (sechs Nullbytes) —
/// an der Systemdiskette gemessen, Descriptor der Verzeichnisdatei.  Wer hier
/// „druckbar" verlangt, findet auf einer echten NDOS-Diskette keinen einzigen
/// Descriptor.
bool vermerk6(const uint8_t* p) {
    bool leer = true;
    for (int i = 0; i < 6; ++i) if (p[i] != 0x00) leer = false;
    return leer || druckbar6(p);
}

/**
 * @brief Die Signatur eines Descriptors — die **billige** Haelfte der Probe.
 *
 * Sie ist bewusst schwaecher als die ZDOS-Fassung: die dortigen festen Trennmarken
 * `FF 00` bei 1EH und 26H sind eine Sitte des A5120-Systems und stehen auf einer
 * echten PC-1715-Diskette **nicht** dort (dort sind es Nullbytes) — verlangt man
 * sie, findet der Suchlauf gar nichts.  Getragen wird die Probe deshalb von der
 * teuren Haelfte im Suchlauf: `FIRSTBL` muss auf einen echten Zeigersektor zeigen,
 * dessen erste Eintragung dieser Descriptor selbst ist (§6).  Das ist eine
 * strukturelle Aussage und zufaellig praktisch nicht zu treffen — E10 bleibt damit
 * gewahrt: **ein erfundener Fund ist schlimmer als ein verpasster.**
 */
bool sieht_aus_wie_descriptor(const std::vector<uint8_t>& d, uint8_t spt, uint8_t tracks) {
    if (d.size() < kSector) return false;
    for (size_t i = 0; i < 6; ++i) if (d[i] != 0x00) return false;

    if (udosTypeName(d[0x0C]).empty()) return false;
    const uint8_t typbits = static_cast<uint8_t>(d[0x0C] & 0xF0);
    if ((typbits & static_cast<uint8_t>(typbits - 1)) != 0) return false;

    const uint16_t reclen = static_cast<uint16_t>(d[0x0F] | (d[0x10] << 8));
    if (reclen == 0 || reclen % 128 != 0 || reclen > 4096) return false;

    const uint32_t sektoren = static_cast<uint32_t>(tracks) * spt;
    const uint16_t records  = static_cast<uint16_t>(d[0x0D] | (d[0x0E] << 8));
    if (records == 0 || records > sektoren) return false;

    for (size_t off : {size_t{0x08}, size_t{0x0A}, kFirstbl}) {
        const UdosPointer p = UdosPointer::fromBytes(d.data() + off);
        if (p.track >= tracks || p.sector_index >= spt) return false;
    }
    return vermerk6(d.data() + 0x18) && vermerk6(d.data() + 0x20);
}

}  // namespace

// ─── Suchlauf ────────────────────────────────────────────────────────────────

FsRecoverReport Udos1715FileSystem::recoverScan(FsRecoverLevel level, bool nachladen) const {
    FsRecoverReport bericht;
    bericht.level = level;

    const uint8_t spt    = secs_per_track_;
    const uint8_t tracks = tracks_;

    // Spurbuchfuehrung wie in `udos1715_check.cpp`.  Die Spur umfasst hier BEIDE
    // Seiten (§1.1) — bekannt ist sie erst, wenn beide gelesen sind.
    std::set<uint8_t> gewollt, da;
    auto brauche = [&](uint8_t t) {
        gewollt.insert(t);
        if (!nachladen
            && (!space_.trackKnown(t, 0)
                || (spt > phys_spt_ && !space_.trackKnown(t, 1)))) {
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

    // ── 1. Was LEBT? ─────────────────────────────────────────────────────────
    if (!brauche(prof_.directory_track)) { abschluss(); return bericht; }
    brauche(prof_.bitmap_track);

    std::map<uint32_t, std::string> lebend;
    std::set<std::string>           lebt_name;

    auto dateiMerken = [&](UdosPointer descriptor, const std::string& wem) {
        std::vector<UdosPointer> sektoren;
        if (!sectorsOfFile(descriptor, sektoren)) return;
        for (const UdosPointer& p : sektoren) lebend.emplace(nr(p), wem);
    };
    dateiMerken(directoryDescriptor(), "DIRECTORY");
    for (const UdosDirEntry& e : directory()) {
        lebt_name.insert(e.name);
        if (!brauche(e.header.track)) continue;
        dateiMerken(e.header, e.name);
    }

    // Der Systembereich (§7.5): belegte Sektoren auf Spur 0 (Urlader und BFOS einer
    // Systemdiskette) sowie auf Verzeichnis- und Kartenspur, die zu keiner Datei
    // gehoeren.  Wie bei ZDOS wird die SPUR nicht ausgeschlossen, sondern nur, was
    // die Karte dort belegt fuehrt — sonst uebersaehe der Suchlauf jede Datei, die
    // dort liegt (bei ZDOS an `NOTE.TO.SD` auf Spur 21 nachgewiesen).
    for (uint8_t t : {static_cast<uint8_t>(0), prof_.directory_track, prof_.bitmap_track}) {
        if (t >= tracks || !da.count(t)) continue;
        for (uint8_t s = 0; s < spt; ++s)
            if (bitmap_.used(t, static_cast<uint8_t>(s + 1)))
                lebend.emplace(nr(UdosPointer{s, t}), "Systembereich");
    }

    // ── 2. Namensreste im Verzeichnis (§13, Kasten) ──────────────────────────
    //
    // Wie bei ZDOS: `removeDirEntry` kompaktiert, der Name ist damit ueberschrieben.
    // Was hinter dem Endebyte noch wie ein Eintrag aussieht, gilt als **Vorschlag**.
    std::map<uint32_t, std::string> namensrest;
    {
        UdosFileHeader dir_hdr;
        std::vector<UdosPointer> dir_kette;
        if (readDescriptor(directoryDescriptor(), dir_hdr)
            && recordChain(dir_hdr, dir_kette)) {
            const uint32_t sek_je_rec = std::max<uint32_t>(1u, dir_hdr.record_len / kSector);
            const size_t   je_sektor  = std::min<size_t>(kSector, dir_hdr.record_len);
            for (const UdosPointer& rec : dir_kette) {
                std::vector<uint8_t> roh;
                for (uint32_t k = 0; k < sek_je_rec && rec.sector_index + k < spt; ++k) {
                    std::vector<uint8_t> d;
                    if (!readSector(UdosPointer{static_cast<uint8_t>(rec.sector_index + k),
                                                rec.track}, d)) break;
                    roh.insert(roh.end(), d.begin(), d.begin() + static_cast<long>(je_sektor));
                }
                size_t i = 0;
                bool hinterm_ende = false;
                while (i + 3 <= roh.size()) {
                    if (roh[i] == 0xFF) { hinterm_ende = true; ++i; continue; }
                    const uint8_t n = static_cast<uint8_t>(roh[i] & 0x3F);
                    if (n == 0 || i + 3 + n > roh.size()) break;
                    const std::string name(reinterpret_cast<const char*>(roh.data() + i + 1), n);
                    const UdosPointer desc = UdosPointer::fromBytes(roh.data() + i + 1 + n);
                    if (!validName(name, nullptr)) break;
                    if (desc.track >= tracks || desc.sector_index >= spt) break;
                    if (hinterm_ende && !lebend.count(nr(desc)))
                        namensrest.emplace(nr(desc), name);
                    i += 3 + n;
                }
            }
        }
    }

    // ── 3. Kandidaten: Sektoren mit der Signatur eines Descriptors ───────────
    std::set<uint32_t> vergeben;
    int laufende_nummer = 0;

    // Die Systemspuren werden NICHT uebersprungen — s. den Kopf von `udos_recover.cpp`.
    for (uint8_t t = 0; t < tracks && bericht.funde.size() < kMaxFunde; ++t) {
        if (!brauche(t)) continue;

        for (uint8_t i = 0; i < spt && bericht.funde.size() < kMaxFunde; ++i) {
            const UdosPointer desc{i, t};
            if (lebend.count(nr(desc)) || vergeben.count(nr(desc))) continue;
            if (level == FsRecoverLevel::Verzeichnis
                && bitmap_.used(t, static_cast<uint8_t>(i + 1)))
                continue;

            std::vector<uint8_t> d;
            if (!readSector(desc, d)) continue;
            if (!sieht_aus_wie_descriptor(d, spt, tracks)) continue;

            UdosFileHeader hdr;
            if (!readDescriptor(desc, hdr)) continue;

            // Die teure, tragende Haelfte der Probe (s. `sieht_aus_wie_descriptor`):
            // FIRSTBL muss ein echter Zeigersektor sein, und seine erste Eintragung
            // muss auf DIESEN Descriptor zurueckzeigen (§6).  Ohne diese Gegenprobe
            // reichten die Byteproben nicht, um einen Datensektor auszuschliessen.
            {
                Udos1715PointerBlock erster;
                if (!readPointerBlock(hdr.firstbl, erster)) continue;
                if (erster.entries.empty() || !(erster.entries.front() == desc)) continue;
            }

            // ── Zeigersektoren und Records ───────────────────────────────────
            std::vector<UdosPointer>          adressen;
            std::vector<Udos1715PointerBlock> bloecke;
            std::vector<std::string>          streit, vorbehalt;
            std::string abbruch;
            if (!pointerBlocks(hdr, adressen, bloecke)) {
                abbruch = "die Zeigersektorkette ab FIRSTBL " + ort(hdr.firstbl)
                        + " ist nicht zu verfolgen: " + lastError();
            }

            // Die Sektoren, aus denen der Fund besteht — Descriptor, Zeigersektoren,
            // Datenrecords.  Ueber `sectorsOfFile` laeuft dasselbe wie beim Loeschen,
            // damit Rettung und Loeschung genau denselben Umfang meinen.
            std::vector<UdosPointer> sektoren;
            if (abbruch.empty() && !sectorsOfFile(desc, sektoren))
                abbruch = "die Sektorliste des Fundes ist nicht zu bilden: " + lastError();

            const uint32_t sek_je_rec = std::max<uint32_t>(1u, hdr.record_len / kSector);
            std::vector<UdosPointer> records;   // Anfaenge der Datenrecords
            for (size_t k = 1; k < adressen.size(); ++k) records.push_back(adressen[k]);

            int crc_kaputt = 0;
            for (const UdosPointer& p : sektoren) {
                const auto it = lebend.find(nr(p));
                if (it != lebend.end())
                    streit.push_back(ort(p) + " gehoert jetzt zu '" + it->second + "'");
                SectorData sec;
                if (!space_.readSector(p.track, headOf(p), idOf(p), sec)
                    || sec.data.size() < kSector) { ++crc_kaputt; continue; }
                if (!sec.ok()) ++crc_kaputt;
            }

            const bool ganz = abbruch.empty() && streit.empty()
                           && records.size() == hdr.record_count;

            FsRecoverFind f;
            f.d = 0;                       // Descriptor-Fund
            f.a = t;
            f.b = i;
            f.c = static_cast<int>(records.size());
            for (const UdosPointer& p : records)
                f.teile.push_back(p.track * 256 + p.sector_index);
            for (const UdosPointer& p : sektoren) vergeben.insert(nr(p));
            vergeben.insert(nr(desc));
            // Sektorliste fuer den Dialog (§13.3b): Descriptor zuerst, dann die
            // Datenrecords in Lesereihenfolge.  Die Zeigersektoren bleiben aussen
            // vor — sie tragen Adressen, keinen Dateiinhalt.
            f.orte.push_back(FsRecoverOrt{desc.track, headOf(desc), idOf(desc)});
            for (const UdosPointer& rec : records)
                for (uint32_t k = 0; k < sek_je_rec && f.orte.size() < kFsRecoverMaxOrte;
                     ++k) {
                    const UdosPointer p{static_cast<uint8_t>(rec.sector_index + k),
                                        rec.track};
                    if (p.sector_index < spt)
                        f.orte.push_back(FsRecoverOrt{p.track, headOf(p), idOf(p)});
                }

            f.type   = hdr.typeName();
            f.origin = "Descriptor " + ort(desc);
            f.cyl    = t;
            f.head   = headOf(desc);
            f.sector_index = idOf(desc);
            f.size   = ganz ? hdr.length()
                            : static_cast<uint64_t>(records.size()) * hdr.record_len;

            char ersatz[24];
            std::snprintf(ersatz, sizeof ersatz, "GERETTET.%03d", ++laufende_nummer);
            const auto rest = namensrest.find(nr(desc));
            if (rest != namensrest.end() && !lebt_name.count(rest->second)) {
                f.vorschlag = rest->second;
                vorbehalt.push_back("Der Name '" + rest->second
                                    + "' ist ein Rest im Verzeichnis, keine gesicherte"
                                      " Angabe");
            } else {
                f.vorschlag = ersatz;
            }

            if (!abbruch.empty()) vorbehalt.insert(vorbehalt.begin(), abbruch);
            if (!ganz && streit.empty() && abbruch.empty())
                vorbehalt.push_back("Der Descriptor sagt " + std::to_string(hdr.record_count)
                                    + " Records an, die Zeigersektoren nennen "
                                    + std::to_string(records.size()));
            if (crc_kaputt)
                vorbehalt.push_back(std::to_string(crc_kaputt)
                                    + " Sektor(en) sind nicht lesbar oder tragen eine"
                                      " falsche Pruefsumme");

            fsRecoverBelege(f.detail, streit, "Sektoren");
            fsRecoverBelege(f.detail, vorbehalt, "Vorbehalte");

            f.quality = !ganz       ? FsRecoverQuality::Bruchstueck
                      : crc_kaputt  ? FsRecoverQuality::Wahrscheinlich
                                    : FsRecoverQuality::Sicher;
            f.wiederherstellbar = false;
            f.warum_nicht       = kWegZurueck;

            bericht.funde.push_back(std::move(f));
        }
    }

    // ── 4. Rohbereiche (nur die Oberflaechensuche) ───────────────────────────
    //
    // Laeufe enden an der SPURGRENZE — sonst naenne der Dateiname eine zweite
    // Sektornummer, die auf einer anderen Spur liegt (§13.3).
    if (level == FsRecoverLevel::Oberflaeche) {
        std::vector<UdosPointer> lauf;
        std::vector<uint8_t>     lauf_daten;

        auto laufAbschliessen = [&] {
            if (lauf.empty()) return;
            if (bericht.funde.size() >= kMaxFunde) { lauf.clear(); lauf_daten.clear(); return; }
            FsRecoverFind f;
            f.d       = 1;
            f.a       = lauf.front().track;
            f.b       = lauf.front().sector_index;
            f.c       = static_cast<int>(lauf.size());
            f.quality = FsRecoverQuality::Bruchstueck;
            f.size    = static_cast<uint64_t>(lauf.size()) * kSector;
            f.type    = fsRecoverEinordnung(lauf_daten);
            for (const UdosPointer& p : lauf) {
                f.teile.push_back(p.track * 256 + p.sector_index);
                if (f.orte.size() < kFsRecoverMaxOrte)
                    f.orte.push_back(FsRecoverOrt{p.track, headOf(p), idOf(p)});
            }
            f.cyl     = lauf.front().track;
            f.head    = headOf(lauf.front());
            f.sector_index = idOf(lauf.front());

            char wo[56];
            std::snprintf(wo, sizeof wo, "fragment_c%dh%d_s%u-s%u.bin", f.cyl, f.head,
                          static_cast<unsigned>(lauf.front().sector_index),
                          static_cast<unsigned>(lauf.back().sector_index));
            f.vorschlag = wo;
            f.origin    = "Bereich ohne Descriptor, " + ort(lauf.front())
                        + (lauf.size() > 1 ? "…" + std::to_string(lauf.back().sector_index) : "");
            f.detail    = "Kein Descriptor — Name, Typ und Laenge sind unbekannt;"
                          " gerettet wird der Rohinhalt (" + f.type + ")";
            f.warum_nicht = "Ein Rohbereich hat keinen Descriptor, aus dem sich eine Datei"
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
                if (!readSector(p, d) || fsRecoverFuellmuster(d)) { laufAbschliessen(); continue; }
                lauf.push_back(p);
                if (lauf_daten.size() < 4096)
                    lauf_daten.insert(lauf_daten.end(), d.begin(), d.end());
            }
            laufAbschliessen();
        }
        laufAbschliessen();
    }

    abschluss();
    return bericht;
}

// ─── Lesen ───────────────────────────────────────────────────────────────────

bool Udos1715FileSystem::recoverRead(const FsRecoverFind& f, std::vector<uint8_t>& out) const {
    out.clear();

    if (f.d == 1) {                              // Rohbereich
        for (int teil : f.teile) {
            const UdosPointer p{static_cast<uint8_t>(teil & 0xFF),
                                static_cast<uint8_t>((teil >> 8) & 0xFF)};
            std::vector<uint8_t> d;
            if (!readSector(p, d)) { out.insert(out.end(), kSector, 0xE5); continue; }
            out.insert(out.end(), d.begin(), d.end());
        }
        if (out.empty()) return fail("Von diesem Bereich ist kein Sektor lesbar");
        return true;
    }

    const UdosPointer desc{static_cast<uint8_t>(f.b), static_cast<uint8_t>(f.a)};
    UdosFileHeader hdr;
    if (!readDescriptor(desc, hdr)) return false;

    // Frisch verfolgen; bricht die Kette, gilt, was der Suchlauf gesehen hat.
    std::vector<UdosPointer> records;
    if (!recordChain(hdr, records)) {
        records.clear();
        for (int teil : f.teile)
            records.push_back(UdosPointer{static_cast<uint8_t>(teil & 0xFF),
                                          static_cast<uint8_t>((teil >> 8) & 0xFF)});
    }

    const uint32_t sek_je_rec = sectorsPerRecord(hdr.record_len);
    const size_t   je_sektor  = std::min<size_t>(kSector, hdr.record_len);
    for (const UdosPointer& rec : records) {
        for (uint32_t k = 0; k < sek_je_rec; ++k) {
            const UdosPointer p{static_cast<uint8_t>(rec.sector_index + k), rec.track};
            std::vector<uint8_t> d;
            if (p.sector_index >= secs_per_track_ || !readSector(p, d)) {
                // Fehlendes auffuellen, damit die Offsets des uebrigen stimmen (§13.3).
                out.insert(out.end(), je_sektor, 0xE5);
                continue;
            }
            out.insert(out.end(), d.begin(), d.begin() + static_cast<long>(je_sektor));
        }
    }
    if (out.empty()) return fail("Von diesem Fund ist kein einziger Record lesbar");

    if (records.size() == hdr.record_count) {
        const uint64_t laenge   = hdr.length();
        const bool     programm = (hdr.type_byte & 0x80) != 0;
        if (!(programm && hdr.segment_len > laenge) && out.size() > laenge)
            out.resize(static_cast<size_t>(laenge));
    }
    return true;
}

// ─── Kein Zurueckschreiben — aber eine brauchbare Absage ─────────────────────

bool Udos1715FileSystem::recoverRestore(const FsRecoverFind& f, const std::string& name) {
    (void)f; (void)name;
    return fail(kWegZurueck);
}

// ─── Angaben fuer das Beiblatt ───────────────────────────────────────────────

bool Udos1715FileSystem::recoverEntry(const FsRecoverFind& f, FileEntry& out) const {
    if (f.d != 0) return false;
    UdosFileHeader hdr;
    if (!readDescriptor(UdosPointer{static_cast<uint8_t>(f.b), static_cast<uint8_t>(f.a)}, hdr))
        return false;
    out.name   = f.name.empty() ? f.vorschlag : f.name;
    out.volume = f.volume;
    udosKopfInEintrag(hdr, out);
    return true;
}
