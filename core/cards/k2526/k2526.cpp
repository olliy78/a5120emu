#include "core/cards/k2526/k2526.h"
#include "core/logger.h"

// ─── Construction ────────────────────────────────────────────────────────────

K2526::K2526(K1520Bus& bus)
    : K2526(bus, A5120Config{}) {}

K2526::K2526(K1520Bus& bus, const A5120Config& cfg)
    : bus_(bus), cfg_(cfg)
{
    // ── ZVE1 (Haupt-CPU) mit K1520-Bus verdrahten ────────────────────────────
    // Die CPU liegt physisch auf der K2526-Karte. Alle Speicher- und I/O-
    // Zugriffe laufen über den K1520-Bus.
    cpu_.readByte     = [this](uint16_t a)           { return bus_.memRead(a); };
    cpu_.writeByte    = [this](uint16_t a, uint8_t d){ bus_.memWrite(a, d); };
    cpu_.readPort     = [this](uint16_t p)           { return bus_.ioRead(p & 0xFF); };
    cpu_.writePort    = [this](uint16_t p, uint8_t d){ bus_.ioWrite(p & 0xFF, d); };
    cpu_.retiCallback = [this]()                     { bus_.signalRETI(); };

    // ── ZVE2 (DMA-CPU) mit K1520-Bus verdrahten ──────────────────────────────
    // ZVE2 teilt den selben Bus mit ZVE1: gleiches RAM, gleiche I/O-Ports.
    // kein retiCallback: ZVE2 führt keine Interrupt-Service-Routinen aus.
    zve2_.readByte  = [this](uint16_t a)           { return bus_.memRead(a); };
    zve2_.writeByte = [this](uint16_t a, uint8_t d){ bus_.memWrite(a, d); };
    zve2_.readPort  = [this](uint16_t p)           { return bus_.ioRead(p & 0xFF); };
    zve2_.writePort = [this](uint16_t p, uint8_t d){ bus_.ioWrite(p & 0xFF, d); };

    // BS-PIO Port-A-Ausgangs-Callback: A7=MEMDI1/2 steuert Speicherzugriffssperre.
    bs_pio_.setPortAOutputCallback([this](uint8_t data) {
        bool memdi = (data >> 7) & 1;
        bus_.setMEMDI(memdi);
        LOG_DEBUG("K2526", "BS-PIO PortA out=0x%02X → MEMDI=%d", data, (int)memdi);
    });

    // BS-PIO Port-B-Ausgangs-Callback: reagiert auf /LD-ROM (B0) und /SA (B4).
    bs_pio_.setPortBOutputCallback([this](uint8_t data) {
        onBSPIOPortBOut(data);
    });
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

void K2526::attachToBus(K1520Bus& bus)
{
    // Ports 00H–0FH registrieren (U-Bus + BS-PIO + CTC).
    bus.registerIO(this, cfg_.io_base, 16);
    LOG_DEBUG("K2526", "attachToBus: I/O 00H-0FH registriert");
}

void K2526::powerOn()
{
    // ── Lade-ROM aktivieren ───────────────────────────────────────────────
    rom_enabled_ = true;
    bus_.registerMem(&rom_, 0x0000, 1024);
    LOG_INFO("K2526", "Power-on: Lade-ROM aktiv (0000H-03FFH)");

    // ── BS-PIO im Bitmode (Mode 3) initialisieren ─────────────────────────
    // Laut Originalschaltung (Abschn. 6.10.2): beide Ports im Bitmode.
    //
    // Port A (08H Data, 09H Ctrl): Richtungsmaske 0x7F
    //   A7=MEMDI1/2 = Ausgang(0), A6-A0 = Eingänge(1) → 0111 1111 = 0x7F
    bs_pio_.ioWrite(1, 0xCF);  // Port A: Mode 3 (11xx1111)
    bs_pio_.ioWrite(1, 0x7F);  // Port A: Richtungsmaske

    // Port B (0AH Data, 0BH Ctrl): Richtungsmaske 0xE2
    //   B0=/LD-ROM     Ausgang(0)
    //   B1=/INT-BS     Eingang(1)
    //   B2=/SPS-ESR    Ausgang(0)
    //   B3=/WAIT-ZVE2  Ausgang(0)
    //   B4=/SA         Ausgang(0)
    //   B5-B7 Config   Eingänge(1) → 1110 0010 = 0xE2
    bs_pio_.ioWrite(3, 0xCF);  // Port B: Mode 3
    bs_pio_.ioWrite(3, 0xE2);  // Port B: Richtungsmaske

    // Nach RESET hält Pull-up R3:4 /LD-ROM=high → ROM aktiv.
    // Der Callback feuert erst, wenn CPU explizit Port B Data (0AH) beschreibt.
    LOG_INFO("K2526", "BS-PIO Mode 3 init: PortA-dir=0x7F, PortB-dir=0xE2");
}

void K2526::clockTick()
{
    ctc_.clockTick();
}

// ─── I/O-Dispatch (Originalschaltung Abschn. 6.9.2) ─────────────────────────
//
// Adressbelegung (AB7-AB4=0, AB3-AB0 unterscheiden):
//   00H–07H  U-Bus    (bidirektionale Treiber A40/41, Tastatur X5)
//   08H–0BH  BS-PIO   (Q301, AB3=1 AB2=0)
//   0CH–0FH  CTC      (Q302, AB3=1 AB2=1)
//
// Z80PIO und Z80CTC verwenden intern (port & 0x03) zur Sub-Port-Selektion,
// daher kann der absolute Port direkt übergeben werden.

uint8_t K2526::ioRead(uint8_t port)
{
    if (port <= 0x07) {
        // ── U-Bus ──────────────────────────────────────────────────────────
        uint8_t result = 0xFF;
        switch (port) {
            case 0x01:  // /UCS2: Tastengültigkeitsabfrage
                result = ubus_kbd_valid_;
                LOG_DEBUG("K2526", "U-Bus IN 01H (/UCS2 Kbd-Valid) => 0x%02X", result);
                break;
            case 0x06:  // /UCS1: Tastencode lesen
                result = ubus_kbd_code_;
                LOG_DEBUG("K2526", "U-Bus IN 06H (/UCS1 Kbd-Code)  => 0x%02X", result);
                break;
            default:
                LOG_DEBUG("K2526", "U-Bus IN port=0x%02X => 0xFF (n.i.)", port);
                break;
        }
        return result;
    } else if (port <= 0x0B) {
        // ── BS-PIO (Ports 08H–0BH) ─────────────────────────────────────────
        uint8_t result = bs_pio_.ioRead(port);
        LOG_DEBUG("K2526", "BS-PIO IN  port=0x%02X (sub=%u) => 0x%02X",
                  port, port & 0x03u, result);
        return result;
    } else if (port <= 0x0F) {
        // ── CTC (Ports 0CH–0FH) ────────────────────────────────────────────
        uint8_t result = ctc_.ioRead(port);
        LOG_DEBUG("K2526", "CTC    IN  port=0x%02X (ch=%u)  => 0x%02X",
                  port, port & 0x03u, result);
        return result;
    }
    return 0xFF;
}

void K2526::ioWrite(uint8_t port, uint8_t data)
{
    if (port <= 0x07) {
        // ── U-Bus ──────────────────────────────────────────────────────────
        switch (port) {
            case 0x00:  // OUT 00H: /INT-BS – Aufruf Betriebssystemebene via BS-PIO B1
                LOG_DEBUG("K2526", "U-Bus OUT 00H (/INT-BS trigger) data=0x%02X", data);
                break;
            case 0x02:  // /RES-SPA: Speicherschutz-Indikator zurücksetzen
                LOG_DEBUG("K2526", "U-Bus OUT 02H (/RES-SPA) data=0x%02X", data);
                break;
            case 0x03:  // /UCS4: Fehlerlampe / INS-MODE-Anzeige
                ubus_error_lamp_ = data;
                LOG_DEBUG("K2526", "U-Bus OUT 03H (/UCS4 ErrLamp) data=0x%02X", data);
                break;
            case 0x04:  // /RES-ZVE2: DMA-CPU reset (active-low, bit 0)
                // bit0=0 → /RES-ZVE2 asserted → ZVE2 in reset
                // bit0=1 → /RES-ZVE2 released → ZVE2 running from PC=0
                if (!(data & 0x01) && !zve2_reset_) {
                    zve2_reset_ = true;
                    LOG_INFO("K2526", "ZVE2 in Reset versetzt (OUT 04H=0x%02X)", data);
                } else if ((data & 0x01) && zve2_reset_) {
                    zve2_reset_ = false;
                    zve2_.reset();
                    LOG_INFO("K2526", "ZVE2 gestartet: PC=0000H (OUT 04H=0x%02X)", data);
                }
                break;
            case 0x05:  // /UCS3: Lampenansteuerung (Selektoren)
                ubus_lamp_ = data;
                LOG_DEBUG("K2526", "U-Bus OUT 05H (/UCS3 Lamps)  data=0x%02X", data);
                break;
            default:
                LOG_DEBUG("K2526", "U-Bus OUT port=0x%02X data=0x%02X (n.i.)", port, data);
                break;
        }
    } else if (port <= 0x0B) {
        // ── BS-PIO (Ports 08H–0BH) ─────────────────────────────────────────
        LOG_DEBUG("K2526", "BS-PIO OUT port=0x%02X (sub=%u) data=0x%02X",
                  port, port & 0x03u, data);
        bs_pio_.ioWrite(port, data);
    } else if (port <= 0x0F) {
        // ── CTC (Ports 0CH–0FH) ────────────────────────────────────────────
        LOG_DEBUG("K2526", "CTC    OUT port=0x%02X (ch=%u)  data=0x%02X",
                  port, port & 0x03u, data);
        ctc_.ioWrite(port, data);
    }
}

// ─── BS-PIO Port-B-Ausgangs-Handler ──────────────────────────────────────────
//
// /LD-ROM ist AKTIV LOW (Originalschaltung Abschn. 6.10.1 B0):
//   B0=1 (high, Pull-up nach RESET) → /LD-ROM=1 → ROM AKTIV
//   B0=0 (low, vom BIOS gesetzt)    → /LD-ROM=0 → ROM DEAKTIVIERT
//
// Das BIOS schreibt im 2. Teil des Startprogramms B0=0, um das ROM abzuschalten.

void K2526::onBSPIOPortBOut(uint8_t data)
{
    bool rom_should_be_disabled = !(data & 0x01);  // B0=0 → ROM aus

    if (rom_should_be_disabled && rom_enabled_) {
        rom_enabled_ = false;
        bus_.unregisterMem(&rom_);
        LOG_INFO("K2526", "Lade-ROM DEAKTIVIERT: BS-PIO B0=/LD-ROM=0 (PortB=0x%02X)", data);
    } else if (!rom_should_be_disabled && !rom_enabled_) {
        rom_enabled_ = true;
        bus_.registerMem(&rom_, 0x0000, 1024);
        LOG_INFO("K2526", "Lade-ROM REAKTIVIERT: BS-PIO B0=/LD-ROM=1 (PortB=0x%02X)", data);
    }

    if (!(data & 0x10)) {
        LOG_WARN("K2526", "BS-PIO B4=/SA aktiv (0x%02X) – Netzausschaltung!", data);
    }
}

// ─── InterruptSlave – Interne Daisy-Chain: IEI → CTC → BS-PIO → IEO ─────────
//
// Originalschaltung (Abb. 1):
//   CTC am Ende der 1. Prioritätenkette (Systembus)    – höhere Prio
//   BS-PIO am Ende der 2. Prioritätenkette (Koppelbus) – niedrigere Prio
//
// Emulatorvereinfachung: Eine zusammengeführte Kette.

void K2526::setIEI(bool iei)
{
    iei_in_ = iei;
    ctc_.setIEI(iei);
    bs_pio_.setIEI(ctc_.getIEO());  // CTC-IEO → BS-PIO-IEI (Koppelbus)
    LOG_TRACE("K2526", "setIEI(%d): CTC.IEI=%d → BS-PIO.IEI=%d",
              (int)iei, (int)iei, (int)ctc_.getIEO());
}

bool K2526::getIEO() const
{
    return bs_pio_.getIEO();
}

bool K2526::hasInterrupt() const
{
    bool ctc_int = ctc_.hasInterrupt();
    bool pio_int = bs_pio_.hasInterrupt();
    if (ctc_int || pio_int) {
        LOG_DEBUG("K2526", "hasInterrupt: CTC=%d BS-PIO=%d", (int)ctc_int, (int)pio_int);
    }
    return ctc_int || pio_int;
}

uint8_t K2526::getVector() const
{
    if (ctc_.hasInterrupt()) {
        uint8_t v = ctc_.getVector();
        LOG_DEBUG("K2526", "getVector: CTC-Vektor=0x%02X", v);
        return v;
    }
    if (bs_pio_.hasInterrupt()) {
        uint8_t v = bs_pio_.getVector();
        LOG_DEBUG("K2526", "getVector: BS-PIO-Vektor=0x%02X", v);
        return v;
    }
    return 0xFF;
}

void K2526::onRETI()
{
    ctc_.onRETI();
    bs_pio_.onRETI();
    LOG_DEBUG("K2526", "RETI: weitergeleitet an CTC und BS-PIO");
}
