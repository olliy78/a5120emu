# Boot-Analyse: A5120-Emulator

## 1. Symptom

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



### some ideas (Rohdaten aus früherer Analyse — siehe Abschnitt 6)

The boot trace still shows the same pattern - just more iterations of the same loop. The 0x001E appears only 3 times (DJNZ falls through 3 times in 5M cycles), but we never escape the loop!

Wait, looking at 0x001E appearing 3 times! The DJNZ does fall through 3 times in 5M cycles! But after falling through, we don't see 0x0303 or any other address beyond 0x0038 appearing. Let me look at what happens after 0x001E.

From the disassembly: 0x001E: OUT (D3h),A

0x0020: JR NC, -1 (target 0x0021) 0x0022: RST 38h

So after the OUT instruction, there's a conditional jump that loops back, and then RST 38h instructions. Let me check the actual ROM data at these addresses to understand what's really there. Verifying the DJNZ instruction target address—the offset 0x3E equals +62 decimal, so from PC 0x001E the jump lands at 0x005C as expected.

Now examining the next sequence: a JR NC instruction at 0x0020 with offset 0xFF (which is -1 in signed form), so if no carry flag is set it jumps back to 0x0021, landing on what appears to be a RST 38h instruction.

Looking at the execution paths, after the OUT instruction at 0x001E, the carry flag state becomes critical. The OUT instruction itself doesn't modify carry, so I need to trace back where carry gets set. Following the RST 18h path: there's a CCF instruction at 0x0030 that complements the carry flag, then LD A loads 0xDF without affecting it, and RST 18h jumps to 0x0018 where an ADD HL,BC operation would change the carry flag based on the result.

The key insight is that HL accumulates values through repeated additions of BC in the loop, and eventually when HL overflows, the carry flag gets set. At 0x0020, the JR NC instruction checks this carry state—if no carry is set, it jumps to 0x0021 (RST 38h), otherwise it falls through to 0x0022 (also RST 38h). Either way, both paths lead to the same RST 38h instruction.

Now I'm realizing that when RST 38h executes from 0x0021, it pushes the return address 0x0022 onto the stack and jumps to 0x0038. But this isn't going through the PUSH AF area at 0x003E-0x003F like I initially thought—it's a direct jump to 0x0038. At that address, there's an ADD A,A instruction, though I need to figure out what value A actually contains at that point since the earlier LD A,0xDF might have been modified by the loop iterations. I'm seeing the loop structure more clearly now—after RST 30h, we jump back to 0x0030 where CCF toggles the carry flag, then we load 0xDF into A and call RST 18h, which eventually leads back to DJNZ. The decrement causes B to wrap from 0 to 0xFF, so we jump back to 0x005C and cycle through again, never actually reaching the LD A,(FFFCh) path that would break out of this repetitive sequence. So RST 18h jumps to 0x0018, where it performs some operations with HL and BC, outputs to port D3h, then uses DJNZ to conditionally jump back to 0x005C. When that loop finally falls through with B=0, it outputs again and checks the carry flag—if no carry, it executes RST 38h which pushes the return address 0x0022 and jumps to 0x0038. There the code adds A to itself and conditionally jumps to 0x0048 if the result was zero, otherwise it decrements C at 0x003B.

Now I'm realizing that RST 30h at 0x003C doesn't actually return via an explicit RET instruction—the return address gets pushed onto the stack but then overwritten by subsequent RST calls, so the stack frame is essentially abandoned rather than properly unwound.

The stack here is doing double duty: it's storing both return addresses and timing constants that the code reads back via memory access to determine loop exit conditions. The "returns" are never actually used as returns—they're just values sitting on the stack that get read to control program flow.

Now I'm tracing through the RST instruction targets and their return paths. The RST 18h at 0x0033 pushes 0x0034 as a return address, but execution doesn't immediately return there. Instead, RST 18h jumps to 0x0018, where a DJNZ either branches to 0x005C if B is nonzero or falls through to 0x001E if B reaches zero, eventually triggering another RST 38h.

Following the first path where DJNZ jumps to 0x005C, I see a sequence that loads H with 0, pushes AF twice, then executes RST 38h at 0x0061, which pushes 0x0062 and jumps to 0x0038. From there, ADD A,A operates on 0xDF to produce 0xBE with carry set, the conditional jump is skipped, then RST 30h at 0x003C pushes 0x003D and jumps back to 0x0030. The stack now has multiple return addresses accumulating—0x0034, 0x0062, and 0x003D—and the only way execution can actually return to 0x0034 is through a RET instruction.

Looking at the available RET conditions in the ROM, I see RET P at 0x000A and 0x000B (which return when S=0), RET M at 0x0016, 0x0017, and 0x0026 (which return when S=1). The key is figuring out what value of A after the ADD A,A at 0x0038 would set S=0, allowing RET P to pop and return to the stacked address. If A starts at 0x80, then ADD A,A produces 0x00 with S=0, which would satisfy the RET P condition.

But tracing through the actual execution path: when A=0x40, ADD A,A gives 0x80 with S=1, so the JR Z at 0x0039 doesn't jump, falling through to 0x003B and 0x003C where RST 30h executes. This RST 30h eventually chains through RST 18h at 0x0033, which pushes 0x0034 to the stack and jumps to 0x0018 to run the DJNZ loop. After the loop completes, we hit OUT D3h at 0x001E, then JR NC which leads to RST 38h at 0x0021, pushing 0x0022 and jumping to 0x0038. At that point with A=0xDF, ADD A,A produces 0xBE with C=1 and Z=0, so we'd need A to be 0x00 to satisfy the return condition. Looking at the execution frequency data from the histogram, I can see that 0x0033 (RST 18h) executes 13 times across 5 million cycles, while 0x0061 (RST 38h at that address) shows 10 times, and the DJNZ fall-through at 0x001E only happens 3 times. This suggests the DJNZ loop does eventually break, and when it does, the subsequent RST 38h at 0x0021/0x0022 enters the routine at 0x0038 with a different register state than before.

But 0x0040 never appears in the histogram at all, which means either the code path to reach it requires a very specific state that's not being hit, or the boot ROM is spending most of its time in a different loop entirely—possibly one involving the CTC timer with a clock divider that makes the actual execution time much longer than the cycle count suggests.

Looking at the port writes, 0xD3 is marked as unknown hardware, but it could be an I/O port on the Koppelbus or similar that the emulator isn't handling properly, which might be causing the code to hang or loop unexpectedly.

What's interesting is that the histogram shows the inner loop instructions (0x0018 through 0x001C) all appearing with roughly equal frequency around 814-838 times, suggesting they're executing at the same rate rather than the outer loop running significantly more iterations than the inner one.

The DJNZ at 0x001C is jumping back to 0x005C almost every time, but it does fall through occasionally—only 3 times across the entire 5 million cycle sample. That's roughly once every 1.67 million cycles, which works out to about 256 complete iterations of the DJNZ loop at roughly 6500 cycles per iteration. After those rare fall-throughs at 0x001E, the code writes to the I/O port and then branches based on a condition flag, potentially routing to the interrupt handler at 0x0038.

The critical observation is that the histogram shows zero hits to any 0x030x addresses, meaning the floppy bootstrap routine at 0x0303 never executes during this 5 million cycle window. On real hardware, the boot sequence should complete within 2-3 seconds, so this absence suggests something might still be preventing the loop from exiting properly.

Looking at the exit conditions: the code reaches the CALL 0x0303 instruction only when A has bit 7 clear (so S flag is 0 after OR A) and A is non-zero (so Z flag is 0), which means A must fall in the range 0x01-0x7F.

Tracing back through the execution path, I can see how we arrive at 0x0068: starting from loading a value at 0xFFFC, then hitting RST 38h which jumps to 0x0038, where ADD A,A with A=0 sets the Z flag, triggering the conditional jump to 0x0048 where RLCA executes, followed by the unconditional jump to 0x0068.

The key insight is that at 0x0048, the carry flag determines whether A becomes 0x00 or 0x01 after RLCA—if carry is set, we get 0x01 which clears the Z flag and allows the CALL to 0x0303 to execute, breaking out of the timing loop. So I need to trace back further to see what sets the carry flag before reaching that RLCA instruction. So at 0x0068, the OR instruction with A=0x01 clears the zero flag, which means the code skips both conditional jumps and falls through to call 0x0303 — that's the breakthrough. The exit condition requires (FFFCh) to equal 0x80, but I'm stuck because none of the code paths I've traced actually produce A=0x80 to push there. The carry flag from ADD A,A with A=0x00 is always zero, so that doesn't help either.

Wait, if (FFFCh) contains 0x80 instead, then ADD A,A with A=0x80 produces a carry, RLCA sets A to 0x01, and the OR succeeds — so the ROM definitely needs 0x80 at that VRAM address. Now I'm trying to figure out where 0x80 originates, and the PUSH AF instruction is the key since it writes A to memory, so I need to trace how A gets set to 0x80 in the first place. Now I'm tracing through the stack pointer changes more carefully. After the RET M instruction at 0x0016, SP increments twice (once for each byte popped), moving from 0xFFFF to 0x0001. Then when RST 38h executes, I need to figure out where SP ends up and what value gets pushed to 0xFFFC. I caught an error in my stack pointer arithmetic—0xFFFE minus 1 is 0xFFFD, not 0xFFFC. So after pushing the return address 0x003D, SP lands at 0xFFFD, then the RST 18h instruction at 0x0033 pushes 0x0034 with the high byte going to 0xFFFC and the low byte to 0xFFFB, leaving SP at 0xFFFB before jumping to 0x0018.

Now tracing through the DJNZ loop—it eventually falls through to 0x001E, then 0x0020, where a conditional jump takes us to RST 38h at 0x0021, which pushes the return address 0x0022 onto the stack at 0xFFFA and 0xFFF9 respectively. From there we jump to 0x0038, which leads to another RST 30h at 0x003C that pushes 0x003D, and the stack pointer keeps decrementing as we nest deeper into these interrupt handlers.

Each RST instruction pushes another return address, so the stack grows downward through memory, with 0xFFFC getting repeatedly overwritten. I'm realizing there's something about the PUSH instruction that might be relevant here—

specifically, when PUSH AF executes at 0x005E and 0x005F, it's pushing the A register (0xDF from the earlier load) and the flags register onto the stack, filling positions 0xFFFA through 0xFFF7 with these values.

Then RST 38h at 0x0061 pushes the return address 0x0062 onto the stack, moving SP down to 0xFFF5. From there, nested RST instructions (RST 30h, RST 18h) continue pushing return addresses as the stack grows downward, with the second iteration of RST 18h bringing SP to 0xFFF3 and storing 0x0034 at that location.

The question now is what value ends up at 0xFFFC—whether it gets overwritten or remains untouched by this sequence of operations.

For 0xFFFC to contain 0x80, I need to trace when a PUSH AF instruction executes with SP=0xFFFD and the A register holds 0x80, which would write A to 0xFFFC. However, the PUSH AF at 0x005E uses A=0xDF from the preceding LD A,0xDF instruction, so that's not the source of the 0x80 value.

Looking at the actual VRAM contents after 5M cycles shows 0xFFFC = 0xDF, which matches what PUSH AF wrote when SP was 0xFFFD. Then LD A,(FFFCh) loads 0xDF into A, and at 0x0038 the ADD A,A instruction doubles it to 0xBE with the carry flag set.

Since Z isn't set, the JR Z at 0x0039 doesn't branch, so execution continues to DEC C at 0x003B and then RST 30h. The timing loop keeps cycling through, but 0xDF never produces the 0x80 value needed to trigger the exit condition. The SP must be decreasing as the loop runs, so eventually PUSH AF executes with a different stack pointer and potentially a different value in A, though A keeps getting reloaded as 0xDF at the start of each iteration. I'm getting bogged down in the ROM trace details—the RST 28h instruction jumps to 0x0028, which then calls RST 30h, creating an alternate execution path that I don't need to fully trace right now. I should just increase the cycle count to let the timing loop run longer and see if the boot ROM progresses, since it likely needs tens of millions of cycles to complete at the system's clock speed. Looking at the VRAM contents at the end of memory, I'm seeing some data there that might be relevant to the boot state. I'm noticing there are actually two RST 38h instructions back-to-back at 0x0043 and 0x0044. The first one pushes the return address and jumps to 0x0038, where it shifts the accumulator left and checks if the result is zero—if so, it branches to handle the RLCA and jump to 0x0068, otherwise it decrements C and executes another RST.

Now I'm tracing through the different cases based on what's stored at [FFFCh]. When it's 0x00 or 0x80, the ADD A,A produces zero with different carry flags, which affects the RLCA result and determines whether the code loops back or calls 0x0303. I'm working through the edge case where [FFFCh]=0xDF to see how that shifts.

The critical insight is that 0x80 is the only value that actually breaks out of the loop—specifically when the carry flag gets set during ADD A,A. That means I need to figure out what operation would set [FFFCh] to 0x80, which would be pushing the F register when only the Sign flag is set.

When ADD A,A executes with A=0x40, the result is 0x80, which sets the Sign flag. The overflow flag also gets set because 0x40+0x40 causes a sign change, so the F register becomes 0x84 (Sign and overflow both set, no carry, no zero flag).

Then when ADD A,A runs again with A=0x80, the result wraps to 0x00, setting the Zero and Carry flags instead. After DEC 0x81 to 0x80, the flags would be S=1, Z=0, H=0, P/V=0, N=1, giving F=0x82 or 0x83 depending on the carry flag—still not matching 0x80. Let me try ADD A,A with A=0xC0: that gives 0x180, so A becomes 0x80 with carry set.

With A=0x40 and ADD A,A, I get 0x80 with no carry, and the overflow flag sets because the sign flipped from positive to negative, making F=0x84. Getting F to be exactly 0x80 seems tricky—I'd need S=1 with all other flags clear, which requires a result in the negative range with no carry, no half-carry, and no overflow. Let me explore other approaches.

Actually, I've been confusing the byte order for PUSH AF. On the Z80, A is the high byte and F is the low byte, so when pushing, A goes to the higher address and F to the lower one. With SP starting at 0xFFFD, the first decrement puts A at 0xFFFC, then the second decrement puts F at 0xFFFB. So when DJNZ jumps to 0x005C with SP still at 0xFFFB, the first PUSH AF at 0x005E decrements SP to 0xFFFA and stores A (0xDF) there, then decrements again to 0xFFF9 and stores the flags register.

The second PUSH AF at 0x005F continues from SP=0xFFF9, pushing A and F to 0xFFF8 and 0xFFF7 respectively.

The key insight is that 0xFFFC—which was set to 0x00 by the initial RST 18h push—remains untouched by these PUSH AF operations as the stack grows downward, and subsequent RST pushes in later iterations will interact with different stack locations.

Looking at the VRAM snapshot after 5M cycles, I see 0xFFFC now contains 0xDF, which means a PUSH AF must have written there at some point when SP was 0xFFFD. This would happen after enough RST pushes (like RST 30h at 0x003C bringing SP to 0xFFFD, then RST 18h further adjusting it) before the PUSH AF at 0x005E executes.

But tracing through the actual execution path: RST 30h drops SP to 0xFFFD, then RST 18h at 0x0033 pushes again, bringing SP to 0xFFFB by the time PUSH AF runs. For PUSH AF to execute with SP=0xFFFD instead, I'd need to skip that RST 18h call entirely, which doesn't align with the control flow I'm seeing.

The VRAM data showing [0xFFFC]=0xDF after 5M cycles confirms PUSH AF did write there, so the stack pointer must have been at 0xFFFB at that moment. I should trace through the fall-through path more systematically to understand the exact sequence, or just follow what the emulator trace shows and check whether it eventually reaches CALL 0x0303.

On real hardware at 2.4576 MHz, the boot takes about 2 seconds—that's roughly 5 million cycles, which matches our simulation length. The timing loop exits when [0xFFFC] becomes 0x80, but right now it's 0xDF, so something needs to load A with 0x80 and write it there to break out of the loop.

Looking at how A could become 0x80: it could come from ADD A,A when A=0x40, or through rotation operations like RLCA. I'm tracing through the code at 0x0048 where RLCA appears, and checking if there's an XOR operation at 0x0073 that might be manipulating the accumulator value.

But now I'm wondering if the timing loop isn't actually supposed to exit via [0xFFFC]=0x80 at all—maybe there's a different exit condition I'm missing. Looking back at the disassembly around 0x003E through 0x0049, I see multiple PUSH AF instructions, a load from [0xFFFC], RST calls, and a conditional jump, followed by RLCA. The sequence is confusing because the RST instructions at 0x0043 and 0x0044 seem unusual in this context.

Let me trace through what happens when [0xFFFC]=0xDF: after loading that value and executing RST 38h, we jump to 0x0038 where ADD A,A doubles it to 0xBE with the carry flag set, then a conditional jump on zero isn't taken since the result isn't zero.

Then at 0x003B we decrement C and hit RST 30h, which pushes the return address 0x003D onto the stack and jumps to 0x0030. That target code starts with CCF, loads 0xDF into A, and executes RST 18h—so the code at 0x003E-0x0040 is actually part of the RST 30h handler itself, not a separate path.

When RST 18h runs from 0x0033, it pushes 0x0034 as the return address and jumps to 0x0018. After that returns, we're back at 0x0034, which is where things get interesting: there's a sequence of RST 38h calls and CP (HL) instructions, and then at 0x0038 we hit the ADD A,A instruction we've been tracking. The JR Z at 0x0039 jumps forward 13 bytes to 0x0048 if the zero flag is set.

Looking at what comes after, I see DEC C, then RST 30h calls, and finally PUSH AF instructions at 0x003E-0x003F. The key insight is that 0x0038 sits right in the middle of the RST 30h target code range, and those final PUSH AF instructions are at the very end of it. For execution to actually reach 0x003E-0x003F, the code has to be running within the RST 30h handler and somehow get past the RST 18h call that happens earlier. I'm realizing this could be a trampoline pattern where RST instructions push return addresses onto the stack, allowing the code to return to specific locations and continue execution sequentially. When an RST at one address pushes the next address as a return value, and that return is executed, it lands at the pushed address and can fall through to the next instruction. When RST 30h at 0x003D executes, it pushes the next address 0x003E onto the stack and jumps to 0x0030, where CCF and LD A,0xDF execute before RST 18h. That return address 0x003E stays on the stack as execution continues through DJNZ falling through to 0x001E, then 0x0020 with a conditional jump, eventually reaching RST 38h which jumps to 0x0038 where a Z-conditional branch doesn't trigger since A holds 0xBE.

The execution loops back through 0x003B and 0x003C before hitting RST 30h again, which pushes 0x003D this time. But I'm realizing the code never actually reaches 0x003E directly—there must be a RET instruction somewhere that's pulling from the stack. Looking at the ROM, I see RET M instructions at 0x0016 and 0x0017, though those are in the sequential code path rather than inside the RST 30h target at 0x0030.

Now examining the area around 0x0026-0x0027, which contains RET M instructions within what should be RST 38h padding space. The flow after DJNZ falls through to RST 38h at 0x0021, jumping to 0x0038, where ADD A,A and a conditional jump determine whether execution continues to 0x003B or branches elsewhere. But wait: if the RST 38h at 0x0021 pushes return address 0x0022. Then at 0x0038 → ...

Actually, I realize I need to look at ALL the RET instructions in the ROM:0x000A: RET P (S=0)0x000B: RET P (S=0) 0x0016: RET M (S=1) 0x0017: RET M (S=1)0x0026: RET M (S=1) 0x0027: RET M (S=1)And there are more at other addresses... let me look at the full ROM again:Row 2 (0x0020): 30 FF FF FF FF FF F8 F8 → 0x0026=F8=RET M, 0x0027=F8=RET M

Now I'm scanning through the remaining rows of the ROM to identify all RET instructions. Row 3 has no RET opcodes, Row 4 has none either, and Row 5 contains a POP AF at 0x0057 which isn't a RET. Continuing through Row 6 to see what else is there. I'm going to step back and just run more cycles to see if the boot ROM actually exits this timing loop, since I might be overcomplicating the control flow. Looking at what happens at 0xFFFC after 5M cycles—it's 0xDF, but it needs to reach 0x80. The issue is that A is consistently 0xDF or 0xBE when PUSH AF executes, and the F register doesn't seem to ever equal 0x80 based on my calculations, so I need to reconsider the register state from a different angle, particularly how B and C are decrementing together. So BC stays at 0x00xx since B is locked at 0, and C decrements each loop iteration. Each ADD HL,BC operation adds a small value (0-255) to HL, and since the loop runs ADD HL,BC twice per iteration, I need to figure out how many iterations it takes for HL to overflow past 0xFFFF. Each iteration decrements C and adds BC to HL twice, so HL grows by 2×C per cycle. Since C starts at 0xFF and counts down through 0xFE, 0xFD, and so on, I need to figure out when HL reaches 0x10000 and overflows. 000 (16-bit overflow). HL starts at 0. We add 2×C each iteration.

Sum of 2×C over one full cycle of C (0xFF down to 0x00): Sum = 2 × (0xFF + 0xFE + ... + 0x00) = 2 × (255+254+...+0) = 2 × 255×256/2 = 255×256 = 65280 = 0xFF00.

HL is 16-bit and maxes out at 0xFFFF. After one complete C cycle (256 iterations), HL would increase by 0xFF00 without overflow. But individual ADD HL,BC operations might trigger the carry flag when the sum exceeds 0xFFFF—for instance, when HL=0xFF00 and we add BC=0x0100, we get 0x10000, which overflows. Let me reconsider this by tracking the total accumulation: over 256×256 total DEC C operations across multiple outer loop cycles, HL accumulates roughly 65536 × 2 × 127.5, which is around 16 million—far exceeding 16-bit capacity and causing multiple overflows.

The key insight is that each ADD HL,BC can set the carry flag when overflow occurs. At instruction 0x0020, JR NC checks the carry flag: if carry is clear, it jumps to 0x0021 (RST 38h); if carry is set, it falls through to 0x0022 (also RST 38h). Both paths invoke RST 38h but from slightly different addresses, though both ultimately loop back to 0x0038.

Rather than continue tracing this manually, I should test with a much larger cycle count to see if the ROM ever reaches CALL 0x0303. I'm also noticing that the C register gets set to specific values during the loop—particularly, DEC C at 0x003B only executes when the JR Z at 0x0039 doesn't jump (meaning A≠0 after ADD A,A), which happens when A=0xDF from the LD A,0xDF instruction at 0x0031. So 0x001B is actually the operand for the OUT instruction at 0x001A, not a separate instruction. The real instruction is OUT (0xD3),A which is 2 bytes total.

Looking at the control flow: C decrements with each loop iteration at 0x003B, while B stays at 0. The HL register accumulates 2×C each time through. When ADD HL,BC causes an overflow and sets the carry flag, the conditional jump at 0x0020 will fall through instead of looping back.

Wait, I'm seeing two ADD HL,BC operations in sequence, so the carry flag that matters for the JR NC check comes from the second one. Either way, whether carry is set or not, the code seems to end up at RST 38h. Let me look at the actual byte values to clarify what's at each address.

0x0021 and 0x0022 both contain 0xFF, which is the RST 38h opcode. So whether the JR NC jumps or falls through, execution hits RST 38h—just at different addresses depending on the carry flag state.

The key difference is which return address gets pushed onto the stack: 0x0022 if the jump is taken, or 0x0023 if it's not. Later, when RET M executes with the sign flag set, it could pop whichever address is on the stack and jump back to it. If 0x0023 gets popped, that's another RST 38h instruction, creating a recursive loop through the same interrupt handler. I'm getting tangled up in the conditional branches and stack manipulations here—the RET M instruction keeps popping addresses that lead back into more RET M calls, creating this nested chain. Rather than trace through every cycle manually, I should just run an extended simulation to see how long it actually takes to reach the CALL at 0x0303. Looking at the boot trace output, I'm noticing the DJNZ fall-through at 0x001E appears multiple times within a 5-cycle window, so there's a pattern I can use to estimate the overall timing.

The real constraint is the ADD HL,BC×2 carry condition—I need HL to be large enough that adding twice the BC value overflows 0xFFFF. With BC starting at 0x00FF, that's only 0x1FE added, so HL needs to be quite high already for the carry to trigger.

But here's the key insight: HL accumulates across iterations. Each time DJNZ falls through (roughly every 1.67M cycles), HL grows by around 256 × 2 × the average C value. If C is around 0x80, that's a full 0x10000 increment per cycle—a complete 16-bit overflow. So the carry DOES get produced during each DJNZ fall-through.

The fact that 0x001E appears three times in the trace means three DJNZ fall-throughs occurred, which means three times the ADD HL,BC generated carry. Since JR NC at 0x0020 didn't jump those three times, execution fell through to RST 38h at 0x0022 each time.

This creates a complex stack state with multiple return addresses (0x0023, 0x003D, 0x0034, 0x0062, 0xFFFE after wrapping) being pushed and later popped when RET conditions are met. The nested RST/RET trampoline creates an intricate timing sequence. I should try running with 50M or 100M cycles to see if CALL 0x0303 eventually gets reached, since the ROM might just be taking far longer than expected to complete its initialization loop.

### some neuer idears (Rohdaten — siehe Abschnitt 6)

memReadDebug calls bus_.memRead(addr). With Lesesperre active (K7024 isReadable=false), the bus reads from K3526 instead. So the VRAM dump at 0xFFFC-0xFFFF should be showing K3526's content (0xDF), which is what the CPU reads when executing LD A,(0xFFFC).

So 0xFFFC in K3526 = 0xDF. The CPU reads 0xDF, then at 0x0038 does ADD A,A → 0xDF * 2 = 0x1BE → A = 0xBE, C flag set, Z flag clear.

Since Z isn't set, the JR Z at 0x0039 doesn't execute. After DEC C and RST 30h at 0x003C, the boot timing loop would need 0xFFFC to contain 0x80 to get the Z flag set and jump to 0x0048, but it's 0xDF instead. That means 0x80 must be written there somehow—either through a PUSH AF with SP=0xFFFD and A=0x80, or via a direct write operation.

For A to equal 0x80, I'd need ADD A,A with A=0x40 first. The real question is whether the boot ROM ever sets A to 0x40 and executes PUSH AF at the right stack pointer. Let me reconsider this from first principles—the boot ROM works on real hardware, so what's actually different between the emulator and real hardware? On real hardware A starts at 0xFF, while the old emulator had A=0x00, but the new one matches real hardware with A=0xFF. Yet both seem to hit the same loop, which suggests I need to trace through the execution path more carefully.

Tracing with A=0xFF, I get XOR 0x77 producing A=0x88 with the sign flag set, so the conditional returns don't trigger. I need to check the actual ROM bytes to see what's really happening at those addresses rather than relying on my assumptions about the disassembly.

analyse_bootrom.md
analyse_bootrom.md
Now I can see the structure more clearly. Let me trace through the critical path:

From the histogram, the hottest addresses are:

0x0019 (ADD HL,BC)

0x003B (DEC C)

0x005E (PUSH AF)

0x0030 (CCF - RST 30h handler)

0x001A (OUT (D3h),A)

0x005F (PUSH AF)

0x0038 (ADD A,A - RST 38h handler)

0x001C (DJNZ 005Ch)

0x0018 (ADD HL,BC - RST 18h handler)

0x005C (LD H,0)

0x0060 (LD A,A - NOP equivalent)

0x0031 (LD A,DFh)

The less frequently hit addresses include the RST handlers at 0x0033 and 0x0061, plus the DJNZ fall-through at 0x001E.

Now I'm tracing through the boot ROM path to understand what happens after the CALL to 0x0303. Looking at the disassembly around 0x006F, I can see this call is reached after some conditional logic involving OR A and a conditional call to 28FCh, followed by a jump. I need to re-examine the exact sequence at 0x0068 to understand the control flow leading up to this critical call. I see that the jump at 0x0048 lands at 0x0068, which is actually the high byte operand of the LD BC instruction, so it executes as OR A. Then there's another OR A at 0x0069, followed by a CALL M instruction at 0x006A that uses the next two bytes as its address operand. 28FC is in K3526 RAM initialized to 0xFF, so if the CALL M is taken (S=1), it jumps there and executes RST 38h, pushing the return address and jumping to 0x0038. If not taken (S=0), execution continues to a conditional jump on the Z flag at 0x006D, and if that's also not taken, it calls 0x0303. To reach that final CALL, I need to trace back through the conditions starting from the RLCA at 0x0048.

Now I'm working through the paths that lead to 0x0068 — specifically, when the ADD A,A result is zero, which happens if A starts as 0x00 or 0x80. If A=0x00, the ADD produces zero with Z=1, so the JR Z at 0x0039 jumps to 0x0048, the RLCA keeps A at 0x00, and then the JR to 0x0068 continues the chain. If A=0x80, the ADD also produces zero after wraparound.

I need to clarify what RLCA actually does — it rotates A left circularly, moving bit 7 into both bit 0 and the carry flag, not rotating through carry. So when A=0x00 after the ADD, RLCA still leaves A at 0x00.

Then the OR A instruction at 0x0068 sets the Z flag since the result is still zero, and the subsequent conditional jumps follow: CALL M doesn't execute because S=0, but JR Z does execute since Z=1, jumping back to 0x003C.

Now with A=0x80 instead, ADD A,A produces 0x100, which wraps to A=0x00 with the carry flag set to 1 and Z=1. The JR Z at 0x0048 takes us to RLCA, but here's where I need to be careful: RLCA operates on the current value of A (which is now 0x00 after the ADD), not the carry flag from before. So bit 7 of 0x00 is 0, meaning RLCA sets C=0 and leaves A=0x00.

Then OR A sets Z=1 again, so JR Z loops back to 0x003C. I'm trying to figure out what initial value at [0xFFFC] would actually break this loop instead of cycling through it. So if [0xFFFC] = 0x80, then ADD A,A produces 0x00 with the zero flag set and carry flag set, which triggers a conditional jump to 0x0048. I'm trying to trace through exactly what happens at each step of this instruction sequence.

At 0x0048, I'm executing RLCA on A = 0x00. This rotates A left circularly—bit 7 wraps around to bit 0—so the result stays 0x00 with carry cleared. The key thing I'm realizing is that RLCA is a circular rotation, not a rotate-through-carry operation like RLA, so it doesn't preserve the carry flag from the previous ADD instruction.

After the jump to 0x0068, I'm testing A with OR A, which sets the zero flag since A is still 0x00. The conditional call at 0x006A doesn't trigger because the sign flag isn't set, so I jump back to 0x003C and loop again. This means A = 0x80 at [0xFFFC] also fails to break out of the loop, so I need to figure out what value would actually allow the CALL at 0x0303 to execute.

For that to happen, after the OR A instruction, I need A to be non-negative (bit 7 = 0) and non-zero. Working backwards through the RLCA and ADD A,A operations, A at [0xFFFC] needs to be in the range 0x01-0x7F to produce the right conditions after the rotation. So when V equals 0x00 or 0x80, the zero flag gets set and the loop continues with A becoming 0x00. But if V is anything else, the code falls through to decrement C and jump to the next routine. I see I misread the control flow earlier. Let me trace through this more carefully—after the RST 30h call at 0x003C pushes its return address, execution jumps to 0x0030 where it toggles the carry flag, loads 0xDF into A, then calls RST 18h which pushes 0x0034 and jumps to 0x0018.

The real puzzle is what happens when those two PUSH AF instructions at 0x003E-0x003F execute—I need to figure out what's actually on the stack at that point and how these nested RST calls are building up the return addresses. After the ADD A,A operation produces 0xBE with carry set, the JR Z check doesn't trigger. Then C gets decremented and RST 30h jumps back to 0x0030, which toggles the carry flag and loads 0xDF into A before jumping to 0x0018 again—so this outer loop decrements C with each iteration while the inner loop keeps cycling through.

Looking at the RST 18h handler, I see it's doing two consecutive ADD HL,BC operations, then an OUT instruction that outputs to port 0xD3, followed by what looks like a DJNZ at 0x001C to loop back. The RST 18h handler loops B times, adding HL to BC twice per iteration and outputting to port D3h, then falls through when B reaches zero. I'm trying to figure out what initializes B—it seems like BC might start at zero after reset, but I need to trace where B actually gets set in the execution flow. then execution continues to 0x0020 and hits another RST instruction at 0x0021 or 0x0022, which pushes yet another return address onto the stack before jumping to 0x0038. So by the time we reach the ADD A,A instruction at 0x0038, the stack is cluttered with multiple return addresses from the different RST calls—0x0034, 0x003D, and 0x0022 or 0x0023. With A already set to 0xBE from the previous ADD A,A operation, I'm now tracking what happens next with this accumulated stack state.

The conditional jumps and RST calls keep stacking more addresses as the code loops, but I'm realizing I need to figure out where the RET instructions actually execute—they're scattered throughout the early initialization section at 0x000A, 0x0016, and 0x0026, but execution only passes through them once at startup, so I'm reconsidering how the control flow actually works here. RST 30h jumps to 0x0030 where CCF clears the carry, then loads 0xDF into A and calls RST 18h. That routine performs ADD HL,BC twice and outputs to port D3h in a loop of 256 iterations, then checks the carry flag from the last addition to determine whether to jump or fall through at 0x0020.

Both paths lead to RST 38h at either 0x0021 or 0x0022, which jumps to 0x0038. There, A becomes 0xBE after doubling 0xDF, the zero flag isn't set so we skip the conditional jump, then C decrements and we loop back via RST 30h. This cycle continues until C reaches zero.

Wait—I'm realizing that LD A,0xDF at 0x0031 reloads A every iteration, so ADD A,A always produces 0xBE. That means the zero flag is never set, so the conditional jump at 0x003A is never taken. The code block at 0x0040-0x0049 is unreachable through this path.

The only way to reach that block would be if RST 30h at 0x003D executes instead, pushing return address 0x003E onto the stack. Then when a RET instruction fires later, it could pop that address and jump into the PUSH AF sequence. But I need to figure out when RST 30h at 0x003D actually gets called. So when RST 30h executes at 0x003C, it pushes 0x003D as the return address and jumps to 0x0030. The handler there runs CCF, loads 0xDF into A, then executes RST 18h—which pushes 0x0034 and jumps into the inner DJNZ loop. After that loop completes, execution continues to RST 38h.

Now I'm seeing the pattern: RST 30h at 0x003C always executes because the JR Z condition is never taken (A is always 0xBE). This means 0x003D gets pushed onto the stack, and eventually some RET instruction pops it, landing back at 0x003D—which is another RST 30h instruction. That one pushes 0x003E and goes through the same handler sequence, creating a chain where each RST 30h invocation returns to the next one.

Looking at the RET instructions scattered throughout the ROM, they're all conditional (RET P or RET M) and located in the RST vector areas—RST 08h has RET P at 0x000A and 0x000B, RST 10h is nearby, and so on. One of these conditional returns must be the one that pops 0x003D off the stack. Looking at where RET M instructions appear at 0x0026 and 0x0027 in the RST 38h area, I'm realizing these would only execute when a RET instruction is encountered with the sign flag set. The question is how execution actually reaches those addresses—tracing through the RST 38h handler at 0x0038, which adds A to itself, conditionally jumps if zero, decrements C, and calls RST 30h, I need to figure out what path leads back into that 0x0026-0x0027 region. Now I'm tracing through the actual execution when RST 38h gets called with the carry flag set during the DJNZ loop. The instruction at 0x0022 triggers RST 38h, which pushes the return address 0x0023 onto the stack and jumps to 0x0038. From there, the ADD A,A operation with A=0xDF produces 0xBE and sets the carry flag, so the conditional jump is skipped. Then DEC C executes, followed by RST 30h at 0x003C, which pushes 0x003D and jumps to 0x0030. At that point, CCF clears the carry, then A gets loaded with 0xDF, and RST 18h pushes 0x0034 before jumping to 0x0018.

At 0x0018, the code performs two ADD HL,BC operations, outputs to port D3h, and runs a DJNZ loop 256 times. If the ADD HL,BC at 0x0019 causes an overflow and sets the carry, then the JR NC at 0x0020 doesn't branch, so execution falls through to RST 38h at 0x0022, which pushes another return address. This means multiple return addresses are accumulating on the stack with each iteration of the loop.

The stack pointer starts at 0xFFFF and descends through K3526 RAM (where Lesesperre causes writes to go to both K7024 and K3526, but reads come from K3526). After the DJNZ/RST sequence completes, RET M at 0x0016 pops the top two bytes and jumps to 0xFFFF with SP at 0x0001. At that address, RST 38h executes (since K3526 RAM contains 0xFF), pushing 0x0000 and jumping to 0x0038. There, ADD A,A operates on A = 0x88 (the result of an earlier XOR with 0x77).

The ADD A,A instruction doubles the accumulator to 0x110, leaving A = 0x10 with the carry flag set. The zero flag is clear since bit 7 of 0x10 is 0. The conditional jump JR Z doesn't execute, then DEC C decrements C from 0 to 0xFF. RST 30h at 0x003C pushes the return address 0x003D onto the stack (SP decrements from 0xFFFF to 0xFFFD) and jumps to 0x0030.

Actually, let me restart the trace more carefully from the beginning with A = 0xFF. XOR A with 0x77 gives 0x88, which sets the sign flag but clears zero, half-carry, and the carry flag—XOR doesn't affect the carry bit the way I was thinking.

Let me reconsider the Z80 flag register layout: bit 7 is sign, bit 6 is zero, bit 5 and 3 are undocumented copies of result bits, bit 4 is half-carry, bit 2 is parity/overflow, bit 1 is add/subtract, and bit 0 is carry.

For the OR operation with result 0x88 (10001000b), I need to check the parity: two bits set means even parity, so PV should be 1. Setting S=1, Z=0, H=0, PV=1, N=0, C=0 gives me the flag byte value. The S flag gets set to 1 after the XOR operation, and then I'm tracing through a sequence of instructions: disabling interrupts twice, adding HL and DE (which doesn't change anything since both are zero), applying DAA to the accumulator, loading D from B, incrementing B, loading B from L, and finally hitting a conditional return that checks the S flag.

Now I'm continuing through the next section where the return at 0x000B isn't taken, then loading DE with 0x5011 from the instruction bytes, which gets modified to 0x0011 after D is set to B (which is 0). DE then decrements twice down to 0x000F, interrupts are disabled again, and the accumulator decrements from 0x88 down to 0x86.

The sign flag is set since 0x86 has bit 7 set to 1, so the RET M instruction at 0x0016 is taken. This triggers a return where the program counter is reconstructed from memory: PCL comes from [0xFFFF] which reads 0xFF from RAM, and PCH comes from [0x0000] which reads 0xEE from ROM, with the stack pointer incrementing to 0x0001 afterward.

Now I'm at address 0xEEFF where there's another RST 38h instruction. I need to trace through what happens when RST executes—it pushes the next instruction's address onto the stack and then jumps to the reset vector at 0x0038. Since the instruction is at 0xEEFF, the next address would be 0xEF00, which gets pushed as the return address before the jump occurs. SP decrements to 0x0000, where the high byte of PC (0xEF) gets written—but 0x0000 is in the boot ROM which is read-only. The K3526 RAM card covers the full address space, so the write might broadcast to it instead of the ROM, or it might fail entirely depending on how the K1520 bus handles writes to shadowed memory regions.

Actually, thinking through this more carefully: the Z80 PUSH instruction stores the high byte first at the higher address, then the low byte at the lower address. So I need to reconsider the order of what's being written where.

For the RST instruction at 0xEEFF, the return address is 0xEF00. With SP starting at 0x0001, the high byte 0xEF gets written to address 0x0000, and the low byte 0x00 gets written to 0xFFFF. This means [0x0000] in K3526 becomes 0xEF (the ROM stays unchanged), and [0xFFFF] gets overwritten from 0xFF to 0x00. After the push, SP becomes 0xFFFF, and execution jumps to 0x0038 where the next instruction is ADD A,A with A = 0x86.

The ADD A,A doubles A to 0x10C, so A becomes 0x0C with the carry flag set. The zero flag isn't set, so the conditional jump isn't taken. Now I'm tracing through the register state at DEC C—C is the low byte of BC, which started at 0, then B was incremented to 1 at 0x0008, but then immediately overwritten back to 0 at 0x0009 when LD B,L executed with L = 0. So BC is still 0 at this point.

Following through the subsequent instructions—DEC DE, DI, DEC A, and RET M—BC remains unchanged at 0x0000. When RST 38h executes, BC is still 0x0000. At 0x003B, DEC C wraps C from 0 to 0xFF, then RST 30h pushes the return address and jumps to 0x0030. There, CCF toggles the carry flag from 1 to 0.

Now loading A with 0xDF, then RST 18h pushes the next instruction address onto the stack and jumps to 0x0018. I'm executing a pair of ADD HL,BC operations that add 0x00FF to HL twice, resulting in 0x01FE with no carry. Then outputting 0xDF to port D3h, followed by DJNZ which decrements B and branches if non-zero.

Since B starts at 0, decrementing gives 0xFF, so the jump to 0x005C executes. At 0x005C I'm clearing H while keeping L at 0xFE, then pushing the accumulator and flags onto the stack—A (0xDF) goes to 0xFFFA and F goes to 0xFFF9, leaving SP at 0xFFF9.

Another PUSH AF follows, storing the same values at 0xFFF8 and 0xFFF7 with SP now at 0xFFF7. The LD A,A is a no-op, then RST 38h pushes the return address 0x0062 onto the stack and jumps to 0x0038. There, ADD A,A doubles 0xDF to get 0xBE with the carry flag set, and the zero flag isn't set so the conditional jump doesn't trigger. Then C gets decremented from 0xFF to 0xFE.

Now RST 30h pushes 0x003D and jumps to 0x0030. After that, CCF clears the carry, A loads 0xDF, and RST 18h pushes 0x0034 before jumping to 0x0018. Then I'm performing ADD HL,BC operations where BC equals 0xFFFE, causing HL to overflow as it adds repeatedly—first from 0x00FE to 0x00FC, then to 0x00FA, with the carry flag staying set throughout.

The next section outputs via D3h, then enters a DJNZ loop where B starts at 0xFF and decrements each iteration. Inside the loop, H gets zeroed, AF gets pushed twice, and RST 38h executes before more arithmetic and flag operations. This inner loop runs 255 times total as B counts down from 0xFF to 0x01, and during each iteration C also decrements by 1, so by the end C has dropped from 0xFE significantly. So the first DJNZ at 0x001C starts with B=0, which decrements to 0xFF and jumps back to 0x005C, giving us 255 total iterations before B wraps to 0x00 and the loop exits. I'm realizing that RST 18h gets called repeatedly from within the inner loop via RST 30h, so the stack keeps growing with each iteration. Let me trace through what happens after that first DJNZ jump: the code loads H with 0, pushes AF twice to the stack, then executes RST 38h, which triggers a chain of RST calls that eventually circles back to RST 30h. if B isn't zero, it loops back to 0x005C. I'm realizing each inner loop iteration pushes a lot onto the stack—between the PUSH AF instructions and the RST calls, that's 10 bytes per iteration. With 255 iterations, that's 2550 bytes being pushed, starting from an SP around 0xFFFB.

After calculating the stack pointer movement, it ends up around 0xF605. Then DJNZ falls through when B reaches zero, continuing to 0x001E where an OUT instruction executes, followed by a conditional jump that depends on the carry flag. This triggers another RST call, pushing the return address and moving the stack pointer further down to around 0xF603.

At the interrupt handler at 0x0038, I'm working through the arithmetic: A gets shifted left (0xDF becomes 0xBE), then C gets decremented. After 255 iterations of the inner loop, C wraps around—starting from 0xFE and subtracting 0xFF in 8-bit arithmetic gives -1, which wraps to 0xFF.

The critical realization is that each inner iteration pushes 10 bytes onto the stack, so the stack grows and the return addresses stored at 0xFFFC keep changing. I need to trace what's actually at 0xFFFC after the RET M instruction at 0x0016 executes, since that's where the execution flow depends on the stack state. SP wraps to 0xFFFF after the push, writing the return address 0x003D across memory locations 0xFFFE and 0xFFFD, then PC jumps to 0x0038 where ADD A,A executes with A=0x86, followed by DEC C (bringing C from 0 to 0xFF), and finally RST 30h at 0x003C pushes the next return address.

After CCF and LD A,0xDF, RST 18h at 0x0033 pushes return address 0x0034 to the stack, placing 0x00 at 0xFFFC and 0x34 at 0xFFFB with SP now at 0xFFFB. I notice that 0xFFFC now contains 0x00, which means when LD A,(0xFFFC) executes, it will read this value.

The inner DJNZ loop starting at 0x0018 runs 255 times (since B=0 wraps to 0xFF). Each iteration pushes AF twice at 0x005E and 0x005F, decrementing SP each time, then executes RST 38h at 0x0061 which pushes the return address 0x0062 onto the stack, further reducing SP to 0xFFF5. I'm recalculating the stack pointer changes through the inner loop iterations—need to verify how many bytes are actually being pushed per cycle since my initial count of 10 bytes per iteration might be off. So that's 2550 bytes of stack space consumed, bringing SP down from 0xFFFB to 0xF605. When DJNZ finally exits with B at zero, execution continues to the next instruction, and I need to trace what happens to HL through all this—it starts at zero but L never gets reset each iteration, so I need to recalculate its actual value at this point.

Now I'm working through the RST 18h calls and how they affect HL. BC starts as 0x00FF, so after two ADD HL,BC operations HL becomes 0x01FE. Then DJNZ jumps back and H gets cleared to zero while L stays at 0xFE, making HL = 0x00FE. The tricky part is tracking what BC becomes after the first RST 18h call, since DEC C happens at 0x003B and I need to figure out its value for the next iteration. written to [0xFFFC]? When SP = 0xFFFD, a PUSH AF operation decrements SP and writes A to the lower address and F to [0xFFFC]. So I need to trace when SP reaches 0xFFFD to understand how [0xFFFC] gets overwritten with 0xDF. Let me trace through the call chain more carefully. RST 18h at 0x003C decremented SP from 0xFFFD to 0xFFFB, writing the return address. Before that, RST 30h had already moved SP from 0xFFFF to 0xFFFD after the DEC C instruction. So I need to track where each RST instruction lands and how SP changes through the sequence. RST 30h at 0x003C triggers another stack write, then CCF and LD A load a value before RST 18h pushes the return address. The DJNZ loop continues decrementing B through 254 more iterations, each consuming 10 bytes of stack space, building up a deep recursion pattern. I need to reconsider the DJNZ behavior more carefully. When B starts at 0xFF, the first decrement gives 0xFE which is non-zero, so it jumps—that's iteration 1. Continuing down, when B reaches 0x01, decrementing gives 0x00, which causes the fall-through. So there are actually 254 inner loop iterations, not 255. Adding the initial pass through with B=0 that triggered the first jump, I'm getting tangled in the exact count. Let me step back and figure out what value needs to be at address 0xFFFC for the code to properly exit this nested loop structure. Let me re-examine the disassembly more carefully. It looks like the bytes are overlapping in a confusing way—the LD BC instruction at 0x0066 loads 0xB781, but then 0x0069 shows OR A, which means the third byte of that load (0xB7) is being reinterpreted as part of the next instruction. Then I'm seeing CALL M with address 0x28FC, followed by JR Z to 0x003C, and finally CALL 0x0303. us here when jumping from 0x0048. At 0x0068, I'm executing OR A using the 0xB7 byte from what looks like a LD BC instruction, which updates the flags based on the A register's value. Since A was 0x00 from the RLCA operation, the sign flag stays clear, so the CALL M at 0x006A won't trigger. The JR Z at 0x006D would be taken if A is 0x00 (setting the Z flag), but if A was 0x01 from rotating 0x80, then Z wouldn't be set.

For the CALL 0x0303 at 0x006F to execute, I need A to be non-zero and positive. Tracing back, A at 0x0069 comes from RLCA at 0x0048, which gets its value from ADD A,A at 0x0038 (invoked via RST 38h at 0x0043), and that ultimately comes from loading a value at memory address 0xFFFC using the BUS.

Now I'm realizing the constraint: [0xFFFC] must be either 0x00 or 0x80 for the JR Z at 0x0039 to jump to 0x0048. If it's 0x80, then ADD A,A produces 0x00 with the carry flag set, triggering the jump, and then RLCA operates on zero.

If it's 0x00, ADD A,A also produces 0x00 but with carry clear, still triggering the jump. In both cases, OR A on the result keeps the zero flag set, so we loop back to 0x003C.

Wait, both paths loop? Let me trace through what actually happens before the LD A,(FFFCh) instruction at 0x0040, because there's an RST 38h right after it at 0x0043 that I need to account for. I'm trying to figure out when execution actually reaches the RET M instruction at 0x0026/0x0027. The RST 38h handler at 0x0038 can't directly jump there, and the return addresses pushed by RST calls from 0x0021 or 0x0022 would be 0x0022 or 0x0023, not 0x0026. I'm realizing there might be an RST instruction at 0x0024 that I need to trace through. I need to trace through how execution actually reaches 0x0026 where that RET M instruction sits. The only ways are either a branch jumps there directly, or sequential execution falls through from 0x0025. If RST 38h at 0x0024 executes, it pushes the return address and jumps to 0x0038, where the code continues with a conditional jump and then decrements C.

Now I'm working through what happens if RST 38h is at 0x0025 instead—it would push 0x0026 and jump to 0x0038, then the arithmetic and conditional logic plays out, eventually triggering RST 30h at 0x003C. That pushes another return address and jumps to 0x0030, which then chains to RST 18h at 0x0018. Meanwhile, at 0x001E there's an OUT instruction followed by a conditional jump, and if the carry flag is clear, it jumps to 0x0021 where another RST 38h executes, pushing 0x0022 onto the stack. The return addresses keep stacking up.

But I'm realizing the core issue: 0x0026 only gets popped if it's at the top of the stack when a RET executes, and that only happens if execution reaches 0x0026 itself. I think I need to step back and consider whether the emulator is missing something fundamental about how the hardware works—maybe the CTC timer isn't triggering interrupts correctly, or the PIO status isn't being read properly, or there's something about port 0xD3 that I'm not accounting for. The histogram shows 0x001A executing around 828 times, which suggests the code is stuck in a loop at that output instruction. So 0xD3 is actually the port number in the OUT instruction—the bytes D3 D3 mean OUT to port 0xD3 with the accumulator. There are two of these instructions at 0x001A and 0x001E, both writing to the same port. Then there's a conditional jump at 0x0020 that uses JR NC with an offset of -1, which loops back to 0x0021.

The interesting part is that both the taken and not-taken paths lead to RST 38h instructions, but they're at different addresses—0x0021 if the jump is taken, 0x0022 if it falls through. This means the return address pushed onto the stack differs depending on which path executes, which matters for what gets popped later.

Now I'm working through what value needs to end up at [0xFFFC] to actually exit the loop. The trace shows 0xDF got stored there, but that doesn't satisfy the exit condition. Let me trace through the mechanics: loading from [0xFFFC] into A, then RST 38h pushes the return address and jumps to 0x0038, where ADD A,A doubles the value and sets flags based on the result. I need to figure out which value would actually trigger the loop exit. So the code jumps to 0x0068, performs an OR operation that sets Z=1, and then the conditional jump at 0x006A takes us back to 0x003C—creating an infinite loop regardless of whether the memory value was 0x00 or 0x80. Let me look more carefully at the actual disassembly around 0x0043 to make sure I'm reading the instruction boundaries correctly. I'm realizing there's a critical control flow issue here—the RST 30h instruction at 0x003D would jump away to 0x0030, so execution could never naturally fall through to reach the LD A,(FFFCh) instruction at 0x0040. This suggests either the code path is unreachable under normal conditions, or there's something about how these RST calls return that I'm not accounting for. 0x003E would only be a return address on the stack if RST 30h at 0x003D executed, which requires either a branch targeting 0x003D or a RET popping 0x003D from the stack. The return address 0x003D gets pushed onto the stack regularly in the main loop when RST 30h at 0x003C fires, so if a conditional RET like RET M executes with 0x003D at the top of the stack, the CPU would jump back to 0x003D and execute it.

When RST 30h at 0x003D runs, it pushes 0x003E onto the stack. The handler then executes through its sequence—CCF, loading 0xDF into A, then RST 18h which triggers a DJNZ loop—and eventually hits RST 38h, which pushes the next return address (0x0022 or 0x0023 depending on where RST 38h is located). After the handler completes and returns, execution lands at PUSH AF at 0x003E.

Now at 0x0038, ADD A,A with A=0xDF produces 0xBE, setting S=1 (since bit 7 is set). The Z flag is 0, so JR Z doesn't trigger. Then DEC C executes, followed by another RST 30h, which again pushes a return address and jumps back into the handler loop.

I'm realizing the stack is accumulating multiple return addresses as the program cycles through these RST instructions. Each iteration of the main loop pushes addresses from RST 30h, RST 18h, and the nested DJNZ iterations with their own RST calls, creating a growing stack of 0x003D, 0x0034, and other return addresses.

The RET M instructions at 0x0026/0x0027 would only execute if the code somehow reaches those addresses, but with A=0xDF producing S=1 after ADD A,A at 0x0038, the program falls through to DEC C and RST 30h instead. For those RET M instructions to trigger, I'd need ADD A,A to produce a result where bit 7 is clear, putting A in the 0x00-0x7F range.

But here's the problem: A is always loaded as 0xDF at 0x0031 before reaching 0x0038, so it should always set S=1. Unless... the code reaches 0x0038 through a different entry point where A holds a different value. RST 38h is the only path to 0x0038, and the key question becomes what value A contains when RST 38h actually fires. Looking at where RST 38h can be triggered from, the initial call at 0xEEFF has A=0x86. So at 0x0034, which is the return address from RST 18h, there's another RST 38h instruction. The execution path gets there when RST 18h pushes 0x0034 and jumps to 0x0018, then after DJNZ falls through and RST 38h executes at 0x0020-0x0022, it eventually returns via RET and pops that 0x0034 address back, landing right at that next RST 38h.

Now I'm trying to figure out when RET M would actually fire—it's at 0x0026 or 0x0027, and it only executes when the program counter reaches those addresses. The tricky part is understanding how execution gets there in the first place, since RST instructions push return addresses rather than directly reaching them. So the S flag is set to 1 when 0xDF becomes 0xBE, which means RET M won't trigger. Let me reconsider the RET P instruction at 0x000A instead. For execution to reach that address, I'd need RST 08h to fire, which would jump to 0x0008 where it loads B with L, then... I need to trace through what happens next. So RST 08h jumps to 0x0008, which executes INC B, then loads B with L, then conditionally returns if the sign flag is clear. Without knowing what L contains at that point, it's hard to trace the full effect, but I'm trying to figure out if RST 08h is actually called during normal execution or if it's just dead code in the ROM.

Looking at RST 28h instead—it chains to RST 30h, which then calls RST 18h, creating this nested sequence of restarts that eventually flows into RST 38h. The manual trace is getting unwieldy with all these jumps and conditional returns, so I'm going to step back and think about this differently. The boot ROM timing loop might actually be working correctly, but it could just be taking an enormous number of cycles to complete—at the A5120's clock speed of 2.4576 MHz, a real boot might take several seconds, which translates to millions of cycles. The emulator could be running the code faithfully but just needs time to finish.

Let me try pushing the cycle count much higher—50 million or even 100 million cycles—to see if the loop eventually breaks out. Or maybe I'm misunderstanding what this loop is actually doing. It might not be a timing loop at all, but rather waiting for some specific hardware condition—like a CTC interrupt signal, a ZC/TO output from the timer, or a PIO status flag. Port 0xD3 keeps appearing in the disassembly, and I should figure out what that port actually controls in the K2526 architecture. = 211 decimal. Port 0xD3 might actually be a STATUS register for the K2526 BS-PIO, but writing to it is puzzling. Actually, I think those OUT instructions to 0xD3 might be functioning as timing delays rather than actual device communication—on real hardware, even writes to non-existent ports consume T-states, so multiple OUT instructions could create a deliberate delay loop. If that's the case, the timing depends on how many iterations the inner loop runs, which would be 256 cycles per B register iteration.

Looking at the histogram data, the hot addresses cluster around specific instructions that execute repeatedly. The DJNZ at 0x001C jumping back to 0x005C suggests a tight inner loop that dominates the execution profile, with those addresses accounting for roughly 8-9% of the total cycles. Tracing through that loop path—0x005E, 0x005F, 0x0060, 0x0061—would show where most of the time is actually spent. 8.5%). 0x001C appears 814 times (8.2%). These are sampled by PC at each batch. The slight difference is because 0x0018 (first ADD HL,BC) is counted separately from 0x0019 (second).

OK so the inner DJNZ runs at a rate of about:

814 (DJNZ at 0x001C) hits in 9883 samples
If the DJNZ falls through only 3 times in 9883 samples (0x001E appears 3 times = 0.03%), then the ratio is:

814 / 3 ≈ 271 DJNZ iterations per fall-through
This makes sense: 256 inner iterations (B: 0xFF→0x00) per fall-through.
Actually it's more: the fall-through at 0x001E happens when B reaches zero, and since B starts at some initial value and decrements each iteration, going from that value down through 0xFF, 0xFE, and so on until hitting 0x00 gives us 256 total DJNZ iterations before the fall-through—which aligns with the 814/3 ≈ 271 ratio I'm seeing. So across the 5M cycle window, the DJNZ falls through just 3 times, meaning each fall-through represents one complete pass through the outer timing loop with a full set of inner iterations and one DEC C operation. On real hardware running at 2.4576 MHz and booting in about 2 seconds, that's roughly 5M cycles total, and in that span the DJNZ falls through 3 times.

But if the boot ROM actually exits after some fixed number of outer loop iterations—say 100—then I'd need about 167M cycles to match that, which seems impractical. I'm wondering now if the exit isn't based on a fixed count at all, but rather triggered by a hardware signal. The OUT instruction to port 0xD3 might be signaling a CTC or PIO, or maybe there's a CTC interrupt involved. Let me reconsider what port 0xD3 actually controls in the A5120's port decoding scheme.

Looking back at the boot analysis, the CTC ports on K2526 are 0x0C-0x0F, but K8025 has additional CTCs and PIOs. The boot ROM does initialize the CTC later for serial baudrate around address 0x0303, so it's not active during the timing loop itself. I need to figure out what port 0xD3 maps to in the K2526 internal I/O space. by the R register, but there's no instruction in the traced code that actually reads it. I'm also wondering if an interrupt could be breaking out of the loop, though interrupts are disabled right after the DI instruction, so that doesn't seem likely either. Let me trace through when interrupts might actually be re-enabled.

Looking at the boot ROM, I'm searching for the EI opcode (0xFB) to see where execution might resume with interrupts active, but I'm not finding it in the disassembly section I've examined so far. So the timing loop appears to run entirely with interrupts disabled.

On actual hardware, the A register starts at 0xFF after reset, and after the XOR and subsequent decrements, it becomes 0x86, which determines how the RET P instruction behaves.

Now I'm tracing through what happens after the ADD A,A operation sets A to 0x0C with the sign flag clear. The JR Z doesn't trigger, then DEC C wraps it to 0xFF, and execution jumps to RST 30h at 0x0030, which clears the carry flag and loads 0xDF into A before jumping to RST 18h.

From there, I'm working through the HL arithmetic operations—adding BC (0x00FF) twice to get HL to 0x01FE—then DJNZ causes B to wrap and jump back. After pushing the accumulator and loading H with 0, another RST 38h triggers, this time with A=0xDF, which produces 0xBE with carry set and sign flag set, leading back through the same conditional jump and RST 30h sequence.

Now I'm loading A with 0xDF and jumping to 0x0018 again, repeating the HL additions with BC=0x00FE to reach 0x01FC. The DJNZ loop continues, and I'm realizing that B actually decrements from 0xFF down through 255 iterations before reaching 0x00 and falling through—the decrement happens before the zero check. Now I'm focusing on the key question: what happens to HL after those 255 inner iterations, and does the ADD HL,BC instruction set the carry flag? I need to carefully track how C changes—it starts at 0xFE after the outer loop's decrement, then gets decremented once per inner iteration, so after 255 iterations C wraps around to 0xFF. Let me trace through the first iteration to verify this pattern. H gets reset to 0 at the start of each inner iteration, so HL becomes 0x00LL where L is the current low byte. Then two ADD HL,BC operations add the current C value (0x00CC) to HL twice, causing L to accumulate with potential carries into H. At the top of the loop, H gets reset to 0, then I'm adding BC to HL twice in succession—so HL accumulates 2*C each iteration, with the result staying within 16-bit bounds since both H and B are 0. the issue now—H gets reset to 0 at the start of each inner loop iteration, but L persists and accumulates. So after the first ADD HL,BC, L holds (L+C) mod 256 with a potential carry into H, and the second ADD HL,BC adds C again, making L grow by roughly 2C per iteration. The carry flag from that second ADD is what determines whether the loop continues.

When H=1 after the first ADD (meaning the previous L plus C exceeded 255), the second ADD becomes 0x01LL + 0x00CC, which still can't overflow 16 bits since H maxes out at 1 and C is at most 255.

But wait—I'm realizing L actually accumulates across iterations, not resetting each time. So at the start of iteration n, L already contains the sum from previous iterations, not just a single byte. That changes everything about whether the 16-bit carry can actually trigger. ADD HL,BC produces a carry only when the 16-bit sum exceeds 0xFFFF, but with H capped at 1 from the first ADD, B at 0, and both C and L at most 0xFF, the maximum possible value is 0x02FE—well below the overflow threshold, so no carry is generated from the second ADD.

This means the conditional jump at 0x0020 always takes the no-carry path, which routes execution to RST 38h and pushes return address 0x0022 onto the stack. The RST 38h at 0x0021 never executes from the normal flow, so 0x0023 is never pushed as a return address—only 0x0022 comes from the DJNZ loop.

Now I need to trace back to understand how [0xFFFC] gets written with a different value and how the boot ROM actually exits, which likely ties back to that first RST 18h call and the stack state it created.

The RST 18h at 0x0033 decrements SP from 0xFFFD to 0xFFFB, writing the high byte 0x00 to [0xFFFC] and the low byte 0x34 to [0xFFFB]. So [0xFFFC] starts at 0x00, and then as the inner loop runs with repeated PUSH AF and RST instructions, the stack pointer keeps moving to lower addresses. The only way [0xFFFC] changes is if one of these pushes executes with SP positioned to write directly to that address. Looking at the trace after 5M cycles, [0xFFFC] has become 0xDF, which means a push operation must have hit that location.

That 0xDF value came from a PUSH AF instruction when SP was 0xFFFD. For SP to reach 0xFFFD, it had to come from RST 30h at 0x003C decrementing from 0xFFFF. The only way SP could be 0xFFFF is if it wrapped around after the initial RST 38h at 0xEEFF brought it down from 0x0001 to 0xFFFF. Then RST 30h decrements it to 0xFFFD, writing the return address bytes to [0xFFFE] and [0xFFFD].

Next, RST 18h at 0x0033 decrements SP again to 0xFFFB, and this is when [0xFFFC] gets written for the first time with the value 0x00. The inner DJNZ loop then runs 255 times, each iteration consuming 10 bytes of stack space, which brings SP down to around 0xF605. After the loop exits, RST 38h at 0x0021 pushes the return address 0x0022 onto the stack.

Now at 0x0038, SP is at 0xF603. The code performs an ADD A,A operation that transforms A from 0xDF to 0xBE with the sign flag set, then checks the zero flag—which isn't set, so the conditional jump is skipped. Then it decrements C and continues.