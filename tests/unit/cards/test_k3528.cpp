/**
 * @file test_k3528.cpp
 * @brief K3528 (K8915 Gen 2): Speicherbild je A8H-Wert, /MEMDI, Zugriffswege.
 *        doc/design/24_k8915_varianten.md R3, AP-V4.
 */

#include <gtest/gtest.h>
#include <vector>

#include "core/cards/k3528/k3528.h"

namespace {

using Q = K3528::Quelle;

/// Bus-Gerät, das Schreibzyklen zählt (1000H–1FFFH, wie die K7024).
struct BusRam : MemDevice {
    int schreib = 0;
    uint8_t v[0x1000] = {};
    uint8_t memRead(uint16_t a) override { return v[a & 0xFFF]; }
    void memWrite(uint16_t a, uint8_t d) override { ++schreib; v[a & 0xFFF] = d; }
};

struct K3528Test : ::testing::Test {
    K1520Bus bus;
    K3528    sp{bus};
    uint8_t  zre[0x1000] = {};
    void SetUp() override {
        sp.attachToBus(bus);
        sp.setZreWeg([this](uint16_t a) { return zre[a & 0xFFF]; },
                     [this](uint16_t a, uint8_t d) { zre[a & 0xFFF] = d; });
        sp.powerOn(0x00);
    }
    /// Erwartung je 4-KB-Slot als Zeichenkette: R = RAM, Z = ZRE, B = Bus.
    static std::string bild(const K3528& s) {
        std::string r;
        for (int i = 0; i < 16; ++i) {
            Q q = s.ortVon(uint16_t(i << 12));
            r += (q == Q::Ram ? 'R' : q == Q::Zre ? 'Z' : 'B');
        }
        return r;
    }
};

}  // namespace

TEST_F(K3528Test, SpeicherbildJeRegisterwert) {
    // R3: A8H = 00H (Reset), 06H/0EH, 87H/8FH — je 4-KB-Slot 0000 … F000.
    struct F { uint8_t reg; const char* soll; };
    const F tab[] = {
        {0x00, "ZBBBBBBBBBBBBBBB"},
        {0x06, "ZBBBRRRRRRRRRRRR"},
        {0x0E, "ZBBBRRRRRRRRRRRR"},
        {0x87, "RRRRRRRRRRRRRRRR"},
        {0x8F, "RRRRRRRRRRRRRRRR"},
    };
    for (const auto& f : tab) {
        bus.ioWrite(0xA8, f.reg);
        EXPECT_EQ(bild(sp), f.soll) << "A8H = " << std::hex << int(f.reg);
    }
}

TEST_F(K3528Test, ResetLoeschtRegisterRamBleibt) {
    bus.ioWrite(0xA8, 0x87);
    sp.memWrite(0x2345, 0x5A);
    sp.reset();
    EXPECT_EQ(sp.reg(), 0x00);
    EXPECT_EQ(bild(sp), "ZBBBBBBBBBBBBBBB");
    EXPECT_EQ(sp.ramPeek(0x2345), 0x5A);
    bus.ioWrite(0xA8, 0x87);
    EXPECT_EQ(sp.memRead(0x2345), 0x5A);
}

TEST_F(K3528Test, RegisterHatVierPorts) {
    for (uint8_t p = 0xA8; p <= 0xAB; ++p) {
        bus.ioWrite(p, uint8_t(p));            // A8H→A8, A9H→A9 …
        EXPECT_EQ(sp.reg(), p);
    }
    // außerhalb: nicht dekodiert
    sp.setReg(0x00);
    bus.ioWrite(0xAC, 0x87);
    EXPECT_EQ(sp.reg(), 0x00);
}

TEST_F(K3528Test, RegisterIstNichtLesbar) {
    bus.ioWrite(0xA8, 0x87);
    for (uint8_t p = 0xA8; p <= 0xAB; ++p) EXPECT_EQ(bus.ioRead(p), 0xFF);
}

TEST_F(K3528Test, GewaehlterSpeicherErscheintNichtAmBus) {
    BusRam k7024;
    bus.registerMem(&k7024, 0x1000, 0x1000);
    bus.ioWrite(0xA8, 0x06);
    sp.memWrite(0x1000, 0x11);                  // Bus: K7024
    EXPECT_EQ(k7024.schreib, 1);
    bus.ioWrite(0xA8, 0x87);
    sp.memWrite(0x1000, 0x22);                  // K3528 bedient selbst
    EXPECT_EQ(k7024.schreib, 1);
    EXPECT_EQ(sp.memRead(0x1000), 0x22);
    EXPECT_EQ(k7024.v[0], 0x11);
    bus.ioWrite(0xA8, 0x06);
    EXPECT_EQ(sp.memRead(0x1000), 0x11);        // wieder die K7024
}

TEST_F(K3528Test, MemdiSperrtDieZre) {
    zre[0x0010] = 0x77;
    bus.ioWrite(0xA8, 0x00);
    EXPECT_FALSE(sp.memdi());
    EXPECT_EQ(sp.memRead(0x0010), 0x77);
    sp.memWrite(0x0C10, 0x33);
    EXPECT_EQ(zre[0x0C10], 0x33);
    bus.ioWrite(0xA8, 0x80);                    // /MEMDI, keine Seite gewählt
    EXPECT_TRUE(sp.memdi());
    EXPECT_EQ(sp.ortVon(0x0000), Q::Bus);
    sp.memWrite(0x0C10, 0x44);                  // geht an den (leeren) Bus
    EXPECT_EQ(zre[0x0C10], 0x33);
    EXPECT_EQ(sp.memRead(0x0010), 0xFF);        // Bus ohne Gerät
}

TEST_F(K3528Test, Memdi1OhneWirkungAufDasBild) {
    // /MEMDI1 = Bit 3, aktiv bei 0.
    bus.ioWrite(0xA8, 0x06);
    EXPECT_TRUE(sp.memdi1());
    const std::string a = bild(sp);
    bus.ioWrite(0xA8, 0x0E);
    EXPECT_FALSE(sp.memdi1());
    EXPECT_EQ(bild(sp), a);
}

TEST_F(K3528Test, Bits4Bis6Wirkungslos) {
    for (uint8_t basis : {0x00, 0x06, 0x87}) {
        bus.ioWrite(0xA8, basis);
        const std::string a = bild(sp);
        for (uint8_t m : {0x10, 0x20, 0x40, 0x70}) {
            bus.ioWrite(0xA8, uint8_t(basis | m));
            EXPECT_EQ(bild(sp), a) << std::hex << int(basis | m);
        }
    }
}

TEST_F(K3528Test, TraceSiehtRamUndZreAberNichtDenBus) {
    int n = 0;
    sp.setMemTrace([&](bool, bool, uint16_t, uint8_t) { ++n; });
    bus.ioWrite(0xA8, 0x06);
    sp.memRead(0x0000);      // ZRE
    sp.memRead(0x5000);      // RAM
    sp.memRead(0x2000);      // Bus
    EXPECT_EQ(n, 2);
}

TEST(K3528Config, MemdiAnBit0ErgibtDasselbeBildFuerAlleBekanntenWerte) {
    K1520Bus b1, b2;
    K3528::Config c2; c2.belegung = K3528::Belegung::memdiAnBit0();
    K3528 a(b1), b(b2, c2);
    for (uint8_t v : {0x00, 0x06, 0x0E, 0x87, 0x8F}) {
        a.setReg(v); b.setReg(v);
        for (int s = 0; s < 16; ++s)
            EXPECT_EQ(a.ortVon(uint16_t(s << 12)), b.ortVon(uint16_t(s << 12)))
                << "A8H " << std::hex << int(v) << " Slot " << s;
        EXPECT_EQ(a.memdi(), b.memdi()) << std::hex << int(v);
    }
}

TEST(K3528Config, BelegungIstKonfiguration) {
    // Abweichende Brücken ändern das Bild: Seite 1 an Bit 5, /MEMDI aktiv bei 0 an Bit 4.
    K1520Bus bus;
    K3528::Config c;
    c.belegung.seite1 = 5;
    c.belegung.memdi_bit = 4;
    c.belegung.memdi_aktiv_h = false;
    K3528 sp(bus, c);
    sp.setReg(0x10);     // Bit 4 = 1 → /MEMDI inaktiv, nichts gewählt
    EXPECT_EQ(sp.ortVon(0x0000), K3528::Quelle::Zre);
    EXPECT_EQ(sp.ortVon(0x4000), K3528::Quelle::Bus);
    sp.setReg(0x20);     // Bit 5: Seite 1; Bit 4 = 0 → /MEMDI aktiv
    EXPECT_EQ(sp.ortVon(0x4000), K3528::Quelle::Ram);
    EXPECT_EQ(sp.ortVon(0x0000), K3528::Quelle::Bus);
    // nicht verdrahtet (0xFF): nie gewählt
    c.belegung.seite0 = 0xFF;
    K3528 sp2(bus, c);
    sp2.setReg(0xFF);
    EXPECT_NE(sp2.ortVon(0x0000), K3528::Quelle::Ram);
}
