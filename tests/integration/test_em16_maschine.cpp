/**
 * @file test_em16_maschine.cpp
 * @brief A5120.16 in der Maschine (S4): U880 und U8001 auf gemeinsamer Maschinenzeit,
 *        ohne CP/A — Z80- und U8001-Code direkt in den Speicher.
 *
 * Der U880 lädt die U8001-Firmware über ein EM-Fenster (Seite 4 → Zelle 0), gibt
 * RESET16 frei, fordert mit TRQ8 den 8-Bit-Mode an, wartet auf PIO A6 (8/16) und liest
 * das Ergebnis zurück.  Dazu Save-State v6 mitten im 16-Bit-Lauf.  Der Ende-zu-Ende-
 * Lauf unter CP/A steht in test_em16_abl.cpp.
 */
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/machines/a5120/a5120.h"
#include "tests/support/temp_path.h"
#include "tools/z8000/z8k_asm.h"

namespace {

// U880-Programm bei 8000H (von Hand assembliert, Kommentar = Quelle).
const std::vector<uint8_t> kZ80 = {
    0xF3,                               // DI
    0x31, 0x00, 0x7F,                   // LD SP,7F00H
    0x3E, 0xCF, 0xD3, 0xAA,             // PIO A: Bitbetrieb
    0x3E, 0xFF, 0xD3, 0xAA,             //        alles Eingang
    0x3E, 0xCF, 0xD3, 0xAB,             // PIO B: Bitbetrieb
    0x3E, 0x80, 0xD3, 0xAB,             //        B7 Eingang
    0x3E, 0x38, 0xD3, 0xA9,             // RESET16=1, /TRQ8=1, /STOP=1, /RAMEN=0
    0x01, 0xAF, 0x40, 0x3E, 0x0C, 0xED, 0x79,   // A22[4] <- 0CH (PEN, WE, Block 0)
    0x21, 0x00, 0x90, 0x11, 0x00, 0x40, 0x01, 0x00, 0x02, 0xED, 0xB0,  // LDIR 9000H→4000H, 512
    0x3E, 0x28, 0xD3, 0xA9,             // RESET16=0 ⇒ 16-Bit-Mode, U8001 läuft
    0x06, 0x00, 0x10, 0xFE,             // kurz warten (DJNZ $)
    0x3E, 0x08, 0xD3, 0xA9,             // /TRQ8=0: 8-Bit-Mode anfordern
    0xDB, 0xA8, 0xCB, 0x77, 0x28, 0xFA, // W: IN A,(A8H); BIT 6,A; JR Z,W (8/16)
    0x3A, 0x80, 0x40, 0x32, 0x00, 0x70, // LD A,(4080H); LD (7000H),A
    0x3A, 0x81, 0x40, 0x32, 0x01, 0x70, // LD A,(4081H); LD (7001H),A
    0x76,                               // HALT
};

const char* kFw =
    "  SEG\n"
    "  ORG <<0>>%0000\n  DW 0,%C000,%0000,%0100\n"
    "  ORG <<0>>%0100\n"
    "  LDL RR14,#<<0>>%0FF0\n"
    "  LD R1,#0\n  LD R2,#1000\n"
    "S: ADD R1,R2\n  DJNZ R2,S\n"          // Σ 1..1000 = 500500 = %7A314 → %A314
    "  LD <<0>>%0080,R1\n"
    "W: MBIT\n  JR MI,W\n"                   // auf µI (= /TRQ8) warten
    "  MSET\n"                               // µ0 → TREN → BUSRQ → BUSAK
    "  MRES\n  HALT\n";

A5120Machine::Config em256() {
    A5120Machine::Config c;
    c.em = A5120Machine::Config::Em::em256;
    return c;
}

void lade(A5120Machine& m) {
    m.powerOn();
    for (size_t i = 0; i < kZ80.size(); ++i) m.memWriteDebug(uint16_t(0x8000 + i), kZ80[i]);
    z8k::AsmOptions o; o.seg = true;
    auto a = z8k::assemble(kFw, o);
    for (auto& e : a.errors) ADD_FAILURE() << "asm " << e.line << ": " << e.text;
    for (auto& kv : a.image) m.memWriteDebug(uint16_t(0x9000 + (kv.first & 0xFFFF)), kv.second);
    m.cpuDebug().PC = 0x8000;
}

/// Bis der U880 auf HALT steht.
bool bisHalt(A5120Machine& m, long long max) {
    for (long long n = 0; n < max; n += 1000) {
        m.run(1000);
        if (m.cpuDebug().halted) return true;
    }
    return false;
}

}  // namespace

/**
 * @test Em16Maschine/U880StartetU8001UndHoltErgebnisZurueck
 * @brief Der ganze Ablauf in der Maschine: Laden im 8-Bit-Mode, RESET16 = 0, TRQ8 →
 *   MSET → BUSAK → A29 beim M1, Ergebnis über das Fenster.  Die U8001-Zeit folgt der
 *   Maschinenzeit im Verhältnis 4 : 2,45.
 */
TEST(Em16Maschine, U880StartetU8001UndHoltErgebnisZurueck) {
    A5120Machine m(em256());
    lade(m);
    const uint64_t t0 = m.machineCycles();
    ASSERT_TRUE(bisHalt(m, 2'000'000));
    EXPECT_EQ(m.memReadDebug(0x7000), 0xA3);
    EXPECT_EQ(m.memReadDebug(0x7001), 0x14);
    const EM& em = *m.em();
    EXPECT_TRUE(em.mode8());
    EXPECT_TRUE(em.busAck16());
    EXPECT_TRUE(em.tren());
    const double soll = double(m.machineCycles() - t0) * 4.0 / 2.45;
    EXPECT_NEAR(double(em.u8001().cycles), soll, soll * 0.01);
    EXPECT_GT(em.u8001().cycles, 15000u);    // die Summenschleife lief wirklich
}

/**
 * @test Em16Maschine/SaveStateV6MittenIm16BitMode
 * @brief saveState mitten im 16-Bit-Lauf, loadState in eine frische Maschine: gleicher
 *   Fortgang bis aufs Taktende.  Ein EM-Stand lädt auch in eine Maschine ohne EM
 *   (der EM-Block wird übergangen).
 */
TEST(Em16Maschine, SaveStateV6MittenIm16BitMode) {
    A5120Machine a(em256());
    lade(a);
    for (int i = 0; i < 2000 && (a.em()->mode8() || a.em()->u8001().inReset()); ++i) a.run(200);
    a.run(3000);
    ASSERT_FALSE(a.em()->mode8());
    ASSERT_FALSE(a.em()->u8001().inReset());
    const std::string pfad = k1520test::tempPath("k1520_em16_state.bin");
    ASSERT_TRUE(a.saveState(pfad));

    A5120Machine b(em256());
    b.powerOn();
    ASSERT_TRUE(b.loadState(pfad));
    EXPECT_EQ(b.em()->u8001().pc, a.em()->u8001().pc);
    ASSERT_TRUE(bisHalt(a, 2'000'000));
    ASSERT_TRUE(bisHalt(b, 2'000'000));
    EXPECT_EQ(b.memReadDebug(0x7000), 0xA3);
    EXPECT_EQ(b.memReadDebug(0x7001), 0x14);
    EXPECT_EQ(a.machineCycles(), b.machineCycles());
    EXPECT_EQ(a.em()->u8001().cycles, b.em()->u8001().cycles);

    A5120Machine ohne;
    ohne.powerOn();
    EXPECT_TRUE(ohne.loadState(pfad));
    std::remove(pfad.c_str());
}
