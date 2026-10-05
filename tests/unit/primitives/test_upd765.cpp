/**
 * @test Upd765.* — Floppy-Controller µPD765/U8272 auf Sektorebene (doc/design/21_pc1715.md AP-W2):
 *       Befehle 03/04/05/06/07/08/0A/0D/0F/11, MSR-Phasen, DMA mit TC, EOT ohne TC, Nicht-DMA,
 *       Fehlerfälle (ND/MA/DE/NW/NR/WC), ungültiger Befehl, Reset, Save-State, SCP-3.0-Diskette.
 *
 * Testdiskette: 5 × 1024 B, 80 Zylinder, 2 Köpfe, MFM (das Format der SCP-3.0-Diskette), nur die
 * Spuren, die ein Test braucht, sind formatiert.
 */

#include <gtest/gtest.h>
#include "core/primitives/upd765.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/drive_profile.h"
#include "tests/support/fixtures.h"
#include <memory>

namespace {

uint8_t pat(int c, int h, int r, int i) { return static_cast<uint8_t>(c * 31 + h * 17 + r * 7 + i); }

constexpr uint8_t RQM = 0x80, DIO = 0x40, EXM = 0x20, CB = 0x10;

struct Rig {
    FloppyDriveV2 drive;
    FloppyDriveV2 leer;          ///< Laufwerk 1: ohne Diskette
    Upd765 f;

    static DriveProfile profil() {
        DriveProfile p = builtinDriveProfile("K5601");
        p.supports_fm = true;
        return p;
    }

    Rig() : drive(profil()), leer(profil()) {
        EXPECT_TRUE(drive.mount(DiskImage::createBlank(80, 2), false));
        f.setDrive(0, &drive);
        f.setDrive(1, &leer);
        // Laufwerk 2/3: kein Laufwerk angeschlossen
        for (int c = 0; c < 4; ++c)
            for (int h = 0; h < 2; ++h) formatiere(c, h);
    }

    void formatiere(int cyl, int head, Encoding enc = Encoding::MFM, int n = 5, int size = 1024) {
        std::vector<LogicalSector> secs;
        for (int r = 1; r <= n; ++r) {
            LogicalSector s;
            s.cyl = static_cast<uint8_t>(cyl); s.head = static_cast<uint8_t>(head);
            s.id = static_cast<uint8_t>(r); s.size = static_cast<uint16_t>(size);
            for (int i = 0; i < size; ++i) s.data.push_back(pat(cyl, head, r, i));
            secs.push_back(std::move(s));
        }
        ASSERT_TRUE(drive.writeTrackAt(static_cast<uint8_t>(cyl), static_cast<uint8_t>(head),
                                       TrackCodec::buildTrack(secs, enc)));
    }

    void cmd(std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes) {
            ASSERT_TRUE(f.readMsr() & RQM) << "FDC nicht bereit für Befehlsbyte";
            ASSERT_FALSE(f.readMsr() & DIO);
            f.writeData(b);
        }
    }
    std::vector<uint8_t> result(size_t n) {
        std::vector<uint8_t> r;
        for (size_t i = 0; i < n; ++i) {
            EXPECT_EQ(f.readMsr() & (RQM | DIO | CB), RQM | DIO | CB) << "Ergebnisbyte " << i;
            r.push_back(f.readData());
        }
        EXPECT_EQ(f.readMsr() & (RQM | DIO | CB), RQM) << "Ergebnisphase nicht beendet";
        return r;
    }
    void settle() { f.tick(200000); }

    std::vector<uint8_t> dmaIn(size_t n, bool tc) {   // FDC → Speicher
        std::vector<uint8_t> out;
        for (size_t i = 0; i < n; ++i) {
            int guard = 0;
            while (!f.drq() && ++guard < 100000) f.tick(64);
            if (!f.drq()) { ADD_FAILURE() << "kein DRQ bei Byte " << i; return out; }
            out.push_back(f.dmaRead());
        }
        if (tc) { f.setTC(true); f.tick(1000); f.setTC(false); }
        return out;
    }
    void dmaOut(const std::vector<uint8_t>& d, bool tc) {   // Speicher → FDC
        for (size_t i = 0; i < d.size(); ++i) {
            int guard = 0;
            while (!f.drq() && ++guard < 100000) f.tick(64);
            ASSERT_TRUE(f.drq()) << "kein DRQ bei Byte " << i;
            f.dmaWrite(d[i]);
        }
        if (tc) { f.setTC(true); f.tick(1000); f.setTC(false); }
    }
    void seek(uint8_t cyl) {
        cmd({0x0F, 0x00, cyl});
        settle();
        cmd({0x08});
        result(2);
    }
    std::vector<uint8_t> sektor(int c, int h, int r) {
        std::vector<uint8_t> v;
        for (int i = 0; i < 1024; ++i) v.push_back(pat(c, h, r, i));
        return v;
    }
};

}  // namespace

// ─── MSR / Befehlsphase / ungültig ────────────────────────────────────────────

TEST(Upd765, MsrPhasen_IdleBefehlAusfuehrungErgebnis)
{
    Rig r;
    EXPECT_EQ(r.f.readMsr(), RQM);                         // Idle: RQM, DIO = 0
    r.f.writeData(0x46);                                   // READ DATA MFM
    EXPECT_EQ(r.f.readMsr(), RQM | CB);                    // Befehlsphase
    for (uint8_t b : {0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x2A, 0xFF}) r.f.writeData(b);
    EXPECT_EQ(r.f.readMsr(), CB);                          // Ausführung (DMA): kein RQM, kein EXM
    r.dmaIn(1024, true);
    EXPECT_EQ(r.f.readMsr(), RQM | DIO | CB);              // Ergebnisphase
    r.result(7);
    EXPECT_EQ(r.f.readMsr(), RQM);
}

TEST(Upd765, UngueltigerBefehl_ST0_80)
{
    Rig r;
    r.f.writeData(0x1F);
    EXPECT_EQ(r.f.readMsr() & (RQM | DIO | CB), RQM | DIO | CB);
    EXPECT_EQ(r.f.readData(), 0x80);
    EXPECT_EQ(r.f.readMsr(), RQM);
    // Befehle mit unzulässigen Flag-Bits (hier MFM an SEEK) sind ebenfalls ungültig,
    // ebenso nicht benutzte Befehle (READ TRACK 02).
    r.f.writeData(0x4F);
    EXPECT_EQ(r.f.readData(), 0x80);
    r.f.writeData(0x02);
    EXPECT_EQ(r.f.readData(), 0x80);
}

TEST(Upd765, SenseInterruptOhneAnstehendes_ST0_80)
{
    Rig r;
    r.cmd({0x08});
    EXPECT_EQ(r.result(1), (std::vector<uint8_t>{0x80}));
}

// ─── Specify / Recalibrate / Seek / Sense ─────────────────────────────────────

TEST(Upd765, Specify_SetztNichtDmaOhneErgebnis)
{
    Rig r;
    EXPECT_FALSE(r.f.nonDma());
    r.cmd({0x03, 0xDF, 0x03});                             // ND = 1
    EXPECT_TRUE(r.f.nonDma());
    EXPECT_EQ(r.f.readMsr(), RQM);                         // kein Ergebnis, sofort wieder Idle
    r.cmd({0x03, 0xDF, 0x02});                             // ND = 0
    EXPECT_FALSE(r.f.nonDma());
}

TEST(Upd765, SeekUndRecalibrate_MitSenseInterruptStatus)
{
    Rig r;
    r.cmd({0x0F, 0x00, 0x0A});                             // SEEK Zylinder 10
    EXPECT_EQ(r.f.readMsr() & 0x1F, 0x01);                 // D0B: Laufwerk 0 sucht; CB = 0
    EXPECT_FALSE(r.f.irq());
    r.f.tick(1000);
    EXPECT_TRUE(r.f.seekBusy(0));
    r.settle();
    EXPECT_EQ(r.f.readMsr() & 0x0F, 0x00);
    EXPECT_TRUE(r.f.irq());
    EXPECT_EQ(r.drive.currentCylinder(), 10);
    r.cmd({0x08});
    EXPECT_EQ(r.result(2), (std::vector<uint8_t>{0x20, 0x0A}));   // SE, PCN
    EXPECT_FALSE(r.f.irq());

    r.cmd({0x07, 0x00});                                   // RECALIBRATE
    r.settle();
    r.cmd({0x08});
    EXPECT_EQ(r.result(2), (std::vector<uint8_t>{0x20, 0x00}));
    EXPECT_EQ(r.drive.currentCylinder(), 0);
    EXPECT_EQ(r.f.pcn(0), 0);
}

TEST(Upd765, RecalibrateOhneLaufwerk_EquipmentCheck)
{
    Rig r;
    r.cmd({0x07, 0x02});                                   // Einheit 2: nichts angeschlossen
    r.settle();
    r.cmd({0x08});
    EXPECT_EQ(r.result(2), (std::vector<uint8_t>{0x72, 0x00}));   // SE + IC=01 + EC, US 2
}

TEST(Upd765, SenseDriveStatus_T0_WP_RDY)
{
    Rig r;
    r.cmd({0x04, 0x00});
    EXPECT_EQ(r.result(1), (std::vector<uint8_t>{0x38}));  // RDY + T0 + zweiseitig
    r.seek(5);
    r.cmd({0x04, 0x04});                                   // Kopf 1
    EXPECT_EQ(r.result(1), (std::vector<uint8_t>{0x2C}));  // RDY + TS + HD, nicht T0
    r.drive.setWriteProtect(true);
    r.cmd({0x04, 0x00});
    EXPECT_EQ(r.result(1)[0] & 0x60, 0x60);               // WP + RDY
    r.cmd({0x04, 0x01});                                   // Laufwerk ohne Diskette
    EXPECT_EQ(r.result(1)[0] & 0x20, 0x00);               // nicht bereit
    r.cmd({0x04, 0x02});                                   // kein Laufwerk
    EXPECT_EQ(r.result(1)[0], 0x02);
}

TEST(Upd765, ReadyRueckruf_MotorAusMachtNichtBereit)
{
    Rig r;
    bool motor = false;
    r.f.ready = [&](int) { return motor; };
    r.cmd({0x04, 0x00});
    EXPECT_EQ(r.result(1)[0] & 0x20, 0);
    motor = true;
    r.cmd({0x04, 0x00});
    EXPECT_EQ(r.result(1)[0] & 0x20, 0x20);
}

// ─── Read ID ──────────────────────────────────────────────────────────────────

TEST(Upd765, ReadId_LiefertDieIdFelderDerReiheNach)
{
    Rig r;
    r.seek(2);
    for (int i = 1; i <= 6; ++i) {
        r.cmd({0x4A, 0x04});                               // MFM, Kopf 1
        r.settle();
        const auto e = r.result(7);
        const uint8_t rr = static_cast<uint8_t>(((i - 1) % 5) + 1);
        EXPECT_EQ(e, (std::vector<uint8_t>{0x04, 0x00, 0x00, 2, 1, rr, 3})) << i;
    }
}

TEST(Upd765, ReadId_FmUndMfmUnterscheiden)
{
    Rig r;
    r.formatiere(8, 0, Encoding::FM, 16, 128);             // FM-Spur
    r.seek(8);
    r.cmd({0x0A, 0x00});                                   // FM-Lesen
    r.settle();
    auto e = r.result(7);
    EXPECT_EQ(e[0] & 0xC0, 0x00);
    EXPECT_EQ(e[5], 1); EXPECT_EQ(e[6], 0);
    r.cmd({0x4A, 0x00});                                   // MFM-Lesen derselben Spur
    r.settle();
    e = r.result(7);
    EXPECT_EQ(e[0] & 0xC0, 0x40);                          // Abnormal Termination
    EXPECT_EQ(e[1], 0x01);                                 // MA
    r.seek(0);                                             // MFM-Spur mit FM-Lesen
    r.cmd({0x0A, 0x00});
    r.settle();
    EXPECT_EQ(r.result(7)[1], 0x01);
}

// ─── Read Data ────────────────────────────────────────────────────────────────

TEST(Upd765, ReadData_DmaBisTc_DatenUndST0Normal)
{
    Rig r;
    r.seek(1);
    r.cmd({0x46, 0x04, 0x01, 0x01, 0x02, 0x03, 0x05, 0x2A, 0xFF});  // C=1 H=1 R=2 N=3 EOT=5
    EXPECT_FALSE(r.f.drq());                               // erst nach der Latenz
    const auto d = r.dmaIn(1024, true);
    EXPECT_EQ(d, r.sektor(1, 1, 2));
    EXPECT_TRUE(r.f.irq());
    EXPECT_EQ(r.result(7), (std::vector<uint8_t>{0x04, 0x00, 0x00, 1, 1, 3, 3}));   // R + 1
    EXPECT_FALSE(r.f.irq());
}

TEST(Upd765, ReadData_MehrereSektorenBisTc)
{
    Rig r;
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x01, 0x03, 0x05, 0x2A, 0xFF});
    const auto d = r.dmaIn(2 * 1024, true);                // Sektor 1 + 2, TC am Ende von 2
    auto soll = r.sektor(0, 0, 1);
    auto s2 = r.sektor(0, 0, 2);
    soll.insert(soll.end(), s2.begin(), s2.end());
    EXPECT_EQ(d, soll);
    EXPECT_EQ(r.result(7), (std::vector<uint8_t>{0x00, 0x00, 0x00, 0, 0, 3, 3}));
}

TEST(Upd765, ReadData_TcAmLetztenSektorEot_CPlusEinsRGleichEins)
{
    Rig r;
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x05, 0x03, 0x05, 0x2A, 0xFF});
    r.dmaIn(1024, true);
    EXPECT_EQ(r.result(7), (std::vector<uint8_t>{0x00, 0x00, 0x00, 1, 0, 1, 3}));
}

TEST(Upd765, ReadData_BisEotOhneTc_AbnormalMitEndOfCylinder)
{
    Rig r;
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x04, 0x03, 0x05, 0x2A, 0xFF});   // Sektor 4 und 5, kein TC
    const auto d = r.dmaIn(2 * 1024, false);
    EXPECT_EQ(d.size(), 2048u);
    r.settle();
    EXPECT_FALSE(r.f.drq());
    EXPECT_EQ(r.result(7), (std::vector<uint8_t>{0x40, 0x80, 0x00, 1, 0, 1, 3}));   // IC=01, EN
}

TEST(Upd765, ReadData_TcMitteImSektor_BeendetSofortOhneFehler)
{
    Rig r;
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x02, 0x03, 0x05, 0x2A, 0xFF});
    const auto d = r.dmaIn(100, true);
    EXPECT_EQ(d.size(), 100u);
    EXPECT_FALSE(r.f.drq());
    EXPECT_EQ(r.result(7), (std::vector<uint8_t>{0x00, 0x00, 0x00, 0, 0, 2, 3}));   // R unverändert
}

TEST(Upd765, ReadData_MtWechseltAufKopf1)
{
    Rig r;
    r.cmd({0xC6, 0x00, 0x00, 0x00, 0x05, 0x03, 0x05, 0x2A, 0xFF});   // MT: Sektor 5 Kopf 0, dann Kopf 1
    auto d = r.dmaIn(1024, false);
    d = r.dmaIn(1024, true);                               // Sektor 1 von Kopf 1
    EXPECT_EQ(d, r.sektor(0, 1, 1));
    const auto e = r.result(7);
    EXPECT_EQ(e[0], 0x04);                                 // IC = 00, HD = 1
    EXPECT_EQ(e[4], 1);                                    // H = 1
    EXPECT_EQ(e[5], 2);                                    // R + 1
}

TEST(Upd765, ReadData_NichtDmaUeberDatenregister)
{
    Rig r;
    r.cmd({0x03, 0xDF, 0x03});                             // ND = 1
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x2A, 0xFF});
    std::vector<uint8_t> d;
    int guard = 0;
    while (d.size() < 1024 && ++guard < 1000000) {
        r.f.tick(32);
        const uint8_t m = r.f.readMsr();
        if (m & RQM) {
            EXPECT_EQ(m & (DIO | EXM | CB), DIO | EXM | CB);
            EXPECT_TRUE(r.f.irq());
            d.push_back(r.f.readData());
        }
    }
    EXPECT_EQ(d, r.sektor(0, 0, 3));
    r.settle();
    EXPECT_EQ(r.result(7)[0] & 0xC0, 0x40);                // EOT = 3 = R ohne TC: EN
}

// ─── Write Data + Scan Equal ──────────────────────────────────────────────────

TEST(Upd765, WriteData_UndScanEqual_GleichUndUngleich)
{
    Rig r;
    std::vector<uint8_t> neu(1024);
    for (int i = 0; i < 1024; ++i) neu[i] = static_cast<uint8_t>(0xA5 ^ i);

    r.cmd({0x45, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x2A, 0xFF});   // WRITE DATA R=3
    r.dmaOut(neu, true);
    EXPECT_EQ(r.result(7), (std::vector<uint8_t>{0x00, 0x00, 0x00, 1, 0, 1, 3}));  // EOT = R: C+1/R=1
    // Nachbarsektoren unberührt, Sektor 3 geändert (über den Lesepfad geprüft)
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x2A, 0xFF});
    EXPECT_EQ(r.dmaIn(1024, true), neu);
    r.result(7);
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x02, 0x03, 0x02, 0x2A, 0xFF});
    EXPECT_EQ(r.dmaIn(1024, true), r.sektor(0, 0, 2));
    r.result(7);

    // SCAN EQUAL (Prüflesen): gleich → SH, IC = 00
    r.cmd({0x51, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x2A, 0x01});
    r.dmaOut(neu, true);
    auto e = r.result(7);
    EXPECT_EQ(e[0] & 0xC0, 0x00);
    EXPECT_EQ(e[2], 0x08);                                 // Scan Equal Hit

    // ungleich → SN
    auto falsch = neu;
    falsch[700] ^= 0x01;
    r.cmd({0x51, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x2A, 0x01});
    r.dmaOut(falsch, true);
    e = r.result(7);
    EXPECT_EQ(e[0] & 0xC0, 0x00);
    EXPECT_EQ(e[2], 0x04);                                 // Scan Not Satisfied

    // FFH im Speicher ist Platzhalter
    auto joker = falsch;
    joker[700] = 0xFF;
    r.cmd({0x51, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x2A, 0x01});
    r.dmaOut(joker, true);
    EXPECT_EQ(r.result(7)[2], 0x08);
}

TEST(Upd765, WriteData_TcMitteImSektor_FuelltMitNullen)
{
    Rig r;
    r.cmd({0x45, 0x00, 0x00, 0x00, 0x01, 0x03, 0x05, 0x2A, 0xFF});
    r.dmaOut(std::vector<uint8_t>(10, 0x77), true);
    r.result(7);
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x2A, 0xFF});
    const auto d = r.dmaIn(1024, true);
    for (int i = 0; i < 10; ++i) EXPECT_EQ(d[i], 0x77);
    for (int i = 10; i < 1024; ++i) ASSERT_EQ(d[i], 0x00) << i;
    r.result(7);
}

// ─── Format Track ─────────────────────────────────────────────────────────────

TEST(Upd765, FormatTrack_UndLesenDanach)
{
    Rig r;
    r.seek(6);                                             // unformatiert (leere Diskette)
    r.cmd({0x4A, 0x00});                                   // READ ID: keine Marken
    r.settle();
    EXPECT_EQ(r.result(7)[1], 0x01);                       // MA

    r.cmd({0x4D, 0x00, 0x03, 0x05, 0x35, 0xE5});           // FORMAT, N=3, SC=5, GPL, D=E5
    std::vector<uint8_t> ids;
    for (uint8_t s = 1; s <= 5; ++s) for (uint8_t b : {uint8_t(6), uint8_t(0), s, uint8_t(3)}) ids.push_back(b);
    r.dmaOut(ids, false);                                  // TC wird beim FORMAT ignoriert
    r.settle();
    const auto e = r.result(7);
    EXPECT_EQ(e[0] & 0xC0, 0x00);
    EXPECT_EQ(e[1], 0x00);

    r.cmd({0x4A, 0x00});
    r.settle();
    EXPECT_EQ(r.result(7), (std::vector<uint8_t>{0x00, 0x00, 0x00, 6, 0, 1, 3}));
    r.cmd({0x46, 0x00, 0x06, 0x00, 0x04, 0x03, 0x04, 0x2A, 0xFF});
    const auto d = r.dmaIn(1024, true);
    EXPECT_EQ(d, std::vector<uint8_t>(1024, 0xE5));
    r.result(7);
    // die Spur ist eine gültige Spur: parseTrack sieht fünf Sektoren ohne CRC-Fehler
    const auto secs = TrackCodec::parseTrack(r.drive.track(0));
    ASSERT_EQ(secs.size(), 5u);
    for (const auto& s : secs) { EXPECT_TRUE(s.id_crc_ok); EXPECT_TRUE(s.data_crc_ok); }
}

// ─── Fehlerfälle ──────────────────────────────────────────────────────────────

TEST(Upd765, Fehler_SektorFehlt_NoData)
{
    Rig r;
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x09, 0x03, 0x09, 0x2A, 0xFF});
    r.settle();
    EXPECT_FALSE(r.f.drq());
    EXPECT_EQ(r.result(7), (std::vector<uint8_t>{0x40, 0x04, 0x00, 0, 0, 9, 3}));
}

TEST(Upd765, Fehler_FalscherZylinder_WrongCylinder)
{
    Rig r;
    r.cmd({0x46, 0x00, 0x07, 0x00, 0x02, 0x03, 0x02, 0x2A, 0xFF});   // C=7, Kopf steht auf 0
    r.settle();
    const auto e = r.result(7);
    EXPECT_EQ(e[0], 0x40);
    EXPECT_EQ(e[1], 0x04);                                 // ND
    EXPECT_EQ(e[2], 0x10);                                 // WC
}

TEST(Upd765, Fehler_FmLesenAufMfmSpur_KeineAdressmarke)
{
    Rig r;
    r.cmd({0x06, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x2A, 0xFF});   // ohne MFM-Bit
    r.settle();
    const auto e = r.result(7);
    EXPECT_EQ(e[0], 0x40);
    EXPECT_EQ(e[1], 0x01);                                 // MA
}

TEST(Upd765, Fehler_UnformatierteSpur_KeineAdressmarke)
{
    Rig r;
    r.seek(40);
    r.cmd({0x46, 0x00, 40, 0x00, 0x01, 0x03, 0x01, 0x2A, 0xFF});
    r.settle();
    EXPECT_EQ(r.result(7)[1], 0x01);
}

TEST(Upd765, Fehler_DatenCrc_DEundDD)
{
    Rig r;
    // Datenbyte kippen, CRC stehen lassen
    TrackImage& t = r.drive.mutableTrack(0);
    const auto secs = TrackCodec::parseTrack(t);
    t.bytes[secs[1].data_pos + 10] ^= 0x01;
    r.f.tick(0);
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x02, 0x03, 0x05, 0x2A, 0xFF});
    const auto d = r.dmaIn(1024, false);                   // die Daten kommen trotzdem
    EXPECT_EQ(d.size(), 1024u);
    r.settle();
    const auto e = r.result(7);
    EXPECT_EQ(e[0], 0x40);
    EXPECT_EQ(e[1], 0x20);                                 // DE
    EXPECT_EQ(e[2], 0x20);                                 // DD
    EXPECT_EQ(e[5], 2);                                    // R bleibt
}

TEST(Upd765, Fehler_SchreibschutzUndFormat_NotWritable)
{
    Rig r;
    r.drive.setWriteProtect(true);
    r.cmd({0x45, 0x00, 0x00, 0x00, 0x01, 0x03, 0x05, 0x2A, 0xFF});
    EXPECT_FALSE(r.f.drq());
    EXPECT_EQ(r.result(7), (std::vector<uint8_t>{0x40, 0x02, 0x00, 0, 0, 1, 3}));
    r.cmd({0x4D, 0x00, 0x03, 0x05, 0x35, 0xE5});
    EXPECT_EQ(r.result(7)[1], 0x02);
    // Lesen bleibt möglich
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x2A, 0xFF});
    EXPECT_EQ(r.dmaIn(1024, true), r.sektor(0, 0, 1));
    r.result(7);
}

TEST(Upd765, Fehler_LaufwerkLeerOderFehlt_NotReady)
{
    Rig r;
    r.cmd({0x46, 0x01, 0x00, 0x00, 0x01, 0x03, 0x01, 0x2A, 0xFF});   // Laufwerk 1: ohne Diskette
    EXPECT_EQ(r.result(7)[0], 0x49);                       // IC = 01, NR, US 1
    r.cmd({0x4A, 0x02});                                   // Laufwerk 2: nicht da
    EXPECT_EQ(r.result(7)[0], 0x4A);
    r.cmd({0x4D, 0x03, 0x03, 0x05, 0x35, 0xE5});
    EXPECT_EQ(r.result(7)[0], 0x4B);
}

// ─── Reset / Save-State ───────────────────────────────────────────────────────

TEST(Upd765, Reset_BrichtAllesAbUndHaeltDenBausteinAn)
{
    Rig r;
    r.cmd({0x0F, 0x00, 0x20});
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x01, 0x03, 0x05, 0x2A, 0xFF});
    r.f.setReset(true);
    EXPECT_EQ(r.f.readMsr(), 0x00);
    r.f.writeData(0x08);                                   // im Reset ignoriert
    r.f.setReset(false);
    EXPECT_EQ(r.f.readMsr(), RQM);
    EXPECT_FALSE(r.f.irq());
    EXPECT_FALSE(r.f.drq());
    r.settle();
    EXPECT_EQ(r.drive.currentCylinder(), 0);               // der Seek ist verworfen
    r.cmd({0x08});
    EXPECT_EQ(r.result(1)[0], 0x80);
}

TEST(Upd765, DrqUndIrqFlankenwerdenGemeldet)
{
    Rig r;
    int drq_hoch = 0, irq_hoch = 0;
    r.f.onDrq = [&](bool v) { if (v) ++drq_hoch; };
    r.f.onIrq = [&](bool v) { if (v) ++irq_hoch; };
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x2A, 0xFF});
    r.dmaIn(1024, true);
    EXPECT_EQ(drq_hoch, 1024);
    EXPECT_EQ(irq_hoch, 1);
}

TEST(Upd765, SaveState_MitteImTransfer)
{
    Rig r;
    r.cmd({0x46, 0x00, 0x00, 0x00, 0x03, 0x03, 0x05, 0x2A, 0xFF});
    auto d = r.dmaIn(300, false);
    std::vector<uint8_t> blob;
    r.f.serialize(blob);

    Upd765 g;
    g.setDrive(0, &r.drive);
    g.setDrive(1, &r.leer);
    const uint8_t* p = blob.data();
    ASSERT_TRUE(g.deserialize(p, blob.data() + blob.size()));
    EXPECT_EQ(p, blob.data() + blob.size());
    EXPECT_EQ(g.readMsr(), r.f.readMsr());
    EXPECT_EQ(g.drq(), r.f.drq());

    for (size_t i = 300; i < 1024; ++i) {
        int guard = 0;
        while (!g.drq() && ++guard < 100000) g.tick(64);
        ASSERT_TRUE(g.drq());
        d.push_back(g.dmaRead());
    }
    g.setTC(true);
    g.tick(1000);
    EXPECT_EQ(d, r.sektor(0, 0, 3));
    EXPECT_EQ(g.readMsr() & (RQM | DIO | CB), RQM | DIO | CB);
    std::vector<uint8_t> res;
    for (int i = 0; i < 7; ++i) res.push_back(g.readData());
    EXPECT_EQ(res, (std::vector<uint8_t>{0x00, 0x00, 0x00, 0, 0, 4, 3}));

    // abgeschnittener Datenstrom wird abgelehnt
    const uint8_t* q = blob.data();
    Upd765 h;
    EXPECT_FALSE(h.deserialize(q, blob.data() + blob.size() / 2));
}

// ─── Echte Diskette ───────────────────────────────────────────────────────────

TEST(Upd765, SCP30Diskette_ReadIdUndReadData)
{
    k1520test::TempDisk disk("pc1715w_scp30_system.hfe");
    FloppyDriveV2 drive(Rig::profil());
    ASSERT_TRUE(drive.mount(DiskImage::open(disk.path(), std::nullopt, false), false))
        << drive.lastError();
    Upd765 f;
    f.setDrive(0, &drive);
    auto cmd = [&](std::initializer_list<uint8_t> b) { for (uint8_t x : b) f.writeData(x); };
    auto result = [&](int n) { std::vector<uint8_t> v; for (int i = 0; i < n; ++i) v.push_back(f.readData()); return v; };

    cmd({0x4A, 0x00});
    f.tick(100000);
    const auto id = result(7);
    ASSERT_EQ(id[0] & 0xC0, 0x00) << "ST1 " << int(id[1]);
    EXPECT_EQ(id[3], 0);       // Zylinder 0
    EXPECT_EQ(id[6], 3);       // 1024-Byte-Sektoren

    const auto soll = TrackCodec::parseTrack(drive.track(0));
    ASSERT_EQ(soll.size(), 5u);
    const uint8_t r1 = soll[0].id;
    cmd({0x46, 0x00, 0x00, 0x00, r1, 0x03, r1, 0x2A, 0xFF});
    std::vector<uint8_t> d;
    for (int guard = 0; d.size() < 1024 && guard < 1000000; ++guard) {
        f.tick(64);
        if (f.drq()) d.push_back(f.dmaRead());
    }
    f.setTC(true);
    f.tick(1000);
    EXPECT_EQ(d, soll[0].data);
    EXPECT_EQ(result(7)[0] & 0xC0, 0x00);
}
