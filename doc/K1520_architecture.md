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
│   │   │   ├── chargen_zg1.h    # Zeichengenerator obere Zeilen 0–7  (EPROM A103)
│   │   │   └── chargen_zg2.h    # Zeichengenerator untere Zeilen 8–11 (EPROM A123)
│   │   ├── k8025/               # ASS – Anschlußsteuerung Seriell
│   │   │   └── k8025.h/cpp
│   │   └── k5122/               # AFS – Folienspeicher-Anschlußsteuerung
│   │       └── k5122.h/cpp      #      formatagnostischer Lesekopf-Streaming-Controller (§8.5)
│   │
│   ├── peripherals/             # Externe Peripheriegeräte
│   │   ├── k7637/               # Serielle Tastatur
│   │   │   ├── k7637.h/cpp
│   │   │   └── keytable.h       # Scan-Code-Tabellen (aus .bin)
│   │   └── floppy_drive/        # formatagnostischer Floppy-Stack (§8.5)
│   │       ├── disk_format.h/cpp       # DiskFormat/TrackFormat (Geometrie + Verfahren je Spur)
│   │       ├── format_catalog.h/cpp    # lädt data/formats.yaml + Validierung (§8.6)
│   │       ├── track_image.h/cpp       # zentrale TrackImage-Abstraktion (Byte-+Markenstrom)
│   │       ├── track_codec.h/cpp       # IBM-Track (FM/MFM) bauen/parsen + CRC
│   │       ├── bit_codec.h/cpp         # Bitzellen ⇆ Bytes (MFM/FM, HFE)
│   │       ├── drive_profile.h/cpp     # Laufwerksprofile (4 Slots)
│   │       ├── disk_medium.h/cpp       # DAS interne Diskettenabbild (alle Spuren, §8.7)
│   │       ├── image_codec.h/cpp       # Container-Fabrik (.img/.hfe/.dmk) + Sniffing
│   │       ├── img_codec.h/cpp         # .img  ⇄ DiskMedium (braucht DiskFormat)
│   │       ├── hfe_codec.h/cpp         # .hfe  ⇄ DiskMedium (HxC v1, MFM/FM, Mischdichte)
│   │       ├── dmk_codec.h/cpp         # .dmk  ⇄ DiskMedium (David Keil)
│   │       ├── disk_image.h/cpp        # gemountete Diskette: Medium + Dateibindung + Autosave
│   │       └── floppy_drive2.h/cpp     # FloppyDriveV2 (Profil + Kopfposition + Diskette)
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
│   ├── util/                    # Querschnitts-Helfer
│   │   └── yaml_lite.h/cpp      # minimaler YAML-Subset-Parser für Config-Dateien (§8.6.2)
│   │
│   └── api/                     # C-API (öffentliche Schnittstelle)
│       ├── k1520_api.h          # Stabiles C-ABI
│       └── k1520_api.cpp
│
├── data/                        # ══ LAUFZEIT-KONFIGURATION ══
│   └── formats.yaml             # Diskettenformat-Katalog (§8.6)
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
├── tests/                       # ══ TESTS ══ (Ebene = Verzeichnis = ctest-Label)
│   ├── support/                 # k1520_testsupport: Screen/Tastatur/TempDisk
│   ├── unit/                    # eine Klasse isoliert (spiegelt core/)
│   │   ├── primitives/  bus/  cards/  peripherals/  util/
│   ├── debugtools/              # die header-only Bausteine aus tools/*.h
│   ├── integration/             # ganze Maschine, echter Kaltboot
│   ├── cli/                     # Werkzeuge als Prozess (Blackbox)
│   ├── system/                  # FORMAT/CPABCGEN/SCPX/HARDY (langsam)
│   ├── fixtures/disks/          # Testdisketten
│   └── python/                  # pytest: C-ABI + GUI
│       ├── conftest.py          # ctypes-Loader, Fixtures, Headless-Qt
│       ├── test_c_api.py
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
Zeichengenerator: 2 EPROMs A103 (Zeilen 0–7) + A123 (Zeilen 8–11), je 1024 Bytes
    │ Adressierung: [Bit9:3 = Zeichencode 0x00–0x7F] + [Bit2:0 = Zeilen-Nr]
    │ → 8 Pixel pro Zeile; ein lateinischer Satz (kein Kyrillisch)
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
    │ physischer K7637-Tastencode (NICHT ASCII), 9600 Baud, 1+8+1
    │ Latenz: ~1 Byte-Zeit (~2604 Takte) bis das Byte am SIO ankommt
    ▼
K8025 SIO A32 (Kanal A, Daten 0x5C / Status 0x5D)
    │ UART-Empfang (4-fach-FIFO)
    ▼
Z80: 25ms-Timer-ISR pollt die Tastatur (kbdpin=0, KEIN eigener Tastatur-IRQ),
    │ liest 0x5C, recodiert via BIOS-cp37-Tabelle, puffert in 0xF6D9
    ▼
CCP CONIN holt das Zeichen aus dem Puffer
```

> **Real-HW-treues Timing (Fix 2026-06-18, s. `doc/design/08_k7637_keyboard.md`):** Tastatur→Host-
> Bytes (Tastencodes UND die 0x80-Typcode-Quittungen) werden über `K7637::service(now_cycles)`
> (pro Instruktion aus der Run-Loop) erst nach einer 9600-Baud-Byte-Zeit freigegeben — sonst
> konkurrieren der ISR-Tastatur-Scan und der Vordergrund-LED-Handshake um dasselbe RX-Byte und
> fluten den Tastaturpuffer. `translateKey()` liefert physische K7637-Codes (ET1 0xFF ≠ Enter 0xC0,
> Cursor 0x94–0x97, F1–F8 0xC1–0xC8, …), die der OS-`cp37`-Treiber recodiert.

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

### 8.2 Disk-Format-Konfiguration (ABGELÖST → §8.6)

> **Historisch.** Das hier skizzierte `cpaFormates.cfg`-Format stammt aus dem CP/A-Umfeld und ist
> **nie produktiv benutzt worden**: `FormatParser::parseFile()` wird ausschließlich von
> `test_format_parser.cpp` aufgerufen, und seine reale Grammatik (`[name]`-Sektionen +
> `track cf cl hf hl sp bps`) weicht vom unten gezeigten `disk … end`-Stil ab. Produktiv gelten
> die einkompilierten `FormatParser::builtinFormats()`. Beides wird durch den YAML-Katalog
> **§8.6** ersetzt (Etappe E3).

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

**Laufwerksbestückung (Drive-Typen) — bei der Maschinen-Erzeugung:** Welche Laufwerks*typen*
in den 4 K5122-Slots stecken, wird beim Erzeugen der Maschine festgelegt (Laufzeit, nicht
compile-time):

```c
// Default-Bestückung (4× K5601, 5,25"-MFM, doppelseitig):
K1520Handle h  = k1520_create(K1520_MACHINE_A5120);

// Explizite Bestückung (NULL/"" = Default K5601; unbekannter Name → Default-Profil):
//   z. B. Slot 0 = 8"-FM-Laufwerk, Slots 1–3 = 5,25"-MFM:
K1520Handle h2 = k1520_create_configured(K1520_MACHINE_A5120,
                                         "MF3200", NULL, NULL, NULL);
```

Die Profile heißen wie die realen Laufwerke (`builtinDriveProfile`):

| Profil | Zoll | Spuren×Köpfe | Verfahren | Kapazität |
|--------|------|--------------|-----------|-----------|
| `K5601` (**Default**) | 5,25″ | 80×2 | MFM + FM | 800 KB |
| `K5600.10` | 5,25″ | 40×1 | MFM | 200 KB |
| `K5600.20` | 5,25″ | 80×1 | MFM + FM | 400 KB |
| `MF3200` | 8″ | 77×1 | **nur FM** | ~300 KB |
| `MF6400` | 8″ | 77×1 | FM + MFM | ~600 KB |
| `none` | — | — | — | leerer Slot |

Beide 8″-Laufwerke sind **einseitig mit 77 Spuren** und unterscheiden sich allein im
beherrschten Verfahren — das MF6400 kann zusätzlich MFM und damit die doppelte Kapazität;
es fährt deshalb auch die FM-Formate des MF3200. Das **K5602 ist zum MF3200 voll kompatibel**
und hat kein eigenes Profil. Frühere technische Namen (`ss_525_40`, `mf6400_8_ss77`, …) lösen
`builtinDriveProfile` weiterhin als **Alias** auf, damit gespeicherte Konfigurationen
(`app/config_io.py`) nicht still auf ein anderes Laufwerk zurückfallen.

Das Verfahren (FM/MFM) ist
Laufwerks-Default (`DriveProfile::default_read_encoding`), das OS schaltet zur Laufzeit per
Steuerwort um (0x85=MFM / 0x87=FM). C++-seitig direkt über `A5120Machine::Config` (Tools wie
`k1520dbg`/`boot_trace` können das fest setzen), später per GUI bzw. Config-Datei (CLI).
Der gewählte Profilname ist über `K5122::DebugState::driveProfileName` (Debugger `dev k5122`)
beobachtbar.

> **Laufwerkstyp = reine BIOS-Software-Eigenschaft, kein K5122-Limit.** Welche Formate die
> CP/A-Formatierprogramme anbieten und welche Geometrie das OS erwartet, entscheidet allein
> der BIOS-DPB (`dpbtyp`, aus dem Generierungswert `diskA/B/C/D`); die K5122 streamt
> formatagnostisch Bits. Deshalb lassen sich mit **Combo-Boot-Disketten** (B:/C: als
> Fremdtypen konfiguriert) alle Laufwerkstypen-Menüs — inkl. der 8″-FM/MFM-Formate — ohne
> echte Hardware im Emulator abgreifen. Katalog + Pipeline: `docs/format.md`.

### 8.5 Formatagnostischer Floppy-Stack (K5122 + DiskImage/TrackImage) — 2026-06-10

> **Teilweise abgelöst durch §8.7 (2026-08-05).** Controller-Modell, `TrackImage`, `TrackCodec`,
> `BitCodec` und der Boot-Lesepfad gelten unverändert. Die hier beschriebene **Backend-Schicht**
> (`DiskImage` abstrakt, `RawSectorImage`/`HfeImage` als dateigebundene Unterklassen, Spur-Cache
> in `FloppyDriveV2`) ist durch das **interne Medium + Container-Codecs** ersetzt.

Die `K5122` (`core/cards/k5122/`) ist der **formatagnostische** Floppy-Controller (einzige
Floppy-Karte) auf der Peripherie-Schicht `core/peripherals/floppy_drive/`.  Sie modelliert einen
**Lesekopf über der rotierenden Spur** und kennt keine Sektorgrößen/CRCs/Boot-Stadien mehr.  *(Sie
ersetzte eine ältere monolithische On-the-fly-Synthese-K5122 — die §8.1–§8.4 beschreiben deren
PIO-Protokoll, das auf Port-/Signalebene unverändert gilt; das dort skizzierte karten-interne
Synthese-Modell ist durch den TrackImage-Stack ersetzt.)*  Vollständiges Modell:
`doc/design/07_k5122_afs.md`.

```
K5122 (Controller-Karte)                  core/cards/k5122/
   │ PIO 10H–18H, /STR /ST MK MK1, BUSRQ-Arbitrierung, Index aus rpm
   │ streamt TrackImage byteweise über 16H; MK/MK1 → nextMark()
   ▼  fordert TrackImage(cyl,head)
FloppyDriveV2  — DriveProfile + 1-Spur-Cache je Kopf    floppy_drive2.*
   ▼  readTrack / writeTrack / geometry
DiskImage (abstrakt)                      disk_image.* (open()/Sniffing)
   ├── RawSectorImage  (.img + DiskFormat) ──► TrackCodec   raw_sector_image.*
   └── HfeImage        (.hfe, HFE v1)       ──► BitCodec     hfe_image.*
   ▼  liefert/nimmt
TrackImage — zentrale Abstraktion (decodierter Byte- + Markenstrom, Encoding-Tag)  track_image.*
TrackCodec — IBM-Track (FM/MFM) bauen/parsen + CRC-16        track_codec.*
BitCodec   — Bitzellen ⇆ Bytes (MFM/FM) für HFE             bit_codec.*
DriveProfile[4] — Zoll/Spuren/Köpfe/U-min/Verfahren je Slot  drive_profile.*
```

**Kernpunkte:**
- **`TrackImage`** ist der einzige Berührungspunkt zwischen Controller und Dateiformat:
  decodierte Spur-Bytes (Gaps/Sync/IDAM/DATA/echte CRCs) + paralleles `marks[]` (Adressmarken
  aus dem fehlenden Clock-Bit, nicht aus dem Bytewert) + `encoding` (FM/MFM).
- **Verfahrensneutral:** FM vs. MFM lebt allein in der Codec-Schicht (`TrackCodec`/`BitCodec`);
  Controller und `TrackImage` sind agnostisch.  4 Laufwerksprofile (5,25″ 80×2 MFM, 5,25″ 40×1,
  8″ 77×1 FM, 8″ 77×2 MFM) je Slot **zur Laufzeit konfigurierbar** (`A5120Machine::Config` /
  `k1520_create_configured`, Default 4× K5601, siehe §8.4); Index-Periode aus `rpm`.
- **HFE v1** (`HXCPICFE`, ISOIBM_MFM=0/FM=2) wird lesend+schreibend unterstützt; `DiskImage::open`
  erkennt die Signatur und ist self-describing (kein `DiskFormat` nötig).  Die Bitebene
  (`BitCodec`: 16 Zellen/Byte, HFE-LSB-first ↔ intern MSB-first via bytereverse, A1-Sync =
  Zellwort `0x4489`, MFM-Clock `c_i=¬(d_{i-1}∨d_i)`, FM-Sondertakt C7/D7) ist nach der
  Greaseweazle/HxC-Spec implementiert und per unabhängigem Python-Konverter (`tools/img_to_hfe.py`)
  cross-validiert.
- **CRC:** eine zentrale Primitive `TrackCodec::crc16` (Standard-IBM-CCITT, Poly 0x1021, Seed
  0xFFFF). Der Boot-Lesepfad benutzt die Daten-CRC über `[A1 A1 A1 FB]+Daten` — die echte
  Disk-CRC; beide Boot-Leser werden über den gemeinsamen 4×A1-Stream bedient (s. u.).
- **Verdrahtung & Boot:** `A5120Machine` verdrahtet die `K5122` als Slot-2-Floppy (`a5120.h`).  Die
  A5120 **bootet die echte Standard-IBM-MFM-Diskette vollständig in CP/A** (`CP/A, Version 25.09.89 …`)
  bis zum interaktiven Prompt, alle `test_boot_integration`-Stadien grün — inkl. Boot von den
  Laufwerken **B: und C:** (leere niedrigere Laufwerke werden vom ROM übersprungen).  Der Boot-Lesepfad
  ist **codierungstreu** (keine Fake-Umwandlung): `startReadTransfer()` streamt
  `buildFaithfulReadTrack` (4×A1-Sync — der gemeinsame Modus für Boot-ROM und SYL-Lader), Resync über
  `romReadResyncTarget`; das 4. A1 ist reines Sync und nicht in der CRC. Die MK/MK1-Re-Sync-Strobes
  (`resyncToNextMark`) springen IDAM→DATEN→nächstes IDAM. Vollständiges Modell:
  `doc/design/07_k5122_afs.md` §10; Boot-Kette (ROM → SYL → Sekundärlader → CP/A-Bootsystem →
  `@OS.COM`) im Detail: §14.5.
- **Schreiben & Formatieren:** Der `/WE`-flankengesteuerte Schreibpfad (`beginWriteField`/
  `commitWriteField`) und der Vollspur-FORMAT-Schreibpfad (`parseFormatStream`/`commitFormatTrack`/
  `writeTrackAt`) sind implementiert und codierungstreu (FM ohne A1-Sync, MFM mit); `FORMAT.COM`
  formatiert+verifiziert alle Sektorgrößen. `HfeImage::readTrack` erkennt das Verfahren **pro Spur**
  (Mischdichte-Disks: FM-Systemspuren + MFM-Datenspuren, z. B. 8″-MF6400). Details + Formatkatalog:
  `docs/format.md`.
- **Tests:** `test_track_codec`, `test_bit_codec`, `test_hfe_codec`, `test_img_codec`,
  `test_dmk_codec`, `test_disk_medium`, `test_disk_image`,
  `test_drive_profile`, `test_floppy_drive2`, `test_k5122` (GoogleTest);
  alle grün, ebenso `test_boot_integration` (Full-Machine) und `test_k2526` (ZVE2-Floppy-Kette).

### 8.6 Diskettenformat-Katalog aus YAML (`formats.yaml`) — 2026-08-03

> **Status: umgesetzt.** `FormatParser::builtinFormats()` und `parseFile()` sind entfallen;
> die Formate stehen in `data/formats.yaml`. Neue Dateien: `core/util/yaml_lite.{h,cpp}`
> (Parser), `core/peripherals/floppy_drive/format_catalog.{h,cpp}` (Laden/Validieren),
> `disk_format.{h,cpp}` (Datenmodell, ehem. `format_parser.*`). Guard-Tests:
> `test_yaml_lite` (16), `test_format_catalog` (19); Gesamtstand 644/644 ctest grün.

#### 8.6.0 Ausgangslage — warum der Umbau

Die Formatdefinitionen sind heute **fest einkompiliert**: `FormatParser::builtinFormats()`
(`format_parser.cpp:102`) baut ~25 `DiskFormat`-Structs im Code auf, `A5120Machine` übernimmt sie
im Konstruktor (`a5120.cpp:55`). Daneben existiert `FormatParser::parseFile()` — ein INI-artiger
Parser für ein aus dem CP/A-Umfeld stammendes Fremdformat, der **ausschließlich in Tests**
aufgerufen wird (`test_format_parser.cpp`) und dessen tatsächliche Grammatik (`[name]` +
`track cf cl hf hl sp bps`) nicht einmal dem in §8.2 dokumentierten `disk … end`-Stil entspricht.
Beide Altformate können das Entscheidende nicht ausdrücken:

| Fehlt heute | Konsequenz |
|-------------|------------|
| **Verfahren (FM/MFM) pro Spurbereich** | `TrackFormat` (`format_parser.h:7`) hat kein `encoding`-Feld. Das Verfahren gilt **pro Image**: `RawSectorImage::enc_` (`raw_sector_image.h:62`) und der `enc`-Parameter von `DiskImage::create` (`disk_image.h:109`). Mischdichte-Disketten (FM-Systemspur + MFM-Daten) können daher zwar **gelesen** werden (`HfeImage::readTrack` probiert beide Verfahren, `hfe_image.cpp:220-238`), aber weder angelegt noch sauber zurückgeschrieben werden. |
| **Laufwerks-Kompatibilität** | `A5120Machine::compatibleFormats()` (`a5120.cpp:608`) rät über eine reine Geometrie-Heuristik (`numCylinders ≤ prof.num_cyls && numHeads ≤ prof.num_heads`). Ein 8″-FM-Format erscheint damit im Dropdown eines 5,25″-Laufwerks. |
| **Standardformat je Laufwerkstyp** | `defaultFormatFor()` (`a5120.cpp:551`) ist eine hartkodierte `if`-Kette. |
| **Klartextbeschreibung** | Die GUI zeigt nur nackte Katalognamen (`k5601_ds40_17x256`). |

Ziel: **eine** menschenlesbare YAML-Datei als alleinige Quelle, die Mischdichte, Sektorgrößen-Mix
und Laufwerks-Kompatibilität ausdrückt; beide Altparser entfallen.

#### 8.6.1 Dateiformat — `data/formats.yaml`

```yaml
# Katalog der Diskettenformate.  Schema-Version für spätere Migrationen.
version: 1

formats:
  # ── Einfaches Format: alle Spuren gleich ─────────────────────────────────
  - name:        cpa800
    description: "CP/A 800K — 80 Spuren, doppelseitig, 5×1024 MFM"
    drives:      [K5601]
    default_for: [K5601]     # Standard beim Anlegen (leerer Formatname)
    encoding:    mfm                        # Vorgabe für alle Spurbereiche
    tracks:
      - { cyls: 0-79, heads: 0-1, sectors: 5, size: 1024 }

  # ── Asymmetrischer Systembereich (boot-kritisch, exakt wie der Builtin) ──
  - name:        cpa780
    description: "CP/A 780K Bootdiskette — 128B-Systembereich + 1024B-Daten"
    drives:      [K5601]
    encoding:    mfm
    tracks:
      - { cyls: 0,    heads: 0-1, sectors: 26, size: 128  }   # System
      - { cyls: 1,    heads: 0,   sectors: 26, size: 128  }   # Stage-2-Lader
      - { cyls: 1,    heads: 1,   sectors: 5,  size: 1024 }   # 1. Datenspur
      - { cyls: 2-79, heads: 0-1, sectors: 5,  size: 1024 }   # Daten + Dateisystem

  # ── MISCHDICHTE: FM-Systemspur + MFM-Datenspuren (neu ausdrückbar) ───────
  - name:        mf6400_sys
    description: "8″ MF6400 600K — FM-Systemspur, MFM-Daten"
    drives:      [MF6400]
    tracks:
      - { cyls: 0,    heads: 0, sectors: 26, size: 128,  encoding: fm  }
      - { cyls: 1-76, heads: 0, sectors: 8,  size: 1024, encoding: mfm }

  # ── Nur als .hfe darstellbar ─────────────────────────────────────────────
  - name:        k5601_ds40_5x1024
    description: "K5601 §3.4 Format V — 40 Spuren, doppelseitig, 5×1024"
    drives:      [K5600.10]
    encoding:    mfm
    containers:  [hfe]                      # .img nicht erzeugbar (Doppelschritt)
    tracks:
      - { cyls: 0-39, heads: 0-1, sectors: 5, size: 1024 }
```

**Format-Ebene:**

| Feld | Pflicht | Typ | Bedeutung |
|------|---------|-----|-----------|
| `name` | ja | string | Eindeutiger Katalogname (C-API, GUI, Tools, `--format`) |
| `description` | nein | string | Klartext fürs GUI-Dropdown |
| `drives` | ja | list\<string\> | Kompatible `DriveProfile`-Namen (§8.4). **Nur** diese Formate bietet ein Slot an. |
| `default_for` | nein | list\<string\> | Profile, für die dies das Standardformat ist — ersetzt `defaultFormatFor()` |
| `encoding` | nein | `fm`\|`mfm` | Vorgabe für Spurbereiche ohne eigenes `encoding` |
| `containers` | nein | list | `hfe`, `img` — darstellbare Dateitypen (Default: beide) |
| `tracks` | ja | list | ≥ 1 Spurbereich |

**Spurbereich (`tracks[]`):**

| Feld | Pflicht | Typ | Bedeutung |
|------|---------|-----|-----------|
| `cyls` | ja | `N` oder `N-M` | Zylinderbereich, **inklusive** |
| `heads` | ja | `N` oder `N-M` | Kopfbereich, inklusive |
| `sectors` | ja | int | Sektoren je Spur |
| `size` | ja | int | Bytes/Sektor (128/256/512/1024) |
| `encoding` | nein | `fm`\|`mfm` | **Pro Spurbereich** — überschreibt die Format-Vorgabe |
| `first_sector` | nein | int | Erste Sektor-ID (Default 1) |
| `interleave` | nein | int | Sektor-Verschränkung (Default 1) — *Phase 2* |
| `gaps` | nein | map | Überschreibt `GapParams` (§`track_codec.h`) — *Phase 2* |

**Auflösungsregeln:** Verfahren = `tracks[].encoding` → `formats[].encoding` → Laufwerks-Default
(`prof.supports_mfm ? MFM : FM`, heutiges Verhalten). Bereiche sind inklusive und müssen
**überlappungsfrei** sein (V2); `DiskFormat::findTrack()` behält seine „erster Treffer"-Semantik.

#### 8.6.2 YAML-Subset (handgeschriebener Parser)

Bewusst **kein** yaml-cpp: keine externe Abhängigkeit für `libk1520core.so`, offline baubar,
konsistent mit dem vorhandenen handgeschriebenen Parser. Der Parser (`core/util/yaml_lite.{h,cpp}`,
~300 Zeilen) unterstützt genau:

| Unterstützt | Nicht unterstützt (→ Ladefehler mit Zeilennummer) |
|-------------|--------------------------------------------------|
| Kommentare `#` bis Zeilenende | Anchors/Aliases `&a` / `*a`, Merge-Keys `<<:` |
| Einrückungs-Verschachtelung (**nur Leerzeichen**) | Tabs als Einrückung |
| Block-Maps `key: value`, Block-Listen `- item` | Mehrzeilige Skalare `|` / `>` |
| Flow-Maps `{ a: 1, b: 2 }` (einzeilig) | Mehrere Dokumente (`---`), Tags (`!!str`) |
| Flow-Listen `[a, b, c]` | Komplexe Keys (`? …`) |
| Skalare: bare, `'…'`, `"…"`; int dez/`0x`; bool `true`/`false` | Zeitstempel, Sets, Ordered-Maps |

Das Ergebnis ist ein generischer `YamlNode` (Map/List/Scalar); `FormatCatalog` bildet ihn auf
`DiskFormat` ab. Damit ist der Parser eigenständig testbar (`test_yaml_lite`) und für spätere
Konfigurationsdateien (z. B. `drives.yaml`, §8.6.8/E6) wiederverwendbar.

#### 8.6.3 Datenmodell (C++)

```cpp
struct TrackFormat {                     // disk_format.h (ehem. format_parser.h)
    uint8_t  cyl_first, cyl_last;
    uint8_t  head_first, head_last;
    uint8_t  secs_per_track;
    uint16_t bytes_per_sec;
    Encoding encoding        = Encoding::MFM;   // NEU — pro Spurbereich
    uint8_t  first_sector_id = 1;               // NEU
    // Phase 2: uint8_t interleave; std::optional<GapParams> gaps;
};

struct DiskFormat {
    std::string              name, description;
    std::vector<std::string> drives, default_for;   // NEU
    bool                     allow_img = true, allow_hfe = true;  // NEU (containers)
    std::vector<TrackFormat> tracks;

    uint8_t  numHeads() const;  uint8_t numCylinders() const;  uint64_t totalBytes() const;
    const TrackFormat* findTrack(uint8_t cyl, uint8_t head) const;
    Encoding predominantEncoding() const;   // NEU — HFE-Header, DiskGeometry
    bool     isMixedEncoding()     const;   // NEU
    bool     fitsDrive(const DriveProfile&) const;   // NEU — Validierung V4
};

class FormatCatalog {                    // NEU — ersetzt FormatParser
public:
    static std::vector<std::string> searchPaths();
    static FormatCatalog             load(const std::vector<std::string>& files,
                                          std::string* error);
    const DiskFormat*                find(const std::string& name) const;
    std::vector<const DiskFormat*>   forDrive(const DriveProfile&) const;  // explizite Liste
    const DiskFormat*                defaultFor(const DriveProfile&) const;
    const std::vector<std::string>&  warnings() const;   // nicht-fatale Befunde
    const std::vector<std::string>&  sources()  const;   // geladene Dateien (Diagnose)
};
```

#### 8.6.4 Suchpfad und Laden

Der Katalog ist eine **reine Laufzeitdatei** (kein Build-Codegen). Geladen wird in
**aufsteigender Priorität** — alle gefundenen Dateien werden gelesen, ein späterer `name`
**ersetzt** einen früheren gleichen Namens (User-Override):

1. `K1520_FORMATS_DEFAULT` — Compile-Define via `add_compile_definitions` (CMake setzt es im
   Dev-Build auf `${CMAKE_SOURCE_DIR}/data/formats.yaml`, im Install-Build auf den Install-Pfad)
2. `<Verzeichnis von libk1520core.so>/../share/a5120emu/formats.yaml`
3. `./data/formats.yaml` (CWD)
4. `${XDG_CONFIG_HOME:-~/.config}/a5120emu/formats.yaml`
5. `$K1520_FORMATS` (Datei **oder** Verzeichnis; mehrere `:`-getrennt) — höchste Priorität

Wird **keine** Datei gefunden, schlägt die `A5120Machine`-Konstruktion **laut** fehl:
`last_error_` listet **alle durchsuchten Pfade** auf, `k1520dbg`/`boot_trace` drucken sie.
Das ist die bewusste Kehrseite der Laufzeitdatei (siehe R2) — Schritt 1 sorgt dafür, dass
ctest, `boot_trace` und `k1520dbg` ohne jede Umgebungsvariable funktionieren.

#### 8.6.5 Validierung beim Laden

| # | Regel | Verstoß |
|---|-------|---------|
| V1 | Pflichtfelder vorhanden, `name` katalogweit eindeutig | **Fehler** |
| V1b | Unbekannte Felder | **Warnung** (vorwärtskompatibel) |
| V2 | Spurbereiche eines Formats überlappungsfrei; `cyl_first ≤ cyl_last`, `head ≤ 1` | **Fehler** |
| V3 | `drives`/`default_for` verweisen auf existierende Profilnamen | **Fehler** |
| V4 | Für **jedes** `drives`-Profil: `numHeads ≤ prof.num_heads`, `numCylinders ≤ prof.num_cyls`, und jedes verwendete Verfahren von `prof.supports(enc)` gedeckt | **Fehler** |
| V5 | Spurkapazität: `sectors × size` + Gaps ≤ `indexPeriodCycles / bytePeriodCycles` | **Warnung** (s. u.) |

> **V3 — Fallstrick:** `builtinDriveProfile()` (`drive_profile.h:155`) liefert für **unbekannte
> Namen stillschweigend das Default-Profil**. Die Validierung darf daher *nicht* über den
> Rückgabewert prüfen, sonst geht jeder Tippfehler als `K5601` durch. Nötig ist ein neuer
> Accessor `knownDriveProfileNames()`, gegen den geprüft wird.

> **V5 — nur Warnung, und warum:** `DriveProfile::bytePeriodCycles()` (`drive_profile.h:132`)
> hat die Datenrate fest auf FM = 125 kbit/s / MFM = 250 kbit/s verdrahtet — die **5,25″-Werte
> aus dem K5601-Datenblatt**. Reale 8″-FM-Laufwerke arbeiten mit 250 kbit/s. Rechnet man V5
> mit dem heutigen Modell, ergibt sich für das existierende `mf3200`-Format (8″, 360 min⁻¹, FM,
> 4×1024) eine Kapazität von nur `408333 / 156 ≈ 2617` Bytes gegenüber 4096 Bytes Nutzdaten —
> das **bestehende, funktionierende Format würde abgelehnt**. V5 bleibt deshalb Warnung, bis
> `DriveProfile` die Datenrate als eigenes Feld führt (Folgearbeit, nicht Teil dieses Umbaus).

#### 8.6.6 Schichten-Umbau — Verfahren pro Spur durchziehen

Das ist der eigentliche Eingriff; die YAML ist nur die Eingabe dafür. Betroffen:

| Ort | Heute | Nachher |
|-----|-------|---------|
| `raw_sector_image.cpp:121` | `buildTrack(sektoren, enc_)` — Image-weites Verfahren | `buildTrack(sektoren, tf->encoding, gaps)` aus `fmt_.findTrack(cyl, head)` |
| `raw_sector_image.h:62` | `enc_` bestimmt alles | `enc_` nur noch **Fallback** für Formate ohne `encoding` |
| `raw_sector_image.cpp:83` | `geometry().encoding = enc_` | `= fmt_.predominantEncoding()` |
| `disk_image.cpp:199` | `buildTrack(emptySectors(*tf,c,h), enc)` | `…, tf->encoding` — Mischdichte anlegbar |
| `disk_image.cpp:207` | `side_len` aus max. Spurlänge | unverändert **max über alle Spuren**, aber Mischdichte-Test nötig (R3) |
| `disk_image.cpp:122` | HFE-Header `enc` | `fmt.predominantEncoding()`; die andere Dichte deckt der vorhandene Dual-Decode ab (`hfe_image.cpp:220-238`) |
| `disk_image.h` | `open(…, raw_encoding)` (2026-08-03 ergänzt) | Parameter **entfernt** — das Format ist jetzt die Autorität (s. u.) |
| `k5122.cpp` | Verfahren aus `profile().supports_mfm` abgeleitet | entfällt — `DiskImage::open(path, fmt, wp)` |
| `a5120.cpp` | `disk_formats_ = builtinFormats()` | `FormatCatalog::loadDefault(&fatal)`, wirft bei fatalem Fehler |
| `a5120.cpp` | `defaultFormatFor()` (if-Kette) | `disk_formats_.defaultFor(prof)` — aus `default_for:` |
| `a5120.cpp` | `compatibleFormats()` (Geometrie-Heuristik) | `disk_formats_.forDrive(prof)` — **explizite** `drives:`-Liste |
| `format_parser.{h,cpp}` | `builtinFormats()` + `parseFile()` | **entfallen** beide; Datei → `disk_format.{h,cpp}` + `format_catalog.{h,cpp}` |

`writeTrack` braucht keine Änderung: `TrackCodec::parseTrack` liest das Verfahren bereits aus
`TrackImage::encoding` — der Controller liefert die Spur in der Codierung, in der er sie gelesen
bzw. geschrieben hat.

> **Abweichung 1 vom Entwurf — `raw_encoding` ganz entfernt statt „Fallback".** Der Plan wollte
> den 2026-08-03 ergänzten Parameter als Rückfall behalten. Das ist nicht darstellbar:
> `TrackFormat::encoding` hat immer einen Wert, „nicht deklariert" ist von „MFM" nicht
> unterscheidbar — der Parameter wäre wirkungslose Ballast-API geworden. Das Verfahren kommt
> jetzt ausschließlich aus dem Format (pro Spurbereich). Für Formate ganz ohne Spurbereiche
> bleibt intern MFM.
>
> **Abweichung 2 — `drives:` wird beim MOUNTEN nicht erzwungen**, nur bei `createDisk()` und in
> der angebotenen Auswahl (`compatibleFormats()`). Grund: bei self-describing `.hfe` ist der
> übergebene Formatname nur ein Platzhalter (die Geometrie steht in der Datei) — `tools/format_driver`
> mountet z. B. **alle** Slots nominell als `"cpa780"`, auch die 8″-Laufwerke der Combo-Boot-Tests.
> Zudem ist der Laufwerkstyp auf der A5120 reine BIOS-Software (§8.4), Combo-Disketten betreiben an
> B:/C: bewusst Fremdtypen. Eine Prüfung beim Mounten hat genau diese vier `format_integration`-Tests
> zerlegt. Das Ziel „kein doppelseitiges Format an einseitigem Laufwerk **auswählbar**" wird dort
> erzwungen, wo das Format die Struktur wirklich bestimmt: beim Anlegen und in der GUI-Liste.

#### 8.6.7 C-API und GUI

Die C-ABI bleibt **stabil** — `DiskFormat` wandert nie über die Grenze, nur Namen/Strings.
Bestehend bleiben `k1520_drive_format_count/_name/_default_format` (ihre *Semantik* wird
präziser: explizite Kompatibilität statt Geometrie-Heuristik). Neu:

```c
const char* k1520_format_description(K1520Handle h, const char* name);  /* GUI-Dropdown */
const char* k1520_formats_source(K1520Handle h);   /* geladene Datei(en) — Diagnose */
const char* k1520_last_init_error(void);           /* Grund eines fehlgeschlagenen create */
```

**Startabbruch ohne Handle.** Fehlt der Katalog oder ist er syntaktisch kaputt, wirft der
`A5120Machine`-Konstruktor; `k1520_create*` fängt das, gibt `NULL` zurück und legt den Grund in
`k1520_last_init_error()` ab — `k1520_last_error(h)` ist mangels Handle nicht erreichbar. Die
Python-Bindung (`K1520Emulator.__init__`) macht daraus ein `RuntimeError`, `app/main.py` gibt es
aus und beendet mit Exit-Code 1. Beispielausgabe:

```
Keine Formatkatalog-Datei (formats.yaml) gefunden.
Gesucht wurde in:
  - /home/…/a5120emu_ui/data/formats.yaml
  - /usr/bin/../share/a5120emu/formats.yaml
  - data/formats.yaml
  - /home/…/.config/a5120emu/formats.yaml
Abhilfe: data/formats.yaml bereitstellen oder K1520_FORMATS=<datei> setzen.
```

**Einzelne fehlerhafte Definitionen** sind dagegen nicht fatal — sie werden übersprungen und mit
Datei, Zeile, Name und Grund auf stderr (und ins Log) gemeldet, die übrigen Formate bleiben nutzbar:

```
[Formatkatalog] …/formats.yaml:8: Format 'kaputt' übersprungen — 'size': 777 — erlaubt sind 128, 256, 512, 1024
```

GUI (`app/ui/drive_widget.py`): `_populate_format_combo()` zeigt `"<name> — <description>"` als
Label (Standard mit Präfix `Standard: `) und behält den Katalognamen in `userData`.

#### 8.6.8 Etappen

| # | Inhalt | Status |
|---|--------|--------|
| **E1** | `yaml_lite` + Tests, ohne jede Verdrahtung | ✅ `test_yaml_lite` (16 Tests) |
| **E2** | `FormatCatalog` + `data/formats.yaml` als **1:1-Abbild** der 25 Builtins (`encoding: mfm` durchgängig; nur `mf3200` ist FM — wie die bisherige Ableitung aus dem FM-Laufwerk) | ✅ `BootKritischeGeometrien_Unveraendert` prüft cpa780 Spurbereich für Spurbereich |
| **E3** | `A5120Machine` auf Katalog umstellen; `builtinFormats()`/`parseFile()` **entfernt**; §8.2 als abgelöst markiert | ✅ 644/644 ctest, `test_boot_integration` grün |
| **E4** | Verfahren pro Spur durch `RawSectorImage`/`DiskImage::create` gezogen | ✅ `RawMischdichte_VerfahrenJeSpur`, `Mischdichte_VerfahrenProSpurbereich` |
| **E5** | C-API `_description`/`_source`/`_last_init_error`, GUI-Labels, Startabbruch | ✅ end-to-end geprüft |
| **E6** | *offen, optional:* `DriveProfile` ebenfalls aus YAML (`drives.yaml`) — derselbe Parser | — |

#### 8.6.9 Risiken

- **R1 — Boot-Regression durch Encoding-Deklaration (größtes Risiko).** Die A5120-Bootdisketten
  sind **reines Standard-IBM-MFM**; der Boot-ROM startet in FM, findet keine IDAM und schaltet
  per MK auf MFM um (§14.5, CLAUDE.md „Boot-Invarianten"). Würde `cpa780` seine 128-B-Systemspuren
  als `encoding: fm` deklarieren — was intuitiv plausibel wirkt —, bräche der verifizierte
  Bootpfad. **Deshalb ist E2 strikt verfahrens-neutral** (`encoding: mfm` überall, exakt wie heute);
  Mischdichte kommt erst in E4 und nur an **neuen** Formaten. Guard: `test_boot_integration`
  (`Stage3_FullyLoadsAndJumpsToOs`).
- **R2 — Laufzeit-Dateiabhängigkeit** (bewusste Entscheidung): Fehlt `formats.yaml`, kennt die
  Maschine **kein einziges** Format — auch die boot-kritischen `cpa780`/`cpa800` nicht. Mitigation:
  Compile-Define als letzter Rückfall (§8.6.4 Schritt 1) + laute, alle Pfade nennende Fehlermeldung.
- **R3 — HFE-Mischdichte.** HFE v1 trägt nur **eine** `bitrate` und **ein** Verfahren im Header.
  Lesen ist gelöst (Dual-Decode). Beim **Anlegen** ist zu beachten: `BitCodec` kodiert FM *und* MFM
  mit 16 Zellen/Byte (`bit_codec.h:15`), die reale Zeitkapazität je Umdrehung unterscheidet sich
  aber. `side_len` muss nach der **dichtesten** Spur bemessen werden; per Test absichern.
- **R4 — Doppelschritt-Formate** (40 Spuren in 80-Spur-Laufwerk, physisch = 2 × logisch) bleiben
  ungelöst; das Feld `containers: [hfe]` kann die Einschränkung jetzt **explizit** ausdrücken,
  statt sie wie früher nur als Kommentar im Quelltext zu führen. Im ausgelieferten Katalog ist es
  bewusst noch nirgends gesetzt (alle Formate erlauben `img` und `hfe`), damit der Umbau
  verhaltensneutral bleibt.

---

### 8.7 Internes Diskettenabbild + Container-Codecs (`DiskMedium`) — 2026-08-05

> **Ablösung der Backend-Schicht aus §8.5.** Dort hatte jedes Dateiformat eine eigene
> `DiskImage`-Unterklasse, die **auf der Datei arbeitete** (`RawSectorImage`: seek/write je
> Sektor; `HfeImage`: In-place-Blöcke). Ab hier gibt es **ein internes, bitstrom-orientiertes
> Diskettenabbild**; Dateiformate sind reine **Container-Codecs** davor. Der `TrackImage`-Stack
> (§8.5), der Boot-Lesepfad und das PIO-Protokoll bleiben unverändert.
> Vollständiger Feinentwurf: **`doc/design/09_floppy_drive.md`**.

**Warum der Umbau.** Die dateigebundenen Backends erzwangen drei Einschränkungen, die
sich nicht lokal reparieren ließen:

1. **`.img` verliert das Dateisystem fremder OS.** Ein rohes Sektorimage speichert nur
   Datenfeld-Nutzbytes. **UDOS** hängt aber je Sektor einen Sektorkontrollblock
   (Verkettungszeiger + eigene CRC) **hinter die Daten-CRC** — genau diese Bytes fielen
   beim Rückschreiben weg (`doc/udos_diskettenformat.md`).
2. **Kein Formatwechsel.** Das Backend war an den Dateityp gebunden; eine gemountete
   Diskette ließ sich nicht als anderer Typ herausschreiben.
3. **Keine echte Leerdiskette.** `DiskImage::create` musste `.hfe` **vorformatiert**
   anlegen, weil eine gap-leere Datei den Controller hängen ließ — auf einer
   vorformatierten Diskette kann UDOS seinen Zeiger-Anhang aber nicht unterbringen.

**Neues Modell.**

```
Datei (.img | .hfe | .dmk) ──load──► DiskMedium (alle Spuren als TrackImage) ──► K5122
                           ◄─save──        ▲ Schreibzugriffe des Gastsystems
```

- **`DiskMedium`** (`disk_medium.*`) hält **jede** Spur als `TrackImage`
  (Vollumdrehungs-Byte-/Markenstrom inkl. Gaps, Sync, echter CRCs, FM **und** MFM
  gemischt) plus Dirty-Bit je Spur. Das ist der verlustfreie, bitzellen-rückcodierbare
  „Bitstrom“ des Auftrags — Bytes statt Zellen, weil der Controller ohnehin byteweise
  arbeitet und die Markeninformation (fehlendes Clock-Bit) sonst verloren ginge.
- **`ImageCodec`** (`image_codec.*` + `img_codec/hfe_codec/dmk_codec.*`) lädt/speichert
  Container **vollständig**; kein Codec hält Dateizustand. `.img` braucht ein
  `DiskFormat`, `.hfe`/`.dmk` sind self-describing.
- **`DiskImage`** ist jetzt **konkret**: Medium + Dateibindung + Schreibschutz. Es
  schreibt schmutzige Spuren **verzögert** (≈ 0,5 s Maschinenzeit, `autoFlush()` aus
  `A5120Machine::run()`) in die gebundene Datei zurück, sodass die Datei dem Abbild
  stets mit leichtem Zeitversatz entspricht. `saveAs()` schreibt in einen beliebigen
  Container **und bindet um**.
- **`FloppyDriveV2`** hat **keinen Spur-Cache** mehr — es referenziert das Medium direkt.
- **`.dmk`** (David Keil) ist neu: 16-B-Header, je Spur 128-B-IDAM-Tabelle
  (u16-Offsets, Bit15 = MFM) + roher Spur-Byte-Strom (FM-Bytes verdoppelt). Damit
  verlustfrei wie HFE, aber ohne Bitzellen-Ebene.
- **`rawCompatible()`** ist das geforderte Flag: eine Spur ist `.img`-tauglich nur mit
  ≥ 1 Sektor, gültigen CRCs und **reinen Gap-Bytes hinter der Daten-CRC**. Sobald das
  Gastsystem etwas anderes schreibt (UDOS-Anhang) oder eine Spur unformatiert bleibt,
  wird `.img` als Ziel abgelehnt (GUI blendet es aus).
- **Leerdiskette:** `DiskImage::createBlank()` erzeugt ein Medium in **Laufwerks**-Geometrie
  (K5601 80×2, K5600.10 40×1, …) mit **unformatierten** Spuren. Der Controller streamt
  für sie markenlosen Gap-Flux (§7 des Feinentwurfs), das Gastsystem läuft in den
  Index-Timeout — genau wie auf echter Hardware — und kann die Diskette formatieren.
  Die Ablehnung markenloser Images beim Mounten (`hasFormattedData()`) entfällt damit.

**Nicht betroffen:** `TrackImage`/`TrackCodec`/`BitCodec`, der treue FM/MFM-Lesepfad
(§8.5, §14.5), die Boot-Invarianten und das K5122-Portprotokoll.

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

Das Testsystem ist nach Testebenen gegliedert (`unit`, `debugtools`,
`integration`, `cli`, `system`, `python`), jede Ebene ein Verzeichnis unter
`tests/` und zugleich ein ctest-Label.  Begründung der Gliederung, was in welche
Ebene gehört und die bewussten Auslassungen: **`doc/design/12_testing.md`**.
Ausführen, einen Test hinzufügen, gemeinsame Infrastruktur: **`tests/README.md`**.
Testdisketten: **`tests/fixtures/README.md`**.

Kurz:

```sh
tools/dev.sh test          # Regression ohne die langsamen (~12 s)
tools/dev.sh test-format   # nur die langsamen System-Tests (~51 s)
tools/dev.sh test-level unit
```

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

### 14.5 K5122 Diskettenleseverfahren (Adressmarken-Streaming)

Der Bootvorgang nutzt **drei** verschiedene Sektor-Leseroutinen (Z80-seitig, RE-Befund) —
der Emulator bedient alle drei über **einen** codierungstreuen Streaming-Lesekopf:

- **Boot-ROM** (`0x01DD`, läuft auf ZVE2): liest IDAM-Header und Daten mit *einem*
  `/STR`-Strobe am Stück, mit festen Offsets, ohne Markensuche.
- **Sekundär-Bootloader** (`0x062E`, eingehängt via `[0x0000]=JP 0x062E`): sucht aktiv
  die Adressmarken (`0xFE` IDAM, `0xFB` Datenmarke), verträgt variable Gaps, und
  verifiziert jeden Sektor per CRC-16. Nutzt **`MK` = Steuer-Port-A Bit 1** (`0xB5`→`0x87`).
- **Dritte Stufe / CP/A-Loader** (`0x1F7D`, 1024-B-Datenbereich): liest IDAM und Daten
  **kontinuierlich** (`INIR`, kein Per-Byte-Strobe) und re-synchronisiert über **`MK1` =
  Steuer-Port-A Bit 4** (`0xB5`↔`0x85`) statt `MK` (§14.5b).

Alle Routinen synchronisieren über die **`MK`/`MK1`-Steuersignale** (Steuer-Port-A Bit 1
bzw. Bit 4): jede Re-Sync-Flanke lässt den K5122-Datenseparator auf die **nächste
Adressmarke** synchronisieren — `IDAM → DATEN → nächstes IDAM → …`. Belegstellen:
ROM `0x0224/0x022D` und `0x0249/0x025F`; Loader `0x066E/0x0670` und `0x06B9/0x06BB`.

**Emulator-Modell (aktuell — TrackImage-Streaming, `doc/design/07_k5122_afs.md` §10).** Die
oben unterschiedenen Feld-/Stream-Modelle sind zu **einem** codierungstreuen Lesekopf-Streaming
vereinheitlicht: die aktive Spur ist eine fertige `TrackImage` (reale Gaps/Sync/Marken/CRC),
`ioRead(0x16)` streamt sie byteweise rotierend, und **jede MK/MK1-Re-Sync-Flanke**
(`resyncToNextMark`) springt zur nächsten Adressmarke (IDAM→DATEN→nächstes IDAM). Damit
bedient **ein** Stream ROM, Sekundärlader **und** dritte Stufe; die früheren
`buildField`/`advanceField`/`stream_continuous_`-Sonderfälle sind entfallen (eine
Standard-IBM-CCITT-Daten-CRC, §8.5). Für den Boot-Read materialisiert
`startReadTransfer()` den 4×A1-`buildFaithfulReadTrack`, der Boot-ROM (1 Wegwerf + 3 Reads,
FE@buf[4]) **und** SYL-Lader (skip-A1-bis-FE) zugleich akzeptiert.

**Track-Ende / BUSRQ-Arbitrierung** (weiterhin nötig): Nach einer voll gelesenen 128-B-Spur
fällt die ZVE2-Routine in ihre Idle-Schleife `L0696` und disabled dort den ctrl-PIO-Port-B-
Interrupt (`OUT(13H),03H`). Auf echter HW setzt `/STR=1` dann `/BUSRQ` zurück; im durchgängig
gestepperten Emulator gibt `K5122::ioWrite` bei `OUT(13H),03H` während eines Lese-Transfers
`/BUSRQ` frei → ZVE1 übernimmt, verarbeitet die Spur und setzt ZVE2 per `OUT(04)` zurück.

So trägt der Stream die ganze Boot-Kette: ZVE2 liest ganze Boot-Spuren (cyl 0/1, 128 B), ZVE1
CRC-verifiziert jeden Sektor, und nach 52 Sektoren springt der Loader nach `0x0880` (`JP 0x1800`)
in die dritte Stufe — das **CP/A-Bootsystem**, das `@OS.COM` aus dem 1024-B-Datenbereich lädt.

### 14.5b Dritte Stufe: `@OS.COM`-Ladephase (1024-B-Datenbereich) — GELÖST

Die dritte Stufe (CP/A-Bootsystem, Einsprung `0x1800`, FCB `@OS   COM` @`0x08CD`) gibt ihren
Banner aus (`CP/A-Bootsystem, Version 05.04.88 laedt @OS.COM …`) und liest `@OS.COM` aus dem
1024-B-Datenbereich. Ihre ZVE2-Routine `0x1F7D` (IDAM-Verify) + `0x2038` (Daten-Read) liest IDAM
**und** Daten in **einem kontinuierlichen Strom** (`INIR`) und re-synchronisiert über **`MK1`
(Steuer-Port-A Bit 4**, `0xB5`↔`0x85`) statt `MK` (Bit 1). Fehleranzeige `sub_1BF0` baut
`"RC;T,Si,Se=TTSSSS"` (`'C'`=CRC, `'S'`=Suche, `'U'`=Timeout).

Drei Ursachen mussten dafür gelöst werden — alle im aktuellen Stand behoben:

1. **ZVE2-Start aus dem Reset** (`K2526::zve2StartFromReset`). Die Stufe setzt `[0x0000]=JP 0x1F7D`,
   resettet ZVE2 (`OUT(04)=0x00`) und stellt `[0x0000]` sofort wieder her — sie startet ZVE2 nie
   explizit (bit0=1). `A5120Machine::run()` startet ZVE2 aus dem Reset, sobald `/BUSRQ` (das `/STR`)
   assertiert und ZVE2 im Reset steht, sodass es das *aktuelle* `[0x0000]=JP 0x1F7D` fetcht.
2. **Asymmetrische Mixed-Geometrie** (`format_parser.cpp`, §14.5c).
3. **MK1-Re-Sync** (`K5122::resyncToNextMark` auf der MK1-Fallflanke). Der letzte Stall (Timeout `'U'`
   bei cyl 2/3 head 1) war ein **K5122-Feldmodell-Bug**: ZVE2s MK1-Datenseparator-Resync wurde
   ignoriert, sodass ein Daten-`0xA1` als A1-Adressmarken-Sync missverstanden wurde und die
   IDAM-Suche entgleiste. Der Fix springt bei MK1 IDAM→DATEN→nächstes IDAM (überspringt die
   Daten). **Kein** ZVE1↔ZVE2-Handshake-Bug (das war eine falsche Fährte). Guards: Boot-Test
   `Stage3_FullyLoadsAndJumpsToOs` + `K5122 Continuous1024_MK1ResyncJumpsToNextAddressMark`.

**Ergebnis:** `@OS.COM` lädt vollständig, die dritte Stufe springt ins OS, und der A5120 bootet
den vollen CP/A-Kaltstart bis zum interaktiven Prompt (`CP/A, Version 25.09.89 …`).

### 14.5c Disk-Geometrie der cpa780-Diskette (asymmetrische Mixed-Geometry)

`0x1F7D` erwartet den 1024-B-Datenbereich hardkodiert ab **cyl 2** (IDAM cyl=2, size_code=3).
Die Seiten sind interleaved (cyl0/A, cyl0/B, cyl1/A, cyl1/B, …); der Systembereich sind **drei**
physische 128-B-Seiten (cyl 0 beide Seiten + cyl 1 Seite A), der **1024-B-Datenbereich beginnt
bei cyl 1 Seite B**:

| phys. Spur | Datei-Offset | Inhalt | Sektorgröße |
|---|---|---|---|
| cyl 0 A | `0x0000` | Stage-1-Boot + SYL | 128 B |
| cyl 0 B | `0x0D00` | Füller (`0x53`) | 128 B |
| cyl 1 A | `0x1A00` | Stage-2-Loader + SYL | 128 B |
| cyl 1 B | `0x2700` | erste Datenspur (Füller) | **1024 B** |
| cyl 2 A | `0x3B00` | CP/M-**Verzeichnis** (`"@OS     COM"` + CPABCGEN/FORMAT/…) | 1024 B |
| cyl 2 B | `0x5000` | `@OS.COM`-Daten … | 1024 B |

`3 × 3328 + 5120 = 0x3B00` → das Verzeichnis liegt exakt auf der cyl-2-Seite-A-Grenze,
sektor-aligned. Format (`format_parser.cpp`, asymmetrisch): `{0,0,0,1,26,128}` +
`{1,1,0,0,26,128}` + `{1,1,1,1,5,1024}` + `{2,79,0,1,5,1024}`; `findTrack`/`sectorOffset` werten
den Head-Bereich aus (mid-Zylinder-Asymmetrie cyl 1 A = 128 B, cyl 1 B = 1024 B). Würde man cyl 1 B
als 128 B modellieren, verschöbe sich der Datenbereich `0x700` nach vorn und die `@OS.COM`-Alloc-
Blöcke wären fehlausgerichtet.

### 14.7a Beobachtung: ctrl_pio_ Port B (Vektor 0x60) [evtl. obsolet]

Der Loader bewaffnet zusätzlich ctrl_pio_ **Port B** (Vektor 0x60, Event-ISR `0x0624`)
via `OUT(13H),97H/AFH`. Dieser Interrupt wird im Emulator nie ausgelöst (`setBSTB` nie
aufgerufen) — der volle Boot gelingt trotzdem (§14.5b), er ist also offenbar nicht nötig.

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

### 14.9 Offene Implementierungslücken

Der volle CP/A-Kaltstart (Boot-Kette bis interaktiver Prompt) funktioniert; die Restpunkte sind
nicht boot-kritisch:

| Lücke | Auswirkung |
|---|---|
| ctrl_pio_ Port B Floppy-Events (`setBSTB`) | Event-ISR 0x0624 (Vektor 0x60) feuert nie; für den Boot nicht nötig, evtl. obsolet (§14.7a) |
| Port 0xEE nicht dekodiert (vermutl. CTC-Alias 0x0E) | niedrige Priorität; 1 Schreibzugriff in 9M Zyklen, vermutl. Nebenwirkung |
| Koppelbus-Kette (/IEI1–/IEO1) nicht verdrahtet | für Boot irrelevant; niedrigprioritäre BS-PIO-Interrupts nicht gereiht |
| RETI-Erkennung im Z80CTC | im Z80PIO vorhanden (`onRETI`/IUS); verschachtelte CTC-Interrupts ggf. unvollständig |
| Post-Boot VRAM-Wipe nach ~50–65M Idle-Takten | kosmetisch; vermutl. Uhr-/Timing-Drift + streunende ZVE2-Floppy-Aktivität (`doc/open_points.md`) |

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
