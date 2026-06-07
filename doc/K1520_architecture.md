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
│  Slot 1: K3526 (OPS) – Operationsspeicher 64 KB DRAM          │
│  Slot 2: K5122 (AFS) – Anschlußsteuerung Folienspeicher (FDC) │
│  Slot 3: K8025 (ASS) – Anschlußsteuerung Seriell (V.24+IFSS)  │
│  Slot 4: K2526 (ZRE) – Zentrale Recheneinheit                 │
│  Slot 5: K7024 (ABS) – Adapter Bildschirm (CRT-Controller)    │
│  Slot 6-11: leer (erweiterbar)                                 │
└────────────────────────────────────────────────────────────────┘
            │                    │
      Systembus               Koppelbus
      (X1, 58-pol.)          (X2, 58-pol.)

Documented variant A5120.16 slot sequence:

1. K8025 (ASS) - serial interfaces
2. 062-9005 - Z8000 CPU card
3. 062-9000 - Z8000 RAM card
4. K2526 (ZRE) - central processing unit
5. K7024 (ABS) - display
6. K5122 (AFS) - floppy controller
7. K3526 (OPS) - RAM
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

## 14. Interrupt-System A5120 (vollständige Spezifikation)

> **Quellenlage / Belegstatus.** Diese Spezifikation wurde gegen die transkribierte
> Originaldokumentation abgeglichen (`doc/trascripted/`: *Zentrale Recheneinheit
> K2526/K2527*, *Anschlußsteuerung K 8025*, *Floppy Anschlußsteuerung K 5122*,
> *Linieninterface BUS K 1520* (TGL 37271/01), *Büro Computer Robotron A5120*).
> Jede Aussage ist markiert: **[belegt]** = wörtlich in der Originaldoku,
> **[abgeleitet]** = aus Schaltungslogik geschlossen (plausibel, aber nicht wörtlich),
> **[Emulator]** = Implementierungsentscheidung im Code, **[offen]** = ungeklärt.

### 14.0 K1520-Bus: Pinbelegung und Anzahl der Ketten

**[belegt]** TGL 37271/01 (Tabelle 11) definiert auf dem **Systembus X1** genau
**eine** Interrupt-Prioritätskette und eine DMA-Kette:

| Signal | Pin | Signal | Pin |
|--------|-----|--------|-----|
| /INT   | C23 | /NMI   | A23 |
| /IEI   | C10 | /IEO   | A10 |
| /BAI   | (DMA-Kette) | /BAO | (DMA-Kette) |

**[belegt]** Die zweite Kette /IEI1–/IEO1 ist laut TGL **kein X1-Signal**, sondern
ein Mittel, um *innerhalb einer Karte* mehrere Interrupt-Controller in Reihe zu
schalten (TGL Bild 2, Verweis auf Koppelbus-Tabelle 12 / KROS 0314). Wird sie
*zwischen* Karten geführt, dann über den **Koppelbus X2**. Die konkrete A5120-X2-
Belegung ist in den vorliegenden Transkripten **nicht** enthalten (KROS 0314 fehlt).

### 14.1 Primäre Daisy-Chain (K1520-Systembus, /IEI–/IEO)

**[belegt]** Verbindung: /IEO (A10) eines Slots → /IEI (C10) des nächsten, über fest
verlötete Wire-Wrap-Brücken der Backplane (nicht automatisch durch Slot-Nachbarschaft).
Karten ohne Interrupt-Fähigkeit (K3526 OPS, K7024 ABS) müssen /IEI→/IEO brücken oder
am Kettenende liegen.

**Implementierte Reihenfolge (höchste → niedrigste Priorität) [Emulator]:**

```
K5122 AFS → K8025 ASS → K2526 ZRE
```

```cpp
// core/machines/a5120/a5120.cpp:46
bus_.setInterruptChain({&afs_, &ass_, &zre_});
```

**Begründung & Auflösung eines Doku-Widerspruchs.** Die robotrontechnik-Beschreibung
(*Büro Computer Robotron A5120*) sagt narrativ „die ZRE besitzt die höchste Priorität".
Das **K2526-Datenblatt** ist hier aber präziser und maßgeblich [belegt]:

- Der **CTC der ZRE** liegt am **Ende der 1. Prioritätenkette** (Eingang /IEI Systembus,
  Ausgang /IEO1 Koppelbus) — §2.5 K2526-Doku.
- Die **zentrale Baugruppensteuerung (BS-PIO)** liegt am **Ende der 2. Prioritätenkette**
  (/IEI1 Koppelbus) — §2.9 K2526-Doku.

Die ZRE-Bausteine sitzen also an den **Kettenenden = niedrigste Priorität**; die
zeitkritischen Peripheriekarten (AFS, ASS) gehen voran. Der Emulator faltet beide
physischen Ketten in **eine** Kette zusammen: `{afs, ass, zre}`, wobei `zre` intern
`CTC → BS-PIO` ist. Effektiv ergibt das `AFS → ASS → ZRE-CTC → ZRE-BS-PIO` und bildet
damit die reale Zwei-Ketten-Topologie korrekt nach (CTC am Ende von Kette 1, BS-PIO am
absoluten Ende). Die narrative „ZRE höchste Priorität"-Aussage ist damit als ungenau
einzustufen.

### 14.2 Interne Interrupt-Ketten der Karten

#### K2526 ZRE (intern: CTC → BS-PIO) — **[belegt]**

```
/IEI-Eingang
    → Z80CTC A35  (Q302, Ports 0CH–0FH; Kanal 0 > 1 > 2 > 3)   ← höhere Priorität
    → Z80PIO A36 BS-PIO (Q301, Ports 08H–0BH; Port A > Port B)
/IEO-Ausgang
```

- **[belegt]** „Bei der ZRE K 2526 hat der CTC eine höhere Priorität als der BS-PIO"
  (K2526-Doku §5). Code: `K2526::setIEI` → `ctc_.setIEI(iei); bs_pio_.setIEI(ctc_.getIEO())`.
- **[belegt]** CTC Kanal 2 → Kanal 3 ist **fest auf der Platine verdrahtet** (ZC/TO2 →
  CLK/TRG3) zur Bildung der Systemzeit; Kanal 3 erzeugt die Systemzeit-Interrupts.
- **[belegt]** Die 2. ZVE (ZVE2, DMA-Prozessor) ist **nicht interruptfähig** (/INT, /NMI
  fest high) — sie führt keine ISR aus.
- **BS-PIO Port A** (Bitmode): gemeinsamer Vektor für /M1 (Einzelschritt), /SUE (Batterie),
  SPS-Verletzung, /EBF; ROM-Phase nutzt /ASTB-Pfad. **Port B**: separater Vektor, einzige
  Quelle B1 (/INT-BS via OUT 00H = OS-Ebenen-Wechsel).

#### K8025 ASS (intern: SIO A33 → SIO A32 → CTC A34) — **[belegt]/[abgeleitet]**

```
/IEI-Eingang
    → Z80SIO A33 (DFÜ-SIO, Ports 50H–53H; Ch A > Ch B)   ← höchste Priorität [belegt]
    → Z80SIO A32 (Tastatur/Drucker-SIO, Ports 5CH–5FH)   ← [abgeleitet: Reihenfolge]
    → Z80CTC A34 (Baudraten-Generator, Ports 58H–5BH)    ← [abgeleitet: Position]
/IEO-Ausgang
```

- **[belegt]** „Die DFÜ besitzt in jedem Fall die höhere Priorität gegenüber dem Drucker"
  (K8025-Doku §2.2.2) ⇒ SIO A33 vor SIO A32. Position von CTC A34 in der Kette ist
  abgeleitet (im Code `sio_dfue_ → sio_kbd_printer_ → ctc_a34_`).
- **Korrektur ggü. früheren Fassungen:** **A31 (Ports 54H–57H) ist KEIN PIO**, sondern ein
  **U212-Latch-Register** ohne Interrupt-Logik [belegt]. Es gehört **nicht** in die Daisy-Chain.
  Der Emulator modelliert es zwar als `Z80PIO pio_a31_` (DIL-Schalter-Readout), schließt es
  aber bereits aus der Kette aus (`k8025.cpp:58` „PIO A31 excluded from chain") — korrekt.
- **[belegt]** SIO A32 Kanal A (Port 5CH): Tastatur K7637, 9600 Baud IFSS (X4; verdrängt den
  Zusatzdrucker). Kanal B (Port 5EH): Hauptdrucker IFSS (X3).
- **[belegt]** SIO A33 Kanal A (Port 50H): DFÜ V.24 (X6). Kanal B (Port 52H): DFÜ IFSS (X5).
- **[belegt]** CTC A34 Kanal 0 taktet den Drucker-SIO (fest 9600 Bd); die DFÜ-Takte kommen
  im Normalfall vom **ZRE-CTC** über den Koppelbus (ZC/TO). Drucker-Priorität ist per
  Brücken W1:8–11 umschaltbar (zeitkritisch = 1. Kette nach DFÜ; zeitunkritisch = 2. Kette).

#### K5122 AFS (intern: Steuer-PIO → Daten-PIO) — **[belegt]/[abgeleitet]**

```
/IEI-Eingang
    → Z80PIO ctrl_pio_ (Steuer-PIO A1.2, Ports 10H–13H; Port A > Port B)
    → Z80PIO data_pio_ (Daten-PIO  A1.1, Ports 14H–17H; Port A > Port B)
/IEO-Ausgang
```

- **[belegt]** Der Steuer-PIO ist „in die Interruptkette für zeitkritische Geräte
  eingeordnet (/IEI–/IEO)". Interne Reihenfolge Steuer→Daten ist [abgeleitet].
- **[belegt]** Steuer-PIO **Port A** läuft im **Mode 0 (Output)**, sein Strobe-Eingang
  **/ASTB = physikalischer INDEX-Puls (IX)** des Laufwerks (Pinbelegungstabelle Tor A).
  Die fallende Flanke löst den Port-A-Interrupt aus → in der ROM-Phase Vektor 0xBA.
- **[belegt]** Steuer-PIO **Port B** läuft im **Mode 3 (Bitmode)** mit Statussignalen
  (B0 /RDYL, B1 MKE, B5 /WP, B6 /FW, B7 /TO …). Der Interrupt entsteht hier durch
  Pegelüberwachung der maskierten Bits, **nicht** über eine /BSTB-Flanke.
- **[offen]** Welches physikalische Signal in der Bootloader-Phase den Port-B-Interrupt
  (Vektor 0x60) treiben soll, ist in der Hardware-Doku **nicht** dokumentiert.

### 14.3 Interrupt-Modus

Der Z80 arbeitet im **Interrupt-Modus 2 (IM 2)**. Die Interrupt-Vektortabelle liegt im RAM:
- **I-Register × 256 + Vektor** = Adresse des 16-Bit-ISR-Zeigers
- Das niederwertigste Bit des Gerätevektors muss 0 sein (gerade Tabellenadressen)

### 14.4 Interrupt-Vektoren nach Boot-Phase

#### Boot-ROM-Phase (I = 0x00, IM 2)

| Interrupt-Quelle | Vektor | Tabellen-Adresse | ISR | Funktion |
|---|---|---|---|---|
| K2526 BS-PIO Port A (/ASTB) | 0xB8 | [0x00B8..B9] | 0x007A | Unbekannt (ROM-intern) |
| **K5122 ctrl_pio_ Port A (/ASTB)** | **0xBA** | [0x00BA..BB] | **0x01C7** | **Indexpuls-Dekrement [0x03F7]** |
| K5122 ctrl_pio_ Port B | 0xE2 | [0x00E2..E3] | — | Nicht genutzt |

Der Vektor 0xBA (ISR 0x01C7) ist der **kritische Index-Puls-Interrupt**: Der K5122 ctrl_pio_ Port A empfängt den Floppy-Indexpuls als /ASTB-Signal (Mode 0, Output) und triggert diesen Interrupt. Die ISR dekrementiert [0x03F7] und signalisiert Timeout, falls der Sektor nicht innerhalb von 16 Umdrehungen gelesen wird.

**Wichtig:** Vektor 0xBA kommt von der **K5122 ctrl_pio_ Port A**, nicht vom ZRE-CTC.

#### Bootloader-Phase (I = 0x07, IM 2)

Nach Übergabe an 0x0437 setzt der Loader `LD A,07H / LD I,A`. Alle Vektoren werden nun über Seite 0x07 aufgelöst.

| Interrupt-Quelle | Vektor | Tabellen-Adresse | ISR | Funktion |
|---|---|---|---|---|
| K5122 ctrl_pio_ Port B | 0x60 | [0x0760..61] | 0x0624 | Event-ISR: setzt Bit 0 in [0x07F7] |
| K5122 ctrl_pio_ Port A | 0x62 | [0x0762..63] | 0x060E | Timer-ISR: dekrementiert [0x07EC], setzt Bit 1 in [0x07F7] bei Timeout |

Die Loader-Phase erbt `IFF1=1` und `IM 2` vom Boot-ROM (kein `DI` im Übergabepfad).

### 14.5 ZVE2-Completion und ctrl_pio_ Port A IE — **gelöst (Stand Trace 2026-06)**

ZVE2 schreibt am Ende jedes ROM-DMA-Durchlaufs (ROM 0x0267) `OUT(11H),03H`
(Interrupt-Control-Word, IE=0) und deaktiviert damit den ctrl_pio_-Port-A-Interrupt
(Indexpuls, Vektor 0xBA). Der Bootloader programmiert anschließend nur den Vektor
(`OUT(11H),0x62`, Bit0=0), aber kein IE-Enable.

**Diese Lücke ist im Emulator bereits geschlossen:** `K5122::endDmaTransfer()`
([`k5122.cpp:708`](../core/cards/k5122/k5122.cpp)) ruft nach jeder erkannten
DMA-Completion `ctrl_pio_.ioWrite(1, 0x83)` auf (IE=1, OR, active-LOW) und stellt
Port-A-IE wieder her.

**Trace-Beleg (`boot_trace -p 9000000 disks/cpadisk01.img`):** In der Post-Banner-Phase
werden Interrupts **mit Vektor 0x62 korrekt zugestellt** (an PC 052D/052E/0530/…), nicht
mehr 0xBA. Damit ist die frühere Diagnose „Timer-ISR feuert nie / nur 0xBA" **überholt**.
Die `analyse_bootloader.md` §5 beschreibt noch den alten Stand.

### 14.6 Tatsächliche Freeze-Ursache: nicht erkannte DMA-Completion in Loader-Runden

**Das Interrupt-System funktioniert in der Bootloader-Phase** (IM2, IFF1=1, Daisy-Chain,
Vektor-Auflösung, RETI, Port-A-Index-Interrupt mit wiederhergestelltem IE — alles im Trace
bestätigt). Der Freeze hat eine **andere** Ursache, die im Trace eindeutig belegt ist:

1. Der Loader biegt bei `0x0489` den **ZVE2-Einsprungvektor** um:
   `LD (0001H),062EH` ⇒ `[0x0000..2] = C3 2E 06 = JP 0x062E`. Ab jetzt führt ein
   ZVE2-Neustart **nicht** mehr die Boot-ROM-DMA-Routine (`0x01DD`) aus, sondern die
   **loader-eigene** Sektor-Leseroutine bei `0x062E` (liest via `IN A,(16H)`, kopiert per
   `INIR`, inkrementiert den Empfangszähler `[0x07E3]`, schaltet am Ende die PIO-Interrupts
   mit `OUT(11H),03 / OUT(13H),03` ab).
2. Diese Loader-Routine schreibt **niemals `[0x03F8]`** (per Disassemblat verifiziert).
3. `A5120Machine::run()` erkennt DMA-Completion **ausschließlich** über einen `0→3`-Übergang
   von `[0x03F8]` (ROM-Konvention) und ruft nur dann `endDmaTransfer()` /
   `bus_.releaseBUSRQ()`.

**Folge:** Für die Loader-getriebenen Runden (ab Runde 3, Post-Banner) wird `/BUSRQ`
**nie freigegeben**. Trace: `assertBUSRQ`=3, aber `releaseBUSRQ`=2 — die 3. Runde hängt.
ZVE1 läuft nur dank des konkurrierenden Steppings (`a5120.cpp:158`) weiter, spinnt aber in
seiner Warteschleife `0x052A`, weil der Empfangszähler `[0x07E3]` bei 1 stehen bleibt
(`[0x07E3]==[0x07EA]==1`, `[0x07EC]=5`, `[0x07F7]=0`).

**TIEFERE URSACHE (Trace 2026-06-07, nach dem Head-Fix):** Die fehlende `/BUSRQ`-Freigabe
ist nur ein *Folgesymptom*. Der eigentliche Blocker ist ein **Sektorformat-Mismatch**:
Die loader-eigene ZVE2-Leseroutine (`0x062E`) erwartet ein **realistisches MFM-Format**,
der emulierte Spur-Stream liefert aber ein auf die **Boot-ROM zugeschnittenes,
vereinfachtes** Format. ZVE2 hängt deshalb in seiner Fehler-/Retry-Schleife (`0x06B2`)
fest (Trace: 22 792× Steuerwort `0x87` + 22 784× `0xB5`, `/STR` nie auf 1, daher auch
nie BUSRQ-Freigabe).

**Byte-genaue Gegenüberstellung (ROM-Disassemblat `0x025A→0x020B→0x0249` vs. Loader `0x0648/0x0678`):**

| Position | Boot-ROM-Routine (NZ-Pfad, `[03FD]=0x87`) | Loader-Routine (`0x062E`) |
|---|---|---|
| buf[0] | gelesen, **verworfen** (beliebig) | muss `0xA1` (skip) **oder** `0xFE` sein |
| buf[1] | `0xFE` (IDAM, Vergleich) | `0xFE` (IDAM) |
| buf[2..4] | cyl, head, sec (Vergleich) | cyl, head, sec (Vergleich) |
| buf[5] | size (Vergleich) | size (gelesen, verworfen) |
| Füll-/Gap | **2 Bytes** (buf[6], buf[7]) verworfen | **11 Gap-Bytes** + 1 Byte + `0xFB` DAM |
| Daten | ab **buf[8]** (128 Bytes, INI×3+INIR) | ab **buf[19]** (128 Bytes, INIR) |
| CRC | 2 Bytes | 2 Bytes |
| Blocklänge | **138 Bytes** | **~149 Bytes** |

Was `K5122::doReadSector` baut (138-Byte-Block, exakt auf die ROM getunt):
```
0x00 0xFE cyl head sec size 0x00 0x00 <128 Daten> 0x00 0x00    ← KEIN 0xA1-Sync, KEIN 0xFB-DAM, nur 2-Byte-Gap
```

**Schlussfolgerung (ROM-Routine vollständig analysiert):** Die Boot-ROM liest mit **festen
Offsets** und sucht **weder** `0xA1`-Sync **noch** die `0xFB`-Datenmarke; sie verträgt nur das
2-Byte-Gap. Die Loader-Routine sucht aktiv Marken und erwartet ein langes Gap + `0xFB`-DAM,
mit Daten bei Offset ~19 statt 8. **Ein einziger statischer Block kann beide nicht bedienen**
(Daten können nicht gleichzeitig bei Offset 8 und 19 liegen). Der „ein-einheitliches-Format"-
Ansatz ist damit **verworfen**.

**Region-Hypothese widerlegt (2026-06-07).** Beide Routinen lesen **dieselbe Region**: cyl 0,
head 0 = Boot-Spur (cpa780: cyl 0–2 = 26×128 B, `format_parser.cpp:109`). Es ist **kein**
Format-/Region-Unterschied, sondern ein **unterschiedliches `/STR`-Zugriffsmuster**:

| | Boot-ROM (`0x01DD`) | Loader-ZVE2 (`0x062E`) |
|---|---|---|
| Sektor-Start | `01F4` `/STR=1` → `01FA` `/STR=0` (1 Abfall) | ZVE1 `OUT(10),A1` `/STR=0` |
| nach Header | `/STR` bleibt 0 → liest Header+Daten **kontinuierlich** | `0643` `/STR=1`, liest IDAM+Gap, dann **`066E` `/STR=0` (2. Abfall mitten im Sektor)** |
| Daten | direkt (Offset 8) | **nach 2. Strobe** → sucht `0xFB`-DAM, dann Daten |

**Korrektes Hardware-Modell:** Jeder `/STR`-Strobe lässt den K5122 **zur nächsten
Adressmarke synchronisieren** — 1. Strobe → IDAM (`0xFE`), 2. Strobe → Datenmarke (`0xFB`).
Die ROM liest mit *einem* Strobe IDAM+Daten am Stück (Minimal-Pfad); der Loader nutzt einen
*zweiten* Strobe, um gezielt auf das Datenfeld zu re-synchronisieren. Der Emulator streamt
dagegen nur einen **linearen Puffer** und ignoriert den 2. Strobe — daher findet der Loader
nie sein `0xFB`-DAM.

**Generischer Fix (UMGESETZT 2026-06-07):** In `K5122` wird ein `/STR`-Abfall, der **mitten in
einem laufenden Lese-Transfer** auftritt (also *nachdem* die IDAM/Header bereits gestreamt
wurden — das tut **nur** der Loader; die ROM hält `/STR` durchgehend auf 0), als **Re-Sync auf
das Datenfeld** modelliert: `beginDataField()` gibt ab da `A1 FB <128 Daten> CRC CRC` des
aktuellen Sektors aus (`k5122.cpp`, neue Member `in_data_field_`/`data_field_buf_`). Das Sync-
Byte in `doReadSector` ist von `0x00` auf `0xA1` gesetzt. Die ROM erzeugt keinen zweiten
`/STR`-Abfall → ihr kontinuierlicher Lesepfad bleibt unberührt. **Alle 40 K5122/Boot-Tests grün**
(2 Tests auf den neuen `0xA1`-Kontrakt angepasst).

**Resultierender Fortschritt (Trace):** ZVE2 liest jetzt **echte Sektordaten** (statt sofort in
die Fehlerschleife zu laufen), steppt über Track-Grenzen (cyl 0 → cyl 1) und **lädt die erste
Stufe des Folge-Loaders nach `0x0800`** — dort steht jetzt die Signatur `53 59 4C` = `"SYL"`
plus echter Z80-Code (vorher: 0 Bytes geladen, sofortiger Freeze).

**Verbleibendes Problem [offen] — Per-Sektor-Pacing:** Der Mehrsektor-Load akkumuliert noch
nicht vollständig. In der aktuellen Modellierung liest ZVE2 in **einer** BUSRQ-Session die ganze
rotierende Spur kontinuierlich, der Loader erwartet aber ein **Per-Sektor-Handshake** (ZVE2 liest
*einen* Sektor → signalisiert ZVE1 via `[07E3]++` → ZVE1 verarbeitet/CRC-prüft → nächster Sektor).
Folge: nur ~1 Sektor landet sauber bei `0x0800`, der Sektorzähler `[07FC]` unterläuft (0x34→0xFD),
ZVE1 spinnt weiter bei `0x052A`. Nächster Schritt: BUSRQ als **Per-Sektor-Session** modellieren
(ZVE2-Routine `0x062E` setzt am Sektorende `/STR=1` via `OUT(10),0xAD` — als Session-Ende nutzen),
statt der „ganze-Spur-am-Stück"-Session. Das ist ein eigenständiger, tieferer Umbau.

**Bereits umgesetzt (kein Hook, echter Bugfix):** `current_head_` wird jetzt **vor**
`doReadSector` aus dem MR/SD-Bit (Bit5) des Start-Steuerworts gelatcht
([`k5122.cpp` ZVE1-Read-Start-Zweig](../core/cards/k5122/k5122.cpp)). Vorher las Runde 3
mit dem veralteten Head=1 → IDAM-Head-Mismatch. Jetzt lesen alle Anforderungen Head 0
korrekt; alle 40 K5122/Boot-Tests bleiben grün. Dies beseitigt den *ersten* von zwei
Format-Mismatches; der zweite (0xA1-Sync / 0xFB-DAM / Gap) bleibt offen.

**Sekundär [offen]:** Der Loader bewaffnet zusätzlich ctrl_pio_ **Port B** (Vektor 0x60,
Event-ISR 0x0624). Dieser Interrupt wird im Emulator nie ausgelöst (`setBSTB` nie
aufgerufen). Erst nach Lösung des Format-Mismatches feststellbar, ob er benötigt wird.

### 14.7 Zweite Interrupt-Kette (/IEI1–/IEO1, Koppelbus) — **[abgeleitet/spekulativ]**

**[belegt]** Real existiert die zweite Kette: das K2526-Datenblatt führt /IEI1 (X2 Pin 26C)
und /IEO1 (X2 Pin 26A) und ordnet die BS-PIO „am Ende der 2. Prioritätenkette" ein. Der
ZRE-CTC speist seinen /IEO als /IEO1 auf den Koppelbus.

**[spekulativ]** Die konkrete A5120-Ringverdrahtung über alle Karten ist in den
vorliegenden Transkripten **nicht** belegt (KROS 0314 / TGL-Tabelle 12 fehlt). Die früher
hier angegebene Kette `ZRE.IEO1 → ASS.IEI1 → … → ABS → zurück` ist eine **Annahme**, nicht
gesichert. Sicher ist nur: BS-PIO der ZRE = niedrigste Priorität; ein „zeitunkritischer"
Drucker-SIO der K8025 kann (Brücken W1:8–11) in dieser Kette liegen.

**[Emulator]** Diese Kette ist nur als Koppelbus-Platzhalter vorhanden, **nicht verdrahtet**
(`backplane.h`). Für den aktuellen Boot ist sie nicht relevant (alle Boot-Interrupts laufen
über die primäre Kette).

### 14.8 NMI-Quellen — **[belegt]**

| Quelle | Mechanismus | NMI? |
|---|---|---|
| Q240 MemIOProtect: **unerlaubter E/A-Befehl** | `/IORQ` über A39/6 → /NMI (flüchtig) | **ja** → `bus_.assertNMI()` |
| Q240: **unerlaubter Speicher-Schreibzugriff** | setzt **BS-PIO Port A Bit 3 (SPS)** + `/MEMDI=0` | **nein** (maskierbarer Port-A-Interrupt) |
| /SUE (Koppelbus): Spannungsüberwachung/Netzeinbruch | permanenter NMI | ja (im Emulator nicht implementiert) |

**Wichtige Korrektur:** Nicht *jede* Schutzverletzung löst NMI aus. Nur die **E/A**-Verletzung
führt zum NMI; die **Speicher**-Schreibschutzverletzung erzeugt einen *maskierbaren* Interrupt
über BS-PIO Port A Bit 3. NMI führt zu `RST 0066H`. Der NMI-Handler liest BS-PIO Port A Bit 2
zur Quellenunterscheidung (low=Sonderbedingung permanent, high=E/A-Verletzung flüchtig) und
schreibt Port 02H (/RES-SPA) zum Rücksetzen.

### 14.9 Offene Implementierungslücken (Stand 2026-06)

| Lücke | Status | Auswirkung |
|---|---|---|
| **Per-Sektor-Pacing** (ZVE2 liest ganze Spur in 1 BUSRQ-Session statt 1 Sektor/Session) | **offen — aktuelle Freeze-Ursache** | Mehrsektor-Load akkumuliert nicht; nur ~1 Sektor bei 0x0800, `[07FC]` unterläuft (siehe §14.6) |
| Sektorformat-Mismatch (0xA1-Sync + 0xFB-DAM via /STR-Re-Sync) | **erledigt** | `beginDataField()` modelliert den 2. /STR-Strobe; ZVE2 liest jetzt echte Daten, „SYL"-Stufe nach 0x0800 geladen |
| Head-Latch vor doReadSector | **erledigt** | Runde 3 las mit veraltetem Head=1; jetzt korrekt Head 0 (alle 40 K5122/Boot-Tests grün) |
| DMA-Completion nur via `[0x03F8]=3` (ROM-Konvention) | offen (Folgeproblem) | greift erst, wenn der Format-Mismatch gelöst ist und ZVE2 die Runde beendet |
| ctrl_pio_ Port A IE-Wiederherstellung nach DMA | **erledigt** (`endDmaTransfer` → `ioWrite(1,0x83)`) | Vektor 0x62 wird zugestellt (Trace bestätigt) |
| ctrl_pio_ Port B Floppy-Events (`setBSTB`) | offen | Event-ISR 0x0624 (Vektor 0x60) feuert nie; physikal. Signalquelle [offen] |
| Port 0xEE nicht dekodiert (vermutl. CTC-Alias 0x0E) | niedrige Priorität | 1 Schreibzugriff in 9M Zyklen, vermutl. Nebenwirkung |
| Koppelbus-Kette (/IEI1–/IEO1) nicht verdrahtet | für Boot irrelevant | niedrigprioritäre BS-PIO-Interrupts nicht gereiht |
| RETI-Erkennung im Z80CTC | im Z80PIO vorhanden (`onRETI`/IUS) | verschachtelte CTC-Interrupts ggf. unvollständig |

---

## 15. Offene Fragen (Hardware-Dokumentation)

### 15.1 K3526 OPS – Reine passive DRAM?

Hat die K3526 außer dem 64KB DRAM irgendwelche Steuerlogik (Banking-Register, Schreibschutz-Logik)? Oder ist sie wirklich nur ein passiver 64KB DRAM-Block?

### 15.2 PRG710 / K8915 Hardware

Für diese Maschinen fehlen noch alle Hardware-Informationen. Die Architektur unterstützt sie bereits als Konfiguration, aber die konkreten Karten und deren Verdrahtung müssen noch ermittelt werden.

---

## 16. Migrationspfad aus bestehendem Code

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
