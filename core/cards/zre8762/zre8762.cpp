/**
 * @file zre8762.cpp
 * @brief ZRE 045-8762 des K8915 — Speicherlogik nach Blatt 3 und CPU-Verdrahtung.
 * @see zre8762.h, doc/design/16_k8915.md §4.2a
 */

#include "core/cards/zre8762/zre8762.h"
#include "core/cards/zre8762/rom_data.h"
#include "core/logger.h"

K8915Zre::K8915Zre(K1520Bus& bus) : K8915Zre(bus, Config{}) {}

K8915Zre::K8915Zre(K1520Bus& bus, const Config& cfg)
    : bus_(bus), cfg_(cfg)
{
    // Die CPU sieht zuerst die Karte (ROM/Bank 1/Bank 2), erst nicht gewählte
    // Adressen gehen auf den Systembus (D12 → D28, s. Kopf von zre8762.h).
    cpu_.readByte     = [this](uint16_t a)            { return memRead(a); };
    cpu_.writeByte    = [this](uint16_t a, uint8_t d) { memWrite(a, d); };
    cpu_.readPort     = [this](uint16_t p)            { return bus_.ioRead(p & 0xFF); };
    cpu_.writePort    = [this](uint16_t p, uint8_t d) { bus_.ioWrite(p & 0xFF, d); };
    cpu_.retiCallback = [this]()                      { bus_.signalRETI(); };
    rebuildMap();
}

void K8915Zre::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, cfg_.ctc_base, 4);
    // D31/D33 dekodieren A7–A2; A1/A0 bleiben frei ⇒ das Register liegt vierfach.
    bus.registerIO(this, cfg_.reg_base, 4);
}

void K8915Zre::powerOn(uint8_t fill)
{
    for (auto& bank : ram_) bank.fill(fill);
    reset();
}

void K8915Zre::reset()
{
    ctc_.reset();
    setReg(0x00);          // CLR des DS8212 an /RESET
    cpu_.reset();
}

// ─── E/A ─────────────────────────────────────────────────────────────────────

uint8_t K8915Zre::ioRead(uint8_t port)
{
    if (static_cast<uint8_t>(port - cfg_.ctc_base) < 4)
        return ctc_.ioRead(port & 0x03);
    // Das Register ist nur schreibbar (Takt aus /IORQ ∧ /WR); ein IN liest den
    // offenen Datenbus.
    return 0xFF;
}

void K8915Zre::ioWrite(uint8_t port, uint8_t data)
{
    if (static_cast<uint8_t>(port - cfg_.ctc_base) < 4) {
        ctc_.ioWrite(port & 0x03, data);
        return;
    }
    setReg(data);
}

// ─── Register A8H → Speicherbild ─────────────────────────────────────────────

bool K8915Zre::klemme(uint8_t nr, uint8_t r)
{
    switch (nr) {
        case 9:  return !(r & 0x80);   // /DO8 (über D17)
        case 11: return !(r & 0x01);   // /DO1 (über D7)
        case 13: return  (r & 0x80);
        case 15: return  (r & 0x01);
        case 17: return  (r & 0x40);
        case 19: return  (r & 0x02);
        case 21: return  (r & 0x20);
        case 23: return  (r & 0x04);
        case 25: return  (r & 0x10);
        case 27: return  (r & 0x08);
        default: return true;          // offen: TTL-Eingang liest H
    }
}

void K8915Zre::setReg(uint8_t v)
{
    reg_ = v;
    const auto& f = cfg_.feld;
    memdi_  = !klemme(f.x8_memdi,   v);   // aktiv L
    memdi1_ = !klemme(f.x12_memdi1, v);
    bus_.setMEMDI(memdi_);
    rebuildMap();
    LOG_DEBUG("ZRE8762", "A8H := %02X  /MEMDI=%d /MEMDI1=%d", v, (int)memdi_, (int)memdi1_);
}

void K8915Zre::rebuildMap()
{
    const auto& f = cfg_.feld;
    const bool rom      = klemme(f.x10_rom,    reg_);
    const bool seite[4] = { klemme(f.x14_seite0, reg_), klemme(f.x18_seite1, reg_),
                            klemme(f.x22_seite2, reg_), klemme(f.x26_seite3, reg_) };
    const bool bank2    = klemme(f.x16_bank2,  reg_);
    const uint32_t viertel = (klemme(f.x24_bank2_a14, reg_) ? 1u : 0u)
                           | (klemme(f.x20_bank2_a15, reg_) ? 2u : 0u);

    for (int s = 0; s < 16; ++s) {
        const int      p    = s >> 2;              // 16-KB-Seite (D23 aus AB14/AB15)
        const uint32_t addr = uint32_t(s) << 12;
        Slot slot{Quelle::Bus, addr};
        if (rom && s == 0) {
            // D9: Seite 0 ∧ A12 = A13 = 0 ∧ X10.  Das ROM hat Vorrang und sperrt
            // /CAS1 (D14) und die Bank-2-Wahl (D6).
            slot = {Quelle::Rom, 0};
        } else if (p == 1 && bank2) {
            // D6 = NAND(63, P1, /ROM); D32 legt 61/62 statt AB14/AB15 an die RAM-
            // Adressen ⇒ Viertel von Bank 2.  (Wäre zugleich Seite 1 von Bank 1
            // gewählt, lägen beide Bänke parallel am Datenbus — das Gerät tut das
            // nie; hier gewinnt Bank 2.)
            slot = {Quelle::Bank2, (viertel << 14) | (addr & 0x3FFF)};
        } else if (seite[p]) {
            slot = {Quelle::Bank1, addr};          // Bank 1 identisch abgebildet
        }
        map_[s] = slot;
    }
}

K8915Zre::Ort K8915Zre::ortVon(uint16_t addr) const
{
    const Slot& s = map_[addr >> 12];
    return {s.quelle, s.basis + (addr & 0x0FFFu)};
}

uint8_t K8915Zre::memRead(uint16_t addr)
{
    const Slot& s = map_[addr >> 12];
    const uint32_t off = s.basis + (addr & 0x0FFFu);
    switch (s.quelle) {
        case Quelle::Rom:   return K8915_ZRE_BOOT_ROM[off];
        case Quelle::Bank1: return ram_[0][off];
        case Quelle::Bank2: return ram_[1][off];
        case Quelle::Bus:   break;
    }
    return bus_.memRead(addr);
}

void K8915Zre::memWrite(uint16_t addr, uint8_t data)
{
    const Slot& s = map_[addr >> 12];
    const uint32_t off = s.basis + (addr & 0x0FFFu);
    switch (s.quelle) {
        case Quelle::Rom:   return;                 // EPROM: Schreiben folgenlos
        case Quelle::Bank1: ram_[0][off] = data; return;
        case Quelle::Bank2: ram_[1][off] = data; return;
        case Quelle::Bus:   break;
    }
    bus_.memWrite(addr, data);
}
