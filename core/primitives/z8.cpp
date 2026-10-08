/**
 * @file z8.cpp
 * @brief Z8-Einchiprechner — Ausführung, Registerdatei, Ports, Zähler, UART, Interrupts.
 *
 * Datenblattstellen (Zilog UM0016) und Annahmen [Z8-…]: doc/p8000/z8_abdeckung.md.
 *
 * @license MIT
 */
#include "core/primitives/z8.h"
#include "core/primitives/z8/z8_table.h"
#include "core/util/zustand.h"

#include <algorithm>
#include <limits>

namespace {
constexpr uint8_t TMR_LOAD0 = 0x01, TMR_EN0 = 0x02, TMR_LOAD1 = 0x04, TMR_EN1 = 0x08;
constexpr uint8_t P3M_P2PP = 0x01, P3M_HS0 = 0x04, P3M_HS2 = 0x20, P3M_SER = 0x40, P3M_PAR = 0x80;

inline unsigned teiler(uint8_t pre) { unsigned p = pre >> 2; return p ? p : 64; }
inline bool paritaetUngerade7(uint8_t v) {   // Anzahl Einsen in Bit 0–6 ungerade?
    v &= 0x7F; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return (v & 1) != 0;
}
}  // namespace

Z8::Z8(const Config& cfg) : cfg_(cfg) {
    p01m_ = cfg_.p01mReset;
    for (int i = 0; i < 4; ++i) { lastOut_[i] = 0; lastDrv_[i] = 0; }
}

// ═════════════════════════════════════════════════════════════════════════════
// Reset
// ═════════════════════════════════════════════════════════════════════════════

void Z8::setResetLine(bool aktiv) {
    if (aktiv && !resetLine_) { resetPending_ = true; }
    resetLine_ = aktiv;
}

void Z8::reset() { setResetLine(true); setResetLine(false); }

void Z8::tuReset() {
    // UM0016 Table 12/13: GPR bleiben, Steuerregister wie folgt.
    pc = 0x000C;
    rp = 0;
    irqReg = 0;
    irqEin_ = false;
    imr = uint8_t(imr & 0x7F);
    tmr_ = 0;
    t_[0].pre = uint8_t(t_[0].pre & ~0x01);
    t_[1].pre = uint8_t(t_[1].pre & ~0x03);
    t_[0].getriggert = t_[1].getriggert = false;
    lief_[0] = lief_[1] = false;
    tout_ = true;
    p01m_ = cfg_.p01mReset;
    p2m_ = 0xFF;
    p3m_ = 0;
    out_[3] = uint8_t(out_[3] | 0xF0);
    txSr_ = 0; txDiv_ = 0; txBits_ = 0;
    rxStart_ = false; rxDiv_ = 0; rxBits_ = 0; rxSr_ = 0; rxAlt_ = true;
    for (int i = 0; i < 3; ++i) { hsGelesen_[i] = true; hsVoll_[i] = false; hsDav_[i] = true; }
    halt_ = stop_ = false;
    ausgaengeMelden();
}

// ═════════════════════════════════════════════════════════════════════════════
// Ablauf
// ═════════════════════════════════════════════════════════════════════════════

int Z8::step() {
    waits_ = 0;
    if (resetLine_) {                       // /RESET gehalten: Takt läuft, nichts geschieht
        takte += 1;
        befehlsEnde_ = takte;
        syncTo(takte);
        return 1;
    }
    if (resetPending_) {                    // Start 5–10 Takte nach dem Loslassen (UM0016 S. 35)
        resetPending_ = false;
        befehlsEnde_ = takte;
        syncTo(takte);
        tuReset();
        takte += 5;
        syncTo(takte);
        return 5;
    }
    if (stop_) { takte += 6; befehlsEnde_ = takte; syncTo(takte); return 6; }

    int c;
    const int n = hoechsteAnforderung();
    if (n >= 0) {
        befehlsEnde_ = takte + 24;
        halt_ = false;
        c = interruptAnnehmen(n);
    } else if (halt_) {
        c = 6;
        befehlsEnde_ = takte + 6;
    } else {
        lastPc_ = pc;
        // Zugriffe des Befehls wirken zur Zeit „Befehlsende" (Annahme [Z8-T1]).
        befehlsEnde_ = takte + 6;           // vorläufig für das Holen
        const uint8_t op = holen();
        befehlsEnde_ = takte + z8::tabelle()[op].takte;
        c = ausfuehren(op);
    }
    c += waits_;
    takte += uint64_t(c);
    syncTo(takte);
    return c;
}

uint64_t Z8::laufeBis(uint64_t t) {
    uint64_t n = 0;
    while (takte < t) { step(); ++n; }
    return n;
}

// ═════════════════════════════════════════════════════════════════════════════
// Interrupts
// ═════════════════════════════════════════════════════════════════════════════

int Z8::hoechsteAnforderung() const {
    if (!(imr & 0x80)) return -1;
    const uint8_t p = uint8_t(irqReg & imr & 0x3F);
    if (!p) return -1;
    // UM0016 Figure 96, Table 19/20.  Gruppen A = IRQ5/IRQ3, B = IRQ2/IRQ0, C = IRQ1/IRQ4.
    int A[2] = {5, 3}, B[2] = {2, 0}, C[2] = {1, 4};
    if (ipr & 0x20) std::swap(A[0], A[1]);
    if (ipr & 0x04) std::swap(B[0], B[1]);
    if (ipr & 0x02) std::swap(C[0], C[1]);
    const int* g[3];
    switch (((ipr >> 2) & 0x06) | (ipr & 0x01)) {   // Bit 4, 3, 0
    case 2: g[0] = A; g[1] = B; g[2] = C; break;
    case 3: g[0] = A; g[1] = C; g[2] = B; break;
    case 4: g[0] = B; g[1] = C; g[2] = A; break;
    case 5: g[0] = C; g[1] = B; g[2] = A; break;
    case 6: case 7: g[0] = B; g[1] = A; g[2] = C; break;   // 111 reserviert [Z8-I2]
    default: g[0] = C; g[1] = A; g[2] = B; break;          // 001; 000 reserviert [Z8-I2]
    }
    for (auto* gr : g)
        for (int i = 0; i < 2; ++i)
            if (p & (1u << gr[i])) return gr[i];
    return -1;
}

int Z8::interruptAnnehmen(int n) {
    const uint16_t alt = pc;
    if (cfg_.scheinholenBeiInterrupt && pc >= cfg_.programmbus)
        (void)progLesen(pc, Z8Zugriff::Scheinholen);   // Figure 102: Opcode verworfen
    imr = uint8_t(imr & 0x7F);
    irqReg = uint8_t(irqReg & ~(1u << n));
    pushW(pc);
    pushB(flags);
    const uint16_t v = uint16_t(2 * n);
    const uint8_t hi = progLesen(v, Z8Zugriff::Vektor);
    const uint8_t lo = progLesen(uint16_t(v + 1), Z8Zugriff::Vektor);
    pc = uint16_t(hi << 8 | lo);
    ++irqZahl_[n];
    if (onInterrupt) onInterrupt(n, alt, pc);
    return 24;                           // UM0016 S. 107: 24 interne Takte
}

// ═════════════════════════════════════════════════════════════════════════════
// Zähler T0/T1 und UART
// ═════════════════════════════════════════════════════════════════════════════

bool Z8::t1Intern() const {
    if (t_[1].pre & 0x02) return true;          // PRE1.1 = 1: interner Takt, TIN aus
    switch ((tmr_ >> 4) & 3) {
    case 0: return false;                       // externer Takt an TIN
    case 1: return (pinIn_[3] & 0x02) != 0;     // Torbetrieb: zählt bei TIN = H
    default: return t_[1].getriggert;           // (Re-)Trigger: läuft nach der Flanke
    }
}

bool Z8::laeuft(int n) const {
    if (n == 0) return (tmr_ & TMR_EN0) != 0;
    return (tmr_ & TMR_EN1) && t1Intern();
}

void Z8::laufPruefen(uint64_t jetzt, bool anker0, bool anker1) {
    for (int n = 0; n < 2; ++n) {
        const bool l = laeuft(n);
        if (l && (!lief_[n] || (n == 0 ? anker0 : anker1)))
            t_[n].naechster = jetzt + 4;        // erste Zählung 4 Takte später (UM0016 S. 83)
        lief_[n] = l;
    }
}

void Z8::syncTo(uint64_t t) {
    for (;;) {
        int w = -1;
        uint64_t nx = std::numeric_limits<uint64_t>::max();
        for (int n = 0; n < 2; ++n)
            if (lief_[n] && t_[n].naechster < nx) { nx = t_[n].naechster; w = n; }
        if (w < 0 || nx > t) break;
        t_[w].naechster += 4;
        zaehlerTakt(w, nx);
    }
    if (t > periTakt_) periTakt_ = t;
}

void Z8::zaehlerTakt(int n, uint64_t zeit) {
    Z8Zaehler& z = t_[n];
    if (z.vorteiler > 1) { --z.vorteiler; return; }
    z.vorteiler = uint8_t(teiler(z.pre));       // Vorteiler läuft immer im Dauerbetrieb
    z.zaehler = uint16_t((z.zaehler - 1) & 0xFF);
    if (z.zaehler == 0) endwert(n, zeit);
}

void Z8::endwert(int n, uint64_t zeit) {
    Z8Zaehler& z = t_[n];
    ++z.abgelaufen;
    const unsigned toutSel = (tmr_ >> 6) & 3;
    if (toutSel == unsigned(n + 1)) { tout_ = !tout_; ausgaengeMelden(); }
    if (n == 0 && (p3m_ & P3M_SER)) uartTakt(zeit);
    else anfordern(4 + n);
    if (z.pre & 0x01) {
        z.zaehler = z.anfang;                   // Dauerbetrieb: Anfangswert neu
    } else {
        tmr_ = uint8_t(tmr_ & ~(n == 0 ? TMR_EN0 : TMR_EN1));   // Einzeldurchlauf: steht bei 00
        z.getriggert = false;
        laufPruefen(zeit, false, false);
    }
}

void Z8::uartTakt(uint64_t zeit) {
    // Sender: ein Bit je 16 T0-Endwerte (UM0016 S. 120).
    if (txBits_) {
        if (++txDiv_ >= 16) {
            txDiv_ = 0;
            txSr_ = uint16_t(txSr_ >> 1);
            if (--txBits_ == 0) {
                txSr_ = 0;
                anfordern(4);
                if (uartGesendet) uartGesendet(txByte_);
            }
            ausgaengeMelden();
        }
    }
    // Empfänger: 16-fache Abtastung, Startbitprüfung in der Bitmitte (UM0016 S. 118).
    const bool L = p30Quelle ? p30Quelle(zeit) : (pinIn_[3] & 0x01) != 0;
    if (!rxStart_) {
        if (rxAlt_ && !L) { rxStart_ = true; rxDiv_ = 0; rxBits_ = 0; rxSr_ = 0; }
        rxAlt_ = L;
        return;
    }
    ++rxDiv_;
    if (rxBits_ == 0) {
        if (rxDiv_ < 8) return;
        rxDiv_ = 0;
        if (L) { rxStart_ = false; rxAlt_ = L; return; }    // falsches Startbit
        rxBits_ = 1;
        return;
    }
    if (rxDiv_ < 16) return;
    rxDiv_ = 0;
    if (rxBits_ <= 8) {
        if (L) rxSr_ = uint16_t(rxSr_ | (1u << (rxBits_ - 1)));
        ++rxBits_;
        return;
    }
    // Stoppbitmitte: Zeichen fertig (keine Rahmenprüfung, UM0016 S. 119)
    uint8_t d = uint8_t(rxSr_);
    if (p3m_ & P3M_PAR) {
        const bool einsen = paritaetUngerade7(d) != ((d & 0x80) != 0);   // ungerade Gesamtzahl?
        d = uint8_t((d & 0x7F) | (einsen ? 0x00 : 0x80));                 // Bit 7 = Paritätsfehler
    }
    rxBuf_ = d;
    rxStart_ = false;
    rxAlt_ = L;
    anfordern(3);
    if (uartEmpfangen) uartEmpfangen(d);
}

void Z8::t1Flanke() {
    if (t_[1].pre & 0x02) return;               // interner Takt: TIN ohne Wirkung auf T1
    const uint64_t jetzt = std::max(periTakt_, takte);
    switch ((tmr_ >> 4) & 3) {
    case 0:                                     // externer Takt: Flanke = Vorteilertakt
        if (tmr_ & TMR_EN1) zaehlerTakt(1, jetzt);
        break;
    case 1: break;                              // Tor schliesst (laufPruefen beim Pegel)
    case 2:                                     // Trigger: nur die erste Flanke
        if ((tmr_ & TMR_EN1) && !t_[1].getriggert) {
            t_[1].zaehler = t_[1].anfang; t_[1].vorteiler = uint8_t(teiler(t_[1].pre));
            t_[1].getriggert = true;
            laufPruefen(jetzt, false, true);
        }
        break;
    case 3:                                     // Retrigger: jede Flanke lädt neu
        if (tmr_ & TMR_EN1) {
            t_[1].zaehler = t_[1].anfang; t_[1].vorteiler = uint8_t(teiler(t_[1].pre));
            t_[1].getriggert = true;
            laufPruefen(jetzt, false, true);
        }
        break;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Ports
// ═════════════════════════════════════════════════════════════════════════════

int Z8::hsRichtung(int port) const {
    switch (port) {
    case 0: {
        if (!(p3m_ & P3M_HS0)) return -1;
        const unsigned m = p01m_ >> 6;
        return m == 1 ? 0 : m == 0 ? 1 : -1;
    }
    case 1: {
        if ((p3m_ & 0x18) != 0x18) return -1;
        const unsigned m = (p01m_ >> 3) & 3;
        return m == 1 ? 0 : m == 0 ? 1 : -1;
    }
    case 2:
        if (!(p3m_ & P3M_HS2)) return -1;
        return (p2m_ & 0x80) ? 0 : 1;
    default: return -1;
    }
}

uint8_t Z8::pinsVon(int port, uint8_t maske) const {
    (void)maske;
    return pinIn_[port & 3];
}

uint8_t Z8::p3Ausgang() const {
    uint8_t v = uint8_t(out_[3] & 0xF0);
    auto setz = [&v](int bit, bool an) { v = an ? uint8_t(v | (1u << bit)) : uint8_t(v & ~(1u << bit)); };
    // P34: /DM (inaktiv = 1 ausserhalb der Zyklen) bzw. RDY1/DAV1
    const unsigned m34 = (p3m_ >> 3) & 3;
    if (m34 == 1 || m34 == 2) setz(4, true);
    else if (m34 == 3) { const int r = hsRichtung(1); if (r == 0) setz(4, !hsVoll_[1]); else if (r == 1) setz(4, hsDav_[1]); }
    // P35: RDY0/DAV0
    if (p3m_ & P3M_HS0) { const int r = hsRichtung(0); if (r == 0) setz(5, !hsVoll_[0]); else if (r == 1) setz(5, hsDav_[0]); }
    // P36: RDY2/DAV2 bzw. TOUT
    if (p3m_ & P3M_HS2) { const int r = hsRichtung(2); if (r == 0) setz(6, !hsVoll_[2]); else setz(6, hsDav_[2]); }
    else {
        const unsigned ts = (tmr_ >> 6) & 3;
        if (ts == 1 || ts == 2) setz(6, tout_);
        else if (ts == 3) setz(6, true);        // interner Takt am Pin [Z8-T5]
    }
    // P37: serieller Ausgang (ruhend 1)
    if (p3m_ & P3M_SER) setz(7, txBits_ == 0 || (txSr_ & 1));
    return v;
}

uint8_t Z8::portTreibt(int port) const {
    switch (port & 3) {
    case 0: {
        uint8_t m = 0;
        if ((p01m_ & 3) != 1) m |= 0x0F;        // Ausgang oder Adresse
        if ((p01m_ >> 6) != 1) m |= 0xF0;
        return m;
    }
    case 1: return ((p01m_ >> 3) & 3) == 0 ? 0xFF : 0x00;   // AD/hochohmig: nur im Zyklus
    case 2: {
        const uint8_t aus = uint8_t(~p2m_);
        return (p3m_ & P3M_P2PP) ? aus : uint8_t(aus & ~out_[2]);   // offener Drain treibt nur 0
    }
    default: return 0xF0;
    }
}

uint8_t Z8::portPegel(int port) const {
    port &= 3;
    switch (port) {
    case 0: {
        uint8_t v = 0;
        const unsigned lo = p01m_ & 3, hi = p01m_ >> 6;
        v |= lo >= 2 ? uint8_t((lastAddr_ >> 8) & 0x0F) : lo == 0 ? uint8_t(out_[0] & 0x0F) : uint8_t(pinIn_[0] & 0x0F);
        v |= hi >= 2 ? uint8_t((lastAddr_ >> 8) & 0xF0) : hi == 0 ? uint8_t(out_[0] & 0xF0) : uint8_t(pinIn_[0] & 0xF0);
        return v;
    }
    case 1: return ((p01m_ >> 3) & 3) == 0 ? out_[1] : pinIn_[1];
    case 2: {
        const uint8_t aus = uint8_t(~p2m_);
        uint8_t ov = (p3m_ & P3M_P2PP) ? out_[2] : uint8_t(out_[2] & pinIn_[2]);
        return uint8_t((ov & aus) | (pinIn_[2] & ~aus));
    }
    default: return uint8_t((pinIn_[3] & 0x0F) | (p3Ausgang() & 0xF0));
    }
}

void Z8::ausgaengeMelden() {
    if (!portAusgang) return;
    for (int p = 0; p < 4; ++p) {
        const uint8_t t = portTreibt(p), v = portPegel(p);
        if (t != lastDrv_[p] || ((v ^ lastOut_[p]) & t)) {
            lastDrv_[p] = t; lastOut_[p] = v;
            portAusgang(p, v, t);
        }
    }
}

uint8_t Z8::portLesen(int port) {
    port &= 3;
    if (port == 3) return uint8_t((pinIn_[3] & 0x0F) | (p3Ausgang() & 0xF0));
    // Eingangsmaske
    uint8_t ein = 0;
    if (port == 0) { if ((p01m_ & 3) == 1) ein |= 0x0F; if ((p01m_ >> 6) == 1) ein |= 0xF0; }
    else if (port == 1) { if (((p01m_ >> 3) & 3) == 1) ein = 0xFF; }
    else { ein = p2m_; if (!(p3m_ & P3M_P2PP)) ein = uint8_t(ein | (~p2m_ & out_[2])); }   // offener Drain: 1 liest den Pin
    uint8_t pins = pinIn_[port];
    if (ein && portEingang) pins = portEingang(port, ein);
    uint8_t v = uint8_t(portPegel(port) & ~ein);
    if (port == 2 && !(p3m_ & P3M_P2PP)) v = uint8_t(v & ~(~p2m_ & out_[2]));
    v = uint8_t(v | (pins & ein));
    // Handshake-Eingang: gelatchte Daten
    if (hsRichtung(port) == 0) {
        uint8_t hm = port == 2 ? p2m_ : ein;
        if (hsVoll_[port]) v = uint8_t((v & ~hm) | (hsLatch_[port] & hm));
        hsGelesen_[port] = true;
        const int davBit = port == 0 ? 2 : port == 1 ? 3 : 1;
        if (hsVoll_[port] && (pinIn_[3] & (1u << davBit))) { hsVoll_[port] = false; ausgaengeMelden(); }
    }
    return v;
}

void Z8::portSchreiben(int port, uint8_t v) {
    port &= 3;
    if (port == 3) out_[3] = uint8_t((out_[3] & 0x0F) | (v & 0xF0));   // P30–P33 sind Eingänge
    else out_[port] = v;
    if (port < 3 && hsRichtung(port) == 1) {
        const int rdyBit = port == 0 ? 2 : port == 1 ? 3 : 1;
        if (pinIn_[3] & (1u << rdyBit)) hsDav_[port] = false;   // DAV aktiv nur bei RDY = H
    }
    ausgaengeMelden();
}

void Z8::p3Flanke(int bit, bool neu) {
    if (!neu) {   // fallende Flanke
        switch (bit) {
        case 0: if (!(p3m_ & P3M_SER)) anfordern(3); break;
        case 1: anfordern(2); t1Flanke(); break;
        case 2: anfordern(0); break;
        case 3: anfordern(1); break;
        }
    }
    // Handshake: Port 0 an P32, Port 1 an P33, Port 2 an P31
    const int port = bit == 2 ? 0 : bit == 3 ? 1 : bit == 1 ? 2 : -1;
    if (port >= 0) {
        const int r = hsRichtung(port);
        if (r == 0) {
            if (!neu && !hsVoll_[port]) {                 // DAV ↓: Daten latchen, RDY ↓
                uint8_t pins = pinIn_[port];
                if (portEingang) pins = portEingang(port, 0xFF);
                hsLatch_[port] = pins;
                hsVoll_[port] = true;
                hsGelesen_[port] = false;
            } else if (neu && hsVoll_[port] && hsGelesen_[port]) {
                hsVoll_[port] = false;                    // DAV ↑ nach dem Lesen: RDY ↑
            }
        } else if (r == 1 && !neu) {
            hsDav_[port] = true;                          // RDY ↓: DAV ↑
        }
    }
    ausgaengeMelden();
}

void Z8::setPort(int port, uint8_t pegel) {
    port &= 3;
    syncTo(std::max(periTakt_, takte));
    const uint8_t alt = pinIn_[port];
    pinIn_[port] = pegel;
    if (port == 3) {
        const uint8_t d = uint8_t((alt ^ pegel) & 0x0F);
        for (int b = 0; b < 4; ++b)
            if (d & (1u << b)) p3Flanke(b, (pegel >> b) & 1);
        if (d & 0x02) laufPruefen(std::max(periTakt_, takte), false, false);   // Tor
    }
}

void Z8::setPin(int port, int bit, bool pegel) {
    port &= 3;
    uint8_t v = pinIn_[port];
    v = pegel ? uint8_t(v | (1u << bit)) : uint8_t(v & ~(1u << bit));
    setPort(port, v);
}

// ═════════════════════════════════════════════════════════════════════════════
// Registerdatei
// ═════════════════════════════════════════════════════════════════════════════

uint8_t Z8::regLesen(uint8_t a) {
    if (a <= 3) { sync(); return portLesen(a); }
    if (a <= cfg_.gprOben) return reg[a];
    if (a < 0xF0) return 0xFF;                      // nicht vorhanden
    if (a <= 0xFB) sync();
    switch (a) {
    case 0xF0: return rxBuf_;
    case 0xF1: return tmr_;
    case 0xF2: return uint8_t(t_[1].zaehler);
    case 0xF4: return uint8_t(t_[0].zaehler);
    case 0xFA: return irqEin_ ? uint8_t(irqReg & 0x3F) : 0;
    case 0xFB: return imr;
    case 0xFC: return flags;
    case 0xFD: return rp;
    case 0xFE: return uint8_t(sp >> 8);
    case 0xFF: return uint8_t(sp);
    default: return 0xFF;                           // F3, F5–F9: nur schreibbar (UM0016 S. 81)
    }
}

uint8_t Z8::regSicht(uint8_t a) const {
    if (a <= 3) return portPegel(a);
    if (a <= cfg_.gprOben) return reg[a];
    if (a < 0xF0) return 0xFF;
    switch (a) {
    case 0xF0: return rxBuf_;
    case 0xF1: return tmr_;
    case 0xF2: return uint8_t(t_[1].zaehler);
    case 0xF3: return t_[1].pre;
    case 0xF4: return uint8_t(t_[0].zaehler);
    case 0xF5: return t_[0].pre;
    case 0xF6: return p2m_;
    case 0xF7: return p3m_;
    case 0xF8: return p01m_;
    case 0xF9: return ipr;
    case 0xFA: return irqEin_ ? uint8_t(irqReg & 0x3F) : 0;
    case 0xFB: return imr;
    case 0xFC: return flags;
    case 0xFD: return rp;
    case 0xFE: return uint8_t(sp >> 8);
    default: return uint8_t(sp);
    }
}

void Z8::regSchreiben(uint8_t a, uint8_t v) {
    if (a <= 3) { sync(); portSchreiben(a, v); return; }
    if (a <= cfg_.gprOben) { reg[a] = v; return; }
    if (a < 0xF0) return;
    if (a <= 0xFB) sync();
    const uint64_t jetzt = std::max(periTakt_, befehlsEnde_);
    switch (a) {
    case 0xF0:
        txByte_ = v;
        if (p3m_ & P3M_SER) {
            uint8_t d = v;
            if (p3m_ & P3M_PAR) d = uint8_t((d & 0x7F) | (paritaetUngerade7(d) ? 0x00 : 0x80));
            txSr_ = uint16_t((uint16_t(d) << 1) | (3u << 9));   // Start 0, 8 Daten, 2 Stopp
            txBits_ = 11;
            txDiv_ = 0;                                         // ÷16 neu synchronisiert
            ausgaengeMelden();
        }
        break;
    case 0xF1: {
        tmr_ = uint8_t(v & ~(TMR_LOAD0 | TMR_LOAD1));           // Ladebits lesen sich als 0
        const bool l0 = v & TMR_LOAD0, l1 = v & TMR_LOAD1;
        const unsigned ts = (v >> 6) & 3;
        if (l0) {
            t_[0].zaehler = t_[0].anfang; t_[0].vorteiler = uint8_t(teiler(t_[0].pre));
            if (ts == 1) tout_ = true;
        }
        if (l1) {
            t_[1].zaehler = t_[1].anfang; t_[1].vorteiler = uint8_t(teiler(t_[1].pre));
            if (ts == 2) tout_ = true;
        }
        if (!(v & TMR_EN1)) t_[1].getriggert = false;
        laufPruefen(jetzt, l0, l1);
        ausgaengeMelden();
        break;
    }
    case 0xF2: t_[1].anfang = v; break;
    case 0xF3: t_[1].pre = v; laufPruefen(jetzt, false, false); break;
    case 0xF4: t_[0].anfang = v; break;
    case 0xF5: t_[0].pre = v; break;
    case 0xF6: p2m_ = v; ausgaengeMelden(); break;
    case 0xF7:
        if (!(v & P3M_SER)) { txSr_ = 0; txBits_ = 0; txDiv_ = 0; rxStart_ = false; rxDiv_ = 0; rxBits_ = 0; }
        p3m_ = v;
        ausgaengeMelden();
        break;
    case 0xF8: p01m_ = v; ausgaengeMelden(); break;
    case 0xF9: ipr = v; break;
    case 0xFA: if (irqEin_) irqReg = uint8_t(v & 0x3F); break;   // vor dem ersten EI gesperrt
    case 0xFB: imr = v; break;
    case 0xFC: flags = v; break;
    case 0xFD: rp = v; break;
    case 0xFE: sp = uint16_t((sp & 0x00FF) | (v << 8)); break;
    default:   sp = uint16_t((sp & 0xFF00) | v); break;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Bus und Speicher
// ═════════════════════════════════════════════════════════════════════════════

uint16_t Z8::pinAdresse(uint16_t a) const {
    uint16_t hi = 0;
    const unsigned lo = p01m_ & 3, ho = p01m_ >> 6;
    const uint8_t p0 = lo == 0 || ho == 0 ? out_[0] : 0;
    hi |= lo >= 2 ? uint16_t(a & 0x0F00) : uint16_t(((lo == 0 ? p0 : pinIn_[0]) & 0x0F) << 8);
    hi |= ho >= 2 ? uint16_t(a & 0xF000) : uint16_t(((ho == 0 ? p0 : pinIn_[0]) & 0xF0) << 8);
    return uint16_t(hi | (a & 0xFF));
}

uint8_t Z8::extLesen(uint16_t a, bool daten, Z8Zugriff art) {
    sync();
    Z8BusZyklus c;
    c.adresse = pinAdresse(a);
    c.lesen = true;
    c.datenspeicher = daten;
    const unsigned m34 = (p3m_ >> 3) & 3;
    c.dm = daten && (m34 == 1 || m34 == 2);
    c.art = art;
    c.hochohmig = ((p01m_ >> 3) & 3) == 3;
    c.erweitert = (p01m_ & 0x20) != 0;
    c.takt = befehlsEnde_;
    lastAddr_ = c.adresse;
    if (c.erweitert) waits_ += 1;
    uint8_t v = 0xFF;
    if (!c.hochohmig && busLesen) v = busLesen(c);
    lastCycle_ = c; lastData_ = v;
    return v;
}

void Z8::extSchreiben(uint16_t a, bool daten, Z8Zugriff art, uint8_t v) {
    sync();
    Z8BusZyklus c;
    c.adresse = pinAdresse(a);
    c.lesen = false;
    c.datenspeicher = daten;
    const unsigned m34 = (p3m_ >> 3) & 3;
    c.dm = daten && (m34 == 1 || m34 == 2);
    c.art = art;
    c.hochohmig = ((p01m_ >> 3) & 3) == 3;
    c.erweitert = (p01m_ & 0x20) != 0;
    c.takt = befehlsEnde_;
    lastAddr_ = c.adresse;
    if (c.erweitert) waits_ += 1;
    if (!c.hochohmig && busSchreiben) busSchreiben(c, v);
    lastCycle_ = c; lastData_ = v;
}

uint8_t Z8::progLesen(uint16_t a, Z8Zugriff art) {
    if (a < cfg_.programmbus) return programmLesen ? programmLesen(a) : 0xFF;
    return extLesen(a, false, art);
}

void Z8::progSchreiben(uint16_t a, uint8_t v) {
    if (a < cfg_.programmbus) { ++romWrites_; return; }   // ROM/Programmbus: ohne Wirkung
    extSchreiben(a, false, Z8Zugriff::Konstante, v);
}

uint8_t Z8::holen() {
    const uint8_t v = progLesen(pc, pc == lastPc_ ? Z8Zugriff::Holen : Z8Zugriff::Operand);
    pc = uint16_t(pc + 1);
    return v;
}

void Z8::pushB(uint8_t v) {
    if (stapelIntern()) {
        const uint8_t s = uint8_t(sp - 1);
        sp = uint16_t((sp & 0xFF00) | s);
        regSchreiben(s, v);
    } else {
        sp = uint16_t(sp - 1);
        extSchreiben(sp, true, Z8Zugriff::Stapel, v);
    }
}

uint8_t Z8::popB() {
    if (stapelIntern()) {
        const uint8_t s = uint8_t(sp);
        const uint8_t v = regLesen(s);
        sp = uint16_t((sp & 0xFF00) | uint8_t(s + 1));
        return v;
    }
    const uint8_t v = extLesen(sp, true, Z8Zugriff::Stapel);
    sp = uint16_t(sp + 1);
    return v;
}

// ═════════════════════════════════════════════════════════════════════════════
// Befehle
// ═════════════════════════════════════════════════════════════════════════════

bool Z8::bedingung(unsigned cc) const {
    const bool C = flags & F_C, Z = flags & F_Z, S = flags & F_S, V = flags & F_V;
    switch (cc & 15) {
    case 0x0: return false;
    case 0x1: return S != V;
    case 0x2: return Z || (S != V);
    case 0x3: return C || Z;
    case 0x4: return V;
    case 0x5: return S;
    case 0x6: return Z;
    case 0x7: return C;
    case 0x8: return true;
    case 0x9: return S == V;
    case 0xA: return !(Z || (S != V));
    case 0xB: return !C && !Z;
    case 0xC: return !V;
    case 0xD: return !S;
    case 0xE: return !Z;
    default:  return !C;
    }
}

uint8_t Z8::alu(int mnI, uint8_t d, uint8_t s, bool& schreiben) {
    using z8::Mn;
    const Mn mn = static_cast<Mn>(mnI);
    schreiben = true;
    const unsigned c = (flags & F_C) ? 1 : 0;
    unsigned r;
    switch (mn) {
    case Mn::ADD: case Mn::ADC: {
        const unsigned ci = mn == Mn::ADC ? c : 0;
        r = unsigned(d) + s + ci;
        setF(F_C, r > 0xFF);
        setF(F_V, (~(d ^ s) & (d ^ r) & 0x80) != 0);
        setF(F_D, false);
        setF(F_H, ((d & 15) + (s & 15) + ci) > 15);
        break;
    }
    case Mn::SUB: case Mn::SBC: case Mn::CP: {
        const unsigned ci = mn == Mn::SBC ? c : 0;
        r = unsigned(d) - s - ci;
        setF(F_C, unsigned(d) < unsigned(s) + ci);
        setF(F_V, ((d ^ s) & (d ^ r) & 0x80) != 0);
        if (mn == Mn::CP) { schreiben = false; break; }
        setF(F_D, true);
        setF(F_H, unsigned(d & 15) < unsigned(s & 15) + ci);
        break;
    }
    case Mn::OR:  r = d | s; setF(F_V, false); break;
    case Mn::AND: r = d & s; setF(F_V, false); break;
    case Mn::XOR: r = d ^ s; setF(F_V, false); break;
    case Mn::TCM: r = uint8_t(~d) & s; setF(F_V, false); schreiben = false; break;
    case Mn::TM:  r = d & s; setF(F_V, false); schreiben = false; break;
    default: schreiben = false; return d;
    }
    r &= 0xFF;
    setF(F_Z, r == 0);
    setF(F_S, (r & 0x80) != 0);
    return uint8_t(r);
}

uint8_t Z8::einop(int mnI, uint8_t d) {
    using z8::Mn;
    const Mn mn = static_cast<Mn>(mnI);
    const bool c = flags & F_C;
    uint8_t r = d;
    switch (mn) {
    case Mn::DEC: r = uint8_t(d - 1); setF(F_V, r == 0x7F); break;
    case Mn::INC: r = uint8_t(d + 1); setF(F_V, r == 0x80); break;
    case Mn::COM: r = uint8_t(~d); setF(F_V, false); break;
    case Mn::RL:  r = uint8_t(d << 1 | d >> 7); setF(F_C, d & 0x80); setF(F_V, (d ^ r) & 0x80); break;
    case Mn::RLC: r = uint8_t(d << 1 | (c ? 1 : 0)); setF(F_C, d & 0x80); setF(F_V, (d ^ r) & 0x80); break;
    case Mn::RR:  r = uint8_t(d >> 1 | d << 7); setF(F_C, d & 1); setF(F_V, (d ^ r) & 0x80); break;
    case Mn::RRC: r = uint8_t(d >> 1 | (c ? 0x80 : 0)); setF(F_C, d & 1); setF(F_V, (d ^ r) & 0x80); break;
    case Mn::SRA: r = uint8_t((d & 0x80) | d >> 1); setF(F_C, d & 1); setF(F_V, false); break;
    case Mn::SWAP: r = uint8_t(d << 4 | d >> 4); break;          // C, V undefiniert: bleiben [Z8-F2]
    case Mn::DA: {
        // UM0016 DA-Tabelle; ausserhalb der Tabelle wie die Rechnung [Z8-F1]
        const bool h = flags & F_H;
        unsigned v = d;
        unsigned korr = 0;
        if (h || (d & 15) > 9) korr |= 0x06;
        if (c || d > 0x99) korr |= 0x60;
        if (flags & F_D) { v = d - korr; setF(F_C, c || v > 0xFF); }
        else             { v = d + korr; setF(F_C, c || v > 0xFF); }
        r = uint8_t(v);                                          // V undefiniert: bleibt
        break;
    }
    default: return d;
    }
    setF(F_Z, r == 0);
    setF(F_S, (r & 0x80) != 0);
    return r;
}

int Z8::ausfuehren(uint8_t op) {
    using z8::Mn;
    using z8::Fmt;
    const z8::Insn& in = z8::tabelle()[op];
    int cyc = in.takte;
    const unsigned hi = op >> 4;

    if (in.mn == Mn::Ungueltig || ((in.mn == Mn::HALT || in.mn == Mn::STOP) && !cfg_.haltStop)) {
        ++illegal_;
        if (onIllegal) onIllegal(uint16_t(pc - 1), op);
        return 6;                            // unbelegt: wie NOP [Z8-B1]
    }

    switch (in.mn) {
    // ── Einoperanden (R/IR) ─────────────────────────────────────────────────
    case Mn::DEC: case Mn::RLC: case Mn::INC: case Mn::DA: case Mn::COM: case Mn::RL:
    case Mn::CLR: case Mn::RRC: case Mn::SRA: case Mn::RR: case Mn::SWAP: {
        uint8_t a;
        if (in.fmt == Fmt::r1) a = arbeitsreg(hi);
        else {
            const uint8_t b = holen();
            a = in.fmt == Fmt::IR1 ? regLesen(R(b)) : R(b);
        }
        if (in.mn == Mn::CLR) { regSchreiben(a, 0); break; }
        regSchreiben(a, einop(int(in.mn), regLesen(a)));
        break;
    }
    case Mn::DECW: case Mn::INCW: {
        const uint8_t b = holen();
        const uint8_t a = in.fmt == Fmt::IR1 ? regLesen(R(b)) : R(b);
        const uint16_t v = uint16_t(paarLesen(a) + (in.mn == Mn::INCW ? 1 : -1));
        setF(F_Z, v == 0);
        setF(F_S, (v & 0x8000) != 0);
        setF(F_V, in.mn == Mn::INCW ? v == 0x8000 : v == 0x7FFF);
        paarSchreiben(a, v);
        break;
    }
    case Mn::POP: {
        const uint8_t b = holen();
        const uint8_t a = in.fmt == Fmt::IR1 ? regLesen(R(b)) : R(b);
        regSchreiben(a, popB());
        break;
    }
    case Mn::PUSH: {
        const uint8_t b = holen();
        const uint8_t a = in.fmt == Fmt::IR1 ? regLesen(R(b)) : R(b);
        pushB(regLesen(a));
        if (!stapelIntern()) cyc += 2;       // Karte: 10/12 bzw. 12/14 Takte
        break;
    }
    case Mn::SRP: rp = holen(); break;

    // ── Zweioperanden (Arithmetik/Logik) ────────────────────────────────────
    case Mn::ADD: case Mn::ADC: case Mn::SUB: case Mn::SBC: case Mn::OR: case Mn::AND:
    case Mn::TCM: case Mn::TM: case Mn::CP: case Mn::XOR: {
        uint8_t dst, src;
        switch (in.fmt) {
        case Fmt::r1_r2: { const uint8_t b = holen(); dst = arbeitsreg(b >> 4); src = regLesen(arbeitsreg(b & 15)); break; }
        case Fmt::r1_Ir2: { const uint8_t b = holen(); dst = arbeitsreg(b >> 4); src = regLesen(regLesen(arbeitsreg(b & 15))); break; }
        case Fmt::R2_R1: { const uint8_t s = holen(), d = holen(); src = regLesen(R(s)); dst = R(d); break; }
        case Fmt::IR2_R1: { const uint8_t s = holen(), d = holen(); src = regLesen(regLesen(R(s))); dst = R(d); break; }
        case Fmt::R1_IM: { const uint8_t d = holen(); src = holen(); dst = R(d); break; }
        default /*IR1_IM*/: { const uint8_t d = holen(); src = holen(); dst = regLesen(R(d)); break; }
        }
        bool schreiben;
        const uint8_t r = alu(int(in.mn), regLesen(dst), src, schreiben);
        if (schreiben) regSchreiben(dst, r);  // Ziel FLAGS: das Ergebnis gewinnt [Z8-F3]
        break;
    }

    // ── Laden ───────────────────────────────────────────────────────────────
    case Mn::LD:
        switch (in.fmt) {
        case Fmt::r1_IM: regSchreiben(arbeitsreg(hi), holen()); break;
        case Fmt::r1_R2: { const uint8_t s = holen(); regSchreiben(arbeitsreg(hi), regLesen(R(s))); break; }
        case Fmt::r2_R1: { const uint8_t d = holen(); regSchreiben(R(d), regLesen(arbeitsreg(hi))); break; }
        case Fmt::r1_Ir2: { const uint8_t b = holen(); regSchreiben(arbeitsreg(b >> 4), regLesen(regLesen(arbeitsreg(b & 15)))); break; }
        case Fmt::Ir1_r2: { const uint8_t b = holen(); const uint8_t v = regLesen(arbeitsreg(b & 15)); regSchreiben(regLesen(arbeitsreg(b >> 4)), v); break; }
        case Fmt::R2_R1: { const uint8_t s = holen(), d = holen(); regSchreiben(R(d), regLesen(R(s))); break; }
        case Fmt::IR2_R1: { const uint8_t s = holen(), d = holen(); regSchreiben(R(d), regLesen(regLesen(R(s)))); break; }
        case Fmt::R1_IM: { const uint8_t d = holen(), v = holen(); regSchreiben(R(d), v); break; }
        case Fmt::IR1_IM: { const uint8_t d = holen(), v = holen(); regSchreiben(regLesen(R(d)), v); break; }
        case Fmt::IR1_R2: { const uint8_t s = holen(), d = holen(); const uint8_t v = regLesen(R(s)); regSchreiben(regLesen(R(d)), v); break; }
        case Fmt::r1_X: {   // Basis + Index, ohne Arbeitsregister-Abbildung [Z8-A3]
            const uint8_t b = holen(), x = holen();
            regSchreiben(arbeitsreg(b >> 4), regLesen(uint8_t(x + regLesen(arbeitsreg(b & 15)))));
            break;
        }
        default /*X_r1*/: {
            const uint8_t b = holen(), x = holen();
            const uint8_t v = regLesen(arbeitsreg(b >> 4));
            regSchreiben(uint8_t(x + regLesen(arbeitsreg(b & 15))), v);
            break;
        }
        }
        break;
    case Mn::LDC: case Mn::LDE: {
        const uint8_t b = holen();
        const uint8_t r = arbeitsreg(b >> 4), rr = arbeitsreg(b & 15);
        const uint16_t adr = paarLesen(rr);
        const bool ext = in.mn == Mn::LDE;
        if (in.fmt == Fmt::r1_Irr2)
            regSchreiben(r, ext ? extLesen(adr, true, Z8Zugriff::Extern) : progLesen(adr, Z8Zugriff::Konstante));
        else {
            const uint8_t v = regLesen(r);
            if (ext) extSchreiben(adr, true, Z8Zugriff::Extern, v); else progSchreiben(adr, v);
        }
        break;
    }
    case Mn::LDCI: case Mn::LDEI: {
        const uint8_t b = holen();
        const uint8_t r = arbeitsreg(b >> 4), rr = arbeitsreg(b & 15);
        const uint16_t adr = paarLesen(rr);
        const uint8_t ra = regLesen(r);
        const bool ext = in.mn == Mn::LDEI;
        if (in.fmt == Fmt::Ir1_Irr2) {
            regSchreiben(ra, ext ? extLesen(adr, true, Z8Zugriff::Extern) : progLesen(adr, Z8Zugriff::Konstante));
            regSchreiben(r, uint8_t(ra + 1));
            paarSchreiben(rr, uint16_t(adr + 1));
        } else {
            const uint8_t v = regLesen(ra);
            if (ext) extSchreiben(adr, true, Z8Zugriff::Extern, v); else progSchreiben(adr, v);
            // Überlappen r und rr, entscheidet die Reihenfolge: erst das Paar, dann r [Z8-A4]
            paarSchreiben(rr, uint16_t(adr + 1));
            regSchreiben(r, uint8_t(ra + 1));
        }
        break;
    }

    // ── Programmsteuerung ───────────────────────────────────────────────────
    case Mn::JP:
        if (in.fmt == Fmt::IRR1) { const uint8_t b = holen(); pc = paarLesen(R(b)); }
        else {
            const uint8_t h = holen(), l = holen();
            if (bedingung(hi)) { pc = uint16_t(h << 8 | l); cyc = in.takteSprung; }
        }
        break;
    case Mn::JR: {
        const int8_t ra = int8_t(holen());
        if (bedingung(hi)) { pc = uint16_t(pc + ra); cyc = in.takteSprung; }
        break;
    }
    case Mn::DJNZ: {
        const int8_t ra = int8_t(holen());
        const uint8_t a = arbeitsreg(hi);
        const uint8_t v = uint8_t(regLesen(a) - 1);
        regSchreiben(a, v);
        if (v) { pc = uint16_t(pc + ra); cyc = in.takteSprung; }
        break;
    }
    case Mn::CALL: {
        uint16_t ziel;
        if (in.fmt == Fmt::IRR1) { const uint8_t b = holen(); ziel = paarLesen(R(b)); }
        else { const uint8_t h = holen(), l = holen(); ziel = uint16_t(h << 8 | l); }
        pushW(pc);
        pc = ziel;
        break;
    }
    case Mn::RET: pc = popW(); break;
    case Mn::IRET: flags = popB(); pc = popW(); imr = uint8_t(imr | 0x80); break;

    // ── CPU-Steuerung ───────────────────────────────────────────────────────
    case Mn::DI: imr = uint8_t(imr & 0x7F); break;
    case Mn::EI: imr = uint8_t(imr | 0x80); irqEin_ = true; break;
    case Mn::RCF: setF(F_C, false); break;
    case Mn::SCF: setF(F_C, true); break;
    case Mn::CCF: flags = uint8_t(flags ^ F_C); break;
    case Mn::NOP: break;
    case Mn::HALT: halt_ = true; break;
    case Mn::STOP: stop_ = true; break;
    default: break;
    }
    return cyc;
}

// ═════════════════════════════════════════════════════════════════════════════
// Sicht und Save-State
// ═════════════════════════════════════════════════════════════════════════════

Z8Sicht Z8::sicht() const {
    Z8Sicht s{};
    s.pc = pc; s.sp = sp; s.flags = flags; s.rp = rp; s.imr = imr;
    s.irq = irqEin_ ? uint8_t(irqReg & 0x3F) : 0;
    s.ipr = ipr; s.p01m = p01m_; s.p2m = p2m_; s.p3m = p3m_; s.tmr = tmr_; s.sio = rxBuf_;
    s.irqFreigegeben = irqEin_; s.haltZustand = halt_; s.stopZustand = stop_; s.reset = resetLine_;
    s.t[0] = t_[0]; s.t[1] = t_[1]; s.tout = tout_;
    for (int i = 0; i < 4; ++i) { s.portAus[i] = out_[i]; s.portPins[i] = portPegel(i); }
    s.rxPuffer = rxBuf_; s.txSchieber = txBits_ ? txSr_ : 0; s.txTeiler = txDiv_; s.rxTeiler = rxDiv_;
    s.rxLaeuft = rxStart_; s.rxSchieber = rxSr_;
    s.letzterZyklus = lastCycle_; s.letztesDatum = lastData_;
    s.takte = takte;
    for (int i = 0; i < 6; ++i) s.interrupts[i] = irqZahl_[i];
    return s;
}

void Z8::visit(k1520::ZAr& a) {
    a.num(pc); a.num(flags); a.num(rp); a.num(sp); a.num(imr); a.num(irqReg); a.num(ipr);
    a.raw(reg, sizeof reg);
    a.num(takte);
    a.flag(resetLine_); a.flag(resetPending_); a.flag(halt_); a.flag(stop_); a.flag(irqEin_);
    a.num(periTakt_); a.num(lastPc_);
    a.raw(out_, 4); a.raw(pinIn_, 4); a.raw(hsLatch_, 3);
    for (int i = 0; i < 3; ++i) { a.flag(hsGelesen_[i]); a.flag(hsVoll_[i]); a.flag(hsDav_[i]); }
    a.num(lastAddr_); a.num(p01m_); a.num(p2m_); a.num(p3m_); a.num(tmr_);
    for (auto& z : t_) {
        a.num(z.anfang); a.num(z.pre); a.num(z.zaehler); a.num(z.vorteiler); a.flag(z.getriggert);
        a.num(z.naechster); a.num(z.abgelaufen);
    }
    a.flag(lief_[0]); a.flag(lief_[1]); a.flag(tout_);
    a.num(rxBuf_); a.num(txSr_); a.num(txBits_); a.num(txDiv_); a.num(rxDiv_); a.flag(rxStart_);
    a.num(rxSr_); a.num(rxBits_); a.flag(rxAlt_); a.num(txByte_);
    for (auto& n : irqZahl_) a.num(n);
}

void Z8::serialize(std::vector<uint8_t>& out) const {
    out.push_back(SAVE_VERSION);
    auto ar = k1520::ZAr::schreiber(out);
    const_cast<Z8*>(this)->visit(ar);
}

bool Z8::deserialize(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 1 || *p != SAVE_VERSION) return false;
    const uint8_t* q = p + 1;
    Z8 tmp(cfg_);
    auto ar = k1520::ZAr::leser(q, end);
    tmp.visit(ar);
    if (!ar.ok) return false;
    // Zustand übernehmen, Rückrufe behalten
    auto keep = [&](Z8& d) {
        d.programmLesen = programmLesen; d.busLesen = busLesen; d.busSchreiben = busSchreiben;
        d.portEingang = portEingang; d.portAusgang = portAusgang; d.p30Quelle = p30Quelle;
        d.uartGesendet = uartGesendet; d.uartEmpfangen = uartEmpfangen;
        d.onInterrupt = onInterrupt; d.onIllegal = onIllegal;
    };
    keep(tmp);
    *this = tmp;
    p = ar.p;
    for (int i = 0; i < 4; ++i) { lastOut_[i] = uint8_t(~portPegel(i)); lastDrv_[i] = 0xFF; }
    ausgaengeMelden();
    return true;
}
