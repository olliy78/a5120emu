# K1520 Emulator – Architekturkonzept

**Autor:** Olaf Krieger  
**Stand:** Mai 2026  
**Status:** Planungsdokument (v0.1)

---

## 1. Vision und Ziele

### 1.1 Ausgangssituation

Der bestehende `a5120emu` emuliert den **Robotron A5120** (1984) – einen Z80-basierten DDR-Rechner. Die Emulation ist **BIOS-trap-basiert**: Original-CPA-BIOS-Einsprungpunkte werden durch `HALT`-Opcodes ersetzt und von C++ abgefangen. Dies funktioniert ausschließlich für CPA (CP/M-kompatibel) und kann kein anderes Betriebssystem booten.

### 1.2 Ziele der Neuarchitektur

| Ziel | Beschreibung |
|------|--------------|
| **Multi-Maschine** | A5120, A5130, K8915 und weitere K1520-Rechner konfigurierbar |
| **Beliebige OS** | Booten von Diskettenimage – CPA, aber auch andere DDR-OS |
| **Echte Hardware-Emulation** | FDC, CRT-Controller, Tastatur, PIO/SIO/CTC auf Register-Ebene |
| **Qt6-GUI** | Maschinenspezifisches Aussehen, Disk-Images zur Laufzeit laden |
| **CLI-Modus** | Konsolenein-/ausgabe ohne Zeichengenerator und GUI |
| **Erweiterbarkeit** | Neue Karten und Maschinen als Konfiguration, nicht als Compile-time-Option |
| **CPArun bleibt** | Eigenständiges Tool, keine Änderungen |

---

## 2. Technologie-Stack und Begründung

### 2.1 C++-Kern + Python-Anwendung: Sinnvoll?

**Ja, diese Aufteilung ist sehr sinnvoll** und folgt dem bewährten Muster moderner Emulatoren (z.B. MAME, BSNES, Dolphin).

```
┌─────────────────────────────────────────────────────────────────┐
│  Python-Anwendung (PyQt6 / PySide6)                             │
│  GUI, Konfiguration, Maschinendefinitionen, Plugin-System        │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  C-API (libk1520core.so)                                │   │
│  │  Stabile ABI-Grenze, keine Python-Abhängigkeiten im Kern │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  C++-Kern (libk1520core)                                        │
│  Z80, Bus, Geräte, Timing – performance-kritisch                │
└─────────────────────────────────────────────────────────────────┘
```

**Vorteile der Aufteilung:**

- **Performance:** Z80-Emulation, FDC-Timing und Framebuffer-Rendering laufen in nativem C++ ohne GIL-Overhead
- **Flexibilität:** Maschinendefinitionen, GUI-Themes und Konfigurationen in Python – keine Neukompilierung bei Änderungen
- **Testbarkeit:** C++-Kern per `ctest` testbar; Python-Schicht per `pytest`
- **CLI-Modus:** C++ Kern kann ohne Python gestartet werden (`k1520cli`-Binary)
- **Qt6 in Python:** PySide6 (offizielle Qt-Bindung) oder PyQt6 sind stabil und vollständig. Qt-Widgets, QOpenGL und QAudio sind verfügbar.

**Bindungsstrategie:**  
Kein pybind11 – stattdessen eine **saubere C-API** (`extern "C"`) als `libk1520core.so`. Python nutzt `ctypes` oder `cffi`. Dies hält die ABI stabil und macht den Kern auch von anderen Sprachen nutzbar.

---

## 3. Gesamtarchitektur

### 3.1 Komponentenübersicht

```
k1520emu/
├── core/                        # C++ Kern (neu)
│   ├── cpu/                     # Z80-CPU (bestehend, erweitert)
│   ├── bus/                     # K1520-Bus-Backplane
│   ├── memory/                  # Speicherverwaltung mit Banking
│   ├── devices/                 # Gerätekarten (Hardware-Emulation)
│   │   ├── fdc/                 # K5122 Diskettenkontroller
│   │   ├── crt/                 # K7027 CRT-Controller
│   │   ├── kbd/                 # K7028 Tastaturkontroller
│   │   └── io/                  # PIO, SIO, CTC, DMA
│   ├── machines/                # Maschinenkonfigurationen
│   ├── scheduler/               # Zyklengenaues Timing
│   └── api/                     # C-API (libk1520core.so)
│
├── app/                         # Python-Anwendung (neu)
│   ├── ui/                      # Qt6-GUI
│   ├── config/                  # Maschinenkonfigurationen (JSON/YAML)
│   └── core_binding/            # ctypes-Bindung an C-API
│
├── cli/                         # C++ CLI-Binary (neu)
│   └── k1520cli.cpp             # Standalone-CLI ohne Python
│
├── cparun/                      # Unverändert
├── src/                         # Bestehender Code (Migration)
└── tests/                       # C++ und Python Tests
```

### 3.2 Datenfluss (GUI-Modus)

```
Python Qt6 GUI
│
├── Tastatureingabe → ctypes → k1520_key_event() → [C++ Kern]
│                                                     │
│                                                     ▼
│                                              Z80 CPU läuft
│                                              Buszyklen werden ausgeführt
│                                              Geräte reagieren auf I/O-Ports
│                                                     │
├── Qt-Timer (60 Hz) → k1520_get_framebuffer() ──────┘
│   → QImage aus Framebuffer rendern
│   → In Machine-View-Widget anzeigen
│
└── Disk-Mount → k1520_mount_disk() → [C++ FDC-Emulation]
```

### 3.3 Datenfluss (CLI-Modus)

```
Terminal (stdin/stdout)
│
├── stdin → k1520_console_input() → [C++ Kern] → UART/Tastatur-Emulation
│
└── [C++ Kern] → CRT-VRAM → Text-Extraktion → stdout
    (Zeichengenerator-ROM wird umgangen)
```

---

## 4. C++-Kern: Detailarchitektur

### 4.1 K1520-Bus (Backplane)

Der **K1520-Bus** ist der zentrale Verbindungsbus des Systems. Alle Karten kommunizieren über ihn.

```cpp
class K1520Bus {
public:
    // Speicherbus (64 KB Adressraum)
    uint8_t memRead(uint16_t addr);
    void    memWrite(uint16_t addr, uint8_t data);

    // I/O-Bus (256 Port-Adressen, 8-Bit)
    uint8_t ioRead(uint8_t port);
    void    ioWrite(uint8_t port, uint8_t data);

    // Interrupt-System
    void    requestINT(uint8_t vector);
    void    requestNMI();

    // Kartenregistrierung
    void    addDevice(BusDevice* device, uint8_t ioBase, uint8_t ioSize);
    void    addMemDevice(MemDevice* device, uint16_t base, uint16_t size);
};
```

Jede Karte implementiert `BusDevice` oder `MemDevice` und registriert ihren I/O-Port- bzw. Adressbereich beim Bus.

### 4.2 Z80-CPU (Migration bestehend)

Die bestehende `z80.cpp/h` wird übernommen. Anpassungen:

- `readByte`/`writeByte` → direkt an `K1520Bus::memRead/Write`
- `readPort`/`writePort` → direkt an `K1520Bus::ioRead/Write`
- **HALT-Trap-Mechanismus entfällt** für Hardware-Emulation (bleibt optional für Legacy-CPA-Modus)
- Zyklenzähler für Timing-genaue Ausführung (`step(maxCycles) → usedCycles`)

### 4.3 Speicherverwaltung mit Banking

K1520-Rechner nutzen **Memory Banking** für mehr als 64 KB RAM. Die Speicherkarte muss konfigurierbare Bänke unterstützen:

```
Adressraum:
0x0000 – 0x3FFF: ROM/RAM Bank 0 (wählbar per I/O-Port)
0x4000 – 0x7FFF: RAM Bank (wählbar)
0x8000 – 0xBFFF: RAM Bank (wählbar)
0xC000 – 0xEFFF: RAM Bank (wählbar)
0xF000 – 0xF7FF: RAM / I/O-Bereich
0xF800 – 0xFFFF: Bildschirm-VRAM
```

**Offene Frage:** Genaue Banking-Register-Adressen und -Mechanismus für A5120 und A5130 – Blockschaltplan der Speicherkarte erforderlich.

### 4.4 Gerätekarten

#### 4.4.1 K5122 / K2526 – Diskettenkontroller

**Wichtigste Komponente für OS-unabhängiges Booten.**

Der K5122 ist ein dedizierter DDR-FDC-Chip (ähnlich NEC µPD765 / Intel 8272). Der bestehende BIOS-Trap-Ansatz muss durch echte Registeremulation ersetzt werden.

**Benötigte Informationen:**
- [ ] K5122-Datenblatt: Registeradressbelegung, Befehlssatz, Statusregister
- [ ] I/O-Port-Adressen der K2526-Karte im K1520-Bus
- [ ] Timing: Sektorlese-/Schreibsequenzen, DRQ/IRQ-Signalisierung
- [ ] Interruptvektor der K2526-Karte

**Bekannt aus `biosdskt.mac`:** Grundprinzip der PIO-gesteuerten FDC-Kommunikation  
**Noch unklar:** Vollständiger Befehlssatz, genaue Port-Adressen, Fehlerbehandlungs-Sequenzen

```cpp
class FDC_K5122 : public BusDevice {
    // Registersatz
    enum Reg { STATUS = 0, DATA = 1, CONTROL = 2 };

    void ioWrite(uint8_t port, uint8_t data) override;
    uint8_t ioRead(uint8_t port) override;

    // Interne FDC-State-Machine
    void processCommand();
    void executeSectorRead();
    void executeSectorWrite();
    void executeSeek();
    // ...

    FloppyDisk drives_[4];
    // Timing: Schrittraten, Settle-Zeit etc.
};
```

#### 4.4.2 K7027 – CRT-Controller

Der K7027 implementiert die Bildschirmansteuerung mit **Hardware-Zeichengenerator** (EPROM-basiert, typisch 8×10 Pixel pro Zeichen).

**Emulationsschichten:**

```
VRAM (0xF800–0xFFFF, 1920 Bytes)
         │
         ▼
K7027-Emulation
  ├── Zeichengenerator-ROM (EPROM-Abbild)
  ├── Cursor-Steuerung (Position, Blinken)
  ├── Attributbyte-Dekodierung (invers, hell/dunkel)
  └── Framebuffer-Rendering (640×400 oder 480×300 Pixel)
         │
         ▼
Framebuffer (RGBA, für Qt/CLI)
```

**Für CLI-Modus:** VRAM wird als ASCII-Text interpretiert (Zeichengenerator-ROM übersprungen), Ausgabe geht direkt auf stdout.

**Benötigte Informationen:**
- [ ] K7027-Blockschaltplan und Registerübersicht
- [ ] Zeichengenerator-EPROM-Inhalt (Bitmap-Font)
- [ ] I/O-Port-Adressen der K7027-Karte
- [ ] Bildschirmformat: Zeichen/Zeile, Zeilen/Bildschirm, Pixelauflösung
- [ ] Attributbyte-Format (falls vorhanden)
- [ ] Cursorbewegung via I/O oder VRAM-Sonderbytes?

#### 4.4.3 K7028 – Tastaturkontroller

Schnittstelle zwischen Tastaturmatrix und K1520-Bus.

**Benötigte Informationen:**
- [ ] Scan-Code-Tabelle der K7028-Tastatur (Hardware-Scancodes → ASCII)
- [ ] I/O-Port-Adressen
- [ ] Interrupt-Mechanismus (PIO-gesteuert? Direkter IRQ-Vektor?)
- [ ] Tastatur-Scancodes für Sondertasten (F-Tasten, DDR-spezifische Tasten)

#### 4.4.4 Z80-Peripherie-Chips (PIO, SIO, CTC, DMA)

Diese entsprechen den Zilog-Standardchips und sind gut dokumentiert:

| Karte | Chip | Funktion | Port-Adressen |
|-------|------|----------|---------------|
| K2520 | Z80-PIO | Parallele I/O (2× 8-Bit) | ? |
| K2521 | Z80-SIO | Serielle I/O (RS-232, V.24) | ? |
| K2523 | Z80-CTC | Timer/Zähler (4 Kanäle) | ? |
| K2524 | Z80-DMA | DMA-Kontroller | ? |

**Bekannt:** Chip-Funktionalität aus Zilog-Datenblättern  
**Benötigt:** I/O-Port-Belegung für K1520-Karten und Interrupt-Vektoren

### 4.5 Maschinendefinitionen

Jede Maschine ist eine **Konfiguration von Karten** und deren Verbindungen.

```cpp
struct MachineConfig {
    std::string name;           // "A5120", "A5130", ...
    std::string description;
    uint32_t    cpuFreqHz;      // 2500000 für A5120
    std::string romImage;       // Pfad zum Monitor-ROM
    uint16_t    romLoadAddr;    // Adresse für ROM-Einblendung
    std::vector<CardConfig> cards;  // Verbauete Karten
};

struct CardConfig {
    std::string type;           // "K2526", "K7027", etc.
    uint8_t     ioBase;         // Basis-I/O-Port
    std::string romImage;       // Optional: Karten-ROM (Zeichengenerator)
    // Karten-spezifische Parameter...
};
```

In Python/JSON konfiguriert:
```json
{
  "machine": "A5120",
  "cpu_freq_hz": 2500000,
  "rom": "roms/a5120_monitor.rom",
  "cards": [
    { "type": "K2526", "io_base": 80, "drives": 2 },
    { "type": "K7027", "io_base": 64, "charset_rom": "roms/k7027_chars.rom" },
    { "type": "K7028", "io_base": 96 },
    { "type": "K2520", "io_base": 112 },
    { "type": "K2523", "io_base": 128 }
  ]
}
```

**Offene Fragen für vollständige Konfiguration:**
- [ ] A5120: Welche Karten sind standardmäßig verbaut?
- [ ] A5130: Unterschiede zur A5120 (erweitertes RAM? andere FDC?)
- [ ] Monitor-ROM: Ist ein EPROM-Abbild des Boot-ROMs verfügbar?
- [ ] Welche weiteren K1520-Maschinen sollen unterstützt werden?

---

## 5. Boot-Prozess für beliebige Betriebssysteme

### 5.1 Problem mit dem bisherigen Ansatz

```
Aktuell (HALT-Trap):
CPU → HALT → C++ fängt ab → BIOS-Funktion in Software → zurück

Problem: Funktioniert NUR für CPA, weil der Trap-Mechanismus
die CPA-BIOS-Sprungta belle kennen muss.
```

### 5.2 Neuer Ansatz: Hardware-genaue Emulation

```
Neu (Hardware-Level):
CPU → I/O-Port-Write → FDC-Emulation reagiert → DRQ/IRQ → CPU liest Daten

Vorteil: Jedes OS, das echte K1520-Hardware nutzt, läuft automatisch.
```

### 5.3 Boot-Sequenz (Hardware-genau)

```
1. Reset: CPU springt auf Adresse 0x0000
           → Monitor-ROM (Bootstrap) ist eingeblendet
           
2. Monitor-ROM:
   - Initialisiert RAM (Speichertest)
   - Initialisiert CTC (Timer für Baudrate etc.)
   - Initialisiert FDC (K5122 Reset, Motor-On)
   - Liest Sektor 0, Spur 0, Seite 0 vom Laufwerk A: in RAM
   - Springt zum gelesenen Bootstrap-Code
   
3. Bootstrap-Code (von Diskette):
   - Liest weitere Sektoren nach, lädt OS-Kernel in RAM
   - Springt zum OS-Einstieg

4. OS läuft nativ auf emulierter Z80-Hardware
   - FDC-Zugriffe: echte K5122-Registeroperationen
   - Bildschirm: echte K7027-VRAM-Schreibvorgänge
   - Tastatur: echte K7028-PIO-Abfragen
```

**Schlüsselbedingung:** Monitor-ROM-Abbild muss vorhanden sein, oder der Boot-Vektor muss konfigurierbar sein.

### 5.4 Legacy-CPA-Kompatibilität

Der bestehende `@os.com`-Lade-Mechanismus und der HALT-Trap-Modus bleiben als **optionaler Legacy-Modus** erhalten, da er schnell und ohne ROM-Images funktioniert.

---

## 6. C-API (libk1520core)

Die C-API ist die einzige Schnittstelle zwischen Python und dem C++-Kern. Sie ist bewusst minimalistisch gehalten.

```c
// ─── Maschinen-Lebenszyklus ───────────────────────────────────────
K1520Handle k1520_create(const char* config_json);
void        k1520_destroy(K1520Handle h);
void        k1520_reset(K1520Handle h);
int         k1520_load_config(K1520Handle h, const char* json_path);

// ─── Ausführungssteuerung ─────────────────────────────────────────
// Führt exakt 'cycles' CPU-Zyklen aus, gibt tatsächlich verbrauchte zurück
int         k1520_run_cycles(K1520Handle h, int cycles);
void        k1520_step(K1520Handle h);           // Einzelschritt (Debug)
bool        k1520_is_running(K1520Handle h);

// ─── Anzeige ──────────────────────────────────────────────────────
// Framebuffer: RGBA-Pixelarray, Größe = width × height × 4 Bytes
const uint8_t* k1520_get_framebuffer(K1520Handle h);
int            k1520_get_fb_width(K1520Handle h);
int            k1520_get_fb_height(K1520Handle h);
bool           k1520_framebuffer_dirty(K1520Handle h); // Seit letztem Aufruf geändert?

// ─── Eingabe ──────────────────────────────────────────────────────
void        k1520_key_press(K1520Handle h, uint32_t keycode);
void        k1520_key_release(K1520Handle h, uint32_t keycode);

// ─── Laufwerke ────────────────────────────────────────────────────
bool        k1520_mount_disk(K1520Handle h, int drive, const char* image_path);
bool        k1520_unmount_disk(K1520Handle h, int drive);
bool        k1520_disk_activity(K1520Handle h, int drive);  // LED-Status

// ─── CLI-Modus: Konsolen-I/O ──────────────────────────────────────
// Aktiviert Text-Bypass-Modus (Zeichengenerator umgangen)
void        k1520_set_console_mode(K1520Handle h, bool enable);
bool        k1520_console_output_available(K1520Handle h);
int         k1520_console_read_char(K1520Handle h);  // -1 = kein Zeichen
void        k1520_console_write_char(K1520Handle h, char c);

// ─── Debug/Diagnose ───────────────────────────────────────────────
void        k1520_get_cpu_state(K1520Handle h, K1520CpuState* out);
uint8_t     k1520_mem_read(K1520Handle h, uint16_t addr);
uint8_t     k1520_io_read(K1520Handle h, uint8_t port);
const char* k1520_get_log(K1520Handle h);  // Geräte-Log für Debugging
```

---

## 7. Python-Anwendung (Qt6-GUI)

### 7.1 Architektur

```
app/
├── main.py                  # Einstiegspunkt, Argument-Parsing
├── core_binding/
│   ├── __init__.py
│   └── k1520.py             # ctypes-Wrapper um C-API
├── ui/
│   ├── main_window.py       # Hauptfenster (QMainWindow)
│   ├── machine_view.py      # Maschinenansicht mit Screen + Frontpanel
│   ├── screen_widget.py     # QWidget: Framebuffer → QImage → paint
│   ├── drive_widget.py      # Diskettenlaufwerk-Slots (per Drag&Drop)
│   ├── config_dialog.py     # Hardwarekonfigurationsdialog
│   └── themes/
│       ├── a5120.py         # A5120-spezifische Farben/Layout
│       └── a5130.py         # A5130-spezifisches Layout
├── config/
│   ├── machines/
│   │   ├── a5120.json       # A5120-Maschinenkonfiguration
│   │   └── a5130.json       # A5130-Maschinenkonfiguration
│   └── user_settings.json   # Letzte verwendete Disks, Fensterposition etc.
└── emulator_thread.py       # QThread: C++-Kern läuft in eigenem Thread
```

### 7.2 Emulator-Thread-Modell

Der C++-Kern läuft in einem **separaten QThread**, um die GUI reaktionsfähig zu halten:

```python
class EmulatorThread(QThread):
    frameReady = Signal(bytes)  # RGBA-Framebuffer für GUI-Update
    diskActivity = Signal(int, bool)  # Laufwerk, aktiv

    def run(self):
        while not self.stopped:
            # Emuliere ~2.5 MHz / 60 Hz = ~41.667 Zyklen pro Frame
            cycles_per_frame = self.cpu_freq_hz // 60
            k1520_run_cycles(self.handle, cycles_per_frame)

            if k1520_framebuffer_dirty(self.handle):
                fb = k1520_get_framebuffer(self.handle)
                self.frameReady.emit(bytes(fb))
```

### 7.3 Screen-Widget

```python
class ScreenWidget(QWidget):
    def paintEvent(self, event):
        if self.framebuffer:
            img = QImage(self.framebuffer,
                         self.fb_width, self.fb_height,
                         QImage.Format_RGBA8888)
            # Skalierung mit Integer-Faktor (1×, 2×, 3×)
            scaled = img.scaled(self.size(), Qt.KeepAspectRatio,
                                Qt.FastTransformation)
            QPainter(self).drawImage(0, 0, scaled)
```

### 7.4 Frontpanel-Design

Das GUI soll **optisch an den jeweiligen Rechner erinnern**. Grundprinzip:

```
┌──────────────────────────────────────────────────────────────────┐
│  [ROBOTRON A5120]                              [●] Power LED     │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                                                          │   │
│  │              Bildschirm (640×400, grün-phosphor)         │   │
│  │                                                          │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌──────────────┐  ┌──────────────┐                            │
│  │  Laufwerk A: │  │  Laufwerk B: │   [Datei laden]  [Reset]  │
│  │  [●] aktiv   │  │  [○] leer    │                            │
│  │  diskA.img ▼ │  │  (leer)   ▼ │                            │
│  └──────────────┘  └──────────────┘                            │
└──────────────────────────────────────────────────────────────────┘
```

- Phosphorfarbe und Gehäusefarbe per Maschinenprofil konfigurierbar
- Drag & Drop von `.img`-Dateien auf Laufwerksslots
- Laufwerks-LED blinkt bei Aktivität
- Kontextmenü an Laufwerkslots: Öffnen, Auswerfen, Neues Image erstellen

### 7.5 Konfigurationsdialog

```
Maschinenkonfiguration
┌─────────────────────────────────────────────────────────┐
│ Maschinentyp: [A5120          ▼]                        │
│ CPU-Frequenz: [2.500.000] Hz                            │
│ Monitor-ROM:  [roms/a5120_monitor.rom]  [Durchsuchen]   │
│                                                         │
│ Verbauete Karten:                                       │
│ ┌─────────────────────────────────────────────────────┐ │
│ │ [✓] K2526 – FDC         Port: 0x50  Laufwerke: [2] │ │
│ │ [✓] K7027 – CRT         Port: 0x40  Charset: [ROM] │ │
│ │ [✓] K7028 – Tastatur    Port: 0x60                 │ │
│ │ [✓] K2520 – PIO         Port: 0x70                 │ │
│ │ [ ] K2521 – SIO         Port: 0x78                 │ │
│ │ [✓] K2523 – CTC         Port: 0x80                 │ │
│ │ [+] Karte hinzufügen...                             │ │
│ └─────────────────────────────────────────────────────┘ │
│                          [Abbrechen]  [Übernehmen]      │
└─────────────────────────────────────────────────────────┘
```

---

## 8. CLI-Modus

### 8.1 Konzept

Im CLI-Modus wird kein Qt6 gestartet. Der C++-Kern läuft direkt mit dem bestehenden `AnsiTerminal`-Konzept, erweitert um echte Hardware-Emulation.

**Text-Bypass-Mechanismus:**
- K7027-Emulation schreibt VRAM wie immer
- Statt Zeichengenerator-EPROM werden Bytes direkt als ASCII interpretiert
- Ausgabe geht auf stdout (ANSI-Terminal)

```bash
# CLI-Modus
k1520emu --machine a5120 --disk-a cpadisk.img --cli

# GUI-Modus (Standard)
k1520emu --machine a5120 --disk-a cpadisk.img

# Batch-Modus (für Skripting, aus bestehendem Code übernommen)
k1520emu --machine a5120 --disk-a cpadisk.img --cli --exec "dir" --output /tmp/dir.txt
```

### 8.2 Starters für CLI (`k1520cli.cpp`)

Das bestehende `a5120emu.cpp` wird zum Ausgangspunkt für `k1520cli.cpp`. Es nutzt die gleiche C-API wie die Python-Anwendung.

---

## 9. Migrationspfad vom bestehenden Code

### 9.1 Phasen

**Phase 1: Kern-Umbau** (Hardware-Basis)
- Bestehende `z80.cpp/h` → `core/cpu/`
- Bestehende `memory.cpp/h` → `core/memory/` (erweitert um Banking)
- Bestehende `floppy.cpp/h` → `core/devices/fdc/` (als Disk-Image-Backend)
- Neues `core/bus/k1520bus.cpp` (Backplane mit Geräteregistrierung)
- Neuer K5122-FDC (Hardware-Register-Emulation, nutzt Floppy-Backend)
- Bestehenden `terminal.h`-Ansatz für CLI-Modus beibehalten
- `cpa_bios.cpp` → als Legacy-Modus erhalten, optional aktivierbar

**Phase 2: C-API und Python-Bindung**
- `core/api/k1520_api.h` und `k1520_api.cpp` (C-Interface)
- `app/core_binding/k1520.py` (ctypes-Wrapper)
- Basis-Qt6-Fenster mit Screen-Widget und Emulator-Thread

**Phase 3: Vollständige GUI**
- Maschinenspezifische Themes
- Konfigurationsdialog
- Disk-Drag&Drop, Laufwerks-LEDs

**Phase 4: Weitere Maschinen**
- A5130-Konfiguration
- Weitere K1520-Varianten

### 9.2 Was bleibt unverändert

| Komponente | Status |
|------------|--------|
| `cparun/` | Unverändert – eigenständiges Projekt |
| `z80.cpp/h` | Kernlogik übernommen, Callbacks angepasst |
| `floppy.cpp/h` | Als Disk-Image-Backend wiederverwendet |
| `tests/` | Z80- und Floppy-Tests bleiben gültig |
| `cpa_src/` | Referenzmaterial – unveränderter CPA-Quellcode |
| `disks/` | Weiterhin verwendbare Disk-Images |

---

## 10. Benötigte Hardware-Informationen

Dies ist die Prioritätsliste der fehlenden Informationen für eine vollständige Hardware-Emulation:

### Kritisch (ohne diese kein OS-unabhängiges Booten)

| # | Information | Verwendung | Quelle |
|---|-------------|------------|--------|
| 1 | **K5122 FDC Registerübersicht** | Befehlssatz, Statusregister, DRQ/IRQ | Datenblatt |
| 2 | **K2526-Karte I/O-Port-Adressen** | Dekodierung im K1520-Bus | Schaltplan |
| 3 | **Monitor-ROM-Inhalt (EPROM-Abbild)** | Boot-Sequenz ohne CPA-Loader | ROM-Dump |
| 4 | **K7027-Registerübersicht** | CRT-Steuerung, Cursor, Timing | Datenblatt |
| 5 | **K7027 I/O-Port-Adressen** | I/O-Dekodierung | Schaltplan |

### Wichtig (für Vollständigkeit der Hardware-Emulation)

| # | Information | Verwendung | Quelle |
|---|-------------|------------|--------|
| 6 | **Zeichengenerator-EPROM-Inhalt** | Pixel-genaue Textdarstellung | ROM-Dump |
| 7 | **K7028 Tastenscancodes** | Korrekte Tastenzuordnung | Datenblatt |
| 8 | **K7028 I/O-Port-Adressen** | I/O-Dekodierung | Schaltplan |
| 9 | **K2520/K2521/K2523 Port-Adressen** | PIO/SIO/CTC-Emulation | Schaltplan |
| 10 | **A5120-Gesamtblockschaltplan** | Memory-Banking-Register | Schaltplan |
| 11 | **A5130-Unterschiede zu A5120** | Separate Maschinenkonfig | Datenblatt |

### Hilfreich (für weitere Maschinen)

| # | Information | Verwendung | Quelle |
|---|-------------|------------|--------|
| 12 | **K8915-Konfiguration** | Weitere Maschinendefinition | Datenblatt |
| 13 | **Weitere K1520-OS-Images** | Test verschiedener Betriebssysteme | Disk-Images |
| 14 | **Frontpanel-Fotos A5120/A5130** | GUI-Design | Foto |

---

## 11. Nicht-CPA-Betriebssysteme

### Bekannte K1520-Betriebssysteme

Neben CPA gab es für K1520-Rechner weitere Betriebssysteme:

- **SCP** (System Control Program) – eigene DDR-Entwicklung
- **MOS** (Modular Operating System) – für K8915-Varianten
- **UDOS** – weiteres DDR-Betriebssystem
- Möglicherweise proprietäre Betriebssysteme für Spezialanwendungen

Für das Booten dieser Systeme wird voraussichtlich Folgendes benötigt:
- Korrekte Monitor-ROM-Emulation (Startsequenz hängt vom ROM ab)
- Hardware-genaue FDC-Emulation (andere OS nutzen FDC direkt, nicht via BIOS-Trap)
- Ggf. andere Speicherkonfigurationen (Banking-Register)

---

## 12. Verzeichnisstruktur (Zielzustand)

```
k1520emu/
├── core/                            # C++ Kern-Bibliothek
│   ├── CMakeLists.txt
│   ├── cpu/
│   │   ├── z80.cpp / z80.h          # Bestehend (angepasst)
│   │   └── z80_types.h
│   ├── bus/
│   │   ├── k1520bus.cpp / .h        # K1520-Backplane
│   │   └── bus_device.h             # Abstrakte Geräte-Schnittstelle
│   ├── memory/
│   │   ├── memory_map.cpp / .h      # Banking-Speicher
│   │   └── rom.cpp / .h             # ROM-Image-Loader
│   ├── devices/
│   │   ├── fdc/
│   │   │   ├── fdc_k5122.cpp / .h   # K5122 FDC
│   │   │   └── floppy_disk.cpp / .h # Disk-Image (aus floppy.cpp)
│   │   ├── crt/
│   │   │   ├── crt_k7027.cpp / .h   # K7027 CRT-Controller
│   │   │   └── chargen.cpp / .h     # Zeichengenerator-EPROM
│   │   ├── kbd/
│   │   │   └── kbd_k7028.cpp / .h   # K7028 Tastaturkontroller
│   │   └── io/
│   │       ├── pio_z80.cpp / .h     # Z80 PIO (K2520)
│   │       ├── sio_z80.cpp / .h     # Z80 SIO (K2521)
│   │       ├── ctc_z80.cpp / .h     # Z80 CTC (K2523)
│   │       └── dma_z80.cpp / .h     # Z80 DMA (K2524)
│   ├── machines/
│   │   ├── machine.h               # Abstrakte Maschinendefinition
│   │   ├── a5120.cpp / .h          # A5120-Konfiguration
│   │   └── a5130.cpp / .h          # A5130-Konfiguration
│   ├── scheduler/
│   │   └── scheduler.cpp / .h      # Zyklengenaues Timing
│   ├── terminal/
│   │   ├── terminal.h              # Bestehend
│   │   ├── terminal_ansi.cpp       # Bestehend
│   │   └── terminal_cli.cpp        # CLI-Bypass (Text ohne Zeichengenerator)
│   ├── legacy/
│   │   └── cpa_bios.cpp / .h       # Legacy-CPA-Modus (HALT-Trap)
│   └── api/
│       ├── k1520_api.h             # C-API-Header (öffentlich)
│       └── k1520_api.cpp           # C-API-Implementierung
│
├── cli/                            # Standalone CLI-Binary
│   ├── CMakeLists.txt
│   └── k1520cli.cpp
│
├── app/                            # Python Qt6-Anwendung
│   ├── requirements.txt            # PySide6, etc.
│   ├── main.py
│   ├── emulator_thread.py          # QThread für C++-Kern
│   ├── core_binding/
│   │   └── k1520.py                # ctypes-Bindung
│   ├── ui/
│   │   ├── main_window.py
│   │   ├── machine_view.py
│   │   ├── screen_widget.py
│   │   ├── drive_widget.py
│   │   └── config_dialog.py
│   ├── themes/
│   │   ├── base_theme.py
│   │   ├── a5120_theme.py
│   │   └── a5130_theme.py
│   └── config/
│       └── machines/
│           ├── a5120.json
│           └── a5130.json
│
├── roms/                           # ROM-Abbilder (EPROM-Dumps)
│   └── .gitkeep                    # Leer – ROMs nicht im Repo
│
├── cparun/                         # Unverändert
├── src/                            # Bestehend (wird nach core/ migriert)
├── cpa_src/                        # CPA-Quellcode (Referenz)
├── disks/                          # Disk-Images
├── tests/                          # C++ Tests (erweitert)
│   └── python/                     # Python-Tests (neu)
├── doc/                            # Diese Datei und weitere Doku
└── CMakeLists.txt                  # Angepasst für Kern + CLI
```

---

## 13. Offene Fragen und nächste Schritte

### 13.1 Fragen an den Nutzer

1. **Monitor-ROM:** Ist ein EPROM-Abbild des A5120/A5130 Boot-ROMs verfügbar? Ohne dieses ist echte Hardware-Emulation nicht möglich (alternativ: synthetischer Bootstrap).

2. **K5122-Datenblatt:** Ist die K5122-FDC-Spezifikation verfügbar? Sie ist das Herzstück für OS-unabhängiges Booten.

3. **K7027-Zeichengenerator:** Ist der EPROM-Inhalt des Zeichengenerators verfügbar? Nötig für Pixel-genaue Textdarstellung.

4. **Welche weiteren Maschinen:** Außer A5120 und A5130 – welche konkreten K1520-Varianten sollen unterstützt werden?

5. **Welche Nicht-CPA-OS:** Welche Betriebssysteme (mit Disk-Images) sollen booten können?

6. **A5120-Aussehen:** Wie soll das GUI-Theme für A5120 und A5130 aussehen? Fotos/Beschreibung des Frontpanels?

### 13.2 Empfohlene Reihenfolge

```
Schritt 1: Hardware-Doku beschaffen (ROMs, Schaltpläne, Datenblätter)
     ↓
Schritt 2: K1520Bus + abstrakte Device-Schnittstelle implementieren
     ↓
Schritt 3: K5122 FDC hardware-genau implementieren (Basis für alle OS)
     ↓
Schritt 4: K7027 CRT hardware-genau implementieren
     ↓
Schritt 5: Monitor-ROM einbinden → echter Boot-Test
     ↓
Schritt 6: C-API fertigstellen
     ↓
Schritt 7: Python-Bindung + minimale Qt6-GUI (Screen + Keyboard)
     ↓
Schritt 8: Vollständige GUI (Themes, Konfigurationsdialog, Disk-Management)
     ↓
Schritt 9: Weitere Maschinen und OS
```

---

*Dieses Dokument wird aktualisiert, sobald Hardware-Dokumentation vorliegt.*
