/**
 * @file prg_boot.cpp
 * @brief Siehe prg_boot.h.
 *
 * Belege der Unterscheidung (Abzüge aus doc/design/20_prg710.md §5.4, nachgemessen
 * 2026-10-02 an den Fixtures `prg710_*`/`prg710-1_*`):
 *
 *  - **UDOS:** Spur 2 trägt den Zweitlader (1000H).  Er setzt den IM-2-Vektor des CTC
 *    Kanal 3: `LD HL,09FFH` (710) bzw. `LD HL,0A2FH` (710-1), danach `LD (0FE6H),HL`
 *    → Bytefolge `21 FF 09 22 E6 0F` bzw. `21 2F 0A 22 E6 0F` (Zweitlader Offset 14H;
 *    `doc/prg710/resident.md` §2).  Der A5120-UDOS-Lader trägt sie nicht.
 *  - **SCPX:** das BIOS ab Byte 6144 greift auf die Tastatur zu — 710 (V1.5 und V1.7/K7609)
 *    über den 8279 (`IN (C8H)`, `IN/OUT (C9H)`), 710-1 über die K7672 an der K8025
 *    (`LD C,5EH`, `IN (5FH)`).  Gemessen: `B152V24` 5 Zugriffe C8H/C9H, 0 auf 5EH/5FH;
 *    `B17272V2` 0 bzw. 2; `B152IFSS` (710) 5 bzw. 4 — die IFSS-Ports 5EH/5FH sind also
 *    allein kein Merkmal.
 */
#include "core/filesystem/prg_boot.h"

#include <algorithm>

namespace prg_boot {
namespace {

constexpr size_t kBiosAb      = 6144;   ///< DE00H − C600H
constexpr size_t kCcpAb       = 512;    ///< C800H − C600H
constexpr size_t kCcpLaenge   = kBiosAb - kCcpAb;   ///< 5632 B = CCPBD17.SYS
constexpr size_t kSpurBytes   = 16384;  ///< Systemspuren von `scpx640`

bool hatLader(const std::vector<uint8_t>& b) {
    return b.size() >= 5 && b[0] == 0x18 && b[1] == 0x03
        && b[2] == 'S' && b[3] == 'Y' && b[4] == 'L';
}

/// @brief Kommt @p muster ab irgendeiner Stelle von @p b ab @p von vor?
bool enthaelt(const std::vector<uint8_t>& b, size_t von, const std::vector<uint8_t>& muster) {
    if (b.size() < von + muster.size()) return false;
    return std::search(b.begin() + static_cast<long>(von), b.end(),
                       muster.begin(), muster.end()) != b.end();
}

Variante udosVariante(const std::vector<uint8_t>& b) {
    const bool a = enthaelt(b, 0, {0x21, 0xFF, 0x09, 0x22, 0xE6, 0x0F});
    const bool c = enthaelt(b, 0, {0x21, 0x2F, 0x0A, 0x22, 0xE6, 0x0F});
    if (a == c) return Variante::Unbekannt;
    return a ? Variante::Prg710 : Variante::Prg710_1;
}

Variante scpxVariante(const std::vector<uint8_t>& b, size_t bios_ab) {
    // Der 8279 (C8H/C9H) gehoert allein dem PRG 710.  Die Ports 5EH/5FH sind KEIN Merkmal
    // des 710-1: das V1.5-BIOS `B152IFSS` des 710 greift dort auf die IFSS-Schnittstelle zu
    // (gemessen: `IN (5EH)`, `IN (5FH)`, `LD C,5FH`, `OUT (5EH)`).  Deshalb entscheidet die
    // Tastatur: 8279 da → 710; sonst 5EH/5FH da → 710-1 (K7672 an der K8025).
    int p8279 = 0, pk8025 = 0;
    for (size_t i = bios_ab; i + 1 < b.size(); ++i) {
        const uint8_t op = b[i], port = b[i + 1];
        if ((op == 0xDB || op == 0xD3) && (port == 0xC8 || port == 0xC9)) ++p8279;
        if ((op == 0xDB || op == 0xD3 || op == 0x0E) && (port == 0x5E || port == 0x5F)) ++pk8025;
    }
    if (p8279 > 0) return Variante::Prg710;
    if (pk8025 > 0) return Variante::Prg710_1;
    return Variante::Unbekannt;
}

}  // namespace

const char* name(Variante v) {
    switch (v) {
        case Variante::Prg710:   return "PRG 710";
        case Variante::Prg710_1: return "PRG 710-1";
        default:                 return "unbekannt";
    }
}

Variante ausName(const std::string& s) {
    if (s == "710")   return Variante::Prg710;
    if (s == "710-1") return Variante::Prg710_1;
    return Variante::Unbekannt;
}

Kennung erkenne(const std::vector<uint8_t>& img, System sys) {
    Kennung k;
    if (!hatLader(img)) return k;
    k.ist_prg  = true;
    k.variante = (sys == System::Udos) ? udosVariante(img)
               : (img.size() > kBiosAb ? scpxVariante(img, kBiosAb) : Variante::Unbekannt);
    return k;
}

std::string problem(const std::vector<uint8_t>& img, System sys, Variante erwartet,
                    Variante vorhanden) {
    const Kennung k = erkenne(img, sys);
    if (!k.ist_prg) {
        if (erwartet == Variante::Unbekannt) return {};
        return "Das Bootabbild traegt keinen PRG-Lader (Byte 0–4 = 18 03 'SYL') — es stammt "
               "nicht von einer Systemdiskette des PRG 710/710-1.";
    }
    // Ein PRG-Abbild muss die Teile tragen, ohne die der Kaltstart nicht bis zum
    // Betriebssystem kommt; sonst bliebe eine halbe Systemdiskette zurueck.
    if (sys == System::Scpx && img.size() <= kBiosAb)
        return "Das SCPX-Bootabbild endet vor dem BIOS (" + std::to_string(img.size())
             + " Byte, das BIOS beginnt bei Byte " + std::to_string(kBiosAb) + ").";
    if (sys == System::Udos && img.size() < 3 * 26 * 128)
        return "Das UDOS-Bootabbild endet vor dem Zweitlader (Spur 2, " + std::to_string(img.size())
             + " Byte) — der Lader findet ihn nicht.";

    if (k.variante == Variante::Unbekannt) {
        if (erwartet != Variante::Unbekannt)
            return "Fuer welches Geraet das Bootabbild gebaut ist, laesst sich nicht erkennen — "
                   "es passt nicht sicher zum " + std::string(name(erwartet)) + ".";
        return {};
    }
    if (erwartet != Variante::Unbekannt && k.variante != erwartet)
        return "Das Bootabbild ist fuer den " + std::string(name(k.variante)) + " gebaut, "
               "verlangt war der " + name(erwartet) + " (Tastatur- und MKE-Treiber "
               "unterscheiden sich; es bootet dort nicht).";
    if (vorhanden != Variante::Unbekannt && vorhanden != k.variante)
        return "Die Diskette traegt schon ein System fuer den " + std::string(name(vorhanden))
             + "; das Bootabbild ist fuer den " + name(k.variante)
             + " gebaut. Eine neue Diskette anlegen, statt die Systeme zu mischen.";
    return {};
}

bool scpxBand(const std::vector<uint8_t>& syl, const std::vector<uint8_t>& ccp,
              const std::vector<uint8_t>& bios, Variante ziel,
              std::vector<uint8_t>& out, Variante& erkannt, std::string& err) {
    if (syl.size() != 256 || !hatLader(syl)) {
        err = "SYL17.SYS muss 256 Byte lang sein und mit dem Lader (18 03 'SYL') beginnen.";
        return false;
    }
    if (ccp.size() != kCcpLaenge) {
        err = "CCPBD17.SYS muss " + std::to_string(kCcpLaenge) + " Byte lang sein (C800H–DDFFH), "
              "ist aber " + std::to_string(ccp.size()) + " Byte lang.";
        return false;
    }
    // Ein BIOS beginnt bei DE00H mit der Sprungleiste; der Kaltstart-Sprung `JP DF84H`
    // oder ein anderer `JP` an der Spitze genuegt als Gegenprobe gegen eine falsche Datei.
    if (bios.size() < 3328 || bios.size() > kSpurBytes - kBiosAb || bios[0] != 0xC3) {
        err = "Das BIOS (" + std::to_string(bios.size()) + " Byte) sieht nicht wie ein "
              "SCPX-BIOS aus (3328–10240 Byte, beginnt mit JP).";
        return false;
    }
    erkannt = scpxVariante(bios, 0);
    if (erkannt == Variante::Unbekannt) {
        err = "Aus dem BIOS laesst sich das Geraet nicht ablesen (weder 8279 C8H/C9H noch "
              "K7672 5EH/5FH).";
        return false;
    }
    if (ziel != Variante::Unbekannt && ziel != erkannt) {
        err = "Das BIOS ist fuer den " + std::string(name(erkannt)) + " gebaut, verlangt war der "
              + name(ziel) + " (710: B15x/B17x09, 710-1: B17x72).";
        return false;
    }
    out.assign(kBiosAb, 0xAA);
    std::copy(syl.begin(), syl.end(), out.begin());
    std::copy(ccp.begin(), ccp.end(), out.begin() + static_cast<long>(kCcpAb));
    out.insert(out.end(), bios.begin(), bios.end());
    while (out.size() % 256) out.push_back(0xAA);
    return true;
}

}  // namespace prg_boot
