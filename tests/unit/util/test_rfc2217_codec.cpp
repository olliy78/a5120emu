/**
 * @file test_rfc2217_codec.cpp
 * @brief Unit-Tests für den RFC-2217-Codec (`core/serial/rfc2217_codec.h`).
 *
 * @details
 * Doc: doc/design/19_serielle_schnittstellen.md §7.5, §6.4, §11.
 * Gruppen: Anfangsverhandlung (WILL 44 / DO 44), jeder Befehl hin und zurück,
 * +100-Antworten, Baud in Netzreihenfolge, Masken, 0xFF in SB, Zerreißen.
 */
#include <gtest/gtest.h>

#include "core/serial/rfc2217_codec.h"

using namespace serial;
using namespace serial::telnet;
using namespace serial::rfc2217;
using Art = Rfc2217Ereignis::Art;
using Bytes = std::vector<uint8_t>;

namespace {

// Was `von` ausgegeben hat, geht (optional byteweise) in `nach`.
void uebertrage(Rfc2217Codec& von, Rfc2217Codec& nach, bool byteweise = false) {
    Bytes b = von.nimmAusgabe();
    if (byteweise) {
        for (uint8_t x : b) nach.eingabe(&x, 1);
    } else {
        nach.eingabe(b);
    }
}

// Client und Server verhandeln lassen, bis Ruhe ist.
void verbinde(Rfc2217Codec& c, Rfc2217Codec& s) {
    c.start();
    s.start();
    for (int i = 0; i < 10 && (c.hatAusgabe() || s.hatAusgabe()); ++i) {
        uebertrage(c, s);
        uebertrage(s, c);
    }
}

Rfc2217Ereignis eins(Rfc2217Codec& c) {
    auto v = c.nimmEreignisse();
    EXPECT_EQ(v.size(), 1u);
    return v.empty() ? Rfc2217Ereignis{} : v[0];
}

}  // namespace

TEST(Rfc2217Codec, ClientSendetWill44ServerDo44) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    c.start();
    Bytes cb = c.nimmAusgabe();
    EXPECT_EQ(cb, (Bytes{IAC, WILL, 44}));
    s.start();
    Bytes sb = s.nimmAusgabe();
    // Telnet-Anfang des Servers, am Ende DO 44
    ASSERT_GE(sb.size(), 3u);
    EXPECT_EQ(Bytes(sb.end() - 3, sb.end()), (Bytes{IAC, DO, 44}));
}

TEST(Rfc2217Codec, HandschlagAktiviertOption44BeiderseitsOhnePingpong) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    verbinde(c, s);
    EXPECT_TRUE(c.comPortAktiv());
    EXPECT_TRUE(s.comPortAktiv());
    EXPECT_FALSE(c.hatAusgabe());
    EXPECT_FALSE(s.hatAusgabe());
}

TEST(Rfc2217Codec, BaudInNetzreihenfolge) {
    Rfc2217Codec c(TelnetRolle::Client);
    c.sendeBaud(9600);
    EXPECT_EQ(c.nimmAusgabe(), (Bytes{IAC, SB, 44, 1, 0x00, 0x00, 0x25, 0x80, IAC, SE}));
}

TEST(Rfc2217Codec, BaudMitFF_WirdVerdoppeltUndFehlerfreiZurueckgelesen) {
    // 0x000000FF = 255 Bd und 0xFF00FF00 zum Härten
    for (uint32_t baud : {255u, 0xFF00FF00u, 115200u, 0u}) {
        Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
        c.sendeBaud(baud);
        uebertrage(c, s, /*byteweise=*/true);
        auto e = eins(s);
        EXPECT_EQ(e.art, Art::Baud);
        EXPECT_EQ(e.wert, baud);
        EXPECT_FALSE(e.antwort);
    }
    Rfc2217Codec c(TelnetRolle::Client);
    c.sendeBaud(255);
    EXPECT_EQ(c.nimmAusgabe(), (Bytes{IAC, SB, 44, 1, 0, 0, 0, IAC, IAC, IAC, SE}));
}

TEST(Rfc2217Codec, ServerAntwortetMitGastwertAlsBefehlPlus100) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    c.sendeBaud(19200);
    uebertrage(c, s);
    auto e = eins(s);
    ASSERT_EQ(e.art, Art::Baud);
    EXPECT_EQ(e.wert, 19200u);
    // Der Gast läuft mit 9600 — die Antwort trägt den Gastwert, nicht den verlangten.
    s.sendeBaud(9600, /*alsAntwort=*/true);
    Bytes wire = s.nimmAusgabe();
    EXPECT_EQ(wire, (Bytes{IAC, SB, 44, 101, 0, 0, 0x25, 0x80, IAC, SE}));
    c.eingabe(wire);
    auto a = eins(c);
    EXPECT_EQ(a.art, Art::Baud);
    EXPECT_TRUE(a.antwort);
    EXPECT_EQ(a.wert, 9600u);
}

TEST(Rfc2217Codec, DatenbitsParitaetStoppbitsHinUndZurueck) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    c.sendeDatenbits(7);
    c.sendeParitaet(3);
    c.sendeStoppbits(2);
    EXPECT_EQ(Bytes(c.nimmAusgabe()), (Bytes{IAC, SB, 44, 2, 7, IAC, SE, IAC, SB, 44, 3, 3, IAC, SE,
                                              IAC, SB, 44, 4, 2, IAC, SE}));
    c.sendeDatenbits(7);
    c.sendeParitaet(3);
    c.sendeStoppbits(2);
    uebertrage(c, s);
    auto ev = s.nimmEreignisse();
    ASSERT_EQ(ev.size(), 3u);
    EXPECT_EQ(ev[0].art, Art::Datenbits);
    EXPECT_EQ(ev[0].wert, 7u);
    EXPECT_EQ(ev[1].art, Art::Paritaet);
    EXPECT_EQ(ev[1].wert, 3u);
    EXPECT_EQ(ev[2].art, Art::Stoppbits);
    EXPECT_EQ(ev[2].wert, 2u);
    s.sendeDatenbits(8, true);
    s.sendeParitaet(1, true);
    s.sendeStoppbits(1, true);
    Bytes w = s.nimmAusgabe();
    c.eingabe(w);
    auto an = c.nimmEreignisse();
    ASSERT_EQ(an.size(), 3u);
    for (auto& e : an) EXPECT_TRUE(e.antwort);
    EXPECT_EQ(an[0].wert, 8u);
    EXPECT_EQ(an[1].wert, 1u);
    EXPECT_EQ(an[2].wert, 1u);
    // Antwortnummern 102/103/104
    EXPECT_EQ(w[3], 102);
}

TEST(Rfc2217Codec, AbfragenHabenWertNull) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    c.sendeBaud(0);
    c.sendeDatenbits(0);
    c.sendeSignatur("");
    uebertrage(c, s);
    auto ev = s.nimmEreignisse();
    ASSERT_EQ(ev.size(), 3u);
    EXPECT_EQ(ev[0].wert, 0u);
    EXPECT_EQ(ev[1].wert, 0u);
    EXPECT_EQ(ev[2].art, Art::Signatur);
    EXPECT_TRUE(ev[2].text.empty());
}

TEST(Rfc2217Codec, Signatur) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    c.sendeSignatur("k1520emu 1.2");
    uebertrage(c, s);
    auto e = eins(s);
    EXPECT_EQ(e.art, Art::Signatur);
    EXPECT_EQ(e.text, "k1520emu 1.2");
    EXPECT_FALSE(e.antwort);
    s.sendeSignatur("k1520emu 1.2", true);
    Bytes w = s.nimmAusgabe();
    EXPECT_EQ(w[3], 100);
    c.eingabe(w);
    auto a = eins(c);
    EXPECT_TRUE(a.antwort);
    EXPECT_EQ(a.text, "k1520emu 1.2");
}

TEST(Rfc2217Codec, SteuerwerteLeitungenFlussartBreak) {
    const uint8_t alle[] = {FLUSS_ABFRAGE, FLUSS_KEINE, FLUSS_XONXOFF, FLUSS_HARDWARE,
                            BREAK_ABFRAGE, BREAK_EIN, BREAK_AUS,
                            DTR_ABFRAGE, DTR_EIN, DTR_AUS, RTS_ABFRAGE, RTS_EIN, RTS_AUS,
                            EINFLUSS_ABFRAGE, EINFLUSS_KEINE, EINFLUSS_XONXOFF, EINFLUSS_HARDWARE,
                            FLUSS_DCD, FLUSS_DTR, FLUSS_DSR};
    for (uint8_t w : alle) {
        Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
        c.sendeSteuerung(w);
        uebertrage(c, s, true);
        auto e = eins(s);
        EXPECT_EQ(e.art, Art::Steuerung);
        EXPECT_EQ(e.wert, w);
        EXPECT_FALSE(e.antwort);
        // Server antwortet (Flussart: aktuelle Einstellung)
        s.sendeSteuerung(w, true);
        Bytes wire = s.nimmAusgabe();
        EXPECT_EQ(wire[3], 105);
        c.eingabe(wire);
        auto a = eins(c);
        EXPECT_TRUE(a.antwort);
        EXPECT_EQ(a.wert, w);
    }
    EXPECT_EQ(RTS_EIN, 11);
    EXPECT_EQ(RTS_AUS, 12);
    EXPECT_EQ(DTR_EIN, 8);
    EXPECT_EQ(DTR_AUS, 9);
    EXPECT_EQ(BREAK_EIN, 5);
    EXPECT_EQ(BREAK_AUS, 6);
}

TEST(Rfc2217Codec, NotifyWirdAlsBefehl106Und107Gesendet) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    // Vorgabe-Maske Modem 0xFF: durchgelassen.
    EXPECT_TRUE(s.sendeModemState(MS_CTS | MS_DELTA_CTS));
    EXPECT_TRUE(s.sendeLineState(0x60, /*ungefiltert=*/true));  // Line-Maske Vorgabe 0 → nur ungefiltert
    Bytes w = s.nimmAusgabe();
    EXPECT_EQ(w, (Bytes{IAC, SB, 44, 107, 0x11, IAC, SE, IAC, SB, 44, 106, 0x60, IAC, SE}));
    c.eingabe(w);
    auto ev = c.nimmEreignisse();
    ASSERT_EQ(ev.size(), 2u);
    EXPECT_EQ(ev[0].art, Art::ModemState);
    EXPECT_TRUE(ev[0].antwort);
    EXPECT_EQ(ev[0].wert, 0x11u);
    EXPECT_EQ(ev[1].art, Art::LineState);
    EXPECT_EQ(ev[1].wert, 0x60u);
}

TEST(Rfc2217Codec, ModemstateByteMitFFWirdVerdoppelt) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    s.sendeModemState(0xFF);
    Bytes w = s.nimmAusgabe();
    EXPECT_EQ(w, (Bytes{IAC, SB, 44, 107, IAC, IAC, IAC, SE}));
    c.eingabe(w);
    EXPECT_EQ(eins(c).wert, 0xFFu);
}

TEST(Rfc2217Codec, MaskenSteuernDieNotifies) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    // Client setzt Masken: Modem nur CTS-Änderung (Bit 0x01), Line 0x02
    c.sendeModemStateMaske(MS_DELTA_CTS);
    c.sendeLineStateMaske(0x02);
    uebertrage(c, s);
    auto ev = s.nimmEreignisse();
    ASSERT_EQ(ev.size(), 2u);
    EXPECT_EQ(ev[0].art, Art::ModemStateMaske);
    EXPECT_EQ(ev[0].wert, 0x01u);
    EXPECT_EQ(ev[1].art, Art::LineStateMaske);
    EXPECT_EQ(s.modemStateMaske(), 0x01);
    EXPECT_EQ(s.lineStateMaske(), 0x02);

    EXPECT_FALSE(s.sendeModemState(MS_DELTA_DSR | MS_DSR));  // nicht maskiert → nichts
    EXPECT_FALSE(s.hatAusgabe());
    EXPECT_TRUE(s.sendeModemState(MS_DELTA_CTS | MS_CTS | MS_DSR));
    EXPECT_TRUE(s.hatAusgabe());
    s.nimmAusgabe();
    EXPECT_FALSE(s.sendeLineState(0x01));
    EXPECT_TRUE(s.sendeLineState(0x03));
    s.nimmAusgabe();
    // Ungefiltert geht immer (Anfangszustand)
    EXPECT_TRUE(s.sendeModemState(MS_DSR, true));
}

TEST(Rfc2217Codec, MaskenAntwortDesServersAendertDieMaskenDesClientsNicht) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    s.sendeModemStateMaske(0x55, true);  // Server-Antwort 111
    Bytes w = s.nimmAusgabe();
    EXPECT_EQ(w[3], 111);
    c.eingabe(w);
    EXPECT_EQ(c.modemStateMaske(), 0xFF);
    auto e = eins(c);
    EXPECT_TRUE(e.antwort);
    EXPECT_EQ(e.wert, 0x55u);
}

TEST(Rfc2217Codec, FlussHaltUndWeiterSowiePurge) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    c.sendeFlussHalt(true);
    c.sendeFlussHalt(false);
    c.sendeLeeren(PURGE_EMPFANG);
    c.sendeLeeren(PURGE_SENDEN);
    c.sendeLeeren(PURGE_BEIDE);
    uebertrage(c, s, true);
    auto ev = s.nimmEreignisse();
    ASSERT_EQ(ev.size(), 5u);
    EXPECT_EQ(ev[0].art, Art::FlussHalt);
    EXPECT_EQ(ev[1].art, Art::FlussWeiter);
    EXPECT_EQ(ev[2].art, Art::Leeren);
    EXPECT_EQ(ev[2].wert, 1u);
    EXPECT_EQ(ev[3].wert, 2u);
    EXPECT_EQ(ev[4].wert, 3u);
    s.sendeLeeren(PURGE_BEIDE, true);
    s.sendeFlussHalt(true, true);
    Bytes w = s.nimmAusgabe();
    EXPECT_EQ(w, (Bytes{IAC, SB, 44, 112, 3, IAC, SE, IAC, SB, 44, 108, IAC, SE}));
    c.eingabe(w);
    auto a = c.nimmEreignisse();
    ASSERT_EQ(a.size(), 2u);
    EXPECT_TRUE(a[0].antwort);
    EXPECT_EQ(a[1].art, Art::FlussHalt);
}

TEST(Rfc2217Codec, NutzdatenLaufNebenDenBefehlenUndNichtVermischt) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    verbinde(c, s);
    c.sende(Bytes{'A', 0xFF, 'B'});
    c.sendeBaud(1200);
    c.sende(Bytes{'C'});
    Bytes w = c.nimmAusgabe();
    for (uint8_t x : w) s.eingabe(&x, 1);  // byteweise
    EXPECT_EQ(s.nimmNutzdaten(), (Bytes{'A', 0xFF, 'B', 'C'}));
    EXPECT_EQ(eins(s).wert, 1200u);
}

TEST(Rfc2217Codec, KurzeUndFremdeBefehleWerdenUeberlesen) {
    Rfc2217Codec s(TelnetRolle::Server);
    // SET-BAUDRATE mit nur 2 Byte, unbekannter Befehl 77, leere SB, SB fremder Option
    s.eingabe(Bytes{IAC, SB, 44, 1, 0, 1, IAC, SE, IAC, SB, 44, 77, 9, IAC, SE, IAC, SB, 44, IAC, SE,
                    IAC, SB, 99, 1, 2, IAC, SE});
    EXPECT_TRUE(s.nimmEreignisse().empty());
}

TEST(Rfc2217Codec, ParitaetUndStoppbitsUmrechnungZurSerialFormat) {
    EXPECT_EQ(paritaetNetz(0), 1);  // N
    EXPECT_EQ(paritaetNetz(1), 2);  // O
    EXPECT_EQ(paritaetNetz(2), 3);  // E
    EXPECT_EQ(paritaetSio(1), 0);
    EXPECT_EQ(paritaetSio(2), 1);
    EXPECT_EQ(paritaetSio(3), 2);
    EXPECT_EQ(paritaetSio(4), 255);  // Mark: nicht abbildbar
    EXPECT_EQ(stoppNetz(2), 1);
    EXPECT_EQ(stoppNetz(3), 3);
    EXPECT_EQ(stoppNetz(4), 2);
    EXPECT_EQ(stoppHalbe(1), 2);
    EXPECT_EQ(stoppHalbe(3), 3);
    EXPECT_EQ(stoppHalbe(2), 4);
}

TEST(Rfc2217Codec, TelnetSchichtDarunterBleibtTransparent) {
    Rfc2217Codec c(TelnetRolle::Client), s(TelnetRolle::Server);
    verbinde(c, s);
    // BINARY läuft → CR ohne NUL
    s.sende(Bytes{0x0D, 0x0A});
    EXPECT_EQ(s.nimmAusgabe(), (Bytes{0x0D, 0x0A}));
    // Unbekannte Telnet-Option wird abgelehnt
    s.eingabe(Bytes{IAC, DO, 31});
    EXPECT_EQ(s.nimmAusgabe(), (Bytes{IAC, WONT, 31}));
}
