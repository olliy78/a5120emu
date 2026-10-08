#include "core/peripherals/p8000_terminal_hw/tastatur_k7673.h"

#include <algorithm>

#include "core/peripherals/p8000_terminal_hw/rom_k7673.h"
#include "core/util/zustand.h"

namespace k1520::p8000 {

namespace {
// Rahmen eines Bytes (Firmware 0240H/0285H, gemessen in internen Takten ab dem ersten P3 = C0H)
constexpr uint64_t R_DATEN0 = 36, R_TAKT0 = 632, R_START = 974, R_BIT = 522,
                   R_DATEN = 144, R_TIEF = 180;
constexpr uint64_t SENDE_VERSATZ = 30;   ///< Zeile gelesen → Sendebeginn (Aufruf 021FH)
}  // namespace

TastaturK7673::TastaturK7673(const Config& cfg) : cfg_(cfg) {
    const uint8_t* r = cfg_.rom ? cfg_.rom : K7673_09;
    rom_.assign(r, r + 2048);
    if (cfg_.quarzHz < 2) cfg_.quarzHz = 8'000'000;
    einschalten();
}

void TastaturK7673::einschalten() {
    scan_ = {}; kandidat_ = {}; gueltig_ = {};
    zaehler38_ = 0; zeile_ = 0;
    letzte_ = 0; held1D_ = false; ledMerker_ = 0; p2_ = 0x8F;
    z0D_ = 0; z0C_ = 0;
    puffer_.clear();
    flanken_.clear();
    log_.clear();
    jetzt_ = 0;
    flanken_.push_back({0, true, true});     // Reset: P3 = Port-Vorgabe
    flanken_.push_back({65, false, false});  // 001FH: LD P3,#00H
    lTakt_ = false; lDaten_ = false;
    einreihen(0xAA);                         // 0043H: Selbsttest gut
    naechst_ = INIT_TAKTE;                   // erste Zeile
    tick_ = TICK_TAKTE;
}

void TastaturK7673::druecke(int z, int s) {
    if (z >= 0 && z < ZEILEN && s >= 0 && s < SPALTEN) matrix_[size_t(z)] |= uint16_t(1u << s);
}
void TastaturK7673::loslassen(int z, int s) {
    if (z >= 0 && z < ZEILEN && s >= 0 && s < SPALTEN) matrix_[size_t(z)] &= uint16_t(~(1u << s));
}
void TastaturK7673::allesLoslassen() { matrix_.fill(0); }
bool TastaturK7673::gedrueckt(int z, int s) const {
    return z >= 0 && z < ZEILEN && s >= 0 && s < SPALTEN && ((matrix_[size_t(z)] >> s) & 1);
}

uint8_t TastaturK7673::merker(int z, int s) const { return rom(adresse(z * 16 + s)); }
uint8_t TastaturK7673::code(int z, int s) const { return rom(uint16_t(adresse(z * 16 + s) + 1)); }

std::vector<uint8_t> TastaturK7673::makeFolge(int z, int s) const {
    std::vector<uint8_t> f;
    const uint8_t m = merker(z, s), c = code(z, s);
    if (m & 8) {
        uint16_t a = uint16_t(((m & 7) << 8) | c);
        for (int n = (m >> 4) ? (m >> 4) : 256; n > 0; --n) f.push_back(rom(a++));
    } else {
        if (m & 1) f.push_back(0xE0);
        f.push_back(c);
    }
    return f;
}

void TastaturK7673::einreihen(uint8_t b) {
    if (puffer_.size() >= 17) return;                 // 35H = 35H: voll
    puffer_.push_back(puffer_.size() == 16 ? uint8_t(0xFF) : b);
}

void TastaturK7673::senden(uint8_t b) {
    const uint64_t s = jetzt_;
    auto f = [&](uint64_t t, bool takt, bool daten) {
        if (lTakt_ == takt && lDaten_ == daten) return;
        lTakt_ = takt; lDaten_ = daten;
        flanken_.push_back({t, takt, daten});
    };
    f(s, true, true);
    f(s + R_DATEN0, true, false);
    f(s + R_TAKT0, false, false);
    f(s + R_START, true, false);                      // Startbit an der steigenden Flanke
    for (int i = 0; i < 8; ++i) {
        const uint64_t basis = s + R_START + R_BIT * uint64_t(i);
        const bool d = !((b >> i) & 1);
        f(basis + R_DATEN, true, d);
        f(basis + R_TIEF, false, d);
        f(basis + R_BIT, true, d);
    }
    f(s + BYTE_TAKTE, false, true);                   // Ruhe: P3 = 80H
    log_.push_back(b);
}

void TastaturK7673::ausgeben(uint16_t adr, bool brk) {
    const uint8_t r6 = brk ? 0x80 : 0x00;
    auto led = [&](uint8_t m) {
        if (brk) { ledMerker_ = uint8_t(ledMerker_ & ~m); return; }
        if (ledMerker_ & m) return;
        ledMerker_ = uint8_t(ledMerker_ | m);
        p2_ = uint8_t(p2_ ^ m);
    };
    if (adr == 0x2EF) held1D_ = !brk;                 // 1DH-Taste gedrückt merken (3AH)
    if (adr == 0x2FF) {                               // PAUSE
        if (brk) return;
        if (held1D_) adr = 0x3ED;
    } else if (adr == 0x3DB) led(0x10);               // ON/OFF
    else if (adr == 0x32F) led(0x20);                 // CAPS LOCK
    else if (adr == 0x3DF) led(0x40);                 // MODE
    const uint8_t m = rom(adr);
    if (m & 0x08) {                                   // Folge
        int n = (m >> 4) ? (m >> 4) : 256;
        uint16_t a = uint16_t(((m & 7) << 8) | rom(uint16_t(adr + 1)));
        for (; n > 0; --n) einreihen(uint8_t(rom(a++) | r6));
        return;
    }
    if (m & 0x01) einreihen(0xE0);
    einreihen(uint8_t(rom(uint16_t(adr + 1)) | r6));
}

uint64_t TastaturK7673::runde() {
    if (zaehler38_ == 0) { kandidat_ = scan_; zaehler38_ = 1; return KOPIE_EXTRA; }
    if (scan_ != kandidat_) { zaehler38_ = 0; return 0; }
    if (zaehler38_ != STABIL) { ++zaehler38_; return 0; }
    zaehler38_ = 0;
    int n = 0;
    for (uint8_t b : kandidat_) for (int i = 0; i < 8; ++i) n += (b >> i) & 1;
    if (n > 3) return AUSWERT_EXTRA;                  // Phantomtasten: verworfen
    for (int i = 0; i < 16; ++i) {
        const uint8_t diff = uint8_t(kandidat_[size_t(i)] ^ gueltig_[size_t(i)]);
        if (!diff) continue;
        const uint8_t neu = uint8_t(diff & kandidat_[size_t(i)]);
        for (int b = 0; b < 8; ++b)
            if (neu & (1u << b)) {
                const uint16_t a = adresse(i * 8 + b);
                letzte_ = a;
                ausgeben(a, false);
                z0D_ = 0;
            }
        const uint8_t weg = uint8_t(diff & gueltig_[size_t(i)]);
        for (int b = 0; b < 8; ++b)
            if (weg & (1u << b)) {
                const uint16_t a = adresse(i * 8 + b);
                if (a == letzte_) letzte_ = uint16_t(letzte_ & 0x00FF);   // CLR 20H
                ausgeben(a, true);
            }
    }
    if ((letzte_ >> 8) != 0 && letzte_ != 0x2FF && z0D_ == VERZOEGERUNG && z0C_ == ABSTAND) {
        ausgeben(letzte_, false);
        z0C_ = 0;
    }
    gueltig_ = kandidat_;
    return AUSWERT_EXTRA;
}

void TastaturK7673::schritt() {
    scan_[size_t(zeile_ * 2)] = uint8_t(matrix_[size_t(zeile_)]);
    scan_[size_t(zeile_ * 2 + 1)] = uint8_t(matrix_[size_t(zeile_)] >> 8);
    if (zeile_ == 7) {
        const uint64_t extra = runde();
        zeile_ = 0;
        naechst_ = jetzt_ + RUNDE_REST + extra;
        return;
    }
    ++zeile_;
    naechst_ = jetzt_ + ZEILE_TAKTE;
    if (!puffer_.empty()) {                           // 021FH: ein Byte aus dem Puffer
        const uint8_t b = puffer_.front();
        puffer_.pop_front();
        const uint64_t zurueck = jetzt_;
        jetzt_ += SENDE_VERSATZ;
        senden(b);
        jetzt_ = zurueck;
        naechst_ += BYTE_TAKTE;
    }
}

void TastaturK7673::laufeBis(uint64_t ziel) {
    for (;;) {
        const uint64_t t = std::min(naechst_, tick_);
        if (t > ziel) break;
        jetzt_ = t;
        if (tick_ <= naechst_) {                      // IRQ5 (0117H)
            if (z0D_ != VERZOEGERUNG) ++z0D_;
            if (z0C_ != ABSTAND) ++z0C_;
            tick_ += TICK_TAKTE;
            continue;
        }
        schritt();
    }
    jetzt_ = std::max(jetzt_, ziel);
}

std::vector<TastaturFlanke> TastaturK7673::holeFlanken(uint64_t bis) {
    std::vector<TastaturFlanke> v;
    while (!flanken_.empty() && flanken_.front().takt <= bis) {
        v.push_back(flanken_.front());
        flanken_.pop_front();
    }
    return v;
}

void TastaturK7673::visit(ZAr& a) {
    for (auto& m : matrix_) a.num(m);
    a.raw(scan_.data(), 16); a.raw(kandidat_.data(), 16); a.raw(gueltig_.data(), 16);
    a.num(zaehler38_); a.num(zeile_); a.flag(lTakt_); a.flag(lDaten_);
    a.num(letzte_); a.flag(held1D_); a.num(ledMerker_); a.num(p2_); a.num(z0D_); a.num(z0C_);
    uint32_t n = uint32_t(puffer_.size());
    a.num(n);
    if (!a.save) { if (n > 17) { a.ok = false; return; } puffer_.resize(n); }
    for (auto& b : puffer_) a.num(b);
    a.num(jetzt_); a.num(naechst_); a.num(tick_);
    n = uint32_t(flanken_.size());
    a.num(n);
    if (!a.save) { if (n > 100000) { a.ok = false; return; } flanken_.resize(n); }
    for (auto& f : flanken_) { a.num(f.takt); a.flag(f.taktPegel); a.flag(f.daten); }
}

void TastaturK7673::serialize(std::vector<uint8_t>& out) const {
    out.push_back(SAVE_VERSION);
    auto a = ZAr::schreiber(out);
    const_cast<TastaturK7673*>(this)->visit(a);
}

bool TastaturK7673::deserialize(const uint8_t*& p, const uint8_t* end) {
    if (p >= end || *p != SAVE_VERSION) return false;
    const uint8_t* q = p + 1;
    auto a = ZAr::leser(q, end);
    visit(a);
    if (!a.ok) return false;
    p = a.p;
    return true;
}

}  // namespace k1520::p8000
