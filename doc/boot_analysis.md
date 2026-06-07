# Boot-Analyse: A5120-Emulator

> **⚠️ HISTORISCH / GRÖSSTENTEILS ÜBERHOLT (Stand 2026-06).** Dieses Dokument hält
> die *frühe* Boot-Bring-up-Analyse (Mai 2026) fest, als der Emulator noch gar nicht
> bootete. Inzwischen läuft die gesamte Boot-Kette: Boot-ROM → Sekundär-Loader →
> CP/A-Bootsystem, das `@OS.COM` lädt. Die hier als „behoben" markierten Punkte sind
> erledigt; die unten gesammelten Roh-Ideen („some ideas") sind überholt.
>
> **Maßgebliche, aktuelle Dokumentation:**
> - K5122-Lesemodell + offene Punkte: [`K1520_architecture.md` §14.5/§14.6](K1520_architecture.md)
> - Interrupt-System A5120: [`K1520_architecture.md` §14](K1520_architecture.md)
> - Bootloader-Stufen: [`analyse_bootloader.md`](analyse_bootloader.md)
> - ROM-Phase + Disassemblat: [`analyse_zre_rom_boot.md`](analyse_zre_rom_boot.md)
>
> Nur die Koppelbus-/Baudrate-/Interrupt-Ketten-Notizen in §3 haben noch
> Referenzwert (teils in `K1520_architecture.md` §14 übernommen).

## 1. Symptom (historisch)

Der Emulator startet nicht: Der Boot-ROM (0x0000–0x03FF) läuft in eine Endlosschleife,
ohne je den Floppy-Ladevorgang (CALL 0x0303) zu erreichen. Der Bildschirm bleibt leer.

---

## 2. Bisherige Erkenntnisse und Korrekturen

### 2.1 K3526 OPS — RAM-Initialisierung (behoben)

**Problem:** Die K3526-Klasse initialisierte ihren 64-KB-Speicher mit `0x00` (NOP-Sled).

**Auswirkung:** Beim ersten `RET M` im Boot-ROM (Adresse 0x0016) wird der Stack-Pointer
SP=0xFFFF dereferenziert. Das Byte an 0xFFFF (Kopplung aus K7024-VRAM und/oder K3526)
bestimmt den Sprungziel-PC zusammen mit dem ROM-Byte 0xEE an Adresse 0x0000.
Mit K3526=0x00 würde das Boot-ROM zu Adresse 0x0000 springen (NOP-Sled) statt zu einem
sinnvollen RST-Ziel.

**Korrektur:** `mem_.fill(0xFF)` in `K3526::K3526()`.

```
Echte DRAM-Hardware: Power-on-Zustand typischerweise 0xFF (oder undefiniert).
0xFF = RST 38h → springt zu 0x0038 (echter Boot-Einstieg im ROM).
```

**Datei:** `core/cards/k3526/k3526.cpp`

---

### 2.2 K7024 ABS — VRAM-Initialisierung und isWritable (behoben)

**Problem 1 — Zufällige VRAM-Initialisierung:**
Der K7024-Konstruktor initialisierte VRAM mit Zufallsdaten (Simulation von Power-on-Rauschen).
Das byte an VRAM-Offset 0x7FF (Adresse 0xFFFF) war zufällig. Das Boot-ROM liest beim
ersten `RET M` an 0x0016 exakt [0xFFFF] als low byte des Rücksprungiels.

**Auswirkung:** Zufälliges Sprungziel → kein deterministischer Boot möglich.

**Problem 2 — isWritable()=false (Fehlkorrektur):**
In einer früheren Diagnose wurde `K7024::isWritable()` auf `false` gesetzt in der Annahme,
dass Schreibzugriffe auf 0xF800-0xFFFF an K3526 gehen sollen. Das ist falsch.

**Korrektes K1520-Busverhalten (K7024 Lesesperre = X15:2–X16:2, Standardstellung):**
- CPU-Schreibzugriffe auf 0xF800–0xFFFF → **K7024 VRAM** (Bildschirmspeicher-Update)
- CPU-Lesezugriffe auf 0xF800–0xFFFF → **K7024 VRAM** (Inhalt des Bildschirmspeichers)

Der Z80-Stack nach Reset (SP=0xFFFF) liegt im VRAM-Bereich. Boot-ROM-RST-Befehle pushen
Return-Adressen dorthin. `LD A,(FFFCh)` liest denselben VRAM-Bereich zurück.
Schreiben (isWritable=false) und Lesen würden auf verschiedene Devices gehen → Inkonsistenz.

**Korrektur:**
1. `isWritable()` auf `true` zurückgesetzt (Original-Verhalten).
2. VRAM mit `0xFF` initialisiert statt Zufallswerten — entspricht dem typischen DRAM-Zustand
   nach dem Einschalten realer Hardware.

**Datei:** `core/cards/k7024/k7024.cpp`, `core/cards/k7024/k7024.h`

---

### 2.3 Boot-ROM-Ablauf (Analyse)

Das Boot-ROM ist eine verschachtelte Trampoline-Struktur aus RST-Befehlen (0xFF = RST 38h,
0xF7 = RST 30h, 0xDF = RST 18h), DJNZ-Schleifen und bedingten Returns.

**Kritischer Pfad (vereinfacht):**

```
Reset → PC=0, SP=0xFFFF, A=0xFF, F=0xFF
0x0000: XOR 0x77     → A=0x88, S=1 (negativ)
0x000A: RET P        → S=1 → NICHT ausgeführt (fällt durch)
0x000B: RET P        → S=1 → NICHT ausgeführt
0x000C: LD DE,5011h
0x0010: DEC DE (x2)
0x0014: DEC A (x2)   → A=0x86, S=1
0x0016: RET M        → S=1 → AUSGEFÜHRT!
  → PCL = [0xFFFF] = 0xFF (K7024 VRAM, init 0xFF)
  → PCH = [0x0000] = 0xEE (ROM[0x0000])
  → PC = 0xEEFF (K3526 RAM, init 0xFF = RST 38h)
0xEEFF: RST 38h      → PC = 0x0038 (echter Boot-Einstieg)

Ab 0x0038: Timing-Schleife via RST 30h/18h/DJNZ
0x003E: PUSH AF (x2) → Stack schreibt in K7024 VRAM (~0xFFFx)
0x0040: LD A,(FFFCh) → liest VRAM-Inhalt (von vorherigen Pushes/RSTs)
...
0x006F: CALL 0303h   → echter Floppy-Bootstrap-Code
```

**Port 0xD3** (mehrfach im Boot-ROM): Adresse liegt außerhalb aller bekannten Karten.
Möglicherweise ein historisches Relikt oder ein Signal auf dem Koppelbus der realen Hardware,
das im Emulator ignoriert werden kann (kein registriertes Gerät → write ignored).

---

## 3. Koppelbus — fehlende Verdrahtung (zu korrigieren)

### 3.1 ZRE CTC ZC/TO → Koppelbus

**Dokumentation (K2526 ZRE):** CTC ZC/TO[2] → CLK/TRG[3] "fest verdrahtet".
Die CTC-Ausgänge ZC/TO[0] und ZC/TO[2] müssen auf den Koppelbus getrieben werden.

**Aktueller Code (`a5120.cpp`):**
```cpp
koppel_.zc_to[2].connect([this](bool lvl) {
    zre_.ctc().clkTrg(3, lvl);   // Koppelbus → ZRE CTC CLK/TRG3
});
```
Fehler: Der Koppelbus-Signal `zc_to[2]` wird nie angetrieben (kein `drive()`-Aufruf).
ZRE CTC liefert kein ZCTO-Callback auf den Koppelbus.

**Korrektur:** ZCTO-Callback auf ZRE CTC setzen, der `koppel_.zc_to[ch].drive()` aufruft.

### 3.2 K8025 ASS — Baudrate-Takt (W1:7)

**Dokumentation (K8025):** Brücke W1:7 in "gezeichneter Stellung" = Baudrate-Takt kommt
vom ZRE CTC ZC/TO[0] über den Koppelbus.

**Aktueller Code:** Koppelbus `zc_to[0]` wird nicht mit K8025 CTC A34 verbunden.
K8025 CTC A34 erhält keine externen Taktimpulse aus dem ZRE CTC.

**Korrektur:** `koppel_.zc_to[0]` → K8025 CTC A34 CLK/TRG-Eingänge verbinden.

### 3.3 K8025 ASS — Interrupt-Kette (W1:8-W1:11)

**Dokumentation (K8025):** Brücken W1:8–W1:11 in "gezeichneter Stellung" = Drucker-SIO
(A32 Ch B) liegt in der **2. Interrupt-Kette (Koppelbus /IEI1–/IEO1)**, nicht im
Systembus.

**Aktueller Code:**
```cpp
bus_.setInterruptChain({&afs_, &ass_, &zre_});
```
K8025 (gesamte Karte) ist in der Systembus-Kette. Das ist teilweise falsch:
- SIO A33 (DFÜ), SIO A32 Ch A (Tastatur), CTC A34: Systembus-Kette — korrekt
- SIO A32 Ch B (Drucker): sollte Koppelbus 2. Kette sein — fehlt

Auswirkung auf Boot: Wahrscheinlich gering (Drucker wird beim Boot nicht genutzt).
Für spätere BIOS-Interrupt-Behandlung relevant.

---

## 4. Aktueller Stand (nach Korrekturen)

| Komponente | Status |
|---|---|
| K3526 RAM init 0xFF | ✅ behoben |
| K7024 VRAM init 0xFF | ✅ behoben |
| K7024 isWritable()=true | ✅ behoben |
| Koppelbus ZRE CTC → zc_to | ✅ behoben |
| Koppelbus zc_to[0] → K8025 CTC | ✅ behoben |
| Boot-ROM läuft bis CALL 0303h | ⬜ zu verifizieren |
| Floppy-Bootstrap lädt | ⬜ zu verifizieren |

---

## 5. Nächste Schritte

1. Build + Lauf mit boot_trace (-s -v -c 10000000) prüfen, ob CALL 0303h erreicht wird.
2. Insbesondere: erscheint Milestone 0x003E (PUSH-AF-Sequenz) jemals?
3. Falls 0x003E nie erscheint: Stack-Analyse aus dem Single-Step-Trace auswerten.
4. Falls 0x003E erscheint: Wert von [0xFFFC] und Bedingungen für RLCA prüfen.
5. Floppy-Emulation erst testen, wenn CALL 0x0303 bestätigt ist.

---

## 6. Detailanalyse des Boot-ROM-Ablaufs (Mai 2026)

### 6.1 Disassembly der kritischen ROM-Bereiche (0x0000–0x006F)

Die ROM-Bytes aus `ZRE_BOOT_ROM` wurden manuell disassembliert. Das ROM ist
**doppelt-kodiert**: viele Bytes sind als Paar wiederholt (z.B. `F3 F3 = DI DI`).
Tatsächliche Instruktionssequenz:

```
; Reset-Vektor und frühe Initialisierung
0x0000: EE 77     XOR A, 0x77        ; A = 0xFF ^ 0x77 = 0x88, S=1
0x0002: F3        DI
0x0003: F3        DI
0x0004: 19        ADD HL, DE
0x0005: 19        ADD HL, DE
0x0006: 27        DAA
0x0007: 50        LD D, B            ; D = B (=0 nach Reset)
0x0008: 04        INC B              ; RST-08h-Vektor: INC B
0x0009: 45        LD B, L
0x000A: F0        RET P              ; Return if Sign=0 (hier S=1 → kein Sprung)
0x000B: F0        RET P
0x000C: 11 11 50  LD DE, 0x5011
0x000F: 50        LD D, B
0x0010: 1B        DEC DE             ; DE = 0x500F
0x0011: 1B        DEC DE             ; DE = 0x500E
0x0012: F3        DI
0x0013: F3        DI
0x0014: 3D        DEC A              ; A = 0x87
0x0015: 3D        DEC A              ; A = 0x86, S=1
0x0016: F8        RET M              ; S=1 → AUSGEFÜHRT!
                                     ; PCL = [0xFFFF] = 0xFF (K3526 init)
                                     ; PCH = [0x0000] = 0xEE (ROM)
                                     ; → PC = 0xEEFF, SP = 0x0001
0x0017: F8        RET M (nicht erreicht)

; RST-18h-Handler (Timing-Schleife, Kern)
0x0018: 09        ADD HL, BC
0x0019: 09        ADD HL, BC
0x001A: D3 D3     OUT (0xD3), A      ; Timing-Delay (11 T-States, kein registr. Gerät)
0x001C: 10 3E     DJNZ 0x5C          ; B-- / wenn B≠0: → 0x5C
0x001E: D3 D3     OUT (0xD3), A      ; (nach DJNZ-Durchfall)
0x0020: 30 FF     JR NC, 0x21        ; wenn Carry=0: → 0x21 (RST 38h)
0x0022: FF        RST 38h            ; (wenn Carry=1: hier)

; RST-20h-Vektor (zufällig in Codebereich)
; 0x0020-0x0025: 0x30 FF FF FF FF FF → JR NC + 4× RST 38h
0x0026: F8        RET M
0x0027: F8        RET M

; RST-28h-Handler
0x0028: F7        RST 30h

; RST-30h-Handler
0x0030: 3F        CCF                ; Carry invertieren
0x0031: 3E DF     LD A, 0xDF
0x0033: DF        RST 18h            ; push 0x0034 → 0x0018

; RST-38h-Handler (Entscheidungsknoten)
0x0038: 87        ADD A, A           ; A = A*2, setzt S/Z/C
0x0039: 28 0D     JR Z, 0x48         ; wenn Z=1 (A war 0x00 oder 0x80) → RLCA-Pfad
0x003B: 0D        DEC C
0x003C: F7        RST 30h            ; push 0x003D → 0x0030 (Hauptschleife)
0x003D: F7        RST 30h            ; push 0x003E → 0x0030 (selten erreicht!)
0x003E: F5        PUSH AF            ; schreibt A an [SP-1]
0x003F: F5        PUSH AF
0x0040: 3A FC FF  LD A, (0xFFFC)     ; liest Stack-Inhalt (K3526 mit Lesesperre)
0x0043: FF        RST 38h            ; push 0x0044 → 0x0038

; RLCA-Ausstiegspfad
0x0048: 07        RLCA               ; A rotieren
0x0049: 18 1D     JR 0x68            ; immer → 0x68

; DJNZ-Sprungziel (innere Schleife)
0x005C: 26 00     LD H, 0
0x005E: F5        PUSH AF            ; A=0xDF an Stack
0x005F: F5        PUSH AF
0x0060: 7F        LD A, A (NOP)
0x0061: FF        RST 38h            ; push 0x0062 → 0x0038

; Ausstiegsprüfung
0x0068: B7        OR A               ; Flags auf A setzen
0x0069: B7        OR A
0x006A: FC FC 28  CALL M, 0x28FC     ; wenn S=1 (A≥0x80): call in RAM (0xFF→RST38h)
0x006D: 28 CB     JR Z, 0x3C        ; wenn Z=1 (A=0): zurück zur Schleife → LOOP
0x006F: CD 03 03  CALL 0x0303        ; wenn A in 0x01–0x7F: FLOPPY-BOOTSTRAP!
```

### 6.2 Bedingung für den Boot-Ausstieg

Für `CALL 0x0303` muss nach dem `RLCA` (0x0048) gelten:
- `A ∈ 0x01..0x7F` (nicht-null, nicht-negativ)

`RLCA` rotiert A zirkulär links. Da JR Z nach `ADD A,A` genau dann springt, wenn
A=0x00 oder A=0x80 war (beides liefert A=0x00 nach ADD):

- `[0xFFFC] = 0x00` → ADD A,A → 0x00 → RLCA(0x00) = 0x00 → `JR Z` zurück → **Loop**
- `[0xFFFC] = 0x80` → ADD A,A → 0x00, C=1 → RLCA(0x00) = 0x00 → **Loop**
- `[0xFFFC] = 0xDF` → ADD A,A → 0xBE, S=1, Z=0 → kein JR Z → DEC C, RST 30h → **Loop**
- `[0xFFFC] = 0xFF` → ADD A,A → 0xFE, S=1, Z=0 → **Loop**

**→ Kein normaler Wert in [0xFFFC] scheint den Ausstieg zu ermöglichen!**

Das deutet auf eine der folgenden Ursachen hin:

**Hypothese A: Der Code erreicht 0x003E nie (dominante Hypothese)**  
0x003E wird nur durch RST 30h an 0x003D gepusht. 0x003D wird nur durch einen RET
erreicht, der 0x003D vom Stack poppt. Das setzt voraus, dass RET M an 0x0026/0x0027
feuert — und das setzt voraus, dass die Returnadressen 0x0025/0x0026 auf dem Stack
liegen. Das PC-Histogramm bestätigt: 0x0040 erscheint NIE. Ursache unklar.

**Hypothese B: Fehlende Hardware-Bedingung (CTC/Port 0xD3)**  
Die `OUT (0xD3), A`-Instruktionen könnten auf echter Hardware einen CTC-Takteingang
treiben, der dann per ZC/TO-Ausgang ein Signal für die nächste Stufe erzeugt.
Im Emulator läuft OUT 0xD3 ins Leere (kein Gerät registriert).

**Hypothese C: K3526-Schreibzugriff auf ROM-überlappten Bereich**  
Beim RET M an 0x0016: PCH kommt aus ROM[0x0000]=0xEE. Aber RST 38h an 0xEEFF
schreibt Rücksprungadresse 0xEF00 auf den Stack: [0x0000] = 0xEF in K3526.
Wenn ROM deaktiviert wird, liest [0x0000] aus K3526 = 0xEF. Dieser Mechanismus
ist korrekt im Emulator — K3526 isWritable=true bei allen Adressen.

### 6.3 Bug in boot_trace.cpp (behoben)

`fprintf(stderr, "Max cycles: %d\n\n", disk_path, total_limit)` — `disk_path`
(const char*) war als erstes Argument übergeben, verschob `total_limit` aus dem
Format. Der Wert für `%d` wurde aus dem Pointer-Wert von `disk_path` gelesen,
nicht aus `total_limit`. Korrektur: überflüssiges `disk_path` entfernt.

### 6.4 Erweiterungen des boot_trace.cpp (Mai 2026)

Das Tool wurde grundlegend überarbeitet:

| Neu | Beschreibung |
|-----|-------------|
| `-s` | Single-Step-Trace: jede ROM-Instruktion mit Registerzustand ausgeben |
| `-n N` | Anzahl der Single-Step-Instruktionen (Standard: 200) |
| `-v` | Verbose: jede Änderung von [0xFFFC] mit SP/PC ausgeben |
| `-c N` | Maximale Zyklen (Standard: 5.000.000) |
| Milestones | Erkennt 0x003E, 0x0040, 0x0048, 0x006F und meldet erstes Auftreten |
| ROM-Disable | Erkennt wenn BS-PIO B0=/LD-ROM deaktiviert wird |
| Register-Dump | Vollständiger CPU-Zustand bei Milestones |
| [0xFFFC]-Tracking | Protokolliert jeden Wertwechsel mit Cycle-Nummer |
| ROM-Labels | Disassembly-Kommentare bei PC-Histogramm-Ausgabe |

Außerdem wurden Accessor-Methoden in `A5120Machine` ergänzt:
`cpuSP()`, `cpuAF()`, `cpuBC()`, `cpuDE()`, `cpuHL()`, `isRomEnabled()`,
`setCpuTraceCallback()`.

---

## 7. Aktueller Stand (Mai 2026)

| Komponente | Status |
|---|---|
| K3526 RAM init 0xFF | ✅ behoben |
| K7024 VRAM init 0xFF | ✅ behoben |
| K7024 isWritable()=true | ✅ behoben |
| Koppelbus ZRE CTC → zc_to | ✅ behoben |
| Koppelbus zc_to[0] → K8025 CTC | ✅ behoben |
| boot_trace.cpp fprintf-Bug | ✅ behoben |
| boot_trace.cpp erweitert | ✅ Milestones, Single-Step, Register-Dump |
| ROM-Disassembly 0x0000–0x006F | ✅ dokumentiert (Abschnitt 6.1) |
| Boot-ROM läuft bis CALL 0303h | ⬜ zu verifizieren (Hypothese A/B) |
| Floppy-Bootstrap lädt | ⬜ blockiert |

---

## 8. Nächste Analyse-Schritte

1. **boot_trace mit Single-Step** ausführen: `./boot_trace -s -n 500 -v disk_b.img`
   - Prüfen: erscheint 0x003E im Milestone-Log?
   - Wenn nein: Wann feuert der erste RET M (0x0016)? Was ist auf dem Stack?
   - Welchen Wert hat SP nach dem RET M an 0x0016?

2. **Hypothese A verifizieren**: Warum erscheint 0x003E nie?
   - Wohin springt der erste RET M (0x0016)? → PC = [SP] aus K3526 = 0xEEFF
   - RST 38h an 0xEEFF: schreibt [0x0000]=0xEF in K3526, [0xFFFF] löscht Inhalt
   - Dann: ADD A,A mit A=0x86 → 0x0C, S=0 → Z=0 → DEC C, RST 30h an 0x003C
   - Erster RST 30h pushes 0x003D — enthält dieser nach dem tiefen Stapel-Aufbau
     jemals den Pfad zurück zu 0x003E?

3. **Hypothese B (Port 0xD3)**: Falls ein K2526-CTC-Kanal über interne Logik
   auf Port 0xD3 reagiert (andere Adressdekodierung?), müsste das in der
   K2526-Dokumentation nachgeprüft werden.

4. **Längeren Trace** laufen lassen (50M Zyklen): `./boot_trace -c 50000000 disk_b.img`
   - Reale Hardware braucht ~5M Zyklen bei 2.4576 MHz für ~2 Sekunden.
   - Falls nach 50M immer noch kein 0x003E: systematisches Problem.


---

*(Die früheren Roh-Notizen „some ideas" / „some neuer idears" — Stream-of-Consciousness
aus der Phase, in der der Emulator noch nicht bootete — wurden 2026-06 entfernt, da
vollständig überholt. Aktueller Stand: siehe Header oben.)*
