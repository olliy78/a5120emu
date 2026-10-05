/**
 * @file test_k5122_pc1715.cpp
 * @brief K5122 in der Konfiguration „1715" (doc/design/21_pc1715.md AP-2): Portlage
 *        Daten-PIO 00H–03H / Steuer-PIO 04H–07H, SE-Register 20H, MO-Register 21H
 *        (DB4–7 = /MO), Indexinterrupt über /ASTB im Mode 0, Markenlesen wie der Urlader S502.
 *
 * Belege: Servicehandbuch PC 1715 §1.5.2.1/§1.5.2.2, CP/A-BIOS `biopdskt.mac`
 * (doc/pc1715/cpa_bios.md §1), `doc/EPROMS/PC1715/s502.prn`.
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <vector>

#include "core/bus/k1520_bus.h"
#include "core/cards/k5122/k5122.h"
#include "core/logger.h"
#include "core/peripherals/floppy_drive/disk_format.h"
#include "core/peripherals/floppy_drive/drive_profile.h"
#include "tests/support/temp_path.h"

namespace {

constexpr uint32_t kHz = 2'458'000;   // PC 1715

DiskFormat format5x1024() {
    DiskFormat f;
    f.name = "pc1715_5x1024";
    TrackFormat t;
    t.cyl_first = 0; t.cyl_last = 9; t.head_first = 0; t.head_last = 1;
    t.secs_per_track = 5; t.bytes_per_sec = 1024; t.encoding = Encoding::MFM;
    f.tracks.push_back(t);
    return f;
}

class K5122Pc1715 : public ::testing::Test {
protected:
    K1520Bus    bus;
    K5122       card{bus, {builtinDriveProfile("K5601"), builtinDriveProfile("K5601"),
                           builtinDriveProfile("none"), builtinDriveProfile("none")}, kHz};
    DiskFormat  fmt = format5x1024();
    std::string pfad;

    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
        card.setPortlage(K5122::Portlage::Pc1715);
        card.setSynchronisation(K5122::Synchronisation::Wait);
        pfad = k1520test::tempPath("k1520_pc1715_5x1024.img");
        {
            std::ofstream o(pfad, std::ios::binary | std::ios::trunc);
            std::vector<char> b(10 * 2 * 5 * 1024, static_cast<char>(0xE5));
            o.write(b.data(), static_cast<std::streamsize>(b.size()));
        }
        ASSERT_TRUE(card.mountDisk(0, pfad, fmt, false));
        // fdc_init des S502 (0573H): Daten-PIO A Mode 0, B Mode 1, Steuer-PIO A Mode 0,
        // B Mode 3 mit Maske E3H.
        card.ioWrite(0x01, 0x0F);
        card.ioWrite(0x03, 0x4F);
        card.ioWrite(0x05, 0x0F);
        card.ioWrite(0x04, 0xFF);
        card.ioWrite(0x07, 0xCF);
        card.ioWrite(0x07, 0xE3);
    }
    void TearDown() override { std::remove(pfad.c_str()); }
};

}  // namespace

TEST(K5122Portlage, VorgabeIstK1520) {
    K1520Bus bus;
    K5122 card(bus);
    EXPECT_EQ(card.portlage(), K5122::Portlage::K1520);
}

/// Steuer-PIO an 04H–07H, Daten-PIO an 00H–03H (AB0 = Steuerwort, AB1 = Kanal); die
/// K1520-Lage 10H–18H gibt es in dieser Konfiguration nicht.
TEST_F(K5122Pc1715, PortlageDatenUndSteuerPio) {
    card.ioWrite(0x20, 0xEE);   // LW 0 wählen
    card.ioWrite(0x21, 0xEE);   // Motor LW 0
    // Steuer-PIO A Mode 0: Ausgaberegister zurücklesbar; Daten-PIO A ebenso, getrennt.
    card.ioWrite(0x04, 0xBF);
    card.ioWrite(0x00, 0x5A);
    EXPECT_EQ(card.ioRead(0x04), 0xBF);
    EXPECT_EQ(card.ioRead(0x00), 0x5A);
    // Schritt nach innen über 04H (BFH, 3FH, BFH = /ST-Impuls, Bit5 = 1): Steuer-PIO A.
    EXPECT_EQ(card.drive(0).currentCylinder(), 0);
    card.ioWrite(0x04, 0x3F);
    card.ioWrite(0x04, 0xBF);
    EXPECT_EQ(card.drive(0).currentCylinder(), 1);
    // /T0 (Steuer-PIO B Bit 7, Eingang): auf Spur 1 = 1, nach dem Schritt hinaus = 0.
    EXPECT_NE(card.ioRead(0x06) & 0x80, 0);
    card.ioWrite(0x04, 0x9F);
    card.ioWrite(0x04, 0x1F);
    card.ioWrite(0x04, 0x9F);
    EXPECT_EQ(card.drive(0).currentCylinder(), 0);
    EXPECT_EQ(card.ioRead(0x06) & 0x80, 0);
    // 10H–18H und 20H–23H lesen FFH (SE-/MO-Register sind nur beschreibbar).
    for (uint8_t p : {0x10, 0x12, 0x14, 0x16, 0x18, 0x20, 0x21}) EXPECT_EQ(card.ioRead(p), 0xFF) << int(p);
}

/// SE-Register 20H: DB4–7 /SE (Auswahl), DB0–3 /LCK = Türverriegelung — KEIN Motor.  Den
/// Motor schaltet das MO-Register 21H mit DB4–7 (/MO, low-aktiv); 22H/23H spiegeln.
TEST_F(K5122Pc1715, SeRegisterWaehltMoRegisterSchaltetMotor) {
    card.ioWrite(0x20, 0xDD);                 // LW 1 wählen, /LCK 1
    EXPECT_EQ(card.debugState().drive, 1);
    EXPECT_FALSE(card.isMotorOn(1)) << "20H schaltet am 1715 keinen Motor";
    card.ioWrite(0x21, 0xEF);                 // /MO0 = 0
    EXPECT_TRUE(card.isMotorOn(0));
    EXPECT_FALSE(card.isMotorOn(1));
    card.ioWrite(0x20, 0xFF);                 // abwählen: Motoren laufen weiter (S502 0587H)
    EXPECT_TRUE(card.isMotorOn(0));
    // UDOS 1715: 20H := FFH, 21H := 00H ⇒ alle Motoren an (das Datenbyte zählt, nicht /SE).
    card.ioWrite(0x21, 0x00);
    for (int d = 0; d < 4; ++d) EXPECT_TRUE(card.isMotorOn(d)) << d;
    card.ioWrite(0x23, 0xFF);                 // Spiegel von 21H: alle aus
    for (int d = 0; d < 4; ++d) EXPECT_FALSE(card.isMotorOn(d)) << d;
    card.ioWrite(0x22, 0xBB);                 // Spiegel von 20H
    EXPECT_EQ(card.debugState().drive, 2);
}

/// WaitRDY des S502 (0402H): Steuer-PIO A Mode 0, Interruptwort 83H; jeder Indeximpuls an
/// /ASTB ist ein Interrupt — ohne dass 04H dazwischen geschrieben wird.  ≥ 3 in 0,94 s.
TEST_F(K5122Pc1715, JederIndexImpulsIstEinInterrupt) {
    bus.setInterruptChain({&card});
    card.ioWrite(0x20, 0xEE);
    card.ioWrite(0x21, 0xEE);
    card.ioWrite(0x05, 0x83);
    int ints = 0;
    for (long t = 0; t < static_cast<long>(kHz) * 94 / 100; t += 1000) {
        card.update(1000);
        bus.markIntDirty();
        bus.updateInterruptChain();
        if (bus.isINT()) {
            (void)bus.interruptAcknowledge();
            ++ints;
            bus.signalRETI();
            bus.markIntDirty();
        }
    }
    // 300 U/min = 5 Indexe/s; abzüglich Motoranlauf mindestens drei.
    EXPECT_GE(ints, 3);
    EXPECT_LE(ints, 5);
}

/// Markenlesen wie `ReadField` (S502 0483H): A5H, BBH, IN 02H, dann 85H; MKE (06H Bit 1,
/// high-aktiv) kommt, und das erste IN 02H liefert das erste A1 der Sync-Gruppe, danach
/// A1 A1 FE und das Kennfeld.
TEST_F(K5122Pc1715, MarkeLesenWieDerUrlader) {
    card.ioWrite(0x20, 0xEE);
    card.ioWrite(0x21, 0xEE);
    card.update(static_cast<int>(kHz / 10));
    auto lies = [&] { const uint8_t b = card.ioRead(0x02); card.update(bus.takeWaitCycles()); return b; };
    card.ioWrite(0x04, 0xA5);
    card.ioWrite(0x04, 0xBB);
    (void)lies();
    card.ioWrite(0x04, 0x85);
    int n = 0;
    while (!(card.ioRead(0x06) & 0x02) && n < 100'000) { card.update(40); ++n; }
    ASSERT_LT(n, 100'000) << "MKE kam nicht";
    std::vector<uint8_t> b;
    for (int i = 0; i < 8; ++i) b.push_back(lies());
    EXPECT_EQ(b[0], 0xA1);
    // bis zu drei A1, dann FE + Zylinder 0, Kopf 0
    size_t k = 0;
    while (k < b.size() && b[k] == 0xA1) ++k;
    ASSERT_LT(k + 2, b.size());
    EXPECT_EQ(b[k], 0xFE);
    EXPECT_EQ(b[k + 1], 0x00);
    EXPECT_EQ(b[k + 2], 0x00);
}
