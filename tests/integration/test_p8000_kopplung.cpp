/**
 * @file test_p8000_kopplung.cpp
 * @brief P8000 Kopplung 8-Bit ↔ 16-Bit (doc/design/25_p8000.md §10.3, AP P11): Leitungen K1–K15
 *        an den echten Karten, dazu der Save-State P8KS v2 der Maschine mit 16-Bit-Teil.
 *
 * Die Pegelfälle programmieren die PIOs so, wie MON8 (`K.MON8.S`) und MON16 (`p.init.s` ENTRY_)
 * es tun: 8-PIO0 A Byteeingabe (Modus 1), B Bitbetrieb B0–B6 Eingang/B7 Ausgang; 16-PIO0 A
 * Byteausgabe (Modus 0), B Bit 0–4 Ausgang/5–6 Eingang; 16-PIO1 A Byteeingabe, B Bitbetrieb Eingang.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/bus/k1520_bus.h"
#include "core/cards/p8000/karte16.h"
#include "core/cards/p8000/karte8.h"
#include "core/cards/p8000/kopplung.h"
#include "core/logger.h"
#include "core/machines/p8000/p8000.h"
#include "tests/support/p8000_input.h"

using namespace k1520test::p8000;

namespace {
// 8-Bit-Seite
constexpr uint8_t P0AD = 0x0C, P0AC = 0x0D, P0BD = 0x0E, P0BC = 0x0F, L1 = 0x10, L2 = 0x14;
// 16-Bit-Seite (FF91 …: A2 = Steuerwort, A1 = Port B)
constexpr uint16_t Q0DA = 0xFF91, Q0DB = 0xFF93, Q0CA = 0xFF95, Q0CB = 0xFF97;
constexpr uint16_t Q1DA = 0xFF99, Q1DB = 0xFF9B, Q1CA = 0xFF9D, Q1CB = 0xFF9F;

struct Paar {
    K1520Bus bus;
    P8000Karte8 k8;
    P8000Karte16 k16;
    P8000Kopplung kp;

    static P8000Karte16::Config cfg16(P8000Karte16::Config::Index i) {
        P8000Karte16::Config c;
        c.index = i;
        return c;
    }
    explicit Paar(P8000Karte16::Config::Index i = P8000Karte16::Config::Index::I4, bool rueck = true)
        : k8(bus), k16(cfg16(i)), kp(k8, k16, P8000Kopplung::Config{rueck}) {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
        k8.powerOn();
        k16.powerOn();
        kp.rechne();
    }
    void out8(uint8_t p, uint8_t v) { k8.ioWrite(p, v); }
    uint8_t in8(uint8_t p) { return k8.ioRead(p); }
    static Z8kBusCycle io(uint16_t port, bool lesen) {
        Z8kBusCycle c;
        c.st = Z8kStatus::Io; c.system = true; c.word = false; c.read = lesen; c.addr = port;
        return c;
    }
    void out16(uint16_t p, uint8_t v) { k16.busSchreiben(io(p, false), v); }
    uint8_t in16(uint16_t p) { return uint8_t(k16.busLesen(io(p, true))); }

    /// MON8: 8-PIO0 B Bitbetrieb, B0–B6 Eingang, B7 Ausgang; A Byteeingabe mit Interrupt.
    void init8(uint8_t b7 = 0x80) {
        out8(P0BC, 0xCF); out8(P0BC, 0x7F);
        out8(P0BD, b7);
        out8(P0AC, 0x10); out8(P0AC, 0x4F); out8(P0AC, 0x83);
        (void)in8(P0AD);                       // Scheineingabe ⇒ ARDY H
    }
    /// MON16 ENTRY_: PIO0 A Byteausgabe (Interrupt an), B Bit 0–4 Ausgabe/5–6 Eingabe; PIO1 A
    /// Byteeingabe, B Bit-Eingabe.
    void init16() {
        out16(Q0CA, 0x20); out16(Q0CA, 0x0F); out16(Q0CA, 0x83);
        out16(Q0CB, 0xCF); out16(Q0CB, 0x60);
        out16(Q1CA, 0x4F); (void)in16(Q1DA);
        out16(Q1CB, 0xCF); out16(Q1CB, 0xFF);
    }
    bool int8()  { bus.markIntDirty(); bus.updateInterruptChain(); return bus.isINT(); }
    bool int16() { auto& b = k16.peripherieBus(); b.markIntDirty(); b.updateInterruptChain(); return b.isINT(); }
};
}  // namespace

// ─── K11/K12: Reset und NMI-Weiche ───────────────────────────────────────────

TEST(P8000Kopplung, B7HaeltSechzehnBitImReset) {
    Paar p;
    EXPECT_TRUE(p.k16.inReset());                        // B7 Eingang ⇒ Pull-up ⇒ RESET aktiv
    EXPECT_TRUE(p.kp.pegel().reset16);
    p.init8(0x80);
    EXPECT_TRUE(p.k16.inReset());
    p.out8(P0BD, 0x00);                                  // „START U8000"
    EXPECT_FALSE(p.k16.inReset());
    EXPECT_GT(p.k16.schritt(), 1);                       // der U8001 arbeitet Befehle ab
    p.out8(P0BD, 0x80);
    EXPECT_TRUE(p.k16.inReset());
    p.out8(P0BD, 0x00);
    p.k8.reset();                                        // Taste /RESP: PIOs zurück ⇒ B7 offen
    EXPECT_TRUE(p.k16.inReset());
}

TEST(P8000Kopplung, B7NullGibtFreiUndLenktNmi) {
    Paar p;
    p.init8(0x80);
    p.k8.nmiTaste();                                     // B7 = 1 ⇒ U880
    EXPECT_TRUE(p.bus.isNMI());
    EXPECT_EQ(p.k16.nmiIdentifier() & 1, 0);
    p.bus.clearNMI();
    p.out8(P0BD, 0x00);
    p.k8.nmiTaste();                                     // B7 = 0 ⇒ MANUALNMI (K12)
    EXPECT_FALSE(p.bus.isNMI());
    EXPECT_EQ(p.k16.nmiIdentifier() & 1, 1);
    for (int i = 0; i < 10; ++i) p.k16.schritt();        // Tastenimpuls (24 Takte) vorbei
    EXPECT_EQ(p.k16.nmiIdentifier() & 1, 0);
}

// ─── K1–K5, K15: Byte 8 → 16 (Bit-Banging über die DS8282) ───────────────────

TEST(P8000Kopplung, SoftwareStrobe8nach16) {
    Paar p;
    p.init8();
    p.init16();
    EXPECT_TRUE(p.k16.pio1().ardy());                    // Eingaberegister leer
    EXPECT_EQ(p.in8(P0BD) & 0x40, 0x40);                 // K5: „Port frei" (MON8 OUTP16)
    EXPECT_EQ(p.in16(Q0DB) & 0x20, 0x20);                // K15: B5 „DATEN DA" = 1 (nein)

    p.out8(L1, 0xAA);                                    // K1
    p.out8(L2, 0x00);                                    // /ASTB = L
    EXPECT_TRUE(p.k16.pio1().ardy());
    p.out8(L2, 0x2A | 0x80);                             // K4: steigende Flanke
    EXPECT_FALSE(p.k16.pio1().ardy());
    EXPECT_EQ(p.in8(P0BD) & 0x40, 0x00);                 // K5
    EXPECT_EQ(p.in16(Q0DB) & 0x20, 0x00);                // K15: DATEN DA
    EXPECT_EQ(p.in16(Q1DB) & 0x7F, 0x2A);                // K2/K3: Status, INT-16 = 0
    EXPECT_EQ(p.in16(Q1DA), 0xAA);                       // Datenbyte; Lesen ⇒ ARDY H
    EXPECT_TRUE(p.k16.pio1().ardy());
    EXPECT_EQ(p.in8(P0BD) & 0x40, 0x40);

    p.out8(L2, 0x01);                                    // K2: INT-16 als Pegel an B0
    EXPECT_EQ(p.in16(Q1DB) & 0x01, 0x01);
}

TEST(P8000Kopplung, OhneRueckfuehrungLesenB5B6EinsIndex1) {
    Paar p(P8000Karte16::Config::Index::I1, /*rueck=*/false);   // Brücken 4XR1/5XR1 gezogen
    p.init8();
    p.init16();
    p.out8(L1, 0x11); p.out8(L2, 0x00); p.out8(L2, 0x80);
    EXPECT_FALSE(p.k16.pio1().ardy());
    EXPECT_EQ(p.in16(Q0DB) & 0x60, 0x60);                // offen ⇒ Pull-up, MON16 sähe nichts
}

// ─── K6–K10, K15: Byte 16 → 8 (PIO-Handschlag ARDY → /ASTB) ───────────────────

TEST(P8000Kopplung, PioPioHandschlag16nach8) {
    Paar p;
    p.init8();
    p.init16();
    EXPECT_TRUE(p.k8.pio0().ardy());                     // K8 = H
    EXPECT_FALSE(p.k16.pio0().ardy());
    EXPECT_EQ(p.in16(Q0DB) & 0x40, 0x00);                // K15: ENABLE OUTPUT
    EXPECT_EQ(p.in8(P0BD) & 0x20, 0x00);                 // K7: keine Daten
    EXPECT_FALSE(p.int8());
    EXPECT_FALSE(p.int16());

    p.out16(Q0DB, 0x0A);                                 // K9/K10: Status, INT-8 = 0
    EXPECT_EQ(p.in8(P0BD) & 0x1F, 0x0A);
    p.out16(Q0DA, 0x55);                                 // K6 + K7: ARDY H ⇒ /ASTB ↑ an der 8-Bit-PIO
    EXPECT_FALSE(p.k8.pio0().ardy());                    // übernommen
    EXPECT_TRUE(p.int8());                               // Interrupt auf der 8-Bit-Seite
    EXPECT_EQ(p.in8(P0BD) & 0x20, 0x20);                 // B5 liest DD-8 zurück
    EXPECT_EQ(p.in16(Q0DB) & 0x40, 0x40);                // ENABLE OUTPUT weg
    EXPECT_FALSE(p.int16());

    EXPECT_EQ(p.in8(P0AD), 0x55);                        // Lesen ⇒ 8-ARDY H ⇒ K8 ↑ ⇒ 16-ARDY L
    EXPECT_TRUE(p.k8.pio0().ardy());
    EXPECT_FALSE(p.k16.pio0().ardy());
    EXPECT_TRUE(p.int16());                              // Interrupt auf der 16-Bit-Seite
    EXPECT_EQ(p.in16(Q0DB) & 0x40, 0x00);
    EXPECT_EQ(p.in8(P0BD) & 0x20, 0x00);

    p.out16(Q0DB, 0x01);                                 // K9 als Pegel
    EXPECT_EQ(p.in8(P0BD) & 0x01, 0x01);
}

// ─── Reset-Taste: PIOs der 16-Bit-Seite (Index 4 bleiben, Index 1 zurück) ─────

TEST(P8000Kopplung, ResetTasteLaesstSechzehnBitPiosStehenIndex4) {
    Paar p(P8000Karte16::Config::Index::I4);
    p.init8(0x00);
    p.init16();
    ASSERT_FALSE(p.k16.inReset());
    ASSERT_TRUE(p.k16.pio1().ardy());
    p.k8.reset();                                        // B7 offen ⇒ RESET ⇒ MRESET−
    EXPECT_TRUE(p.k16.inReset());
    EXPECT_TRUE(p.k16.pio1().ardy());                    // PIORESET− hängt nicht an RESET
}

TEST(P8000Kopplung, ResetTasteSetztSechzehnBitPiosZurueckIndex1) {
    Paar p(P8000Karte16::Config::Index::I1);
    p.init8(0x00);
    p.init16();
    ASSERT_TRUE(p.k16.pio1().ardy());
    p.k8.reset();
    EXPECT_TRUE(p.k16.inReset());
    EXPECT_FALSE(p.k16.pio1().ardy());                   // Index 1: PIOs an MRESET−
}

// ─── Maschine: Save-State P8KS v2 mit 16-Bit-Teil ─────────────────────────────

namespace {
P8000Machine::Config mit16() {
    P8000Machine::Config c;
    c.karte16 = true;
    return c;
}
std::string ersterUnterschied(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) return "Länge " + std::to_string(a.size()) + " ≠ " + std::to_string(b.size());
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) {
            std::string r = "erstes abweichendes Byte bei Versatz " + std::to_string(i);
            for (size_t q = 5; q + 5 <= a.size();) {   // Abschnitt [id u8][len u32]
                const uint32_t n = uint32_t(a[q + 1]) | uint32_t(a[q + 2]) << 8 | uint32_t(a[q + 3]) << 16 | uint32_t(a[q + 4]) << 24;
                if (i < q + 5 + n) { r += " (Abschnitt " + std::to_string(a[q]) + ", +" + std::to_string(i - q - 5) + " von " + std::to_string(n) + ")"; break; }
                q += 5 + n;
            }
            return r;
        }
    return "gleich";
}
}  // namespace

/// Lauf bis zum U8000-Monitor (Konsole über die Kopplung), speichern, in eine zweite Maschine laden,
/// in beiden dasselbe Kommando tippen — danach Byte für Byte gleich.  Gespeichert wird erst am
/// Prompt `*`: während der SIO-Tests 60/61 sind Zeichen an tty4–tty7 im Hub unterwegs, und der
/// Hub-Wandler steht nicht im Stand (P7e).
TEST(P8000Stand, RundreiseMitSechzehnBitTeilIstBitgleich) {
    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    P8000Machine a(mit16()), b(mit16());
    a.powerOn();
    b.powerOn();
    ASSERT_TRUE(laufeBisText(a, "Press RETURN", 200'000'000)) << bild(a);
    tippe(a, "\r");
    ASSERT_TRUE(laufeBisPrompt(a, ">", 4'000'000)) << bild(a);
    tippeZeile(a, "x");
    ASSERT_TRUE(laufeBisText(a, "Press NMI", 40'000'000)) << bild(a);
    laufe(a, 2'000'000);
    a.nmi();
    ASSERT_TRUE(laufeBisText(a, "MAXSEG=<0F>", 400'000'000)) << bild(a);
    ASSERT_TRUE(laufeBisPrompt(a, "*", 40'000'000)) << bild(a);

    const std::vector<uint8_t> stand = a.stateBytes();
    ASSERT_GT(stand.size(), 1'000'000u);                 // 1 MB DRAM des 16-Bit-Teils
    ASSERT_TRUE(b.restoreStateBytes(stand)) << b.stateError();
    EXPECT_TRUE(b.stateBytes() == stand) << ersterUnterschied(b.stateBytes(), stand);
    // Ein Kommando über die Kopplung (Terminal → 8-Bit-SIO → Kopplung → MON16 → zurück).
    tippeZeile(a, "D 8000");
    tippeZeile(b, "D 8000");
    for (int runde = 0; runde < 3; ++runde) {
        laufe(a, 2'000'000);
        laufe(b, 2'000'000);
        const auto sa = a.stateBytes(), sb = b.stateBytes();
        ASSERT_TRUE(sa == sb) << "Runde " << runde << ": " << ersterUnterschied(sa, sb);
    }
    EXPECT_EQ(bild(a), bild(b));
    EXPECT_NE(bild(a).find("<00>8000 "), std::string::npos) << bild(a);   // MON16 hat geantwortet
    EXPECT_EQ(a.karte16()->cpu().pc, b.karte16()->cpu().pc);
}

/// Ein Stand P8KS v1 (ohne 16-Bit-Teil) lädt weiter in eine Maschine ohne 16-Bit-Karte und wird
/// von einer Maschine mit 16-Bit-Karte abgelehnt; ein v2-Stand passt nur zur selben Bestückung.
TEST(P8000Stand, StandV1LaedtNurOhneSechzehnBitTeil) {
    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    P8000Machine ohne, quelle;
    quelle.powerOn();
    laufe(quelle, 1'000'000);
    std::vector<uint8_t> v = quelle.stateBytes();
    // v3 → v1: Version 1, im Konfigurationsabschnitt (der erste) fehlt das Flag „16-Bit-Karte"
    // (ohne 16-Bit-Karte sind v2 und v3 gleich aufgebaut).
    ASSERT_EQ(v[4], P8000Machine::P8000_STAND);
    ASSERT_EQ(v[5], 1);
    uint32_t n = uint32_t(v[6]) | uint32_t(v[7]) << 8 | uint32_t(v[8]) << 16 | uint32_t(v[9]) << 24;
    ASSERT_EQ(v[10 + n - 1], 0);                         // karte16 = false
    v.erase(v.begin() + 10 + n - 1);
    --n;
    for (int i = 0; i < 4; ++i) v[size_t(6 + i)] = uint8_t(n >> (8 * i));
    v[4] = 1;

    ASSERT_TRUE(ohne.restoreStateBytes(v)) << ohne.stateError();
    EXPECT_EQ(ohne.stateBytes(), quelle.stateBytes());

    P8000Machine mit(mit16());
    EXPECT_FALSE(mit.restoreStateBytes(v));
    EXPECT_NE(mit.stateError().find("v1"), std::string::npos) << mit.stateError();
    EXPECT_FALSE(mit.restoreStateBytes(quelle.stateBytes()));   // v3 ohne 16-Bit-Teil
    EXPECT_FALSE(ohne.restoreStateBytes(mit.stateBytes()));     // v3 mit 16-Bit-Teil
}
