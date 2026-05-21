# C++ Testsystem des Robotron A5120 Emulators

## Übersicht

Das Testsystem besteht aus zwei unabhängigen Teilen:

1. **`tests/test_main.cpp`** – Ein schlankes, eigenes Testframework ohne externe Abhängigkeiten.
   Es deckt grundlegende Z80-CPU-Befehle, Speicheroperationen, Diskettenzugriff und einfache
   Integrationsszenarien ab.

2. **`tests/cpp/`** – Eine Sammlung von Google Test (GTest)-Unit-Tests, die jede
   Hardware-Komponente des Emulators isoliert und vollständig testen.

Beide Testsuiten werden über CMake gebaut und können mit CTest ausgeführt werden.

---

## Voraussetzungen

| Werkzeug     | Version       |
|--------------|---------------|
| CMake        | >= 3.14       |
| C++17-Compiler (GCC / Clang) | beliebig |
| Google Test  | wird per FetchContent geladen oder systemweit installiert |

---

## Build & Ausführung

```bash
# Repository klonen und bauen
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Alle Tests mit CTest ausführen
cd build && ctest --output-on-failure

# Einzelne GTest-Binärdatei direkt starten
./build/tests/test_k2526
./build/tests/test_ctc

# Legacy-Testsuite (test_main.cpp)
./build/test_main
```

### Hilfreiche CTest-Optionen

```bash
ctest -R k2526       # nur Tests, die "k2526" enthalten
ctest -V             # ausführliche Ausgabe
ctest --parallel 4   # parallele Ausführung
```

---

## Verzeichnisstruktur

```
tests/
├── test_main.cpp          # Legacy-Suite: Z80, Memory, Floppy, Integration
└── cpp/
    ├── test_bus.cpp       # K1520-Bus-Dispatcher
    ├── test_ctc.cpp       # Z80 CTC (U857D)
    ├── test_floppy.cpp    # Floppy-Laufwerk (FloppyDrive)
    ├── test_format_parser.cpp  # Disk-Format-Parser
    ├── test_k2526.cpp     # K2526 ZRE-Karte (ZVE1+ZVE2, ROM, Schutz)
    ├── test_k3526.cpp     # K3526 OPS-Speicherkarte
    ├── test_k5122.cpp     # K5122 AFS-Floppy-Controller
    ├── test_k7024.cpp     # K7024 ABS-Bildschirmkarte
    ├── test_k7637.cpp     # K7637-Tastaturcontroller
    ├── test_k8025.cpp     # K8025-Peripheriekarte
    ├── test_pio.cpp       # Z80 PIO
    └── test_sio.cpp       # Z80 SIO
```

---

## Testfälle

### K1520\_Bus (`test_bus.cpp`)

Testet den zentralen Systembus des A5120 (K1520-Architektur), der I/O-Zugriffe,
Speicherzugriffe und den Z80-Interrupt-Daisy-Chain-Mechanismus koordiniert.

#### `K1520Bus/IODispatch_WriteReach`
- **Ziel:** Ein Schreibzugriff auf einen registrierten I/O-Port erreicht das Gerät korrekt.
- **Testschritte:** MockIO auf Port 0x10–0x13 registrieren; `ioWrite(0x12, 0xAB)` aufrufen.
- **Erwartet (PASS):** `last_port_written == 0x12`, `last_data_written == 0xAB`, `write_count == 1`.

#### `K1520Bus/IODispatch_Read`
- **Ziel:** Lesezugriffe auf registrierte Ports liefern den Gerätewert; nicht registrierte Ports liefern 0xFF (Bus-Float).
- **Testschritte:** MockIO auf Port 0x20 registrieren (Rückgabe 0x55); `ioRead(0x20)` und `ioRead(0x21)` aufrufen.
- **Erwartet (PASS):** `ioRead(0x20) == 0x55`, `ioRead(0x21) == 0xFF`.

#### `K1520Bus/IODispatch_Conflict_Throws`
- **Ziel:** Überlappende Port-Registrierungen werden als Fehler erkannt und werfen eine Exception.
- **Testschritte:** Zwei Geräte auf überlappenden Port-Bereichen registrieren.
- **Erwartet (PASS):** `std::runtime_error` wird geworfen.

#### `K1520Bus/IODI_Blocks`
- **Ziel:** Das globale IODI-Signal sperrt alle I/O-Zugriffe (Bus-Inhibit).
- **Testschritte:** `setIODI(true)` aufrufen; danach schreiben und lesen.
- **Erwartet (PASS):** Gerät empfängt keine Schreibzugriffe; Lesen liefert 0xFF.

#### `K1520Bus/MemDispatch_Write`
- **Ziel:** Schreibzugriff auf registrierten Speicherbereich wird korrekt weitergeleitet.
- **Testschritte:** MockMem auf 0x1000..0x10FF registrieren; `memWrite(0x1010, 0xCC)`.
- **Erwartet (PASS):** `mem.buf[0x10] == 0xCC`.

#### `K1520Bus/MemDispatch_Read`
- **Ziel:** Lesezugriff auf registrierten Speicher liefert den Inhalt; nicht registrierte Adressen liefern 0xFF.
- **Testschritte:** MockMem auf 0x2000 registrieren, `buf[5] = 0x77`; `memRead(0x2005)` und `memRead(0x3000)`.
- **Erwartet (PASS):** `memRead(0x2005) == 0x77`, `memRead(0x3000) == 0xFF`.

#### `K1520Bus/MemDispatch_OverlapLaterWins`
- **Ziel:** Später registriertes ROM überschreibt einen RAM-Bereich (ROM-Overlay).
- **Testschritte:** RAM auf 0x0000..0x00FF, dann ROM auf 0x0000..0x003F registrieren.
- **Erwartet (PASS):** Adresse 0x0000 liefert ROM-Wert (0xBB); 0x0040 liefert RAM-Wert (0xAA).

#### `K1520Bus/MemDispatch_ROMIgnoresWrite`
- **Ziel:** Schreibzugriffe auf Read-Only-Speicher (ROM) werden ignoriert.
- **Testschritte:** ROM (`isWritable() == false`) registrieren; `memWrite(0x0000, 0x42)`.
- **Erwartet (PASS):** `rom.buf[0] == 0xEE` (unverändert).

#### `K1520Bus/MEMDI_BlocksRead`
- **Ziel:** Das globale MEMDI-Signal sperrt alle Speicher-Lesezugriffe.
- **Testschritte:** `setMEMDI(true)`; danach `memRead`.
- **Erwartet (PASS):** Rückgabe 0xFF.

#### `K1520Bus/UnregisterMem`
- **Ziel:** Nach dem Abmelden eines Speicherbereichs liefern Zugriffe Bus-Float (0xFF).
- **Testschritte:** `registerMem`, `unregisterMem`, `memRead`.
- **Erwartet (PASS):** `memRead(0x1000) == 0xFF`.

#### `K1520Bus/InterruptChain_HigherPriorityWins`
- **Ziel:** Das Gerät mit höherer Priorität (vorne in der Kette) gewinnt beim Interrupt-Acknowledge.
- **Testschritte:** hi (Vektor 0x20) und lo (Vektor 0x30) anmelden; beide anfordern.
- **Erwartet (PASS):** `interruptAcknowledge() == 0x20`; `lo.iei_in == false`.

#### `K1520Bus/InterruptChain_LowerPriorityIfHigherClear`
- **Ziel:** Wenn das höher priorisierte Gerät keinen Interrupt anfordert, darf das niedrig priorisierte Gerät.
- **Testschritte:** Nur `lo.irq_req = true`.
- **Erwartet (PASS):** `interruptAcknowledge() == 0x30`.

#### `K1520Bus/InterruptChain_NoInterrupt`
- **Ziel:** Ohne ausstehende Interrupts liefert `interruptAcknowledge()` den Sentinel-Wert 0xFF.
- **Erwartet (PASS):** `interruptAcknowledge() == 0xFF`.

#### `K1520Bus/BUSRQ_InitiallyFalse`
- **Ziel:** Der BUSRQ-Ausgang ist nach der Initialisierung inaktiv.
- **Erwartet (PASS):** `isBUSRQ() == false`.

#### `K1520Bus/AssertBUSRQ_IsBUSRQReturnsTrue`
- **Ziel:** `assertBUSRQ()` setzt den BUSRQ-Ausgang.
- **Erwartet (PASS):** `isBUSRQ() == true`.

#### `K1520Bus/ReleaseBUSRQ_AfterAssert_ReturnsFalse`
- **Ziel:** `releaseBUSRQ()` gibt den Bus wieder frei.
- **Erwartet (PASS):** Nach Release: `isBUSRQ() == false`.

#### `K1520Bus/AssertBUSRQ_Idempotent`
- **Ziel:** Mehrfaches `assertBUSRQ()` ist idempotent und stürzt nicht ab.
- **Erwartet (PASS):** Nach doppeltem Assert und anschließendem Release: `isBUSRQ() == false`.

#### `K1520Bus/ReleaseBUSRQ_WithoutAssert_DoesNotCrash`
- **Ziel:** `releaseBUSRQ()` ohne vorherigen Assert stürzt nicht ab.
- **Erwartet (PASS):** `isBUSRQ() == false`, kein Absturz.

---

### CTC (`test_ctc.cpp`)

Testet die Z80 CTC-Emulation (Chip U857D), die im A5120 für Timer, Baudratengenerierung
und Zählfunktionen verwendet wird. Alle Tests orientieren sich am Steuerregisterformat
des U857D-Datenblatts.

#### `Z80CTC/TimerMode_ZCTOFiresAfter160Ticks`
- **Ziel:** ZC/TO-Puls tritt nach genau `Prescaler × TC = 16 × 10 = 160` Systemtakten auf.
- **Testschritte:** Kanal 0 mit Steuerbyte 0x05, TC=10 konfigurieren; 159 Takte – kein Puls; Takt 160.
- **Erwartet (PASS):** Nach 159 Takten: `zcto_count == 0`; nach Takt 160: `zcto_count == 1`.

#### `Z80CTC/TimerMode_AutoReload`
- **Ziel:** Der Timer lädt den TC automatisch nach jedem Nulldurchgang nach.
- **Testschritte:** 320 Takte (2 × 160) ausführen.
- **Erwartet (PASS):** `zcto_count == 2`.

#### `Z80CTC/TimerMode_ZCTOPulseHighAfterOneTick`
- **Ziel:** Der ZC/TO-Puls ist genau einen Systemtakt breit (fallende und steigende Flanke).
- **Testschritte:** TC=1, Prescaler ÷16; Puls erwartet nach 16 Takten, Ende nach Takt 17.
- **Erwartet (PASS):** `events[0].second == false` (fallend), `events[1].second == true` (steigend).

#### `Z80CTC/TimerMode_Prescaler256`
- **Ziel:** Prescaler ÷256 verlängert die Periode auf `2 × 256 = 512` Takte.
- **Testschritte:** Steuerbyte 0x25, TC=2; 511 Takte – kein Puls; Takt 512.
- **Erwartet (PASS):** Genau ein Puls nach 512 Takten.

#### `Z80CTC/TimerMode_InterruptGenerated`
- **Ziel:** Timer generiert Interrupt bei Nulldurchgang, wenn IRQ aktiviert und IEI=true.
- **Testschritte:** Steuerbyte 0x85, TC=5, IEI=true; 80 Takte.
- **Erwartet (PASS):** `hasInterrupt() == true`.

#### `Z80CTC/TimerMode_NoInterruptWhenIEILow`
- **Ziel:** Interrupt wird unterdrückt, wenn IEI nicht gesetzt ist.
- **Testschritte:** IEI=false, IRQ aktiviert, 16 Takte.
- **Erwartet (PASS):** `hasInterrupt() == false`.

#### `Z80CTC/CounterMode_ZCTOAfterNPulses`
- **Ziel:** Counter-Modus: ZC/TO nach genau TC externen Flanken.
- **Testschritte:** Steuerbyte 0x55 (Counter, steigende Flanke), TC=5; 4 Flanken – kein Puls; 5. Flanke.
- **Erwartet (PASS):** `fired == 1` nach der 5. Flanke.

#### `Z80CTC/CounterMode_FallingEdgeIgnoredWhenRisingSelected`
- **Ziel:** Im Counter-Modus mit steigender Flanke werden fallende Flanken ignoriert.
- **Testschritte:** 3 fallende Flanken, dann 3 steigende.
- **Erwartet (PASS):** Nach 3 fallenden Flanken: kein Puls; nach 3 steigenden: `fired == 1`.

#### `Z80CTC/CounterMode_FallingEdge`
- **Ziel:** Counter-Modus mit fallender Flanke (D4=0) zählt korrekt.
- **Testschritte:** Steuerbyte 0x45, TC=3; 3 fallende Flanken.
- **Erwartet (PASS):** `fired == 1`.

#### `Z80CTC/ZCTOCallback_ReceivesCorrectChannel`
- **Ziel:** Der ZC/TO-Callback liefert den korrekten Kanal-Index.
- **Testschritte:** Kanal 2 konfigurieren; nach dem Puls Kanal-Index auslesen.
- **Erwartet (PASS):** `cb_channel == 2`.

#### `Z80CTC/ZCTOCallback_NotCalledBeforeFire`
- **Ziel:** Kein vorzeitiger Callback-Aufruf vor dem Nulldurchgang.
- **Testschritte:** 159 Takte (von 160 nötig).
- **Erwartet (PASS):** `called == false`.

#### `Z80CTC/InterruptVector_Calculation`
- **Ziel:** Interrupt-Vektoren werden korrekt als `vec_base | (Kanal << 1)` berechnet.
- **Testschritte:** Vektorbasis 0x20; alle 4 Kanäle konfigurieren und abfragen.
- **Erwartet (PASS):** Ch0→0x20, Ch1→0x22, Ch2→0x24, Ch3→0x26.

#### `Z80CTC/InterruptVector_NoInterruptReturns0xFF`
- **Ziel:** `getVector()` liefert 0xFF, wenn kein Kanal einen Interrupt anfordert.
- **Erwartet (PASS):** `getVector() == 0xFF`.

#### `Z80CTC/DaisyChain_IEOLowWhenPending`
- **Ziel:** IEO=false, wenn ein Kanal einen ausstehenden Interrupt hat.
- **Erwartet (PASS):** `hasInterrupt() == true`, `getIEO() == false`.

#### `Z80CTC/DaisyChain_IEOHighWhenNoPending`
- **Ziel:** IEO gibt IEI durch, wenn kein Interrupt aussteht.
- **Erwartet (PASS):** `hasInterrupt() == false`, `getIEO() == true`.

#### `Z80CTC/DaisyChain_Ch0BlocksCh1`
- **Ziel:** Kanal 0 hat höhere Priorität als Kanal 1; bei gleichzeitigem Interrupt gewinnt Kanal 0.
- **Erwartet (PASS):** `getVector() == 0x20` (Kanal 0).

#### `Z80CTC/DaisyChain_IEILowBlocksAll`
- **Ziel:** IEI=false blockiert alle Interrupt-Anforderungen und setzt IEO=false.
- **Erwartet (PASS):** `hasInterrupt() == false`, `getIEO() == false`.

#### `Z80CTC/IoRead_ReturnsCurrentCount`
- **Ziel:** `ioRead()` liefert den aktuellen Zählerstand in Echtzeit.
- **Testschritte:** TC=10; 0 Takte → 10; 16 Takte → 9; 32 Takte → 8.
- **Erwartet (PASS):** Zähler dekrementiert sich korrekt.

#### `Z80CTC/IoRead_WrapsToZeroForTC256`
- **Ziel:** TC=0 wird als 256 interpretiert; `ioRead()` liefert 0 (uint8_t-Überlauf); `getCount()` liefert 256.
- **Erwartet (PASS):** `ioRead(0) == 0`, `getCount(0) == 256`.

#### `Z80CTC/GetCount_TimerDecrementsCorrectly`
- **Ziel:** `getCount()` gibt den rohen internen Zähler zurück.
- **Erwartet (PASS):** Initial 5; nach 16 Takten: 4.

#### `Z80CTC/IsTimerMode_True`
- **Ziel:** `isTimerMode()` gibt true für einen im Timer-Modus konfigurierten Kanal.
- **Erwartet (PASS):** `isTimerMode(0) == true`.

#### `Z80CTC/IsTimerMode_FalseForCounter`
- **Ziel:** `isTimerMode()` gibt false für Counter-Modus.
- **Erwartet (PASS):** `isTimerMode(0) == false`.

#### `Z80CTC/ControlWord_ResetClearsCounter`
- **Ziel:** Software-Reset (D1=1) setzt den Zähler sofort auf 0.
- **Testschritte:** TC=10, 48 Takte (Zähler = 7); `ioWrite(0, 0x03)`.
- **Erwartet (PASS):** `ioRead(0) == 0`.

#### `Z80CTC/ControlWord_ResetStopsTimer`
- **Ziel:** Nach dem Reset läuft der Timer nicht mehr.
- **Testschritte:** Reset; danach 160 weitere Takte.
- **Erwartet (PASS):** `ioRead(0) == 0` (kein Nachladen).

#### `Z80CTC/ControlWord_ResetClearsPendingInterrupt`
- **Ziel:** Reset löscht einen ausstehenden Interrupt.
- **Testschritte:** IRQ auslösen; `ioWrite(0, 0x03)`.
- **Erwartet (PASS):** `hasInterrupt() == false`.

#### `Z80CTC/MultipleChannels_IndependentTiming`
- **Ziel:** Zwei Kanäle mit unterschiedlichen Zeitkonstanten laufen unabhängig.
- **Testschritte:** Ch0: TC=5 (Periode 80); Ch1: TC=10 (Periode 160); nach 80 und 160 Takten prüfen.
- **Erwartet (PASS):** Nach 80: Ch0=1, Ch1=0; nach 160: Ch0=2, Ch1=1.

#### `Z80CTC/TimerMode_ClkTrgStartsTimer`
- **Ziel:** Im CLK/TRG-Trigger-Modus startet der Timer erst nach einer externen Flanke (One-Shot).
- **Testschritte:** 64 Takte vor dem Trigger – kein Puls; Trigger; 32 weitere Takte.
- **Erwartet (PASS):** Kein Puls vor dem Trigger; genau ein Puls nach 32 Takten.

---

### Floppy-Laufwerk (`test_floppy.cpp`)

Testet die `FloppyDrive`-Klasse, die ein einzelnes physikalisches Diskettenlaufwerk
(mit Steppermotor, Kopfpositionierung und Sektor-I/O) emuliert.

#### `FloppyDrive/MountValidImage`
- **Ziel:** Ein gültiges Disk-Image wird erfolgreich eingebunden.
- **Testschritte:** Temporäres CPA780-Image erstellen; `mount()` aufrufen.
- **Erwartet (PASS):** `isMounted() == true`, `isWriteProtect() == false`.

#### `FloppyDrive/MountNonexistent_Fails`
- **Ziel:** Einbinden eines nicht vorhandenen Images schlägt fehl.
- **Erwartet (PASS):** `mount() == false`, `isMounted() == false`.

#### `FloppyDrive/Unmount`
- **Ziel:** Nach `unmount()` ist das Laufwerk nicht mehr eingebunden.
- **Erwartet (PASS):** `isMounted() == false`.

#### `FloppyDrive/ReadBootSector_CPA780`
- **Ziel:** Lesen von Spur 0, Seite 0, Sektor 1 liefert 128 Bytes (CPA780-Bootspur).
- **Erwartet (PASS):** `data.size() == 128`, `data[0] == 0xE5` (Blank-Markierung).

#### `FloppyDrive/ReadDataSector_CPA780`
- **Ziel:** Lesen eines Datensektors auf Spur 10 liefert 1024 Bytes (CPA780-Datenspur).
- **Erwartet (PASS):** `data.size() == 1024`.

#### `FloppyDrive/ReadSector_NotMounted_Empty`
- **Ziel:** Lesen ohne eingebundenes Image liefert leere Daten.
- **Erwartet (PASS):** `data.empty() == true`.

#### `FloppyDrive/ReadSector_InvalidSectorId_Empty`
- **Ziel:** Ungültige Sektor-ID (außerhalb des Formats) liefert leere Daten.
- **Testschritte:** CPA780 hat max. 26 Sektoren; Sektor 27 anfordern.
- **Erwartet (PASS):** `data.empty() == true`.

#### `FloppyDrive/WriteAndReadBack`
- **Ziel:** Geschriebene Daten können korrekt zurückgelesen werden.
- **Testschritte:** Sektor mit 0xAA füllen; schreiben; zurücklesen.
- **Erwartet (PASS):** `readback[0] == 0xAA`, `readback[511] == 0xAA`.

#### `FloppyDrive/WriteProtect_Blocks`
- **Ziel:** Schreibzugriff auf ein schreibgeschütztes Laufwerk schlägt fehl.
- **Erwartet (PASS):** `writeSector() == false`.

#### `FloppyDrive/SeekAndStep`
- **Ziel:** `seekTrack()` und `step()` verändern die Zylinder-Position korrekt.
- **Testschritte:** Auf Spur 40 seeken; einwärts und auswärts steppen.
- **Erwartet (PASS):** `currentCylinder()` ändert sich entsprechend.

#### `FloppyDrive/ActivitySetAfterRead`
- **Ziel:** Aktivitätsflag wird nach einem Lesezugriff gesetzt und durch `clearActive()` gelöscht.
- **Erwartet (PASS):** Nach Read: `isActive() == true`; nach Clear: `isActive() == false`.

---

### Disk-Format-Parser (`test_format_parser.cpp`)

Testet den `FormatParser`, der Disk-Formate (CPA780, CPA800 und eigene Definitionen)
aus eingebetteten Daten oder Konfigurationsdateien lädt.

#### `FormatParser/BuiltinFormats_NotEmpty`
- **Ziel:** Die eingebetteten Formate sind nicht leer.
- **Erwartet (PASS):** `builtinFormats().empty() == false`.

#### `FormatParser/Builtin_CPA780_Exists`
- **Ziel:** Das Format "cpa780" ist eingebettet und hat zwei Spurformate (Boot + Daten).
- **Erwartet (PASS):** Format gefunden; `tracks.size() == 2`.

#### `FormatParser/CPA780_BootTrackGeometry`
- **Ziel:** Bootspur (Zyl. 0, Seite 0) hat 26 Sektoren à 128 Bytes.
- **Erwartet (PASS):** `secs_per_track == 26`, `bytes_per_sec == 128`.

#### `FormatParser/CPA780_DataTrackGeometry`
- **Ziel:** Datenspur (Zyl. 10, Seite 1) hat 5 Sektoren à 1024 Bytes.
- **Erwartet (PASS):** `secs_per_track == 5`, `bytes_per_sec == 1024`.

#### `FormatParser/CPA800_UniformGeometry`
- **Ziel:** CPA800 hat ein einheitliches Format für alle Spuren (5 × 1024 Bytes).
- **Erwartet (PASS):** `tracks.size() == 1`, 5 Sektoren à 1024 Bytes.

#### `FormatParser/TotalBytes_CPA800`
- **Ziel:** Gesamtgröße wird korrekt berechnet: 80 Zyl. × 2 Seiten × 5 Sekt. × 1024 B = 819200 B.
- **Erwartet (PASS):** `totalBytes() == 819200`.

#### `FormatParser/ParseFile_BasicSyntax`
- **Ziel:** Eine einfache Konfigurationsdatei wird korrekt geparst.
- **Testschritte:** Temporäre Datei mit einem Format und zwei Spurdefinitionen erstellen; parsen.
- **Erwartet (PASS):** Ein Format mit zwei Spurformaten.

#### `FormatParser/ParseFile_MultipleFormats`
- **Ziel:** Mehrere Formate in einer Datei werden korrekt geparst.
- **Erwartet (PASS):** Zwei Formate mit korrekten Namen.

#### `FormatParser/ParseFile_NonExistent_Throws`
- **Ziel:** Parsen einer nicht vorhandenen Datei wirft eine Exception.
- **Erwartet (PASS):** `std::runtime_error` wird geworfen.

---

### ZRE\_K2526 (`test_k2526.cpp`)

Testet die K2526-Zentralrecheneinheits-Karte (ZRE), die ZVE1 (Haupt-Z80), ZVE2 (DMA-CPU),
Boot-ROM, BS-PIO, CTC und die Q240-Speicherschutzhardware enthält.

#### ROM-Initialisierung

##### `K2526/IsRomEnabled_InitialFalse`
- **Ziel:** Nach der Konstruktion (vor `powerOn()`) ist das ROM als aktiviert markiert.
- **Erwartet (PASS):** `isRomEnabled() == true`.

##### `K2526/PowerOn_RomRegistered_MemReadNonFF`
- **Ziel:** Nach `powerOn()` ist das Boot-ROM auf dem Bus registriert.
- **Testschritte:** `attachToBus()`, `powerOn()`, `memRead(0x0000)`.
- **Erwartet (PASS):** Gelesener Wert ≠ 0xFF und == `ZRE_BOOT_ROM[0]`.

##### `K2526/PowerOn_RomData_MatchesArray`
- **Ziel:** Das vollständige Boot-ROM (1 KiB) stimmt mit der Referenz-ROM-Tabelle überein.
- **Erwartet (PASS):** Alle 1024 Bytes stimmen mit `ZRE_BOOT_ROM[]` überein.

##### `K2526/RomWriteIgnored`
- **Ziel:** Schreibzugriffe auf das ROM werden ignoriert.
- **Erwartet (PASS):** Byte an 0x0010 bleibt nach Schreibversuch unverändert.

#### ROM deaktivieren (BS-PIO Port B, Bit 0)

##### `K2526/BSPioPortB_Bit0Low_DisablesRom`
- **Ziel:** Das aktiv-low-Signal /LD-ROM (B0=0) deaktiviert das ROM.
- **Testschritte:** `ioWrite(0x0A, 0x00)` (B0=0).
- **Erwartet (PASS):** `isRomEnabled() == false`; `memRead(0x0000) == 0xFF`.

##### `K2526/BSPioPortB_DisableRom_IsIdempotent`
- **Ziel:** Zweifaches Deaktivieren des ROMs stürzt nicht ab.
- **Erwartet (PASS):** `isRomEnabled() == false`.

##### `K2526/BSPioPortB_Bit0High_RomStaysEnabled`
- **Ziel:** B0=1 lässt das ROM aktiv.
- **Erwartet (PASS):** `isRomEnabled() == true`; `memRead(0x0000) != 0xFF`.

#### MEMDI-Signal (BS-PIO Port A, Bit 7)

##### `K2526/BSPioPortA_Bit7_SetsMEMDI`
- **Ziel:** A7=1 aktiviert das globale MEMDI-Signal des Busses.
- **Testschritte:** `ioWrite(0x08, 0x80)`, dann `ioWrite(0x08, 0x00)`.
- **Erwartet (PASS):** `bus.getMEMDI() == true` / `false`.

#### Takt und I/O-Dispatch

##### `K2526/ClockTick_DoesNotCrash`
- **Ziel:** 1000 Taktzyklen stürzen nicht ab.
- **Erwartet (PASS):** Kein Absturz.

##### `K2526/IODispatch_AllPortsReadable` / `AllPortsWritable`
- **Ziel:** Lese- und Schreibzugriffe auf alle Ports 0x00–0x0F stürzen nicht ab.
- **Erwartet (PASS):** Kein Absturz.

#### ZVE1 (Haupt-CPU)

##### `K2526/CpuReset_SetsPCToZero`
- **Ziel:** `cpuReset()` setzt den Programmzähler auf 0x0000.
- **Erwartet (PASS):** `cpuPC() == 0x0000`.

##### `K2526/CpuIFF1_FalseAfterReset`
- **Ziel:** Nach Reset sind Interrupts deaktiviert.
- **Erwartet (PASS):** `cpuIFF1() == false`.

##### `K2526/CpuStep_WithBootRom_AdvancesPC`
- **Ziel:** Ausführen eines Boot-ROM-Befehls erhöht den PC.
- **Erwartet (PASS):** `cpuStep() > 0`; `cpuPC() > 0`.

#### ZVE2 (DMA-CPU)

##### `K2526/ZVE2_StartsInReset`
- **Ziel:** ZVE2 ist nach der Konstruktion im Reset-Zustand.
- **Erwartet (PASS):** `isZVE2InReset() == true`.

##### `K2526/ZVE2_Port04_Bit1_ReleasesReset`
- **Ziel:** Schreiben von 0x01 auf Port 0x04 (Bit 0 = 1 = /RES-ZVE2 inaktiv) gibt ZVE2 frei.
- **Erwartet (PASS):** `isZVE2InReset() == false`.

##### `K2526/ZVE2_Port04_Bit0_AssertsReset`
- **Ziel:** Bit 0 = 0 auf Port 0x04 setzt ZVE2 zurück in den Reset.
- **Erwartet (PASS):** `isZVE2InReset() == true`.

##### `K2526/ZVE2Step_ExecutesInstructions_WhenNotInReset`
- **Ziel:** ZVE2 führt Befehle aus, wenn es nicht im Reset ist.
- **Erwartet (PASS):** `zve2Step() > 0`; PC wird erhöht.

##### `K2526/ZVE2_Wait_PortB3Low_StallsZVE2`
- **Ziel:** /WAIT-ZVE2 (B3=0) hält ZVE2 an; `zve2Step()` gibt 0 zurück.
- **Erwartet (PASS):** `isZVE2Waiting() == true`; `zve2Step() == 0`.

##### `K2526/ZVE2_Wait_DoesNotAffectZVE1`
- **Ziel:** /WAIT-ZVE2 beeinflusst ZVE1 nicht.
- **Erwartet (PASS):** `cpuStep() > 0` trotz `isZVE2Waiting() == true`.

#### Q240-Speicherschutz (`K2526Protection`-Fixture)

##### `K2526Protection/NoProtection_ReadWriteOk`
- **Ziel:** Ohne eingetragenen Schutz sind Lese-/Schreibzugriffe transparent.
- **Erwartet (PASS):** Kein Verstoß; Datenwert korrekt.

##### `K2526Protection/WriteProtect_BlocksZVE1Write`
- **Ziel:** WP-Flag im Q240 blockiert ZVE1-Schreibzugriff auf das Segment.
- **Erwartet (PASS):** RAM bleibt unverändert; `isSPSViolation() == true`.

##### `K2526Protection/ReadProtect_BlocksZVE1Read`
- **Ziel:** RP-Flag blockiert ZVE1-Lesezugriff; Rückgabe 0xFF.
- **Erwartet (PASS):** `val == 0xFF`; `isSPSViolation() == true`.

##### `K2526Protection/Violation_SetsNMI`
- **Ziel:** Ein Schutzverstoß löst einen NMI auf dem Bus aus.
- **Erwartet (PASS):** `bus.isNMI() == true`.

##### `K2526Protection/RESSPA_ClearsViolation`
- **Ziel:** Schreiben auf Port 0x02 (/RES-SPA) löscht Verstoß und NMI.
- **Erwartet (PASS):** `isSPSViolation() == false`; `bus.isNMI() == false`.

##### `K2526Protection/ZVE2_BypassesProtection`
- **Ziel:** ZVE2 (DMA-CPU) umgeht den Q240-Schutz vollständig.
- **Erwartet (PASS):** ZVE2-Schreib-/Lesezugriff erfolgreich; kein Verstoß.

##### `K2526Protection/SegmentBoundary_64ByteAligned`
- **Ziel:** Segmente sind 64-Byte-ausgerichtet; letztes Byte des Segments ist geschützt, erstes des nächsten nicht.
- **Erwartet (PASS):** 0x003F geschützt; 0x0040 nicht geschützt.

---

### K3526 OPS-Speicherkarte (`test_k3526.cpp`)

Testet die K3526 OPS-Karte (Operativspeicher), die 64 KiB RAM in vier 16-KiB-Gruppen
(Group 0–3) bereitstellt, die individuell deaktiviert werden können.

#### `K3526/ReadWrite_Group0` – `ReadWrite_Group3`
- **Ziel:** Lesen und Schreiben in jeder der vier 16-KiB-Gruppen funktioniert korrekt.
- **Erwartet (PASS):** Zurückgelesener Wert stimmt mit geschriebenem Wert überein.

#### `K3526/InitialState_Zero`
- **Ziel:** Nach der Konstruktion ist der gesamte Speicher auf 0x00 initialisiert.
- **Erwartet (PASS):** Alle Testbytes sind 0x00.

#### `K3526/Fill_AllBytes`
- **Ziel:** `fill()` füllt alle 65536 Bytes mit dem angegebenen Wert.
- **Erwartet (PASS):** Alle Grenzadressen der Gruppen enthalten den Füllwert.

#### `K3526/MemDI_DisabledGroup_ReadReturns0xFF`
- **Ziel:** Deaktivierte Gruppe liefert 0xFF (Bus-Float).
- **Testschritte:** `setMemDI(1, true)`; Lesezugriff auf 0x4000–0x7FFF.
- **Erwartet (PASS):** Alle Adressen der Gruppe liefern 0xFF.

#### `K3526/MemDI_DisabledGroup_WriteIgnored`
- **Ziel:** Schreibzugriffe auf deaktivierte Gruppe werden ignoriert.
- **Erwartet (PASS):** Nach Re-Enable: Wert ist noch 0x00.

#### `K3526/MemDI_DisabledGroup_OtherGroupsUnaffected`
- **Ziel:** Das Deaktivieren einer Gruppe beeinflusst die anderen Gruppen nicht.
- **Erwartet (PASS):** Gruppen 0, 2, 3 lesbar; Gruppe 1 liefert 0xFF.

#### `K3526/GroupBoundary_*`
- **Ziel:** Grenzadressen jeder Gruppe (0x3FFF/0x4000, 0x7FFF/0x8000 usw.) sind korrekt adressierbar.
- **Erwartet (PASS):** Lese-/Schreibzugriffe an Grenzen liefern korrekte Werte.

#### `K3526/AttachToBus_*`
- **Ziel:** Die Karte funktioniert korrekt, wenn sie über den K1520-Bus angesprochen wird.
- **Erwartet (PASS):** Bus-Lese-/Schreibzugriffe, MemDI-Signal und Gruppenabgrenzungen funktionieren.

---

### K5122 AFS-Floppy-Controller (`test_k5122.cpp`)

Testet die K5122 AFS-Karte (Anschaltung Flexible Speicher), den Floppy-Controller
des A5120, der zwei FD-Laufwerke über eine PIO-Schnittstelle und ein DMA-Protokoll ansteuert.

#### Einbinden/Auswerfen

##### `K5122Test/MountValidDisk_DriveBecomesActive`
- **Ziel:** Einbinden eines gültigen Images aktiviert das Laufwerk.
- **Erwartet (PASS):** `isDiskActive(0) == true`.

##### `K5122Test/MountNonexistentFile_ReturnsFalse`
- **Ziel:** Einbinden eines fehlenden Images schlägt fehl.
- **Erwartet (PASS):** `mountDisk() == false`; `isDiskActive(0) == false`.

##### `K5122Test/UnmountDisk_DriveBecomesInactive`
- **Ziel:** `unmountDisk()` deaktiviert das Laufwerk.
- **Erwartet (PASS):** `isDiskActive(0) == false`.

#### Schreibschutz

##### `K5122Test/MountWithWriteProtect_IsWriteProtected`
- **Ziel:** Einbinden mit `write_protect=true` setzt den Schreibschutzstatus.
- **Erwartet (PASS):** `isDiskWriteProtected(0) == true`.

##### `K5122Test/SetWriteProtect_ChangesFlag`
- **Ziel:** `setWriteProtect()` kann den Schreibschutz nachträglich umschalten.
- **Erwartet (PASS):** Flag wechselt entsprechend.

#### Laufwerk-Index-Grenzen

##### `K5122Test/MountDriveOutOfRange_ReturnsFalse`
- **Ziel:** Ungültige Laufwerk-Indizes (-1 oder 4) werden abgelehnt.
- **Erwartet (PASS):** `mountDisk() == false`.

##### `K5122Test/MultipleDrivesMountIndependently`
- **Ziel:** Zwei Laufwerke können unabhängig eingebunden werden.
- **Erwartet (PASS):** Laufwerk 0 und 1 aktiv; 2 und 3 inaktiv.

#### Status-Port B (Laufwerkstatus)

##### `K5122Test/StatusPortB_NotMounted_RDYLInactive`
- **Ziel:** Ohne eingebundenes Image: /RDYL (Bit 0) = 1 (nicht bereit).
- **Erwartet (PASS):** `status & 0x01 != 0`.

##### `K5122Test/StatusPortB_Mounted_RDYLActive`
- **Ziel:** Mit eingebundenem Image: /RDYL = 0 (bereit).
- **Erwartet (PASS):** `status & 0x01 == 0`.

##### `K5122Test/StatusPortB_WriteProtected_WPActive`
- **Ziel:** Schreibgeschütztes Laufwerk: /WP (Bit 5) = 0 (aktiv-low).
- **Erwartet (PASS):** `status & 0x20 == 0`.

##### `K5122Test/StatusPortB_AtTrack0_FWActive`
- **Ziel:** Nach dem Einbinden ist Spur 0 erreicht: /FW (Bit 6) = 0.
- **Erwartet (PASS):** `status & 0x40 == 0`.

#### DMA-Protokoll

##### `K5122Test/DMA_ReadMode_BUSRQAssertedOnSTR`
- **Ziel:** Fallende /STR-Flanke im Lesemodus aktiviert BUSRQ.
- **Erwartet (PASS):** `bus.isBUSRQ() == true`.

##### `K5122Test/DMA_ReadMode_BUSRQReleasedAfterDmaUpdate`
- **Ziel:** `dmaUpdate()` schließt den DMA-Transfer ab und gibt den Bus frei.
- **Erwartet (PASS):** `bus.isBUSRQ() == false`.

##### `K5122Test/DMA_ReadMode_SectorDataAvailableAfterDmaUpdate`
- **Ziel:** Nach `dmaUpdate()` sind Sektordaten über Port 0x16 lesbar.
- **Erwartet (PASS):** Erste zwei Bytes == 0xE5 (Blank-Image).

##### `K5122Test/DMA_Read_SectorImmediatelyAvailableAfterSTR`
- **Ziel:** Im ZVE2-kompatiblen Modus ist der Sektor bereits nach der /STR-Flanke lesbar.
- **Erwartet (PASS):** `ioRead(0x16) == 0xE5` ohne vorheriges `dmaUpdate()`.

##### `K5122Test/DMA_Read_BUSRQAutoReleasedWhenLastByteRead`
- **Ziel:** BUSRQ wird automatisch freigegeben, wenn das letzte Sektorbyte gelesen wird.
- **Erwartet (PASS):** `bus.isBUSRQ() == false` nach dem Auslesen aller Bytes.

##### `K5122Test/DMA_Write_ZVE2Style_CommitViaSeccondSTR`
- **Ziel:** ZVE2-Schreib-DMA: ZVE1 löst BUSRQ aus; ZVE2 füllt den Puffer; zweite /STR-Flanke schreibt den Sektor und gibt BUSRQ frei.
- **Erwartet (PASS):** `bus.isBUSRQ() == false` nach dem ZVE2-Commit.

---

### K7024 ABS-Bildschirmkarte (`test_k7024.cpp`)

Testet die K7024 ABS-Karte (Anschaltung Bildschirm), den Zeichengenerator und Framebuffer
des A5120. Die Karte wandelt VRAM-Zeichencodes in einen 640×288-Pixel-Framebuffer um.

#### `K7024Test/CharA_ProducesNonZeroPixels`
- **Ziel:** Schreiben von 'A' (0x41) in das VRAM erzeugt sichtbare Pixel im Framebuffer.
- **Testschritte:** `memWrite(0xF800, 0x41)`; erste 8×8 Pixel des Framebuffers prüfen.
- **Erwartet (PASS):** Mindestens ein Byte im Framebuffer ist ≠ 0x00.

#### `K7024Test/CursorBit_InvertsRows10And11`
- **Ziel:** Bit 7 (Cursor-Bit) invertiert Pixelzeilen 10 und 11 der Zeichenzelle.
- **Testschritte:** Leerzeichen mit gesetztem Bit 7 schreiben; Zeilen 10–11 prüfen.
- **Erwartet (PASS):** Alle Bytes in Zeilen 10–11 == 0xFF.

#### `K7024Test/FbDirty_SetAfterWrite`
- **Ziel:** Das Dirty-Flag wird nach einem VRAM-Schreibzugriff gesetzt.
- **Erwartet (PASS):** `fbDirty() == true` nach dem Schreiben.

#### `K7024Test/ConsoleMode_PollReturnsWrittenChar`
- **Ziel:** Im Konsolenmodus liefert `pollTextChange()` geschriebene Zeichen mit Position.
- **Testschritte:** `setConsoleMode(true)`; 'A' schreiben; `pollTextChange()` aufrufen.
- **Erwartet (PASS):** `got == true`; `ch == 'A'`; `x == 0`; `y == 0`.

#### `K7024Test/ConsoleMode_ControlCharMappedToSpace`
- **Ziel:** Steuerzeichen (< 0x20) werden als Leerzeichen zurückgegeben.
- **Erwartet (PASS):** `ch == ' '`.

#### `K7024Test/AllSpaceClear_FramebufferAllZeros`
- **Ziel:** Alle VRAM-Zellen mit Leerzeichen füllen erzeugt einen komplett schwarzen Framebuffer.
- **Erwartet (PASS):** Alle 640×288 Framebuffer-Bytes sind 0x00.

#### `K7024Test/FramebufferDimensions`
- **Ziel:** Framebuffer hat die erwarteten Abmessungen (640×288).
- **Erwartet (PASS):** `fbWidth() == 640`; `fbHeight() == 288`; `getFramebuffer() != nullptr`.

---

### K7637-Tastaturcontroller (`test_k7637.cpp`)

Testet die K7637-Tastaturcontroller-Emulation, die Tastendrücke in Scan-Codes umwandelt
und über eine Z80-SIO-Schnittstelle an den Rechner überträgt.

#### Tastenübersetzung

##### `K7637/KeyPress_UppercaseA_Sends_0x41`
- **Ziel:** Taste 'A' sendet ASCII 0x41.
- **Erwartet (PASS):** Ein Byte 0x41 in der SIO-RX-FIFO.

##### `K7637/KeyPress_Ctrl_A_Sends_0x01`
- **Ziel:** Ctrl+A erzeugt Steuerzeichen 0x01.
- **Erwartet (PASS):** Ein Byte 0x01.

##### `K7637/KeyPress_Return_Sends_CR`
- **Ziel:** Return-Taste sendet Carriage Return (0x0D).
- **Erwartet (PASS):** Ein Byte 0x0D.

##### `K7637/KeyPress_Backspace_Sends_BS`
- **Ziel:** Backspace sendet 0x08.
- **Erwartet (PASS):** Ein Byte 0x08.

##### `K7637/KeyPress_Delete_Sends_DEL`
- **Ziel:** Delete sendet 0x7F.
- **Erwartet (PASS):** Ein Byte 0x7F.

##### `K7637/KeyPress_Escape_Sends_ESC`
- **Ziel:** Escape sendet 0x1B.
- **Erwartet (PASS):** Ein Byte 0x1B.

#### Cursortasten

##### `K7637/CursorUp_Sends_0x1E`
- **Ziel:** Cursor-Hoch sendet 0x1E.
- **Erwartet (PASS):** Ein Byte 0x1E.

##### `K7637/CursorDown_Sends_0x1F` / `CursorLeft_Sends_0x1C` / `CursorRight_Sends_0x1D`
- **Ziel:** Cursor-Tasten senden die korrekten A5120-spezifischen Codes.
- **Erwartet (PASS):** Entsprechende Bytes 0x1F, 0x1C, 0x1D.

#### Funktionstasten

##### `K7637/FunctionKey_F1_Sends_0x80`
- **Ziel:** F1 sendet 0x80.
- **Erwartet (PASS):** Ein Byte 0x80.

##### `K7637/FunctionKey_F8_Sends_0x87`
- **Ziel:** F8 sendet 0x87 (F1+7).
- **Erwartet (PASS):** Ein Byte 0x87.

#### Tastenwiederholung

##### `K7637/KeyRepeat_AfterDelay_SendsAgain`
- **Ziel:** Nach 500 ms Haltezeit startet die automatische Wiederholung.
- **Erwartet (PASS):** Mindestens ein weiteres Byte 0x41 nach `tick(500)`.

##### `K7637/KeyRelease_StopsRepeat`
- **Ziel:** `keyRelease()` stoppt die Wiederholung.
- **Erwartet (PASS):** Nach `keyRelease()` und `tick(1000)`: keine weiteren Bytes.

#### TX-Befehle (Tastatur-Reset / LED)

##### `K7637/TxCommand_Reset_ClearsLedState`
- **Ziel:** Befehl 0x00 (Reset) löscht alle Lock-Flags.
- **Erwartet (PASS):** `capsLock() == false`; `scrollLock() == false`; `numLock() == false`.

##### `K7637/TxCommand_LedControl_SetsLockFlags`
- **Ziel:** Befehl 0x52 gefolgt von LED-Byte setzt die Lock-Flags.
- **Erwartet (PASS):** Caps und Num gesetzt; Scroll nicht.

---

### K8025-Peripheriekarte (`test_k8025.cpp`)

Testet die K8025-Karte, die Tastatur (SIO A32), DFÜ-Schnittstelle (SIO A33),
Drucker (SIO A32 Kanal B) und CTC A34 integriert.

#### Tastatur

##### `K8025/KeyboardRxByte_SioA32_ChA_HasInterrupt`
- **Ziel:** Ein injiziertes Tastenbyte löst einen SIO-Interrupt aus.
- **Testschritte:** RX-Interrupt auf SIO A32 Ch A aktivieren; `keyboardRxByte(0x41)`.
- **Erwartet (PASS):** `sioA32().hasInterrupt() == true`.

##### `K8025/KeyboardTxAvailable_FalseInitially`
- **Ziel:** Vor dem Senden durch den Rechner ist kein TX-Byte vorhanden.
- **Erwartet (PASS):** `keyboardTxAvailable() == false`.

#### DFÜ-Schnittstelle

##### `K8025/DfueRxByte_SioA33_ChA_HasRxData`
- **Ziel:** `dfueRxByte()` legt ein Byte in der SIO ab; RR0 Bit 0 wird gesetzt.
- **Erwartet (PASS):** `rr0 & 0x01 != 0`.

##### `K8025/DfueTxAvailable_TrueAfterCpuWrite`
- **Ziel:** Schreibzugriff des CPUs auf SIO A33 Port 0x50 erzeugt ein TX-Byte.
- **Erwartet (PASS):** `dfueTxAvailable() == true`; `dfueTxGet() == 0x55`.

#### Drucker

##### `K8025/PrinterTxAvailable_TrueAfterCpuWrite`
- **Ziel:** Schreiben auf SIO A32 Kanal B (Port 0x5E) erzeugt ein Drucker-TX-Byte.
- **Erwartet (PASS):** `printerTxAvailable() == true`; `printerTxGet() == 0x0D`.

#### Interrupt-Kette

##### `K8025/InterruptChain_KeyboardRxByte_HasInterrupt`
- **Ziel:** Keyboard-Byte löst Interrupt in der gesamten Karte aus.
- **Erwartet (PASS):** `hasInterrupt() == true`.

##### `K8025/InterruptChain_IEO_FalseWhenInterruptPending`
- **Ziel:** IEO wird blockiert, wenn ein Interrupt aussteht.
- **Erwartet (PASS):** `getIEO() == false`.

---

### Z80 PIO (`test_pio.cpp`)

Testet die Z80-PIO-Emulation, die parallele Ein-/Ausgabe in vier Modi
(Output, Input, Bidirektional, Bit-Control) realisiert.

#### Modus 0 (Ausgabe)

##### `Z80PIO/Mode0_CPUWrite_UpdatesOutputLatch`
- **Ziel:** CPU-Schreibzugriff aktualisiert den Ausgabelatch.
- **Erwartet (PASS):** `portARead() == 0x42`.

##### `Z80PIO/Mode0_CPUWrite_FiresCallback`
- **Ziel:** Schreibzugriff löst den Output-Callback aus.
- **Erwartet (PASS):** `received == 0xBE`.

#### Modus 1 (Eingabe)

##### `Z80PIO/Mode1_ExternalWrite_CPUReadsBack`
- **Ziel:** Externes Anlegen eines Wertes ist für die CPU lesbar.
- **Erwartet (PASS):** `ioRead(0) == 0x55`.

##### `Z80PIO/Mode1_CPUWrite_IsIgnored`
- **Ziel:** CPU-Schreibzugriffe im Eingabemodus werden ignoriert.
- **Erwartet (PASS):** Externer Wert bleibt erhalten; Callback nicht aufgerufen.

#### Modus 3 (Bit-Control)

##### `Z80PIO/Mode3_MixedDirection_ReadReturnsCorrectBits`
- **Ziel:** Im Bit-Control-Modus werden Eingabe- und Ausgabebits korrekt gemischt.
- **Testschritte:** Obere 4 Bit = Eingang (0x55), untere 4 = Ausgang (0xAA); lesen.
- **Erwartet (PASS):** `ioRead(0) == 0x5A`.

#### Interrupt-Vektor

##### `Z80PIO/InterruptVector_GetVector_WhenPending`
- **Ziel:** `getVector()` gibt den programmierten Vektor zurück, wenn ein Interrupt aussteht.
- **Erwartet (PASS):** `getVector() == 0xC4`.

#### Daisy-Chain

##### `Z80PIO/DaisyChain_PortA_Pending_Blocks_PortB`
- **Ziel:** Port A blockiert Port B in der internen Daisy-Chain.
- **Erwartet (PASS):** `getVector() == 0x10` (Port A); `getIEO() == false`.

---

### Z80 SIO (`test_sio.cpp`)

Testet die Z80-SIO-Emulation (Serial I/O), die serielle Kommunikation mit
zwei unabhängigen Kanälen (A und B) und einem 3-tiefen RX-FIFO realisiert.

#### RX-Pfad

##### `Z80SIO/RX_ByteSetsBit0_ReadClearsBit0WhenEmpty`
- **Ziel:** Empfang eines Bytes setzt RR0 Bit 0; Lesen löscht es nach dem Leeren des FIFO.
- **Erwartet (PASS):** RR0 Bit 0 gesetzt nach `rxByte()`; gelöscht nach Auslesen.

##### `Z80SIO/RX_FIFO_MultipleBytes`
- **Ziel:** FIFO puffert mehrere Bytes in korrekter Reihenfolge (FIFO).
- **Erwartet (PASS):** Bytes in Eingangsreihenfolge lesbar.

##### `Z80SIO/RX_FIFO_Full`
- **Ziel:** Bei vollem FIFO (3 Bytes) führt ein weiteres Byte zu einem Overrun-Flag in RR1.
- **Erwartet (PASS):** `rr1 & 0x08 != 0` (Overrun-Bit).

#### TX-Pfad

##### `Z80SIO/TX_WriteData_AvailableThenGet`
- **Ziel:** CPU-Schreibzugriff erzeugt ein TX-Byte; `txGet()` liefert es und löscht den Buffer.
- **Erwartet (PASS):** `txAvailable() == true`; `txGet() == 0x55`; danach `false`.

#### Interrupts

##### `Z80SIO/TX_Interrupt_EnabledAfterTxGet`
- **Ziel:** TX-leer-Interrupt wird ausgelöst, nachdem die externe Seite das Byte abgeholt hat.
- **Erwartet (PASS):** `hasInterrupt() == true` nach `txGet()`.

##### `Z80SIO/RX_Interrupt_AllReceivedMode`
- **Ziel:** RX-Interrupt wird bei jedem empfangenen Byte ausgelöst (all-received mode).
- **Erwartet (PASS):** `hasInterrupt() == true` nach `rxByte()`.

##### `Z80SIO/InterruptVector_FromWR2`
- **Ziel:** Interrupt-Vektor wird korrekt aus WR2 abgeleitet mit Statusbits für Kanal und Ursache.
- **Erwartet (PASS):** Vektorbits für Ch A Rx == 0x0C.

#### Daisy-Chain

##### `Z80SIO/IEI_IEO_PassThrough_NoInterrupt`
- **Ziel:** IEO = IEI, wenn kein Interrupt aussteht.
- **Erwartet (PASS):** `getIEO() == true` bei IEI=true.

##### `Z80SIO/IEI_IEO_Blocked_WhenInterruptPending`
- **Ziel:** IEO = false, wenn ein Interrupt aussteht.
- **Erwartet (PASS):** `getIEO() == false`.

---

## Legacy-Testsuite (`test_main.cpp`)

Die Legacy-Testsuite verwendet kein GTest, sondern ein eigenes Makro-Framework
(`TEST` / `END_TEST` / `CHECK`). Sie wird als separate ausführbare Datei gebaut
und dient als Basis-Rauchtest für die grundlegenden Emulatorkomponenten.

### Z80 CPU – Grundbefehle

| Test | Geprüfter Befehl | PASS-Kriterium |
|------|-----------------|----------------|
| NOP | `NOP` | PC=1, 4 Takte |
| LD_A_immediate | `LD A, 42h` | A=0x42, PC=2 |
| LD_BC_immediate | `LD BC, 1234h` | BC=0x1234 |
| ADD_A_B | `ADD A, B` | Summe korrekt, Flags |
| ADD_A_overflow | `ADD A, B` mit Überlauf | A=0, Z- und C-Flag |
| SUB_A_B | `SUB B` | Differenz, N-Flag |
| AND_A / OR_A / XOR_A | Logische Befehle | Korrekte Ergebnisse und Flags |
| INC_DEC | `INC B`, `DEC B` | Korrekte Werte, Z-Flag |
| JP / JR / JR_backward | Sprungbefehle | PC auf korrekter Zieladresse |
| CALL_RET | `CALL` und `RET` | PC, SP korrekt |
| PUSH_POP | `PUSH BC`, `POP BC` | Stack korrekt |
| DJNZ | `DJNZ -2` (Loop) | B dekrementiert, Schleife |

### Z80 CPU – Erweiterte Befehle

| Test | Befehl | PASS-Kriterium |
|------|--------|----------------|
| LDIR | `LDIR` | 5 Bytes korrekt kopiert |
| CPIR | `CPIR` | Byte 'B' gefunden, Z-Flag |
| IM2 | `IM 2` | IM=2 |
| NEG | `NEG` | Zweierkomplement korrekt |

### Z80 CPU – CB-Präfix

| Test | Befehl | PASS-Kriterium |
|------|--------|----------------|
| RLC_A | `RLC A` | Rotation, C-Flag |
| BIT_7_A | `BIT 7, A` | Z-Flag klar |
| SET_3_A | `SET 3, A` | Bit 3 gesetzt |
| SRL_A | `SRL A` | Rechts-Shift, C-Flag |

### Z80 CPU – IX-Präfix

| Test | Befehl | PASS-Kriterium |
|------|--------|----------------|
| LD_IX_immediate | `LD IX, 1234h` | IX=0x1234 |
| LD_A_IX_d | `LD A, (IX+5)` | A=0x42 |

### Speicher

| Test | Funktion | PASS-Kriterium |
|------|----------|----------------|
| read_write | Lesen/Schreiben an 0x0000 und 0xFFFF | Korrekte Rückgabe |
| clear | `clear()` | Alle Bytes 0x00 |
| fill | `fill(start, end, val)` | Bereich gefüllt |
| screen_buffer | Bildschirmpuffer-Zugriff | `getScreenChar()` korrekt |

### Diskette (Legacy)

| Test | Funktion | PASS-Kriterium |
|------|----------|----------------|
| create_blank | Blank-Image erstellen | `isLoaded()`, Größe 819200 |
| read_write_sector | Sektor schreiben/lesen | Daten stimmen überein |
| blank_sector_content | Leersektor lesen | Wert 0xE5 |
| save_load | Image speichern und neu laden | Daten erhalten |

### Integration

| Test | Szenario | PASS-Kriterium |
|------|----------|----------------|
| simple_program | Summe 1..10 | A=55, HALT |
| memory_copy_program | LDIR-Kopierprogramm | "HELLO" korrekt kopiert |
| conditional_jump | JR Z springt korrekt | A=0x00 (Sprung genommen) |
