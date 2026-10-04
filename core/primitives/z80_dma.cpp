/**
 * @file z80_dma.cpp
 * @brief Z80-DMA (UA858) – Implementierung, siehe z80_dma.h
 */
#include "z80_dma.h"
#include <cstring>
#include <type_traits>

Z80Dma::Z80Dma(const std::string& name) : name_(name) {}

void Z80Dma::reset() {
    // C3: DMA und Interrupts sperren, Auto-Restart aus, RDY low-aktiv, Statusbyte 38H,
    // Port-Timing 4 Zyklen.  Programmierte Adressen/Richtung bleiben (Datenblatt).
    s_.enabled = s_.started = s_.forceReady = false;
    s_.intEnable = s_.intPending = s_.ius = s_.enableAfterReti = false;
    s_.wr5 = 0; s_.intCtl = 0; s_.wr4 &= ~0x60;
    s_.timingA = s_.timingB = 4;
    s_.readMask = 0x7F; s_.readPos = 0; s_.status = 0x38;
    s_.folgeN = s_.folgeI = 0;
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
    if (s_.folgeI < s_.folgeN) { nimmFolge(d); return; }
    s_.folgeN = s_.folgeI = 0;
    schreibeWR(d);
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
        if (d & 0x40) { s_.enabled = true; }              // DMA-Enable-Bit (kompatibel) [?]
        if (d & 0x08) queue({F_MASK3});
        if (d & 0x10) queue({F_MATCH3});
        break;
    case 0x01:                                            // WR4
        s_.wr4 = d;
        if (d & 0x04) queue({F_B_LO});
        if (d & 0x08) queue({F_B_HI});
        if (d & 0x10) queue({F_INTCTL});
        break;
    case 0x02:                                            // WR5 (D2 = 0)
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
    case F_VECTOR: s_.vector = d; break;
    case F_TIMING_A: s_.timingA = ((d & 3) == 0) ? 4 : ((d & 3) == 1 ? 3 : 2); break;
    case F_TIMING_B: s_.timingB = ((d & 3) == 0) ? 4 : ((d & 3) == 1 ? 3 : 2); break;
    case F_READMASK: s_.readMask = d & 0x7F; s_.readPos = 0; break;
    default: break;                                       // Pulse, Maske, Vergleich: nur aufnehmen
    }
    if (s_.folgeI >= s_.folgeN) s_.folgeN = s_.folgeI = 0;
}

void Z80Dma::befehl(uint8_t d) {
    switch (d) {
    case 0xC3: reset(); break;
    case 0xC7: s_.timingA = 4; break;
    case 0xCB: s_.timingB = 4; break;
    case 0xCF: load(); break;
    case 0xD3:                                            // Continue: Zähler 0, Adressen laufen weiter
        s_.counter = 0; s_.enabled = true; s_.started = false;
        setEndLevel(false);
        break;
    case 0x87: s_.enabled = true; s_.started = false; setEndLevel(false); break;
    case 0x83: s_.enabled = false; break;
    case 0xAB: s_.intEnable = true; break;
    case 0xAF: s_.intEnable = false; break;
    case 0xA3: s_.intEnable = false; s_.intPending = false; s_.ius = false; setEndLevel(false); break;
    case 0xB3: s_.forceReady = true; break;
    case 0xBB: queue({F_READMASK}); break;
    case 0xBF: s_.readMask = 0x01; s_.readPos = 0; break;
    case 0x8B: s_.status = (s_.status & ~0x01) | 0x30; break;   // Reinitialize Status Byte
    case 0xA7: s_.readPos = 0; break;
    case 0xB7: s_.enableAfterReti = true; break;
    default: break;
    }
}

void Z80Dma::load() {
    // Quell-Port-Adresse in den Adresszähler; Bytezähler 0 (Datenblatt).
    if (quelleIstA()) s_.aCur = s_.aStart; else s_.bCur = s_.bStart;
    s_.counter = 0;
    s_.started = false;
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
        case 1: return s_.counter & 0xFF;
        case 2: return (s_.counter >> 8) & 0xFF;
        case 3: return s_.aCur & 0xFF;
        case 4: return s_.aCur >> 8;
        case 5: return s_.bCur & 0xFF;
        default: return s_.bCur >> 8;
        }
    }
    return status();
}

uint8_t Z80Dma::status() const {
    // D1 folgt dem RDY-Pin (aktiv nach WR5-Polarität; Force Ready zählt nicht), D3 dem
    // anstehenden Interrupt — beides Leitungszustände, kein gespeichertes Bit.
    const bool pinAktiv = s_.readyPin == ((s_.wr5 & 0x08) != 0);
    uint8_t st = static_cast<uint8_t>(s_.status & ~0x0A);
    if (!pinAktiv) st |= 0x02;
    if (!s_.intPending) st |= 0x08;
    return st;
}

// ─── Ausführung ──────────────────────────────────────────────────────────────

void Z80Dma::setReady(bool pegel) { s_.readyPin = pegel; }

bool Z80Dma::ready() const {
    bool aktivHoch = s_.wr5 & 0x08;
    return s_.forceReady || (s_.readyPin == aktivHoch);
}

bool Z80Dma::busRequest() const {
    if (!s_.enabled) return false;
    if (ready()) return true;
    return modeCont() && s_.started;      // Continuous hält den Bus auch ohne RDY
}

uint16_t Z80Dma::schritt(uint16_t a, uint8_t wr) const {
    switch ((wr >> 4) & 3) {
    case 0: return a - 1;
    case 1: return a + 1;
    default: return a;
    }
}

int Z80Dma::step() {
    if (!s_.enabled || !ready()) return 0;
    const bool a2b = quelleIstA();
    uint16_t& src = a2b ? s_.aCur : s_.bCur;
    uint16_t& dst = a2b ? s_.bCur : s_.aCur;
    const uint8_t srcWr = a2b ? s_.wr1 : s_.wr2, dstWr = a2b ? s_.wr2 : s_.wr1;
    const bool srcIo = srcWr & 0x08, dstIo = dstWr & 0x08;

    uint8_t b = 0xFF;
    if (srcIo) { if (portRead) b = portRead(src); }
    else       { if (memRead) b = memRead(src); }
    if (dstIo) { if (portWrite) portWrite(dst, b); }
    else       { if (memWrite) memWrite(dst, b); }
    src = schritt(src, srcWr);
    dst = schritt(dst, dstWr);
    s_.started = true;
    s_.status |= 0x01;
    const int takte = s_.timingA + s_.timingB;

    if (++s_.counter > s_.blockLen) blockEndeBehandeln();
    return takte;
}

void Z80Dma::blockEndeBehandeln() {
    s_.status &= ~0x20;                   // Bit 5 = 0: Blockende erreicht
    setEndLevel(true);
    if (s_.intEnable && (s_.intCtl & 0x02)) s_.intPending = true;
    if (s_.wr5 & 0x20) {                  // Auto-Restart: beide Ports neu, weiter
        s_.aCur = s_.aStart; s_.bCur = s_.bStart;
        s_.counter = 0; s_.started = false;
    } else {                              // Stopp bei Blockende
        s_.enabled = false; s_.started = false;
    }
}

// ─── Interrupt ───────────────────────────────────────────────────────────────

uint8_t Z80Dma::getVector() const {
    Z80Dma* self = const_cast<Z80Dma*>(this);   // wie Z80CTC: die Quittung löscht die Anforderung
    uint8_t v = s_.vector;
    if (s_.intCtl & 0x20) v = (v & 0xF9) | 0x04;     // Status beeinflusst Vektor: V2 = Blockende [?]
    if (s_.intPending) { self->s_.intPending = false; self->s_.ius = true; }
    return v;
}

void Z80Dma::onRETI() {
    s_.ius = false;
    if (s_.enableAfterReti) { s_.enableAfterReti = false; s_.enabled = true; s_.started = false; }
}

// ─── Savestate ───────────────────────────────────────────────────────────────

void Z80Dma::serialize(std::vector<uint8_t>& out) const {
    static_assert(std::is_trivially_copyable_v<S>, "S muss POD sein");
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&s_);
    out.insert(out.end(), p, p + sizeof(S));
    out.push_back(iei_ ? 1 : 0);
}

bool Z80Dma::deserialize(const uint8_t*& p, const uint8_t* end) {
    if (static_cast<size_t>(end - p) < sizeof(S) + 1) return false;
    std::memcpy(&s_, p, sizeof(S)); p += sizeof(S);
    iei_ = (*p++ != 0);
    return true;
}
