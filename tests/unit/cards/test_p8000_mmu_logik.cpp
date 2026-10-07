/**
 * @test P8000MmuLogik.* / P8000MmuLogikMatrix.* / P8000MmuLogikGegenprobe.* — MMU-Steuerlogik
 *       der 16-Bit-Karte (AP P9b): Auswahltabelle (alle 6 Zeilen), NBR-Vergleich, On-Board-
 *       Fenster, Adressbildung MMU ein/aus, SUP-Weg, /CS über LAD1–3, Register FFC1–FFF9,
 *       TRPL/IF1L, Trap-Quittung, Reset, Save-State, Debugger-Sicht; Matrix über alle
 *       Zyklusarten × Zustände; Gegenprobe MON16 `p.test.s` (Testschritte 84–90) und WEGA `mch.s`.
 *       Sollwerte aus doc/p8000/schaltplan_16bit.md (als TABELLE formuliert, nicht aus den
 *       Gattern des Codes); Abdeckung: doc/p8000/mmu_logik_abdeckung.md.
 */

#include <gtest/gtest.h>
#include "core/cards/p8000/mmu_logik16.h"

#include <vector>

namespace {

using L = P8000MmuLogik16;
using W = L::Wahl;
using Z = L::Ziel;

// MON16/WEGA-Konstanten (p.test.s, mch.s)
constexpr uint16_t CODE_MMU = 0x00FC, DATA_MMU = 0x00FA, STACK_MMU = 0x00F6, ALL_MMU = 0x00F0;
constexpr uint8_t ST_DATA = 0x8, ST_STACK = 0x9, ST_EDATA = 0xA, ST_ESTACK = 0xB, ST_INSTR = 0xC,
                  ST_IF1 = 0xD;

void sout(L& l, uint16_t mmu, uint8_t cmd, uint8_t v) {
    l.spezialSchreiben(uint16_t((cmd << 8) | (mmu & 0xFF)), uint16_t((v << 8) | v));   // Byte auf beiden Hälften
}
uint8_t sin(L& l, uint16_t mmu, uint8_t cmd) {
    return uint8_t(l.spezialLesen(uint16_t((cmd << 8) | (mmu & 0xFF))) >> 8);
}
/// LD_3SDR-Weg: SAR := 0, dann je MMU 64 × 4 Byte über %0F (SDR + SAR-Inkrement).
void ladeAlle(L& l, uint16_t mmu, const uint8_t sdr[4]) {
    sout(l, mmu, Z8010::CMD_SAR, 0);
    sout(l, mmu, Z8010::CMD_DSCR, 0);
    for (int s = 0; s < 64; ++s)
        for (int b = 0; b < 4; ++b) sout(l, mmu, Z8010::CMD_SDR_INC, sdr[b]);
}
void ladeSdr(L& l, uint16_t mmu, uint8_t seg, uint16_t basis, uint8_t limit, uint8_t attr) {
    sout(l, mmu, Z8010::CMD_SAR, seg);
    for (uint8_t b : {uint8_t(basis >> 8), uint8_t(basis), limit, attr}) sout(l, mmu, Z8010::CMD_SDR, b);
}

Z8kBusCycle zyk(uint8_t st, bool system, uint8_t seg, uint16_t off, bool lesen = true) {
    Z8kBusCycle c;
    c.st = Z8kStatus(st);
    c.system = system;
    c.word = true;
    c.read = lesen;
    c.seg = seg;
    c.addr = off;
    return c;
}

/// Kennbasis je MMU: A23–A22 = 01 Code, 10 Data, 11 Stack; A21–A16 = Segment (6 Bit).
uint16_t kennbasis(int mmu, uint8_t seg) { return uint16_t(((mmu + 1) << 14) | ((seg & 0x3F) << 8)); }

/// Drei MMUs wie am P8000 (Mode %D0/%D1/%D2), jedes SDR mit Kennbasis, Limit FF, ohne Schutz.
void kennaufbau(L& l) {
    sout(l, CODE_MMU, Z8010::CMD_MR, 0xD0);
    sout(l, DATA_MMU, Z8010::CMD_MR, 0xD1);
    sout(l, STACK_MMU, Z8010::CMD_MR, 0xD2);
    const uint16_t port[3] = {CODE_MMU, DATA_MMU, STACK_MMU};
    for (int m = 0; m < 3; ++m)
        for (uint8_t s = 0; s < 64; ++s) ladeSdr(l, port[m], s, kennbasis(m, s), 0xFF, 0x00);
}

/// Erwartete Wahl — die Tabelle schaltplan_16bit.md §1.2 + §1.3 + §2, nicht die Gatter.
W erwarteteWahl(uint8_t st, bool system, bool segUser, uint8_t seg, uint16_t off, uint8_t nbr,
                uint8_t scr, bool gleichStack = true) {
    if (st < 0x8 || st > 0xD) return W::Keine;
    if (!(scr & L::SCR_BDMEM_AUS) && (seg & 0x7F) == 0 && off < 0x8000) return W::Keine;   // On-Board
    if (system) return W::Code;
    if (segUser) return (seg & 0x40) ? W::Stack : W::Data;
    if (st >= 0xC) return W::Code;                    // Befehlsholen 1100/1101
    const uint8_t nbrEff = ((seg >> 1) & 0x1F) == 0 ? 0xFF : nbr;   // 4D16: Latch hochohmig
    const uint8_t hi = uint8_t(off >> 8);
    return (gleichStack ? hi >= nbrEff : hi > nbrEff) ? W::Stack : W::Data;
}

struct Rig {
    L l;
    std::vector<bool> segt;
    explicit Rig(const L::Config& c = L::Config()) : l(c) {
        l.onSegt = [this](bool a) { segt.push_back(a); };
    }
};

}  // namespace

// ═══ Auswahltabelle P2b §1.2 — je Zeile ein Fall, sichtbar an der übersetzten Adresse ═════════

class P8000MmuLogikTabelle : public ::testing::Test {
protected:
    L l;
    void SetUp() override {
        kennaufbau(l);
        l.ioSchreiben(L::P_NBR, 0x80);
    }
    L::Zugriff z(uint8_t st, bool system, uint8_t seg, uint16_t off, bool lesen = true) {
        return l.zyklus(zyk(st, system, seg, off, lesen));
    }
};

TEST_F(P8000MmuLogikTabelle, Zeile1_SystemModeGehtFuerAllesAnCode) {
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    for (uint8_t st : {ST_DATA, ST_STACK, ST_INSTR, ST_IF1}) {
        for (bool segUser : {false, true}) {
            l.ioSchreiben(L::P_SCR, uint8_t(L::SCR_MMU_ON | L::SCR_BDMEM_AUS | (segUser ? L::SCR_SEG_USR : 0)));
            const auto a = z(st, true, 0x3E, 0xF612);
            EXPECT_EQ(a.wahl, W::Code);
            EXPECT_EQ(a.adresse, (uint32_t(kennbasis(0, 0x3E)) << 8) + 0xF612) << int(st);
            EXPECT_EQ(a.treiber, 1);
        }
    }
}

TEST_F(P8000MmuLogikTabelle, Zeile2_SegUserSn6NullGehtAnData) {
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS | L::SCR_SEG_USR);
    for (uint8_t st : {ST_DATA, ST_STACK, ST_INSTR, ST_IF1}) {
        const auto a = z(st, false, 0x05, 0x1234);
        EXPECT_EQ(a.wahl, W::Data) << int(st);
        EXPECT_EQ(a.adresse, (uint32_t(kennbasis(1, 0x05)) << 8) + 0x1234);
    }
}

TEST_F(P8000MmuLogikTabelle, Zeile3_SegUserSn6EinsGehtAnStack) {
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS | L::SCR_SEG_USR);
    for (uint8_t st : {ST_DATA, ST_STACK, ST_INSTR, ST_IF1}) {
        const auto a = z(st, false, 0x45, 0x1234);   // SN6 = 1 → Stack-MMU sieht Segment 05
        EXPECT_EQ(a.wahl, W::Stack) << int(st);
        EXPECT_EQ(a.adresse, (uint32_t(kennbasis(2, 0x05)) << 8) + 0x1234);
    }
}

TEST_F(P8000MmuLogikTabelle, Zeile4_NichtsegmentiertesHolenGehtAnCode) {
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    for (uint8_t st : {ST_INSTR, ST_IF1})
        for (uint16_t off : {0x0000, 0x7F00, 0x8000, 0xFFFE}) {   // NBR spielt keine Rolle
            const auto a = z(st, false, 0x3F, off);
            EXPECT_EQ(a.wahl, W::Code);
            EXPECT_EQ(a.adresse, (uint32_t(kennbasis(0, 0x3F)) << 8) + off);
        }
}

TEST_F(P8000MmuLogikTabelle, Zeile5_DatenzyklusUnterNbrGehtAnData) {
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    for (uint8_t st : {ST_DATA, ST_STACK, ST_EDATA, ST_ESTACK}) {
        const auto a = z(st, false, 0x3F, 0x7FFE);
        EXPECT_EQ(a.wahl, W::Data) << int(st);
        EXPECT_EQ(a.adresse, (uint32_t(kennbasis(1, 0x3F)) << 8) + 0x7FFE);
    }
}

TEST_F(P8000MmuLogikTabelle, Zeile6_DatenzyklusUeberNbrGehtAnStack) {
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    for (uint8_t st : {ST_DATA, ST_STACK, ST_EDATA, ST_ESTACK}) {
        const auto a = z(st, false, 0x3F, 0x8100);
        EXPECT_EQ(a.wahl, W::Stack) << int(st);
        EXPECT_EQ(a.adresse, (uint32_t(kennbasis(2, 0x3F)) << 8) + 0x8100);
    }
}

TEST_F(P8000MmuLogikTabelle, NbrGleichheitGehtAnStack) {
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    EXPECT_EQ(z(ST_DATA, false, 0x3F, 0x7FFF).wahl, W::Data);
    EXPECT_EQ(z(ST_DATA, false, 0x3F, 0x8000).wahl, W::Stack);   // High = NBR
    EXPECT_EQ(z(ST_DATA, false, 0x3F, 0x80FF).wahl, W::Stack);
}

TEST(P8000MmuLogik, NbrGleichheitNachWegaGehtAnData) {
    // [L1] Gegenstück: WEGA nsseg/getmem (`cpb rh7,NBREAK; jr ugt`) ⇒ Gleichheit = Data.
    L::Config c;
    c.nbrGleichheitStack = false;
    L l(c);
    kennaufbau(l);
    l.ioSchreiben(L::P_NBR, 0x80);
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, false, 0x3F, 0x80FF)).wahl, W::Data);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, false, 0x3F, 0x8100)).wahl, W::Stack);
}

TEST_F(P8000MmuLogikTabelle, Status1001WirdNichtAusgewertet) {
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    EXPECT_EQ(z(ST_STACK, false, 0x3F, 0x1000).wahl, W::Data);    // Stackzyklus unter NBR
    EXPECT_EQ(z(ST_DATA, false, 0x3F, 0xF000).wahl, W::Stack);    // Datenzyklus über NBR
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS | L::SCR_SEG_USR);
    EXPECT_EQ(z(ST_STACK, false, 0x05, 0xF000).wahl, W::Data);    // segmentiert: nur SN6
    EXPECT_EQ(z(ST_DATA, false, 0x45, 0x0000).wahl, W::Stack);
}

TEST_F(P8000MmuLogikTabelle, NbrLatchHochohmigInSegment0_1_64_65) {
    // [L2] 4D16: Q = 1 bei SN1–SN5 = 0 ⇒ Vergleichereingänge offen ⇒ NBR wirkt als FFH.
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    for (uint8_t seg : {0x00, 0x01, 0x40, 0x41}) {
        EXPECT_EQ(z(ST_DATA, false, seg, 0x9000).wahl, W::Data) << int(seg);
        EXPECT_EQ(z(ST_DATA, false, seg, 0xFF00).wahl, W::Stack) << int(seg);
        EXPECT_EQ(l.probe(zyk(ST_DATA, false, seg, 0)).nbrWirksam, 0xFF);
    }
    for (uint8_t seg : {0x02, 0x03, 0x3F, 0x42, 0x7F}) {
        EXPECT_EQ(z(ST_DATA, false, seg, 0x9000).wahl, W::Stack) << int(seg);
        EXPECT_EQ(l.probe(zyk(ST_DATA, false, seg, 0)).nbrWirksam, 0x80);
    }
}

// ═══ Adressbildung, On-Board, SUP ════════════════════════════════════════════════════════════

TEST(P8000MmuLogik, MmuAusPhysIstSegmentMal64K) {
    L l;
    kennaufbau(l);   // die MMUs laufen weiter, nur die Treiber zeigen die lokale Adresse
    l.ioSchreiben(L::P_SCR, L::SCR_BDMEM_AUS);
    for (int seg = 0; seg < 128; ++seg)
        for (uint16_t off : {0x0000, 0x0001, 0x7FFF, 0x8000, 0xABCD, 0xFFFF})
            for (bool system : {true, false}) {
                const auto a = l.zyklus(zyk(ST_DATA, system, uint8_t(seg), off, false));
                EXPECT_EQ(a.ziel, Z::Hauptspeicher);
                EXPECT_FALSE(a.mmuAdresse);
                EXPECT_EQ(a.adresse, (uint32_t(seg) << 16) | off);
                EXPECT_EQ(a.adresse & 0x800000u, 0u);   // A23 = 0
            }
}

TEST(P8000MmuLogik, MmuEinOhneTreiberSchwebtAufEins) {
    // [L7] Code-MMU URS = 0 sieht Segment 64+ nicht ⇒ niemand treibt TRAD.
    L l;
    kennaufbau(l);
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    const auto a = l.zyklus(zyk(ST_DATA, true, 0x45, 0x1234));
    EXPECT_EQ(a.wahl, W::Code);
    EXPECT_EQ(a.treiber, 0);
    EXPECT_EQ(a.adresse, 0xFFFF34u);
    // Nach MR := 0 (MON16 ENTRY_) treibt keine MMU mehr.
    sout(l, ALL_MMU, Z8010::CMD_MR, 0);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, true, 0x01, 0x5678)).adresse, 0xFFFF78u);
}

TEST(P8000MmuLogik, MehrereTreiberVerdrahtetesUnd) {
    // [L7] Durchreichen (TRNS = 0) in Code und Data, Stack übersetzt: MON16 SEGMENT_TRAP_TEST.
    L l;
    sout(l, CODE_MMU, Z8010::CMD_MR, 0x80);
    sout(l, DATA_MMU, Z8010::CMD_MR, 0x81);
    sout(l, STACK_MMU, Z8010::CMD_MR, 0xD2);
    ladeSdr(l, STACK_MMU, 0x02, 0x0012, 0xFF, 0);
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_SEG_USR);
    const auto a = l.zyklus(zyk(ST_DATA, false, 0x42, 0xA000));
    EXPECT_EQ(a.wahl, W::Stack);
    EXPECT_EQ(a.treiber, 7);
    EXPECT_TRUE(a.konflikt);
    // Code: 42A0, Data: 02A0 (SN6 fest L), Stack: 0012 + A0 = 00B2.
    EXPECT_EQ(a.adresse, (uint32_t(0x42A0 & 0x02A0 & 0x00B2) << 8));
    EXPECT_EQ(l.sicht().konflikte, 1u);
}

TEST(P8000MmuLogik, OnBoardNurSegment0Unter8000) {
    L l;
    kennaufbau(l);
    for (uint8_t scr = 0; scr < 16; ++scr) {
        l.ioSchreiben(L::P_SCR, scr);
        for (int seg = 0; seg < 128; seg += (seg < 4 ? 1 : 0x1D))
            for (uint32_t off = 0; off < 0x10000; off += 0x0FFF)
                for (bool system : {true, false})
                    for (uint8_t st : {ST_DATA, ST_STACK, ST_INSTR, ST_IF1}) {
                        const auto a = l.zyklus(zyk(st, system, uint8_t(seg), uint16_t(off), true));
                        const bool on = !(scr & 1) && seg == 0 && off < 0x8000;
                        if (!on) { EXPECT_EQ(a.ziel, Z::Hauptspeicher); EXPECT_EQ(a.wartetakte, 0); continue; }
                        EXPECT_EQ(a.wahl, W::Keine);
                        EXPECT_EQ(a.wartetakte, 1);
                        if (off < 0x4000)      { EXPECT_EQ(a.ziel, Z::OnBoardEprom); EXPECT_EQ(a.adresse, off); }
                        else if (off < 0x6000) { EXPECT_EQ(a.ziel, Z::OnBoardSram);  EXPECT_EQ(a.adresse, off & 0x7FF); }
                        else                   { EXPECT_EQ(a.ziel, Z::OnBoardLeer); }
                    }
    }
    // Grenzen genau
    l.ioSchreiben(L::P_SCR, 0);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, true, 0, 0x3FFF)).ziel, Z::OnBoardEprom);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, true, 0, 0x4000)).ziel, Z::OnBoardSram);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, true, 0, 0x47FF)).adresse, 0x7FFu);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, true, 0, 0x4800)).adresse, 0x000u);   // Spiegel
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, true, 0, 0x5FFF)).ziel, Z::OnBoardSram);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, true, 0, 0x6000)).ziel, Z::OnBoardLeer);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, true, 0, 0x7FFF)).ziel, Z::OnBoardLeer);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, true, 0, 0x8000)).ziel, Z::Hauptspeicher);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, true, 0, 0x8000)).adresse, 0x008000u);   // MMU aus
}

TEST(P8000MmuLogik, OnBoardSperrtDieMmuAuswahl) {
    // IMEML ⇒ alle drei N/S-Eingänge H: eine MST-MMU übersetzt nicht, setzt kein REF.
    L l;
    kennaufbau(l);
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON);
    const auto pr = l.probe(zyk(ST_DATA, true, 0, 0x4000));
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(pr.nsHigh[i]);
        EXPECT_FALSE(pr.mmu[i].adresse);
    }
    l.zyklus(zyk(ST_DATA, true, 0, 0x4000));
    EXPECT_EQ(l.mmu(L::Mmu::Code).deskriptor(0).attr & Z8010::ATTR_REF, 0);
}

TEST(P8000MmuLogik, SuppressVerhindertSchreibenNurAmHauptspeicher) {
    Rig r;
    // Data-MMU ohne MST übersetzt jeden Zyklus (auch On-Board) — Segment 0 schreibgeschützt.
    sout(r.l, DATA_MMU, Z8010::CMD_MR, 0xC1);
    ladeSdr(r.l, DATA_MMU, 0, 0x0000, 0xFF, Z8010::ATTR_RD);
    r.l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON);
    // On-Board-SRAM: Verletzung zieht SUP und SEGT, der Speicher schreibt trotzdem.
    auto a = r.l.zyklus(zyk(ST_DATA, true, 0, 0x4010, false));
    EXPECT_EQ(a.ziel, Z::OnBoardSram);
    EXPECT_EQ(a.sup, 2);
    EXPECT_FALSE(a.unterdrueckt);
    EXPECT_TRUE(r.l.segt());
    // Hauptspeicher: derselbe Befehl, SUP bis Befehlsende ⇒ unterdrückt.
    a = r.l.zyklus(zyk(ST_DATA, true, 0, 0x9010, false));
    EXPECT_EQ(a.ziel, Z::Hauptspeicher);
    EXPECT_TRUE(a.unterdrueckt);
    // Nächster Befehl, nach Quittung und VTR-Löschen: ohne Verletzung nichts unterdrückt.
    r.l.zyklus(zyk(ST_IF1, true, 0, 0x0100));
    r.l.segtQuittung();
    sout(r.l, ALL_MMU, Z8010::CMD_VTR_LOESCHEN, 0);
    r.l.zyklus(zyk(ST_IF1, true, 0, 0x0102));
    a = r.l.zyklus(zyk(ST_DATA, true, 0, 0x9010, true));
    EXPECT_FALSE(a.unterdrueckt);
}

TEST(P8000MmuLogik, SupWirktAuchBeiMmuAusUndBeimLesen) {
    // [L8] SYSDS hängt nicht an MMU ON und sperrt den Datenstrobe in beide Richtungen.
    L l;
    sout(l, DATA_MMU, Z8010::CMD_MR, 0xD1);
    ladeSdr(l, DATA_MMU, 0x3F, 0x0000, 0x10, 0);   // Limit 10H
    l.ioSchreiben(L::P_SCR, L::SCR_BDMEM_AUS);      // MMU aus
    l.ioSchreiben(L::P_NBR, 0xFF);
    auto a = l.zyklus(zyk(ST_DATA, false, 0x3F, 0x2000, true));
    EXPECT_FALSE(a.mmuAdresse);
    EXPECT_EQ(a.adresse, 0x3F2000u);
    EXPECT_TRUE(a.unterdrueckt);
    EXPECT_TRUE(l.segt());
}

// ═══ Spezial-E/A: /CS = LAD1/2/3 ═════════════════════════════════════════════════════════════

TEST(P8000MmuLogik, CsAusLad123) {
    L l;
    for (int lo = 0; lo < 256; ++lo) {
        sout(l, ALL_MMU, Z8010::CMD_MR, 0);
        l.spezialSchreiben(uint16_t(lo), 0x5555);   // Befehl %00 = MR, Daten AD8–15
        const uint8_t erwartet = uint8_t(((lo & 2) ? 0 : 1) | ((lo & 4) ? 0 : 2) | ((lo & 8) ? 0 : 4));
        EXPECT_EQ(L::csMaske(uint16_t(lo)), erwartet);
        EXPECT_EQ(l.mmu(L::Mmu::Code).mr(), (erwartet & 1) ? 0x55 : 0) << lo;
        EXPECT_EQ(l.mmu(L::Mmu::Data).mr(), (erwartet & 2) ? 0x55 : 0) << lo;
        EXPECT_EQ(l.mmu(L::Mmu::Stack).mr(), (erwartet & 4) ? 0x55 : 0) << lo;
    }
    // Lesen: nur AD8–15 getrieben; mehrere gewählt ⇒ UND; keine ⇒ offen.
    sout(l, CODE_MMU, Z8010::CMD_MR, 0xF1);
    sout(l, DATA_MMU, Z8010::CMD_MR, 0xE2);
    sout(l, STACK_MMU, Z8010::CMD_MR, 0xD4);
    for (int lo = 0; lo < 256; ++lo) {
        const uint8_t cs = L::csMaske(uint16_t(lo));
        uint8_t hi = 0xFF;
        if (cs & 1) hi &= 0xF1;
        if (cs & 2) hi &= 0xE2;
        if (cs & 4) hi &= 0xD4;
        EXPECT_EQ(l.spezialLesen(uint16_t(lo)), uint16_t((hi << 8) | 0xFF)) << lo;
    }
    // MON16/WEGA-Konstanten
    EXPECT_EQ(L::csMaske(CODE_MMU), 1);
    EXPECT_EQ(L::csMaske(DATA_MMU), 2);
    EXPECT_EQ(L::csMaske(STACK_MMU), 4);
    EXPECT_EQ(L::csMaske(ALL_MMU), 7);
}

TEST(P8000MmuLogik, SpezialEaTraegtDenBefehlImHohenAdressbyte) {
    L l;
    ladeSdr(l, STACK_MMU, 0x3E, 0x1234, 0xF5, 0x22);
    EXPECT_EQ(l.mmu(L::Mmu::Stack).deskriptor(0x3E).basis, 0x1234);
    EXPECT_EQ(l.mmu(L::Mmu::Code).deskriptor(0x3E).basis, 0);
    sout(l, STACK_MMU, Z8010::CMD_SAR, 0x3E);
    EXPECT_EQ(sin(l, STACK_MMU, Z8010::CMD_SDR), 0x12);
    EXPECT_EQ(sin(l, STACK_MMU, Z8010::CMD_SDR), 0x34);
    EXPECT_EQ(sin(l, STACK_MMU, Z8010::CMD_SDR), 0xF5);
    EXPECT_EQ(sin(l, STACK_MMU, Z8010::CMD_SDR), 0x22);
}

// ═══ Register FFC1–FFF9 ══════════════════════════════════════════════════════════════════════

TEST(P8000MmuLogik, NurEigenePorts) {
    for (auto idx : {L::Config::Index::I1, L::Config::Index::I4}) {
        L::Config c;
        c.index = idx;
        L l(c);
        int n = 0;
        for (uint32_t p = 0; p < 0x10000; ++p) {
            const bool soll = (p >= 0xFFC0 && (p & 1)) ||
                              (idx == L::Config::Index::I4 && p >= 0xFFB8 && p <= 0xFFBF && (p & 1));
            EXPECT_EQ(l.istEigenerPort(uint16_t(p)), soll) << std::hex << p;
            n += soll;
        }
        EXPECT_EQ(n, idx == L::Config::Index::I4 ? 36 : 32);
    }
}

TEST(P8000MmuLogik, ScrVierBitLesbarObereBitsEinsResetNull) {
    L l;
    EXPECT_EQ(l.ioLesen(L::P_SCR), 0xF0);   // nach Netz-Ein
    for (int v = 0; v < 256; ++v) {
        l.ioSchreiben(L::P_SCR, uint8_t(v));
        EXPECT_EQ(l.scr(), v & 0x0F);
        EXPECT_EQ(l.ioLesen(L::P_SCR), 0xF0 | (v & 0x0F));
    }
    l.reset();
    EXPECT_EQ(l.ioLesen(L::P_SCR), 0xF0);
}

TEST(P8000MmuLogik, RegisterSpiegelnAnA1A2) {
    // [L9]
    L l;
    for (uint16_t a : {0xFFC1, 0xFFC3, 0xFFC5, 0xFFC7}) {
        l.ioSchreiben(a, uint8_t(a & 0x0F));
        for (uint16_t b : {0xFFC1, 0xFFC3, 0xFFC5, 0xFFC7}) EXPECT_EQ(l.ioLesen(b), 0xF0 | (a & 0x0F));
    }
    l.ioSchreiben(0xFFD7, 0x5A);
    EXPECT_EQ(l.ioLesen(L::P_NBR), 0x5A);
    l.ioSchreiben(0xFFDB, 0);     // LEDEIN-Spiegel
    l.ioSchreiben(0xFFBF, 0);     // LEDAUS-Spiegel
    EXPECT_FALSE(l.runLed());
    l.ioSchreiben(0xFFDF, 0);
    EXPECT_TRUE(l.runLed());
}

TEST(P8000MmuLogik, NbrLesenSchreibenStartwertUndUeberlebtReset) {
    L::Config c;
    c.nbrStart = 0xA5;
    L l(c);
    EXPECT_EQ(l.ioLesen(L::P_NBR), 0xA5);
    for (int v = 0; v < 256; ++v) {
        l.ioSchreiben(L::P_NBR, uint8_t(v));
        EXPECT_EQ(l.ioLesen(L::P_NBR), v);
    }
    l.ioSchreiben(L::P_NBR, 0x3C);
    l.reset();                    // DS8282 hat keinen Reset
    EXPECT_EQ(l.nbr(), 0x3C);
}

TEST(P8000MmuLogik, SbrFfc9SpeichertNichts) {
    // [L3] MON16 Testschritt 84 schreibt AA/55 und liest zurück — nur auf Index-0-Hardware.
    L l;
    l.ioSchreiben(L::P_NBR, 0x77);
    for (uint8_t v : {0xAA, 0x55, 0x00}) {
        l.ioSchreiben(L::P_SBR, v);
        EXPECT_EQ(l.ioLesen(L::P_SBR), 0xFF);
    }
    EXPECT_EQ(l.nbr(), 0x77);   // FFC9 berührt das NBR nicht
}

TEST(P8000MmuLogik, NurSchreibbareUndNurLesbareAdressen) {
    // [L10] FFD9/FFE1/FFE9 lesen offen; TRPL/IF1L nehmen nichts an.
    L l;
    int soft = 0, reti = 0;
    l.onSoftreset = [&] { ++soft; };
    l.onReti = [&] { ++reti; };
    for (uint16_t p : {L::P_FFD9, L::P_RETI, L::P_SOFTRESET, L::P_LEDAUS}) EXPECT_EQ(l.ioLesen(p), 0xFF);
    EXPECT_EQ(soft, 0);
    l.ioSchreiben(L::P_TRPL, 0x12);
    l.ioSchreiben(L::P_IF1L, 0x34);
    EXPECT_EQ(l.ioLesen(L::P_TRPL), 0);
    EXPECT_EQ(l.ioLesen(L::P_IF1L), 0);
    EXPECT_EQ(reti, 0);
}

TEST(P8000MmuLogik, Index4RunLedEinAusUndReset) {
    L l;
    EXPECT_TRUE(l.runLed());           // MRESET− setzt das RS-FF
    l.ioSchreiben(L::P_LEDAUS, 0);
    EXPECT_FALSE(l.runLed());
    l.ioSchreiben(L::P_FFD9, 0);       // LEDEIN
    EXPECT_TRUE(l.runLed());
    l.ioSchreiben(L::P_LEDAUS, 0);
    l.reset();
    EXPECT_TRUE(l.runLed());
}

TEST(P8000MmuLogik, Index1Ffd9IstSnvrOhneWirkungUndKeinLedaus) {
    L::Config c;
    c.index = L::Config::Index::I1;
    L l(c);
    std::vector<uint8_t> vor;
    l.serialize(vor);
    l.ioSchreiben(L::P_FFD9, 0x42);
    l.ioSchreiben(L::P_LEDAUS, 0);
    std::vector<uint8_t> nach;
    l.serialize(nach);
    EXPECT_EQ(vor, nach);
    EXPECT_EQ(l.ioLesen(L::P_FFD9), 0xFF);
    EXPECT_FALSE(l.istEigenerPort(L::P_LEDAUS));
}

TEST(P8000MmuLogik, SoftresetMeldetSichBeiBeidenIndizes) {
    for (auto idx : {L::Config::Index::I1, L::Config::Index::I4}) {
        L::Config c;
        c.index = idx;
        L l(c);
        int n = 0;
        l.onSoftreset = [&] { ++n; };
        l.ioSchreiben(L::P_SOFTRESET, 0x00);
        l.ioSchreiben(0xFFEF, 0xFF);
        EXPECT_EQ(n, 2);
    }
}

TEST(P8000MmuLogik, RetiFolgeEdDann4D) {
    L l;
    int n = 0;
    l.onReti = [&] { ++n; };
    l.ioSchreiben(L::P_RETI, 0x4D);
    EXPECT_EQ(n, 0);
    l.ioSchreiben(L::P_RETI, 0xED);
    l.ioSchreiben(L::P_RETI, 0x4D);
    EXPECT_EQ(n, 1);
    l.ioSchreiben(L::P_RETI, 0x4D);
    l.ioSchreiben(L::P_RETI, 0xED);
    l.ioSchreiben(L::P_RETI, 0x00);
    l.ioSchreiben(L::P_RETI, 0x4D);
    EXPECT_EQ(n, 1);
}

// ═══ Reset ══════════════════════════════════════════════════════════════════════════════════

TEST(P8000MmuLogik, MresetSetztMsenUndLaesstDeskriptoren) {
    // [L5] Handbuch §2.2: „MMU-Steuerregister gelöscht, Master-Enable nicht auf Null".
    Rig r;
    kennaufbau(r.l);
    sout(r.l, DATA_MMU, Z8010::CMD_MR, 0xD1);
    ladeSdr(r.l, DATA_MMU, 0x3F, 0, 0x00, 0);
    r.l.ioSchreiben(L::P_NBR, 0x80);
    r.l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    r.l.zyklus(zyk(ST_DATA, false, 0x3F, 0x1000));   // SLV ⇒ SEGT
    ASSERT_TRUE(r.l.segt());
    r.l.reset();
    EXPECT_FALSE(r.l.segt());
    EXPECT_EQ(r.segt.back(), false);
    for (auto m : {L::Mmu::Code, L::Mmu::Data, L::Mmu::Stack}) {
        EXPECT_EQ(r.l.mmu(m).mr(), Z8010::MR_MSEN);
        EXPECT_EQ(r.l.mmu(m).vtr(), 0);
    }
    EXPECT_EQ(r.l.mmu(L::Mmu::Code).deskriptor(5).basis, kennbasis(0, 5));
    EXPECT_EQ(r.l.scr(), 0);
    // Durchreichen: MMU ON ohne Programmierung liefert SN·64K + Offset aus allen dreien.
    r.l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    const auto a = r.l.zyklus(zyk(ST_DATA, true, 0x05, 0x1234));
    EXPECT_EQ(a.treiber, 7);
    EXPECT_EQ(a.adresse, 0x051234u);
    EXPECT_FALSE(a.konflikt);

    L::Config c;
    c.csBeimReset = false;
    L ohne(c);
    for (auto m : {L::Mmu::Code, L::Mmu::Data, L::Mmu::Stack}) EXPECT_EQ(ohne.mmu(m).mr(), 0);
}

// ═══ Trap: TRPL, IF1L, Kennwort ══════════════════════════════════════════════════════════════

class P8000MmuLogikTrap : public ::testing::Test {
protected:
    Rig r;
    void SetUp() override {
        kennaufbau(r.l);
        // Data-MMU: Segment 3F nur lesbar; Stack-MMU: Segment 3F Limit 10H.
        ladeSdr(r.l, DATA_MMU, 0x3F, kennbasis(1, 0x3F), 0xFF, Z8010::ATTR_RD);
        ladeSdr(r.l, STACK_MMU, 0x3F, kennbasis(2, 0x3F), 0x10, 0);
        // Code-MMU: Segment 3E nur bis 20H lang (Holen dahinter verletzt).
        ladeSdr(r.l, CODE_MMU, 0x3E, kennbasis(0, 0x3E), 0x20, 0);
        r.l.ioSchreiben(L::P_NBR, 0x80);
        r.l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    }
};

TEST_F(P8000MmuLogikTrap, TrplHaeltOffsetLowDesVerletzendenZyklus) {
    std::vector<L::SegtEreignis> ev;
    r.l.onSegtEreignis = [&](const L::SegtEreignis& e) { ev.push_back(e); };
    r.l.zyklus(zyk(ST_IF1, false, 0x3F, 0x1234));
    r.l.zyklus(zyk(ST_INSTR, false, 0x3F, 0x1236));
    const auto a = r.l.zyklus(zyk(ST_DATA, false, 0x3F, 0x5678, false));   // RDV in der Data-MMU
    EXPECT_TRUE(a.unterdrueckt);
    EXPECT_TRUE(r.l.segt());
    EXPECT_EQ(r.l.ioLesen(L::P_TRPL), 0x78);
    EXPECT_EQ(r.l.ioLesen(L::P_IF1L), 0x34);
    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].mmus, 2);
    EXPECT_EQ(ev[0].wahl, W::Data);
    EXPECT_EQ(ev[0].nummer, 1u);
    EXPECT_EQ(r.l.sicht().letztesSegt.trpl, 0x78);
    // Unechtes Holen danach ändert die Latches nicht (Stufe 2 friert ein).
    r.l.zyklus(zyk(ST_IF1, false, 0x3F, 0x123A));
    EXPECT_EQ(r.l.ioLesen(L::P_IF1L), 0x34);
    EXPECT_EQ(r.l.sicht().if1lStufe1, 0x3A);
}

TEST_F(P8000MmuLogikTrap, If1lHaeltVorgaengerWennDasHolenVerletzt) {
    r.l.zyklus(zyk(ST_IF1, false, 0x3E, 0x11AB));
    r.l.zyklus(zyk(ST_IF1, false, 0x3E, 0x22CD));   // Offset-High 22 > Limit 20 ⇒ SLV
    EXPECT_TRUE(r.l.segt());
    EXPECT_EQ(r.l.ioLesen(L::P_TRPL), 0xCD);
    EXPECT_EQ(r.l.ioLesen(L::P_IF1L), 0xAB);
    // Gleiche Aussage wie IOFF der Code-MMU (Z8010: Vorgängerbefehl).
    EXPECT_EQ(sin(r.l, CODE_MMU, Z8010::CMD_IOFF), 0x11);
}

TEST_F(P8000MmuLogikTrap, ZweiteMmuBeiGezogenerLeitungTaktetNicht) {
    r.l.zyklus(zyk(ST_IF1, false, 0x3F, 0x0100));
    r.l.zyklus(zyk(ST_DATA, false, 0x3F, 0x2011, false));   // Data RDV ⇒ Flanke
    r.l.zyklus(zyk(ST_STACK, false, 0x3F, 0xF022, false));  // Stack SLV, Leitung schon aktiv
    EXPECT_TRUE(r.l.mmu(L::Mmu::Stack).segt());
    EXPECT_EQ(r.l.ioLesen(L::P_TRPL), 0x11);
    EXPECT_EQ(r.l.sicht().segtFlanken, 1u);
    // Quittung: beide Kennwortbits, dann fällt die Leitung.
    const uint16_t kw = r.l.segtQuittung();
    EXPECT_EQ((kw >> 8) & 7, 6);
    EXPECT_FALSE(r.l.segt());
    ASSERT_GE(r.segt.size(), 2u);
    EXPECT_EQ(r.segt[r.segt.size() - 2], true);
    EXPECT_EQ(r.segt.back(), false);
}

TEST_F(P8000MmuLogikTrap, If1lStufe1NurBeiStatus1101) {
    r.l.zyklus(zyk(ST_IF1, false, 0x3F, 0x0144));
    r.l.zyklus(zyk(ST_INSTR, false, 0x3F, 0x0166));   // weiteres Befehlswort
    r.l.zyklus(zyk(ST_DATA, false, 0x3F, 0x0188));
    EXPECT_EQ(r.l.sicht().if1lStufe1, 0x44);

    L::Config c;
    c.if1lAuchStatus1100 = true;
    L l2(c);
    l2.zyklus(zyk(ST_IF1, true, 0, 0x0144));
    l2.zyklus(zyk(ST_INSTR, true, 0, 0x0166));
    EXPECT_EQ(l2.sicht().if1lStufe1, 0x66);
}

TEST(P8000MmuLogik, SegtQuittungKennwort) {
    // MSEN-MMUs treiben AD(8+ID): H die anfordernde, L die übrigen; Rest offen; UND bei gleicher ID.
    for (int maskeMsen = 0; maskeMsen < 8; ++maskeMsen)
        for (int anfordernd = 0; anfordernd < 3; ++anfordernd) {
            L l;
            const uint16_t port[3] = {CODE_MMU, DATA_MMU, STACK_MMU};
            for (int i = 0; i < 3; ++i) ladeSdr(l, port[i], 0x3F, 0, 0x00, 0);   // Limit 0 ⇒ SLV ab 0100
            // Anfordernde MMU: MSEN + TRNS ohne MST (übersetzt unabhängig von N/S), ID 4+i.
            for (int i = 0; i < 3; ++i) {
                const bool msen = (maskeMsen >> i) & 1;
                const uint8_t mr = uint8_t((msen || i == anfordernd ? 0x80 : 0) |
                                           (i == anfordernd ? 0x40 : 0) | (4 + i));
                sout(l, port[i], Z8010::CMD_MR, mr);
            }
            l.ioSchreiben(L::P_SCR, L::SCR_BDMEM_AUS);
            l.zyklus(zyk(ST_DATA, true, 0x3F, 0x0200));
            ASSERT_TRUE(l.segt());
            uint16_t soll = 0xFFFF;
            for (int i = 0; i < 3; ++i) {
                const bool treibt = ((maskeMsen >> i) & 1) || i == anfordernd;
                if (treibt && i != anfordernd) soll &= uint16_t(~(1u << (12 + i)));
            }
            EXPECT_EQ(l.segtQuittung(), soll) << maskeMsen << "/" << anfordernd;
            EXPECT_FALSE(l.segt());
        }
    // Gleiche ID (MON16 schreibt %D0 an ALL_MMU): L gewinnt.
    L l;
    sout(l, ALL_MMU, Z8010::CMD_MR, 0xC0);   // ohne MST: alle übersetzen, alle ID 0
    ladeSdr(l, CODE_MMU, 0x01, 0, 0x00, 0);
    ladeSdr(l, DATA_MMU, 0x01, 0, 0xFF, 0);
    ladeSdr(l, STACK_MMU, 0x01, 0, 0xFF, 0);
    l.ioSchreiben(L::P_SCR, L::SCR_BDMEM_AUS);
    l.zyklus(zyk(ST_DATA, true, 0x01, 0x0100));
    ASSERT_TRUE(l.mmu(L::Mmu::Code).segt());
    EXPECT_EQ(l.segtQuittung(), 0xFEFF);
}

// ═══ Nicht-Speicherzyklen, Probe, Save-State ═════════════════════════════════════════════════

TEST(P8000MmuLogik, NichtSpeicherzyklenOhneZielUndOhneWirkung) {
    L l;
    kennaufbau(l);
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON);
    std::vector<uint8_t> vor;
    l.serialize(vor);
    for (uint8_t st : {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0xE, 0xF})
        for (bool system : {true, false}) {
            const auto a = l.zyklus(zyk(st, system, 0, 0x0000));
            EXPECT_EQ(a.ziel, Z::Keins);
            EXPECT_EQ(a.wahl, W::Keine);
        }
    std::vector<uint8_t> nach;
    l.serialize(nach);
    EXPECT_EQ(vor, nach);
}

TEST(P8000MmuLogik, ProbeOhneNebenwirkungUndGleichDemZyklus) {
    Rig r;
    kennaufbau(r.l);
    ladeSdr(r.l, DATA_MMU, 0x3F, 0x0400, 0x20, Z8010::ATTR_RD);
    r.l.ioSchreiben(L::P_NBR, 0x80);
    r.l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
    const Z8kBusCycle faelle[] = {zyk(ST_IF1, false, 0x3F, 0x0100), zyk(ST_DATA, false, 0x3F, 0x1000),
                                  zyk(ST_DATA, false, 0x3F, 0x3000, false), zyk(ST_STACK, false, 0x3F, 0xF000),
                                  zyk(ST_DATA, true, 0x00, 0x4000), zyk(ST_DATA, false, 0x45, 0x1000)};
    for (const auto& c : faelle) {
        std::vector<uint8_t> vor, nach;
        r.l.serialize(vor);
        const auto pr = r.l.probe(c);
        r.l.serialize(nach);
        EXPECT_EQ(vor, nach);
        const auto z = r.l.zyklus(c);
        EXPECT_EQ(pr.zugriff.ziel, z.ziel);
        EXPECT_EQ(pr.zugriff.adresse, z.adresse);
        EXPECT_EQ(pr.zugriff.wahl, z.wahl);
        EXPECT_EQ(pr.zugriff.sup, z.sup);
        EXPECT_EQ(pr.zugriff.unterdrueckt, z.unterdrueckt);
        EXPECT_EQ(pr.zugriff.treiber, z.treiber);
    }
    EXPECT_EQ(L::wahlName(W::Stack), std::string("STACK"));
    EXPECT_EQ(L::zielName(Z::Hauptspeicher), std::string("HAUPT"));
}

TEST(P8000MmuLogik, SaveStateRundreiseIstBitgleich) {
    Rig a;
    kennaufbau(a.l);
    ladeSdr(a.l, DATA_MMU, 0x3F, 0x0400, 0x20, Z8010::ATTR_RD);
    a.l.ioSchreiben(L::P_NBR, 0x80);
    a.l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS | L::SCR_PARITAET);
    a.l.ioSchreiben(L::P_LEDAUS, 0);
    a.l.ioSchreiben(L::P_RETI, 0xED);                      // halbe RETI-Folge
    a.l.zyklus(zyk(ST_IF1, false, 0x3F, 0x0156));
    a.l.zyklus(zyk(ST_DATA, false, 0x3F, 0x1078, false));  // RDV: SEGT, SUP bis Befehlsende
    ASSERT_TRUE(a.l.segt());
    std::vector<uint8_t> s1;
    a.l.serialize(s1);

    Rig b;
    b.l.ioSchreiben(L::P_NBR, 0x11);
    const uint8_t* p = s1.data();
    ASSERT_TRUE(b.l.deserialize(p, s1.data() + s1.size()));
    EXPECT_EQ(p, s1.data() + s1.size());
    ASSERT_FALSE(b.segt.empty());
    EXPECT_TRUE(b.segt.back());                            // Pegel an die CPU
    std::vector<uint8_t> s2;
    b.l.serialize(s2);
    EXPECT_EQ(s1, s2);
    // Gleiches Weiterlaufen: SUP läuft bis Befehlsende weiter, RETI-Folge wird vollendet.
    int retiA = 0, retiB = 0;
    a.l.onReti = [&] { ++retiA; };
    b.l.onReti = [&] { ++retiB; };
    a.l.ioSchreiben(L::P_RETI, 0x4D);
    b.l.ioSchreiben(L::P_RETI, 0x4D);
    EXPECT_EQ(retiA, 1);
    EXPECT_EQ(retiB, 1);
    const auto za = a.l.zyklus(zyk(ST_DATA, false, 0x3F, 0x9000, false));
    const auto zb = b.l.zyklus(zyk(ST_DATA, false, 0x3F, 0x9000, false));
    EXPECT_TRUE(za.unterdrueckt);
    EXPECT_EQ(za.unterdrueckt, zb.unterdrueckt);
    EXPECT_EQ(a.l.segtQuittung(), b.l.segtQuittung());
    std::vector<uint8_t> s3, s4;
    a.l.serialize(s3);
    b.l.serialize(s4);
    EXPECT_EQ(s3, s4);
    // Defekt: abgeschnitten oder falsche Version ⇒ abgelehnt, Ist-Zustand bleibt.
    std::vector<uint8_t> kurz(s1.begin(), s1.end() - 1);
    p = kurz.data();
    std::vector<uint8_t> vor, nach;
    b.l.serialize(vor);
    EXPECT_FALSE(b.l.deserialize(p, kurz.data() + kurz.size()));
    b.l.serialize(nach);
    EXPECT_EQ(vor, nach);
    s1[0] = 99;
    p = s1.data();
    EXPECT_FALSE(b.l.deserialize(p, s1.data() + s1.size()));
}

// ═══ Matrix ══════════════════════════════════════════════════════════════════════════════════

TEST(P8000MmuLogikMatrix, GatterGebenHoechstensEineMmuUndFolgenDerTabelle) {
    // Alle Eingangskombinationen der reinen Gatterlogik (16 Status × 2^5).
    for (int st = 0; st < 16; ++st)
        for (int b = 0; b < 32; ++b) {
            const bool system = b & 1, segUser = b & 2, sn6 = b & 4, datenSeite = b & 8, onBoard = b & 16;
            const W w = L::auswahl(uint8_t(st), system, segUser, sn6, datenSeite, onBoard);
            W soll;
            if (!(st & 8) || onBoard) soll = W::Keine;            // nur IST3, nicht On-Board
            else if (system) soll = W::Code;
            else if (segUser) soll = sn6 ? W::Stack : W::Data;
            else if (st & 4) soll = W::Code;
            else soll = datenSeite ? W::Data : W::Stack;
            EXPECT_EQ(w, soll) << st << "/" << b;
        }
}

TEST(P8000MmuLogikMatrix, AlleZyklusartenMalZustaende) {
    for (bool gleichStack : {true, false}) {
        L::Config c;
        c.nbrGleichheitStack = gleichStack;
        L l(c);
        kennaufbau(l);
        size_t faelle = 0;
        for (uint8_t nbr : {0x00, 0x80, 0xFF}) {
            l.ioSchreiben(L::P_NBR, nbr);
            for (uint8_t scr = 0; scr < 8; ++scr) {   // Bit 0/1/2 alle Kombinationen
                l.ioSchreiben(L::P_SCR, scr);
                for (int st = 0; st < 16; ++st)
                    for (bool system : {true, false})
                        for (uint8_t seg : {0x00, 0x01, 0x02, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x7F})
                            for (uint16_t off : {0x0000, 0x00FF, 0x7FFF, 0x8000, 0x80FF, 0x8100, 0xFEFF, 0xFF00,
                                                 0xFFFF})
                                for (bool lesen : {true, false}) {
                                    const auto z = l.zyklus(zyk(uint8_t(st), system, seg, off, lesen));
                                    const W soll = erwarteteWahl(uint8_t(st), system, scr & L::SCR_SEG_USR, seg,
                                                                 off, nbr, scr, gleichStack);
                                    ++faelle;
                                    ASSERT_EQ(z.wahl, soll) << st << " sys" << system << " seg" << int(seg)
                                                            << " off" << off << " nbr" << int(nbr)
                                                            << " scr" << int(scr);
                                    const bool speicher = st >= 8 && st <= 0xD;
                                    if (!speicher) { ASSERT_EQ(z.ziel, Z::Keins); continue; }
                                    const bool onBoard = !(scr & 1) && seg == 0 && off < 0x8000;
                                    if (onBoard) { ASSERT_NE(z.ziel, Z::Hauptspeicher); continue; }
                                    ASSERT_EQ(z.ziel, Z::Hauptspeicher);
                                    ASSERT_FALSE(z.unterdrueckt);   // Limit FF, keine Schutzbits
                                    uint32_t adr;
                                    if (!(scr & L::SCR_MMU_ON)) adr = (uint32_t(seg) << 16) | off;
                                    else if (soll == W::Code && (seg & 0x40)) adr = 0xFFFF00u | (off & 0xFF);
                                    else {
                                        const int m = soll == W::Code ? 0 : soll == W::Data ? 1 : 2;
                                        adr = (uint32_t(kennbasis(m, seg)) << 8) + off;
                                    }
                                    ASSERT_EQ(z.adresse, adr) << st << " seg" << int(seg) << " off" << off;
                                    ASSERT_FALSE(z.konflikt);
                                }
            }
        }
        EXPECT_EQ(faelle, 3u * 8 * 16 * 2 * 9 * 9 * 2);
    }
}

// ═══ Gegenprobe Software ═════════════════════════════════════════════════════════════════════

TEST(P8000MmuLogikGegenprobe, Mon16ErkennungIndexAbEins) {
    // p.test.s vor Testschritt 84: MMU aus <01>0000 := 0001, <01>0100 := 0000; SDR Code/Data/Stack
    // Seg 0 = LISTE4/LISTE4/LISTE10, Seg 1 = LISTE8/LISTE9; MR alle %D0, S_BNK = 02; im System-
    // Mode segmentiert `ld r12, @rr10` mit rr10 = <01>0100.  r12 ≠ 0 ⇒ REM_MMU1 ≠ 0 ⇒ Index ≥ 1:
    // SEG USR-Weg, Testschritt 84 (SBR-Rücklesen) entfällt.  Die Code-MMU (Seg 1, Limit 0)
    // verletzt (SLV) ⇒ SUP ⇒ der Lesezyklus erreicht den Speicher nicht ⇒ offener Bus ≠ 0.
    L l;
    l.ioSchreiben(L::P_SCR, 0);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, true, 0x01, 0x0100, false)).adresse, 0x010100u);
    ladeSdr(l, CODE_MMU, 0, 0x0000, 0xFF, 0x00);
    ladeSdr(l, CODE_MMU, 1, 0x0100, 0x00, 0x00);
    ladeSdr(l, DATA_MMU, 0, 0x0000, 0xFF, 0x00);
    ladeSdr(l, DATA_MMU, 1, 0x0101, 0x00, 0x00);
    ladeSdr(l, STACK_MMU, 0, 0x0000, 0x00, 0x20);
    sout(l, ALL_MMU, Z8010::CMD_MR, 0xD0);
    l.ioSchreiben(L::P_SCR, 0x02);
    const auto a = l.zyklus(zyk(ST_DATA, true, 0x01, 0x0100, true));
    EXPECT_EQ(a.wahl, W::Code);
    EXPECT_EQ(a.adresse, 0x010100u);
    EXPECT_TRUE(a.unterdrueckt);   // [L8] ⇒ Karte liefert leer16 ≠ 0
    EXPECT_TRUE(l.segt());
}

TEST(P8000MmuLogikGegenprobe, Mon16Testschritte85bis90) {
    // SEGMENT_TRAP_TEST (REM_MMU1 ≠ 0): S_BNK = 06, rl4 = %D2 ⇒ Stack-MR = D2, Code 80, Data 81,
    // Segmente 42–7F; rl4 = %D1 ⇒ Data-MR = D1, Code 80, Stack 82, Segmente 02–3F.  Normal-Mode
    // segmentiert, `ld @rr2, r5`; der Befehl selbst kommt On-Board (Segment 0 < 8000H).
    // Kennwort: `exb; and #7` ⇒ 1 Code, 2 Data, 4 Stack.
    const uint8_t L4[4] = {0x00, 0x00, 0xFF, 0x00}, L5[4] = {0x00, 0x00, 0x00, 0x00},
                  L6[4] = {0x00, 0x00, 0xFF, 0x20}, L7[4] = {0x00, 0x00, 0xFF, 0x01};
    struct Fall { int nr; const uint8_t *code, *data, *stack; uint16_t off; uint8_t rl4, soll; };
    const Fall faelle[] = {
        {85, L4, L4, L5, 0xA000, 0xD2, 4}, {86, L4, L4, L5, 0x8000, 0xD1, 0},
        {87, L4, L6, L4, 0xA000, 0xD2, 0}, {88, L4, L6, L4, 0x8000, 0xD1, 2},
        {89, L4, L7, L7, 0xA000, 0xD2, 4}, {90, L4, L7, L7, 0x8000, 0xD1, 2},
    };
    for (const auto& f : faelle) {
        L l;
        l.ioSchreiben(L::P_NBR, 0x90);
        ladeAlle(l, CODE_MMU, f.code);
        ladeAlle(l, DATA_MMU, f.data);
        ladeAlle(l, STACK_MMU, f.stack);
        const bool stapel = f.rl4 & 2;
        for (int seg = stapel ? 0x42 : 0x02; seg < (stapel ? 0x80 : 0x40); ++seg) {
            l.ioSchreiben(L::P_SCR, 0x06);
            if (stapel) {
                sout(l, STACK_MMU, 0, f.rl4); sout(l, CODE_MMU, 0, 0x80); sout(l, DATA_MMU, 0, 0x81);
            } else {
                sout(l, DATA_MMU, 0, f.rl4); sout(l, CODE_MMU, 0, 0x80); sout(l, STACK_MMU, 0, 0x82);
            }
            EXPECT_EQ(l.zyklus(zyk(ST_IF1, false, 0x00, 0x2520)).ziel, Z::OnBoardEprom);
            const auto w = l.zyklus(zyk(ST_DATA, false, uint8_t(seg), f.off, false));
            l.zyklus(zyk(ST_IF1, false, 0x00, 0x2524));   // nächstes Holen (ggf. unecht)
            uint8_t id = 0;
            if (l.segt()) id = uint8_t((l.segtQuittung() >> 8) & 7);
            EXPECT_EQ(id, f.soll) << "Testschritt " << f.nr << " Segment " << seg;
            EXPECT_EQ(w.unterdrueckt, f.soll != 0) << f.nr;
            sout(l, ALL_MMU, Z8010::CMD_VTR_LOESCHEN, 0);   // TRAP_OUT: Reset VTR
            sout(l, ALL_MMU, Z8010::CMD_MR, 0);
            l.ioSchreiben(L::P_SCR, 0);
        }
    }
}

TEST(P8000MmuLogikGegenprobe, WegaNichtsegmentierterAnwender) {
    // mmu.h: TEXT/DATA = Segment 3F, STAK 7F; Anwender läuft nichtsegmentiert in <3F>, SEG USR = 0.
    // Holen → Code-MMU Seg 3F, Daten unter NBR → Data-MMU Seg 3F, Stack über NBR → Stack-MMU Seg 3F.
    L l;
    kennaufbau(l);
    l.ioSchreiben(L::P_NBR, 0xC0);
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS | L::SCR_PARITAET);
    EXPECT_EQ(l.zyklus(zyk(ST_IF1, false, 0x3F, 0x0000)).adresse, uint32_t(kennbasis(0, 0x3F)) << 8);
    EXPECT_EQ(l.zyklus(zyk(ST_DATA, false, 0x3F, 0x1234)).adresse, (uint32_t(kennbasis(1, 0x3F)) << 8) + 0x1234);
    EXPECT_EQ(l.zyklus(zyk(ST_STACK, false, 0x3F, 0xFFFE, false)).adresse,
              (uint32_t(kennbasis(2, 0x3F)) << 8) + 0xFFFE);
    // Der Kern (System-Mode, segmentiert) sieht nur die Code-MMU — auch für sein Segment 3E.
    EXPECT_EQ(l.zyklus(zyk(ST_STACK, true, 0x3E, 0xFFFE, false)).wahl, W::Code);
}
