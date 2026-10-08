/**
 * @file test_z8.cpp
 * @brief Z8-Primitive (core/primitives/z8.h): Befehlssatz gegen Zilog UM0016 — Matrix je
 *        Opcode und Adressierungsart mit Flags, Takte, Registerdatei.  Peripherie (Zähler,
 *        UART, Interrupts, Ports, Bus) in test_z8_peripherie.cpp.
 *
 * Die Erwartungswerte kommen aus EIGENEN Referenzfunktionen, die die Datenblatt-Definition
 * wörtlich nachrechnen (vorzeichenbehafteter Bereich für V, Borgen für C/H usw.), nicht aus
 * der Kernrechnung.  Abdeckung: doc/p8000/z8_abdeckung.md.
 */
#include "tests/unit/primitives/z8_rig.h"
#include "core/primitives/z8/z8_table.h"

using z8test::Rig;

namespace {

constexpr uint8_t C = Z8::F_C, Z = Z8::F_Z, S = Z8::F_S, V = Z8::F_V, D = Z8::F_D, H = Z8::F_H;

struct Ref { uint8_t r; uint8_t f; bool schreibt; };

/// Datenblatt-Referenz für ADD … XOR (Table 39): Ergebnis und Flags.
Ref refAlu(const std::string& mn, uint8_t d, uint8_t s, uint8_t fin) {
    Ref o{0, fin, true};
    const int ci = (fin & C) ? 1 : 0;
    auto set = [&](uint8_t f, bool an) { o.f = an ? uint8_t(o.f | f) : uint8_t(o.f & ~f); };
    auto zs = [&](uint8_t r) { set(Z, r == 0); set(S, r & 0x80); };
    if (mn == "ADD" || mn == "ADC") {
        const int c = mn == "ADC" ? ci : 0;
        const int u = d + s + c;
        const int sv = int8_t(d) + int8_t(s) + c;
        o.r = uint8_t(u);
        set(C, u > 255); set(V, sv < -128 || sv > 127); set(D, false);
        set(H, (d & 15) + (s & 15) + c > 15); zs(o.r);
    } else if (mn == "SUB" || mn == "SBC" || mn == "CP") {
        const int c = mn == "SBC" ? ci : 0;
        const int u = d - s - c;
        const int sv = int8_t(d) - int8_t(s) - c;
        o.r = uint8_t(u);
        set(C, u < 0); set(V, sv < -128 || sv > 127); zs(o.r);
        if (mn == "CP") o.schreibt = false;
        else { set(D, true); set(H, (d & 15) - (s & 15) - c < 0); }
    } else {
        if (mn == "AND") o.r = d & s;
        else if (mn == "OR") o.r = d | s;
        else if (mn == "XOR") o.r = d ^ s;
        else if (mn == "TM") { o.r = d & s; o.schreibt = false; }
        else /*TCM*/ { o.r = uint8_t(~d) & s; o.schreibt = false; }
        set(V, false); zs(o.r);
    }
    return o;
}

/// Referenz der Einoperandenbefehle (ohne DA).
Ref refEin(const std::string& mn, uint8_t d, uint8_t fin) {
    Ref o{d, fin, true};
    auto set = [&](uint8_t f, bool an) { o.f = an ? uint8_t(o.f | f) : uint8_t(o.f & ~f); };
    const bool c = fin & C;
    const bool vorz = d & 0x80;
    if (mn == "DEC") { o.r = uint8_t(d - 1); set(V, int8_t(d) == -128); }
    else if (mn == "INC") { o.r = uint8_t(d + 1); set(V, int8_t(d) == 127); }
    else if (mn == "COM") { o.r = uint8_t(~d); set(V, false); }
    else if (mn == "CLR") { o.r = 0; return o; }                    // keine Flags
    else if (mn == "RL") { o.r = uint8_t(d << 1 | d >> 7); set(C, vorz); set(V, bool(o.r & 0x80) != vorz); }
    else if (mn == "RLC") { o.r = uint8_t(d << 1 | c); set(C, vorz); set(V, bool(o.r & 0x80) != vorz); }
    else if (mn == "RR") { o.r = uint8_t(d >> 1 | d << 7); set(C, d & 1); set(V, bool(o.r & 0x80) != vorz); }
    else if (mn == "RRC") { o.r = uint8_t(d >> 1 | (c ? 0x80 : 0)); set(C, d & 1); set(V, bool(o.r & 0x80) != vorz); }
    else if (mn == "SRA") { o.r = uint8_t(int8_t(d) >> 1); set(C, d & 1); set(V, false); }
    else if (mn == "SWAP") { o.r = uint8_t(d << 4 | d >> 4); }       // C, V undefiniert (bleiben)
    set(Z, o.r == 0); set(S, o.r & 0x80);
    return o;
}

const uint8_t WERTE[] = {0x00, 0x01, 0x09, 0x0F, 0x10, 0x55, 0x7F, 0x80, 0x81, 0x99, 0xAA, 0xF0, 0xFE, 0xFF};
const uint8_t FLAGS_EIN[] = {0x00, C, uint8_t(C | Z | S | V | D | H), uint8_t(Z | V | H), uint8_t(D | Z8::F_F1 | Z8::F_F2)};

/// Vorbereitung für die sechs Adressierungsarten der Zweioperandenbefehle:
/// RP = 40H; Ziel 40H (r0), Quelle 41H (r1), Zeiger 42H → 41H, 43H → 40H.
void bereite(Rig& rg, uint8_t d, uint8_t s, uint8_t f) {
    rg.cpu.rp = 0x40;
    rg.r(0x40) = d; rg.r(0x41) = s; rg.r(0x42) = 0x41; rg.r(0x43) = 0x40;
    rg.cpu.flags = f;
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Tabelle
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Tabelle, BelegteOpcodesUndLaengenWieOpcodeKarte) {
    int belegt = 0;
    for (int op = 0; op < 256; ++op) {
        const z8::Insn& in = z8::tabelle()[uint8_t(op)];
        if (in.mn == z8::Mn::Ungueltig) continue;
        if (in.mn == z8::Mn::HALT || in.mn == z8::Mn::STOP) continue;   // Z86
        ++belegt;
    }
    // Figure 134: 16×(8..E) = 112, Spalten 0/1: 32, Spalten 2–7: 10×6 + 19 Sonderzeilen, Spalte F: 8
    EXPECT_EQ(belegt, 112 + 32 + 60 + 19 + 8);
    EXPECT_EQ(z8::tabelle()[0xD6].len, 3);   // CALL DA
    EXPECT_EQ(z8::tabelle()[0xD4].len, 2);   // CALL @RR („2-Byte, erscheint als 3"?)
    EXPECT_EQ(z8::tabelle()[0xC7].len, 3);
    EXPECT_EQ(z8::tabelle()[0x31].len, 2);
    for (int op : {0xE2, 0xF2, 0x84, 0x94, 0xC4, 0xF4, 0x85, 0xC5, 0xD5, 0xF6, 0xF7, 0x0F, 0x5F})
        EXPECT_EQ(z8::tabelle()[uint8_t(op)].mn, z8::Mn::Ungueltig) << std::hex << op;
}

// ═════════════════════════════════════════════════════════════════════════════
// Zweioperandenbefehle: 10 Operationen × 6 Adressierungsarten × Werte × Flags
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Alu, MatrixAlleOperationenAlleArtenMitFlagsUndTakten) {
    const struct { const char* mn; uint8_t row; } ops[] = {
        {"ADD", 0x0}, {"ADC", 0x1}, {"SUB", 0x2}, {"SBC", 0x3}, {"OR", 0x4},
        {"AND", 0x5}, {"TCM", 0x6}, {"TM", 0x7}, {"CP", 0xA}, {"XOR", 0xB}};
    const int takte[6] = {6, 6, 10, 10, 10, 10};   // Figure 134
    int faelle = 0;
    Rig rg;
    rg.boot();
    for (auto& o : ops) {
        for (int art = 0; art < 6; ++art) {
            const uint8_t op = uint8_t(o.row << 4 | (2 + art));
            for (uint8_t d : WERTE) for (uint8_t s : WERTE) for (uint8_t f : FLAGS_EIN) {
                bereite(rg, d, s, f);
                int c;
                switch (art) {
                case 0: c = rg.eins({op, 0x01}); break;          // r0,r1
                case 1: c = rg.eins({op, 0x02}); break;          // r0,@r2
                case 2: c = rg.eins({op, 0x41, 0x40}); break;    // 40H,41H
                case 3: c = rg.eins({op, 0x42, 0x40}); break;    // 40H,@42H
                case 4: c = rg.eins({op, 0x40, s}); break;       // 40H,#s
                default: c = rg.eins({op, 0x43, s}); break;      // @43H,#s
                }
                const Ref e = refAlu(o.mn, d, s, f);
                ASSERT_EQ(rg.r(0x40), e.schreibt ? e.r : d) << o.mn << " Art " << art << " d=" << int(d) << " s=" << int(s) << " f=" << int(f);
                ASSERT_EQ(rg.cpu.flags, e.f) << o.mn << " Art " << art << " d=" << int(d) << " s=" << int(s) << " f=" << int(f);
                ASSERT_EQ(c, takte[art]) << o.mn << " Art " << art;
                ASSERT_EQ(rg.r(0x41), art >= 4 ? s : s);   // Quelle unverändert
                ++faelle;
            }
        }
    }
    EXPECT_EQ(faelle, 10 * 6 * 14 * 14 * 5);
}

TEST(Z8Alu, HandbuchBeispielAdd43h08h) {
    // UM0016 S. 149: ADD 43h,08h → 04 08 43
    std::vector<uint8_t> b; std::string err;
    ASSERT_TRUE(z8asm::befehl("ADD 43H,08H", 0, b, err)) << err;
    EXPECT_EQ(b, (std::vector<uint8_t>{0x04, 0x08, 0x43}));
}

TEST(Z8Alu, ArbeitsregisterUeberEscapeEx) {
    // R-Feld E3 = Arbeitsregister r3 = RP.7–4 | 3 (UM0016 Table 37, „adding 1110b")
    Rig rg; rg.boot();
    rg.cpu.rp = 0x70; rg.r(0x73) = 5; rg.r(0x30) = 7;
    rg.eins({0x04, 0x30, 0xE3});          // ADD r3,30H
    EXPECT_EQ(rg.r(0x73), 12);
}

// ═════════════════════════════════════════════════════════════════════════════
// Einoperandenbefehle
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Einop, MatrixAlleWerteBeideArtenMitFlagsUndTakten) {
    const struct { const char* mn; uint8_t row; int takte; } ops[] = {
        {"DEC", 0x0, 6}, {"RLC", 0x1, 6}, {"INC", 0x2, 6}, {"COM", 0x6, 6}, {"RL", 0x9, 6},
        {"CLR", 0xB, 6}, {"RRC", 0xC, 6}, {"SRA", 0xD, 6}, {"RR", 0xE, 6}, {"SWAP", 0xF, 8}};
    Rig rg; rg.boot();
    for (auto& o : ops)
        for (int ind = 0; ind < 2; ++ind)
            for (int d = 0; d < 256; ++d)
                for (uint8_t f : FLAGS_EIN) {
                    rg.r(0x50) = uint8_t(d); rg.r(0x60) = 0x50; rg.cpu.flags = f;
                    const int c = rg.eins({uint8_t(o.row << 4 | ind), uint8_t(ind ? 0x60 : 0x50)});
                    const Ref e = refEin(o.mn, uint8_t(d), f);
                    ASSERT_EQ(rg.r(0x50), e.r) << o.mn << " d=" << d;
                    ASSERT_EQ(rg.cpu.flags, e.f) << o.mn << " d=" << d << " f=" << int(f);
                    ASSERT_EQ(c, o.takte) << o.mn;
                }
}

TEST(Z8Einop, IncArbeitsregisterKurzform) {
    for (unsigned n = 0; n < 16; ++n) {
        Rig rg; rg.boot();
        rg.cpu.rp = 0x20;
        rg.r(uint8_t(0x20 + n)) = 0x7F;
        EXPECT_EQ(rg.eins({uint8_t(n << 4 | 0x0E)}), 6);
        EXPECT_EQ(rg.r(uint8_t(0x20 + n)), 0x80);
        EXPECT_EQ(rg.cpu.flags & (S | V | Z), S | V);
    }
}

TEST(Z8Einop, IncwDecwAlleArtenUndGrenzen) {
    const struct { uint16_t v; } werte[] = {{0x0000}, {0x00FF}, {0x7FFF}, {0x8000}, {0xFFFF}, {0x1234}};
    for (auto w : werte) for (int inc = 0; inc < 2; ++inc) for (int ind = 0; ind < 2; ++ind) {
        Rig rg; rg.boot();
        rg.cpu.rp = 0x30;
        rg.r(0x34) = uint8_t(w.v >> 8); rg.r(0x35) = uint8_t(w.v);
        rg.r(0x50) = 0x34;
        rg.cpu.flags = C | D | H;
        const uint8_t op = uint8_t((inc ? 0xA0 : 0x80) | ind);
        const int c = rg.eins({op, uint8_t(ind ? 0x50 : 0xE4)});   // rr4 bzw. @50H
        const uint16_t e = uint16_t(w.v + (inc ? 1 : -1));
        EXPECT_EQ(uint16_t(rg.r(0x34) << 8 | rg.r(0x35)), e);
        uint8_t f = C | D | H;
        if (e == 0) f |= Z;
        if (e & 0x8000) f |= S;
        if (inc ? int16_t(w.v) == 32767 : int16_t(w.v) == -32768) f |= V;
        EXPECT_EQ(rg.cpu.flags, f) << std::hex << w.v << " inc=" << inc;
        EXPECT_EQ(c, 10);
    }
}

TEST(Z8Einop, DaNachBcdAdditionUndSubtraktionAllePaare) {
    // UM0016 DA-Tabelle: für jedes gültige BCD-Paar ADD/ADC/SUB/SBC, dann DA.
    auto bcd = [](int v) { return uint8_t((v / 10) << 4 | (v % 10)); };
    Rig rg; rg.boot();
    for (int a = 0; a < 100; ++a) for (int b = 0; b < 100; ++b) for (int ci = 0; ci < 2; ++ci)
        for (int sub = 0; sub < 2; ++sub) {
            rg.cpu.rp = 0x40;
            rg.r(0x40) = bcd(a); rg.r(0x41) = bcd(b);
            rg.cpu.flags = ci ? C : 0;
            const uint8_t op = sub ? 0x32 : 0x12;              // SBC/ADC r0,r1
            rg.eins({op, 0x01});
            const int c = rg.eins({0x40, 0xE0}, 0x0200);       // DA r0
            int erg, carry;
            if (sub) { erg = a - b - ci; carry = erg < 0; if (erg < 0) erg += 100; }
            else { erg = a + b + ci; carry = erg >= 100; erg %= 100; }
            ASSERT_EQ(rg.r(0x40), bcd(erg)) << a << (sub ? "-" : "+") << b << "+c" << ci;
            ASSERT_EQ((rg.cpu.flags & C) != 0, carry != 0) << a << (sub ? "-" : "+") << b;
            ASSERT_EQ((rg.cpu.flags & Z) != 0, erg == 0);
            ASSERT_EQ((rg.cpu.flags & S) != 0, (bcd(erg) & 0x80) != 0);
            ASSERT_EQ(c, 8);
        }
}

TEST(Z8Einop, DaHandbuchBeispiel15plus27) {
    Rig rg; rg.boot();
    rg.r(0x5F) = 0x15;
    rg.eins({0x06, 0x5F, 0x27});      // ADD 5FH,#27H → 3CH
    EXPECT_EQ(rg.r(0x5F), 0x3C);
    rg.eins({0x40, 0x5F}, 0x0110);    // DA 5FH
    EXPECT_EQ(rg.r(0x5F), 0x42);
    EXPECT_EQ(rg.cpu.flags & (C | Z | S), 0);
    // DA @45H (Register 45H enthält 5FH)
    rg.r(0x45) = 0x5F; rg.r(0x5F) = 0x3C; rg.cpu.flags = 0;
    rg.eins({0x41, 0x45}, 0x0120);
    EXPECT_EQ(rg.r(0x5F), 0x42);
}

// ═════════════════════════════════════════════════════════════════════════════
// Laden
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Laden, AlleLdFormenWieHandbuchBeispiele) {
    // UM0016 LD, Beispiele 1–12 (Register 34H/45H/CFH auf 7xH verlegt, da 80H–EFH fehlen)
    Rig rg; rg.boot();
    rg.cpu.rp = 0x10;                                  // r0..r15 = 10H..1FH
    EXPECT_EQ(rg.eins({0xFC, 0x34}), 6);               // LD r15,#34H
    EXPECT_EQ(rg.r(0x1F), 0x34);
    rg.r(0x34) = 0xFC;
    EXPECT_EQ(rg.eins({0xE8, 0x34}), 6);               // LD r14,34H
    EXPECT_EQ(rg.r(0x1E), 0xFC);
    rg.r(0x1E) = 0x45;
    EXPECT_EQ(rg.eins({0xE9, 0x36}), 6);               // LD 36H,r14
    EXPECT_EQ(rg.r(0x36), 0x45);
    rg.r(0x1C) = 0x34; rg.r(0x34) = 0xFF;
    EXPECT_EQ(rg.eins({0xE3, 0xDC}), 6);               // LD r13,@r12
    EXPECT_EQ(rg.r(0x1D), 0xFF);
    rg.r(0x1D) = 0x45; rg.r(0x1C) = 0x00;
    EXPECT_EQ(rg.eins({0xF3, 0xDC}), 6);               // LD @r13,r12
    EXPECT_EQ(rg.r(0x45), 0x00);
    rg.r(0x45) = 0xCF;
    EXPECT_EQ(rg.eins({0xE4, 0x45, 0x34}), 10);        // LD 34H,45H
    EXPECT_EQ(rg.r(0x34), 0xCF);
    rg.r(0x45) = 0x7F; rg.r(0x7F) = 0xFF;
    EXPECT_EQ(rg.eins({0xE5, 0x45, 0x34}), 10);        // LD 34H,@45H
    EXPECT_EQ(rg.r(0x34), 0xFF);
    EXPECT_EQ(rg.eins({0xE6, 0x34, 0xA4}), 10);        // LD 34H,#0A4H
    EXPECT_EQ(rg.r(0x34), 0xA4);
    rg.r(0x1E) = 0x7F;
    EXPECT_EQ(rg.eins({0xE7, 0xEE, 0xFC}), 10);        // LD @r14,#0FCH
    EXPECT_EQ(rg.r(0x7F), 0xFC);
    rg.r(0x34) = 0x6F; rg.r(0x45) = 0xFF;
    EXPECT_EQ(rg.eins({0xF5, 0x45, 0x34}), 10);        // LD @34H,45H
    EXPECT_EQ(rg.r(0x6F), 0xFF);
    rg.r(0x10) = 0x08; rg.r(0x2C) = 0x4F;
    EXPECT_EQ(rg.eins({0xC7, 0xA0, 0x24}), 10);        // LD r10,24H(r0)
    EXPECT_EQ(rg.r(0x1A), 0x4F);
    rg.r(0x10) = 0x0B; rg.r(0x1A) = 0x83;
    EXPECT_EQ(rg.eins({0xD7, 0xA0, 0xF0}), 10);        // LD 0F0H(r0),r10 → IMR = 83H
    EXPECT_EQ(rg.cpu.imr, 0x83);
}

TEST(Z8Laden, ClrUndLdBeruehrenKeineFlags) {
    Rig rg; rg.boot();
    for (uint8_t f : {uint8_t(0), uint8_t(0xFF)}) {
        rg.cpu.flags = f;
        rg.eins({0xB0, 0x40}); rg.eins({0x0C, 0x00}); rg.eins({0xE6, 0x40, 0x00});
        EXPECT_EQ(rg.cpu.flags, f);
    }
}

TEST(Z8Laden, NichtVorhandeneRegisterLesenFFUndIgnorierenSchreiben) {
    Rig rg; rg.boot();
    for (int a : {0x80, 0xA0, 0xDF}) {          // (EFH wäre im Befehl r15)
        rg.r(0x40) = 0;
        rg.eins({0xE6, uint8_t(a), 0x12});            // LD a,#12H — ohne Wirkung
        rg.eins({0xE4, uint8_t(a), 0x40});            // LD 40H,a
        EXPECT_EQ(rg.r(0x40), 0xFF) << std::hex << a;
    }
    // Arbeitsregistergruppe 8 (RP = 80H) zeigt ins Leere
    rg.cpu.rp = 0x80;
    rg.eins({0x0C, 0x55});                            // LD r0,#55H
    rg.eins({0x09, 0x40});                            // LD 40H,r0
    EXPECT_EQ(rg.r(0x40), 0xFF);
}

TEST(Z8Laden, NurSchreibbareRegisterLesenFF) {
    // UM0016 S. 81: Vorteiler nur schreibbar, lesen FFH; ebenso P2M/P3M/P01M/IPR.
    Rig rg; rg.boot();
    for (int a : {0xF3, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9}) {
        rg.eins({0xE4, uint8_t(a), 0x40});
        EXPECT_EQ(rg.r(0x40), 0xFF) << std::hex << a;
    }
}

TEST(Z8Laden, LdcLdeLdciLdeiMitBuszyklen) {
    Rig rg; rg.boot();
    rg.eins({0xE6, 0xF8, 0x96});                       // P01M: A8–A15, AD0–7, Stapel intern
    rg.cpu.rp = 0x20;
    // Programmbus (unter 1000H) und externer Programmspeicher
    rg.prog[0x0456] = 0x22;
    rg.extProg[0x30A2] = 0x33;
    rg.r(0x26) = 0x04; rg.r(0x27) = 0x56;
    EXPECT_EQ(rg.eins({0xC2, 0x26}), 12);              // LDC r2,@rr6 (Programmbus)
    EXPECT_EQ(rg.r(0x22), 0x22);
    EXPECT_TRUE(rg.bus.empty() || rg.bus.back().c.art != Z8Zugriff::Konstante);
    rg.r(0x26) = 0x30; rg.r(0x27) = 0xA2;
    rg.bus.clear();
    rg.eins({0xC2, 0x26});
    EXPECT_EQ(rg.r(0x22), 0x33);
    ASSERT_EQ(rg.bus.size(), 1u);
    EXPECT_EQ(rg.bus[0].c.adresse, 0x30A2);
    EXPECT_EQ(rg.bus[0].c.art, Z8Zugriff::Konstante);
    EXPECT_FALSE(rg.bus[0].c.datenspeicher);
    EXPECT_TRUE(rg.bus[0].c.lesen);
    // LDC schreiben: Programmbus ohne Wirkung, extern ein Schreibzyklus /DM inaktiv
    rg.r(0x22) = 0x5A; rg.r(0x26) = 0x04; rg.r(0x27) = 0x00;
    rg.eins({0xD2, 0x26});                             // LDC @rr6,r2
    EXPECT_EQ(rg.prog[0x0400], 0xFF);
    EXPECT_EQ(rg.cpu.romSchreibversuche(), 1u);
    rg.r(0x26) = 0x80; rg.bus.clear();
    rg.eins({0xD2, 0x26});
    EXPECT_EQ(rg.extProg[0x8000], 0x5A);
    ASSERT_EQ(rg.bus.size(), 1u);
    EXPECT_FALSE(rg.bus[0].c.lesen);
    // LDE / LDEI: Datenspeicher (/DM nur mit P3M-Freigabe)
    rg.extDaten[0x404A] = 0xAB; rg.extDaten[0x404B] = 0xC3;
    rg.r(0x26) = 0x40; rg.r(0x27) = 0x4A; rg.r(0x22) = 0x52; rg.bus.clear();
    EXPECT_EQ(rg.eins({0x83, 0x26}), 18);              // LDEI @r2,@rr6
    EXPECT_EQ(rg.eins({0x83, 0x26}), 18);
    EXPECT_EQ(rg.r(0x52), 0xAB); EXPECT_EQ(rg.r(0x53), 0xC3);
    EXPECT_EQ(rg.r(0x22), 0x54); EXPECT_EQ(rg.r(0x27), 0x4C);
    ASSERT_EQ(rg.bus.size(), 2u);
    EXPECT_TRUE(rg.bus[0].c.datenspeicher);
    EXPECT_FALSE(rg.bus[0].c.dm);                      // P3M = 0: P34 ist normaler Ausgang
    rg.eins({0xE6, 0xF7, 0x08});                       // LD P3M,#08H (P34 = /DM)
    rg.bus.clear();
    rg.r(0x22) = 0x77; rg.r(0x26) = 0x12; rg.r(0x27) = 0x34;
    EXPECT_EQ(rg.eins({0x92, 0x26}), 12);              // LDE @rr6,r2
    EXPECT_EQ(rg.extDaten[0x1234], 0x77);
    ASSERT_EQ(rg.bus.size(), 1u);
    EXPECT_TRUE(rg.bus[0].c.dm);
    EXPECT_EQ(rg.bus[0].c.art, Z8Zugriff::Extern);
    rg.r(0x52) = 0x11; rg.r(0x22) = 0x52;
    EXPECT_EQ(rg.eins({0x93, 0x26}), 18);              // LDEI @rr6,@r2
    EXPECT_EQ(rg.extDaten[0x1234], 0x11);
    EXPECT_EQ(rg.r(0x22), 0x53); EXPECT_EQ(rg.r(0x27), 0x35);
    // LDCI @rr6,@r2 (extern)
    rg.r(0x26) = 0x90; rg.r(0x27) = 0x00; rg.r(0x22) = 0x52;
    EXPECT_EQ(rg.eins({0xD3, 0x26}), 18);
    EXPECT_EQ(rg.extProg[0x9000], 0x11);
    // LDCI @r2,@rr6
    rg.extProg[0x9001] = 0x99; rg.r(0x22) = 0x60;
    EXPECT_EQ(rg.eins({0xC3, 0x26}), 18);
    EXPECT_EQ(rg.r(0x60), 0x99);
    EXPECT_EQ(rg.r(0x22), 0x61);
}

TEST(Z8Laden, AdressnibblesVonPort0OhneAdressbetrieb) {
    // P01M = 4DH (Reset): P0 Eingang ⇒ A8–A15 = Pegel an P0 (UM0016 S. 132: 8/12/16 Bit)
    Rig rg; rg.boot();
    rg.cpu.setPort(0, 0x5A);
    rg.cpu.rp = 0x20; rg.r(0x26) = 0x12; rg.r(0x27) = 0x34; rg.bus.clear();
    rg.eins({0x82, 0x26});
    ASSERT_EQ(rg.bus.size(), 1u);
    EXPECT_EQ(rg.bus[0].c.adresse, 0x5A34);
    rg.eins({0xE6, 0xF8, 0x4E});                        // P0L = A8–A11, P0H Eingang
    rg.bus.clear(); rg.eins({0x82, 0x26});
    EXPECT_EQ(rg.bus[0].c.adresse, 0x5234);
    rg.eins({0xE6, 0xF8, 0x96});                        // ganz Adresse (Terminal)
    rg.bus.clear(); rg.eins({0x82, 0x26});
    EXPECT_EQ(rg.bus[0].c.adresse, 0x1234);
    EXPECT_EQ(rg.cpu.portPegel(0), 0x12);
}

// ═════════════════════════════════════════════════════════════════════════════
// Stapel, Aufrufe, Sprünge
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Stapel, InternPushPopAufSplSphUnberuehrt) {
    Rig rg; rg.boot();
    rg.cpu.sp = 0x5580;                                 // SPH = 55H bleibt
    rg.r(0x40) = 0xAB; rg.r(0x41) = 0x40;
    EXPECT_EQ(rg.eins({0x70, 0x40}), 10);               // PUSH 40H
    EXPECT_EQ(rg.cpu.sp, 0x557F);
    EXPECT_EQ(rg.r(0x7F), 0xAB);
    EXPECT_EQ(rg.eins({0x71, 0x41}), 12);               // PUSH @41H (→ 40H)
    EXPECT_EQ(rg.r(0x7E), 0xAB);
    EXPECT_EQ(rg.eins({0x50, 0x43}), 10);               // POP 43H
    EXPECT_EQ(rg.r(0x43), 0xAB);
    rg.r(0x40) = 0;
    EXPECT_EQ(rg.eins({0x51, 0x41}), 10);               // POP @41H → 40H
    EXPECT_EQ(rg.r(0x40), 0xAB);
    EXPECT_EQ(rg.cpu.sp, 0x5580);
    EXPECT_TRUE(rg.bus.empty());
}

TEST(Z8Stapel, ExternPushPopCallRetMitDatenspeicher) {
    Rig rg; rg.boot();
    rg.eins({0xE6, 0xF8, 0x92});                        // P01M: Stapel extern, A8–A15, AD
    rg.eins({0xE6, 0xF7, 0x10});                        // P34 = /DM
    rg.cpu.sp = 0x3002;
    rg.bus.clear();
    // CALL 3521H von 1A47H (UM0016 CALL Beispiel 1)
    rg.extProg[0x1A47] = 0xD6; rg.extProg[0x1A48] = 0x35; rg.extProg[0x1A49] = 0x21;
    rg.cpu.pc = 0x1A47;
    EXPECT_EQ(rg.cpu.step(), 20);
    EXPECT_EQ(rg.cpu.pc, 0x3521);
    EXPECT_EQ(rg.cpu.sp, 0x3000);
    EXPECT_EQ(rg.extDaten[0x3000], 0x1A);
    EXPECT_EQ(rg.extDaten[0x3001], 0x4A);
    // Reihenfolge: Holen ×3, dann PCL nach SP-1, PCH nach SP-2 (Figure 102)
    ASSERT_GE(rg.bus.size(), 5u);
    EXPECT_EQ(rg.bus[3].c.adresse, 0x3001); EXPECT_EQ(rg.bus[3].d, 0x4A);
    EXPECT_EQ(rg.bus[4].c.adresse, 0x3000); EXPECT_EQ(rg.bus[4].d, 0x1A);
    EXPECT_TRUE(rg.bus[4].c.dm);
    EXPECT_EQ(rg.bus[4].c.art, Z8Zugriff::Stapel);
    rg.extProg[0x3521] = 0xAF;                          // RET
    EXPECT_EQ(rg.cpu.step(), 14);
    EXPECT_EQ(rg.cpu.pc, 0x1A4A);
    EXPECT_EQ(rg.cpu.sp, 0x3002);
    // PUSH/POP extern: 2 Takte mehr (Figure 134: 10/12, 12/14)
    rg.r(0x40) = 0x77;
    EXPECT_EQ(rg.eins({0x70, 0x40}), 12);
    EXPECT_EQ(rg.extDaten[0x3001], 0x77);
    rg.r(0x41) = 0x40;
    EXPECT_EQ(rg.eins({0x71, 0x41}), 14);
    EXPECT_EQ(rg.eins({0x50, 0x42}), 10);
    EXPECT_EQ(rg.r(0x42), 0x77);
}

TEST(Z8Stapel, CallIndirektUndIret) {
    Rig rg; rg.boot();
    rg.cpu.sp = 0x0072; rg.cpu.pc = 0x0100;
    rg.r(0x34) = 0x05; rg.r(0x35) = 0x21;
    EXPECT_EQ(rg.eins({0xD4, 0x34}), 20);               // CALL @34H (Paar 34H/35H)
    EXPECT_EQ(rg.cpu.pc, 0x0521);
    EXPECT_EQ(rg.cpu.sp, 0x0070);
    EXPECT_EQ(rg.r(0x70), 0x01); EXPECT_EQ(rg.r(0x71), 0x02);
    // IRET: FLAGS, PC vom Stapel, IMR.7 = 1
    rg.cpu.sp = 0x0060;
    rg.r(0x60) = 0xA5; rg.r(0x61) = 0x12; rg.r(0x62) = 0x34;
    rg.cpu.imr = 0x05;
    EXPECT_EQ(rg.eins({0xBF}), 16);
    EXPECT_EQ(rg.cpu.flags, 0xA5);
    EXPECT_EQ(rg.cpu.pc, 0x1234);
    EXPECT_EQ(rg.cpu.imr, 0x85);
    EXPECT_EQ(rg.cpu.sp, 0x0063);
    // JP @rr4
    rg.cpu.rp = 0x40; rg.r(0x44) = 0x0A; rg.r(0x45) = 0xBC;
    EXPECT_EQ(rg.eins({0x30, 0xE4}), 8);
    EXPECT_EQ(rg.cpu.pc, 0x0ABC);
}

namespace {
bool refCc(unsigned cc, uint8_t f) {   // UM0016 Table 36
    const bool c = f & C, z = f & Z, s = f & S, v = f & V;
    switch (cc) {
    case 0x0: return false;          case 0x8: return true;
    case 0x7: return c;              case 0xF: return !c;
    case 0x6: return z;              case 0xE: return !z;
    case 0xD: return !s;             case 0x5: return s;
    case 0x4: return v;              case 0xC: return !v;
    case 0x9: return (s ^ v) == 0;   case 0x1: return (s ^ v) == 1;
    case 0xA: return (z || (s ^ v)) == 0;
    case 0x2: return (z || (s ^ v)) == 1;
    case 0xB: return !c && !z;       default /*3*/: return c || z;
    }
}
}  // namespace

TEST(Z8Sprung, JrJpAlleBedingungenAlleFlagkombinationen) {
    for (unsigned cc = 0; cc < 16; ++cc)
        for (unsigned fl = 0; fl < 16; ++fl) {
            const uint8_t f = uint8_t((fl & 1 ? C : 0) | (fl & 2 ? Z : 0) | (fl & 4 ? S : 0) | (fl & 8 ? V : 0));
            const bool j = refCc(cc, f);
            {
                Rig rg; rg.boot(); rg.cpu.flags = f;
                const int c = rg.eins({uint8_t(cc << 4 | 0x0B), 0x10}, 0x0100);   // JR cc,+10H
                EXPECT_EQ(rg.cpu.pc, j ? 0x0112 : 0x0102) << "JR cc=" << cc << " f=" << int(f);
                EXPECT_EQ(c, j ? 12 : 10);
                EXPECT_EQ(rg.cpu.flags, f);
            }
            {
                Rig rg; rg.boot(); rg.cpu.flags = f;
                const int c = rg.eins({uint8_t(cc << 4 | 0x0D), 0x0A, 0xBC}, 0x0100);
                EXPECT_EQ(rg.cpu.pc, j ? 0x0ABC : 0x0103) << "JP cc=" << cc;
                EXPECT_EQ(c, j ? 12 : 10);
            }
        }
}

TEST(Z8Sprung, JrRueckwaertsUndDjnz) {
    Rig rg; rg.boot();
    EXPECT_EQ(rg.eins({0x8B, 0xFE}, 0x0200), 12);      // JR $
    EXPECT_EQ(rg.cpu.pc, 0x0200);
    // DJNZ: 12 genommen, 10 sonst; keine Flags
    rg.cpu.rp = 0x10; rg.r(0x16) = 3; rg.cpu.flags = 0x5A;
    EXPECT_EQ(rg.eins({0x6A, 0xFE}, 0x0300), 12); EXPECT_EQ(rg.cpu.pc, 0x0300);
    EXPECT_EQ(rg.cpu.step(), 12);
    EXPECT_EQ(rg.cpu.step(), 10); EXPECT_EQ(rg.cpu.pc, 0x0302);
    EXPECT_EQ(rg.r(0x16), 0);
    EXPECT_EQ(rg.cpu.flags, 0x5A);
    rg.r(0x16) = 0;                                     // 0 → 255 Durchläufe
    int n = 0;
    rg.cpu.pc = 0x0300;
    while (rg.cpu.pc == 0x0300 && n < 1000) { rg.cpu.step(); ++n; }
    EXPECT_EQ(n, 256);
}

// ═════════════════════════════════════════════════════════════════════════════
// CPU-Steuerung, Takte, Register
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Steuer, FlagbefehleSrpDiEi) {
    Rig rg; rg.boot();
    rg.cpu.flags = 0x7F;
    EXPECT_EQ(rg.eins({0xDF}), 6); EXPECT_EQ(rg.cpu.flags, 0xFF);   // SCF
    EXPECT_EQ(rg.eins({0xCF}), 6); EXPECT_EQ(rg.cpu.flags, 0x7F);   // RCF
    EXPECT_EQ(rg.eins({0xEF}), 6); EXPECT_EQ(rg.cpu.flags, 0xFF);   // CCF
    EXPECT_EQ(rg.eins({0xFF}), 6); EXPECT_EQ(rg.cpu.flags, 0xFF);   // NOP
    EXPECT_EQ(rg.eins({0x31, 0x70}), 6); EXPECT_EQ(rg.cpu.rp, 0x70);
    EXPECT_EQ(rg.eins({0x9F}), 6); EXPECT_EQ(rg.cpu.imr & 0x80, 0x80);
    EXPECT_TRUE(rg.cpu.irqFreigegeben());
    EXPECT_EQ(rg.eins({0x8F}), 6); EXPECT_EQ(rg.cpu.imr & 0x80, 0);
    // SRP F0: Arbeitsregister = Steuerregister (Tastatur K7673)
    rg.eins({0x31, 0xF0});
    rg.eins({0xBC, 0x2A});                               // LD r11,#2AH → IMR
    EXPECT_EQ(rg.cpu.imr, 0x2A);
}

TEST(Z8Steuer, UnbelegteOpcodesWieNopMitMeldung) {
    Rig rg; rg.boot();
    int gemeldet = 0;
    rg.cpu.onIllegal = [&](uint16_t, uint8_t) { ++gemeldet; };
    for (int op = 0; op < 256; ++op) {
        if (z8::tabelle()[uint8_t(op)].mn != z8::Mn::Ungueltig &&
            z8::tabelle()[uint8_t(op)].mn != z8::Mn::HALT && z8::tabelle()[uint8_t(op)].mn != z8::Mn::STOP) continue;
        EXPECT_EQ(rg.eins({uint8_t(op)}, 0x0400), 6) << std::hex << op;
        EXPECT_EQ(rg.cpu.pc, 0x0401);
    }
    EXPECT_EQ(gemeldet, 256 - 231);
    EXPECT_EQ(rg.cpu.illegale(), uint64_t(256 - 231));
}

TEST(Z8Steuer, HaltStopNurMitZ86Option) {
    Z8Config c; c.haltStop = true;
    Rig rg(c); rg.boot();
    rg.prog[0x0006] = 0x05; rg.prog[0x0007] = 0x00;            // Vektor IRQ3 → 0500H
    rg.prog[0x0500] = 0xBF;                                    // IRET
    rg.cpu.sp = 0x70;
    rg.eins({0x9F}, 0x0600);                                   // EI
    rg.cpu.imr = 0x88;
    rg.eins({0x7F});                                           // HALT
    EXPECT_TRUE(rg.cpu.angehalten());
    const uint16_t pc = rg.cpu.pc;
    for (int i = 0; i < 10; ++i) rg.cpu.step();
    EXPECT_EQ(rg.cpu.pc, pc);
    rg.cpu.setPin(3, 0, false);                                // P30 ↓ → IRQ3 weckt
    EXPECT_EQ(rg.cpu.step(), 24);
    EXPECT_EQ(rg.cpu.pc, 0x0500);
    EXPECT_FALSE(rg.cpu.angehalten());
    rg.cpu.step();
    EXPECT_EQ(rg.cpu.pc, pc);                                  // weiter hinter HALT
    // Ohne Option: 7F ist unbelegt
    Rig r2; r2.boot();
    r2.eins({0x7F});
    EXPECT_FALSE(r2.cpu.angehalten());
    EXPECT_EQ(r2.cpu.illegale(), 1u);
}

TEST(Z8Steuer, TakteAllerOpcodesWieOpcodeKarte) {
    // Figure 134, unabhängig von der Tabelle des Kerns hier abgeschrieben (nicht genommen).
    static const int karte[16][16] = {
        //  0   1   2   3   4   5   6   7   8  9  A   B   C  D   E  F
        {   6,  6,  6,  6, 10, 10, 10, 10,  6, 6,10, 10,  6,10,  6, 0},  // 0
        {   6,  6,  6,  6, 10, 10, 10, 10,  6, 6,10, 10,  6,10,  6, 0},  // 1
        {   6,  6,  6,  6, 10, 10, 10, 10,  6, 6,10, 10,  6,10,  6, 0},  // 2
        {   8,  6,  6,  6, 10, 10, 10, 10,  6, 6,10, 10,  6,10,  6, 0},  // 3
        {   8,  8,  6,  6, 10, 10, 10, 10,  6, 6,10, 10,  6,10,  6, 0},  // 4
        {  10, 10,  6,  6, 10, 10, 10, 10,  6, 6,10, 10,  6,10,  6, 0},  // 5
        {   6,  6,  6,  6, 10, 10, 10, 10,  6, 6,10, 10,  6,10,  6, 0},  // 6
        {  10, 12,  6,  6, 10, 10, 10, 10,  6, 6,10, 10,  6,10,  6, 0},  // 7
        {  10, 10, 12, 18,  0,  0,  0,  0,  6, 6,10, 10,  6,10,  6, 6},  // 8
        {   6,  6, 12, 18,  0,  0,  0,  0,  6, 6,10, 10,  6,10,  6, 6},  // 9
        {  10, 10,  6,  6, 10, 10, 10, 10,  6, 6,10, 10,  6,10,  6,14},  // A
        {   6,  6,  6,  6, 10, 10, 10, 10,  6, 6,10, 10,  6,10,  6,16},  // B
        {   6,  6, 12, 18,  0,  0,  0, 10,  6, 6,10, 10,  6,10,  6, 6},  // C
        {   6,  6, 12, 18, 20,  0, 20, 10,  6, 6,10, 10,  6,10,  6, 6},  // D
        {   6,  6,  0,  6, 10, 10, 10, 10,  6, 6,10, 10,  6,10,  6, 6},  // E
        {   8,  8,  0,  6,  0, 10,  0,  0,  6, 6,10, 10,  6,10,  6, 6},  // F
    };
    for (int op = 0; op < 256; ++op) {
        const int k = karte[op >> 4][op & 15];
        const z8::Insn& in = z8::tabelle()[uint8_t(op)];
        if (k == 0) { EXPECT_TRUE(in.mn == z8::Mn::Ungueltig || in.mn == z8::Mn::HALT || in.mn == z8::Mn::STOP) << std::hex << op; continue; }
        EXPECT_EQ(in.takte, k) << std::hex << op;
        // ausgeführt (DJNZ mit Zähler 1 → nicht genommen; JR/JP mit cc F/sonst Flags 0)
        Rig rg; rg.boot();
        rg.cpu.rp = 0x40; rg.cpu.sp = 0x0070;
        for (int i = 0x40; i < 0x50; ++i) rg.r(uint8_t(i)) = 0x01;
        rg.cpu.flags = 0;
        const bool cc = (op & 15) == 0xB || (op & 15) == 0xD;
        const bool genommen = cc && ((op >> 4) == 8 || refCc(unsigned(op >> 4), 0));
        const int c = rg.eins({uint8_t(op), 0x45, 0x46});
        EXPECT_EQ(c, genommen ? 12 : k) << std::hex << op;
    }
}

TEST(Z8Register, FlagsAlsZielDasErgebnisGewinnt) {
    // [Z8-F3] Ziel FLAGS eines flagsetzenden Befehls: geschriebenes Ergebnis gilt.
    Rig rg; rg.boot();
    rg.cpu.flags = 0x00;
    rg.eins({0x06, 0xFC, 0x10});          // ADD FLAGS,#10H
    EXPECT_EQ(rg.cpu.flags, 0x10);
    rg.eins({0x46, 0xFC, 0x80});          // OR FLAGS,#80H
    EXPECT_EQ(rg.cpu.flags, 0x90);
}

TEST(Z8Register, RegisterpaarUngeradeNimmtNundNplus1) {
    // [Z8-A2] ungerade Paaradresse: n und n+1 (UM0016 verlangt gerade Adressen)
    Rig rg; rg.boot();
    rg.r(0x41) = 0x12; rg.r(0x42) = 0xFF;
    rg.eins({0xA0, 0x41});                // INCW 41H
    EXPECT_EQ(rg.r(0x41), 0x13);
    EXPECT_EQ(rg.r(0x42), 0x00);
}

TEST(Z8Register, IndirekterInhaltExIstKeinArbeitsregister) {
    // [Z8-A1] Der Inhalt eines Zeigerregisters ist eine volle Registeradresse.
    Rig rg; rg.boot();
    rg.cpu.rp = 0x40; rg.r(0x45) = 0x33;
    rg.r(0x50) = 0xE5;                    // zeigt auf „E5" = nicht vorhanden
    rg.eins({0xE5, 0x50, 0x60});          // LD 60H,@50H
    EXPECT_EQ(rg.r(0x60), 0xFF);
}
