/**
 * @file test_mc80_mbi.cpp
 * @brief Magnetbandinterface MBI des MC80.2x (AP M11): Tore, PIO-B-Eingänge, Laufwerksregister
 *        EFH, Schreibweg (Bytetakt aus CTC 2 → ÷2 → CTC 0 → CTC 3), Leseweg (Synchronisation an
 *        CTC 3, acht Bittakte an CTC 0 je Byte über das Schieberegister).  Annahmen [?] stehen in
 *        core/cards/mc80/mbi.h.
 */

#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "core/cards/mc80/mbi.h"
#include "core/cards/mc80/senke.h"
#include "core/peripherals/kassette/kassetten_medium.h"

namespace {

constexpr int HZ = 2'457'600;

void takte(Mc80Mbi& m, int64_t n)
{
    while (n > 0) {
        const int t = n < 16 ? static_cast<int>(n) : 16;
        m.clockTick(t);
        n -= t;
    }
}

/// PIN0: PIO B mit der Tabelle PIOT (Vektor 2CH, Betriebsart 3, Bits 7–4 Eingang, Int gesperrt).
void pioB(Mc80Mbi& m)
{
    for (uint8_t b : {0x2C, 0xFF, 0xF0, 0x17, 0x3F}) m.ioWrite(0xFF, b);
}

std::shared_ptr<kassette::KassettenMedium> neueKassette()
{
    return std::make_shared<kassette::KassettenMedium>(kassette::Geometrie::kompaktkassette());
}

}  // namespace

TEST(Mc80Mbi, ToreUndRegister)
{
    Mc80Mbi m;
    K1520Bus bus;
    m.attachToBus(bus);
    EXPECT_EQ(bus.ioOwner(0xEF), &m);
    for (uint8_t p = 0xF8; p != 0x00; ++p) EXPECT_EQ(bus.ioOwner(p), &m) << std::hex << int(p);
    EXPECT_EQ(bus.ioOwner(0xF7), nullptr);
    bus.ioWrite(0xEF, 0x13);
    EXPECT_EQ(m.register_efh(), 0x13);
    EXPECT_EQ(bus.ioRead(0xEF), 0xFF);              // nur schreibbar
    m.reset();
    EXPECT_EQ(m.register_efh(), 0x00);
}

TEST(Mc80Mbi, EingaengePioB)
{
    Mc80Mbi m;
    pioB(m);
    // Ohne Kassette: Bit 7 = 0 (KAS. EINLEGEN), Bit 5 = 1.
    EXPECT_EQ(m.ioRead(0xFE) & 0xF0, 0x60);
    auto k = neueKassette();
    ASSERT_TRUE(m.laufwerk().kassetteEinlegen(k));
    takte(m, 64);
    // Eingelegt, im Vorspann (Bit 6 = 0 = Bandende [?]), nicht gesperrt.
    EXPECT_EQ(m.ioRead(0xFE) & 0xF0, 0x80);
    m.laufwerk().kassetteAuswerfen();
    k->setSchreibschutz(kassette::Seite::A, true);
    ASSERT_TRUE(m.laufwerk().kassetteEinlegen(k));
    takte(m, 64);
    EXPECT_EQ(m.ioRead(0xFE) & 0x20, 0x20);         // Riegel ausgebrochen
}

TEST(Mc80Mbi, LaufwerksregisterSteuertDasBand)
{
    Mc80Mbi m;
    pioB(m);
    ASSERT_TRUE(m.laufwerk().kassetteEinlegen(neueKassette()));
    m.ioWrite(0xFE, 0x03);                          // LIN0: Lesen + Capstan
    m.ioWrite(0xEF, 0x13);                          // Lauf
    takte(m, HZ);                                   // 1 s (Anlauf 0,5 s [?])
    const double p1 = m.laufwerk().positionMm();
    EXPECT_GT(p1, 15.0);
    takte(m, HZ / 2);
    EXPECT_NEAR(m.laufwerk().positionMm() - p1, 47.6 / 2, 1.0);   // 4,76 cm/s
    m.ioWrite(0xEF, 0x06);                          // schneller Vorlauf
    takte(m, HZ);
    EXPECT_GT(m.laufwerk().positionMm(), p1 + 200.0);
    m.ioWrite(0xEF, 0x0A);                          // schneller Rücklauf bis an den Anfang
    takte(m, 3 * HZ);
    EXPECT_EQ(m.laufwerk().positionMm(), 0.0);
    // Am Anfang liegt der Vorspann unter der Lichtschranke: Bit 6 = 0 (RCK0 wartet darauf).
    EXPECT_EQ(m.ioRead(0xFE) & 0x40, 0x00);
    m.ioWrite(0xEF, 0x00);
    m.ioWrite(0xFE, 0x01);                          // AUS
    takte(m, HZ / 10);
    EXPECT_FALSE(m.laufwerk().bewegt());
    EXPECT_FALSE(m.laufwerk().reserviert());
}

TEST(Mc80Mbi, SchreibenEinByteJe5Millisekunden)
{
    Mc80Mbi m;
    pioB(m);
    auto k = neueKassette();
    ASSERT_TRUE(m.laufwerk().kassetteEinlegen(k));
    ASSERT_TRUE(m.laufwerk().positionSetzen(200'000));   // hinter dem Vorspann (80 mm)
    // WIN0 (0A60H): PIO A Ausgabe, CTC 0 47H/08H, CTC 3 C7H/01H, CTC 2 07H/32H.
    m.ioWrite(0xFD, 0x0F);
    m.ioWrite(0xFC, 0x5A);
    m.ioWrite(0xF8, 0x08);
    m.ioWrite(0xF8, 0x47); m.ioWrite(0xF8, 0x08);
    m.ioWrite(0xFB, 0xC7); m.ioWrite(0xFB, 0x01);
    m.ioWrite(0xFA, 0x07); m.ioWrite(0xFA, 0x32);
    m.ioWrite(0xFE, 0x02);                          // Schreiben, Capstan, Pausenpegel
    m.ioWrite(0xEF, 0x13);
    takte(m, HZ);                                   // Anlauf; Pausenpegel schreibt nichts
    EXPECT_EQ(m.geschriebeneBytes(), 0u);
    EXPECT_TRUE(m.ctc().debugState().ch[3].intPending);   // CTC 3 zählt die Bytezeiten trotzdem
    m.ioWrite(0xFE, 0x0A);                          // Daten
    takte(m, HZ);                                   // 1 s
    // 2 457 600 / 16 / 50 = 3072 Hz, ÷2 = 1536 Bit/s [?] → 192 Byte/s.
    EXPECT_NEAR(static_cast<double>(m.geschriebeneBytes()), 192.0, 2.0);
    m.ioWrite(0xFE, 0x02);                          // Pausenpegel: Block zu Ende
    takte(m, HZ / 10);
    m.ioWrite(0xEF, 0x00);
    m.ioWrite(0xFE, 0x01);
    takte(m, HZ / 10);
    ASSERT_EQ(k->anzahlBloecke(kassette::Seite::A), 1u);
    const auto& b = k->block(kassette::Seite::A, 0).bytes;
    EXPECT_EQ(b.size(), m.geschriebeneBytes());
    for (uint8_t x : b) ASSERT_EQ(x, 0x5A);
}

TEST(Mc80Mbi, LesenSynchronisationUndBytes)
{
    Mc80Mbi m;
    pioB(m);
    auto k = neueKassette();
    // Ein Datenblock wie WVO0 ihn baut: AA · BLN 0001 · 128 Daten · CRC lo,hi · AA.
    std::vector<uint8_t> nutz = {0x00, 0x01};
    for (int i = 0; i < 128; ++i) nutz.push_back(static_cast<uint8_t>(i));
    const uint16_t crc = kassette::crc16_arc(nutz);
    std::vector<uint8_t> block = {0xAA};
    block.insert(block.end(), nutz.begin(), nutz.end());
    block.push_back(static_cast<uint8_t>(crc));
    block.push_back(static_cast<uint8_t>(crc >> 8));
    block.push_back(0xAA);
    ASSERT_EQ(block.size(), 0x86u);
    ASSERT_TRUE(k->schreibeBlock(kassette::Seite::A, 150'000, block));
    ASSERT_TRUE(m.laufwerk().kassetteEinlegen(k));
    // LIN0 (07A0H): PIO A Betriebsart 1, CTC 3 F7H/01H; ISY schärft CTC 0 mit C7H/08H.
    m.setIEI(true);
    m.ioWrite(0xFD, 0x4F);
    m.ioWrite(0xF8, 0x08);
    m.ioWrite(0xFB, 0xF7); m.ioWrite(0xFB, 0x01);
    m.ioWrite(0xF8, 0xC7); m.ioWrite(0xF8, 0x08);
    m.ioWrite(0xFE, 0x03);
    m.ioWrite(0xEF, 0x13);
    std::vector<uint8_t> gelesen;
    int sync = 0;
    for (int64_t t = 0; t < 6LL * HZ; t += 16) {
        m.clockTick(16);
        while (m.hasInterrupt()) {
            const uint8_t v = m.getVector();
            if (v == 0x0E) ++sync;                  // CTC 3: ISY
            if (v == 0x08) gelesen.push_back(m.ioRead(0xFC));   // CTC 0: ILB
            m.retiAnnehmen();
        }
    }
    EXPECT_EQ(sync, 2);                             // Blockanfang und Blockende
    EXPECT_EQ(gelesen, block);
    EXPECT_EQ(m.geleseneBytes(), block.size());
}

TEST(Mc80Mbi, SenkeVerwirftUndLiestFF)
{
    Mc80Senke s("PPE", 0xE0, 9);
    K1520Bus bus;
    s.attachToBus(bus);
    EXPECT_EQ(bus.ioOwner(0xE8), &s);
    EXPECT_EQ(bus.ioOwner(0xE9), nullptr);
    bus.ioWrite(0xE2, 0x78);
    bus.ioWrite(0xE2, 0x70);
    EXPECT_EQ(s.schreibzugriffe(0xE2), 2u);
    EXPECT_EQ(s.letzterWert(0xE2), 0x70);
    EXPECT_EQ(bus.ioRead(0xE6), 0xFF);
}
