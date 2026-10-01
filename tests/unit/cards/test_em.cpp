/**
 * @file test_em.cpp
 * @brief Unit-Tests: Erweiterungsmodul EM064/EM256, 8-Bit-Seite (core/cards/em/).
 *
 * Soll aus doc/design/17_a5120_16.md §7 (Schaltpläne 062-9005/9000) und dem
 * Original-BIOS `biosremc.mac`/`biosrem.mac`.  Je Tor/Register ein Fall, dazu die
 * `OUT (n),A`-Falle (A liegt auf AB8–15) mit einem echten Z80.
 */
#include <gtest/gtest.h>
#include "core/bus/k1520_bus.h"
#include "core/cards/em/em.h"
#include "core/cards/k3526/k3526.h"
#include "core/primitives/z80.h"

namespace {

constexpr uint16_t kPA = 0xA8, kPB = 0xA9, kPAc = 0xAA, kPBc = 0xAB;
constexpr uint16_t kSt8 = 0xAC, kVek = 0xAD, kSt16 = 0xAE, kAttr = 0xAF;

// PIO B wie biosremc.mac: b0..b6 Ausgabe, b7 Eingabe
constexpr uint8_t SG0 = 1, SG1 = 2, N_RAMEN = 4, N_STOP = 8, RESET16 = 0x10,
                  N_TRQ8 = 0x20, PR = 0x40;

struct Rig {
    K1520Bus bus;
    K3526    ops;
    EM       em;
    explicit Rig(EM::Config c = {}) : em(bus, c) {
        ops.attachToBus(bus);
        em.attachToBus();
        em.powerOn();
    }
    void pioInit() {
        bus.ioWrite(kPAc, 0xCF); bus.ioWrite(kPAc, 0xFF); bus.ioWrite(kPAc, 0x07);
        bus.ioWrite(kPBc, 0xCF); bus.ioWrite(kPBc, 0x80);
        // Wie `emina`: sofort RESET16 | /RAMEN nachschieben.  Zwischen Richtungs-
        // wort und erster Ausgabe treibt die PIO ihr Ausgaberegister (hier 00) —
        // RESET16 = 0 für einen Augenblick, auch am echten Gerät.
        portB(RESET16 | N_RAMEN);
    }
    void portB(uint8_t v) { bus.ioWrite(kPB, v); }
    /// Attribut schreiben wie `ramon`: OUT (C),A mit B = Seite·16.
    void attr(int page, uint8_t stored) { bus.ioWrite(uint16_t((page << 12) | kAttr), stored); }
};

}  // namespace

// ─── Grundstellung ───────────────────────────────────────────────────────────

/**
 * @test EM/NachNetzEin_8BitMode_KeineSeite
 * @brief Nach Netz-Ein ist die PIO hochohmig: RESET16 (R4:2) und /RAMEN (R4:1) sind
 *   über Pull-ups H ⇒ U8001 im Reset, 8/16 = 1, keine Seite eingeblendet.
 */
TEST(EM, NachNetzEin_8BitMode_KeineSeite) {
    Rig r;
    EXPECT_TRUE(r.em.reset16());
    EXPECT_TRUE(r.em.mode8());
    EXPECT_FALSE(r.em.ramEnabled());
    EXPECT_FALSE(r.em.tren());
    EXPECT_TRUE(r.em.ledV2());
    EXPECT_FALSE(r.em.ledV1());
    for (uint32_t a = 0; a < 0x10000; a += 0x1000)
        EXPECT_FALSE(r.em.drivesMemdi(uint16_t(a)));
}

/**
 * @test EM/PioA_ZeigtRegisterUndBetriebsart
 * @brief PIO Port A (alles Eingang): PIOA-0..2 = A33 = 0, /VI = 1, INT-16 = 0,
 *   N/S = L im Reset (am Gerät gemessen 2026-09-29: 48H), 8/16 = 1, TREN = 0 ⇒ 0x48.
 *   `ns_im_reset = true` bleibt als Gegenprobe (0x68).
 */
TEST(EM, PioA_ZeigtRegisterUndBetriebsart) {
    Rig r;
    r.pioInit();
    EXPECT_EQ(r.bus.ioRead(kPA), 0x48);
    EM::Config c; c.ns_im_reset = true;
    Rig r2(c);
    r2.pioInit();
    EXPECT_EQ(r2.bus.ioRead(kPA), 0x68);
}

// ─── E/A-Decoder ─────────────────────────────────────────────────────────────

/**
 * @test EM/Decoder_AchtToreAbModadr_AB8bis15Egal
 * @brief Die Karte belegt genau MODADR..MODADR+7; AB8–15 wertet der Decoder nicht aus.
 */
TEST(EM, Decoder_AchtToreAbModadr_AB8bis15Egal) {
    Rig r;
    r.pioInit();
    EXPECT_EQ(r.bus.ioRead(0x12A8), 0x48);        // AB8–15 beliebig
    EXPECT_EQ(r.bus.ioRead(0xA7), 0xFF);          // Nachbartore: niemand
    EXPECT_EQ(r.bus.ioRead(0xB0), 0xFF);

    K1520Bus bus;
    EM::Config c; c.modadr = 0xC8;
    EM em(bus, c);
    em.attachToBus();
    bus.ioWrite(0x30CF, 0x05);                    // A22 an C8H+7
    EXPECT_EQ(em.attribute(3), 0x05);
    EXPECT_EQ(bus.ioRead(0xA8), 0xFF);
}

/**
 * @test EM/PioTore_ReihenfolgeDatenVorSteuer
 * @brief MODADR+0 = A-Daten, +1 = B-Daten, +2 = A-Steuer, +3 = B-Steuer: das BIOS
 *   programmiert Port B über ABH und schreibt RESET16|/RAMEN an A9H.
 */
TEST(EM, PioTore_ReihenfolgeDatenVorSteuer) {
    Rig r;
    r.pioInit();
    r.portB(RESET16);                             // /RAMEN = 0 ⇒ RAMEN
    EXPECT_TRUE(r.em.ramEnabled());
    EXPECT_TRUE(r.em.ledV1());
    EXPECT_EQ(r.bus.ioRead(kPB) & 0x7F, RESET16);
}

// ─── Attributspeicher A22 ────────────────────────────────────────────────────

/**
 * @test EM/A22_SchreibenUndNegiertLesen_AdresseAusAB12bis15
 * @brief OUT (C),A an AFH: Seite = B>>4, gespeichert werden DB0–3; IN liefert sie
 *   negiert (74189), DB4–7 offen.  Gelesen wird die Seite aus AB12–15 des IN-Zyklus
 *   (A13 = transparenter Latch, s. em.h).
 */
TEST(EM, A22_SchreibenUndNegiertLesen_AdresseAusAB12bis15) {
    Rig r;
    for (int p = 0; p < 16; ++p) r.attr(p, uint8_t(p ^ 0x5));
    for (int p = 0; p < 16; ++p) {
        EXPECT_EQ(r.em.attribute(p), (p ^ 0x5) & 0xF);
        const uint8_t v = r.bus.ioRead(uint16_t((p << 12) | 0x0300 | kAttr));  // AB8–11 egal
        EXPECT_EQ(v, 0xF0 | (~(p ^ 0x5) & 0xF)) << "Seite " << p;
    }
    r.bus.ioWrite(0x20AF, 0xF3);                  // nur DB0–3 werden gespeichert
    EXPECT_EQ(r.em.attribute(2), 0x3);
}

/**
 * @test EM/A22_LesartLetzterSpeicherzugriff
 * @brief Die Handbuchlesart (Konfiguration) liest die Seite des letzten Speicherzugriffs.
 */
TEST(EM, A22_LesartLetzterSpeicherzugriff) {
    EM::Config c; c.lesart = EM::A22Lesart::LetzterSpeicherzugriff;
    Rig r(c);
    r.attr(9, 0x6);
    r.attr(1, 0xA);
    (void)r.bus.memRead(0x9123);
    EXPECT_EQ(r.bus.ioRead(0x10AF) & 0x0F, (~0x6) & 0xF);   // Seite 9, nicht B = 1
}

/**
 * @test EM/OutNA_LegtAaufAB8bis15
 * @brief Die Falle: `OUT (0AFH),A` adressiert die Seite A>>4 — mit echtem Z80.
 *   Gegenprobe `OUT (C),A` mit B = 30H.
 */
TEST(EM, OutNA_LegtAaufAB8bis15) {
    Rig r;
    Z80 cpu;
    cpu.readByte  = [&](uint16_t a) { return r.bus.memRead(a); };
    cpu.writeByte = [&](uint16_t a, uint8_t d) { r.bus.memWrite(a, d); };
    cpu.readPort  = [&](uint16_t p) { return r.bus.ioRead(p); };
    cpu.writePort = [&](uint16_t p, uint8_t d) { r.bus.ioWrite(p, d); };
    const uint8_t prog[] = {
        0x3E, 0x75,             // LD A,75H
        0xD3, 0xAF,             // OUT (0AFH),A   → Seite 7 ← 5
        0x01, 0xAF, 0x30,       // LD BC,30AFH
        0x3E, 0x06,             // LD A,6
        0xED, 0x79,             // OUT (C),A      → Seite 3 ← 6
        0x76                    // HALT
    };
    r.ops.load(0x0100, prog, sizeof prog);
    cpu.reset();
    cpu.PC = 0x0100;
    for (int i = 0; i < 5; ++i) cpu.step();
    EXPECT_EQ(r.em.attribute(7), 0x5);
    EXPECT_EQ(r.em.attribute(3), 0x6);
    EXPECT_EQ(r.em.attribute(0), 0xF);            // nicht die Seite aus dem Tor-Byte
}

// ─── Seitenabbildung ─────────────────────────────────────────────────────────

/**
 * @test EM/Seite_EinblendenMitMemdi_ZelleAusSegmentUndA15A14
 * @brief PEN = 1 ∧ RAMEN ∧ 8/16: die Karte zieht /MEMDI und bedient den Zugriff; Zelle
 *   = SG1·128K + SG0·64K + (A15-8, A14-8, AB13..0).  Der K3526 schreibt nicht mit.
 */
TEST(EM, Seite_EinblendenMitMemdi_ZelleAusSegmentUndA15A14) {
    Rig r;
    r.pioInit();
    r.bus.memWrite(0x4123, 0x11);                 // K1520-RAM vorher
    r.attr(4, 0x4);                               // PEN, WE, /A15-8 = 0 ⇒ A15-8 = 1, A14-8 = 0
    r.portB(RESET16 | SG1);                       // RAMEN, Segment 2
    EXPECT_TRUE(r.em.drivesMemdi(0x4123));
    EXPECT_FALSE(r.em.drivesMemdi(0x5123));
    r.bus.memWrite(0x4123, 0xAB);
    EXPECT_EQ(r.em.peek((2u << 16) | 0x8000 | 0x0123), 0xAB);
    EXPECT_EQ(r.bus.memRead(0x4123), 0xAB);
    EXPECT_EQ(r.ops.rawPtr()[0x4123], 0x11);      // K1520-RAM unberührt
    r.portB(RESET16 | SG1 | N_RAMEN);             // RAM aus ⇒ wieder K1520-RAM
    EXPECT_EQ(r.bus.memRead(0x4123), 0x11);
}

/**
 * @test EM/Seite_Schreibschutz_UnterdrueckNurWRI
 * @brief WE = 0 verhindert nur WRI: der Zyklus gehört trotzdem der Karte (/MEMDI),
 *   also landet der Schreibzugriff weder im EM noch im K1520-RAM.
 */
TEST(EM, Seite_Schreibschutz_UnterdrueckNurWRI) {
    Rig r;
    r.pioInit();
    r.bus.memWrite(0x6000, 0x22);
    r.attr(6, 0x2 | 0xC);                         // PEN, schreibgeschützt, A15/A14 = 0
    r.portB(RESET16);
    r.em.poke(0x2000, 0x33);                      // Segment 0, Zelle 2000H (AB13..0)
    EXPECT_EQ(r.bus.memRead(0x6000), 0x33);
    r.bus.memWrite(0x6000, 0x44);
    EXPECT_EQ(r.em.peek(0x2000), 0x33);
    EXPECT_EQ(r.ops.rawPtr()[0x6000], 0x22);
}

/**
 * @test EM/EM064_SegmenteGespiegelt
 * @brief EM064: SG0/SG1 gehen nicht in die Matrix — 64 KB in allen vier Segmenten.
 */
TEST(EM, EM064_SegmenteGespiegelt) {
    EM::Config c; c.variante = EM::Variante::EM064;
    Rig r(c);
    EXPECT_EQ(r.em.size(), 64u * 1024u);
    r.pioInit();
    r.attr(4, 0x0);                               // A15-8 = A14-8 = 1
    r.portB(RESET16);
    r.bus.memWrite(0x4010, 0x5A);
    r.portB(RESET16 | SG0 | SG1);
    EXPECT_EQ(r.bus.memRead(0x4010), 0x5A);
    EXPECT_EQ(r.em.peek(0xC010), 0x5A);
}

// ─── Betriebsartensteuerung ──────────────────────────────────────────────────

/**
 * @test EM/Reset16_Null_16BitMode_OhneU8001KeinRueckweg
 * @brief RESET16 = 0 setzt A29 sofort zurück (16-Bit-Mode): der U880 sieht keinen
 *   EM-Speicher mehr.  TRQ8 bewirkt ohne `MSET` nichts (µ0 = H ⇒ TREN = 0); zurück
 *   geht es nur über RESET16 = 1.
 */
TEST(EM, Reset16_Null_16BitMode_OhneU8001KeinRueckweg) {
    Rig r;
    r.pioInit();
    r.attr(4, 0x0);
    r.portB(RESET16);
    EXPECT_TRUE(r.em.drivesMemdi(0x4000));
    r.portB(0);                                   // RESET16 = 0, /TRQ8 = 0 (TRQ8 aktiv)
    EXPECT_FALSE(r.em.mode8());
    EXPECT_TRUE(r.em.trq8());
    EXPECT_FALSE(r.em.tren());
    EXPECT_FALSE(r.em.drivesMemdi(0x4000));
    EXPECT_EQ(r.bus.ioRead(kPA) & 0xC0, 0x00);    // 8/16 = 0, TREN = 0
    r.portB(N_TRQ8);
    EXPECT_FALSE(r.em.mode8());
    r.portB(RESET16 | N_TRQ8);
    EXPECT_TRUE(r.em.mode8());
    EXPECT_TRUE(r.em.drivesMemdi(0x4000));
}

// ─── Register A34/A35/A36 ────────────────────────────────────────────────────

/**
 * @test EM/Vektor8_SetztVi_Reset16LoeschtA34
 * @brief ADH schreibt A34 ⇒ INT → PIO A3 (/VI) = 0.  Solange RESET16 anliegt, ist A34
 *   gelöscht (X15), und RESET16 = 1 nimmt die Anforderung zurück.
 */
TEST(EM, Vektor8_SetztVi_Reset16LoeschtA34) {
    Rig r;
    r.pioInit();
    r.bus.ioWrite(kVek, 0x42);                    // unter RESET16: wirkungslos
    EXPECT_FALSE(r.em.viPending());
    r.portB(N_RAMEN | N_TRQ8);                    // RESET16 = 0
    r.bus.ioWrite(kVek, 0x42);
    EXPECT_TRUE(r.em.viPending());
    EXPECT_EQ(r.em.vector8(), 0x42);
    EXPECT_EQ(r.bus.ioRead(kPA) & 0x08, 0x00);
    r.portB(RESET16 | N_RAMEN | N_TRQ8);
    EXPECT_FALSE(r.em.viPending());
    EXPECT_EQ(r.bus.ioRead(kPA) & 0x08, 0x08);
}

/**
 * @test EM/Status8Und16
 * @brief ACH schreibt A36 (nur für den U8001 sichtbar, nicht rücklesbar); AEH liest A35.
 */
TEST(EM, Status8Und16) {
    Rig r;
    r.bus.ioWrite(kSt8, 0x9C);
    EXPECT_EQ(r.em.status8(), 0x9C);
    EXPECT_EQ(r.bus.ioRead(kSt8), 0xFF);
    EXPECT_EQ(r.bus.ioRead(kVek), 0xFF);
    EXPECT_EQ(r.bus.ioRead(kSt16), r.em.status16());
}

// ─── Parität ─────────────────────────────────────────────────────────────────

/**
 * @test EM/Paritaet_Testhaken_PeUndPr_Interrupt
 * @brief Der Testhaken setzt das PER-FF ⇒ /PE (B7) = 0 und bei scharfer B7-Maske ein
 *   PIO-Interrupt; PR = 1 setzt das FF zurück (und hält es zurückgesetzt).
 */
TEST(EM, Paritaet_Testhaken_PeUndPr_Interrupt) {
    Rig r;
    r.pioInit();
    r.bus.ioWrite(kPBc, 0x20);                    // Vektor
    r.bus.ioWrite(kPBc, 0xCF); r.bus.ioWrite(kPBc, 0x80);
    r.bus.ioWrite(kPBc, 0x97); r.bus.ioWrite(kPBc, 0x7F);   // nur B7, aktiv LOW
    r.portB(RESET16 | PR);
    r.em.injectParityError();
    EXPECT_FALSE(r.em.parityError());             // PR hält das FF zurück
    r.portB(RESET16);
    r.em.injectParityError();
    EXPECT_TRUE(r.em.parityError());
    EXPECT_EQ(r.bus.ioRead(kPB) & 0x80, 0x00);
    r.em.setIEI(true);
    EXPECT_TRUE(r.em.hasInterrupt());
    EXPECT_EQ(r.em.getVector(), 0x20);
    r.portB(RESET16 | PR);
    EXPECT_FALSE(r.em.parityError());
    EXPECT_EQ(r.bus.ioRead(kPB) & 0x80, 0x80);
}

// ─── Reset ───────────────────────────────────────────────────────────────────

/**
 * @test EM/SystemReset_PioHochohmig_DramUndA22Bleiben
 * @brief /RESET: PIO zurück ⇒ Pull-ups ⇒ RESET16, /RAMEN; DRAM und Attributspeicher
 *   behalten ihren Inhalt (RAM-Floppy übersteht die Reset-Taste).
 */
TEST(EM, SystemReset_PioHochohmig_DramUndA22Bleiben) {
    Rig r;
    r.pioInit();
    r.attr(4, 0x0);
    r.portB(N_TRQ8);                              // 16-Bit-Mode
    r.em.poke(0x1234, 0x77);
    r.em.reset();
    EXPECT_TRUE(r.em.reset16());
    EXPECT_TRUE(r.em.mode8());
    EXPECT_FALSE(r.em.ramEnabled());
    EXPECT_EQ(r.em.attribute(4), 0x0);
    EXPECT_EQ(r.em.peek(0x1234), 0x77);
}
