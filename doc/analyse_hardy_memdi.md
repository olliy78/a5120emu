# HARDY-Hardwaretest: „MEMDI aktiv: RDY bei:"-Freeze — Analyse & Fix

HARDY (V.3/1, Humboldt-Universität 1987) ist ein K1520-/A5120-Hardware-Testprogramm.
Auf `disks/scpx_5x1024_hardy.hfe` (SCPX 1526 V1.7, Geometrie 5×1024) bootet es
vollautomatisch bis `A>`; nach `HARDY⏎` bestätigt man zwei Dialoge mit der Leertaste,
dann folgt die Testauswahl. Der erste Test (`1 - Rechner`) **fror auf dem Emulator beim
Schritt „MEMDI aktiv: RDY bei:" ein** (auf echter HW läuft er durch). Ursache und Fix:

## 1. Der Freeze: /MEMDI sperrte den Befehls-Fetch → RST-38-Endlosschleife

Der MEMDI-Test ruft `sub_06A9` auf:
```
06A9  LD C,08H     ; BS-PIO Port A (Daten)
06AB  IN A,(C)
06AD  SET 7,A      ; Bit7 = /MEMDI setzen
06AF  OUT (C),A    ; → bus_.setMEMDI(true)
06B1  EI           ; ← dieser Fetch …
06B2  RET          ;   … und dieser laufen NACH dem Setzen von /MEMDI
```
Der Emulator sperrte bei gesetztem `/MEMDI` **alle** Speicherzugriffe (Read+Write → 0xFF).
Damit lieferte der Fetch von `EI` (0x06B1) `0xFF` = Opcode `RST 38H` → Sprung nach
`0x0038` → dort wieder `0xFF` → **Endlos-RST-38-Schleife**. Das war das Einfrieren.

## 2. /MEMDI ist die „Speicherbereichsumschaltung" — auf dem Standard-A5120 wirkungslos

Auf echter HW läuft der Code nach dem `OUT` weiter (`EI; RET`), und HARDYs RDY-Test
(`sub_0915`) lässt `/MEMDI` sogar **gesetzt** zurück und führt danach Stack-Operationen
(`PUSH/POP/CALL`) und BDOS-Ausgaben aus. Also darf `/MEMDI` weder Fetch/Read NOCH den
Stack/Normalspeicher sperren.

`/MEMDI1`/`/MEMDI2` (BS-PIO Q301 Port A Bit7 → Backplane) sind die
*Speicherbereichsumschaltung*: sie schalten nur OPS-Gruppen ab, die per Jumper auf
MEMDI1/2 verdrahtet sind. Auf dem modellierten Standard-A5120 ist **keine** Gruppe darauf
verdrahtet → `/MEMDI` hat **keinen** Speichereffekt.

**Fix** (`core/bus/k1520_bus.cpp`): das globale `memdi_` gatet weder `memRead` noch
`memWrite`. Das Flag wird weiter getrieben/abgefragt (`getMEMDI`), damit HARDY es über
Port A zurücklesen kann. HARDYs Gruppen-Readout zeigt daraufhin korrekt `[ 0 ]` für alle
vier 16K-Gruppen (= „von keinem MEMDI gesteuert" — der Normalzustand ohne Bank-Switching;
`[ - ]` wäre der Fehlerfall „Speicher defekt"). Eine per Jumper geschaltete Gruppe würde
über `K3526::setMemDI` abgebildet, nicht über dieses globale Signal.

## 3. „System - PIO" und „RDY bei:" — dynamische Bus-Strobes auf BS-PIO Port A

Zwei weitere Rechner-Untertests meldeten Fehler, weil Bus-Strobes am BS-PIO Port A nicht
modelliert waren (Port-A-Belegung s. `core/cards/k2526/k2526.h`):

- **System-PIO-Test** (`0x07F0`): scharf über Maske `0xFE` → überwacht **A0 = /M1**
  (aktiv-LOW). `/M1` pulst bei jedem Opcode-Fetch; der PIO-Selbsttest erwartet daher
  garantiert einen Interrupt („ok"). Ohne /M1-Quelle: „System - PIO kein INTERRUPT !".
- **MEMDI-RDY-Test** (`sub_0915`/`sub_09D7`): scharf über Maske `0x9F` → überwacht
  **A5 = /WR** und **A6 = /RDY** (aktiv-LOW, AND). Ohne diese Strobes bleibt „RDY bei:"
  leer; mit ihnen meldet der Test RDY über den vollen Adressraum: **„RDY bei: 00-FF"**.

**Fix** (`core/cards/k2526/k2526.cpp`): die Power-on-Port-A-Eingänge setzen A0/A5/A6 aktiv
(`port_a_inputs_ = 0xFF & ~0x61`) — im laufenden System liegen diese Strobes praktisch
dauernd aktiv an, und der PIO tastet sie pegelbasiert ab. Sie sind ausschließlich
Interruptquellen (HARDY liest Port A sonst nur in der MEMDI-Toggle-Routine, wo dank
Richtungsmaske `0x7F` nur Bit7 = Ausgang zählt).

Zusätzlich wertet der Z80PIO die Mode-3-Interruptbedingung jetzt **beim Scharfmachen**
pegelbasiert neu aus (`core/primitives/z80_pio.cpp`, `writeCtrl`) — der reale U855 meldet
sofort, wenn der aktuelle Eingangspegel die Maske erfüllt, nicht erst bei der nächsten
Pegeländerung.

## 4. Ergebnis

Der Rechner-Test läuft komplett durch und meldet **keine Fehler**:
```
System - CTC   ok
System - PIO   ok
keine    OSS
MEMDI1 aktiv:  RDY  bei:   00-FF
MEMDI2 aktiv:  RDY  bei:   00-FF
Gruppe:    0000-3FFF  4000-7FFF  8000-BFFF  C000-FFFF
MEMDI        [ 0 ]      [ 0 ]      [ 0 ]      [ 0 ]
```
Danach kehrt die Leertaste zurück ins Testmenü (HARDYs Dialoge/Untermenüs lesen die
Tastatur per **Direkt-Poll** der Tastatur-SIO, Ports 0x5C/0x5D — kein BDOS-Puffer; eine
Taste greift nur, wenn HARDY im Moment des Anschlags pollt, daher im Guard-Test das
Nachdrücken bis zum Folge-Screen).

Guard: `tests/cpp/test_hardy.cpp` → `Hardy.RechnerTestRunsCleanWithoutFreezing`
(Label `format_integration`; `tools/dev.sh test-format`). Zeitlage-Golden `cli_dbg_json`
(CMakeLists) wurde an das minimal verschobene CP/A-Boot-Timing angepasst (1E52/231A).
