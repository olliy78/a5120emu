/**
 * @test Z80Dma.* — UA858 (doc/design/21_pc1715.md AP-W2a): die Programmierfolgen von S550, Lader,
 *       BIOS und INIT.COM (doc/pc1715/pc1715w_hardware.md §4.5) gegen simulierten Speicher/FDC.
 */

#include <gtest/gtest.h>
#include "core/primitives/z80_dma.h"
#include <vector>

namespace {

struct Rig {
    Z80Dma dma;
    std::vector<uint8_t> ram = std::vector<uint8_t>(65536, 0);
    std::vector<uint8_t> fdc;       ///< Bytes, die der FDC liefert (Port 41H lesen)
    size_t fdcPos = 0;
    std::vector<uint8_t> fdcGot;    ///< Bytes, die in Port 41H geschrieben wurden
    std::vector<int> ports;         ///< benutzte Port-Adressen
    std::vector<bool> tc;
    Rig() {
        dma.memRead  = [this](uint16_t a) { return ram[a]; };
        dma.memWrite = [this](uint16_t a, uint8_t d) { ram[a] = d; };
        dma.portRead  = [this](uint16_t a) { ports.push_back(a); return fdcPos < fdc.size() ? fdc[fdcPos++] : uint8_t(0xFF); };
        dma.portWrite = [this](uint16_t a, uint8_t d) { ports.push_back(a); fdcGot.push_back(d); };
        dma.blockEnde = [this](bool l) { tc.push_back(l); };
    }
    void w(std::initializer_list<uint8_t> b) { for (uint8_t x : b) dma.ioWrite(0, x); }
    /// Gemeinsamer Floppy-Programmkopf; @p wr0 = 7D (Lesen) | 79 (Schreiben).
    void floppy(uint8_t wr0, uint16_t adr, uint16_t len1, uint8_t richtung) {
        w({0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC7, 0xCB, 0x83});
        w({wr0, uint8_t(adr), uint8_t(adr >> 8), uint8_t(len1 - 1), uint8_t((len1 - 1) >> 8)});
        w({0x14, 0x28, 0x95, 0x41, 0x12, 0x14, 0x82, 0xCF, richtung, 0xCF, 0xAB, 0x87});
    }
    /// DRQ-getriebener Lauf: je Byte DRQ hoch (RDY low-aktiv → Pegel 0), step, DRQ runter.
    int lauf(int max = 100000) {
        int n = 0;
        while (n < max) {
            dma.setReady(false);                // /RDY = 0 = aktiv
            if (!dma.busRequest()) break;
            if (dma.step() == 0) break;
            dma.setReady(true);
            dma.cpuZyklus();                    // Byte-Betrieb: die CPU kommt zwischen zwei Bytes dran
            ++n;
        }
        return n;
    }
};

TEST(Z80Dma, FloppyLesenFdcNachSpeicher) {
    Rig r;
    for (int i = 0; i < 300; i++) r.fdc.push_back(uint8_t(i));
    r.floppy(0x7D, 0x2000, 256, 0x01);        // B->A: FDC -> Speicher
    EXPECT_EQ(r.lauf(), 256);                 // Länge-1 programmiert, 256 Bytes
    for (int i = 0; i < 256; i++) ASSERT_EQ(r.ram[0x2000 + i], uint8_t(i)) << i;
    EXPECT_EQ(r.ram[0x2100], 0);
    EXPECT_EQ(r.fdcPos, 256u);
    for (int p : r.ports) EXPECT_EQ(p, 0x41);
    EXPECT_EQ(r.dma.adresseA(), 0x2100);
    EXPECT_FALSE(r.dma.enabled());
    EXPECT_EQ(r.dma.status() & 0x20, 0);      // Blockende erreicht
    EXPECT_TRUE(r.dma.blockEndeErreicht());
    ASSERT_EQ(r.tc.size(), 1u);
    EXPECT_TRUE(r.tc[0]);
}

TEST(Z80Dma, LaengePlusEinsByte) {
    Rig r;
    r.fdc.assign(10, 0x55);
    r.floppy(0x7D, 0x1000, 2, 0x01);          // ll = 1 → 2 Bytes
    EXPECT_EQ(r.lauf(), 2);
    r.dma.setReady(false);
    EXPECT_EQ(r.dma.step(), 0);               // gestoppt
    EXPECT_EQ(r.ram[0x1001], 0x55);
    EXPECT_EQ(r.ram[0x1002], 0);
}

TEST(Z80Dma, FloppySchreibenSpeicherNachFdc) {
    Rig r;
    for (int i = 0; i < 16; i++) r.ram[0x3000 + i] = uint8_t(0xA0 + i);
    r.floppy(0x79, 0x3000, 16, 0x05);         // A->B: Speicher -> FDC
    EXPECT_EQ(r.lauf(), 16);
    ASSERT_EQ(r.fdcGot.size(), 16u);
    for (int i = 0; i < 16; i++) EXPECT_EQ(r.fdcGot[i], uint8_t(0xA0 + i));
    EXPECT_EQ(r.dma.adresseA(), 0x3010);
}

TEST(Z80Dma, DoppelLoadMitRichtungswechselVersorgtBeidePorts) {
    Rig r;
    r.floppy(0x7D, 0x2000, 4, 0x01);
    // vor Enable: A-Zähler = A-Start (1. LOAD, A war Quelle), B-Zähler = 41H (2. LOAD, B Quelle)
    EXPECT_EQ(r.dma.adresseA(), 0x2000);
    EXPECT_EQ(r.dma.adresseB(), 0x0041);
}

TEST(Z80Dma, InterruptUndVektorBeiBlockende) {
    Rig r;
    r.fdc.assign(8, 1);
    r.dma.setIEI(true);
    r.floppy(0x7D, 0x2000, 4, 0x01);
    EXPECT_FALSE(r.dma.hasInterrupt());
    r.lauf();
    ASSERT_TRUE(r.dma.hasInterrupt());
    EXPECT_FALSE(r.dma.getIEO());
    EXPECT_EQ(r.dma.getVector(), 0x14);
    EXPECT_FALSE(r.dma.hasInterrupt());
    EXPECT_FALSE(r.dma.getIEO());             // unter Bedienung
    r.dma.onRETI();
    EXPECT_TRUE(r.dma.getIEO());
}

TEST(Z80Dma, KeinInterruptOhneEnableInterrupts) {
    Rig r;
    r.fdc.assign(8, 1);
    r.dma.setIEI(true);
    r.w({0xC3, 0x7D, 0x00, 0x20, 0x03, 0x00, 0x14, 0x28, 0x95, 0x41, 0x12, 0x14, 0x82, 0xCF, 0x01, 0xCF, 0x87});
    r.lauf();
    EXPECT_FALSE(r.dma.hasInterrupt());
    EXPECT_TRUE(r.dma.blockEndeErreicht());
}

TEST(Z80Dma, ISRResetUndDisableInterrupts) {
    Rig r;
    r.fdc.assign(8, 1);
    r.dma.setIEI(true);
    r.floppy(0x7D, 0x2000, 4, 0x01);
    r.lauf();
    ASSERT_TRUE(r.dma.hasInterrupt());
    r.w({0xA3});
    EXPECT_FALSE(r.dma.hasInterrupt());
    EXPECT_FALSE(r.dma.blockEndeErreicht());
    EXPECT_FALSE(r.tc.back());
}

TEST(Z80Dma, StatusLesenBBundSiebenMalIN) {
    Rig r;
    r.fdc.assign(8, 1);
    r.floppy(0x7D, 0x2000, 4, 0x01);
    r.lauf();
    r.w({0xBB, 0x7F});
    uint8_t st = r.dma.ioRead(0);
    EXPECT_EQ(st & 0x20, 0);
    EXPECT_EQ(st & 0x01, 1);
    EXPECT_EQ(r.dma.ioRead(0), 4);            // Zähler L = übertragene Bytes
    EXPECT_EQ(r.dma.ioRead(0), 0);
    EXPECT_EQ(r.dma.ioRead(0), 0x04);         // Adresse A = 2004H
    EXPECT_EQ(r.dma.ioRead(0), 0x20);
    EXPECT_EQ(r.dma.ioRead(0), 0x41);         // Adresse B fest
    EXPECT_EQ(r.dma.ioRead(0), 0x00);
    EXPECT_EQ(r.dma.ioRead(0), st);           // Umlauf
    r.w({0xBF});
    EXPECT_EQ(r.dma.ioRead(0), st);
}

TEST(Z80Dma, FrischNachResetBlockendeNichtErreicht) {
    Rig r;
    r.w({0xC3});
    r.w({0xBB, 0x01});
    EXPECT_NE(r.dma.ioRead(0) & 0x20, 0);
}

TEST(Z80Dma, PruefLesenContinueReinitStatus) {
    Rig r;
    r.fdc.assign(64, 7);
    r.dma.setIEI(true);
    r.floppy(0x7D, 0x2000, 4, 0x01);
    r.lauf();
    r.dma.getVector(); r.dma.onRETI();
    r.w({0xD3, 0x8B, 0xAB, 0x87});           // Continue, Reinit Status, Enable Int, Enable DMA
    EXPECT_NE(r.dma.status() & 0x20, 0);
    EXPECT_FALSE(r.dma.blockEndeErreicht());
    EXPECT_EQ(r.dma.bytezaehler(), 0);
    EXPECT_EQ(r.lauf(), 4);                  // gleiche Länge noch einmal
    EXPECT_EQ(r.dma.adresseA(), 0x2008);     // Adresse läuft weiter
    EXPECT_TRUE(r.dma.hasInterrupt());
}

TEST(Z80Dma, ForceReadyOhneRdy) {
    Rig r;
    for (int i = 0; i < 4; i++) r.ram[0x0600 + i] = uint8_t(i + 1);
    // Blindlauf in der Form von Lader/BIOS/INIT (mit 01 CF vor B3): Speicher fest -> 40H, Force Ready.
    // Die S550-Form ohne `01 CF` lädt nur A; Port B bliebe nach Datenblatt bei 0 [?].
    // P9c: die Richtung (05) muss VOR 87 stehen — jedes Steuerbyte sperrt die DMA (Datenblatt).
    r.w({0xC3, 0x7D, 0xBA, 0x06, 0x01, 0x00, 0x24, 0x28, 0x80, 0x85, 0x40, 0x82, 0xCF, 0x01, 0xCF, 0x05, 0xB3, 0x87});
    r.dma.setReady(true);                    // RDY inaktiv (low-aktiv)
    EXPECT_TRUE(r.dma.busRequest());
    EXPECT_EQ(r.dma.step(), 8);
    // P9c: im Byte-Betrieb setzt die Busfreigabe nach dem Byte Force Ready zurück (UA858 §10.1
    // B3: „so erfolgt lediglich die Übertragung eines Bytes“) — das zweite Byte wartet auf RDY.
    r.dma.cpuZyklus();
    EXPECT_FALSE(r.dma.busRequest());
    EXPECT_EQ(r.dma.step(), 0);
    ASSERT_EQ(r.fdcGot.size(), 1u);
    EXPECT_EQ(r.ports[0], 0x40);
    EXPECT_EQ(r.dma.adresseA(), 0x06BA);     // A fest
}

TEST(Z80Dma, RdyPolaritaet) {
    Rig r;
    r.w({0xC3, 0x7D, 0x00, 0x10, 0x00, 0x00, 0x14, 0x10, 0x80, 0xAD, 0x00, 0x20, 0x82, 0xCF, 0x05, 0xCF, 0x87});
    r.dma.setReady(true);
    EXPECT_FALSE(r.dma.busRequest());        // WR5 82: low-aktiv, Pegel 1 = nicht bereit
    r.dma.setReady(false);
    EXPECT_TRUE(r.dma.busRequest());
    r.w({0x8A, 0x87});                       // WR5 mit D3 = 1: aktiv hoch (jedes Steuerbyte sperrt → 87)
    r.dma.setReady(false);
    EXPECT_FALSE(r.dma.busRequest());
    r.dma.setReady(true);
    EXPECT_TRUE(r.dma.busRequest());
}

TEST(Z80Dma, SpeicherNachSpeicherContinuous) {
    Rig r;
    for (int i = 0; i < 0x40; i++) r.ram[0x4000 + i] = uint8_t(i ^ 0x5A);
    // BIOS-Interbank-MOVE: C3 C7 CB 83 79 A len-1 14 10 80 AD B 82 CF 05 CF B3 87
    r.w({0xC3, 0xC7, 0xCB, 0x83, 0x79, 0x00, 0x40, 0x3F, 0x00, 0x14, 0x10, 0x80, 0xAD, 0x00, 0x50,
         0x82, 0xCF, 0x05, 0xCF, 0xB3, 0x87});
    r.dma.setReady(true);                    // RDY egal (Force Ready)
    int n = 0, takte = 0;
    while (r.dma.busRequest()) { takte += r.dma.step(); ++n; }
    EXPECT_EQ(n, 0x40);
    EXPECT_EQ(takte, 0x40 * 8);
    for (int i = 0; i < 0x40; i++) ASSERT_EQ(r.ram[0x5000 + i], uint8_t(i ^ 0x5A));
    EXPECT_EQ(r.ram[0x5040], 0);
    EXPECT_EQ(r.dma.adresseA(), 0x4040);
    EXPECT_EQ(r.dma.adresseB(), 0x5040);
}

TEST(Z80Dma, DekrementUndAutoRestart) {
    Rig r;
    for (int i = 0; i < 4; i++) r.ram[0x100 + i] = uint8_t(i + 1);
    // A: Speicher dekrementierend ab 0103H, B: Speicher inkrementierend ab 0200H, Auto-Restart
    r.w({0xC3, 0x79, 0x03, 0x01, 0x03, 0x00, 0x04, 0x10, 0x80, 0xAD, 0x00, 0x02, 0xA2, 0xCF, 0x05, 0xCF, 0xB3, 0x87});
    for (int i = 0; i < 4; i++) r.dma.step();
    EXPECT_EQ(r.ram[0x200], 4); EXPECT_EQ(r.ram[0x203], 1);
    // Auto-Restart: läuft von vorn weiter
    EXPECT_TRUE(r.dma.enabled());
    EXPECT_EQ(r.dma.adresseA(), 0x0103);
    EXPECT_EQ(r.dma.adresseB(), 0x0200);
}

TEST(Z80Dma, FolgebytesNachWR4MitIntCtlPulseVektor) {
    Rig r;
    r.fdc.assign(4, 3);
    r.dma.setIEI(true);
    // Interrupt-Steuerbyte 1AH: Blockende + Pulse-Byte (33H) + Vektor (66H) folgen;
    // ein falsch einsortiertes Folgebyte würde als Befehl laufen und das Ergebnis verfälschen
    r.w({0xC3, 0x7D, 0x00, 0x20, 0x01, 0x00, 0x14, 0x28, 0x95, 0x41, 0x1A, 0x33, 0x66,
         0x82, 0xCF, 0x01, 0xCF, 0xAB, 0x87});
    r.lauf();
    EXPECT_EQ(r.ram[0x2000], 3);
    EXPECT_EQ(r.ram[0x2001], 3);
    ASSERT_TRUE(r.dma.hasInterrupt());
    EXPECT_EQ(r.dma.getVector(), 0x66);
}

// P9c: B7 gibt NICHT frei, sondern hält die Busanforderung bis zum RETI zurück (Zilog S. 53:
// ISR gibt B7, 87, RETI).  Vorher gab B7 + RETI frei — kein Gastsystem benutzt B7.
TEST(Z80Dma, EnableNachRETI) {
    Rig r;
    r.dma.setIEI(true);
    r.w({0xC3, 0x82, 0xB7});
    r.dma.setReady(false);                   // RDY aktiv
    EXPECT_FALSE(r.dma.enabled());
    r.w({0x87});
    EXPECT_TRUE(r.dma.enabled());
    EXPECT_FALSE(r.dma.busRequest());
    r.dma.onRETI();
    EXPECT_TRUE(r.dma.busRequest());
}

TEST(Z80Dma, SaveStateRundreise) {
    Rig r;
    r.fdc.assign(64, 9);
    r.dma.setIEI(true);
    r.floppy(0x7D, 0x2000, 8, 0x01);
    for (int i = 0; i < 3; i++) { r.dma.setReady(false); r.dma.step(); r.dma.cpuZyklus(); }
    std::vector<uint8_t> buf;
    r.dma.serialize(buf);

    Rig q;
    q.fdc.assign(64, 9);
    const uint8_t* p = buf.data();
    ASSERT_TRUE(q.dma.deserialize(p, buf.data() + buf.size()));
    EXPECT_EQ(p, buf.data() + buf.size());
    EXPECT_EQ(q.dma.adresseA(), 0x2003);
    EXPECT_EQ(q.dma.bytezaehler(), 3);
    EXPECT_EQ(q.lauf(), 5);                  // Rest des Blocks
    EXPECT_TRUE(q.dma.hasInterrupt());
    EXPECT_EQ(q.dma.getVector(), 0x14);

    const uint8_t* kurz = buf.data();
    EXPECT_FALSE(q.dma.deserialize(kurz, buf.data() + 10));
}

} // namespace

/**
 * @test Statusbyte D1/D3 sind Leitungszustände (AP-W3, Datenblatt RR0: D1 = 0 „RDY aktiv",
 *       D3 = 0 „Interrupt anstehend").  S550 0539H prüft nach dem Lesen D1: steht RDY noch
 *       (= der U8272 will weitere Bytes), gilt der Auftrag als gescheitert.  Vorher stand D1
 *       fest auf 0 — der Urlader verwarf jeden gelesenen Sektor und versuchte es endlos neu.
 */
TEST(Z80Dma, StatusD1FolgtRdyD3FolgtInterrupt) {
    Rig r;
    r.fdc.assign(4, 0x55);
    r.dma.setIEI(true);
    r.floppy(0x7D, 0x2000, 4, 0x01);
    r.lauf();
    r.dma.setReady(true);                     // DRQ weg → /RDY = 1 = inaktiv (WR5 82H)
    r.w({0xBB, 0x01});
    uint8_t st = r.dma.ioRead(0);
    EXPECT_EQ(st & 0x20, 0);                  // Blockende
    EXPECT_NE(st & 0x02, 0);                  // RDY inaktiv
    EXPECT_EQ(st & 0x08, 0);                  // Interrupt steht an
    r.dma.setReady(false);                    // RDY aktiv
    EXPECT_EQ(r.dma.ioRead(0) & 0x02, 0);
    r.dma.getVector();                        // Quittung
    EXPECT_NE(r.dma.ioRead(0) & 0x08, 0);
}
