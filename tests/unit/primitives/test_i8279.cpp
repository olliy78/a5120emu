/**
 * @test I8279.* — Tastaturteil des 8279 (doc/prg710/resident.md §5): Befehle 02H/C1H,
 *       Status (Zeichenzahl, voll, Unter-/Überlauf), FIFO-Tiefe 8, Datenlesen.
 */

#include <gtest/gtest.h>
#include "core/primitives/i8279.h"

TEST(I8279, NachResetLeerUndStatusNull)
{
    I8279 k;
    EXPECT_EQ(k.readStatus(), 0);
    EXPECT_FALSE(k.irq());
}

TEST(I8279, TasteLandetImFifoUndWirdInReihenfolgeGelesen)
{
    I8279 k;
    k.pushKey(0x37);
    k.pushKey(0x41);
    EXPECT_EQ(k.readStatus() & I8279::ST_ANZAHL, 2);
    EXPECT_TRUE(k.irq());
    EXPECT_EQ(k.readData(), 0x37);
    EXPECT_EQ(k.readData(), 0x41);
    EXPECT_EQ(k.readStatus(), 0);
}

TEST(I8279, FifoHatAchtEintraegeUndMeldetUeberlauf)
{
    I8279 k;
    for (int i = 0; i < 8; ++i) EXPECT_TRUE(k.pushKey(static_cast<uint8_t>(i)));
    EXPECT_TRUE(k.full());
    EXPECT_EQ(k.readStatus() & (I8279::ST_VOLL | I8279::ST_ANZAHL), I8279::ST_VOLL)
        << "8 Einträge: Zähler läuft über die drei Bit (0), Bit 3 = voll";
    EXPECT_FALSE(k.pushKey(0x55));
    EXPECT_NE(k.readStatus() & I8279::ST_UEBERLAUF, 0);
    EXPECT_EQ(k.readData(), 0) << "der verlorene Code stand nie im FIFO";
}

TEST(I8279, LesenAusLeeremFifoMeldetUnterlaufUndLiefertFF)
{
    I8279 k;
    EXPECT_EQ(k.readData(), 0xFF);
    EXPECT_NE(k.readStatus() & I8279::ST_UNTERLAUF, 0);
}

TEST(I8279, ClearAllLoeschtFifoUndZustandsbits)
{
    I8279 k;
    k.writeCommand(0x02);
    EXPECT_EQ(k.lastMode(), 0x02);
    k.pushKey(1);
    k.readData(); k.readData();   // zweites Lesen: Unterlauf
    k.pushKey(2);
    k.writeCommand(0xC1);
    EXPECT_EQ(k.readStatus(), 0);
    EXPECT_EQ(k.lastCommand(), 0xC1);
    EXPECT_EQ(k.lastMode(), 0x02) << "Clear ändert die Betriebsart nicht";
}

TEST(I8279, UnbekannteBefehleSindFolgenlos)
{
    I8279 k;
    k.pushKey(9);
    k.writeCommand(0x40);   // Lesen FIFO/Sensor
    k.writeCommand(0x90);   // Anzeige-RAM schreiben
    k.writeData(0x12);
    EXPECT_EQ(k.readStatus() & I8279::ST_ANZAHL, 1);
}
