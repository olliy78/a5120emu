/**
 * @file test_mc80_2x.cpp
 * @brief MC80.21/.22 (AP M11): das Betriebssystem (ROM-Satz ver2, bsys_2x.prn) meldet sich über
 *        die Displaylogik mit dem Menü, nimmt Tasten über die Matrix an und speichert/lädt ein
 *        Programm über die MBI auf die Kassette.
 *
 * Erwartungen aus doc/mc80/bsys_2x.prn: BSK (04ACH) schreibt Menütabelle 1 rückwärts ab 05B5H
 * („0270  TASTENTEST“, „049D  INIT“, „0600  BAND“), danach Tabelle 2 (3FFFH = 00H → Editor …),
 * und setzt „>“ in Spalte 5 der gewählten Zeile (PRE 053EH).  Bandmenü MBM (2000H): „MAGNETBAND :“,
 * „SCHREIBEN“/„LESEN“/„LOESCHEN“; Parameter „AB BLOCK-NR.:“, „ADRESSE VON .... BIS ....“, „NAME :“.
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "core/machines/mc80/mc80_2x.h"
#include "core/peripherals/kassette/kassetten_medium.h"

namespace {

using V = Mc80_2xMachine::Config::Variante;

std::string bildText(const Mc80_2xMachine& m)
{
    std::string s;
    for (int z = 0; z < 8; ++z) s += std::to_string(z) + ": |" + m.textZeile(z) + "|\n";
    return s;
}

int findeZeile(const Mc80_2xMachine& m, const std::string& teil)
{
    for (int z = 0; z < 8; ++z)
        if (m.textZeile(z).find(teil) != std::string::npos) return z;
    return -1;
}

struct Bsys {
    std::unique_ptr<Mc80_2xMachine> m;
    explicit Bsys(V v = V::Mc8022) {
        Mc80_2xMachine::Config c;
        c.variante = v;
        m = std::make_unique<Mc80_2xMachine>(c);
    }
    void fahre(uint64_t takte) {
        const uint64_t ziel = m->totalCycles() + takte;
        while (m->totalCycles() < ziel) m->run(5000);
    }
    /// Läuft, bis @p teil im Bild steht (höchstens @p max Takte).  @return Zeile oder −1.
    int bisText(const std::string& teil, uint64_t max) {
        const uint64_t ziel = m->totalCycles() + max;
        while (m->totalCycles() < ziel) {
            m->run(50'000);
            if (const int z = findeZeile(*m, teil); z >= 0) return z;
        }
        return -1;
    }
    /// Tasten abgeben, dann @p nach Takte weiter.
    void tasten(uint64_t nach = 500'000) {
        while (!m->tastenLeer()) m->run(5000);
        fahre(nach);
    }
    void tippe(const std::string& s, uint64_t nach = 500'000) {
        for (char c : s) EXPECT_TRUE(m->zeichenEinreihen(c)) << "keine Taste für '" << c << "'";
        tasten(nach);
    }
    void taste(int nr, uint64_t nach = 500'000) {
        m->tasteEinreihen(nr);
        tasten(nach);
    }
};

constexpr int T_ENTER = 0x30, T_RUNTER = 0x33, T_OFF = 0x32;

}  // namespace

TEST(Mc80_2x, BsysMeldetSich)
{
    for (V v : {V::Mc8021, V::Mc8022}) {
        Bsys b(v);
        const int z = b.bisText("TASTENTEST", 20'000'000);
        ASSERT_EQ(z, 0) << Mc80_2xMachine::variantenName(v) << "\n" << bildText(*b.m);
        b.fahre(1'000'000);
        // PRE: Auswahlzeiger „>“ in Spalte 5 der ersten Zeile, Adresse in Spalte 0–3; auf dem
        // Zeiger blinkt der Kursor (Code 1FH = „_“, TST 02A0H).
        std::string z0 = b.m->textZeile(0);
        ASSERT_GE(z0.size(), 6u) << bildText(*b.m);
        EXPECT_TRUE(z0[5] == '>' || z0[5] == '_') << bildText(*b.m);
        z0[5] = '>';
        EXPECT_EQ(z0, "0270 >TASTENTEST") << bildText(*b.m);
        EXPECT_EQ(b.m->textZeile(1), "049D  INIT") << bildText(*b.m);
        EXPECT_EQ(b.m->textZeile(2), "0600  BAND") << bildText(*b.m);
        // Menütabelle 2 (3FFFH = 00H) und 3 (5FFFH = 00H) folgen: Editor-ROMs im PFS.
        EXPECT_EQ(b.m->textZeile(3), "3000  EDITOR") << bildText(*b.m);
        EXPECT_EQ(b.m->textZeile(6), "4000  BASIC-80.2") << bildText(*b.m);
        // Dieselben Zeichen per Mustervergleich aus dem Bitmap (Ersatz-U402 [?]).
        for (int s = 0; s < 32; ++s)
            EXPECT_EQ(b.m->dpl().zeichenAusBild(1, s), b.m->screenChar(s, 1)) << "Spalte " << s;
        EXPECT_EQ(b.m->dpl().zeichenAusBild(0, 6), 'T');
        // Die DPL zeichnet aus den PIO-Ausgängen, nicht aus dem BWS — beide stimmen überein.
        for (int s = 0; s < 32; ++s) {
            const uint8_t bws = b.m->memReadDebug(static_cast<uint16_t>(0x0C00 + s));
            EXPECT_EQ(b.m->dpl().zelle(0, s), bws & 0x3F) << "Spalte " << s;
        }
    }
}

TEST(Mc80_2x, SpeicherUndTore)
{
    // hardware_2x.md §3/§4: ROM ver2 auf der K2521, PFS ab 2000H (2400H leer), OPS ab C000H.
    Bsys b;
    auto& m = *b.m;
    EXPECT_EQ(m.memReadDebug(0x0000), 0x31);           // LD SP,1000H
    EXPECT_EQ(m.memReadDebug(0x0600), 0xCD);           // BAND: CALL MBM (ver2)
    EXPECT_EQ(m.memReadDebug(0x2000), 0xC5);           // MBM: PUSH BC (mo01)
    EXPECT_EQ(m.memReadDebug(0x2400), 0xFF);           // Sockel 1 leer („frei“)
    EXPECT_EQ(m.memReadDebug(0x3FFF), 0x00);           // Menütabelle 2 vorhanden (mo08)
    EXPECT_EQ(m.memReadDebug(0x5FFF), 0x00);           // Menütabelle 3 vorhanden (mo16)
    EXPECT_EQ(m.memReadDebug(0x6000), 0xFF);           // kein Prozessprogramm → BSK
    m.memWriteDebug(0x2000, 0x00);                     // EPROM: Schreiben wirkungslos
    EXPECT_EQ(m.memReadDebug(0x2000), 0xC5);
    m.memWriteDebug(0xC123, 0x5A);
    EXPECT_EQ(m.memReadDebug(0xC123), 0x5A);
    m.memWriteDebug(0x0C10, 0x12);                     // BWS im ZRE-RAM
    EXPECT_EQ(m.memReadDebug(0x0C10), 0x12);
    m.memWriteDebug(0x1000, 0x12);                     // 1000H–1FFFH unbelegt
    EXPECT_EQ(m.memReadDebug(0x1000), 0xFF);
    // Tore: ZRE 80H–87H, DPL F0H–F7H, MBI EFH/F8H–FFH, Senken 00H–0FH und E0H–E8H.
    EXPECT_EQ(m.bus().ioOwner(0x80), &m.zre());
    EXPECT_EQ(m.bus().ioOwner(0xF4), &m.dpl());
    EXPECT_EQ(m.bus().ioOwner(0xEF), &m.mbi());
    EXPECT_EQ(m.bus().ioOwner(0xFF), &m.mbi());
    EXPECT_EQ(m.bus().ioOwner(0x01), &m.systemtore());
    EXPECT_EQ(m.bus().ioOwner(0xE8), &m.ppe());
    EXPECT_EQ(m.bus().ioOwner(0xE9), nullptr);
    // BSYS gibt beim Start die RAM-Schreibsperre frei (RSP: 60 × OUT 01H) [BS Bl. 13].
    ASSERT_GE(b.bisText("BAND", 20'000'000), 0);
    EXPECT_EQ(m.systemtore().schreibzugriffe(0x01), 60u);
    EXPECT_EQ(m.systemtore().schreibzugriffe(0x02), 0u);
    // Kassette: Laufwerk 0 ist das SK 1200 der MBI; K6022 kollidiert mit der PPE auf E0H.
    EXPECT_EQ(m.kassettenLaufwerk(0), &m.mbi().laufwerk());
    EXPECT_EQ(m.kassettenLaufwerk(1), nullptr);
    EXPECT_EQ(m.machineType(), 6);
    EXPECT_EQ(m.fbWidth(), 528);
    EXPECT_EQ(m.fbHeight(), 256);
}

TEST(Mc80_2x, ZusatzkartenNachDerGrundregel)
{
    Bsys b;
    EXPECT_FALSE(b.m->installK6022());                 // E0H belegt (PPE)
    EXPECT_NE(b.m->k6022Fehler().find("E0H"), std::string::npos) << b.m->k6022Fehler();
    EXPECT_TRUE(b.m->installRaf(RAF::Typ::RAF512));    // 88H/89H: nicht nachgebildet → frei [?]
}

TEST(Mc80_2x, BildwechselEtwa50Hertz)
{
    // Zeilenautomat (dpl.h): 8 Zeilen à 33 · 158,5 + 832 Takte = 19,7 ms → ≈ 50,7 Hz
    // („FREQUENZ CA. 50 HERTZ“ im Listing [BS Bl. 4]).
    Bsys b;
    ASSERT_GE(b.bisText("BAND", 20'000'000), 0);
    const uint64_t n0 = b.m->dpl().bahnen();
    b.fahre(Mc80_2xMachine::CPU_HZ);          // 1 s
    const double hz = static_cast<double>(b.m->dpl().bahnen() - n0) / 8.0;
    EXPECT_NEAR(hz, 50.7, 1.0);
}

TEST(Mc80_2x, TastaturMatrixFuehrtMenueAus)
{
    // Kursor zweimal hinunter, ENTER: das Menü springt auf die Adresse der Zeile (PRE/HDL) —
    // „0600  BAND“ → Magnetbandmenü MBM (2000H).
    Bsys b;
    ASSERT_EQ(b.bisText("TASTENTEST", 20'000'000), 0);
    b.taste(T_RUNTER);
    b.taste(T_RUNTER);
    b.taste(T_ENTER, 2'000'000);
    EXPECT_EQ(findeZeile(*b.m, "MAGNETBAND :"), 0) << bildText(*b.m);
    EXPECT_GE(findeZeile(*b.m, "SCHREIBEN"), 1) << bildText(*b.m);
    EXPECT_GE(findeZeile(*b.m, "LOESCHEN"), 1) << bildText(*b.m);
    // OFF verlässt das Bandmenü (Cy = 1 → JP BSY = Neustart) zurück ins Hauptmenü.
    b.taste(T_OFF, 3'000'000);
    EXPECT_EQ(findeZeile(*b.m, "TASTENTEST"), 0) << bildText(*b.m);
}

namespace {

/// Bandmenü öffnen (Hauptmenü → 3. Zeile „0600  BAND“).
void bandmenue(Bsys& b)
{
    ASSERT_GE(b.bisText("TASTENTEST", 20'000'000), 0);
    b.taste(T_RUNTER);
    b.taste(T_RUNTER);
    b.taste(T_ENTER, 2'000'000);
    ASSERT_EQ(findeZeile(*b.m, "MAGNETBAND :"), 0) << bildText(*b.m);
}

/// Läuft, bis das Bandmenü wieder steht (nach MWR0/MRD0 springt BAND zurück nach MBM).
bool bisBandmenue(Bsys& b, uint64_t max)
{
    const uint64_t ziel = b.m->totalCycles() + max;
    while (b.m->totalCycles() < ziel) {
        b.m->run(1'000'000);
        if (findeZeile(*b.m, "MAGNETBAND :") == 0 && findeZeile(*b.m, "LOESCHEN") >= 1) return true;
    }
    return false;
}

/// Parameter eines Bandauftrags eingeben: Blocknummer, Anfangs-, Endadresse (je mit ENTER).
void parameter(Bsys& b, const char* bln, const char* von, const char* bis)
{
    for (const char* t : {bln, von, bis}) {
        b.tippe(t);
        b.taste(T_ENTER, 1'000'000);
    }
}

}  // namespace

TEST(Mc80_2x, KassetteSpeichernLaden)
{
    // Handbuch 4.3 / bsys_2x.prn MBM 2000H: SCHREIBEN → „AB BLOCK-NR.:“ 0001, „ADRESSE VON …
    // BIS …“ C000/C07F, „NAME :“ → MWR0 (Vorlücke ≈ 27 s, Bandmarke, Kennzeichnungsblock,
    // Datenblock 0001, Bandmarke).  Danach RAM löschen, LESEN ab Block 0001 nach C000.
    Bsys b;
    auto medium = std::make_shared<kassette::KassettenMedium>(kassette::Geometrie::kompaktkassette());
    ASSERT_TRUE(b.m->mbi().laufwerk().kassetteEinlegen(medium));
    for (int i = 0; i < 0x80; ++i)
        b.m->memWriteDebug(static_cast<uint16_t>(0xC000 + i), static_cast<uint8_t>(i * 7 + 3));

    bandmenue(b);
    b.taste(T_ENTER, 1'000'000);                       // „> SCHREIBEN“ (Zeile 1)
    EXPECT_GE(findeZeile(*b.m, "MB-SCHREIBEN"), 0) << bildText(*b.m);
    parameter(b, "0001", "C000", "C07F");
    EXPECT_GE(findeZeile(*b.m, "NAME :"), 0) << bildText(*b.m);
    b.tippe("PROGRAMM TEST");
    b.taste(T_ENTER, 1'000'000);
    ASSERT_TRUE(bisBandmenue(b, 200'000'000)) << bildText(*b.m);

    // Auf dem Band: Bandmarke, Kennzeichnungsblock (BLN FFFFH), Block 0001, Bandmarke.
    const auto s = kassette::Seite::A;
    ASSERT_EQ(medium->anzahlBloecke(s), 4u);
    EXPECT_TRUE(medium->block(s, 0).bandmarke());
    const auto& name = medium->block(s, 1).bytes;
    ASSERT_EQ(name.size(), 0x86u);
    EXPECT_EQ(name[0], 0xAA);
    EXPECT_EQ(name[1], 0xFF);
    EXPECT_EQ(name[2], 0xFF);
    const auto& daten = medium->block(s, 2).bytes;
    ASSERT_EQ(daten.size(), 0x86u);
    EXPECT_EQ(daten[1], 0x00);                         // BLN 0001, höherwertiges Byte zuerst
    EXPECT_EQ(daten[2], 0x01);
    for (int i = 0; i < 0x80; ++i) ASSERT_EQ(daten[static_cast<size_t>(3 + i)], static_cast<uint8_t>(i * 7 + 3));
    EXPECT_EQ(kassette::crc16_arc(daten.data() + 1, 2 + 0x80 + 2), 0);   // CRC über BLN + Daten
    EXPECT_EQ(daten.back(), 0xAA);
    EXPECT_TRUE(medium->block(s, 3).bandmarke());

    // RAM löschen, zurücklesen.
    for (int i = 0; i < 0x80; ++i) b.m->memWriteDebug(static_cast<uint16_t>(0xC000 + i), 0x00);
    b.taste(T_RUNTER);                                  // „> LESEN“
    b.taste(T_ENTER, 1'000'000);
    EXPECT_GE(findeZeile(*b.m, "MB-LESEN"), 0) << bildText(*b.m);
    parameter(b, "0001", "C000", "C07F");
    ASSERT_TRUE(bisBandmenue(b, 400'000'000)) << bildText(*b.m);
    for (int i = 0; i < 0x80; ++i)
        ASSERT_EQ(b.m->memReadDebug(static_cast<uint16_t>(0xC000 + i)), static_cast<uint8_t>(i * 7 + 3)) << i;
    EXPECT_GT(b.m->mbi().geleseneBytes(), 0x86u);
}
