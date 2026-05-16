# K1520 Emulator – Architekturkonzept v2.0

**Autor:** Olaf Krieger  
**Stand:** Mai 2026  
**Basis:** TGL 37271/01, Technische Dokumentation A5120, K2526, K7024, K8025, K5122, K7637

---

## 1. Projektüberblick

### 1.1 Ziel

Entwicklung eines hardwarenahen, modularen Emulators für die **K1520-Rechnerfamilie** (DDR, 1980er Jahre). Zielcomputer:

| Computer | Karten | OS |
|----------|--------|-----|
| Robotron A5120 | K2526, K3526, K7024, K8025, K5122 | CPA, SIOS, SCP, UDOS, MUTOS |
| PRG710 / PRG710-1 | (noch zu ermitteln) | SCPX |
| K8915 | (noch zu ermitteln) | — |
| weitere | konfigurierbar | — |

### 1.2 Abgrenzung zum Vorgänger

Der bestehende `a5120emu` (Verzeichnis `src/`) bleibt unverändert als Legacy-Code erhalten. Der neue Emulator entsteht **vollständig neu** in separaten Verzeichnissen und teilt **keinen Code** mit dem Vorgänger außer den Z80-Disassembler-Tools.

`cparun/` bleibt eigenständiges Tool ohne Änderungen.

### 1.3 Grundprinzip

Der neue Emulator bildet echte K1520-Hardware nach — nicht BIOS-Traps oder OS-Spezifika. Dadurch kann jedes Betriebssystem booten, das auf echter Hardware läuft.

---

## 2. Emulierte Hardware (A5120 als Referenzplattform)

### 2.1 K1520-System

Der K1520 ist ein genormter Backplane-Bus (TGL 37271/01) auf dem Karten mit Z80-kompatiblen Chips aufgebaut werden.

```
A5120 Backplane (11 Slots)
┌────────────────────────────────────────────────────────────────┐
│  Slot 1: K2526 (ZRE) – Zentrale Recheneinheit                 │
│  Slot 2: K3526 (OPS) – Operationsspeicher 64 KB DRAM          │
│  Slot 3: K8025 (ASS) – Anschlußsteuerung Seriell (V.24+IFSS)  │
│  Slot 4: K5122 (AFS) – Anschlußsteuerung Folienspeicher (FDC) │
│  Slot 5: K7024 (ABS) – Adapter Bildschirm (CRT-Controller)    │
│  Slot 6-11: leer (erweiterbar)                                 │
└────────────────────────────────────────────────────────────────┘
            │                    │
      Systembus               Koppelbus
      (X1, 58-pol.)          (X2, 58-pol.)
```

### 2.2 K1520 Systembus-Signale (TGL 37271/01)

| Gruppe | Signale | Beschreibung |
|--------|---------|--------------|
| Datenbus | DB0–DB7 | 8-Bit bidirektional |
| Adressbus | AB0–AB15 | 16-Bit (bei /IORQ: AB8–AB15 = Register-Inhalt) |
| Steuerbus | /MREQ, /IORQ, /RD, /WR, /RFSH, /M1 | CPU-Zyklen |
| Steuerbus | /HALT, /BUSRQ, /INT, /NMI, /WAIT, /RDY | Synchronisierung |
| Steuerbus | /RESET, /MEMDI, /IODI | System |
| Kette | /IEI, /IEO | Interrupt-Prioritätskette (Daisy-Chain) |
| Kette | /BAI, /BAO | DMA-Prioritätskette |
| Takt | TAKT | 2.4576 MHz (407 ns Zyklus) |

### 2.3 Koppelbus

Der Koppelbus (X2) überträgt Signale, die nicht zum Standard-Z80-Busprotokoll gehören und maschinenspezifisch verdrahtet werden. Im A5120 erfolgt die Verdrahtung über **Wickelbrücken** in der Backplane.

Koppelbus-Signale:
- `/IEI1`, `/IEO1` – zweite Interrupt-Prioritätskette
- `CLK/TRGO`, `CLK/TRG1`, `CLK/TRG2` – CTC-Taktausgänge
- `ZC/TO`, `ZC/TO1`, `ZC/TO2` – CTC Zero-Count-Ausgänge
- `/SUE` – Spannungsüberwachung
- `/MEMDI1`, `/MEMDI2` – Speicherbereichsumschaltung
- `/SA` – Sonderausgang (Netzausschaltung)
- `12N` – −12V Versorgung

### 2.4 I/O-Port-Belegung A5120

| Adressen | Karte | Funktion |
|----------|-------|----------|
| 00H–07H | K2526 ZRE | U-Bus (Tastaturcode, Lampen), DMA-Reset |
| 08H–0BH | K2526 ZRE | BS-PIO (Steuerung, Schutz) |
| 0CH–0FH | K2526 ZRE | CTC Kanäle 0–3 |
| 10H–17H | K5122 AFS | Floppy Steuer-PIO + Daten-PIO |
| 18H | K5122 AFS | Laufwerksauswahl (8212) |
| 50H–53H | K8025 ASS | SIO A33 (DFÜ/Tastatur) |
| 54H–57H | K8025 ASS | Register A31 |
| 58H–5BH | K8025 ASS | CTC A34 |
| 5CH–5FH | K8025 ASS | SIO A32 (Drucker) |
| mem. | K7024 ABS | VRAM F800H–FFFFH (konfigurierbar) |

### 2.5 Speicherkarte

```
0000H–03FFH: Lade-ROM (1 KB, auf K2526, nach /RESET aktiv)
0400H–F7FFH: RAM (K3526, 64 KB DRAM)
F800H–FFFFH: VRAM (K7024, 2 KB, konfigurierbar)
```

---

## 3. Projektstruktur

Der neue Emulator lebt im selben Repository (`a5120emu/`), aber vollständig getrennt vom alten Code:

```
a5120emu/
├── src/                         # LEGACY (unverändert, Referenz)
├── cparun/                      # UNVERÄNDERT (eigenständig)
│
├── core/                        # ══ NEUER C++ KERN ══
│   ├── CMakeLists.txt
│   ├── bus/                     # K1520 Bus-Simulator
│   │   ├── k1520_bus.h/cpp      # Systembus
│   │   └── koppelbus.h/cpp      # Koppelbus (Signal-Routing)
│   │
│   ├── primitives/              # Chip-Primitive (Bausteine für Karten)
│   │   ├── z80_cpu.h/cpp        # Z80 CPU (aus src/ adaptiert)
│   │   ├── z80_pio.h/cpp        # Z80 PIO (Q 301)
│   │   ├── z80_sio.h/cpp        # Z80 SIO (Q 304)
│   │   ├── z80_ctc.h/cpp        # Z80 CTC (Q 302)
│   │   ├── z80_dma.h/cpp        # Z80 DMA (Cpu2-Modus)
│   │   ├── ram_device.h/cpp     # DRAM-Block
│   │   └── eprom_device.h/cpp   # EPROM-Block (Daten aus .h-Datei)
│   │
│   ├── cards/                   # Steckkarten (K1520-kompatibel)
│   │   ├── k2526/               # ZRE – Zentrale Recheneinheit
│   │   │   ├── k2526.h/cpp
│   │   │   └── rom_data.h       # EPROM-Inhalt (aus .bin generiert)
│   │   ├── k3526/               # OPS – Operationsspeicher
│   │   │   └── k3526.h/cpp
│   │   ├── k7024/               # ABS – Bildschirmkarte
│   │   │   ├── k7024.h/cpp
│   │   │   ├── charset_latin.h  # Zeichengenerator (A103 EPROM)
│   │   │   └── charset_cyrillic.h # Zeichengenerator (A123 EPROM)
│   │   ├── k8025/               # ASS – Anschlußsteuerung Seriell
│   │   │   └── k8025.h/cpp
│   │   └── k5122/               # AFS – Folienspeicher-Anschlußsteuerung
│   │       └── k5122.h/cpp
│   │
│   ├── peripherals/             # Externe Peripheriegeräte
│   │   ├── k7637/               # Serielle Tastatur
│   │   │   ├── k7637.h/cpp
│   │   │   └── keytable.h       # Scan-Code-Tabellen (aus .bin)
│   │   └── floppy_drive/        # Diskettenlaufwerk (Image-Datei)
│   │       ├── floppy_drive.h/cpp
│   │       └── format_parser.h/cpp  # cpaFormates.cfg-Parser
│   │
│   ├── machines/                # Maschinenkonfigurationen
│   │   ├── machine.h            # Abstrakte Maschinenbasis
│   │   ├── a5120/
│   │   │   ├── a5120.h/cpp      # A5120 Instantiierung
│   │   │   └── backplane.h      # Koppelbus-Verdrahtung A5120
│   │   └── prg710/
│   │       ├── prg710.h/cpp
│   │       └── backplane.h
│   │
│   └── api/                     # C-API (öffentliche Schnittstelle)
│       ├── k1520_api.h          # Stabiles C-ABI
│       └── k1520_api.cpp
│
├── app/                         # ══ PYTHON QT6 ANWENDUNG ══
│   ├── requirements.txt         # PySide6
│   ├── main.py
│   ├── emulator_thread.py       # QThread für C++-Kern
│   ├── core_binding/
│   │   └── k1520.py             # ctypes-Wrapper
│   ├── ui/
│   │   ├── main_window.py       # Hauptfenster
│   │   ├── machine_view.py      # Maschinenansicht (Theme)
│   │   ├── screen_widget.py     # Framebuffer → Qt Widget
│   │   ├── drive_widget.py      # Diskettenlaufwerk-Widget
│   │   └── config_dialog.py     # Konfigurationsdialog
│   ├── themes/
│   │   ├── a5120_theme.py       # A5120 Farben und Layout
│   │   └── prg710_theme.py
│   └── config/
│       └── machines/
│           ├── a5120.json
│           └── prg710.json
│
├── cli/                         # ══ CLI BINARY ══
│   ├── CMakeLists.txt
│   └── k1520cli.cpp             # Standalone ohne Python
│
├── tests/                       # ══ TESTS ══
│   ├── cpp/                     # C++ Unit-Tests (GoogleTest)
│   │   ├── test_bus.cpp
│   │   ├── test_z80.cpp
│   │   ├── test_k7024.cpp
│   │   └── ...
│   └── python/                  # Python Integrationstests
│       ├── conftest.py          # ctypes-Loader
│       ├── test_k5122.py
│       ├── test_k7024.py
│       └── ...
│
├── tools/                       # UNVERÄNDERT + erweitert
│   ├── z80_disasm.py            # (bestehend)
│   ├── z80_disasm3.py           # (bestehend)
│   └── eprom_to_h.py            # NEU: .bin → .h Konverter
│
└── doc/
    ├── K1520_architecture.md    # Diese Datei
    ├── design/                  # Modul-Feinentwürfe
    │   ├── 01_k1520_bus.md
    │   ├── 02_primitives.md
    │   ├── 03_k2526_zre.md
    │   ├── 04_k3526_ops.md
    │   ├── 05_k7024_abs.md
    │   ├── 06_k8025_ass.md
    │   ├── 07_k5122_afs.md
    │   ├── 08_k7637_keyboard.md
    │   ├── 09_floppy_drive.md
    │   ├── 10_c_api.md
    │   ├── 11_python_app.md
    │   └── 12_testing.md
    ├── EPROMS/                  # Binäre EPROM-Inhalte
    └── trascripted/             # Originaldokumentation
```

---

## 4. Emulationsphilosophie

### 4.1 Hardware-genau, nicht OS-spezifisch

Jede Karte wird nach ihrer technischen Dokumentation implementiert. Das Ergebnis: Jedes OS das auf echter Hardware läuft, läuft auch auf dem Emulator – ohne spezifische Kenntnis des OS.

| Vorgänger | Neu |
|-----------|-----|
| HALT-Trap → CPA-BIOS-Funktion | I/O-Port → Karten-Register-Emulation |
| Nur CPA bootbar | Beliebiges K1520-OS bootbar |
| BIOS in C++ reimplementiert | BIOS läuft als Z80-Code nativ |
| Monolithisch | Modular, Karte = eigene Klasse |

### 4.2 Karte als eigenständige Klasse (und optional als .so)

Jede Steckkarte ist eine **in sich geschlossene C++ Klasse**, die:
- am K1520Bus registriert wird (I/O-Port-Bereiche, Memory-Bereiche)
- im Koppelbus Signale senden und empfangen kann
- DIP-Schalter und Brücken als **Compile-Zeit-Konfiguration** hat
- EPROM-Inhalte als eingebettete `uint8_t`-Arrays hat

Für **Tests** kann jede Karte als eigenständige `.so`-Bibliothek gebaut werden und über Python/ctypes direkt angesteuert werden — ohne den Rest des Systems.

Für **Produktion** werden alle Karten statisch in `libk1520core.so` gelinkt.

### 4.3 Keine Zyklus-genaue Simulation

Der Emulator ist **transaktionsgenau**, nicht zyklus-genau:
- Z80 CPU führt vollständige Buszyklen aus (Fetch, Decode, Execute)
- Karten reagieren sofort auf Bus-Zugriffe
- Keine Emulation von Propagierungsverzögerungen oder Bus-Timing
- Interrupt-Daisy-Chain wird korrekt verwaltet (reihenfolgetreu)

Das ist ausreichend, um Software korrekt auszuführen. Timing-empfindliche Hardware (z.B. Floppy-Markenerkennung) wird funktional, nicht timing-genau emuliert.

### 4.4 EPROM-Daten als compile-time Konstanten

```cpp
// Generiert aus: tools/eprom_to_h.py doc/EPROMS/Bildschirm_ABS_K7024_A103.bin
// Enthält 1024 Bytes Zeichengenerator-Daten (Latein)
#pragma once
#include <cstdint>
static constexpr uint8_t CHARSET_LATIN[1024] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Char 0x00
    // ...
};
```

Ein Austausch ohne Neukompilierung ist nicht vorgesehen und nicht nötig.

---

## 5. C++ Kern: Schichtmodell

```
┌─────────────────────────────────────────────────────────────────┐
│  Schicht 4: Maschinenkonfiguration                              │
│  a5120.h: Karten instantiieren, Koppelbus verdrahten            │
├─────────────────────────────────────────────────────────────────┤
│  Schicht 3: Peripheriegeräte                                    │
│  k7637 (Tastatur), FloppyDrive (Image), emulierteDrucker       │
├─────────────────────────────────────────────────────────────────┤
│  Schicht 2: Steckkarten                                         │
│  k2526, k3526, k7024, k8025, k5122                             │
├─────────────────────────────────────────────────────────────────┤
│  Schicht 1: Chip-Primitive                                      │
│  Z80CPU, Z80PIO, Z80SIO, Z80CTC, RAMDevice, EPROMDevice        │
├─────────────────────────────────────────────────────────────────┤
│  Schicht 0: K1520 Bus-Simulator                                 │
│  K1520Bus (Systembus), Koppelbus (Signal-Router)               │
└─────────────────────────────────────────────────────────────────┘
```

### 5.1 Schicht 0: K1520 Bus-Simulator

Der Bus-Simulator ist der Kern des Systems. Alle Karten registrieren sich beim Bus und erhalten Callbacks für ihre zugewiesenen Adressbereiche.

```cpp
class K1520Bus {
public:
    // Registrierung: Karte meldet I/O-Port-Bereich an
    void registerIODevice(BusDevice* dev, uint8_t basePort, uint8_t numPorts);
    // Registrierung: Karte meldet Speicherbereich an
    void registerMemDevice(MemDevice* dev, uint16_t base, uint16_t size);

    // CPU-Zugriffe (von Z80CPU aufgerufen)
    uint8_t memRead(uint16_t addr);
    void    memWrite(uint16_t addr, uint8_t data);
    uint8_t ioRead(uint8_t port);
    void    ioWrite(uint8_t port, uint8_t data);

    // Bus-Signale (von Karten gesetzt)
    void assertINT(BusDevice* src);    // /INT anfordern
    void releaseINT(BusDevice* src);
    void assertNMI();
    void assertWAIT();
    void releaseWAIT();
    void assertRESET();

    // Interrupt-Daisy-Chain (Reihenfolge = Priorität)
    void setInterruptChain(std::vector<InterruptSlave*> chain);

    // DMA-Prioritätskette
    void setDMAChain(std::vector<DMADevice*> chain);

    // /MEMDI (Speicherzugriff sperren)
    void setMEMDI(bool disabled);
    bool isMEMDI() const;
};
```

### 5.2 Koppelbus

Der Koppelbus modelliert die Wickelbrücken-Verdrahtung der Backplane. Er ist kein generischer Bus, sondern ein **Signal-Router**: Karten können benannte Signale anbieten und empfangen. Die Verdrahtung ist in `machines/a5120/backplane.h` definiert.

```cpp
// In machines/a5120/backplane.h:
// Definiert die Verdrahtung der Wickelbrücken für A5120
struct A5120Backplane {
    static void wire(Koppelbus& kb,
                     K2526& zre, K8025& ass, K5122& afs, K7024& abs) {
        // Zweite Interrupt-Kette: ZRE BS-PIO hat niedrigste Priorität
        kb.connect(zre.IEO1(), ass.IEI1());
        kb.connect(ass.IEO1(), afs.IEI1());
        kb.connect(afs.IEO1(), abs.IEI1());
        kb.connect(abs.IEO1(), zre.IEI1_sink());

        // CTC-Taktverbindungen (ZRE CTC → K8025 CTC Takteingang)
        kb.connect(zre.ctc_zc_to2(), zre.ctc_clk_trg3());  // intern auf ZRE
        // Weitere Verbindungen nach Ermittlung der Backplane-Verdrahtung...
    }
};
```

### 5.3 Schicht 1: Chip-Primitive

Die Primitiven sind generische Implementierungen der Z80-Peripheriechips, unabhängig von jeder konkreten Karte.

**Z80PIO** (Q 301 / U 855):
- 2 Ports (A, B), jeweils 8 Bit
- Betriebsarten: Output, Input, Bidirektional, Bit-Mode
- Interrupt mit Daisy-Chain (IEI/IEO)

**Z80SIO** (Q 304 / U 856):
- 2 Kanäle (A, B), asynchron/synchron
- UART-Protokoll: Start-Stop, programmierbare Baudrate
- Interrupt mit Daisy-Chain

**Z80CTC** (Q 302 / U 857):
- 4 Kanäle, Timer und Zähler
- ZC/TO-Ausgänge für Taktverbindungen
- Interrupt mit Daisy-Chain

**Z80CPU**:
- Aus `src/z80.cpp` adaptiert
- Callbacks → K1520Bus (statt direkte Memory-Pointer)
- Zyklenzähler für Timing

**RAMDevice / EPROMDevice**:
- Einfache Speicherblöcke mit konfigurierbaren Adressbereichen
- EPROM initialisiert mit eingebetteten Daten aus `.h`-Datei

### 5.4 Schicht 2: Steckkarten

Jede Karte besteht aus:
1. Einer Klasse, die am K1520Bus und Koppelbus registriert wird
2. Primitiven (PIO, SIO, CTC, RAM, EPROM), die die Karte intern verwendet
3. DIP-Schalter / Brücken-Konfiguration (Template-Parameter oder Konstanten)

Beispiel-Interface für eine Karte:

```cpp
class K5122 : public BusDevice, public InterruptSlave {
public:
    // Konfiguration (entspricht DIP-Schaltern/Brücken)
    struct Config {
        uint8_t io_base = 0x10;      // Basis-I/O-Adresse (Standard A5120)
        bool    use_busrq = true;    // BUSRQ oder WAIT-Modus
        int     num_drives = 3;      // Laufwerke A/B/C
    };

    K5122(K1520Bus& bus, Koppelbus& koppel, const Config& cfg);

    // BusDevice Interface
    uint8_t ioRead(uint8_t port) override;
    void    ioWrite(uint8_t port, uint8_t data) override;

    // Laufwerk-Verwaltung (aus C-API aufrufbar)
    void    mountDrive(int drv, FloppyDrive* drive);
    void    unmountDrive(int drv);
    bool    isDriveActive(int drv) const;

    // InterruptSlave (Daisy-Chain)
    void    setIEI(bool) override;
    bool    getIEO() const override;
};
```

### 5.5 Maschinenkonfiguration (compile-time)

Eine Maschinenkonfiguration instantiiert alle Karten mit ihren konkreten DIP-Schalter-Einstellungen und verdrahtet den Koppelbus:

```cpp
// machines/a5120/a5120.cpp
class A5120Machine : public Machine {
    K1520Bus bus_;
    Koppelbus koppel_;

    // Karten mit A5120-spezifischer Konfiguration
    K2526 zre_{bus_, koppel_, K2526::A5120Config{}};
    K3526 ops_{bus_};
    K8025 ass_{bus_, koppel_, K8025::A5120Config{}};
    K5122 afs_{bus_, koppel_, K5122::A5120Config{}};
    K7024 abs_{bus_, koppel_, K7024::A5120Config{}};

public:
    A5120Machine() {
        // Koppelbus-Verdrahtung für A5120
        A5120Backplane::wire(koppel_, zre_, ass_, afs_, abs_);
    }
};
```

Die Umschaltung zwischen Maschinen erfolgt **vor dem Start** über die C-API:

```c
K1520Handle k1520_create(K1520MachineType type);  // K1520_MACHINE_A5120, etc.
```

---

## 6. Bildschirmemulation (K7024 → Framebuffer)

### 6.1 Architektur

```
K7024-Emulation
    │
    │ VRAM-Schreibzugriff (0xF800–0xFFFF, 1920 Bytes)
    ▼
Zeichengenerator-ROM (EPROM A103/A123, 1024 Bytes)
    │ Adressierung: [Bit6:0 = Zeichencode] + [Bit9:7 = Zeilen-Nr]
    │ → 8 Pixel pro Zeile
    ▼
Pixel-Framebuffer (intern in K7024)
    640 × 288 Pixel (80 Zeichen × 8px, 24 Zeilen × 12px)
    Format: 8-Bit Monochrom (0=schwarz, 255=weiß/grün)
    │
    │ k1520_get_framebuffer() via C-API
    ▼
Python Qt6: ScreenWidget
    │ QImage (640×288, Grayscale) → colorize (grüner Phosphor)
    │ Skalierung: Integer-Faktor (1×, 2×, 3×)
    ▼
QPainter → QWidget
```

### 6.2 Zeichengenerator-Adressierung (K7024-konform)

Das Zeichen-EPROM hat 1024 Bytes. Adresse: `[A9:A3] = 7-Bit ISO-Zeichencode`, `[A2:A0] = Zeile (0–7 von 12)`.

Für 12 Zeilen eines Zeichens werden je 8 Pixel aus dem EPROM gelesen (bei `/LP3`-Steuerung: die obere und untere Hälfte des EPROMs werden selektiv ausgewählt). Die genaue Adressierung wird in `doc/design/05_k7024_abs.md` spezifiziert.

### 6.3 Farbe

Die K7024 erzeugt ein monochromes Video-Signal (`/VIDEO`, aktiv-low). Der Framebuffer ist monochrom. Die Phosphorfarbe wird **ausschließlich in der Python-GUI** appliziert (kein Teil der C++ Emulation):

```python
def apply_phosphor(gray_img: QImage) -> QImage:
    # Phosphorfarbe A5120: P31 (grün), typisch #00FF40
    # Wird als RGB-Colorize über QImage angewandt
```

### 6.4 CLI-Textmodus

Im CLI-Modus umgeht K7024 den Zeichengenerator. Stattdessen:
- VRAM-Änderung → Unicode-Ausgabe auf stdout (Byte direkt als ASCII/ISO-8859-1)
- Cursor-Position wird via ANSI-Escape-Codes gesetzt

Die C-API bietet beide Schnittstellen:
```c
// GUI-Modus: Pixel-Framebuffer
const uint8_t* k1520_get_framebuffer(K1520Handle h);  // 640×288 Bytes

// CLI-Modus: Text-Änderungen
bool k1520_console_char_changed(K1520Handle h, int* x, int* y, char* ch);
```

---

## 7. Tastaturemulation (K7637 → K8025)

### 7.1 Signal-Kette

```
PC-Tastatur (Host)
    │ Qt6 KeyPressEvent oder stdin
    ▼
K7637-Emulation (C++ Klasse)
    │ Tastenmatrix → IFSS seriell, 9600 Baud, 1+8+1
    │ Tastencode-Tabellen aus Keyboard-EPROM
    ▼
K8025 SIO (Kanal B, Port 50H/51H)
    │ UART-Empfang, Interrupt (SIO-IEO/IEI-Kette)
    ▼
Z80 CPU: Interrupt-Handler liest Port 50H
    │ Tastencode an OS übergeben
```

### 7.2 K7637 Emulationsklasse

Die K7637 ist kein K1520-Bus-Gerät, sondern ein **externes Peripheriegerät** mit IFSS-Schnittstelle. Im Emulator verbindet es sich direkt mit dem SIO-Kanal des K8025:

```cpp
class K7637Keyboard {
public:
    // Anbindung an SIO-Kanal
    void connect(Z80SIO::Channel& sio_channel);

    // Eingabe-Interface (von außen aufgerufen)
    void keyPress(uint32_t host_keycode);
    void keyRelease(uint32_t host_keycode);
    void consoleChar(char c);  // für CLI-Modus

    // Kommandos von K8025 (Lampen, Akustik)
    void setLED(uint8_t led_mask);
    void beep();
};
```

### 7.3 Tastatur-Code-Tabellen

Die K7637 hat 3 Code-Tabellen (CTAB0, CTAB1, CTAB2) mit je Normal- und Shift-Belegung. Diese sind im Keyboard-EPROM (2KB) gespeichert. Da das EPROM nicht vorliegt, werden die Tabellen aus der Dokumentation rekonstruiert.

### 7.4 Kommandos an Tastatur (K8025 → K7637)

| Byte | Wirkung |
|------|---------|
| 00H | Software-Reset |
| 20H | Fehler-LED blinken |
| 44H | Akustisches Signal (~1s) |
| 52H | LED-Anzeigen ein/aus |
| 55H + xxH | Erweiterte LED-Steuerung |

---

## 8. Diskettenemulation (K5122 + FloppyDrive)

### 8.1 Schichtung

```
K5122 FDC-Karte
    │ PIO-Register (10H–18H)
    │ Steuersignale: /WE, MK, /STR, /ST, MR/SD, /HL
    │ Statussignale: /RDYL, /TO, /WP, /FW
    ▼
FloppyDrive (C++ Klasse, pro Laufwerk)
    │ Image-Datei (.img), Format-Definition (.cfg)
    │ Spur/Seite/Sektor → Byte-Offset im Image
    ▼
Host-Dateisystem
```

### 8.2 Disk-Format-Konfiguration

Das Format einer Diskette ist in einer Konfigurationsdatei im `cpaFormates.cfg`-Stil definiert:

```
disk cpa780
    cyls = 80
    heads = 2
    tracks 0-1.0 ibm.mfm  # Spur 0+1, Kopf 0: 26 Sektoren à 128 Bytes
        secs = 26
        bps = 128
    end
    tracks 2-79.0 ibm.mfm  # Spur 2-79, Kopf 0: 5 Sektoren à 1024 Bytes
        secs = 5
        bps = 1024
    end
    ...
end
```

Der `FormatParser` liest diese Datei und berechnet Byte-Offsets für jede Sektor-Anforderung.

### 8.3 K5122 PIO-Protokoll (vereinfacht)

Die K5122 verwendet zwei Z80-PIO-Chips für die Kommunikation mit dem Z80:
- **Steuer-PIO** (10H–13H): Kontrollsignale (seek, step, head load, write enable)
- **Daten-PIO** (14H–17H): Datenbytes beim Lesen/Schreiben
- **Laufwerksauswahl** (18H): Welches Laufwerk ist aktiv

Der Z80 kommuniziert über PIO-Interrupts (/BUSRQ-Modus) oder /WAIT-Verlängerung. Der Emulator löst den Datentransfer nach Abschluss einer internen Sektor-Operation aus.

### 8.4 Laufwerkszuweisung via C-API

```c
// Laufwerk A: mit CPA780-Image bestücken
k1520_set_disk_format(h, 0, "cpa780");
k1520_mount_disk(h, 0, "/path/to/cpadisk.img");

// Laufwerk B: leeres Image erstellen
k1520_create_disk(h, 1, "cpa800", "/path/to/new.img");
```

---

## 9. C-API (libk1520.so)

Die C-API ist die einzige Schnittstelle zwischen Python und dem C++-Kern. Sie ist `extern "C"` und damit stabil über Compiler-Versionen.

```c
// ─── Maschinen-Lifecycle ─────────────────────────────────────────
typedef void* K1520Handle;
typedef enum { K1520_MACHINE_A5120, K1520_MACHINE_PRG710 } K1520MachineType;

K1520Handle k1520_create(K1520MachineType type);
void        k1520_destroy(K1520Handle h);
void        k1520_reset(K1520Handle h);
void        k1520_power_on(K1520Handle h);
void        k1520_power_off(K1520Handle h);

// ─── Ausführungssteuerung ─────────────────────────────────────────
int         k1520_run(K1520Handle h, int max_cycles); // returns cycles used
void        k1520_stop(K1520Handle h);
bool        k1520_is_running(K1520Handle h);

// ─── Anzeige (GUI-Modus) ──────────────────────────────────────────
const uint8_t* k1520_framebuffer(K1520Handle h);  // 640×288 monochrom
int            k1520_fb_width(K1520Handle h);      // 640
int            k1520_fb_height(K1520Handle h);     // 288
bool           k1520_fb_dirty(K1520Handle h);      // seit letztem Aufruf?
void           k1520_fb_clear_dirty(K1520Handle h);

// ─── Anzeige (CLI-Modus) ──────────────────────────────────────────
void        k1520_set_console_mode(K1520Handle h, bool enable);
int         k1520_console_poll(K1520Handle h,     // gibt geändertes Zeichen zurück
                               int* x, int* y);   // Position; -1 = kein Ereignis

// ─── Tastatur ────────────────────────────────────────────────────
void        k1520_key_press(K1520Handle h, uint32_t keycode);
void        k1520_key_release(K1520Handle h, uint32_t keycode);
void        k1520_console_input(K1520Handle h, char c); // für CLI-Modus

// ─── Diskettenlaufwerke ───────────────────────────────────────────
bool        k1520_mount_disk(K1520Handle h, int drive,
                             const char* image_path,
                             const char* format_name);
bool        k1520_unmount_disk(K1520Handle h, int drive);
bool        k1520_disk_active(K1520Handle h, int drive);
bool        k1520_disk_write_protected(K1520Handle h, int drive);
void        k1520_set_write_protect(K1520Handle h, int drive, bool wp);

// ─── Serielle Schnittstellen (K8025) ─────────────────────────────
// Für externen Anschluss (Drucker, Terminal) via Python
typedef void (*K1520SerialRxCallback)(void* ctx, uint8_t byte);
void        k1520_serial_set_rx_callback(K1520Handle h, int port,
                                          K1520SerialRxCallback cb, void* ctx);
void        k1520_serial_tx(K1520Handle h, int port, uint8_t byte);

// ─── Debug ───────────────────────────────────────────────────────
typedef struct { uint16_t pc, sp, af, bc, de, hl, ix, iy; } K1520CpuRegs;
void        k1520_get_cpu_regs(K1520Handle h, K1520CpuRegs* out);
uint8_t     k1520_mem_read(K1520Handle h, uint16_t addr);
const char* k1520_last_error(K1520Handle h);
```

---

## 10. Python Qt6-Anwendung

### 10.1 Fenster-Layout A5120

```
┌─────────────────────────────────────────────────────────────────────┐
│ [ROBOTRON A5120]                              [●] Betriebsanzeige   │
├───────────────────────────────────┬─────────────────────────────────┤
│                                   │  ┌─────────────────────────┐   │
│   Bildschirm (640×288 → 2×)       │  │  Laufwerk A:            │   │
│   (grüne Phosphor-Farbe)          │  │  [cpa780     ▾] [Öffnen]│   │
│                                   │  │  cpadisk.img  [●] aktiv  │   │
│                                   │  ├─────────────────────────┤   │
│                                   │  │  Laufwerk B:            │   │
│                                   │  │  [cpa800     ▾] [Öffnen]│   │
│                                   │  │  (leer)                 │   │
│                                   │  ├─────────────────────────┤   │
│                                   │  │  Laufwerk C:            │   │
│                                   │  │  [cpa800     ▾] [Öffnen]│   │
│                                   │  │  (leer)                 │   │
│                                   │  └─────────────────────────┘   │
│                                   │                                 │
│                                   │  [RESET]  [Konfiguration...]    │
└───────────────────────────────────┴─────────────────────────────────┘
```

### 10.2 Emulator-Thread-Modell

```python
class EmulatorThread(QThread):
    frameReady = Signal()
    diskActivity = Signal(int, bool)

    def run(self):
        CYCLES_PER_FRAME = 2_457_600 // 60  # 2.4576 MHz / 60 Hz
        while not self._stop:
            k1520_run(self.handle, CYCLES_PER_FRAME)

            if k1520_fb_dirty(self.handle):
                k1520_fb_clear_dirty(self.handle)
                self.frameReady.emit()

            for drv in range(4):
                self.diskActivity.emit(drv, k1520_disk_active(self.handle, drv))
```

### 10.3 Maschinenkonfiguration (JSON)

```json
{
  "machine": "A5120",
  "display_name": "Robotron A5120",
  "theme": "a5120",
  "drives": [
    {"slot": 0, "label": "Laufwerk A:"},
    {"slot": 1, "label": "Laufwerk B:"},
    {"slot": 2, "label": "Laufwerk C:"}
  ],
  "serial_ports": [
    {"slot": 0, "label": "V.24 / DFÜ"},
    {"slot": 1, "label": "Drucker IFSS"}
  ]
}
```

---

## 11. CLI-Modus

```bash
# GUI-Modus (Standard)
k1520emu --machine a5120 --disk-a cpadisk.img:cpa780

# CLI-Modus (Terminal-I/O)
k1520cli --machine a5120 --disk-a cpadisk.img:cpa780 --console

# Batch-Modus (Skripting)
k1520cli --machine a5120 --disk-a cpadisk.img:cpa780 \
         --inject "DIR" --output /tmp/dir.txt --exit-on-idle
```

Im CLI-Modus:
- K7024 schreibt Unicode auf stdout statt Framebuffer
- K7637 liest von stdin (raw mode)
- Kein Qt6-Import notwendig

---

## 12. Build-System

```cmake
# CMakeLists.txt (root)
cmake_minimum_required(VERSION 3.20)
project(k1520emu)

# Optionen
option(BUILD_SHARED_LIBS "Build cards as shared libs for testing" OFF)
option(BUILD_GUI         "Build Python Qt6 application"          ON)
option(BUILD_CLI         "Build CLI binary"                      ON)
option(BUILD_TESTS       "Build test suite"                      ON)

add_subdirectory(core)     # libk1520core (+ optionale Karten-Libs)
add_subdirectory(cli)      # k1520cli
# Python-App: kein cmake-Target, via pip install -e .
```

**Für Entwicklung/Tests** (`-DBUILD_SHARED_LIBS=ON`):
- `libk1520_bus.so` – Bus-Simulator
- `libk1520_k7024.so` – Bildschirmkarte (einzeln testbar)
- `libk1520_k5122.so` – FDC (einzeln testbar)
- `libk1520core.so` – Alles zusammen (C-API)

**Für Produktion** (`-DBUILD_SHARED_LIBS=OFF`):
- `libk1520core.so` – Alles statisch zusammengelinkt, eine .so

---

## 13. Testkonzept

### 13.1 C++ Unit-Tests (GoogleTest)

```cpp
// tests/cpp/test_k7024.cpp
TEST(K7024, VRAMWriteUpdatesFramebuffer) {
    K1520Bus bus;
    K7024 screen(bus, K7024::A5120Config{});
    bus.memWrite(0xF800, 'A');  // Zeichen 'A' schreiben
    EXPECT_EQ(screen.getFramebuffer()[0..7], charset_latin['A'][0]);
}
```

### 13.2 Python-Integrationstests (pytest + ctypes)

```python
# tests/python/test_k5122.py
def test_floppy_seek(k5122_lib, tmp_image):
    drv = k5122_lib.k5122_create()
    k5122_lib.k5122_mount(drv, 0, tmp_image, "cpa780")
    # Emuliere SEEK-Kommando via PIO
    k5122_lib.k5122_pio_write(drv, 0x10, STEP_IN | HEAD_LOAD)
    time.sleep(0.001)
    assert k5122_lib.k5122_get_track(drv, 0) == 1
```

---

## 14. Offene Fragen (vor Implementierungsbeginn zu klären)

### 14.1 KRITISCH: Koppelbus-Verdrahtung A5120

Die Verdrahtung der Wickelbrücken in der A5120-Backplane ist für die korrekte Emulation der Interrupt-Priorität und der CTC-Taktverteilung essenziell.

**Benötigt:**
1. Vollständige Interrupt-Prioritätskette (/IEI–/IEO): Welche Karte hat welche Priorität?
2. Zweite Kette (/IEI1–/IEO1 via Koppelbus): Reihenfolge der Karten?
3. CTC-Verbindungen: Welche `ZC/TO`-Ausgänge gehen zu welchen `CLK/TRG`-Eingängen?
4. Ist `/SUE` (Spannungsüberwachung) mit einer externen Schaltung verbunden?

→ **Bitte Backplane-Verdrahtungsplan oder Schaltplan der Rückverdrahtungseinheit beschaffen.**

### 14.2 WICHTIG: K8025 – Welcher SIO-Kanal für Tastatur?

Die K8025 hat zwei SIO-Chips:
- **SIO A33** (Port 50H–53H): "DFÜ" – ist dies die Tastatur (K7637)?
- **SIO A32** (Port 5CH–5FH): "Drucker" – nur Drucker?

Welcher Kanal (A oder B von SIO A33) ist mit dem IFSS-Anschluss X5 verbunden, der zur K7637 geht?

### 14.3 WICHTIG: K3526 OPS – Reine passive DRAM?

Hat die K3526 außer dem 64KB DRAM irgendwelche Steuerlogik (Banking-Register, Schreibschutz-Logik)? Oder ist sie wirklich nur ein passiver 64KB DRAM-Block?

### 14.4 Keyboard-EPROM K7637

Das EPROM der K7637 (2KB, Scancodes) liegt nicht vor. Die Code-Tabellen können aus der Dokumentation rekonstruiert werden, aber eine Überprüfung wäre hilfreich.

### 14.5 PRG710 / K8915 Hardware

Für diese Maschinen fehlen noch alle Hardware-Informationen. Die Architektur unterstützt sie bereits als Konfiguration, aber die konkreten Karten und deren Verdrahtung müssen noch ermittelt werden.

---

## 15. Migrationspfad aus bestehendem Code

| Komponente | Herkunft | Aktion |
|------------|----------|--------|
| `src/z80.cpp/h` | Legacy | In `core/primitives/z80_cpu.cpp/h` übernehmen, Bus-Callbacks anpassen |
| `src/floppy.cpp/h` | Legacy | Image-Lese-/Schreiblogik in `core/peripherals/floppy_drive/` übernehmen |
| `tools/z80_disasm*.py` | Legacy | Unverändert übernehmen, `eprom_to_h.py` hinzufügen |
| `src/cpa_bios.cpp` | Legacy | Nicht übernehmen (HALT-Trap-Ansatz entfällt) |
| `src/terminal*.cpp` | Legacy | Nicht übernehmen (durch K7024+K7637 ersetzt) |
| `cparun/` | Legacy | Komplett unverändert beibehalten |
| `cpa_src/` | Referenz | Nur zum Studium des CPA-BIOS, kein Code übernehmen |
| `disks/*.img` | Assets | Weiterhin verwendbar |

---

*Detaillierte Feinentwürfe für jedes Modul in `doc/design/0X_*.md`.*
