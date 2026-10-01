/**
 * @file test_telnet_codec.cpp
 * @brief Unit-Tests für den Telnet-Zustandsautomaten (`core/serial/telnet_codec.h`).
 *
 * @details
 * Doc: doc/design/19_serielle_schnittstellen.md §7.4, §11.
 * Gruppen: IAC-Verdopplung, zerrissene Sequenzen (byteweise an jeder Stelle),
 * Verhandlung Server/Client (kein Pingpong, Ablehnung), CR NUL, roher Strom,
 * Unterverhandlung.
 */
#include <gtest/gtest.h>

#include "core/serial/telnet_codec.h"

using namespace serial;
using namespace serial::telnet;
using Bytes = std::vector<uint8_t>;

namespace {

void speise(TelnetCodec& c, const Bytes& b) { c.eingabe(b); }

void speiseByteweise(TelnetCodec& c, const Bytes& b) {
    for (uint8_t x : b) c.eingabe(&x, 1);
}

// Zwei Codecs über Kreuz verbinden, bis beide still sind.  Liefert die Zahl
// der Runden; mehr als ein paar heißt Pingpong.
int pumpe(TelnetCodec& a, TelnetCodec& b, Bytes* anB = nullptr, Bytes* anA = nullptr) {
    int runden = 0;
    while ((a.hatAusgabe() || b.hatAusgabe()) && runden < 100) {
        Bytes x = a.nimmAusgabe(), y = b.nimmAusgabe();
        if (anB) anB->insert(anB->end(), x.begin(), x.end());
        if (anA) anA->insert(anA->end(), y.begin(), y.end());
        b.eingabe(x);
        a.eingabe(y);
        ++runden;
    }
    return runden;
}

}  // namespace

TEST(TelnetCodec, ServerStartSendetDieVierAngebote) {
    TelnetCodec s(TelnetRolle::Server);
    s.start();
    s.start();  // zweiter Aufruf darf nichts doppelt senden
    Bytes soll = {IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA, IAC, WILL, OPT_BINARY, IAC, DO, OPT_BINARY};
    EXPECT_EQ(s.nimmAusgabe(), soll);
}

TEST(TelnetCodec, ClientStartSchweigt) {
    TelnetCodec c(TelnetRolle::Client);
    c.start();
    EXPECT_TRUE(c.nimmAusgabe().empty());
}

TEST(TelnetCodec, IacWirdBeimSendenVerdoppelt) {
    TelnetCodec c(TelnetRolle::Client);
    c.sende(Bytes{0x41, 0xFF, 0xFF, 0x42});
    EXPECT_EQ(c.nimmAusgabe(), (Bytes{0x41, 0xFF, 0xFF, 0xFF, 0xFF, 0x42}));
}

TEST(TelnetCodec, IacIacWirdBeimEmpfangZuEinemByte) {
    TelnetCodec c(TelnetRolle::Client);
    speise(c, {0x41, IAC, IAC, 0x42});
    EXPECT_EQ(c.nimmNutzdaten(), (Bytes{0x41, 0xFF, 0x42}));
    EXPECT_TRUE(c.nimmAusgabe().empty());
}

TEST(TelnetCodec, ZerrissenBytewiseAnJederStelleGleichesErgebnis) {
    // Nutzdaten, IAC IAC, Verhandlung und Unterverhandlung (mit 0xFF) gemischt.
    Bytes strom = {'a', IAC, IAC, 'b', IAC, WILL, OPT_SGA, 'c', IAC, SB, 44, 1, IAC, IAC, 2, IAC, SE, 'd'};
    TelnetCodec ganz(TelnetRolle::Client), teil(TelnetRolle::Client);
    speise(ganz, strom);
    speiseByteweise(teil, strom);
    EXPECT_EQ(ganz.nimmNutzdaten(), (Bytes{'a', 0xFF, 'b', 'c', 'd'}));
    EXPECT_EQ(teil.nimmNutzdaten(), (Bytes{'a', 0xFF, 'b', 'c', 'd'}));
    EXPECT_EQ(ganz.nimmAusgabe(), (Bytes{IAC, DO, OPT_SGA}));
    EXPECT_EQ(teil.nimmAusgabe(), (Bytes{IAC, DO, OPT_SGA}));
    auto sg = ganz.nimmSub(), st = teil.nimmSub();
    ASSERT_EQ(sg.size(), 1u);
    ASSERT_EQ(st.size(), 1u);
    EXPECT_EQ(st[0].option, 44);
    EXPECT_EQ(st[0].daten, (Bytes{1, 0xFF, 2}));
    EXPECT_EQ(sg[0].daten, st[0].daten);

    // und an jeder einzelnen Schnittstelle in zwei Stücke gerissen
    for (size_t k = 0; k <= strom.size(); ++k) {
        TelnetCodec c(TelnetRolle::Client);
        c.eingabe(strom.data(), k);
        c.eingabe(strom.data() + k, strom.size() - k);
        EXPECT_EQ(c.nimmNutzdaten(), (Bytes{'a', 0xFF, 'b', 'c', 'd'})) << "Schnitt " << k;
        EXPECT_EQ(c.nimmAusgabe(), (Bytes{IAC, DO, OPT_SGA})) << "Schnitt " << k;
    }
}

TEST(TelnetCodec, ServerClientHandschlagEndetOhnePingpong) {
    TelnetCodec s(TelnetRolle::Server), c(TelnetRolle::Client);
    s.start();
    c.start();
    int runden = pumpe(s, c);
    EXPECT_LE(runden, 4);
    // Server: Echo/SGA/BINARY in Senderichtung, BINARY auch beim Client.
    EXPECT_TRUE(s.lokalAktiv(OPT_ECHO));
    EXPECT_TRUE(s.lokalAktiv(OPT_SGA));
    EXPECT_TRUE(s.lokalAktiv(OPT_BINARY));
    EXPECT_TRUE(s.entferntAktiv(OPT_BINARY));
    EXPECT_TRUE(c.lokalAktiv(OPT_BINARY));
    EXPECT_TRUE(c.entferntAktiv(OPT_BINARY));
    EXPECT_TRUE(c.entferntAktiv(OPT_ECHO));
    EXPECT_TRUE(c.entferntAktiv(OPT_SGA));
    // Danach ist Ruhe.
    EXPECT_FALSE(s.hatAusgabe());
    EXPECT_FALSE(c.hatAusgabe());
}

TEST(TelnetCodec, UnbekannteOptionenWerdenAbgelehnt) {
    TelnetCodec s(TelnetRolle::Server);
    speise(s, {IAC, DO, 24 /*TERMINAL-TYPE*/, IAC, WILL, 31 /*NAWS*/});
    EXPECT_EQ(s.nimmAusgabe(), (Bytes{IAC, WONT, 24, IAC, DONT, 31}));
    // Wiederholtes Verlangen wird ebenso beantwortet, bringt aber keinen Zustand.
    speise(s, {IAC, DONT, 24, IAC, WONT, 31});
    EXPECT_TRUE(s.nimmAusgabe().empty());
}

TEST(TelnetCodec, GegenseiteDieJedeAntwortMitGegenfrageBeantwortetDrehtKeineSchleife) {
    // Bösartige Gegenseite: auf jedes WONT/DONT ein neues WILL/DO.  Die
    // Q-Methode antwortet auf eine Ablehnung nie mit derselben Anfrage.
    TelnetCodec s(TelnetRolle::Server);
    int gesendet = 0;
    speise(s, {IAC, DO, 24});
    for (int i = 0; i < 50; ++i) {
        Bytes a = s.nimmAusgabe();
        gesendet += int(a.size());
        // Antwort auf unser WONT 24 ist ein erneutes DO 24
        speise(s, {IAC, DO, 24});
    }
    // Jedes DO wird genau einmal mit WONT beantwortet (3 Bytes), nie mehr.
    EXPECT_EQ(gesendet, 50 * 3);
    EXPECT_FALSE(s.lokalAktiv(24));
}

TEST(TelnetCodec, ErbetenesUndAbgelehntesFuehrtNichtZurueck) {
    TelnetCodec s(TelnetRolle::Server);
    s.erlaube(24, true, false);
    s.bieteAn(24);
    EXPECT_EQ(s.nimmAusgabe(), (Bytes{IAC, WILL, 24}));
    speise(s, {IAC, DONT, 24});  // Ablehnung
    EXPECT_TRUE(s.nimmAusgabe().empty());
    EXPECT_FALSE(s.lokalAktiv(24));
}

TEST(TelnetCodec, CrNulWirdZuCrOhneBinary) {
    TelnetCodec c(TelnetRolle::Client);
    speise(c, {'a', 0x0D, 0x00, 'b', 0x0D, 0x0A, 0x0D, 0x0D, 0x00});
    EXPECT_EQ(c.nimmNutzdaten(), (Bytes{'a', 0x0D, 'b', 0x0D, 0x0A, 0x0D, 0x0D}));
}

TEST(TelnetCodec, CrNulZerrissenZwischenCrUndNul) {
    TelnetCodec c(TelnetRolle::Client);
    speise(c, {0x0D});
    EXPECT_EQ(c.nimmNutzdaten(), (Bytes{0x0D}));
    speise(c, {0x00, 'x'});
    EXPECT_EQ(c.nimmNutzdaten(), (Bytes{'x'}));
}

TEST(TelnetCodec, CrWirdBeimSendenZuCrNulOhneBinary) {
    TelnetCodec c(TelnetRolle::Client);
    c.sende(Bytes{'a', 0x0D, 0x0A});
    EXPECT_EQ(c.nimmAusgabe(), (Bytes{'a', 0x0D, 0x00, 0x0A}));
}

TEST(TelnetCodec, MitBinaryKeineCrUebersetzungInBeideRichtungen) {
    TelnetCodec s(TelnetRolle::Server), c(TelnetRolle::Client);
    s.start();
    c.start();
    pumpe(s, c);
    s.sende(Bytes{0x0D, 0x00, 0x0A});
    EXPECT_EQ(s.nimmAusgabe(), (Bytes{0x0D, 0x00, 0x0A}));  // unverändert: CR, NUL, LF
    speise(c, {0x0D, 0x00, 0x0A});
    EXPECT_EQ(c.nimmNutzdaten(), (Bytes{0x0D, 0x00, 0x0A}));  // NUL bleibt
}

TEST(TelnetCodec, RoherStromOhneIacIstTransparent) {
    TelnetCodec c(TelnetRolle::Server);
    c.start();
    c.nimmAusgabe();
    Bytes roh = {'h', 'a', 'l', 'l', 'o', 0x0A, 0x01, 0x7F, 0x80, 0xFE};
    speise(c, roh);
    EXPECT_EQ(c.nimmNutzdaten(), roh);
    EXPECT_FALSE(c.hatAusgabe());
}

TEST(TelnetCodec, UnterverhandlungFremderOptionWirdGeliefertNichtBeantwortet) {
    TelnetCodec c(TelnetRolle::Server);
    speise(c, {IAC, SB, 99, 1, 2, 3, IAC, SE, 'z'});
    auto sub = c.nimmSub();
    ASSERT_EQ(sub.size(), 1u);
    EXPECT_EQ(sub[0].option, 99);
    EXPECT_EQ(sub[0].daten, (Bytes{1, 2, 3}));
    EXPECT_EQ(c.nimmNutzdaten(), (Bytes{'z'}));
    EXPECT_TRUE(c.nimmAusgabe().empty());
    EXPECT_TRUE(c.nimmSub().empty());  // abgeholt
}

TEST(TelnetCodec, SendeSubVerdoppeltIacInDaten) {
    TelnetCodec c(TelnetRolle::Client);
    c.sendeSub(44, Bytes{1, 0xFF, 2});
    EXPECT_EQ(c.nimmAusgabe(), (Bytes{IAC, SB, 44, 1, IAC, IAC, 2, IAC, SE}));
}

TEST(TelnetCodec, ZuLangeUnterverhandlungWirdVerworfenStromBleibtIntakt) {
    TelnetCodec c(TelnetRolle::Client);
    Bytes b = {IAC, SB, 44};
    b.insert(b.end(), TelnetCodec::SUB_MAX + 100, 0x55);
    b.insert(b.end(), {IAC, SE, 'k'});
    speise(c, b);
    EXPECT_TRUE(c.nimmSub().empty());
    EXPECT_EQ(c.nimmNutzdaten(), (Bytes{'k'}));
}

TEST(TelnetCodec, UngueltigesEndeInUnterverhandlungVerwirftSieUndLiestBefehl) {
    TelnetCodec c(TelnetRolle::Client);
    speise(c, {IAC, SB, 44, 1, IAC, WILL, OPT_SGA, 'x'});
    EXPECT_TRUE(c.nimmSub().empty());
    EXPECT_EQ(c.nimmAusgabe(), (Bytes{IAC, DO, OPT_SGA}));
    EXPECT_EQ(c.nimmNutzdaten(), (Bytes{'x'}));
}

TEST(TelnetCodec, NopUndAndereEinzelbefehleWerdenUeberlesen) {
    TelnetCodec c(TelnetRolle::Client);
    speise(c, {'a', IAC, 241 /*NOP*/, 'b', IAC, 246 /*AYT*/, 'c'});
    EXPECT_EQ(c.nimmNutzdaten(), (Bytes{'a', 'b', 'c'}));
}

// ─── AP-T1a: Zustände der Q-Methode, die der Abdeckungsbau ungeprüft fand ──────

/// RFC 1143: ein zweiter eigener Wunsch, solange der erste unbeantwortet ist, geht
/// NICHT noch einmal hinaus; die Bestätigung schaltet ein, ohne Antwort.
TEST(TelnetCodec, WiederholterWunschVorDerAntwortGehtNurEinmalHinaus) {
    TelnetCodec c(TelnetRolle::Client);
    c.bieteAn(OPT_SGA);
    c.bieteAn(OPT_SGA);
    c.verlange(OPT_ECHO);
    c.verlange(OPT_ECHO);
    EXPECT_EQ(c.nimmAusgabe(), (Bytes{IAC, WILL, OPT_SGA, IAC, DO, OPT_ECHO}));
    speise(c, Bytes{IAC, DO, OPT_SGA, IAC, WILL, OPT_ECHO});
    EXPECT_TRUE(c.lokalAktiv(OPT_SGA));
    EXPECT_TRUE(c.entferntAktiv(OPT_ECHO));
    EXPECT_TRUE(c.nimmAusgabe().empty()) << "Bestätigung auf eigene Anfrage bleibt stumm";
    c.bieteAn(OPT_SGA);
    EXPECT_TRUE(c.nimmAusgabe().empty()) << "schon aktiv: nichts zu sagen";
}

/// Schaltet die Gegenseite eine aktive Option ab (WONT/DONT), wird das genau einmal
/// bestätigt; ein wiederholtes WONT/DONT bleibt stumm (kein Pingpong).
TEST(TelnetCodec, AbschaltenDurchDieGegenseiteWirdEinmalBestaetigt) {
    TelnetCodec c(TelnetRolle::Client);
    c.bieteAn(OPT_SGA);
    c.verlange(OPT_ECHO);
    speise(c, Bytes{IAC, DO, OPT_SGA, IAC, WILL, OPT_ECHO});
    c.nimmAusgabe();
    speise(c, Bytes{IAC, WONT, OPT_ECHO, IAC, DONT, OPT_SGA});
    EXPECT_FALSE(c.entferntAktiv(OPT_ECHO));
    EXPECT_FALSE(c.lokalAktiv(OPT_SGA));
    EXPECT_EQ(c.nimmAusgabe(), (Bytes{IAC, DONT, OPT_ECHO, IAC, WONT, OPT_SGA}));
    speise(c, Bytes{IAC, WONT, OPT_ECHO, IAC, DONT, OPT_SGA});
    EXPECT_TRUE(c.nimmAusgabe().empty());
}

/// Lehnt die Gegenseite einen eigenen Wunsch ab (WillJa + WONT/DONT), ist die Option
/// aus, ohne Antwort — und ein späterer Wunsch fragt erneut.
TEST(TelnetCodec, AbgelehnterWunschIstAusUndDarfErneutGefragtWerden) {
    TelnetCodec c(TelnetRolle::Client);
    c.verlange(OPT_ECHO);
    c.nimmAusgabe();
    speise(c, Bytes{IAC, WONT, OPT_ECHO});
    EXPECT_FALSE(c.entferntAktiv(OPT_ECHO));
    EXPECT_TRUE(c.nimmAusgabe().empty());
    c.verlange(OPT_ECHO);
    EXPECT_EQ(c.nimmAusgabe(), (Bytes{IAC, DO, OPT_ECHO}));
}
