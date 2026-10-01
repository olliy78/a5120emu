/**
 * @file test_k7028.cpp
 * @brief ATS K7028.30 des K8915 (doc/design/16_k8915.md §3.2, §8a AP-E2):
 *        Portdekodierung samt Spiegel, Interruptkette der CTCs, Anzeigelatch; dazu
 *        die Anschlüsse nach außen (Entwurf 19 §3.2, AP-S5, `K7028Seriell.*`):
 *        Kanalzuordnung, Taktquelle, V.24-Leitungen, Loop = alter Prüfstecker,
 *        Tastatur unberührt, alter Unterbau (Abnehmer/Einspeisen).
 */

#include <gtest/gtest.h>

#include <vector>

#include "core/bus/k1520_bus.h"
#include "core/cards/k7028/k7028.h"
#include "core/serial/wandler.h"

using k1520::serial::Wandler;
using k1520::serial::WandlerEinstellung;

namespace {

struct Aufbau {
    K1520Bus bus;
    K7028    ats;
    explicit Aufbau(const K7028::Config& c = K7028::Config{}) : ats(c) {
        ats.attachToBus(bus);
        bus.setInterruptChain({&ats});
    }
    /// SIO-Kanal wie die Init-Tabelle FCEFH des ROMs einstellen (über @p steuer).
    void sioInit(uint8_t steuer) {
        for (uint8_t b : {0x18, 0x03, 0xC1, 0x04, 0x45, 0x05, 0x68, 0xC0})
            bus.ioWrite(steuer, b);
    }
};

}  // namespace

/// A2 wird nicht dekodiert: 44H–47H = 40H–43H usw.; C/D = A0, B/A = A1.
TEST(K7028, SpiegeladressenUndKanalreihenfolge)
{
    Aufbau a;
    a.ats.sio1().channelA().rxByte(0x11);
    a.ats.sio1().channelB().rxByte(0x22);
    a.ats.sio2().channelA().rxByte(0x33);
    a.ats.sio2().channelB().rxByte(0x44);
    EXPECT_EQ(a.bus.ioRead(0x44), 0x11);   // SIO1-A Daten über den Spiegel
    EXPECT_EQ(a.bus.ioRead(0x42), 0x22);   // SIO1-B Daten
    EXPECT_EQ(a.bus.ioRead(0x54), 0x33);   // SIO2-A Daten über den Spiegel
    EXPECT_EQ(a.bus.ioRead(0x52), 0x44);   // SIO2-B Daten = Tastatur
    // Steuerport liefert RR0: Bit0 = Zeichen da.
    a.ats.sio2().channelB().rxByte(0x55);
    EXPECT_EQ(a.bus.ioRead(0x57) & 0x01, 0x01);   // 57H = Spiegel von 53H
    EXPECT_EQ(a.bus.ioRead(0x53) & 0x01, 0x01);
    EXPECT_EQ(a.bus.ioRead(0x42 + 4), 0x22)
        << "leer, aber schon einmal 0x22 empfangen (oben): Z80SIO liefert das "
           "zuletzt empfangene Byte weiter (AP-E4c), nicht FFH";

    // CTC 2 Kanal 2 über 5AH und 5EH, CTC 1 Kanal 0 über 4CH.
    a.bus.ioWrite(0x5E, 0x07);   // Zeitgeber, Vorteiler 16, ZK folgt
    a.bus.ioWrite(0x5A, 0x30);
    EXPECT_EQ(a.bus.ioRead(0x5A), 0x30);
    EXPECT_TRUE(a.ats.ctc2().isTimerMode(2));
    a.bus.ioWrite(0x4C, 0x17);
    a.bus.ioWrite(0x48, 0x21);
    EXPECT_EQ(a.bus.ioRead(0x4C), 0x21);
}

/// Kanal 3 beider CTCs als Zeitgeber mit Interrupt, wie der CTC-Test des ROMs
/// (F208H): Vektor F0H + 6, Periode = Zeitkonstante × 256 Takte.
TEST(K7028, CtcKanal3LiefertVektorNachZeitkonstante)
{
    Aufbau a;
    a.bus.ioWrite(0x48, 0xF0);
    a.bus.ioWrite(0x4B, 0xA7);
    a.bus.ioWrite(0x4B, 0x18);    // 24 × 256 = 6144 Takte
    a.bus.markIntDirty();
    a.bus.updateInterruptChain();
    int t = 0;
    while (t < 20'000 && !a.bus.isINT()) {
        if (a.ats.clockTick(16)) a.bus.markIntDirty();
        a.bus.updateInterruptChain();
        t += 16;
    }
    EXPECT_EQ(t, 6144);
    EXPECT_STREQ(a.ats.intDeviceName(), "ATS CTC1");
    EXPECT_EQ(a.bus.interruptAcknowledge(), 0xF6);
    a.bus.signalRETI();

    // Anhalten mit 03H löscht die Anforderung (so beendet das ROM jedes Fenster).
    a.bus.ioWrite(0x4B, 0x03);
    for (int i = 0; i < 1000; ++i) a.ats.clockTick(16);
    a.bus.markIntDirty();
    a.bus.updateInterruptChain();
    EXPECT_FALSE(a.bus.isINT());

    // CTC 2 über den Spiegel 5FH.
    a.bus.ioWrite(0x5C, 0xF0);
    a.bus.ioWrite(0x5F, 0xA7);
    a.bus.ioWrite(0x5F, 0x24);    // 36 × 256
    int u = 0;
    while (u < 20'000 && !a.ats.hasInterrupt()) { a.ats.clockTick(16); u += 16; }
    EXPECT_EQ(u, 9216);
    EXPECT_STREQ(a.ats.intDeviceName(), "ATS CTC2");
}

/// Anzeigelatch 60H–67H: nur beschreibbar, Wert bleibt stehen; /RESET ⇒ FFH.
TEST(K7028, AnzeigelatchUndReset)
{
    Aufbau a;
    EXPECT_EQ(a.ats.anzeige(), 0xFF);
    a.bus.ioWrite(0x61, 0xB0);
    EXPECT_EQ(a.ats.anzeige(), 0xB0);
    EXPECT_EQ(a.bus.ioRead(0x61), 0xFF);
    a.ats.reset();
    EXPECT_EQ(a.ats.anzeige(), 0xFF);
}

// ─── K7028Seriell: Anschlüsse nach außen (Entwurf 19 §3.2, AP-S5) ───────────

namespace {

/// Drei Wandler wie in der Maschine (Hub ohne Faden), gemeinsamer Takt.
struct Wandlerkette {
    Aufbau&  a;
    Wandler  w0, w1, w2;
    uint64_t z = 0;
    explicit Wandlerkette(Aufbau& auf)
        : a(auf), w0(auf.ats.anschluss(K7028::Sio1A)), w1(auf.ats.anschluss(K7028::Sio1B)),
          w2(auf.ats.anschluss(K7028::Sio2A)) {}
    void loop(bool an) {
        WandlerEinstellung e;
        e.loop = an;
        w0.einstellen(e);
        w1.einstellen(e);
        w2.einstellen(e);
    }
    void laufe(uint64_t takte) {
        for (const uint64_t ende = z + takte; z < ende; z += 16) {
            w0.takt(z);
            w1.takt(z);
            w2.takt(z);
            a.ats.clockTick(16);
        }
    }
};

/// CTC-Kanal @p port als Zeitgeber, Vorteiler 16, Zeitkonstante @p zk (ROM: 17H/01H).
void ctcZeitgeber(K1520Bus& bus, uint8_t port, uint8_t zk) {
    bus.ioWrite(port, 0x17);
    bus.ioWrite(port, zk);
}

}  // namespace

/// Namen und Stecker nach der Beschriftung am Gerät (Anwender, AP-S12): X3
/// „Drucker/IFSS1" = SIO1-B (BIOS-Drucker), X4 „V.24" = SIO1-A (einzige mit
/// Steuerleitungen), X5 „DFÜ/IFSS2" = SIO2-A; die Reihenfolge (= Hub-/C-ABI-Index)
/// folgt den Steckern.  Tastatur fest.
TEST(K7028Seriell, Kanalzuordnung)
{
    Aufbau a;
    EXPECT_STREQ(a.ats.anschluss(K7028::Sio1B).name(), "Drucker/IFSS1");
    EXPECT_STREQ(a.ats.anschluss(K7028::Sio1B).stecker(), "X3");
    EXPECT_FALSE(a.ats.anschluss(K7028::Sio1B).v24());
    EXPECT_STREQ(a.ats.anschluss(K7028::Sio1A).name(), "V.24");
    EXPECT_STREQ(a.ats.anschluss(K7028::Sio1A).stecker(), "X4");
    EXPECT_TRUE(a.ats.anschluss(K7028::Sio1A).v24());
    EXPECT_STREQ(a.ats.anschluss(K7028::Sio2A).name(), "DFÜ/IFSS2");
    EXPECT_STREQ(a.ats.anschluss(K7028::Sio2A).stecker(), "X5");
    EXPECT_FALSE(a.ats.anschluss(K7028::Sio2A).v24());
    EXPECT_EQ(K7028::Sio1B, 0);
    EXPECT_EQ(K7028::Sio1A, 1);
    EXPECT_EQ(K7028::Sio2A, 2);
    for (int k = 0; k < K7028::KanalAnzahl; ++k)
        EXPECT_TRUE(a.ats.anschluss(static_cast<K7028::Kanal>(k)).taktquellen().empty())
            << "K7028: Takt fest verdrahtet";
    EXPECT_STREQ(K7028::TASTATUR_NAME, "Tastatur K7672");

    // Jeder Anschluss nimmt die Sendebytes SEINES Kanals (8 Bit wie im ROM; nach dem
    // Reset stünden 5 Bit in WR5 und die Leitung trüge nur die unteren fünf).
    for (uint8_t st : {0x41, 0x43, 0x51}) a.sioInit(st);
    a.bus.ioWrite(0x40, 0x31);
    a.bus.ioWrite(0x42, 0x32);
    a.bus.ioWrite(0x50, 0x33);
    EXPECT_EQ(a.ats.anschluss(K7028::Sio1A).senderNimm(), 0x31);
    EXPECT_EQ(a.ats.anschluss(K7028::Sio1B).senderNimm(), 0x32);
    EXPECT_EQ(a.ats.anschluss(K7028::Sio2A).senderNimm(), 0x33);
    a.ats.anschluss(K7028::Sio1B).empfange(0x44);
    EXPECT_EQ(a.bus.ioRead(0x42), 0x44);
    EXPECT_TRUE(a.ats.nimmSeriellGeaendert());
    EXPECT_FALSE(a.ats.nimmSeriellGeaendert());
}

/// SIO1-A ← CTC1 K0, SIO1-B ← CTC1 K2 (Stromlaufplan: ZC/TO2 über D9:02 an RxTxCB;
/// BIOS: CTC1-K2 = 4AH für den Drucker), SIO2-A ← CTC2 K0.
TEST(K7028Seriell, TaktquelleJeKanal)
{
    Aufbau a;
    // BIOS-Drucker (DE90H): CTC1-K2 05H/01H, SIO1-B WR4 44H, WR5 68H → 9600 8N1.
    a.bus.ioWrite(0x4A, 0x05);
    a.bus.ioWrite(0x4A, 0x01);
    for (uint8_t b : {0x18, 0x04, 0x44, 0x03, 0xC1, 0x05, 0x68}) a.bus.ioWrite(0x43, b);
    auto f = a.ats.anschluss(K7028::Sio1B).format();
    EXPECT_TRUE(f.gueltig);
    EXPECT_EQ(f.baud_nenn, 9600u);
    EXPECT_EQ(f.zeichen_takte, 10u * 256u);

    // SIO1-A und SIO2-A wie im ROM (WR4 45H: ×16, ungerade Parität), aber verschiedene
    // CTC-Kanäle — jeder Kanal hängt nur an seinem.
    a.sioInit(0x41);
    a.sioInit(0x51);
    EXPECT_FALSE(a.ats.anschluss(K7028::Sio1A).format().gueltig) << "CTC1 K0 läuft noch nicht";
    ctcZeitgeber(a.bus, 0x48, 2);   // CTC1 K0: 4800
    ctcZeitgeber(a.bus, 0x58, 1);   // CTC2 K0: 9600
    EXPECT_EQ(a.ats.anschluss(K7028::Sio1A).format().baud_nenn, 4800u);
    EXPECT_EQ(a.ats.anschluss(K7028::Sio2A).format().baud_nenn, 9600u);
    EXPECT_EQ(a.ats.anschluss(K7028::Sio2A).format().zeichen_takte, 11u * 256u)
        << "8 Daten + Parität + Start + Stopp";
    ctcZeitgeber(a.bus, 0x49, 4);   // CTC1 K1 ändert nichts an SIO1-A (Multiplexer: K0)
    EXPECT_EQ(a.ats.anschluss(K7028::Sio1A).format().baud_nenn, 4800u);
    ctcZeitgeber(a.bus, 0x4A, 8);   // CTC1 K2 → nur SIO1-B
    EXPECT_EQ(a.ats.anschluss(K7028::Sio1B).format().baud_nenn, 1200u);
    EXPECT_EQ(a.ats.anschluss(K7028::Sio1A).format().baud_nenn, 4800u);
}

/// V.24 (SIO1-A): RTS/DTR aus WR5, /CTSA ← V106, /DCDA ← V109.  Die IFSS-Kanäle haben
/// keine Steuerleitungen: ihre Eingänge bleiben inaktiv (RR0 wie vor AP-S5).
TEST(K7028Seriell, V24LeitungenNurAnSio1A)
{
    Aufbau a;
    auto& v24 = a.ats.anschluss(K7028::Sio1A);
    EXPECT_FALSE(v24.rts());
    EXPECT_FALSE(v24.dtr());
    a.bus.ioWrite(0x41, 0x05);
    a.bus.ioWrite(0x41, 0xEA);   // WR5: DTR, 8 Bit, Tx ein, RTS
    EXPECT_TRUE(v24.rts());
    EXPECT_TRUE(v24.dtr());
    EXPECT_EQ(a.bus.ioRead(0x41) & 0x28, 0x00) << "Einschaltzustand: /CTS, /DCD inaktiv";
    v24.setzeEingaenge(true, false, false);
    EXPECT_EQ(a.bus.ioRead(0x41) & 0x28, 0x20) << "CTS";
    v24.setzeEingaenge(false, true, true);
    EXPECT_EQ(a.bus.ioRead(0x41) & 0x28, 0x08) << "DCD (DSR hat keinen SIO-Eingang)";

    a.ats.anschluss(K7028::Sio1B).setzeEingaenge(true, true, true);
    a.ats.anschluss(K7028::Sio2A).setzeEingaenge(true, true, true);
    EXPECT_EQ(a.bus.ioRead(0x43) & 0x28, 0x00);
    EXPECT_EQ(a.bus.ioRead(0x51) & 0x28, 0x00);
}

/// Loop = der frühere Prüfstecker: jedes Sendebyte kommt nach einer Zeichenzeit am
/// selben Kanal an (Selbsttest des ROMs: AAH an allen drei, 11 Bit bei 9600 Bd).
TEST(K7028Seriell, LoopIstDerAltePruefstecker)
{
    Aufbau a;
    for (uint8_t p : {0x48, 0x49, 0x4A, 0x58}) ctcZeitgeber(a.bus, p, 1);
    for (uint8_t s : {0x41, 0x43, 0x51}) a.sioInit(s);
    Wandlerkette w(a);
    w.loop(true);
    w.laufe(64);
    a.bus.ioWrite(0x44, 0xAA);
    a.bus.ioWrite(0x46, 0xAA);
    a.bus.ioWrite(0x54, 0xAA);
    w.laufe(11 * 256 - 256);
    EXPECT_EQ(a.bus.ioRead(0x45) & 0x01, 0) << "noch unterwegs";
    w.laufe(2 * 256);
    EXPECT_EQ(a.bus.ioRead(0x44), 0xAA);
    EXPECT_EQ(a.bus.ioRead(0x46), 0xAA);
    EXPECT_EQ(a.bus.ioRead(0x54), 0xAA);

    // Ohne Loop: kein Echo, das Byte verfällt nach seiner Zeichenzeit (Kabel ab).
    w.loop(false);
    a.bus.ioWrite(0x44, 0x55);
    w.laufe(4 * 11 * 256);
    EXPECT_EQ(a.bus.ioRead(0x45) & 0x01, 0);
    EXPECT_EQ(a.bus.ioRead(0x45) & 0x04, 0x04) << "Sender wieder leer";
}

/// SIO2-B gehört der Tastatur: kein Anschluss, kein Wandler fasst sie an.
TEST(K7028Seriell, TastaturUnberuehrt)
{
    Aufbau a;
    Wandlerkette w(a);
    w.loop(true);
    a.bus.ioWrite(0x52, 0x34);
    w.laufe(100'000);
    EXPECT_TRUE(a.ats.sio2().channelB().txAvailable());
    EXPECT_EQ(a.ats.sio2().channelB().rx_fifo.size(), 0u);
}

/// Alter Unterbau: der Abnehmer bekommt die Bytes in ihrer Zeichenzeit (7 Bit bei
/// WR5 = 28H, AP-E4c), nur solange weder Loop noch Transport den Stecker belegen;
/// Einspeisen geht dann ins Leere.
TEST(K7028Seriell, AlterUnterbauSchweigtBeiBelegtemStecker)
{
    Aufbau a;
    ctcZeitgeber(a.bus, 0x4A, 1);
    for (uint8_t b : {0x18, 0x04, 0x45, 0x03, 0x41, 0x05, 0x28}) a.bus.ioWrite(0x43, b);
    std::vector<uint8_t> ab;
    a.ats.setAbnehmer(K7028::Sio1B, [&](uint8_t b) { ab.push_back(b); });
    Wandlerkette w(a);
    a.bus.ioWrite(0x46, 0xC1);
    w.laufe(20'000);
    EXPECT_EQ(ab, std::vector<uint8_t>{0x41}) << "7 Bit";
    a.ats.empfange(K7028::Sio1B, 0x13);
    EXPECT_EQ(a.bus.ioRead(0x46), 0x13);

    w.loop(true);
    w.laufe(64);
    a.bus.ioWrite(0x46, 0x42);
    w.laufe(20'000);
    EXPECT_EQ(ab.size(), 1u) << "Loop gesteckt: das Byte geht an den Prüfstecker";
    EXPECT_EQ(a.bus.ioRead(0x46), 0x42);
    a.ats.empfange(K7028::Sio1B, 0x11);
    EXPECT_EQ(a.bus.ioRead(0x46), 0x42) << "Einspeisen bei belegtem Stecker: ins Leere";

    w.w1.anbinden();
    w.loop(false);
    w.laufe(64);
    a.bus.ioWrite(0x46, 0x43);
    w.laufe(20'000);
    EXPECT_EQ(ab.size(), 1u) << "Transport angebunden: Abnehmer schweigt";
    uint8_t buf[4];
    EXPECT_EQ(w.w1.fernNimm(buf, 4), 1u);
    EXPECT_EQ(buf[0], 0x43);
}

/// Break des Gastes (WR5 D4) erscheint am Anschluss; ein Break vom Wandler setzt RR0 D7
/// des richtigen Kanals (AP-T1b).
TEST(K7028Seriell, BreakInBeideRichtungen)
{
    Aufbau a;
    auto rr0 = [&](uint8_t ctrl) { a.bus.ioWrite(ctrl, 0x10); return a.bus.ioRead(ctrl); };
    struct Fall { K7028::Kanal k; uint8_t ctrl; };
    for (Fall f : {Fall{K7028::Sio1A, 0x41}, Fall{K7028::Sio1B, 0x43},
                   Fall{K7028::Sio2A, 0x51}}) {
        auto& an = a.ats.anschluss(f.k);
        EXPECT_FALSE(an.breakGesendet());
        a.bus.ioWrite(f.ctrl, 0x05);
        a.bus.ioWrite(f.ctrl, 0x78);
        EXPECT_TRUE(an.breakGesendet()) << int(f.ctrl);
        a.bus.ioWrite(f.ctrl, 0x05);
        a.bus.ioWrite(f.ctrl, 0x68);
        EXPECT_FALSE(an.breakGesendet());

        EXPECT_EQ(rr0(f.ctrl) & 0x80, 0x00);
        an.breakEmpfang(true);
        EXPECT_EQ(rr0(f.ctrl) & 0x80, 0x80) << int(f.ctrl);
        an.breakEmpfang(false);
        EXPECT_EQ(rr0(f.ctrl) & 0x80, 0x00);
    }
}
