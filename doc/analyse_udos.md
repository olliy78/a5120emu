# UDOS bootet nicht — Fehlermeldung + zerschriebener Bildschirm: Analyse

**Stand:** 2026-08-03. Branch `boot_udos`, Fixture `disks/udos_boot_scp.hfe`
(HFE v1, 80 Spuren, 2 Seiten, 249 kbit/s — von echter Hardware eingelesen, s.
[[project_real_disk_hfe_readpath]] bzw. §9).

**UDOS** ist ein **nicht CP/M-basiertes** Betriebssystem (Zilog-RIO-Abkömmling; das
geladene Systemabbild trägt die Kennung `ACTIVATE: 790705 COPYRIGHT, ZILOG, INC.
1978,1979`). Auf einem echten A5120 bootet die Diskette sauber; im Emulator erschien
zuerst eine Fehlermeldung und danach wurde der Bildschirm mit wirren Zeichen
überschrieben.

> ## ✅ Ursache gefunden und behoben (§5/§6) — Rest-Punkt offen (§8)
> Die Fehlermeldung ist der **UDOS-Debugger** (`DEBUBC43`), der über einen
> **Pseudo-Interrupt** angesprungen wird. Auslöser ist ein Emulator-Fehler im
> **Z80-PIO-Modell**: `Z80PIO::hasInterrupt()` prüfte das **IUS**-Bit nicht, während
> `Z80PIO::getVector()` es prüft. Sobald ein Port ein neues `pending` hat, während
> sein eigenes IUS noch gesetzt ist (kein `RETI` erfolgt), zieht die Karte `/INT`, die
> Quittung findet aber **keinen vektorfähigen Port** und liefert den Fallback `0xFF`
> → IM-2-Sprung ins Leere → `RST 38H` → Debugger. Weil nichts das `pending` löscht,
> ist der Zustand **selbsterhaltend**: ein **Interrupt-Sturm** (4174 Pseudo-Interrupts
> im gemessenen Fenster). **Ein-Zeilen-Fix** in `core/primitives/z80_pio.cpp`
> (dieselbe `!ius`-Bedingung wie in `getVector()`) — 608/608 ctest + 58/58
> Legacy-Harness + 8/8 `format_integration` grün. Danach lädt UDOS seine Systemspuren
> vollständig (`BOOT FO UDOS V…`-Kennung ab `0x4000`). **Offen** bleibt ein einzelner,
> *korrekt vektorisierter* Index-Interrupt (§8.1).

---

## 0. Stand & Einstieg für die nächste Session

**Branch `boot_udos`**, gemergt mit `origin/main` (YAML-Formatkatalog, Laufwerksprofil-
Umbenennungen, Reset/Power-Cycle, GUI). Testlage nach Komplett-Neubau:
**662/662 ctest + 58/58 Legacy-Harness + 8/8 `format_integration`**.

**Erledigt:** Der Pseudo-Interrupt-Sturm (§4/§5) ist gefixt und regressionsfrei. UDOS
durchläuft jetzt die komplette Ladekette bis zum geladenen Systemabbild.

**Offen:** §8.1 (ein Index-Interrupt `0xBA` ohne IM-2-Eintrag) und §8.2 (DMA in den
Bildschirmspeicher). UDOS erreicht **noch keinen** Bedienzustand.

### Reststand in einem Kommando reproduzieren

```sh
tools/dev.sh trace disks/udos_boot_scp.hfe -c 45000000 -p 45000000 \
    --itrace /tmp/u.csv -L /dev/null --quiet --json
cut -d, -f5 /tmp/u.csv | sort | uniq -c      # 7× 0x01C7, 22× 0x0A0A, 62× 0x007A, 1× 0xFFFF
```

Das eine `0xFFFF` ist der Rest-Fehlschlag: Takt **13 027 481**, unterbrochener PC
`0x071D`, Vektor **`0xBA`** (K5122-Indexpuls), Tabelleneintrag `[0x0FBA]` = `FFFF`.
Diese Werte sind **vor und nach dem main-Merge identisch**.

### Was bereits widerlegt oder verworfen ist (nicht erneut probieren)

| Idee | Ergebnis |
|---|---|
| NMI / Q240-Schutzverletzung als Auslöser | ❌ `bnmi` bleibt stumm — es ist der `RST 38H`-Pfad |
| Lesepfad / Diskettenfehler | ❌ `disk verify`: 160 Spuren, 4057 Sektoren, **0 CRC-Fehler** |
| `pending` beim Sperren der IE löschen | ✅ **ist inzwischen in `main`** (`z80_pio.cpp:127/139`) — ändert am Reststand **nichts** |
| IE-Zustand beim DMA-Rundenstart merken und wiederherstellen | ❌ wirkungslos (der Löscher *ist* ZVE2) |
| Restore nur, wenn die Sperre von ZVE2 kam (I/O-Urheber-Flag am Bus) | ❌ wirkungslos, gleiche Begründung |
| `ctrl_pio_.ioWrite(1, 0x83)` in `endDmaTransfer()` ersatzlos streichen | ⚠ UDOS kommt am BREAK vorbei, der Lesevorgang wird aber **nie fertig** (`0x0719/0x071D`: 11 838 → 94 770 Treffer) |
| K7024 verhalte sich unrealistisch wie RAM | ❌ falsch — Lesesperre ist korrekt modelliert, s. §8.2 |

### Nächste konkrete Schritte

1. **Wer setzt das Fertig-Bit?** Der Treiber wartet auf `BIT 7,(IY+0AH)` = `[0x0E93]`.
   `--watch 0x0E93` zeigt, welche ISR es setzt. Das ist der Schlüssel zu der Frage,
   warum der Lesevorgang ohne den `0x83`-Restore nie fertig wird — und damit dazu, ob
   dieser Emulator-Eingriff überhaupt entfallen kann.
2. **Wird `[0x0FBA]` jemals gefüllt?** `--watch 0x0FBA,0x0FBB` über einen langen Lauf.
   Falls ja, kommt unser Indexpuls schlicht zu früh (Timing statt Logik).
3. **Vektor `0xE8` gegenprüfen:** Im residenten System (`0x0000–0x0BFF`) und in
   `NDOS.MAC` nach einem Schreiben von `0xE8` auf Port `0x11` suchen. Erwartet wird er
   laut `UNFLOPPY.MAC` und unserem eigenen `k5122.cpp`-Kommentar (*ivdsk1*).
4. **Speicherausbau (§8.2):** Ermitteln, ob UDOS die Obergrenze selbst misst oder vom
   Lade-ROM übernimmt, und was dabei herauskommt.

> **Werkzeug-Warnung:** `k1520dbg` ist auf dieser Disk ab ca. 15 Mio. Takten praktisch
> unbenutzbar (`g 15000000` = 0,3 s, `g 15500000` > 90 s) — Ursache und Messwerte in
> §10 bzw. `tools/feature_request_source_annotation.md` §7. Für lange Läufe
> `boot_trace` nehmen, `k1520dbg` nur mit Breakpoint.

---

## 1. Symptom & Reproduktion

Kurz nach dem Start erscheint oben links ein Registerauszug, danach füllt sich der
Bildschirm mit Code-/Datenmüll.

**Reproduktion** (Copy-on-Write ist Default — das Fixture kann nicht beschädigt werden):

```sh
tools/dev.sh trace disks/udos_boot_scp.hfe -c 14000000 -p 14000000 -L /dev/null
```

Der VRAM-Textdump am Ende des Reports zeigt (vor dem Fix):

```
|BREAK FFFF FF C3 3E 0A .........................................................|
| A  B  C  D  E  F  H  L  I  IX   IY   PC   SP  .................................|
|14 14 B7 0B C5 5C 08 17 0F 0000 0E89 FFFF 0CFA .................................|
|77 31 31 31 31 61 0B E5 ........................................................|
|+...............................................................................|
```

Ab ca. 15 Mio. Takten ist der Bildschirm mit Bruchstücken des UDOS-Systemabbilds
überschrieben (u. a. lesbar: `CONIN.conin  CONOUT.conout  SYSLST.syslst` und die
Zilog-Kennung).

---

## 2. Was die Fehlermeldung ist

Die Meldung stammt **nicht** vom Emulator, sondern aus UDOS selbst. Die
kommentierten Originalquellen liegen unter `~/projects/UDOS/UDOS_/` (Varianten
`…BC43` = **B**üro**c**omputer A5120, daneben `PC1715/`):

| Datei | Rolle | Ladeadresse |
|-------|-------|-------------|
| `SYL0BC43.MAC` / `SYL1BC43.MAC` | Systemlader Spur 0 / Spur 1 | ab `0x0400` |
| `POSIBC43.MAC` (= `UDBC43PO.MAC`) | POSINIT — Hardware-Erkennung | `.PHASE 1000H` |
| `DEBUBC43.MAC` | **Debugger/Monitor** (residentes System) | `ORG 0` |
| `UNFLOPPY.MAC`, `NDOS.MAC` | Floppytreiber, NDOS | — |

In `DEBUBC43.MAC` steht die Textkonstante:

```asm
	DB	6,'BREAK '
	DB	7,'ERROR: '
	DB	"A'B'C'D'E'F'H'L'"
	DB	'A B C D E F H L '
	DB	'I IXIYPCSPBR'
```

Die Kopfzeile des Screendumps (`A B C D E F H L I IX IY PC SP`) ist exakt diese
Tabelle. `IY=0E89` im Dump ist der Registerrettungsbereich des Debuggers, den
`DEBUBC43` selbst mit `LD IY,0E89H` setzt. **Die „Fehlermeldung" ist also ein
Breakpoint-/Trap-Report des UDOS-Monitors** — UDOS ist in seinen Debugger gefallen.

### 2.1 Die Trap-Einsprünge

Aus `DEBUBC43.MAC` (ab `ORG 0`) durchgezählt ergeben sich zwei bekannte Adressen:

```
0024:  DI / LD (0EBC),SP / LD SP,0EB8 / PUSH IY,IX,HL,DE,BC,AF …   ← Trap-Eintritt
0038:  PUSH HL / LD HL,(0EEC) / EX (SP),HL / RET     ← RST 38H  → springt auf 0x0024
0066:  PUSH HL / LD HL,(0EEE) / EX (SP),HL / RET     ← NMI      → springt auf 0x0024
```

`0x0EEC` und `0x0EEE` werden in der Debugger-Initialisierung beide auf `0x0024`
gesetzt (`LD HL,0024H / LD (0EEC),HL / LD (0EEE),HL`).

Der Debugger berechnet die gemeldete Adresse als *Rücksprungadresse − 1*
(`LD HL,(0EBC) … LD E,(HL) / INC HL / LD D,(HL) / DEC DE / LD (0EBA),DE`) und druckt
vier Bytes ab dort. Der Dump `BREAK FFFF FF C3 3E 0A` heißt also: **die CPU war bei
`0xFFFF`**, dort steht `0xFF`, und ab `0x0000` folgt `C3 3E 0A` (`JP 0A3EH`).

**`bnmi` bleibt stumm — es ist kein NMI (keine Q240-Schutzverletzung), sondern der
`RST 38H`-Pfad.**

---

## 3. Wo der Sprung ins Leere herkommt

`k1520dbg` mit Breakpoint auf dem `RST 38H`-Stub liefert die exakte Vorgeschichte:

```sh
printf 'b 0x0038\ng 15000000\nr\nbt\nq\n' | tools/dev.sh tool k1520dbg disks/udos_boot_scp.hfe
```

```
** bp ZVE1 : ZVE1 PC=0038
  ZVE1 PC=0038 SP=0CF8(->0000) AF=145C BC=14B7 DE=0BC5 HL=0817 IY=0E89 I=0F  cyc=12821456
  #0 0038
  #1 FFFF (call → 0038, ret 0000)     ← 0xFF an 0xFFFF ausgeführt = RST 38H
  #2 03B3 (call → 0BFD, ret 03B6)     ← Systemaufruf „Betriebssystem laden"
  #3 10EC (call → 0000, ret 10ED)     ← RST 00H am Ende von POSINIT
  #4 0174 (call → 0437, ret 0177)     ← Boot-ROM → SYL-Lader
```

Die Boot-Kette selbst ist also **intakt**: Boot-ROM → SYL-Lader → POSINIT
(`0x1000`, endet mit `RST 00H`) → residenter Debugger/System ab `0x0000`.

Der Debugger startet mit `LD A,4FH` (`'O'`) und führt dieses Kommando automatisch
aus — das ist „**O**S laden". Der Live-Disassembler zeigt an `0x0395` exakt den
Quelltext von `DEBUBC43.MAC` (Label `M03F8`), nur um wenige Bytes verschoben:

```
0398: FE 4F          CP 4FH             ; Kommando 'O'
039C: 21 00 40       LD HL,4000H        ; Ladeadresse
039F: 22 8B 0E       LD (0E8BH),HL
03A2: 21 BC 02       LD HL,02BCH        ; Länge
03A5: 22 8D 0E       LD (0E8DH),HL
03A8: 21 10 15       LD HL,1510H        ; Spur/Sektor
03AB: 3A DC 0E       LD A,(0EDCH)       ; Laufwerksnummer (aus POSINIT)
03B3: CD FD 0B       CALL 0BFDH         ; → JP 0700H  = Floppytreiber
03B6: B7             OR A
03B7: 2A 02 40       LD HL,(4002H)
03BA: 11 42 4F       LD DE,4F42H        ; Signatur "BO"
03BD: ED 52          SBC HL,DE
03BF: CA 00 40       JP Z,4000H         ; passt → Betriebssystem starten
```

**Der Absturz passiert mitten in diesem Diskettenladen.** Der Instruktionstrace
(`trace`-Kommando in `k1520dbg`) zeigt den Moment unmissverständlich:

```
T1 c12821406  0719: FD CB 0A 7E    BIT 7,(IY+0AH)   … SP=0CFC
T1 c12821445  FFFF: FF             RST 38H          … SP=0CFA     ← SP−2!
T1 c12821456  0038: E5             PUSH HL
```

ZVE1 wartet in der Schleife `0719/071D` (`BIT 7,(IY+0AH) / JR Z`) auf das
Fertig-Bit des Treibers, während ZVE2 per `INIR` (Port `0x16`) die Sektordaten
holt. Zwischen `0x0719` und dem nächsten Befehl **fehlt** das `JR` und der Stack ist
um 2 gewachsen: Das ist kein ausgeführtes `0xFF`, sondern eine **angenommene
Interrupt-Anforderung**.

### 3.1 Die IM-2-Rechnung

UDOS läuft in **Interrupt Mode 2** mit `I=0x0F`, die Vektortabelle liegt also auf
Seite `0x0F00`. Der Z80 bildet `addr = (I<<8) | (vector & 0xFE)`:

| Vektor | Tabelleneintrag | Inhalt | Folge |
|--------|-----------------|--------|-------|
| `0xE6` | `0x0FE6` | `0A0A` | System-CTC Kanal 3 → **gültige ISR** |
| `0xBA` | `0x0FBA` | `FFFF` | K5122-Index → Sprung nach `0xFFFF` |
| `0xFF` | `0x0FFE` | `FFFF` | **Fallback „kein Gerät"** → `0xFFFF` |

`0xFFFF` liegt im **K7024-VRAM** (`0xF800–0xFFFF`), das bewusst mit `0xFF`
initialisiert wird (`k7024.cpp:83` — das Boot-ROM braucht das für `RET M`). Dort
steht also `0xFF` = `RST 38H` → Debugger. **Ein Sprung ins Leere landet damit
zuverlässig im UDOS-Monitor.**

---

## 4. Der Interrupt-Sturm

`boot_trace` protokolliert jeden quittierten Vektor (`K1520Bus::interruptAcknowledge`,
Log-Level `debug`):

```sh
tools/dev.sh trace disks/udos_boot_scp.hfe -c 16000000 -p 16000000 \
    --log-cycle 11000000:16000000:debug -L /tmp/udos.log --quiet --json
rg -o "INT-Quittung: Vektor=0x[0-9A-F]+" /tmp/udos.log | sort | uniq -c
```

**Vor dem Fix:**

```
      1 INT-Quittung: Vektor=0xBA      ← K5122 Index (legitim)
     22 INT-Quittung: Vektor=0xE6      ← System-CTC Kanal 3 (legitim)
   4174 INT-Quittung: Vektor=0xFF      ← Pseudo-Vektor!
```

Passend dazu im PC-Histogramm: `0x0038` und `0x0039` (der `RST 38H`-Stub) mit je
**4176** Treffern. Der Bildschirm wird nicht „irgendwie" zerstört — der Rechner tut
nichts anderes mehr, als tausendfach in den Debugger zu fallen, während ZVE2
weiterhin Sektordaten per DMA ablegt.

Ein `0xFF` entsteht in `K1520Bus::interruptAcknowledge()` nur, wenn **kein** Gerät
der Daisy-Chain `hasInterrupt()` meldet:

```cpp
uint8_t K1520Bus::interruptAcknowledge() {
    int_dirty_ = true;
    for (auto* dev : int_chain_)
        if (dev->hasInterrupt()) { … return dev->getVector(); }
    return 0xFF;                    // ← „kein Gerät"
}
```

…doch die Chain hatte `/INT` unmittelbar davor selbst gesetzt (`INT: inaktiv →
aktiv`). Der Widerspruch ist real: **die Karte fordert an, kann aber keinen Vektor
liefern.** Instrumentiert man `K5122::getVector()`, ist der Schuldige eindeutig
(`chain[0]` = K5122):

```
   4174 DIAG getVector: ctrl_pio → 0xFF
```

und der Portzustand beim Fallback:

```
4175 DIAG Fallback 0xFF [K5122-CTRL]:
       A(ie=1 pend=1 ius=1 iei=1 vec=BA mode=0)
       B(ie=0 pend=0 ius=0 iei=0 vec=FF mode=3)
```

---

## 5. Ursache: `hasInterrupt()` ignoriert IUS, `getVector()` nicht

Die beiden Methoden im Z80-PIO-Modell waren **nicht deckungsgleich**:

```cpp
bool Z80PIO::hasInterrupt() const {                    // ── Anforderung
    return (porta_.iei && porta_.pending) || (portb_.iei && portb_.pending);
}                                                      //   ^ kein !ius

uint8_t Z80PIO::getVector() const {                    // ── Quittung
    if (porta_.iei && porta_.pending && !porta_.ius) { … return porta_.int_vector; }
    if (portb_.iei && portb_.pending && !portb_.ius) { … return portb_.int_vector; }
    return 0xFF;                                       //   ^ !ius wird geprüft
}
```

Sobald ein Port `pending=1` **und** `ius=1` hat, gilt:

1. `hasInterrupt()` → `true` → der Bus zieht `/INT`, der Prozessor nimmt an.
2. `getVector()` überspringt den Port (`!ius` schlägt fehl) → Fallback `0xFF`.
3. `getVector()` löscht `pending` in diesem Zweig **nicht** → Bedingung bleibt.

→ **Ein Pseudo-Interrupt pro Instruktionsgrenze, endlos.**

Wie kommt `pending && ius` zustande? `ius` wird in `getVector()` gesetzt und nur von
`onRETI()` gelöscht. UDOS' Index-ISR-Slot (`0x0FBA`) ist leer — der erste, *korrekt*
mit `0xBA` vektorisierte Index-Interrupt springt nach `0xFFFF` in den Debugger, und
der kehrt **nie mit `RETI` zurück**. `ius` bleibt für immer gesetzt; der nächste
Indexpuls setzt `pending` erneut — und der Sturm beginnt.

> **Dieselbe Fehlerklasse gab es schon einmal:** `Z80CTC::hasInterrupt()` nutzte das
> rohe `anyPending()` statt der `iei`/`ius`-gegateten `anyServiceable()` und
> erzeugte einen Spurious-`0xFF`-Sturm, der die CP/A-Uhr ~1100× zu schnell laufen
> ließ. Im PIO war die Korrektur nie nachgezogen worden.

### 5.1 Der Fix

`core/primitives/z80_pio.cpp` — die Anforderung bekommt exakt die Bedingung der
Quittung:

```cpp
bool Z80PIO::hasInterrupt() const {
    // WICHTIG: dieselbe Bedingung wie getVector() — inklusive `!ius`. …
    return (porta_.iei && porta_.pending && !porta_.ius) ||
           (portb_.iei && portb_.pending && !portb_.ius);
}
```

Damit können Anforderung und Quittung nicht mehr auseinanderlaufen; ein Port, der
bedient wird, hält seinen `/INT` zurück, bis das `RETI` sein `ius` löscht — genau
das, was die Daisy-Chain modellieren soll. Die Port-B-Priorisierung bleibt korrekt,
weil `setIEI()` `portb_.iei` ohnehin schon über `porta_.pending || porta_.ius`
gatet.

---

## 6. Wirkung des Fixes

| Messgröße (45 Mio. Takte) | vorher | nachher |
|---|---|---|
| quittierte Pseudo-Vektoren `0xFF` | 4174 | **0** |
| Treffer im `RST 38H`-Stub (`0x0038`) | 4176 | 1 |
| angenommene Interrupts gesamt | Sturm | 30 (7× `0x01C7`, 22× `0x0A0A`, 1× Fehlschlag) |
| Systemabbild ab `0x4000` | nie geladen | `18 19 "BOOT FO UDOS V…"` — **Signatur `BO` an `0x4002` stimmt** |

Der Ladepfad läuft jetzt durch: Boot-ROM → SYL-Lader → POSINIT → Debugger-Autostart
`'O'` → Floppytreiber `0x0700` → Systemabbild geladen. Im PC-Histogramm taucht der
UDOS-Treiber (`0x0616–0x0678`, `0x0BE8`) mit je ~13 000 Durchläufen auf, wo vorher
nur der Debugger-Trap stand.

**Regression:** `tools/dev.sh test` → **608/608 ctest + 58/58 Legacy-Harness**;
`tools/dev.sh test-format` → **8/8** (inkl. `bootdisk_cpa780`, `mf3200_fmt7`,
`mf6400_fmt1`, `k5600_10/20`, `ScpxInit.*`, `Hardy.*`). Kein Testfall verändert sich.

---

## 7. Wie UDOS die Hardware benutzt (Referenz)

Nützlich für weitere Arbeiten — alles aus `POSIBC43.MAC` + Live-Trace bestätigt.

### 7.1 POSINIT (`0x1000`)

```asm
	LD	SP,0D00H
	LD	A,0FFH / OUT (13H),A    ; K5122 Steuer-PIO Port B: Modus 3
	LD	A,0F3H / OUT (13H),A    ;   Richtungsmaske
	LD	A,7FH  / OUT (17H),A    ; K5122 Daten-PIO Port B: Modus 1 (Eingabe)
	LD	A,3FH  / OUT (11H),A    ; K5122 Steuer-PIO Port A: Modus 0 (Ausgabe)
	         OUT (15H),A        ; K5122 Daten-PIO Port A: Modus 0
	LD	A,0E0H / OUT (0CH),A    ; System-CTC
	LD	A,37H  / OUT (0EH),A
	LD	A,0C0H / OUT (0EH),A
	…
	LD	BC,0C00H / LD DE,0 / LD HL,1100H / LDIR   ; System 1100→0000 (3 KB)
```

- **Laufwerkserkennung** (`0x105F`–`0x108F`): für 4 Laufwerke (`OUT (18H)` mit
  rotierendem `77→EE→DD→BB`) je bis zu `4EH`=78 Step-Impulse (`OUT (10H),5FH/0DFH`)
  und Prüfung von Bit 7 aus `IN A,(12H)`. Ergebnis (`0xFF`=da / `0x00`=nicht da,
  maskiert mit `51H`) landet in der Laufwerkstabelle ab **`0x0BE1`**. Die
  Verzögerungsschleife `M1084` ist der Spitzenreiter im PC-Histogramm (481 280
  Treffer) — **normal, kein Hänger**.
- **Konsolenerkennung** (`0x108F`–`0x10CF`): erst SIO A33 (`OUT (52H),0`,
  `IN A,(52H)`, Test `(A & 0F0H) == 80H`), sonst SIO A32 → Konsolenport `5CH` nach
  `[0x0612]`. In unserer Konfiguration greift der **`5C`-Zweig** (unsere Tastatur).
- **Bildschirmseite**: `LD A,0F8H / LD (0FD7H),A / LD (0FDFH),A` → UDOS gibt auf dem
  **K7024-VRAM ab `0xF800`** aus (deshalb ist der BREAK-Dump überhaupt sichtbar).
- Abschluss: `RST 00H` → residentes System ab `0x0000`.

### 7.2 Floppytreiber (`0x0700`, = `~/projects/UDOS/UDOS/FLOPPY76/5122/UDOSFLOP.700`)

Die Datei ist byteidentisch mit dem geladenen Code (1280 Bytes → `0x0700–0x0BFF`,
inkl. Laufwerkstabelle `0x0BE1` und Sprungvektor `0x0BFD: JP 0700H`).

```
0700: LD HL,(0FC7H) / LD A,H / OR L / JR NZ,0700   ; Warteschlange frei?
070C: LD (0FC7H),IY                                ; Auftrag einhängen
0710: BIT 0,(IY+01H) / JR NZ,0720 / CALL 0720
0719: BIT 7,(IY+0AH) / JR Z,0719                   ; auf Fertig-Bit warten
```

Der Auftragsblock ist `IY` (=`0x0E89`), `(IY+0Ah)` = `0x0E93` ist das Statusbyte.
**Wichtig:** Der Treiber taktet ausschließlich über **System-CTC Kanal 3**
(`OUT (0FH),A`, Vektor `0xE6`, ISR `0x0A0A`) — er programmiert **keinen**
PIO-Interruptvektor und benutzt den Indexpuls-Interrupt der K5122 nicht.

---

## 8. Offene Punkte

### 8.1 Ein Index-Interrupt ohne IM-2-Eintrag (`0xBA` → `0x0FBA` = `FFFF`)

Nach dem Fix bleibt **genau ein** fehlschlagender Interrupt (Takt ≈ 13 027 481,
unterbrochener PC `0x071D`, also mitten in der Warteschleife des Treibers):

```
29,13027481,INT,0x071D,0xFFFF,0x0CFA        # boot_trace --itrace
… INT-Quittung: Vektor=0xBA                 # K5122 Steuer-PIO Port A
```

Kette der Fakten (alle gemessen, `--watchio 0x11`):

| Takt | Schreiber | `OUT (11H)` | Wirkung |
|------|-----------|-------------|---------|
| ~70 k | Boot-ROM (ZVE1) | `0BAH` | **Interruptvektor = `0xBA`** (`zre.prn` `0x00EB`) |
| 2 505 634 | UDOS-SYL-Lader (ZVE1 @`0x050C`) | `097H` + Maske `0FFH` | **IE = 1** (Lader nutzt den Indexpuls) |
| 3 093 250 | **ZVE2** (Boot-ROM-Leseroutine @`0x0269`) | `003H` | IE = 0 („INTENA=0") |
| — | **Emulator** `K5122::endDmaTransfer()` | `083H` | IE = 1 **zurückgesetzt** |
| 3 634 900 | POSINIT (ZVE1 @`0x1013`) | `03FH` | Modus 0 — **IE bleibt 1** |

Der Indexpuls-Interrupt ist also weiterhin scharf, während das System längst auf
`I=0x0F` umgeschaltet hat — und `0x0FBA` ist leer (`x 0x0FB8 8` → lauter `FF`).
Die Schwesterquelle `UNFLOPPY.MAC` (PC-1715-Variante desselben Treibers) baut ihre
Tabelle so auf:

```asm
	LD	HL,CTCINT / LD (0FE0H),HL      ; CTC
	LD	HL,INDEXI / LD (0FE8H),…       ; INDEX  → Vektor 0xE8
	LD	HL,MARINT / LD (0FEAH),HL      ; MARINT → Vektor 0xEA
```

Das deckt sich mit unserem eigenen Kommentar in `k5122.cpp` („Disketten-Index-
Interrupt *ivdsk1, Vektor 0xE8*"). **Der erwartete Indexvektor ist `0xE8`, nicht
`0xBA`** — `0xBA` ist der Wert, den das *Boot-ROM* hinterlassen hat. Zu klären ist
daher, wer den Vektor auf der echten Maschine auf `0xE8` umprogrammiert (der
K5122-Treiber tut es nachweislich nicht) bzw. warum auf echter Hardware in diesem
Fenster kein Indexpuls-Interrupt ankommt.

**Verdächtig ist zusätzlich `K5122::endDmaTransfer()`:**

```cpp
// ctrl_pio_ Port-A Interrupt-Enable wiederherstellen (von ZVE2-OUT(11H,03H) gelöscht).
ctrl_pio_.ioWrite(1, 0x83);        // ← bedingungslos IE=1
```

Das ist ein CP/A-motivierter Eingriff (Vollspur-FORMAT braucht den Index-Interrupt).
Er nimmt zwar tatsächlich *ZVE2s* Sperre zurück, ist aber **bedingungslos** und
damit ein Kandidat für eine sauberere Modellierung. Zwei erprobte Varianten sind
**verworfen**, weil sie hier nichts ändern (der Löscher *ist* ZVE2) bzw. UDOS in der
Warteschleife `0x0719` hängen lassen (ersatzloses Streichen):

- IE-Zustand beim Rundenstart merken und wiederherstellen → wirkungslos.
- Restore nur, wenn die Sperre von ZVE2 kam (I/O-Urheber-Flag am Bus) → wirkungslos.
- `ioWrite(1, 0x83)` streichen → UDOS kommt am BREAK vorbei, der Lesevorgang wird
  aber nie fertig (`0x0719/0x071D` von 11 838 auf 94 770 Treffer).

### 8.2 UDOS schreibt per DMA in den Bildschirmspeicher

`--watch 0xF810,0xFA00` zeigt, dass **ZVE2** (`INIR` @`0x0AD6`) Sektordaten nach
`0xF800+` ablegt:

```
[#5 cyc16363793] WR [F810]=F9  (Z1.PC=0676 Z2.PC=0AD8)
[#6 cyc16402758] WR [FA00]=00  (Z1.PC=0638 Z2.PC=0AD4)
```

Ob das legitim ist (UDOS legt Puffer hoch) oder Folge einer falsch berechneten
Zieladresse, ist **nicht geklärt**.

**Korrektur einer naheliegenden Fehlannahme:** Der K7024 ist bei uns **kein**
gewöhnliches Schreib-/Lese-RAM, sondern bereits hardwaretreu modelliert. Die
**Lesesperre** (Brücken X13/X14) ist im A5120 der Normalfall und in
`K7024::A5120Config` per Default gesetzt (`read_protect(true)`, `k7024.h:73`):

```cpp
/// @return false when Lesesperre is active (A5120 default), true otherwise
bool isReadable() const override { return !cfg_.read_protect; }
```

Der K7024 **antwortet also gar nicht auf Lesezugriffe** — das tut der K3526
darunter; Schreibzugriffe erreichen **beide** (`k7024.h:112–130`). `0xF800–0xFFFF`
ist für UDOS damit ganz normaler Speicher, und das ist **korrekt so**: Auf echter
Hardware ist es genauso. Die Konsequenz ist unbequem: Auch ein realer A5120 müsste
den Bildschirm zerschreiben, wenn dort Daten abgelegt werden. Damit verschiebt sich
die Frage von „wie verhält sich unsere Karte?" auf **„warum landen die Daten
überhaupt dort?"** — mögliche Richtungen:

1. Die **Speicher-Ausbaumessung** liefert bei uns ein anderes Ergebnis als real.
   Das Lade-ROM misst den Ausbau über den PIO-Interrupt (deshalb die
   `pending`-Löschung beim Sperren, `z80_pio.cpp:127/139`); UDOS könnte dieses
   Resultat übernehmen oder selbst messen und bei uns fälschlich bis `0xFFFF`
   kommen.
2. Die Zieladresse des Ladevorgangs ist falsch — dann müsste sich zeigen, woher
   der Treiber sie bezieht (Auftragsblock `IY`, s. §7.2).
3. Es ist tatsächlich normal, und UDOS löscht den Bildschirm später wieder — dann
   ist §8.1 die einzige echte Baustelle.

### 8.3 Konsole

Die Konsolenerkennung wählt korrekt den Zweig SIO A32 (`0x5C/0x5D`, unsere
Tastatur); Port `0x5D` wird im Lauf ~1241× gelesen. Bildschirmausgabe geht über
`0xF800`. Ein serielles Terminal ist damit **nicht** zwingend — die frühere Notiz
„UDOS erwartet ein serielles Terminal" ist durch `LD A,0F8H / LD (0FD7H),A`
widerlegt.

---

## 9. Vorgeschichte: der Lesepfad

`disks/udos_boot_scp.hfe` ist eine **real eingelesene** Diskette. Damit sie
überhaupt lesbar wurde, musste zuvor `BitCodec::decode` repariert werden (Re-Sync an
jeder Sync-Gruppe statt einmaligem Einrasten; real geschriebene Felder liegen in
unterschiedlichen Bytephasen). Siehe Commit `472f255` und
`project_real_disk_hfe_readpath`. Der Lesepfad ist für diese Analyse **nicht**
ursächlich — das Medium ist einwandfrei:

```
(dbg) disk verify
  A: disks/udos_boot_scp.hfe — Geometrie 80 Zyl × 2 Kopf, MFM
  → 160 Spuren, 4057 Sektoren, 0 CRC-Fehler, 0 Problem-Spuren, 0 leer   ✓ OK
```

…und die Systemspuren werden nach dem Fix vollständig und korrekt geladen
(Signatur `BO` an `0x4002` stimmt).

---

## 10. Werkzeug-Rezepte für diese Fehlerklasse

```sh
# 1) Bildschirm zu einem Zeitpunkt ansehen (Fehlermeldung finden)
tools/dev.sh trace DISK -c <n> -p <n> -L /dev/null      # VRAM-Textdump am Ende

# 2) Alle angenommenen Interrupts als CSV (seq,cyc,kind,int_pc,isr_pc,sp)
tools/dev.sh trace DISK -c 45000000 -p 45000000 --itrace /tmp/i.csv --quiet --json
cut -d, -f5 /tmp/i.csv | sort | uniq -c        # ISR 0xFFFF = Sprung ins Leere

# 3) Welcher VEKTOR wurde quittiert?  (K1520Bus loggt ihn auf DEBUG)
tools/dev.sh trace DISK -c 16000000 -p 16000000 \
     --log-cycle 11000000:16000000:debug -L /tmp/u.log --quiet --json
rg -o "INT-Quittung: Vektor=0x[0-9A-F]+" /tmp/u.log | sort | uniq -c

# 4) Wie kam die CPU dorthin?  (exakte Aufruf-History)
printf 'b 0x0038\ng 15000000\nr\nbt\nq\n' | tools/dev.sh tool k1520dbg DISK

# 5) Wer programmiert einen Port?  (mit BEIDEN CPU-PCs)
tools/dev.sh trace DISK -c 14000000 -p 14000000 --watchio 0x11 -L /dev/null
```

> **Falle:** `-c` ist die **Gesamt**-Taktobergrenze; `-p` verlängert nur *innerhalb*
> davon. `-c 5000000 -p 14000000` läuft trotzdem nur 5 Mio. Takte.
>
> **Falle:** `k1520dbg` reagiert bei `g` mit großen Taktzahlen nicht mehr in
> vertretbarer Zeit — auf dieser Disk ist `g 15000000` in 0,3 s fertig, `g 15500000`
> nach 90 s noch nicht. Grund: `g` schleift auf der **ZVE1-Uhr** (`cpuCycles()` =
> `zre_.cpu().cycles`), und die kriecht, sobald ZVE2 den Bus hält. Für lange Läufe
> `boot_trace` (`--until`, `--itrace`) nehmen und `k1520dbg` gezielt mit Breakpoint
> einsetzen.

> **Was an den Werkzeugen fehlte** — die Arbeit an dieser Analyse bestand zu großen
> Teilen aus Handarbeit, die ein Werkzeug erledigen sollte (Opcode-Längen zählen,
> Hex-Dumps vergleichen, zweimal C++ mit `fprintf` instrumentieren, um an einen
> PIO-Portzustand zu kommen). Die daraus abgeleiteten Verbesserungen stehen in
> **`tools/feature_request_source_annotation.md`**.

---

## 11. Wie die UDOS-Originalquellen genutzt wurden (Methodik)

Ohne `~/projects/UDOS/` wäre diese Analyse ein Vielfaches teurer geworden. Der
Arbeitsablauf ist auf andere Fremd-Betriebssysteme übertragbar — deshalb hier
explizit:

**Schritt 1 — Die Meldung dem Betriebssystem zuordnen.** Der VRAM-Dump lieferte den
Suchbegriff, eine einzige Suche das Modul:

```sh
rg -il "BREAK" ~/projects/UDOS      # → UDOS_/DEBUBC43.MAC (+ PC1715-Varianten)
```

Der Fund `DB 6,'BREAK '` / `DB 'A B C D E F H L '` / `DB 'I IXIYPCSPBR'` beweist:
Die Meldung ist **UDOS' eigener Monitor**, kein Emulator-Fehlertext. Das drehte die
Fragestellung sofort von „was gibt der Emulator aus?" auf „warum fällt UDOS in
seinen Debugger?".

**Schritt 2 — Adressen aus den Quellen gewinnen.** Die `.MAC`-Dateien sind
**Quelltexte ohne Adressspalte** (kein `.prn`), also für `-l` unbrauchbar (§10).
Zwei Eigenschaften machten sie trotzdem adressierbar:

- Sie stammen aus einem Disassemblat, dessen Labels die **Adresse im Namen tragen**:
  `M03F8:` liegt auf `0x03F8`, `M1084:` auf `0x1084`, `M0EDC EQU 0EDCH`. Damit war
  eine Adresskarte gratis vorhanden.
- Die Kopfzeilen nennen die Ladeadresse explizit (`ORG 700H`, `.PHASE 1000H`,
  „DIESES PROGRAMMSTUECK WIRD … AUF DIE ADRESSE 1000H GELADEN").

Für `DEBUBC43.MAC` (`ORG 0`) habe ich die Opcode-Längen ab `0x0000` **von Hand
durchgezählt**, um die Trap-Einsprünge zu finden — und traf exakt die
Z80-Architekturadressen `0x0038` (RST 38H) und `0x0066` (NMI), was die Zählung
zugleich verifizierte. Genau diese Handarbeit sollte das Werkzeug abnehmen
(Feature-Request §1/§2 in `tools/feature_request_source_annotation.md`).

**Schritt 3 — Gegen den laufenden Code validieren, nie blind vertrauen.** Die Disk
trägt einen **anderen Build** als die Quellen. Der Live-Disassembler zeigte die
`M03F8`-Sequenz um ca. `0x45` verschoben ab `0x0395`:

```
Quelle DEBUBC43.MAC        Live (u 0x0390 24)
  CP 4FH                     0398: FE 4F        CP 4FH
  LD HL,4000H                039C: 21 00 40     LD HL,4000H
  LD (M0E8B),HL              039F: 22 8B 0E     LD (0E8BH),HL
```

**Regel:** Die Quelle liefert *Semantik und Kommentare*, die Live-Disassembly
liefert *Adressen und Wahrheit*. Beides zusammen ersetzt das Reverse-Engineering.

**Schritt 4 — Binäre Identität prüfen.** `UDOSFLOP.700` beginnt mit
`2A C7 0F 7C B5 20 F9 36 C3 FD 75 0A FD 22 C7 0F` — Byte für Byte das, was
`u 0x0700` im RAM zeigt (`LD HL,(0FC7H) / LD A,H / OR L / JR NZ,0700 / …`). Damit
war belegt: **diese Datei *ist* der laufende Treiber** (1280 B → `0x0700–0x0BFF`).
Anschließend statisch disassembliert und gezielt durchsucht:

```sh
python3 tools/z80_disasm2.py --org 0x700 ~/projects/UDOS/UDOS/FLOPPY76/5122/UDOSFLOP.700 > /tmp/f.asm
rg -n "OUT \(" /tmp/f.asm            # → nur 0x0F (CTC), 0x10, 0x12, 0x18 — kein 0x11!
rg -n "LD \(0F[0-9A-F]{2}H\)" /tmp/f.asm   # → kein Schreiben eines IM-2-Vektors
```

Das ist der **Negativbeweis** für §8.1: Der Treiber programmiert weder einen
PIO-Interruptvektor noch nutzt er den Indexpuls — er taktet ausschließlich über
System-CTC Kanal 3. Ein solcher Beweis wäre aus reiner Emulator-Beobachtung kaum zu
führen gewesen.

**Schritt 5 — Schwesterquellen für fehlende Stellen.** Die IM-2-Tabellen­initialisierung
fehlt im A5120-Treiber, steht aber in der PC-1715-Variante `UNFLOPPY.MAC` desselben
Treibers:

```asm
	LD	HL,CTCINT / LD (0FE0H),HL      ; CTC
	LD	HL,INDEXI / LD (0FE8H),…       ; INDEX  → Vektor 0xE8
	LD	HL,MARINT / LD (0FEAH),HL      ; MARINT → Vektor 0xEA
```

Daraus folgt die Erwartung „Indexvektor = `0xE8`" — unabhängig bestätigt durch
unseren eigenen Kommentar in `k5122.cpp` (*ivdsk1, Vektor 0xE8*). So entstand die
offene Frage in §8.1.

**Schritt 6 — Auffälligkeiten entschärfen.** `POSIBC43.MAC` erklärte, dass der
Spitzenreiter des PC-Histogramms (`0x1084`, 481 280 Treffer) die
Step-Verzögerungsschleife der Laufwerkserkennung ist — **kein Hänger**. Ohne die
Quelle wäre das ein naheliegender, aber falscher Verdächtiger gewesen.

---

## 12. Quellen

- `~/projects/UDOS/UDOS_/` — kommentierte UDOS-4.3-Quellen für den A5120
  (`SYL0BC43`/`SYL1BC43`, `POSIBC43`/`UDBC43PO`, `DEBUBC43`, `UNFLOPPY`, `NDOS`);
  `PC1715/` enthält die Schwestervarianten. **Nicht neu disassemblieren.**
- `~/projects/UDOS/UDOS/FLOPPY76/5122/UDOSFLOP.700` — das geladene Treiberbinary
  (`ORG 0x700`), byteidentisch mit dem laufenden Code.
- `doc/EPROMS/zre.prn` — kommentiertes Boot-ROM-Listing (`-l`-ladbar); `0x00EB`
  setzt den K5122-Indexvektor `0xBA`, `0x0269` sperrt ihn auf ZVE2 wieder.
- `doc/analyse_zre_rom_boot.md`, `doc/K1520_architecture.md` §14.5 — Boot-Kette.
- `doc/analyse_scpx_com_load.md` — verwandte ZVE1/ZVE2-Arbitrierungsanalyse.
