/**
 * @file test_serial_wandler.cpp
 * @brief Unit-Tests des Wandlers (Entwurf 19 §6) mit Attrappe statt Karte und statt
 *        Socket, dazu die Formatrechnung (§6.2) und die RFC-2217-Leitungen der
 *        Transporte ohne Netz (§6.4).
 *
 * Die „Gegenseite" ist hier der Test selbst (`fernGib`/`fernNimm`) bzw. ein
 * `Rfc2217Codec` der Gegenrolle.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/primitives/z80_sio.h"
#include "core/serial/sio_format.h"
#include "core/serial/transport.h"
#include "core/serial/wandler.h"
#include "tests/unit/util/attrappe_anschluss.h"

using namespace k1520::serial;
using k1520test::AttrappeAnschluss;
namespace r2 = ::serial::rfc2217;

namespace {

constexpr uint64_t Z9600 = 2560;    // 10 Bit × 16 × 16
constexpr uint64_t Z1200 = 20480;   // 10 Bit × 16 × 128

/// Maschine laufen lassen: `takte` Takte in Schritten von `schritt`; `lesen` = der Gast
/// holt sein FIFO nach jedem Schritt ab.
void laufe(Wandler& w, AttrappeAnschluss& a, uint64_t& z, uint64_t takte, bool lesen = true,
           uint64_t schritt = 16) {
    const uint64_t ende = z + takte;
    for (; z < ende; z += schritt) {
        w.takt(z);
        if (lesen) a.lies();
    }
}

std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

}  // namespace

// ─── Formatrechnung (§6.2) ─────────────────────────────────────────────────

TEST(SerialFormat, RechnetZeichentakteUndNormbaud) {
    const SerialFormat f = serialFormatRechnen(16, 8, 0, 2, 16);
    EXPECT_TRUE(f.gueltig);
    EXPECT_EQ(f.baud_nenn, 9600u);
    EXPECT_EQ(f.zeichen_takte, Z9600);

    // 7E1½: (1 + 7 + 1) Bit + 1,5 Stopp = 10,5 Bit × 64 × 3
    const SerialFormat g = serialFormatRechnen(64, 7, 2, 3, 3);
    EXPECT_TRUE(g.gueltig);
    EXPECT_EQ(g.zeichen_takte, 21u * 64 * 3 / 2);
    EXPECT_EQ(g.baud_nenn, 12800u);   // keine Normrate in 2 % Nähe → ganzzahlig

    // ×1, CTC 21: 117028 Bd liegt 1,6 % neben 115200 → Normrate.
    EXPECT_EQ(serialFormatRechnen(1, 8, 0, 2, 21).baud_nenn, 115200u);
    EXPECT_EQ(normBaud(0), 0u);
    EXPECT_EQ(normBaud(1190), 1200u);
    EXPECT_EQ(normBaud(1300), 1300u);
}

TEST(SerialFormat, UnbekannterCtcOderSynchronIstUngueltig) {
    EXPECT_FALSE(serialFormatRechnen(16, 8, 0, 2, 0).gueltig);   // CTC nicht programmiert
    const SerialFormat s = serialFormatRechnen(1, 8, 0, 0, 16);   // Synchronbetrieb
    EXPECT_FALSE(s.gueltig);
    EXPECT_EQ(s.baud_nenn, 153600u);   // Anzeige bleibt möglich

    const SerialFormat e = ersatzFormat();
    EXPECT_TRUE(e.gueltig);
    EXPECT_EQ(e.baud_nenn, 9600u);
    EXPECT_EQ(e.zeichen_takte, Z9600);
}

TEST(SerialFormat, AusSioKanalUndCtcTeiler) {
    Z80SIO sio;
    auto& k = sio.channelA();
    k.wr[4] = 0x44;   // ×16, 1 Stoppbit, keine Parität
    k.wr[5] = 0x60;   // Tx 8 Bit
    k.wr[3] = 0xC0;   // Rx 8 Bit
    const SerialFormat f = serialFormatAusSio(k, 16);
    EXPECT_TRUE(f.gueltig);
    EXPECT_EQ(f.baud_nenn, 9600u);
    EXPECT_EQ(f.daten, 8);
    EXPECT_EQ(f.stopp_halbe, 2);
    EXPECT_EQ(f.zeichen_takte, Z9600);
    k.wr[4] = 0xCF;   // ×64, 2 Stoppbits, gerade Parität
    const SerialFormat g = serialFormatAusSio(k, 16);
    EXPECT_EQ(g.paritaet, 2);
    EXPECT_EQ(g.stopp_halbe, 4);
    EXPECT_EQ(g.baud_nenn, 2400u);
}

// ─── Wandler ────────────────────────────────────────────────────────────────

TEST(SerialWandler, SendetImZeichentakt) {
    AttrappeAnschluss a;
    a.fmt = serialFormatRechnen(16, 8, 0, 2, 128);   // 1200 Bd
    Wandler w(a);
    w.anbinden();
    a.sende("ABCDE");
    std::vector<uint64_t> zeiten;
    size_t belegt = 0;
    for (uint64_t z = 0; z < 6 * Z1200; ++z) {
        w.takt(z);
        if (w.fernBelegt() != belegt) {
            belegt = w.fernBelegt();
            zeiten.push_back(z);
        }
    }
    ASSERT_EQ(zeiten.size(), 5u);
    for (size_t i = 1; i < zeiten.size(); ++i) {
        const uint64_t d = zeiten[i] - zeiten[i - 1];
        EXPECT_GE(d, Z1200);
        EXPECT_LE(d, Z1200 + Z1200 / 16);
    }
    uint8_t buf[8];
    ASSERT_EQ(w.fernNimm(buf, sizeof buf), 5u);
    EXPECT_EQ(std::string(buf, buf + 5), "ABCDE");
    EXPECT_EQ(w.sicht().bytes_gesendet, 5u);
}

TEST(SerialWandler, EmpfaengtImZeichentakt) {
    AttrappeAnschluss a;
    Wandler w(a);
    w.anbinden();
    const auto d = bytes("12345");
    ASSERT_EQ(w.fernGib(d.data(), d.size()), 5u);
    std::vector<uint64_t> zeiten;
    for (uint64_t z = 0; z < 6 * Z9600; ++z) {
        w.takt(z);
        if (!a.fifo.empty()) {
            zeiten.push_back(z);
            a.lies();
        }
    }
    ASSERT_EQ(zeiten.size(), 5u);
    for (size_t i = 1; i < zeiten.size(); ++i) {
        EXPECT_GE(zeiten[i] - zeiten[i - 1], Z9600);
        EXPECT_LE(zeiten[i] - zeiten[i - 1], Z9600 + Z9600 / 16);
    }
    EXPECT_EQ(a.gelesenText(), "12345");
}

TEST(SerialWandler, RueckstauBeimSendenHaeltDenGastAn) {
    AttrappeAnschluss a;
    Wandler w(a);
    w.anbinden();
    a.sende(std::vector<uint8_t>(5000, 0x55));
    uint64_t z = 0;
    laufe(w, a, z, 5100 * Z9600, false, 160);
    EXPECT_EQ(w.fernBelegt(), Wandler::PUFFER);
    EXPECT_EQ(a.gastSendet.size(), 5000 - Wandler::PUFFER);   // der Gast wartet auf TxEmpty
    EXPECT_EQ(w.sicht().bytes_gesendet, Wandler::PUFFER);
    // Die Gegenseite nimmt 100 ab → genau 100 rücken nach, keines geht verloren.
    uint8_t buf[100];
    ASSERT_EQ(w.fernNimm(buf, sizeof buf), 100u);
    laufe(w, a, z, 200 * Z9600, false, 160);
    EXPECT_EQ(w.fernBelegt(), Wandler::PUFFER);
    EXPECT_EQ(a.gastSendet.size(), 5000 - Wandler::PUFFER - 100);
}

TEST(SerialWandler, RueckstauBeimEmpfangenOhneUeberlauf) {
    AttrappeAnschluss a;
    Wandler w(a);
    w.anbinden();
    std::vector<uint8_t> d(5000);
    for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<uint8_t>(i * 7);
    // Der Empfangspuffer nimmt 4 KiB, der Rest bleibt bei der Gegenseite (TCP-Fenster).
    EXPECT_EQ(w.fernGib(d.data(), d.size()), Wandler::PUFFER);
    EXPECT_EQ(w.fernFrei(), 0u);

    // Der Gast liest NICHT: das FIFO füllt sich auf 3 — und nicht weiter.
    uint64_t z = 0;
    laufe(w, a, z, 100 * Z9600, false);
    EXPECT_EQ(a.fifo.size(), 3u);
    EXPECT_EQ(a.ueberlauf, 0u);

    // Der Gast liest wieder; der Rest wird nachgereicht, sobald Platz ist.
    size_t gegeben = Wandler::PUFFER;
    while (a.gelesen.size() + a.fifo.size() < d.size()) {
        laufe(w, a, z, 64 * Z9600);
        if (gegeben < d.size()) gegeben += w.fernGib(d.data() + gegeben, d.size() - gegeben);
        ASSERT_LT(z, 20000 * Z9600) << "Zustellung stockt";
    }
    a.lies();
    EXPECT_EQ(a.ueberlauf, 0u);
    EXPECT_EQ(a.gelesen, d);
    EXPECT_EQ(w.sicht().bytes_empfangen, d.size());
}

TEST(SerialWandler, LangsamerGastBekommtNieEinenUeberlauf) {
    AttrappeAnschluss a;
    a.tiefe = 1;   // strenger als der SIO
    Wandler w(a);
    w.anbinden();
    const auto d = bytes("Der Gast liest nur selten, verliert aber nichts.");
    w.fernGib(d.data(), d.size());
    uint64_t z = 0;
    for (int i = 0; i < 400 && a.gelesen.size() < d.size(); ++i) {
        laufe(w, a, z, 3 * Z9600, false);   // drei Zeichenzeiten nicht lesen
        a.lies();
    }
    EXPECT_EQ(a.ueberlauf, 0u);
    EXPECT_EQ(a.gelesen, d);
}

TEST(SerialWandler, XoffHaeltDenEmpfangAnBisXon) {
    AttrappeAnschluss a;
    Wandler w(a);
    WandlerEinstellung e;
    e.xonxoff = true;
    w.einstellen(e);
    w.anbinden();
    a.sende(std::vector<uint8_t>{0x13});   // XOFF
    uint64_t z = 0;
    laufe(w, a, z, Z9600);                 // XOFF ist hinaus
    const auto d = bytes("abc");
    w.fernGib(d.data(), d.size());
    laufe(w, a, z, 20 * Z9600);
    EXPECT_TRUE(a.gelesen.empty()) << "nach XOFF darf nichts zugestellt werden";
    EXPECT_TRUE(w.sicht().xoffHalt);
    uint8_t buf[4];
    ASSERT_EQ(w.fernNimm(buf, sizeof buf), 1u);   // das XOFF selbst geht trotzdem hinaus
    EXPECT_EQ(buf[0], 0x13);

    a.sende(std::vector<uint8_t>{0x11});   // XON
    laufe(w, a, z, 20 * Z9600);
    EXPECT_EQ(a.gelesenText(), "abc");

    // Ohne „XON/XOFF beachten" läuft 0x13 einfach durch (Binärübertragung).
    e.xonxoff = false;
    w.einstellen(e);
    a.sende(std::vector<uint8_t>{0x13});
    w.fernGib(d.data(), d.size());
    laufe(w, a, z, 20 * Z9600);
    EXPECT_EQ(a.gelesenText(), "abcabc");
}

TEST(SerialWandler, RtsWegHaeltDenEmpfangAnNurBeiV24) {
    AttrappeAnschluss a;
    a.rtsAus = true;   // „rtsAus" = Ausgang RTS aktiv
    Wandler w(a);
    w.anbinden();
    uint64_t z = 0;
    laufe(w, a, z, Z9600);   // der Gast benutzt RTS …
    a.rtsAus = false;        // … und nimmt es weg
    const auto d = bytes("xyz");
    w.fernGib(d.data(), d.size());
    laufe(w, a, z, 20 * Z9600);
    EXPECT_TRUE(a.gelesen.empty());
    a.rtsAus = true;
    laufe(w, a, z, 20 * Z9600);
    EXPECT_EQ(a.gelesenText(), "xyz");

    // IFSS: keine Leitungen, also auch kein RTS-Halt.
    AttrappeAnschluss b;
    b.istV24 = false;
    b.rtsAus = false;
    Wandler wb(b);
    wb.anbinden();
    wb.fernGib(d.data(), d.size());
    z = 0;
    laufe(wb, b, z, 20 * Z9600);
    EXPECT_EQ(b.gelesenText(), "xyz");
}

/// Befund AP-S5: SCPX 8915 schreibt WR5 = 68H (RTS und DTR aus), CP/A fasst den
/// V.24-Kanal nicht an.  Ein Gast, der RTS NIE setzt, benutzt keine Hardware-
/// Flusssteuerung — der RTS-Halt gilt erst, wenn RTS einmal aktiv war (seit dem
/// Anbinden bzw. dem letzten Reset des Gastes).
TEST(SerialWandler, RtsNieGesetztHaeltNicht) {
    AttrappeAnschluss a;
    a.rtsAus = false;
    Wandler w(a);
    w.anbinden();
    const auto d = bytes("abc");
    w.fernGib(d.data(), d.size());
    uint64_t z = 0;
    laufe(w, a, z, 10 * Z9600);
    EXPECT_EQ(a.gelesenText(), "abc");

    // Einmal benutzt → Halt; Reset des Gastes hebt ihn auf.
    a.rtsAus = true;
    laufe(w, a, z, Z9600);
    a.rtsAus = false;
    w.fernGib(d.data(), d.size());
    laufe(w, a, z, 10 * Z9600);
    EXPECT_EQ(a.gelesenText(), "abc");
    w.gastZurueckgesetzt();
    laufe(w, a, z, 10 * Z9600);
    EXPECT_EQ(a.gelesenText(), "abcabc");
}

/// Der Wandler meldet der Karte, ob Loop oder Transport den Stecker belegen (der alte
/// Unterbau der Karten schweigt dann).
TEST(SerialWandler, MeldetDieBelegungDesSteckers) {
    AttrappeAnschluss a;
    Wandler w(a);
    uint64_t z = 0;
    laufe(w, a, z, Z9600);
    EXPECT_EQ(a.belegtMeldungen, (std::vector<bool>{false}));
    WandlerEinstellung e;
    e.loop = true;
    w.einstellen(e);
    laufe(w, a, z, Z9600);
    e.loop = false;
    w.einstellen(e);
    w.anbinden();
    laufe(w, a, z, Z9600);
    w.abbinden();
    laufe(w, a, z, Z9600);
    EXPECT_EQ(a.belegtMeldungen, (std::vector<bool>{false, true, false}));
}

TEST(SerialWandler, LoopBringtGesendetesNachEinerZeichenzeitZurueck) {
    AttrappeAnschluss a;
    a.dtrAus = false;
    Wandler w(a);
    WandlerEinstellung e;
    e.loop = true;
    w.einstellen(e);
    a.sende("hallo");
    uint64_t erstes = 0;
    for (uint64_t z = 0; z < 8 * Z9600; ++z) {
        w.takt(z);
        if (!a.fifo.empty() && a.gelesen.empty()) erstes = z;
        a.lies();
    }
    EXPECT_EQ(a.gelesenText(), "hallo");
    EXPECT_GE(erstes, Z9600);
    EXPECT_EQ(w.sicht().bytes_gesendet, 5u);
    // Prüfstecker bei V.24: RTS→CTS, DTR→DSR/DCD.
    EXPECT_TRUE(a.cts);
    EXPECT_FALSE(a.dsr);
    EXPECT_FALSE(a.dcd);
    a.rtsAus = false;
    a.dtrAus = true;
    uint64_t z = 8 * Z9600;
    laufe(w, a, z, Z9600);
    EXPECT_FALSE(a.cts);
    EXPECT_TRUE(a.dsr);
    EXPECT_TRUE(a.dcd);
}

TEST(SerialWandler, NichtsAngeschlossenLaesstZeichenImTaktVerfallen) {
    AttrappeAnschluss a;
    Wandler w(a);
    a.sende("abc");
    uint64_t z = 0;
    laufe(w, a, z, Z9600 - 32);
    EXPECT_EQ(a.gastSendet.size(), 2u);   // eins weg, das nächste erst nach einer Zeichenzeit
    laufe(w, a, z, 3 * Z9600);
    EXPECT_TRUE(a.gastSendet.empty());
    EXPECT_EQ(w.sicht().bytes_gesendet, 0u);
    EXPECT_EQ(w.fernBelegt(), 0u);
    EXPECT_FALSE(a.cts);
    EXPECT_FALSE(a.dsr);
    EXPECT_FALSE(a.dcd);

    // RTS/CTS-Brücke: auch ohne Verbindung CTS := RTS, DSR = DCD := DTR.
    WandlerEinstellung e;
    e.rtscts_bruecke = true;
    w.einstellen(e);
    laufe(w, a, z, Z9600);
    EXPECT_TRUE(a.cts);
    EXPECT_TRUE(a.dsr);
    EXPECT_TRUE(a.dcd);
}

TEST(SerialWandler, UngueltigesFormatTaktetMit9600_8N1) {
    AttrappeAnschluss a;
    a.fmt = serialFormatRechnen(16, 7, 2, 4, 0);   // CTC unbekannt
    Wandler w(a);
    w.anbinden();
    a.sende("ab");
    std::vector<uint64_t> zeiten;
    size_t belegt = 0;
    for (uint64_t z = 0; z < 3 * Z9600; ++z) {
        w.takt(z);
        if (w.fernBelegt() != belegt) {
            belegt = w.fernBelegt();
            zeiten.push_back(z);
        }
    }
    ASSERT_EQ(zeiten.size(), 2u);
    EXPECT_GE(zeiten[1] - zeiten[0], Z9600);
    EXPECT_LE(zeiten[1] - zeiten[0], Z9600 + Z9600 / 16);
    const WandlerSicht s = w.sicht();
    EXPECT_FALSE(s.format.gueltig);
    EXPECT_EQ(s.wirksam.baud_nenn, 9600u);
    EXPECT_EQ(s.wirksam.daten, 8);
}

TEST(SerialWandler, FormatwechselWirdErstNach100msGemeldet) {
    AttrappeAnschluss a;
    Wandler w(a);
    uint64_t z = 0;
    laufe(w, a, z, PHI_NENN / 5);
    const uint32_t stand = w.sicht().gemeldetStand;
    a.fmt = serialFormatRechnen(16, 8, 0, 2, 128);
    laufe(w, a, z, PHI_NENN / 10 - 4000);
    EXPECT_EQ(w.sicht().gemeldetStand, stand) << "vor Ablauf der 100 ms gemeldet";
    EXPECT_EQ(w.sicht().wirksam.baud_nenn, 1200u);   // getaktet wird sofort
    laufe(w, a, z, 8000);
    EXPECT_EQ(w.sicht().gemeldetStand, stand + 1);
    EXPECT_EQ(w.sicht().gemeldet.baud_nenn, 1200u);
}

TEST(SerialWandler, TaktquelleWirdImEmulationsfadenGewaehlt) {
    AttrappeAnschluss a;
    Wandler w(a);
    WandlerEinstellung e;
    e.taktquelle = 1;
    w.einstellen(e);
    EXPECT_EQ(a.taktquelle, -1);
    w.takt(0);
    EXPECT_EQ(a.taktquelle, 1);
}

// ─── RFC 2217 ohne Netz: Leitungen (§6.4) ──────────────────────────────────

namespace {
/// Transport und Codec der Gegenrolle aneinander (statt eines Sockets).
struct Draht {
    NetzTransport& t;
    ::serial::Rfc2217Codec& gegen;
    Wandler& w;
    AttrappeAnschluss& a;
    uint64_t z = 0;
    std::vector<::serial::Rfc2217Ereignis> ereignisse;

    void pumpe() {
        for (int i = 0; i < 4; ++i) {
            const auto zuGegen = t.nimmAusgabe();
            gegen.eingabe(zuGegen);
            const auto zuUns = gegen.nimmAusgabe();
            t.eingabe(zuUns.data(), zuUns.size());
            t.abgleich(w);
            laufe(w, a, z, Z9600);
            t.abgleich(w);
            for (auto& e : gegen.nimmEreignisse()) ereignisse.push_back(e);
        }
    }
    const ::serial::Rfc2217Ereignis* letztes(::serial::Rfc2217Ereignis::Art art) const {
        for (auto it = ereignisse.rbegin(); it != ereignisse.rend(); ++it)
            if (it->art == art) return &*it;
        return nullptr;
    }
};
}  // namespace

TEST(SerialWandler, Rfc2217ServerKreuztDieLeitungenWieEinNullmodem) {
    AttrappeAnschluss a;
    a.rtsAus = true;
    a.dtrAus = false;
    a.fmt = serialFormatRechnen(16, 8, 0, 2, 128);   // Gast 1200 Bd
    Wandler w(a);
    Rfc2217Transport srv(::serial::TelnetRolle::Server);
    ::serial::Rfc2217Codec cli(::serial::TelnetRolle::Client);
    srv.start(w);
    cli.start();
    Draht d{srv, cli, w, a};
    d.pumpe();

    // Client-RTS → unser CTS, Client-DTR → unser DSR und DCD.
    cli.sendeSteuerung(r2::RTS_AUS);
    cli.sendeSteuerung(r2::DTR_EIN);
    d.pumpe();
    EXPECT_FALSE(a.cts);
    EXPECT_TRUE(a.dsr);
    EXPECT_TRUE(a.dcd);
    cli.sendeSteuerung(r2::RTS_EIN);
    cli.sendeSteuerung(r2::DTR_AUS);
    d.pumpe();
    EXPECT_TRUE(a.cts);
    EXPECT_FALSE(a.dsr);
    EXPECT_FALSE(a.dcd);

    // Unser RTS → sein CTS, unser DTR → DSR + CD (NOTIFY-MODEMSTATE).
    const auto* m = d.letztes(::serial::Rfc2217Ereignis::Art::ModemState);
    ASSERT_NE(m, nullptr);
    EXPECT_TRUE(m->wert & r2::MS_CTS);
    EXPECT_FALSE(m->wert & r2::MS_DSR);
    EXPECT_FALSE(m->wert & r2::MS_CD);
    a.rtsAus = false;
    a.dtrAus = true;
    d.pumpe();
    m = d.letztes(::serial::Rfc2217Ereignis::Art::ModemState);
    ASSERT_NE(m, nullptr);
    EXPECT_FALSE(m->wert & r2::MS_CTS);
    EXPECT_TRUE(m->wert & r2::MS_DSR);
    EXPECT_TRUE(m->wert & r2::MS_CD);
    EXPECT_TRUE(m->wert & (r2::MS_DELTA_CTS | r2::MS_DELTA_DSR));

    // Baud: die Anfrage ändert den Gast nie, die Antwort trägt den Gastwert.
    cli.sendeBaud(9600);
    cli.sendeSignatur("");
    d.pumpe();
    const auto* b = d.letztes(::serial::Rfc2217Ereignis::Art::Baud);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(b->antwort);
    EXPECT_EQ(b->wert, 1200u);
    EXPECT_EQ(srv.baudGegenseite(), 9600u);
    EXPECT_TRUE(srv.baudAbweichend());
    const auto* s = d.letztes(::serial::Rfc2217Ereignis::Art::Signatur);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->text, signaturText());
    EXPECT_EQ(s->text.rfind("k1520emu ", 0), 0u);

    // PURGE 2 (Sendepuffer des Zugangsservers = unser Empfangspuffer).
    const auto x = bytes("weg");
    a.rtsAus = false;   // Halt, damit nichts zugestellt wird
    w.fernGib(x.data(), x.size());
    cli.sendeLeeren(r2::PURGE_SENDEN);
    d.pumpe();
    EXPECT_EQ(w.sicht().puffer_empfangen, 0u);
}

TEST(SerialWandler, Rfc2217ClientBelegtDieLeitungenGerade) {
    AttrappeAnschluss a;
    a.rtsAus = true;
    a.dtrAus = true;
    Wandler w(a);
    Rfc2217Transport cli(::serial::TelnetRolle::Client);
    ::serial::Rfc2217Codec srv(::serial::TelnetRolle::Server);
    srv.start();
    cli.start(w);
    Draht d{cli, srv, w, a};
    d.pumpe();
    // Anfangsmeldung des Clients: Format und Leitungen.
    bool rtsEin = false, dtrEin = false, baud = false;
    for (const auto& e : d.ereignisse) {
        if (e.art == ::serial::Rfc2217Ereignis::Art::Steuerung && e.wert == r2::RTS_EIN) rtsEin = true;
        if (e.art == ::serial::Rfc2217Ereignis::Art::Steuerung && e.wert == r2::DTR_EIN) dtrEin = true;
        if (e.art == ::serial::Rfc2217Ereignis::Art::Baud && e.wert == 9600) baud = true;
    }
    EXPECT_TRUE(rtsEin);
    EXPECT_TRUE(dtrEin);
    EXPECT_TRUE(baud);

    // NOTIFY-MODEMSTATE → Eingänge ohne Kreuzung.
    srv.sendeModemState(r2::MS_CTS | r2::MS_CD, true);
    d.pumpe();
    EXPECT_TRUE(a.cts);
    EXPECT_FALSE(a.dsr);
    EXPECT_TRUE(a.dcd);

    // Gast nimmt RTS weg → SET-CONTROL RTS_AUS.
    a.rtsAus = false;
    d.ereignisse.clear();
    d.pumpe();
    const auto* s = d.letztes(::serial::Rfc2217Ereignis::Art::Steuerung);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->wert, r2::RTS_AUS);

    // Antwort des Servers ≠ eigener Wert → Hinweis.
    srv.sendeBaud(1200, true);
    d.pumpe();
    EXPECT_EQ(cli.baudGegenseite(), 1200u);
    EXPECT_TRUE(cli.baudAbweichend());
}

// ─── Ergänzt in AP-T1b ─────────────────────────────────────────────────────

/// Springt der Taktzähler zurück (Maschine neu, Zähler genullt), beginnen alle Fristen
/// neu — sonst wartete der Wandler bis zur alten Taktzahl und der Gast stünde still.
TEST(SerialWandler, RuecksprungDesTaktzaehlersStartetDieFristenNeu) {
    AttrappeAnschluss a;
    Wandler w(a);
    w.anbinden();
    uint64_t z = 50 * Z9600;
    a.sende("a");
    laufe(w, a, z, Z9600 / 2);
    EXPECT_EQ(w.fernBelegt(), 1u);
    a.sende("bc");
    z = 0;   // Zähler genullt
    laufe(w, a, z, 3 * Z9600);
    EXPECT_EQ(w.fernBelegt(), 3u) << "nach dem Rücksprung nichts mehr gesendet";
    const uint8_t rein[] = {'x', 'y'};
    ASSERT_EQ(w.fernGib(rein, 2), 2u);
    z = 0;
    laufe(w, a, z, 3 * Z9600);
    EXPECT_EQ(a.gelesenText(), "xy");
}

/// Break im Loop: der Prüfstecker gibt das Break des Gastes an seinen eigenen Empfänger
/// zurück; angebunden kommt es von der Gegenseite, Kabel ab nimmt es weg.
TEST(SerialWandler, BreakKommtUeberDenLoopUndVonDerGegenseite) {
    AttrappeAnschluss a;
    Wandler w(a);
    WandlerEinstellung e;
    e.loop = true;
    w.einstellen(e);
    uint64_t z = 0;
    a.brkAus = true;
    laufe(w, a, z, Z9600);
    EXPECT_TRUE(a.brkEin);
    EXPECT_TRUE(w.sicht().brk);
    a.brkAus = false;
    laufe(w, a, z, Z9600);
    EXPECT_FALSE(a.brkEin);

    w.einstellen(WandlerEinstellung{});
    w.anbinden();
    EXPECT_TRUE(w.angebunden());
    w.fernBreak(true);
    laufe(w, a, z, Z9600);
    EXPECT_TRUE(a.brkEin);
    w.abbinden();
    EXPECT_FALSE(w.angebunden());
    laufe(w, a, z, Z9600);
    EXPECT_FALSE(a.brkEin) << "Kabel ab hält kein Break fest";
}

/// Der Gast wird zurückgesetzt: ein XOFF- und ein RTS-Halt des alten Gastes gelten nicht
/// weiter (AP-S5), der Empfang geht sofort weiter.
TEST(SerialWandler, GastResetLoestXoffUndRtsHalt) {
    AttrappeAnschluss a;
    Wandler w(a);
    WandlerEinstellung e;
    e.xonxoff = true;
    w.einstellen(e);
    w.anbinden();
    uint64_t z = 0;
    a.sende(std::string(1, '\x13'));   // XOFF
    laufe(w, a, z, 2 * Z9600);
    ASSERT_TRUE(w.sicht().xoffHalt);
    const uint8_t rein[] = {'q'};
    w.fernGib(rein, 1);
    laufe(w, a, z, 2 * Z9600);
    EXPECT_TRUE(a.gelesen.empty());
    w.gastZurueckgesetzt();
    a.rtsAus = false;   // der neue Gast hat RTS (noch) nicht gesetzt
    laufe(w, a, z, 2 * Z9600);
    EXPECT_EQ(a.gelesenText(), "q");
    EXPECT_FALSE(w.sicht().xoffHalt);
}
