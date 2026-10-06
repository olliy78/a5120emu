/**
 * @file speicher8.cpp
 * @brief ADP-gebanktes Speichergerät der P8000-8-Bit-Karte — siehe speicher8.h.
 */

#include "core/cards/p8000/speicher8.h"
#include "core/logger.h"
#include <algorithm>

P8000Speicher8::P8000Speicher8(const Config& cfg) : cfg_(cfg) {
    rom_.fill(0xFF);
    powerOn();
}

void P8000Speicher8::setRom(const uint8_t* daten, size_t n) {
    rom_.fill(0xFF);
    std::copy(daten, daten + std::min<size_t>(n, rom_.size()), rom_.begin());
}

void P8000Speicher8::attachToBus(K1520Bus& bus) {
    bus_ = &bus;
    bus.registerIO(this, PORT_ADP, 8);
    bus.registerMem(this, 0x0000, 0x8000);
    bus.registerMem(this, 0x8000, 0x8000);
}

void P8000Speicher8::reset() {
    rff_ = false;
}

void P8000Speicher8::powerOn() {
    adp_.fill(cfg_.adp_start & 7);
    sram_.fill(cfg_.ram_fuellwert);
    dram_.fill(cfg_.ram_fuellwert);
    warte_ = 0;
    eprom_zugriffe_ = 0;
    reset();
}

uint8_t P8000Speicher8::peek(uint16_t addr) const {
    const uint8_t s = selekt(addr);
    uint8_t v = 0xFF;   // niemand treibt den Datenbus [Annahme]
    // EPROM 1 bei A12 = 0, EPROM 2 bei A12 = 1; A13–A15 werden nicht ausgewertet
    if (s & 1) v &= rom_[addr & 0x1FFF];
    if (s & 2) v &= sram_[addr & 0x7FF];
    if (s & 4) v &= dram_[addr];
    return v;
}

uint8_t P8000Speicher8::memRead(uint16_t addr) {
    const uint8_t s = selekt(addr);
    if (s & 1) { warte_ += WARTETAKTE_JE_ZUGRIFF; ++eprom_zugriffe_; }
    if (s != 0 && (s & (s - 1)))
        LOG_DEBUG("P8000MEM", "Mehrfachselektion lesend @%04X (Select %X): UND der Bänke", addr, s);
    return peek(addr);
}

void P8000Speicher8::memWrite(uint16_t addr, uint8_t d) {
    const uint8_t s = selekt(addr);
    // PROM-CE aktiv ⇒ der WAIT-Generator läuft auch bei Schreibzugriffen
    if (s & 1) { warte_ += WARTETAKTE_JE_ZUGRIFF; ++eprom_zugriffe_; }
    if (s & 2) sram_[addr & 0x7FF] = d;
    if (s & 4) dram_[addr] = d;
}

uint8_t P8000Speicher8::ioRead(uint8_t port) {
    // 04H–07H: auch IN setzt RFF (/RES-RFF ohne /RD-Bedingung); 00H–03H: WEADP nur schreibend
    if (port & 4) rff_ = true;
    return 0xFF;   // nichts treibt den Datenbus [Annahme, Messfrage 2b]
}

void P8000Speicher8::ioWrite(uint8_t port, uint8_t data) {
    if (port & 4) { rff_ = true; return; }
    // Zelle = A12–A15 der E/A-Adresse, Inhalt = D0–D2 (MH7489 /WE = WEADP, nur Schreiben)
    const int zelle = bus_ ? (bus_->ioAddress() >> 12) & 0x0F : 0;
    adp_[zelle] = data & 7;
}
