/**
 * @file z80_dma.cpp
 * @brief Z80-DMA (UA858) – Implementierung, siehe z80_dma.h und doc/p8000/z80_dma.md
 */
#include "z80_dma.h"
#include <cstring>
#include <type_traits>

Z80Dma::Z80Dma(const std::string& name) : name_(name) {}

void Z80Dma::reset() {
    // Einschalten/Systemreset: C3 und dazu, was C3 laut Datenblatt NICHT anfasst (Lesesequenz,
    // RDY-Polarität, Betriebsart) — so wie der Baustein nach dem Einschalten dasteht [Annahme:
    // Lesemaske 7FH, Betriebsart Byte, RDY low-aktiv].
    befehlReset();
    s_.wr5 = 0; s_.wr4 &= ~0x60;
    s_.readMask = 0x7F; s_.readPos = 0;
    s_.folgeN = s_.folgeI = 0;
    x_.impulse = 0;
}

void Z80Dma::befehlReset() {
    // C3: Interrupts sperren (IP/IUS zurück), Busanforderung sperren, Auto-Restart und WAIT aus,
    // Zeitverhalten Standard, Force Ready aus.  Lesesequenz und Adressen bleiben (Datenblatt).
    s_.enabled = s_.started = s_.forceReady = false;
    s_.intEnable = s_.intPending = s_.ius = s_.enableAfterReti = false;
    s_.wr5 &= ~0x30; s_.intCtl = 0;
    s_.timingA = s_.timingB = 4;
    x_.variabelA = x_.variabelB = false;
    x_.wr3 &= ~0x60;
    x_.rdyFreigabe = x_.byteFreigabe = x_.nachlauf = false;
    x_.intGrund = 0;
    s_.status = 0x38;
    setEndLevel(false);
}

void Z80Dma::setEndLevel(bool l) {
    if (s_.endLevel == l) return;
    s_.endLevel = l;
    if (blockEnde) blockEnde(l);
}

void Z80Dma::queue(std::initializer_list<uint8_t> f) {
    for (uint8_t x : f) if (s_.folgeN < sizeof(s_.folge)) s_.folge[s_.folgeN++] = x;
}

// ─── Steuerport ──────────────────────────────────────────────────────────────

void Z80Dma::ioWrite(uint8_t, uint8_t d) {
    if (s_.folgeI < s_.folgeN) { nimmFolge(d); pruefeRdyInterrupt(); return; }
    s_.folgeN = s_.folgeI = 0;
    // Jedes Steuerbyte sperrt die DMA, bis 87H oder WR3 D6 sie wieder freigibt (Datenblatt:
    // „the act of writing a control byte to the DMA disables the DMA“).
    if (d != 0x87) s_.enabled = false;
    schreibeWR(d);
    pruefeRdyInterrupt();
}

void Z80Dma::schreibeWR(uint8_t d) {
    if (!(d & 0x80)) {
        if (d & 0x03) {                                   // WR0
            s_.wr0 = d;
            if (d & 0x08) queue({F_A_LO});
            if (d & 0x10) queue({F_A_HI});
            if (d & 0x20) queue({F_LEN_LO});
            if (d & 0x40) queue({F_LEN_HI});
        } else if ((d & 0x07) == 0x04) {                  // WR1
            s_.wr1 = d;
            if (d & 0x40) queue({F_TIMING_A});
        } else if ((d & 0x07) == 0x00) {                  // WR2
            s_.wr2 = d;
            if (d & 0x40) queue({F_TIMING_B});
        }
        return;
    }
    switch (d & 0x03) {
    case 0x00:                                            // WR3
        x_.wr3 = d;
        if (d & 0x08) queue({F_MASK3});
        if (d & 0x10) queue({F_MATCH3});
        if (d & 0x20) s_.intEnable = true;                // ≙ AB
        if (d & 0x40) { s_.enabled = true; s_.started = false; }   // ≙ 87
        break;
    case 0x01:                                            // WR4
        s_.wr4 = d;
        if (d & 0x04) queue({F_B_LO});
        if (d & 0x08) queue({F_B_HI});
        if (d & 0x10) queue({F_INTCTL});
        break;
    case 0x02:                                            // WR5 (D2 = 0), sonst undefiniert [A4]
        if (!(d & 0x04)) s_.wr5 = d;
        break;
    default: befehl(d); break;                            // WR6
    }
}

void Z80Dma::nimmFolge(uint8_t d) {
    switch (s_.folge[s_.folgeI++]) {
    case F_A_LO:   s_.aStart = (s_.aStart & 0xFF00) | d; break;
    case F_A_HI:   s_.aStart = (s_.aStart & 0x00FF) | (d << 8); break;
    case F_LEN_LO: s_.blockLen = (s_.blockLen & 0xFF00) | d; break;
    case F_LEN_HI: s_.blockLen = (s_.blockLen & 0x00FF) | (d << 8); break;
    case F_B_LO:   s_.bStart = (s_.bStart & 0xFF00) | d; break;
    case F_B_HI:   s_.bStart = (s_.bStart & 0x00FF) | (d << 8); break;
    case F_INTCTL: {
        s_.intCtl = d;
        // Folgebytes dieses Bytes stehen VOR dem Rest der Warteschlange (Pulse, dann Vektor)
        uint8_t rest[8], n = 0;
        for (uint8_t i = s_.folgeI; i < s_.folgeN; i++) rest[n++] = s_.folge[i];
        s_.folgeN = s_.folgeI = 0;
        if (d & 0x08) queue({F_PULSE});
        if (d & 0x10) queue({F_VECTOR});
        for (uint8_t i = 0; i < n; i++) queue({rest[i]});
        break; }
    case F_PULSE:  x_.impulsCtl = d; break;
    case F_VECTOR: s_.vector = d; break;
    case F_TIMING_A:
        x_.zeitA = d; x_.variabelA = true;
        s_.timingA = ((d & 3) == 0) ? 4 : ((d & 3) == 1 ? 3 : 2);   // 11 wie 2 [A2]
        break;
    case F_TIMING_B:
        x_.zeitB = d; x_.variabelB = true;
        s_.timingB = ((d & 3) == 0) ? 4 : ((d & 3) == 1 ? 3 : 2);
        break;
    case F_MASK3:  x_.maske = d; break;
    case F_MATCH3: x_.vergleich = d; break;
    case F_READMASK: s_.readMask = d & 0x7F; s_.readPos = 0; break;
    default: break;
    }
    if (s_.folgeI >= s_.folgeN) s_.folgeN = s_.folgeI = 0;
}

void Z80Dma::befehl(uint8_t d) {
    switch (d) {
    case 0xC3: befehlReset(); break;
    case 0xC7: s_.timingA = 4; x_.variabelA = false; break;
    case 0xCB: s_.timingB = 4; x_.variabelB = false; break;
    case 0xCF: load(); break;
    case 0xD3:                                            // Continue: Zähler 0, Adressen laufen weiter
        s_.counter = 0; s_.started = false;
        s_.status |= 0x20;
        x_.nachlauf = false;
        setEndLevel(false);
        break;
    case 0x87:                                            // Enable DMA: setzt nichts zurück
        s_.enabled = true; s_.started = false; x_.byteFreigabe = false;
        setEndLevel(false);
        break;
    case 0x83: break;                                     // Disable DMA (schon durch das Schreiben)
    case 0xAB: s_.intEnable = true; break;
    case 0xAF: s_.intEnable = false; break;
    case 0xA3:
        s_.intEnable = false; s_.intPending = false; s_.ius = false; s_.forceReady = false;
        setEndLevel(false);
        break;
    case 0xB3: s_.forceReady = true; break;
    case 0xBB: queue({F_READMASK}); break;
    case 0xBF: s_.readMask = 0x01; s_.readPos = 0; break;          // [A3]
    case 0x8B: s_.status |= 0x30; break;                           // Reinitialize Status Byte
    case 0xA7: s_.readPos = 0; break;
    case 0xB7: s_.enableAfterReti = true; break;
    default: break;                                                // [A4]
    }
}

void Z80Dma::load() {
    // Quell-Port immer; Ziel-Port nur, wenn nicht fest (Zilog „Fixed-Address Programming“).
    // Bytezähler 0, Force Ready aus, Status D0 (Transfer erfolgt) und D5 (Blockende) zurück.
    const bool a2b = quelleIstA();
    const bool zielFest = ((a2b ? s_.wr2 : s_.wr1) & 0x20) != 0;
    if (a2b) { s_.aCur = s_.aStart; if (!zielFest) s_.bCur = s_.bStart; }
    else     { s_.bCur = s_.bStart; if (!zielFest) s_.aCur = s_.aStart; }
    s_.counter = 0;
    s_.started = false;
    s_.forceReady = false;
    s_.status = static_cast<uint8_t>((s_.status & ~0x01) | 0x20);
    x_.nachlauf = false;
    setEndLevel(false);
}

uint8_t Z80Dma::ioRead(uint8_t) {
    // Lesesequenz in Maskenreihenfolge: Status, Zähler L/H, A L/H, B L/H; danach wieder von vorn.
    for (int i = 0; i < 7; i++) {
        int pos = (s_.readPos + i) % 7;
        if (!(s_.readMask & (1 << pos))) continue;
        s_.readPos = (pos + 1) % 7;
        switch (pos) {
        case 0: return status();
        case 1: return zaehlerLesen() & 0xFF;
        case 2: return (zaehlerLesen() >> 8) & 0xFF;
        case 3: return s_.aCur & 0xFF;
        case 4: return s_.aCur >> 8;
        case 5: return s_.bCur & 0xFF;
        default: return s_.bCur >> 8;
        }
    }
    return status();                                      // Lesemaske 0 [A3]
}

int32_t Z80Dma::zaehlerLesen() const {
    return (zaehler_blocklaenge_ && s_.counter > s_.blockLen) ? s_.blockLen : s_.counter;
}

uint8_t Z80Dma::status() const {
    // D1 folgt dem RDY-Pin (aktiv nach WR5-Polarität; Force Ready zählt nicht), D3 dem
    // anstehenden Interrupt — beides Leitungszustände, kein gespeichertes Bit.
    uint8_t st = static_cast<uint8_t>(s_.status & ~0x0A);
    if (!pinAktiv()) st |= 0x02;
    if (!s_.intPending) st |= 0x08;
    return st;
}

// ─── Ausführung ──────────────────────────────────────────────────────────────

void Z80Dma::setReady(bool pegel) {
    const bool vorher = ready();
    s_.readyPin = pegel;
    // Burst gibt den Bus bei RDY inaktiv ab — das zählt als Busfreigabe (Interrupt bei RDY
    // wieder scharf); Continuous hält ihn.
    if (vorher && !ready() && s_.started && !modeCont()) x_.rdyFreigabe = false;
    pruefeRdyInterrupt();
}

bool Z80Dma::ready() const {
    return s_.forceReady || pinAktiv();
}

bool Z80Dma::busRequest() const {
    if (!s_.enabled || s_.ius || s_.enableAfterReti || x_.byteFreigabe) return false;
    if (rdyInterruptGewaehlt() && !x_.rdyFreigabe) return false;   // erst der Interrupt [A6]
    if (ready()) return true;
    return modeCont() && s_.started;      // Continuous hält den Bus auch ohne RDY
}

void Z80Dma::cpuZyklus() { x_.byteFreigabe = false; pruefeRdyInterrupt(); }

void Z80Dma::pruefeRdyInterrupt() {
    // Interrupt bei RDY: vor der Busanforderung [A6]
    if (!s_.enabled || !rdyInterruptGewaehlt() || x_.rdyFreigabe) return;
    if (s_.intPending || s_.ius || !ready()) return;
    interrupt(0);
}

void Z80Dma::interrupt(uint8_t grund) {
    if (!s_.intEnable) return;                            // [A5]
    if (s_.intPending) grund |= x_.intGrund;              // Treffer + Blockende kommen zusammen
    s_.intPending = true;
    x_.intGrund = grund & 3;
}

uint16_t Z80Dma::schritt(uint16_t a, uint8_t wr) const {
    switch ((wr >> 4) & 3) {
    case 0: return a - 1;
    case 1: return a + 1;
    default: return a;
    }
}

int Z80Dma::zyklus(bool portA) const {
    if (portA ? x_.variabelA : x_.variabelB) return portA ? s_.timingA : s_.timingB;
    if (!standard_zyklen_) return 4;                      // Vorgabe (bisher): 4 Takte
    return ((portA ? s_.wr1 : s_.wr2) & 0x08) ? 4 : 3;    // Datenblatt: E/A 4, Speicher 3
}

int Z80Dma::warte(bool portA, bool ea, uint16_t adr, bool schreiben) const {
    if (!(s_.wr5 & 0x10) || !warteTakte) return 0;
    if (portA ? x_.variabelA : x_.variabelB) {            // variabel: Speicher ≥ 3, E/A = 4 [A9]
        const int z = zyklus(portA);
        if (ea ? z != 4 : z < 3) return 0;
    }
    const int w = warteTakte(ea, adr, schreiben);
    return w > 0 ? w : 0;
}

int Z80Dma::step() {
    if (!busRequest() || !ready()) return 0;
    const bool a2b = quelleIstA();
    uint16_t& src = a2b ? s_.aCur : s_.bCur;
    uint16_t& dst = a2b ? s_.bCur : s_.aCur;
    const uint8_t srcWr = a2b ? s_.wr1 : s_.wr2, dstWr = a2b ? s_.wr2 : s_.wr1;
    const bool srcIo = srcWr & 0x08, dstIo = dstWr & 0x08;
    const uint8_t art = s_.wr0 & 0x03;                    // 1 Transfer, 2 Search, 3 beides
    const bool schreiben = art != 2;

    // Impuls VOR dem Byte mit dem bisherigen Zählerstand [A7]
    if ((s_.intCtl & 0x04) && (s_.counter & 0xFF) == x_.impulsCtl) {
        ++x_.impulse;
        if (impuls) impuls();
    }

    int takte = zyklus(a2b) + warte(a2b, srcIo, src, false);
    uint8_t b = 0xFF;
    if (srcIo) { if (portRead) b = portRead(src); }
    else       { if (memRead) b = memRead(src); }
    if (schreiben) {
        takte += zyklus(!a2b) + warte(!a2b, dstIo, dst, true);
        if (dstIo) { if (portWrite) portWrite(dst, b); }
        else       { if (memWrite) memWrite(dst, b); }
        dst = schritt(dst, dstWr);
    }
    src = schritt(src, srcWr);
    s_.started = true;
    s_.status |= 0x01;
    ++s_.counter;

    // Vergleich (Search, Search/Transfer): Maske 0 = Bit zählt.  Das Nachlaufbyte nach einem
    // Treffer (Search Burst/Continuous) wird nicht mehr verglichen [A8].
    bool treffer = false;
    if (art & 2) {
        if (x_.nachlauf) {
            x_.nachlauf = false;
            if (s_.counter > blockEndeBei()) blockEndeBehandeln(false);
            s_.enabled = false;                           // Stopp bei Treffer schlägt Auto-Restart
            busFreigabe();
            return takte;
        }
        treffer = ((b ^ x_.vergleich) & ~x_.maske) == 0;
        if (treffer) s_.status &= ~0x10;
    }

    if (s_.counter > blockEndeBei()) { blockEndeBehandeln(treffer); return takte; }

    if (treffer) {
        if (s_.intCtl & 0x01) interrupt(1);
        if (x_.wr3 & 0x04) {                              // Stopp bei Treffer
            if (art == 2 && !modeByte()) { x_.nachlauf = true; return takte; }   // Tabelle 2
            s_.enabled = false;
            busFreigabe();
            return takte;
        }
    }
    if (modeByte()) { busFreigabe(); x_.byteFreigabe = true; }
    return takte;
}

void Z80Dma::busFreigabe() {
    // Bus zurück an die CPU: Force Ready fällt (Datenblatt), Interrupt bei RDY wieder scharf.
    s_.forceReady = false;
    s_.started = false;
    x_.rdyFreigabe = false;
}

void Z80Dma::blockEndeBehandeln(bool treffer) {
    s_.status &= ~0x20;                   // Bit 5 = 0: Blockende erreicht
    setEndLevel(true);
    x_.nachlauf = false;
    uint8_t grund = 0;
    if (treffer && (s_.intCtl & 0x01)) grund |= 1;
    if (s_.intCtl & 0x02) grund |= 2;
    if (grund) interrupt(grund);
    busFreigabe();
    if (treffer && (x_.wr3 & 0x04)) {     // Stopp bei Treffer schlägt Auto-Restart
        s_.enabled = false;
    } else if (s_.wr5 & 0x20) {           // Auto-Restart: beide Ports neu, weiter [A10]
        s_.aCur = s_.aStart; s_.bCur = s_.bStart;
        s_.counter = 0;
    } else {                              // Stopp bei Blockende
        s_.enabled = false;
    }
}

// ─── Interrupt ───────────────────────────────────────────────────────────────

uint8_t Z80Dma::vektor() const {
    uint8_t v = s_.vector;
    if (s_.intCtl & 0x20) v = static_cast<uint8_t>((v & 0xF9) | (x_.intGrund << 1));
    return v;
}

uint8_t Z80Dma::getVector() const {
    Z80Dma* self = const_cast<Z80Dma*>(this);   // wie Z80CTC: die Quittung löscht die Anforderung
    const uint8_t v = vektor();
    if (intLeitung()) { self->s_.intPending = false; self->s_.ius = true; }
    return v;
}

void Z80Dma::onRETI() {
    // Der K1520Bus meldet RETI allen Gliedern; dekodieren darf ihn nur, wer IEI = high hat [A11].
    if (!iei_) return;
    s_.ius = false;
    if (s_.enableAfterReti) {             // B7: Busanforderung ab jetzt erlaubt
        s_.enableAfterReti = false;
        x_.rdyFreigabe = true;
    }
    pruefeRdyInterrupt();
}

Z80Dma::Sicht Z80Dma::sicht() const {
    Sicht v{};
    v.wr0 = s_.wr0; v.wr1 = s_.wr1; v.wr2 = s_.wr2; v.wr3 = x_.wr3; v.wr4 = s_.wr4; v.wr5 = s_.wr5;
    v.zeitA = x_.zeitA; v.zeitB = x_.zeitB; v.variabelA = x_.variabelA; v.variabelB = x_.variabelB;
    v.zyklusA = static_cast<uint8_t>(zyklus(true)); v.zyklusB = static_cast<uint8_t>(zyklus(false));
    v.maske = x_.maske; v.vergleich = x_.vergleich;
    v.intSteuer = s_.intCtl; v.impulsSteuer = x_.impulsCtl; v.vektorRoh = s_.vector;
    v.startA = s_.aStart; v.startB = s_.bStart; v.blocklaenge = s_.blockLen;
    v.adresseA = s_.aCur; v.adresseB = s_.bCur;
    v.bytesBewegt = s_.counter; v.zaehlerGelesen = static_cast<uint16_t>(zaehlerLesen());
    v.status = status(); v.lesemaske = s_.readMask; v.lesePos = s_.readPos;
    v.folgeOffen = static_cast<uint8_t>(s_.folgeN - s_.folgeI);
    v.betriebsart = modus(); v.transferart = s_.wr0 & 3; v.aQuelle = quelleIstA();
    v.freigegeben = s_.enabled; v.begonnen = s_.started; v.forceReady = s_.forceReady;
    v.rdyPin = s_.readyPin; v.rdyAktiv = pinAktiv();
    v.intFreigabe = s_.intEnable; v.intAnstehend = s_.intPending; v.ius = s_.ius;
    v.warteAufReti = s_.enableAfterReti; v.rdyFreigabe = x_.rdyFreigabe; v.byteFreigabe = x_.byteFreigabe;
    v.endePegel = s_.endLevel; v.autoRestart = (s_.wr5 & 0x20) != 0; v.waitFunktion = (s_.wr5 & 0x10) != 0;
    v.stoppBeiTreffer = (x_.wr3 & 0x04) != 0; v.iei = iei_;
    v.intGrund = x_.intGrund; v.impulse = x_.impulse;
    v.busAnforderung = busRequest();
    return v;
}

// ─── Savestate ───────────────────────────────────────────────────────────────
// Aufbau: Rohkopie S (unverändert seit AP-W2a), IEI-Byte, dann der Erweiterungsblock
// 'D' 'M' '2' + Rohkopie X.  Ein Stand ohne Kennung (vor P9c) lädt mit X-Vorgaben.

void Z80Dma::serialize(std::vector<uint8_t>& out) const {
    static_assert(std::is_trivially_copyable_v<S>, "S muss POD sein");
    static_assert(std::is_trivially_copyable_v<X>, "X muss POD sein");
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&s_);
    out.insert(out.end(), p, p + sizeof(S));
    out.push_back(iei_ ? 1 : 0);
    out.push_back('D'); out.push_back('M'); out.push_back('2');
    const uint8_t* q = reinterpret_cast<const uint8_t*>(&x_);
    out.insert(out.end(), q, q + sizeof(X));
}

bool Z80Dma::deserialize(const uint8_t*& p, const uint8_t* end) {
    if (static_cast<size_t>(end - p) < sizeof(S) + 1) return false;
    std::memcpy(&s_, p, sizeof(S)); p += sizeof(S);
    iei_ = (*p++ != 0);
    x_ = X{};
    if (end - p >= 3 && p[0] == 'D' && p[1] == 'M' && p[2] == '2') {
        if (static_cast<size_t>(end - p) < 3 + sizeof(X)) return false;
        std::memcpy(&x_, p + 3, sizeof(X)); p += 3 + sizeof(X);
    }
    return true;
}
