/**
 * @file test_prg710_seriell.cpp
 * @brief PRG 710 / 710-1 Etappe 4 (doc/design/20_prg710.md AP-P4): die K8025 führt ihre
 *        Kanäle über den `SerialHub` nach außen, benannt nach der Gerätebeschriftung
 *        (`DRUCK.DOK`, §3.7/§3.9).
 *
 * Belegung (Index = C-ABI):
 * - PRG 710:   0 „V.24“ X4 (A33-A, 50H/51H) · 1 „IFSS Hauptdrucker“ X6 (A32-B, 5EH/5FH) ·
 *              2 „ZIFSS Zusatzdrucker“ X5 (A32-A, 5CH/5DH)
 * - PRG 710-1: 0 „V.24“ · 1 „ZIFSS Zusatzdrucker“ (A32-B trägt die Tastatur K7672: fest)
 * - dahinter in beiden Varianten „Fernschreiber“ (ASS 590069, AP-P8c)
 *
 * Wie UDOS den Drucker anschaltet: `SET PRI TO V24` lädt `DRUCK.V24` (der Treiber heisst
 * `DRUCK.<Typ>`, `NOTE.TO.UDOS.4.3`), `SET PRI ON` spiegelt die Bildschirmausgabe dorthin.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/logger.h"
#include "core/machines/prg710/prg710.h"
#include "core/serial/hub.h"
#include "tests/support/fixtures.h"
#include "tests/support/temp_path.h"

namespace fs = std::filesystem;
using V = Prg710Machine::Config::Variante;
using namespace k1520::serial;

namespace {

constexpr int       kSchritt = 20'000;
constexpr long long kBoot    = 120'000'000;
constexpr long long kBefehl  =  60'000'000;
constexpr uint32_t  QK_RETURN = 0x01000004;

const char* diskette(V v) {
    return v == V::Prg710_1 ? "prg710-1_udos43_k5601_v43_189.hfe"
                            : "prg710_udos43_k5601_mrs_boot.hfe";
}

std::string zeile(Prg710Machine& m, int r) {
    std::string s;
    for (int c = 0; c < 80; ++c) {
        const uint8_t ch = static_cast<uint8_t>(m.screenChar(c, r) & 0x7F);
        s += (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
std::string bild(Prg710Machine& m) {
    std::string s;
    for (int r = 0; r < 24; ++r) s += zeile(m, r) + "\n";
    return s;
}
std::string letzteZeile(Prg710Machine& m) {
    for (int r = 23; r >= 0; --r) {
        std::string z = zeile(m, r);
        if (!z.empty()) return z;
    }
    return {};
}
void lauf(Prg710Machine& m, long long n) {
    for (long long d = 0; d < n;) d += m.run(kSchritt);
}
bool bisText(Prg710Machine& m, const std::string& t, long long frist) {
    for (long long d = 0; d < frist;) {
        d += m.run(kSchritt);
        if (bild(m).find(t) != std::string::npos) return true;
    }
    return false;
}
bool bisPrompt(Prg710Machine& m, long long frist) {
    long long ruhig = 0;
    for (long long d = 0; d < frist;) {
        const int n = m.run(kSchritt);
        d += n;
        ruhig = (letzteZeile(m) == "%") ? ruhig + n : 0;
        if (ruhig >= 2'000'000) return true;
    }
    return false;
}
void taste(Prg710Machine& m, uint32_t k) {
    m.keyPress(k, false, false);
    lauf(m, 100'000);
    m.keyRelease(k);
    lauf(m, 100'000);
}
bool kommando(Prg710Machine& m, const std::string& k) {
    for (char c : k) taste(m, static_cast<uint8_t>(c));
    taste(m, QK_RETURN);
    return bisPrompt(m, kBefehl);
}
std::string dateiInhalt(const std::string& pfad) {
    std::ifstream f(fs::u8path(pfad), std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

Prg710Machine::Config cfgFuer(V v) {
    Prg710Machine::Config c;
    c.variante = v;
    return c;
}

}  // namespace

class Prg710Seriell : public ::testing::TestWithParam<V> {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
};

/// Namen, Stecker, Anzahl je Variante; nur V.24 trägt Steuerleitungen; Takt fest (CTC A34).
TEST_P(Prg710Seriell, HubNamenUndAnzahl) {
    Prg710Machine m(cfgFuer(GetParam()));
    auto* hub = m.serialHub();
    ASSERT_NE(hub, nullptr);
    std::vector<std::string> namen, stecker;
    std::vector<bool> v24;
    for (auto* a : m.serielleAnschluesse()) {
        namen.push_back(a->name());
        stecker.push_back(a->stecker());
        v24.push_back(a->v24());
    }
    EXPECT_EQ(hub->anzahl(), static_cast<int>(namen.size()));
    if (GetParam() == V::Prg710) {
        EXPECT_EQ(namen, (std::vector<std::string>{"V.24", "IFSS Hauptdrucker", "ZIFSS Zusatzdrucker",
                                                   "Fernschreiber"}));
        EXPECT_EQ(stecker, (std::vector<std::string>{"X4", "X6", "X5", "590069"}));
        EXPECT_EQ(v24, (std::vector<bool>{true, false, false, false}));
        EXPECT_TRUE(m.festeSchnittstellen().empty());
    } else {
        EXPECT_EQ(namen, (std::vector<std::string>{"V.24", "ZIFSS Zusatzdrucker", "Fernschreiber"}));
        EXPECT_EQ(stecker, (std::vector<std::string>{"X4", "X5", "590069"}));
        EXPECT_EQ(v24, (std::vector<bool>{true, false, false}));
        EXPECT_EQ(m.festeSchnittstellen().size(), 1u);
    }
}

/// Ein Byte von aussen kommt im Empfänger der richtigen SIO an (und nur dort).
TEST_P(Prg710Seriell, EmpfangLandetImRichtigenKanal) {
    Prg710Machine m(cfgFuer(GetParam()));
    auto& ass = m.ass();
    struct Kanal { K8025::Schnittstelle s; uint8_t daten, steuer; };
    std::vector<Kanal> kanaele = {{K8025::DfueV24, 0x50, 0x51}, {K8025::ZifssA32A, 0x5C, 0x5D}};
    if (GetParam() == V::Prg710) kanaele.push_back({K8025::Drucker, 0x5E, 0x5F});
    for (auto& k : kanaele) {   // WR3: Empfänger frei, 8 Bit
        ass.ioWrite(k.steuer, 0x03);
        ass.ioWrite(k.steuer, 0xC1);
    }
    for (auto& k : kanaele) {
        for (auto& a : kanaele) EXPECT_EQ(ass.ioRead(a.steuer) & 1, 0) << "vor dem Byte";
        ass.anschluss(k.s).empfange(0x5A);
        for (auto& a : kanaele)
            EXPECT_EQ(ass.ioRead(a.steuer) & 1, a.s == k.s ? 1 : 0) << std::hex << int(k.daten);
        EXPECT_EQ(ass.ioRead(k.daten), 0x5A);
    }
}

/// UDOS: `SET PRI TO V24` + `SET PRI ON`; `DATE` erscheint am V.24-Anschluss, den der
/// Wandler „Datei“ fängt (Gast programmiert Baud/Format selbst).
TEST_P(Prg710Seriell, UdosDruckV24LandetInDerDatei) {
    k1520test::TempDisk disk(diskette(GetParam()));
    const std::string pfad = k1520test::tempPath("prg710_v24_ausgabe.txt");
    fs::remove(fs::u8path(pfad));

    Prg710Machine m(cfgFuer(GetParam()));
    ASSERT_TRUE(m.mountDisk(0, disk, m.defaultFormatName(0), false)) << m.lastError();
    SerialKonfig k = m.serialHub()->konfig(0);   // Vorgabe der Maschine (Taktquelle 1) behalten
    k.betriebsart = Betriebsart::Datei;
    k.datei       = pfad;
    k.loop        = false;
    ASSERT_TRUE(m.serialHub()->konfigurieren(0, k));
    ASSERT_TRUE(m.serialHub()->start(0));

    m.powerOn();
    lauf(m, 3'000'000);
    taste(m, QK_RETURN);
    ASSERT_TRUE(bisText(m, "Neues Datum", kBoot)) << bild(m);
    for (char c : std::string("021086")) taste(m, static_cast<uint8_t>(c));
    ASSERT_TRUE(bisPrompt(m, kBefehl)) << bild(m);

    ASSERT_TRUE(kommando(m, "SET PRI TO V24")) << bild(m);
    ASSERT_TRUE(kommando(m, "SET PRI ON")) << bild(m);
    ASSERT_TRUE(kommando(m, "DATE")) << bild(m);
    lauf(m, 20'000'000);   // Zeichenzeit des Wandlers abwarten (9600 Bd)

    // Der Wandler schreibt höchstens alle 0,5 s; Beenden schreibt den Rest.
    m.serialHub()->stop(0);
    std::string aus = dateiInhalt(pfad);
    aus.erase(std::remove(aus.begin(), aus.end(), '\0'), aus.end());   // Treiber füllt mit NUL auf
    EXPECT_NE(aus.find("%DATE"), std::string::npos) << aus;
    EXPECT_NE(aus.find("Donnerstag, der 2. Oktober 1986"), std::string::npos)
        << "Datei: [" << aus << "]\n" << bild(m);

    // Parameter wie der Gast sie programmiert hat (DRUCK.DOK §3: 8 Bit, 2 Stopp; Takt CTC A34 K2 = Taktquelle 1, die der Treiber nutzt).
    EXPECT_EQ(m.serialHub()->konfig(0).taktquelle, 1);
    const auto f = m.serielleAnschluesse()[0]->format();
    EXPECT_TRUE(f.gueltig);
    EXPECT_EQ(f.daten, 8);
    // Befund: DRUCK.V24 programmiert WR4 ohne Parität (DRUCK.DOK nennt „ungerade“ als Druckereinstellung).
    EXPECT_EQ(f.paritaet, 0);
    EXPECT_EQ(f.stopp_halbe, 4);
    EXPECT_EQ(f.baud_nenn, 9600u) << f.baud_nenn;
    fs::remove(fs::u8path(pfad));
}

INSTANTIATE_TEST_SUITE_P(Varianten, Prg710Seriell, ::testing::Values(V::Prg710, V::Prg710_1),
                         [](const auto& i) { return i.param == V::Prg710 ? "Prg710" : "Prg710_1"; });
