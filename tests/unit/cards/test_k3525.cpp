/**
 * @file test_k3525.cpp
 * @brief Unit-Tests der K3525 (16-KB-dRAM-Karte der frühen A5120, AP A2).
 *
 * Geprüft: Lesen/Schreiben, Brückenfeld Startadresse (4-KB-Raster), Ablehnung
 * unzulässiger Basen, vier Karten = 64 KB am Bus, Power-on 0xFF, /MEMDI1/2
 * (setMemDI) und Bus-/MEMDI je Zugriff, Überdeckung durch ein Lese-ROM,
 * load()/fill()/restore().
 */

#include <gtest/gtest.h>
#include <stdexcept>
#include "core/cards/k3525/k3525.h"

namespace {
K3525 karte(uint16_t base, bool memdi_source = false) {
    K3525::Config c;
    c.base_addr = base;
    c.memdi_source = memdi_source;
    return K3525(c);
}

/// Vorrangspeicher, der die Seite 4000H–40FFH übernimmt (wie das EM256).
class Vorrang4000 : public MemdiDriver {
public:
    uint8_t cell = 0x5A;
    bool drivesMemdi(uint16_t a) override { return (a & 0xFF00) == 0x4000; }
    uint8_t memRead(uint16_t) override { return cell; }
    void memWrite(uint16_t, uint8_t d) override { cell = d; }
};

/// Nur lesbares ROM über 0000–03FF.
class Rom : public MemDevice {
public:
    uint8_t memRead(uint16_t) override { return 0xC3; }
    void memWrite(uint16_t, uint8_t) override {}
    bool isWritable() const override { return false; }
};
}  // namespace

TEST(K3525, PowerOnLiest0xFF) {
    K3525 k;
    EXPECT_EQ(k.memRead(0x0000), 0xFF);
    EXPECT_EQ(k.memRead(0x3FFF), 0xFF);
}

TEST(K3525, LesenSchreibenAnBasisUndEnde) {
    auto k = karte(0x8000);
    k.memWrite(0x8000, 0x12);
    k.memWrite(0xBFFF, 0x34);
    EXPECT_EQ(k.memRead(0x8000), 0x12);
    EXPECT_EQ(k.memRead(0xBFFF), 0x34);
    EXPECT_EQ(k.rawPtr()[0], 0x12);
    EXPECT_EQ(k.rawPtr()[0x3FFF], 0x34);
}

TEST(K3525, AdressenAusserhalbWerdenIgnoriert) {
    auto k = karte(0x4000);
    k.memWrite(0x3FFF, 0x11);
    k.memWrite(0x8000, 0x22);
    EXPECT_EQ(k.memRead(0x3FFF), 0xFF);
    EXPECT_EQ(k.memRead(0x8000), 0xFF);
    EXPECT_EQ(k.rawPtr()[0], 0xFF);
    EXPECT_EQ(k.rawPtr()[0x3FFF], 0xFF);
}

TEST(K3525, BasisImVierKBRasterBeliebig) {
    // X8/X9: 4-KB-Raster, 16 KB zusammenhängend (karten.md §5)
    for (uint16_t b : {0x1000, 0x2000, 0x3000, 0x5000, 0xC000}) {
        auto k = karte(b);
        k.memWrite(b, 0x77);
        EXPECT_EQ(k.memRead(b), 0x77) << std::hex << b;
        EXPECT_EQ(k.memRead(static_cast<uint16_t>(b - 1)), 0xFF);
    }
}

TEST(K3525, UnzulaessigeBasenWerdenAbgelehnt) {
    EXPECT_THROW(karte(0x0800), std::invalid_argument);   // nicht im 4-KB-Raster
    EXPECT_THROW(karte(0xD000), std::invalid_argument);   // 16 KB passen nicht mehr in 64 KB
    EXPECT_THROW(karte(0xF000), std::invalid_argument);
}

TEST(K3525, VierKartenErgeben64KBAmBus) {
    K1520Bus bus;
    K3525 k0 = karte(0x0000), k1 = karte(0x4000), k2 = karte(0x8000), k3 = karte(0xC000);
    k0.attachToBus(bus); k1.attachToBus(bus); k2.attachToBus(bus); k3.attachToBus(bus);
    for (uint32_t a = 0; a < 0x10000; a += 0x0FFF) {
        bus.memWrite(static_cast<uint16_t>(a), static_cast<uint8_t>(a >> 4));
    }
    for (uint32_t a = 0; a < 0x10000; a += 0x0FFF) {
        EXPECT_EQ(bus.memRead(static_cast<uint16_t>(a)), static_cast<uint8_t>(a >> 4)) << std::hex << a;
    }
    bus.memWrite(0x4000, 0xA1);
    bus.memWrite(0x3FFF, 0xA2);
    EXPECT_EQ(k0.rawPtr()[0x3FFF], 0xA2);   // Kartengrenze: jede Seite für sich
    EXPECT_EQ(k1.rawPtr()[0], 0xA1);
}

// Der Bus lehnt Überlappung nicht ab (Doku in k1520_bus.h behauptet es): Lesen = letzte
// Karte gewinnt, Schreiben geht an beide — wie doppelt belegte Adressen am echten Bus.
TEST(K3525, UeberlappendeKarten_LesenLetzteGewinntSchreibenAnBeide) {
    K1520Bus bus;
    K3525 a = karte(0x0000), b = karte(0x2000);
    a.attachToBus(bus);
    b.attachToBus(bus);
    bus.memWrite(0x2000, 0x66);
    EXPECT_EQ(a.rawPtr()[0x2000], 0x66);
    EXPECT_EQ(b.rawPtr()[0], 0x66);
    b.fill(0x11);
    EXPECT_EQ(bus.memRead(0x2000), 0x11);
}

TEST(K3525, SetMemDI_LiestFFUndVerwirftSchreiben) {
    auto k = karte(0x0000);
    k.memWrite(0x0010, 0x42);
    k.setMemDI(true);
    EXPECT_EQ(k.memRead(0x0010), 0xFF);
    k.memWrite(0x0010, 0x99);
    k.setMemDI(false);
    EXPECT_EQ(k.memRead(0x0010), 0x42);   // Inhalt blieb, Schreiben fiel weg
}

TEST(K3525, BusMemdiJeZugriff_NurBusGebruecktesSchweigt) {
    K1520Bus bus;
    K3525 k = karte(0x4000);
    k.attachToBus(bus);
    bus.memWrite(0x4000, 0x22);
    Vorrang4000 em;
    bus.addMemdiDriver(&em);
    EXPECT_EQ(bus.memRead(0x4000), 0x5A);       // Vorrangspeicher liefert
    bus.memWrite(0x4000, 0xEE);
    EXPECT_EQ(em.cell, 0xEE);
    EXPECT_EQ(k.rawPtr()[0], 0x22);             // Karte schrieb nicht mit
    bus.memWrite(0x4100, 0x33);                 // andere Seite: normal
    EXPECT_EQ(bus.memRead(0x4100), 0x33);
}

TEST(K3525, AufMemdi12GebruecktHoertNichtAufBusMemdi) {
    K1520Bus bus;
    K3525 k = karte(0x4000, /*memdi_source=*/true);
    k.attachToBus(bus);
    Vorrang4000 em;
    bus.addMemdiDriver(&em);
    bus.memWrite(0x4000, 0x77);
    EXPECT_EQ(k.rawPtr()[0], 0x77);
    k.setMemDI(true);
    bus.memWrite(0x4000, 0x78);
    EXPECT_EQ(k.rawPtr()[0], 0x77);
}

TEST(K3525, RomUeberdecktLesenSchreibenKommtDurch) {
    K1520Bus bus;
    K3525 k = karte(0x0000);
    k.attachToBus(bus);
    Rom rom;
    bus.registerMem(&rom, 0x0000, 0x0400);
    bus.memWrite(0x0000, 0x55);
    EXPECT_EQ(bus.memRead(0x0000), 0xC3);       // ROM sichtbar
    EXPECT_EQ(k.rawPtr()[0], 0x55);             // RAM darunter beschrieben
}

TEST(K3525, FillLoadRestore) {
    auto k = karte(0xC000);
    k.fill(0x00);
    EXPECT_EQ(k.memRead(0xC123), 0x00);
    const uint8_t blk[3] = {1, 2, 3};
    k.load(0x3FFE, blk, 3);                     // über das Ende hinaus: abgeschnitten
    EXPECT_EQ(k.memRead(0xFFFE), 1);
    EXPECT_EQ(k.memRead(0xFFFF), 2);
    k.setMemDI(true);
    k.load(0x0000, blk, 3);                     // umgeht /MEMDI
    EXPECT_EQ(k.rawPtr()[2], 3);
    std::array<uint8_t, K3525::kGroesse> img;
    img.fill(0x9C);
    k.restore(img.data());
    EXPECT_EQ(k.rawPtr()[0x1234], 0x9C);
}

TEST(K3525, WaitBrueckeWirdNurGefuehrt) {
    K3525::Config c;
    c.wait_im_m1 = true;
    K3525 k(c);
    EXPECT_TRUE(k.config().wait_im_m1);
    EXPECT_FALSE(K3525().config().wait_im_m1);
}
