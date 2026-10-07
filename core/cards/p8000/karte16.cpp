/**
 * @file karte16.cpp
 * @brief 16-Bit-Rechnerkarte des P8000 — Busverteilung, E/A-Dekoder, Reset/NMI, Peripherie.
 * @see karte16.h, doc/p8000/schaltplan_16bit.md, doc/design/25_p8000.md §10
 */

#include "core/cards/p8000/karte16.h"
#include "core/cards/p8000/rom_mon16.h"
#include "core/logger.h"
#include "core/serial/sio_format.h"
#include "core/util/zustand.h"

namespace {
constexpr uint8_t SAVE_VERSION = 1;

P8000MmuLogik16::Config mmuConfig(const P8000Karte16::Config& c) {
    P8000MmuLogik16::Config m;
    m.index = c.index == P8000Karte16::Config::Index::I1 ? P8000MmuLogik16::Config::Index::I1
                                                         : P8000MmuLogik16::Config::Index::I4;
    m.nbrGleichheitStack = c.nbr_gleichheit_stack;
    m.csBeimReset = c.cs_beim_reset;
    m.if1lAuchStatus1100 = c.if1l_auch_status1100;
    m.nbrStart = c.nbr_start;
    m.leer8 = c.leer8;
    m.leer16 = c.leer16;
    return m;
}

Z8kConfig cpuConfig() {
    Z8kConfig z;
    z.model = Z8kModel::Z8001;
    return z;
}

/// Baustein des E/A-Dekoders 4D24 (FF80–FFBF): LAD3–5; nur Status 0010, A15–A7 = 1, A0 = 1 (D13).
enum Baustein { SIO0 = 0, SIO1, PIO0, PIO1, PIO2, CTC0, CTC1, LEDAUS };
bool istPeripherie(uint16_t port) { return (port & 0xFFC1) == 0xFF81; }
int  baustein(uint16_t port) { return (port >> 3) & 7; }
/// SIO/PIO-Register des Primitivs: Bit 0 = C/D (A2), Bit 1 = Kanal/Port B (A1).
uint8_t z80Reg(uint16_t port) { return uint8_t(((port >> 2) & 1) | (((port >> 1) & 1) << 1)); }
uint8_t ctcKanal(uint16_t port) { return uint8_t((port >> 1) & 3); }
}  // namespace

// ─── Anschluss je tty (Entwurf 19/25 §10.6) ──────────────────────────────────
class P8000Karte16::Anschluss : public k1520::serial::SerialAnschluss {
public:
    Anschluss(P8000Karte16& k, int i) : k_(k), i_(i) {
        name_ = "tty" + std::to_string(4 + i);
        stecker_ = "X" + std::to_string(4 + i);
    }
    const char* name() const override { return name_.c_str(); }
    const char* stecker() const override { return stecker_.c_str(); }
    bool v24() const override { return true; }

    k1520::serial::SerialFormat format() const override {
        const Z80SIO::Channel::Format f = ch().format();
        const Z80CTC& c = i_ == 3 ? k_.ctc1_ : k_.ctc0_;
        const int kanal = i_ == 3 ? 0 : i_;
        return k1520::serial::serialFormatRechnenQ16(f.teiler, f.tx_bits, f.paritaet, f.stopp_halbe,
                                                     c.teilerTakteQ16(kanal) * 2, k_.cfg_.takt_hz);
    }
    bool senderHatZeichen() const override { return ch().senderHatZeichen(); }
    uint8_t senderNimm() override {
        uint8_t b = ch().txGet();
        const uint8_t bits = ch().format().tx_bits;
        if (bits < 8) b &= static_cast<uint8_t>((1u << bits) - 1);
        k_.seriell_geaendert_ = true;
        return b;
    }
    bool empfaengerFrei() const override { return ch().empfaengerFrei(); }
    void empfange(uint8_t b) override { ch().rxByte(b); k_.seriell_geaendert_ = true; }
    bool rts() const override { return ch().rts(); }
    bool dtr() const override { return ch().dtr(); }
    void setzeEingaenge(bool, bool, bool dcd) override { dcd_ = dcd; wirke(); }
    bool& dcdRef() { return dcd_; }
    bool breakGesendet() const override { return ch().breakSenden(); }
    void breakEmpfang(bool aktiv) override { ch().setzeBreakEmpfang(aktiv); k_.seriell_geaendert_ = true; }

    /// [K5] /CTS tty4 = RTS, tty5–7 fest aktiv; /DCD vom Anschluss.
    void wirke() {
        ch().setzeCTS(i_ == 0 ? ch().rts() : true);
        ch().setzeDCD(dcd_);
        k_.seriell_geaendert_ = true;
    }

private:
    Z80SIO::Channel& ch() const {
        Z80SIO& s = i_ < 2 ? k_.sio0_ : k_.sio1_;
        return (i_ & 1) ? s.channelB() : s.channelA();
    }
    P8000Karte16& k_;
    int           i_;
    bool          dcd_ = false;
    std::string   name_, stecker_;
};

P8000Karte16::~P8000Karte16() = default;

k1520::serial::SerialAnschluss& P8000Karte16::anschluss(int i) { return *anschluesse_[size_t(i & 3)]; }

// ─── Aufbau ──────────────────────────────────────────────────────────────────

P8000Karte16::P8000Karte16(const Config& cfg)
    : cfg_(cfg), cpu_(cpuConfig()), mmu_(mmuConfig(cfg)), dram_(cfg.dram) {
    for (int i = 0; i < TTY_ANZAHL; ++i) anschluesse_[size_t(i)] = std::make_unique<Anschluss>(*this, i);
    switch (cfg.mon16) {
        case Config::Mon16::V3_0: rom_ = P8K_MON16_3_0; break;
        case Config::Mon16::V3_1: rom_ = P8K_MON16_3_1; break;
        case Config::Mon16::V3_3: rom_ = P8K_MON16_3_3; break;
    }
    if (!dram_.fehler().empty()) LOG_ERROR("P8000", "16-Bit-Karte: %s", dram_.fehler().c_str());

    // Wie an der 8-Bit-Karte (gleiche Bausteine, gleiche Datenblattlage, merkposten/p8000.md 1).
    ctc0_.setResetLaedtZeitkonstante(true);
    ctc1_.setResetLaedtZeitkonstante(true);
    pio0_.setModus3Flanke(true);
    pio1_.setModus3Flanke(true);
    pio2_.setModus3Flanke(true);

    // BUS BAUD CLK (Bl. 10, 14): CTC0 K0–K2, CTC1 K0 und K2; CTC1 ZC/TO2 → CLK/TRG3.
    const uint64_t baud = cfg.baudquarz_hz / 8;
    for (int k = 0; k < 3; ++k) ctc0_.setzeEingangsTakt(k, baud, cfg.takt_hz);
    ctc1_.setzeEingangsTakt(0, baud, cfg.takt_hz);
    ctc1_.setzeEingangsTakt(2, baud, cfg.takt_hz);
    ctc1_.setZCTOCallback([this](int ch, bool lvl) { if (ch == 2) ctc1_.clkTrg(3, lvl); });
    ctc1_.setzeEingangsQuelleQ16(3, [this] { return ctc1_.teilerTakteQ16(2); });

    peri_.setInterruptChain({&ctc0_, &ctc1_, &sio0_, &sio1_, &pio0_, &pio1_, &pio2_});

    cpu_.read = [this](const Z8kBusCycle& c) { return busLesen(c); };
    cpu_.write = [this](const Z8kBusCycle& c, uint16_t d) { busSchreiben(c, d); };
    mmu_.onSegt = [this](bool aktiv) { cpu_.setSEGT(aktiv); };
    mmu_.onReti = [this] { peri_.signalRETI(); };
    mmu_.onSoftreset = [this] {
        // FFE9: SOFTRESET− ist ein Impuls ⇒ MRESET− bis zum nächsten Takt (Bl. 6), die CPU
        // startet danach aus dem Reset-Vektor.  PIOs bleiben (PIORESET− hängt nicht daran).
        LOG_INFO("P8000", "16-Bit-Karte: SOFTRESET");
        mresetBausteine();
        cpu_.reset();
    };

    // [K1] PIO2-B0/B1/B2/B7 hinter dem DL540, WDC fehlt ⇒ 0.
    pins_[2][1] = {0x00, 0x87};
    powerOn();
}

// ─── Pins ────────────────────────────────────────────────────────────────────

void P8000Karte16::setzePinTreiber(int p, int port, uint8_t pegel, uint8_t treibMask) {
    pins_[p][port] = {pegel, treibMask};
    pinsAnlegen(p);
}

void P8000Karte16::pinsAnlegen(int i) {
    pio(i).setExternA(pins_[i][0].pegel, pins_[i][0].maske);
    pio(i).setExternB(pins_[i][1].pegel, pins_[i][1].maske);
    peri_.markIntDirty();
}

// ─── Reset / NMI ─────────────────────────────────────────────────────────────

void P8000Karte16::piosZuruecksetzen() {
    pio0_.reset(); pio1_.reset(); pio2_.reset();
    for (int i = 0; i < 3; ++i) pinsAnlegen(i);
}

void P8000Karte16::mresetBausteine() {
    mmu_.reset();                     // SCR := 0, LED an, UB8010 zurück
    ctc0_.reset(); ctc1_.reset();
    sio0_.reset(); sio1_.reset();
    dram_.reset();                    // BUSMRESET− an RES− der Karten
    if (cfg_.index == Config::Index::I1) piosZuruecksetzen();   // Index 1: PIOs an MRESET−
    paritaetNachSCR();
    for (auto& a : anschluesse_) a->wirke();
    seriell_geaendert_ = true;
    peri_.markIntDirty();
}

void P8000Karte16::mresetSetzen(bool aktiv) {
    if (aktiv == mreset_) return;
    mreset_ = aktiv;
    if (aktiv) {
        mresetBausteine();
        cpu_.setResetLine(true);
    } else {
        cpu_.setResetLine(false);     // Resetsequenz: FCW/PC aus 0002/0004/0006
    }
}

void P8000Karte16::powerOn() {
    dram_.powerOn();
    sram_.fill(cfg_.sram_fuellwert);
    mmu_.powerOn();
    manual_nmi_ = false;
    nmi_rest_ = 0;
    paritaet_ff_ = false;
    piosZuruecksetzen();              // PRES− an PIORESET−
    mreset_ = false;
    mresetSetzen(true);               // PRES− an MRESET−
    cpu_.cycles = 0;
    zeit_ = 0;
    mresetSetzen(reset_eingang_);
    nmiNachfuehren();
}

void P8000Karte16::setResetEingang(bool aktiv) {
    reset_eingang_ = aktiv;
    mresetSetzen(aktiv);
}

void P8000Karte16::setTresetEingang(bool aktiv) {
    const bool flanke = aktiv && !treset_eingang_;
    treset_eingang_ = aktiv;
    if (flanke && cfg_.index == Config::Index::I4) piosZuruecksetzen();
}

void P8000Karte16::setManualNmi(bool aktiv) {
    manual_nmi_ = aktiv;
    if (!aktiv) nmi_rest_ = 0;
    nmiNachfuehren();
}

void P8000Karte16::nmiTaste() {
    manual_nmi_ = true;
    nmi_rest_ = cfg_.nmi_impuls_takte > 0 ? cfg_.nmi_impuls_takte : 1;
    nmiNachfuehren();
}

void P8000Karte16::nmiNachfuehren() {
    const bool pegel = manual_nmi_ || paritaet_ff_;   // [K3]; POWER FAIL wird nicht erzeugt
    if (pegel == nmi_pegel_) return;
    nmi_pegel_ = pegel;
    cpu_.setNMI(pegel);
}

void P8000Karte16::paritaetNachSCR() {
    // /R des Paritäts-FF = CLRPARITY− = SCR Bit 3 (Bl. 10); dieselbe Leitung geht als CL_PAR− an
    // den Speicherbus.
    const bool clr = (mmu_.scr() & P8000MmuLogik16::SCR_PARITAET) == 0;
    dram_.setzeClrParitaet(clr);
    if (clr) paritaet_ff_ = false;
    nmiNachfuehren();
}

uint16_t P8000Karte16::nmiIdentifier() const {
    uint16_t b = 0;
    if (manual_nmi_) b |= 0x01;
    if (paritaet_ff_) b |= 0x04;
    // Bit 1 POWER FAIL: nie; Bit 3 MSDOSNMI (Index 4) ohne Karte / Masse (Index 1): 0.
    return uint16_t((cfg_.leer16 & 0xFFF0) | b);
}

// ─── Zeit ────────────────────────────────────────────────────────────────────

void P8000Karte16::takt(int n) {
    bool dirty = ctc0_.clockTick(n);
    dirty |= ctc1_.clockTick(n);
    dirty |= nimmSeriellGeaendert();
    if (dirty) peri_.markIntDirty();
    if (nmi_rest_ > 0 && (nmi_rest_ -= n) <= 0) {
        nmi_rest_ = 0;
        manual_nmi_ = false;
        nmiNachfuehren();
    }
}

int P8000Karte16::schritt() {
    if (mreset_) {   // CPU, CTC, SIO stehen; die Zeit läuft
        ++zeit_;
        return 1;
    }
    peri_.updateInterruptChain();
    cpu_.setVI(peri_.isINT());
    const int n = cpu_.step();
    zeit_ += uint64_t(n);
    takt(n);
    return n;
}

void P8000Karte16::laufeBis(uint64_t ziel) {
    while (zeit_ < ziel) {
        if (mreset_) { zeit_ = ziel; break; }
        schritt();
    }
}

// ─── Bus ─────────────────────────────────────────────────────────────────────

uint16_t P8000Karte16::speicherLesen(const Z8kBusCycle& c) {
    const P8000MmuLogik16::Zugriff z = mmu_.zyklus(c);
    if (z.wartetakte) cpu_.addWaitCycles(z.wartetakte);
    if (c.st == Z8kStatus::MemStack) stapelImpuls();
    using Ziel = P8000MmuLogik16::Ziel;
    switch (z.ziel) {
        case Ziel::OnBoardEprom: {
            const uint16_t a = uint16_t(z.adresse & 0x3FFE);
            return uint16_t((rom_[a] << 8) | rom_[a + 1]);
        }
        case Ziel::OnBoardSram: {
            const uint16_t a = uint16_t(z.adresse & 0x7FE);
            return uint16_t((sram_[a] << 8) | sram_[a + 1]);
        }
        case Ziel::Hauptspeicher: {
            if (z.unterdrueckt)                       // SYSDS gesperrt [L8], Datum [K7]
                return cfg_.unterdrueckt_liest_offset ? c.addr : cfg_.leer16;
            uint16_t w = cfg_.leer16;
            if (!dram_.lesen(z.adresse, w)) return cfg_.leer16;
            if (dram_.pe() && (mmu_.scr() & P8000MmuLogik16::SCR_PARITAET)) {   // [K4]
                paritaet_ff_ = true;
                nmiNachfuehren();
            }
            return w;
        }
        default: return cfg_.leer16;              // 6000–7FFF leer
    }
}

void P8000Karte16::speicherSchreiben(const Z8kBusCycle& c, uint16_t d) {
    const P8000MmuLogik16::Zugriff z = mmu_.zyklus(c);
    if (z.wartetakte) cpu_.addWaitCycles(z.wartetakte);
    if (c.st == Z8kStatus::MemStack) stapelImpuls();
    using Ziel = P8000MmuLogik16::Ziel;
    if (z.ziel == Ziel::OnBoardSram) {
        const uint16_t a = uint16_t(z.adresse & 0x7FF);
        if (c.word) { sram_[a & 0x7FE] = uint8_t(d >> 8); sram_[(a & 0x7FE) + 1] = uint8_t(d); }
        else sram_[a] = (a & 1) ? uint8_t(d) : uint8_t(d >> 8);   // getrennte /CS je Byte-Spur
    } else if (z.ziel == Ziel::Hauptspeicher && !z.unterdrueckt) {
        dram_.schreiben(z.adresse, c.word, d);
    }
    // EPROM (/OE nur beim Lesen) und 6000–7FFF: keine Wirkung
}

uint8_t P8000Karte16::ioLesenByte(uint16_t port) {
    if (mmu_.istEigenerPort(port)) return mmu_.ioLesen(port);
    if (!istPeripherie(port)) return cfg_.leer8;
    peri_.markIntDirty();
    switch (baustein(port)) {
        case SIO0: return sio0_.ioRead(z80Reg(port));
        case SIO1: return sio1_.ioRead(z80Reg(port));
        case PIO0: return pio0_.ioRead(z80Reg(port));
        case PIO1: return pio1_.ioRead(z80Reg(port));
        case PIO2: return pio2_.ioRead(z80Reg(port));
        case CTC0: return ctc0_.ioRead(ctcKanal(port));
        case CTC1: return ctc1_.ioRead(ctcKanal(port));
        default:   return cfg_.leer8;
    }
}

void P8000Karte16::ioSchreibenByte(uint16_t port, uint8_t d) {
    if (mmu_.istEigenerPort(port)) {
        const uint8_t scrVorher = mmu_.scr();
        mmu_.ioSchreiben(port, d);
        if (mmu_.scr() != scrVorher) paritaetNachSCR();
        return;
    }
    if (!istPeripherie(port)) return;
    peri_.markIntDirty();
    const int b = baustein(port);
    switch (b) {
        case SIO0: case SIO1: {
            Z80SIO& s = b == SIO0 ? sio0_ : sio1_;
            s.ioWrite(z80Reg(port), d);
            for (auto& a : anschluesse_) a->wirke();   // WR5-RTS wirkt auf /CTS tty4
            break;
        }
        case PIO0: case PIO1: case PIO2: {
            const int i = b - PIO0;
            pio(i).ioWrite(z80Reg(port), d);
            pinsAnlegen(i);   // Moduswechsel lädt die Eingabebits nicht aus den Pins nach
            break;
        }
        case CTC0: ctc0_.ioWrite(ctcKanal(port), d); break;
        case CTC1: ctc1_.ioWrite(ctcKanal(port), d); break;
        default: break;
    }
}

uint16_t P8000Karte16::busLesen(const Z8kBusCycle& c) {
    switch (c.st) {
        case Z8kStatus::Io:
            // Peripherie am Low-Datenbus (AD0–7); AD8–15 treibt niemand.
            return uint16_t((cfg_.leer16 & 0xFF00) | ioLesenByte(c.addr));
        case Z8kStatus::SpecialIo:  return mmu_.spezialLesen(c.addr);
        case Z8kStatus::SegTrapAck: return mmu_.segtQuittung();
        case Z8kStatus::NmiAck:     return nmiIdentifier();
        case Z8kStatus::ViAck: {
            peri_.updateInterruptChain();
            const uint8_t v = peri_.interruptAcknowledge();
            peri_.markIntDirty();
            return uint16_t((cfg_.leer16 & 0xFF00) | v);
        }
        case Z8kStatus::Refresh:
            dram_.refresh();
            return cfg_.leer16;
        default:
            if (c.isMemory()) return speicherLesen(c);
            return cfg_.leer16;                    // [K6]
    }
}

void P8000Karte16::busSchreiben(const Z8kBusCycle& c, uint16_t d) {
    switch (c.st) {
        case Z8kStatus::Io:        ioSchreibenByte(c.addr, uint8_t(d)); break;
        case Z8kStatus::SpecialIo: mmu_.spezialSchreiben(c.addr, d); break;
        default:
            if (c.isMemory()) speicherSchreiben(c, d);
            break;
    }
}

// ─── Save-State ──────────────────────────────────────────────────────────────

namespace {
void visitZ8000(k1520::ZAr& a, Z8000& z, Z8kRunState& r) {
    for (auto& x : z.Rg) a.num(x);
    a.num(z.R14[0]); a.num(z.R14[1]); a.num(z.R15[0]); a.num(z.R15[1]);
    a.num(z.fcw); a.num(z.pc); a.num(z.pcSeg); a.num(z.psapSeg); a.num(z.psapOff);
    a.num(z.refresh); a.num(z.cycles);
    a.flag(r.resetLine); a.flag(r.resetPending); a.flag(r.nmiLine); a.flag(r.nmiPending);
    a.flag(r.vi); a.flag(r.nvi); a.flag(r.stopLine); a.flag(r.busReq); a.flag(r.mi); a.flag(r.mo);
    a.flag(r.busAck); a.flag(r.halted); a.flag(r.stopped); a.flag(r.haveW0); a.flag(r.inRepeat);
    a.num(r.w0);
    for (auto& w : r.repWords) a.num(w);
    a.num(r.repNwords); a.flag(r.repSeg); a.num(r.repNext); a.num(r.lastPc); a.num(r.lastPcSeg);
    a.num(r.refreshAcc);
}
}  // namespace

void P8000Karte16::serialize(std::vector<uint8_t>& out) const {
    auto* self = const_cast<P8000Karte16*>(this);
    out.push_back(SAVE_VERSION);
    {
        auto a = k1520::ZAr::schreiber(out);
        Z8kRunState r = cpu_.runState();
        visitZ8000(a, self->cpu_, r);
    }
    mmu_.serialize(out);
    dram_.serialize(out);
    out.insert(out.end(), sram_.begin(), sram_.end());
    for (const Z80CTC* c : {&ctc0_, &ctc1_}) { c->serialize(out); c->serializeTakt(out); }
    sio0_.serialize(out);
    sio1_.serialize(out);
    for (const Z80PIO* q : {&pio0_, &pio1_, &pio2_}) { q->serialize(out); q->serializeHandshake(out); }
    auto a = k1520::ZAr::schreiber(out);
    bool re = reset_eingang_, tr = treset_eingang_, mr = mreset_, mn = manual_nmi_, np = nmi_pegel_,
         pf = paritaet_ff_;
    int32_t rest = nmi_rest_;
    uint64_t t = zeit_;
    a.flag(re); a.flag(tr); a.flag(mr); a.flag(mn); a.flag(np); a.flag(pf); a.num(rest); a.num(t);
    for (auto& po : self->pins_) for (Pins& pn : po) { a.num(pn.pegel); a.num(pn.maske); }
    for (auto& an : self->anschluesse_) a.flag(an->dcdRef());
}

bool P8000Karte16::deserialize(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 1 || *p != SAVE_VERSION) return false;
    ++p;
    {
        auto a = k1520::ZAr::leser(p, end);
        Z8kRunState r{};
        visitZ8000(a, cpu_, r);
        if (!a.ok) return false;
        cpu_.setRunState(r);
        p = a.p;
    }
    if (!mmu_.deserialize(p, end)) return false;   // stellt /SEGT der CPU wieder her
    if (!dram_.deserialize(p, end)) return false;
    if (size_t(end - p) < sram_.size()) return false;
    std::copy(p, p + sram_.size(), sram_.begin());
    p += sram_.size();
    for (Z80CTC* c : {&ctc0_, &ctc1_})
        if (!c->deserialize(p, end) || !c->deserializeTakt(p, end)) return false;
    if (!sio0_.deserialize(p, end) || !sio1_.deserialize(p, end)) return false;
    for (Z80PIO* q : {&pio0_, &pio1_, &pio2_})
        if (!q->deserialize(p, end) || !q->deserializeHandshake(p, end)) return false;
    auto a = k1520::ZAr::leser(p, end);
    bool re = false, tr = false, mr = false, mn = false, np = false, pf = false;
    int32_t rest = 0;
    uint64_t t = 0;
    a.flag(re); a.flag(tr); a.flag(mr); a.flag(mn); a.flag(np); a.flag(pf); a.num(rest); a.num(t);
    for (auto& po : pins_) for (Pins& pn : po) { a.num(pn.pegel); a.num(pn.maske); }
    for (auto& an : anschluesse_) a.flag(an->dcdRef());
    if (!a.ok) return false;
    p = a.p;
    reset_eingang_ = re; treset_eingang_ = tr; mreset_ = mr; manual_nmi_ = mn; nmi_pegel_ = np;
    paritaet_ff_ = pf; nmi_rest_ = rest; zeit_ = t;
    seriell_geaendert_ = true;
    peri_.markIntDirty();
    return true;
}
