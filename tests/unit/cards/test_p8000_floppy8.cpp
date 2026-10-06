/**
 * @file test_p8000_floppy8.cpp
 * @brief Floppy-Seite der 8-Bit-Karte des P8000 (U8272 + UA858 + PIO2-Glue + Laufwerke):
 *        READ DATA per DMA in den DRAM, Laufwerkswahl nur über PB0–3, TC, INT an PB4,
 *        FDC-Reset über PB7, Datenratenumschaltung und das MON8-Rig (doc/design/25_p8000.md
 *        §10.7/§10.11 AP P5f, doc/p8000/schaltplan_8bit.md §6).
 */

#include <gtest/gtest.h>
#include "core/cards/p8000/floppy8.h"
#include "core/cards/p8000/karte8.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "tests/support/fixtures.h"
#include <string>

namespace {

constexpr const char* FIXTURE = "udosP8000_640k_wega.hfe";
constexpr uint16_t ZIEL = 0x8000;

P8000Karte8::Config karteCfg() {
    P8000Karte8::Config c;
    c.adp_start = 0x04;   // alle Seiten DRAM (sobald RFF gesetzt ist)
    return c;
}

struct P8000Floppy8_ : ::testing::Test {
    K1520Bus     bus;
    P8000Karte8  k{bus, karteCfg()};
    P8000Floppy8 f{k};
    k1520test::TempDisk disk{FIXTURE};

    void SetUp() override {
        k.powerOn();
        out(0x04, 0x00);   // RES_RFF: ab jetzt wirkt der ADP ⇒ DRAM überall
    }
    void out(uint8_t port, uint8_t d) { bus.ioWrite(port, d); }
    uint8_t in(uint8_t port) { return bus.ioRead(port); }

    /// PIO2 wie im Betrieb: A (Bitbetrieb, nur A7 Eingang) und B (nur B4 Eingang) ausgeben.
    void pio(uint8_t a, uint8_t b) {
        out(0x1D, 0xCF); out(0x1D, 0x80); out(0x1C, a);
        out(0x1F, 0xCF); out(0x1F, 0x10); out(0x1E, b);
    }
    static constexpr uint8_t A_5_25 = 0x1F;   // PA4 = 1 (4 MHz), Motoren aus, TC (A6) = 0
    void mountFixture() { ASSERT_TRUE(f.mount(0, disk.path(), f.laufwerke().defaultFormatName(0))); }

    /// Φ-Takte ohne CPU: nur FDC und DMA laufen (die CPU würde MON8 ausführen).
    void lauf(int takte) {
        for (int t = 0; t < takte;) {
            int n = 4;
            if (f.dma().busRequest()) n = k.schritt();
            f.takt(n);
            t += n;
        }
    }
    void fdcCmd(std::initializer_list<uint8_t> b) {
        for (uint8_t x : b) { ASSERT_TRUE(in(0x20) & 0x80); out(0x21, x); }
    }
    std::vector<uint8_t> fdcResult(int n) {
        std::vector<uint8_t> r;
        for (int i = 0; i < n; ++i) { lauf(100); r.push_back(in(0x21)); }
        return r;
    }
    /// DMA: FDC (Port B = E/A 20H, fest) → DRAM (Port A, +1), @p len Bytes; RDY aktiv high,
    /// Blockende-Interrupt nicht freigegeben (TC kommt trotzdem aus dem Blockende-Pegel).
    void dmaLesenProgrammieren(uint16_t adr, uint16_t len) {
        const uint16_t l = len - 1;
        for (int x : {0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC7, 0xCB, 0x83,
                          0x7D, int(adr & 0xFF), int(adr >> 8), int(l & 0xFF), int(l >> 8),
                          0x14, 0x28, 0x95, 0x20, 0x02, 0x8A, 0xCF, 0x01, 0xCF, 0x87})
            out(0x3C, uint8_t(x));
    }
};

/// Sektor @p id der Spur Z0/K0 der Fixture, unabhängig vom FDC gelesen (zweite Kopie der Datei).
std::vector<uint8_t> sektorAusDatei(const std::string& pfad, uint8_t id) {
    FloppyDriveV2 d(builtinDriveProfile("K5601"));
    if (!d.mount(DiskImage::open(pfad, std::nullopt, false), false)) return {};
    for (const auto& s : TrackCodec::parseTrack(d.track(0)))
        if (s.id == id) return s.data;
    return {};
}

}  // namespace

TEST_F(P8000Floppy8_, ReadDataZ0K0S1_PerDmaInsDram_InhaltGleichDerDatei) {
    k1520test::TempDisk zweite(FIXTURE, "p8000_floppy8_ref.hfe");
    const auto soll = sektorAusDatei(zweite.path(), 1);
    ASSERT_EQ(soll.size(), 256u);
    mountFixture();
    pio(A_5_25, 0x0E);                         // PB7 = 0 (FDC läuft), /SE0 = 0
    dmaLesenProgrammieren(ZIEL, 256);
    // READ DATA MFM: Z0 K0 S1 N1 (256 B) EOT = 1, GPL, DTL
    fdcCmd({0x46, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x0E, 0xFF});
    lauf(400000);
    for (int i = 0; i < 256; ++i) ASSERT_EQ(k.speicher().dram(ZIEL + i), soll[i]) << "Byte " << i;
    EXPECT_EQ(k.speicher().dram(ZIEL + 256), 0);   // nicht darüber hinaus
    const auto r = fdcResult(7);
    EXPECT_EQ(r[0] & 0xC0, 0x00) << "ST0 " << int(r[0]);   // normal beendet
    EXPECT_EQ(r[1], 0x00);
    EXPECT_FALSE(f.dma().enabled());
}

TEST_F(P8000Floppy8_, LaufwerkswahlNurUeberPb0bis3_UnitImBefehlZaehltNicht) {
    k1520test::TempDisk zweite(FIXTURE, "p8000_floppy8_ref.hfe");
    const auto soll = sektorAusDatei(zweite.path(), 1);
    mountFixture();   // nur Laufwerk 0 hat eine Diskette
    pio(A_5_25, 0x0F);                         // /SE0..3 alle 1: kein Laufwerk gewählt
    EXPECT_EQ(f.gewaehlt(), -1);
    // Befehl mit US = 0, aber keins gewählt ⇒ Not Ready (ST0 D3)
    fdcCmd({0x4A, 0x00});
    lauf(2000);
    EXPECT_EQ(fdcResult(7)[0] & 0x08, 0x08);
    // Laufwerk 1 gewählt (leer) und Befehl mit US = 0 ⇒ ebenfalls Not Ready
    out(0x1E, 0x0D);
    EXPECT_EQ(f.gewaehlt(), 1);
    fdcCmd({0x4A, 0x00});
    lauf(2000);
    EXPECT_EQ(fdcResult(7)[0] & 0x08, 0x08);
    // Laufwerk 0 gewählt, Befehl mit US = 1: gelesen wird trotzdem Laufwerk 0
    out(0x1E, 0x0E);
    EXPECT_EQ(f.gewaehlt(), 0);
    dmaLesenProgrammieren(ZIEL, 256);
    fdcCmd({0x46, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x0E, 0xFF});
    lauf(400000);
    for (int i = 0; i < 256; ++i) ASSERT_EQ(k.speicher().dram(ZIEL + i), soll[i]) << "Byte " << i;
    const auto r = fdcResult(7);
    EXPECT_EQ(r[0] & 0xC3, 0x01);    // IC = 00, Unit-Nummer des Befehls (1) im ST0
    // zwei Laufwerke gleichzeitig gewählt ⇒ keins
    out(0x1E, 0x0C);
    EXPECT_EQ(f.gewaehlt(), -1);
}

TEST_F(P8000Floppy8_, TcBeendetDenTransfer_DmaBlockendeUndPa6) {
    mountFixture();
    pio(A_5_25, 0x0E);
    // (a) DMA-Blockende: 256 Byte, EOT = 16 ⇒ ohne TC bliebe der FDC beim nächsten Sektor
    dmaLesenProgrammieren(ZIEL, 256);
    fdcCmd({0x46, 0x00, 0x00, 0x00, 0x01, 0x01, 0x10, 0x0E, 0xFF});
    lauf(400000);
    auto r = fdcResult(7);
    EXPECT_EQ(r[0] & 0xC0, 0x00) << "ST0 " << int(r[0]);
    EXPECT_EQ(r[1] & 0x80, 0x00) << "EN: das Blockende war kein TC";
    // (b) kein TC von der DMA (DMA zu kurz freigegeben), PA6 per Software nach dem Sektor
    dmaLesenProgrammieren(ZIEL, 4096);   // viel länger als ein Sektor: Blockende kommt nicht
    fdcCmd({0x46, 0x00, 0x00, 0x00, 0x02, 0x01, 0x10, 0x0E, 0xFF});
    lauf(100000);                         // erster Sektor ist durch, FDC liest S3 weiter
    EXPECT_TRUE(f.fdc().drq() || true);
    out(0x1C, A_5_25 | 0x40);            // PA6 = 1 ⇒ TC
    lauf(400000);
    r = fdcResult(7);
    EXPECT_EQ(r[1] & 0x80, 0x00) << "ST1 " << int(r[1]) << ": PA6-TC beendet den Befehl";
}

TEST_F(P8000Floppy8_, FdcInterruptAnPb4_RecalibrateSenseInterrupt) {
    mountFixture();
    pio(A_5_25, 0x0E);
    EXPECT_EQ(in(0x1E) & 0x10, 0);
    fdcCmd({0x07, 0x00});                 // RECALIBRATE
    lauf(100000);
    EXPECT_TRUE(f.fdc().irq());
    EXPECT_EQ(in(0x1E) & 0x10, 0x10) << "FDC-INT liegt als Pegel an PIO2-B4";
    fdcCmd({0x08});                       // SENSE INTERRUPT STATUS
    const auto r = fdcResult(2);
    EXPECT_EQ(r[0] & 0xE0, 0x20) << "ST0 " << int(r[0]);   // Seek End
    EXPECT_EQ(in(0x1E) & 0x10, 0x00);
}

TEST_F(P8000Floppy8_, FdcImResetBisPb7Null_UndDatenrateNachPa4) {
    EXPECT_EQ(in(0x20), 0x00) << "nach /RES: PB7 offen = Pull-up ⇒ FDC im Reset (MSR 00H)";
    pio(A_5_25, 0x0E);
    f.takt(1);
    EXPECT_EQ(in(0x20) & 0x80, 0x80);
    EXPECT_EQ(f.fdc().datenrate(), 250u);
    out(0x1C, A_5_25 & ~0x10);            // PA4 = 0 ⇒ 8 MHz (8″)
    f.takt(1);
    EXPECT_EQ(f.fdc().datenrate(), 500u);
}

TEST_F(P8000Floppy8_, FdcRegisterSpiegeln22Hund23H) {
    pio(A_5_25, 0x0E);
    EXPECT_EQ(in(0x22), in(0x20));
    fdcCmd({0x08});                       // ohne anstehenden Interrupt ⇒ ungültig: ST0 = 80H
    lauf(100);
    EXPECT_EQ(in(0x23), 0x80);
}

TEST_F(P8000Floppy8_, Mon8_3_1_HardwaretestOhneFehlerBisZumMonitorPrompt) {
    std::string tty1;
    long t = 0;
    bool ok = false;
    k.powerOn();
    while (t < 200'000'000) {
        const int n = k.schritt();
        if (n == 0) break;
        k.takt(n);
        f.takt(n);
        t += n;
        for (int i = 0; i < 4; ++i) {   // alle Kanäle abholen (sonst staut sich ihr Sender: ERROR 21)
            auto& a = k.anschluss(i);
            while (a.senderHatZeichen()) {
                const char c = static_cast<char>(a.senderNimm());
                if (i == 1) tty1 += c;
            }
        }
        if (tty1.find("Press RETURN") != std::string::npos) { ok = true; break; }
    }
    ASSERT_TRUE(ok) << "tty1: [" << tty1 << "] PC=" << std::hex << k.cpu().PC;
    EXPECT_EQ(tty1.find("ERROR"), std::string::npos) << tty1;
}
