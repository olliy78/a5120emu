#include "core/peripherals/tastatur1715/tastatur1715.h"
#include "core/peripherals/tastatur1715/rom_s600.h"

namespace {
// Codetabelle des S600 je Position {unverschoben, Shift}; Spalte 8 (Sondertasten)
// trägt nur ESC.  Aus doc/pc1715/tastatur.md §6; der Test prüft sie gegen das ROM.
struct Paar { uint8_t n, s; };
constexpr Paar T(char n, char s) { return {uint8_t(n), uint8_t(s)}; }
constexpr Paar G(uint8_t c) { return {c, c}; }
constexpr Paar X = {0, 0};

const Paar kTabelle[Tastatur1715::SPALTEN][Tastatur1715::ZEILEN] = {
    /* 0 */ {T('r','R'), G(0xD0), T('4','$'), G(0xD0), T('v','V'), T('f','F'), G(0xCE), G(0xBD)},
    /* 1 */ {T('e','E'), G(0xAC), T('3','#'), G(0xB3), T('c','C'), T('d','D'), G(0xB9), G(0xB6)},
    /* 2 */ {T('w','W'), G(0xBB), T('2','"'), G(0xB2), T('x','X'), T('s','S'), G(0xB8), G(0xB5)},
    /* 3 */ {T('@','`'), G(0x9E), T('-','='), G(0x9D), G(0x9E), T(':','*'), X,        G(0x8A)},
    /* 4 */ {T('p','P'), X,       T('0','_'), X,       T('/','?'), T(';','+'), G(0xD2), G(0xD1)},
    /* 5 */ {T('[','{'), X,       T('^','~'), G(0x8C), T('z','Z'), T(']','}'), G(0x82), G(0x8B)},
    /* 6 */ {T('u','U'), G(0x8E), T('7','\''),G(0x86), T('m','M'), T('j','J'), G(0x7F), G(0x89)},
    /* 7 */ {T('q','Q'), G(0xB0), T('1','!'), G(0xB1), T('\\','|'),T('a','A'), G(0xB7), G(0xB4)},
    /* 8 */ {X, X, G(0x1B), X, X, X, X, X},
    /* 9 */ {T('i','I'), G(0xCD), T('8','('), G(0xC2), T(',','<'), T('k','K'), G(0xC1), G(0xC0)},
    /*10 */ {T('o','O'), G(0xD3), T('9',')'), G(0xD4), T('.','>'), T('l','L'), G(0xCF), G(0x83)},
    /*11 */ {T('t','T'), G(0xA3), T('5','%'), G(0xA2), T('b','B'), T('g','G'), G(0xA0), G(0xA1)},
    /*12 */ {T('y','Y'), G(0x20), T('6','&'), G(0x88), T('n','N'), T('h','H'), G(0x8D), G(0x87)},
};

constexpr int SHIFT_L_SP = 8, SHIFT_L_ZE = 1;
}  // namespace

Tastatur1715::Tastatur1715(const uint8_t* rom, uint32_t tastaturHz, uint32_t rechnerHz)
    : rom_(rom ? rom : PC1715_S600_TASTATUR), tastaturHz_(tastaturHz), rechnerHz_(rechnerHz) {
    // ROM über den ganzen Adressraum gespiegelt, kein RAM (Schreiben findet nicht statt)
    cpu_.readByte  = [this](uint16_t a) -> uint8_t { return rom_[a & 0x7FF]; };
    cpu_.writeByte = [](uint16_t, uint8_t) {};
    cpu_.readPort  = [this](uint16_t p) -> uint8_t { return lesePort(p); };
    cpu_.writePort = [this](uint16_t, uint8_t v) { bit(v & 1); };  // je OUT ein Takt = ein Bit
    reset();
}

void Tastatur1715::reset() {
    cpu_.reset();
    ledSiSo_ = ledLock_ = false;
    cpuTakte_ = hostTakte_ = hostSumme_ = zeitLetztesByte_ = 0;
    rahmenZustand_ = 0;
    rahmenByte_ = 0;
    letztesBit_ = true;
}

uint8_t Tastatur1715::lesePort(uint16_t p) {
    if (p & 0x8000) {
        // Spaltenabfrage: real genau eine Spalte; mehrere werden verodert
        uint8_t r = 0;
        for (int c = 0; c < SPALTEN; c++)
            if (p & (1u << c)) r |= matrix_[c];
        return r;
    }
    // Leuchtenwert gilt nur bei A15 = 0 (die Abfrage-INs tragen A13 = A14 = 0);
    // den Rückgabewert verwirft das ROM.
    ledSiSo_ = (p >> 13) & 1;
    ledLock_ = (p >> 14) & 1;
    return 0xFF;
}

void Tastatur1715::bit(bool b) {
    if (bitTrace) bitTrace(b, cpuTakte_);
    switch (rahmenZustand_) {
    case 0:
        if (!b && letztesBit_) { rahmenZustand_ = 1; rahmenByte_ = 0; }
        break;
    case 9:  // Stoppbit; 0 = Rahmenfehler, Byte verwerfen
        rahmenZustand_ = 0;
        if (b) {
            zeitLetztesByte_ = cpuTakte_ * rechnerHz_ / tastaturHz_;
            if (byteOut) byteOut(rahmenByte_);
        }
        break;
    default:  // Datenbit 1..8, LSB zuerst
        if (b) rahmenByte_ |= uint8_t(1u << (rahmenZustand_ - 1));
        rahmenZustand_++;
        break;
    }
    letztesBit_ = b;
}

void Tastatur1715::press(int c, int r) {
    if (c >= 0 && c < SPALTEN && r >= 0 && r < ZEILEN) matrix_[c] |= uint8_t(1u << r);
}
void Tastatur1715::release(int c, int r) {
    if (c >= 0 && c < SPALTEN && r >= 0 && r < ZEILEN) matrix_[c] &= uint8_t(~(1u << r));
}
void Tastatur1715::releaseAll() {
    for (auto& m : matrix_) m = 0;
}
bool Tastatur1715::isPressed(int c, int r) const {
    return c >= 0 && c < SPALTEN && r >= 0 && r < ZEILEN && ((matrix_[c] >> r) & 1);
}

void Tastatur1715::runTastaturTakte(uint64_t takte) {
    const uint64_t ziel = cpuTakte_ + takte;
    while (cpuTakte_ < ziel) {
        int n = cpu_.step();
        cpuTakte_ += n > 0 ? n : 1;
    }
    hostTakte_ = hostSumme_ = cpuTakte_ * rechnerHz_ / tastaturHz_;
}

void Tastatur1715::run(uint64_t hostTakte) {
    // Ziel aus der Summe der Rechnertakte, nicht je Aufruf gerundet; die
    // Überschreitung der letzten Instruktion holt der nächste Aufruf wieder ein.
    hostSumme_ += hostTakte;
    const uint64_t ziel = hostSumme_ * tastaturHz_ / rechnerHz_;
    while (cpuTakte_ < ziel) {
        int n = cpu_.step();
        cpuTakte_ += n > 0 ? n : 1;
    }
    hostTakte_ = hostSumme_;
}

bool Tastatur1715::tasteFuer(char c, Taste& out) {
    // ET (Eingabetaste, 3/4): CP/A biopkbd.mac macht 9EH zu CR; 9DH ist die Cursortaste <-'.
    // (3,1) trägt im ROM ebenfalls ET, hat aber keine Taste (tastatur.md §6) — deshalb fest (3,4).
    if (c == '\r') { out = {3, 4, false}; return true; }
    const uint8_t code = uint8_t(c);
    for (int pass = 0; pass < 2; pass++)  // erst unverschoben, dann mit Shift
        for (int sp = 0; sp < SPALTEN; sp++)
            for (int ze = 0; ze < ZEILEN; ze++) {
                const Paar& p = kTabelle[sp][ze];
                const uint8_t w = pass ? p.s : p.n;
                if (w == 0 || w != code) continue;
                out = {sp, ze, pass == 1};
                return true;
            }
    return false;
}

bool Tastatur1715::pressKeyFor(char c) {
    Taste t;
    if (!tasteFuer(c, t)) return false;
    // Shift zuerst und einzeln: zwei im selben Durchlauf neu erkannte Tasten
    // sendet das ROM nicht (Fehlbedienungsunterdrückung).
    if (t.shift) {
        press(SHIFT_L_SP, SHIFT_L_ZE);
        runTastaturTakte(4 * DURCHLAUF_TAKTE);
    }
    press(t.spalte, t.zeile);
    return true;
}

void Tastatur1715::tippe(const std::string& text) {
    for (char c : text) {
        if (!pressKeyFor(c)) continue;
        runTastaturTakte(6 * DURCHLAUF_TAKTE);   // Entprellung (3 Durchläufe) plus Reserve für die Phase
        releaseAll();
        runTastaturTakte(4 * DURCHLAUF_TAKTE);   // Pause: Rahmen laufen aus, Taste gilt als losgelassen
    }
}

// ── Save-State ───────────────────────────────────────────────────────────────
namespace {
void put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(v & 0xFF); o.push_back(v >> 8); }
void put64(std::vector<uint8_t>& o, uint64_t v) { for (int i = 0; i < 8; i++) o.push_back(uint8_t(v >> (8 * i))); }
bool get8(const uint8_t*& p, const uint8_t* e, uint8_t& v) { if (p >= e) return false; v = *p++; return true; }
bool get16(const uint8_t*& p, const uint8_t* e, uint16_t& v) {
    uint8_t a, b;
    if (!get8(p, e, a) || !get8(p, e, b)) return false;
    v = uint16_t(a | b << 8);
    return true;
}
bool get64(const uint8_t*& p, const uint8_t* e, uint64_t& v) {
    v = 0;
    for (int i = 0; i < 8; i++) { uint8_t b; if (!get8(p, e, b)) return false; v |= uint64_t(b) << (8 * i); }
    return true;
}
}  // namespace

void Tastatur1715::serialize(std::vector<uint8_t>& o) const {
    // Das ROM nutzt weder Interrupts noch Speicher: Register, Takte, Matrix und
    // Rahmenzustand genügen.
    for (uint16_t r : {cpu_.AF, cpu_.BC, cpu_.DE, cpu_.HL, cpu_.AF_, cpu_.BC_, cpu_.DE_, cpu_.HL_,
                       cpu_.IX, cpu_.IY, cpu_.PC, cpu_.SP})
        put16(o, r);
    o.push_back(cpu_.I);
    o.push_back(cpu_.R);
    put64(o, cpuTakte_);
    put64(o, hostSumme_);
    put64(o, zeitLetztesByte_);
    for (uint8_t m : matrix_) o.push_back(m);
    o.push_back(uint8_t(ledSiSo_ | ledLock_ << 1 | letztesBit_ << 2));
    o.push_back(uint8_t(rahmenZustand_));
    o.push_back(rahmenByte_);
}

bool Tastatur1715::deserialize(const uint8_t*& p, const uint8_t* e) {
    uint16_t* regs[] = {&cpu_.AF, &cpu_.BC, &cpu_.DE, &cpu_.HL, &cpu_.AF_, &cpu_.BC_, &cpu_.DE_,
                        &cpu_.HL_, &cpu_.IX, &cpu_.IY, &cpu_.PC, &cpu_.SP};
    for (auto* r : regs)
        if (!get16(p, e, *r)) return false;
    uint8_t fl, z;
    if (!get8(p, e, cpu_.I) || !get8(p, e, cpu_.R)) return false;
    if (!get64(p, e, cpuTakte_) || !get64(p, e, hostSumme_) || !get64(p, e, zeitLetztesByte_)) return false;
    for (auto& m : matrix_)
        if (!get8(p, e, m)) return false;
    if (!get8(p, e, fl) || !get8(p, e, z) || !get8(p, e, rahmenByte_)) return false;
    ledSiSo_ = fl & 1;
    ledLock_ = fl & 2;
    letztesBit_ = fl & 4;
    rahmenZustand_ = z;
    hostTakte_ = hostSumme_;
    return true;
}
