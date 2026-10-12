/**
 * @file dbg_p8000.h
 * @brief Textbausteine des P8000 für k1520dbg (doc/design/25_p8000.md AP P12a/P12b).
 *
 * Reine Funktionen auf den Beobachtungsschnittstellen der Karten (`Z80Dma::sicht()`,
 * `P8000MmuLogik16::sicht()/probe()`, `Z8010::probe()/deskriptor()/letzteVerletzung()`,
 * `P8000Kopplung::pegel()`) — ohne Debugger-Zustand, deshalb unit-testbar
 * (tests/debugtools/test_dbg_p8000.cpp):
 *
 *  - Z80-Seite (P12a): Speicherbild je 4-KB-Seite (ADP), UA858-Register und Statusprotokoll,
 *    Kopplungsleitungen K1–K15 samt Protokoll der Zugriffe.
 *  - Z8000-Seite (P12b): UB8010-Register/Deskriptoren der drei MMUs, Steuerlogik (SCR, NBR,
 *    TRPL, IF1L), probeweise Übersetzung `xlat` und logischer/physischer Speicherzugriff
 *    (`ml`/`mp`) ohne Nebenwirkung, SEGT-Protokoll.
 *
 * Nichts davon schreibt in einen Baustein: die Übersetzung läuft über `probe()`, der Speicher
 * wird mit `peek` gelesen.
 *
 * @license MIT
 */
#pragma once
#include "core/cards/p8000/dram16.h"
#include "core/cards/p8000/karte16.h"
#include "core/cards/p8000/karte8.h"
#include "core/cards/p8000/kopplung.h"
#include "core/cards/p8000/mmu_logik16.h"
#include "core/cards/p8000/speicher8.h"
#include "core/primitives/z8010.h"
#include "core/primitives/z80_dma.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace dbgp8 {

// Formatprüfung nur unter GCC/Clang — MSVC kennt __attribute__ nicht
// (Release-Bau Windows schlug 2026-10-10 daran fehl; MinGW merkt es nicht).
#if defined(__GNUC__)
inline std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
#endif
inline std::string fmt(const char* f, ...) {
    char b[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

/// Ringprotokoll mit Zeitstempel (Maschinentakte t8) — für `dma log`, `kopp log`, `segt`.
class Protokoll {
public:
    struct Eintrag { uint64_t takt; std::string text; uint32_t n = 1; };
    explicit Protokoll(size_t kapazitaet = 512) : cap_(kapazitaet) {}
    /// Gleicher Text wie der letzte Eintrag: gefaltet (Zähler, Zeit des ersten) — Warteschleifen
    /// (`16-PIO0 rd B-Daten 23` tausendfach) füllen sonst den Ring.
    void add(uint64_t takt, std::string text) {
        ++gesamt_;
        if (!e_.empty() && e_.back().text == text) { ++e_.back().n; return; }
        if (e_.size() >= cap_) e_.pop_front();
        e_.push_back({takt, std::move(text), 1});
    }
    void clear() { e_.clear(); gesamt_ = 0; }
    const std::deque<Eintrag>& eintraege() const { return e_; }
    uint64_t gesamt() const { return gesamt_; }
    /// Die letzten @p n Einträge als Zeilen "t8=… text [×N]"; @p nurSchreiben lässt Lesezugriffe
    /// (" rd ") weg — Schreiben und Latch-Ausgaben sind die Information, Warteschleifen nicht.
    std::vector<std::string> zeilen(size_t n, bool nurSchreiben = false) const {
        std::vector<std::string> out;
        for (size_t i = e_.size(); i-- > 0 && out.size() < n;) {
            if (nurSchreiben && e_[i].text.find(" rd ") != std::string::npos) continue;
            out.push_back(fmt("t8=%-10llu %s%s", (unsigned long long)e_[i].takt, e_[i].text.c_str(),
                              e_[i].n > 1 ? fmt("  x%u", e_[i].n).c_str() : ""));
        }
        std::reverse(out.begin(), out.end());
        return out;
    }
private:
    size_t cap_;
    std::deque<Eintrag> e_;
    uint64_t gesamt_ = 0;
};

// ═══ P12a: Z80-Seite ═════════════════════════════════════════════════════════

/// Speicherbild der 8-Bit-Karte je 4-KB-Seite: `E` EPROM, `S` SRAM, `D` DRAM, `M` mehrere, `-` nichts.
inline std::string speicherbild(const P8000Speicher8& sp) {
    std::string s;
    for (int p = 0; p < 16; ++p) {
        switch (sp.selekt(uint16_t(p << 12))) {
            case 1: s += 'E'; break;
            case 2: s += 'S'; break;
            case 4: s += 'D'; break;
            case 0: s += '-'; break;
            default: s += 'M'; break;
        }
    }
    return s;
}

/// Eine Zeile je Seite: Adressbereich, ADP-Zelle, wirksamer Select (RFF!), Klartext.
inline std::vector<std::string> adpZeilen(const P8000Speicher8& sp) {
    std::vector<std::string> out;
    out.push_back(fmt("RFF=%d (%s)  map=%s   (E=EPROM S=SRAM D=DRAM M=mehrere -=leer; je 4 KB ab 0000H)",
                      sp.rffGesetzt() ? 1 : 0, sp.rffGesetzt() ? "ADP wirkt" : "EPROM ueberall selektiert",
                      speicherbild(sp).c_str()));
    out.push_back("  Seite Bereich      ADP  wirksam  Auswahl");
    for (int p = 0; p < 16; ++p) {
        const uint8_t sel = sp.selekt(uint16_t(p << 12));
        std::string w;
        if (sel & 1) w += "PROM ";
        if (sel & 2) w += "SRAM ";
        if (sel & 4) w += "DRAM ";
        if (w.empty()) w = "nichts (liest FFH)";
        out.push_back(fmt("  %-5X %04X-%04X   %X    %X       %s", p, p << 12, (p << 12) | 0xFFF,
                          sp.adp(p) & 7, sel & 7, w.c_str()));
    }
    return out;
}

/// RR0 des UA858 als Text (D1/D3/D4/D5 sind low-aktiv).
inline std::string dmaRr0Text(uint8_t s) {
    std::string t;
    t += (s & 0x01) ? "Transfer-erfolgt " : "";
    t += !(s & 0x02) ? "RDY-aktiv " : "";
    t += !(s & 0x08) ? "INT-anstehend " : "";
    t += !(s & 0x10) ? "Treffer " : "";
    t += !(s & 0x20) ? "Blockende " : "";
    if (t.empty()) t = "-";
    else t.pop_back();
    return t;
}

inline const char* dmaBetriebsart(uint8_t b) {
    return b == 0 ? "Byte" : b == 1 ? "Continuous" : b == 2 ? "Burst" : "unzulaessig (11)";
}
inline const char* dmaTransferart(uint8_t t) {
    return t == 1 ? "Transfer" : t == 2 ? "Search" : t == 3 ? "Search+Transfer" : "unzulaessig (00)";
}
inline std::string dmaPort(char name, uint8_t wr, bool variabel, uint8_t zeit, uint8_t zyklus, uint16_t start,
                           uint16_t aktuell) {
    return fmt("Port %c: %s, %s, Zyklus %d Takte%s  Start=%04X aktuell=%04X  (WR%d=%02X)", name,
               (wr & 0x08) ? "E/A" : "Speicher",
               (wr & 0x20) ? "fest" : ((wr & 0x10) ? "aufsteigend" : "absteigend"), zyklus,
               variabel ? fmt(" (Zeitbyte %02X)", zeit).c_str() : " (Standard)", start, aktuell,
               name == 'A' ? 1 : 2, wr);
}

/// Alle Register und Zustandsbits des UA858 als Zeilen (`dma`).
inline std::vector<std::string> dmaZeilen(const Z80Dma::Sicht& s) {
    std::vector<std::string> o;
    o.push_back(fmt("WR0=%02X  %s, %s  Blocklaenge=%04X (Zaehler N+1 gelesen: %04X)", s.wr0,
                    dmaTransferart(s.transferart), s.aQuelle ? "A -> B" : "B -> A", s.blocklaenge,
                    s.zaehlerGelesen));
    o.push_back("  " + dmaPort('A', s.wr1, s.variabelA, s.zeitA, s.zyklusA, s.startA, s.adresseA));
    o.push_back("  " + dmaPort('B', s.wr2, s.variabelB, s.zeitB, s.zyklusB, s.startB, s.adresseB));
    o.push_back(fmt("WR3=%02X  Maske=%02X Vergleich=%02X%s", s.wr3, s.maske, s.vergleich,
                    s.stoppBeiTreffer ? "  Stopp bei Treffer" : ""));
    o.push_back(fmt("WR4=%02X  %s  Int-Steuer=%02X Impuls-Steuer=%02X Vektor=%02X", s.wr4,
                    dmaBetriebsart(s.betriebsart), s.intSteuer, s.impulsSteuer, s.vektorRoh));
    o.push_back(fmt("WR5=%02X  Auto-Restart=%s  /CE-/WAIT=%s  RDY aktiv %s", s.wr5, s.autoRestart ? "ja" : "nein",
                    s.waitFunktion ? "WAIT" : "CE", s.rdyAktiv ? "high" : "low"));
    o.push_back(fmt("Lauf: freigegeben=%d begonnen=%d force-ready=%d RDY-Pin=%d Bus-Anforderung=%d  Bytes bewegt=%d",
                    s.freigegeben, s.begonnen, s.forceReady, s.rdyPin, s.busAnforderung, (int)s.bytesBewegt));
    o.push_back(fmt("Ende-Pegel=%d  Impulse=%u  RDY-Freigabe=%d Byte-Freigabe=%d", s.endePegel,
                    (unsigned)s.impulse, s.rdyFreigabe, s.byteFreigabe));
    o.push_back(fmt("Interrupt: frei=%d anstehend=%d IUS=%d wartet-auf-RETI=%d IEI=%d Grund(V2V1)=%d", s.intFreigabe,
                    s.intAnstehend, s.ius, s.warteAufReti, s.iei, s.intGrund));
    o.push_back(fmt("RR0=%02X  %s", s.status, dmaRr0Text(s.status).c_str()));
    o.push_back(fmt("Lesemaske=%02X naechstes Lese-Register=%d  offene Folgebytes=%d", s.lesemaske, s.lesePos,
                    s.folgeOffen));
    return o;
}

/**
 * @brief Statusprotokoll des UA858: wird je U880-Befehl (und je Portzugriff) mit der Sicht gefüttert
 *        und schreibt eine Zeile, sobald sich Status, Freigabe, Lauf, Interrupt, Bus oder die
 *        Betriebsart ändern — nicht bei jedem Byte (Zähler und Adressen laufen ohnehin).
 */
class DmaProtokoll {
public:
    bool an = false;
    Protokoll prot{1024};
    void clear() { prot.clear(); have_ = false; }
    /// Portzugriff des U880 auf 3CH–3FH (Steuerbyte/Lesen).
    void port(uint64_t takt, uint16_t pc, bool lesen, uint8_t wert) {
        if (an) prot.add(takt, fmt("PC=%04X %s 3xH %02X", pc, lesen ? "IN " : "OUT", wert));
    }
    void beobachte(uint64_t takt, uint16_t pc, const Z80Dma::Sicht& s) {
        if (!an) return;
        const uint32_t key = uint32_t(s.status) | (s.freigegeben << 8) | (s.begonnen << 9) |
                             (s.intAnstehend << 10) | (s.ius << 11) | (s.busAnforderung << 12) |
                             (s.endePegel << 13) | (s.forceReady << 14) | (s.warteAufReti << 15) |
                             (uint32_t(s.betriebsart) << 16) | (uint32_t(s.transferart) << 18);
        if (have_ && key == last_) return;
        prot.add(takt, fmt("PC=%04X RR0=%02X [%s] %s%s%s%s%s  %s  Zaehler=%d A=%04X B=%04X", pc, s.status,
                           dmaRr0Text(s.status).c_str(), s.freigegeben ? "frei " : "gesperrt ",
                           s.begonnen ? "laeuft " : "", s.busAnforderung ? "BUSRQ " : "", s.intAnstehend ? "INT " : "",
                           s.ius ? "IUS " : "", dmaBetriebsart(s.betriebsart), (int)s.bytesBewegt, s.adresseA,
                           s.adresseB));
        last_ = key;
        have_ = true;
    }
private:
    uint32_t last_ = 0;
    bool have_ = false;
};

/// Kopplungsleitungen K1–K15 als Zeilen: Pegel der Leitungen und Stand der beiden Latches.
inline std::vector<std::string> koppZeilen(const P8000Kopplung& k, uint8_t latch1, uint8_t latch2,
                                           const Z80PIO::DebugState& p8_0, const Z80PIO::DebugState& p16_0,
                                           const Z80PIO::DebugState& p16_1, bool runLed) {
    const auto& p = k.pegel();
    std::vector<std::string> o;
    o.push_back(fmt("K1  D0-7/8-16  =%02X   (DS8282 L1 10H = %02X)  -> 16-PIO1 A", p.d8_16, latch1));
    o.push_back(fmt("K2-4 L2         =%02X   (DS8282 L2 14H = %02X)  INT-16=%d V1-6=%02X RDY/8-16(/ASTB)=%d -> 16-PIO1 B",
                    p.l2, latch2, p.l2 & 1, (p.l2 >> 1) & 0x3F, (p.l2 >> 7) & 1));
    o.push_back(fmt("K5  E0-8        =%d    (16-PIO1 ARDY -> 8-PIO0 B6)", p.e0_8));
    o.push_back(fmt("K6  D0-7/16-8   =%02X   (16-PIO0 A-Pins -> 8-PIO0 A)", p.d16_8));
    o.push_back(fmt("K7  DD-8        =%d    (16-PIO0 ARDY -> 8-PIO0 /ASTB, B5)", p.dd_8));
    o.push_back(fmt("K8  RDY/16-8    =%d    (8-PIO0 ARDY -> 16-PIO0 /ASTB)", p.rdy16_8));
    o.push_back(fmt("K9/10 B16-8     =%02X   INT-8=%d V1-4=%X (16-PIO0 B0-B4 -> 8-PIO0 B0-B4)", p.b16_8, p.b16_8 & 1,
                    (p.b16_8 >> 1) & 0xF));
    o.push_back(fmt("K11 RESET16     =%d    (8-PIO0 B7eff; 1 = U8001 im Reset)", p.reset16));
    o.push_back(fmt("K14 RUN-LED     =%d", runLed ? 1 : 0));
    auto pio = [&](const char* n, const Z80PIO::DebugState& s) {
        o.push_back(fmt("%s  A: mode=%u out=%02X in=%02X dir=%02X   B: mode=%u out=%02X in=%02X dir=%02X", n,
                        s.port[0].mode, s.port[0].out, s.port[0].in, s.port[0].dir, s.port[1].mode,
                        s.port[1].out, s.port[1].in, s.port[1].dir));
    };
    pio("8-PIO0 ", p8_0);
    pio("16-PIO0", p16_0);
    pio("16-PIO1", p16_1);
    return o;
}

/// Ein E/A-Zugriff der 8-Bit-Seite auf Kopplungstore (Latch 10H–17H, PIO0 0CH–0FH) als Protokolltext.
inline std::string koppText8(uint8_t port, bool lesen, uint8_t wert) {
    if (port >= 0x10 && port <= 0x13) return fmt("8->16  L1 (D0-7/8-16)  = %02X", wert);
    if (port >= 0x14 && port <= 0x17)
        return fmt("8->16  L2 INT-16=%d V1-6=%02X RDY/8-16=%d  (%02X)", wert & 1, (wert >> 1) & 0x3F, wert >> 7, wert);
    if (port >= 0x0C && port <= 0x0F)
        return fmt("8-PIO0 %s %c-%s %02X", lesen ? "rd" : "wr", (port & 2) ? 'B' : 'A', (port & 1) ? "Steuer" : "Daten",
                   wert);
    return std::string();
}
inline bool koppPort8(uint8_t port) { return (port >= 0x0C && port <= 0x0F) || (port >= 0x10 && port <= 0x17); }

/// Ein E/A-Zugriff der 16-Bit-Seite auf PIO0/PIO1 (FF91–FF9F) als Protokolltext; leer = nicht Kopplung.
inline std::string koppText16(uint16_t port, bool lesen, uint16_t wert) {
    if (port < 0xFF91 || port > 0xFF9F) return std::string();
    const int pio = port < 0xFF99 ? 0 : 1;
    const uint16_t a = uint16_t(port & 0x7);   // A1 = Kanal, A2 = Steuerwort
    return fmt("16-PIO%d %s %c-%s %02X", pio, lesen ? "rd" : "wr", (a & 2) ? 'B' : 'A', (a & 4) ? "Steuer" : "Daten",
               wert & 0xFF);
}

// ═══ P12b: Z8000-Seite / MMU ═════════════════════════════════════════════════

/// "RDV|SYSV|…" aus dem Verletzungstypregister (oder Probe-Befund).
inline std::string vtrText(uint8_t v) {
    static const struct { uint8_t bit; const char* n; } t[] = {
        {Z8010::VTR_RDV, "RDV"}, {Z8010::VTR_SYSV, "SYSV"}, {Z8010::VTR_SLV, "SLV"}, {Z8010::VTR_CPUIV, "CPUIV"},
        {Z8010::VTR_EXCV, "EXCV"}, {Z8010::VTR_PWW, "PWW"}, {Z8010::VTR_SWW, "SWW"}, {Z8010::VTR_FATL, "FATL"}};
    std::string s;
    for (auto& e : t)
        if (v & e.bit) { if (!s.empty()) s += '|'; s += e.n; }
    return s.empty() ? "-" : s;
}

/// Attributbyte des Deskriptors: "RD SYS CPUI EXC DMAI DIRW CHG REF" (gesetzte Bits).
inline std::string attrText(uint8_t a) {
    static const struct { uint8_t bit; const char* n; } t[] = {
        {Z8010::ATTR_RD, "RD"}, {Z8010::ATTR_SYS, "SYS"}, {Z8010::ATTR_CPUI, "CPUI"}, {Z8010::ATTR_EXC, "EXC"},
        {Z8010::ATTR_DMAI, "DMAI"}, {Z8010::ATTR_DIRW, "DIRW"}, {Z8010::ATTR_CHG, "CHG"}, {Z8010::ATTR_REF, "REF"}};
    std::string s;
    for (auto& e : t)
        if (a & e.bit) { if (!s.empty()) s += ' '; s += e.n; }
    return s.empty() ? "-" : s;
}

inline const char* mmuName(P8000MmuLogik16::Mmu m) {
    return m == P8000MmuLogik16::Mmu::Code ? "Code" : m == P8000MmuLogik16::Mmu::Data ? "Data" : "Stack";
}

/// Modusregister: "MSEN TRNS URS MST/NMS ID=n".
inline std::string mrText(uint8_t mr) {
    std::string s;
    if (mr & Z8010::MR_MSEN) s += "MSEN ";
    if (mr & Z8010::MR_TRNS) s += "TRNS ";
    if (mr & Z8010::MR_URS) s += "URS ";
    if (mr & Z8010::MR_MST) s += "MST ";
    if (mr & Z8010::MR_NMS) s += "NMS ";
    return s + fmt("ID=%d", mr & Z8010::MR_ID);
}

/// Alle Register einer UB8010 (Mode, SAR, DSCR, Verletzungs-/Befehlsregister, /SEGT).
inline std::vector<std::string> mmuRegisterZeilen(const Z8010& z, const char* name) {
    std::vector<std::string> o;
    o.push_back(fmt("UB8010 %s  MR=%02X [%s]  SAR=%02X DSCR=%d  /SEGT=%s", name, z.mr(), mrText(z.mr()).c_str(),
                    z.sar(), z.dscr() & 3, z.segt() ? "AKTIV" : "-"));
    const auto v = z.letzteVerletzung();
    o.push_back(fmt("  VTR=%02X [%s]  VSN=%02X VOFF=%02X  BCSR=%02X (N/S=%c R/W=%c)  ISN=%02X IOFF=%02X%s", v.vtr,
                    vtrText(v.vtr).c_str(), v.vsn, v.voff, v.bcsr, (v.bcsr & Z8010::BCSR_NS) ? 'N' : 'S',
                    (v.bcsr & Z8010::BCSR_RW) ? 'R' : 'W', v.isn, v.ioff, z.supBisBefehlsende() ? "  SUP bis Befehlsende" : ""));
    return o;
}

/// Deskriptoren @p von..@p bis (0–63); ohne @p alle nur die belegten (Basis, Limit oder Attribut ≠ 0).
inline std::vector<std::string> mmuDeskriptorZeilen(const Z8010& z, int von, int bis, bool alle) {
    std::vector<std::string> o;
    for (int n = von; n <= bis && n < 64; ++n) {
        const Z8010::Deskriptor d = z.deskriptor(n);
        if (!alle && !d.basis && !d.limit && !d.attr) continue;
        // Basis = Bit 23..8 der Anfangsadresse, Limit = Anzahl 256-B-Blöcke − 1 (Segmentgröße (limit+1)·256, [TB])
        const uint32_t basis = uint32_t(d.basis) << 8;
        o.push_back(fmt("  SDR %02d  Basis=%06X Limit=%02X (%u Byte)  Attr=%02X [%s]", n, basis, d.limit,
                        (unsigned)(d.limit + 1) * 256u, d.attr, attrText(d.attr).c_str()));
    }
    if (o.empty()) o.push_back("  (keine belegten Deskriptoren)");
    return o;
}

/// SCR/NBR/TRPL/IF1L, Zähler, letzter Zyklus (Steuerlogik der drei MMUs).
inline std::vector<std::string> mmuLogikZeilen(const P8000MmuLogik16& l) {
    const auto s = l.sicht();
    std::vector<std::string> o;
    o.push_back(fmt("SCR=%02X  On-Board %s  MMU %s  Anwender %s  Paritaet %s", s.scr,
                    (s.scr & P8000MmuLogik16::SCR_BDMEM_AUS) ? "AUS" : "ein",
                    (s.scr & P8000MmuLogik16::SCR_MMU_ON) ? "EIN" : "aus",
                    (s.scr & P8000MmuLogik16::SCR_SEG_USR) ? "segmentiert (SEG USR)" : "nichtsegm.",
                    (s.scr & P8000MmuLogik16::SCR_PARITAET) ? "scharf" : "geloescht"));
    o.push_back(fmt("NBR=%02X (wirksam %02X)  SBR=-  TRPL=%02X IF1L=%02X (Stufe 1: %02X)  /SEGT=%s  RUN-LED=%d",
                    s.nbr, l.probe(Z8kBusCycle{}).nbrWirksam, s.trpl, s.if1l, s.if1lStufe1, s.segt ? "AKTIV" : "-",
                    s.runLed ? 1 : 0));
    o.push_back(fmt("Zyklen je Wahl: Keine=%llu Code=%llu Data=%llu Stack=%llu  On-Board=%llu unterdrueckt=%llu "
                    "Konflikte=%llu SEGT-Flanken=%llu",
                    (unsigned long long)s.zyklen[0], (unsigned long long)s.zyklen[1], (unsigned long long)s.zyklen[2],
                    (unsigned long long)s.zyklen[3], (unsigned long long)s.onBoard, (unsigned long long)s.unterdrueckt,
                    (unsigned long long)s.konflikte, (unsigned long long)s.segtFlanken));
    return o;
}

enum class Art { Code, Data, Stack };
inline bool parseArt(const std::string& s, Art& a) {
    if (s == "code" || s == "c" || s == "instr" || s == "fetch") { a = Art::Code; return true; }
    if (s == "data" || s == "d") { a = Art::Data; return true; }
    if (s == "stack" || s == "s") { a = Art::Stack; return true; }
    return false;
}
/// Buszyklus-Status für die Zugriffsart: Befehl 1100 (erstes Wort 1101 bei @p erstes), Daten 1000, Stapel 1001.
inline Z8kBusCycle zyklusFuer(uint8_t seg, uint16_t off, Art art, bool system, bool lesen = true, bool erstes = false) {
    Z8kBusCycle c;
    c.st = art == Art::Code ? (erstes ? Z8kStatus::MemInstrFirst : Z8kStatus::MemInstr)
         : art == Art::Data ? Z8kStatus::MemData : Z8kStatus::MemStack;
    c.system = system;
    c.word = true;
    c.read = lesen;
    c.seg = uint8_t(seg & 0x7F);
    c.addr = off;
    return c;
}

/// Ergebnis einer probeweisen Übersetzung samt Text.
struct Xlat {
    P8000MmuLogik16::Probe probe;
    bool onBoard = false;       ///< Ziel liegt auf der Karte (EPROM/SRAM/leer)
    bool hauptspeicher = false;
    uint32_t phys = 0;          ///< Hauptspeicher: 24-Bit-Adresse; On-Board: Byteadresse im Baustein
    std::vector<std::string> zeilen;
};

inline Xlat xlat(const P8000MmuLogik16& l, const Z8kBusCycle& c) {
    using L = P8000MmuLogik16;
    Xlat x;
    x.probe = l.probe(c);
    const auto& z = x.probe.zugriff;
    x.hauptspeicher = z.ziel == L::Ziel::Hauptspeicher;
    x.onBoard = z.ziel == L::Ziel::OnBoardEprom || z.ziel == L::Ziel::OnBoardSram || z.ziel == L::Ziel::OnBoardLeer;
    x.phys = z.adresse;
    const char* fcw = c.system ? "System" : "Normal";
    x.zeilen.push_back(fmt("<<%u>>%%%04X  %s %s (%s)  -> %s%s", c.seg, c.addr, z8kStatusName(c.st), fcw,
                           c.read ? "lesen" : "schreiben", L::zielName(z.ziel),
                           z.ziel == L::Ziel::Keins ? "" : fmt("  %s%06X", x.hauptspeicher ? "phys=" : "Baustein+", z.adresse).c_str()));
    x.zeilen.push_back(fmt("  gewaehlte MMU: %s%s  MMU-Adresse=%s  Treiber=%X  SUP=%X%s%s  Wartetakte=%d",
                           L::wahlName(z.wahl), z.wahl == L::Wahl::Keine ? " (On-Board)" : "",
                           z.mmuAdresse ? "ja (TRAD)" : "nein (lokal)", z.treiber, z.sup,
                           z.unterdrueckt ? "  UNTERDRUECKT" : "", z.konflikt ? "  KONFLIKT" : "", z.wartetakte));
    static const char* n[3] = {"Code ", "Data ", "Stack"};
    for (int i = 0; i < 3; ++i) {
        const auto& m = x.probe.mmu[i];
        x.zeilen.push_back(fmt("  MMU %s  N/S-Eingang=%c  %s  %s%s%s", n[i], x.probe.nsHigh[i] ? 'H' : 'L',
                               m.adresse ? fmt("A8-23=%06X%s", m.phys & 0xFFFF00, m.geprueft ? " (geprueft)" : " (ungeprueft)").c_str()
                                         : "tri-state",
                               m.verletzung ? ("Verletzung " + vtrText(m.verletzung)).c_str() : "keine Verletzung",
                               m.warnung ? "  +Schreibwarnung" : "", m.dmaiVerletzung ? "  +DMAI" : ""));
    }
    return x;
}

/// Ein Byte hinter einer Übersetzung ohne Nebenwirkung; false = kein Baustein antwortet (offener Bus).
inline bool physByte(P8000Karte16& k, const P8000MmuLogik16::Zugriff& z, uint32_t byteOffset, uint8_t& v) {
    using L = P8000MmuLogik16;
    v = 0xFF;
    switch (z.ziel) {
        case L::Ziel::OnBoardEprom: v = k.rom()[(z.adresse + byteOffset) & 0x3FFF]; return true;
        case L::Ziel::OnBoardSram:  v = k.sram(uint16_t(z.adresse + byteOffset)); return true;
        case L::Ziel::Hauptspeicher:
            if (z.unterdrueckt) return false;
            if (!k.dram().gewaehlt(z.adresse + byteOffset)) return false;
            v = k.dram().peek(z.adresse + byteOffset);
            return true;
        default: return false;
    }
}

/// `ml`: ein Byte logisch (über die Auswahllogik und die MMUs) — Offset wird je Byte neu übersetzt.
inline bool logByte(P8000Karte16& k, uint8_t seg, uint16_t off, Art art, bool system, uint8_t& v) {
    const auto z = k.mmu().probe(zyklusFuer(seg, off, art, system)).zugriff;
    return physByte(k, z, 0, v);
}

/// `mp`: ein Byte physisch (Hauptspeicher A0–A23, ohne MMU).
inline bool physHaupt(P8000Karte16& k, uint32_t adr, uint8_t& v) {
    v = 0xFF;
    if (!k.dram().gewaehlt(adr)) return false;
    v = k.dram().peek(adr);
    return true;
}

/// Eine SEGT-Flanke als Protokolltext.
inline std::string segtText(const P8000MmuLogik16::SegtEreignis& e) {
    std::string w;
    static const char* n[3] = {"Code", "Data", "Stack"};
    for (int i = 0; i < 3; ++i)
        if (e.mmus & (1u << i)) { if (!w.empty()) w += '+'; w += n[i]; }
    return fmt("SEGT #%llu durch MMU %s  im Zyklus %s %s <<%u>>%%%04X (%s)  TRPL=%02X IF1L=%02X  gewaehlt: %s",
               (unsigned long long)e.nummer, w.empty() ? "?" : w.c_str(), z8kStatusName(e.zyklus.st),
               e.zyklus.system ? "S" : "N", e.zyklus.seg, e.zyklus.addr, e.zyklus.read ? "R" : "W", e.trpl, e.if1l,
               P8000MmuLogik16::wahlName(e.wahl));
}

}  // namespace dbgp8
