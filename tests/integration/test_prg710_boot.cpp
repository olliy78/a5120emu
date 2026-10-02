/**
 * @file test_prg710_boot.cpp
 * @brief PRG 710 / 710-1 Etappe 1 (doc/design/20_prg710.md AP-P1d): beide echten
 *        Boot-ROMs laufen bis zur Tastaturabfrage; was das ROM ohne Diskette und
 *        ohne Laufwerke tut; Registerfolge der Speicherverwaltung; Marken-FF-Ruhepegel.
 *
 * **Am Lauf festgestellt (§4a, AP-P1d):**
 * - „LW0 DEF“/„LW1 DEF“ erscheinen **nicht** vor der Starttaste.  Beide ROMs geben bis
 *   zur Taste nur „NKM-LOADER“ aus; der PRG 710 liest vorher beide Laufwerke *ohne
 *   Auswertung* (0097H), der 710-1 gar nicht.  Ausgewertet wird erst nach der Taste
 *   (710: 00A0H, 710-1: 008EH).
 * - **Status C0H heisst „Spur 0 nicht gefunden“** (256 Schritte ohne /TO = 0, 020AH),
 *   also Laufwerk fehlt oder defekt → „LWn DEF“.  Ein vorhandenes Laufwerk **ohne
 *   Diskette** findet Spur 0 (der Endlagenschalter kennt keine Diskette) und läuft in
 *   das Warten auf /RDY (0238H, 65536 Runden) → **Status C2H** → „DISKERROR C2“.
 * - Danach „NO SYSTEM“ und Sprung nach 0020H: das ROM programmiert die
 *   Speicherverwaltung neu und wartet wieder auf die Taste.
 *
 * Die Starttaste kommt am 710 seit AP-P2a über `keyPress` und K7609 → 8279 der ATP
 * (C8H/C9H); am 710-1 noch als Byte in den Empfänger von SIO A32-B (AP-P2b: K7672).
 */

#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <vector>

#include "core/logger.h"
#include "core/machines/prg710/prg710.h"
#include "tests/support/fixtures.h"

namespace {
using V = Prg710Machine::Config::Variante;

constexpr int       kSchritt = 20'000;
constexpr long long kFrist   = 40'000'000;   // ≈ 16 s Maschinenzeit

Prg710Machine::Config cfgFuer(V v, const char* lw = "K5601") {
    Prg710Machine::Config c;
    c.variante  = v;
    c.laufwerke = {lw, lw, "none", "none"};
    return c;
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

/// PC in der Tastaturabfrage der RAM-Kopie (Seite 0 = OPS-RAM)?
/// 710: Warteschleife 007DH–0084H + 8279-Abfrage 0160H–0167H;
/// 710-1: 0070H–0077H + SIO-Abfrage 0163H–016AH.
bool inTastaturabfrage(Prg710Machine& m) {
    if (m.speicher().ortVon(0).quelle != Prg710Speicher::Quelle::Ops) return false;
    const uint16_t pc = m.cpuPC();
    if (m.variante() == V::Prg710_1)
        return (pc >= 0x0070 && pc <= 0x0077) || (pc >= 0x0163 && pc <= 0x016A);
    return (pc >= 0x007D && pc <= 0x0084) || (pc >= 0x0160 && pc <= 0x0167);
}

/// Läuft, bis die CPU 1 Mio. Takte am Stück in der Tastaturabfrage stand; Rückgabe = Takte.
long long bisTastaturabfrage(Prg710Machine& m) {
    long long done = 0, drin = 0;
    while (done < kFrist) {
        const int n = m.run(kSchritt);
        done += n;
        drin = inTastaturabfrage(m) ? drin + n : 0;
        if (drin >= 1'000'000) return done;
    }
    return -1;
}

/// Statusbyte des ROM-Parameterblocks (IY+10): 710 IY = 03D4H, 710-1 IY = 03D7H.
uint8_t status(Prg710Machine& m) {
    return m.memReadDebug(m.variante() == V::Prg710_1 ? 0x03E1 : 0x03DE);
}

constexpr uint32_t QK_RETURN = 0x01000004;   // Qt::Key_Return

/// Starttaste über die Tastaturmodelle: 710 ET1 = 37H (K7609 → 8279), 710-1 ENTER = 0DH
/// (K7672 → SIO A32-B) — beide über denselben `keyPress`.  Das Loslassen gehört dazu: die
/// K7672 wiederholt eine gehaltene Taste (Firmware), der 8279 nicht.
void starttaste(Prg710Machine& m) {
    if (m.variante() == V::Prg710) {
        m.keyPress(QK_RETURN, false, false);
        m.keyRelease(QK_RETURN);
    } else {
        m.printerSend(0x0D);   // ENTER als Byte in A32-B, bis AP-P2b die K7672 anschließt
    }
}

}  // namespace

class Prg710Boot : public ::testing::TestWithParam<V> {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
};

/**
 * @test Prg710Boot.OhneDisketteNurNkmLoaderUndTastaturabfrage
 * @brief Zwei K5601 ohne Diskette: Zeile 0 „NKM-LOADER“, sonst nichts (keine
 *        Laufwerksmeldung vor der Taste); das ROM wartet in seiner Tastaturabfrage
 *        (710: 8279 C9H, 710-1: SIO A32-B 5FH).  Der 710 hat beide Laufwerke vorher
 *        angesprochen (Status des letzten Versuchs: C2H = nicht bereit), der 710-1 nicht.
 */
TEST_P(Prg710Boot, OhneDisketteNurNkmLoaderUndTastaturabfrage) {
    Prg710Machine m(cfgFuer(GetParam()));
    m.powerOn();
    ASSERT_GT(bisTastaturabfrage(m), 0) << "PC=" << std::hex << m.cpuPC() << "\n" << bild(m);
    EXPECT_EQ(zeile(m, 0), "NKM-LOADER");
    for (int r = 1; r < 24; ++r) EXPECT_EQ(zeile(m, r), "") << "Zeile " << r << "\n" << bild(m);
    if (GetParam() == V::Prg710) {
        EXPECT_EQ(status(m), 0xC2) << "Laufwerk 1 gelesen, ohne Diskette nicht bereit";
        EXPECT_EQ(m.afs().drive(0).currentCylinder(), 0) << "Spur 0 angefahren";
    } else {
        EXPECT_EQ(status(m), 0x00) << "710-1 liest vor ENTER nicht";
    }
    // Abfrage liest den richtigen Baustein: am 710 C9H, am 710-1 RR0 von A32-B.
    uint64_t c9 = 0, p5f = 0;
    m.setBusTrace([&](bool io, bool rd, uint16_t a, uint8_t) {
        if (!io || !rd) return;
        if ((a & 0xFF) == 0xC9) ++c9;
        if ((a & 0xFF) == 0x5F) ++p5f;
    });
    m.run(100'000);
    if (GetParam() == V::Prg710) { EXPECT_GT(c9, 100u); EXPECT_EQ(p5f, 0u); }
    else                         { EXPECT_GT(p5f, 100u); EXPECT_EQ(c9, 0u); }
}

/**
 * @test Prg710Boot.StarttasteOhneDisketteDiskerrorC2
 * @brief Starttaste (710: ET = 37H, 710-1: ENTER = 0DH) mit zwei leeren K5601: je
 *        Laufwerk „DISKERROR C2“ (Spur 0 gefunden, /RDY bleibt aus), dann „NO SYSTEM“,
 *        und das ROM wartet wieder auf die Taste.
 */
TEST_P(Prg710Boot, StarttasteOhneDisketteDiskerrorC2) {
    Prg710Machine m(cfgFuer(GetParam()));
    m.powerOn();
    ASSERT_GT(bisTastaturabfrage(m), 0);
    if (GetParam() == V::Prg710) {
        EXPECT_EQ(m.atp().kbc().lastMode(), 0x02) << "8279: kodierte Abtastung, N-Tasten-Rollover";
        EXPECT_EQ(m.atp().kbc().lastCommand(), 0xC1) << "8279: Clear all (letzter Befehl)";
    }
    starttaste(m);
    ASSERT_GT(bisTastaturabfrage(m), 0) << bild(m);
    EXPECT_EQ(zeile(m, 0), "NKM-LOADER");
    EXPECT_EQ(zeile(m, 1), "DISKERROR C2") << bild(m);
    EXPECT_EQ(zeile(m, 2), "DISKERROR C2") << bild(m);
    EXPECT_EQ(zeile(m, 3), "NO SYSTEM") << bild(m);
    EXPECT_EQ(m.speicher().attr(0), 0x10) << "nach dem Neustart wieder aus der RAM-Kopie";
}

/**
 * @test Prg710Boot.OhneLaufwerkeLwDef
 * @brief Gegenprobe Laufwerke `none` (Slot unbestückt): /TO bleibt 1, nach 256
 *        Schritten Status C0H.  Vor der Taste dasselbe Bild wie mit Laufwerken (der 710
 *        verwirft das Ergebnis); nach der Taste „LW0 DEF“, „LW1 DEF“, „NO SYSTEM“.
 */
TEST_P(Prg710Boot, OhneLaufwerkeLwDef) {
    Prg710Machine m(cfgFuer(GetParam(), "none"));
    m.powerOn();
    ASSERT_GT(bisTastaturabfrage(m), 0);
    EXPECT_EQ(zeile(m, 0), "NKM-LOADER");
    EXPECT_EQ(zeile(m, 1), "") << bild(m);
    if (GetParam() == V::Prg710) EXPECT_EQ(status(m), 0xC0) << "Spur 0 nicht gefunden";
    starttaste(m);
    ASSERT_GT(bisTastaturabfrage(m), 0) << bild(m);
    EXPECT_EQ(zeile(m, 1), "LW0 DEF") << bild(m);
    EXPECT_EQ(zeile(m, 2), "LW1 DEF") << bild(m);
    EXPECT_EQ(zeile(m, 3), "NO SYSTEM") << bild(m);
    // Der Neustart ab 0020H kopiert das ROM erneut nach 0000H — samt Parameterblock.
    // Der 710 liest danach beide Laufwerke wieder (C0H), der 710-1 nicht (00H aus dem ROM).
    EXPECT_EQ(status(m), GetParam() == V::Prg710 ? 0xC0 : 0x00);
}

/**
 * @test Prg710Boot.SpeicherverwaltungFolgtDemArbeitsmodell
 * @brief Arbeitsmodell §4b am echten ROM: Registerfolge (Seite = A12–A15 der
 *        E/A-Adresse) und Sichtwechsel.  Bildschirmausgaben schalten E8H[F] auf FFH
 *        und zurück auf F0H; davor/dazwischen: EBH ← 00H, 16 × (EAH[n] ← n,
 *        E8H[n] ← 10H), E8H[0] ← 0FH, EBH ← 0FH, Kopie, E8H[0] ← 10H aus 105DH.
 */
TEST_P(Prg710Boot, SpeicherverwaltungFolgtDemArbeitsmodell) {
    Prg710Machine m(cfgFuer(GetParam()));
    m.powerOn();
    struct W { uint8_t port, seite, wert; uint16_t pc; Prg710Speicher::Quelle s0; };
    std::vector<W> folge;
    m.setBusTrace([&](bool io, bool rd, uint16_t a, uint8_t d) {
        const uint8_t p = static_cast<uint8_t>(a);
        if (!io || rd || p < 0xE8 || p > 0xEB) return;
        // Sicht NACH dem Schreiben (der Beobachter läuft hinter dem Gerät).
        folge.push_back({p, static_cast<uint8_t>(m.bus().ioAddress() >> 12), d,
                         m.cpuPC(), m.speicher().ortVon(0x0100).quelle});
    });
    ASSERT_GT(bisTastaturabfrage(m), 0);
    m.setBusTrace(nullptr);

    // Bildschirmumschaltung herausfiltern (E8H[F] = FFH/F0H), Rest = Programmierung.
    std::vector<W> prog;
    int vram_ein = 0, vram_aus = 0;
    for (const W& w : folge) {
        if (w.port == 0xE8 && w.seite == 0xF && (w.wert == 0xFF || w.wert == 0xF0)) {
            (w.wert == 0xFF ? vram_ein : vram_aus)++;
            continue;
        }
        prog.push_back(w);
    }
    EXPECT_GT(vram_ein, 10);
    EXPECT_EQ(vram_ein, vram_aus) << "jeder Bildschirmzugriff blendet das VRAM wieder aus";
    ASSERT_EQ(prog.size(), 1u + 32u + 2u + 1u);
    using Q = Prg710Speicher::Quelle;
    EXPECT_EQ(prog[0].port, 0xEB); EXPECT_EQ(prog[0].wert, 0x00);
    for (int n = 0; n < 16; ++n) {
        const W& ea = prog[1 + 2 * n];
        const W& e8 = prog[2 + 2 * n];
        EXPECT_EQ(ea.port, 0xEA); EXPECT_EQ(ea.seite, n); EXPECT_EQ(ea.wert, n);
        EXPECT_EQ(e8.port, 0xE8); EXPECT_EQ(e8.seite, n); EXPECT_EQ(e8.wert, 0x10);
        EXPECT_EQ(e8.s0, Q::Zre) << "EBH = 0: Register wirken noch nicht (Seite " << n << ")";
    }
    EXPECT_EQ(prog[33].port, 0xE8); EXPECT_EQ(prog[33].seite, 0); EXPECT_EQ(prog[33].wert, 0x0F);
    EXPECT_EQ(prog[34].port, 0xEB); EXPECT_EQ(prog[34].wert, 0x0F);
    EXPECT_EQ(prog[34].s0, Q::Zre) << "Freigabe mit E8H[0] = 0FH: ZRE bleibt sichtbar";
    EXPECT_EQ(prog[35].port, 0xE8); EXPECT_EQ(prog[35].seite, 0); EXPECT_EQ(prog[35].wert, 0x10);
    EXPECT_EQ(prog[35].pc & 0xFF00, 0x1000) << "aus der Kopie bei 1000H umgeschaltet";
    EXPECT_EQ(prog[35].s0, Q::Ops) << "E8H[0] = 10H: Seite 0 = OPS-RAM";
    // Endzustand: alles OPS identisch, VRAM ausgeblendet, ROM-Kopie bei 0000H.
    EXPECT_EQ(m.speicher().freigabe(), 0x0F);
    for (int n = 0; n < 16; ++n) EXPECT_EQ(m.speicher().seite(n), n);
    EXPECT_EQ(m.speicher().attr(0xF), 0x10) << "aus der Schleife; ab hier keine Bildausgabe mehr";
    EXPECT_EQ(m.memReadDebug(0x0000), m.zre().memRead(0x0000)) << "ROM-Kopie in Seite 0";
    EXPECT_EQ(m.speicher().ortVon(0xF800).quelle, Q::Ops);
}

/**
 * @test Prg710Boot.MarkenFFRuhepegelAmTorB
 * @brief K5122 Tor B Bit 1 (12H) in Ruhe, vom ROM programmiert: am 710 1 (low-aktiv),
 *        am 710-1 0 (high-aktiv) — passend zu `JR NZ` bzw. `JR Z` der Lese-Routine
 *        (§4a.1).  Der „Bit 1 = 1 vor der PIO-Programmierung“ aus AP-P1c war die
 *        fehlende Anmeldung der K5122 am Bus (FFH von einem leeren Port).
 */
TEST_P(Prg710Boot, MarkenFFRuhepegelAmTorB) {
    Prg710Machine m(cfgFuer(GetParam()));
    m.powerOn();
    const uint8_t erwartet = GetParam() == V::Prg710 ? 0x02 : 0x00;
    EXPECT_EQ(m.ioReadDebug(0x12) & 0x02, erwartet) << "nach Netz-Ein (PIO im Grundzustand)";
    EXPECT_NE(m.ioReadDebug(0x12), 0xFF) << "K5122 antwortet an 12H";
    ASSERT_GT(bisTastaturabfrage(m), 0);
    const uint8_t b = m.ioReadDebug(0x12);
    EXPECT_EQ(b & 0x02, erwartet);
    if (GetParam() == V::Prg710) EXPECT_EQ(b & 0x80, 0) << "/TO: Laufwerk 1 steht auf Spur 0";
    EXPECT_EQ(b & 0x01, 0x01) << "/RDYL: ohne Diskette nicht bereit";
}

/**
 * @test Prg710Boot.StarttasteLaedtDenBootsektor
 * @brief Mit der UDOS-Bootdiskette des jeweiligen Geräts in Laufwerk 0: Starttaste →
 *        das ROM liest Spur 0, Sektor 1–4 (512 Byte) nach 0400H, die CRC des ROMs stimmt
 *        (Status 80H), dort steht `18 03 'SYL'`, und es springt nach 0400H.
 *        Wächter für die Markensuche im `/WAIT`-Betrieb beider Varianten: das 710-ROM
 *        schlägt das Marken-FF in einer Schleife neu an und fragt nach 12 Takten ab
 *        (02DDH, Marken-FF low-aktiv) — ohne `K5122::setMkeJedesSyncByte` hängt es
 *        dort schon vor der Taste; das 710-1-ROM schlägt einmal an und wartet (high-aktiv).
 *        Was nach 0400H kommt (Zweitlader, UDOS), ist AP-P3.
 */
TEST_P(Prg710Boot, StarttasteLaedtDenBootsektor) {
    const bool v1 = GetParam() == V::Prg710_1;
    k1520test::TempDisk disk(v1 ? "prg710-1_udos_k5601_boot.hfe" : "prg710_udos43_k5601_boot01.hfe");
    Prg710Machine m(cfgFuer(GetParam()));
    ASSERT_TRUE(m.mountDisk(0, disk, m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_GT(bisTastaturabfrage(m), 0) << "PC=" << std::hex << m.cpuPC() << "\n" << bild(m);
    if (!v1) EXPECT_EQ(status(m), 0xC2) << "vor der Taste: Laufwerk 0 gelesen, zuletzt das leere 1";
    bool bei0400 = false;
    uint8_t st = 0;
    m.setCpuTraceCallback([&](const Z80& z) {
        if (z.PC == 0x0400 && !bei0400
            && m.speicher().ortVon(0x0400).quelle == Prg710Speicher::Quelle::Ops) {
            bei0400 = true; st = status(m); m.stop();
        }
    });
    starttaste(m);
    for (long long done = 0; !bei0400 && done < kFrist;) done += m.run(kSchritt);
    m.setCpuTraceCallback(nullptr);
    ASSERT_TRUE(bei0400) << "PC=" << std::hex << m.cpuPC() << "\n" << bild(m);
    EXPECT_EQ(st, 0x80) << "Lesen gut (CRC des ROMs, 0336H)";
    EXPECT_EQ(m.memReadDebug(0x0400), 0x18);
    EXPECT_EQ(m.memReadDebug(0x0402), 'S');
    EXPECT_EQ(m.memReadDebug(0x0403), 'Y');
    EXPECT_EQ(m.memReadDebug(0x0404), 'L');
    EXPECT_EQ(zeile(m, 1), "") << "keine Fehlermeldung vor dem Sprung\n" << bild(m);
}

/**
 * @test Prg710Boot.TastaturAmRichtigenBaustein
 * @brief 710: ENTER kommt als 37H (ET1) im FIFO des 8279 an, das ROM liest ihn über C8H.
 */
TEST(Prg710Boot, TastaturAmRichtigenBaustein) {
    Prg710Machine m(cfgFuer(V::Prg710));
    m.powerOn();
    ASSERT_GT(bisTastaturabfrage(m), 0);
    starttaste(m);
    uint8_t gelesen = 0xFF;
    m.setBusTrace([&](bool io, bool rd, uint16_t a, uint8_t d) {
        if (io && rd && (a & 0xFF) == 0xC8) gelesen = d;
    });
    for (long long done = 0; gelesen == 0xFF && done < kFrist;) done += m.run(kSchritt);
    m.setBusTrace(nullptr);
    EXPECT_EQ(gelesen, 0x37);
    EXPECT_EQ(m.ioReadDebug(0xD0), 0xFF) << "EPROMmer-Attrappe liest FFH";
}

TEST(Prg710Boot, OhneTastaturKeinStart) {
    Prg710Machine::Config c = cfgFuer(V::Prg710);
    c.tastatur = false;
    Prg710Machine m(c);
    m.powerOn();
    ASSERT_GT(bisTastaturabfrage(m), 0);
    starttaste(m);
    for (long long done = 0; done < 3'000'000;) done += m.run(kSchritt);
    EXPECT_TRUE(inTastaturabfrage(m)) << bild(m);
    EXPECT_EQ(zeile(m, 1), "");
}

INSTANTIATE_TEST_SUITE_P(Varianten, Prg710Boot, ::testing::Values(V::Prg710, V::Prg710_1),
                         [](const auto& i) { return i.param == V::Prg710 ? "Prg710" : "Prg710_1"; });
