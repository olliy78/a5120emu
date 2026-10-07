/**
 * @file mmu_logik16.cpp
 * @brief P8000 16-Bit-Karte: MMU-Steuerlogik.  Quellen und Annahmen [L1]–[L12]: mmu_logik16.h.
 * @license MIT
 */
#include "mmu_logik16.h"

#include "core/util/zustand.h"

namespace {
constexpr uint8_t SAVE_VERSION = 1;
constexpr uint8_t ST_INSTR = 0xC;
constexpr uint8_t ST_IF1 = 0xD;

bool istSpeicherzyklus(uint8_t st) { return st >= 0x8 && st <= 0xD; }

/// Pegel /SEGT der drei MMUs als Bitmaske.
uint8_t segtMaske(const std::array<Z8010, 3>& m) {
    return uint8_t((m[0].segt() ? 1 : 0) | (m[1].segt() ? 2 : 0) | (m[2].segt() ? 4 : 0));
}
}  // namespace

const char* P8000MmuLogik16::wahlName(Wahl w) {
    static const char* const k[4] = {"-", "CODE", "DATA", "STACK"};
    return k[uint8_t(w) & 3];
}

const char* P8000MmuLogik16::zielName(Ziel z) {
    static const char* const k[5] = {"-", "EPROM", "SRAM", "LEER", "HAUPT"};
    return uint8_t(z) < 5 ? k[uint8_t(z)] : "?";
}

P8000MmuLogik16::P8000MmuLogik16() : P8000MmuLogik16(Config{}) {}
P8000MmuLogik16::P8000MmuLogik16(const Config& cfg) : cfg_(cfg) { powerOn(); }

void P8000MmuLogik16::powerOn() {
    for (auto& m : mmu_) m.powerOn();
    nbr_ = cfg_.nbrStart;                 // DS8282 ohne Reset (Plan §1.3)
    trpl_ = if1l_ = if1lStufe1_ = 0;      // DL374 ohne Reset — Startwert gewählt
    retiVorher_ = 0;
    segtPegel_ = false;
    letzterZyklus_ = Z8kBusCycle{};
    letzterZugriff_ = Zugriff{};
    letztesSegt_ = SegtEreignis{};
    for (auto& n : zyklen_) n = 0;
    onBoard_ = unterdrueckt_ = konflikte_ = segtFlanken_ = 0;
    reset();
}

void P8000MmuLogik16::reset() {
    scr_ = 0;          // D18 /CLR = MRESET− (Bl. 9)
    led_ = true;       // RS-FF 3D3 gesetzt von MRESET− ⇒ LED an (Plan §5.1 Befund 3)
    for (auto& m : mmu_) m.reset(cfg_.csBeimReset);   // [L5]
    segtNachfuehren();
}

void P8000MmuLogik16::segtNachfuehren() {
    const bool neu = segtMaske(mmu_) != 0;
    if (neu == segtPegel_) return;
    segtPegel_ = neu;
    if (onSegt) onSegt(neu);
}

// ─── Gatterlogik Bl. 5 ───────────────────────────────────────────────────────

P8000MmuLogik16::Wahl P8000MmuLogik16::auswahl(uint8_t status, bool system, bool segUser, bool sn6,
                                               bool datenSeite, bool onBoard) {
    // Wörtlich nach schaltplan_16bit.md §1.2; K = IMEML (1 = kein On-Board-Zugriff).
    const bool ist3 = (status & 0x8) != 0;
    const bool ist2 = (status & 0x4) != 0;
    const bool ins = !system;                    // IN/S−: 1 = Normal
    const bool k = !onBoard;
    const bool n = !(ist3 && k);
    const bool p = !(segUser && ins);
    const bool q = !(ins && !segUser && !ist2);
    const bool x = !(p || n);
    const bool y = !(q || n);
    const bool mcode = !(p && q && !n);                          // low-aktiv
    const bool mdata = !((!sn6 && x) || (datenSeite && y));
    const bool mstack = !((sn6 && x) || (!datenSeite && y));
    if (!mcode) return Wahl::Code;
    if (!mdata) return Wahl::Data;
    if (!mstack) return Wahl::Stack;
    return Wahl::Keine;
}

bool P8000MmuLogik16::onBoardFenster(uint8_t status, uint8_t scr, uint8_t seg, uint16_t offset) {
    // 5D24: E1 = BDMEMON−, E2 = MEMREQ, E3 = W = NOR(SN0, SN6) ∧ ¬LAD15 ∧ Q(4D16 = SN1–5 = 0).
    return istSpeicherzyklus(status & 0x0F) && !(scr & SCR_BDMEM_AUS) && (seg & 0x7F) == 0 &&
           offset < 0x8000;
}

P8000MmuLogik16::Eingang P8000MmuLogik16::eingang(const Z8kBusCycle& c) const {
    Eingang in{};
    const uint8_t st = uint8_t(c.st) & 0x0F;
    const uint8_t seg = c.seg & 0x7F;
    in.onBoard = onBoardFenster(st, scr_, seg, c.addr);
    in.nbrWirksam = ((seg >> 1) & 0x1F) == 0 ? 0xFF : nbr_;   // [L2]
    const uint8_t hi = uint8_t(c.addr >> 8);
    const bool datenSeite = cfg_.nbrGleichheitStack ? in.nbrWirksam > hi : in.nbrWirksam >= hi;   // [L1]
    in.wahl = auswahl(st, c.system, (scr_ & SCR_SEG_USR) != 0, (seg & 0x40) != 0, datenSeite, in.onBoard);
    for (int i = 0; i < 3; ++i) {
        in.mz[i].status = st;
        in.mz[i].nsHigh = in.wahl != Wahl(i + 1);
        in.mz[i].lesen = c.read;
        in.mz[i].seg = i == 0 ? seg : uint8_t(seg & 0x3F);   // SN6 nur an der Code-MMU (Bl. 3)
        in.mz[i].offset = c.addr;
        in.mz[i].dma = false;
    }
    return in;
}

P8000MmuLogik16::Zugriff P8000MmuLogik16::bilde(const Z8kBusCycle& c, const Eingang& in,
                                                const Z8010::Ergebnis e[3]) const {
    Zugriff z;
    z.wahl = in.wahl;
    if (!istSpeicherzyklus(uint8_t(c.st) & 0x0F)) return z;
    for (int i = 0; i < 3; ++i) {
        if (e[i].adresse) z.treiber |= uint8_t(1u << i);
        if (e[i].sup) z.sup |= uint8_t(1u << i);
    }
    if (in.onBoard) {   // IMEML sperrt MMU-Auswahl, TRAD-Freigabe und SYSDS
        const uint16_t off = c.addr;
        if (off < 0x4000)      { z.ziel = Ziel::OnBoardEprom; z.adresse = off; }
        else if (off < 0x6000) { z.ziel = Ziel::OnBoardSram;  z.adresse = off & 0x07FF; }   // LAD11/12 offen
        else                   { z.ziel = Ziel::OnBoardLeer;  z.adresse = off; }            // 5D24 Y3 offen
        z.wartetakte = 1;
        return z;
    }
    z.ziel = Ziel::Hauptspeicher;
    z.unterdrueckt = z.sup != 0;   // [L8]
    if (scr_ & SCR_MMU_ON) {
        z.mmuAdresse = true;
        uint16_t trad = 0xFFFF;    // offen = 1 [L7]
        bool erster = true;
        uint16_t erste = 0;
        for (int i = 0; i < 3; ++i) {
            if (!e[i].adresse) continue;
            const uint16_t t = uint16_t(e[i].phys >> 8);
            if (erster) { erste = t; erster = false; }
            else if (t != erste) z.konflikt = true;
            trad &= t;
        }
        z.adresse = (uint32_t(trad) << 8) | (c.addr & 0xFF);
    } else {
        z.adresse = (uint32_t(c.seg & 0x7F) << 16) | c.addr;   // BUSA16–22 = SN0–6, A23 = 0
    }
    return z;
}

// ─── Bus ─────────────────────────────────────────────────────────────────────

P8000MmuLogik16::Zugriff P8000MmuLogik16::zyklus(const Z8kBusCycle& c) {
    letzterZyklus_ = c;
    const uint8_t st = uint8_t(c.st) & 0x0F;
    if (!istSpeicherzyklus(st)) {   // die UB8010 werten nur %8–%D aus
        letzterZugriff_ = Zugriff{};
        return letzterZugriff_;
    }
    const Eingang in = eingang(c);
    const uint8_t vorher = segtMaske(mmu_);
    Z8010::Ergebnis e[3];
    for (int i = 0; i < 3; ++i) e[i] = mmu_[i].zyklus(in.mz[i]);
    const uint8_t nachher = segtMaske(mmu_);
    const Zugriff z = bilde(c, in, e);

    // TRPL und IF1L-Stufe 2 takten mit der Flanke der Sammelleitung [L6].
    const bool flanke = !segtPegel_ && nachher != 0;
    if (flanke) {
        trpl_ = uint8_t(c.addr & 0xFF);
        if1l_ = if1lStufe1_;
        ++segtFlanken_;
        letztesSegt_ = SegtEreignis{c, uint8_t(nachher & ~vorher), trpl_, if1l_, in.wahl, segtFlanken_};
    }
    segtNachfuehren();
    if (flanke && onSegtEreignis) onSegtEreignis(letztesSegt_);
    // IF1L-Stufe 1 übernimmt am Zyklusende [L6].
    if (st == ST_IF1 || (cfg_.if1lAuchStatus1100 && st == ST_INSTR)) if1lStufe1_ = uint8_t(c.addr & 0xFF);

    ++zyklen_[uint8_t(z.wahl) & 3];
    if (z.ziel != Ziel::Hauptspeicher) ++onBoard_;
    if (z.unterdrueckt) ++unterdrueckt_;
    if (z.konflikt) ++konflikte_;
    letzterZugriff_ = z;
    return z;
}

uint16_t P8000MmuLogik16::segtQuittung() {
    uint16_t getrieben = 0, wert = 0xFFFF;
    for (auto& m : mmu_) {
        const Z8010::Kennung k = m.segtQuittung();
        getrieben |= k.maske;
        wert &= uint16_t(k.wert | ~k.maske);   // verdrahtetes UND [L7]
    }
    segtNachfuehren();
    return uint16_t((cfg_.leer16 & ~getrieben) | (wert & getrieben));
}

uint16_t P8000MmuLogik16::spezialLesen(uint16_t port) {
    const uint8_t cs = csMaske(port);
    const uint8_t code = uint8_t(port >> 8);
    uint8_t v = 0xFF;
    for (int i = 0; i < 3; ++i)
        if (cs & (1u << i)) v &= mmu_[i].kommandoLesen(code);   // mehrere: UND [L7]
    const uint8_t hi = cs ? v : uint8_t(cfg_.leer16 >> 8);
    segtNachfuehren();
    return uint16_t((hi << 8) | (cfg_.leer16 & 0xFF));   // UB8010 treibt nur AD8–15
}

void P8000MmuLogik16::spezialSchreiben(uint16_t port, uint16_t ad) {
    const uint8_t cs = csMaske(port);
    const uint8_t code = uint8_t(port >> 8);
    for (int i = 0; i < 3; ++i)
        if (cs & (1u << i)) mmu_[i].kommandoSchreiben(code, uint8_t(ad >> 8));
    segtNachfuehren();   // %10 nimmt /SEGT zurück
}

// ─── Register FFC1–FFF9 (+ FFB9) ─────────────────────────────────────────────

bool P8000MmuLogik16::istEigenerPort(uint16_t port) const {
    return (port & 0xFFC1) == 0xFFC1 || (istIndex4() && (port & 0xFFF9) == 0xFFB9);   // [L9]
}

uint8_t P8000MmuLogik16::ioLesen(uint16_t port) {
    if ((port & 0xFFC1) != 0xFFC1) return cfg_.leer8;   // LEDAUS / fremd
    switch ((port >> 3) & 7) {
    case 0: return uint8_t(0xF0 | (scr_ & 0x0F));       // [L4]
    case 2: return nbr_;                                // 8D22, bitgleich
    case 6: return trpl_;
    case 7: return if1l_;
    default: return cfg_.leer8;                         // SBR [L3], FFD9/FFE1/FFE9 [L10]
    }
}

void P8000MmuLogik16::ioSchreiben(uint16_t port, uint8_t daten) {
    if ((port & 0xFFC1) != 0xFFC1) {
        if (istIndex4() && (port & 0xFFF9) == 0xFFB9) led_ = false;   // LEDAUS
        return;
    }
    switch ((port >> 3) & 7) {
    case 0: scr_ = daten & 0x0F; break;
    case 2: nbr_ = daten; break;
    case 3: if (istIndex4()) led_ = true; break;        // LEDEIN; Index 1: SNVR ohne Wirkung [L10]
    case 4:                                             // RETI [L12]
        if (retiVorher_ == 0xED && daten == 0x4D && onReti) onReti();
        retiVorher_ = daten;
        break;
    case 5: if (onSoftreset) onSoftreset(); break;
    default: break;                                     // SBR [L3], TRPL/IF1L nur lesbar
    }
}

// ─── Beobachtung ─────────────────────────────────────────────────────────────

P8000MmuLogik16::Probe P8000MmuLogik16::probe(const Z8kBusCycle& c) const {
    Probe pr;
    const uint8_t st = uint8_t(c.st) & 0x0F;
    if (!istSpeicherzyklus(st)) return pr;
    const Eingang in = eingang(c);
    Z8010::Ergebnis e[3];
    for (int i = 0; i < 3; ++i) {
        pr.mmu[i] = mmu_[i].probe(in.mz[i]);
        pr.nsHigh[i] = in.mz[i].nsHigh;
        e[i].adresse = pr.mmu[i].adresse;
        e[i].phys = pr.mmu[i].phys;
        // SUP wie zyklus(): eigene Verletzung oder laufender SUP bis Befehlsende (nicht über %D).
        e[i].sup = (pr.mmu[i].geprueft && pr.mmu[i].verletzung) ||
                   (mmu_[i].supBisBefehlsende() && st != ST_IF1);
    }
    pr.nbrWirksam = in.nbrWirksam;
    pr.zugriff = bilde(c, in, e);
    return pr;
}

P8000MmuLogik16::Sicht P8000MmuLogik16::sicht() const {
    Sicht s{};
    s.scr = scr_; s.nbr = nbr_; s.trpl = trpl_; s.if1l = if1l_; s.if1lStufe1 = if1lStufe1_;
    s.runLed = led_; s.segt = segtPegel_;
    s.letzterZyklus = letzterZyklus_;
    s.letzterZugriff = letzterZugriff_;
    s.letztesSegt = letztesSegt_;
    for (int i = 0; i < 4; ++i) s.zyklen[i] = zyklen_[i];
    s.onBoard = onBoard_; s.unterdrueckt = unterdrueckt_; s.konflikte = konflikte_;
    s.segtFlanken = segtFlanken_;
    return s;
}

// ─── Save-State ──────────────────────────────────────────────────────────────

void P8000MmuLogik16::serialize(std::vector<uint8_t>& out) const {
    out.push_back(SAVE_VERSION);
    auto a = k1520::ZAr::schreiber(out);
    uint8_t scr = scr_, nbr = nbr_, trpl = trpl_, if1l = if1l_, st1 = if1lStufe1_, reti = retiVorher_;
    bool led = led_, segt = segtPegel_;
    a.num(scr); a.num(nbr); a.num(trpl); a.num(if1l); a.num(st1); a.num(reti);
    a.flag(led); a.flag(segt);
    for (const auto& m : mmu_) m.serialize(out);
}

bool P8000MmuLogik16::deserialize(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 1 || *p != SAVE_VERSION) return false;
    const uint8_t* q = p + 1;
    auto a = k1520::ZAr::leser(q, end);
    uint8_t scr = 0, nbr = 0, trpl = 0, if1l = 0, st1 = 0, reti = 0;
    bool led = false, segt = false;
    a.num(scr); a.num(nbr); a.num(trpl); a.num(if1l); a.num(st1); a.num(reti);
    a.flag(led); a.flag(segt);
    if (!a.ok) return false;
    q = a.p;
    std::array<Z8010, 3> neu;
    for (auto& m : neu)
        if (!m.deserialize(q, end)) return false;
    // Erst jetzt übernehmen — ein Fehler lässt den Ist-Zustand stehen.
    mmu_ = neu;
    scr_ = scr & 0x0F; nbr_ = nbr; trpl_ = trpl; if1l_ = if1l; if1lStufe1_ = st1; retiVorher_ = reti;
    led_ = led;
    segtPegel_ = segt;
    p = q;
    if (onSegt) onSegt(segtPegel_);
    return true;
}
