/**
 * @file k2521.cpp
 * @brief ZRE K2521 des PRG 710 — Speicherfenster, CPU-Verdrahtung, CTC-Kaskade, Interruptkette.
 * @see k2521.h, doc/design/20_prg710.md §3.1, §7.2
 */

#include "core/cards/k2521/k2521.h"
#include "core/cards/k2521/rom_prg710.h"
#include "core/cards/k2521/rom_prg710_1.h"
#include "core/cards/k2521/rom_k8915g2.h"
#include "core/logger.h"

K2521::Config K2521::Config::prg710()
{
    Config c;
    c.rom     = PRG710_ZRE_ROM;
    c.rom_len = sizeof(PRG710_ZRE_ROM);
    return c;
}

K2521::Config K2521::Config::prg710_1()
{
    Config c;
    c.rom     = PRG710_1_ZRE_ROM;
    c.rom_len = sizeof(PRG710_1_ZRE_ROM);
    return c;
}

K2521::Config K2521::Config::k8915g2()
{
    Config c;
    c.rom     = K8915G2_ZRE_ROM;       // Abzug unverändert, Byte 0A33H = 04H (F9)
    c.rom_len = sizeof(K8915G2_ZRE_ROM);
    // X10:1–4 / X11:1–4 am Gerät des Anwenders alle geschlossen (2026-10-05, F22):
    // die CTC-Kaskade ZC/TO0→CLK1, TO1→TRG2, TO2→TRG3 steht.  Der vierte Draht
    // ist in der Beschreibung S.6 nicht erklärt [?] und hier nicht abgebildet.
    c.kaskade_to0_clk1 = true;
    c.kaskade_to1_clk2 = true;
    c.kaskade_to2_clk3 = true;
    c.iei_quelle       = IeiQuelle::System;
    return c;
}

K2521::K2521(K1520Bus& bus) : K2521(bus, Config{}) {}

K2521::K2521(K1520Bus& bus, const Config& cfg)
    : bus_(bus), cfg_(cfg)
{
    if (cfg_.rom_len > 0x0C00) cfg_.rom_len = 0x0C00;   // 3 × U555

    // Speicherweg: Vorgabe Systembus; setSpeicherweg() leitet über die Speicherverwaltung.
    cpu_.readByte  = [this](uint16_t a) {
        return lesen_ ? lesen_(a) : bus_.memRead(a);
    };
    cpu_.writeByte = [this](uint16_t a, uint8_t d) {
        if (schreiben_) schreiben_(a, d); else bus_.memWrite(a, d);
    };
    // Volle 16-Bit-E/A-Adresse (AB8–15 für die Speicherverwaltung, AP-P1b).
    cpu_.readPort     = [this](uint16_t p)            { return bus_.ioRead(p); };
    cpu_.writePort    = [this](uint16_t p, uint8_t d) { bus_.ioWrite(p, d); };
    cpu_.retiCallback = [this]()                      { bus_.signalRETI(); };

    // CTC-Kaskade X10–X11: ZC/TO[n] → CLK/TRG[n+1]; zusätzlich nach außen (Koppelbus).
    ctc_.setZCTOCallback([this](int ch, bool lvl) {
        if (ch == 0 && cfg_.kaskade_to0_clk1) ctc_.clkTrg(1, lvl);
        if (ch == 1 && cfg_.kaskade_to1_clk2) ctc_.clkTrg(2, lvl);
        if (ch == 2 && cfg_.kaskade_to2_clk3) ctc_.clkTrg(3, lvl);
        if (zcto_ext_) zcto_ext_(ch, lvl);
    });
}

void K2521::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, cfg_.ctc_base, 4);
    bus.registerIO(this, cfg_.pio_base, 4);
}

void K2521::setSpeicherweg(LeseFn lesen, SchreibFn schreiben)
{
    lesen_     = std::move(lesen);
    schreiben_ = std::move(schreiben);
}

void K2521::powerOn(uint8_t fill)
{
    ram_.fill(fill);
    reset();
}

void K2521::reset()
{
    ctc_.reset();
    pio_.reset();
    cpu_.reset();
}

// ─── E/A ─────────────────────────────────────────────────────────────────────

uint8_t K2521::pioPort(uint8_t rel) const
{
    // Karte: B/A = AB0, C/D = AB1; Baustein: 0 = A-D, 1 = A-S, 2 = B-D, 3 = B-S.
    return cfg_.pio_ab_an_a0 ? uint8_t(((rel & 1) << 1) | ((rel >> 1) & 1)) : rel;
}

uint8_t K2521::ioRead(uint8_t port)
{
    if (uint8_t(port - cfg_.ctc_base) < 4) return ctc_.ioRead(port & 0x03);
    if (uint8_t(port - cfg_.pio_base) < 4) return pio_.ioRead(pioPort(port & 0x03));
    return 0xFF;
}

void K2521::ioWrite(uint8_t port, uint8_t data)
{
    if (uint8_t(port - cfg_.ctc_base) < 4) { ctc_.ioWrite(port & 0x03, data); return; }
    if (uint8_t(port - cfg_.pio_base) < 4) { pio_.ioWrite(pioPort(port & 0x03), data); return; }
}

// ─── Interruptkette: IEI → CTC → PIO → IEO ───────────────────────────────────

void K2521::setIEI(bool iei)
{
    // X14–X15: bei /IODI hängt die Karte an der Spitze der Kette.
    if (cfg_.iei_quelle == IeiQuelle::HoechstePrioritaet) iei = true;
    ctc_.setIEI(iei);
    pio_.setIEI(ctc_.getIEO());
}

uint8_t K2521::getVector() const
{
    if (ctc_.hasInterrupt()) return ctc_.getVector();
    if (pio_.hasInterrupt()) return pio_.getVector();
    return 0xFF;
}

const char* K2521::intDeviceName() const
{
    if (ctc_.hasInterrupt()) return "K2521 CTC";
    if (pio_.hasInterrupt()) return "K2521 PIO";
    return "K2521";
}

void K2521::onRETI()
{
    ctc_.onRETI();
    pio_.onRETI();
}

// ─── Speicher ────────────────────────────────────────────────────────────────

uint8_t K2521::memRead(uint16_t a) const
{
    if (a < cfg_.rom_len && cfg_.rom) return cfg_.rom[a];
    if (a < 0x0C00) return 0xFF;                 // leere U555-Fassung
    if (a < 0x1000) return ram_[a - 0x0C00];
    return 0xFF;
}

void K2521::memWrite(uint16_t a, uint8_t d)
{
    if (a >= 0x0C00 && a < 0x1000) ram_[a - 0x0C00] = d;
}
