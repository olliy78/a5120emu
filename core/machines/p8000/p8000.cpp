/**
 * @file p8000.cpp
 * @brief P8000 — Verdrahtung und Laufschleife der 8-Bit-Seite (AP P7a).
 * @see p8000.h, doc/design/25_p8000.md §10.2/§10.6; Vorbild core/machines/pc1715/pc1715.cpp
 */

#include "core/machines/p8000/p8000.h"
#include "core/logger.h"

P8000Karte8::Config P8000Machine::karteConfig(const Config& c)
{
    P8000Karte8::Config k;
    k.index = c.index8;
    k.mon8 = c.mon8;
    k.takt_hz = c.takt8_hz;
    k.latch_start = c.latch_start;
    k.adp_start = c.adp_start;
    k.ram_fuellwert = c.ram_fuellwert;
    return k;
}

P8000Floppy8::Config P8000Machine::floppyConfig(const Config& c)
{
    P8000Floppy8::Config f;
    f.laufwerke = c.laufwerke;
    return f;
}

P8000Machine::P8000Machine() : P8000Machine(Config{}) {}
P8000Machine::~P8000Machine() = default;

P8000Machine::P8000Machine(const Config& cfg)
    : cfg_(cfg)
    , karte_(bus_, karteConfig(cfg))
    , floppy_(karte_, floppyConfig(cfg))
    , term_anschluss_(karte_.anschluss(KONSOLE_TTY), term_, cfg.takt8_hz)
    , hub_(cfg.takt8_hz)
{
    karte_.cpu().abortBeforeExecute = [this] { return stop_.load(std::memory_order_relaxed); };
    // tty0, tty2, tty3 nach außen (Reihenfolge = C-ABI-Index); tty1 hängt am Kern-Terminal.
    for (auto* a : serielleAnschluesse()) hub_.registriere(*a);
}

std::vector<k1520::serial::SerialAnschluss*> P8000Machine::serielleAnschluesse()
{
    std::vector<k1520::serial::SerialAnschluss*> v;
    for (int t = 0; t < P8000Karte8::TTY_ANZAHL; ++t)
        if (t != KONSOLE_TTY) v.push_back(&karte_.anschluss(t));
    return v;
}

// ─── Lebenslauf ──────────────────────────────────────────────────────────────

void P8000Machine::nachReset()
{
    stop_.store(false);
    hub_.gastZurueckgesetzt();
    serial_naechst_ = 0;
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.markIntDirty();
}

void P8000Machine::powerOn()
{
    lw().flushDisks();
    karte_.powerOn();        // RAM/ADP/Latches vorbelegen, RESI = 1, /RES (Floppy über Reset-Haken)
    nachReset();
    LOG_INFO("P8000", "Netz ein (8-Bit-Teil)");
}

void P8000Machine::reset()
{
    bestueckungAbschliessen();
    lw().flushDisks();
    karte_.reset();          // Taste /RESP: RESI = 0
    nachReset();
    LOG_INFO("P8000", "Reset-Taste");
}

// ─── Terminal ────────────────────────────────────────────────────────────────

uint8_t P8000Machine::screenChar(int col, int row) const
{
    if (col < 0 || row < 0 || col >= k1520::p8000::Terminal::SPALTEN ||
        row >= k1520::p8000::Terminal::ZEILEN)
        return 0;
    const auto& z = term_.zelle(row, col);
    return z.feld ? uint8_t(' ') : z.zeichen;
}

void P8000Machine::keyPress(uint32_t k, bool, bool ctrl)
{
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k, ctrl});
}

void P8000Machine::tastenAbgeben()
{
    std::deque<Taste> t;
    {
        std::lock_guard<std::mutex> lk(tasten_sperre_);
        t.swap(tasten_);
    }
    using k1520::p8000::TerminalTaste;
    for (const Taste& e : t) {
        switch (e.code) {
            case 0x01000004: case 0x01000005: term_.taste(TerminalTaste::CR);  continue;   // Return/Enter
            case 0x01000000: term_.taste(TerminalTaste::ESC); continue;                    // Escape
            case 0x01000003: term_.taste(TerminalTaste::BS);  continue;                    // Backspace
            case 0x01000001: term_.taste(TerminalTaste::HT);  continue;                    // Tab
            case 0x01000007: term_.taste(TerminalTaste::DEL); continue;                    // Delete
            default: break;
        }
        if (e.code == '\r' || e.code == '\n') { term_.taste(TerminalTaste::CR); continue; }
        if (e.code < 0x80) term_.zeichenTaste(static_cast<uint8_t>(e.code), e.ctrl);
    }
}

// ─── Laufwerke ───────────────────────────────────────────────────────────────

bool P8000Machine::isMotorOn(int d) const
{
    return const_cast<P8000Floppy8&>(floppy_).motorAn(d);
}

bool P8000Machine::isDiskLedOn(int d) const
{
    auto& f = const_cast<P8000Floppy8&>(floppy_);
    return d >= 0 && d < 4 && (f.motorAn(d) || f.fdc().unitBusy(d));
}

// ─── Diagnose ────────────────────────────────────────────────────────────────

uint8_t P8000Machine::memReadDebug(uint16_t a)
{
    return const_cast<P8000Karte8&>(karte_).speicher().peek(a);
}

void P8000Machine::memWriteDebug(uint16_t a, uint8_t d)
{
    karte_.speicher().memWrite(a, d);
    (void)karte_.speicher().nimmWartetakte();
}

// ─── Laufschleife (Entwurf §10.2, nur 8-Bit-Seite) ───────────────────────────

int P8000Machine::run(int max_cycles)
{
    bestueckungAbschliessen();
    tastenAbgeben();
    if (nmi_taster_.exchange(false, std::memory_order_relaxed)) karte_.nmiTaste();

    int remaining = max_cycles;
    while (remaining > 0 && !stop_.load(std::memory_order_relaxed)) {
        k1520::logging::Logger::instance().update(total_cycles_, karte_.cpu().PC, 0);
        const int n = karte_.schritt();      // DMA hält die CPU; EPROM-Wartetakte eingerechnet
        if (n == 0) break;                   // Debugger-Halt
        karte_.takt(n);
        floppy_.takt(n);
        remaining     -= n;
        total_cycles_ += static_cast<uint64_t>(n);

        term_anschluss_.takt(static_cast<uint64_t>(n));
        if (total_cycles_ >= serial_naechst_) serial_naechst_ = hub_.takt(total_cycles_);
    }
    lw().autoFlush(total_cycles_);
    return max_cycles - remaining;
}
