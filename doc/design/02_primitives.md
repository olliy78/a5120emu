# Feinentwurf: Chip-Primitive

**Modul:** `core/primitives/`  
**Dateien:** `z80_cpu.h/cpp`, `z80_pio.h/cpp`, `z80_sio.h/cpp`, `z80_ctc.h/cpp`, `z80_dma.h/cpp`, `ram_device.h/cpp`, `eprom_device.h/cpp`

---

## 1. Aufgabe

Die Primitiven sind generische Implementierungen der Z80-Chip-Familie und einfacher Speicherbausteine. Sie sind **unabhängig von einer konkreten Karte** und können von mehreren Karten verwendet werden.

---

## 2. Z80CPU

Basiert auf dem bestehenden `src/z80.cpp`. Anpassungen:
- Callback-Interface → `K1520Bus` (statt direkte Memory-Pointer)
- Zyklenzähler für Timing
- HALT-Trap-Mechanismus entfernt

```cpp
class Z80CPU {
public:
    explicit Z80CPU(K1520Bus& bus);

    void reset();
    int  step();              // 1 Befehl, Rückgabe: verbrauchte Zyklen
    int  run(int maxCycles);  // läuft bis maxCycles, Rückgabe: Restzyklen

    bool isHalted() const;

    // CPU-Zustand (für Debugging)
    struct Regs {
        uint16_t pc, sp, af, bc, de, hl, ix, iy;
        uint16_t af2, bc2, de2, hl2;  // Shadow-Register
        uint8_t  i, r;
        bool     iff1, iff2;
        uint8_t  im;  // Interrupt-Modus 0/1/2
    };
    Regs getRegs() const;
    void setRegs(const Regs&);

    // Direkte Byte-Injektion für Tests
    void setPC(uint16_t);
    void setSP(uint16_t);

private:
    K1520Bus& bus_;
    // ... (aus src/z80.h übernommen)
};
```

---

## 3. Z80PIO (Q 301 / U 855)

Der Z80 PIO hat zwei unabhängige Ports (A und B), jeweils 8-Bit.

### 3.1 Betriebsarten pro Port

| Modus | Wert | Beschreibung |
|-------|------|--------------|
| Output | 0 | Port ist vollständig Ausgang |
| Input  | 1 | Port ist vollständig Eingang |
| Bidir  | 2 | Bidirektional (mit Handshake) |
| BitCtl | 3 | Jedes Bit einzeln konfigurierbar (IN/OUT) |

### 3.2 Interface

```cpp
class Z80PIO : public BusDevice, public InterruptSlave {
public:
    // port_a_data, port_a_ctrl, port_b_data, port_b_ctrl:
    // entspricht den 4 I/O-Adressen (A0, A1 = Tor-/Steuerwort-Auswahl)
    explicit Z80PIO(const std::string& name = "PIO");

    // BusDevice: 4 aufeinanderfolgende I/O-Ports
    uint8_t ioRead(uint8_t port) override;
    void    ioWrite(uint8_t port, uint8_t data) override;

    // InterruptSlave (Daisy-Chain)
    void    setIEI(bool) override;
    bool    getIEO() const override;
    bool    hasInterrupt() const override;
    uint8_t getVector() const override;

    // Externe Leitungen (von anderen Klassen gesetzt/gelesen)
    // Port A
    void    portAWrite(uint8_t data);   // externe Quelle schreibt
    uint8_t portARead() const;          // externe Quelle liest Ausgang
    void    setASTB(bool);              // Strobe-Eingang

    // Port B
    void    portBWrite(uint8_t data);
    uint8_t portBRead() const;
    void    setBSTB(bool);

    // Interrupt-Konfiguration
    // (wird automatisch aus Steuerwort gesetzt)
    bool    isInterruptEnabled() const;
    uint8_t getInterruptVector() const;

    // Callbacks für Pegelwechsel
    using PortCallback = std::function<void(uint8_t data)>;
    void    setPortAOutputCallback(PortCallback);
    void    setPortBOutputCallback(PortCallback);

private:
    // Interne Zustand
    struct Port {
        uint8_t mode = 1;          // Default: Input
        uint8_t output = 0;
        uint8_t input  = 0xFF;
        uint8_t direction = 0xFF;  // Bit=1: Input (nur BitCtl)
        bool    ie = false;
        uint8_t int_vector = 0;
        uint8_t int_mask = 0;
        bool    int_active_high = false;
        bool    int_and_logic = false;
        bool    interrupt_pending = false;
        bool    iei = false;
    };
    Port porta_, portb_;
    std::string name_;
    PortCallback cb_a_, cb_b_;
};
```

---

## 4. Z80SIO (Q 304 / U 856)

Der Z80 SIO hat zwei serielle Kanäle (A und B).

### 4.1 Hauptfunktionen

- Asynchron (UART): 5–8 Datenbits, 1/1.5/2 Stopbits, Parität
- Synchron (SDLC/HDLC)
- Baudrate: extern via CLK-Eingang oder CTC-Ausgang
- Interrupt: Daisy-Chain, pro Kanal konfigurierbar

### 4.2 Interface

```cpp
class Z80SIO : public BusDevice, public InterruptSlave {
public:
    explicit Z80SIO(const std::string& name = "SIO");

    // BusDevice: 4 I/O-Ports (ChA-Daten, ChA-Ctrl, ChB-Daten, ChB-Ctrl)
    uint8_t ioRead(uint8_t port) override;
    void    ioWrite(uint8_t port, uint8_t data) override;

    // InterruptSlave
    void    setIEI(bool) override;
    bool    getIEO() const override;
    bool    hasInterrupt() const override;
    uint8_t getVector() const override;

    // Serielle Schnittstelle Kanal A/B (physikalisch)
    struct Channel {
        void    rxByte(uint8_t byte);       // Byte empfangen (von extern)
        bool    txAvailable() const;         // Byte zum Senden bereit?
        uint8_t txGet();                     // Byte holen (von extern lesen)
        bool    rxFull() const;

        void    setClockSource(std::function<void()> clk_callback);
        void    setRTS(bool);
        bool    getCTS() const;
        // ...
    };

    Channel& channelA();
    Channel& channelB();

private:
    // Kanalzustand (WR0–WR7, RR0–RR2 pro Kanal)
    struct ChannelState { /* ... */ };
    Channel ch_a_, ch_b_;
    bool iei_ = false;
    uint8_t int_vector_ = 0;
};
```

---

## 5. Z80CTC (Q 302 / U 857)

4 unabhängige Kanäle, Timer oder Zähler-Modus.

### 5.1 Betrieb

- **Timer-Modus:** Zählt System-Taktzyklen, generiert ZC/TO bei Null
- **Counter-Modus:** Zählt externe CLK/TRG-Flanken
- Prescaler: ÷16 oder ÷256

### 5.2 Interface

```cpp
class Z80CTC : public BusDevice, public InterruptSlave {
public:
    explicit Z80CTC(const std::string& name = "CTC");

    // BusDevice: 4 I/O-Ports (Kanäle 0–3)
    uint8_t ioRead(uint8_t port) override;
    void    ioWrite(uint8_t port, uint8_t data) override;

    // InterruptSlave
    void    setIEI(bool) override;
    bool    getIEO() const override;
    bool    hasInterrupt() const override;
    uint8_t getVector() const override;

    // Takt-Eingang (aufgerufen bei jedem System-Takt)
    void    clockTick();

    // CLK/TRG-Eingänge für Counter-Modus
    void    clkTrg(int channel, bool rising_edge);

    // ZC/TO-Ausgänge
    using ZCTOCallback = std::function<void(int channel, bool level)>;
    void    setZCTOCallback(ZCTOCallback);

    int     getCount(int channel) const;
    bool    isTimerMode(int channel) const;

private:
    struct ChannelState {
        uint8_t control = 0;
        uint8_t timeConst = 0;
        int     counter = 0;
        bool    zcto_state = false;
        bool    interrupt_pending = false;
    };
    ChannelState ch_[4];
    uint8_t int_vector_ = 0;
    ZCTOCallback zc_cb_;
};
```

---

## 6. RAMDevice

Einfacher RAM-Block ohne Sonderlogik.

```cpp
class RAMDevice : public MemDevice {
public:
    explicit RAMDevice(uint16_t size);

    uint8_t  memRead(uint16_t addr) override;
    void     memWrite(uint16_t addr, uint8_t data) override;
    bool     isWritable() const override { return true; }

    // Direktzugriff (für Tests und Initialisierung)
    uint8_t* data();
    void     fill(uint8_t value = 0x00);
    void     load(const uint8_t* src, uint16_t offset, uint16_t len);

private:
    std::vector<uint8_t> mem_;
};
```

---

## 7. EPROMDevice

ROM mit compile-time-Daten. Schreibzugriffe werden ignoriert.

```cpp
template<size_t N>
class EPROMDevice : public MemDevice {
public:
    explicit EPROMDevice(const uint8_t (&data)[N])
        : data_(data) {}

    uint8_t memRead(uint16_t addr) override {
        return (addr < N) ? data_[addr] : 0xFF;
    }
    void memWrite(uint16_t, uint8_t) override {}  // ROM: ignoriert
    bool isWritable() const override { return false; }

private:
    const uint8_t* data_;
};

// Verwendung:
// #include "k2526/rom_data.h"   (generiert aus EPROM-Binary)
// EPROMDevice<sizeof(ZRE_ROM_DATA)> rom{ZRE_ROM_DATA};
```

---

## 8. EPROM-Daten (.h-Datei-Format)

Alle EPROM-Inhalte werden vom Tool `tools/eprom_to_h.py` generiert:

```python
# tools/eprom_to_h.py
import sys, pathlib

def convert(bin_path: str, symbol: str, out_path: str):
    data = pathlib.Path(bin_path).read_bytes()
    lines = [f"// Generiert aus: {bin_path}",
             f"// Größe: {len(data)} Bytes",
             f"#pragma once",
             f"#include <cstdint>",
             f"static constexpr uint8_t {symbol}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")
    lines.append("};")
    pathlib.Path(out_path).write_text("\n".join(lines))
```

Aufrufe:
```bash
python tools/eprom_to_h.py \
    doc/EPROMS/Prozessoreinheit_ZRE_K2525_A26.bin \
    ZRE_BOOT_ROM \
    core/cards/k2526/rom_data.h

python tools/eprom_to_h.py \
    doc/EPROMS/Bildschirm_ABS_K7024_A103.bin \
    CHARSET_LATIN \
    core/cards/k7024/charset_latin.h

python tools/eprom_to_h.py \
    doc/EPROMS/Bildschirm_ABS_K7024_A123.bin \
    CHARSET_CYRILLIC \
    core/cards/k7024/charset_cyrillic.h
```

---

## 9. Testbarkeit

Alle Primitiven sind **ohne Bus** testbar (direktes Instanziieren):

```python
# tests/python/test_z80_ctc.py
def test_ctc_timer_interrupt(ctc_lib):
    ctc = ctc_lib.z80_ctc_create()
    ctc_lib.z80_ctc_write(ctc, 0, 0xA5)  # IE=1, Mode=Timer, Prescaler=256
    ctc_lib.z80_ctc_write(ctc, 0, 5)     # Time constant = 5
    # 5 × 256 = 1280 Takte bis Interrupt
    for _ in range(1280):
        ctc_lib.z80_ctc_clock(ctc)
    assert ctc_lib.z80_ctc_has_interrupt(ctc, 0)
```
