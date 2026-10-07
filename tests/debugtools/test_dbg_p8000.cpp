/**
 * @file test_dbg_p8000.cpp
 * @brief tools/dbg_p8000.h — die Textbausteine des P8000-Debuggers (AP P12a/P12b): Speicherbild,
 *        UA858-Register und -Statusprotokoll, Kopplungsprotokoll, UB8010-Register/Deskriptoren,
 *        probeweise Übersetzung (`xlat`), logischer/physischer Speicherzugriff (`ml`/`mp`).
 *
 * Wichtigste Eigenschaft von P12b: die Sicht ändert NICHTS (kein Violation-Latch, keine REF/CHG-Bits,
 * kein SEGT) — `Xlat.OhneNebenwirkung`.
 */
#include <gtest/gtest.h>

#include "tools/dbg_p8000.h"

namespace {

using L = P8000MmuLogik16;

bool enthaelt(const std::vector<std::string>& v, const std::string& n) {
    for (const auto& s : v)
        if (s.find(n) != std::string::npos) return true;
    return false;
}
bool enthaelt(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }

// MON16/WEGA-Konstanten (p.test.s, mch.s): /CS-Auswahl der drei UB8010 über die Spezial-E/A-Adresse.
constexpr uint16_t CODE_MMU = 0x00FC, DATA_MMU = 0x00FA, STACK_MMU = 0x00F6;

void sout(L& l, uint16_t mmu, uint8_t cmd, uint8_t v) {
    l.spezialSchreiben(uint16_t((cmd << 8) | (mmu & 0xFF)), uint16_t((v << 8) | v));
}
void ladeSdr(L& l, uint16_t mmu, uint8_t seg, uint16_t basis, uint8_t limit, uint8_t attr) {
    sout(l, mmu, Z8010::CMD_SAR, seg);
    for (uint8_t b : {uint8_t(basis >> 8), uint8_t(basis), limit, attr}) sout(l, mmu, Z8010::CMD_SDR, b);
}
/// Wie am P8000: Mode %D0/%D1/%D2 (MSEN, TRNS, URS/MST), MMU ein, On-Board aus.
void mmuEin(L& l) {
    sout(l, CODE_MMU, Z8010::CMD_MR, 0xD0);
    sout(l, DATA_MMU, Z8010::CMD_MR, 0xD1);
    sout(l, STACK_MMU, Z8010::CMD_MR, 0xD2);
    l.ioSchreiben(L::P_SCR, L::SCR_MMU_ON | L::SCR_BDMEM_AUS);
}

}  // namespace

// ═══ P12a: Z80-Seite ═════════════════════════════════════════════════════════════════════════

TEST(DbgP8000Adp, NachResetIstUeberallDasEpromSelektiert) {
    P8000Speicher8 sp;
    EXPECT_EQ(dbgp8::speicherbild(sp), "EEEEEEEEEEEEEEEE");
    const auto z = dbgp8::adpZeilen(sp);
    EXPECT_TRUE(enthaelt(z, "RFF=0 (EPROM ueberall selektiert)"));
    EXPECT_EQ(z.size(), size_t(18));   // Kopf, Spaltenkopf, 16 Seiten
    EXPECT_TRUE(enthaelt(z, "0000-0FFF"));
    EXPECT_TRUE(enthaelt(z, "F000-FFFF"));
}

TEST(DbgP8000Dma, RegisterUndStatusSindLesbar) {
    Z80Dma d("T");
    d.setIEI(true);
    d.reset();
    // WR0: Transfer A->B, alle vier Folgebytes (Start A, Blocklänge); WR1/WR2: Speicher aufsteigend
    for (uint8_t b : {0x7D, 0x34, 0x12, 0x10, 0x00, 0x14, 0x10}) d.ioWrite(0, b);
    const auto z = dbgp8::dmaZeilen(d.sicht());
    EXPECT_TRUE(enthaelt(z, "WR0=7D  Transfer, A -> B  Blocklaenge=0010"));
    EXPECT_TRUE(enthaelt(z, "Port A: Speicher, aufsteigend"));
    EXPECT_TRUE(enthaelt(z, "Start=1234"));
    EXPECT_TRUE(enthaelt(z, "RR0="));
    EXPECT_TRUE(enthaelt(z, "Lesemaske=7F"));
    // RR0: D1/D3/D4/D5 sind low-aktiv
    EXPECT_EQ(dbgp8::dmaRr0Text(0x3A), "-");
    EXPECT_EQ(dbgp8::dmaRr0Text(0x3B), "Transfer-erfolgt");
    EXPECT_EQ(dbgp8::dmaRr0Text(0x00), "RDY-aktiv INT-anstehend Treffer Blockende");
}

TEST(DbgP8000Dma, ProtokollSchreibtNurAenderungen) {
    Z80Dma d("T");
    d.setIEI(true);
    d.reset();
    dbgp8::DmaProtokoll p;
    p.beobachte(1, 0x100, d.sicht());                 // aus: nichts
    EXPECT_EQ(p.prot.gesamt(), 0u);
    p.an = true;
    p.beobachte(2, 0x100, d.sicht());
    p.beobachte(3, 0x102, d.sicht());                 // gleich: keine zweite Zeile
    EXPECT_EQ(p.prot.gesamt(), 1u);
    d.ioWrite(0, 0x87);                               // Freigabe ändert den Schlüssel
    p.beobachte(4, 0x104, d.sicht());
    EXPECT_EQ(p.prot.gesamt(), 2u);
    p.port(5, 0x106, false, 0xC3);
    const auto z = p.prot.zeilen(10);
    ASSERT_EQ(z.size(), size_t(3));
    EXPECT_TRUE(enthaelt(z[1], "frei"));
    EXPECT_TRUE(enthaelt(z[2], "OUT 3xH C3"));
    p.clear();
    EXPECT_EQ(p.prot.gesamt(), 0u);
}

TEST(DbgP8000Kopplung, ProtokolltexteUndPorterkennung) {
    EXPECT_TRUE(dbgp8::koppPort8(0x0C));
    EXPECT_TRUE(dbgp8::koppPort8(0x14));
    EXPECT_FALSE(dbgp8::koppPort8(0x18));
    EXPECT_EQ(dbgp8::koppText8(0x10, false, 0x5A), "8->16  L1 (D0-7/8-16)  = 5A");
    EXPECT_TRUE(enthaelt(dbgp8::koppText8(0x14, false, 0x83), "INT-16=1 V1-6=01 RDY/8-16=1"));
    EXPECT_EQ(dbgp8::koppText8(0x0E, true, 0x07), "8-PIO0 rd B-Daten 07");
    EXPECT_EQ(dbgp8::koppText16(0xFF91, false, 0x41), "16-PIO0 wr A-Daten 41");
    EXPECT_EQ(dbgp8::koppText16(0xFF9F, true, 0x00), "16-PIO1 rd B-Steuer 00");
    EXPECT_EQ(dbgp8::koppText16(0xFFA1, false, 0), "");     // PIO2 ist die WDC-Seite
    EXPECT_EQ(dbgp8::koppText16(0xFF81, false, 0), "");     // SIO0
}

TEST(DbgP8000Protokoll, RingBehaeltDieLetztenEintraege) {
    dbgp8::Protokoll p(3);
    for (uint64_t i = 1; i <= 5; ++i) p.add(i * 100, "e" + std::to_string(i));
    EXPECT_EQ(p.gesamt(), 5u);
    const auto z = p.zeilen(10);
    ASSERT_EQ(z.size(), size_t(3));
    EXPECT_TRUE(enthaelt(z[0], "t8=300"));
    EXPECT_TRUE(enthaelt(z[2], "e5"));
    EXPECT_EQ(p.zeilen(1).size(), size_t(1));
}

TEST(DbgP8000Protokoll, GleicheEintraegeWerdenGefaltetUndLesenLaesstSichAusblenden) {
    dbgp8::Protokoll p;
    for (int i = 0; i < 5; ++i) p.add(100 + i, "16-PIO0 rd B-Daten 23");
    p.add(200, "16-PIO0 wr A-Daten 26");
    p.add(210, "16-PIO0 rd B-Daten 65");
    EXPECT_EQ(p.gesamt(), 7u);
    ASSERT_EQ(p.eintraege().size(), size_t(3));
    const auto alle = p.zeilen(10);
    EXPECT_TRUE(enthaelt(alle[0], "t8=100        16-PIO0 rd B-Daten 23  x5"));
    const auto w = p.zeilen(10, true);
    ASSERT_EQ(w.size(), size_t(1));
    EXPECT_TRUE(enthaelt(w[0], "wr A-Daten 26"));
}

// ═══ P12b: Z8000-Seite / MMU ═════════════════════════════════════════════════════════════════

TEST(DbgP8000Mmu, TexteVonVtrAttributUndModus) {
    EXPECT_EQ(dbgp8::vtrText(0), "-");
    EXPECT_EQ(dbgp8::vtrText(Z8010::VTR_RDV | Z8010::VTR_PWW), "RDV|PWW");
    EXPECT_EQ(dbgp8::attrText(Z8010::ATTR_RD | Z8010::ATTR_SYS), "RD SYS");
    EXPECT_EQ(dbgp8::attrText(0), "-");
    EXPECT_EQ(dbgp8::mrText(0xD0), "MSEN TRNS MST ID=0");
}

TEST(DbgP8000Mmu, RegisterUndDeskriptorenDerDreiMmus) {
    L l;
    mmuEin(l);
    ladeSdr(l, DATA_MMU, 5, 0x1234, 0x0F, Z8010::ATTR_RD);
    const auto reg = dbgp8::mmuRegisterZeilen(l.mmu(L::Mmu::Data), "Data");
    EXPECT_TRUE(enthaelt(reg, "UB8010 Data  MR=D1 [MSEN TRNS MST ID=1]"));
    EXPECT_TRUE(enthaelt(reg, "VTR=00 [-]"));
    const auto sdr = dbgp8::mmuDeskriptorZeilen(l.mmu(L::Mmu::Data), 0, 63, false);
    ASSERT_EQ(sdr.size(), size_t(1));
    EXPECT_TRUE(enthaelt(sdr[0], "SDR 05  Basis=123400 Limit=0F (4096 Byte)  Attr=01 [RD]"));
    // Code-MMU: nichts belegt
    EXPECT_TRUE(enthaelt(dbgp8::mmuDeskriptorZeilen(l.mmu(L::Mmu::Code), 0, 63, false), "keine belegten"));
    // ausdrücklicher Bereich zeigt auch leere Deskriptoren
    EXPECT_EQ(dbgp8::mmuDeskriptorZeilen(l.mmu(L::Mmu::Code), 2, 4, true).size(), size_t(3));
    const auto lg = dbgp8::mmuLogikZeilen(l);
    EXPECT_TRUE(enthaelt(lg, "SCR=03"));
    EXPECT_TRUE(enthaelt(lg, "MMU EIN"));
}

TEST(DbgP8000Xlat, SystemZyklusGehtAnDieCodeMmu) {
    L l;
    mmuEin(l);
    ladeSdr(l, CODE_MMU, 5, 0x1234, 0xFF, 0x00);
    const auto x = dbgp8::xlat(l, dbgp8::zyklusFuer(5, 0x0010, dbgp8::Art::Data, true));
    EXPECT_TRUE(x.hauptspeicher);
    EXPECT_FALSE(x.onBoard);
    EXPECT_EQ(x.phys, 0x123410u);
    EXPECT_EQ(x.probe.zugriff.wahl, L::Wahl::Code);
    EXPECT_TRUE(enthaelt(x.zeilen, "phys=123410"));
    EXPECT_TRUE(enthaelt(x.zeilen, "gewaehlte MMU: CODE"));
    EXPECT_TRUE(enthaelt(x.zeilen, "keine Verletzung"));
}

TEST(DbgP8000Xlat, VerletzungWirdGemeldetAberNichtAusgeloest) {
    L l;
    mmuEin(l);
    ladeSdr(l, CODE_MMU, 6, 0x2000, 0xFF, Z8010::ATTR_RD);   // schreibgeschützt
    const auto vorher = l.sicht();
    const auto attrVorher = l.mmu(L::Mmu::Code).deskriptor(6).attr;
    const auto lesen = dbgp8::xlat(l, dbgp8::zyklusFuer(6, 0x0000, dbgp8::Art::Data, true, true));
    const auto schreiben = dbgp8::xlat(l, dbgp8::zyklusFuer(6, 0x0000, dbgp8::Art::Data, true, false));
    EXPECT_EQ(lesen.probe.mmu[0].verletzung, 0);
    EXPECT_NE(schreiben.probe.mmu[0].verletzung & Z8010::VTR_RDV, 0);
    EXPECT_TRUE(enthaelt(schreiben.zeilen, "Verletzung RDV"));
    // OHNE Nebenwirkung: weder Verletzungsregister noch SEGT noch REF/CHG-Bits noch Zähler
    EXPECT_EQ(l.mmu(L::Mmu::Code).vtr(), 0);
    EXPECT_FALSE(l.segt());
    EXPECT_EQ(l.mmu(L::Mmu::Code).deskriptor(6).attr, attrVorher);
    EXPECT_EQ(l.sicht().segtFlanken, vorher.segtFlanken);
    EXPECT_EQ(l.sicht().zyklen[1], vorher.zyklen[1]);
}

TEST(DbgP8000Xlat, OnBoardUnterSegment0) {
    L l;                                               // SCR = 0: On-Board ein, MMU aus
    const auto x = dbgp8::xlat(l, dbgp8::zyklusFuer(0, 0x0100, dbgp8::Art::Code, true));
    EXPECT_TRUE(x.onBoard);
    EXPECT_EQ(x.probe.zugriff.ziel, L::Ziel::OnBoardEprom);
    EXPECT_TRUE(enthaelt(x.zeilen, "On-Board"));
    EXPECT_FALSE(x.hauptspeicher);
}

TEST(DbgP8000Speicher, LogischUndPhysischOhneNebenwirkung) {
    P8000Karte16 k;
    k.powerOn();
    L& l = k.mmu();
    mmuEin(l);
    ladeSdr(l, CODE_MMU, 5, 0x0A34, 0xFF, 0x00);
    ASSERT_TRUE(k.dram().poke(0x0A3410, 0xAB));
    uint8_t v = 0;
    EXPECT_TRUE(dbgp8::logByte(k, 5, 0x0010, dbgp8::Art::Data, true, v));
    EXPECT_EQ(v, 0xAB);
    EXPECT_TRUE(dbgp8::physHaupt(k, 0x0A3410, v));
    EXPECT_EQ(v, 0xAB);
    // `ml` = `mp` über die MMU
    uint8_t p = 0;
    ASSERT_TRUE(dbgp8::physHaupt(k, dbgp8::xlat(l, dbgp8::zyklusFuer(5, 0x0010, dbgp8::Art::Data, true)).phys, p));
    EXPECT_EQ(p, v);
    // Hauptspeicher ohne Karte: offener Bus, false
    EXPECT_FALSE(dbgp8::physHaupt(k, 0xF00000, v));
    EXPECT_EQ(v, 0xFF);
    // On-Board nach SCR = 0: EPROM, Byte stimmt mit dem Abzug überein
    l.ioSchreiben(L::P_SCR, 0);
    EXPECT_TRUE(dbgp8::logByte(k, 0, 0x0000, dbgp8::Art::Code, true, v));
    EXPECT_EQ(v, k.rom()[0]);
    // nichts davon hat eine Verletzung/SEGT hinterlassen
    EXPECT_EQ(l.mmu(L::Mmu::Code).vtr(), 0);
    EXPECT_FALSE(l.segt());
}

TEST(DbgP8000Segt, ProtokolltextNenntMmuZyklusUndLatches) {
    L::SegtEreignis e;
    e.zyklus = dbgp8::zyklusFuer(7, 0x1234, dbgp8::Art::Data, false, false);
    e.mmus = 0x02;
    e.trpl = 0x5A;
    e.if1l = 0xC3;
    e.wahl = L::Wahl::Data;
    e.nummer = 3;
    const std::string t = dbgp8::segtText(e);
    EXPECT_TRUE(enthaelt(t, "SEGT #3 durch MMU Data"));
    EXPECT_TRUE(enthaelt(t, "DATA N <<7>>%1234 (W)"));
    EXPECT_TRUE(enthaelt(t, "TRPL=5A IF1L=C3"));
}

TEST(DbgP8000Xlat, ArtenUndZyklusstatus) {
    dbgp8::Art a;
    EXPECT_TRUE(dbgp8::parseArt("code", a));
    EXPECT_EQ(a, dbgp8::Art::Code);
    EXPECT_TRUE(dbgp8::parseArt("s", a));
    EXPECT_EQ(a, dbgp8::Art::Stack);
    EXPECT_FALSE(dbgp8::parseArt("nix", a));
    EXPECT_EQ(dbgp8::zyklusFuer(1, 2, dbgp8::Art::Code, true).st, Z8kStatus::MemInstr);
    EXPECT_EQ(dbgp8::zyklusFuer(1, 2, dbgp8::Art::Code, true, true, true).st, Z8kStatus::MemInstrFirst);
    EXPECT_EQ(dbgp8::zyklusFuer(1, 2, dbgp8::Art::Stack, false).st, Z8kStatus::MemStack);
    EXPECT_FALSE(dbgp8::zyklusFuer(1, 2, dbgp8::Art::Data, false).system);
}
