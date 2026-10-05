/**
 * @file test_k3820.cpp
 * @brief PFS K3820 (16 × 2708): Chip-Raster, Startadresse X8/X9, leere Sockel, Sperre über
 *        /MEMDI bzw. /MEMDI1/2, Beispielabzüge.  doc/design/24_k8915_varianten.md AP-V11,
 *        doc/k8915g2/karten.md §3.  Die Karte steckt in keiner Maschine.
 */

#include <gtest/gtest.h>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/cards/k3820/k3820.h"

namespace {

using L = K3820::MemdiLeitung;

/// Sockel @p i mit einem Kennmuster belegen: Byte = Index, letztes Byte = ~Index.
void kennmuster(K3820& k, int i)
{
    std::vector<uint8_t> d(K3820::kChipSize, uint8_t(i));
    d.back() = uint8_t(~i);
    k.setChip(i, d.data(), d.size());
}

K3820::Config cfgAb(uint16_t start, L memdi = L::Memdi)
{
    K3820::Config c;
    c.startadresse = start;
    c.memdi = memdi;
    return c;
}

std::vector<uint8_t> lies(const std::string& name)
{
    std::ifstream f(std::string(K3820_ABZUG_DIR) + "/" + name, std::ios::binary);
    if (!f) throw std::runtime_error("Abzug fehlt: " + name);
    return {std::istreambuf_iterator<char>(f), {}};
}

/// Lesbares Bus-RAM unter der Karte (zeigt, wer antwortet, wenn die Karte schweigt).
struct BusRam : MemDevice {
    uint8_t wert = 0x55;
    int schreib = 0;
    uint8_t memRead(uint16_t) override { return wert; }
    void memWrite(uint16_t, uint8_t) override { ++schreib; }
};

/// Vorrangspeicher, der für ein Fenster die Bus-Leitung /MEMDI zieht (Muster EM256).
struct Vorrang : MemdiDriver {
    uint16_t von, bis;
    Vorrang(uint16_t v, uint16_t b) : von(v), bis(b) {}
    bool drivesMemdi(uint16_t a) override { return a >= von && a <= bis; }
    uint8_t memRead(uint16_t) override { return 0xA5; }
    void memWrite(uint16_t, uint8_t) override {}
};

}  // namespace

TEST(K3820, ChipRasterNachAbb7)
{
    // Abb. 7: Sockel = relative Adresse >> 10; Reihe 0 = 0000/0400/0800/0C00 … Reihe 3 = 3000…3C00.
    K1520Bus bus;
    K3820 k(cfgAb(0x0000));
    for (int i = 0; i < K3820::kChips; ++i) kennmuster(k, i);
    k.attachToBus(bus);
    for (int i = 0; i < K3820::kChips; ++i) {
        const uint16_t a = uint16_t(i * 0x400);
        EXPECT_EQ(bus.memRead(a), uint8_t(i)) << std::hex << a;
        EXPECT_EQ(bus.memRead(uint16_t(a + 0x3FF)), uint8_t(~i)) << std::hex << a;
    }
    EXPECT_EQ(bus.memRead(0x4000), 0xFF);   // ausserhalb: kein Gerät
}

TEST(K3820, StartadresseIn4KbSchritten)
{
    for (uint16_t start = 0; start <= 0xC000; start += 0x1000) {
        K1520Bus bus;
        K3820 k(cfgAb(start));
        kennmuster(k, 0);
        kennmuster(k, 15);
        k.attachToBus(bus);
        EXPECT_EQ(bus.memRead(start), 0x00) << std::hex << start;
        EXPECT_EQ(bus.memRead(uint16_t(start + 0x3C00)), 0x0F) << std::hex << start;
        EXPECT_EQ(bus.memRead(uint16_t(start + 0x3FFF)), uint8_t(~15)) << std::hex << start;
        EXPECT_TRUE(k.belegt(start));
        EXPECT_FALSE(k.belegt(uint16_t(start + 0x4000)));
        if (start) EXPECT_FALSE(k.belegt(uint16_t(start - 1)));
    }
    EXPECT_THROW(K3820(cfgAb(0x0800)), std::invalid_argument);
}

TEST(K3820, BrueckenX8X9)
{
    // :1/:2/:3/:4 = 4/8/16/32 KB, alle vier = F000H (Tabelle gedr. S.23).
    EXPECT_EQ(K3820::Config::ausBruecken(0x0).startadresse, 0x0000);
    EXPECT_EQ(K3820::Config::ausBruecken(0x1).startadresse, 0x1000);
    EXPECT_EQ(K3820::Config::ausBruecken(0x2).startadresse, 0x2000);
    EXPECT_EQ(K3820::Config::ausBruecken(0x4).startadresse, 0x4000);
    EXPECT_EQ(K3820::Config::ausBruecken(0x8).startadresse, 0x8000);
    EXPECT_EQ(K3820::Config::ausBruecken(0xC).startadresse, 0xC000);
    EXPECT_EQ(K3820::Config::ausBruecken(0xF).startadresse, 0xF000);
    // Vorgaben: /MEMDI gebrückt (Normalkonfiguration), X10–X11 offen.
    EXPECT_EQ(K3820::Config{}.memdi, L::Memdi);
    EXPECT_TRUE(K3820::Config{}.wait_m1);
}

TEST(K3820, UeberlaufHinterFFFF)
{
    // 4-Bit-Subtrahierer: Start E000H ⇒ E000–FFFF und 0000–1FFF [?].
    K1520Bus bus;
    K3820 k(cfgAb(0xE000));
    for (int i = 0; i < K3820::kChips; ++i) kennmuster(k, i);
    k.attachToBus(bus);
    EXPECT_EQ(bus.memRead(0xE000), 0x00);
    EXPECT_EQ(bus.memRead(0xFC00), 0x07);
    EXPECT_EQ(bus.memRead(0x0000), 0x08);
    EXPECT_EQ(bus.memRead(0x1C00), 0x0F);
    EXPECT_TRUE(k.belegt(0x1FFF));
    EXPECT_FALSE(k.belegt(0x2000));
    EXPECT_FALSE(k.belegt(0xDFFF));
}

TEST(K3820, LeereChipsLesenFF)
{
    K1520Bus bus;
    K3820 k(cfgAb(0x8000));
    k.attachToBus(bus);
    for (int i = 0; i < K3820::kChips; ++i) EXPECT_FALSE(k.chipBelegt(i));
    for (uint32_t a = 0x8000; a < 0xC000; a += 0x101) EXPECT_EQ(bus.memRead(uint16_t(a)), 0xFF);

    // Kurzer Inhalt: der Rest des Sockels bleibt FFH; geleert liest er wieder FFH.
    const uint8_t kurz[3] = {0xC3, 0x00, 0x80};
    k.setChip(5, kurz, 3);
    EXPECT_TRUE(k.chipBelegt(5));
    EXPECT_EQ(bus.memRead(0x9400), 0xC3);
    EXPECT_EQ(bus.memRead(0x9403), 0xFF);
    k.leereChip(5);
    EXPECT_FALSE(k.chipBelegt(5));
    EXPECT_EQ(bus.memRead(0x9400), 0xFF);
    EXPECT_THROW(k.setChip(16, kurz, 3), std::out_of_range);
}

TEST(K3820, SchreibzugriffeWirkungslos)
{
    K1520Bus bus;
    BusRam ram;                       // beschreibbares RAM unter der Karte
    bus.registerMem(&ram, 0x4000, 0x4000);
    K3820 k(cfgAb(0x4000));
    kennmuster(k, 0);
    k.attachToBus(bus);
    EXPECT_FALSE(k.isWritable());
    bus.memWrite(0x4000, 0x12);
    EXPECT_EQ(bus.memRead(0x4000), 0x00);   // Karte unverändert
    EXPECT_EQ(k.peek(0x0000), 0x00);
    EXPECT_EQ(ram.schreib, 1);              // der Schreibzyklus geht nur ans RAM
}

TEST(K3820, BusMemdiSperrt)
{
    // Bus-/MEMDI je Zugriff: ein Vorrangspeicher übernimmt, die Karte bleibt still.
    K1520Bus bus;
    K3820 k(cfgAb(0xC000));
    kennmuster(k, 0);
    k.attachToBus(bus);
    Vorrang v(0xC000, 0xC0FF);
    bus.addMemdiDriver(&v);
    EXPECT_EQ(bus.memRead(0xC000), 0xA5);
    EXPECT_EQ(bus.memRead(0xC100), 0x00);   // ausserhalb des Vorrangfensters: Karte
    EXPECT_FALSE(k.gesperrt());              // keine statische Sperre
}

TEST(K3820, Memdi1Memdi2NachBruecke)
{
    struct Fall { L leitung; bool sperrt1, sperrt2; };
    for (Fall f : {Fall{L::Memdi, false, false}, Fall{L::Memdi1, true, false},
                   Fall{L::Memdi2, false, true}, Fall{L::Keine, false, false}}) {
        K1520Bus bus;
        BusRam ram;
        bus.registerMem(&ram, 0x0000, 0x4000);
        K3820 k(cfgAb(0x0000, f.leitung));
        kennmuster(k, 0);
        k.attachToBus(bus);
        const int fall = int(f.leitung);
        EXPECT_EQ(bus.memRead(0x0000), 0x00) << fall;

        k.setMemdi1(true);
        EXPECT_EQ(k.gesperrt(), f.sperrt1) << fall;
        EXPECT_EQ(bus.memRead(0x0000), f.sperrt1 ? 0x55 : 0x00) << fall;   // hochohmig ⇒ RAM
        k.setMemdi1(false);
        EXPECT_EQ(bus.memRead(0x0000), 0x00) << fall;

        k.setMemdi2(true);
        EXPECT_EQ(k.gesperrt(), f.sperrt2) << fall;
        EXPECT_EQ(bus.memRead(0x0000), f.sperrt2 ? 0x55 : 0x00) << fall;
        k.setMemdi2(false);
        EXPECT_FALSE(k.gesperrt()) << fall;
        EXPECT_EQ(bus.memRead(0x0000), 0x00) << fall;
    }
}

TEST(K3820, WaitBruecke)
{
    K3820::Config c;
    EXPECT_TRUE(K3820(c).wartetM1());    // X10–X11 offen
    c.wait_m1 = false;                   // geschlossen: keine WAIT-Bildung
    EXPECT_FALSE(K3820(c).wartetM1());
}

TEST(K3820, BeispielabzuegeAnRichtigerAdresse)
{
    // Abzüge des Anwenders: nur „3000“ und „3C00“ programmiert.  Bei Start C000H
    // (Hypothese karten.md §3) liegen sie an F000H bzw. FC00H.
    const auto c3000 = lies("k8915g2_pfs3820_3000.bin");
    const auto c3c00 = lies("k8915g2_pfs3820_3C00.bin");
    ASSERT_EQ(c3000.size(), 0x400u);
    ASSERT_EQ(c3c00.size(), 0x400u);

    K1520Bus bus;
    K3820 k(K3820::Config::ausBruecken(0xC));
    k.setChip(0x3000 >> 10, c3000.data(), c3000.size());
    k.setChip(0x3C00 >> 10, c3c00.data(), c3c00.size());
    k.attachToBus(bus);
    for (uint16_t i = 0; i < 0x400; ++i) {
        ASSERT_EQ(bus.memRead(uint16_t(0xF000 + i)), c3000[i]) << std::hex << i;
        ASSERT_EQ(bus.memRead(uint16_t(0xFC00 + i)), c3c00[i]) << std::hex << i;
    }
    EXPECT_EQ(bus.memRead(0xC000), 0xFF);
    EXPECT_EQ(bus.memRead(0xF400), 0xFF);
}

TEST(K3820, GesamtabzugGleichEinzelchips)
{
    const auto alles = lies("k8915g2_pfs3820_0000-3FFF.bin");
    ASSERT_EQ(alles.size(), 0x4000u);
    K3820 k;
    k.ladeAbbild(alles.data(), alles.size());
    static const char* namen[16] = {"0000", "0400", "0800", "0C00", "1000", "1400", "1800", "1C00",
                                    "2000", "2400", "2800", "2C00", "3000", "3400", "3800", "3C00"};
    int leer = 0;
    for (int i = 0; i < K3820::kChips; ++i) {
        const auto einzel = lies(std::string("k8915g2_pfs3820_") + namen[i] + ".bin");
        ASSERT_EQ(einzel.size(), 0x400u);
        bool alleFF = true;
        for (uint16_t j = 0; j < 0x400; ++j) {
            ASSERT_EQ(k.peek(uint16_t(i * 0x400 + j)), einzel[j]) << namen[i];
            alleFF &= einzel[j] == 0xFF;
        }
        leer += alleFF;
    }
    EXPECT_EQ(leer, 14);   // 14 von 16 Chips gelöscht (Entwurf 24 §10)
}
