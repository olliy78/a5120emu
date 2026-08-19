/**
 * @file cpm_recover.cpp
 * @brief Wiederherstellung geloeschter CP/M-Dateien (CP/A, SCPX, SCP1700).
 *
 * Umsetzung von `doc/design/15_dateisystempruefung.md` §13.  Sie steht neben
 * `cpm_check.cpp` und aus demselben Grund nicht in `cpm_fs.cpp`: dort ist der
 * Betrieb zu Hause, hier die Rettung.  Methoden von @ref CpmFileSystem sind es
 * trotzdem — sie brauchen die Innenansicht (Blockgroesse, Verzeichnisbloecke,
 * Versatztabelle).
 *
 * ### Warum das ueberhaupt geht
 * **CP/M loescht mit EINEM Byte.**  `erase` setzt das Nutzerbyte des
 * Verzeichnisplatzes auf 0xE5 und ist fertig: Name, Typ, Attributbits,
 * Extentnummer, Satzzahl und **alle Blockzeiger** bleiben unveraendert stehen, die
 * Bloecke selbst sind unberuehrt.  Und weil CP/M keinen gespeicherten Belegungsplan
 * fuehrt (er wird beim Mounten aus dem Verzeichnis gebaut), ist auch nichts weiter
 * nachzuziehen.  Eine geloeschte Datei ist damit **vollstaendig beschrieben** — sie
 * wird nur nicht mehr gefunden.
 *
 * Verloren geht genau eines: der **Nutzerbereich**.  Er stand in ebendem Byte, das
 * 0xE5 geworden ist.  Wiederhergestellt wird deshalb immer nach Bereich 0, und das
 * steht auch so im Text des Fundes.
 *
 * ### Zwei Arten von Fund
 * 1. **Geloeschte Verzeichnisplaetze** — mit Namen, Groesse und Blockliste.  Das ist
 *    der billige Teil: er kostet keinen Spurzugriff ausser dem Verzeichnis.
 * 2. **Freie Bloecke mit Inhalt** (nur @c FsRecoverLevel::Oberflaeche) — die
 *    Bruchstuecke ohne Verzeichnisplatz: neu aufgesetztes Verzeichnis,
 *    ueberschriebener Verzeichnisbereich, halb neu beschriebene Diskette.  Sie
 *    haben keinen Namen und keine Struktur, nur einen Ort und einen Inhalt.
 *
 * @see doc/design/15_dateisystempruefung.md §13
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/check/fs_recover.h"
#include "core/filesystem/cpm/cpm_fs.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>

namespace {

constexpr uint32_t kExtentBytes = 16384;
constexpr uint32_t kRecordBytes = 128;

/// @brief Hoechstens so viele Rohbereiche — eine Liste, die niemand mehr liest,
///        ist genau dann wertlos, wenn sie am noetigsten waere.
constexpr size_t kMaxRohbereiche = 200;

/// @brief "NAME    TYP" aus einem rohen Platz, ohne die Attributhochbits.
std::string rohName(const uint8_t* p) {
    std::string name, typ;
    for (int i = 0; i < 8; ++i) name += static_cast<char>(p[1 + i] & 0x7F);
    for (int i = 0; i < 3; ++i) typ  += static_cast<char>(p[9 + i] & 0x7F);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!typ.empty()  && typ.back()  == ' ') typ.pop_back();
    return typ.empty() ? name : name + "." + typ;
}

/**
 * @brief Sieht der Name eines geloeschten Platzes wie ein CP/M-Name aus?
 *
 * Das ist der einzige Filter gegen Fehltreffer, den es hier gibt — ein
 * Verzeichnisplatz voller Nutzdaten faengt auch einmal mit 0xE5 an.  Verlangt
 * werden druckbare Grossbuchstaben ohne CP/M-Trennzeichen und ein nicht leerer
 * Namensteil; das ist genau das, was `validName` beim Schreiben zulaesst.
 */
bool nameTaugt(const uint8_t* p) {
    static const std::string verboten = "<>.,;:=?*[]%|()/\\";
    bool leer = true;
    for (int k = 1; k <= 11; ++k) {
        const uint8_t u = static_cast<uint8_t>(p[k] & 0x7F);
        if (u < 0x20 || u == 0x7F) return false;
        if (u != ' ') leer = false;
        const char c = static_cast<char>(u);
        if (c >= 'a' && c <= 'z') return false;
        if (verboten.find(c) != std::string::npos) return false;
    }
    if (leer) return false;
    // Der NAMENsteil (Byte 1..8) muss etwas tragen; ein Platz, der nur einen Typ
    // hat, ist keiner.
    for (int k = 1; k <= 8; ++k)
        if ((p[k] & 0x7F) != ' ') return true;
    return false;
}

/// @brief Einordnung eines Rohbereichs nach seinem Inhalt (§13.3).
std::string einordnung(const std::vector<uint8_t>& d) {
    if (d.empty()) return "unklar";
    size_t druckbar = 0;
    for (uint8_t b : d)
        if ((b >= 0x20 && b < 0x7F) || b == 0x09 || b == 0x0A || b == 0x0D || b == 0x1A)
            ++druckbar;
    if (druckbar * 10 >= d.size() * 9) return "Text";
    // Z80-Einsprungmuster am Anfang: JP nn (C3), LD SP,nn (31), DI (F3).
    if (d[0] == 0xC3 || d[0] == 0x31 || d[0] == 0xF3) return "Programm";
    return "unklar";
}

/// @brief Bis zu drei Belege aufzaehlen, den Rest zusammenfassen.
void belegAnhaengen(std::string& detail, const std::vector<std::string>& belege,
                    const std::string& was) {
    if (belege.empty()) return;
    if (!detail.empty()) detail += "; ";
    for (size_t i = 0; i < belege.size() && i < 3; ++i)
        detail += (i ? ", " : "") + belege[i];
    if (belege.size() > 3)
        detail += " und " + std::to_string(belege.size() - 3) + " weitere " + was;
}

}  // namespace

// ─── Suchlauf ────────────────────────────────────────────────────────────────

FsRecoverReport CpmFileSystem::recoverScan(FsRecoverLevel level, bool nachladen) const {
    FsRecoverReport bericht;
    bericht.level = level;

    const int start = space_.trackIndexOf(prof_.data_cyl, prof_.data_head);
    if (start < 0) return bericht;

    const uint64_t track_bytes = static_cast<uint64_t>(secs_per_track_) * sector_size_;
    const size_t   tracks      = space_.trackCount() - static_cast<size_t>(start);
    const uint64_t dir_bytes   = static_cast<uint64_t>(prof_.dir_entries) * 32;
    const size_t   dir_tracks  =
        static_cast<size_t>((dir_bytes + track_bytes - 1) / track_bytes);

    // Gezaehlt wird, was DIESE Suchtiefe ansehen will — wie beim Pruefbericht
    // (fs_check.h): sonst saehe eine vollstaendige Verzeichnissuche unvollstaendig aus.
    const size_t bis = (level == FsRecoverLevel::Oberflaeche) ? tracks
                                                              : std::min(dir_tracks, tracks);
    bericht.spuren_gesamt = static_cast<int>(bis);
    std::vector<bool> spur_da(bis, true);
    for (size_t i = 0; i < bis; ++i) {
        const SectorSpace::TrackRef t = space_.trackAt(static_cast<size_t>(start) + i);
        if (nachladen || space_.trackKnown(t.cyl, t.head)) { ++bericht.spuren_gelesen; continue; }
        spur_da[i]           = false;
        bericht.vollstaendig = false;
    }
    if (!spur_da.empty() && !spur_da[0]) return bericht;   // ohne Verzeichnis keine Suche

    std::vector<uint8_t> roh;
    if (!directoryRaw(roh)) return bericht;

    // ── Was LEBT?  Bloecke und Namen ─────────────────────────────────────────
    std::map<uint16_t, std::string> lebend;      // Block → Datei, die ihn hat
    std::set<std::string>           lebt_name;   // Name (gross) einer lebenden Datei
    for (const CpmDirEntry& d : directory()) {
        if (d.free() || d.user > 15) continue;
        lebt_name.insert(d.name);
        for (uint16_t blk : d.blocks)
            if (blk != 0 && blk < total_blocks_)
                lebend.emplace(blk, d.user == 0 ? d.name
                                                : std::to_string(d.user) + ":" + d.name);
    }

    // ── Ort eines Blocks (fuer den Sprung in den Diskeditor) ─────────────────
    auto ortVonBlock = [&](uint16_t blk, int& cyl, int& head, int& idx) {
        cyl = head = idx = -1;
        const uint64_t off = static_cast<uint64_t>(blk) * prof_.block_size;
        const size_t   ti  = static_cast<size_t>(off / track_bytes);
        if (ti >= tracks) return;
        const SectorSpace::TrackRef t = space_.trackAt(static_cast<size_t>(start) + ti);
        cyl  = t.cyl;
        head = t.head;
        idx  = skew_tab_[(off % track_bytes) / sector_size_];
    };

    // ── Einen Block lesen und dabei die CRCs beachten ────────────────────────
    //
    // `readAt` taugt dafuer nicht: es liefert die Bytes eines Sektors auch dann,
    // wenn dessen CRC nicht stimmt (und das ist im Betrieb richtig so).  Fuer die
    // Guete eines Fundes ist aber genau das die Frage.
    auto blockLesen = [&](uint16_t blk, std::vector<uint8_t>& out, int& schadhaft) -> bool {
        const uint64_t off = static_cast<uint64_t>(blk) * prof_.block_size;
        if (off + prof_.block_size > data_bytes_) return false;
        SectorData sec;
        for (uint32_t g = 0; g < prof_.block_size; g += sector_size_) {
            const size_t ti = static_cast<size_t>((off + g) / track_bytes);
            if (ti >= tracks || (ti < spur_da.size() && !spur_da[ti])) return false;
            const SectorSpace::TrackRef t = space_.trackAt(static_cast<size_t>(start) + ti);
            const uint16_t lsec = static_cast<uint16_t>(((off + g) % track_bytes) / sector_size_);
            const uint8_t  id   = static_cast<uint8_t>(t.first_id + skew_tab_[lsec]);
            if (!space_.readSector(t.cyl, t.head, id, sec)
                || sec.data.size() < sector_size_) {
                ++schadhaft;
                out.insert(out.end(), sector_size_, 0xE5);
                continue;
            }
            if (!sec.ok()) ++schadhaft;
            out.insert(out.end(), sec.data.begin(), sec.data.begin() + sector_size_);
        }
        return true;
    };

    // ── 1. Geloeschte Verzeichnisplaetze ─────────────────────────────────────
    //
    // Zusammengefasst wird ueber den NAMEN: er ueberlebt das Loeschen, der
    // Nutzerbereich nicht.  Zwei nacheinander geloeschte Dateien gleichen Namens
    // werden dadurch zu einem Fund — das ist die ehrlichere Auskunft, denn
    // auseinanderhalten liessen sie sich ohnehin nicht.
    struct Teil { int slot; int extent; uint8_t records; std::vector<uint16_t> blocks; };
    std::map<std::string, std::vector<Teil>> nach_name;

    for (int i = 0; i < prof_.dir_entries; ++i) {
        const uint8_t* p = roh.data() + static_cast<size_t>(i) * 32;
        if (p[0] != 0xE5) continue;
        // Ein NIE benutzter Platz ist nach `mkfs` durchgehend 0xE5 — der traegt nichts.
        bool alles_e5 = true;
        for (int k = 1; k < 32; ++k) if (p[k] != 0xE5) { alles_e5 = false; break; }
        if (alles_e5 || !nameTaugt(p)) continue;

        Teil t;
        t.slot    = i;
        t.extent  = (p[12] & 0x1F) + (p[14] & 0x3F) * 32;
        t.records = p[15];
        if (wide_ptr_)
            for (int k = 0; k < 8; ++k)
                t.blocks.push_back(static_cast<uint16_t>(p[16 + 2 * k] | (p[17 + 2 * k] << 8)));
        else
            for (int k = 0; k < 16; ++k) t.blocks.push_back(p[16 + k]);

        // Mindestens ein brauchbarer Blockzeiger — sonst ist es kein Dateirest.
        bool brauchbar = false;
        for (uint16_t blk : t.blocks)
            if (blk != 0 && blk >= dir_blocks_ && blk < total_blocks_) brauchbar = true;
        if (!brauchbar) continue;

        nach_name[rohName(p)].push_back(std::move(t));
    }

    // Bloecke, die schon einem Verzeichnisfund gehoeren — sie noch einmal als
    // Rohbereich anzubieten waere dieselbe Sache zweimal.
    std::set<uint16_t> vergeben;

    for (auto& [name, teile] : nach_name) {
        std::sort(teile.begin(), teile.end(),
                  [](const Teil& a, const Teil& b) { return a.extent < b.extent; });

        FsRecoverFind f;
        f.name      = name;
        f.vorschlag = name;
        f.volume    = 0;
        f.a         = 0;                       // Verzeichnisfund
        f.origin    = teile.size() == 1
                    ? "Verzeichnisplatz " + std::to_string(teile.front().slot)
                    : std::to_string(teile.size()) + " Verzeichnisplaetze ab "
                      + std::to_string(teile.front().slot);
        f.size      = static_cast<uint64_t>(teile.back().extent) * kExtentBytes
                    + static_cast<uint64_t>(teile.back().records) * kRecordBytes;

        std::vector<std::string> streit, wild, karies;
        int  schadhaft = 0;
        bool erster_ort = true;

        for (const Teil& t : teile) {
            f.teile.push_back(t.slot);
            for (uint16_t blk : t.blocks) {
                if (blk == 0) continue;
                if (blk >= total_blocks_ || blk < dir_blocks_) {
                    wild.push_back("Blockzeiger " + std::to_string(blk)
                                   + " liegt ausserhalb des Datenbereichs");
                    continue;
                }
                const auto it = lebend.find(blk);
                if (it != lebend.end())
                    streit.push_back("Block " + std::to_string(blk) + " gehoert jetzt zu "
                                     + it->second);
                else if (!vergeben.insert(blk).second)
                    karies.push_back("Block " + std::to_string(blk)
                                     + " wird von einem zweiten geloeschten Eintrag"
                                       " beansprucht");
                if (erster_ort) {
                    ortVonBlock(blk, f.cyl, f.head, f.sector_index);
                    erster_ort = false;
                }
                // Die Pruefsummen kosten einen Spurzugriff je Block — das ist
                // genau der Preis, den die Verzeichnissuche NICHT zahlen soll
                // (an einer physischen Diskette zoege sie sonst die ganze
                // Scheibe ein, obwohl sie nur ins Verzeichnis sehen wollte).
                if (level == FsRecoverLevel::Oberflaeche) {
                    std::vector<uint8_t> puffer;
                    blockLesen(blk, puffer, schadhaft);
                }
            }
        }

        // Fehlende Extents: die Datei faengt erst spaeter an oder hat ein Loch.
        const int schritt = static_cast<int>(ext_per_entry_);
        bool luecke = teile.front().extent >= schritt;
        for (size_t k = 1; k < teile.size(); ++k)
            if (teile[k].extent - teile[k - 1].extent > schritt) luecke = true;

        if (schadhaft)
            karies.push_back(std::to_string(schadhaft)
                             + " Sektor(en) tragen eine falsche Pruefsumme");
        if (luecke)
            wild.push_back("es fehlen Verzeichnisplaetze — der Fund hat Loecher");

        belegAnhaengen(f.detail, streit, "Bloecke");
        belegAnhaengen(f.detail, wild,   "Stellen");
        belegAnhaengen(f.detail, karies, "Stellen");

        f.quality = (!streit.empty() || !wild.empty()) ? FsRecoverQuality::Bruchstueck
                  : (!karies.empty())                  ? FsRecoverQuality::Wahrscheinlich
                                                       : FsRecoverQuality::Sicher;

        // Auf die Diskette zurueck darf nur, was keiner lebenden Datei ins Gehege
        // kommt — weder ueber die Bloecke noch ueber den Namen.
        if (!streit.empty())
            f.warum_nicht = "Bloecke des Fundes gehoeren inzwischen einer anderen Datei —"
                            " ein Eintrag darauf erzeugte eine Kreuzbelegung";
        else if (lebt_name.count(name))
            f.warum_nicht = "Eine Datei '" + name + "' steht bereits im Verzeichnis —"
                            " unter einem anderen Namen geht es";
        f.wiederherstellbar = f.warum_nicht.empty();

        bericht.funde.push_back(std::move(f));
    }

    // ── 2. Freie Bloecke mit Inhalt (nur die Oberflaechensuche) ──────────────
    if (level == FsRecoverLevel::Oberflaeche) {
        const std::vector<bool> karte = allocationMap();
        std::vector<uint16_t>   lauf;
        std::vector<uint8_t>    lauf_daten;

        auto laufAbschliessen = [&]() {
            if (lauf.empty()) return;
            if (bericht.funde.size() >= kMaxRohbereiche + nach_name.size()) {
                lauf.clear(); lauf_daten.clear();
                return;
            }
            FsRecoverFind f;
            f.a       = 1;                       // Rohbereich
            f.volume  = 0;
            f.quality = FsRecoverQuality::Bruchstueck;   // ohne Struktur nie mehr
            f.size    = static_cast<uint64_t>(lauf.size()) * prof_.block_size;
            f.type    = einordnung(lauf_daten);
            for (uint16_t blk : lauf) f.teile.push_back(blk);
            ortVonBlock(lauf.front(), f.cyl, f.head, f.sector_index);

            char wo[48];
            std::snprintf(wo, sizeof wo, "fragment_c%dh%d_b%u-b%u.bin", f.cyl, f.head,
                          static_cast<unsigned>(lauf.front()),
                          static_cast<unsigned>(lauf.back()));
            f.vorschlag = wo;
            f.origin    = "freier Bereich, Block " + std::to_string(lauf.front())
                        + (lauf.size() > 1 ? "…" + std::to_string(lauf.back()) : "");
            f.detail    = "Kein Verzeichnisplatz — Name und Laenge sind unbekannt;"
                          " gerettet wird der Rohinhalt (" + f.type + ")";
            f.warum_nicht = "Ein Rohbereich hat keinen Verzeichnisplatz, den man"
                            " zurueckholen koennte — herausholen laesst er sich aber";
            bericht.funde.push_back(std::move(f));
            lauf.clear();
            lauf_daten.clear();
        };

        for (uint16_t blk = static_cast<uint16_t>(dir_blocks_); blk < total_blocks_; ++blk) {
            const bool belegt = blk < karte.size() && karte[blk];
            if (belegt || vergeben.count(blk)) { laufAbschliessen(); continue; }

            std::vector<uint8_t> daten;
            int schadhaft = 0;
            if (!blockLesen(blk, daten, schadhaft) || daten.empty()) {
                laufAbschliessen();
                continue;
            }
            // Ein Block aus EINEM immer gleichen Byte ist Fuellmuster, kein Inhalt —
            // und zwar unabhaengig davon, welches (FORMAT.COM fuellt je nach
            // Menuepunkt mit 0xE5, 0xF6 oder dem Pruefmuster 0x53).
            bool einerlei = true;
            for (uint8_t b : daten) if (b != daten[0]) { einerlei = false; break; }
            if (einerlei) { laufAbschliessen(); continue; }

            lauf.push_back(blk);
            if (lauf_daten.size() < 4096)
                lauf_daten.insert(lauf_daten.end(), daten.begin(),
                                  daten.begin() + static_cast<long>(
                                      std::min<size_t>(daten.size(), 4096)));
        }
        laufAbschliessen();
    }

    bericht.sortieren();
    return bericht;
}

// ─── Lesen ───────────────────────────────────────────────────────────────────

bool CpmFileSystem::recoverRead(const FsRecoverFind& f, std::vector<uint8_t>& out) const {
    out.clear();

    if (f.a == 1) {                                   // Rohbereich
        for (int blk : f.teile)
            if (!readBlock(static_cast<uint16_t>(blk), out)) return false;
        return true;
    }

    std::vector<uint8_t> roh;
    if (!directoryRaw(roh)) return false;

    // Die Plaetze frisch aus dem Verzeichnis nehmen: zwischen Suchlauf und Rettung
    // kann geschrieben worden sein, und dann gilt der Zettel von vorhin nicht mehr.
    struct Teil { int extent; uint8_t records; std::vector<uint16_t> blocks; };
    std::vector<Teil> teile;
    for (int slot : f.teile) {
        if (slot < 0 || static_cast<size_t>(slot) * 32 + 32 > roh.size())
            return fail("Verzeichnisplatz " + std::to_string(slot) + " gibt es nicht");
        const uint8_t* p = roh.data() + static_cast<size_t>(slot) * 32;
        Teil t;
        t.extent  = (p[12] & 0x1F) + (p[14] & 0x3F) * 32;
        t.records = p[15];
        if (wide_ptr_)
            for (int k = 0; k < 8; ++k)
                t.blocks.push_back(static_cast<uint16_t>(p[16 + 2 * k] | (p[17 + 2 * k] << 8)));
        else
            for (int k = 0; k < 16; ++k) t.blocks.push_back(p[16 + k]);
        teile.push_back(std::move(t));
    }
    if (teile.empty()) return fail("Der Fund nennt keinen Verzeichnisplatz");

    std::sort(teile.begin(), teile.end(),
              [](const Teil& a, const Teil& b) { return a.extent < b.extent; });
    const uint64_t laenge = static_cast<uint64_t>(teile.back().extent) * kExtentBytes
                          + static_cast<uint64_t>(teile.back().records) * kRecordBytes;

    for (const Teil& t : teile) {
        const uint64_t anfang = static_cast<uint64_t>(t.extent & ~(ext_per_entry_ - 1))
                              * kExtentBytes;
        // Fehlende Teile werden aufgefuellt, damit die Offsets der uebrigen stimmen
        // (§13.3) — fuer ein Textdokument ist das brauchbar, fuer ein Programm nicht,
        // und genau das steht im Beiblatt der Rettung.
        if (out.size() < anfang) out.resize(static_cast<size_t>(anfang), 0xE5);

        for (uint16_t blk : t.blocks) {
            if (blk == 0 || blk >= total_blocks_ || blk < dir_blocks_) {
                out.resize(out.size() + prof_.block_size, 0xE5);
                continue;
            }
            if (!readBlock(blk, out)) return false;
        }
    }
    if (laenge && out.size() > laenge) out.resize(static_cast<size_t>(laenge));
    return true;
}

// ─── Wiederherstellen ────────────────────────────────────────────────────────

bool CpmFileSystem::recoverRestore(const FsRecoverFind& f, const std::string& name) {
    if (f.a != 0)
        return fail("Ein Rohbereich laesst sich nicht wieder eintragen — er hat keinen"
                    " Verzeichnisplatz.  Herausholen geht.");

    const std::string ziel = name.empty() ? f.name : name;
    std::string warum;
    if (!validName(ziel, &warum)) return fail("'" + ziel + "': " + warum);

    std::vector<uint8_t> roh;
    if (!directoryRaw(roh)) return false;

    // ── Noch einmal alles nachpruefen, unmittelbar vor dem Schreiben (§13.3) ──
    std::map<uint16_t, std::string> lebend;
    for (const CpmDirEntry& d : directory()) {
        if (d.free() || d.user > 15) continue;
        if (d.name == ziel)
            return fail("Eine Datei '" + ziel + "' steht bereits im Verzeichnis");
        for (uint16_t blk : d.blocks)
            if (blk != 0 && blk < total_blocks_) lebend.emplace(blk, d.name);
    }

    for (int slot : f.teile) {
        if (slot < 0 || static_cast<size_t>(slot) * 32 + 32 > roh.size())
            return fail("Verzeichnisplatz " + std::to_string(slot) + " gibt es nicht");
        const uint8_t* p = roh.data() + static_cast<size_t>(slot) * 32;
        if (p[0] != 0xE5)
            return fail("Verzeichnisplatz " + std::to_string(slot)
                        + " ist inzwischen wieder vergeben");
        if (rohName(p) != f.name)
            return fail("Auf Verzeichnisplatz " + std::to_string(slot)
                        + " steht nicht mehr '" + f.name + "'");
        const int n = ptrs_per_entry_;
        for (int k = 0; k < n; ++k) {
            const uint16_t blk = wide_ptr_
                ? static_cast<uint16_t>(p[16 + 2 * k] | (p[17 + 2 * k] << 8))
                : p[16 + k];
            if (blk == 0) continue;
            const auto it = lebend.find(blk);
            if (it != lebend.end())
                return fail("Block " + std::to_string(blk) + " gehoert inzwischen zu '"
                            + it->second + "' — ein Eintrag darauf erzeugte eine"
                              " Kreuzbelegung");
        }
    }

    // ── Schreiben: EIN Byte je Platz, plus den Namen, wenn umbenannt wird ────
    //
    // Der urspruengliche Nutzerbereich stand in ebendem Byte, das 0xE5 geworden
    // ist; wiederhergestellt wird deshalb nach Bereich 0.
    const size_t punkt = ziel.find('.');
    const std::string basis = punkt == std::string::npos ? ziel : ziel.substr(0, punkt);
    const std::string typ   = punkt == std::string::npos ? "" : ziel.substr(punkt + 1);

    for (int slot : f.teile) {
        uint8_t* p = roh.data() + static_cast<size_t>(slot) * 32;
        p[0] = 0;
        if (ziel != f.name) {
            // Die Hochbits sind die Attribute (R/O, SYS, ARCHIV) und gehoeren nicht
            // zum Namen — sie bleiben stehen.
            for (int k = 0; k < 8; ++k)
                p[1 + k] = static_cast<uint8_t>((p[1 + k] & 0x80)
                           | (k < static_cast<int>(basis.size()) ? basis[static_cast<size_t>(k)] : ' '));
            for (int k = 0; k < 3; ++k)
                p[9 + k] = static_cast<uint8_t>((p[9 + k] & 0x80)
                           | (k < static_cast<int>(typ.size()) ? typ[static_cast<size_t>(k)] : ' '));
        }
        if (!writeDirEntry(slot, p)) return false;
    }
    return true;
}
