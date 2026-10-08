/**
 * @file test_p8000_terminal_einheit.cpp
 * @brief Originalterminal als Einheit (AP P20a/b, Entwurf 28 §6/§7): Terminalrechner mit
 *        Firmware P8T 5.0 + Tastatur K7673.09 an einem Rechnerkanal (`TerminalHwKopplung`)
 *        bzw. am `SerialHub` (Telnet), austauschbar mit dem Kern-Terminal (`TerminalGeraet`).
 */
#include <gtest/gtest.h>

#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/peripherals/p8000_terminal/terminal_geraet.h"
#include "core/peripherals/p8000_terminal_hw/terminal_einheit.h"
#include "core/serial/hub.h"
#include "core/serial/net/socket.h"

using namespace k1520::p8000;
using namespace k1520::serial;

namespace {

/// Rechnerkanal (SIO des Gastes): sendet aus `gast`, empfängt in `empfangen`, 9600 Bd 8N2 bei 4 MHz.
struct KarteStub : SerialAnschluss {
    std::deque<uint8_t> gast;
    std::string empfangen;
    int breaks = 0;
    bool brk = false;
    SerialFormat fmt;
    KarteStub() {
        fmt.gueltig = true; fmt.baud_nenn = 9600; fmt.daten = 8; fmt.stopp_halbe = 4;
        fmt.zeichen_takte = 11ull * 4'000'000 / 9600;
    }
    const char* name() const override { return "tty1"; }
    const char* stecker() const override { return "X5"; }
    bool v24() const override { return true; }
    SerialFormat format() const override { return fmt; }
    bool senderHatZeichen() const override { return !gast.empty(); }
    uint8_t senderNimm() override { uint8_t b = gast.front(); gast.pop_front(); return b; }
    bool empfaengerFrei() const override { return true; }
    void empfange(uint8_t b) override { if (b) empfangen += char(b); }
    void breakEmpfang(bool a) override { if (a && !brk) ++breaks; brk = a; }
    void senden(const std::string& s) { for (unsigned char c : s) gast.push_back(c); }
};

constexpr uint64_t MS4 = 4000;   // Maschinentakte je ms bei 4 MHz

void laufe(TerminalGeraet& t, uint64_t ms) { for (uint64_t i = 0; i < ms; ++i) t.takt(MS4); }

std::string zl(const TerminalGeraet& t, int z) {
    std::string s = t.text(z);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace

TEST(P8000TerminalEinheit, TasteAWirdAlsAGesendet)
{
    KarteStub k;
    HwTerminalGeraet t(k);
    laufe(t, 1000);                                // Einschaltmeldung, AA der Tastatur
    EXPECT_EQ(zl(t, 0), "ADM31/9600 baud/Video Attr. on (c)zft/keaw");
    ASSERT_TRUE(t.zeichenTaste('a', false));
    laufe(t, 400);
    EXPECT_EQ(k.empfangen, "a");
    ASSERT_TRUE(t.zeichenTaste('A', false));       // SHIFT + a über die Matrix
    ASSERT_TRUE(t.zeichenTaste('c', true));        // CTRL-C
    laufe(t, 800);
    EXPECT_EQ(k.empfangen, "aA\x03");
}

TEST(P8000TerminalEinheit, HostLoeschtDasBildMitEscKlammer2J)
{
    KarteStub k;
    HwTerminalGeraet t(k);
    laufe(t, 1000);
    ASSERT_TRUE(t.taste(TerminalTaste::MODE));     // VT100
    laufe(t, 400);
    EXPECT_EQ(zl(t, 0), "VT100/9600 baud/Video Attr. on (c)zft/keaw");
    k.senden("Zeile\r\nnoch eine");
    laufe(t, 200);
    EXPECT_EQ(zl(t, 1), "Zeile");
    k.senden("\x1b[2J");
    laufe(t, 300);                                 // Löschen dauert ≈ 65 ms
    for (int z = 0; z < 24; ++z) EXPECT_EQ(zl(t, z), "") << z;
    // auch die 8275-Zellen sind leer
    auto& hw = static_cast<HwTerminalGeraet&>(t).einheit().hw();
    for (int z = 0; z < 24; ++z)
        for (int s = 0; s < 80; ++s)
            if (!hw.bildZelle(z, s).empty) ASSERT_EQ(hw.bildZelle(z, s).code, ' ') << z << "/" << s;
}

TEST(P8000TerminalEinheit, BreakTasteMeldetBreakAmKanal)
{
    KarteStub k;
    HwTerminalGeraet t(k);
    laufe(t, 1000);
    ASSERT_TRUE(t.taste(TerminalTaste::BREAK));
    laufe(t, 400);
    EXPECT_EQ(k.breaks, 1);
    EXPECT_FALSE(k.brk);
    EXPECT_EQ(k.empfangen, "");
}

/// Beide Terminals hinter derselben Schnittstelle: gleiche Folge, gleiches Bild (Zeile 1–3).
TEST(P8000TerminalEinheit, KernUndOriginalSindAustauschbar)
{
    KarteStub ka, kb;
    std::vector<std::unique_ptr<TerminalGeraet>> t;
    t.push_back(std::make_unique<KernTerminalGeraet>(ka));
    t.push_back(std::make_unique<HwTerminalGeraet>(kb));
    KarteStub* karte[2] = {&ka, &kb};
    for (int i = 0; i < 2; ++i) {
        laufe(*t[size_t(i)], 1000);
        karte[i]->senden("\x1b*\x1e" "eins\r\nzwei\r\n\x1b=\"\x25" "drei");
        laufe(*t[size_t(i)], 400);
        EXPECT_TRUE(t[size_t(i)]->zeichenTaste('x', false));
        laufe(*t[size_t(i)], 400);
    }
    for (int z = 0; z < 24; ++z) EXPECT_EQ(zl(*t[0], z), zl(*t[1], z)) << z;
    EXPECT_EQ(t[0]->zeile(), t[1]->zeile());
    EXPECT_EQ(t[0]->spalte(), t[1]->spalte());
    EXPECT_EQ(zl(*t[1], 2), "     drei");
    EXPECT_EQ(ka.empfangen, "x");
    EXPECT_EQ(kb.empfangen, "x");
}

TEST(P8000TerminalEinheit, SaveStateDerKopplungSetztFort)
{
    KarteStub k1, k2;
    HwTerminalGeraet a(k1), b(k2);
    laufe(a, 1000);
    k1.senden("abc");
    laufe(a, 1);
    std::vector<uint8_t> st;
    a.serialize(st);
    const uint8_t* p = st.data();
    ASSERT_TRUE(b.deserialize(p, st.data() + st.size()));
    k2.gast = k1.gast;
    laufe(a, 100);
    laufe(b, 100);
    EXPECT_EQ(zl(a, 1), "abc");
    EXPECT_EQ(zl(b, 1), "abc");
}

/// Eigenständiger Betrieb („P8000 Terminal"): der Hub der Einheit als Telnet-Server, ein Client
/// spielt den Rechner.
TEST(P8000TerminalEinheit, TelnetAmHubDerEinheit)
{
    P8000TerminalEinheit e;
    e.laufeMs(1000);
    SerialHub& hub = e.hub();
    ASSERT_EQ(hub.anzahl(), 1);
    EXPECT_EQ(hub.info(0).name, "Terminal (XB5)");
    SerialKonfig k = hub.konfig(0);
    k.betriebsart = Betriebsart::Telnet;
    k.rolle = Rolle::Server;
    k.port = 0;
    ASSERT_TRUE(hub.konfigurieren(0, k));
    ASSERT_TRUE(hub.start(0));
    const uint16_t port = hub.status(0).port_aktiv;
    ASSERT_NE(port, 0);
    net::Fehler fe;
    auto ziele = net::aufloesen("127.0.0.1", port, &fe);
    ASSERT_FALSE(ziele.empty());
    net::Socket s = net::verbindenAlle(ziele, 2000, &fe);
    ASSERT_TRUE(s.gueltig());
    const std::string hallo = "Hallo P8000";
    bool da = false;
    for (int i = 0; i < 300 && !da; ++i) {
        if (i == 20) ASSERT_GT(net::senden(s.fd(), hallo.data(), hallo.size()).n, 0u);
        e.laufeMs(10);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        da = e.hw().text(1).rfind(hallo, 0) == 0;
    }
    EXPECT_TRUE(da) << e.hw().text(1);
    ASSERT_TRUE(e.zeichenTaste('q'));
    std::string zurueck;
    for (int i = 0; i < 300 && zurueck.find('q') == std::string::npos; ++i) {
        e.laufeMs(10);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        char buf[64];
        const auto r = net::empfangen(s.fd(), buf, sizeof buf);
        if (r.status == net::IoStatus::Ok && r.n > 0) zurueck.append(buf, r.n);
    }
    EXPECT_NE(zurueck.find('q'), std::string::npos);
    hub.stop(0);
}
