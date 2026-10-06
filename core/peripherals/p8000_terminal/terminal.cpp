#include "core/peripherals/p8000_terminal/terminal.h"

#include <algorithm>

#include "core/util/zustand.h"

namespace k1520::p8000 {

namespace {
constexpr uint8_t ESC = 0x1B;

int begrenze(int v, int lo, int hi) { return std::min(std::max(v, lo), hi); }
int tabRechts(int s) { return (s / 8 + 1) * 8; }
int tabLinks(int s) { return s % 8 ? s - s % 8 : s - 8; }
}  // namespace

// ── Bild ────────────────────────────────────────────────────────────────────

uint8_t Terminal::wirksamesAttribut(int z, int s) const {
    uint8_t a = 0;
    for (int i = 0; i <= s; ++i)
        if (bild_[z][i].feld) a = bild_[z][i].attr;
    return a;
}

std::string Terminal::text(int z) const {
    std::string t;
    for (const auto& c : bild_[z]) t += c.feld ? ' ' : static_cast<char>(c.zeichen);
    return t;
}

void Terminal::bildLoeschen() {
    for (auto& zeile : bild_) zeile = Zeile{};
}

void Terminal::rollen() {
    for (int z = 0; z < ZEILEN - 1; ++z) bild_[z] = bild_[z + 1];
    bild_[ZEILEN - 1] = Zeile{};
}

void Terminal::zeilenvorschub() {
    if (z_ < ZEILEN - 1) ++z_; else rollen();
}

void Terminal::weiter() {
    if (++s_ >= SPALTEN) { s_ = 0; zeilenvorschub(); }
}

void Terminal::zeichenLoeschen(int n) {
    n = std::min(n, SPALTEN - s_);
    auto& r = bild_[z_];
    for (int i = s_; i < SPALTEN - n; ++i) r[i] = r[i + n];
    for (int i = SPALTEN - n; i < SPALTEN; ++i) r[i] = TerminalZelle{};
}

void Terminal::zeichenEinfuegen(int n) {
    n = std::min(n, SPALTEN - s_);
    auto& r = bild_[z_];
    for (int i = SPALTEN - 1; i >= s_ + n; --i) r[i] = r[i - n];
    for (int i = s_; i < s_ + n; ++i) r[i] = TerminalZelle{};
}

void Terminal::zeilenLoeschen(int n) {
    n = std::min(n, ZEILEN - z_);
    for (int z = z_; z < ZEILEN - n; ++z) bild_[z] = bild_[z + n];
    for (int z = ZEILEN - n; z < ZEILEN; ++z) bild_[z] = Zeile{};
}

void Terminal::zeilenEinfuegen(int n) {
    n = std::min(n, ZEILEN - z_);
    for (int z = ZEILEN - 1; z >= z_ + n; --z) bild_[z] = bild_[z - n];
    for (int z = z_; z < z_ + n; ++z) bild_[z] = Zeile{};
}

void Terminal::loescheBisZeilenende() {
    for (int i = s_; i < SPALTEN; ++i) bild_[z_][i] = TerminalZelle{};
}

void Terminal::loescheBisSchirmende() {
    loescheBisZeilenende();
    for (int z = z_ + 1; z < ZEILEN; ++z) bild_[z] = Zeile{};
}

void Terminal::loescheBisCursor() {
    for (int z = 0; z < z_; ++z) bild_[z] = Zeile{};
    for (int i = 0; i <= s_; ++i) bild_[z_][i] = TerminalZelle{};
}

void Terminal::meldung() {
    const std::string m = std::string(modus_ == TerminalModus::ADM31 ? "ADM31" : "VT100") +
                          "/9600 baud/Video Attr. " + (video_ ? "on" : "off");
    for (size_t i = 0; i < m.size(); ++i) bild_[0][i].zeichen = static_cast<uint8_t>(m[i]);
    z_ = 1; s_ = 0;
}

void Terminal::neuInitialisieren() {
    bildLoeschen();
    zg2_ = false; programm_ = false; sgr_ = 0; zu_ = Zustand::Boden;
    gz_ = gs_ = 0;
    aus_.clear();
    meldung();
}

// ── Host → Terminal ─────────────────────────────────────────────────────────

void Terminal::attributFeld(uint8_t attr) {
    if (!video_) return;   // „Video Attr. off": Sequenz wird verschluckt
    bild_[z_][s_] = TerminalZelle{' ', attr, true, false};
    weiter();
}

void Terminal::zeichenAusgeben(uint8_t c) {
    bild_[z_][s_] = TerminalZelle{c, 0, false, zg2_};
    weiter();
}

void Terminal::steuerzeichen(uint8_t c) {
    const bool adm = modus_ == TerminalModus::ADM31;
    switch (c) {
    case 0x07: ++klingel_; break;
    case 0x08:
        if (s_ > 0) --s_;
        else if (adm && z_ > 0) { --z_; s_ = SPALTEN - 1; }
        break;
    case 0x09:
        if (adm) {
            const int n = tabRechts(s_);
            if (n >= SPALTEN) { s_ = 0; zeilenvorschub(); } else s_ = n;
        } else {
            s_ = std::min(tabRechts(s_), SPALTEN - 1);
        }
        break;
    case 0x0A: zeilenvorschub(); break;
    case 0x0B: if (z_ > 0) --z_; break;
    case 0x0C:
        if (adm) weiter(); else if (s_ < SPALTEN - 1) ++s_;
        break;
    case 0x0D: s_ = 0; break;
    case 0x1E: z_ = s_ = 0; break;
    case ESC: zu_ = Zustand::Esc; break;
    default: break;   // übrige Steuerzeichen: keine Wirkung
    }
}

void Terminal::eingabe(uint8_t b) {
    switch (zu_) {
    case Zustand::Boden:
        if (b == ESC) zu_ = Zustand::Esc;
        else if (programm_) zeichenAusgeben(b);
        else if (b < 0x20) steuerzeichen(b);
        else if (b != 0x7F) zeichenAusgeben(b);
        break;
    case Zustand::Esc:
        escZeichen(b);
        break;
    case Zustand::EscY:
        escY_ = b - 0x20;
        zu_ = Zustand::EscX;
        break;
    case Zustand::EscX:
        z_ = begrenze(escY_, 0, ZEILEN - 1);
        s_ = begrenze(b - 0x20, 0, SPALTEN - 1);
        zu_ = Zustand::Boden;
        break;
    case Zustand::EscG:
        zu_ = Zustand::Boden;
        switch (b) {
        case '0': attributFeld(0); break;
        case '1': case '3': attributFeld(ATTR_LEER); break;
        case '2': attributFeld(ATTR_BLINK); break;
        case '4': attributFeld(ATTR_INVERS); break;
        case '5': case '7': attributFeld(ATTR_LEER | ATTR_INVERS); break;
        case '6': attributFeld(ATTR_BLINK | ATTR_INVERS); break;
        default: break;
        }
        break;
    case Zustand::Csi:
        if (b == ESC) { zu_ = Zustand::Esc; }
        else if (b < 0x20) steuerzeichen(b);   // Steuerzeichen laufen mitten in der Folge
        else if (b >= '0' && b <= '9') {
            if (par_.empty()) par_.push_back(0);
            par_.back() = std::min(par_.back() * 10 + (b - '0'), 9999);
        } else if (b == ';') {
            if (par_.empty()) par_.push_back(0);
            par_.push_back(0);
        } else if (b == '?' || b == '>' || b == '<' || b == '=') csiPrivat_ = true;
        else if (b >= 0x40 && b <= 0x7E) csiEnde(b);
        break;   // Zwischenzeichen 20H–2FH: ignoriert
    }
}

void Terminal::escZeichen(uint8_t c) {
    zu_ = Zustand::Boden;
    if (c == ESC) { zu_ = Zustand::Esc; return; }

    if (modus_ == TerminalModus::VT100) {
        switch (c) {
        case '[': zu_ = Zustand::Csi; par_.clear(); csiPrivat_ = false; break;
        case 'D': zeilenvorschub(); break;                         // IND
        case 'E': s_ = 0; if (z_ < ZEILEN - 1) ++z_; break;        // NEL (kein Rollen, s. Kopf)
        case 'M':                                                  // RI
            if (z_ > 0) --z_;
            else { const int s = s_; zeilenEinfuegen(1); s_ = s; }
            break;
        case '7': gz_ = z_; gs_ = s_; break;                       // TEKSC
        case '8': z_ = gz_; s_ = gs_; break;                       // TEKRC
        default: break;
        }
        return;
    }

    switch (c) {   // ADM31
    case 'I': {    // CBT: linear, endet an HOME
        const int p = z_ * SPALTEN + s_;
        // Zeilenlänge 80 ist durch 8 teilbar: die Tabstopps des linearen Bildes sind die der Zeile
        const int q = p % 8 ? p - p % 8 : std::max(0, p - 8);
        z_ = q / SPALTEN; s_ = q % SPALTEN;
        break;
    }
    case 'i': {    // CHT: linear, endet am Bildschirmende
        const int p = std::min(z_ * SPALTEN + s_ / 8 * 8 + 8, ZEILEN * SPALTEN - 1);
        z_ = p / SPALTEN; s_ = p % SPALTEN;
        break;
    }
    case 'W': zeichenLoeschen(1); break;
    case 'Q': zeichenEinfuegen(1); break;
    case '=': zu_ = Zustand::EscY; break;
    case 'R': zeilenLoeschen(1); s_ = 0; break;
    case 'T': loescheBisZeilenende(); break;
    case 'E': zeilenEinfuegen(1); s_ = 0; break;
    case 'Y': loescheBisSchirmende(); break;
    case 'u': case 'X': programm_ = false; break;
    case 'U': programm_ = true; break;
    case '*': case ':': bildLoeschen(); break;   // Cursor bleibt
    case 'G': zu_ = Zustand::EscG; break;
    default: break;
    }
}

int Terminal::par(size_t i, int vorgabe) const {
    return i < par_.size() && par_[i] > 0 ? par_[i] : vorgabe;
}

void Terminal::csiEnde(uint8_t f) {
    zu_ = Zustand::Boden;
    if (csiPrivat_) return;
    const int raw = par_.empty() ? 0 : par_[0];
    const int n = par(0, 1);
    switch (f) {
    case 'A': z_ = std::max(0, z_ - n); break;
    case 'B': z_ = std::min(ZEILEN - 1, z_ + n); break;
    case 'C': s_ = std::min(SPALTEN - 1, s_ + n); break;
    case 'D': s_ = std::max(0, s_ - n); break;
    case 'H': case 'f':
        z_ = begrenze(par(0, 1) - 1, 0, ZEILEN - 1);
        s_ = begrenze(par(1, 1) - 1, 0, SPALTEN - 1);
        break;
    case 'I': for (int i = 0; i < n; ++i) s_ = std::min(tabRechts(s_), SPALTEN - 1); break;
    case 'Z': for (int i = 0; i < n; ++i) s_ = std::max(0, tabLinks(s_)); break;
    case 'P': zeichenLoeschen(n); break;
    case '@': zeichenEinfuegen(n); break;
    case 'M': zeilenLoeschen(n); break;
    case 'L': zeilenEinfuegen(n); break;   // vor der Cursorzeile (W6)
    case 'n':
        if (raw == 6)
            aus("\x1b[" + std::to_string(z_ + 1) + ";" + std::to_string(s_ + 1) + "R");
        break;
    case 'J':
        if (raw == 0) loescheBisSchirmende();
        else if (raw == 1) loescheBisCursor();
        else if (raw == 2) bildLoeschen();
        break;
    case 'K':
        if (raw == 0) loescheBisZeilenende();
        else if (raw == 1) for (int i = 0; i <= s_; ++i) bild_[z_][i] = TerminalZelle{};
        else if (raw == 2) bild_[z_] = Zeile{};
        break;
    case 'm':
        if (par_.empty()) par_.push_back(0);
        for (size_t i = 0; i < par_.size() && i < 3; ++i) {
            switch (par_[i]) {
            case 0: sgr_ = 0; break;
            case 1: sgr_ |= ATTR_BOLD; break;
            case 4: sgr_ |= ATTR_UNTERSTRICH; break;
            case 5: sgr_ |= ATTR_BLINK; break;
            case 7: sgr_ |= ATTR_INVERS; break;
            default: break;
            }
        }
        attributFeld(sgr_);
        break;
    default: break;
    }
}

// ── Tastatur ────────────────────────────────────────────────────────────────

void Terminal::zeichenTaste(uint8_t c, bool ctrl) {
    if (ctrl) {
        if (c >= 'a' && c <= 'z') c -= 0x20;
        if (c < 0x40 || c > 0x5F) return;
        c &= 0x1F;
    } else if (caps_ && c >= 'a' && c <= 'z') {
        c -= 0x20;
    }
    if (online_) aus_.push_back(c); else eingabe(c);
}

void Terminal::taste(TerminalTaste t) {
    const bool adm = modus_ == TerminalModus::ADM31;
    auto sende = [this](const std::string& s) {
        if (online_) aus(s); else eingabe(s);
    };
    using T = TerminalTaste;
    switch (t) {
    case T::VT:   sende(adm ? "\x0B" : "\x1b[A"); break;
    case T::LF:   sende(adm ? "\x0A" : "\x1b[B"); break;
    case T::FF:   sende(adm ? "\x0C" : "\x1b[C"); break;   // W2: 0CH
    case T::BS:   sende(adm ? "\x08" : "\x1b[D"); break;
    case T::HOME: sende(adm ? "\x1E" : "\x1b[H"); break;
    case T::HT:   sende("\x09"); break;
    case T::NL:   sende("\x08"); break;
    case T::CR:   sende("\x0D"); break;
    case T::ESC:  sende("\x1b"); break;
    case T::DEL:  sende("\x7F"); break;
    case T::LINE_ERASE:   sende(adm ? "\x1bT" : "\x1b[K"); break;
    case T::PAGE_ERASE:   sende(adm ? "\x1bY" : "\x1b[J"); break;
    case T::LINE_INSERT:  sende(adm ? "\x1bE" : "\x1b[L"); break;
    case T::CHAR_INSERT:  sende(adm ? "\x1bQ" : "\x1b[@"); break;
    case T::LINE_DELETE:  sende(adm ? "\x1bR" : "\x1b[M"); break;   // W3
    case T::CHAR_DELETE:  sende(adm ? "\x1bW" : "\x1b[P"); break;
    case T::TAB:      sende(adm ? "\x09" : "\x1b[I"); break;
    case T::BACKTAB:  sende(adm ? "\x1bI" : "\x1b[Z"); break;
    case T::BREAK: break_ = true; break;
    case T::MODE:
        modus_ = adm ? TerminalModus::VT100 : TerminalModus::ADM31;
        neuInitialisieren();
        break;
    case T::VIDEO: video_ = !video_; neuInitialisieren(); break;
    case T::ON_OFF: online_ = !online_; break;
    case T::SI_SO: zg2_ = !zg2_; break;
    }
}

}  // namespace k1520::p8000

// ─── Save-State ──────────────────────────────────────────────────────────────

void k1520::p8000::Terminal::visit(k1520::ZAr& a)
{
    for (auto& zeile : bild_)
        for (TerminalZelle& c : zeile) { a.num(c.zeichen); a.num(c.attr); a.flag(c.feld); a.flag(c.zg2); }
    a.num(z_); a.num(s_); a.num(gz_); a.num(gs_);
    a.en(modus_);
    a.flag(video_); a.flag(online_); a.flag(programm_); a.flag(zg2_); a.flag(caps_); a.flag(break_);
    a.num(klingel_); a.num(sgr_);
    a.en(zu_);
    a.num(escY_);
    uint32_t n = static_cast<uint32_t>(par_.size());
    a.num(n);
    if (!a.save) { if (!a.ok || n > 64) { a.ok = false; return; } par_.assign(n, 0); }
    for (int& v : par_) a.num(v);
    a.flag(csiPrivat_);
    uint32_t m = static_cast<uint32_t>(aus_.size());
    a.num(m);
    if (a.save) { for (uint8_t b : aus_) a.num(b); return; }
    if (!a.ok || m > 65536) { a.ok = false; return; }
    aus_.clear();
    for (uint32_t i = 0; i < m && a.ok; ++i) { uint8_t b = 0; a.num(b); aus_.push_back(b); }
}

void k1520::p8000::Terminal::serialize(std::vector<uint8_t>& out) const
{
    auto a = k1520::ZAr::schreiber(out);
    const_cast<Terminal*>(this)->visit(a);
}

bool k1520::p8000::Terminal::deserialize(const uint8_t*& p, const uint8_t* end)
{
    auto a = k1520::ZAr::leser(p, end);
    Terminal tmp;
    tmp.visit(a);
    if (!a.ok) return false;
    *this = std::move(tmp);
    p = a.p;
    return true;
}
