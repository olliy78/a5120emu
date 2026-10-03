/**
 * @file prg710_speicher.cpp
 * @brief Speicherverwaltung E8H–EBH + OPS-RAM des PRG 710 — siehe prg710_speicher.h.
 */

#include "core/cards/prg710_speicher/prg710_speicher.h"
#include "core/logger.h"
#include <algorithm>

void Prg710Speicher::reset() {
    attr_.fill(0);
    seite_.fill(0);
    freigabe_ = 0;
}

void Prg710Speicher::powerOn(uint8_t fill) {
    ops_.fill(fill);
    reset();
}

uint8_t Prg710Speicher::ioRead(uint8_t) {
    return 0xFF;   // die Register werden nirgends gelesen (§4b)
}

void Prg710Speicher::ioWrite(uint8_t port, uint8_t data) {
    // Registerindex = A12–A15 der E/A-Adresse (wie der Attributspeicher des EM256)
    const int n = (bus_.ioAddress() >> 12) & 0x0F;
    switch (port & 3) {
    case 0:   // E8H
        attr_[n] = data;
        // belegt: 0FH, 10H, F0H, FFH; alles andere [?] unbekannt, nur gespeichert
        if (data != 0x0F && data != 0x10 && data != 0xF0 && data != 0xFF)
            LOG_DEBUG("PRG710MEM", "[?] E8H[%X] := %02X unbelegter Wert", n, data);
        break;
    case 1:   // E9H
        LOG_DEBUG("PRG710MEM", "[?] E9H := %02X (unbelegt)", data);
        break;
    case 2:   // EAH
        seite_[n] = data & 0x0F;
        break;
    case 3:   // EBH
        freigabe_ = data;
        if (data != 0x00 && data != 0x0F)
            LOG_DEBUG("PRG710MEM", "[?] EBH := %02X unbelegter Wert (nur 00H/0FH belegt, wirkt als !=0)", data);
        break;
    }
}

Prg710Speicher::Ort Prg710Speicher::ortVon(uint16_t addr) const {
    const int n = addr >> 12;
    if (freigabe_ == 0) {   // Abbildung aus: ZRE 0000–0FFF, VRAM F800–FFFF, sonst OPS unter gleicher Adresse
        if (addr < ZRE_ENDE)      return {Quelle::Zre, addr};
        if (addr >= VRAM_ANFANG)  return {Quelle::Vram, addr};
        return {Quelle::Ops, addr};
    }
    if ((attr_[n] & 0x0F) != 0) {   // Systemkarte dieser Seite
        if (n == 0)                         return {Quelle::Zre, addr};
        if (n == 0x0F && addr >= VRAM_ANFANG) return {Quelle::Vram, addr};
        if (n == 0x0F)                      return {Quelle::Ops, uint16_t((seite_[n] << 12) | (addr & 0x0FFF))};
        return {Quelle::Leer, addr};
    }
    return {Quelle::Ops, uint16_t((seite_[n] << 12) | (addr & 0x0FFF))};
}

uint8_t Prg710Speicher::memRead(uint16_t addr) {
    const Ort o = ortVon(addr);
    switch (o.quelle) {
    case Quelle::Zre:  return zre_r_  ? zre_r_(o.offset)  : 0xFF;
    case Quelle::Vram: return vram_r_ ? vram_r_(o.offset) : 0xFF;
    case Quelle::Ops:  return ops_[o.offset];
    case Quelle::Leer: break;
    }
    return 0xFF;
}

void Prg710Speicher::memWrite(uint16_t addr, uint8_t data) {
    const Ort o = ortVon(addr);
    switch (o.quelle) {
    case Quelle::Zre:  if (zre_w_)  zre_w_(o.offset, data);  break;
    case Quelle::Vram: if (vram_w_) vram_w_(o.offset, data); break;
    case Quelle::Ops:  ops_[o.offset] = data; break;
    case Quelle::Leer: break;
    }
}
