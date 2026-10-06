/**
 * @test Z8010.* — UB8010/Z8010-MMU (P8000 AP P9a): Register und Befehle, Übersetzung, jede
 *       Verletzungsart, SUP/SEGT, Zustandsautomat ([TB] Anhang A), DMA, Mehrfach-MMU und die
 *       Kern-Initialisierung aus WEGA `uts/conf/mch.s` (`start`, `physaddr`, `invsdrs`, `resmmu`).
 *       Quelle der Sollwerte: doc/p8000/z8010_mmu.md, doc/trascripted/MMU_8010.md.
 */

#include <gtest/gtest.h>
#include "core/primitives/z8010.h"

#include <vector>

namespace {

constexpr uint8_t DATA = 0x8, STACK = 0x9, INSTR = 0xC, IF1 = 0xD;

struct Rig {
    Z8010 mmu;
    std::vector<bool> segtFlanken;
    Rig() { mmu.onSegt = [this](bool a) { segtFlanken.push_back(a); }; }

    void w(uint8_t code, uint8_t d) { mmu.kommandoSchreiben(code, d); }
    uint8_t r(uint8_t code) { return mmu.kommandoLesen(code); }

    /// SDR über SAR + %0B (4 Byte) laden — der Weg von WEGA.
    void sdr(uint8_t nr, uint16_t basis, uint8_t limit, uint8_t attr) {
        w(Z8010::CMD_SAR, nr);
        for (uint8_t b : {uint8_t(basis >> 8), uint8_t(basis), limit, attr}) w(Z8010::CMD_SDR, b);
    }
    Z8010::Ergebnis cpu(uint8_t st, uint8_t seg, uint16_t off, bool lesen, bool nsHigh = false) {
        return mmu.zyklus({st, nsHigh, lesen, seg, off, false});
    }
    Z8010::Ergebnis dma(uint8_t st, uint8_t seg, uint16_t off, bool lesen, bool nsHigh = false) {
        return mmu.zyklus({st, nsHigh, lesen, seg, off, true});
    }
    /// Befehlsende nach einer Verletzung: unechtes Holen + Quittung.
    Z8010::Kennung quittiere(uint8_t seg = 0, uint16_t pc = 0x0100) {
        cpu(IF1, seg, pc, true);
        return mmu.segtQuittung();
    }
};

/// Übersetzend, eine Tabelle (MST = 0), ID 2.
constexpr uint8_t MR_AN = Z8010::MR_MSEN | Z8010::MR_TRNS | 2;

// ─── Lebenslauf und Register ────────────────────────────────────────────────

TEST(Z8010, PowerOnAllesNullUndKeineAdresse) {
    Rig t;
    EXPECT_EQ(t.r(Z8010::CMD_MR), 0);
    EXPECT_EQ(t.r(Z8010::CMD_VTR), 0);
    EXPECT_FALSE(t.mmu.segt());
    EXPECT_FALSE(t.cpu(DATA, 1, 0x1234, true).adresse);   // MSEN = 0 ⇒ Tri-State
}

TEST(Z8010, ResetMitCsSchaltetDurchreichenEin) {
    Rig t;
    t.w(Z8010::CMD_MR, 0xFF);
    t.sdr(7, 0x1234, 0x56, 0x00);
    t.w(Z8010::CMD_DSCR, 2);
    t.mmu.reset(true);
    EXPECT_EQ(t.mmu.mr(), Z8010::MR_MSEN);   // MSEN, TRNS = 0
    EXPECT_EQ(t.mmu.dscr(), 0);
    EXPECT_EQ(t.mmu.deskriptor(7).basis, 0x1234);   // SDR überleben den Reset
    auto e = t.cpu(DATA, 0x45, 0xABCD, true);
    ASSERT_TRUE(e.adresse);
    EXPECT_EQ(e.phys, 0x45ABCDu);                    // SN → A16–A22, A23 = L
    t.mmu.reset(false);
    EXPECT_EQ(t.mmu.mr(), 0);
}

TEST(Z8010, RegisterBitlagen) {
    Rig t;
    t.w(Z8010::CMD_MR, 0xD3);
    EXPECT_EQ(t.r(Z8010::CMD_MR), 0xD3);
    t.w(Z8010::CMD_SAR, 0xFF);
    EXPECT_EQ(t.r(Z8010::CMD_SAR), 0x3F);
    t.w(Z8010::CMD_DSCR, 0xFF);
    EXPECT_EQ(t.r(Z8010::CMD_DSCR), 0x03);
    // Statusregister sind nur lesbar.
    t.w(Z8010::CMD_VTR, 0xFF);
    EXPECT_EQ(t.r(Z8010::CMD_VTR), 0);
}

TEST(Z8010, UnbenutzteBefehleLesenFFundWirkenNicht) {
    Rig t;
    for (uint8_t c : {0x12, 0x17, 0x1F, 0x21, 0xFF, 0x11, 0x15}) EXPECT_EQ(t.r(c), 0xFF) << int(c);
    t.w(Z8010::CMD_MR, 0x5A);
    t.w(0x12, 0x00);
    t.w(0x21, 0x00);
    EXPECT_EQ(t.mmu.mr(), 0x5A);
}

// ─── Deskriptorzugriffe ─────────────────────────────────────────────────────

TEST(Z8010, GesamterDeskriptorUndDscrZurueckAufNull) {
    Rig t;
    t.sdr(5, 0x2311, 0x7F, Z8010::ATTR_RD);
    auto d = t.mmu.deskriptor(5);
    EXPECT_EQ(d.basis, 0x2311);
    EXPECT_EQ(d.limit, 0x7F);
    EXPECT_EQ(d.attr, Z8010::ATTR_RD);
    EXPECT_EQ(t.mmu.dscr(), 0);
    EXPECT_EQ(t.mmu.sar(), 5);
    std::vector<uint8_t> rb;
    for (int i = 0; i < 4; ++i) rb.push_back(t.r(Z8010::CMD_SDR));
    EXPECT_EQ(rb, (std::vector<uint8_t>{0x23, 0x11, 0x7F, 0x01}));
    EXPECT_EQ(t.mmu.dscr(), 0);
}

TEST(Z8010, BasisfeldErstHighDannLowAbbruchLaesstDscrAufEins) {
    Rig t;
    t.w(Z8010::CMD_SAR, 6);
    t.w(Z8010::CMD_BASIS, 0xAB);
    EXPECT_EQ(t.mmu.dscr(), 1);   // [TB] §7.4: nur ein Byte ⇒ DSCR bleibt 1
    t.w(Z8010::CMD_BASIS, 0xCD);
    EXPECT_EQ(t.mmu.dscr(), 0);
    EXPECT_EQ(t.mmu.deskriptor(6).basis, 0xABCD);
    EXPECT_EQ(t.mmu.sar(), 6);
}

TEST(Z8010, LimitUndAttributStellenDscrSelbstUndDanachNull) {
    Rig t;
    t.w(Z8010::CMD_SAR, 9);
    t.w(Z8010::CMD_DSCR, 1);
    t.w(Z8010::CMD_LIMIT, 0x42);
    EXPECT_EQ(t.mmu.dscr(), 0);
    t.w(Z8010::CMD_DSCR, 1);
    t.w(Z8010::CMD_ATTR, 0x24);
    EXPECT_EQ(t.mmu.dscr(), 0);
    EXPECT_EQ(t.mmu.deskriptor(9).limit, 0x42);
    EXPECT_EQ(t.mmu.deskriptor(9).attr, 0x24);
    EXPECT_EQ(t.mmu.deskriptor(9).basis, 0);
    EXPECT_EQ(t.r(Z8010::CMD_LIMIT), 0x42);
    EXPECT_EQ(t.r(Z8010::CMD_ATTR), 0x24);
}

TEST(Z8010, DscrNichtNullTrifftFalschesFeld) {
    // [TB] §7.4 / Annahme [A13]: %0B beginnt beim stehengebliebenen DSCR.
    Rig t;
    t.w(Z8010::CMD_SAR, 3);
    t.w(Z8010::CMD_DSCR, 2);
    t.w(Z8010::CMD_SDR, 0x11);
    t.w(Z8010::CMD_SDR, 0x22);
    EXPECT_EQ(t.mmu.deskriptor(3).limit, 0x11);
    EXPECT_EQ(t.mmu.deskriptor(3).attr, 0x22);
    EXPECT_EQ(t.mmu.dscr(), 0);
}

TEST(Z8010, InkrementierendeBefehleLaufenVon63Auf0Um) {
    Rig t;
    t.w(Z8010::CMD_SAR, 63);
    for (uint8_t b : {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}) t.w(Z8010::CMD_SDR_INC, b);
    EXPECT_EQ(t.mmu.deskriptor(63).basis, 0x0102);
    EXPECT_EQ(t.mmu.deskriptor(63).attr, 0x04);
    EXPECT_EQ(t.mmu.deskriptor(0).basis, 0x0506);
    EXPECT_EQ(t.mmu.deskriptor(0).attr, 0x08);
    EXPECT_EQ(t.mmu.sar(), 1);
    EXPECT_EQ(t.mmu.dscr(), 0);

    t.w(Z8010::CMD_SAR, 62);
    for (uint8_t b : {0xA0, 0xA1, 0xA2}) t.w(Z8010::CMD_LIMIT_INC, b);
    EXPECT_EQ(t.mmu.deskriptor(62).limit, 0xA0);
    EXPECT_EQ(t.mmu.deskriptor(63).limit, 0xA1);
    EXPECT_EQ(t.mmu.deskriptor(0).limit, 0xA2);
    EXPECT_EQ(t.mmu.sar(), 1);

    t.w(Z8010::CMD_SAR, 63);
    for (uint8_t b : {0xB0, 0xB1}) t.w(Z8010::CMD_ATTR_INC, b);
    EXPECT_EQ(t.mmu.deskriptor(63).attr, 0xB0);
    EXPECT_EQ(t.mmu.deskriptor(0).attr, 0xB1);

    t.w(Z8010::CMD_SAR, 63);
    for (uint8_t b : {0xC0, 0xC1, 0xC2, 0xC3}) t.w(Z8010::CMD_BASIS_INC, b);
    EXPECT_EQ(t.mmu.deskriptor(63).basis, 0xC0C1);
    EXPECT_EQ(t.mmu.deskriptor(0).basis, 0xC2C3);
    EXPECT_EQ(t.mmu.sar(), 1);
    // Lesen zählt genauso.
    t.w(Z8010::CMD_SAR, 63);
    EXPECT_EQ(t.r(Z8010::CMD_BASIS_INC), 0xC0);
    EXPECT_EQ(t.r(Z8010::CMD_BASIS_INC), 0xC1);
    EXPECT_EQ(t.r(Z8010::CMD_BASIS_INC), 0xC2);
    EXPECT_EQ(t.mmu.sar(), 0);
}

TEST(Z8010, CpuiUndDmaiSetzenAlleUnabhaengigVomDatenbyte) {
    Rig t;
    t.sdr(10, 0, 0, Z8010::ATTR_RD);
    t.w(Z8010::CMD_CPUI_SETZEN, 0x00);   // MAME würde bei 0 löschen — [TB] setzt immer
    t.w(Z8010::CMD_DMAI_SETZEN, 0x00);
    for (int i = 0; i < 64; ++i) {
        EXPECT_TRUE(t.mmu.deskriptor(i).attr & Z8010::ATTR_CPUI) << i;
        EXPECT_TRUE(t.mmu.deskriptor(i).attr & Z8010::ATTR_DMAI) << i;
    }
    EXPECT_EQ(t.mmu.deskriptor(10).attr, Z8010::ATTR_RD | Z8010::ATTR_CPUI | Z8010::ATTR_DMAI);
}

// ─── Übersetzung ─────────────────────────────────────────────────────────────

TEST(Z8010, UebersetzungBeispielBild43) {
    Rig t;
    t.sdr(5, 0x2311, 0xFF, 0);
    t.w(Z8010::CMD_MR, MR_AN);
    auto e = t.cpu(DATA, 5, 0x1528, true);
    ASSERT_TRUE(e.adresse);
    EXPECT_EQ(e.phys, 0x232628u);
    EXPECT_FALSE(e.sup);
}

TEST(Z8010, StapelsegmentUmruendetIn24Bit) {
    // [TB] §5.4 Beispiel: TOP = 8, n = 4 ⇒ Basis %FF08, Limit %FC, %FFE0 → %7E0.
    Rig t;
    t.sdr(1, 0xFF08, 0xFC, Z8010::ATTR_DIRW);
    t.w(Z8010::CMD_MR, MR_AN);
    auto e = t.cpu(STACK, 1, 0xFFE0, true);
    ASSERT_TRUE(e.adresse);
    EXPECT_EQ(e.phys, 0x0007E0u);
    EXPECT_EQ(t.cpu(STACK, 1, 0xFC00, true).phys, 0x000400u);   // unterstes Wort
    // Beispiel TOP = %1E8, n = %24 ⇒ Basis %00E8, Limit %DC.
    t.sdr(2, 0x00E8, 0xDC, Z8010::ATTR_DIRW);
    EXPECT_EQ(t.cpu(STACK, 2, 0xFFFE, true).phys, 0x01E7FEu);
    EXPECT_EQ(t.cpu(STACK, 2, 0xDC00, true).phys, 0x01C400u);
}

TEST(Z8010, NurSpeicherzyklenWerdenUebersetzt) {
    Rig t;
    t.w(Z8010::CMD_MR, MR_AN);
    for (uint8_t st : {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0xE, 0xF})
        EXPECT_FALSE(t.cpu(st, 0, 0x1000, true).adresse) << int(st);
    for (uint8_t st : {0x8, 0x9, 0xA, 0xB, 0xC, 0xD})
        EXPECT_TRUE(t.cpu(st, 0, 0x1000, true).adresse) << int(st);
}

TEST(Z8010, UrsUndMstWaehlenDieMmu) {
    Rig t;
    t.w(Z8010::CMD_MR, MR_AN);                 // URS = 0
    EXPECT_FALSE(t.cpu(DATA, 0x45, 0, true).adresse);
    t.w(Z8010::CMD_MR, MR_AN | Z8010::MR_URS);
    EXPECT_TRUE(t.cpu(DATA, 0x45, 0, true).adresse);
    EXPECT_FALSE(t.cpu(DATA, 0x05, 0, true).adresse);

    // MST, NMS = 0: nur bei N/S = L ([TB] §4.1 Nr. 5).
    t.w(Z8010::CMD_MR, MR_AN | Z8010::MR_MST);
    EXPECT_TRUE(t.cpu(DATA, 1, 0, true, false).adresse);
    EXPECT_FALSE(t.cpu(DATA, 1, 0, true, true).adresse);
    t.w(Z8010::CMD_MR, MR_AN | Z8010::MR_MST | Z8010::MR_NMS);
    EXPECT_FALSE(t.cpu(DATA, 1, 0, true, false).adresse);
    EXPECT_TRUE(t.cpu(DATA, 1, 0, true, true).adresse);

    // TRNS = 0: URS und MST werden ignoriert ([TB] §4.1 Nr. 3) — Abweichung von MAME.
    t.w(Z8010::CMD_MR, Z8010::MR_MSEN | Z8010::MR_MST | Z8010::MR_NMS);
    auto e = t.cpu(DATA, 0x45, 0x1234, true, false);
    ASSERT_TRUE(e.adresse);
    EXPECT_EQ(e.phys, 0x451234u);
}

TEST(Z8010, NichtGewaehlteMmuErzeugtKeineVerletzung) {
    Rig t;
    t.sdr(1, 0, 0x00, Z8010::ATTR_CPUI);
    t.w(Z8010::CMD_MR, MR_AN | Z8010::MR_MST);
    auto e = t.cpu(DATA, 1, 0x8000, false, true);   // N/S = H ⇒ nicht gewählt
    EXPECT_FALSE(e.adresse);
    EXPECT_FALSE(e.sup);
    EXPECT_EQ(t.mmu.vtr(), 0);
    EXPECT_FALSE(t.mmu.segt());
}

// ─── Verletzungsarten ────────────────────────────────────────────────────────

struct Fall { const char* name; uint8_t attr; uint8_t limit; uint8_t st; uint16_t off; bool lesen; bool nsHigh; uint8_t vtr; uint8_t bcsr; };

TEST(Z8010, JedeVerletzungsartSetztIhrFlagMitSupUndSegt) {
    const Fall faelle[] = {
        {"RDV",  Z8010::ATTR_RD,   0xFF, DATA,  0x1000, false, false, Z8010::VTR_RDV,   0x08},
        {"SYSV", Z8010::ATTR_SYS,  0xFF, DATA,  0x1000, true,  true,  Z8010::VTR_SYSV,  0x38},
        {"SLV",  0,                0x0F, DATA,  0x1000, true,  false, Z8010::VTR_SLV,   0x18},
        {"SLV-DIRW", Z8010::ATTR_DIRW, 0xF0, STACK, 0xEFFE, true, false, Z8010::VTR_SLV, 0x19},
        {"CPUIV", Z8010::ATTR_CPUI, 0xFF, IF1,  0x1000, true,  false, Z8010::VTR_CPUIV, 0x1D},
        {"EXCV", Z8010::ATTR_EXC,  0xFF, DATA,  0x1000, true,  false, Z8010::VTR_EXCV,  0x18},
    };
    for (const auto& f : faelle) {
        Rig t;
        t.sdr(3, 0x0100, f.limit, f.attr);
        t.w(Z8010::CMD_MR, MR_AN);
        auto e = t.cpu(f.st, 3, f.off, f.lesen, f.nsHigh);
        EXPECT_TRUE(e.adresse) << f.name;            // Adresse auch bei Verletzung ([TB] §2.1.1)
        EXPECT_TRUE(e.sup) << f.name;
        EXPECT_TRUE(t.mmu.segt()) << f.name;
        EXPECT_EQ(t.r(Z8010::CMD_VTR), f.vtr) << f.name;
        EXPECT_EQ(t.r(Z8010::CMD_VSN), 3) << f.name;
        EXPECT_EQ(t.r(Z8010::CMD_VOFF), uint8_t(f.off >> 8)) << f.name;
        EXPECT_EQ(t.r(Z8010::CMD_BCSR), f.bcsr) << f.name;
        EXPECT_EQ(t.mmu.deskriptor(3).attr & (Z8010::ATTR_REF | Z8010::ATTR_CHG), 0) << f.name;
    }
}

TEST(Z8010, ErlaubteZugriffeAmRandDesSegments) {
    Rig t;
    t.sdr(3, 0, 0x0F, Z8010::ATTR_EXC);
    t.sdr(4, 0, 0xF0, Z8010::ATTR_DIRW | Z8010::ATTR_RD);
    t.sdr(5, 0, 0xFF, Z8010::ATTR_SYS);
    t.w(Z8010::CMD_MR, MR_AN);
    EXPECT_FALSE(t.cpu(IF1, 3, 0x0FFE, true).sup);     // Limit N ⇒ bis 256·N+255
    EXPECT_FALSE(t.cpu(INSTR, 3, 0x0FFE, true).sup);   // EXC: %C erlaubt
    EXPECT_FALSE(t.cpu(STACK, 4, 0xF000, true).sup);   // DIRW: ab 256·N
    EXPECT_FALSE(t.cpu(IF1, 4, 0xFFFE, true).sup);     // RD: Holen erlaubt
    EXPECT_FALSE(t.cpu(DATA, 5, 0x1000, false, false).sup);   // SYS bei N/S = L
    EXPECT_EQ(t.mmu.vtr(), 0);
    EXPECT_FALSE(t.mmu.segt());
}

TEST(Z8010, MehrereVerletzungenEinesZugriffsBeispiel812) {
    // [TB] §8.1.2: MMU2 (URS = 1, ID 2), Schreiben auf <65>%9328 in ein RD-Segment über dem Limit.
    Rig t;
    t.sdr(1, 0x4000, 0x8F, Z8010::ATTR_RD);
    t.w(Z8010::CMD_MR, MR_AN | Z8010::MR_URS);
    auto e = t.cpu(DATA, 65, 0x9328, false, true);
    EXPECT_TRUE(e.sup);
    EXPECT_EQ(t.r(Z8010::CMD_VTR), 0x05);
    EXPECT_EQ(t.r(Z8010::CMD_BCSR), 0x28);
    EXPECT_EQ(t.r(Z8010::CMD_VSN), 0x01);
    EXPECT_EQ(t.r(Z8010::CMD_VOFF), 0x93);
    t.cpu(IF1, 65, 0x0010, true, true);   // unechtes Holen
    auto k = t.mmu.segtQuittung();
    EXPECT_EQ(k.maske, 0x0400);           // AD10
    EXPECT_EQ(k.wert, 0x0400);
    EXPECT_FALSE(t.mmu.segt());
}

TEST(Z8010, SchreibwarnungImUnterstenBlock) {
    Rig t;
    t.sdr(7, 0xFF08, 0xFC, Z8010::ATTR_DIRW);
    t.w(Z8010::CMD_MR, MR_AN);
    auto e = t.cpu(STACK, 7, 0xFCFE, false);
    EXPECT_FALSE(e.sup);                   // nie SUP bei Warnung
    EXPECT_TRUE(t.mmu.segt());
    EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_PWW);
    EXPECT_EQ(t.mmu.deskriptor(7).attr, Z8010::ATTR_DIRW | Z8010::ATTR_REF | Z8010::ATTR_CHG);
    // Lesen im untersten Block warnt nicht.
    Rig u;
    u.sdr(7, 0xFF08, 0xFC, Z8010::ATTR_DIRW);
    u.w(Z8010::CMD_MR, MR_AN);
    u.cpu(STACK, 7, 0xFCFE, true);
    EXPECT_EQ(u.mmu.vtr(), 0);
}

TEST(Z8010, RefUndChgNurBeiCpuOhneVerletzung) {
    Rig t;
    t.sdr(1, 0, 0xFF, 0);
    t.sdr(2, 0, 0xFF, 0);
    t.w(Z8010::CMD_MR, MR_AN);
    t.cpu(DATA, 1, 0x10, true);
    EXPECT_EQ(t.mmu.deskriptor(1).attr, Z8010::ATTR_REF);
    t.cpu(DATA, 1, 0x10, false);
    EXPECT_EQ(t.mmu.deskriptor(1).attr, Z8010::ATTR_REF | Z8010::ATTR_CHG);
    t.dma(DATA, 2, 0x10, false);
    EXPECT_EQ(t.mmu.deskriptor(2).attr, 0);
}

// ─── SUP-Dauer, unechtes Holen, Quittung ────────────────────────────────────

TEST(Z8010, SupGiltBisZumBefehlsende) {
    Rig t;
    t.sdr(1, 0, 0x0F, 0);
    t.sdr(2, 0, 0xFF, 0);
    t.w(Z8010::CMD_MR, MR_AN);
    t.cpu(IF1, 2, 0x0100, true);
    EXPECT_TRUE(t.cpu(DATA, 1, 0x2000, false).sup);   // SLV
    EXPECT_TRUE(t.cpu(DATA, 2, 0x0010, false).sup);   // gleicher Befehl, an sich erlaubt
    EXPECT_FALSE(t.dma(DATA, 2, 0x0010, false).sup);  // DMA dazwischen unberührt
    EXPECT_TRUE(t.cpu(STACK, 2, 0x0FFE, false).sup);
    EXPECT_EQ(t.mmu.deskriptor(2).attr & Z8010::ATTR_CHG, 0);   // unterdrückt ⇒ kein CHG
    EXPECT_FALSE(t.cpu(IF1, 2, 0x0104, true).sup);    // unechtes Holen ohne Verletzung
    t.mmu.segtQuittung();
    EXPECT_FALSE(t.cpu(STACK, 2, 0x0FFC, false).sup); // Statusretten nicht unterdrückt
    EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV);
}

TEST(Z8010, UnechtesHolenGibtNurSup) {
    Rig t;
    t.sdr(1, 0, 0x0F, 0);
    t.sdr(2, 0, 0x00, 0);   // nur Block 0
    t.w(Z8010::CMD_MR, MR_AN);
    t.cpu(IF1, 1, 0x0100, true);
    t.cpu(DATA, 1, 0x2000, true);                    // SLV
    auto e = t.cpu(IF1, 2, 0x0100, true);            // unechtes Holen, verletzt
    EXPECT_TRUE(e.sup);
    EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV);          // kein FATL, kein weiteres Flag
    EXPECT_EQ(t.mmu.isn(), 1);
    EXPECT_EQ(t.mmu.ioff(), 0x01);
    EXPECT_EQ(t.segtFlanken, (std::vector<bool>{true}));
    auto k = t.mmu.segtQuittung();
    EXPECT_EQ(k.wert, 0x0400);
    EXPECT_EQ(t.segtFlanken, (std::vector<bool>{true, false}));
}

TEST(Z8010, QuittungsKennwortNurBeiMsen) {
    Rig t;
    EXPECT_EQ(t.mmu.segtQuittung().maske, 0);   // MSEN = 0 ⇒ Leitung nicht getrieben
    t.w(Z8010::CMD_MR, Z8010::MR_MSEN | 5);
    auto k = t.mmu.segtQuittung();
    EXPECT_EQ(k.maske, 0x2000);                 // AD13 L-Pegel, keine Anforderung
    EXPECT_EQ(k.wert, 0);
}

TEST(Z8010, IsnIoffZeigenAufDenLetztenBefehl) {
    Rig t;
    t.sdr(3, 0, 0xFF, 0);
    t.sdr(4, 0, 0x00, 0);
    t.w(Z8010::CMD_MR, MR_AN);
    t.cpu(IF1, 3, 0x1234, true);
    t.cpu(INSTR, 3, 0x1236, true);
    t.cpu(DATA, 4, 0x0500, true);   // SLV im Datenzugriff
    EXPECT_EQ(t.mmu.isn(), 3);
    EXPECT_EQ(t.mmu.ioff(), 0x12);

    // Verletzt das Holen selbst, steht der Vorgänger (der Sprung) in ISN/IOFF.
    Rig u;
    u.sdr(3, 0, 0xFF, 0);
    u.sdr(4, 0, 0x00, 0);
    u.w(Z8010::CMD_MR, MR_AN);
    u.cpu(IF1, 3, 0x5600, true);
    u.cpu(IF1, 4, 0x0700, true);    // Sprungziel außerhalb
    EXPECT_EQ(u.mmu.vtr(), Z8010::VTR_SLV);
    EXPECT_EQ(u.mmu.isn(), 3);
    EXPECT_EQ(u.mmu.ioff(), 0x56);
    EXPECT_EQ(u.mmu.vsn(), 4);
    EXPECT_EQ(u.mmu.voff(), 0x07);
    EXPECT_EQ(u.mmu.bcsr(), 0x1D);
}

// ─── Zustandsautomat ([TB] Anhang A) ────────────────────────────────────────

struct Automat : Rig {
    Automat() {
        sdr(1, 0x0000, 0x0F, 0);                     // Daten, Blöcke 0..F
        sdr(2, 0x0000, 0xFF, 0);                     // Code
        sdr(3, 0xFF10, 0xF8, Z8010::ATTR_DIRW);      // Systemstapel, Warnblock F8
        w(Z8010::CMD_MR, MR_AN);
    }
    void befehl() { cpu(IF1, 2, 0x0200, true); }
    void verletze() { cpu(DATA, 1, 0x4000, true); }  // SLV
    Z8010::Ergebnis stapelWarnung(bool nsHigh = false, uint8_t st = STACK) {
        return cpu(st, 3, 0xF8F0, false, nsHigh);
    }
};

TEST(Z8010, ZweiteVerletzungImFolgebefehlSetztFatl) {
    Automat t;
    t.befehl();
    t.verletze();
    t.quittiere(2, 0x0204);
    EXPECT_FALSE(t.mmu.segt());
    t.befehl();
    auto e = t.cpu(DATA, 1, 0x0010, false);         // erlaubt
    EXPECT_FALSE(e.sup);
    t.befehl();
    t.sdr(5, 0, 0xFF, Z8010::ATTR_RD);
    e = t.cpu(DATA, 5, 0x0010, false);              // RDV im späteren Befehl
    EXPECT_TRUE(e.sup);
    EXPECT_TRUE(t.mmu.segt());                       // Übergang zieht SEGT einmal [A6]
    EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV | Z8010::VTR_FATL);   // RDV nicht dazu
    EXPECT_EQ(t.mmu.vsn(), 1);                       // Register bleiben bei der ersten
    t.quittiere(2, 0x0208);
    // FATL-Zustand: Verletzung ⇒ nur SUP, Schreibwarnung ⇒ nichts.
    t.befehl();
    e = t.cpu(DATA, 5, 0x0010, false);
    EXPECT_TRUE(e.sup);
    EXPECT_FALSE(t.mmu.segt());
    t.befehl();
    e = t.stapelWarnung(true);
    EXPECT_FALSE(e.sup);
    EXPECT_FALSE(t.mmu.segt());
    EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV | Z8010::VTR_FATL);
}

TEST(Z8010, MehrereVerletzungenImSelbenBefehlSammelnFlagsOhneFatl) {
    Automat t;
    t.sdr(5, 0, 0xFF, Z8010::ATTR_RD);
    t.befehl();
    t.verletze();                          // SLV
    t.cpu(DATA, 5, 0x0010, false);         // RDV, gleicher Befehl
    t.stapelWarnung();                     // Warnung, gleicher Befehl ⇒ nichts
    EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV | Z8010::VTR_RDV);
    EXPECT_EQ(t.mmu.voff(), 0x40);         // erster Zugriff festgehalten
}

TEST(Z8010, SchreibwarnungAufDemSystemstapelSetztSww) {
    Automat t;
    t.befehl();
    t.verletze();
    t.cpu(IF1, 2, 0x0204, true);           // unecht
    t.mmu.segtQuittung();
    auto e = t.stapelWarnung();            // Statusretten in die Warnzone
    EXPECT_FALSE(e.sup);
    EXPECT_TRUE(t.mmu.segt());
    EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV | Z8010::VTR_SWW);
    e = t.stapelWarnung();                 // gleicher Abschnitt: kein zweiter Wechsel
    t.cpu(IF1, 2, 0x0300, true);           // unecht (SEGT steht an)
    t.mmu.segtQuittung();
    e = t.stapelWarnung();                 // SWW-Zustand: Stapelwarnung ⇒ nichts
    EXPECT_FALSE(t.mmu.segt());
    EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV | Z8010::VTR_SWW);
    // Verletzung im SWW-Zustand ⇒ SWW/FATL mit SEGT und SUP.
    t.befehl();
    e = t.cpu(DATA, 1, 0x4000, true);
    EXPECT_TRUE(e.sup);
    EXPECT_TRUE(t.mmu.segt());
    EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV | Z8010::VTR_SWW | Z8010::VTR_FATL);
    t.quittiere(2, 0x0400);
    // SWW/FATL: Warnungen weder SEGT noch SUP, Verletzungen nur SUP.
    t.befehl();
    e = t.stapelWarnung(false, DATA);
    EXPECT_FALSE(e.sup);
    EXPECT_FALSE(t.mmu.segt());
    t.befehl();
    EXPECT_TRUE(t.cpu(DATA, 1, 0x4000, true).sup);
    EXPECT_FALSE(t.mmu.segt());

    // Rückwege: %14 ⇒ SWW-Zustand, %13 ⇒ einfach, %11 ⇒ normal.
    t.w(Z8010::CMD_FATL_LOESCHEN, 0);
    EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV | Z8010::VTR_SWW);
    t.w(Z8010::CMD_SWW_LOESCHEN, 0);
    EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV);
    t.w(Z8010::CMD_VTR_LOESCHEN, 0);
    EXPECT_EQ(t.mmu.vtr(), 0);
}

TEST(Z8010, SchreibwarnungImNormalmodusOderOhneStapelstatusSetztFatl) {
    for (int fall = 0; fall < 3; ++fall) {   // Normalmodus, Datenstatus, EPU-Stapel %B
        Automat t;
        t.befehl();
        t.verletze();
        t.quittiere(2, 0x0204);
        t.befehl();
        auto e = fall == 0 ? t.stapelWarnung(true) : t.stapelWarnung(false, fall == 1 ? DATA : uint8_t(0xB));
        EXPECT_FALSE(e.sup) << fall;
        EXPECT_TRUE(t.mmu.segt()) << fall;
        EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV | Z8010::VTR_FATL) << fall;
    }
}

TEST(Z8010, ResetBefehl10LoeschtMrVtrDscrUndSegt) {
    Automat t;
    t.verletze();
    t.w(Z8010::CMD_DSCR, 3);
    ASSERT_TRUE(t.mmu.segt());
    t.w(Z8010::CMD_RESET, 0);
    EXPECT_EQ(t.mmu.mr(), 0);
    EXPECT_EQ(t.mmu.vtr(), 0);
    EXPECT_EQ(t.mmu.dscr(), 0);
    EXPECT_FALSE(t.mmu.segt());
    EXPECT_EQ(t.mmu.deskriptor(3).basis, 0xFF10);
}

TEST(Z8010, Befehl11LoeschtVtrAberNichtDieAnstehendeAnforderung) {
    Automat t;
    t.verletze();
    t.w(Z8010::CMD_VTR_LOESCHEN, 0);
    EXPECT_EQ(t.mmu.vtr(), 0);
    EXPECT_TRUE(t.mmu.segt());
    EXPECT_EQ(t.mmu.segtQuittung().wert, 0x0400);
}

// ─── DMA ─────────────────────────────────────────────────────────────────────

TEST(Z8010, DmaVerletzungNurSupKeinStatus) {
    Rig t;
    t.sdr(1, 0, 0x0F, Z8010::ATTR_CPUI);
    t.sdr(2, 0, 0xFF, Z8010::ATTR_DMAI);
    t.sdr(3, 0x0100, 0xFF, Z8010::ATTR_RD);
    t.w(Z8010::CMD_MR, MR_AN);
    EXPECT_FALSE(t.dma(DATA, 1, 0x0100, false).sup);   // CPUI gilt nicht für DMA
    EXPECT_TRUE(t.dma(DATA, 1, 0x2000, true).sup);     // Länge
    EXPECT_TRUE(t.dma(DATA, 2, 0x0000, true).sup);     // DMAI
    auto e = t.dma(DATA, 3, 0x0000, false);            // RD
    EXPECT_TRUE(e.sup);
    EXPECT_EQ(e.phys, 0x010000u);
    EXPECT_FALSE(t.dma(DATA, 3, 0x0000, true).sup);    // nur für den eigenen Zyklus
    EXPECT_EQ(t.mmu.vtr(), 0);
    EXPECT_FALSE(t.mmu.segt());
    EXPECT_TRUE(t.segtFlanken.empty());
    // Schreibwarnung durch DMA: kein Signal.
    t.sdr(4, 0, 0xF0, Z8010::ATTR_DIRW);
    e = t.dma(STACK, 4, 0xF010, false);
    EXPECT_FALSE(e.sup);
    EXPECT_EQ(t.mmu.vtr(), 0);
}

// ─── Mehrfach-MMU ────────────────────────────────────────────────────────────

TEST(Z8010, DreiMmusMitMstWieAmP8000) {
    // N/S-Eingang = Auswahlsignal (L = gewählt), Mode D0/D1/D2 (MON16 p.test.s).
    Z8010 code, data, stack;
    code.kommandoSchreiben(Z8010::CMD_MR, 0xD0);
    data.kommandoSchreiben(Z8010::CMD_MR, 0xD1);
    stack.kommandoSchreiben(Z8010::CMD_MR, 0xD2);
    for (Z8010* m : {&code, &data, &stack}) {
        m->kommandoSchreiben(Z8010::CMD_SAR, 0x3F);
        for (uint8_t b : {0x00, 0x10, 0xFF, 0x00}) m->kommandoSchreiben(Z8010::CMD_SDR, b);
    }
    data.kommandoSchreiben(Z8010::CMD_SAR, 0x3F);
    data.kommandoSchreiben(Z8010::CMD_BASIS, 0x00);
    data.kommandoSchreiben(Z8010::CMD_BASIS, 0x20);
    data.kommandoSchreiben(Z8010::CMD_ATTR, Z8010::ATTR_RD);

    // Datenzyklus: nur die Daten-MMU sieht N/S = L.
    auto zyk = [](bool gewaehlt, bool lesen) {
        return Z8010::Zyklus{DATA, !gewaehlt, lesen, 0x3F, 0x1234, false};
    };
    EXPECT_FALSE(code.zyklus(zyk(false, false)).adresse);
    EXPECT_FALSE(stack.zyklus(zyk(false, false)).adresse);
    auto e = data.zyklus(zyk(true, false));
    ASSERT_TRUE(e.adresse);
    EXPECT_EQ(e.phys, 0x003234u);                     // Basis %0020 + %12
    EXPECT_TRUE(e.sup);                                    // RD
    EXPECT_EQ(data.kommandoLesen(Z8010::CMD_BCSR), 0x08);  // N/S-Pegel des Eingangs = L
    // Quittung: alle drei treiben ihre Leitung, nur die Daten-MMU H.
    uint16_t maske = 0, wert = 0;
    for (Z8010* m : {&code, &data, &stack}) {
        auto k = m->segtQuittung();
        maske |= k.maske;
        wert |= k.wert;
    }
    EXPECT_EQ(maske, 0x0700);
    EXPECT_EQ(wert, 0x0200);   // p.test.s: Bit 1 = DATA
}

// ─── Gegenprobe WEGA-Kern (uts/conf/mch.s) ──────────────────────────────────

TEST(Z8010, WegaKernstartMchS) {
    // start: SAR := %3E, SDR %3E der Code-MMU := {Basis = _kclicks + 10 − %100, Limit %F5,
    // Attr %22}, sotirb mit 4 Byte (ldl @rr4, rr0: r0 = Basis, rh1 = Limit, rl1 = Attr),
    // danach MR der Stack-MMU := %D3.  Code-MMU-MR hat der Monitor gesetzt (Annahme %D0).
    Z8010 code, stack;
    code.kommandoSchreiben(Z8010::CMD_MR, 0xD0);
    const uint16_t kclicks = 0x0123 + 10;                // r3 + USIZE
    const uint16_t basis = uint16_t(kclicks - 0x100);    // wrappt: %002D
    code.kommandoSchreiben(Z8010::CMD_SAR, 0x3E);
    const uint8_t mmuinit[4] = {uint8_t(basis >> 8), uint8_t(basis), 0xF5, 0x22};
    for (uint8_t b : mmuinit) code.kommandoSchreiben(Z8010::CMD_SDR, b);   // sotirb, Code %0B bei jedem Byte
    stack.kommandoSchreiben(Z8010::CMD_MR, 0xD3);
    EXPECT_EQ(code.dscr(), 0);
    EXPECT_EQ(code.deskriptor(0x3E).basis, 0x002D);
    EXPECT_EQ(code.deskriptor(0x3E).attr, Z8010::ATTR_DIRW | Z8010::ATTR_SYS);
    EXPECT_EQ(stack.mr() & 0x07, 3);   // ID 3
    EXPECT_TRUE(stack.mr() & Z8010::MR_MST);
    EXPECT_FALSE(stack.mr() & Z8010::MR_NMS);

    // rr14 = <%3E>%FFFE: oberstes Wort = letztes Wort des Kerns + USIZE.
    auto zyk = [](uint8_t st, uint16_t off, bool lesen) {
        return Z8010::Zyklus{st, false, lesen, 0x3E, off, false};
    };
    auto e = code.zyklus(zyk(STACK, 0xFFFE, false));
    ASSERT_TRUE(e.adresse);
    EXPECT_EQ(e.phys, (uint32_t(kclicks - 1) << 8) | 0xFE);
    EXPECT_FALSE(e.sup);
    // User-Struktur ab <%3E>%F600 = physisch _kclicks(alt) · 256.
    e = code.zyklus(zyk(DATA, 0xF600, false));
    EXPECT_EQ(e.phys, uint32_t(0x0123) << 8);
    EXPECT_FALSE(e.sup);
    EXPECT_FALSE(code.segt());
    // Block %F5 ist die Warnzone (pwwarn1 prüft VTR-Bit 5).
    e = code.zyklus(zyk(STACK, 0xF5F0, false));
    EXPECT_FALSE(e.sup);
    EXPECT_TRUE(code.segt());
    EXPECT_EQ(code.kommandoLesen(Z8010::CMD_VTR) & 0x20, 0x20);
    // resmmu: %11 an alle; danach unter der Zone ⇒ SLV.
    code.kommandoSchreiben(Z8010::CMD_VTR_LOESCHEN, 0);
    code.segtQuittung();
    e = code.zyklus(zyk(STACK, 0xF4FE, false));
    EXPECT_TRUE(e.sup);
    EXPECT_EQ(code.vtr(), Z8010::VTR_SLV);

    // physaddr: SAR := Segment, zweimal %08 lesen (erst High, dann Low).
    code.kommandoSchreiben(Z8010::CMD_SAR, 0x3E);
    const uint8_t hi = code.kommandoLesen(Z8010::CMD_BASIS);
    const uint8_t lo = code.kommandoLesen(Z8010::CMD_BASIS);
    EXPECT_EQ((hi << 8) | lo, 0x002D);
    EXPECT_EQ(code.dscr(), 0);

    // invsdrs: soutb DATA_MMU+%1500, rh0 mit beliebigem rh0.
    stack.kommandoSchreiben(Z8010::CMD_CPUI_SETZEN, 0x00);
    for (int i = 0; i < 64; ++i) EXPECT_TRUE(stack.deskriptor(i).attr & Z8010::ATTR_CPUI);
}

// ─── Systematische Matrizen ─────────────────────────────────────────────────
// Erwartungen unabhängig von der Implementierung aus [TB] §3–§6 formuliert.

std::vector<uint8_t> zustand(const Z8010& m) {
    std::vector<uint8_t> v;
    m.serialize(v);
    return v;
}

/// Bekannter Ausgangszustand: SDR i = {Basis i·%0101, Limit i, Attr i & %3F}, SAR %21, MR %C2,
/// eine festgehaltene SLV-Verletzung (VTR %04, SEGT aktiv).
struct Vorbelegt : Rig {
    Vorbelegt() {
        for (int i = 0; i < 64; ++i) sdr(uint8_t(i), uint16_t(i * 0x0101), uint8_t(i), uint8_t(i & 0x3F));
        w(Z8010::CMD_MR, 0xC2);
        cpu(DATA, 0x01, 0x4000, true);   // Limit 1 ⇒ SLV
        w(Z8010::CMD_SAR, 0x21);
    }
};

TEST(Z8010Matrix, JederBefehlscodeLesen) {
    for (int code = 0; code < 256; ++code) {
        Vorbelegt t;
        const Z8010::Deskriptor d = t.mmu.deskriptor(0x21);
        const uint8_t r = t.r(uint8_t(code));
        int soll;
        uint8_t sarDanach = 0x21, dscrDanach = 0;
        switch (code) {
        case 0x00: soll = 0xC2; break;
        case 0x01: soll = 0x21; break;
        case 0x02: soll = Z8010::VTR_SLV; break;
        case 0x03: soll = 0x01; break;
        case 0x04: soll = 0x40; break;
        case 0x05: soll = 0x18; break;   // Systemmodus, Lesen, Status 8
        case 0x06: case 0x07: soll = 0x00; break;   // kein Holen davor
        case 0x08: soll = d.basis >> 8; dscrDanach = 1; break;
        case 0x0C: soll = d.basis >> 8; dscrDanach = 1; break;
        case 0x0B: case 0x0F: soll = d.basis >> 8; dscrDanach = 1; break;
        case 0x09: soll = d.limit; break;
        case 0x0D: soll = d.limit; sarDanach = 0x22; break;
        case 0x0A: soll = d.attr; break;
        case 0x0E: soll = d.attr; sarDanach = 0x22; break;
        case 0x20: soll = 0x00; break;
        default:   soll = 0xFF; break;   // [A11]
        }
        EXPECT_EQ(r, soll) << "Code " << code;
        EXPECT_EQ(t.mmu.sar(), sarDanach) << "Code " << code;
        EXPECT_EQ(t.mmu.dscr(), dscrDanach) << "Code " << code;
        EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV) << "Code " << code;   // Lesen löscht nichts
        EXPECT_TRUE(t.mmu.segt()) << "Code " << code;
    }
}

TEST(Z8010Matrix, JederBefehlscodeSchreiben) {
    for (int code = 0; code < 256; ++code) {
        Vorbelegt t;
        const auto vorher = zustand(t.mmu);
        const Z8010::Deskriptor d = t.mmu.deskriptor(0x21);
        t.w(uint8_t(code), 0xA5);
        const auto n = t.mmu.deskriptor(0x21);
        const std::string c = "Code " + std::to_string(code);
        switch (code) {
        case 0x00: EXPECT_EQ(t.mmu.mr(), 0xA5) << c; break;
        case 0x01: EXPECT_EQ(t.mmu.sar(), 0x25) << c; EXPECT_EQ(t.mmu.dscr(), 0) << c; break;
        case 0x08: case 0x0B: case 0x0C: case 0x0F:
            EXPECT_EQ(n.basis, uint16_t(0xA500 | (d.basis & 0xFF))) << c;
            EXPECT_EQ(t.mmu.dscr(), 1) << c;
            EXPECT_EQ(t.mmu.sar(), 0x21) << c;
            break;
        case 0x09: case 0x0D:
            EXPECT_EQ(n.limit, 0xA5) << c;
            EXPECT_EQ(t.mmu.sar(), code == 0x0D ? 0x22 : 0x21) << c;
            EXPECT_EQ(t.mmu.dscr(), 0) << c;
            break;
        case 0x0A: case 0x0E:
            EXPECT_EQ(n.attr, 0xA5) << c;
            EXPECT_EQ(t.mmu.sar(), code == 0x0E ? 0x22 : 0x21) << c;
            break;
        case 0x10:
            EXPECT_EQ(t.mmu.mr(), 0) << c; EXPECT_EQ(t.mmu.vtr(), 0) << c;
            EXPECT_FALSE(t.mmu.segt()) << c;
            break;
        case 0x11: EXPECT_EQ(t.mmu.vtr(), 0) << c; EXPECT_TRUE(t.mmu.segt()) << c; break;
        case 0x13: case 0x14: EXPECT_EQ(t.mmu.vtr(), Z8010::VTR_SLV) << c; break;
        case 0x15: case 0x16:
            for (int i = 0; i < 64; ++i)
                EXPECT_EQ(t.mmu.deskriptor(i).attr,
                          uint8_t((i & 0x3F) | (code == 0x15 ? Z8010::ATTR_CPUI : Z8010::ATTR_DMAI))) << c << " SDR " << i;
            break;
        case 0x20: EXPECT_EQ(t.mmu.dscr(), 0x01) << c; break;
        default:
            EXPECT_EQ(zustand(t.mmu), vorher) << c;   // Status-Lesebefehle, reserviert: ohne Wirkung
            break;
        }
    }
}

TEST(Z8010Matrix, LimitUndWachstumsrichtungVollstaendig) {
    for (int dirw = 0; dirw < 2; ++dirw)
        for (int limit = 0; limit < 256; ++limit) {
            Z8010 m;
            m.kommandoSchreiben(Z8010::CMD_MR, MR_AN);
            m.kommandoSchreiben(Z8010::CMD_SAR, 9);
            m.kommandoSchreiben(Z8010::CMD_LIMIT, uint8_t(limit));
            m.kommandoSchreiben(Z8010::CMD_ATTR, dirw ? Z8010::ATTR_DIRW : 0);
            for (int hi = 0; hi < 256; ++hi) {
                const uint16_t off = uint16_t(hi << 8 | 0x5E);
                const bool slv = dirw ? hi < limit : hi > limit;
                const auto pl = m.probe({DATA, false, true, 9, off, false});
                const auto ps = m.probe({STACK, false, false, 9, off, false});
                ASSERT_EQ(pl.verletzung, slv ? Z8010::VTR_SLV : 0) << dirw << ' ' << limit << ' ' << hi;
                ASSERT_EQ(ps.warnung, dirw && hi == limit) << dirw << ' ' << limit << ' ' << hi;
                ASSERT_FALSE(pl.warnung);
            }
        }
}

TEST(Z8010Matrix, SchutzbitsEinzelnUndKombiniert) {
    // Alle 64 Kombinationen der sechs Schutzbits × Status %8–%D × R/W × N/S × CPU/DMA.
    for (int attr = 0; attr < 64; ++attr)
        for (uint8_t st = 0x8; st <= 0xD; ++st)
            for (int lesen = 0; lesen < 2; ++lesen)
                for (int ns = 0; ns < 2; ++ns)
                    for (int dma = 0; dma < 2; ++dma) {
                        Z8010 m;
                        m.kommandoSchreiben(Z8010::CMD_MR, MR_AN);
                        m.kommandoSchreiben(Z8010::CMD_SAR, 0);
                        m.kommandoSchreiben(Z8010::CMD_LIMIT, 0xFF);
                        m.kommandoSchreiben(Z8010::CMD_ATTR, uint8_t(attr));
                        const bool holen = st == 0xC || st == 0xD;
                        uint8_t soll = 0;
                        if (!lesen && (attr & Z8010::ATTR_RD)) soll |= Z8010::VTR_RDV;
                        if (ns && (attr & Z8010::ATTR_SYS)) soll |= Z8010::VTR_SYSV;
                        if (!dma && (attr & Z8010::ATTR_CPUI)) soll |= Z8010::VTR_CPUIV;
                        if (!holen && (attr & Z8010::ATTR_EXC)) soll |= Z8010::VTR_EXCV;
                        const bool dmai = dma && (attr & Z8010::ATTR_DMAI);
                        // DIRW mit Limit FF: nur Block FF erlaubt — Offset FFxx, Schreiben warnt.
                        const bool warn = !dma && !lesen && (attr & Z8010::ATTR_DIRW);
                        const Z8010::Zyklus z{st, ns != 0, lesen != 0, 0, 0xFF10, dma != 0};
                        const auto p = m.probe(z);
                        const std::string c = "attr " + std::to_string(attr) + " st " + std::to_string(st) +
                                              " r " + std::to_string(lesen) + " ns " + std::to_string(ns) +
                                              " dma " + std::to_string(dma);
                        ASSERT_EQ(p.verletzung, soll) << c;
                        ASSERT_EQ(p.dmaiVerletzung, dmai) << c;
                        ASSERT_EQ(p.warnung, warn) << c;
                        const auto e = m.zyklus(z);
                        ASSERT_EQ(e.sup, soll != 0 || dmai) << c;
                        ASSERT_EQ(m.segt(), !dma && (soll != 0 || warn)) << c;
                        ASSERT_EQ(m.vtr(), dma ? 0 : (soll | (warn ? Z8010::VTR_PWW : 0))) << c;
                        if (!dma && soll) {
                            ASSERT_EQ(m.bcsr(), (ns ? 0x20 : 0) | (lesen ? 0x10 : 0) | st) << c;
                        }
                        const uint8_t refchg = dma || soll ? 0
                            : uint8_t(Z8010::ATTR_REF | (lesen ? 0 : Z8010::ATTR_CHG));
                        ASSERT_EQ(m.deskriptor(0).attr, uint8_t(attr | refchg)) << c;
                    }
}

TEST(Z8010Matrix, BasisGrenzenUndSegmenteNullBis63) {
    const uint16_t basen[] = {0x0000, 0x0001, 0x00FF, 0x7FFF, 0x8000, 0xFF00, 0xFF01, 0xFFFF};
    for (int urs = 0; urs < 2; ++urs)
        for (int sdr = 0; sdr < 64; ++sdr)
            for (uint16_t b : basen) {
                Z8010 m;
                m.kommandoSchreiben(Z8010::CMD_MR, uint8_t(MR_AN | (urs ? Z8010::MR_URS : 0)));
                m.kommandoSchreiben(Z8010::CMD_SAR, uint8_t(sdr));
                m.kommandoSchreiben(Z8010::CMD_BASIS, uint8_t(b >> 8));
                m.kommandoSchreiben(Z8010::CMD_BASIS, uint8_t(b));
                m.kommandoSchreiben(Z8010::CMD_LIMIT, 0xFF);
                const uint8_t seg = uint8_t(sdr | (urs ? 0x40 : 0));
                for (uint16_t off : {0x0000, 0x00FF, 0x0100, 0x80A5, 0xFFFF}) {
                    const auto p = m.probe({DATA, false, true, seg, off, false});
                    ASSERT_TRUE(p.adresse);
                    const uint32_t soll = ((uint32_t(b) << 8) + off) & 0xFFFFFF;   // „Umrunden"
                    ASSERT_EQ(p.phys, soll) << sdr << ' ' << b << ' ' << off;
                    // Andere Hälfte der Segmentnummern: diese MMU schweigt.
                    ASSERT_FALSE(m.probe({DATA, false, true, uint8_t(seg ^ 0x40), off, false}).adresse);
                }
                // VSN = Nummer innerhalb der MMU [A9].
                m.kommandoSchreiben(Z8010::CMD_ATTR, Z8010::ATTR_CPUI);
                m.zyklus({DATA, false, true, seg, 0x1200, false});
                ASSERT_EQ(m.vsn(), sdr);
                ASSERT_EQ(m.voff(), 0x12);
            }
}

TEST(Z8010Matrix, ModusRegisterJederWert) {
    // MR 0..255 × SN6 × N/S-Eingang: wann übersetzt, wann durchgereicht, wann Tri-State.
    for (int mr = 0; mr < 256; ++mr)
        for (int sn6 = 0; sn6 < 2; ++sn6)
            for (int ns = 0; ns < 2; ++ns) {
                Z8010 m;
                m.kommandoSchreiben(Z8010::CMD_SAR, 3);
                for (uint8_t b : {0x12, 0x34, 0xFF, 0x00}) m.kommandoSchreiben(Z8010::CMD_SDR, b);
                m.kommandoSchreiben(Z8010::CMD_MR, uint8_t(mr));
                const uint8_t seg = uint8_t(3 | (sn6 ? 0x40 : 0));
                const auto p = m.probe({DATA, ns != 0, true, seg, 0xABCD, false});
                const bool msen = mr & 0x80, trns = mr & 0x40, urs = mr & 0x20, mst = mr & 0x10, nms = mr & 0x08;
                const std::string c = "MR " + std::to_string(mr) + " sn6 " + std::to_string(sn6) + " ns " + std::to_string(ns);
                if (!msen) {
                    ASSERT_FALSE(p.adresse) << c;
                } else if (!trns) {
                    ASSERT_TRUE(p.adresse) << c;
                    ASSERT_FALSE(p.geprueft) << c;
                    ASSERT_EQ(p.phys, (uint32_t(seg) << 16) | 0xABCD) << c;
                } else if (urs != bool(sn6) || (mst && nms != bool(ns))) {
                    ASSERT_FALSE(p.adresse) << c;
                } else {
                    ASSERT_TRUE(p.geprueft) << c;
                    ASSERT_EQ(p.phys, 0x12DFCDu) << c;   // %1234 + %AB
                }
                // Quittung: Leitung AD(8+ID) nur bei MSEN.
                ASSERT_EQ(m.segtQuittung().maske, msen ? uint16_t(1u << (8 + (mr & 7))) : 0) << c;
            }
}

TEST(Z8010Matrix, ProbeHatKeineSeiteneffekte) {
    Automat t;
    t.cpu(IF1, 2, 0x0300, true);
    const auto vorher = zustand(t.mmu);
    for (uint8_t st = 0; st < 16; ++st)
        for (uint8_t seg : {0x01, 0x02, 0x03, 0x41})
            for (uint16_t off : {0x0000, 0x4000, 0xF8F0, 0xFFFE})
                for (int lesen = 0; lesen < 2; ++lesen)
                    for (int dma = 0; dma < 2; ++dma)
                        t.mmu.probe({st, false, lesen != 0, seg, off, dma != 0});
    EXPECT_EQ(zustand(t.mmu), vorher);
    EXPECT_TRUE(t.segtFlanken.empty());
    // probe meldet den Befund, zyklus wirkt ihn aus.
    auto p = t.mmu.probe({DATA, false, true, 1, 0x4000, false});
    EXPECT_EQ(p.verletzung, Z8010::VTR_SLV);
    EXPECT_EQ(p.phys, 0x004000u);
    t.verletze();
    auto v = t.mmu.letzteVerletzung();
    EXPECT_EQ(v.vtr, Z8010::VTR_SLV);
    EXPECT_EQ(v.vsn, 1);
    EXPECT_EQ(v.voff, 0x40);
    EXPECT_EQ(v.bcsr, 0x18);
    EXPECT_EQ(v.isn, 2);
    EXPECT_EQ(v.ioff, 0x03);
    EXPECT_TRUE(v.segtAnforderung);
    EXPECT_TRUE(t.mmu.supBisBefehlsende());
}

TEST(Z8010Matrix, NichtSpeicherzyklenLassenAllesUnberuehrt) {
    for (uint8_t st : {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0xE, 0xF}) {
        Automat t;
        t.sdr(5, 0, 0x00, 0x3F);   // jede Prüfung schlägt an
        const auto vorher = zustand(t.mmu);
        for (int lesen = 0; lesen < 2; ++lesen)
            for (int dma = 0; dma < 2; ++dma) {
                auto e = t.mmu.zyklus({st, true, lesen != 0, 5, 0xFF00, dma != 0});
                EXPECT_FALSE(e.adresse) << int(st);
                EXPECT_FALSE(e.sup) << int(st);
            }
        EXPECT_EQ(zustand(t.mmu), vorher) << int(st);
    }
}

// ─── Save-State ──────────────────────────────────────────────────────────────

TEST(Z8010, SaveStateRundreiseIstBitgleich) {
    Automat t;
    t.befehl();
    t.verletze();
    t.w(Z8010::CMD_SAR, 0x21);
    t.w(Z8010::CMD_BASIS, 0x77);   // DSCR = 1
    std::vector<uint8_t> a;
    t.mmu.serialize(a);

    Z8010 b;
    const uint8_t* p = a.data();
    ASSERT_TRUE(b.deserialize(p, a.data() + a.size()));
    EXPECT_EQ(p, a.data() + a.size());
    std::vector<uint8_t> c;
    b.serialize(c);
    EXPECT_EQ(a, c);
    EXPECT_TRUE(b.segt());
    EXPECT_EQ(b.dscr(), 1);
    EXPECT_EQ(b.vtr(), Z8010::VTR_SLV);
    // Fortsetzung: zweites Basisbyte trifft das Low-Byte, SUP läuft bis Befehlsende weiter.
    b.kommandoSchreiben(Z8010::CMD_BASIS, 0x88);
    EXPECT_EQ(b.deskriptor(0x21).basis, 0x7788);
    EXPECT_TRUE(b.zyklus({DATA, false, true, 2, 0x0000, false}).sup);

    // Zu kurz oder falsche Version ⇒ abgelehnt.
    const uint8_t* q = a.data();
    EXPECT_FALSE(b.deserialize(q, a.data() + a.size() - 1));
    std::vector<uint8_t> falsch = a;
    falsch[0] = 99;
    q = falsch.data();
    EXPECT_FALSE(b.deserialize(q, falsch.data() + falsch.size()));
}

}  // namespace
