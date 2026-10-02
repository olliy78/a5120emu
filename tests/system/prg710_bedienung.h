/**
 * @file prg710_bedienung.h
 * @brief Bedienhelfer für die FORMAT-Wächter des PRG 710 / 710-1 (doc/design/20_prg710.md
 *        AP-P5f): Bild lesen, auf Fragen warten, UDOS bzw. SCPX booten, `FORMAT` bedienen —
 *        über die Tastatur der Variante (8279 + K7609 bzw. K7672), wie ein Anwender.
 *        Benutzt von `tests/integration/test_prg710_format.cpp` und
 *        `tests/system/test_prg710_format_boot.cpp`.
 *
 * **Bedienregel aus AP-P5f — vor `READY? Y` Zeit lassen.**  UDOS-`FORMAT` wählt das
 * Laufwerk selbst (`OUT (18H)`, 5897H) und wartet dann ohne Interruptsperre bis zu
 * ≈ 1 s auf Spur 0 und /RDYL.  Läuft in dieser Zeit noch der Motornachlauf des Residenten
 * vom Laden des Programms (CTC K3, ≈ 2 s nach dem letzten Diskettenzugriff), schaltet
 * dessen ISR alle Laufwerke ab (`OUT (18H),FFH`, 710: 0A27H, 710-1: 0A57H) — FORMAT
 * meldet „ERROR C2“ und kehrt zurück.  Am Gerät liegt zwischen Laden und `Y` immer das
 * Einlegen der Diskette; die Helfer lassen dafür 8 Mio. Takte (≈ 3,3 s) laufen.
 */
#pragma once

#include <string>

#include "core/machines/prg710/prg710.h"

namespace prg710test {

using V = Prg710Machine::Config::Variante;

constexpr int       kSchritt  = 20'000;
constexpr long long kBoot     = 120'000'000;   // Netz-Ein → Datumsabfrage bzw. `A>`
constexpr long long kBefehl   =  60'000'000;   // je Kommando
constexpr long long kFormat   = 400'000'000;   // eine ganze Diskettenseite bzw. Diskette
constexpr long long kEinlegen =   8'000'000;   // „Diskette einlegen“ vor READY? Y
constexpr uint32_t  QK_RETURN = 0x01000004;    // Qt::Key_Return (710: ET1 = 37H, 710-1: 0DH)

inline const char* udosDiskette(V v) {
    return v == V::Prg710_1 ? "prg710-1_udos43_k5601_v43_189.hfe"
                            : "prg710_udos43_k5601_mrs_boot.hfe";
}
inline const char* udosSystemzeile(V v) {
    return v == V::Prg710_1 ? "UDOS PG710-1" : "UDOS PRG710";
}
inline const char* scpxDiskette(V v) {
    return v == V::Prg710_1 ? "prg710-1_scpx17_cpa640_boot.hfe"
                            : "prg710_scpx15_cpa640_sysprg.hfe";
}
inline const char* scpxGruss(V v) {
    return v == V::Prg710_1 ? "SCPX 1526 - V 1.7 (52K)" : "SCPX  V 1.5  B. Daehmlow";
}
inline const char* name(V v) { return v == V::Prg710 ? "Prg710" : "Prg710_1"; }

inline std::string zeile(Prg710Machine& m, int r) {
    std::string s;
    for (int c = 0; c < 80; ++c) {
        const uint8_t ch = static_cast<uint8_t>(m.screenChar(c, r) & 0x7F);
        s += (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
inline std::string bild(Prg710Machine& m) {
    std::string s;
    for (int r = 0; r < 24; ++r) s += zeile(m, r) + "\n";
    return s;
}
inline bool enthaelt(Prg710Machine& m, const std::string& t) {
    return bild(m).find(t) != std::string::npos;
}
/// Letzte nicht leere Bildzeile.
inline std::string letzteZeile(Prg710Machine& m) {
    for (int r = 23; r >= 0; --r) {
        std::string z = zeile(m, r);
        if (!z.empty()) return z;
    }
    return {};
}
/// Vorletzte nicht leere Bildzeile.
inline std::string vorletzteZeile(Prg710Machine& m) {
    bool erste = true;
    for (int r = 23; r >= 0; --r) {
        std::string z = zeile(m, r);
        if (z.empty()) continue;
        if (!erste) return z;
        erste = false;
    }
    return {};
}

inline void lauf(Prg710Machine& m, long long n) {
    for (long long d = 0; d < n;) d += m.run(kSchritt);
}
inline bool bisText(Prg710Machine& m, const std::string& t, long long frist) {
    for (long long d = 0; d < frist;) {
        d += m.run(kSchritt);
        if (enthaelt(m, t)) return true;
    }
    return false;
}
/// Bis die letzte Zeile genau @p f ist und @p ruhe Takte so bleibt (Ausgabe fertig).
inline bool bisZeile(Prg710Machine& m, const std::string& f, long long frist,
                     long long ruhe = 500'000) {
    long long ruhig = 0;
    for (long long d = 0; d < frist;) {
        const int n = m.run(kSchritt);
        d += n;
        ruhig = (letzteZeile(m) == f) ? ruhig + n : 0;
        if (ruhig >= ruhe) return true;
    }
    return false;
}
inline bool bisUdosPrompt(Prg710Machine& m, long long frist) {
    return bisZeile(m, "%", frist, 2'000'000);
}
inline bool bisScpxPrompt(Prg710Machine& m, long long frist) {
    return bisZeile(m, "A>", frist, 2'000'000);
}

/// Eine Taste drücken und loslassen (die K7672 wiederholt eine gehaltene Taste).
inline void taste(Prg710Machine& m, uint32_t k) {
    m.keyPress(k, false, false);
    lauf(m, 100'000);
    m.keyRelease(k);
    lauf(m, 100'000);
}
inline void tippe(Prg710Machine& m, const std::string& s) {
    for (char c : s) taste(m, static_cast<uint8_t>(c));
}
/// UDOS-Kommando + ET bzw. ENTER, dann bis zum nächsten `%`.
inline bool udos(Prg710Machine& m, const std::string& k, long long frist = kBefehl) {
    tippe(m, k);
    taste(m, QK_RETURN);
    return bisUdosPrompt(m, frist);
}
/// SCPX-Kommando + ET bzw. ENTER, dann bis zum nächsten `A>`.
inline bool scpx(Prg710Machine& m, const std::string& k, long long frist = kBefehl) {
    tippe(m, k);
    taste(m, QK_RETURN);
    return bisScpxPrompt(m, frist);
}

/// Netz-Ein → Starttaste (ET bzw. ENTER) → UDOS bis „Neues Datum“; false, wenn sie nicht
/// kommt (die Diskette trägt dann kein vollständiges System).
inline bool udosBisDatum(Prg710Machine& m) {
    m.powerOn();
    lauf(m, 3'000'000);                              // ROM bis zur Tastaturabfrage
    taste(m, QK_RETURN);
    return bisText(m, "Neues Datum", kBoot);
}

/// UDOS-`FORMAT` bedienen: SYSTEMDISK? @p sys, DRIVE? @p lw, ID? @p id, (Diskette
/// einlegen), READY? Y — bis zum `%`.  Die Antworten erst, wenn die Frage steht (der
/// Zeilenleser des OS verwirft vorher getippte Zeichen).
inline bool udosFormat(Prg710Machine& m, const std::string& sys, const std::string& lw,
                       const std::string& id) {
    tippe(m, "FORMAT");
    taste(m, QK_RETURN);
    if (!bisZeile(m, "SYSTEMDISK?", kBefehl)) return false;
    tippe(m, sys); taste(m, QK_RETURN);
    if (!bisZeile(m, "DRIVE?", kBefehl)) return false;
    tippe(m, lw);  taste(m, QK_RETURN);
    if (!bisZeile(m, "ID?", kBefehl)) return false;
    tippe(m, id);  taste(m, QK_RETURN);
    if (!bisZeile(m, "READY?", kBefehl)) return false;
    lauf(m, kEinlegen);
    tippe(m, "Y"); taste(m, QK_RETURN);
    return bisUdosPrompt(m, kFormat);
}

/// SCPX-`FORMAT.COM` (V 1.5) auf B:, Format @p format ('0' … '4', '\r' = Vorgabe 0 =
/// 16 × 256 DS), mit oder ohne Systemkopie, „ONCE MORE? N“ — bis `A>`.
inline bool scpxFormatB(Prg710Machine& m, char format, bool systemkopie) {
    tippe(m, "FORMAT");
    taste(m, QK_RETURN);
    if (!bisText(m, "PLEASE ENTER DRIVE NAME (A/B):", kBefehl)) return false;
    tippe(m, "B");                                   // ohne ENTER (BDOS 1)
    if (!bisText(m, "HIT <ENTER> FOR DEFAULT:", kBefehl)) return false;
    if (format == '\r') taste(m, QK_RETURN); else tippe(m, std::string(1, format));
    if (!bisText(m, "READY? (Y)", kBefehl)) return false;
    lauf(m, kEinlegen);
    tippe(m, "Y");
    // Die Systemkopie bietet FORMAT nur bei 16 × 256 an (5 × 1024: gleich „ONCE MORE“).
    bool gefragt = false;
    for (long long d = 0; !enthaelt(m, "ONCE MORE ? (Y/N)"); d += m.run(kSchritt)) {
        if (d > 2 * kFormat) return false;           // Formatieren + Systemkopie
        if (!gefragt && enthaelt(m, "DO YOU WANT A SYSTEMCOPY?:")) {
            // Erst die Ausgabe zu Ende laufen lassen: das BDOS fragt beim Schreiben die
            // Tastatur ab (^S) und verschluckt ein zu früh getipptes Zeichen.
            lauf(m, 2'000'000);
            tippe(m, systemkopie ? "Y" : "N");
            gefragt = true;
        }
    }
    if (systemkopie && !gefragt) return false;
    lauf(m, 2'000'000);
    tippe(m, "N");
    return bisScpxPrompt(m, kBefehl);
}

}  // namespace prg710test
