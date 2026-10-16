/**
 * @file test_ctc_sprung.cpp
 * @brief Waechter fuer den Sprungpfad von Z80CTC::clockTick(int): er muss Einzeltakten
 *        gleichen — Zustand byte-gleich (serialize, auch zcto_active), Rueckrufe der
 *        beobachteten Kanaele in gleicher Folge, Zaehlerlesen zwischen den Fenstern exakt,
 *        Dirty-Meldung gleich dem Wechsel von „Interrupt anstehend".
 *        Zufaellige Konfigurationen: Zeitgeber/Zaehler, Vorteiler 16/256, Zeitkonstante 0 (= 256),
 *        Trigger-Betrieb, Bruchtakt-Eingaenge, Kaskade ZC/TO(n) → CLK/TRG(n+1) im Rueckruf,
 *        Kanalmaske, Neuprogrammierung und Kanal-Reset mitten im Lauf.
 */
#include <gtest/gtest.h>
#include "core/primitives/z80_ctc.h"
#include <algorithm>
#include <random>
#include <vector>

namespace {

struct Rig {
    Z80CTC ctc;
    std::vector<int> ereignisse;   // 2*Kanal + Pegel, nur Kanaele der Maske

    Rig(uint8_t maske, bool kaskade, const uint64_t hz[4], uint64_t phi) {
        for (int k = 0; k < 4; ++k) if (hz[k]) ctc.setzeEingangsTakt(k, hz[k], phi);
        ctc.setZCTOCallback([this, maske, kaskade](int ch, bool lvl) {
            if (!((maske >> ch) & 1)) return;      // wie jede Verdrahtung: nur Maskenkanaele
            ereignisse.push_back(2 * ch + (lvl ? 1 : 0));
            if (kaskade && ch < 3) ctc.clkTrg(ch + 1, lvl);
        }, maske);
    }
    std::vector<uint8_t> zustand() const {
        std::vector<uint8_t> v; ctc.serialize(v); ctc.serializeTakt(v); return v;
    }
    bool pending() const {
        const auto d = ctc.debugState();
        for (auto& c : d.ch) if (c.intPending) return true;
        return false;
    }
};

}  // namespace

TEST(CtcSprung, ZufallskonfigurationenGleichEinzeltakten) {
    std::mt19937 rng(0xC7C5);
    auto zufall = [&](int n) { return static_cast<int>(rng() % n); };
    constexpr uint64_t PHI = 4000000;
    for (int lauf = 0; lauf < 400; ++lauf) {
        uint64_t hz[4] = {0, 0, 0, 0};
        for (int k = 0; k < 4; ++k) if (zufall(3) == 0) hz[k] = 100000 + zufall(3800000);
        const uint8_t maske = static_cast<uint8_t>(zufall(16));
        const bool kaskade = zufall(2) == 0;
        Rig a(maske, kaskade, hz, PHI), b(maske, kaskade, hz, PHI);

        auto programmiere = [&](int k) {
            uint8_t ctrl = 0x01 | 0x04;
            if (zufall(3) == 0) ctrl |= 0x80;                 // Interrupt
            if (zufall(2) == 0) ctrl |= 0x40;                 // Zaehler
            if (zufall(2) == 0) ctrl |= 0x20;                 // Vorteiler 256
            if (zufall(5) == 0) ctrl |= 0x08;                 // Trigger-Betrieb
            if (zufall(2) == 0) ctrl |= 0x10;
            const uint8_t tc = zufall(4) == 0 ? 0 : static_cast<uint8_t>(1 + zufall(zufall(2) ? 6 : 255));
            for (Rig* r : {&a, &b}) { r->ctc.ioWrite(k, ctrl); r->ctc.ioWrite(k, tc); }
        };
        for (int k = 0; k < 4; ++k) programmiere(k);

        for (int schritt = 0; schritt < 60; ++schritt) {
            const int r = zufall(10);
            const int k = zufall(4);
            if (r == 0) programmiere(k);
            else if (r == 1) for (Rig* x : {&a, &b}) x->ctc.ioWrite(k, 0x03);   // Kanal-Reset
            else if (r == 2) { const bool f = zufall(2); for (Rig* x : {&a, &b}) x->ctc.clkTrg(k, f); }

            const int n = zufall(8) == 0 ? 1 + zufall(200000) : 1 + zufall(zufall(2) ? 40 : 3000);
            const bool p0 = a.pending();
            for (int t = 0; t < n; ++t) a.ctc.clockTick();
            const bool dirty = b.ctc.clockTick(n);

            ASSERT_EQ(a.zustand(), b.zustand()) << "Lauf " << lauf << " Schritt " << schritt << " n=" << n;
            ASSERT_EQ(a.ereignisse, b.ereignisse) << "Lauf " << lauf << " Schritt " << schritt;
            ASSERT_EQ(dirty, a.pending() != p0) << "Lauf " << lauf << " Schritt " << schritt;
            for (int c = 0; c < 4; ++c)                       // Zaehlerlesen zwischen Fenstern
                ASSERT_EQ(a.ctc.ioRead(c), b.ctc.ioRead(c));
            a.ereignisse.clear(); b.ereignisse.clear();
            if (a.pending() && zufall(2)) {                   // Interrupt quittieren: Zustand weiterlaufen lassen
                for (Rig* x : {&a, &b}) { x->ctc.setIEI(true); x->ctc.getVector(); x->ctc.onRETI(); }
            }
        }
    }
}

TEST(CtcSprung, UnbeobachteterBaudkanalSpringtUndLiestExakt) {
    // Baudkanal: Zaehler an 1,229 MHz, ZK 2, kein Interrupt, Kanal nicht in der Maske.
    constexpr uint64_t HZ = 1229000, PHI = 4000000;
    Z80CTC a, b;
    int rufe = 0;
    for (Z80CTC* c : {&a, &b}) {
        c->setzeEingangsTakt(1, HZ, PHI);
        c->setZCTOCallback([&rufe, c, &b](int, bool) { if (c == &b) ++rufe; }, 0x04);
        c->ioWrite(1, 0x47);
        c->ioWrite(1, 2);
    }
    for (int i = 0; i < 1000003; ++i) a.clockTick();
    b.clockTick(1000003);
    EXPECT_EQ(a.getCount(1), b.getCount(1));
    EXPECT_EQ(a.ioRead(1), b.ioRead(1));
    EXPECT_EQ(rufe, 0);   // Kanal 1 liegt ausserhalb der Maske: kein Rueckruf im Sprungpfad
}

// Aufgeschobenes Takten: viele KLEINE Fenster (Befehlsgroesse), der Zustand von b wird nur
// gelegentlich angesehen.  Rueckrufe und Dirty-Meldung muessen trotzdem im selben Fenster
// fallen wie beim Einzeltakt (der Interrupt ist im selben Befehl sichtbar), und jeder
// Zugriff dazwischen (Lesen, Schreiben, clkTrg, Reset, Save-State) sieht den Einzeltakt-Zustand.
TEST(CtcSprung, AufgeschobenesTaktenGleichEinzeltaktenBeiZugriffenDazwischen) {
    std::mt19937 rng(0xDEF3);
    auto zufall = [&](int n) { return static_cast<int>(rng() % n); };
    constexpr uint64_t PHI = 4000000;
    for (int lauf = 0; lauf < 300; ++lauf) {
        uint64_t hz[4] = {0, 0, 0, 0};
        for (int k = 0; k < 4; ++k) if (zufall(3) == 0) hz[k] = 100000 + zufall(3800000);
        const uint8_t maske = static_cast<uint8_t>(zufall(16));
        const bool kaskade = zufall(2) == 0;
        Rig a(maske, kaskade, hz, PHI), b(maske, kaskade, hz, PHI);
        auto programmiere = [&](int k) {
            uint8_t ctrl = 0x01 | 0x04;
            if (zufall(3) == 0) ctrl |= 0x80;
            if (zufall(2) == 0) ctrl |= 0x40;
            if (zufall(2) == 0) ctrl |= 0x20;
            if (zufall(5) == 0) ctrl |= 0x08;
            if (zufall(2) == 0) ctrl |= 0x10;
            const uint8_t tc = zufall(4) == 0 ? 0 : static_cast<uint8_t>(1 + zufall(zufall(2) ? 6 : 255));
            for (Rig* r : {&a, &b}) { r->ctc.ioWrite(k, ctrl); r->ctc.ioWrite(k, tc); }
        };
        for (int k = 0; k < 4; ++k) programmiere(k);

        for (int schritt = 0; schritt < 3000; ++schritt) {
            const int r = zufall(200);
            const int k = zufall(4);
            if (r == 0) programmiere(k);
            else if (r == 1) for (Rig* x : {&a, &b}) x->ctc.ioWrite(k, 0x03);
            else if (r == 2) { const bool f = zufall(2); for (Rig* x : {&a, &b}) x->ctc.clkTrg(k, f); }
            else if (r == 3) ASSERT_EQ(a.ctc.ioRead(k), b.ctc.ioRead(k)) << lauf << "/" << schritt;
            else if (r == 4) ASSERT_EQ(a.zustand(), b.zustand()) << lauf << "/" << schritt;
            else if (r == 5) ASSERT_EQ(a.ctc.getCount(k), b.ctc.getCount(k));
            else if (r == 6 && a.pending()) {
                for (Rig* x : {&a, &b}) { x->ctc.setIEI(true); x->ctc.getVector(); x->ctc.onRETI(); }
            }
            else if (r == 7) for (Rig* x : {&a, &b}) x->ctc.reset();

            const int n = 4 + zufall(17);   // wie ein Befehl: 4..20 Takte
            const bool p0 = a.pending();
            for (int t = 0; t < n; ++t) a.ctc.clockTick();
            const bool dirty = b.ctc.clockTick(n);
            ASSERT_EQ(a.ereignisse, b.ereignisse) << "Lauf " << lauf << " Schritt " << schritt;
            ASSERT_EQ(dirty, a.pending() != p0) << "Lauf " << lauf << " Schritt " << schritt;
            a.ereignisse.clear(); b.ereignisse.clear();
        }
        ASSERT_EQ(a.zustand(), b.zustand()) << "Lauf " << lauf;
    }
}
