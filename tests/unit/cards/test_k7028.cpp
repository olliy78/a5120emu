/**
 * @file test_k7028.cpp
 * @brief ATS K7028.30 des K8915 (doc/design/16_k8915.md §3.2, §8a AP-E2):
 *        Portdekodierung samt Spiegel, Rückschleife je Kanal, Interruptkette der
 *        CTCs, Anzeigelatch.
 */

#include <gtest/gtest.h>

#include <vector>

#include "core/bus/k1520_bus.h"
#include "core/cards/k7028/k7028.h"

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

/// Mit Rückschleife kommt jedes Sendebyte nach einer Zeichenzeit am selben Kanal an.
TEST(K7028, RueckschleifeJeKanalMitZeichenzeit)
{
    Aufbau a(K7028::Config::mitPruefstecker());
    for (uint8_t s : {0x45, 0x47, 0x55}) a.sioInit(s);
    a.bus.ioWrite(0x44, 0xAA);
    a.bus.ioWrite(0x46, 0xAA);
    a.bus.ioWrite(0x54, 0xAA);

    a.ats.service(1000);
    EXPECT_EQ(a.bus.ioRead(0x45) & 0x01, 0) << "noch unterwegs";
    a.ats.service(1000 + K7028::ZEICHEN_TAKTE - 1);
    EXPECT_EQ(a.bus.ioRead(0x45) & 0x01, 0);
    a.ats.service(1000 + K7028::ZEICHEN_TAKTE);
    EXPECT_EQ(a.bus.ioRead(0x44), 0xAA);
    EXPECT_EQ(a.bus.ioRead(0x46), 0xAA);
    EXPECT_EQ(a.bus.ioRead(0x54), 0xAA);

    // Zwei Zeichen hintereinander: das zweite eine Zeichenzeit später.
    a.bus.ioWrite(0x44, 0x55);
    a.ats.service(5000);
    a.bus.ioWrite(0x44, 0x5A);
    a.ats.service(5001);
    a.ats.service(5000 + K7028::ZEICHEN_TAKTE);
    EXPECT_EQ(a.bus.ioRead(0x44), 0x55);
    EXPECT_EQ(a.bus.ioRead(0x44), 0x55)
        << "zweites Zeichen noch unterwegs: FIFO leer, Z80SIO liefert das zuletzt "
           "empfangene Byte (AP-E4c), nicht FFH";
    a.ats.service(5000 + 2 * K7028::ZEICHEN_TAKTE);
    EXPECT_EQ(a.bus.ioRead(0x44), 0x5A);
}

/// Ohne Rückschleife: kein Echo; das Byte geht an den Abnehmer oder verloren.
/// Nur der gewählte Kanal schleift, SIO2-B (Tastatur) fasst die Karte nie an.
TEST(K7028, OhneRueckschleifeKeinEcho)
{
    K7028::Config c;
    c.rueckschleife[K7028::Sio1B] = true;
    Aufbau a(c);
    std::vector<uint8_t> ab;
    a.ats.setAbnehmer(K7028::Sio1A, [&](uint8_t b) { ab.push_back(b); });
    a.bus.ioWrite(0x40, 0x31);
    a.bus.ioWrite(0x42, 0x32);
    a.bus.ioWrite(0x50, 0x33);
    a.bus.ioWrite(0x52, 0x34);   // Tastaturkanal
    a.ats.service(0);
    a.ats.service(100'000);
    EXPECT_EQ(ab, std::vector<uint8_t>{0x31});
    EXPECT_EQ(a.bus.ioRead(0x40), 0x00) << "nie empfangen: Reset-Ruhewert (AP-E4c)";
    EXPECT_EQ(a.bus.ioRead(0x42), 0x32);
    EXPECT_EQ(a.bus.ioRead(0x50), 0x00) << "nie empfangen: Reset-Ruhewert (AP-E4c)";
    EXPECT_TRUE(a.ats.sio2().channelB().txAvailable()) << "SIO2-B gehört der Tastatur";
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
    Aufbau a(K7028::Config::mitPruefstecker());
    EXPECT_EQ(a.ats.anzeige(), 0xFF);
    a.bus.ioWrite(0x61, 0xB0);
    EXPECT_EQ(a.ats.anzeige(), 0xB0);
    EXPECT_EQ(a.bus.ioRead(0x61), 0xFF);
    a.bus.ioWrite(0x44, 0x12);
    a.ats.service(0);
    a.ats.reset();
    a.ats.service(1'000'000);
    EXPECT_EQ(a.ats.anzeige(), 0xFF);
    EXPECT_EQ(a.bus.ioRead(0x44), 0x00)
        << "Zeichen auf der Leitung verworfen (reset() zieht die Schlange ab); der "
           "Empfänger meldet danach seinen Reset-Ruhewert, nicht FFH (AP-E4c)";
}

/**
 * @test K7028.AbnehmerErsetztDieRueckschleifeSeinesKanals
 * @brief AP-E4c / AP-T1a: MIT Prüfstecker ersetzt ein Abnehmer die Rückschleife NUR
 *        auf seinem Kanal — das Byte geht nach außen und kommt nicht zusätzlich als
 *        Echo zurück; die beiden anderen Kanäle schleifen weiter (sonst schlüge der
 *        SIO-Test des ROMs mit angeschlossenem Drucker fehl).  Bei 7 Datenbits (WR5
 *        D6–5 = 01, BIOS-Fassung 900) trägt die Leitung nur die unteren 7 Bit.
 *        (Bis AP-T1a hielt das nur der Integrationsfall mit Fassung 900 fest.)
 */
TEST(K7028, AbnehmerErsetztDieRueckschleifeSeinesKanals)
{
    Aufbau a(K7028::Config::mitPruefstecker());
    std::vector<uint8_t> ab;
    a.ats.setAbnehmer(K7028::Sio1B, [&](uint8_t b) { ab.push_back(b); });
    a.bus.ioWrite(0x40, 0x31);
    a.bus.ioWrite(0x42, 0x32);
    a.bus.ioWrite(0x50, 0x33);
    a.ats.service(0);
    a.ats.service(100'000);
    EXPECT_EQ(ab, std::vector<uint8_t>{0x32});
    EXPECT_EQ(a.bus.ioRead(0x43) & 0x01, 0x00) << "kein Echo neben dem Abnehmer";
    EXPECT_EQ(a.bus.ioRead(0x40), 0x31);
    EXPECT_EQ(a.bus.ioRead(0x50), 0x33);

    a.bus.ioWrite(0x43, 0x05);
    a.bus.ioWrite(0x43, 0x28);           // WR5: 7 Bit, Sender frei
    a.bus.ioWrite(0x42, 0xC1);
    a.ats.service(200'000);
    a.ats.service(300'000);
    EXPECT_EQ(ab, (std::vector<uint8_t>{0x32, 0x41})) << "Bit 7 liegt nicht auf der Leitung";
}
