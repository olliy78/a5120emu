/**
 * @file zve3x.cpp
 * @brief ZVE des MC80.3x — Speicher, CTC/PIO, Segmentregister, RAM-Steuerung.
 * @see zve3x.h, doc/mc80/hardware_3x.md §2–§3
 */

#include "core/cards/mc80/zve3x.h"
#include "core/cards/mc80/rom_zve3x.h"
#include "core/logger.h"

Mc80Zve::Mc80Zve(K1520Bus& bus) : Mc80Zve(bus, Config{}) {}

Mc80Zve::Mc80Zve(K1520Bus& bus, const Config& cfg) : bus_(bus), cfg_(cfg)
{
    if (!cfg_.rom) {
        cfg_.rom     = MC80_ZVE_ROM;
        cfg_.rom_len = sizeof(MC80_ZVE_ROM);
    }
    if (cfg_.rom_len > ROM_GROESSE) cfg_.rom_len = ROM_GROESSE;

    cpu_.readByte     = [this](uint16_t a)            { return bus_.memRead(a); };
    cpu_.writeByte    = [this](uint16_t a, uint8_t d) { bus_.memWrite(a, d); };
    // Volle 16-Bit-E/A-Adresse: die SPE 2 (B0H–B3H) und die VIS 2a werten A8–A15 aus.
    cpu_.readPort     = [this](uint16_t p)            { return bus_.ioRead(p); };
    cpu_.writePort    = [this](uint16_t p, uint8_t d) { bus_.ioWrite(p, d); };
    cpu_.retiCallback = [this]()                      { bus_.signalRETI(); };

    // Tor B = Segmentregister: jede Ausgabe (Betriebsart 0/3) geht an die Speicherkarten.
    pio_.setPortBOutputCallback([this](uint8_t d) { segmentSetzen(d); });
}

void Mc80Zve::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, cfg_.ctc_base, 4);
    bus.registerIO(this, cfg_.pio_base, 4);
    bus.registerIO(this, cfg_.ramst_base, 2);
    bus.registerMem(this, ROM_BASIS, ROM_GROESSE);
    bus.registerMem(this, RAM_BASIS, RAM_GROESSE);
}

void Mc80Zve::powerOn(uint8_t fill)
{
    ram_.fill(fill);
    ramst_ausgaben_ = 0;
    reset();
}

void Mc80Zve::reset()
{
    ctc_.reset();
    pio_.reset();
    cpu_.reset();
    schreibschutz_ = false;   // egos.md §1.6: nach RESET aus
    segmentSetzen(0x00);
}

void Mc80Zve::segmentSetzen(uint8_t m)
{
    segment_ = m;
    if (segment_cb_) segment_cb_(m);
}

// ─── E/A ─────────────────────────────────────────────────────────────────────

uint8_t Mc80Zve::ioRead(uint8_t port)
{
    if (uint8_t(port - cfg_.ctc_base) < 4) return ctc_.ioRead(port & 0x03);
    if (uint8_t(port - cfg_.pio_base) < 4) return pio_.ioRead(pioPort(port & 0x03));
    return 0xFF;   // 8CH/8DH: nur Ausgabe
}

void Mc80Zve::ioWrite(uint8_t port, uint8_t data)
{
    if (uint8_t(port - cfg_.ctc_base) < 4) { ctc_.ioWrite(port & 0x03, data); return; }
    if (uint8_t(port - cfg_.pio_base) < 4) { pio_.ioWrite(pioPort(port & 0x03), data); return; }
    if (uint8_t(port - cfg_.ramst_base) < 2) {
        // Schreibsenke: Wirkung der einzelnen Ausgaben unbekannt [?] (egos.md §1.6).
        ++ramst_ausgaben_;
        LOG_TRACE("MC80-ZVE", "RAM-Steuerung %02XH <= %02XH (A8-A15 %02XH, ohne Wirkung)",
                  port, data, bus_.ioAddress() >> 8);
    }
}

// ─── Speicher ────────────────────────────────────────────────────────────────

uint8_t Mc80Zve::memRead(uint16_t a)
{
    if (a < ROM_GROESSE) return a < cfg_.rom_len ? cfg_.rom[a] : 0xFF;
    if (a >= RAM_BASIS) return ram_[a - RAM_BASIS];
    return 0xFF;
}

void Mc80Zve::memWrite(uint16_t a, uint8_t d)
{
    if (a < RAM_BASIS) return;                       // ROM schluckt den Schreibzyklus
    if (schreibschutz_ && a >= SCHUTZ_BASIS) return; // geschützt: wirkungslos
    ram_[a - RAM_BASIS] = d;
}

// ─── Interruptkette: IEI → CTC → PIO → IEO [?] ───────────────────────────────

void Mc80Zve::setIEI(bool iei)
{
    ctc_.setIEI(iei);
    pio_.setIEI(ctc_.getIEO());
}

uint8_t Mc80Zve::getVector() const
{
    if (ctc_.hasInterrupt()) return ctc_.getVector();
    if (pio_.hasInterrupt()) return pio_.getVector();
    return 0xFF;
}

const char* Mc80Zve::intDeviceName() const
{
    if (ctc_.hasInterrupt()) return "MC80-ZVE CTC";
    if (pio_.hasInterrupt()) return "MC80-ZVE PIO";
    return "MC80-ZVE";
}

void Mc80Zve::onRETI()
{
    ctc_.onRETI();
    pio_.onRETI();
}
