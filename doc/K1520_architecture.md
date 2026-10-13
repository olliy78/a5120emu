# K1520emu – Architektur

**Autor:** Olaf Krieger  
**Stand:** Oktober 2026 (erste Fassung Mai 2026)  
**Basis:** TGL 37271/01, technische Dokumentation der emulierten Rechner und Karten

> **Wozu dieses Dokument.** Es ist die **Übersicht**: wie Kern, Bibliotheken, Oberfläche
> und Werkzeuge zusammenhängen, welche Regeln für alle Maschinen gelten und wo die
> Einzelheiten stehen. Die Feinentwürfe je Baugruppe und Maschine liegen unter
> [`doc/design/`](design/), die Festlegungen mit ihren Wächtern unter
> [`doc/merkposten/`](merkposten/). Wo sich beide widersprechen, gilt der jüngere Entwurf —
> und dieses Dokument ist zu berichtigen.
>
> Die Abschnittsnummern §5.1a/b, §5.3a, §8.5–§8.8 und §14 werden aus Quelltext und anderen
> Dokumenten zitiert und bleiben deshalb stabil.

---

## 1. Projektüberblick

### 1.1 Ziel

Ein hardwarenaher, modularer Emulator für Rechner aus dem **K1520-Bausteinsystem**
(DDR, 1980er Jahre) — und für die **EAW P8000**, die kein K1520-Rechner ist, aber genug
Bausteine mit ihnen teilt, um im selben Kern zu leben.

| Rechner | Wesentliche Karten / Bausteine | Betriebssysteme | Entwurf |
|---|---|---|---|
| **robotron A5120** | K2526 (ZRE, zwei Z80), K3526, K7024, K8025, K5122, Tastatur K7637 | CP/A, SCPX, UDOS | dieses Dokument, [design/03](design/03_k2526_zre.md)–[09](design/09_floppy_drive.md) |
| **A5120.16** | A5120 + Erweiterungsmodul EM064/EM256 mit U8001 | wie A5120 | [design/17](design/17_a5120_16.md) |
| **robotron K8915** V3 / V2 | ZRE 045-8762 bzw. K2521 + K3528, K7028, K7024, K5122, Tastatur K7672 | SCPX 8915 | [design/16](design/16_k8915.md), [design/24](design/24_k8915_varianten.md) |
| **PRG 710 / PRG 710-1** | K2521, Speicherverwaltung, K7024, K5122, ATP 590068 (EPROMmer), ASS 590069 | UDOS 4.3, SCPX | [design/20](design/20_prg710.md) |
| **robotron PC 1715 / 1715W** | eigene ZRE mit 8275-Bild, Tastatur mit eigenem U880, K5122-Bauart bzw. U8272 + Z80-DMA | SCP 1715, CP/A, CP/Z, UDOS 1715, SCP 3.0 | [design/21](design/21_pc1715.md) |
| **EAW P8000** | 8-Bit-Karte (U880), 16-Bit-Karte (U8001 + 3 × U8010), WDC, Originalterminal (Z8) | UDOS, WEGA | [design/25](design/25_p8000.md), [26](design/26_p8000emu_oberflaeche.md), [28](design/28_p8000_originalterminal.md) |

Dazu Optionen, die in jede K1520-Maschine gesteckt werden können: RAM-Floppy RAF
([design/22](design/22_raf512.md)) und Lochstreifen K6022 ([design/23](design/23_lochstreifen.md)).

### 1.2 Programme und Bibliotheken

```
  Emulator-Oberfläche (app/)        k1520DiskTool                Werkzeuge (tools/)
  a5120emu  k8915emu  prg710emu     Oberfläche app/disktool/     k1520dbg  boot_trace
  pc1715emu  p8000emu               CLI k1520disktool            floppy_diag  format_driver
          │ ctypes                          │ ctypes / C++                │ C++, statisch gelinkt
          ▼                                 ▼                             │
  libk1520core  (C-ABI, §9)         libk1520disk  (C-ABI, §9)            │
  Maschinen · Karten · Bus ·        Dateisysteme · DiskVolume            │
  Primitive · Peripherie                    │                             │
          │                                 │                             │
          └──────────────┬──────────────────┘◄────────────────────────────┘
                         ▼
  gemeinsame statische Bibliotheken: Floppy-Schicht (§8), Formatkatalog,
  Geometrieerkennung, Logger, Hilfsbibliotheken
```

- **`libk1520core`** ist der Emulator: alle Maschinen hinter einer C-ABI (§9).
- **`libk1520disk`** ist das DiskTool ohne Z80, Karten und Bus; es teilt mit dem Kern
  nur die Floppy-Schicht (§8.7a) und die Geometrieerkennung (§8.6.9).
- Die **Oberflächen** sind Python/PySide6 um diese Bibliotheken herum (§10).
- Die **Werkzeuge** (§11) linken die statischen Kernbibliotheken direkt und sehen damit
  mehr als die C-ABI — etwa die ZVE2 des A5120 oder Save-States.
- **`cparun/`** ist ein unabhängiges Unterprojekt (CP/M-Programme direkt auf dem Wirt)
  und teilt keinen Code.

Ein früherer monolithischer Emulator mit BIOS-Traps (`src/`) ist im August 2026 entfernt
worden; nichts hier hängt von ihm ab.

### 1.3 Grundprinzip

Der Emulator bildet echte Hardware nach — keine BIOS-Traps, keine Kenntnis des
Betriebssystems. Boot-ROM, BIOS und Betriebssystem laufen als echter Maschinencode;
dadurch bootet jedes System, das auf der echten Hardware läuft, einschließlich
Formatieren mit den Originalprogrammen.

---

## 2. Emulierte Hardware (A5120 als Referenzplattform)

Der A5120 war die erste Maschine des Kerns; an ihm sind Bus, Primitive und Floppy-Schicht
entstanden. Die übrigen Maschinen sind in §2.6 und in ihren Entwürfen beschrieben.

### 2.1 K1520-System

Der K1520 ist ein genormter Rückwandbus (TGL 37271/01), auf dem Karten mit
Z80-kompatiblen Bausteinen stecken.

```
A5120 Rückwand (11 Steckplätze)
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

Dokumentierte Steckfolge des A5120.16:

1. K8025 (ASS) – serielle Schnittstellen
2. 062-9005 – Z8000-CPU-Karte
3. 062-9000 – Z8000-RAM-Karte
4. K2526 (ZRE) – Zentrale Recheneinheit
5. K7024 (ABS) – Bildschirm
6. K5122 (AFS) – Floppy-Controller
7. K3526 (OPS) – RAM
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


### 2.6 Die übrigen Maschinen

| Maschine | Bus / Aufbau | Besonderheit im Kern | Merkposten |
|---|---|---|---|
| **K8915** | K1520, eine CPU, 128 KB mit Speicherumschaltung A8H (V3) bzw. K2521 + K3528 (V2) | K5122 in der Betriebsart `/WAIT` **ohne** ZVE2; Bild nur über `k1520_screen_char` | [k8915](merkposten/k8915.md), [k8915_varianten](merkposten/k8915_varianten.md) |
| **PRG 710 / 710-1** | K1520, K2521, Seitenregister E8H–EBH | Variante wählt ROM, Tastaturweg (8279/K7609 bzw. K7672) und Marken-FF-Polarität; EPROMmer, Fernschreiber | [prg710](merkposten/prg710.md) |
| **PC 1715 / 1715W** | eigene Platine, kein Steckbus | ROM-Overlay, Bild aus dem Haupt-RAM über 8275; 1715W mit 256 KB, U8272 + Z80-DMA | [pc1715](merkposten/pc1715.md) |
| **P8000** | **eigener Bus**, kein K1520 | 8- und 16-Bit-Teil mit eigener Zeitbasis (`laufeBis`), MMU U8010, WDC mit Original-Firmware, Originalterminal mit Z8 | [p8000](merkposten/p8000.md) |

Der PC 1715 und die P8000 haben keinen K1520-Steckplatz; RAF und K6022 lassen sich am
PC 1715 nur mechanisch gegenprüfen, an der P8000 gar nicht stecken
(`K1520Machine::zusatzkartenSteckbar()`).

---

## 3. Projektstruktur

```
k1520emu/
├── core/                        C++-Kern
│   ├── bus/                     K1520Bus (Speicher-/E-A-Verteilung, INT-Kette, BUSRQ, NMI, MEMDI)
│   │                            + Koppelbus (benannte Signale der Rückwand)
│   ├── primitives/              Bausteine: Z80 (U880), Z80-PIO/-CTC/-SIO/-DMA, U8001 (z8000),
│   │                            U8010 (z8010), Z8, 8275, 8279, µPD765/U8272, RAM/EPROM
│   ├── cards/                   Steckkarten, je Karte ein Verzeichnis (§5.4)
│   ├── peripherals/             Geräte außerhalb des Busses: Tastaturen (k7637, k7672, k7609,
│   │                            tastatur1715), floppy_drive/ (§8), winchester/, lochstreifen/,
│   │                            p8000_terminal{,_hw}/
│   ├── machines/                machine.h (K1520Machine), laufwerke.*, je Maschine ein Verzeichnis
│   ├── filesystem/              Dateisysteme fürs DiskTool: cpm/, udos/, wega/, check/, DiskVolume
│   ├── serial/                  SerialHub, Wandler, Telnet/RFC 2217/Datei (§16.1)
│   ├── api/                     C-ABI: k1520_api (Emulator), k1520_disk_api (DiskTool),
│   │                            k1520_sync_api (physische Diskette), k1520_export.h
│   ├── util/                    yaml_lite, gzip_datei, os_compat, zustand
│   ├── logger.{h,cpp}           Laufzeit-gesteuertes Protokoll (§12)
│   └── version.h.in             Bauversion (§12)
├── app/                         Python/PySide6: Emulator-Oberfläche (§10), disktool/, gw/
├── tools/                       Debugger, Tracer, DiskTool-CLI, Disassembler, Gastprogramme (§11)
├── data/                        formats.yaml (§8.6), default_config_*.yaml (Auslieferungszustand)
├── tests/                       Testebenen unit/ debugtools/ integration/ cli/ system/ python/ (§13)
├── packaging/                   verteilbares Paket, Inno-Setup
├── disks/                       Beispieldisketten je <maschine>_<system>/
├── third_party/                 miniz, isocline
├── cparun/                      eigenständiges Unterprojekt
└── doc/                         dieses Dokument, design/, merkposten/, Analysen, EPROM-Abzüge
```

---

## 4. Emulationsphilosophie

### 4.1 Hardware-genau, nicht OS-spezifisch

Jede Karte wird nach ihrer technischen Dokumentation implementiert. Ergebnis: jedes
Betriebssystem, das auf der echten Hardware läuft, läuft auch im Emulator — ohne
spezifische Kenntnis dieses Systems.

| BIOS-Trap-Emulator | Dieser Kern |
|---|---|
| HALT-Trap → BIOS-Funktion in C++ | E/A-Port → Register der Karte |
| nur das eine System bootbar | jedes System der Maschine bootbar |
| BIOS nachprogrammiert | BIOS läuft als Maschinencode |
| monolithisch | modular, Karte = eigene Klasse |

### 4.2 Karte als eigenständige Klasse

Jede Steckkarte ist eine in sich geschlossene C++-Klasse, die

- sich am Bus anmeldet (E/A- und Speicherbereiche, Glied der Interruptkette),
- über den Koppelbus Signale ausgibt und empfängt,
- Brücken und DIP-Schalter als **Konfigurationsstruktur** zur Bauzeit bekommt
  (z. B. `K2526::A5120Config`), nicht als Laufzeiteinstellung,
- EPROM-Inhalte als eingebettete Datenfelder trägt (§4.4).

Jede Karte ist eine eigene **statische** Bibliothek (`k1520_k5122`, `k1520_k7024`, …) und
damit für sich testbar; `libk1520core` linkt alle zusammen.

### 4.3 Befehlsgenau, nicht zyklusgenau

Der Kern ist **befehls- bzw. transaktionsgenau**:

- die CPU führt einen Befehl als Ganzes aus; Karten reagieren sofort auf den Buszugriff,
- nach jedem Befehl bekommen Zähler, Controller und Geräte die verstrichenen Takte
  (`clockTick(n)`, `takt(n)`, `advance(n)`) — **Maschinenzeit**, nicht Wirtszeit,
- keine Laufzeiten und Bus-Timings auf Signalebene, aber die Interruptkette ist
  reihenfolgetreu.

Wo Zeit für das Gastsystem sichtbar ist (Indexpuls, Baudrate, Wartezyklen der EPROMs,
Tastaturwiederholung), wird sie in Maschinentakten gerechnet. Mehrere Prozessoren einer
Maschine werden in fester Reihenfolge bis zur selben Maschinenzeit nachgezogen
(A5120.16 §5.1b, P8000 in [design/25](design/25_p8000.md) §10.2) — der Lauf bleibt
deterministisch. Was der Rechenaufwand dabei kostet und welche Regeln daraus folgen: §5.3a.

### 4.4 EPROM-Daten als Konstanten zur Bauzeit

```cpp
// Erzeugt aus: tools/eprom_to_h.py doc/EPROMS/K7024/...
#pragma once
#include <cstdint>
static constexpr uint8_t CHARGEN_ZG1[1024] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Zeichen 0x00
    // ...
};
```

Boot-ROMs, Zeichengeneratoren und Gerätefirmware (WDC, Originalterminal) werden so
eingebaut, nicht zur Laufzeit geladen. Ausnahme ist der EPROMmer des PRG 710, dessen
Sockel ein Gerät ist und Dateien liest.

---

## 5. C++-Kern: Schichtmodell

```
┌─────────────────────────────────────────────────────────────────┐
│  Schicht 4: Maschinen                                           │
│  K1520Machine ← A5120 · K8915 · PRG 710 · PC 1715 · P8000       │
│  Karten anlegen, Koppelbus verdrahten, Laufschleife             │
├─────────────────────────────────────────────────────────────────┤
│  Schicht 3: Peripherie                                          │
│  Tastaturen · Laufwerke (§8) · Winchester · Terminal · SerialHub│
├─────────────────────────────────────────────────────────────────┤
│  Schicht 2: Steckkarten                                         │
│  K2526 · K3526 · K7024 · K8025 · K5122 · K7028 · EM · RAF · …   │
├─────────────────────────────────────────────────────────────────┤
│  Schicht 1: Bausteine (Primitive)                               │
│  Z80 · PIO · CTC · SIO · DMA · U8001 · U8010 · Z8 · 8275 · …    │
├─────────────────────────────────────────────────────────────────┤
│  Schicht 0: Bus                                                 │
│  K1520Bus (Systembus) · Koppelbus (Signal-Router)               │
└─────────────────────────────────────────────────────────────────┘
```

Jede Schicht kennt nur die darunter. Die Floppy-Schicht unterhalb von `DiskImage` kennt
überhaupt keine Maschine (§8.7a).

### 5.1 Schicht 0: K1520-Bus

Karten melden sich am Bus an und bekommen Rückrufe für ihre Bereiche. Die Verteilung läuft
über eine Seitentabelle (`rebuildPageTable`), sodass ein Speicherzugriff keinen
Bereichsvergleich kostet.

```cpp
class K1520Bus {
public:
    // Anmeldung
    void registerIO(BusDevice* dev, uint8_t basePort, uint8_t numPorts = 1);
    void registerMem(MemDevice* dev, uint16_t base, uint16_t size);
    void unregisterMem(MemDevice* dev);
    void addMemdiDriver(MemdiDriver* dev);            // /MEMDI je Zugriff (§5.1a)

    // Zugriffe der CPU — E/A mit voller Adresse AB0–15 (§5.1a)
    uint8_t memRead(uint16_t addr);    void memWrite(uint16_t addr, uint8_t data);
    uint8_t ioRead(uint16_t addr);     void ioWrite(uint16_t addr, uint8_t data);

    // Interruptkette (Reihenfolge = Priorität) und Annahme
    void setInterruptChain(std::initializer_list<InterruptSlave*> chain);
    void appendInterruptChain(InterruptSlave* dev);
    uint8_t interruptAcknowledge();    void signalRETI();

    // Bussignale
    void assertINT();   void releaseINT();
    void assertNMI();   void clearNMI();
    void assertWAIT();  void releaseWAIT();  void addWaitCycles(int n);
    void assertBUSRQ(); void releaseBUSRQ();
    void assertRESET();
    void setIODI(bool disabled);
};
```

Die Interruptkette wird nur neu bewertet, wenn ein Glied sich als geändert meldet
(`markIntDirty`). Der A5120 nutzt zusätzlich `setBusMasterZVE2` für die zweite CPU der
K2526 (§14.5).

#### 5.1a /MEMDI gegen MEMDI1/2 (seit 2026-09-28, A5120.16 S1)

Zwei Leitungen, die der Emulator früher in einem Flag zusammenwarf:

- **Bus-/MEMDI (X1 C09)** ist die Speicherprioritätenkette.  Ein `MemdiDriver`
  (heute nur das Erweiterungsmodul EM064/EM256, `core/cards/em/`) entscheidet **je
  Zugriff allein aus der Adresse**, ob er den Zyklus übernimmt; dann liefert ER das
  Byte, und eine K3526-Gruppe mit `memdi_source = false` verwirft den Schreibzyklus.
  Beim EM ist das MEN = ¬(8/16 ∧ RAMEN ∧ PEN) (A310/12 → A112 → A17/11, Scan 9005/2),
  **nicht** PEN allein.
- **MEMDI1/2** der Rückverdrahtung ist ein statischer Pegel vom BS-PIO A7
  (`K2526::onMemdi12` → Koppelbus).  Darauf hört nur eine Gruppe mit
  `memdi_source = true` — am A5120 keine.  Deshalb läuft HARDYs MEMDI-Test durch.

Ohne angemeldeten Vorrangspeicher kostet das im Speicherpfad einen Vergleich.  Die
E/A-Zyklen tragen seit demselben Umbau die volle Adresse AB0–15 (`ioAddress()`),
weil der Attributspeicher des EM256 über AB12–15 adressiert wird (`OUT (n),A` legt
dabei **A** auf AB8–15).

#### 5.1b Zweite CPU auf dem EM: U8001 und Maschinenzeit (seit 2026-09-29, A5120.16 S4)

Der U8001 (`Z8000`, `core/primitives/z8000.h`) sitzt auf der EM-Karte (`EM::u8001()`)
und sieht **nur** den EM-Speicher und die Register A33/A35/A34/A36 — die Karte
übersetzt jeden seiner Buszyklen (Status ST3..0, N/S, SN) über Segmentweiche A42 und
Umschalter A41 in eine DRAM-Zelle (`EM::cellFor16`, dieselbe Zelle wie beim U880,
big-endian).  Zeit: nach jedem Schritt des U880 (bzw. ZVE2/WAIT-Takt) ruft die
Maschine `EM::advance(n)`; die Karte zieht den U8001 befehlsweise im Verhältnis
f16/f8 (4 MHz / 2,45 MHz, `EM::Config`) nach und führt den Rest als Guthaben.  Im
Reset, bei abgegebenem Bus (BUSAK bei anliegendem BUSRQ) und im Stop wird die Zeit
nur verbucht.  Jeder Befehl des U880 beginnt mit `EM::onU880M1()` — der Takt des FF
A29 (8/16).  **Der U880 läuft im 16-Bit-Mode weiter** (Handbuch §1.7.3); BUSRQ/BUSAK
ist der Handschlag zwischen Karte und U8001.  Ohne EM (`Config::em = none`) fällt jeder
dieser Aufrufe weg — der Lauf ist bitgleich dem A5120.  Save-State v6 hängt den
EM-Block (DRAM, A22, PIO, Register, U8001 mit `runState()`) mit Kennbyte an den
Geräteteil an.

*Debug-Anschluss (S5):* `EM::setStepHook` wird **vor** jedem Schritt des U8001 gerufen
(nicht, solange er geparkt ist); liefert er true, kehrt `advance` sofort zurück und die
Zeit bleibt als Guthaben stehen — so hält `k1520dbg` den U8001 vor dem Befehl an.
`EM::setEventHook` meldet jede Kommunikationstransaktion (PIO A32, A33–A36, A22,
Quittungen) und jeden Pegelwechsel (FF A29, INT-16, TREN, BUSRQ/BUSAK, RESET16, NVI,
STOP) als `EM::EreignisInfo`; Text dazu in `tools/em_trace.h` (`boot_trace --em`,
`k1520dbg emlog`).  Ohne Rückruf kostet jede Stelle einen Test.  Nach aussen:
`k1520_create_with_em`, `k1520_em_variant`, `k1520_em_led_v1/_v2`, `k1520_em_mode16`,
`k1520_em_state` (`K1520EmState`: Register, FCW, PSAP, Kartenzustand).

### 5.2 Koppelbus

Der Koppelbus (`core/bus/koppelbus.h`) bildet die maschinenspezifisch verdrahteten
Leitungen der Rückwand nach. Er ist kein Bus im eigentlichen Sinn, sondern ein
**Signal-Router**: eine Karte treibt eine benannte Leitung (`drive(pegel)`), andere hängen
sich mit einem Rückruf daran (`connect(...)`). Verdrahtet wird in der Maschine, beim A5120
im Konstruktor von `A5120Machine`:

```cpp
// core/machines/a5120/a5120.cpp (Auszug)
koppel_.zc_to[ch].drive(lvl);                       // ZRE-CTC gibt ZC/TO aus
koppel_.zc_to[2].connect([this](bool lvl) { ... }); // Taktkaskade
koppel_.memdi1.connect([this, g](bool level) { ops_.setMemDI(g, !level); });
```

Die zweite Interruptkette (/IEI1–/IEO1) ist als Leitung vorhanden, aber nicht verdrahtet
(§14.7).

### 5.3 Schicht 1: Bausteine

Die Primitive sind generische Nachbildungen der Bausteine, unabhängig von jeder Karte.
Grundsatz seit der P8000: **ein Baustein wird vollständig und systematisch getestet**, nicht
nur so weit, wie die erste Karte ihn braucht, und die Debugger unterstützen ihn ganz.

| Baustein | Datei | Benutzt von |
|---|---|---|
| Z80 / U880 | `z80.{h,cpp}` | allen Maschinen, auch im WDC der P8000 |
| Z80-PIO / U855 | `z80_pio.*` | allen Maschinen |
| Z80-CTC / U857 | `z80_ctc.*` | allen Maschinen (Rechenaufwand §5.3a) |
| Z80-SIO / U856 | `z80_sio.*` | K8025, K7028, PC 1715, P8000 |
| Z80-DMA / U858 | `z80_dma.*` | PC 1715W, P8000 |
| U8001 (Z8000) | `z8000.*`, `z8000/` | EM256 (A5120.16), P8000 16-Bit-Karte |
| U8010 (MMU) | `z8010.*` | P8000 |
| Z8 | `z8.*`, `z8/` | P8000-Originalterminal |
| 8275 (CRT-Controller) | `i8275.*` | PC 1715, P8000-Originalterminal |
| 8279 (Tastatur/Anzeige) | `i8279.*` | PRG 710 (K7609, ATP 590068) |
| µPD765 / U8272 | `upd765.*` | PC 1715W, P8000 |
| RAM- / EPROM-Block | `ram_device.h`, `eprom_device.h` | Karten |

Die Z80 ruft den Bus über Rückrufe; sie hält einen Taktzähler und kann für den Debugger
**vor** einem Befehl anhalten (`abortBeforeExecute`).

### 5.3a Rechenaufwand: wo die Zeit hingeht und die Regeln dazu — 2026-10-12

**Befund (Callgrind, Release + `-g`).** Der Kern, nicht die Oberfläche, begrenzt das
erreichbare Vielfache der Echtzeit: der Emulationsfaden (`app/emulationstakt.py`, 2-ms-Scheiben)
läuft bei Überlast zu 100 %, der GUI-Faden braucht ≈ 6 %, es gibt kein Busy-Wait. Im Kern lag
der größte Posten bei **allen** Maschinen im **`Z80CTC`**: A5120 ≈ 32 %, K8915 ≈ 45 %, P8000
≈ 62 %. Ursache: Baudraten-Kanäle (Zeitgeber, ZK klein, z. B. Steuerwort 47H/ZK 2) erzeugen
alle paar Takte einen Nulldurchgang; die alte Fenster-Vorprüfung in `clockTick(int)` sah darum
fast immer ein Ereignis und fiel auf „ein `clockTick()` je Takt" zurück — und die Karten rufen
`clockTick(n)` nach **jedem Befehl** mit n = 4…20.

**Zwei Stufen im CTC (`core/primitives/z80_ctc.{h,cpp}`), beide bitgleich zum Einzeltakten:**

1. **Sprung bis vor die nächste BEOBACHTETE Flanke** (`springe`). Beobachtet ist ein Kanal,
   wenn seine ZC/TO-Flanke eine Wirkung hat: Interrupt freigegeben, Trigger-Betrieb
   (`CTRL_CLK_TRG`, `fireZCTO` hält ihn an) oder Bit in der **Rückruf-Maske**
   (`setZCTOCallback(cb, kanalmaske)`, `zcto_beobachtet(i)`). Unbeobachtete Kanäle werden rein
   arithmetisch fortgeschrieben (Prescaler/Zähler, Nachladen per Modulo); nur die Flanke eines
   beobachteten Kanals läuft als exakter Einzeltakt. Der Zustand am Fensterende ist
   byte-gleich, auch `zcto_active` (Save-State). Prescaler ≥ Teiler bzw. Bruchtakt ≥ φ fallen
   auf Einzeltakte zurück.
2. **Aufgeschobenes Takten** (`clockTick(int)` inline im Header): die Takte werden nur
   aufsummiert (`aufgeschoben_`); verrechnet wird erst, wenn die Summe den **Horizont**
   (`berechneHorizont()`: Takte bis vor die nächste beobachtete Flanke) überschreitet — dann
   fällt die Flanke samt Dirty-Meldung im selben `clockTick`-Aufruf wie vorher, also im selben
   Befehl — oder wenn ein Zugriff den Zustand sieht.

**Zugriffsdisziplin (nicht aufweichen):** jede Methode, die CTC-Zustand liest oder schreibt,
ruft vorher `sichtbar()` — heute `ioRead`, `ioWrite`, `clkTrg`, `getCount`, `debugState`,
`serialize`/`serializeTakt`, `reset`, `setZCTOCallback`, `setzeEingangsTakt`, das einzelne
`clockTick()`. Nach `deserialize*` werden die angesammelten Takte verworfen; nach jeder
Neuprogrammierung ist der Horizont 0 (wird beim nächsten Takt neu bestimmt). **Wer dem CTC
einen neuen Zugriffsweg gibt, muss `sichtbar()` rufen**, sonst sieht er einen veralteten Zähler.

**Rückruf-Maske je Maschine** — Vorgabe `0x0F` (alle beobachtet) ist die sichere Wahl;
verkleinert nur, wo geprüft ist, dass niemand sonst die Flanke abnimmt (Kaskade, Koppelbus,
SIO-Takt als Flanke):

| Maschine / Karte | Maske | Grund |
|---|---|---|
| A5120 (K2526) | `0x07` | der Koppelbus kennt nur ZC/TO0…2 |
| PRG 710 | `0x01` | nach außen geht nur ZC/TO0 |
| K2521 (PRG 710, K8915 V2) | aus Kaskaden + äußerem Rückruf (`zctoVerdrahten()`) | Kaskade K2→K3 hängt als Rückruf an K2 |
| PC 1715W | `0x02` | nur K1 → K2 (Uhr) |
| P8000 Karte16, WDC | `0x04` | nur K2 wird ausgewertet |
| K7028, PC-1715-ZRE, P8000 Karte8 | — | kein Rückruf ⇒ nichts per Maske beobachtet |

**Kleinere Posten:** `Z80PIO::pins/treiber` und `Z80SIO::Channel::format` stehen inline im
Header; `Z80CTC::teilerTakteQ16` merkt sich die letzte 64-Bit-Division.

**Wirkung** (headless über die C-ABI, Mio. Takte je Sekunde, gleicher Rechner):

| Maschine | vorher | nachher |
|---|---|---|
| A5120 (CP/A-Prompt) | 66 | 135 (≈ 54× bei 2,5 MHz) |
| K8915 | 39 | 114 |
| PRG 710 | 100 | 125 |
| PC 1715 | ≈ 83 | 94 |
| P8000 (WEGA, Originalterminal + WDC) | 12,6 (3,2×) | 47 (11,8×) — dazu §10.2a im Entwurf 25 |

**Nachweis der Bitgleichheit:** Hashes von RAM + Bild nach 200–400 Mio. Takten vorher/nachher
gleich (A5120, K8915, PRG 710, PC 1715); P8000-Save-State nach 100 Mio. Takten `cmp`-gleich.
Wächter: `CtcSprung.*` (`tests/unit/primitives/test_ctc_sprung.cpp`: 400 Zufallskonfigurationen
Sprung == Einzeltakt inkl. Kaskade, Zählerlesen, Neuprogrammierung, Kanal-Reset;
`AufgeschobenesTaktenGleichEinzeltaktenBeiZugriffenDazwischen` mit 300 × 3000 Fenstern und
zufälligen Zugriffen dazwischen). Messen für den A5120: `tools/dev.sh tool bench_run`.


### 5.4 Schicht 2: Steckkarten

Jede Karte besteht aus einer Klasse, die sich an Bus und Koppelbus anmeldet, den Bausteinen,
die sie intern benutzt, und einer `Config`-Struktur für Brücken und Schalter.

| Karte | Verzeichnis | Maschinen |
|---|---|---|
| K2526 ZRE (zwei Z80, Q240-Schutz, Boot-ROM) | `k2526/` | A5120 |
| K3526 OPS (64 KB) | `k3526/` | A5120 |
| K7024 ABS (Bildschirm) | `k7024/` | A5120, K8915, PRG 710 |
| K8025 ASS (seriell) | `k8025/` | A5120, PRG 710 |
| K5122 AFS (Floppy, Betriebsarten `/BUSRQ` und `/WAIT`) | `k5122/` | A5120, K8915, PRG 710, PC 1715 (`Portlage::Pc1715`) |
| EM064/EM256 (U8001) | `em/` | A5120.16 |
| ZRE 045-8762 (CPU + 128 KB) | `zre8762/` | K8915 V3 |
| K2521 ZRE · K3528 RAM | `k2521/`, `k3528/` | K8915 V2, PRG 710 |
| K7028 ATS (2 × SIO, 2 × CTC) | `k7028/` | K8915 |
| Speicherverwaltung · ATP 590068 · ASS 590069 | `prg710_speicher/`, `atp590068/`, `ass590069/` | PRG 710 |
| PC-1715-ZRE · 1715W-Speicher/Bild | `pc1715_zre/`, `pc1715w_speicher/` | PC 1715 / 1715W |
| 8-/16-Bit-Karte, MMU-Logik, DRAM, Floppy, Kopplung, WDC | `p8000/` | P8000 |
| RAF 128/512/2M | `raf/` | Option, alle K1520-Maschinen |
| K6022 (Lochstreifen) | `k6022/` | Option, alle K1520-Maschinen |
| K3820 PFS (Festwertspeicher) | `k3820/` | keine — liegt für spätere Geräte bereit |

Die Einzelheiten jeder Karte stehen in ihrem Feinentwurf; die K5122 hat mit
[design/07](design/07_k5122_afs.md) den umfangreichsten.

### 5.5 Schicht 4: Maschinen

Alle Maschinen leiten von **`K1520Machine`** (`core/machines/machine.h`) ab. Diese Klasse
enthält **genau** das, was die C-ABI braucht: Lebenslauf (`powerOn`, `reset`, `run`,
`stop`, `nmi`), Bild, Tastatur, Disketten, serielle Schnittstellen, Optionen,
Diagnosezugriffe. Was einer Maschine eigen ist (ZVE2 und Save-States des A5120,
Speicherumschaltung des K8915, Platten der P8000), bleibt in der konkreten Klasse und wird
von den Werkzeugen direkt benutzt.

```
K1520Machine (abstrakt)
├── A5120Machine         core/machines/a5120/      (+ EM über Config::em)
├── K8915Machine         core/machines/k8915/      (Config::generation = V3 | Gen2)
├── Prg710Machine        core/machines/prg710/     (Variante 710 | 710-1)
├── Pc1715Machine        core/machines/pc1715/     (Variante PC1715 | PC1715W)
├── P8000Machine         core/machines/p8000/      (Ausstattung, Terminal, WDC)
└── P8000TerminalMachine core/machines/p8000/      (nur Originalterminal, ohne Rechner)
```

**`Laufwerke`** (`core/machines/laufwerke.*`) ist der gemeinsame Baustein für alles, was
an der K5122 und ihren vier Laufwerken hängt — Einlegen, Anlegen, Speichern unter,
Formaterkennung, Schreibschutz, Autosave. Die Maschinen reichen nur durch; auch die
Floppy der P8000-8-Bit-Karte benutzt ihn.

**Anlegen und Bestücken.** Die Maschine wird über die C-ABI erzeugt
(`k1520_create`, `k1520_create_configured`, `k1520_create_k8915`, `k1520_create_prg710`,
`k1520_create_pc1715[_ex]`, `k1520_create_p8000[_terminal]`, `k1520_create_with_em`).
Optionen werden **nach dem Anlegen und vor dem ersten Lauf** gesteckt
(`installRaf`, `installK6022`), damit es nicht für jede Kombination eine eigene
Erzeugerfunktion braucht.

**Laufschleife.** `run(max_cycles)` führt Befehl für Befehl aus und gibt nach jedem Befehl
die verbrauchten Takte an Zähler, Controller, Tastatur und `SerialHub` weiter (§4.3).
Eingaben von außen (Tasten, NMI, Anhalten) werden fadensicher eingereiht und in `run()`
verbraucht. Die Oberfläche ruft `run` aus ihrem eigenen Faden (§10.2).

**Save-State.** Der A5120 (Format v7, mit EM-Block) und die P8000 (P8KS v4) können ihren
Zustand speichern und laden; der Medieninhalt gehört nicht dazu.

---

## 6. Bildschirm

### 6.1 Wege zum Bild

| Maschine | Erzeugt von | Zugang |
|---|---|---|
| A5120, K8915, PRG 710 | K7024: VRAM + Zeichengenerator → Pixelbild 640 × 288 | `k1520_framebuffer`, `k1520_screen_char` |
| PC 1715 / 1715W | 8275 liest den Bildspeicher per DMA aus dem Haupt-RAM bzw. dem Bild-RAM, Pixelbild 640 × 300 | ebenso |
| P8000 | Originalterminal: Z8-Firmware + 8275 im Terminal | `k1520_term_framebuffer`, `k1520_term_*` |

Der Text wird **nie** über `k1520_mem_read` gelesen: beim K8915 überdeckt die
Speicherumschaltung den Bildspeicher, beim PC 1715 und der P8000 liegt er gar nicht im
Adressraum der Haupt-CPU. `k1520_screen_char` fragt die Bildschirmkarte direkt.

Die K7024 nutzt zwei Zeichen-EPROMs: `chargen_zg1.h` (Pixelzeilen 0–7) und
`chargen_zg2.h` (Zeilen 8–11); Einzelheiten in [design/05](design/05_k7024_abs.md).

### 6.2 Darstellung

Der Kern liefert ein **monochromes** Pixelbild (8 Bit je Punkt, Stufen für Halbhell). Alles
Weitere — Phosphorfarbe, Nachleuchten, Krümmung, Rasterlinien — macht die Oberfläche in
einem GLSL-Shader (`app/ui/screen_widget.py`, `QOpenGLWidget`, eingestellt über `CRTParams`).
Die Emulation weiß davon nichts.

### 6.3 Text statt Bild

Für Tests, Werkzeuge und die Zwischenablage gibt es den Bildinhalt als Text:
`k1520_screen_char(col, row)` je Zeichen, `k1520_console_poll` für geänderte Zeichen,
beim P8000-Terminal `k1520_term_snapshot` und eine Mitschrift (`k1520_term_log_*`).

---

## 7. Tastatur

### 7.1 Tastaturen der Maschinen

| Maschine | Tastatur | Anschluss |
|---|---|---|
| A5120 | K7637 | seriell an K8025 SIO A32, 9600 Bd |
| K8915 | K7672 (Z8 in der Tastatur, SCP/DCP) | seriell an K7028 |
| PRG 710 | K7609 | 8279 auf der ATP 590068 |
| PRG 710-1 | K7672 | K8025 A32-B |
| PC 1715 | eigene Tastatur mit U880 + S600 | seriell an SIO-A der ZRE |
| P8000 | K7673.09 | am Originalterminal (Z8) |

Alle Tastaturen sind **Peripherie**, keine Busgeräte; sie liefern die physischen Codes der
echten Tastatur (nicht ASCII), die das Gastsystem selbst umsetzt. Die Oberfläche übersetzt
Wirtstasten über eine Tabelle je Tastatur und hat für jede eine Bildschirmtastatur
(`app/ui/keyboard_*.py`).

### 7.2 Zeit

Tastaturbytes werden nach der Zeichenzeit der Schnittstelle freigegeben, in Maschinentakten
(`service(now_cycles)`). Beim K7637 war das die Lösung für Doppel- und Fehleingaben: ohne
die 9600-Baud-Latenz konkurrierten der Tastatur-Scan der Timer-ISR und die LED-Quittung um
dasselbe Empfangsbyte ([design/08](design/08_k7637_keyboard.md)).

Die **Tastenwiederholung** zählt in der Oberfläche in Echtzeit (`setKeyRepeatRealtime`):
der Rechnertakt ist einstellbar, die Tastatur hat ihren eigenen Quarz. In Tests und
Werkzeugen bleibt sie in Maschinentakten, damit Läufe wiederholbar sind.

### 7.3 K7637 im Detail

Signalkette, Codetabellen und die Kommandos des Rechners an die Tastatur (Reset, LED,
Signalton) stehen in [design/08](design/08_k7637_keyboard.md); der Weg der Zeichen im
A5120-BIOS (25-ms-Timer-ISR, Puffer, CONIN) ebenda.

---

## 8. Diskettenemulation (K5122 + Laufwerk)

### 8.1 Schichtung

```
K5122 (Karte)                     core/cards/k5122/
   │ PIO-Register 10H–18H, /STR /ST MK MK1, Index aus der Drehzahl
   ▼
FloppyDriveV2 — DriveProfile + Kopfposition + Spurdichte-Übersetzung
   ▼
DiskImage — DiskMedium (alle Spuren im Speicher) + Bindung + Autosave
   ├── Datei: ImageCodec (.img | .hfe | .dmk)                       §8.7
   └── echte Diskette: TrackSync + Arbeitsfaden (Greaseweazle)       §8.8
   ▼
TrackImage · TrackCodec · BitCodec                                   §8.5
```

Die Geschichte des Stapels in Kürze: zuerst ein Controller, der Sektoren nachbildete
(§8.3/§8.4 beschreiben dessen Portprotokoll, das unverändert gilt); dann der
formatagnostische Lesekopf (§8.5); dann der Formatkatalog in YAML (§8.6); dann das interne
Medium mit Container-Codecs (§8.7); zuletzt die echte Diskette als zweite Bindung (§8.8).
Wo ältere Abschnitte etwas als abgelöst kennzeichnen, gilt der jüngere.

Die P8000 nutzt die Floppy-Schicht ebenfalls (Floppy der 8-Bit-Karte); ihre
Winchester-Platte ist ein eigenes Gerät (§8.9).

### 8.2 Disk-Format-Konfiguration (ABGELÖST → §8.6)

> **Historisch.** Hier stand ein `cpaFormates.cfg`-artiges Format, das nie produktiv benutzt
> wurde. Die Formate stehen seit August 2026 in `data/formats.yaml` (§8.6).

### 8.3 K5122 PIO-Protokoll (vereinfacht)

Die K5122 verwendet zwei Z80-PIO-Bausteine:
- **Steuer-PIO** (10H–13H): Steuersignale (Schritt, Kopf laden, Schreibfreigabe, Marken)
- **Daten-PIO** (14H–17H): Datenbytes beim Lesen und Schreiben
- **Laufwerksauswahl** (18H, 8212): **High-Nibble = /SE (Auswahl), Low-Nibble = /LCK
  (Motor)**, beide low-aktiv

Im A5120 holt die ZVE2 die Bytes unter `/BUSRQ` ab (§14.5); K8915, PRG 710 und PC 1715
fahren die zweite Betriebsart `/WAIT` mit nur einer CPU. Vollständiges Modell:
[design/07](design/07_k5122_afs.md).

### 8.4 Laufwerkszuweisung via C-API

```c
// Laufwerk A: Abbild einlegen. Der Formatname zählt nur bei .img; bei .hfe/.dmk steht
// die Geometrie in der Datei, verlangt wird trotzdem ein gültiger Katalogname
// (die Oberfläche gibt den Laufwerksstandard, k1520_drive_default_format).
k1520_mount_disk(h, 0, "/pfad/cpadisk.hfe", "cpa800", false);

// Laufwerk B: leere, unformatierte Diskette in Laufwerksgeometrie anlegen
k1520_create_disk(h, 1, "/pfad/neu.hfe", "", false);
```

**Laufwerksbestückung** — welche Laufwerkstypen in den vier Anschlüssen stecken, wird beim
Erzeugen der Maschine festgelegt:

```c
K1520Handle h  = k1520_create(K1520_MACHINE_A5120);              // 4 × K5601
K1520Handle h2 = k1520_create_configured(K1520_MACHINE_A5120,
                                         "MF3200", NULL, NULL, NULL);
```

| Profil | Zoll | Spuren × Köpfe | Verfahren | Kapazität |
|--------|------|--------------|-----------|-----------|
| `K5601` (**Vorgabe**) | 5,25″ | 80 × 2 | MFM + FM | 800 KB |
| `K5600.10` | 5,25″ | 40 × 1 | MFM | 200 KB |
| `K5600.20` | 5,25″ | 80 × 1 | MFM + FM | 400 KB |
| `MF3200` | 8″ | 77 × 1 | **nur FM** | ~300 KB |
| `MF6400` | 8″ | 77 × 1 | FM + MFM | ~600 KB |
| `none` | — | — | — | leerer Anschluss |

Das K5602 ist zum MF3200 voll kompatibel und hat kein eigenes Profil; frühere technische
Namen (`ss_525_40`, …) werden als Alias aufgelöst. Das Verfahren (FM/MFM) ist
Laufwerksvorgabe (`DriveProfile::default_read_encoding`), das Gastsystem schaltet per
Steuerwort um (0x85 = MFM / 0x87 = FM).

Passt eine Diskette nicht zur Spurdichte des Laufwerks (40 Spuren im 80er-Laufwerk und
umgekehrt, zweiseitig im einseitigen), wird sie **übersetzt statt abgewiesen**
(`FloppyDriveV2::mount`, `mediumCylinder()`); die Einschränkung meldet `k1520_disk_notice`.
Einzelheiten: [design/09](design/09_floppy_drive.md) §7.2.

> **Laufwerkstyp = Eigenschaft des BIOS, keine Grenze der K5122.** Welche Formate die
> Formatierprogramme anbieten, entscheidet der Diskettenparameterblock des BIOS; die K5122
> streamt formatagnostisch Bits. Deshalb lassen sich mit Combo-Boot-Disketten alle
> Laufwerkstypen-Menüs ohne echte Hardware abgreifen ([format.md](format.md)).

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
  `doc/format.md`.
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
| `step` | nein | `1`\|`2` | **Doppelschritt** (2026-08-11): logische Spur `n` liegt auf physischem Zylinder `n·step`; `tracks:` bleibt logisch. Default 1. |
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

GUI (`app/ui/drive_widget.py`): der Laufwerkskasten zeigt hinter *Format:* das **erkannte**
Format (`k1520_disk_detected_format` → `A5120Machine::detectedFormatName`, §8.6.9), nicht eine
Einstellung.  Eine Formatauswahl gibt es nur noch als **Rückfrage bei `.img`**
(`app/ui/format_dialog.py`) — dort zeigt die Liste `"<name> — <description>"` und behält den
Katalognamen in `Qt::UserRole`.

#### 8.6.9 Erkanntes Diskettenformat (`detectedFormatName`)

Dieselbe Geometrie-Erkennung wie im k1520DiskTool (`GeometryProbe`, doc/design/13_k1520disktool.md
§12.1), angewandt auf das Medium, das gerade im Laufwerk liegt.  Dafür steht `geometry_probe.cpp`
seit 2026-09-13 in einer **eigenen** statischen Bibliothek `k1520_geometry`; sie ist das einzige
Stück der Dateisystem-Schicht, das auch `libk1520core.so` braucht — die beiden gemeinsamen
Bibliotheken bleiben ansonsten getrennt (§10 des DiskTool-Entwurfs).

Drei Fälle liefern **leer** ≡ „unbekannt": kein Katalogformat passt, zwei passen *gleich gut und
beschreiben verschiedene Sektorräume*, oder die Diskette liegt erst teilweise im Speicher
(physisches Laufwerk — eine Messung zöge dort die ganze Scheibe ein).  Die Einschränkung im
zweiten Fall ist wesentlich: `cpa640` und `k5601_16x256` sind bis auf den Namen derselbe Eintrag,
und ein gleich guter Treffer mit **identischer** aufgelöster Spurbelegung ist keine Mehrdeutigkeit
— beide Namen ergäben byteweise dasselbe `.img`.

Bei `.img` wird nicht gemessen, sondern das beim Einlegen **erklärte** Format gemeldet
(`DiskImage::diskFormat()`): ein rohes Sektorabbild trägt keine Adressmarken, seine Geometrie ist
Vereinbarung, nicht Befund.  Wächter: `A5120DiskApi.DetectedFormat_*`.

#### 8.6.8 Etappen

| # | Inhalt | Status |
|---|--------|--------|
| **E1** | `yaml_lite` + Tests, ohne jede Verdrahtung | ✅ `test_yaml_lite` (16 Tests) |
| **E2** | `FormatCatalog` + `data/formats.yaml` als **1:1-Abbild** der 25 Builtins (`encoding: mfm` durchgängig; nur `mf3200` ist FM — wie die bisherige Ableitung aus dem FM-Laufwerk) | ✅ `BootKritischeGeometrien_Unveraendert` prüft cpa780 Spurbereich für Spurbereich |
| **E3** | `A5120Machine` auf Katalog umstellen; `builtinFormats()`/`parseFile()` **entfernt**; §8.2 als abgelöst markiert | ✅ 644/644 ctest, `test_boot_integration` grün |
| **E4** | Verfahren pro Spur durch `RawSectorImage`/`DiskImage::create` gezogen | ✅ `RawMischdichte_VerfahrenJeSpur`, `Mischdichte_VerfahrenProSpurbereich` |
| **E5** | C-API `_description`/`_source`/`_last_init_error`, GUI-Labels, Startabbruch | ✅ end-to-end geprüft |
| **E6** | *offen, optional:* `DriveProfile` ebenfalls aus YAML (`drives.yaml`) — derselbe Parser | — |

#### 8.6.10 Risiken

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
- **R4 — Doppelschritt-Formate** (40 Spuren in einem 80-Spur-Laufwerk, physisch = 2 × logisch)
  sind **gelöst (2026-08-11)**: das Format trägt `step: 2`, `tracks:` bleibt logisch, und
  `DiskFormat::physicalCylinder()` / `logicalCylinder()` rechnen um. Umgesetzt in `SectorSpace`
  (Slot kennt logische *und* physische Spur), `ImgCodec` (das rohe Sektorabbild ist logisch),
  `DiskImage::create` (ungerade Zylinder bleiben unformatiert) und `GeometryProbe` (die Lücken
  sind ein **positives** Erkennungsmerkmal, zusätzlich muss die Spurnummer im ID-Feld die
  logische sein). `formatFitsDrive` prüft die **physische** Ausdehnung. Acht Katalogeinträge
  `*_dstep` decken die §3.4-Geometrien T und U ab; Gegenprobe am laufenden CP/A:
  `DiskToolNeueDisketten.CpaLiestDoppelschrittDiskette`.

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

### 8.7a Die Floppy-Schicht trägt zwei Programme — 2026-08-13

Seit dem **k1520DiskTool** (`doc/design/13_k1520disktool.md`) ist
`core/peripherals/floppy_drive/` nicht mehr nur Zulieferer des Emulators: eine zweite
Bibliothek **`libk1520disk.so`** übersetzt diese Schicht zusammen mit
`core/filesystem/` — **ohne Z80, ohne Karten, ohne Bus**. Daraus folgt eine Regel, die
für jede Änderung unterhalb von `DiskImage` gilt: **nichts darf die Maschine kennen**.
Kein Maschinentakt in einer Signatur, keine `A5120Machine`, keine Kartenabhängigkeit;
die Maschinenuhr des Autosave steht als *Parameter* in `autoFlush(now)`.

Das DiskTool hat die Schicht dabei um eine **Schreibseite** erweitert, die der Emulator
mitträgt (Einzelheiten: `doc/design/09_floppy_drive.md` §11):

- **`TrackCodec` schreibt in vorhandene Spuren** statt sie neu zu bauen —
  `writeSector`/`writeSectorAt` (Datenfeld + CRC, CRC auch **wörtlich** setzbar),
  `writeSectorTail` (die Bytes hinter der Daten-CRC = UDOS-Sektorkontrollblock),
  `createSector`/`eraseSectorAt`. `buildTrack()` taugt dafür **nicht**: es verlöre alles
  hinter der Daten-CRC, bei UDOS also die gesamte Dateiverkettung.
- **`parseTrack` liefert zusätzlich Byte-Offsets, gespeicherte CRCs und `deleted`.**
- **`TrackView`** (`track_view.*`) zerlegt eine Spur in eine lückenlose Folge
  zeichenbarer Abschnitte (Sektor/Gap/unformatiert) für den Diskeditor. Der Winkel eines
  Bytes ist `Position ÷ Spurlänge` — eine `TrackImage` **ist** eine Umdrehung; Bitrate
  und Drehzahl werden dafür nicht gebraucht.
- **Stapeloperationen sind Transaktionen**: die Rücknahme ist eine Kopie des ganzen
  `DiskMedium` (~1 MB) — billiger als Buchführung über Einzeländerungen.

### 8.8 Physische Diskette am Greaseweazle (`TrackSync`) — 2026-08-15

> **Zweite Art von Bindung neben der Datei.** Voller Feinentwurf:
> **`doc/design/14_physische_diskette.md`**, Medium-Sicht: `doc/design/09_floppy_drive.md` §12.

Eine gemountete Diskette hatte bisher genau eine Quelle: eine **Datei**, die beim Öffnen
vollständig gelesen und beim Autosave vollständig geschrieben wird. Daneben tritt jetzt
die **echte Diskette in einem echten Laufwerk**, angebunden über einen
[Greaseweazle](https://github.com/keirf/greaseweazle)-Adapter am USB. Der Unterschied ist
nicht das Medium, sondern die **Körnung**: gelesen und geschrieben wird **spurweise nach
Bedarf**, nicht am Stück.

```
                                                        ┌── K5122 (Emulator)
echte Diskette ◄─gw─► Arbeitsfaden ◄─Aufträge─► DiskMedium
                        (Python)                        └── DiskVolume (DiskTool)
```

**Vier Festlegungen tragen das:**

1. **Je Spur ein Zustand** statt eines Dirty-Bits: `Unknown` (nie gelesen, Inhalt
   **ungültig**) / `Clean` / `Dirty`. `Unknown` ist **nicht** „unformatiert“ — letzteres
   ist eine belegte Aussage über die Diskette (§8.7), ersteres gar keine. Darum melden
   `formatted()`/`rawCompatible()` zusätzlich `complete()`; sonst erklärte sich eine halb
   gelesene Diskette für unformatiert und ein `.img` schriebe die ungelesenen Spuren als
   Füllbytes fest.
2. **Nachgeladen wird in `DiskMedium::track()`** — der einzigen Stelle, durch die jeder
   Verbraucher geht (Controller, DiskTool, Formaterkennung). Der Aufruf **blockiert**
   (≈ 0,5–0,8 s je Spur); im Emulator wartet der Maschinenfaden, also genau der, der auch
   an einem echten Laufwerk wartete. Die medienweiten Reihenläufe benutzen dagegen
   `peek()` und laden **nie** nach — sonst zöge eine beiläufige Statusabfrage die ganze
   Diskette ein.
3. **Drei Prioritäten** in `TrackSync`: **1** Lesen auf Anforderung (jemand wartet),
   **2** geänderte Spuren zurückschreiben (nach einer Schreibpause von ≈ 0,5 s — dieselbe
   Regel wie der Autosave, sonst schriebe eine UDOS-Dateioperation dieselbe Spur
   dutzendfach), **3** unbekannte Spuren vorausschauend lesen (kürzester Kopfweg zuerst).
   Prio 1 **verdrängt**, unterbricht aber keinen laufenden Zugriff.
3a. **Geschrieben gilt erst nach dem Zurücklesen** (Feinentwurf §7.1). Der Verify-Lauf
   des Gastsystems (`FORMAT`) prüft das *Speicherabbild gegen sich selbst* und sieht eine
   Schadstelle der Diskette nie; deshalb folgt jedem `Write` ein `Verify`, das die Spur
   zurückliest und auf **Sektorebene** vergleicht. Erst dann wird `Dirty` gelöscht.
   Misslingt es zweimal, gilt die Spur als **schadhaft**: sie bleibt `Dirty`, „Speichern"
   meldet Misserfolg mit Spurnummer, und die Oberfläche bietet
   „Diskette neu beschreiben" für eine heile Diskette. Das Zurückgelesene wird **nie**
   ins Abbild übernommen — sonst überschriebe ein misslungener Schreibvorgang genau die
   Daten, die er zerstört hat.
4. **Der Kern kennt Greaseweazle nicht.** `TrackSync` hat keinen eigenen Faden und kein
   USB; ein **fremder Arbeitsfaden** holt sich Aufträge ab (`k1520s_take_job`, die einzige
   blockierende Funktion der ABI) und liefert **HFE-Bitzellen** zurück, die durch
   denselben `BitCodec::decode` laufen wie eine HFE-Datei. Rückrufe in die Anwendung gibt
   es nicht — das hält die GIL aus dem Kern heraus und die Verklemmungen aus dem Entwurf.

**Warum die Gerätehälfte in Python liegt:** Greaseweazle liefert Firmware + **Hosttools in
Python** und keine C++-Bibliothek. Das USB-Protokoll wäre nachbaubar, die gereifte
Flusswechsel-Auswertung (PLL, Index, Wiederholungen) nicht sinnvoll. Beide Anwendungen
sind ohnehin Python um eine Bibliothek herum. Das Paket ist **freiwillig**: fehlt es,
fehlt der Menüpunkt.

**Testbarkeit ohne Hardware** (das Laufwerk hängt in der CI nie am Rechner): Der
Ersatz-Arbeitsfaden bedient die Aufträge aus einer **`.hfe`-Datei** — dieselben
Bitzellen, die der Adapter liefern würde, denn eine HFE-Aufnahme *ist* genau das. Damit
liegen Zustandsverwaltung, Warteschlange, Decodierung und sogar ein voller Boot „von
einer spurweisen Quelle“ in der Regression; die Hardware fügt nur USB hinzu und läuft von
Hand hinter `K1520_GW_HARDWARE=1`.


### 8.9 Winchester-Platte der P8000 — 2026-10

Die Platte am WDC (`core/peripherals/winchester/platte.*`) gehört nicht zur Floppy-Schicht.
Die Original-Firmware des WDC sieht nie Bits, sondern Bytes; eine Spur ist deshalb ein
umlaufender Ring aus Datenbytes mit Markenkennzeichen, das Medium ein rohes LBA-Abbild.
Plattendateien tragen das Typkürzel (`<name>.<k5504|d5126|d5146|vs>.img`) und liegen
**gepackt** als `.img.gz` vor: das Abbild liegt ganz im Speicher, ein eigener Faden schreibt
nach einer Schreibpause zurück (`core/util/gzip_datei.*`). Einzelheiten:
[design/25](design/25_p8000.md) §10.8 und [merkposten/p8000](merkposten/p8000.md).

---

## 9. C-ABI

Die Python-Seite sieht den Kern **nur** über `extern "C"`-Funktionen. Es gibt drei
Schnittstellen in zwei Bibliotheken:

| Kopf | Präfix | Bibliothek | Inhalt |
|---|---|---|---|
| `core/api/k1520_api.h` | `k1520_` | `libk1520core` | Emulator: Maschinen, Bild, Tastatur, Disketten, Schnittstellen, Optionen, Diagnose |
| `core/api/k1520_disk_api.h` | `k1520d_` | `libk1520disk` | DiskTool: Abbild öffnen, Dateien, Diskeditor, Prüfung, Rettung |
| `core/api/k1520_sync_api.h` | `k1520s_` | beide | physische Diskette: Aufträge abholen und erledigen (§8.8) |

Regeln, die für alle drei gelten:

- **`K1520_API`** (`core/api/k1520_export.h`) vor jeder Funktion — ohne das exportiert eine
  MSVC-DLL nichts.
- **Erweitern, nicht ändern.** Neue Fähigkeiten kommen als neue Funktionen; eine
  bestehende Signatur bleibt. Ein Handle (`K1520Handle`) steht für eine Maschine; welche,
  entscheidet allein die Erzeugerfunktion (§5.5).
- **Kein C++-Typ über die Grenze** — nur Zahlen, Zeichenketten, Puffer und einfache
  Strukturen (`K1520EmState`, …).
- **Fehler** über `k1520_last_error(h)`; scheitert schon das Anlegen, über
  `k1520_last_init_error()`.
- **Drei Stellen müssen übereinstimmen** — Kopf, Bibliothek und die ctypes-Deklarationen
  in `app/core_binding/k1520.py` bzw. `k1520disk.py`. `tests/python/test_c_api.py`
  vergleicht sie mechanisch, denn eine abweichende Signatur bricht sonst still.

Funktionsliste und Gruppen: der Kopf selbst und [design/10](design/10_c_api.md).

---

## 10. Python-Anwendung

Feinentwurf: [design/11](design/11_python_app.md), Bedienung:
[`app/help/handbuch.md`](../app/help/handbuch.md).

### 10.1 Ein Programm, fünf Gestalten

Die Oberfläche (`app/main.py` → `app/ui/main_window.py`) ist für alle Maschinen dieselbe.
Was einer Maschine eigen ist, steht im **Programmprofil** (`app/profil.py`): Titel,
Konfigurationsdatei, Takt, Tastatur, Frontplatte, Modellwahl, eigene Aktionen. In der
Oberfläche gibt es kein `if machine == …`. Der Name des Starters (`a5120emu`,
`k8915emu`, `prg710emu`, `pc1715emu`, `p8000emu`) bzw. `--machine` wählt das Profil.

Bausteine der Oberfläche:

- **Aktionen** — jede Bedienung ist eine `QAction` in `app/ui/actions.py`; Menü und
  Symbolleiste zeigen dieselbe, die Leiste wird aus einer Namensliste der Konfiguration
  gebaut. Tastenkürzel nur mit `Strg+Umschalt` (Ausnahme `F11`), weil alle übrigen Tasten
  dem emulierten Rechner gehören.
- **Bildschirm** — `ScreenWidget` mit CRT-Shader (§6.2).
- **Laufwerkskasten**, **Statuszeile** (eingestellter Takt, Laufwerksleuchte, Abbild,
  Schreibschutz), **Einstellungen** mit den Reitern für Schnittstellen, RAM-Disk,
  Lochstreifen.
- **Pfade** — alles über `app/paths.py`: Umgebungsvariable → Installationslayout →
  Quellbaum. `python3 app/main.py --paths` zeigt die ganze Auflösung.
- **Konfiguration** — je Programm eine YAML-Datei (`a5120emu.yaml`, …) im
  Benutzerordner; der Auslieferungszustand ist ebenfalls eine Datei
  (`data/default_config_<maschine>.yaml`).

### 10.2 Fäden

Die Maschine rechnet in einem **eigenen Faden** (`app/emulationstakt.py`) in kurzen
Scheiben; `k1520_run` gibt die GIL frei. Kann der Wirt den eingestellten Takt nicht halten,
wird die Maschine langsamer, nicht die Oberfläche.

Der Kern ist **nicht** threadsicher: jeder Aufruf geht über die **Kernsperre**
(`_GesperrteBibliothek` in `app/core_binding/k1520.py`), mehrere zusammenhängende mit
`with kern_sperre():`. Weil ein `RLock` nicht fair ist, melden Wartende sich an und
bekommen nach der laufenden Scheibe den Vortritt — sonst verhungert die Oberfläche.

Weitere Fäden außerhalb der Maschine: der E/A-Faden des `SerialHub` (§16.1) und der
Arbeitsfaden der physischen Diskette (§8.8).

### 10.3 Das DiskTool

`app/disktool/` ist eine zweite Anwendung über `libk1520disk` mit eigener Oberfläche und
eigenem Handbuch ([`app/disktool/help/handbuch.md`](../app/disktool/help/handbuch.md)).
Beide Programme starten einander über das Menü *Werkzeuge* (`app/programme.py`).
Entwurf: [design/13_k1520disktool](design/13_k1520disktool.md),
[design/15](design/15_dateisystempruefung.md) (Prüfung), [design/27](design/27_wega_dateisystem.md) (WEGA).

---

## 11. Werkzeuge

Die Werkzeuge in `tools/` linken die statischen Kernbibliotheken direkt und kommen damit an
alles, was die C-ABI bewusst nicht zeigt.

| Werkzeug | Zweck | Doku |
|---|---|---|
| `k1520dbg` | interaktiver Debugger für alle Maschinen (`--machine`), beide CPUs des A5120, U8001, Z8 | [handbuch_k1520dbg](handbuch_k1520dbg.md), [tools/k1520dbg.md](../tools/k1520dbg.md) |
| `boot_trace` | nicht-interaktiver Tracer: lokalisiert, wo ein Lauf hängt | [tools/boot_trace.md](../tools/boot_trace.md) |
| `k1520disktool` | DiskTool auf der Kommandozeile | [tools/k1520disktool.md](../tools/k1520disktool.md) |
| `floppy_diag`, `format_driver`, `kbd_test`, `bench_run` | Diagnose, Formatier-Pipeline, Messung | [tools/README.md](../tools/README.md) |
| `z8kasm`, Disassembler | Assembler für U8001-Firmware, Z80/Z8-Disassembler | [tools/z80_disasm.md](../tools/z80_disasm.md) |

Einstieg in die Fehlersuche: [tools/how_to_debug_and_trace.md](../tools/how_to_debug_and_trace.md).
Daneben liegen Gastprogramme mit Quelle und fertigem `.com` (SERTEST, ROMREAD, EM256-Prüfungen),
die auf die Beispieldisketten kopiert werden.

---

## 12. Build-System

- **CMake**, jede Komponente eine statische Bibliothek (`k1520_bus`, `k1520_z80`,
  `k1520_k5122`, `k1520_a5120`, …), daraus die zwei gemeinsamen Bibliotheken
  `k1520core` und `k1520disk`, die Werkzeuge und die Testprogramme.
- **`LOG_LEVEL`** (0–5) ist die **Obergrenze** zur Bauzeit; was darüber liegt, wird nicht
  übersetzt. Die tatsächliche Ausgabe steuert der Logger zur Laufzeit
  (`core/logger.h`): Grundstufe, dazu Fenster nach Befehlszähler oder Takt und ein
  bereichsweiser Schub (`K1520_LOG_BOOST`).
- Zwei Bauverzeichnisse: `build/` (LOG_LEVEL 3) und `build_trace/` (LOG_LEVEL 5).
  **Gebaut und getestet wird immer über `tools/dev.sh`**, das vorher neu baut — ein
  Werkzeug direkt aus einem nicht neu gebauten Verzeichnis testet veraltete Objekte.
- **Plattformen:** Linux (GCC) und Windows (MSVC, eigener Zweig im `CMakeLists.txt`);
  `tools/dev.sh win` baut zur Vorprüfung mit MinGW-w64 und testet unter wine.
- **Version** aus der Datei `VERSION`, abgeleitet über `tools/version.py`
  (`core/version.h.in` → `k1520_version()`); Konzept in [ci_pipeline](ci_pipeline.md) §7.
- **Paket** über `packaging/build_payload.sh` bzw. das Inno-Setup:
  [design/13_distribution](design/13_distribution.md), [merkposten/paketierung](merkposten/paketierung.md).

---

## 13. Testkonzept

Das Testsystem ist nach Ebenen gegliedert (`unit`, `debugtools`, `integration`, `cli`,
`system`, `python`), jede Ebene ein Verzeichnis unter `tests/` und zugleich ein
ctest-Label; quer dazu `fast`/`slow`. Begründung der Gliederung:
[design/12](design/12_testing.md). Ausführen und erweitern: [tests/README.md](../tests/README.md).
Testdisketten: [tests/fixtures/README.md](../tests/fixtures/README.md).

```sh
tools/dev.sh test          # Regression (rund 2900 Fälle, einige Minuten)
tools/dev.sh test-format   # langsame Formatier- und Bootketten
tools/dev.sh test-matrix   # jeder FORMAT.COM-Menüeintrag einzeln
tools/dev.sh test-level unit
```

Zwei Grundsätze tragen den Aufbau: **Festlegungen haben Wächter** — jede Regel in den
Merkposten nennt den Test, der sie hält —, und **die Python-Ebene prüft, was C++ nicht
erreicht**: die C-ABI gegen ihre ctypes-Deklarationen und die Verdrahtung der Oberfläche
(Qt ohne Bildschirm).


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

Beide sind inzwischen als Maschinen umgesetzt (§2.6). Was am Gerät noch zu klären ist,
führen die Pläne als Anwenderfragen: K8915 in [design/16](design/16_k8915.md) §6 und
[merkposten/k8915](merkposten/k8915.md), PRG 710 in [design/20](design/20_prg710.md) §8 und
[merkposten/prg710](merkposten/prg710.md). Weitere offene Punkte: [open_points.md](open_points.md).

---

## 16. Querschnittsthemen

### 16.1 Serielle Schnittstellen nach außen

Die seriellen Kanäle der Karten gehen über **Telnet**, **RFC 2217** (Client oder Server)
oder in eine **Datei** nach außen. Je Maschine ein `SerialHub` (`core/serial/`) mit eigenem
E/A-Faden; je Schnittstelle ein `Wandler`, der in **Maschinenzeit** nach der vom Gast
programmierten Baudrate taktet und verlustfrei durch Rückstau ist. Die Karten liefern nur
einen `SerialAnschluss` und kennen kein Netz. Der Gast ist maßgeblich: eine Anfrage der
Gegenstelle ändert Baudrate und Format nie.
[design/19](design/19_serielle_schnittstellen.md), [merkposten/serielle_schnittstellen](merkposten/serielle_schnittstellen.md).

### 16.2 Steckbare Optionen

- **RAM-Floppy RAF 128/512/2M** auf E/A 88H/89H; RESET erhält den Inhalt, die Oberfläche
  legt ihn als Stand-by-Ablage ab. [design/22](design/22_raf512.md), [merkposten/raf](merkposten/raf.md).
- **Lochstreifen K6022** (Leser und Stanzer) auf E/A E0H–E7H; Bandformate roh, Intel HEX
  und ASCII-Bild, der Stanzer schreibt nach einer Pause zurück.
  [design/23](design/23_lochstreifen.md), [merkposten/lochstreifen](merkposten/lochstreifen.md).

Beide werden nach dem Anlegen und vor dem ersten Lauf gesteckt (§5.5).

### 16.3 Physische Diskette

Ein Greaseweazle-Adapter ersetzt die Datei als Bindung des Mediums; der Kern kennt den
Adapter nicht (§8.8). Das Python-Paket `greaseweazle` ist freiwillig — fehlt es, fehlt der
Menüpunkt, und die Tests laufen trotzdem.
[design/14](design/14_physische_diskette.md), [merkposten/physische_diskette](merkposten/physische_diskette.md).

### 16.4 Verteilung

Das Anwenderpaket (~2 MB) bringt Python und Qt bei der Installation in eine eigene
Umgebung innerhalb der Installation, ohne Administratorrechte; unter Windows über ein
Inno-Setup. Arbeitsdisketten liegen im Dokumentenordner, getrennt vom Programm.
[design/13_distribution](design/13_distribution.md), [packaging/README](../packaging/README.md).

---

*Feinentwürfe je Baugruppe und Maschine: [`doc/design/`](design/). Festlegungen mit Wächtern:
[`doc/merkposten/`](merkposten/).*
