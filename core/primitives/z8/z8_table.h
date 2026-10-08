/**
 * @file z8_table.h
 * @brief Befehlstabelle des Z8 (Zilog Z8601/Z8611/Z8612, UB 8820/8821/8840/8841 M) —
 *        EINE Quelle für Kern (core/primitives/z8.cpp), Disassembler und Assembler (tools/z8/).
 *
 * Grundlage: Zilog UM0016 „Z8 CPU User Manual", Figure 134 (Op Code Map) und Table 39/40.
 * Jede Zeile: Operation, Operandenformat, Länge, Ausführungstakte (interner Takt = XTAL/2),
 * bei Sprüngen die Takte „genommen", dazu die Pipelinetakte aus der Karte (nur Doku).
 *
 * Operandenformate (Byte 1/2 nach dem Opcode, Schreibweise des Handbuchs):
 *  - r      4-Bit-Arbeitsregister (RP.7–4 | n),  R  8-Bit-Registeradresse (Ex = Arbeitsregister x)
 *  - Ir/IR  indirekt (Inhalt = Registeradresse),  RR/IRR  Registerpaar,  Irr  indirektes Arbeitspaar
 *  - X      indiziert: Basis (Byte) + Inhalt eines Arbeitsregisters
 *  - RA     relativ (−128…+127 ab Folgebefehl),  DA  16-Bit-Adresse,  IM  Direktwert
 *
 * @license MIT
 */
#pragma once
#include <cstdint>

namespace z8 {

enum class Mn : uint8_t {
    Ungueltig,
    DEC, RLC, INC, JP, SRP, DA, POP, COM, PUSH, DECW, RL, INCW, CLR, RRC, SRA, RR, SWAP,
    ADD, ADC, SUB, SBC, OR, AND, TCM, TM, CP, XOR,
    LD, DJNZ, JR, LDE, LDEI, LDC, LDCI, CALL,
    DI, EI, RET, IRET, RCF, SCF, CCF, NOP, HALT, STOP,
};

inline const char* mnName(Mn m) {
    static const char* const k[] = {
        "???",
        "DEC", "RLC", "INC", "JP", "SRP", "DA", "POP", "COM", "PUSH", "DECW", "RL", "INCW", "CLR",
        "RRC", "SRA", "RR", "SWAP",
        "ADD", "ADC", "SUB", "SBC", "OR", "AND", "TCM", "TM", "CP", "XOR",
        "LD", "DJNZ", "JR", "LDE", "LDEI", "LDC", "LDCI", "CALL",
        "DI", "EI", "RET", "IRET", "RCF", "SCF", "CCF", "NOP", "HALT", "STOP",
    };
    return k[static_cast<uint8_t>(m)];
}

/// Operandenformat.  Kommentar = Bytefolge nach dem Opcode → Assemblerschreibweise.
enum class Fmt : uint8_t {
    Keins,      ///< —                         → MN
    R1,         ///< dst                       → MN R
    IR1,        ///< dst                       → MN @R
    RR1,        ///< dst (Paar)                → MN RR
    IRR1,       ///< dst (Paar)                → JP/CALL @RR
    r1_r2,      ///< dst<<4|src                → MN r1,r2
    r1_Ir2,     ///< dst<<4|src                → MN r1,@r2
    R2_R1,      ///< src, dst                  → MN R1,R2
    IR2_R1,     ///< src, dst                  → MN R1,@R2
    R1_IM,      ///< dst, im                   → MN R1,#im
    IR1_IM,     ///< dst, im                   → MN @R1,#im
    r1_R2,      ///< src  (r1 = Opcode.7–4)    → LD r1,R2
    r2_R1,      ///< dst  (r2 = Opcode.7–4)    → LD R1,r2
    r1_RA,      ///< ra   (r1 = Opcode.7–4)    → DJNZ r1,ziel
    cc_RA,      ///< ra   (cc = Opcode.7–4)    → JR cc,ziel
    r1_IM,      ///< im   (r1 = Opcode.7–4)    → LD r1,#im
    cc_DA,      ///< hi, lo (cc = Opcode.7–4)  → JP cc,ziel
    r1,         ///< —    (r1 = Opcode.7–4)    → INC r1
    IM,         ///< im                        → SRP #im
    DA,         ///< hi, lo                    → CALL ziel
    r1_Irr2,    ///< r<<4|rr                   → LDC/LDE r,@rr
    Irr2_r1,    ///< r<<4|rr                   → LDC/LDE @rr,r
    Ir1_Irr2,   ///< r<<4|rr                   → LDCI/LDEI @r,@rr
    Irr2_Ir1,   ///< r<<4|rr                   → LDCI/LDEI @rr,@r
    r1_X,       ///< r1<<4|r2, x               → LD r1,x(r2)
    X_r1,       ///< r1<<4|r2, x               → LD x(r2),r1
    Ir1_r2,     ///< dst<<4|src                → LD @r1,r2
    IR1_R2,     ///< src, dst                  → LD @R1,R2
};

struct Insn {
    Mn      mn = Mn::Ungueltig;
    Fmt     fmt = Fmt::Keins;
    uint8_t len = 1;
    uint8_t takte = 6;        ///< Ausführungstakte (bei Sprüngen: nicht genommen)
    uint8_t takteSprung = 0;  ///< Takte, wenn der Sprung genommen wird (0 = kein bedingter Sprung)
    uint8_t pipeline = 0;     ///< Pipelinetakte laut Opcode-Karte (Dokumentation)
};

constexpr uint8_t fmtLen(Fmt f) {
    switch (f) {
    case Fmt::Keins: case Fmt::r1: return 1;
    case Fmt::R2_R1: case Fmt::IR2_R1: case Fmt::R1_IM: case Fmt::IR1_IM:
    case Fmt::cc_DA: case Fmt::DA: case Fmt::r1_X: case Fmt::X_r1: case Fmt::IR1_R2: return 3;
    default: return 2;
    }
}

constexpr Insn mk(Mn m, Fmt f, uint8_t t, uint8_t p, uint8_t tj = 0) {
    Insn i{};
    i.mn = m; i.fmt = f; i.len = fmtLen(f); i.takte = t; i.takteSprung = tj; i.pipeline = p;
    return i;
}

/// Dekodierung eines Opcodes nach der Opcode-Karte (Figure 134).
constexpr Insn decode(uint8_t op) {
    const unsigned hi = op >> 4, lo = op & 15;
    // Spalten 8–E: Zeilenunabhängig
    switch (lo) {
    case 0x8: return mk(Mn::LD, Fmt::r1_R2, 6, 5);
    case 0x9: return mk(Mn::LD, Fmt::r2_R1, 6, 5);
    case 0xA: return mk(Mn::DJNZ, Fmt::r1_RA, 10, 5, 12);
    case 0xB: return mk(Mn::JR, Fmt::cc_RA, 10, 0, 12);
    case 0xC: return mk(Mn::LD, Fmt::r1_IM, 6, 5);
    case 0xD: return mk(Mn::JP, Fmt::cc_DA, 10, 0, 12);
    case 0xE: return mk(Mn::INC, Fmt::r1, 6, 5);
    case 0xF:
        switch (hi) {
        case 0x6: return mk(Mn::STOP, Fmt::Keins, 6, 0);   // nur Z86 (Config::haltStop)
        case 0x7: return mk(Mn::HALT, Fmt::Keins, 6, 0);   // nur Z86 (Config::haltStop)
        case 0x8: return mk(Mn::DI, Fmt::Keins, 6, 1);
        case 0x9: return mk(Mn::EI, Fmt::Keins, 6, 1);
        case 0xA: return mk(Mn::RET, Fmt::Keins, 14, 0);
        case 0xB: return mk(Mn::IRET, Fmt::Keins, 16, 0);
        case 0xC: return mk(Mn::RCF, Fmt::Keins, 6, 5);
        case 0xD: return mk(Mn::SCF, Fmt::Keins, 6, 5);
        case 0xE: return mk(Mn::CCF, Fmt::Keins, 6, 5);
        case 0xF: return mk(Mn::NOP, Fmt::Keins, 6, 0);
        default: return Insn{};
        }
    default: break;
    }
    // Spalten 0/1: Einoperandenbefehle
    if (lo <= 1) {
        const bool ind = lo == 1;
        switch (hi) {
        case 0x0: return mk(Mn::DEC, ind ? Fmt::IR1 : Fmt::R1, 6, 5);
        case 0x1: return mk(Mn::RLC, ind ? Fmt::IR1 : Fmt::R1, 6, 5);
        case 0x2: return mk(Mn::INC, ind ? Fmt::IR1 : Fmt::R1, 6, 5);
        case 0x3: return ind ? mk(Mn::SRP, Fmt::IM, 6, 1) : mk(Mn::JP, Fmt::IRR1, 8, 0);
        case 0x4: return mk(Mn::DA, ind ? Fmt::IR1 : Fmt::R1, 8, 5);
        case 0x5: return mk(Mn::POP, ind ? Fmt::IR1 : Fmt::R1, 10, 5);
        case 0x6: return mk(Mn::COM, ind ? Fmt::IR1 : Fmt::R1, 6, 5);
        case 0x7: return mk(Mn::PUSH, ind ? Fmt::IR1 : Fmt::R1, ind ? 12 : 10, 1);
        case 0x8: return mk(Mn::DECW, ind ? Fmt::IR1 : Fmt::RR1, 10, 5);
        case 0x9: return mk(Mn::RL, ind ? Fmt::IR1 : Fmt::R1, 6, 5);
        case 0xA: return mk(Mn::INCW, ind ? Fmt::IR1 : Fmt::RR1, 10, 5);
        case 0xB: return mk(Mn::CLR, ind ? Fmt::IR1 : Fmt::R1, 6, 5);
        case 0xC: return mk(Mn::RRC, ind ? Fmt::IR1 : Fmt::R1, 6, 5);
        case 0xD: return mk(Mn::SRA, ind ? Fmt::IR1 : Fmt::R1, 6, 5);
        case 0xE: return mk(Mn::RR, ind ? Fmt::IR1 : Fmt::R1, 6, 5);
        case 0xF: return mk(Mn::SWAP, ind ? Fmt::IR1 : Fmt::R1, 8, 5);
        }
    }
    // Spalten 2–7: Zweioperandenbefehle der arithmetisch-logischen Zeilen
    Mn alu = Mn::Ungueltig;
    switch (hi) {
    case 0x0: alu = Mn::ADD; break;  case 0x1: alu = Mn::ADC; break;
    case 0x2: alu = Mn::SUB; break;  case 0x3: alu = Mn::SBC; break;
    case 0x4: alu = Mn::OR; break;   case 0x5: alu = Mn::AND; break;
    case 0x6: alu = Mn::TCM; break;  case 0x7: alu = Mn::TM; break;
    case 0xA: alu = Mn::CP; break;   case 0xB: alu = Mn::XOR; break;
    default: break;
    }
    if (alu != Mn::Ungueltig) {
        switch (lo) {
        case 2: return mk(alu, Fmt::r1_r2, 6, 5);
        case 3: return mk(alu, Fmt::r1_Ir2, 6, 5);
        case 4: return mk(alu, Fmt::R2_R1, 10, 5);
        case 5: return mk(alu, Fmt::IR2_R1, 10, 5);
        case 6: return mk(alu, Fmt::R1_IM, 10, 5);
        case 7: return mk(alu, Fmt::IR1_IM, 10, 5);
        }
    }
    switch (op) {
    case 0x82: return mk(Mn::LDE, Fmt::r1_Irr2, 12, 0);
    case 0x83: return mk(Mn::LDEI, Fmt::Ir1_Irr2, 18, 0);
    case 0x92: return mk(Mn::LDE, Fmt::Irr2_r1, 12, 0);
    case 0x93: return mk(Mn::LDEI, Fmt::Irr2_Ir1, 18, 0);
    case 0xC2: return mk(Mn::LDC, Fmt::r1_Irr2, 12, 0);
    case 0xC3: return mk(Mn::LDCI, Fmt::Ir1_Irr2, 18, 0);
    case 0xD2: return mk(Mn::LDC, Fmt::Irr2_r1, 12, 0);
    case 0xD3: return mk(Mn::LDCI, Fmt::Irr2_Ir1, 18, 0);
    case 0xC7: return mk(Mn::LD, Fmt::r1_X, 10, 5);
    case 0xD7: return mk(Mn::LD, Fmt::X_r1, 10, 5);
    case 0xD4: return mk(Mn::CALL, Fmt::IRR1, 20, 0);
    case 0xD6: return mk(Mn::CALL, Fmt::DA, 20, 0);
    case 0xE3: return mk(Mn::LD, Fmt::r1_Ir2, 6, 5);
    case 0xF3: return mk(Mn::LD, Fmt::Ir1_r2, 6, 5);
    case 0xE4: return mk(Mn::LD, Fmt::R2_R1, 10, 5);
    case 0xE5: return mk(Mn::LD, Fmt::IR2_R1, 10, 5);
    case 0xE6: return mk(Mn::LD, Fmt::R1_IM, 10, 5);
    case 0xE7: return mk(Mn::LD, Fmt::IR1_IM, 10, 5);
    case 0xF5: return mk(Mn::LD, Fmt::IR1_R2, 10, 5);
    default: return Insn{};
    }
}

/// Die 256 Zeilen als Tabelle (zur Laufzeit gebaut, einmal).
struct Tabelle {
    Insn z[256];
    Tabelle() { for (int i = 0; i < 256; ++i) z[i] = decode(static_cast<uint8_t>(i)); }
    const Insn& operator[](uint8_t op) const { return z[op]; }
};
inline const Tabelle& tabelle() { static const Tabelle t; return t; }

/// Bedingungscodes (Table 36); 8 = immer (leer geschrieben).
inline const char* ccName(unsigned cc) {
    static const char* const k[16] = {"F", "LT", "LE", "ULE", "OV", "MI", "Z", "C",
                                      "", "GE", "GT", "UGT", "NOV", "PL", "NZ", "NC"};
    return k[cc & 15];
}

/// Namen der Steuer- und Portregister (Disassembler/Assembler/Debugger).
inline const char* sfrName(uint8_t r) {
    switch (r) {
    case 0x00: return "P0";   case 0x01: return "P1";   case 0x02: return "P2";   case 0x03: return "P3";
    case 0xF0: return "SIO";  case 0xF1: return "TMR";  case 0xF2: return "T1";   case 0xF3: return "PRE1";
    case 0xF4: return "T0";   case 0xF5: return "PRE0"; case 0xF6: return "P2M";  case 0xF7: return "P3M";
    case 0xF8: return "P01M"; case 0xF9: return "IPR";  case 0xFA: return "IRQ";  case 0xFB: return "IMR";
    case 0xFC: return "FLAGS"; case 0xFD: return "RP";  case 0xFE: return "SPH";  case 0xFF: return "SPL";
    default: return nullptr;
    }
}

}  // namespace z8
