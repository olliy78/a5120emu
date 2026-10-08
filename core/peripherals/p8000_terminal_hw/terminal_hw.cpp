#include "core/peripherals/p8000_terminal_hw/terminal_hw.h"

#include <algorithm>

#include "core/peripherals/p8000_terminal_hw/rom_p8t.h"
#include "core/util/zustand.h"

namespace k1520::p8000 {

namespace {

uint8_t umkehren(uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i) if (b & (1u << i)) r = uint8_t(r | (0x80u >> i));
    return r;
}

/// Feldattribut 10URGGBH → ATTR_*-Bits des Kern-Terminals (GG ohne Gegenstück).
uint8_t attrVon(uint8_t b) {
    uint8_t a = 0;
    if (b & 0x02) a |= ATTR_BLINK;
    if (b & 0x10) a |= ATTR_INVERS;
    if (b & 0x20) a |= ATTR_UNTERSTRICH;
    if (b & 0x01) a |= ATTR_BOLD;
    return a;
}

/// Ein Teil-Zustand als [Länge u32][Bytes].
template <class F> void blob(ZAr& a, F&& schreiben, std::function<bool(const uint8_t*&, const uint8_t*)> lesen) {
    if (a.save) {
        std::vector<uint8_t> b;
        schreiben(b);
        uint32_t n = uint32_t(b.size());
        a.num(n);
        a.raw(b.data(), b.size());
        return;
    }
    uint32_t n = 0;
    a.num(n);
    if (!a.ok || uint64_t(a.end - a.p) < n) { a.ok = false; return; }
    const uint8_t* q = a.p;
    if (!lesen(q, a.p + n) || q != a.p + n) { a.ok = false; return; }
    a.p += n;
}

}  // namespace

P8000TerminalHw::P8000TerminalHw(const Config& cfg) : cfg_(cfg), z8_(Z8Config::ub8840()) {
    if (cfg_.firmware) fw_.assign(cfg_.firmware, cfg_.firmware + cfg_.firmwareGroesse);
    else fw_.assign(P8T_FW_5_0, P8T_FW_5_0 + sizeof P8T_FW_5_0);
    zg1_.assign(cfg_.zg1 ? cfg_.zg1 : P8T_ZG_EZS, (cfg_.zg1 ? cfg_.zg1 : P8T_ZG_EZS) + 2048);
    zg2v_.assign(cfg_.zg2 ? cfg_.zg2 : P8T_ZG_DZS, (cfg_.zg2 ? cfg_.zg2 : P8T_ZG_DZS) + 2048);
    if (cfg_.zeichentaktTeiler < 1) cfg_.zeichentaktTeiler = 8;
    verdrahten();
    einschalten();
}

void P8000TerminalHw::verdrahten() {
    z8_.programmLesen = [this](uint16_t a) { return programm(a); };
    z8_.busLesen = [this](const Z8BusZyklus& c) { return busLesen(c); };
    z8_.busSchreiben = [this](const Z8BusZyklus& c, uint8_t v) { busSchreiben(c, v); };
    z8_.portAusgang = [this](int port, uint8_t pegel, uint8_t) { portAusgang(port, pegel); };
    z8_.p30Quelle = [this](uint64_t t) { return p30(t); };
    z8_.uartGesendet = [this](uint8_t b) { gesendet(b); };
}

void P8000TerminalHw::einschalten() {
    ram_.fill(0);
    crt_.reset();
    zpuf_ = {};
    zbedient_.fill(false);
    drqZeile_ = -1;
    dmaLauf_ = false;
    dmaAdresse_ = BWS;
    zg2_ = false;
    sr_ = 0; srVoll_ = false; taktAlt_ = false;
    rx_.clear(); aus_.clear();
    p31Ende_ = 0;
    z8_.setPort(2, 0x00);
    z8_.setPin(3, 1, true);    // P31 DRQ (Ruhe high)
    z8_.setPin(3, 2, true);    // P32 VRTC (invertiert)
    z8_.setPin(3, 3, true);    // P33 Byte voll (low-aktiv)
    z8_.reset();
    p3Alt_ = z8_.portPegel(3);
    const uint64_t jetzt = z8_.takte;
    rxFrei_ = jetzt;
    wdLetzt_ = jetzt;
    bildStart_ = jetzt; bildRest_ = 0;
    bildBeginnen();
    pix_.assign(size_t(pixB_) * size_t(pixH_), 0);
}

// ── Bus ────────────────────────────────────────────────────────────────────

uint8_t P8000TerminalHw::busLesen(const Z8BusZyklus& c) {
    if (!c.dm) return 0xFF;
    const uint16_t a = c.adresse;
    if (a >= BWS && a < BWS + 0x800) { dmaAdresse_ = a; return ram_[a - BWS]; }
    if ((a & 0xFFFE) == 0x1C00) {
        const uint8_t v = crt_.read(a & 1);
        if (crtProtokollieren) crtProtokoll.push_back({(a & 1) != 0, v, true});
        return v;
    }
    return 0xFF;
}

void P8000TerminalHw::busSchreiben(const Z8BusZyklus& c, uint8_t v) {
    const uint16_t a = c.adresse;
    if (c.dm) {
        if (a >= BWS && a < BWS + 0x800) { dmaAdresse_ = a; ram_[a - BWS] = v; }
        else if ((a & 0xFFFE) == 0x1C00) {
            if (crtProtokollieren) crtProtokoll.push_back({(a & 1) != 0, v, false});
            crt_.write(a & 1, v);
        }
        return;
    }
    // Dekoder 1D10 (74138) an A15–A13, nur Schreiben ohne /DM (LDC): Scheinausgaben.
    switch (a >> 13) {
    case 1: zg2_ = true; break;                    // 2000H: Zeichensatz 2
    case 2: zg2_ = false; break;                   // 4000H: Zeichensatz 1
    case 4: ++klingel_; break;                     // 8000H: Signalton
    case 6:                                        // C000H: Tastatur-Schieberegister zurück
        sr_ = 0; srVoll_ = false;
        z8_.setPin(3, 3, true);
        break;
    default: break;
    }
}

void P8000TerminalHw::portAusgang(int port, uint8_t pegel) {
    if (port != 3) return;
    const uint8_t alt = p3Alt_;
    p3Alt_ = pegel;
    if ((alt & 0x20) && !(pegel & 0x20) && drqZeile_ >= 0) {   // P35 ↓: DMA ein
        const Geometrie g = geometrie();
        const int r = drqZeile_;
        const unsigned basis = unsigned(dmaAdresse_ - BWS) & 0x7FF;
        for (int i = 0; i < g.spalten; ++i) zpuf_[size_t(r)][size_t(i)] = ram_[(basis + unsigned(i)) & 0x7FF];
        zbedient_[size_t(r)] = true;
        drqZeile_ = -1;
        ++dmaZahl_;
    }
    if (!(alt & 0x40) && (pegel & 0x40)) wdLetzt_ = z8_.takte;  // P36 ↑: VSYN triggert 74123
}

// ── serielle Leitung ─────────────────────────────────────────────────────────

void P8000TerminalHw::hostByte(uint8_t b, int bits) {
    const uint64_t start = std::max(rxFrei_, z8_.takte);
    rx_.push_back({start, b});
    rxFrei_ = start + uint64_t(std::max(bits, 10)) * BIT_TAKTE;
}

size_t P8000TerminalHw::hostWartend() const {
    size_t n = 0;
    for (const auto& r : rx_) if (r.start + 10 * BIT_TAKTE > z8_.takte) ++n;
    return n;
}

bool P8000TerminalHw::p30(uint64_t t) {
    while (!rx_.empty() && t >= rx_.front().start + 10 * BIT_TAKTE) rx_.pop_front();
    if (rx_.empty() || t < rx_.front().start) return true;
    const uint64_t bit = (t - rx_.front().start) / BIT_TAKTE;
    if (bit == 0) return false;
    if (bit <= 8) return ((rx_.front().byte >> (bit - 1)) & 1) != 0;
    return true;
}

void P8000TerminalHw::gesendet(uint8_t b) {
    const Z8Zaehler& t0 = z8_.zaehler(0);
    const uint64_t anfang = t0.anfang ? t0.anfang : 256;
    const uint64_t vor = (t0.pre >> 2) ? (t0.pre >> 2) : 64;
    const uint64_t bitT = 16 * anfang * vor * 4;
    TerminalSendung s;
    s.byte = b;
    s.brk = b == 0 && bitT * 2 > BIT_TAKTE * 3;
    s.takt = z8_.takte;
    s.dauer = 11 * bitT;
    aus_.push_back(s);
}

// ── Tastatur ────────────────────────────────────────────────────────────────

void P8000TerminalHw::tastaturLeitung(bool takt, bool daten) {
    if (takt && !taktAlt_ && !srVoll_) {
        sr_ = uint16_t(((sr_ << 1) | (daten ? 0 : 1)) & 0x1FF);
        if (sr_ & 0x100) tastaturByte(umkehren(uint8_t(sr_)));
    }
    taktAlt_ = takt;
}

void P8000TerminalHw::tastaturByte(uint8_t b) {
    sr_ = uint16_t(0x100 | umkehren(b));
    srVoll_ = true;
    z8_.setPort(2, uint8_t(b << 1 | b >> 7));   // Bitlage: Firmware liest P2 und dreht rechts
    z8_.setPin(3, 3, false);                    // Byte voll → P33 (IRQ1)
}

// ── Video ───────────────────────────────────────────────────────────────────

P8000TerminalHw::Geometrie P8000TerminalHw::geometrie() const {
    Geometrie g{80, 24, 13, 22, 3, 0, 0};
    if (crt_.configured()) {
        g.spalten = std::min(crt_.cols(), 128);
        g.zeilen = std::min(crt_.rows(), 64);
        g.linien = crt_.lineCount();
        g.hrtc = crt_.hrtcChars();
        g.vrtc = crt_.vrtcRows();
    }
    g.zeile = uint64_t(g.linien) * uint64_t(g.spalten + g.hrtc);
    g.bild = uint64_t(g.zeilen + g.vrtc) * g.zeile;
    return g;
}

void P8000TerminalHw::bildBeginnen() {
    const Geometrie g = geometrie();
    zeileZt_ = g.zeile;
    bildZt_ = g.bild;
    zeilenImBild_ = g.zeilen;
    vrtcZeilen_ = g.vrtc;
    naechstes_ = 0;
}

uint64_t P8000TerminalHw::zeitVon(uint64_t zt) const {
    const uint64_t f = uint64_t(cfg_.zeichentaktTeiler) * Z8_HZ;
    return bildStart_ + (zt * f + bildRest_) / cfg_.punkttaktHz;
}

namespace {
/// Ereignisse eines Bildes: 0…R−2 DRQ Zeile nr+1, R−1 VRTC, R DRQ Zeile 0, R+1 Bildende.
uint64_t ereignisZt(int nr, int R, int V, uint64_t zeile) {
    if (nr < R - 1) return uint64_t(nr) * zeile;
    if (nr == R - 1) return uint64_t(R) * zeile;
    if (nr == R) return uint64_t(R + V - 1) * zeile;
    return uint64_t(R + V) * zeile;
}
}  // namespace

void P8000TerminalHw::ereignis(int nr) {
    const int R = zeilenImBild_;
    auto drq = [&](int zeile) {
        // Nach START DISPLAY beginnt der DMA mit dem nächsten Bild: erste Anforderung = Zeile 0
        // in der letzten VRTC-Zeichenzeile (Datenblatt).  Die Firmware zählt ihre Zeilentabelle
        // ab genau dieser Anforderung (IRP31).
        if (!crt_.displayEnabled()) { dmaLauf_ = false; return; }
        if (zeile == 0) dmaLauf_ = true;
        if (!dmaLauf_) return;
        drqZeile_ = zeile;
        ++drqZahl_;
        z8_.setPin(3, 1, false);
        p31Ende_ = 1;
        p31Zeit_ = z8_.takte + 40;
    };
    if (nr < R - 1) drq(nr + 1);
    else if (nr == R - 1) { z8_.setPin(3, 2, false); bildEnde(); }
    else if (nr == R) drq(0);
    else {
        z8_.setPin(3, 2, true);
        const uint64_t f = uint64_t(cfg_.zeichentaktTeiler) * Z8_HZ;
        const uint64_t q = bildZt_ * f + bildRest_;
        bildStart_ += q / cfg_.punkttaktHz;
        bildRest_ = q % cfg_.punkttaktHz;
        bildBeginnen();
    }
}

void P8000TerminalHw::bildEnde() {
    const Geometrie g = geometrie();
    lesePos_ = 0;
    crt_.dmaRead = [this, g]() -> uint8_t {
        const int r = lesePos_ / g.spalten, s = lesePos_ % g.spalten;
        ++lesePos_;
        if (r >= 64 || !zbedient_[size_t(r)]) return 0x80;   // nicht bediente Zeile: leer [A]
        return zpuf_[size_t(r)][size_t(s)];
    };
    crt_.frame();
    zbedient_.fill(false);
    ++bilder_;
    rastern();
}

void P8000TerminalHw::rastern() {
    const int cols = crt_.configured() ? crt_.cols() : 80;
    const int rows = crt_.configured() ? crt_.rows() : 24;
    const int lin = crt_.configured() ? crt_.lineCount() : 13;
    pixB_ = cols * 8;
    pixH_ = rows * lin;
    pix_.assign(size_t(pixB_) * size_t(pixH_), 0);
    const std::vector<uint8_t>& zg = zg2_ ? zg2v_ : zg1_;
    const int ul = crt_.underlineLine();
    const bool blinkAn = crt_.charBlinkOn();
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const I8275::Cell& z = crt_.cells(r, c);
            for (int l = 0; l < lin; ++l) {
                uint8_t bits = 0;
                const bool vsp = z.empty || (z.blink && !blinkAn);
                if (!vsp && !z.charAttr) bits = zg[size_t((z.code & 0x7F) * 16 + (l & 15))];
                if (l == ul && (z.lten || (z.cursor && crt_.cursorUnderline()))) bits = 0xFF;
                if (z.cursor && !crt_.cursorUnderline()) bits = uint8_t(~bits);
                if (z.rvv) bits = uint8_t(~bits);
                uint8_t* zeile = &pix_[size_t(r * lin + l) * size_t(pixB_) + size_t(c * 8)];
                for (int b = 0; b < 8; ++b)
                    if (bits & (0x80 >> b)) zeile[b] = z.hlgt ? 2 : 1;
            }
        }
}

void P8000TerminalHw::watchdogPruefen() {
    if (!cfg_.watchdogMs) return;
    const uint64_t frist = uint64_t(cfg_.watchdogMs) * Z8_HZ / 1000;
    if (z8_.takte - wdLetzt_ < frist) return;
    ++wdResets_;
    wdLetzt_ = z8_.takte;
    z8_.reset();
}

void P8000TerminalHw::laufeBis(uint64_t ziel) {
    const uint64_t frist = cfg_.watchdogMs ? uint64_t(cfg_.watchdogMs) * Z8_HZ / 1000 : 0;
    while (z8_.takte < ziel) {
        uint64_t naechst = ziel;
        naechst = std::min(naechst, zeitVon(ereignisZt(naechstes_, zeilenImBild_, vrtcZeilen_, zeileZt_)));
        if (p31Ende_) naechst = std::min(naechst, p31Zeit_);
        if (frist) naechst = std::min(naechst, wdLetzt_ + frist);
        if (z8_.takte < naechst) z8_.laufeBis(naechst);
        for (;;) {
            const uint64_t t = zeitVon(ereignisZt(naechstes_, zeilenImBild_, vrtcZeilen_, zeileZt_));
            if (z8_.takte < t) break;
            const int nr = naechstes_++;
            ereignis(nr);   // Bildende setzt naechstes_ zurück
        }
        if (p31Ende_ && z8_.takte >= p31Zeit_) { z8_.setPin(3, 1, true); p31Ende_ = 0; }
        watchdogPruefen();
    }
}

// ── Bild lesen ──────────────────────────────────────────────────────────────

uint16_t P8000TerminalHw::zeilenAdresse(int z) const {
    const uint8_t e = ram_[size_t(ZTAB - BWS + (z & 31))];
    return uint16_t((((e & 0x0F) | 0x10) << 8) | (e & 0xF0));
}

uint8_t P8000TerminalHw::bwsByte(int z, int s) const {
    return ram_[size_t((zeilenAdresse(z) - BWS + s) & 0x7FF)];
}

std::string P8000TerminalHw::text(int z) const {
    std::string t;
    for (int s = 0; s < 80; ++s) {
        const uint8_t b = bwsByte(z, s);
        t += (b & 0x80) ? ' ' : char(b);
    }
    return t;
}

TerminalZelle P8000TerminalHw::zelle(int z, int s) const {
    const uint8_t b = bwsByte(z, s);
    TerminalZelle c;
    c.feld = (b & 0xC0) == 0x80;
    c.zeichen = c.feld ? uint8_t(' ') : uint8_t(b & 0x7F);
    c.attr = c.feld ? attrVon(b) : 0;
    c.zg2 = zg2_;
    return c;
}

uint8_t P8000TerminalHw::wirksamesAttribut(int z, int s) const {
    uint8_t a = 0;
    for (int i = 0; i <= s && i < 80; ++i) {
        const uint8_t b = bwsByte(z, i);
        if ((b & 0xC0) == 0x80) a = attrVon(b);
    }
    return a;
}

// ── Save-State ──────────────────────────────────────────────────────────────

void P8000TerminalHw::visit(ZAr& a) {
    blob(a, [this](std::vector<uint8_t>& o) { z8_.serialize(o); },
         [this](const uint8_t*& p, const uint8_t* e) { return z8_.deserialize(p, e); });
    blob(a, [this](std::vector<uint8_t>& o) { crt_.serialize(o); },
         [this](const uint8_t*& p, const uint8_t* e) { return crt_.deserialize(p, e); });
    a.raw(ram_.data(), ram_.size());
    a.num(bildStart_); a.num(bildRest_); a.num(bildZt_); a.num(zeileZt_);
    a.num(zeilenImBild_); a.num(vrtcZeilen_); a.num(naechstes_); a.num(p31Ende_); a.num(p31Zeit_);
    a.num(dmaAdresse_); a.num(drqZeile_); a.flag(dmaLauf_);
    for (auto& z : zpuf_) a.raw(z.data(), z.size());
    for (auto& b : zbedient_) a.flag(b);
    a.num(drqZahl_); a.num(dmaZahl_);
    a.num(p3Alt_); a.flag(zg2_); a.num(klingel_); a.num(wdLetzt_); a.num(wdResets_);
    a.num(sr_); a.flag(srVoll_); a.flag(taktAlt_);
    uint32_t n = uint32_t(rx_.size());
    a.num(n);
    if (!a.save) { if (n > 100000) { a.ok = false; return; } rx_.resize(n); }
    for (auto& r : rx_) { a.num(r.start); a.num(r.byte); }
    a.num(rxFrei_);
    n = uint32_t(aus_.size());
    a.num(n);
    if (!a.save) { if (n > 100000) { a.ok = false; return; } aus_.resize(n); }
    for (auto& s : aus_) { a.num(s.byte); a.flag(s.brk); a.num(s.takt); a.num(s.dauer); }
    a.num(bilder_);
}

void P8000TerminalHw::serialize(std::vector<uint8_t>& out) const {
    out.push_back(SAVE_VERSION);
    auto a = ZAr::schreiber(out);
    const_cast<P8000TerminalHw*>(this)->visit(a);
}

bool P8000TerminalHw::deserialize(const uint8_t*& p, const uint8_t* end) {
    if (p >= end || *p != SAVE_VERSION) return false;
    const uint8_t* q = p + 1;
    auto a = ZAr::leser(q, end);
    visit(a);
    if (!a.ok) return false;
    p = a.p;
    verdrahten();
    // Pins nach dem Zustand neu anlegen (P2 = Register, P33 = voll, P31/P32 folgen den Ereignissen)
    if (srVoll_) { const uint8_t b = umkehren(uint8_t(sr_)); z8_.setPort(2, uint8_t(b << 1 | b >> 7)); }
    z8_.setPin(3, 3, !srVoll_);
    z8_.setPin(3, 1, p31Ende_ == 0);
    z8_.setPin(3, 2, naechstes_ < zeilenImBild_);
    rastern();
    return true;
}

}  // namespace k1520::p8000
