/**
 * @file cpa_dpb.cpp
 * @brief Umsetzung von @ref CpaDpbRule — Vorlage ist `biosdsk.mac`, Marke `drdfrm`.
 *
 * Die Kommentare nennen die Marken des Originals, damit ein Vergleich moeglich bleibt.
 *
 * @see doc/cpa_format_detection.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/cpm/cpa_dpb.h"

#include <algorithm>
#include <array>

namespace {

/// @brief Die Blockgroessentabellen `dbl1k` / `dbl2k` / `dbl2k0` (Shift → Bytes).
constexpr uint32_t k1K = 1024;   ///< `dbl1k`  db 3,7,0
constexpr uint32_t k2K = 2048;   ///< `dbl2k`  db 4,0fh,1  und `dbl2k0` db 4,0fh,0

/**
 * @brief `dtrsl0..3` — vier Tabellen à sechs Zeilen.
 *
 * Reihenfolge der Zeilen wie im BIOS: 5″ 40 SS, 5″ 80 SS, 5″ 40 DS, 5″ 80 DS,
 * dann (`if disk8`) 8″ 77 SS FM und (`if dsk8mf`) 8″ 77 SS MFM.
 *
 * Die Zahlen sind 1:1 die `db`-Zeilen; `dsldir` ist dort Eintraege**−1**, hier bereits
 * die Anzahl.  `2*N+dslfo` ist zerlegt in @ref CpaDpbEntry::sys_tracks und
 * @ref CpaDpbEntry::fixed_off.
 *
 * **Bekannte Eigenheit der Quelle:** die 8″-MFM-Zeile der 256-B-Tabelle steht dort als
 * `db 77,52,127,2*2,dslfo,dbl2k-$` — mit Komma statt `+`, also SECHS Bytes.  Der Eintrag
 * ist damit im Original verschoben und praktisch unbenutzbar; hier steht die offenkundig
 * gemeinte Fassung (`2*2+dslfo`).
 */
constexpr std::array<std::array<CpaDpbEntry, 6>, 4> kTabellen{{
    // ── dtrsl0: 128-B-Sektoren ───────────────────────────────────────────────
    {{
        { 64, 2, false, k1K },   // 40,26, 63,2*2+dslvo,dbl1k    CP/M Standard
        {128, 2, false, k2K },   // 80,26,127,2*2+dslvo,dbl2k
        {128, 0, true,  k2K },   // 80,26,127,2*0+dslfo,dbl2k
        {128, 0, true,  k2K },   // 160,26,127,2*0+dslfo,dbl2k0
        { 64, 2, false, k1K },   // 77,26, 63,2*2+dslvo,dbl1k    8″ CP/M Standard
        {128, 2, false, k2K },   // 77,40,127,2*2+dslvo,dbl2k    8″ MFM
    }},
    // ── dtrsl1: 256-B-Sektoren ───────────────────────────────────────────────
    {{
        { 64, 3, true,  k2K },   // 40,32, 63,2*3+dslfo,dbl2k    SCP Hausformat A51xx
        { 64, 3, true,  k2K },   // 80,32, 63,2*3+dslfo,dbl2k    SCP
        {128, 4, true,  k2K },   // 80,32,127,2*4+dslfo,dbl2k
        {128, 4, true,  k2K },   // 160,32,127,2*4+dslfo,dbl2k0  SCP Hausformat PC1715
        { 64, 3, true,  k2K },   // 77,32, 63,2*3+dslfo,dbl2k
        {128, 2, true,  k2K },   // 77,52,127,2*2 dslfo,dbl2k    (Quelle fehlerhaft, s.o.)
    }},
    // ── dtrsl2: 512-B-Sektoren ───────────────────────────────────────────────
    // Die 5″-Zeilen tragen dsldir=0 — CP/A legt auf 512-B-5″-Disketten KEIN
    // brauchbares Verzeichnis an (ein einziger Platz).  Das ist kein Uebertragungs-
    // fehler, sondern der Stand des Originals; solche Disketten sind reine
    // Fremdformate, die CP/A nur physisch lesen kann.
    {{
        {  1, 0, true,  k1K },   // 40,36,  0,2*0+dslfo,dbl1k
        {  1, 0, true,  k2K },   // 80,36,  0,2*0+dslfo,dbl2k
        {  1, 0, true,  k2K },   // 80,36,  0,2*0+dslfo,dbl2k
        {  1, 0, true,  k2K },   // 160,36, 0,2*0+dslfo,dbl2k0
        {128, 2, false, k2K },   // 77,36,127,2*2+dslvo,dbl2k    Hausformat IH Mittweida
        {128, 2, false, k2K },   // 77,64,127,2*2+dslvo,dbl2k0   8″ MFM
    }},
    // ── dtrsl3: 1024-B-Sektoren ──────────────────────────────────────────────
    {{
        { 64, 2, false, k1K },   // 40,40, 63,2*2+dslvo,dbl1k    CP/A Standard A51xx
        {128, 2, false, k2K },   // 80,40,127,2*2+dslvo,dbl2k    CP/A
        {128, 0, false, k2K },   // 80,40,127,2*0+dslvo,dbl2k    CP/A
        {192, 4, false, k2K },   // 160,40,191,2*4+dslvo,dbl2k0  CP/A (und SCP)
        { 64, 3, false, k2K },   // 77,32, 63,2*3+dslvo,dbl2k    CP/A und SCP
        {128, 2, false, k2K },   // 77,64,127,2*2+dslvo,dbl2k0   8″ MFM
    }},
}};

/// @brief Sektorlaenge → Laengencode (`dbslc`); 0xFF bei unzulaessiger Laenge.
uint8_t laengencode(uint16_t bytes) {
    switch (bytes) {
        case 128:  return 0;
        case 256:  return 1;
        case 512:  return 2;
        case 1024: return 3;
        default:   return 0xFF;
    }
}

/// @brief Was Spur 0 ueber Systemspuren aussagt (`selsy0`/`selsy1`/`selsy2`).
enum class Spur0 {
    KeinVerzeichnis,   ///< dpbofs bleibt 0 → Tabellenwert benutzen
    Verzeichnis,       ///< dpbofs=255      → 0 Systemspuren
    Leer               ///< dpbofs=254      → an der Datenspur nachsehen
};

/// @brief Die Fallunterscheidung ab `ld a,(iy+0)` — auf den ersten 128 Bytes der Spur 0.
Spur0 beurteileSpur0(const uint8_t* s) {
    if (s[0] == 0xE5) return (s[1] == 0xE5) ? Spur0::Leer : Spur0::Verzeichnis;
    if (s[0] == 0x40) return Spur0::KeinVerzeichnis;   // IBM-/SIOS-Datendiskette
    if (s[0] >= 0x20) return Spur0::KeinVerzeichnis;   // Lader ("SYL"), kein Nutzerbereich
    // Nutzerbereich 0…31: nur dann ein Verzeichnis, wenn der zweite Platz belegt ist
    // (bei SCP1715/MicroDos steht dort 00 00).
    return (s[0x20] == 0 && s[0x21] == 0) ? Spur0::KeinVerzeichnis : Spur0::Verzeichnis;
}

/// @brief Erste 128 Bytes der logischen Spur @p log_track lesen; false = nicht lesbar.
bool leseSatz(const SectorSpace& space, uint16_t log_track, uint8_t* dst) {
    if (log_track >= space.trackCount()) return false;
    const SectorSpace::TrackRef t = space.trackAt(log_track);
    SectorData sec;
    if (!space.readSector(t.cyl, t.head, t.first_id, sec)) return false;
    if (sec.data.size() < 128) return false;
    std::copy(sec.data.begin(), sec.data.begin() + 128, dst);
    return true;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────

CpaDpbEntry CpaDpbRule::entry(uint8_t size_code, uint8_t row) {
    if (size_code > 3 || row > 5) return {};
    return kTabellen[size_code][row];
}

bool CpaDpbRule::derive(const DiskFormat& fmt, const SectorSpace& space,
                        CpaDpb& out, std::string* why) {
    auto fehler = [&](const std::string& t) { if (why) *why = t; return false; };

    const uint8_t koepfe = fmt.numHeads();
    if (koepfe == 0 || space.trackCount() == 0) return fehler("Geometrie ohne Spuren");

    // ── Datenspur bestimmen ──────────────────────────────────────────────────
    // Das BIOS analysiert `dlgint` = 3 — „groesste Anzahl SS-Systemspuren", also
    // **Zylinder 3, Kopf 0**: der erste Aufruf `dsidtt(dlgint)` laeuft, bevor
    // `dpbfds` gesetzt ist, adressiert also einseitig (die Rueckseitenprobe danach
    // heisst nicht umsonst `1+dlgint*2`).  Ab dort muss die Diskette einheitlich
    // sein.  Hat sie weniger Zylinder, nehmen wir den letzten.
    int datenspur = space.trackIndexOf(
        static_cast<uint8_t>(std::min<int>(3, fmt.numCylinders() - 1)), 0);
    if (datenspur < 0) datenspur = static_cast<int>(space.trackCount()) - 1;
    const SectorSpace::TrackRef daten = space.trackAt(static_cast<size_t>(datenspur));

    out.size_code = laengencode(daten.sector_size);
    if (out.size_code > 3)
        return fehler("Sektorlaenge " + std::to_string(daten.sector_size)
                    + " B kennt die CP/A-Erkennung nicht");

    // Einheitlich heisst: gleiche Sektorgroesse UND gleiche Sektorzahl.  Ein
    // 8″-Format mit 26×128 FM davor und 40×128 MFM dahinter hat dieselbe
    // Sektorlaenge und waere sonst „einheitlich" — CP/M rechnet aber mit einer
    // festen Spurkapazitaet.
    auto gleiche_spur = [&](const SectorSpace::TrackRef& t) {
        return t.sector_size == daten.sector_size && t.sectors == daten.sectors;
    };
    for (size_t i = static_cast<size_t>(datenspur); i < space.trackCount(); ++i)
        if (!gleiche_spur(space.trackAt(i)))
            return fehler("Datenbereich ist nicht einheitlich (Spur c"
                        + std::to_string(space.trackAt(i).cyl) + "h"
                        + std::to_string(space.trackAt(i).head) + ")");

    // ── Tabellenzeile waehlen (`sel5zl` / `seld40` / `seldss`) ───────────────
    const uint8_t zylinder = fmt.numCylinders();
    const TrackFormat* tf   = fmt.findTrack(daten.cyl, daten.head);
    const bool         mfm  = tf && tf->encoding == Encoding::MFM;

    if (zylinder >= 76 && zylinder <= 78 && koepfe == 1) {
        out.row = mfm ? CpaDpb::Mfm8 : CpaDpb::Fm8;
    } else {
        out.row = (zylinder <= 40) ? CpaDpb::Ss40 : CpaDpb::Ss80;
        if (koepfe > 1) out.row = static_cast<uint8_t>(out.row + 2);
    }

    const CpaDpbEntry e = entry(out.size_code, out.row);
    out.block_size = e.block_size;

    // ── Systemspuren aufloesen (`seldof`) ────────────────────────────────────
    uint8_t satz[128] = {};
    const bool spur0_lesbar = leseSatz(space, 0, satz);

    if (e.fixed_off || !spur0_lesbar) {
        // Festes Offset, oder Spur 0 unlesbar → „Systemspuren annehmen".
        out.sys_tracks = e.sys_tracks;
    } else {
        switch (beurteileSpur0(satz)) {
            case Spur0::KeinVerzeichnis:
                out.sys_tracks = e.sys_tracks;
                break;
            case Spur0::Verzeichnis:
                out.sys_tracks = 0;
                break;
            case Spur0::Leer: {
                // Alles 0xE5: es koennten LEERE Systemspuren sein.  Das BIOS sieht auf
                // der ersten Datenspur nach — ist auch dort Byte 14 gleich 0xE5, gibt
                // es nichts, wovor Systemspuren stehen koennten.
                uint8_t probe[128] = {};
                out.sys_tracks = e.sys_tracks;
                if (leseSatz(space, e.sys_tracks, probe) && probe[14] == 0xE5)
                    out.sys_tracks = 0;
                break;
            }
        }
    }

    // Sicherung gegen gemischte Geometrie: bei cpa780 loest eine fabrikfrische
    // (durchgehend 0xE5) Diskette auf „0 Systemspuren" auf — dort begaenne der
    // Datenbereich aber in den 128-B-Systemspuren, und CP/M rechnet mit EINER
    // Sektorlaenge je Diskette.  Das Original kommt damit ueber `dpbfsm` zurecht
    // (es zaehlt die abweichenden Spuren und verbiegt die Satzumrechnung); hier ist
    // die ehrliche Antwort, den Datenbereich dort beginnen zu lassen, wo er
    // einheitlich wird — also beim Tabellenwert.
    if (out.sys_tracks < space.trackCount()
        && !gleiche_spur(space.trackAt(out.sys_tracks)))
        out.sys_tracks = e.sys_tracks;
    // Reicht auch der Tabellenwert nicht (8″-Formate mit drei Systemspuren), dann bis
    // zur ersten Spur weitergehen, die zum Datenbereich passt — die Alternative waere,
    // die Diskette abzulehnen, obwohl sie ein lesbares Dateisystem traegt.
    while (out.sys_tracks < space.trackCount()
           && !gleiche_spur(space.trackAt(out.sys_tracks)))
        ++out.sys_tracks;
    if (out.sys_tracks >= space.trackCount())
        return fehler("Datenbereich beginnt nicht auf einer "
                    + std::to_string(daten.sectors) + "×"
                    + std::to_string(daten.sector_size) + "-Spur");

    // `selddr`: eine Diskette MIT Systemspuren bekommt hoechstens 128 Eintraege —
    // die 780K-Bootdiskette waere sonst mit 192 Eintraegen unvertraeglich.
    out.dir_entries = e.dir_entries;
    if (out.sys_tracks != 0 && out.dir_entries >= 192) out.dir_entries = 128;

    // ── Sektorversatz (`seldsv`) ─────────────────────────────────────────────
    // Nur 128-B-Sektoren bekommen die Tabelle `xlt` = 1,7,13,…; sie entspricht
    // Versatz 6.  Ein als MFM betriebenes 8″-Laufwerk bekommt keinen Versatz.
    out.skew = (out.size_code == 0 && out.row != CpaDpb::Mfm8) ? 6 : 0;

    // ── logische Systemspur → (Zylinder, Kopf) ───────────────────────────────
    out.data_cyl  = static_cast<uint8_t>(out.sys_tracks / koepfe);
    out.data_head = static_cast<uint8_t>(out.sys_tracks % koepfe);
    return true;
}

bool CpaDpbRule::profile(const DiskFormat& fmt, const SectorSpace& space,
                         FsProfile& out, std::string* why) {
    CpaDpb d;
    if (!derive(fmt, space, d, why)) return false;

    out             = FsProfile{};
    out.name        = kName;
    out.description = "nach der CP/A-Regel abgeleitet — " + describe(d);
    out.format      = fmt.name;
    out.type        = FsType::Cpm;
    out.data_cyl    = d.data_cyl;
    out.data_head   = d.data_head;
    out.block_size  = d.block_size;
    out.dir_entries = d.dir_entries;
    out.skew        = d.skew;
    out.detect_rank = 1000;      // immer hinter jedem benannten Profil
    return true;
}

std::string CpaDpbRule::describe(const CpaDpb& d) {
    std::string t = std::to_string(d.sys_tracks) + " Systemspuren, "
                  + std::to_string(d.block_size) + "-B-Bloecke, "
                  + std::to_string(d.dir_entries) + " Verzeichnisplaetze";
    if (d.skew) t += ", Versatz " + std::to_string(d.skew);
    return t;
}
