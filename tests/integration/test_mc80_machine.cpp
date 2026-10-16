/**
 * @file test_mc80_machine.cpp
 * @brief MC80.3x als Maschine (AP M2): nach dem Einschalten fuehrt die ZVE EGOS aus — RESET
 *        (0776H), RESET-Kette mit KERN_RESET, Magnetband-Init und der Einschaltmeldung aus dem
 *        SPE-2-ROM, bis zur Kommandoschleife — und laeuft 5 Mio. Takte ohne Absturz.  Bild und
 *        Tastatur prueft test_mc80_boot.cpp (M3); hier geprueft wird ueber den Programmzaehler und die Systemzellen.
 *        Adressen: doc/mc80/egos.prn.
 */

#include <gtest/gtest.h>
#include <bitset>
#include <memory>
#include <stdexcept>

#include "core/machines/mc80/mc80.h"

namespace {

struct Lauf {
    std::unique_ptr<Mc80Machine> m;
    std::bitset<0x10000> besucht;
    explicit Lauf(Mc80Machine::Config cfg = {}) : m(std::make_unique<Mc80Machine>(cfg)) {
        m->zve().cpu().traceCallback = [this](const Z80& c) { besucht.set(c.PC); };
    }
    void fahre(uint64_t takte) {
        while (m->totalCycles() < takte) m->run(100000);
    }
};

TEST(Mc80Maschine, NachDemEinschaltenStehtDerPcAufNull) {
    Mc80Machine m;
    EXPECT_EQ(m.cpuPC(), 0x0000);
    EXPECT_EQ(m.machineType(), 6);
    EXPECT_EQ(m.fbWidth(), 512);
    EXPECT_EQ(m.fbHeight(), 256);
    EXPECT_EQ(m.memReadDebug(0x0000), 0xC3);
    EXPECT_EQ(m.zve().segmentRegister(), 0x00);
}

TEST(Mc80Maschine, EgosLaeuftDurchResetUndKetteBisZurKommandoschleife) {
    Lauf l;
    l.fahre(5'000'000);
    auto& b = l.besucht;
    EXPECT_TRUE(b.test(0x0776)) << "RESET";
    EXPECT_TRUE(b.test(0x0787)) << "nach der 8CH-Schleife";
    EXPECT_TRUE(b.test(0x0B17)) << "KERN_RESET (ROM-Kette)";
    EXPECT_TRUE(b.test(0x1FF7)) << "MB_INIT (Magnetband-RESET-Routine)";
    EXPECT_TRUE(b.test(0x25BD)) << "Einschaltmeldung aus dem SPE-2-ROM";
    EXPECT_TRUE(b.test(0x07BD)) << "Ende der RESET-Kette";
    EXPECT_TRUE(b.test(0x07D4)) << "Kommandoschleife";
    EXPECT_TRUE(b.test(0x07EF)) << "CON-Eingabe";

    // Kein Absturz: EGOS wartet in der Zeileneingabe auf das Zeilenende aus dem
    // Tastaturinterrupt (02D7H: BIT 7,(IY+10) / JR Z) — ohne Tastendruck.
    EXPECT_GE(l.m->cpuPC(), 0x02D7) << std::hex << l.m->cpuPC();
    EXPECT_LE(l.m->cpuPC(), 0x02DB) << std::hex << l.m->cpuPC();
    EXPECT_FALSE(l.m->zve().cpu().halted);
    // Nie Code aus dem Leerbereich oder aus dem ungeoeffneten Segmentfenster ausgefuehrt:
    for (uint32_t a = 0x2800; a < 0xC000; ++a)
        ASSERT_FALSE(b.test(a)) << "Code bei " << std::hex << a;

    // Systemzellen: SEGD/SEGC = Segment 1, Segmentregister 01H; CON-Init hat die Vektoren der
    // ASP im Schutzfenster gesetzt (SIO-A-Rx FFFCH → 044BH, CTC-K3 FFEEH → 046FH) — der Schutz
    // ist also nach der 8CH-Schleife aus.  (Die ADT bei FC00H legt erst das Kommando INIT an.)
    EXPECT_EQ(l.m->memReadDebug(0xFBFE), 0x01);
    EXPECT_EQ(l.m->memReadDebug(0xFBFB), 0x01);
    EXPECT_EQ(l.m->zve().segmentRegister(), 0x01);
    EXPECT_EQ(l.m->memReadDebug(0xFFFC), 0x4B);
    EXPECT_EQ(l.m->memReadDebug(0xFFFD), 0x04);
    EXPECT_EQ(l.m->memReadDebug(0xFFEE), 0x6F);
    EXPECT_EQ(l.m->memReadDebug(0xFFEF), 0x04);
    EXPECT_FALSE(l.m->zve().schreibschutz());
    EXPECT_EQ(l.m->zve().ramSteuerAusgaben(), 256u);
}

TEST(Mc80Maschine, ResetFaehrtEgosErneut) {
    Lauf l;
    l.fahre(2'000'000);
    l.m->reset();
    EXPECT_EQ(l.m->cpuPC(), 0x0000);
    EXPECT_EQ(l.m->zve().segmentRegister(), 0x00);
    l.besucht.reset();
    const uint64_t t0 = l.m->totalCycles();
    l.fahre(t0 + 5'000'000);
    EXPECT_TRUE(l.besucht.test(0x0776));
    EXPECT_TRUE(l.besucht.test(0x07D4));
}

TEST(Mc80Maschine, Variante31UndZweiSpeKarten) {
    Mc80Machine::Config c;
    c.variante = Mc80Machine::Config::Variante::Mc8031;
    c.spe_karten = 2;
    Lauf l(c);
    EXPECT_EQ(l.m->speKarten(), 2);
    l.fahre(5'000'000);
    EXPECT_TRUE(l.besucht.test(0x07D4));
}

TEST(Mc80Maschine, NochNichtNachgebildeteVariantenWerdenAbgelehnt) {
    using V = Mc80Machine::Config::Variante;
    for (V v : {V::Mc8033, V::Mc8021, V::Mc8022}) {
        Mc80Machine::Config c;
        c.variante = v;
        EXPECT_THROW(Mc80Machine m(c), std::invalid_argument) << Mc80Machine::variantenName(v);
    }
    Mc80Machine::Config c;
    c.spe_karten = 3;
    EXPECT_THROW(Mc80Machine m(c), std::invalid_argument);
}

TEST(Mc80Maschine, KeineLaufwerke) {
    Mc80Machine m;
    EXPECT_FALSE(m.mountDisk(0, "x.img", "", false));
    EXPECT_TRUE(m.formatCatalog().formats().empty());
}

}  // namespace
