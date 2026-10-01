/**
 * @file z8k_table.h
 * @brief DIE Befehlstabelle des U8001/U8002 (≙ Zilog Z8001/Z8002), header-only.
 *
 * Eine Zeile je Kodierung (Mnemonik × Adressierungsart × Datenbreite).  Aus
 * derselben Tabelle arbeiten Dekoder/Kodierer (z8k_codec.h), Disassembler
 * (z8k_disasm.h), Assembler (z8k_asm.h) — und der CPU-Kern (S3), der die
 * Takte übernimmt.  Quelle: Zilog Z8000 CPU User's Reference Manual (Kap. 6,
 * Anhang C „Clock Cycles", Opcode-Map); Abweichungen und Unsicherheiten:
 * tools/z8000/README.md (Befunde) und
 * core/primitives/z8000/README.md (CPU-Kern).
 *
 * Aufbau einer Zeile (RowSrc):
 *   mn     Mnemonik in Zilog-Schreibweise
 *   w0/w1  Bitmuster des ersten/zweiten Befehlsworts, Bit 15 links, Leerzeichen
 *          sind Zierde.  '0'/'1' = fest, Kleinbuchstabe = Feld (zusammenhängend;
 *          ein Buchstabe gehört genau einem Wort).  w1 = "" → kein festes
 *          zweites Wort.
 *   ops    Operanden in Syntax-Reihenfolge, Komma-getrennt:  ART[:feld[:feld2]]
 *          (Arten siehe enum Kind).  Operanden mit Erweiterungsworten (DA, X,
 *          BA, RA16, IMB/IMW/IML, PORT) hängen ihre Worte in dieser Reihenfolge
 *          hinter w0 [w1] an.
 *   ns/ss/sl     Takte nichtsegmentiert / segmentiert mit kurzem bzw. langem
 *          Offset (bei Zeilen ohne DA/X gilt ss == sl).
 *   ans/ass/asl  Takte, wenn die Bedingung NICHT erfüllt ist (JP/JR/RET/DJNZ);
 *          0 = keine Unterscheidung.
 *   perN   Zusatztakte je Einheit n.  Was n ist, sagt der Befehl: Wieder-
 *          holungsbefehle je Durchlauf (11 + 9n usw.), statisches/dynamisches
 *          Schieben je Stelle, LDM je Register, MULTL je 1-Bit im Betrag der
 *          unteren 16 Bit des Multiplikanden, HALT und MREQ je Warteschleife.
 *   flags  Z8K_* (Datenbreite, privilegiert, Sprungart, …).
 *
 * @license MIT
 */
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace z8k {

// ── Zeilenmerkmale ───────────────────────────────────────────────────────────
enum : uint32_t {
    Z8K_B      = 1u << 0,   ///< Byte-Operation (Speicheroperand 8 Bit)
    Z8K_L      = 1u << 1,   ///< Langwort (32 Bit)
    Z8K_Q      = 1u << 2,   ///< Quadwort-Ziel (EXTSL)
    Z8K_PRIV   = 1u << 3,   ///< privilegiert: im Normal-Modus Privileged-Instruction-Trap
    Z8K_ALT    = 1u << 4,   ///< nicht-kanonische Zweitkodierung (Assembler: Suffix ".L")
    Z8K_JUMP   = 1u << 5,   ///< JP/JR/DJNZ/DBJNZ
    Z8K_CALL   = 1u << 6,   ///< CALL/CALR/SC (Debugger: step-over)
    Z8K_RET    = 1u << 7,   ///< RET/IRET
    Z8K_REPEAT = 1u << 8,   ///< sich wiederholender Blockbefehl (unterbrechbar)
    Z8K_EPA    = 1u << 9,   ///< erweiterter Befehl (EPU); ohne EPU: Extended Instruction Trap
};

/// Quellform einer Tabellenzeile — so, wie sie im Handbuch steht.
struct RowSrc {
    const char* mn;
    const char* w0;
    const char* w1;
    const char* ops;
    uint16_t ns, ss, sl;
    uint16_t ans, ass, asl;
    uint8_t  perN;
    uint32_t flags;
};

// ── DIE Tabelle ──────────────────────────────────────────────────────────────
// Operandenarten (Kurzform in `ops`, Einzelheiten bei enum Kind):
//   RB RW RL RQ  Byte-/Wort-/Doppel-/Quadregister     RP  Rn (nonseg) / RRn (seg)
//   IR  @Rn / @RRn (≠0)   IO  @Rn als E/A-Port   DA  Adresse   X  Adresse(Rn≠0)
//   BA  Rn(#disp)/RRn(#disp)  BX  Rn(Rm)/RRn(Rm)  RA16/RA8/RA7/RA12 relativ
//   IMB/IMW/IML Direktwert im Erweiterungswort   IM8/IM4 Direktwert im Feld
//   N16  #1..16 (Feld n-1)   BIT  Bitnummer   SHL/SHR Schiebeweite (links/rechts)
//   CC  Bedingung   FL  Flagliste   INT  VI,NVI   CTL  Steuerregister
//   FLAGS  wörtlich   PORT  E/A-Adresse   LDMN  #1..16   RAW  EPU-Feld roh
//   #1/#2  feste Anzahl (Rotieren)
inline constexpr RowSrc kRows[] = {
//   mn        w0                     w1                       ops                     ns  ss  sl  ans ass asl perN flags
    // ---- Arithmetik/Logik mit zwei Operanden: ADD SUB OR AND XOR CP (B/W) und ADDL SUBL CPL
    {"ADDB",  "10 000000 ssss dddd", "",                      "RB:d,RB:s",             4,  4,  4,   0,  0,  0,  0, Z8K_B},
    {"ADDB",  "00 000000 0000 dddd", "",                      "RB:d,IMB",              7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"ADDB",  "00 000000 ssss dddd", "",                      "RB:d,IR:s",             7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"ADDB",  "01 000000 0000 dddd", "",                      "RB:d,DA",               9, 10, 12,   0,  0,  0,  0, Z8K_B},
    {"ADDB",  "01 000000 ssss dddd", "",                      "RB:d,X:s",             10, 10, 13,   0,  0,  0,  0, Z8K_B},
    {"ADD",   "10 000001 ssss dddd", "",                      "RW:d,RW:s",             4,  4,  4,   0,  0,  0,  0, 0},
    {"ADD",   "00 000001 0000 dddd", "",                      "RW:d,IMW",              7,  7,  7,   0,  0,  0,  0, 0},
    {"ADD",   "00 000001 ssss dddd", "",                      "RW:d,IR:s",             7,  7,  7,   0,  0,  0,  0, 0},
    {"ADD",   "01 000001 0000 dddd", "",                      "RW:d,DA",               9, 10, 12,   0,  0,  0,  0, 0},
    {"ADD",   "01 000001 ssss dddd", "",                      "RW:d,X:s",             10, 10, 13,   0,  0,  0,  0, 0},
    {"SUBB",  "10 000010 ssss dddd", "",                      "RB:d,RB:s",             4,  4,  4,   0,  0,  0,  0, Z8K_B},
    {"SUBB",  "00 000010 0000 dddd", "",                      "RB:d,IMB",              7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"SUBB",  "00 000010 ssss dddd", "",                      "RB:d,IR:s",             7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"SUBB",  "01 000010 0000 dddd", "",                      "RB:d,DA",               9, 10, 12,   0,  0,  0,  0, Z8K_B},
    {"SUBB",  "01 000010 ssss dddd", "",                      "RB:d,X:s",             10, 10, 13,   0,  0,  0,  0, Z8K_B},
    {"SUB",   "10 000011 ssss dddd", "",                      "RW:d,RW:s",             4,  4,  4,   0,  0,  0,  0, 0},
    {"SUB",   "00 000011 0000 dddd", "",                      "RW:d,IMW",              7,  7,  7,   0,  0,  0,  0, 0},
    {"SUB",   "00 000011 ssss dddd", "",                      "RW:d,IR:s",             7,  7,  7,   0,  0,  0,  0, 0},
    {"SUB",   "01 000011 0000 dddd", "",                      "RW:d,DA",               9, 10, 12,   0,  0,  0,  0, 0},
    {"SUB",   "01 000011 ssss dddd", "",                      "RW:d,X:s",             10, 10, 13,   0,  0,  0,  0, 0},
    {"ORB",   "10 000100 ssss dddd", "",                      "RB:d,RB:s",             4,  4,  4,   0,  0,  0,  0, Z8K_B},
    {"ORB",   "00 000100 0000 dddd", "",                      "RB:d,IMB",              7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"ORB",   "00 000100 ssss dddd", "",                      "RB:d,IR:s",             7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"ORB",   "01 000100 0000 dddd", "",                      "RB:d,DA",               9, 10, 12,   0,  0,  0,  0, Z8K_B},
    {"ORB",   "01 000100 ssss dddd", "",                      "RB:d,X:s",             10, 10, 13,   0,  0,  0,  0, Z8K_B},
    {"OR",    "10 000101 ssss dddd", "",                      "RW:d,RW:s",             4,  4,  4,   0,  0,  0,  0, 0},
    {"OR",    "00 000101 0000 dddd", "",                      "RW:d,IMW",              7,  7,  7,   0,  0,  0,  0, 0},
    {"OR",    "00 000101 ssss dddd", "",                      "RW:d,IR:s",             7,  7,  7,   0,  0,  0,  0, 0},
    {"OR",    "01 000101 0000 dddd", "",                      "RW:d,DA",               9, 10, 12,   0,  0,  0,  0, 0},
    {"OR",    "01 000101 ssss dddd", "",                      "RW:d,X:s",             10, 10, 13,   0,  0,  0,  0, 0},
    {"ANDB",  "10 000110 ssss dddd", "",                      "RB:d,RB:s",             4,  4,  4,   0,  0,  0,  0, Z8K_B},
    {"ANDB",  "00 000110 0000 dddd", "",                      "RB:d,IMB",              7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"ANDB",  "00 000110 ssss dddd", "",                      "RB:d,IR:s",             7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"ANDB",  "01 000110 0000 dddd", "",                      "RB:d,DA",               9, 10, 12,   0,  0,  0,  0, Z8K_B},
    {"ANDB",  "01 000110 ssss dddd", "",                      "RB:d,X:s",             10, 10, 13,   0,  0,  0,  0, Z8K_B},
    {"AND",   "10 000111 ssss dddd", "",                      "RW:d,RW:s",             4,  4,  4,   0,  0,  0,  0, 0},
    {"AND",   "00 000111 0000 dddd", "",                      "RW:d,IMW",              7,  7,  7,   0,  0,  0,  0, 0},
    {"AND",   "00 000111 ssss dddd", "",                      "RW:d,IR:s",             7,  7,  7,   0,  0,  0,  0, 0},
    {"AND",   "01 000111 0000 dddd", "",                      "RW:d,DA",               9, 10, 12,   0,  0,  0,  0, 0},
    {"AND",   "01 000111 ssss dddd", "",                      "RW:d,X:s",             10, 10, 13,   0,  0,  0,  0, 0},
    {"XORB",  "10 001000 ssss dddd", "",                      "RB:d,RB:s",             4,  4,  4,   0,  0,  0,  0, Z8K_B},
    {"XORB",  "00 001000 0000 dddd", "",                      "RB:d,IMB",              7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"XORB",  "00 001000 ssss dddd", "",                      "RB:d,IR:s",             7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"XORB",  "01 001000 0000 dddd", "",                      "RB:d,DA",               9, 10, 12,   0,  0,  0,  0, Z8K_B},
    {"XORB",  "01 001000 ssss dddd", "",                      "RB:d,X:s",             10, 10, 13,   0,  0,  0,  0, Z8K_B},
    {"XOR",   "10 001001 ssss dddd", "",                      "RW:d,RW:s",             4,  4,  4,   0,  0,  0,  0, 0},
    {"XOR",   "00 001001 0000 dddd", "",                      "RW:d,IMW",              7,  7,  7,   0,  0,  0,  0, 0},
    {"XOR",   "00 001001 ssss dddd", "",                      "RW:d,IR:s",             7,  7,  7,   0,  0,  0,  0, 0},
    {"XOR",   "01 001001 0000 dddd", "",                      "RW:d,DA",               9, 10, 12,   0,  0,  0,  0, 0},
    {"XOR",   "01 001001 ssss dddd", "",                      "RW:d,X:s",             10, 10, 13,   0,  0,  0,  0, 0},
    {"CPB",   "10 001010 ssss dddd", "",                      "RB:d,RB:s",             4,  4,  4,   0,  0,  0,  0, Z8K_B},
    {"CPB",   "00 001010 0000 dddd", "",                      "RB:d,IMB",              7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"CPB",   "00 001010 ssss dddd", "",                      "RB:d,IR:s",             7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"CPB",   "01 001010 0000 dddd", "",                      "RB:d,DA",               9, 10, 12,   0,  0,  0,  0, Z8K_B},
    {"CPB",   "01 001010 ssss dddd", "",                      "RB:d,X:s",             10, 10, 13,   0,  0,  0,  0, Z8K_B},
    {"CP",    "10 001011 ssss dddd", "",                      "RW:d,RW:s",             4,  4,  4,   0,  0,  0,  0, 0},
    {"CP",    "00 001011 0000 dddd", "",                      "RW:d,IMW",              7,  7,  7,   0,  0,  0,  0, 0},
    {"CP",    "00 001011 ssss dddd", "",                      "RW:d,IR:s",             7,  7,  7,   0,  0,  0,  0, 0},
    {"CP",    "01 001011 0000 dddd", "",                      "RW:d,DA",               9, 10, 12,   0,  0,  0,  0, 0},
    {"CP",    "01 001011 ssss dddd", "",                      "RW:d,X:s",             10, 10, 13,   0,  0,  0,  0, 0},
    {"CPL",   "10 010000 ssss dddd", "",                      "RL:d,RL:s",             8,  8,  8,   0,  0,  0,  0, Z8K_L},
    {"CPL",   "00 010000 0000 dddd", "",                      "RL:d,IML",             14, 14, 14,   0,  0,  0,  0, Z8K_L},
    {"CPL",   "00 010000 ssss dddd", "",                      "RL:d,IR:s",            14, 14, 14,   0,  0,  0,  0, Z8K_L},
    {"CPL",   "01 010000 0000 dddd", "",                      "RL:d,DA",              15, 16, 18,   0,  0,  0,  0, Z8K_L},
    {"CPL",   "01 010000 ssss dddd", "",                      "RL:d,X:s",             16, 16, 19,   0,  0,  0,  0, Z8K_L},
    {"SUBL",  "10 010010 ssss dddd", "",                      "RL:d,RL:s",             8,  8,  8,   0,  0,  0,  0, Z8K_L},
    {"SUBL",  "00 010010 0000 dddd", "",                      "RL:d,IML",             14, 14, 14,   0,  0,  0,  0, Z8K_L},
    {"SUBL",  "00 010010 ssss dddd", "",                      "RL:d,IR:s",            14, 14, 14,   0,  0,  0,  0, Z8K_L},
    {"SUBL",  "01 010010 0000 dddd", "",                      "RL:d,DA",              15, 16, 18,   0,  0,  0,  0, Z8K_L},
    {"SUBL",  "01 010010 ssss dddd", "",                      "RL:d,X:s",             16, 16, 19,   0,  0,  0,  0, Z8K_L},
    {"ADDL",  "10 010110 ssss dddd", "",                      "RL:d,RL:s",             8,  8,  8,   0,  0,  0,  0, Z8K_L},
    {"ADDL",  "00 010110 0000 dddd", "",                      "RL:d,IML",             14, 14, 14,   0,  0,  0,  0, Z8K_L},
    {"ADDL",  "00 010110 ssss dddd", "",                      "RL:d,IR:s",            14, 14, 14,   0,  0,  0,  0, Z8K_L},
    {"ADDL",  "01 010110 0000 dddd", "",                      "RL:d,DA",              15, 16, 18,   0,  0,  0,  0, Z8K_L},
    {"ADDL",  "01 010110 ssss dddd", "",                      "RL:d,X:s",             16, 16, 19,   0,  0,  0,  0, Z8K_L},
    // ---- Addition/Subtraktion mit Uebertrag (nur Register)
    {"ADCB",  "10 110100 ssss dddd", "",                      "RB:d,RB:s",             5,  5,  5,   0,  0,  0,  0, Z8K_B},
    {"ADC",   "10 110101 ssss dddd", "",                      "RW:d,RW:s",             5,  5,  5,   0,  0,  0,  0, 0},
    {"SBCB",  "10 110110 ssss dddd", "",                      "RB:d,RB:s",             5,  5,  5,   0,  0,  0,  0, Z8K_B},
    {"SBC",   "10 110111 ssss dddd", "",                      "RW:d,RW:s",             5,  5,  5,   0,  0,  0,  0, 0},
    // ---- Ein-Operanden-Befehle der Gruppe 0C/0D/4C/4D/8C/8D (Unterkode im unteren Halbbyte)
    {"COMB",  "10 001100 dddd 0000", "",                      "RB:d",                  7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"COMB",  "00 001100 dddd 0000", "",                      "IR:d",                 12, 12, 12,   0,  0,  0,  0, Z8K_B},
    {"COMB",  "01 001100 0000 0000", "",                      "DA",                   15, 16, 18,   0,  0,  0,  0, Z8K_B},
    {"COMB",  "01 001100 dddd 0000", "",                      "X:d",                  16, 16, 19,   0,  0,  0,  0, Z8K_B},
    {"COM",   "10 001101 dddd 0000", "",                      "RW:d",                  7,  7,  7,   0,  0,  0,  0, 0},
    {"COM",   "00 001101 dddd 0000", "",                      "IR:d",                 12, 12, 12,   0,  0,  0,  0, 0},
    {"COM",   "01 001101 0000 0000", "",                      "DA",                   15, 16, 18,   0,  0,  0,  0, 0},
    {"COM",   "01 001101 dddd 0000", "",                      "X:d",                  16, 16, 19,   0,  0,  0,  0, 0},
    {"NEGB",  "10 001100 dddd 0010", "",                      "RB:d",                  7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"NEGB",  "00 001100 dddd 0010", "",                      "IR:d",                 12, 12, 12,   0,  0,  0,  0, Z8K_B},
    {"NEGB",  "01 001100 0000 0010", "",                      "DA",                   15, 16, 18,   0,  0,  0,  0, Z8K_B},
    {"NEGB",  "01 001100 dddd 0010", "",                      "X:d",                  16, 16, 19,   0,  0,  0,  0, Z8K_B},
    {"NEG",   "10 001101 dddd 0010", "",                      "RW:d",                  7,  7,  7,   0,  0,  0,  0, 0},
    {"NEG",   "00 001101 dddd 0010", "",                      "IR:d",                 12, 12, 12,   0,  0,  0,  0, 0},
    {"NEG",   "01 001101 0000 0010", "",                      "DA",                   15, 16, 18,   0,  0,  0,  0, 0},
    {"NEG",   "01 001101 dddd 0010", "",                      "X:d",                  16, 16, 19,   0,  0,  0,  0, 0},
    {"TESTB", "10 001100 dddd 0100", "",                      "RB:d",                  7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"TESTB", "00 001100 dddd 0100", "",                      "IR:d",                  8,  8,  8,   0,  0,  0,  0, Z8K_B},
    {"TESTB", "01 001100 0000 0100", "",                      "DA",                   11, 12, 14,   0,  0,  0,  0, Z8K_B},
    {"TESTB", "01 001100 dddd 0100", "",                      "X:d",                  12, 12, 15,   0,  0,  0,  0, Z8K_B},
    {"TEST",  "10 001101 dddd 0100", "",                      "RW:d",                  7,  7,  7,   0,  0,  0,  0, 0},
    {"TEST",  "00 001101 dddd 0100", "",                      "IR:d",                  8,  8,  8,   0,  0,  0,  0, 0},
    {"TEST",  "01 001101 0000 0100", "",                      "DA",                   11, 12, 14,   0,  0,  0,  0, 0},
    {"TEST",  "01 001101 dddd 0100", "",                      "X:d",                  12, 12, 15,   0,  0,  0,  0, 0},
    {"TSETB", "10 001100 dddd 0110", "",                      "RB:d",                  7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"TSETB", "00 001100 dddd 0110", "",                      "IR:d",                 11, 11, 11,   0,  0,  0,  0, Z8K_B},
    {"TSETB", "01 001100 0000 0110", "",                      "DA",                   14, 15, 17,   0,  0,  0,  0, Z8K_B},
    {"TSETB", "01 001100 dddd 0110", "",                      "X:d",                  15, 15, 18,   0,  0,  0,  0, Z8K_B},
    {"TSET",  "10 001101 dddd 0110", "",                      "RW:d",                  7,  7,  7,   0,  0,  0,  0, 0},
    {"TSET",  "00 001101 dddd 0110", "",                      "IR:d",                 11, 11, 11,   0,  0,  0,  0, 0},
    {"TSET",  "01 001101 0000 0110", "",                      "DA",                   14, 15, 17,   0,  0,  0,  0, 0},
    {"TSET",  "01 001101 dddd 0110", "",                      "X:d",                  15, 15, 18,   0,  0,  0,  0, 0},
    {"CLRB",  "10 001100 dddd 1000", "",                      "RB:d",                  7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"CLRB",  "00 001100 dddd 1000", "",                      "IR:d",                  8,  8,  8,   0,  0,  0,  0, Z8K_B},
    {"CLRB",  "01 001100 0000 1000", "",                      "DA",                   11, 12, 14,   0,  0,  0,  0, Z8K_B},
    {"CLRB",  "01 001100 dddd 1000", "",                      "X:d",                  12, 12, 15,   0,  0,  0,  0, Z8K_B},
    {"CLR",   "10 001101 dddd 1000", "",                      "RW:d",                  7,  7,  7,   0,  0,  0,  0, 0},
    {"CLR",   "00 001101 dddd 1000", "",                      "IR:d",                  8,  8,  8,   0,  0,  0,  0, 0},
    {"CLR",   "01 001101 0000 1000", "",                      "DA",                   11, 12, 14,   0,  0,  0,  0, 0},
    {"CLR",   "01 001101 dddd 1000", "",                      "X:d",                  12, 12, 15,   0,  0,  0,  0, 0},
    // ---- Vergleich/Laden Speicher mit Direktwert (Unterkode 1 bzw. 5)
    {"CPB",   "00 001100 dddd 0001", "",                      "IR:d,IMB",             11, 11, 11,   0,  0,  0,  0, Z8K_B},
    {"CPB",   "01 001100 0000 0001", "",                      "DA,IMB",               14, 15, 17,   0,  0,  0,  0, Z8K_B},
    {"CPB",   "01 001100 dddd 0001", "",                      "X:d,IMB",              15, 15, 18,   0,  0,  0,  0, Z8K_B},
    {"CP",    "00 001101 dddd 0001", "",                      "IR:d,IMW",             11, 11, 11,   0,  0,  0,  0, 0},
    {"CP",    "01 001101 0000 0001", "",                      "DA,IMW",               14, 15, 17,   0,  0,  0,  0, 0},
    {"CP",    "01 001101 dddd 0001", "",                      "X:d,IMW",              15, 15, 18,   0,  0,  0,  0, 0},
    {"LDB",   "00 001100 dddd 0101", "",                      "IR:d,IMB",             11, 11, 11,   0,  0,  0,  0, Z8K_B},
    {"LDB",   "01 001100 0000 0101", "",                      "DA,IMB",               14, 15, 17,   0,  0,  0,  0, Z8K_B},
    {"LDB",   "01 001100 dddd 0101", "",                      "X:d,IMB",              15, 15, 18,   0,  0,  0,  0, Z8K_B},
    {"LD",    "00 001101 dddd 0101", "",                      "IR:d,IMW",             11, 11, 11,   0,  0,  0,  0, 0},
    {"LD",    "01 001101 0000 0101", "",                      "DA,IMW",               14, 15, 17,   0,  0,  0,  0, 0},
    {"LD",    "01 001101 dddd 0101", "",                      "X:d,IMW",              15, 15, 18,   0,  0,  0,  0, 0},
    {"PUSH",  "00 001101 dddd 1001", "",                      "IR:d,IMW",             12, 12, 12,   0,  0,  0,  0, },
    // ---- Flag-Byte und Flag-Befehle (8C/8D)
    {"LDCTLB", "10 001100 dddd 0001", "",                      "RB:d,FLAGS",            7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"LDCTLB", "10 001100 ssss 1001", "",                      "FLAGS,RB:s",            7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"SETFLG", "10 001101 ffff 0001", "",                      "FL:f",                  7,  7,  7,   0,  0,  0,  0, },
    {"RESFLG", "10 001101 ffff 0011", "",                      "FL:f",                  7,  7,  7,   0,  0,  0,  0, },
    {"COMFLG", "10 001101 ffff 0101", "",                      "FL:f",                  7,  7,  7,   0,  0,  0,  0, },
    {"NOP",   "10 001101 0000 0111", "",                      "",                      7,  7,  7,   0,  0,  0,  0, },
    // ---- TESTL (1C/5C/9C Unterkode 8)
    {"TESTL", "10 011100 dddd 1000", "",                      "RL:d",                 13, 13, 13,   0,  0,  0,  0, Z8K_L},
    {"TESTL", "00 011100 dddd 1000", "",                      "IR:d",                 13, 13, 13,   0,  0,  0,  0, Z8K_L},
    {"TESTL", "01 011100 0000 1000", "",                      "DA",                   16, 17, 19,   0,  0,  0,  0, Z8K_L},
    {"TESTL", "01 011100 dddd 1000", "",                      "X:d",                  17, 17, 20,   0,  0,  0,  0, Z8K_L},
    // ---- LDM: n = 1..16 aufeinanderfolgende Wortregister (Takte + 3n)
    {"LDM",   "00 011100 ssss 0001", "0000 dddd 0000 nnnn",   "RW:d,IR:s,LDMN:n",     11, 11, 11,   0,  0,  0,  3, },
    {"LDM",   "01 011100 0000 0001", "0000 dddd 0000 nnnn",   "RW:d,DA,LDMN:n",       14, 15, 17,   0,  0,  0,  3, },
    {"LDM",   "01 011100 ssss 0001", "0000 dddd 0000 nnnn",   "RW:d,X:s,LDMN:n",      15, 15, 18,   0,  0,  0,  3, },
    {"LDM",   "00 011100 dddd 1001", "0000 ssss 0000 nnnn",   "IR:d,RW:s,LDMN:n",     11, 11, 11,   0,  0,  0,  3, },
    {"LDM",   "01 011100 0000 1001", "0000 ssss 0000 nnnn",   "DA,RW:s,LDMN:n",       14, 15, 17,   0,  0,  0,  3, },
    {"LDM",   "01 011100 dddd 1001", "0000 ssss 0000 nnnn",   "X:d,RW:s,LDMN:n",      15, 15, 18,   0,  0,  0,  3, },
    // ---- Laden in ein Register: LDB/LD (20/21, 60/61, A0/A1) und LDL (14/54/94)
    {"LDB",   "10 100000 ssss dddd", "",                      "RB:d,RB:s",             3,  3,  3,   0,  0,  0,  0, Z8K_B},
    {"LDB",   "00 100000 0000 dddd", "",                      "RB:d,IMB",              7,  7,  7,   0,  0,  0,  0, Z8K_B|Z8K_ALT},
    {"LDB",   "00 100000 ssss dddd", "",                      "RB:d,IR:s",             7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"LDB",   "01 100000 0000 dddd", "",                      "RB:d,DA",               9, 10, 12,   0,  0,  0,  0, Z8K_B},
    {"LDB",   "01 100000 ssss dddd", "",                      "RB:d,X:s",             10, 10, 13,   0,  0,  0,  0, Z8K_B},
    {"LD",    "10 100001 ssss dddd", "",                      "RW:d,RW:s",             3,  3,  3,   0,  0,  0,  0, 0},
    {"LD",    "00 100001 0000 dddd", "",                      "RW:d,IMW",              7,  7,  7,   0,  0,  0,  0, 0},
    {"LD",    "00 100001 ssss dddd", "",                      "RW:d,IR:s",             7,  7,  7,   0,  0,  0,  0, 0},
    {"LD",    "01 100001 0000 dddd", "",                      "RW:d,DA",               9, 10, 12,   0,  0,  0,  0, 0},
    {"LD",    "01 100001 ssss dddd", "",                      "RW:d,X:s",             10, 10, 13,   0,  0,  0,  0, 0},
    {"LDL",   "10 010100 ssss dddd", "",                      "RL:d,RL:s",             5,  5,  5,   0,  0,  0,  0, Z8K_L},
    {"LDL",   "00 010100 0000 dddd", "",                      "RL:d,IML",             11, 11, 11,   0,  0,  0,  0, Z8K_L},
    {"LDL",   "00 010100 ssss dddd", "",                      "RL:d,IR:s",            11, 11, 11,   0,  0,  0,  0, Z8K_L},
    {"LDL",   "01 010100 0000 dddd", "",                      "RL:d,DA",              12, 13, 15,   0,  0,  0,  0, Z8K_L},
    {"LDL",   "01 010100 ssss dddd", "",                      "RL:d,X:s",             13, 13, 16,   0,  0,  0,  0, Z8K_L},
    {"LDB",   "1100 dddd iiii iiii", "",                      "RB:d,IM8:i",            5,  5,  5,   0,  0,  0,  0, Z8K_B},
    {"LDK",   "10 111101 dddd iiii", "",                      "RW:d,IM4:i",            5,  5,  5,   0,  0,  0,  0, },
    // ---- Speichern: LDB/LD (2E/2F, 6E/6F) und LDL (1D/5D)
    {"LDB",   "00 101110 dddd ssss", "",                      "IR:d,RB:s",             8,  8,  8,   0,  0,  0,  0, Z8K_B},
    {"LDB",   "01 101110 0000 ssss", "",                      "DA,RB:s",              11, 12, 14,   0,  0,  0,  0, Z8K_B},
    {"LDB",   "01 101110 dddd ssss", "",                      "X:d,RB:s",             12, 12, 15,   0,  0,  0,  0, Z8K_B},
    {"LD",    "00 101111 dddd ssss", "",                      "IR:d,RW:s",             8,  8,  8,   0,  0,  0,  0, 0},
    {"LD",    "01 101111 0000 ssss", "",                      "DA,RW:s",              11, 12, 14,   0,  0,  0,  0, 0},
    {"LD",    "01 101111 dddd ssss", "",                      "X:d,RW:s",             12, 12, 15,   0,  0,  0,  0, 0},
    {"LDL",   "00 011101 dddd ssss", "",                      "IR:d,RL:s",            11, 11, 11,   0,  0,  0,  0, Z8K_L},
    {"LDL",   "01 011101 0000 ssss", "",                      "DA,RL:s",              14, 15, 17,   0,  0,  0,  0, Z8K_L},
    {"LDL",   "01 011101 dddd ssss", "",                      "X:d,RL:s",             15, 15, 18,   0,  0,  0,  0, Z8K_L},
    // ---- Basisadressierung (BA, 30..37) und relativ (RA, Feld 0) - Laden und Speichern
    {"LDB",   "00 110000 ssss dddd", "",                      "RB:d,BA:s",            14, 14, 14,   0,  0,  0,  0, Z8K_B},
    {"LDRB",  "00 110000 0000 dddd", "",                      "RB:d,RA16",            14, 14, 14,   0,  0,  0,  0, Z8K_B},
    {"LD",    "00 110001 ssss dddd", "",                      "RW:d,BA:s",            14, 14, 14,   0,  0,  0,  0, 0},
    {"LDR",   "00 110001 0000 dddd", "",                      "RW:d,RA16",            14, 14, 14,   0,  0,  0,  0, 0},
    {"LDL",   "00 110101 ssss dddd", "",                      "RL:d,BA:s",            17, 17, 17,   0,  0,  0,  0, Z8K_L},
    {"LDRL",  "00 110101 0000 dddd", "",                      "RL:d,RA16",            17, 17, 17,   0,  0,  0,  0, Z8K_L},
    {"LDB",   "00 110010 dddd ssss", "",                      "BA:d,RB:s",            14, 14, 14,   0,  0,  0,  0, Z8K_B},
    {"LDRB",  "00 110010 0000 ssss", "",                      "RA16,RB:s",            14, 14, 14,   0,  0,  0,  0, Z8K_B},
    {"LD",    "00 110011 dddd ssss", "",                      "BA:d,RW:s",            14, 14, 14,   0,  0,  0,  0, 0},
    {"LDR",   "00 110011 0000 ssss", "",                      "RA16,RW:s",            14, 14, 14,   0,  0,  0,  0, 0},
    {"LDL",   "00 110111 dddd ssss", "",                      "BA:d,RL:s",            17, 17, 17,   0,  0,  0,  0, Z8K_L},
    {"LDRL",  "00 110111 0000 ssss", "",                      "RA16,RL:s",            17, 17, 17,   0,  0,  0,  0, Z8K_L},
    // ---- Basis-Index-Adressierung (BX, 70..77)
    {"LDB",   "01 110000 ssss dddd", "0000 xxxx 0000 0000",   "RB:d,BX:s:x",          14, 14, 14,   0,  0,  0,  0, Z8K_B},
    {"LD",    "01 110001 ssss dddd", "0000 xxxx 0000 0000",   "RW:d,BX:s:x",          14, 14, 14,   0,  0,  0,  0, 0},
    {"LDL",   "01 110101 ssss dddd", "0000 xxxx 0000 0000",   "RL:d,BX:s:x",          17, 17, 17,   0,  0,  0,  0, Z8K_L},
    {"LDB",   "01 110010 dddd ssss", "0000 xxxx 0000 0000",   "BX:d:x,RB:s",          14, 14, 14,   0,  0,  0,  0, Z8K_B},
    {"LD",    "01 110011 dddd ssss", "0000 xxxx 0000 0000",   "BX:d:x,RW:s",          14, 14, 14,   0,  0,  0,  0, 0},
    {"LDL",   "01 110111 dddd ssss", "0000 xxxx 0000 0000",   "BX:d:x,RL:s",          17, 17, 17,   0,  0,  0,  0, Z8K_L},
    // ---- Adresse laden: LDA (76 DA/X, 34 BA, 74 BX) und LDAR (34 Feld 0); Ziel RP = Rd bzw. RRd
    {"LDA",   "01 110110 0000 dddd", "",                      "RP:d,DA",              12, 13, 15,   0,  0,  0,  0, },
    {"LDA",   "01 110110 ssss dddd", "",                      "RP:d,X:s",             13, 13, 16,   0,  0,  0,  0, },
    {"LDA",   "00 110100 ssss dddd", "",                      "RP:d,BA:s",            15, 15, 15,   0,  0,  0,  0, },
    {"LDA",   "01 110100 ssss dddd", "0000 xxxx 0000 0000",   "RP:d,BX:s:x",          15, 15, 15,   0,  0,  0,  0, },
    {"LDAR",  "00 110100 0000 dddd", "",                      "RP:d,RA16",            15, 15, 15,   0,  0,  0,  0, },
    // ---- Austausch EXB/EX (2C/2D, 6C/6D, AC/AD)
    {"EXB",   "10 101100 ssss dddd", "",                      "RB:d,RB:s",             6,  6,  6,   0,  0,  0,  0, Z8K_B},
    {"EXB",   "00 101100 ssss dddd", "",                      "RB:d,IR:s",            12, 12, 12,   0,  0,  0,  0, Z8K_B},
    {"EXB",   "01 101100 0000 dddd", "",                      "RB:d,DA",              15, 16, 18,   0,  0,  0,  0, Z8K_B},
    {"EXB",   "01 101100 ssss dddd", "",                      "RB:d,X:s",             16, 16, 19,   0,  0,  0,  0, Z8K_B},
    {"EX",    "10 101101 ssss dddd", "",                      "RW:d,RW:s",             6,  6,  6,   0,  0,  0,  0, 0},
    {"EX",    "00 101101 ssss dddd", "",                      "RW:d,IR:s",            12, 12, 12,   0,  0,  0,  0, 0},
    {"EX",    "01 101101 0000 dddd", "",                      "RW:d,DA",              15, 16, 18,   0,  0,  0,  0, 0},
    {"EX",    "01 101101 ssss dddd", "",                      "RW:d,X:s",             16, 16, 19,   0,  0,  0,  0, 0},
    // ---- INC/DEC um n = 1..16 (Feld = n-1)
    {"INCB",  "10 101000 dddd nnnn", "",                      "RB:d,N16:n",            4,  4,  4,   0,  0,  0,  0, Z8K_B},
    {"INCB",  "00 101000 dddd nnnn", "",                      "IR:d,N16:n",           11, 11, 11,   0,  0,  0,  0, Z8K_B},
    {"INCB",  "01 101000 0000 nnnn", "",                      "DA,N16:n",             13, 14, 16,   0,  0,  0,  0, Z8K_B},
    {"INCB",  "01 101000 dddd nnnn", "",                      "X:d,N16:n",            14, 14, 17,   0,  0,  0,  0, Z8K_B},
    {"INC",   "10 101001 dddd nnnn", "",                      "RW:d,N16:n",            4,  4,  4,   0,  0,  0,  0, 0},
    {"INC",   "00 101001 dddd nnnn", "",                      "IR:d,N16:n",           11, 11, 11,   0,  0,  0,  0, 0},
    {"INC",   "01 101001 0000 nnnn", "",                      "DA,N16:n",             13, 14, 16,   0,  0,  0,  0, 0},
    {"INC",   "01 101001 dddd nnnn", "",                      "X:d,N16:n",            14, 14, 17,   0,  0,  0,  0, 0},
    {"DECB",  "10 101010 dddd nnnn", "",                      "RB:d,N16:n",            4,  4,  4,   0,  0,  0,  0, Z8K_B},
    {"DECB",  "00 101010 dddd nnnn", "",                      "IR:d,N16:n",           11, 11, 11,   0,  0,  0,  0, Z8K_B},
    {"DECB",  "01 101010 0000 nnnn", "",                      "DA,N16:n",             13, 14, 16,   0,  0,  0,  0, Z8K_B},
    {"DECB",  "01 101010 dddd nnnn", "",                      "X:d,N16:n",            14, 14, 17,   0,  0,  0,  0, Z8K_B},
    {"DEC",   "10 101011 dddd nnnn", "",                      "RW:d,N16:n",            4,  4,  4,   0,  0,  0,  0, 0},
    {"DEC",   "00 101011 dddd nnnn", "",                      "IR:d,N16:n",           11, 11, 11,   0,  0,  0,  0, 0},
    {"DEC",   "01 101011 0000 nnnn", "",                      "DA,N16:n",             13, 14, 16,   0,  0,  0,  0, 0},
    {"DEC",   "01 101011 dddd nnnn", "",                      "X:d,N16:n",            14, 14, 17,   0,  0,  0,  0, 0},
    // ---- Bitbefehle RES/SET/BIT (22..27, 62..67, A2..A7): statisch (#b) und dynamisch (Rs)
    {"RESB",  "10 100010 dddd bbbb", "",                      "RB:d,BIT:b",            4,  4,  4,   0,  0,  0,  0, Z8K_B},
    {"RESB",  "00 100010 dddd bbbb", "",                      "IR:d,BIT:b",           11, 11, 11,   0,  0,  0,  0, Z8K_B},
    {"RESB",  "01 100010 0000 bbbb", "",                      "DA,BIT:b",             13, 14, 16,   0,  0,  0,  0, Z8K_B},
    {"RESB",  "01 100010 dddd bbbb", "",                      "X:d,BIT:b",            14, 14, 17,   0,  0,  0,  0, Z8K_B},
    {"RESB",  "00 100010 0000 ssss", "0000 dddd 0000 0000",   "RB:d,RW:s",            10, 10, 10,   0,  0,  0,  0, Z8K_B},
    {"RES",   "10 100011 dddd bbbb", "",                      "RW:d,BIT:b",            4,  4,  4,   0,  0,  0,  0, 0},
    {"RES",   "00 100011 dddd bbbb", "",                      "IR:d,BIT:b",           11, 11, 11,   0,  0,  0,  0, 0},
    {"RES",   "01 100011 0000 bbbb", "",                      "DA,BIT:b",             13, 14, 16,   0,  0,  0,  0, 0},
    {"RES",   "01 100011 dddd bbbb", "",                      "X:d,BIT:b",            14, 14, 17,   0,  0,  0,  0, 0},
    {"RES",   "00 100011 0000 ssss", "0000 dddd 0000 0000",   "RW:d,RW:s",            10, 10, 10,   0,  0,  0,  0, 0},
    {"SETB",  "10 100100 dddd bbbb", "",                      "RB:d,BIT:b",            4,  4,  4,   0,  0,  0,  0, Z8K_B},
    {"SETB",  "00 100100 dddd bbbb", "",                      "IR:d,BIT:b",           11, 11, 11,   0,  0,  0,  0, Z8K_B},
    {"SETB",  "01 100100 0000 bbbb", "",                      "DA,BIT:b",             13, 14, 16,   0,  0,  0,  0, Z8K_B},
    {"SETB",  "01 100100 dddd bbbb", "",                      "X:d,BIT:b",            14, 14, 17,   0,  0,  0,  0, Z8K_B},
    {"SETB",  "00 100100 0000 ssss", "0000 dddd 0000 0000",   "RB:d,RW:s",            10, 10, 10,   0,  0,  0,  0, Z8K_B},
    {"SET",   "10 100101 dddd bbbb", "",                      "RW:d,BIT:b",            4,  4,  4,   0,  0,  0,  0, 0},
    {"SET",   "00 100101 dddd bbbb", "",                      "IR:d,BIT:b",           11, 11, 11,   0,  0,  0,  0, 0},
    {"SET",   "01 100101 0000 bbbb", "",                      "DA,BIT:b",             13, 14, 16,   0,  0,  0,  0, 0},
    {"SET",   "01 100101 dddd bbbb", "",                      "X:d,BIT:b",            14, 14, 17,   0,  0,  0,  0, 0},
    {"SET",   "00 100101 0000 ssss", "0000 dddd 0000 0000",   "RW:d,RW:s",            10, 10, 10,   0,  0,  0,  0, 0},
    {"BITB",  "10 100110 dddd bbbb", "",                      "RB:d,BIT:b",            4,  4,  4,   0,  0,  0,  0, Z8K_B},
    {"BITB",  "00 100110 dddd bbbb", "",                      "IR:d,BIT:b",            8,  8,  8,   0,  0,  0,  0, Z8K_B},
    {"BITB",  "01 100110 0000 bbbb", "",                      "DA,BIT:b",             10, 11, 13,   0,  0,  0,  0, Z8K_B},
    {"BITB",  "01 100110 dddd bbbb", "",                      "X:d,BIT:b",            11, 11, 14,   0,  0,  0,  0, Z8K_B},
    {"BITB",  "00 100110 0000 ssss", "0000 dddd 0000 0000",   "RB:d,RW:s",            10, 10, 10,   0,  0,  0,  0, Z8K_B},
    {"BIT",   "10 100111 dddd bbbb", "",                      "RW:d,BIT:b",            4,  4,  4,   0,  0,  0,  0, 0},
    {"BIT",   "00 100111 dddd bbbb", "",                      "IR:d,BIT:b",            8,  8,  8,   0,  0,  0,  0, 0},
    {"BIT",   "01 100111 0000 bbbb", "",                      "DA,BIT:b",             10, 11, 13,   0,  0,  0,  0, 0},
    {"BIT",   "01 100111 dddd bbbb", "",                      "X:d,BIT:b",            11, 11, 14,   0,  0,  0,  0, 0},
    {"BIT",   "00 100111 0000 ssss", "0000 dddd 0000 0000",   "RW:d,RW:s",            10, 10, 10,   0,  0,  0,  0, 0},
    // ---- Rotieren um 1 oder 2 Bit (B2/B3; Bit 1 des Unterkodes = zwei Stellen)
    {"RLB",   "10110010 dddd 0000",  "",                      "RB:d,#1",               6,  6,  6,   0,  0,  0,  0, Z8K_B},
    {"RLB",   "10110010 dddd 0010",  "",                      "RB:d,#2",               7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"RL",    "10110011 dddd 0000",  "",                      "RW:d,#1",               6,  6,  6,   0,  0,  0,  0, 0},
    {"RL",    "10110011 dddd 0010",  "",                      "RW:d,#2",               7,  7,  7,   0,  0,  0,  0, 0},
    {"RLCB",  "10110010 dddd 1000",  "",                      "RB:d,#1",               6,  6,  6,   0,  0,  0,  0, Z8K_B},
    {"RLCB",  "10110010 dddd 1010",  "",                      "RB:d,#2",               7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"RLC",   "10110011 dddd 1000",  "",                      "RW:d,#1",               6,  6,  6,   0,  0,  0,  0, 0},
    {"RLC",   "10110011 dddd 1010",  "",                      "RW:d,#2",               7,  7,  7,   0,  0,  0,  0, 0},
    {"RRB",   "10110010 dddd 0100",  "",                      "RB:d,#1",               6,  6,  6,   0,  0,  0,  0, Z8K_B},
    {"RRB",   "10110010 dddd 0110",  "",                      "RB:d,#2",               7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"RR",    "10110011 dddd 0100",  "",                      "RW:d,#1",               6,  6,  6,   0,  0,  0,  0, 0},
    {"RR",    "10110011 dddd 0110",  "",                      "RW:d,#2",               7,  7,  7,   0,  0,  0,  0, 0},
    {"RRCB",  "10110010 dddd 1100",  "",                      "RB:d,#1",               6,  6,  6,   0,  0,  0,  0, Z8K_B},
    {"RRCB",  "10110010 dddd 1110",  "",                      "RB:d,#2",               7,  7,  7,   0,  0,  0,  0, Z8K_B},
    {"RRC",   "10110011 dddd 1100",  "",                      "RW:d,#1",               6,  6,  6,   0,  0,  0,  0, 0},
    {"RRC",   "10110011 dddd 1110",  "",                      "RW:d,#2",               7,  7,  7,   0,  0,  0,  0, 0},
    // ---- Statisches Schieben (Anzahl im 2. Wort, rechts = negativ; Takte 13 + 3n)
    {"SLLB",  "10110010 dddd 0001",  "0000 0000 0ccc cccc",   "RB:d,SHL:c",           13, 13, 13,   0,  0,  0,  3, Z8K_B},
    {"SRLB",  "10110010 dddd 0001",  "0000 0000 1ccc cccc",   "RB:d,SHR:c",           13, 13, 13,   0,  0,  0,  3, Z8K_B},
    {"SLL",   "10110011 dddd 0001",  "0ccc cccc cccc cccc",   "RW:d,SHL:c",           13, 13, 13,   0,  0,  0,  3, 0},
    {"SRL",   "10110011 dddd 0001",  "1ccc cccc cccc cccc",   "RW:d,SHR:c",           13, 13, 13,   0,  0,  0,  3, 0},
    {"SLLL",  "10110011 dddd 0101",  "0ccc cccc cccc cccc",   "RL:d,SHL:c",           13, 13, 13,   0,  0,  0,  3, Z8K_L},
    {"SRLL",  "10110011 dddd 0101",  "1ccc cccc cccc cccc",   "RL:d,SHR:c",           13, 13, 13,   0,  0,  0,  3, Z8K_L},
    {"SLAB",  "10110010 dddd 1001",  "0000 0000 0ccc cccc",   "RB:d,SHL:c",           13, 13, 13,   0,  0,  0,  3, Z8K_B},
    {"SRAB",  "10110010 dddd 1001",  "0000 0000 1ccc cccc",   "RB:d,SHR:c",           13, 13, 13,   0,  0,  0,  3, Z8K_B},
    {"SLA",   "10110011 dddd 1001",  "0ccc cccc cccc cccc",   "RW:d,SHL:c",           13, 13, 13,   0,  0,  0,  3, 0},
    {"SRA",   "10110011 dddd 1001",  "1ccc cccc cccc cccc",   "RW:d,SHR:c",           13, 13, 13,   0,  0,  0,  3, 0},
    {"SLAL",  "10110011 dddd 1101",  "0ccc cccc cccc cccc",   "RL:d,SHL:c",           13, 13, 13,   0,  0,  0,  3, Z8K_L},
    {"SRAL",  "10110011 dddd 1101",  "1ccc cccc cccc cccc",   "RL:d,SHR:c",           13, 13, 13,   0,  0,  0,  3, Z8K_L},
    // ---- Dynamisches Schieben (Anzahl in Rs, Vorzeichen = Richtung; Takte 15 + 3n)
    {"SDLB",  "10110010 dddd 0011",  "0000 ssss 0000 0000",   "RB:d,RW:s",            15, 15, 15,   0,  0,  0,  3, Z8K_B},
    {"SDL",   "10110011 dddd 0011",  "0000 ssss 0000 0000",   "RW:d,RW:s",            15, 15, 15,   0,  0,  0,  3, 0},
    {"SDLL",  "10110011 dddd 0111",  "0000 ssss 0000 0000",   "RL:d,RW:s",            15, 15, 15,   0,  0,  0,  3, Z8K_L},
    {"SDAB",  "10110010 dddd 1011",  "0000 ssss 0000 0000",   "RB:d,RW:s",            15, 15, 15,   0,  0,  0,  3, Z8K_B},
    {"SDA",   "10110011 dddd 1011",  "0000 ssss 0000 0000",   "RW:d,RW:s",            15, 15, 15,   0,  0,  0,  3, 0},
    {"SDAL",  "10110011 dddd 1111",  "0000 ssss 0000 0000",   "RL:d,RW:s",            15, 15, 15,   0,  0,  0,  3, Z8K_L},
    // ---- Ziffern rotieren (BE/BC): RLDB/RRDB Rbl,Rbs - Rbs steht im Feld 7..4
    {"RLDB",  "10 111110 ssss llll", "",                      "RB:l,RB:s",             9,  9,  9,   0,  0,  0,  0, Z8K_B},
    {"RRDB",  "10 111100 ssss llll", "",                      "RB:l,RB:s",             9,  9,  9,   0,  0,  0,  0, Z8K_B},
    // ---- Dezimalkorrektur und Vorzeichenerweiterung
    {"DAB",   "10 110000 dddd 0000", "",                      "RB:d",                  5,  5,  5,   0,  0,  0,  0, Z8K_B},
    {"EXTSB", "10 110001 dddd 0000", "",                      "RW:d",                 11, 11, 11,   0,  0,  0,  0, },
    {"EXTS",  "10 110001 dddd 1010", "",                      "RL:d",                 11, 11, 11,   0,  0,  0,  0, Z8K_L},
    {"EXTSL", "10 110001 dddd 0111", "",                      "RQ:d",                 11, 11, 11,   0,  0,  0,  0, Z8K_Q},
    // ---- Multiplikation/Division (MULTL 18, MULT 19, DIVL 1A, DIV 1B); MULTL + 7 je 1-Bit
    {"MULTL", "10 011000 ssss dddd", "",                      "RQ:d,RL:s",           282,282,282,   0,  0,  0,  7, Z8K_L},
    {"MULTL", "00 011000 0000 dddd", "",                      "RQ:d,IML",            282,282,282,   0,  0,  0,  7, Z8K_L},
    {"MULTL", "00 011000 ssss dddd", "",                      "RQ:d,IR:s",           282,282,282,   0,  0,  0,  7, Z8K_L},
    {"MULTL", "01 011000 0000 dddd", "",                      "RQ:d,DA",             283,283,286,   0,  0,  0,  7, Z8K_L},
    {"MULTL", "01 011000 ssss dddd", "",                      "RQ:d,X:s",            284,284,287,   0,  0,  0,  7, Z8K_L},
    {"MULT",  "10 011001 ssss dddd", "",                      "RL:d,RW:s",            70, 70, 70,   0,  0,  0,  0, 0},
    {"MULT",  "00 011001 0000 dddd", "",                      "RL:d,IMW",             70, 70, 70,   0,  0,  0,  0, 0},
    {"MULT",  "00 011001 ssss dddd", "",                      "RL:d,IR:s",            70, 70, 70,   0,  0,  0,  0, 0},
    {"MULT",  "01 011001 0000 dddd", "",                      "RL:d,DA",              71, 72, 74,   0,  0,  0,  0, 0},
    {"MULT",  "01 011001 ssss dddd", "",                      "RL:d,X:s",             72, 72, 75,   0,  0,  0,  0, 0},
    {"DIVL",  "10 011010 ssss dddd", "",                      "RQ:d,RL:s",           744,744,744,   0,  0,  0,  0, Z8K_L},
    {"DIVL",  "00 011010 0000 dddd", "",                      "RQ:d,IML",            744,744,744,   0,  0,  0,  0, Z8K_L},
    {"DIVL",  "00 011010 ssss dddd", "",                      "RQ:d,IR:s",           744,744,744,   0,  0,  0,  0, Z8K_L},
    {"DIVL",  "01 011010 0000 dddd", "",                      "RQ:d,DA",             745,746,748,   0,  0,  0,  0, Z8K_L},
    {"DIVL",  "01 011010 ssss dddd", "",                      "RQ:d,X:s",            746,746,749,   0,  0,  0,  0, Z8K_L},
    {"DIV",   "10 011011 ssss dddd", "",                      "RL:d,RW:s",           107,107,107,   0,  0,  0,  0, 0},
    {"DIV",   "00 011011 0000 dddd", "",                      "RL:d,IMW",            107,107,107,   0,  0,  0,  0, 0},
    {"DIV",   "00 011011 ssss dddd", "",                      "RL:d,IR:s",           107,107,107,   0,  0,  0,  0, 0},
    {"DIV",   "01 011011 0000 dddd", "",                      "RL:d,DA",             108,109,111,   0,  0,  0,  0, 0},
    {"DIV",   "01 011011 ssss dddd", "",                      "RL:d,X:s",            109,109,112,   0,  0,  0,  0, 0},
    // ---- Stapel: PUSH (13/53/93), PUSHL (11/51/91), POP (17/57/97), POPL (15/55/95)
    {"PUSH",  "10 010011 dddd ssss", "",                      "IR:d,RW:s",             9,  9,  9,   0,  0,  0,  0, 0},
    {"PUSH",  "00 010011 dddd ssss", "",                      "IR:d,IR:s",            13, 13, 13,   0,  0,  0,  0, 0},
    {"PUSH",  "01 010011 dddd 0000", "",                      "IR:d,DA",              14, 14, 16,   0,  0,  0,  0, 0},
    {"PUSH",  "01 010011 dddd ssss", "",                      "IR:d,X:s",             14, 14, 17,   0,  0,  0,  0, 0},
    {"PUSHL", "10 010001 dddd ssss", "",                      "IR:d,RL:s",            12, 12, 12,   0,  0,  0,  0, Z8K_L},
    {"PUSHL", "00 010001 dddd ssss", "",                      "IR:d,IR:s",            20, 20, 20,   0,  0,  0,  0, Z8K_L},
    {"PUSHL", "01 010001 dddd 0000", "",                      "IR:d,DA",              21, 21, 23,   0,  0,  0,  0, Z8K_L},
    {"PUSHL", "01 010001 dddd ssss", "",                      "IR:d,X:s",             21, 21, 24,   0,  0,  0,  0, Z8K_L},
    {"POP",   "10 010111 ssss dddd", "",                      "RW:d,IR:s",             8,  8,  8,   0,  0,  0,  0, 0},
    {"POP",   "00 010111 ssss dddd", "",                      "IR:d,IR:s",            12, 12, 12,   0,  0,  0,  0, 0},
    {"POP",   "01 010111 ssss 0000", "",                      "DA,IR:s",              16, 16, 18,   0,  0,  0,  0, 0},
    {"POP",   "01 010111 ssss dddd", "",                      "X:d,IR:s",             16, 16, 19,   0,  0,  0,  0, 0},
    {"POPL",  "10 010101 ssss dddd", "",                      "RL:d,IR:s",            12, 12, 12,   0,  0,  0,  0, Z8K_L},
    {"POPL",  "00 010101 ssss dddd", "",                      "IR:d,IR:s",            19, 19, 19,   0,  0,  0,  0, Z8K_L},
    {"POPL",  "01 010101 ssss 0000", "",                      "DA,IR:s",              23, 23, 25,   0,  0,  0,  0, Z8K_L},
    {"POPL",  "01 010101 ssss dddd", "",                      "X:d,IR:s",             23, 23, 26,   0,  0,  0,  0, Z8K_L},
    // ---- Spruenge und Aufrufe (Takte: genommen, alt = nicht genommen)
    {"JP",    "00 011110 dddd cccc", "",                      "CC:c,IR:d",            10, 15, 15,   7,  7,  7,  0, Z8K_JUMP},
    {"JP",    "01 011110 0000 cccc", "",                      "CC:c,DA",               7,  8, 10,   7,  8, 10,  0, Z8K_JUMP},
    {"JP",    "01 011110 dddd cccc", "",                      "CC:c,X:d",              8,  8, 11,   8,  8, 11,  0, Z8K_JUMP},
    {"CALL",  "00 011111 dddd 0000", "",                      "IR:d",                 10, 15, 15,   0,  0,  0,  0, Z8K_CALL},
    {"CALL",  "01 011111 0000 0000", "",                      "DA",                   12, 18, 20,   0,  0,  0,  0, Z8K_CALL},
    {"CALL",  "01 011111 dddd 0000", "",                      "X:d",                  13, 18, 21,   0,  0,  0,  0, Z8K_CALL},
    {"CALR",  "1101 dddd dddd dddd", "",                      "RA12:d",               10, 15, 15,   0,  0,  0,  0, Z8K_CALL},
    {"JR",    "1110 cccc dddd dddd", "",                      "CC:c,RA8:d",            6,  6,  6,   6,  6,  6,  0, Z8K_JUMP},
    {"DJNZ",  "1111 rrrr 1ddd dddd", "",                      "RW:r,RA7:d",           11, 11, 11,  11, 11, 11,  0, Z8K_JUMP},
    {"DBJNZ", "1111 rrrr 0ddd dddd", "",                      "RB:r,RA7:d",           11, 11, 11,  11, 11, 11,  0, Z8K_JUMP|Z8K_B},
    {"RET",   "10 011110 0000 cccc", "",                      "CC:c",                 10, 13, 13,   7,  7,  7,  0, Z8K_RET},
    {"SC",    "01 111111 iiii iiii", "",                      "IM8:i",                33, 39, 39,   0,  0,  0,  0, Z8K_CALL},
    // ---- Test Condition Code (AE/AF)
    {"TCCB",  "10 101110 dddd cccc", "",                      "CC:c,RB:d",             5,  5,  5,   0,  0,  0,  0, Z8K_B},
    {"TCC",   "10 101111 dddd cccc", "",                      "CC:c,RW:d",             5,  5,  5,   0,  0,  0,  0, },
    // ---- Blockbefehle Speicher (BA/BB): Feld 7..4 = Quelle, 2. Wort 0000 r d x (x=1000 einzeln)
    {"LDIB",  "10 111010 ssss 0001", "0000 rrrr dddd 1000",   "IR:d,IR:s,RW:r",       20, 20, 20,   0,  0,  0,  0, Z8K_B},
    {"LDIRB", "10 111010 ssss 0001", "0000 rrrr dddd 0000",   "IR:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0,  9, Z8K_B|Z8K_REPEAT},
    {"LDDB",  "10 111010 ssss 1001", "0000 rrrr dddd 1000",   "IR:d,IR:s,RW:r",       20, 20, 20,   0,  0,  0,  0, Z8K_B},
    {"LDDRB", "10 111010 ssss 1001", "0000 rrrr dddd 0000",   "IR:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0,  9, Z8K_B|Z8K_REPEAT},
    {"CPIB",  "10 111010 ssss 0000", "0000 rrrr dddd cccc",   "RB:d,IR:s,RW:r,CC:c",  20, 20, 20,   0,  0,  0,  0, Z8K_B},
    {"CPIRB", "10 111010 ssss 0100", "0000 rrrr dddd cccc",   "RB:d,IR:s,RW:r,CC:c",  11, 11, 11,   0,  0,  0,  9, Z8K_B|Z8K_REPEAT},
    {"CPDB",  "10 111010 ssss 1000", "0000 rrrr dddd cccc",   "RB:d,IR:s,RW:r,CC:c",  20, 20, 20,   0,  0,  0,  0, Z8K_B},
    {"CPDRB", "10 111010 ssss 1100", "0000 rrrr dddd cccc",   "RB:d,IR:s,RW:r,CC:c",  11, 11, 11,   0,  0,  0,  9, Z8K_B|Z8K_REPEAT},
    {"CPSIB", "10 111010 ssss 0010", "0000 rrrr dddd cccc",   "IR:d,IR:s,RW:r,CC:c",  25, 25, 25,   0,  0,  0,  0, Z8K_B},
    {"CPSIRB", "10 111010 ssss 0110", "0000 rrrr dddd cccc",   "IR:d,IR:s,RW:r,CC:c",  11, 11, 11,   0,  0,  0, 14, Z8K_B|Z8K_REPEAT},
    {"CPSDB", "10 111010 ssss 1010", "0000 rrrr dddd cccc",   "IR:d,IR:s,RW:r,CC:c",  25, 25, 25,   0,  0,  0,  0, Z8K_B},
    {"CPSDRB", "10 111010 ssss 1110", "0000 rrrr dddd cccc",   "IR:d,IR:s,RW:r,CC:c",  11, 11, 11,   0,  0,  0, 14, Z8K_B|Z8K_REPEAT},
    {"LDI",   "10 111011 ssss 0001", "0000 rrrr dddd 1000",   "IR:d,IR:s,RW:r",       20, 20, 20,   0,  0,  0,  0, 0},
    {"LDIR",  "10 111011 ssss 0001", "0000 rrrr dddd 0000",   "IR:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0,  9, 0|Z8K_REPEAT},
    {"LDD",   "10 111011 ssss 1001", "0000 rrrr dddd 1000",   "IR:d,IR:s,RW:r",       20, 20, 20,   0,  0,  0,  0, 0},
    {"LDDR",  "10 111011 ssss 1001", "0000 rrrr dddd 0000",   "IR:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0,  9, 0|Z8K_REPEAT},
    {"CPI",   "10 111011 ssss 0000", "0000 rrrr dddd cccc",   "RW:d,IR:s,RW:r,CC:c",  20, 20, 20,   0,  0,  0,  0, 0},
    {"CPIR",  "10 111011 ssss 0100", "0000 rrrr dddd cccc",   "RW:d,IR:s,RW:r,CC:c",  11, 11, 11,   0,  0,  0,  9, 0|Z8K_REPEAT},
    {"CPD",   "10 111011 ssss 1000", "0000 rrrr dddd cccc",   "RW:d,IR:s,RW:r,CC:c",  20, 20, 20,   0,  0,  0,  0, 0},
    {"CPDR",  "10 111011 ssss 1100", "0000 rrrr dddd cccc",   "RW:d,IR:s,RW:r,CC:c",  11, 11, 11,   0,  0,  0,  9, 0|Z8K_REPEAT},
    {"CPSI",  "10 111011 ssss 0010", "0000 rrrr dddd cccc",   "IR:d,IR:s,RW:r,CC:c",  25, 25, 25,   0,  0,  0,  0, 0},
    {"CPSIR", "10 111011 ssss 0110", "0000 rrrr dddd cccc",   "IR:d,IR:s,RW:r,CC:c",  11, 11, 11,   0,  0,  0, 14, 0|Z8K_REPEAT},
    {"CPSD",  "10 111011 ssss 1010", "0000 rrrr dddd cccc",   "IR:d,IR:s,RW:r,CC:c",  25, 25, 25,   0,  0,  0,  0, 0},
    {"CPSDR", "10 111011 ssss 1110", "0000 rrrr dddd cccc",   "IR:d,IR:s,RW:r,CC:c",  11, 11, 11,   0,  0,  0, 14, 0|Z8K_REPEAT},
    // ---- Uebersetzen (B8, nur Byte): Feld 7..4 = Ziel bzw. src1, 2. Wort 0000 r s x
    {"TRIB",  "10 111000 dddd 0000", "0000 rrrr ssss 0000",   "IR:d,IR:s,RW:r",       25, 25, 25,   0,  0,  0,  0, Z8K_B},
    {"TRTIB", "10 111000 dddd 0010", "0000 rrrr ssss 0000",   "IR:d,IR:s,RW:r",       25, 25, 25,   0,  0,  0,  0, Z8K_B},
    {"TRIRB", "10 111000 dddd 0100", "0000 rrrr ssss 0000",   "IR:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 14, Z8K_B|Z8K_REPEAT},
    {"TRTIRB", "10 111000 dddd 0110", "0000 rrrr ssss 1110",   "IR:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 14, Z8K_B|Z8K_REPEAT},
    {"TRDB",  "10 111000 dddd 1000", "0000 rrrr ssss 0000",   "IR:d,IR:s,RW:r",       25, 25, 25,   0,  0,  0,  0, Z8K_B},
    {"TRTDB", "10 111000 dddd 1010", "0000 rrrr ssss 0000",   "IR:d,IR:s,RW:r",       25, 25, 25,   0,  0,  0,  0, Z8K_B},
    {"TRDRB", "10 111000 dddd 1100", "0000 rrrr ssss 0000",   "IR:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 14, Z8K_B|Z8K_REPEAT},
    {"TRTDRB", "10 111000 dddd 1110", "0000 rrrr ssss 1110",   "IR:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 14, Z8K_B|Z8K_REPEAT},
    // ---- E/A (privilegiert): IN/OUT ueber @Rs (3C..3F) und Portadresse (3A/3B Unterkode 4..7)
    {"INB",   "00 111100 ssss dddd", "",                      "RB:d,IO:s",            10, 10, 10,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"IN",    "00 111101 ssss dddd", "",                      "RW:d,IO:s",            10, 10, 10,   0,  0,  0,  0, Z8K_PRIV},
    {"OUTB",  "00 111110 dddd ssss", "",                      "IO:d,RB:s",            10, 10, 10,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"OUT",   "00 111111 dddd ssss", "",                      "IO:d,RW:s",            10, 10, 10,   0,  0,  0,  0, Z8K_PRIV},
    {"INB",   "00 111010 dddd 0100", "",                      "RB:d,PORT",            12, 12, 12,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"SINB",  "00 111010 dddd 0101", "",                      "RB:d,PORT",            12, 12, 12,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"OUTB",  "00 111010 ssss 0110", "",                      "PORT,RB:s",            12, 12, 12,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"SOUTB", "00 111010 ssss 0111", "",                      "PORT,RB:s",            12, 12, 12,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"IN",    "00 111011 dddd 0100", "",                      "RW:d,PORT",            12, 12, 12,   0,  0,  0,  0, Z8K_PRIV},
    {"SIN",   "00 111011 dddd 0101", "",                      "RW:d,PORT",            12, 12, 12,   0,  0,  0,  0, Z8K_PRIV},
    {"OUT",   "00 111011 ssss 0110", "",                      "PORT,RW:s",            12, 12, 12,   0,  0,  0,  0, Z8K_PRIV},
    {"SOUT",  "00 111011 ssss 0111", "",                      "PORT,RW:s",            12, 12, 12,   0,  0,  0,  0, Z8K_PRIV},
    // ---- Block-E/A (3A/3B): Feld 7..4 = Quelle, 2. Wort 0000 r d x; Eingabe: Quelle = Port, Ausgabe: Ziel = Port
    {"INIB",  "00 111010 ssss 0000", "0000 rrrr dddd 1000",   "IR:d,IO:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"INIRB", "00 111010 ssss 0000", "0000 rrrr dddd 0000",   "IR:d,IO:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_B|Z8K_REPEAT},
    {"SINIB", "00 111010 ssss 0001", "0000 rrrr dddd 1000",   "IR:d,IO:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"SINIRB", "00 111010 ssss 0001", "0000 rrrr dddd 0000",   "IR:d,IO:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_B|Z8K_REPEAT},
    {"OUTIB", "00 111010 ssss 0010", "0000 rrrr dddd 1000",   "IO:d,IR:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"OTIRB", "00 111010 ssss 0010", "0000 rrrr dddd 0000",   "IO:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_B|Z8K_REPEAT},
    {"SOUTIB", "00 111010 ssss 0011", "0000 rrrr dddd 1000",   "IO:d,IR:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"SOTIRB", "00 111010 ssss 0011", "0000 rrrr dddd 0000",   "IO:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_B|Z8K_REPEAT},
    {"INDB",  "00 111010 ssss 1000", "0000 rrrr dddd 1000",   "IR:d,IO:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"INDRB", "00 111010 ssss 1000", "0000 rrrr dddd 0000",   "IR:d,IO:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_B|Z8K_REPEAT},
    {"SINDB", "00 111010 ssss 1001", "0000 rrrr dddd 1000",   "IR:d,IO:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"SINDRB", "00 111010 ssss 1001", "0000 rrrr dddd 0000",   "IR:d,IO:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_B|Z8K_REPEAT},
    {"OUTDB", "00 111010 ssss 1010", "0000 rrrr dddd 1000",   "IO:d,IR:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"OTDRB", "00 111010 ssss 1010", "0000 rrrr dddd 0000",   "IO:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_B|Z8K_REPEAT},
    {"SOUTDB", "00 111010 ssss 1011", "0000 rrrr dddd 1000",   "IO:d,IR:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV|Z8K_B},
    {"SOTDRB", "00 111010 ssss 1011", "0000 rrrr dddd 0000",   "IO:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_B|Z8K_REPEAT},
    {"INI",   "00 111011 ssss 0000", "0000 rrrr dddd 1000",   "IR:d,IO:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV},
    {"INIR",  "00 111011 ssss 0000", "0000 rrrr dddd 0000",   "IR:d,IO:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_REPEAT},
    {"SINI",  "00 111011 ssss 0001", "0000 rrrr dddd 1000",   "IR:d,IO:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV},
    {"SINIR", "00 111011 ssss 0001", "0000 rrrr dddd 0000",   "IR:d,IO:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_REPEAT},
    {"OUTI",  "00 111011 ssss 0010", "0000 rrrr dddd 1000",   "IO:d,IR:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV},
    {"OTIR",  "00 111011 ssss 0010", "0000 rrrr dddd 0000",   "IO:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_REPEAT},
    {"SOUTI", "00 111011 ssss 0011", "0000 rrrr dddd 1000",   "IO:d,IR:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV},
    {"SOTIR", "00 111011 ssss 0011", "0000 rrrr dddd 0000",   "IO:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_REPEAT},
    {"IND",   "00 111011 ssss 1000", "0000 rrrr dddd 1000",   "IR:d,IO:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV},
    {"INDR",  "00 111011 ssss 1000", "0000 rrrr dddd 0000",   "IR:d,IO:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_REPEAT},
    {"SIND",  "00 111011 ssss 1001", "0000 rrrr dddd 1000",   "IR:d,IO:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV},
    {"SINDR", "00 111011 ssss 1001", "0000 rrrr dddd 0000",   "IR:d,IO:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_REPEAT},
    {"OUTD",  "00 111011 ssss 1010", "0000 rrrr dddd 1000",   "IO:d,IR:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV},
    {"OTDR",  "00 111011 ssss 1010", "0000 rrrr dddd 0000",   "IO:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_REPEAT},
    {"SOUTD", "00 111011 ssss 1011", "0000 rrrr dddd 1000",   "IO:d,IR:s,RW:r",       21, 21, 21,   0,  0,  0,  0, Z8K_PRIV},
    {"SOTDR", "00 111011 ssss 1011", "0000 rrrr dddd 0000",   "IO:d,IR:s,RW:r",       11, 11, 11,   0,  0,  0, 10, Z8K_PRIV|Z8K_REPEAT},
    // ---- CPU-Steuerung (privilegiert, ausser LDCTLB/Flag-Befehlen/NOP/SC)
    {"HALT",  "01 111010 0000 0000", "",                      "",                      8,  8,  8,   0,  0,  0,  3, Z8K_PRIV},
    {"IRET",  "01 111011 0000 0000", "",                      "",                     13, 16, 16,   0,  0,  0,  0, Z8K_PRIV|Z8K_RET},
    {"MSET",  "01 111011 0000 1000", "",                      "",                      5,  5,  5,   0,  0,  0,  0, Z8K_PRIV},
    {"MRES",  "01 111011 0000 1001", "",                      "",                      5,  5,  5,   0,  0,  0,  0, Z8K_PRIV},
    {"MBIT",  "01 111011 0000 1010", "",                      "",                      7,  7,  7,   0,  0,  0,  0, Z8K_PRIV},
    {"MREQ",  "01 111011 dddd 1101", "",                      "RW:d",                 12, 12, 12,   0,  0,  0,  7, Z8K_PRIV},
    {"DI",    "01 111100 0000 00vv", "",                      "INT:v",                 7,  7,  7,   0,  0,  0,  0, Z8K_PRIV},
    {"EI",    "01 111100 0000 01vv", "",                      "INT:v",                 7,  7,  7,   0,  0,  0,  0, Z8K_PRIV},
    {"LDCTL", "01 111101 dddd 0kkk", "",                      "RW:d,CTL:k",            7,  7,  7,   0,  0,  0,  0, Z8K_PRIV},
    {"LDCTL", "01 111101 ssss 1kkk", "",                      "CTL:k,RW:s",            7,  7,  7,   0,  0,  0,  0, Z8K_PRIV},
    {"LDPS",  "00 111001 ssss 0000", "",                      "IR:s",                 12, 16, 16,   0,  0,  0,  0, Z8K_PRIV},
    {"LDPS",  "01 111001 0000 0000", "",                      "DA",                   16, 20, 22,   0,  0,  0,  0, Z8K_PRIV},
    {"LDPS",  "01 111001 ssss 0000", "",                      "X:s",                  17, 20, 23,   0,  0,  0,  0, Z8K_PRIV},
    // ---- Erweiterte Befehle (EPA, 0E/0F/4E/4F/8E/8F): ohne EPU -> Extended Instruction Trap.
    // ---- Die Felder gehoeren der EPU und werden roh gefuehrt; Laenge 2 Worte (+ Adresse bei 4E/4F).
    {"EXT0E", "00001110 iiii iiii",  "jjjj jjjj jjjj jjjj",   "RAW:i,RAW:j",           0,  0,  0,   0,  0,  0,  0, Z8K_EPA},
    {"EXT0F", "00001111 iiii iiii",  "jjjj jjjj jjjj jjjj",   "RAW:i,RAW:j",           0,  0,  0,   0,  0,  0,  0, Z8K_EPA},
    {"EXT4E", "01001110 0000 iiii",  "jjjj jjjj jjjj jjjj",   "RAW:i,RAW:j,DA",        0,  0,  0,   0,  0,  0,  0, Z8K_EPA},
    {"EXT4E", "01001110 xxxx iiii",  "jjjj jjjj jjjj jjjj",   "RAW:i,RAW:j,X:x",       0,  0,  0,   0,  0,  0,  0, Z8K_EPA},
    {"EXT4F", "01001111 0000 iiii",  "jjjj jjjj jjjj jjjj",   "RAW:i,RAW:j,DA",        0,  0,  0,   0,  0,  0,  0, Z8K_EPA},
    {"EXT4F", "01001111 xxxx iiii",  "jjjj jjjj jjjj jjjj",   "RAW:i,RAW:j,X:x",       0,  0,  0,   0,  0,  0,  0, Z8K_EPA},
    {"EXT8E", "10001110 iiii iiii",  "jjjj jjjj jjjj jjjj",   "RAW:i,RAW:j",           0,  0,  0,   0,  0,  0,  0, Z8K_EPA},
    {"EXT8F", "10001111 iiii iiii",  "jjjj jjjj jjjj jjjj",   "RAW:i,RAW:j",           0,  0,  0,   0,  0,  0,  0, Z8K_EPA},
};

inline constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

// ── Aufbereitete Form ────────────────────────────────────────────────────────

/// Operandenart.  Registernummer bzw. Wert steht nach dem Dekodieren in Operand.
enum class Kind : uint8_t {
    None,
    RB, RW, RL, RQ,   ///< Register RHn/RLn, Rn, RRn, RQn (Feldwert = Kodierung)
    RP,               ///< Adressregister: Rn nichtsegmentiert, RRn segmentiert (LDA/LDAR)
    IR,               ///< indirekt @Rn / @RRn — Feld ≠ 0 (0 kodiert IM bzw. eine andere Zeile)
    IO,               ///< E/A-Port indirekt @Rn (immer Wortregister)
    DA,               ///< Direktadresse, 1 Wort (nonseg) bzw. 1/2 Worte (seg kurz/lang)
    X,                ///< indiziert addr(Rn), Feld = Indexregister ≠ 0, Adresse wie DA
    BA,               ///< Basis Rn(#disp) / RRn(#disp), Feld ≠ 0, disp im Erweiterungswort
    BX,               ///< Basis-Index Rn(Rm) / RRn(Rm), Basis ≠ 0 (Feld 1), Index Feld 2
    RA16,             ///< relativ, 16 Bit Byteabstand im Erweiterungswort (LDR/LDAR)
    RA8,              ///< JR: Ziel = PC + 2·disp8 (vorzeichenbehaftet)
    RA7,              ///< DJNZ: Ziel = PC − 2·disp7 (nur rückwärts)
    RA12,             ///< CALR: Ziel = PC − 2·disp12 (vorzeichenbehaftet)
    IMB,              ///< Direktwert Byte (Erweiterungswort, Byte doppelt)
    IMW,              ///< Direktwert Wort
    IML,              ///< Direktwert Langwort (zwei Worte, höherwertiges zuerst)
    IM8,              ///< Direktwert 8 Bit im Feld (LDB kurz, SC)
    IM4,              ///< Direktwert 0..15 im Feld (LDK)
    N16,              ///< Anzahl 1..16, Feld = n−1 (INC/DEC)
    BIT,              ///< Bitnummer (statisch)
    SHL,              ///< Schiebeweite links, Feld = n
    SHR,              ///< Schiebeweite rechts, Feld = −n im Zweierkomplement (Vorzeichenbit fest 1)
    CC,               ///< Bedingungskode
    FL,               ///< Flagliste C,Z,S,P/V (Bit 3..0 des Feldes = C Z S P/V)
    INT,              ///< VI/NVI — Bit 1 = VI, Bit 0 = NVI, 0 = betroffen
    CTL,              ///< Steuerregister 2..7 (FCW, REFRESH, PSAPSEG, PSAPOFF, NSPSEG, NSPOFF)
    FLAGS,            ///< das Flag-Byte (LDCTLB)
    PORT,             ///< E/A-Adresse (Erweiterungswort, nie segmentiert)
    LDMN,             ///< Registeranzahl 1..16 (Feld n−1)
    RAW,              ///< EPU-Feld, roh
    LIT,              ///< feste Zahl (#1/#2 beim Rotieren)
};

/// Lage eines Feldes: Befehlswort 0/1, niedrigstes Bit, Breite.  word=0xFF: keins.
struct Field {
    uint8_t word = 0xFF, lsb = 0, width = 0;
    bool valid() const { return word != 0xFF; }
    uint16_t get(uint16_t w) const { return uint16_t((w >> lsb) & ((1u << width) - 1)); }
    uint16_t mask() const { return uint16_t(((1u << width) - 1) << lsb); }
};

struct OpSpec {
    Kind  kind = Kind::None;
    Field f;             ///< Hauptfeld (Register, Wert …)
    Field g;             ///< zweites Feld (BX: Indexregister)
    uint8_t lit = 0;     ///< Kind::LIT: der Wert
};

/// Eine aufbereitete Tabellenzeile.  `index` = Position in kRows.
struct Insn {
    const RowSrc* src = nullptr;
    int      index = 0;
    const char* mn = "";
    uint16_t mask0 = 0, match0 = 0;
    bool     hasW1 = false;
    uint16_t mask1 = 0, match1 = 0;
    int      nops = 0;
    OpSpec   op[4];
    uint16_t cyc[3] = {};      ///< ns, ss, sl
    uint16_t alt[3] = {};      ///< Bedingung nicht erfüllt (0 = wie cyc)
    uint8_t  perN = 0;
    uint32_t flags = 0;

    bool has(uint32_t f) const { return (flags & f) != 0; }
    /// Hat die Zeile einen DA/X-Operanden (dann unterscheiden sich SS/SL)?
    bool hasSegAddr() const {
        for (int i = 0; i < nops; ++i)
            if (op[i].kind == Kind::DA || op[i].kind == Kind::X) return true;
        return false;
    }
};

namespace detail {

inline bool isField(char c) { return c >= 'a' && c <= 'z'; }

/// Bitmuster → Maske/Wert, Felder in fields[letter] eintragen.
inline void parsePattern(const char* pat, uint8_t word, uint16_t& mask, uint16_t& match,
                         std::array<Field, 26>& fields, const char* mn) {
    std::string bitsOnly;
    for (const char* p = pat; *p; ++p) if (*p != ' ') bitsOnly += *p;
    if (bitsOnly.size() != 16)
        throw std::logic_error(std::string("z8k: Muster nicht 16 Bit: ") + mn + " '" + pat + "'");
    mask = match = 0;
    for (int i = 0; i < 16; ++i) {
        char c = bitsOnly[size_t(i)];
        int bit = 15 - i;
        if (c == '0' || c == '1') {
            mask |= uint16_t(1u << bit);
            if (c == '1') match |= uint16_t(1u << bit);
        } else if (isField(c)) {
            Field& f = fields[size_t(c - 'a')];
            if (!f.valid()) { f.word = word; f.lsb = uint8_t(bit); f.width = 1; }
            else {
                if (f.word != word || f.lsb != bit + 1)
                    throw std::logic_error(std::string("z8k: Feld nicht zusammenhaengend: ") + mn);
                f.lsb = uint8_t(bit); ++f.width;
            }
        } else {
            throw std::logic_error(std::string("z8k: unbekanntes Musterzeichen in ") + mn);
        }
    }
}

inline Kind kindFromName(const std::string& s) {
    static const struct { const char* n; Kind k; } kNames[] = {
        {"RB",Kind::RB},{"RW",Kind::RW},{"RL",Kind::RL},{"RQ",Kind::RQ},{"RP",Kind::RP},
        {"IR",Kind::IR},{"IO",Kind::IO},{"DA",Kind::DA},{"X",Kind::X},{"BA",Kind::BA},
        {"BX",Kind::BX},{"RA16",Kind::RA16},{"RA8",Kind::RA8},{"RA7",Kind::RA7},
        {"RA12",Kind::RA12},{"IMB",Kind::IMB},{"IMW",Kind::IMW},{"IML",Kind::IML},
        {"IM8",Kind::IM8},{"IM4",Kind::IM4},{"N16",Kind::N16},{"BIT",Kind::BIT},
        {"SHL",Kind::SHL},{"SHR",Kind::SHR},{"CC",Kind::CC},{"FL",Kind::FL},
        {"INT",Kind::INT},{"CTL",Kind::CTL},{"FLAGS",Kind::FLAGS},{"PORT",Kind::PORT},
        {"LDMN",Kind::LDMN},{"RAW",Kind::RAW},
    };
    for (auto& e : kNames) if (s == e.n) return e.k;
    return Kind::None;
}

/// Braucht die Art ein Feld (true) oder lebt sie im Erweiterungswort bzw. ist fest?
inline bool kindNeedsField(Kind k) {
    switch (k) {
    case Kind::DA: case Kind::RA16: case Kind::IMB: case Kind::IMW: case Kind::IML:
    case Kind::FLAGS: case Kind::PORT: case Kind::LIT: case Kind::None:
        return false;
    default: return true;
    }
}

inline Insn buildInsn(const RowSrc& r, int index) {
    Insn in;
    in.src = &r; in.index = index; in.mn = r.mn;
    std::array<Field, 26> fields{};
    parsePattern(r.w0, 0, in.mask0, in.match0, fields, r.mn);
    if (r.w1 && *r.w1) {
        in.hasW1 = true;
        parsePattern(r.w1, 1, in.mask1, in.match1, fields, r.mn);
    }
    std::array<bool, 26> used{};
    std::string ops = r.ops ? r.ops : "";
    size_t pos = 0;
    while (pos < ops.size()) {
        size_t e = ops.find(',', pos);
        std::string tok = ops.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = (e == std::string::npos) ? ops.size() : e + 1;
        if (in.nops >= 4) throw std::logic_error(std::string("z8k: zu viele Operanden: ") + r.mn);
        OpSpec& o = in.op[in.nops++];
        if (!tok.empty() && tok[0] == '#') {
            o.kind = Kind::LIT; o.lit = uint8_t(std::stoi(tok.substr(1)));
            continue;
        }
        std::string name = tok, f1, f2;
        size_t c1 = tok.find(':');
        if (c1 != std::string::npos) {
            name = tok.substr(0, c1);
            std::string rest = tok.substr(c1 + 1);
            size_t c2 = rest.find(':');
            f1 = rest.substr(0, c2);
            if (c2 != std::string::npos) f2 = rest.substr(c2 + 1);
        }
        o.kind = kindFromName(name);
        if (o.kind == Kind::None)
            throw std::logic_error(std::string("z8k: unbekannte Operandenart '") + name + "' in " + r.mn);
        auto take = [&](const std::string& fn, Field& dst) {
            if (fn.size() != 1 || !isField(fn[0]) || !fields[size_t(fn[0] - 'a')].valid())
                throw std::logic_error(std::string("z8k: Feld '") + fn + "' fehlt im Muster von " + r.mn);
            dst = fields[size_t(fn[0] - 'a')];
            used[size_t(fn[0] - 'a')] = true;
        };
        if (kindNeedsField(o.kind)) {
            if (f1.empty()) throw std::logic_error(std::string("z8k: Operand ohne Feld in ") + r.mn);
            take(f1, o.f);
            if (o.kind == Kind::BX) {
                if (f2.empty()) throw std::logic_error(std::string("z8k: BX ohne Indexfeld in ") + r.mn);
                take(f2, o.g);
            }
        } else if (!f1.empty()) {
            throw std::logic_error(std::string("z8k: Operand mit ueberzaehligem Feld in ") + r.mn);
        }
    }
    for (size_t i = 0; i < 26; ++i)
        if (fields[i].valid() && !used[i])
            throw std::logic_error(std::string("z8k: Feld '") + char('a' + i) + "' ohne Operand in " + r.mn);
    in.cyc[0] = r.ns; in.cyc[1] = r.ss; in.cyc[2] = r.sl;
    in.alt[0] = r.ans; in.alt[1] = r.ass; in.alt[2] = r.asl;
    in.perN = r.perN; in.flags = r.flags;
    return in;
}

} // namespace detail

/// Erfüllen die Felder von w0 (und, falls haveW1, w1) die Nebenbedingungen der
/// Zeile?  (IR/X/BA/BX-Basis ≠ 0, Steuerregister 2..7.)
inline bool fieldsOk(const Insn& in, uint16_t w0, uint16_t w1, bool haveW1) {
    for (int i = 0; i < in.nops; ++i) {
        const OpSpec& o = in.op[i];
        if (!o.f.valid()) continue;
        if (o.f.word == 1 && !haveW1) continue;
        uint16_t v = o.f.get(o.f.word == 0 ? w0 : w1);
        switch (o.kind) {
        case Kind::IR: case Kind::X: case Kind::BA: case Kind::BX:
            if (v == 0) return false;
            break;
        case Kind::CTL:
            if (v < 2) return false;
            break;
        default: break;
        }
    }
    return true;
}

/**
 * Die aufbereitete Tabelle plus Schnellsuche über das erste Befehlswort:
 * `candidates(w0)` liefert die Zeilen (in Tabellenreihenfolge), deren w0-Muster
 * und w0-Nebenbedingungen passen.  Mehr als ein Kandidat heißt: das zweite Wort
 * entscheidet (LDI/LDIR, SLL/SRL, TRIB/TRIRB, …).  Aufbau einmal je Prozess
 * (~0,1 Mio. Einträge), danach nur Lesen — threadsicher.
 */
class Table {
public:
    static const Table& get() { static const Table t; return t; }

    const std::vector<Insn>& insns() const { return insns_; }
    const Insn& at(int i) const { return insns_[size_t(i)]; }

    /// Kandidaten für das erste Befehlswort w0.
    const uint16_t* candidates(uint16_t w0, int& count) const {
        uint32_t b = start_[w0], e = start_[size_t(w0) + 1];
        count = int(e - b);
        return pool_.data() + b;
    }

private:
    Table() {
        insns_.reserve(kRowCount);
        for (size_t i = 0; i < kRowCount; ++i) insns_.push_back(detail::buildInsn(kRows[i], int(i)));
        auto forEach = [](const Insn& in, auto&& fn) {
            uint32_t freeMask = uint16_t(~in.mask0);
            uint32_t sub = 0;               // alle Teilmengen der freien Bits
            do {
                uint16_t w = uint16_t(in.match0 | sub);
                if (fieldsOk(in, w, 0, false)) fn(w);
                sub = (sub - freeMask) & freeMask;
            } while (sub != 0);
        };
        std::vector<uint32_t> cnt(65536, 0);
        for (auto& in : insns_) forEach(in, [&](uint16_t w) { ++cnt[w]; });
        start_.assign(65537, 0);
        for (size_t w = 0; w < 65536; ++w) start_[w + 1] = start_[w] + cnt[w];
        pool_.assign(start_[65536], 0);
        std::vector<uint32_t> fill(start_.begin(), start_.end() - 1);
        for (auto& in : insns_) forEach(in, [&](uint16_t w) { pool_[fill[w]++] = uint16_t(in.index); });
    }
    std::vector<Insn>     insns_;
    std::vector<uint32_t> start_;
    std::vector<uint16_t> pool_;
};

// ── Namen ────────────────────────────────────────────────────────────────────

/// Bedingungskodes, kanonische Namen (Index = Kodierung).
inline const char* ccName(unsigned cc) {
    static const char* const k[16] = {"F","LT","LE","ULE","OV","MI","Z","C",
                                      "T","GE","GT","UGT","NOV","PL","NZ","NC"};
    return k[cc & 15];
}

/// Name (Großbuchstaben) → Bedingungskode inkl. Synonyme (EQ NE ULT UGE PE PO), -1 = keiner.
inline int ccFromName(const std::string& up) {
    static const struct { const char* n; int v; } k[] = {
        {"F",0},{"LT",1},{"LE",2},{"ULE",3},{"OV",4},{"PE",4},{"MI",5},{"Z",6},{"EQ",6},
        {"C",7},{"ULT",7},{"T",8},{"GE",9},{"GT",10},{"UGT",11},{"NOV",12},{"PO",12},
        {"PL",13},{"NZ",14},{"NE",14},{"NC",15},{"UGE",15}};
    for (auto& e : k) if (up == e.n) return e.v;
    return -1;
}

/// Steuerregister nach Kodierung (LDCTL); nichtsegmentiert heißen 5/7 PSAP/NSP.
inline const char* ctlName(unsigned code, bool seg) {
    switch (code & 7) {
    case 2: return "FCW";
    case 3: return "REFRESH";
    case 4: return "PSAPSEG";
    case 5: return seg ? "PSAPOFF" : "PSAP";
    case 6: return "NSPSEG";
    case 7: return seg ? "NSPOFF" : "NSP";
    default: return nullptr;
    }
}

inline int ctlFromName(const std::string& up) {
    static const struct { const char* n; int v; } k[] = {
        {"FCW",2},{"REFRESH",3},{"PSAPSEG",4},{"PSAPOFF",5},{"PSAP",5},
        {"NSPSEG",6},{"NSPOFF",7},{"NSP",7}};
    for (auto& e : k) if (up == e.n) return e.v;
    return -1;
}

} // namespace z8k
