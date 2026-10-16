/**
 * @file test_boot_bcu.cpp
 * @brief BCU 880 auf dem fruehen A5120 (doc/design/30_a5120_fruehe_varianten.md §3.2, AP A7).
 *
 * Die BCU-Disketten „SYL auf Spur 1“ (doc/a5120_alt/bcu/satz{1,2}_SYL_auf_Spur1) tragen auf
 * Zylinder 1 einen 768-B-Lader mit `SYL`-Kennung und Einsprung 042EH — den nimmt nur das
 * Lade-ROM **V0.6** an (doc/a5120_alt/laderom.md §3).  Der Lader holt BCU 880 V1.2 (67 Sektoren
 * ab Zylinder 2 nach D680H, eigene ZVE2-Leseroutine, CRC je Sektor per Software) und springt
 * nach D680H.  Die beiden Saetze unterscheiden sich im Bildtreiber (doc/a5120_alt/bcu.md):
 *
 * - Satz 2 („SIOS 1SS Copy1“): 64×16 ab FC00H = K7023 → Ausfuehrung `Alt`;
 * - Satz 1 („BCU Kaltstart“): 80×24 ab F800H = K7024 → `Neu` mit K7636 am U-Bus.
 *
 * Tastatur in beiden: K7636 ueber die Tore 01H/06H der K2526.  BCU 880 V2.1 liegt nur auf
 * Disketten „ohne SYL“ — die nimmt keines der vier vorhandenen Lade-ROMs an (letzter Fall).
 */
#include <gtest/gtest.h>

#include <string>

#include "core/machines/a5120/a5120.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"

namespace {

using A = A5120Machine::Config::Ausfuehrung;
using L = K2526::Laderom;
using k1520test::kSmallBatch;
using k1520test::QK_RETURN;
using k1520test::TempDisk;

/// Format der BCU-Abbilder: 40 Spuren einseitig, 26×128 MFM (TD0-Kopf: 250 kbit/s, MFM).
constexpr const char* kFormat = "k5601_ss40_26x128";

/// Bildschirm als Text — K7023 (16×64) oder K7024 (24×80); Bit 7 (Kursor) ausgeblendet.
std::string bild(A5120Machine& m) {
    const int sp = m.k7023() ? 64 : 80, ze = m.k7023() ? 16 : 24;
    std::string s;
    for (int r = 0; r < ze; ++r) {
        for (int c = 0; c < sp; ++c) {
            const uint8_t ch = m.screenChar(c, r) & 0x7F;
            s.push_back((ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ');
        }
        s.push_back('\n');
    }
    return s;
}

bool laufeBis(A5120Machine& m, const std::string& needle, long long max_cycles) {
    for (long long n = 0; n < max_cycles; n += kSmallBatch) {
        m.run(static_cast<int>(kSmallBatch));
        if ((n % 100'000) == 0 && bild(m).find(needle) != std::string::npos) return true;
    }
    return bild(m).find(needle) != std::string::npos;
}

/// Systemdiskette V1.2 „ohne SYL“ (`satz2_OhneSYL.1`, „SYS1“) als Datendiskette in B:.
/// Die SYL-Disketten taugen nicht als Dateidiskette: der DPB der V1.2 (EC73H/EC82H: 26×128,
/// Bloecke 1 KB, 64 Eintraege, **OFF = 2**) sucht das Verzeichnis auf Zylinder 2 — dort liegt bei
/// ihnen das System; `dir a:` listet Systemcode als namenlose Eintraege.
constexpr const char* kDatendiskette = "bcu12_ss40-26x128_k5600-10_ohnesyl-sys1.img";

/// Bis zum BCU-Prompt `A>`, dann `dir b:` ueber die K7636: BCU listet die Dateien der
/// Systemdiskette in B: (u. a. `KMBG.REL`, `EPROM.COM`).  @p ende = Taste, die die Zeile
/// abschliesst (die beiden BIOS-Fassungen lesen die K7636 verschieden, siehe Faelle).
void bootUndDir(A5120Machine& m, uint32_t ende) {
    ASSERT_TRUE(laufeBis(m, "\nA>", 40'000'000)) << "kein A>:\n" << bild(m);
    EXPECT_NE(bild(m).find("BCU 880 VERS 1.2"), std::string::npos) << bild(m);
    EXPECT_NE(bild(m).find("FZW/BL"), std::string::npos) << bild(m);
    // Der Kaltstart schickt der K7636 Kommando 00H (Reset, D69CH: `OUT (03H),A`) und holt das
    // Antwort-Typzeichen 80H nie ab — es wird das erste Zeichen der ersten Eingabezeile (das
    // BDOS liest es schon beim Prompt ueber CONST vorab; CONOUT zeigt es als Zelle 80H).
    // Wie am Geraet: erst eine leere Zeile, die es verbraucht, dann der eigentliche Befehl.
    k1520test::typeKey(m, ende);
    auto prompts = [&] {
        int n = 0;
        const std::string s = bild(m);
        for (size_t p = s.find("\nA>"); p != std::string::npos; p = s.find("\nA>", p + 1)) ++n;
        return n;
    };
    for (long long n = 0; n < 20'000'000; n += kSmallBatch) {
        m.run(static_cast<int>(kSmallBatch));
        if ((n % 100'000) == 0 && prompts() >= 2) break;
    }
    ASSERT_GE(prompts(), 2) << "zweiter Prompt fehlt:\n" << bild(m);
    k1520test::typeString(m, "dir b:");
    ASSERT_TRUE(laufeBis(m, "dir b:", 20'000'000))
        << "Eingabe ueber die K7636 kommt nicht an:\n" << bild(m);
    k1520test::typeKey(m, ende);
    ASSERT_TRUE(laufeBis(m, "XSUB", 30'000'000)) << "dir b: ohne Verzeichnis:\n" << bild(m);
    EXPECT_NE(bild(m).find("KMBG     REL"), std::string::npos) << bild(m);
    EXPECT_NE(bild(m).find("EPROM    COM"), std::string::npos) << bild(m);
}

}  // namespace

/**
 * @brief Abnahme AP A7: BCU 880 V1.2 auf dem A5120 „5¼″ alt“ (K7023 64×16, K7636, 3× K5600.10)
 *        mit Lade-ROM V0.6 — Satz 2 „SYL auf Spur 1“ bis `A>`, `dir b:` getippt.
 */
TEST(BootIntegrationBcu, V12BisPrompt) {
    auto cfg = A5120Machine::Config::fuerAusfuehrung(A::Alt);
    cfg.laderom = L::V06;
    A5120Machine m(cfg);
    m.powerOn();
    TempDisk a("bcu12_ss40-26x128_k5600-10_syl-spur1-satz2.img");
    ASSERT_TRUE(m.mountDisk(0, a.path(), kFormat, false)) << m.lastError();
    TempDisk b(kDatendiskette);
    ASSERT_TRUE(m.mountDisk(1, b.path(), kFormat, false)) << m.lastError();
    // Der CONIN dieser Fassung reicht den K7636-Code unuebersetzt durch (EF34: IN 06H; RET):
    // ET1 (FFH) kaeme als Rubout 7FH an.  Die Zeile schliesst die Taste ↵ (A15, Code 0AH) —
    // die BDOS-Zeileneingabe endet auf CR wie auf LF.
    bootUndDir(m, K7636::QK_TASTE_BASE | 0x37);
}

/**
 * @brief Satz 1 „SYL auf Spur 1“ (BCU Kaltstart): dieselbe V1.2, Bildtreiber aber 80×24 ab F800H
 *        → K7024 mit K7636 am U-Bus (`Neu` + `Tastatur::K7636`), Laufwerke K5600.10.
 */
TEST(BootIntegrationBcu, V12Bab2BisPrompt) {
    A5120Machine::Config cfg;
    cfg.laderom  = L::V06;
    cfg.tastatur = A5120Machine::Config::Tastatur::K7636;
    cfg.drive_profiles = {"K5600.10", "K5600.10", "K5600.10", "none"};
    A5120Machine m(cfg);
    m.powerOn();
    TempDisk a("bcu12_ss40-26x128_k5600-10_syl-spur1-satz1.img");
    ASSERT_TRUE(m.mountDisk(0, a.path(), kFormat, false)) << m.lastError();
    TempDisk b(kDatendiskette);
    ASSERT_TRUE(m.mountDisk(1, b.path(), kFormat, false)) << m.lastError();
    // Diese Fassung uebersetzt im CONIN (Tafel EF58H → EF65H): ET1 (FFH) → 0DH.
    bootUndDir(m, QK_RETURN);
}

/**
 * @brief BCU 880 V2.1 („ohne SYL“, Satz 2.2 — laut DT-INFO „Bootfaehig: Ja“) startet mit KEINEM
 *        der vorhandenen Lade-ROMs: V0.9 liest Zylinder 0 und verlangt `SYL` in 0400H, V0.6
 *        liest Zylinder 1 und verlangt es dort; die Diskette traegt auf Zylinder 0 Sektor 1
 *        einen eigenen Kopf (`40 20 'BCU 880 V.24/P'` + Code), auf Zylinder 1 Systemcode.
 *        Das passende Start-EPROM liegt nicht vor (doc/a5120_alt/bcu.md §5) — dieser Fall haelt
 *        den Befund fest, statt einen Start zu erfinden.
 */
TEST(BootIntegrationBcu, V21OhneSylStartetMitKeinemVorhandenenLaderom) {
    for (L l : {L::V09_2, L::V09_1, L::V06}) {
        auto cfg = A5120Machine::Config::fuerAusfuehrung(A::Alt);
        cfg.laderom = l;
        A5120Machine m(cfg);
        m.powerOn();
        TempDisk a("bcu21_ss40-26x128_k5600-10_ohnesyl-satz2-2.img");
        ASSERT_TRUE(m.mountDisk(0, a.path(), kFormat, false)) << m.lastError();
        // Das Lade-ROM laeuft ganz unterhalb 0400H (ROM bzw. seine RAM-Kopie 00BAH–03FFH);
        // jeder Befehl ab 0400H hiesse: ein geladenes Stueck wurde angesprungen.
        uint16_t ausDemRom = 0;
        long long befehle = 0;
        m.setCpuTraceCallback([&](const Z80& c) {
            ++befehle;
            if (c.PC >= 0x0400 && !ausDemRom) ausDemRom = c.PC;
        });
        k1520test::runCycles(m, 12'000'000);
        m.setCpuTraceCallback(nullptr);
        EXPECT_GE(m.cpuCycles(), 11'000'000u) << "Lauf zu kurz";   // ZVE1-Takte (ohne ZVE2-Anteil)
        EXPECT_GT(befehle, 100'000) << "Befehlsrueckruf kam nicht an — der Fall pruefte nichts";
        EXPECT_EQ(ausDemRom, 0) << "Lade-ROM " << int(l) << " hat Code uebergeben, PC="
                                << std::hex << ausDemRom;
        EXPECT_EQ(bild(m).find("BCU"), std::string::npos) << bild(m);
    }
}

/**
 * @brief EXPERIMENT (AP A7b) — EIGENBAU, kein Original: V2.1-System im SYL-Bootaufbau.
 *
 * `tools/bcu_v21_eigenbau.py --treiber` setzt aus der V1.2-SYL-Diskette (Lader Zylinder 1,
 * Kaltstartteil, residenter Plattentreiber F100H–F47FH) und dem V2.1-System der „ohne SYL“-
 * Diskette (`satz2_OhneSYL.2`) eine Diskette zusammen, die es nie gab (doc/a5120_alt/bcu.md §9).
 * Der Test haelt fest, dass das V2.1-BDOS damit bis `A>` kommt und B: liest.  Er sagt NICHTS
 * darueber, wie ein echtes V2.1-Geraet startete.  Ausfuehrung `Neu` + K7636: V2.1 schreibt 80×24
 * ab F800H (CONOUT des BIOS: Zeile 50H).
 */
TEST(BootIntegrationBcu, V21EigenbauBootet) {
    A5120Machine::Config cfg;
    cfg.laderom  = L::V06;
    cfg.tastatur = A5120Machine::Config::Tastatur::K7636;
    cfg.drive_profiles = {"K5600.10", "K5600.10", "K5600.10", "none"};
    A5120Machine m(cfg);
    m.powerOn();
    TempDisk a("bcu21_ss40-26x128_k5600-10_eigenbau-syl-v21.img");
    ASSERT_TRUE(m.mountDisk(0, a.path(), kFormat, false)) << m.lastError();
    TempDisk b("bcu21_ss40-26x128_k5600-10_ohnesyl-satz2-2.img");
    ASSERT_TRUE(m.mountDisk(1, b.path(), kFormat, false)) << m.lastError();
    ASSERT_TRUE(laufeBis(m, "\nA>", 60'000'000)) << "kein A>:\n" << bild(m);
    EXPECT_NE(bild(m).find("BCU 880 V.24-2.1"), std::string::npos) << bild(m);
    // Wie bei V1.2 (Satz 2): CONIN ohne Uebersetzung, Zeilenende = ↵ (A15); die erste Zeile
    // verbraucht das Typzeichen 80H der K7636.
    const uint32_t ende = K7636::QK_TASTE_BASE | 0x37;
    k1520test::typeKey(m, ende);
    k1520test::typeString(m, "dir b:");
    ASSERT_TRUE(laufeBis(m, "dir b:", 30'000'000)) << bild(m);
    k1520test::typeKey(m, ende);
    ASSERT_TRUE(laufeBis(m, "ANMELD", 40'000'000)) << "dir b: ohne Verzeichnis:\n" << bild(m);
}
