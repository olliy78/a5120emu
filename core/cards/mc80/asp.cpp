/**
 * @file asp.cpp
 * @brief ASP des MC80.3x — Dekodierung, Interruptkette SIO > CTC > PIO, Tastaturkanal.
 * @see asp.h, doc/mc80/hardware_3x.md §6
 */

#include "core/cards/mc80/asp.h"

Mc80Asp::Mc80Asp() : Mc80Asp(Config{}) {}

Mc80Asp::Mc80Asp(const Config& cfg) : cfg_(cfg)
{
    sio_.channelA().setzeDCD(true);
    sio_.channelA().setzeCTS(true);
    ctc_.setIntAusVerwirftAnforderung(true);   // EGOS TASTE_ISR 046FH, s. z80_ctc.h
    updateInternalChain();
}

void Mc80Asp::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, cfg_.io_base, 12);
}

void Mc80Asp::reset()
{
    sio_.reset();
    ctc_.reset();
    pio_.reset();
    updateInternalChain();
}

uint8_t Mc80Asp::ioRead(uint8_t port)
{
    const uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base);
    uint8_t r = 0xFF;
    switch (rel >> 2) {
        case 0: r = sio_.ioRead(rel & 3); break;
        case 1: r = ctc_.ioRead(rel & 3); break;
        case 2: r = pio_.ioRead(rel & 3); break;
        default: break;
    }
    updateInternalChain();
    return r;
}

void Mc80Asp::ioWrite(uint8_t port, uint8_t data)
{
    const uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base);
    switch (rel >> 2) {
        case 0: sio_.ioWrite(rel & 3, data); break;
        case 1: ctc_.ioWrite(rel & 3, data); break;
        case 2: pio_.ioWrite(rel & 3, data); break;
        default: break;
    }
    updateInternalChain();
}

// ─── Interruptkette: IEI → SIO → CTC → PIO → IEO ────────────────────────────

void Mc80Asp::updateInternalChain()
{
    sio_.setIEI(iei_in_);
    ctc_.setIEI(sio_.getIEO());
    pio_.setIEI(ctc_.getIEO());
}

void Mc80Asp::setIEI(bool iei)
{
    iei_in_ = iei;
    updateInternalChain();
}

bool Mc80Asp::hasInterrupt() const
{
    return sio_.hasInterrupt() || ctc_.hasInterrupt() || pio_.hasInterrupt();
}

uint8_t Mc80Asp::getVector() const
{
    auto& self = const_cast<Mc80Asp&>(*this);
    uint8_t v = 0xFF;
    if      (sio_.hasInterrupt()) v = sio_.getVector();
    else if (ctc_.hasInterrupt()) v = ctc_.getVector();
    else if (pio_.hasInterrupt()) v = pio_.getVector();
    self.updateInternalChain();
    return v;
}

void Mc80Asp::onRETI()
{
    // Nur der hoechste Baustein in Bedienung nimmt das RETI an (Daisy-Chain).
    sio_.onRETI();
    ctc_.onRETI();
    pio_.onRETI();
    updateInternalChain();
}

const char* Mc80Asp::intDeviceName() const
{
    if (sio_.hasInterrupt()) return "MC80-ASP SIO";
    if (ctc_.hasInterrupt()) return "MC80-ASP CTC";
    if (pio_.hasInterrupt()) return "MC80-ASP PIO";
    return "MC80-ASP";
}

// ─── Tastaturkanal ───────────────────────────────────────────────────────────

bool Mc80Asp::tastaturEmpfaengerFrei() const
{
    return const_cast<Mc80Asp*>(this)->sio_.channelA().empfaengerFrei();
}

void Mc80Asp::empfange(uint8_t b)
{
    sio_.channelA().rxByte(b);
    updateInternalChain();
}

bool Mc80Asp::hatGesendet() const
{
    return const_cast<Mc80Asp*>(this)->sio_.channelA().senderHatZeichen();
}

uint8_t Mc80Asp::nimmGesendet()
{
    const uint8_t b = static_cast<uint8_t>(sio_.channelA().txGet() & 0x7F);
    updateInternalChain();
    return b;
}
