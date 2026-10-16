/**
 * @file dpl.cpp
 * @brief Displaylogik DPL des MC80.2x — Zeichentakt, Bahnen, Tastenmatrix (AP M11).
 * @see dpl.h, doc/mc80/hardware_2x.md §5/§6, doc/mc80/bsys_2x.prn (DPI 00C4H, TSI 00CDH, ANL 0122H)
 */

#include "core/cards/mc80/dpl.h"
#include <algorithm>

namespace {

/// Ersatz-Zeichengenerator [?] (U402 ohne Abzug): 5×7-Satz ASCII 20H–5FH in der Form des 2513,
/// je Zeichen 5 Spalten, Bit 0 = oberste Reihe.  Reihenfolge = ASCII ab 20H.
constexpr uint8_t kFont[64][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00},  // ' ' ! "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},  // # $ %
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1C, 0x22, 0x41, 0x00},  // & ' (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x14, 0x08, 0x3E, 0x08, 0x14}, {0x08, 0x08, 0x3E, 0x08, 0x08},  // ) * +
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00},  // , - .
    {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},  // / 0 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10},  // 2 3 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},  // 5 6 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00},  // 8 9 :
    {0x00, 0x56, 0x36, 0x00, 0x00}, {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},  // ; < =
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x32, 0x49, 0x79, 0x41, 0x3E},  // > ? @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},  // A B C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},  // D E F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},  // G H I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},  // J K L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},  // M N O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},  // P Q R
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},  // S T U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},  // V W X
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x00},  // Y Z [
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7F, 0x00}, {0x04, 0x02, 0x01, 0x02, 0x04},  // \ ] ^
    {0x40, 0x40, 0x40, 0x40, 0x40},                                                                  // _
};

constexpr int kGlyphX = 3;   ///< Lage der Glyphe in der 16 × 32-Zelle
constexpr int kGlyphY = 9;
constexpr int kSkala  = 2;

}  // namespace

Mc80Dpl::Mc80Dpl() : Mc80Dpl(Config{}) {}

Mc80Dpl::Mc80Dpl(const Config& cfg) : cfg_(cfg)
{
    periode_q16_ = (static_cast<uint64_t>(cfg_.zeichentakt_ns) * cfg_.takt_hz * 65536ull) / 1'000'000'000ull;
    if (periode_q16_ == 0) periode_q16_ = 1;
    ctc_.setZCTOCallback([this](int ch, bool pegel) {
        if (pegel) return;
        if (ch == 1) zctoK1();                                  // Ende des Rücklaufs
        else if (ch == 2 && zustand_ == Zustand::Text) ruecklauf();   // Zeilenende (TSI)
    }, 0x06);
    ctc_.setRetiNachIus(true);
    fb_.assign(static_cast<size_t>(BREITE) * HOEHE, 0);
    powerOn();
}

void Mc80Dpl::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, cfg_.io_base, 8);
}

void Mc80Dpl::powerOn()
{
    for (auto& z : zellen_) z.fill(NIE);
    for (auto& k : kurve_) k.clear();
    art_.fill(Art::Leer);
    matrix_.fill(0);
    zeit_ = 0;
    takte_zeichen_ = 0;
    bahnen_ = 0;
    reset();
    geaendert();
}

void Mc80Dpl::reset()
{
    ctc_.reset();
    pio_.reset();
    phase_q16_ = 0;
    a_ = b_ = 0;
    neue_kurve_.clear();
    schritt_ = SCHRITTE;
    ruecklauf();              // nach /RESET steht der Strahl, bis K1 programmiert ist (Notbremse)
    tastaturPins();
    updateInternalChain();
}

// ─── Takt ────────────────────────────────────────────────────────────────────

bool Mc80Dpl::clockTick(int ticks)
{
    bool r = ctc_.clockTick(ticks);
    switch (zustand_) {
        case Zustand::Text:
            phase_q16_ += static_cast<uint64_t>(ticks) << 16;
            while (zustand_ == Zustand::Text && phase_q16_ >= periode_q16_) {
                phase_q16_ -= periode_q16_;
                zeichentakt();
                r = true;
            }
            break;
        case Zustand::Ruecklauf: {
            const uint32_t teiler = std::max<uint32_t>(1, cfg_.k1_teiler);
            rl_acc_ += static_cast<uint32_t>(ticks);
            while (zustand_ == Zustand::Ruecklauf && rl_acc_ >= teiler) {
                rl_acc_ -= teiler;
                puls(1);       // Φ/4 an CLK/TRG1 [?]; ZC/TO1 beendet den Rücklauf (zctoK1)
                r = true;
            }
            if (zustand_ == Zustand::Ruecklauf && zeit_ - rl_seit_ > 4 * (bahnTakte() + 1000)) {
                zustand_ = Zustand::Text;      // Notbremse [?]: K1 nicht programmiert
                phase_q16_ = 0;
            }
            break;
        }
        case Zustand::Analog:
            if (zeit_ + static_cast<uint64_t>(ticks) - kurve_t0_ >= bahnTakte()) {
                kurveAblegen();
                ruecklauf();
                r = true;
            }
            break;
    }
    zeit_ += static_cast<uint64_t>(ticks);
    if (r) updateInternalChain();
    return r;
}

void Mc80Dpl::puls(int kanal)
{
    ctc_.clkTrg(kanal, true);
    ctc_.clkTrg(kanal, false);
}

void Mc80Dpl::zeichentakt()
{
    ++takte_zeichen_;
    // Die Kante zeigt, was jetzt an PIO A liegt; erst danach antwortet die DPI mit dem nächsten.
    if (schritt_ >= 1 && schritt_ <= SPALTEN) {
        uint8_t& z = zellen_[static_cast<size_t>(zeile_)][static_cast<size_t>(schritt_ - 1)];
        const uint8_t code = static_cast<uint8_t>(a_ & 0x3F);
        if (z != code) {
            z = code;
            geaendert();
        }
        if (schritt_ == SPALTEN) ++bahnen_;
    }
    if (schritt_ < SCHRITTE + 1) ++schritt_;
    puls(0);
    puls(2);                  // ZC/TO2 nach 33 Takten → ruecklauf()
}

void Mc80Dpl::ruecklauf()
{
    zustand_ = Zustand::Ruecklauf;
    rl_acc_ = 0;
    rl_seit_ = zeit_;
}

void Mc80Dpl::zctoK1()
{
    if (zustand_ != Zustand::Ruecklauf) return;
    if (a_ & 0x80) {          // Z-Signal analog: Analogbahn (ANL wartet mit HALT auf TI1)
        zustand_ = Zustand::Analog;
        kurve_t0_ = zeit_;
        analog_zeile_ = (zeile_ + 1) & 7;   // ANL ersetzt die auf die letzte Textzeile folgende
        neue_kurve_.clear();
        neue_kurve_.push_back({0, y_akt_});
        schritt_ = SCHRITTE;
    } else {
        zustand_ = Zustand::Text;
        phase_q16_ = 0;       // erster Zeichentakt eine Periode nach dem Zeilenanfang [?]
    }
}

// ─── E/A ─────────────────────────────────────────────────────────────────────

uint8_t Mc80Dpl::ioRead(uint8_t port)
{
    const uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base) & 7;
    const uint8_t r = rel < 4 ? ctc_.ioRead(rel) : pio_.ioRead(static_cast<uint8_t>(rel - 4));
    updateInternalChain();
    return r;
}

void Mc80Dpl::ioWrite(uint8_t port, uint8_t data)
{
    const uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base) & 7;
    if (rel < 4) {
        ctc_.ioWrite(rel, data);
        updateInternalChain();
        return;
    }
    const uint8_t p = static_cast<uint8_t>(rel - 4);
    pio_.ioWrite(p, data);
    if (p == 0) {                       // PIO A Daten: Zeichen / Z-Signal / Zeilenwahl Tastatur
        a_ = data;
    } else if (p == 2) {                // PIO B Daten: Y-Ablenkung
        b_ = data;
        if (zustand_ == Zustand::Analog) {
            const uint64_t x = ((zeit_ - kurve_t0_) << 16) * BREITE / (static_cast<uint64_t>(SCHRITTE) * periode_q16_);
            if (x < static_cast<uint64_t>(BREITE)) neue_kurve_.push_back({static_cast<int>(x), data});
        } else if (a_ & 0x80) {
            y_akt_ = data;              // ANL: „erste Analogpos. einnehmen“
        } else {
            textBahnBeginnen(data);
        }
    }
    tastaturPins();                     // Zeilenwahl oder Betriebsart geändert
    updateInternalChain();
}

void Mc80Dpl::textBahnBeginnen(uint8_t b)
{
    zeile_ = static_cast<int>((static_cast<uint8_t>(~b) >> 5) & 7);
    schritt_ = 0;
    const size_t z = static_cast<size_t>(zeile_);
    if (art_[z] != Art::Text) {
        art_[z] = Art::Text;
        zellen_[z].fill(NIE);
        kurve_[z].clear();
        geaendert();
    }
}

void Mc80Dpl::kurveAblegen()
{
    ++bahnen_;
    const size_t z = static_cast<size_t>(analog_zeile_);
    auto& alt = kurve_[z];
    const bool gleich = art_[z] == Art::Analog && alt.size() == neue_kurve_.size() &&
        std::equal(alt.begin(), alt.end(), neue_kurve_.begin(),
                   [](const Punkt& p, const Punkt& q) { return p.x == q.x && p.y == q.y; });
    if (gleich) return;
    art_[z] = Art::Analog;
    zellen_[z].fill(NIE);
    alt = neue_kurve_;
    geaendert();
}

// ─── Tastatur ────────────────────────────────────────────────────────────────

void Mc80Dpl::tastaturPins()
{
    uint8_t spalten = 0;
    for (int i = 0; i < 7; ++i)
        if (a_ & (0x40 >> i)) spalten |= matrix_[static_cast<size_t>(i)];
    pio_.setExternB(spalten);
}

void Mc80Dpl::setTaste(int nr, bool gedrueckt)
{
    if (nr < 0 || nr > 0x37) return;
    uint8_t& z = matrix_[static_cast<size_t>(nr >> 3)];
    const uint8_t bit = static_cast<uint8_t>(1u << (nr & 7));
    z = gedrueckt ? static_cast<uint8_t>(z | bit) : static_cast<uint8_t>(z & ~bit);
    tastaturPins();
}

bool Mc80Dpl::taste(int nr) const
{
    if (nr < 0 || nr > 0x37) return false;
    return (matrix_[static_cast<size_t>(nr >> 3)] >> (nr & 7)) & 1;
}

void Mc80Dpl::alleTastenLos()
{
    matrix_.fill(0);
    tastaturPins();
}

void Mc80Dpl::breakTaste()
{
    puls(3);
    updateInternalChain();
}

// ─── Interruptkette ──────────────────────────────────────────────────────────

void Mc80Dpl::updateInternalChain()
{
    ctc_.setIEI(iei_in_);
    pio_.setIEI(ctc_.getIEO());
}

void Mc80Dpl::setIEI(bool iei)
{
    iei_in_ = iei;
    updateInternalChain();
}

uint8_t Mc80Dpl::getVector() const
{
    auto& self = const_cast<Mc80Dpl&>(*this);
    uint8_t v = 0xFF;
    if (ctc_.hasInterrupt()) v = self.ctc_.getVector();
    else if (pio_.hasInterrupt()) v = self.pio_.getVector();
    self.updateInternalChain();
    return v;
}

void Mc80Dpl::onRETI()
{
    ctc_.onRETI();
    pio_.onRETI();
    updateInternalChain();
}

bool Mc80Dpl::retiAnnehmen()
{
    if (ctc_.inBedienung()) {
        ctc_.onRETI();                       // setRetiNachIus: höchster Kanal in Bedienung
    } else if (pio_.inBedienung()) {
        pio_.setIEI(true);                   // im ED-Zyklus sperrt nur, wer in Bedienung ist
        pio_.onRETI();
    } else {
        return false;
    }
    updateInternalChain();
    return true;
}

const char* Mc80Dpl::intDeviceName() const
{
    if (ctc_.hasInterrupt()) return "MC80.2x-DPL CTC";
    if (pio_.hasInterrupt()) return "MC80.2x-DPL PIO";
    return "MC80.2x-DPL";
}

// ─── Bild ────────────────────────────────────────────────────────────────────

const uint8_t* Mc80Dpl::glyphe(uint8_t code)
{
    code &= 0x3F;
    const int idx = code < 0x20 ? code + 0x20 : code - 0x20;
    return kFont[idx];
}

uint8_t Mc80Dpl::zelle(int zeile, int spalte) const
{
    if (zeile < 0 || zeile >= ZEILEN || spalte < 0 || spalte >= SPALTEN) return NIE;
    if (art_[static_cast<size_t>(zeile)] != Art::Text) return NIE;
    return zellen_[static_cast<size_t>(zeile)][static_cast<size_t>(spalte)];
}

char Mc80Dpl::zeichen(int zeile, int spalte) const
{
    const uint8_t c = zelle(zeile, spalte);
    if (c == NIE) return ' ';
    return static_cast<char>(c < 0x20 ? c + 0x40 : c);
}

const uint8_t* Mc80Dpl::framebuffer() const
{
    if (fb_revision_ != revision_) {
        zeichne();
        fb_revision_ = revision_;
    }
    return fb_.data();
}

void Mc80Dpl::zeichne() const
{
    std::fill(fb_.begin(), fb_.end(), 0);
    auto setze = [this](int x, int y) {
        if (x >= 0 && x < BREITE && y >= 0 && y < HOEHE) fb_[static_cast<size_t>(y) * BREITE + static_cast<size_t>(x)] = 0xFF;
    };
    for (int z = 0; z < ZEILEN; ++z) {
        const size_t zi = static_cast<size_t>(z);
        if (art_[zi] == Art::Text) {
            for (int s = 0; s < SPALTEN; ++s) {
                const uint8_t code = zellen_[zi][static_cast<size_t>(s)];
                if (code == NIE) continue;
                const uint8_t* g = glyphe(code);
                const int x0 = (s + 1) * ZELLE_B + kGlyphX, y0 = z * BAND_H + kGlyphY;
                for (int c = 0; c < 5; ++c)
                    for (int r = 0; r < 7; ++r)
                        if ((g[c] >> r) & 1)
                            for (int dy = 0; dy < kSkala; ++dy)
                                for (int dx = 0; dx < kSkala; ++dx) setze(x0 + c * kSkala + dx, y0 + r * kSkala + dy);
            }
        } else if (art_[zi] == Art::Analog) {
            const auto& k = kurve_[zi];
            for (size_t i = 0; i < k.size(); ++i) {
                const int y = 255 - k[i].y;
                setze(k[i].x, y);
                if (i + 1 < k.size()) {
                    const int x1 = k[i + 1].x, y1 = 255 - k[i + 1].y;
                    for (int x = k[i].x; x <= x1; ++x) setze(x, y);           // Wert gehalten
                    for (int yy = std::min(y, y1); yy <= std::max(y, y1); ++yy) setze(x1, yy);   // Sprung
                }
            }
        }
    }
}

char Mc80Dpl::zeichenAusBild(int zeile, int spalte) const
{
    if (zeile < 0 || zeile >= ZEILEN || spalte < 0 || spalte >= SPALTEN) return 0;
    const uint8_t* fb = framebuffer();
    const int x0 = (spalte + 1) * ZELLE_B, y0 = zeile * BAND_H;
    for (int code = 0; code < 64; ++code) {
        const uint8_t* g = glyphe(static_cast<uint8_t>(code));
        bool passt = true;
        for (int y = 0; y < BAND_H && passt; ++y) {
            for (int x = 0; x < ZELLE_B && passt; ++x) {
                const int gx = (x - kGlyphX), gy = (y - kGlyphY);
                bool soll = false;
                if (gx >= 0 && gy >= 0 && gx < 5 * kSkala && gy < 7 * kSkala)
                    soll = (g[gx / kSkala] >> (gy / kSkala)) & 1;
                const bool ist = fb[static_cast<size_t>(y0 + y) * BREITE + static_cast<size_t>(x0 + x)] != 0;
                passt = soll == ist;
            }
        }
        if (passt) return static_cast<char>(code < 0x20 ? code + 0x40 : code);
    }
    return 0;
}
