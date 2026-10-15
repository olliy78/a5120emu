/**
 * @test K7636.* — parallele Tastatur K7636.01 an der U-Bus-Schnittstelle der K2526
 *       (doc/a5120_alt/k7636.md, k7636_01_codes.csv): Tabelle gegen die CSV, Host-Tasten,
 *       Status-/Code-Port, Kommandos mit Typzeichen-Antwort, Zeitverhalten, Dauerfunktion,
 *       Anschluss an die K2526 (und: ohne Gerät bleibt die K2526 unverändert).
 */

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include <vector>
#include "core/peripherals/k7636/k7636.h"
#include "core/cards/k2526/k2526.h"
#include "core/bus/k1520_bus.h"

namespace {
constexpr uint32_t QK_RETURN = 0x01000004, QK_ESCAPE = 0x01000000, QK_UP = 0x01000013,
                   QK_DOWN = 0x01000015, QK_LEFT = 0x01000012, QK_RIGHT = 0x01000014;
constexpr uint64_t MS = K7636::ZYKLEN_PRO_MS;

uint32_t T(uint8_t adr) { return K7636::QK_TASTE_BASE | adr; }

/// 06H lesen über die UBusGeraet-Schnittstelle (Lesen quittiert).
uint8_t lies06(K7636& k) { uint8_t w = 0; EXPECT_TRUE(k.ubusRead(0x06, w)); return w; }
uint8_t status(K7636& k) { uint8_t w = 0; EXPECT_TRUE(k.ubusRead(0x01, w)); return w; }

/// Eingeschaltete Tastatur mit gelesenem Typzeichen.
void einschalten(K7636& k)
{
    k.ubusReset();
    ASSERT_EQ(status(k), K7636::STATUS_BEREIT) << "nach dem Einschalten steht das Typzeichen bereit";
    ASSERT_EQ(lies06(k), K7636::TYP);
}
}  // namespace

// ── Tabelle ──────────────────────────────────────────────────────────────────

TEST(K7636, TabelleStimmtMitDerCsvUeberein)
{
    std::ifstream f(K7636_CSV);
    ASSERT_TRUE(f.good()) << K7636_CSV;
    std::string zeile;
    std::getline(f, zeile);   // Kopf
    int n = 0;
    while (std::getline(f, zeile)) {
        // Die ersten neun Felder enthalten (außer 'beschriftung', Feld 7) keine Kommas
        // in Anführungszeichen mit Bedeutung für uns: pos,leitung,zeile,adr,loc_a,loc_b,
        // beschriftung(evtl. "…"),a,b — von hinten lesbar ist es nicht, daher ein kleiner Parser.
        std::vector<std::string> fl;
        std::string cur; bool q = false;
        for (char c : zeile) {
            if (c == '"') q = !q;
            else if (c == ',' && !q) { fl.push_back(cur); cur.clear(); }
            else cur += c;
        }
        fl.push_back(cur);
        ASSERT_GE(fl.size(), 9u) << zeile;
        const int adr = std::stoi(fl[3], nullptr, 16);
        const int a = std::stoi(fl[7], nullptr, 16), b = std::stoi(fl[8], nullptr, 16);
        EXPECT_EQ(adr, n) << "Reihenfolge der CSV = Rechenadresse";
        // Die zwei dokumentierten Berichtigungen (k7636.md §6): A15 a = 0A, C02 b = 53.
        const std::string pos = fl[0];
        const int ea = pos == "A15" ? 0x0A : a, eb = pos == "C02" ? 0x53 : b;
        EXPECT_EQ(K7636::code(static_cast<uint8_t>(adr), false), ea) << pos;
        EXPECT_EQ(K7636::code(static_cast<uint8_t>(adr), true), eb) << pos;
        ++n;
    }
    EXPECT_EQ(n, 128);
}

TEST(K7636, GrundstellungIstKleinschrift)
{
    EXPECT_EQ(K7636::code(0x62, false), 'a');   // C01
    EXPECT_EQ(K7636::code(0x62, true), 'A');
    EXPECT_STREQ(K7636::position(0x62), "C01");
}

TEST(K7636, DauerCodesSindNurDieSechsDerListe)
{
    for (int c = 0; c < 256; ++c) {
        const bool soll = c == 0x07 || c == 0x04 || c == 0x05 || c == 0x20 || c == 0x06 || c == 0x5F;
        EXPECT_EQ(K7636::istDauerCode(static_cast<uint8_t>(c)), soll) << c;
    }
}

// ── Host-Tasten ──────────────────────────────────────────────────────────────

TEST(K7636, ZeichenSuchenDieTasteUndDieStufe)
{
    uint8_t adr; bool b;
    ASSERT_TRUE(K7636::findeZeichen('a', adr, b));
    EXPECT_EQ(adr, 0x62); EXPECT_FALSE(b);
    ASSERT_TRUE(K7636::findeZeichen('A', adr, b));
    EXPECT_EQ(adr, 0x62); EXPECT_TRUE(b);
    ASSERT_TRUE(K7636::findeZeichen('5', adr, b));
    EXPECT_STREQ(K7636::position(adr), "E05") << "Hauptblock vor Ziffernblock";
    ASSERT_TRUE(K7636::findeZeichen('%', adr, b));
    EXPECT_STREQ(K7636::position(adr), "E05"); EXPECT_TRUE(b);
    ASSERT_TRUE(K7636::findeZeichen(' ', adr, b));
    EXPECT_STREQ(K7636::position(adr), "A05");
}

TEST(K7636, ASCIIAmAusgang)
{
    K7636 k; einschalten(k);
    k.keyPress('q', false, false);
    EXPECT_EQ(lies06(k), 'q');
    k.service(100 * MS);
    k.keyPress('Q', false, false);
    EXPECT_EQ(lies06(k), 'Q');
}

TEST(K7636, SondertastenAmAusgang)
{
    K7636 k; einschalten(k);
    const std::map<uint32_t, uint8_t> erwartet = {
        {QK_RETURN, 0xFF}, {QK_ESCAPE, 0x13}, {QK_UP, 0x04}, {QK_DOWN, 0x05},
        {QK_LEFT, 0x06}, {QK_RIGHT, 0x07}};
    uint64_t t = 100 * MS;
    for (auto& [qt, c] : erwartet) {
        k.service(t += 100 * MS);
        k.keyPress(qt, false, false);
        EXPECT_EQ(lies06(k), c) << std::hex << qt;
    }
}

TEST(K7636, PhysischeTasteMitShiftLockUndCtrl)
{
    K7636 k; einschalten(k);
    uint64_t t = 0;
    auto tippe = [&](uint32_t taste, bool shift = false, bool ctrl = false) {
        k.service(t += 100 * MS);
        k.keyPress(taste, shift, ctrl);
        return lies06(k);
    };
    EXPECT_EQ(tippe(T(0x62)), 'a');
    EXPECT_EQ(tippe(T(0x62), true), 'A') << "Host-Shift";
    k.keyPress(T(K7636::ADR_SHIFT), false, false);   // SHIFT-Taste gehalten: kein Code
    EXPECT_FALSE(k.codeBereit());
    EXPECT_EQ(tippe(T(0x62)), 'A');
    k.keyRelease(T(K7636::ADR_SHIFT));
    EXPECT_EQ(tippe(T(0x62)), 'a');
    k.keyPress(T(K7636::ADR_LOCK), false, false);    // LOCK rastet, kein Code
    EXPECT_FALSE(k.codeBereit());
    EXPECT_TRUE(k.lockAktiv()); EXPECT_TRUE(k.shiftLampe());
    EXPECT_EQ(tippe(T(0x62)), 'A');
    k.keyPress(T(K7636::ADR_LOCK), false, false);
    EXPECT_FALSE(k.lockAktiv());
    EXPECT_EQ(tippe(T(0x62)), 'a');
    // Strg: ET2 (FEH) als Vorsatz (CP/A nimmt ET2 als Control-Ersatz)
    k.service(t += 100 * MS);
    k.keyPress('c', false, true);
    EXPECT_EQ(lies06(k), 0xFE);
    k.service(t += 100 * MS);
    EXPECT_EQ(lies06(k), 'c');
}

TEST(K7636, UnbestuecktePositionenUndSonderleitungenSchweigen)
{
    K7636 k; einschalten(k);
    k.keyPress(T(0x08), false, false);   // A8/Z0 nicht belegt
    k.keyPress(T(0x78), false, false);   // SL1 (X2 offen)
    k.keyPress(T(0x59), false, false);   // F57 Steuertaste nicht bestückt
    k.keyPress(T(0x58), false, false);   // F59 Netzschalter
    k.keyPress(T(K7636::ADR_CTRL), false, false);   // A99 nicht bestückt
    EXPECT_FALSE(k.codeBereit());
    EXPECT_EQ(status(k), K7636::STATUS_RUHE);
}

// ── Ports ────────────────────────────────────────────────────────────────────

TEST(K7636, RuhewertUndQuittierung)
{
    K7636 k;
    k.ubusReset();
    ASSERT_EQ(lies06(k), K7636::TYP);
    EXPECT_EQ(status(k), 0x0C) << "Bit 3 (/UINT) = 1: keine Taste; Bit 2 = 1: kein CTRL";
    k.keyPress('x', false, false);
    EXPECT_EQ(status(k), 0x04) << "/UINT = 0: Code liegt bereit";
    EXPECT_EQ(status(k), 0x04) << "Status lesen quittiert nicht";
    EXPECT_EQ(lies06(k), 'x');
    EXPECT_EQ(status(k), 0x0C);
    EXPECT_EQ(lies06(k), 'x') << "das Ausgaberegister behält den letzten Code";
}

TEST(K7636, UnbenutztePortsBedientDasGeraetNicht)
{
    K7636 k; uint8_t w = 0x5A;
    EXPECT_FALSE(k.ubusRead(0x02, w));
    EXPECT_FALSE(k.ubusRead(0x05, w));
    EXPECT_EQ(w, 0x5A);
}

// ── Kommandos ────────────────────────────────────────────────────────────────

TEST(K7636, ResetKommandoAntwortetMitDemTypzeichen)
{
    K7636 k; einschalten(k);
    k.keyPress(T(K7636::ADR_LOCK), false, false);
    k.ubusWrite(0x03, 0xDF);   // Fehlerlampe an
    lies06(k);
    k.ubusWrite(0x03, 0x00);
    EXPECT_FALSE(k.lockAktiv()); EXPECT_FALSE(k.fehlerLampe());
    k.service(10 * MS);
    EXPECT_EQ(lies06(k), K7636::TYP);
}

TEST(K7636, SteuernKommandoSchaltetLampenUndSignal)
{
    K7636 k; einschalten(k);
    // 1 S F E A B C D, 0 = ein.  Hier: nur INS-MODE (E) und Wahllampe 0 (D) an.
    k.ubusWrite(0x03, 0x80 | 0x40 | 0x20 | 0x00 | 0x0E);
    EXPECT_TRUE(k.insModeLampe());
    EXPECT_TRUE(k.wahlLampe(0));
    EXPECT_FALSE(k.wahlLampe(1)); EXPECT_FALSE(k.wahlLampe(2)); EXPECT_FALSE(k.wahlLampe(3));
    EXPECT_FALSE(k.fehlerLampe()); EXPECT_FALSE(k.signalAktiv());
    // Lade-ROM: OUT (3),9FH = Signal + Fehlerlampe an, alles andere aus
    k.service(5 * MS);
    lies06(k);
    k.ubusWrite(0x03, 0x9F);
    EXPECT_TRUE(k.fehlerLampe());
    EXPECT_TRUE(k.signalAktiv());
    EXPECT_EQ(k.signalZaehler(), 1u);
    EXPECT_FALSE(k.insModeLampe());
    k.service(5 * MS + 1300 * MS);
    EXPECT_FALSE(k.signalAktiv()) << "das Signal dauert ca. 1,25 s";
}

TEST(K7636, JedesKommandoWirdMitTypBeantwortet_CpaWartetDarauf)
{
    for (uint8_t kdo : {uint8_t(0x00), uint8_t(0x9F), uint8_t(0x02), uint8_t(0x20), uint8_t(0x7B)}) {
        K7636 k; einschalten(k);
        k.ubusWrite(0x03, kdo);
        EXPECT_FALSE(k.codeBereit()) << "Antwort kommt nach einer kurzen Reaktionszeit";
        k.service(3 * MS);
        ASSERT_TRUE(k.codeBereit()) << std::hex << int(kdo);
        EXPECT_EQ(lies06(k), K7636::TYP);
    }
}

TEST(K7636, Kommando3SchaltetShiftLampeUndFehlerlampe)
{
    K7636 k; einschalten(k);
    k.ubusWrite(0x03, 0x02 | 0x40 | 0x20 | 0x00);   // F=1 S=1 X=0 -> SHIFT-LED an
    EXPECT_TRUE(k.shiftLampe());
    EXPECT_FALSE(k.fehlerLampe());
    k.service(3 * MS); lies06(k);
    k.ubusWrite(0x03, 0x02 | 0x20 | 0x01);          // F=0 -> Fehlerlampe an, X=1 SHIFT-LED aus
    EXPECT_TRUE(k.fehlerLampe());
    EXPECT_FALSE(k.shiftLampe());
}

// ── Zeitverhalten ────────────────────────────────────────────────────────────

TEST(K7636, ZweiCodesGepuffertDritterGehtVerloren)
{
    K7636 k; einschalten(k);
    k.keyPress('a', false, false);   // -> Register
    k.keyPress('b', false, false);   // Puffer 1
    k.keyPress('c', false, false);   // Puffer 2
    k.keyPress('d', false, false);   // voll
    EXPECT_EQ(k.verloren(), 1u);
    EXPECT_EQ(lies06(k), 'a');
}

TEST(K7636, MindestabstandZweierCodes21ms)
{
    K7636 k; einschalten(k);
    k.service(1000 * MS);
    k.keyPress('a', false, false);
    k.keyPress('b', false, false);
    EXPECT_EQ(lies06(k), 'a');
    EXPECT_FALSE(k.codeBereit()) << "b wartet auf den Abstand";
    k.service(1000 * MS + 20 * MS);
    EXPECT_FALSE(k.codeBereit());
    k.service(1000 * MS + 22 * MS);
    ASSERT_TRUE(k.codeBereit());
    EXPECT_EQ(lies06(k), 'b');
}

TEST(K7636, DauerfunktionNurFuerListencodeNach500msDannAlle100ms)
{
    K7636 k; einschalten(k);
    uint64_t t = 1000 * MS;
    k.service(t);
    k.keyPress(QK_RIGHT, false, false);
    EXPECT_EQ(lies06(k), 0x07);
    t += 499 * MS; k.service(t);
    EXPECT_FALSE(k.codeBereit());
    t += 2 * MS;   k.service(t);                 // 501 ms
    ASSERT_TRUE(k.codeBereit());
    EXPECT_EQ(lies06(k), 0x07);
    t += 98 * MS;  k.service(t);                 // 599 ms nach Druck: noch nicht
    EXPECT_FALSE(k.codeBereit());
    t += 4 * MS;   k.service(t);                 // 603 ms
    ASSERT_TRUE(k.codeBereit());
    EXPECT_EQ(lies06(k), 0x07);
    k.keyRelease(QK_RIGHT);
    t += 300 * MS; k.service(t);
    EXPECT_FALSE(k.codeBereit()) << "Loslassen beendet die Wiederholung";
}

TEST(K7636, BuchstabeWiederholtNicht)
{
    K7636 k; einschalten(k);
    uint64_t t = 1000 * MS;
    k.service(t);
    k.keyPress('a', false, false);
    EXPECT_EQ(lies06(k), 'a');
    k.service(t += 2000 * MS);
    EXPECT_FALSE(k.codeBereit());
}

// ── K2526-Anschluss ─────────────────────────────────────────────────────────

TEST(K7636K2526, OhneGeraetBleibtDerUBusWieBisher)
{
    K1520Bus bus;
    K2526 zre(bus);
    zre.attachToBus(bus);
    zre.powerOn();
    EXPECT_EQ(zre.ioRead(0x01), 0x00) << "K7637-Verdrahtung: Standardwert unverändert";
    EXPECT_EQ(zre.ioRead(0x06), 0xFF);
    zre.setKbdCode(0x41); zre.setKbdValid(0x08);
    EXPECT_EQ(zre.ioRead(0x01), 0x08);
    EXPECT_EQ(zre.ioRead(0x06), 0x41);
}

TEST(K7636K2526, AnDerK2526BedientDieTastaturPort01UndPort06)
{
    K1520Bus bus;
    K2526 zre(bus);
    K7636 kbd;
    zre.setUBusGeraet(&kbd);
    zre.attachToBus(bus);
    zre.powerOn();                           // schaltet die Tastatur ein: Typzeichen bereit
    EXPECT_EQ(zre.ioRead(0x01) & 0x0C, 0x04);
    EXPECT_EQ(zre.ioRead(0x06), K7636::TYP);
    EXPECT_EQ(zre.ioRead(0x01), 0x0C) << "Ruhewert — das K2526-Standardverhalten (00H) ist abgelöst";
    kbd.keyPress('j', false, false);
    EXPECT_EQ(zre.ioRead(0x01) & 0x08, 0);
    EXPECT_EQ(zre.ioRead(0x06), 'j');
    EXPECT_EQ(zre.ioRead(0x02), 0xFF) << "übrige U-Bus-Ports wie ohne Gerät";
}

TEST(K7636K2526, KommandoUeberPort03UndNeustartDurchPowerOn)
{
    K1520Bus bus;
    K2526 zre(bus);
    K7636 kbd;
    zre.setUBusGeraet(&kbd);
    zre.attachToBus(bus);
    zre.powerOn();
    zre.ioRead(0x06);
    zre.ioWrite(0x03, 0x9F);                 // Lade-ROM: Signal + Fehlerlampe
    EXPECT_TRUE(kbd.fehlerLampe());
    EXPECT_TRUE(kbd.signalAktiv());
    kbd.service(5 * MS);
    EXPECT_EQ(zre.ioRead(0x06), K7636::TYP);
    zre.powerOn();                           // System-/RESET
    EXPECT_FALSE(kbd.fehlerLampe());
    EXPECT_EQ(zre.ioRead(0x06), K7636::TYP);
}

TEST(K7636K2526, CpaTastaturerkennungLiefertTyp80)
{
    // Ablauf aus bioskbdc.mac (ciini1…coityp): Zeichen überlesen, Ruhestatus Bit 3 = 1,
    // Reset (OUT 03H,0), dann auf Status Bit 3 = 0 warten und Typ & F0H = 80H lesen.
    K1520Bus bus;
    K2526 zre(bus);
    K7636 kbd;
    zre.setUBusGeraet(&kbd);
    zre.attachToBus(bus);
    zre.powerOn();
    for (int i = 0; i < 4; ++i) zre.ioRead(0x06);
    ASSERT_NE(zre.ioRead(0x01) & 8, 0) << "leere Tastatur: Bit 3 = 1 (cpl bleibt aktiv)";
    zre.ioWrite(0x03, 0x00);
    uint64_t t = 0;
    while ((zre.ioRead(0x01) & 8) && t < 100 * MS) kbd.service(t += MS / 10);
    ASSERT_EQ(zre.ioRead(0x01) & 8, 0);
    EXPECT_EQ(zre.ioRead(0x06) & 0xF0, 0x80);
}
