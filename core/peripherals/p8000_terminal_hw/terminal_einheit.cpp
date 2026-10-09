#include "core/peripherals/p8000_terminal_hw/terminal_einheit.h"

#include <algorithm>
#include <numeric>

#include "core/peripherals/p8000_terminal_hw/rom_p8t.h"
#include "core/peripherals/tasten_uhr.h"
#include "core/serial/hub.h"
#include "core/util/zustand.h"

namespace k1520::p8000 {

namespace {
constexpr uint16_t NORMAL_TAB = 0x056C;   ///< 89 Byte, Abtastcode 00–58H (p8t.main.s)
constexpr uint16_t SHIFT_TAB = 0x05C5;    ///< 54 Byte, Abtastcode 00–35H
constexpr uint64_t RAHMEN = 11 * P8000TerminalHw::BIT_TAKTE;   ///< 8N2

/// a · z / n ohne Überlauf für große a (z, n gekürzt und < 2^32).
uint64_t skaliere(uint64_t a, uint64_t z, uint64_t n) { return (a / n) * z + (a % n) * z / n; }
}  // namespace

// ── TerminalUartAnschluss ───────────────────────────────────────────────────

serial::SerialFormat TerminalUartAnschluss::format() const {
    serial::SerialFormat f;
    f.baud_nenn = 9600; f.daten = 8; f.paritaet = 0; f.stopp_halbe = 4;
    f.zeichen_takte = RAHMEN;
    f.gueltig = true;
    return f;
}

void TerminalUartAnschluss::breaksAbholen() const {
    auto& hw = e_.hw();
    while (hw.hatAusgabe() && hw.ausgabe().front().brk) {
        const TerminalSendung s = hw.holeAusgabe();
        breakBis_ = std::max(breakBis_, s.takt + RAHMEN);
    }
}

bool TerminalUartAnschluss::senderHatZeichen() const { breaksAbholen(); return e_.hw().hatAusgabe(); }
uint8_t TerminalUartAnschluss::senderNimm() { breaksAbholen(); return e_.hw().holeAusgabe().byte; }
bool TerminalUartAnschluss::empfaengerFrei() const {
    return e_.hw().hostLeitungFrei() <= e_.takte() + 10 * P8000TerminalHw::BIT_TAKTE;
}
void TerminalUartAnschluss::empfange(uint8_t b) { e_.hw().hostByte(b, 10); }
bool TerminalUartAnschluss::breakGesendet() const { breaksAbholen(); return e_.takte() < breakBis_; }

// ── Einheit ─────────────────────────────────────────────────────────────────

P8000TerminalEinheit::P8000TerminalEinheit(const Config& cfg)
    : cfg_(cfg), hw_(cfg.hw), kb_(cfg.tastatur), uart_(*this) {
    const uint64_t kHz = kb_.taktHz(), g = std::gcd<uint64_t>(kHz, Z8_HZ);
    kbdZ_ = kHz / g;
    kbdN_ = Z8_HZ / g;
    kbdStart_ = takte() + uint64_t(cfg_.tastaturVerzugMs) * Z8_HZ / 1000;
}

P8000TerminalEinheit::~P8000TerminalEinheit() = default;

void P8000TerminalEinheit::einschalten() {
    hw_.einschalten();
    kb_.einschalten();
    kbdStart_ = takte() + uint64_t(cfg_.tastaturVerzugMs) * Z8_HZ / 1000;
    aktionen_.clear();
    hubNaechst_ = 0;
}

uint64_t P8000TerminalEinheit::zuKbd(uint64_t z8) const {
    return z8 < kbdStart_ ? 0 : skaliere(z8 - kbdStart_, kbdZ_, kbdN_);
}
uint64_t P8000TerminalEinheit::zuZ8(uint64_t k) const { return kbdStart_ + skaliere(k, kbdN_, kbdZ_); }

// Wiederholungs-Ticks der K7673 aus der Wirtsuhr: Verzögerung (500 ms) und Abstand (100 ms)
// sind Eigenschaften der Tastatur (eigener Quarz), nicht des Rechnertakts.  Die Uhr wird nur
// gelesen, solange eine Wiederholung läuft — dieser Pfad wird je Befehl gerufen.
void P8000TerminalEinheit::echtzeitTicks() {
    if (!kb_.wiederholungLaeuft()) { ezBezug_ = false; return; }
    const uint64_t jetzt = TastenUhr::wirtsuhrNs();
    if (!ezBezug_) { ezNs_ = jetzt; ezBezug_ = true; return; }
    const uint64_t tick = kb_.tickNs();
    uint64_t n = (jetzt - ezNs_) / tick;
    if (!n) return;
    ezNs_ += n * tick;
    kb_.tickExtern(std::min<uint64_t>(n, 1000));   // nach einer Pause kein Schwall
}

void P8000TerminalEinheit::tastaturNachziehen(uint64_t bis) {
    if (bis < kbdStart_) return;                 // Tastatur noch nicht eingeschaltet
    const bool ez = wiederholungEchtzeit();
    kb_.setTickExtern(ez);
    if (ez) echtzeitTicks();
    const uint64_t k = zuKbd(bis);
    kb_.laufeBis(k);
    for (const TastaturFlanke& f : kb_.holeFlanken(k)) {
        const uint64_t t = zuZ8(f.takt);
        if (t > hw_.takte()) hw_.laufeBis(t);
        hw_.tastaturLeitung(f.taktPegel, f.daten);
    }
}

void P8000TerminalEinheit::aktionenAusfuehren() {
    while (!aktionen_.empty() && takte() >= aktionZeit_) {
        const Aktion a = aktionen_.front();
        aktionen_.pop_front();
        if (a.t.gueltig()) {
            if (a.an) kb_.druecke(a.t.zeile, a.t.spalte);
            else kb_.loslassen(a.t.zeile, a.t.spalte);
        }
        if (!aktionen_.empty()) aktionZeit_ += aktionen_.front().warteZ8;
    }
}

void P8000TerminalEinheit::laufeBis(uint64_t ziel) {
    while (takte() < ziel) {
        uint64_t z = ziel;
        if (!aktionen_.empty()) z = std::min(z, std::max(aktionZeit_, takte()));
        if (hub_) z = std::min(z, std::max(hubNaechst_, takte()));
        tastaturNachziehen(z);
        hw_.laufeBis(z);
        if (!aktionen_.empty() && takte() >= aktionZeit_) aktionenAusfuehren();
        if (hub_ && takte() >= hubNaechst_) {
            const uint64_t n = hub_->takt(takte());
            hubNaechst_ = n > takte() ? n : takte() + 1;
        }
    }
}

void P8000TerminalEinheit::matrixTaste(MatrixTaste t, const std::vector<MatrixTaste>& mods) {
    const uint64_t H = uint64_t(cfg_.tasteHalteMs) * Z8_HZ / 1000;
    const uint64_t P = uint64_t(cfg_.tastePauseMs) * Z8_HZ / 1000;
    const bool leer = aktionen_.empty();
    for (const auto& m : mods) aktionen_.push_back({0, m, true});
    aktionen_.push_back({mods.empty() ? 0 : H, t, true});
    aktionen_.push_back({H, t, false});
    for (const auto& m : mods) aktionen_.push_back({P, m, false});
    aktionen_.push_back({P, MatrixTaste{}, false});   // Pause bis zur nächsten Taste
    if (leer) aktionZeit_ = takte() + aktionen_.front().warteZ8;
}

MatrixTaste P8000TerminalEinheit::positionFuer(const std::vector<uint8_t>& folge) const {
    for (int z = 0; z < TastaturK7673::ZEILEN; ++z)
        for (int s = 0; s < TastaturK7673::SPALTEN; ++s)
            if (kb_.makeFolge(z, s) == folge) return {z, s};
    return {};
}

MatrixTaste P8000TerminalEinheit::positionFuer(TerminalTaste t) const {
    using T = TerminalTaste;
    switch (t) {
    case T::VT: return positionFuer({0xE0, 0x48});
    case T::LF: return positionFuer({0xE0, 0x50});
    case T::FF: return positionFuer({0xE0, 0x4D});
    case T::BS: return positionFuer({0xE0, 0x4B});
    case T::HOME: return positionFuer({0xE0, 0x4F});
    case T::HT: case T::TAB: return positionFuer({0x1D});
    case T::CR: return positionFuer({0x1C});
    case T::ESC: return positionFuer({0x01});
    case T::DEL: return positionFuer({0x0E});
    case T::PAGE_ERASE: return positionFuer({0xE0, 0x47});
    case T::LINE_INSERT: return positionFuer({0xE0, 0x51});
    case T::CHAR_INSERT: return positionFuer({0xE0, 0x49});
    case T::LINE_DELETE: return positionFuer({0xE0, 0x53});
    case T::CHAR_DELETE: return positionFuer({0xE0, 0x52});
    case T::BACKTAB: return positionFuer({0x0F});
    case T::BREAK: return positionFuer({0x3E});
    case T::MODE: return positionFuer({0x3C});
    case T::VIDEO: return positionFuer({0x3D});
    case T::ON_OFF: return positionFuer({0x54});
    case T::SI_SO: return positionFuer({0x3B});
    case T::NL: case T::LINE_ERASE: break;   // keine Taste auf der K7673.09
    }
    return {};
}

bool P8000TerminalEinheit::tastenFuerZeichen(uint8_t c, bool ctrl, std::vector<MatrixTaste>& mods,
                                            MatrixTaste& taste) const {
    mods.clear();
    taste = {};
    const MatrixTaste shift = positionFuer({0x2A}), strg = positionFuer({0x38});
    auto suche = [&](uint8_t z, std::vector<MatrixTaste>& m, MatrixTaste& t) {
        for (uint8_t sc = 0; sc <= 0x58; ++sc) {
            if (hw_.programm(uint16_t(NORMAL_TAB + sc)) != z) continue;
            const MatrixTaste p = positionFuer({sc});
            if (p.gueltig()) { t = p; return true; }
        }
        for (uint8_t sc = 0; sc <= 0x35; ++sc) {
            if (hw_.programm(uint16_t(SHIFT_TAB + sc)) != z) continue;
            const MatrixTaste p = positionFuer({sc});
            if (p.gueltig()) { m.push_back(shift); t = p; return true; }
        }
        if (z == '>') {                          // TGETCHAR: SHIFT + 56H
            const MatrixTaste p = positionFuer({0x56});
            if (p.gueltig()) { m.push_back(shift); t = p; return true; }
        }
        return false;
    };
    if (!ctrl && suche(c, mods, taste)) return true;
    if (!ctrl && c < 0x20) {                     // Steuerzeichen über CTRL (AND 9FH)
        const uint8_t basis = (c >= 1 && c <= 0x1A) ? uint8_t(c | 0x60) : uint8_t(c | 0x40);
        std::vector<MatrixTaste> m;
        if (!suche(basis, m, taste)) return false;
        mods.push_back(strg);
        mods.insert(mods.end(), m.begin(), m.end());
        return true;
    }
    if (ctrl) {
        std::vector<MatrixTaste> m;
        if (!suche(c, m, taste)) return false;
        mods.push_back(strg);
        mods.insert(mods.end(), m.begin(), m.end());
        return true;
    }
    return false;
}

bool P8000TerminalEinheit::zeichenTaste(uint8_t c, bool ctrl) {
    std::vector<MatrixTaste> mods;
    MatrixTaste t;
    if (!tastenFuerZeichen(c, ctrl, mods, t)) return false;
    matrixTaste(t, mods);
    return true;
}

bool P8000TerminalEinheit::taste(TerminalTaste t) {
    const MatrixTaste p = positionFuer(t);
    if (!p.gueltig()) return false;
    matrixTaste(p);
    return true;
}

bool P8000TerminalEinheit::matrixDirekt(MatrixTaste t, bool an) {
    if (t.zeile < 0 || t.spalte < 0 || t.zeile >= TastaturK7673::ZEILEN || t.spalte >= TastaturK7673::SPALTEN)
        return false;
    if (an) kb_.druecke(t.zeile, t.spalte);
    else kb_.loslassen(t.zeile, t.spalte);
    return true;
}

void P8000TerminalEinheit::setzeCapsLock(bool an) {
    if (bool(kb_.leds() & 2) == an) return;
    const MatrixTaste p = positionFuer(std::vector<uint8_t>{0x3A});
    if (p.gueltig()) matrixTaste(p);
}

bool P8000TerminalEinheit::tippe(const std::string& s) {
    for (unsigned char c : s)
        if (!zeichenTaste(c)) return false;
    return true;
}

void P8000TerminalEinheit::tastenAbwarten(uint64_t nachMs, uint64_t maxMs) {
    const uint64_t ende = takte() + maxMs * Z8_HZ / 1000;
    while (!tastenFertig() && takte() < ende) laufe(Z8_HZ / 100);
    laufeMs(nachMs);
}

bool P8000TerminalEinheit::ruheAbwarten(uint64_t maxMs) {
    const uint64_t ende = takte() + maxMs * Z8_HZ / 1000;
    int folge = 0;
    while (takte() < ende) {
        laufe(Z8_HZ / 1000);
        folge = (hw_.ruht() && tastenFertig() && !kb_.flankenAnstehend() && kb_.pufferBelegung() == 0) ? folge + 1 : 0;
        if (folge >= 3) return true;
    }
    return false;
}

serial::SerialHub& P8000TerminalEinheit::hub() {
    if (!hub_) {
        hub_ = std::make_unique<serial::SerialHub>(Z8_HZ);
        hub_->registriere(uart_);
        hubNaechst_ = takte();
    }
    return *hub_;
}

void P8000TerminalEinheit::serialize(std::vector<uint8_t>& out) const {
    out.push_back(SAVE_VERSION);
    hw_.serialize(out);
    kb_.serialize(out);
    auto a = ZAr::schreiber(out);
    uint32_t n = uint32_t(aktionen_.size());
    a.num(n);
    for (auto x : aktionen_) { a.num(x.warteZ8); a.num(x.t.zeile); a.num(x.t.spalte); a.flag(x.an); }
    uint64_t az = aktionZeit_, ks = kbdStart_;
    a.num(az); a.num(ks);
}

bool P8000TerminalEinheit::deserialize(const uint8_t*& p, const uint8_t* end) {
    if (p >= end || *p != SAVE_VERSION) return false;
    const uint8_t* q = p + 1;
    if (!hw_.deserialize(q, end) || !kb_.deserialize(q, end)) return false;
    auto a = ZAr::leser(q, end);
    uint32_t n = 0;
    a.num(n);
    if (!a.ok || n > 100000) return false;
    std::deque<Aktion> akt(n);
    for (auto& x : akt) { a.num(x.warteZ8); a.num(x.t.zeile); a.num(x.t.spalte); a.flag(x.an); }
    uint64_t az = 0, ks = 0;
    a.num(az); a.num(ks);
    if (!a.ok) return false;
    kbdStart_ = ks;
    aktionen_ = std::move(akt);
    aktionZeit_ = az;
    hubNaechst_ = takte();
    p = a.p;
    return true;
}

// ── Kopplung an einen Rechnerkanal ──────────────────────────────────────────

TerminalHwKopplung::TerminalHwKopplung(serial::SerialAnschluss& karte, P8000TerminalEinheit& term,
                                       uint64_t phiNenn)
    : karte_(karte), term_(term), phi_(phiNenn ? phiNenn : 4'000'000), ziel_(term.takte()) {}

uint64_t TerminalHwKopplung::zeichenTakte() const {
    const serial::SerialFormat f = karte_.format();
    if (f.gueltig && f.zeichen_takte) return f.zeichen_takte;
    return 11ull * phi_ / 9600;
}

bool TerminalHwKopplung::baudAbweichend() const {
    const serial::SerialFormat f = karte_.format();
    return f.gueltig && (f.baud_nenn != 9600 || f.daten != 8);
}

void TerminalHwKopplung::takt(uint64_t n) {
    auto& hw = term_.hw();
    const serial::SerialFormat f = karte_.format();
    const int bits = f.gueltig ? 1 + f.daten + (f.paritaet ? 1 : 0) + (f.stopp_halbe + 1) / 2 : 11;
    const uint64_t scheibe = std::max<uint64_t>(1, zeichenTakte() / 8);
    while (n > 0) {
        const uint64_t s = std::min(n, scheibe);
        n -= s;
        rxRest_ = rxRest_ > s ? rxRest_ - s : 0;
        const uint64_t q = s * P8000TerminalHw::Z8_HZ + rest_;
        ziel_ = std::max(ziel_ + q / phi_, ziel_);
        rest_ = q % phi_;
        if (ziel_ > term_.takte()) term_.laufeBis(ziel_);
        // Gast → Terminal (Zeichentakt des Gastformats)
        if (rxRest_ == 0 && karte_.senderHatZeichen()) {
            hw.hostByte(karte_.senderNimm(), bits);
            rxRest_ = zeichenTakte();
        }
        // Terminal → Gast (fertige Rahmen), BREAK eine Rahmenzeit
        while (hw.hatAusgabe()) {
            if (hw.ausgabe().front().brk) {
                const TerminalSendung b = hw.holeAusgabe();
                breakBis_ = std::max(breakBis_, b.takt + RAHMEN);
                if (!breakAktiv_) { karte_.breakEmpfang(true); breakAktiv_ = true; }
                continue;
            }
            if (!karte_.empfaengerFrei()) break;
            karte_.empfange(hw.holeAusgabe().byte);
        }
        if (breakAktiv_ && term_.takte() >= breakBis_) { karte_.breakEmpfang(false); breakAktiv_ = false; }
    }
}

void TerminalHwKopplung::serialize(std::vector<uint8_t>& out) const {
    auto a = ZAr::schreiber(out);
    uint64_t r = rest_, z = ziel_, rx = rxRest_, bb = breakBis_;
    bool ba = breakAktiv_;
    a.num(r); a.num(z); a.num(rx); a.num(bb); a.flag(ba);
}

bool TerminalHwKopplung::deserialize(const uint8_t*& p, const uint8_t* end) {
    auto a = ZAr::leser(p, end);
    uint64_t r = 0, z = 0, rx = 0, bb = 0;
    bool ba = false;
    a.num(r); a.num(z); a.num(rx); a.num(bb); a.flag(ba);
    if (!a.ok) return false;
    rest_ = r; ziel_ = z; rxRest_ = rx; breakBis_ = bb; breakAktiv_ = ba;
    p = a.p;
    return true;
}

}  // namespace k1520::p8000
