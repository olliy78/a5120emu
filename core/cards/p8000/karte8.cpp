/**
 * @file karte8.cpp
 * @brief 8-Bit-Rechnerkarte des P8000 — E/A-Dekoder, Bausteine, Kette, Reset/NMI.
 * @see karte8.h, doc/p8000/schaltplan_8bit.md, doc/design/25_p8000.md §10
 */

#include "core/cards/p8000/karte8.h"
#include "core/cards/p8000/rom_mon8.h"
#include "core/logger.h"
#include "core/serial/sio_format.h"
#include <optional>

namespace {
constexpr uint8_t PORT_CTC0  = 0x08;   // 08H–0BH
constexpr uint8_t PORT_PIO0  = 0x0C;   // 0CH–0FH
constexpr uint8_t PORT_LATCH1 = 0x10;  // 10H–13H (nur schreibend)
constexpr uint8_t PORT_LATCH2 = 0x14;  // 14H–17H (nur schreibend)
constexpr uint8_t PORT_PIO1  = 0x18;   // 18H–1BH
constexpr uint8_t PORT_PIO2  = 0x1C;   // 1CH–1FH
constexpr uint8_t PORT_FDC   = 0x20;   // 20H–23H
constexpr uint8_t PORT_SIO0  = 0x24;   // 24H–27H
constexpr uint8_t PORT_SIO1  = 0x28;   // 28H–2BH
constexpr uint8_t PORT_CTC1  = 0x2C;   // 2CH–2FH
constexpr uint8_t PORT_DMA   = 0x3C;   // 3CH–3FH
constexpr uint8_t PORT_ENDE  = 0x40;   // 30H–3FH: RES1..3 (nur X13) und DMA

inline bool in4(uint8_t port, uint8_t basis) { return uint8_t(port - basis) < 4; }
}  // namespace

// ─── DMA-Platzhalter (UA858, 3CH–3FH) ────────────────────────────────────────
// Erster der Kette (IEI fest +5 V), hier ohne Funktion: kein Interrupt, Tore lesen FFH.
// Die Anbindung (RDY, /BUSRQ, TC) kommt mit P5f.
class P8000Karte8::DmaPlatzhalter : public InterruptSlave {
public:
    void        setIEI(bool iei) override { iei_ = iei; }
    bool        getIEO() const override { return iei_; }
    bool        hasInterrupt() const override { return false; }
    const char* intDeviceName() const override { return "P8000 DMA"; }
private:
    bool iei_ = true;
};

// ─── Anschluss je tty (Entwurf 19, Entwurf 25 §10.6) ─────────────────────────
// tty0/1/2/3 = SIO0-A, SIO0-B, SIO1-A, SIO1-B; Takt: CTC1 K0, CTC1 K1, CTC1 K2, CTC0 K0,
// danach ÷ 2 (7474 8D11/7D11).  Namen und Stecker nach der Gerätebeschriftung (X4…X7)
// sind eine Annahme des Entwurfs („ttyN (X4…X7)").
class P8000Karte8::Anschluss : public k1520::serial::SerialAnschluss {
public:
    Anschluss(P8000Karte8& k, int tty) : k_(k), tty_(tty) {
        name_ = "tty" + std::to_string(tty);
        stecker_ = "X" + std::to_string(4 + tty);
    }
    const char* name() const override { return name_.c_str(); }
    const char* stecker() const override { return stecker_.c_str(); }
    bool v24() const override { return true; }

    k1520::serial::SerialFormat format() const override {
        const Z80SIO::Channel::Format f = ch().format();
        const Z80CTC& c = tty_ == 3 ? k_.ctc0_ : k_.ctc1_;
        const int kanal = tty_ == 3 ? 0 : tty_;
        return k1520::serial::serialFormatRechnenQ16(f.teiler, f.tx_bits, f.paritaet, f.stopp_halbe,
                                                     c.teilerTakteQ16(kanal) * 2, k_.cfg_.takt_hz);
    }
    bool senderHatZeichen() const override { return ch().senderHatZeichen(); }
    uint8_t senderNimm() override {
        uint8_t b = ch().txGet();
        const uint8_t bits = ch().format().tx_bits;
        if (bits < 8) b &= static_cast<uint8_t>((1u << bits) - 1);
        k_.seriell_geaendert_ = true;   // Tx leer → Tx-Interrupt möglich
        return b;
    }
    bool empfaengerFrei() const override { return ch().empfaengerFrei(); }
    void empfange(uint8_t b) override { ch().rxByte(b); k_.seriell_geaendert_ = true; }
    bool rts() const override { return ch().rts(); }
    bool dtr() const override { return ch().dtr(); }
    void setzeEingaenge(bool, bool, bool dcd) override {
        dcd_ = dcd;
        wirke();
    }
    bool breakGesendet() const override { return ch().breakSenden(); }
    void breakEmpfang(bool aktiv) override { ch().setzeBreakEmpfang(aktiv); k_.seriell_geaendert_ = true; }

    /// /CTS: tty0 = RTS (Lokalschleife), übrige fest aktiv [Annahme, Schaltplan §8]; /DCD vom Anschluss.
    void wirke() {
        ch().setzeCTS(tty_ == 0 ? ch().rts() : true);
        ch().setzeDCD(dcd_);
        k_.seriell_geaendert_ = true;
    }

private:
    Z80SIO::Channel& ch() const {
        Z80SIO& s = tty_ < 2 ? k_.sio0_ : k_.sio1_;
        return (tty_ & 1) ? s.channelB() : s.channelA();
    }
    P8000Karte8& k_;
    int          tty_;
    bool         dcd_ = false;
    std::string  name_, stecker_;
};

P8000Karte8::~P8000Karte8() = default;

k1520::serial::SerialAnschluss& P8000Karte8::anschluss(int tty)
{
    return *anschluesse_[static_cast<size_t>(tty & 3)];
}

// ─── Aufbau ──────────────────────────────────────────────────────────────────

P8000Karte8::P8000Karte8(K1520Bus& bus, const Config& cfg)
    : bus_(bus), cfg_(cfg), sp_(P8000Speicher8::Config{cfg.adp_start, cfg.ram_fuellwert}),
      dma_(std::make_unique<DmaPlatzhalter>())
{
    for (int i = 0; i < TTY_ANZAHL; ++i)
        anschluesse_[static_cast<size_t>(i)] = std::make_unique<Anschluss>(*this, i);

    switch (cfg.mon8) {
        case Config::Mon8::V3_0:         sp_.setRom(P8K_MON8_3_0, sizeof P8K_MON8_3_0); break;
        case Config::Mon8::V3_1:         sp_.setRom(P8K_MON8_3_1, sizeof P8K_MON8_3_1); break;
        case Config::Mon8::V3_1_Nur8Bit: sp_.setRom(P8K_MON8_3_1_NUR8BIT, sizeof P8K_MON8_3_1_NUR8BIT); break;
        case Config::Mon8::V2_1_Nur8Bit: sp_.setRom(P8K_MON8_2_1_NUR8BIT, sizeof P8K_MON8_2_1_NUR8BIT); break;
    }

    ctc0_.setResetLaedtZeitkonstante(true);   // MON8-Hardwaretest Fehler 16: Reset ⇒ Zähler = TC
    ctc1_.setResetLaedtZeitkonstante(true);
    // Modus 3 nach Datenblatt flankengetriggert (U855 §6.4): KINIT (OS.INIT der WEGA-
    // Startdiskette) gibt PIO0-B mit Maske FEH „aktiv high" frei, während INT_8 über den
    // Pull-up auf 1 liegt — pegelbasiert sofort ein Interrupt auf den leeren Vektor 0F12H.
    pio0_.setModus3Flanke(true);
    pio1_.setModus3Flanke(true);
    pio2_.setModus3Flanke(true);

    // Baudtakt CPBAUD = Quarz ÷ 8 (exakt 1,229 MHz): CTC0 K0, CTC1 K0–K2 (§5).
    const uint64_t cpbaud = cfg.baudquarz_hz / 8;
    ctc0_.setzeEingangsTakt(0, cpbaud, cfg.takt_hz);
    for (int k = 0; k < 3; ++k) ctc1_.setzeEingangsTakt(k, cpbaud, cfg.takt_hz);

    cpu_.readByte     = [this](uint16_t a)            { return bus_.memRead(a); };
    cpu_.writeByte    = [this](uint16_t a, uint8_t d) { bus_.memWrite(a, d); };
    cpu_.readPort     = [this](uint16_t p)            { return bus_.ioRead(p); };
    cpu_.writePort    = [this](uint16_t p, uint8_t d) { bus_.ioWrite(p, d); };
    cpu_.retiCallback = [this]()                      { bus_.signalRETI(); };

    // 08H–3FH (ohne Lücken in der Registrierung; unbelegte Tore antworten FFH)
    sp_.attachToBus(bus_);
    bus_.registerIO(this, PORT_CTC0, PORT_ENDE - PORT_CTC0);
    ketteAnlegen();
    latch_.fill(cfg_.latch_start);
}

void P8000Karte8::setzeDmaKettenglied(InterruptSlave* dma)
{
    bus_.setInterruptChain({dma, &pio2_, &ctc0_, &sio0_, &sio1_, &pio0_, &pio1_, &ctc1_});
}

void P8000Karte8::ketteAnlegen()
{
    // Schaltplan §3: DMA – PIO2 – CTC0 – SIO0 – SIO1 – (XP6) – PIO0 – PIO1 – CTC1.
    // Die 7411-Vorausschau ändert die Reihenfolge nicht.
    bus_.setInterruptChain({dma_.get(), &pio2_, &ctc0_, &sio0_, &sio1_, &pio0_, &pio1_, &ctc1_});
}

// ─── Pins der PIOs ───────────────────────────────────────────────────────────

void P8000Karte8::setzePinTreiber(int pio_nr, int port, uint8_t pegel, uint8_t treibMask)
{
    pins_[pio_nr][port] = {pegel, treibMask};
    pinsAnlegen(pio_nr);
}

void P8000Karte8::pinsAnlegen(int i)
{
    Z80PIO& p = pio(i);
    Pins a = pins_[i][0], b = pins_[i][1];
    if (i == 2) {   // RESI treibt PIO2-A7 (Eingang)
        a.pegel = static_cast<uint8_t>((a.pegel & 0x7F) | (resi_ ? 0x80 : 0));
        a.maske |= 0x80;
    }
    p.setExternA(a.pegel, a.maske);
    p.setExternB(b.pegel, b.maske);
}

// ─── Lebenslauf ──────────────────────────────────────────────────────────────

void P8000Karte8::resetBausteine()
{
    sp_.reset();                       // RFF Q = 0
    ctc0_.reset(); ctc1_.reset();
    sio0_.reset(); sio1_.reset();
    pio0_.reset(); pio1_.reset(); pio2_.reset();   // /PM1 = /M1 ∧ /RES (§2)
    for (int i = 0; i < 3; ++i) pinsAnlegen(i);
    cpu_.reset();
    for (auto& a : anschluesse_) a->wirke();
    seriell_geaendert_ = true;
    if (reset_haken_) reset_haken_();
    bus_.markIntDirty();
}

void P8000Karte8::powerOn()
{
    sp_.powerOn();
    latch_.fill(cfg_.latch_start);
    resi_ = true;
    resetBausteine();
}

void P8000Karte8::reset()
{
    resi_ = false;
    resetBausteine();
}

void P8000Karte8::nmiTaste()
{
    // /NMI(U880) = NAND(/NMI-UM, NMIP); /NMI-U8000 = NAND(NOT /NMI-UM, NMIP)
    if (b7eff()) {
        bus_.assertNMI();
        LOG_INFO("P8000", "NMI-Taste → U880");
    } else {
        LOG_INFO("P8000", "NMI-Taste → 16-Bit-Karte (PIO0-B7 = 0)");
        if (nmi16_cb_) nmi16_cb_();
    }
}

int P8000Karte8::schritt()
{
    if (bm_anfrage_ && bm_anfrage_()) {   // DMA hält die CPU (/BUSRQ → /BUSAK)
        int used = bm_schritt_();
        if (used == 0) used = 4;          // Bus gehalten, kein RDY
        if (const uint32_t w = sp_.nimmWartetakte(); w > 0) used += static_cast<int>(w);
        cpu_.cycles += static_cast<uint64_t>(used);
        bus_.markIntDirty();              // Blockende kann einen Interrupt melden
        return used;
    }
    bus_.updateInterruptChain();
    if (bus_.isINT() && cpu_.IFF1) {
        const uint8_t vec = bus_.interruptAcknowledge();
        cpu_.interrupt(vec);
    }
    if (bus_.isNMI()) {
        cpu_.nmi();
        bus_.clearNMI();
    }
    int used = cpu_.step();
    if (used == 0) return 0;
    // EPROM-Zugriffe (auch M1) kosten zwei Wartetakte (Schaltplan §1.1).
    if (const uint32_t w = sp_.nimmWartetakte(); w > 0) {
        used += static_cast<int>(w);
        cpu_.cycles += w;
    }
    return used;
}

void P8000Karte8::takt(int n)
{
    bool dirty = ctc0_.clockTick(n);
    dirty |= ctc1_.clockTick(n);
    dirty |= nimmSeriellGeaendert();
    if (dirty) bus_.markIntDirty();
}

// ─── E/A ─────────────────────────────────────────────────────────────────────

uint8_t P8000Karte8::ioRead(uint8_t port)
{
    if (in4(port, PORT_CTC0)) return ctc0_.ioRead(port & 3);
    if (in4(port, PORT_PIO0)) return pio0_.ioRead(port & 3);
    if (in4(port, PORT_PIO1)) return pio1_.ioRead(port & 3);
    if (in4(port, PORT_PIO2)) return pio2_.ioRead(port & 3);
    if (in4(port, PORT_SIO0)) return sio0_.ioRead(port & 3);
    if (in4(port, PORT_SIO1)) return sio1_.ioRead(port & 3);
    if (in4(port, PORT_CTC1)) return ctc1_.ioRead(port & 3);
    if (in4(port, PORT_FDC))  return fdcLesen ? fdcLesen(port & 1) : 0xFF;
    if (uint8_t(port - PORT_DMA) < 4) return dmaLesen ? dmaLesen() : 0xFF;
    // 10H–17H: DS8282 hat /OE fest Low und nur Ausgänge zur 16-Bit-Seite — nicht lesbar;
    // 30H–3FH: RES1..3 (nur X13) ⇒ offener Bus; 3CH–3FH = DMA (Haken)
    return 0xFF;
}

void P8000Karte8::ioWrite(uint8_t port, uint8_t data)
{
    if (in4(port, PORT_CTC0)) { ctc0_.ioWrite(port & 3, data); return; }
    if (in4(port, PORT_CTC1)) { ctc1_.ioWrite(port & 3, data); return; }
    if (in4(port, PORT_PIO0) || in4(port, PORT_PIO1) || in4(port, PORT_PIO2)) {
        const int i = in4(port, PORT_PIO0) ? 0 : (in4(port, PORT_PIO1) ? 1 : 2);
        pio(i).ioWrite(port & 3, data);
        pinsAnlegen(i);   // Moduswechsel lädt die Eingabebits nicht aus den Pins nach
        return;
    }
    if (in4(port, PORT_SIO0) || in4(port, PORT_SIO1)) {
        Z80SIO& s = in4(port, PORT_SIO0) ? sio0_ : sio1_;
        s.ioWrite(port & 3, data);
        for (auto& a : anschluesse_) a->wirke();   // WR5-RTS wirkt auf /CTS tty0
        if (!wait_gewarnt_ && ((s.channelA().wr[1] | s.channelB().wr[1]) & 0x80)) {
            wait_gewarnt_ = true;
            LOG_WARN("P8000", "SIO WR1 D7 (Wait-Funktion, /W/RDY an /WAIT) gesetzt — nicht nachgebildet");
        }
        return;
    }
    if (in4(port, PORT_LATCH1) || in4(port, PORT_LATCH2)) {   // STB = NOR(/CE-Lx, /WR), 4-fach gespiegelt
        const int l = in4(port, PORT_LATCH2) ? 1 : 0;
        latch_[static_cast<size_t>(l)] = data;
        if (latch_cb_) latch_cb_(l, data);
        return;
    }
    if (in4(port, PORT_FDC)) { if (fdcSchreiben) fdcSchreiben(port & 1, data); return; }
    if (uint8_t(port - PORT_DMA) < 4) { if (dmaSchreiben) dmaSchreiben(data); return; }
    // 30H–3BH: nichts auf der Karte
}
