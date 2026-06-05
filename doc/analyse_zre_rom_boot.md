# Analyse: ZRE Boot-ROM (K2526) – Bootsequenz und Fehleranalyse

**Stand:** 2026-05-28  
**ROM-Datei:** `doc/EPROMS/zre.rom` (1024 Byte, 0x0000–0x03FF)  
**Disk-Image:** `disks/cpadisk.img`  
**CPU:** Z80 (ZVE1 auf K2526-Karte), 2,45 MHz  
**Laufwerk:** 5,25"-Floppy, 300 RPM → 200 ms / Umdrehung → **490.000 CPU-Takte / Umdrehung**

---

## 1. Speicher-Übersicht während des Boots

| Adressbereich | Phase             | Inhalt                                              |
|---------------|-------------------|-----------------------------------------------------|
| 0x0000–0x03FF | ROM aktiv         | ZRE-Boot-EPROM (K2526, 1 KB)                        |
| 0x0000–0xFFFF | nach ROM-Disable  | OPS-Karte (64 KB RAM, K3526)                        |
| 0x0000–0x07FF | nach RAM-Clear    | Nullen (durch LDI-Schleife gelöscht)                |
| 0x00BA–0x03FF | nach LDIR-Kopie   | ROM-Inhalt in RAM gespiegelt (838 Byte)             |
| 0x0000–0x0002 | nach ROM-Disable  | `C3 DD 01` = `JP 0x01DDh` (Strobe-Loop-Einstieg)   |

### RAM-Variablen des Boot-ROMs

| Adresse | Bedeutung                                           | Initialwert |
|---------|-----------------------------------------------------|-------------|
| 0x03F0  | Ladeadresse (Sektordaten)                           | 0x0400      |
| 0x03F3  | Erwarteter Zylinder für IDAM-Vergleich              | 0x00        |
| 0x03F4  | Erwarteter Kopf (Head)                              | 0x00        |
| 0x03F5  | Erwarteter Sektor (Sektor-ID)                       | 0x00        |
| 0x03F6  | Erwarteter Size-Code                                | 0x00        |
| 0x03F7  | Indexpuls-Zähler (startet bei 4, sinkt auf 0)       | 4           |
| 0x03F8  | Flag: /STR-Strobe wurde ausgelöst                   | 0           |
| 0x03FA  | Seek-Retry-Counter                                  | 5           |
| 0x03FC  | Aktueller Ctrl-Byte-Wert für Port 0x10              | 0xEE        |
| 0x03FD  | IDAM-Pfad / Step-Steuerung (**Kernproblem!**)       | 0x00        |

---

## 2. Boot-Sequenz (Überblick)

```
RESET (0x0000)
  │
  ├─ RAM[0x0000..0x07FF] löschen (LDI-Schleife, schreibt 0x00)
  ├─ IM 2, I=0, SP=0x07E0
  ├─ BS-PIO init: Vektor A=0xB8, Mode 3, Maske 0x7F
  ├─ RAM-Test (62 Iterationen, IX-Schritte à 0x0400)
  ├─ LDDR: Disk-Tabelle ROM[0x044C..0x046F] → RAM[0x004C..0x006F]
  ├─ JR Z → ROM_COPY (Flag bei 0x044E = 0 beim ersten Boot)
  │
  ├─ ROM_COPY (0x00BC):
  │     LDIR: ROM[0x00BA..0x03FF] → RAM[0x00BA..0x03FF]
  │     → RAM[0x00BA]=0xC7, RAM[0x00BB]=0x01  ← ISR-Vektor!
  │
  ├─ POST_COPY_INIT (0x00C6):
  │     Port B: Modus 3, Vektor 0xE2
  │     ROM DISABLE: OUT(0Ah), A AND 0xF6  ← Bit0=0 deaktiviert ROM
  │     EI
  │     K5122 ctrl PIO Port A: Modus 1 → Modus 0 (Output) → Vektor=0xBA ← KRITISCH
  │     RAM[0x0000..0x0002] = C3 DD 01  (JP 0x01DDh)
  │     RAM[0x03FC] = 0xEE
  │     Port 0x18 (8212 Drive Select) = 0xEE
  │
  ├─ DRIVE_DETECT_LOOP (0x0110):
  │     Liest Port 0x12 (ctrl PIO Port B = Drive Status)
  │     RLCA: Bit7 → Carry; JR NC → WAIT_INDEX_SETUP (Drive ready)
  │     Kein Drive → Step-Pulse an Track 0 → Retry
  │
  ├─ WAIT_INDEX_SETUP (0x0128):
  │     [0x03F7] = 4  (4 Indexpulse abwarten)
  │     K5122 ctrl PIO Port A: Interrupt-Control (INTENA=1, OR, mask=0xFF)
  │     → IE-Flag gesetzt; der nächste /ASTB-Puls (Indexpuls) ruft ISR 0x01C7
  │
  ├─ WAIT_INDEX_LOOP (0x0135):
  │     Liest [0x03F7]; lädt A=0x87; JR Z → STORE_03FD
  │     Timeout-Schleife (DEC C / DJNZ) → BOOT_FAIL
  │
  ├─ [ISR 0x01C7 – pro Indexpuls]:
  │     [0x03F7]--; falls 0: OUT(0x10), 0xBD (/STR setzen), [0x03F8]=1
  │     RETI
  │
  ├─ STORE_03FD (0x0153):
  │     [0x03FD] = 0x87  ← Bit1=1 (NZ-Pfad), Bit7=1 (kein Step)
  │
  ├─ SEEK_SETUP (0x015C):
  │     Zylinder 0, Sektor 0, Kopf 0 setzen
  │     CALL 0x0194 (Laufwerks-Initialisierung)
  │
  └─ STROBE_LOOP / IDAM-Suche (0x01DD):
        Sucht IDAM-Header im Sektor-Puffer
        → Liest Bootsektor nach 0x0400
        → Springt zu 0x0437 (geladener Code)
```

---

## 3. Interrupt-Mechanismus: K5122 ctrl PIO → ISR 0x01C7

**Dies ist der wichtigste und fehleranfälligste Teil des Boots.**

### 3.1 Vektor-Tabelle (IM 2, I=0)

Mit `IM 2` und `I=0` lautet die ISR-Adresse: `memory[(I<<8) | vector] = memory[vector]`.

| Interrupt-Quelle          | Vektor  | ISR-Adresse (nach LDIR) | Ziel                     |
|---------------------------|---------|--------------------------|--------------------------|
| BS-PIO Port A (/ASTB)     | 0xB8    | RAM[0xB8,0xB9] = {7A,00} | 0x007A (ROM-Code)        |
| K5122 ctrl PIO Port A     | **0xBA**| RAM[0xBA,0xBB] = {C7,01} | **0x01C7 (Dekrement-ISR)** |
| K5122 ctrl PIO Port B     | 0xE2    | RAM[0xE2,0xE3]           | (nicht verwendet)        |

**RAM[0xBA] = 0xC7, RAM[0xBB] = 0x01** wird durch den LDIR-Befehl bei 0x00C4 gesetzt:
ROM[0x00BA]=0xC7 (RST-00h-Opcode im disassemblierten Code, aber hier Datenadresse)
ROM[0x00BB]=0x01

### 3.2 K5122 ctrl PIO Port A – Konfigurationssequenz

| Adresse | Wert | OUT-Port | Bedeutung                                    |
|---------|------|----------|----------------------------------------------|
| 0x00DF  | 0x7F | 0x11     | Modus 1 (Input)                              |
| 0x00E7  | 0x3F | 0x11     | **Modus 0 (Output)** ← entscheidend!         |
| 0x00ED  | 0xBA | 0x11     | **Interrupt-Vektor = 0xBA** → ISR 0x01C7    |
| 0x012F  | 0x97 | 0x11     | Interrupt-Control: INTENA=1, OR, mask follows |
| 0x0133  | 0xFF | 0x11     | Interrupt-Maske (in Modus 0 ignoriert)       |

**Modus 0 (Output) + /ASTB-Signal**: Im Z80-PIO Modus 0 löst die fallende Flanke des
/ASTB-Eingangs einen Interrupt aus (wenn IE=1). Der Index-Puls des Laufwerks treibt /ASTB.

### 3.3 Ablauf bei jedem Index-Puls

```
Laufwerk dreht → Index-Puls → k5122.cpp: ctrl_pio_.setASTB(false)
  → Z80PIO::setASTB(false): porta_.mode==0 && porta_.ie → porta_.pending=true
  → K5122::hasInterrupt() → true
  → Bus INT-Leitung aktiv
  → CPU akzeptiert INT (IFF1=1)
  → interruptAcknowledge() → K5122::getVector() → ctrl_pio_.getVector()
  → porta_.int_vector = 0xBA zurückgegeben
  → CPU: ISR = memory[0x00BA, 0x00BB] = {0xC7, 0x01} = 0x01C7
  → ISR_DECREMENT_03F7 (0x01C7): [0x03F7]--
  → Nach 4 Pulsen: [0x03F7]=0 → [0x03FD]=0x87
```

---

## 4. Das [0x03FD]-Problem: IDAM-Pfad-Auswahl

### 4.1 Bedeutung von [0x03FD]

| Wert  | Bit7 | Bit1 | Verhalten bei OUT(0x10),A | IDAM-Pfad        |
|-------|------|------|---------------------------|------------------|
| 0x00  | 0    | 0    | **Step-Puls!** (Bit7=0 → /ST fallende Flanke) | Z-Pfad (falsch!) |
| 0x87  | 1    | 1    | Kein Step (Bit7=1)        | NZ-Pfad (richtig) |

### 4.2 IDAM-Suchschleife (0x025A–0x0265)

```
STROBE_LOOP_BODY (0x025A):
  A = [0x03FD]          ; Pfad-/Step-Konfigurationsbyte
  BIT 1, A              ; Bit1 testen
  OUT (0x10), A         ; GEFAHR: wenn Bit7=0 → Step-Puls ausgelöst!
  IN A, (0x16)          ; buf[0] lesen
  JR Z, READ_IDAM_Z     ; Bit1=0 → Z-Pfad
  JR READ_IDAM_NZ       ; Bit1=1 → NZ-Pfad

READ_IDAM_Z (0x0205):   ; FALSCH: liest 5 Bytes vor dem Vergleich
  IN A, (0x16)          ; buf[1]
  IN A, (0x16)          ; buf[2]  
  IN A, (0x16)          ; buf[3]
  → IN A, (0x16)        ; buf[4] = sector_id
  CP B                  ; sector_id vs. B=0xFE → IMMER FALSCH!

READ_IDAM_NZ (0x020B):  ; RICHTIG: liest 2 Bytes vor dem Vergleich
  IN A, (0x16)          ; buf[1] = 0xFE (IDAM-Marker)
  CP B                  ; 0xFE vs. B=0xFE → TREFFER!
```

### 4.3 IDAM-Puffer-Layout im Emulator

```
sector_buf_ = [0x00, 0xFE, cyl, head, sector_id, size_code, data...]
               buf[0] buf[1] buf[2] buf[3] buf[4]    buf[5]
```

Mit dem **NZ-Pfad** ([0x03FD]=0x87):
- Liest buf[0]=0x00 und buf[1]=0xFE → vergleicht 0xFE mit B=0xFE → ✓ Treffer
- Dann: buf[2]=cyl vs. E, buf[3]=head vs. D, buf[4]=sector_id vs. L, buf[5]=size_code vs. H

Mit dem **Z-Pfad** ([0x03FD]=0x00):
- Liest buf[0]..buf[4] → vergleicht sector_id mit B=0xFE → immer Fehler → Endlosschleife

### 4.4 ZVE2-Architektur: Sektor-Lade-Protokoll

ZVE2 ist ein zweiter Z80-Prozessor auf der K2526-Karte, der als DMA-CPU für den Floppy-Transfer fungiert.
ZVE2 startet bei PC=0x0000 → RAM[0x0000..0x0002] = `C3 DD 01` = JP 0x01DD (STROBE_LOOP).

**Handshake-Protokoll ZVE1 → ZVE2 (CALL 0x0194, Zeilen 0x0194–0x01B5):**

```
0194: LD HL,03F8h              ; HL → [0x03F8] (Abschluss-Flag)
      LD (HL), 0               ; [0x03F8] = 0 (löschen, ZVE2 noch nicht fertig)
      LD (03F7h), 16           ; ISR-Timeout-Zähler auf 16 Indexpulse setzen
      OUT (10h), 0xA5          ; /STR fallende Flanke → doReadSector() + BUSRQ assertieren
      OUT (04h), xxx           ; ZVE2-Reset freigeben → ZVE2 startet bei PC=0x0000
      RET                      ; HL zeigt weiterhin auf [0x03F8]
```

**Warte-Schleife ZVE1 (0x0168–0x016F):**

```
0168: LD A,(HL)     ; A = [0x03F8]  (HL gesetzt von CALL 0x0194)
0169: OR A
016A: JR Z,0168h   ; warten solange [0x03F8]=0 (ZVE2 läuft noch)
016C: DEC A
016D: JR Z,014Bh   ; [0x03F8]=1 → ISR-Timeout → Fehlerbehandlung
016F: CALL 01B6h   ; [0x03F8]=3 → ZVE2 fertig → Bootsektor prüfen
```

**[0x03F8]-Semantik:**

| Wert | Setzer                | Bedeutung                                          |
|------|-----------------------|----------------------------------------------------|
| 0    | CALL 0x0194 (ZVE1)    | ZVE2 läuft noch – ZVE1 wartet                      |
| 1    | ISR 0x01C7 (bei ISR)  | Indexpuls-Timeout: Sektor nicht lesbar → Fehler    |
| 3    | ZVE2 (ca. 0x026F)    | Alle Sektoren geladen, RAM[0x0400] bereit → weiter |

**ZVE2 STROBE_LOOP – /STR-Flanken-Analyse:**

ZVE2 generiert beim ersten Schleifendurchlauf genau **eine** fallende /STR-Flanke:

| Adresse | Befehl              | Bit3 vorher→nachher | /STR-Flanke | Wirkung                        |
|---------|---------------------|----------------------|-------------|--------------------------------|
| 0x01F6  | `OUT(10h), 0xBD`   | 0→1                  | keine       | /STR high (prev_ctrl_a_=0xBD)  |
| **0x01FA** | **`OUT(10h), 0xB5`** | **1→0**          | **FALLEND** | doReadSector() + Bug 7!        |
| 0x024B  | `OUT(10h), 0xB5`   | 0→0                  | keine       | /STR bleibt low                |
| 0x025F  | `OUT(10h), [03FD]` | 0→0                  | keine       | Kein Step, kein neues /STR     |

Nach Abschluss aller Sektoren schreibt ZVE2 `[0x03F8]=3`, und ZVE1 verlässt die Warte-Schleife.

---

## 5. Identifizierte Fehler / Probleme

### Problem 1: Index-Puls-Timing (KRITISCH – behoben)

**Datei:** `core/cards/k5122/k5122.h`, Zeile ~355

```cpp
// FALSCH:
static constexpr int kIndexPeriodCycles = 50000; // ~200ms/4 = pulse every ~50k cycles
// Tatsächlich: 50000 / 2.45MHz = 20ms → ~49 Pulse/s statt 5!

// RICHTIG:
static constexpr int kIndexPeriodCycles = 490000; // 2.45MHz / 5Hz = 490000 Takte/Umdrehung
```

**Berechnung:**
- CPU-Takt: 2,45 MHz = 2.450.000 Hz
- 300 RPM = 5 Umdrehungen/s → Periode = 200 ms
- Takte pro Umdrehung: 2.450.000 / 5 = **490.000**
- Eine vollständige Spur muss gelesen werden können, bevor der nächste Puls kommt

**Auswirkung:** Mit 50.000 Zyklen feuert der Puls ca. 49× pro Sekunde statt 5×. Das ISR
`ISR_DECREMENT_03F7` wird viel zu schnell aufgerufen, bevor der Code in `WAIT_INDEX_SETUP`
(0x0128) ausgeführt wurde und IE gesetzt ist. Außerdem wird das Timing gegenüber der
Sektor-Lesezeit vollständig falsch.

### Problem 2: [0x03FD] bleibt 0x00 bei fehlerhaftem Interrupt

**Ursache:** Wenn der K5122 ctrl PIO Port A Interrupt nicht feuert (weil IE zum Zeitpunkt
des Pulses noch nicht gesetzt ist), bleibt [0x03F7]=4 und [0x03FD]=0x00.

**Symptom:**
- Z-Pfad wird genommen: CP B vergleicht sector_id (≠ 0xFE) → Endlosschleife
- Bei OUT(0x10) mit Bit7=0 wird Step-Puls erzeugt → Kopf springt auf falschen Zylinder

**Lösung:** Nach Korrektur des Index-Puls-Timings auf 490.000 Zyklen ergibt sich:
1. Boot-Code bis 0x012D ≈ 500-1000 Takte → lange vor erstem Puls
2. IE wird gesetzt bei 0x012F (OUT(0x11), 0x97)
3. Erster Puls nach ≈ 490.000 Takten → ISR feuert korrekt

### Problem 3: Drive-Status / ctrl PIO Port B Initialisierung

**Datei:** `core/cards/k5122/k5122.cpp`

Die Drive-Detect-Schleife bei 0x0110:
```
IN A, (0x12)   ; liest ctrl PIO Port B
RLCA           ; rotiert links: altes Bit7 → Carry
JR NC, 0x0128  ; Jump if NC: Carry=0 → altes Bit7=0 → Drive bereit
```

Das `/RDYL`-Signal (Drive Ready, active low) muss in Port B korrekt gesetzt sein.
Der Emulator muss sicherstellen, dass `/RDYL` (Bit0 von Port B = 0 wenn bereit) und
dass Bit7 (/TO = kein Timeout = 1) korrekt gesetzt werden.

**Zu prüfen:** Gibt `ctrl_pio_.portBWrite(s)` in `k5122.cpp` den korrekten Status zurück?
Speziell: Wie verhält sich Bit7 bei einem gemounteten Disk-Image?

### Problem 4: BUSRQ-Deadlock bei ZVE2 (behoben)

**Datei:** `core/machines/a5120/a5120.cpp`

```cpp
// Nach Fix (korrekt):
if (bus_.isBUSRQ()) {
    if (!zre_.isZVE2InReset() && !zre_.isZVE2Waiting()) {
        zre_.zve2Step();   // nur wenn ZVE2 wirklich läuft
    } else {
        afs_.dmaUpdate();  // Fallback: BUSRQ direkt freigeben
    }
    remaining--;
    continue;
}
```

**Hintergrund:** Wenn ZVE2 gestoppt ist (`zve2_wait_=true`), gibt `zve2Step()` 0 zurück
und BUSRQ wird nie freigegeben. ZVE1 kann Port 0x16 nicht lesen.

---

## 6. Neue Fehler (entdeckt 2026-05-28, ZVE2-bezogen)

### Bug 5: ZVE2 bleibt nach Reset-Freigabe im WAIT-Zustand (behoben)

**Datei:** `core/cards/k2526/k2526.cpp`, Port-0x04-Handler

**Ursache:** `onBSPIOPortBOut()` setzt `zve2_wait_=true`, wenn POST_COPY_INIT bit3=0 an BS-PIO Port B
schreibt. Der Port-0x04-Handler gab ZVE2 aus dem Reset, löschte aber nicht `zve2_wait_`.

```cpp
// FALSCH (vorher):
zve2_reset_ = false;
zve2_.reset();

// RICHTIG (nachher):
} else if ((data & 0x01) && zve2_reset_) {
    zve2_reset_ = false;
    zve2_wait_ = false;  // /RESET-Freigabe löscht auch /WAIT
    zve2_.reset();
    LOG_INFO("K2526", "ZVE2 gestartet: PC=0000H (OUT 04H=0x%02X)", data);
}
```

**Folge ohne Fix:** `zve2Step()` gibt sofort 0 zurück (ZVE2 im WAIT). `a5120.cpp` fällt auf
`dmaUpdate()` zurück und gibt BUSRQ sofort frei. ZVE2 führt nie einen einzigen Befehl aus.

---

### Bug 6: sector_id im IDAM-Header 0-basiert statt 1-basiert (behoben)

**Datei:** `core/cards/k5122/k5122.cpp`, `doReadSector()`, ca. Zeile 530

**Ursache:** ZVE2 initialisiert L'=[0x03F5]=1 (1-basierte Sektor-ID für Sektor 1). Der IDAM-Header
im `sector_buf_` enthielt jedoch `current_sector_ - 1` (0-basiert, Wert 0 für den ersten Sektor).

```cpp
// FALSCH:
sector_buf_.push_back(current_sector_ - 1);  // sector ID 0-basiert → Wert 0

// RICHTIG:
sector_buf_.push_back(current_sector_);  // sector ID 1-basiert, stimmt mit L'=[0x03F5]=1 überein
```

**Folge ohne Fix:** IDAM-Vergleich bei 0x021C (`CP L`): sector_id=0 ≠ L'=1 → JR NZ,01F8h →
Endlosschleife. ZVE2 findet nie einen passenden IDAM-Header.

---

### Bug 7: Vorzeitige BUSRQ-Freigabe in handleCtrlPortAWrite (offen)

**Datei:** `core/cards/k5122/k5122.cpp`, `handleCtrlPortAWrite()` und `ioRead()`

**Ursache:** ZVE2 führt als erste Aktion bei 0x01FA `OUT(10h, 0xB5)` aus.
Zuvor hat ZVE1 via ISR 0x01D6 den Wert 0xBD (bit3=1, /STR high) geschrieben.
ZVE2 schreibt 0xB5 (bit3=0, /STR low) → **fallende /STR-Flanke** bei bereits gehaltenem BUSRQ.

In `handleCtrlPortAWrite()` löst exakt diese Konstellation den falschen Zweig aus:

```cpp
if ((prev_ctrl_a_ & 0x08) && !(data & 0x08)) {  // fallende /STR-Flanke erkannt
    bool is_write = !(data & 0x01);
    if (bus_.isBUSRQ()) {
        // BUG: Dieser Zweig läuft auch wenn ZVE2 selbst /STR auslöst!
        if (is_write) { doWriteSector(); }
        dma_pending_ = false;
        bus_.releaseBUSRQ();   // ← gibt BUSRQ nach ~14 ZVE2-Instruktionen frei
    }
    ...
```

**Folge:** Nach ca. 14 ZVE2-Instruktionen (bis zu `OUT(10h,0xB5)` bei 0x01FA) wird BUSRQ freigegeben.
`a5120.cpp` ruft `zve2Step()` nicht mehr auf. ZVE2 kann Port 0x16 nicht lesen, schreibt
nie [0x03F8]=3. ZVE1 bleibt in der Warte-Schleife bei 0x0168 hängen.

**Zusätzliches Problem in `ioRead(0x16)`:**

```cpp
if (buf_pos_ >= sector_buf_.size()) {
    transferring_ = false;
    result = 0xFF;
    if (bus_.isBUSRQ()) {
        dma_pending_ = false;
        bus_.releaseBUSRQ();   // ← feuert auch mitten in ZVE2's INIR-Schleife
    }
    current_sector_ = (current_sector_ % tf->secs_per_track) + 1;
}
```

**Geplanter Fix:**
1. Im „BUSRQ gehalten + READ /STR"-Zweig von `handleCtrlPortAWrite()`: BUSRQ **nicht** freigeben.
   Stattdessen: wenn `sector_buf_` erschöpft, nächsten Sektor via `doReadSector()` laden; sonst
   bestehenden Puffer behalten. BUSRQ bleibt gehalten, ZVE2 läuft weiter.
2. BUSRQ-Auto-Freigabe in `ioRead(0x16)` entfernen oder auf `write_mode_` beschränken.
3. ZVE2-Completion-Erkennung: wenn ZVE2 `OUT(11h, 0x03)` schreibt (ca. 0x0269) oder
   [0x03F8]=3 setzt, dann BUSRQ freigeben, damit ZVE1 die Warte-Schleife verlässt.

---

## 7. Vollständig kommentiertes ROM-Disassembly

```
; =============================================================================
; ZRE EPROM K2526 – Boot-ROM (1 KB, 0x0000–0x03FF)
; CPU: Z80 (ZVE1), 2,45 MHz, IM 2, I=0
; =============================================================================

; ===== RESET ENTRY POINT =====
RESET_ENTRY:           0000  NOP
                       ; BC=0x0800 = 2048: Löscht 2KB RAM (Ziel- und Quelle-Ptr beginnen bei 0)
                        0001  LD BC,0800h
                        0004  LD D,C                         ; D=0
                        0005  LD E,C                         ; E=0
                        0006  LD H,C                         ; H=0
                        0007  LD L,C                         ; L=0
                       ; LDI-Trick: schreibt ROM[0]=0x00 (NOP) nach RAM[0..2047]
                       ; DE=HL=0; LDI: (DE++)=(HL); DEC HL: hält HL bei 0; so bleibt Quelle=0
CLEAR_LOOP:            0008  LDI
                        000A  DEC HL
                        000B  JP PE,0008h                    ; loop bis BC=0

; ===== HARDWARE-INITIALISIERUNG =====
INIT_PORTS:            000E  XOR A
                        000F  OUT (02h),A                    ; Port 0x02: Bank-Select = 0 (Bank 0 aktiv)
                        0011  LD SP,07E0h                    ; Stack-Pointer unterhalb 0x0800
                        0014  IM 2                           ; Interrupt-Modus 2
                        0016  LD A,00h
                        0018  LD I,A                         ; I=0: Vektor-Tabelle bei 0x0000

; ===== BS-PIO (K2526 PIO, Ports 0x08/09/0A/0B) INIT =====
INIT_BS_PIO:           001A  LD A,7Fh
                        001C  OUT (09h),A                    ; Port A Ctrl: 0x7F=mode1(Input)
                        001E  OUT (0Bh),A                    ; Port B Ctrl: 0x7F=mode1(Input)
                        0020  LD A,FFh
                        0022  OUT (08h),A                    ; Port A Data = 0xFF
                        0024  OUT (0Ah),A                    ; Port B Data = 0xFF
                        0026  LD A,B8h
                        0028  OUT (09h),A                    ; Port A: Interrupt-Vektor = 0xB8
                        002A  LD A,FFh
                        002C  OUT (09h),A                    ; Port A: Mode 3 (Bit-Control)
                        002E  LD A,7Fh
                        0030  OUT (09h),A                    ; Mode-3 I/O-Maske: Bit7=Output, Bits6..0=Input

; ===== RAM-TEST / BANK-WALK (62 Durchläufe, je 1KB) =====
RAM_TEST:              0032  LD IX,0800h                    ; Startadresse 0x0800
                        0036  LD (0462h),IX                  ; IX für später sichern
                        003A  LD HL,044Eh                    ; HL → Config-Flag (in RAM, =0 nach Clear)
                        003D  LD DE,0400h                    ; DE = IX-Inkrement (1 KB pro Schritt)
                        0040  LD B,3Eh                       ; B = 62 Iterationen

RAM_TEST_LOOP:         0042  LD C,(IX+0)                    ; lese aktuellen Inhalt
                        0045  LD A,D7h
                        0047  OUT (09h),A                    ; BS-PIO Port A: Int-Ctrl INTENA=1, OR, H-level, mask follows
                        0049  LD A,9Fh
                        004B  OUT (09h),A                    ; Interrupt-Maske = 0x9F
                        004D  EI                             ; Interrupts freigeben
                        004E  LD (IX+0),FFh                  ; schreibe 0xFF → RAM-Test
                        0052  NOP                            ; Wartetakt
                        0053  DI                             ; Interrupts sperren
                        0054  LD A,47h
                        0056  OUT (09h),A                    ; BS-PIO Port A: INTENA=0 (Interrupt deaktivieren)
                        0058  ADD IX,DE                      ; IX += 0x0400 (nächster 1KB-Block)
                        005A  DJNZ 0042h                     ; B-- / loop

; ===== DISK-TABELLE KOPIEREN (ROM → RAM) =====
COPY_DISK_TBL:         005C  LD HL,046Fh                    ; ROM-Quelle (außerhalb 1KB → liest RAM = 0x00)
                        005F  LD DE,006Fh                    ; RAM-Ziel
                        0062  LD BC,0024h                    ; 36 Byte
                        0065  LDDR                           ; kopiere abwärts

; ===== BOOT-FLAG PRÜFEN =====
CHECK_BOOT_FLAG:       0067  LD HL,044Eh                    ; Adresse des Boot-Flags (nach RAM-Clear = 0)
                        006A  BIT 0,(HL)                     ; Bit0 testen
                        006C  JR Z,00BCh                     ; = 0 → erster Boot → ROM_COPY

; Wenn Flag gesetzt: ROM wurde bereits kopiert, direkt zu Hauptcode springen
SKIP_ROM_COPY:         006E  LD HL,0800h
                        0071  LD (004Ch),HL
                        0074  LD HL,(0462h)
                        0077  LD L,9Dh
                        0079  JP (HL)                        ; Sprung zum Hauptprogramm

; ===== ISR-CODE FÜR BS-PIO PORT A (Vektor 0xB8, ISR-Adresse RAM[0xB8,0xB9]={7A,00}=0x007A) =====
; Wird aufgerufen wenn BS-PIO /ASTB während des RAM-Tests feuert (Zuverlässigkeitstest)
ISR_BS_PIO_A:          007A  LD A,47h
                        007C  OUT (09h),A                    ; BS-PIO Port A Interrupt abschalten
                        007E  LD A,FFh
                        0080  CP (IX+0)                      ; Prüfe ob RAM 0xFF enthält (Test bestanden?)
                        0083  JR NZ,00A7h                    ; nicht 0xFF → Fehler-Pfad
                        0085  LD (IX+0),C                    ; restore original value
                        0088  BIT 0,(HL)
                        008A  JR NZ,0092h
                        008C  LD (0460h),IX
                        0090  RETI
                        0092  LD (0464h),IX
                        0096  BIT 6,(HL)
                        0098  JR NZ,00A5h
                        009A  SET 6,(HL)
                        009C  LD (046Ch),IX
                        00A0  LD A,05h
                        00A2  LD (046Ch),A
                        00A5  RETI
                        00A7  BIT 0,(HL)
                        00A9  JR NZ,00B1h
                        00AB  SET 0,(HL)
                        00AD  LD (0462h),IX
                        00B1  LD (0464h),IX
                        00B5  RETI

; ===== ROM-DATEN: ISR-VEKTOREN (werden durch LDIR nach RAM kopiert) =====
; Nach LDIR bei 0x00C4:
;   RAM[0xB8,0xB9] = {7A, 00} → ISR für BS-PIO-Vektor 0xB8 = 0x007A
;   RAM[0xBA,0xBB] = {C7, 01} → ISR für K5122-Vektor  0xBA = 0x01C7  ← KRITISCH
ROM_DATA_BA_BB:        00B7  NOP                            ; Byte = 0x00 = RAM[0xB7]
                        00B8  LD A,D                         ; Byte = 0x7A = RAM[0xB8] ← ISR-Adresse low für vec 0xB8
                        00B9  NOP                            ; Byte = 0x00 = RAM[0xB9] ← ISR-Adresse high = 0x007A
                        00BA  RST 00h                        ; Byte = 0xC7 = RAM[0xBA] ← ISR-Adresse low für vec 0xBA
                        00BB  LD BC,BA21h                    ; enthält 0x01 = RAM[0xBB] ← ISR-Adresse high = 0x01C7

; ===== ROM → RAM KOPIEREN (erster Boot) =====
; Ziel: Laufzeitcode im RAM verfügbar machen
ROM_COPY:              00BC  LD HL,00BAh                    ; HL = 0x00BA (Quell-Start in ROM)
                        00BF  LD D,H                         ; D = 0x00
                        00C0  LD E,L                         ; E = 0xBA → DE = 0x00BA (Ziel in RAM)
                        00C1  LD BC,0346h                    ; BC = 838 Byte (0x00BA..0x03FF)
                        00C4  LDIR                           ; kopiere ROM[0x00BA..0x03FF] → RAM[0x00BA..0x03FF]

; ===== POST-COPY HARDWARE-INIT =====
POST_COPY_INIT:        00C6  LD A,FFh
                        00C8  OUT (0Bh),A                    ; BS-PIO Port B: Mode 3 (bit control)
                        00CA  OUT (13h),A                    ; K5122 ctrl PIO Port B ctrl = 0xFF → Mode 3
                        00CC  LD A,E2h
                        00CE  OUT (0Bh),A                    ; BS-PIO Port B: Interrupt-Vektor = 0xE2
                        00D0  IN A,(0Ah)                     ; lese BS-PIO Port B Daten (Hardware-Config)
                        00D2  AND F6h                        ; AND 0xF6 = ~0x09 → löscht Bit0 und Bit3
                        00D4  OUT (0Ah),A                    ; Schreibe zurück: Bit0=0 → ROM DEAKTIVIERT

ENABLE_INTS_INIT:      00D6  EI                             ; Interrupts freigeben (läuft jetzt aus RAM)
                        00D7  LD A,F3h
                        00D9  OUT (13h),A                    ; K5122 ctrl PIO Port B: 0xF3=int-ctrl (INTENA=1)
                        00DB  LD A,7Fh
                        00DD  OUT (12h),A                    ; K5122 ctrl PIO Port B Data = 0x7F
                        00DF  OUT (11h),A                    ; K5122 ctrl PIO Port A ctrl: 0x7F = Mode 1 (Input)
                        00E1  LD A,A9h
                        00E3  OUT (10h),A                    ; K5122 ctrl PIO Port A Data = 0xA9 (Drive-Ctrl)
                        00E5  LD A,3Fh
                        00E7  OUT (11h),A                    ; K5122 ctrl PIO Port A: 0x3F = Mode 0 (Output)!
                        00E9  OUT (15h),A                    ; K5122 data PIO Port A ctrl: Mode 0
                        00EB  LD A,BAh
                        00ED  OUT (11h),A                    ; *** K5122 ctrl PIO Port A: VEKTOR = 0xBA ***
                                                             ; /ASTB-Flanke (Indexpuls) → ISR = 0x01C7

                        00EF  LD A,04h
                        00F1  LD (07F2h),A                   ; Seek-Retry-Count = 4
                        00F4  LD A,C3h
                        00F6  LD (0000h),A                   ; RAM[0x0000] = 0xC3 (JP-Opcode)
                        00F9  LD HL,01DDh
                        00FC  LD (0001h),HL                  ; RAM[0x0001,2] = 0x01DD → JP 0x01DD
                        00FF  LD A,EEh
                        0101  LD (03FCh),A                   ; [0x03FC] = 0xEE (initialer Ctrl-Wert)
                        0104  OUT (18h),A                    ; 8212 Drive-Select = 0xEE (kein Drive)
                        0106  LD HL,0400h
                        0109  LD B,L                         ; B = 0x00
                        010A  LD (03F0h),HL                  ; Ladeadresse = 0x0400
                        010D  LD DE,5006h                    ; D=0x50 (Retry-Outer), E=0x06

; ===== LAUFWERK-ERKENNUNG =====
DRIVE_DETECT_LOOP:     0110  IN A,(12h)                     ; ctrl PIO Port B: Drive-Status
                        0112  RLCA                           ; altes Bit7 → Carry
                        0113  JR NC,0128h                    ; Carry=0 (Bit7=0) → Drive bereit → WAIT_INDEX
                        0115  DEC D                          ; Outer-Retry--
                        0116  JR Z,0140h                     ; 0 → BOOT_FAIL
SEND_SEEK_TRACK0:      0118  LD A,09h
                        011A  LD B,A
                        011B  OUT (10h),A                    ; Step-Pulse für Track-0-Suche
                        011D  LD A,89h
                        011F  OUT (10h),A
                        0121  DEC C
                        0122  JR NZ,0121h                    ; innerer Delay
                        0124  DJNZ 0121h
                        0126  JR 0110h                       ; nächster Versuch

; ===== 4 INDEXPULSE ABWARTEN =====
WAIT_INDEX_SETUP:      0128  LD HL,03F7h
                        012B  LD (HL),04h                    ; [0x03F7] = 4 (Puls-Zähler)
                        012D  LD A,97h
                        012F  OUT (11h),A                    ; K5122 ctrl PIO Port A: INTENA=1, OR, mask follows
                        0131  LD A,FFh
                        0133  OUT (11h),A                    ; Interrupt-Maske (in Modus 0 irrelevant; IE gesetzt)

WAIT_INDEX_LOOP:       0135  LD A,(HL)                      ; A = [0x03F7] (Indexpuls-Zähler)
                        0136  OR A                           ; Z-Flag wenn 0
                        0137  LD A,87h                       ; A = 0x87 (vorgeladen für [0x03FD])
                        0139  JR Z,0153h                     ; [0x03F7]=0 → 4 Pulse empfangen → weiter
                        013B  DEC C                          ; Timeout-Zähler inner
WAIT_INDEX_INNER:      013C  JR NZ,0135h
                        013E  DJNZ 0135h                     ; Timeout-Zähler outer
BOOT_FAIL:             0140  LD A,(03FCh)
                        0143  CP 77h
                        0145  JP Z,027Eh
                        0148  RLCA
                        0149  JR 0101h                       ; nächsten Drive-Ctrl-Wert versuchen
                        014B  DEC E
                        014C  JR Z,0140h
WAIT_INDEX_OK:         014E  LD A,(03FDh)
                        0151  XOR 02h

; ===== [0x03FD] = 0x87 SETZEN =====
STORE_03FD:            0153  LD (03FDh),A                   ; [0x03FD]=0x87: NZ-Pfad + kein Step

                        0156  LD HL,8001h
                        0159  LD (07F0h),HL
SEEK_SETUP:            015C  LD H,00h
                        015E  LD (03F5h),HL                  ; [0x03F5]=0, [0x03F6]=0 (Sektor, Size)
                        0161  LD L,H
                        0162  LD (03F3h),HL                  ; [0x03F3]=0, [0x03F4]=0 (Zylinder, Kopf)
                        0165  CALL 0194h                     ; Laufwerks-Initialisierung
                        0168  LD A,(HL)
                        0169  OR A
                        016A  JR Z,0168h                     ; warte auf [0x03F8] ≠ 0 (ZVE2 Abschluss oder ISR-Timeout)
STROBE_INIT:           016C  DEC A
                        016D  JR Z,014Bh
                        016F  CALL 01B6h                     ; Bootsektor-Signatur prüfen
                        0172  JR NZ,0140h                    ; Signatur falsch → Boot-Fail
                        0174  CALL 0437h                     ; geladenen Code ausführen

; ... (weiterer Code: Neustart, Kernel-Übergabe, etc.)

; ===== ISR: INDEXPULS-ZÄHLER DEKREMENTIEREN =====
; Aufgerufen durch: Interrupt-Vektor 0xBA → RAM[0xBA,0xBB] = {0xC7,0x01} = 0x01C7
; Bedingung: K5122 ctrl PIO Port A in Modus 0, /ASTB feuert auf Indexpuls, IE=1
ISR_DECREMENT_03F7:    01C7  EI                             ; verschachtelte Interrupts erlaubt
                        01C8  PUSH AF
                        01C9  PUSH HL
                        01CA  LD HL,03F7h
                        01CD  DEC (HL)                       ; [0x03F7]--
                        01CE  JR Z,01D4h                     ; 0 erreicht → /STR-Strobe auslösen
                        01D0  POP HL
                        01D1  POP AF
                        01D2  RETI

ISR_ASSERT_STR:        01D4  LD A,BDh
                        01D6  OUT (10h),A                    ; 0xBD: bit3=1 → /STR high (Leitstand), dann [0x03F8]=1
                        01D8  INC HL                         ; HL → 0x03F8
                        01D9  LD (HL),01h                    ; [0x03F8] = 1 (ISR-Timeout-Flag)
                        01DB  JR 01D0h                       ; RETI

; ===== HAUPT-STROBE/IDAM-SUCHSCHLEIFE (ZVE2-Code, Einstieg via JP 0x01DD) =====
STROBE_LOOP:           01DD  LD HL,(03F0h)                  ; HL = Ladeadresse (0x0400)
                        01E0  LD A,(07F1h)
                        01E3  LD B,A
                        01E4  LD DE,0700h
                        01E7  LD C,16h                       ; C = Port 0x16 (Daten-Lese-Port)

LOAD_IDAM_REGS:        01E9  EXX                            ; wechsle zum Alternativ-Registersatz
                        01EA  LD BC,FEA1h                    ; B'=0xFE (IDAM-Marker), C'=0xA1
                        01ED  LD DE,(03F3h)                  ; D'=[0x03F4]=head, E'=[0x03F3]=cyl
                        01F1  LD HL,(03F5h)                  ; H'=[0x03F6]=size_code, L'=[0x03F5]=sector_id
                        01F4  LD A,BDh
                        01F6  OUT (10h),A                    ; /STR high (Vorbereitung)

ASSERT_STR:            01F8  LD A,B5h
                        01FA  OUT (10h),A                    ; /STR low (Bit3 fällt) → doReadSector()
                        01FC  JR 025Ah                       ; → STROBE_LOOP_BODY

; ... (Zwischen-Code bei 01FE-0204: Fehlerbehandlung)

READ_IDAM_Z:           0205  IN A,(16h)                     ; Z-Pfad: liest buf[1]
                        0207  IN A,(16h)                     ; buf[2]
                        0209  IN A,(16h)                     ; buf[3]
                                                             ; → weiter bei 020B (buf[4]) dann CMP
READ_IDAM_NZ:          020B  IN A,(16h)                     ; NZ-Pfad: liest buf[1] (=0xFE nach NZ-Pfad)
                                                             ; (Z-Pfad fällt hier ein nach buf[3])

CMP_IDAM_MARK:         020D  CP B                           ; vergleiche mit B'=0xFE (IDAM-Marker)
                        020E  JR NZ,01F8h                    ; kein Treffer → erneut /STR
                        0210  IN A,(16h)                     ; buf[nächste] = Zylinder
                        0212  CP E                           ; vs. E'=erwarteter Zylinder
                        0213  JR NZ,01F8h
                        0215  IN A,(16h)                     ; = Kopf
                        0217  CP D                           ; vs. D'=erwarteter Kopf
                        0218  JR NZ,01F8h
IDAM_MATCH:            021A  IN A,(16h)                     ; = Sektor-ID
                        021C  CP L                           ; vs. L'=erwarteter Sektor
                        021D  JR NZ,01F8h
                        021F  IN A,(16h)                     ; = Size-Code
                        0221  CP H                           ; vs. H'=erwarteter Size-Code
                        0222  JR NZ,01FEh                    ; → Sektor-Nummer-Fehler-Handling
                        ; IDAM vollständig gematcht! Jetzt Sektor-Daten lesen...
                        0224  LD A,B5h
                        0226  OUT (10h),A
                        ; ... (Daten-Transfer und Bootsektor-Übergabe)

; ===== STROBE-LOOP KÖRPER =====
STROBE_LOOP_BODY:      025A  LD A,(03FDh)                   ; Lade Pfad-/Step-Konfiguration
                        025D  BIT 1,A                        ; teste Bit1 (0=Z-Pfad, 1=NZ-Pfad)
                        025F  OUT (10h),A                    ; AN PORT SCHREIBEN: Bit7=0 → STEP-PULS!
                        0261  IN A,(16h)                     ; Lese buf[0]
                        0263  JR Z,0205h                     ; Bit1=0 → Z-Pfad (falsch wenn [03FD]=0x00)
                        0265  JR 020Bh                       ; Bit1=1 → NZ-Pfad (korrekt)

; ===== NACH IDAM-TREFFER: 128-BYTE-TRANSFER (ZVE2) =====
; Nach erfolgreichem CP-Vergleich aller 4 IDAM-Felder (0x021A–0x0222):
IDAM_DATA_LOAD:        0224  LD A,B5h
                        0226  OUT (10h),A                    ; /STR low: Puffer für Datentransfer vorbereiten
                        0228  LD A,C1h                       ; INIR-Vorbereitung
                        022A  EXX                            ; zurück zum Hauptregistersatz
                        022B  INIR                           ; 128 Bytes: port(C=0x16) → (HL++), B Mal
                                                             ; lädt sector_buf_[6..133] → RAM[0x0400..0x047F]
                        022D  EXX                            ; wieder zum Alt-Registersatz

; ===== ZVE2-ABSCHLUSS / LOOP-KONTROLLE (0x0243–0x027F) =====
POST_INIR:             0243  LD A,(03F5h)                   ; aktueller Sektor
                        0246  INC A                          ; nächsten Sektor vorbereiten
                        0247  LD HL,(03F0h)                  ; aktuelle Ladeadresse
                        024A  LD A,B5h
                        024B  OUT (10h),A                    ; /STR low (kein Edge, prev_ctrl_a_=0x87)
                        024D  LD DE,0080h                    ; 128 Byte pro Sektor
                        0250  ADD HL,DE                      ; Ladeadresse += 128
                        0252  LD (03F0h),HL                  ; neue Ladeadresse sichern
                        0255  LD A,(07F1h)                   ; Sektoren-pro-Spur Zähler
                        0258  DEC A
                        0259  LD (07F1h),A
                        025B  JR NZ,01E9h                    ; noch Sektoren übrig → nächste IDAM-Suche

; Alle Sektoren geladen: ZVE2 signalisiert Fertigstellung
ZVE2_DONE:             026D  LD A,03h
                        026F  LD (03F8h),A                   ; [0x03F8] = 3 → ZVE1 verlässt Warte-Schleife!
                        0272  EXX
                        0273  JP (HL)                        ; Sprung zur nächsten Aktion (o. Halt)

; ... (restlicher Boot-Code: CTC-Init, Drive-Erkennung, Bootsektor-Übergabe)

; ===== CTC-INIT (nur nach erfolgreichem Boot erreicht!) =====
INIT_CTC:              02C2  OUT (0Ch),A                    ; K2526 CTC Kanal 0
INIT_CTC2:             02F3  OUT (0Dh),A                    ; K2526 CTC Kanal 1
                        02F7  OUT (0Dh),A

; ... (0x03EF - 0x03FF: Daten-/Parameter-Bereich)
; 0x03F0-0x03FF: RAM-Variablen (werden zur Laufzeit überschrieben)
```

---

## 8. Zeitliche Analyse: Wann feuert der erste Index-Puls?

| Ereignis                              | Zyklen nach RESET (ca.) |
|---------------------------------------|-------------------------|
| RAM-Clear (2048 × LDI+DEC+JP ≈ 25T)  | ~51.200                 |
| BS-PIO Init (10 OUT-Befehle × 11T)   | ~51.310                 |
| RAM-Test (62 × ~20 Befehle × 10T)    | ~63.710                 |
| LDIR-Kopie (838 × ~21T)              | ~81.308                 |
| Post-Copy Init (ca. 20 Befehle)       | ~81.500                 |
| EI bei 0x00D6                         | ~81.510                 |
| K5122 Vektor 0xBA bei 0x00ED         | ~81.600                 |
| IE gesetzt bei 0x012F                 | ~82.000                 |
| **Erster Index-Puls (490.000 Tz)**    | **490.000**             |

→ IE wird bei ca. Takt 82.000 gesetzt, erster Index-Puls bei Takt 490.000.  
→ Das ROM hat >400.000 Takte Zeit, die Schleife bei 0x0135 zu erreichen.  
→ Nach 4 Pulsen (4 × 490.000 = 1.960.000 Takte): [0x03F7]=0, [0x03FD]=0x87.

Mit dem falschen Wert kIndexPeriodCycles=50.000:
- Erster Puls bei Takt 50.000 → IE noch NICHT gesetzt (erst bei ~82.000)!
- → Puls wird ignoriert, Zähler sinkt nie!
- Oder: Falls IE früher gesetzt würde, wären 4 Pulse in 200.000 Takten = 81ms statt 800ms.
- In jedem Fall: falsches Timing → Code in WAIT_INDEX_LOOP läuft in Timeout.

---

## 10. ZVE1 ↔ ZVE2 Handshake beim Lesen der Bootspur

### 10.1 Zwei-CPU-Architektur der K2526-Karte

Die K2526-Karte enthält zwei Z80-Prozessoren, die sich einen gemeinsamen Adressraum (RAM der K3526-Karte)
teilen und über den Z80-Bus-Arbitrations-Mechanismus (BUSRQ/BUSACK) koordiniert werden:

| CPU  | Rolle                          | Startadresse       |
|------|--------------------------------|--------------------|
| ZVE1 | Primäre CPU – führt Boot-ROM aus | 0x0000 (ROM/RAM)  |
| ZVE2 | DMA-CPU – liest Sektordaten vom K5122 | 0x0000 → JP 0x01DD |

ZVE1 hält den Bus, solange ZVE2 nicht aktiv ist. Wenn ZVE2 läuft und den Bus benötigt
(um per `IN A,(16h)` / `INI` vom K5122-Puffer zu lesen), assertiert es BUSRQ. Solange
BUSRQ aktiv ist, tritt ZVE1 den Bus an ZVE2 ab (BUSACK) und macht selbst keinen
Fortschritt. In `a5120.cpp` ist dies implementiert als:

```cpp
if (bus_.isBUSRQ()) {
    if (!zre_.isZVE2InReset() && !zre_.isZVE2Waiting()) {
        zre_.zve2Step();   // ZVE2 läuft: Bus gehört ZVE2
    } else {
        afs_.dmaUpdate();  // ZVE2 gestoppt: BUSRQ direkt freigeben (Fallback)
    }
    remaining--;
    continue;
}
```

### 10.2 Gemeinsamer Speicher: Handshake-Variablen

Beide CPUs kommunizieren ausschließlich über RAM-Variablen. Die kritischen Adressen
(alle in `doc/EPROMS/zre_rom.asm` im Abschnitt `RAM-VARIABLEN-BEREICH 0x03EF–0x03FF`
dokumentiert):

| Adresse | Schreiber       | Leser | Bedeutung                                            |
|---------|-----------------|-------|------------------------------------------------------|
| 0x03F3  | ZVE1 (0x0162)  | ZVE2  | Erwarteter Zylinder für IDAM-Vergleich               |
| 0x03F4  | ZVE1 (0x0162)  | ZVE2  | Erwarteter Kopf (Head)                               |
| 0x03F5  | ZVE1 (0x015E)  | ZVE2  | Erwartete Sektor-ID (1-basiert)                      |
| 0x03F6  | ZVE1 (0x015E)  | ZVE2  | Erwarteter Size-Code                                 |
| 0x03F7  | ZVE1 (0x0197), ISR | ZVE1 | Indexpuls-Zähler / Timeout-Wächter                  |
| 0x03F8  | ZVE1 (0x01B3), ZVE2 (0x026B), ISR (0x01D9) | ZVE1 | Abschluss-Flag |
| 0x03F0  | ZVE1 (0x010A)  | ZVE2  | Ladeadresse für Sektordaten (init: 0x0400)           |
| 0x03FD  | ZVE1 (0x0153)  | ZVE2  | IDAM-Pfad-Konfiguration (0x87 = korrekt)             |

---

### 10.3 Phase 1 – ZVE1 bereitet den Handshake vor

Nachdem 4 Indexpulse empfangen wurden und `[0x03FD]` auf `0x87` gesetzt ist
(Abschnitt `STORE_03FD` / `SEEK_SETUP` in `zre_rom.asm` ab Adresse `0x0153`), setzt ZVE1
die IDAM-Vergleichsparameter und ruft `CALL 0194h` auf (Abschnitt
`LAUFWERKS-INITIALISIERUNG` in `zre_rom.asm`):

```
; zre_rom.asm 0x015C–0x0165
SEEK_SETUP:
  LD H,00h
  LD (03F5h),HL      ; [0x03F5]=0 (Sektor-ID), [0x03F6]=0 (Size-Code)
  LD L,H
  LD (03F3h),HL      ; [0x03F3]=0 (Zylinder),  [0x03F4]=0 (Kopf)
  CALL 0194h         ; → Laufwerks-Initialisierung + ZVE2 starten
```

Die Subroutine `0x0194` führt folgende Schritte in dieser Reihenfolge aus
(Abschnitt `LAUFWERKS-INITIALISIERUNG` in `zre_rom.asm` ab Adresse `0x0194`):

| Adresse | Aktion | Bedeutung |
|---------|--------|-----------|
| `0x0197` | `LD (03F7h),10h` | Timeout-Zähler = 16 Indexpulse (≈ 3,2 s) |
| `0x0199` | `OUT(14h),A` | Unbekanntes Steuersignal (ZVE2-Vorbereitung) |
| `0x019B` | `OUT(04h),A` | **ZVE2 aus Reset freigeben** → ZVE2 startet bei PC=0x0000 → JP 0x01DD |
| `0x019F` | `OUT(17h,FFh)` ×2 | K5122-Puffer reset / Sync |
| `0x01A5` | `OUT(10h,A5h)` | **Erste /STR-Flanke** → K5122 `doReadSector()` |
| `0x01AB` | `IN A,(16h)` | Dummy-Lese (positioniert Puffer-Zeiger) |
| `0x01B0` | `OUT(10h,[03FDh])` | /STR-Pegel auf 0x87 setzen (kein Step, NZ-Pfad) |
| `0x01B2` | `INC HL` | HL → 0x03F8 |
| `0x01B3` | `LD (HL),00h` | **[0x03F8] = 0**: ZVE2 läuft, ZVE1 wartet |

**Wichtige Reihenfolge:** `OUT(04h)` (ZVE2 starten) kommt *vor* `OUT(10h,A5h)` (erste
/STR-Flanke). ZVE2 ist also bereits aktiv, bevor der erste `doReadSector()`-Aufruf erfolgt.
`[0x03F8]=0` wird als letztes gesetzt – zu diesem Zeitpunkt ist der Startschuss für ZVE2
bereits gefallen.

---

### 10.4 Phase 2 – ZVE1 wartet

Nach `RET` aus `0x0194` zeigt HL weiterhin auf `[0x03F8]`. ZVE1 polt dieses Flag in einer
Tight-Loop (Abschnitt `STROBE_INIT` in `zre_rom.asm`, Adresse `0x0168`):

```
; zre_rom.asm 0x0168–0x016F
0168:  LD A,(HL)     ; A = [0x03F8]
0169:  OR A          ; Z-Flag wenn 0
016A:  JR Z,0168h   ; [0x03F8]=0 → ZVE2 läuft noch → weiter warten
016C:  DEC A
016D:  JR Z,014Bh   ; [0x03F8]=1 → ISR-Timeout → Retry-Pfad
016F:  CALL 01B6h   ; [0x03F8]=3 (nach DEC: A=2) → ZVE2 fertig → Signatur prüfen
```

Während ZVE1 in dieser Schleife wartet, werden eingehende Indexpulse (K5122 ctrl PIO
Port A Interrupt) vom ISR `ISR_DECREMENT_03F7` (Adresse `0x01C7` in `zre_rom.asm`)
behandelt. Dieser ISR dekrementiert `[0x03F7]`. Falls `[0x03F7]` auf 0 fällt, bevor ZVE2
fertig ist, setzt der ISR `[0x03F8]=1` (Timeout-Pfad, Adresse `0x01D9`).

---

### 10.5 Phase 3 – ZVE2 sucht und liest die Bootspur

ZVE2 startet bei PC=0x0000 und springt sofort zu `STROBE_LOOP` (Adresse `0x01DD` in
`zre_rom.asm`). Der STROBE_LOOP ist der Kern des Sektor-Lese-Protokolls:

#### Schritt 1: IDAM-Parameter laden (0x01DD–0x01F6)

```
; zre_rom.asm STROBE_LOOP / LOAD_IDAM_REGS
01DD:  LD HL,(03F0h)    ; HL = Ladeadresse (0x0400)
01E7:  LD C,16h         ; C = Port 0x16 (K5122-Sektor-Puffer, für INI)
01E9:  EXX              ; Wechsel in Alternativ-Registersatz
01EA:  LD BC,FEA1h      ; B'=0xFE (IDAM-Marker)
01ED:  LD DE,(03F3h)    ; D'=[0x03F4]=Kopf,      E'=[0x03F3]=Zylinder
01F1:  LD HL,(03F5h)    ; H'=[0x03F6]=Size-Code, L'=[0x03F5]=Sektor-ID
01F4:  OUT(10h, 0xBD)   ; /STR high (Rücksetzen)
```

#### Schritt 2: /STR assertieren → doReadSector() (0x01F8–0x01FC)

```
; zre_rom.asm ASSERT_STR
01F8:  LD A,B5h
01FA:  OUT(10h, 0xB5)   ; /STR low: fallende Flanke → K5122 doReadSector()
01FC:  JR 025Ah         ; → STROBE_LOOP_BODY
```

Dies ist der **entscheidende /STR-Strobe**: Die fallende Flanke auf Bit3 von Port 0x10
veranlasst K5122, den nächsten Sektor in seinen internen Puffer zu laden
(`sector_buf_`), sodass ZVE2 ihn per `IN A,(16h)` / `INI` auslesen kann.

#### Schritt 3: Pfad-Auswahl und buf[0] lesen (0x025A–0x0265)

```
; zre_rom.asm STROBE_LOOP_BODY
025A:  LD A,(03FDh)     ; Lade Pfad-Konfiguration
025D:  BIT 1,A          ; Bit1=1 → NZ-Pfad (0x87), Bit1=0 → Z-Pfad (0x00)
025F:  OUT(10h),A       ; /STR-Pegel setzen (Bit7=1 → kein Step-Puls!)
0261:  IN A,(16h)       ; liest buf[0] = 0x00
0263:  JR Z,0205h       ; Z-Pfad (falsch)
0265:  JR 020Bh         ; NZ-Pfad (korrekt, [0x03FD]=0x87)
```

#### Schritt 4: IDAM-Suche im NZ-Pfad (0x020B–0x0222)

Der K5122-Puffer (`sector_buf_`) hat folgendes Layout:

```
buf[0]=0x00  buf[1]=0xFE  buf[2]=cyl  buf[3]=head  buf[4]=sector_id  buf[5]=size_code  buf[6..]=Daten
```

ZVE2 liest Byte für Byte und vergleicht mit den Alternativ-Registern
(Abschnitt `CMP_IDAM_MARK` / `IDAM_MATCH` in `zre_rom.asm`, Adressen `0x020D–0x0222`):

```
020B:  IN A,(16h)       ; buf[1] = 0xFE
020D:  CP B             ; vs. B'=0xFE → Treffer?
020E:  JR NZ,01F8h      ; kein Treffer → neuer /STR-Strobe

0210:  IN A,(16h)       ; buf[2] = Zylinder
0212:  CP E             ; vs. E'=[0x03F3]=0 → Treffer?
0213:  JR NZ,01F8h

0215:  IN A,(16h)       ; buf[3] = Kopf
0217:  CP D             ; vs. D'=[0x03F4]=0 → Treffer?
0218:  JR NZ,01F8h

021A:  IN A,(16h)       ; buf[4] = Sektor-ID
021C:  CP L             ; vs. L'=[0x03F5]=1 → Treffer? (1-basiert!)
021D:  JR NZ,01F8h

021F:  IN A,(16h)       ; buf[5] = Size-Code
0221:  CP H             ; vs. H'=[0x03F6]=0 → Treffer?
0222:  JR NZ,01FEh      ; Size-Mismatch → Fehler-Pfad
```

Bei einem Mismatch löst `JR NZ,01F8h` einen erneuten /STR-Strobe aus: ZVE2 liest den
nächsten Sektor und sucht weiter, bis IDAM-Marker + alle 4 Felder passen.

#### Schritt 5: Datentransfer per INI/INIR (0x0224–0x0247)

Nach vollständigem IDAM-Treffer folgt der Nutzdaten-Transfer
(Abschnitt `IDAM vollstaendig gematcht` in `zre_rom.asm`, Adresse `0x0224`):

```
0224:  OUT(10h, 0xB5)   ; /STR für Datentransfer
0239:  IN A,(16h)       ; erstes Datenbyte aus Puffer
023B:  EXX              ; zurück zum Hauptregistersatz (HL=Ladeadresse, BC=Byte-Zähler)
023C:  DB ED,A2         ; INI: 1 Byte Port(C=0x16) → (HL++), B--
023E:  DB ED,A2         ; INI
0240:  DB ED,A2         ; INI
0242:  DB ED,B2         ; INIR: verbleibende B Bytes Port(C) → (HL++), bis B=0
```

Die Kombination aus 3 einzelnen `INI`-Befehlen gefolgt von `INIR` überträgt alle
128 Byte des Sektors in den RAM ab der aktuellen Ladeadresse (0x0400 für den ersten
Sektor, 0x0480 für den zweiten, usw.).

#### Schritt 6: ZVE2 signalisiert Fertigstellung (0x0267–0x026B)

Wenn alle Sektoren übertragen sind (Abschnitt `ZVE2 ABSCHLUSS` in `zre_rom.asm`):

```
0267:  LD A,03h
0269:  OUT(11h, 03h)    ; K5122 ctrl PIO Port A: INTENA=0 (keine weiteren Interrupts)
026B:  LD (03F8h),A     ; [0x03F8] = 3 → ZVE1 verlässt Warte-Schleife
```

---

### 10.6 Phase 4 – ZVE1 übernimmt und validiert

Sobald `[0x03F8] ≠ 0` gilt, verlässt ZVE1 die Warte-Schleife bei `0x016A`
(Abschnitt `STROBE_INIT` in `zre_rom.asm`):

```
016C:  DEC A           ; [0x03F8]=3 → nach DEC: A=2 → nicht Z
016D:  JR Z,014Bh      ; A=0 (d.h. war 1=Timeout) → Retry
016F:  CALL 01B6h      ; A=2 (d.h. war 3=OK) → Signatur prüfen
```

`CALL 01B6h` prüft die Bootsektor-Signatur (Abschnitt `BOOTSEKTOR-SIGNATUR PRUEFEN`
in `zre_rom.asm`, Adresse `0x01B6`):

```
01B6:  LD HL,(0400h)   ; lade RAM[0x0400,0x0401] als 16-Bit-Wert
01B9:  LD BC,5953h     ; "YS" (little-endian)
01BC:  SBC HL,BC       ; HL - 0x5953 = 0 → Signatur korrekt?
01BE:  RET NZ          ; nein → NZ
01BF:  LD HL,0402h
01C2:  LD A,4Ch        ; 'L'
01C4:  CP (HL)         ; RAM[0x0402] = 'L'?
01C5:  RET NZ
01C6:  RET             ; alles OK
```

Bei Erfolg springt ZVE1 mit `CALL 0437h` (`zre_rom.asm`, Adresse `0x0174`) in den
geladenen Boot-Code. ZVE2 hat seine Aufgabe erfüllt und ist nicht mehr aktiv.

---

### 10.7 Vollständiges Sequenz-Diagramm

```
ZVE1 (Boot-ROM)                          ZVE2 (STROBE_LOOP)          K5122
─────────────────────────────────────    ────────────────────────    ──────────────────

SEEK_SETUP (0x015C):
  [0x03F3..F6] = 0 (Cyl/Head/Sec/Size)
  CALL 0x0194:
    [0x03F7] = 16 (Timeout)
    OUT(04h)  ──────────────────────────► PC=0x0000 → JP 0x01DD
    OUT(10h, 0xA5)  ─────────────────────────────────────────────► doReadSector()
    IN A,(16h)  ◄────────────────────────────────────────────────  buf[0] (Dummy)
    OUT(10h, 0x87)
    [0x03F8] = 0

Warte-Schleife (0x0168):
  while [0x03F8]=0: loop ◄──────────────────────────────────────┐
                                                                 │
                              STROBE_LOOP (0x01DD):             │
                                LD HL,[0x03F0]  (=0x0400)       │
                                EXX; LD BC/DE/HL (IDAM-Regs)    │
                                OUT(10h, 0xBD)   /STR high      │  ──► (kein Edge)
                                OUT(10h, 0xB5) ─────────────────────► doReadSector()
                                JR 025Ah (STROBE_LOOP_BODY)     │
                                                                 │
                              STROBE_LOOP_BODY:                  │
                                IN A,(16h) buf[0]=0x00          │  ◄── buf[0]
                                JR 020Bh (NZ-Pfad)              │
                                                                 │
                              READ_IDAM_NZ:                      │
                                IN A,(16h) ◄────────────────────────── buf[1]=0xFE
                                CP B (0xFE) → Treffer?          │
                                IN A,(16h) ◄────────────────────────── buf[2]=cyl
                                CP E (0) → Treffer?             │
                                IN A,(16h) ◄────────────────────────── buf[3]=head
                                CP D (0) → Treffer?             │
                                IN A,(16h) ◄────────────────────────── buf[4]=sec_id=1
                                CP L (1) → Treffer?             │
                                IN A,(16h) ◄────────────────────────── buf[5]=size=0
                                CP H (0) → Treffer!             │
                                                                 │
                              INI/INIR (128 Byte):               │
                                ← ← ← ← IN Port(16h) ──────────────── buf[6..133]
                                → RAM[0x0400..0x047F]            │
                                                                 │
                              [0x03F8] = 3 ────────────────────►│
                                                                 │
  [0x03F8]=3 erkannt                                             │
  DEC A → A=2, kein JR Z                                        │
  CALL 01B6h: RAM[0x0400]="YSL"? → OK
  CALL 0437h → Boot-Code
```

---

### 10.8 Rolle des Indexpuls-ISR während des Handshakes

Der ISR `ISR_DECREMENT_03F7` (Adresse `0x01C7` in `zre_rom.asm`) läuft parallel zu Phase 2
und Phase 3. Er wird durch jeden Indexpuls der Diskette ausgelöst (K5122 ctrl PIO Port A,
Vektor 0xBA → ISR 0x01C7):

```
; zre_rom.asm ISR_DECREMENT_03F7
01C7:  EI               ; verschachtelte Interrupts erlauben
01CD:  DEC (HL)         ; [0x03F7]--
01CE:  JR Z,01D4h       ; 0 erreicht → Timeout-Pfad

; zre_rom.asm ISR_ASSERT_STR
01D4:  OUT(10h, 0xBD)   ; /STR high (Notfallmaßnahme)
01D9:  LD (HL),01h      ; [0x03F8] = 1 → ZVE1: Timeout erkannt
```

Der ISR hat damit eine **Wächter-Funktion**: Wenn ZVE2 nicht innerhalb von 16 Umdrehungen
(= 16 × 200 ms = 3,2 s) einen Sektor lesen kann, bricht er den Handshake mit Fehlercode 1 ab.
ZVE1 erkennt `[0x03F8]=1` in der Warte-Schleife und geht in die Retry-Logik (`0x014Bh`).

Während des normalen Boots (ZVE2 liest Sektor 1 in unter einer Umdrehung) erreicht
`[0x03F7]` nie 0 über den ISR – ZVE2 schreibt `[0x03F8]=3` deutlich vor Ablauf des
Timeouts.

---

## 11. Alternativer Boot-Pfad (0x027E–0x03AF)

### 10.1 Auslösebedingung

Der alternative Boot-Pfad wird bei `0x0145` (`JP Z,027Eh`) erreicht, wenn die Floppy-Boot-Schleife
alle möglichen Ctrl-Byte-Werte erfolglos durchprobiert hat:

```
BOOT_FAIL (0x0140):
  A = [0x03FC]   ; aktueller Ctrl-Wert (startet bei 0xEE, rotiert bei jedem Retry)
  CP 77h         ; 0x77 = letzter möglicher Wert nach mehrfachem RLCA
  JP Z, 027Eh   ; alle Floppy-Versuche erschöpft → alternativer Boot
  RLCA           ; nächsten Ctrl-Wert berechnen
  JR 0101h       ; nochmal versuchen
```

Die Rotation `0xEE → 0xDC → 0xB8 → 0x71 → 0xE2 → 0xC5 → 0x8B → 0x17 → ...` führt nach mehreren
Schritten zu `0x77`. Damit sind es ca. 8–16 Floppy-Boot-Versuche, bevor der alternative Pfad
aktiviert wird.

---

### 10.2 Hardware-Identifikation (Ports 0x30–0x37)

Der alternative Boot-Pfad verwendet ausschließlich die Ports **0x30–0x37** und **0x31**.
Im A5120-System sind diese Adressen keiner bekannten Karte auf Basis des vorliegenden Codes
eindeutig zuzuordnen. Aus dem Verhalten lässt sich schließen:

| Port | Richtung | Funktion (erschlossen)                              |
|------|----------|-----------------------------------------------------|
| 0x31 | IN       | Daten-Eingang (Sync-Byte / empfangenes Byte)        |
| 0x33 | OUT      | Ctrl/Handshake (0x7F=Init, 0xF0=Reset, 0x97=Sync, 0x03=Stop, 0xF4=ACK) |
| 0x34 | IN/OUT   | Cmd/Status-Register (Bit7=Bereit, Bit0=Typ, Bits6..2=Fehler) |
| 0x35 | IN       | Status (Bit0=Gerät-Typ, Bit3/4=Bereit-Flags)       |
| 0x36 | OUT      | Steuer-Register (Init-Sequenz)                      |
| 0x37 | OUT      | Steuer-Register (Init-Sequenz)                      |

Das Protokoll mit Sync-Bytes, Block-Zählern und einem dedizierten Daten-Eingang (Port 0x31)
deutet auf eine **serielle oder bandbasierte Bootstrap-Schnittstelle** hin – möglicherweise
eine K2521-Karte (Magnetband) oder eine serielle Ladeeinheit. Eine eindeutige Identifikation
erfordert Abgleich mit dem A5120-Schaltplan.

---

### 10.3 Initialisierungssequenz (0x027E–0x02C1)

```
0x027E  LD A,FFh / OUT(18h)          ; Drive-Select deaktivieren (alle Drives weg)

; ISR-Vektortabelle umkonfigurieren:
0x0282  LD (03F0h), 03BCh            ; ISR Kanal 0: Byte lesen + ACK
0x0288  LD (03F2h), 03D4h            ; ISR Kanal 1: Block-/Byte-Zähler
0x028E  LD (03F4h), 03C7h            ; ISR Kanal 2: Byte-Transfer per INI

0x0294  LD A,03h / LD I,A            ; I=3 → Vektor-Tabelle bei 0x0300

; Sekundär-Controller initialisieren:
0x0298  OUT(33h, 7Fh)                ; Ctrl reset
0x029C  OUT(36h, 7Fh)                ; Steuer-Register A reset
0x029E  OUT(37h, 7Fh)                ; Steuer-Register B reset
0x02A2  OUT(34h, 03h)                ; Cmd-Register: Modus 3
0x02A5  OUT(35h, 04h)                ; Daten-Register: 4
0x02A9  OUT(37h, FFh)                ; Steuer-Register B: 0xFF
0x02AD  OUT(37h, FDh)                ; Steuer-Register B: 0xFD
0x02B1  OUT(36h, FFh)                ; Steuer-Register A: 0xFF
0x02B5  OUT(36h, 80h)                ; Steuer-Register A: 0x80

0x02B7  IN A,(34h) / AND 7Ch         ; Status lesen, Fehler-Bits 6..2 prüfen
0x02BB  JP NZ, 03B0h                 ; Fehler → HALT
```

---

### 10.4 CTC-Initialisierung und Bereit-Erkennung (0x02C2–0x02F1)

```
0x02C2  OUT(0Ch, F2h)                ; K2526 CTC Kanal 0: Timer, Prescaler 16
0x02C6  LD (03FAh), 05h              ; Retry-Counter = 5
0x02CA  OUT(34h, E)                  ; Sekundär-Ctrl: E=9 → Befehl
0x02CE  CALL 03B6h (×2)              ; Delay
0x02D7  LD (03F8h), 0400h            ; Ladeadresse = 0x0400
0x02E9  LD (03F6h), 00h              ; Byte-Zähler = 0
0x02EE  LD (03F7h), 14h              ; Block-Zähler = 20

0x02F3  OUT(0Dh, A7h)                ; K2526 CTC Kanal 1: Timer, Prescaler 16
0x02F7  OUT(0Dh, FFh)                ; Zeitkonstante = 0xFF (max. Timeout)
```

Anschließend wartet der Code auf Bit7 von Port 0x34 (Controller-Bereit-Signal). Falls nicht
innerhalb von 3 Versuchen bereit, werden Bits 5/4 gesetzt um den Transfer-Modus zu erzwingen:

```
Bit7=0: OR 20h (Bit5 setzen) → OUT(34h) → warte 3× CALL 03B6h → nochmal prüfen
Bit7=1: Bit5 rücksetzen → direkt zu Konfiguration (033Bh)
```

---

### 10.5 Transfer-Protokoll (0x0355–0x0376)

Das Kernelement des alternativen Boots ist eine **Sync-Schleife** (D=4 Durchläufe):

```
LOOP (D=4):
  OUT(33h, F0h)               ; Ctrl: Reset/Init-Signal
  OUT(33h, 97h) ×2            ; Ctrl: Sync-Signal
  IN A,(31h)                  ; warte auf Sync-Byte von Port 0x31
  XOR A / CP B                ; warte bis B-Zähler durch ISR auf 0 gesetzt wurde
  JR NZ,0365h                 ; Polling (ISR setzt B=0 nach Sync)
  OUT(33h, 03h)               ; ACK / Reset
  [0x03F8] -= 3               ; Ladeadresse um 3 dekrementieren
  DEC D / JR NZ, 0355h        ; nächster Sync-Zyklus
```

Nach 4 Sync-Zyklen: CTC Kanal 1 stoppen, Bootsektor-Signatur prüfen (identisch mit
Floppy-Boot: RAM[0x0400]="YS", RAM[0x0402]='L'), bei Erfolg: `CALL 0x0434`.

---

### 10.6 ISR-Mechanismus im alternativen Boot-Pfad

Mit I=3 zeigt die Vektor-Tabelle auf 0x0300. Die drei neuen ISR-Funktionen:

#### ISR Kanal 0 (0x03BC) – Byte lesen + ACK
```
EI
IN A,(31h)        ; Daten-Byte vom Controller lesen (verworfen – nur Trigger-Funktion)
LD A,F4h
OUT(33h, F4h)     ; Empfangs-Bestätigung an Controller
RETI
```
Signalisiert dem Controller, dass ein Byte empfangen wurde.

#### ISR Kanal 1 (0x03C7) – Byte-Transfer per INI
```
EI
LD HL,(03F8h)     ; aktuelle Schreib-Adresse
INI               ; 1 Byte Port C → (HL++), B--
LD (03F8h),HL     ; neue Adresse sichern
RETI
```
Führt pro Interrupt einen einzelnen INI-Transfer durch. Jeder Interrupt schreibt ein Byte
aus dem Controller-Daten-Port in den RAM-Puffer bei `[0x03F8]` und inkrementiert den Zeiger.

#### ISR Kanal 2 (0x03D4) – Block-/Byte-Zähler
```
EI
HL = 0x03F6
DEC (HL)               ; [0x03F6]-- (Byte-Zähler)
JR NZ, RETI            ; noch Bytes im Block → fertig
INC L                  ; HL → 0x03F7
DEC (HL)               ; [0x03F7]-- (Block-Zähler)
JR Z, ABSCHLUSS        ; alle Blöcke geladen → Abschluss-Pfad
RETI
```

**Abschluss-Pfad (0x03E5):** Manipuliert den Stack um nach RETI zu `0x0393` zu springen
(statt zum normalen Rücksprungziel). Technik: `EX (SP),HL` tauscht die RETI-Rücksprung-
adresse gegen `0x0393` aus. Bei `0x0393` wird Port 0x33 zurückgesetzt und die Boot-Sequenz
endet.

---

### 10.7 Flussdiagramm: Alternativer Boot-Pfad

```
JP Z,027Eh (alle Floppy-Retries erschöpft)
  │
  ├─ Drive-Select deaktivieren (OUT 18h, FFh)
  ├─ ISR-Vektoren umbiegen (I=3, neue ISRs bei 0x03BC/0x03C7/0x03D4)
  ├─ Sekundär-Controller Init (Ports 0x33-0x37)
  ├─ Fehler-Check (AND 7Ch) → NZ → HALT (0x03B0)
  │
  ├─ CTC Kanal 0 init (Port 0x0C, 0xF2)
  ├─ CTC Kanal 1 init (Port 0x0D, 0xA7 + 0xFF)
  ├─ Retry-Counter [0x03FA] = 5
  ├─ Ladeadresse [0x03F8] = 0x0400
  ├─ Byte-Zähler [0x03F6] = 0, Block-Zähler [0x03F7] = 20
  │
  ├─ Controller Bereit-Erkennung (Bit7 Port 0x34, max. 3 Versuche)
  │     → nicht bereit → Transfer-Modus erzwingen
  │     → HALT wenn Status-Bits nicht gesetzt
  │
  ├─ SYNC-SCHLEIFE (D=4 Durchläufe):
  │     OUT(33h): Reset → Sync-Signal
  │     Warte auf Sync-Byte (Port 0x31, ISR setzt B=0)
  │     ACK senden
  │     [0x03F8] -= 3
  │
  ├─ CTC Kanal 1 stoppen (OUT 0Dh, 21h)
  ├─ Bootsektor-Signatur prüfen (CALL 01B6h): RAM[0x0400]="YS", [0x0402]='L'
  │     NZ → Retry: [0x03FA]--; NZ → nochmal; Z → HALT
  │
  └─ Signatur OK: CALL 0x0434 → geladener Boot-Code
```

---

### 10.8 Retry-Logik und Endlosfall

Bei Misserfolg (Signatur falsch oder Timeout):

```
0x039C  LD HL,03FAh / DEC (HL)    ; [0x03FA]--
0x03A4  JP NZ, 02C9h              ; noch Retries → von vorn (CTC-Init)
0x03A7  BIT 0,E / JR Z, 03B0h    ; E Bit0=0 → HALT
0x03AB  LD E,06h / JP 02C4h       ; E Bit0=1 → anderen Geräte-Typ versuchen
```

Maximal 5 Retries pro Geräte-Typ (E Bit0=0 oder 1). Nach Erschöpfung aller Möglichkeiten:
**HALT-Schleife** bei `0x03B4` mit Fehler-Code `0x9F` an Port `0x03`.

---

## 9. Offene Aufgaben / Bug-Status

| ID  | Beschreibung                                              | Datei        | Status     |
|-----|-----------------------------------------------------------|--------------|------------|
| P1  | Index-Puls-Timing: kIndexPeriodCycles = 490.000           | k5122.h      | ✓ behoben  |
| P4  | BUSRQ-Deadlock in a5120.cpp (Fallback auf dmaUpdate)      | a5120.cpp    | ✓ behoben  |
| B5  | ZVE2 bleibt im WAIT-Zustand nach Reset-Freigabe           | k2526.cpp    | ✓ behoben  |
| B6  | sector_id im IDAM-Header 0-basiert statt 1-basiert        | k5122.cpp    | ✓ behoben  |
| B7  | Vorzeitige BUSRQ-Freigabe (Puffer-Leer)                   | k5122.cpp    | ✓ behoben  |
| B8  | Kontinuierlicher Track-Stream, 138-Byte-Blöcke            | k5122.cpp    | ✓ behoben  |
| B9  | BUSRQ-Freigabe erst bei [0x03F8]=3 (statt Puffer-Leer)    | a5120.cpp    | ✓ behoben  |
| B10 | ZVE1/ZVE2 nebenläufig steppen während BUSRQ              | a5120.cpp    | ✓ behoben  |
| —   | /FW-Statusbit (Bit6 statt Bit7) bei Spur 0               | k5122.cpp    | ✓ behoben  |

**✓ GELÖST 2026-06-05:** `boot_trace -L … disks/cpadisk.img` meldet **Boot reached: YES**.
ZVE2 kopiert alle 4 Bootsektoren, schreibt [0x03F8]=3; ZVE1 prüft die Signatur "SYL"
(RAM[0x0400]=53 59 4C) und springt nach 0x0437 (geladener Boot-Code).

Kernerkenntnis (mit verbessertem boot_trace gefunden): ZVE2 liest **138 Bytes pro
Sektor** (6 Header + 2 IDAM-CRC + 128 Daten + 2 Daten-CRC), nicht 134 oder 136 —
die 2 Daten-CRC stammen aus den nachgelagerten INIs bei ROM 0x0245/0x0247. Der
K5122 liefert nun den gesamten Track als kontinuierlichen Stream solcher 138-Byte-
Blöcke; BUSRQ wird gehalten bis ZVE2 [0x03F8]=3 setzt; ZVE1 läuft währenddessen
nebenläufig weiter (sonst überschreibt ZVE1s spätes [0x03F8]=0 das =3).

**Nächste Schritte:** den geladenen CPA-Sekundär-Bootstrap ab 0x0437 zum Laufen
bringen (CTC-Init, K7024-Bildschirm, weitere K5122-Sektoren).

---

## 12. Bug B7 – Analyse: Vorzeitige BUSRQ-Freigabe

### 12.1 Problembeschreibung

`K5122::handleCtrlPortAWrite()` behandelt **jede** fallende /STR-Flanke (Bit3: 1→0 an
Port 0x10), die eintrifft, während `bus_.isBUSRQ()=true`, als ZVE2-Commit-Signal und
ruft `bus_.releaseBUSRQ()` auf — unabhängig davon, ob es sich um einen Schreib-Commit
oder einen Lese-Refresh handelt.

ZVE2 erzeugt im normalen Lese-DMA jedoch **zwei** /STR-Flanken:

| Flanke | Adresse | Wert | Kontext |
|--------|---------|------|---------|
| 1. | `0x01F6` | `OUT(10h, 0xBDh)` | /STR **deassertieren** (Bit3=1): Puffer zurücksetzen |
| 2. | `0x01FA` | `OUT(10h, 0xB5h)` | /STR **assertieren** (Bit3=0, Bit0=1=/WE=read): **neue Sektor-Lese-Anforderung** |

Die zweite Flanke (0xBD→0xB5 an Bit3) trifft ein, **während BUSRQ bereits gehalten
wird** und **bevor** ZVE2 auch nur ein Byte über INIR gelesen hat. Die aktuelle
Emulation gibt daraufhin BUSRQ sofort frei – ZVE1 übernimmt, ZVE2 erhält keine
weiteren Bytes.

### 12.2 EPROM-Kontext: /STR-Pulsfolge in `STROBE_LOOP` (0x01DD–0x01FC)

```
STROBE_LOOP: 01DD  LD HL,(03F0h)   ; Ladeadresse laden
             ...
             01E7  LD C,16h         ; C = K5122 Daten-Port (für INIR)
LOAD_IDAM_REGS:
             01E9  EXX
             01EA  LD BC,FEA1h      ; B'=0xFE (IDAM-Marker)
             01ED  LD DE,(03F3h)    ; D'=Kopf, E'=Zylinder
             01F1  LD HL,(03F5h)    ; H'=Size-Code, L'=Sektor-ID

             01F4  LD A,BDh         ; 0xBD = Bit3=1 → /STR DEASSERTIEREN
             01F6  OUT (10h),A      ;   prev_ctrl_a_.bit3 wird 1
                                    ;   → kein Trigger in handleCtrlPortAWrite

ASSERT_STR:  01F8  LD A,B5h         ; 0xB5 = Bit3=0, Bit0=1 (/WE=read)
             01FA  OUT (10h),A      ;   ← FALLENDE FLANKE (prev bit3=1 → jetzt 0)!
                                    ;   bus_.isBUSRQ() = TRUE (ZVE2 hat Bus)
                                    ;   is_write = !(0xB5 & 0x01) = false  → LESEN

             01FC  JR 025Ah         ; → STROBE_LOOP_BODY
```

Wenn `handleCtrlPortAWrite(0xB5)` mit `bus_.isBUSRQ()=true` und `is_write=false`
aufgerufen wird, führt der aktuelle Code aus:

```cpp
if (bus_.isBUSRQ()) {
    if (is_write) doWriteSector();   // übersprungen (is_write=false)
    dma_pending_ = false;
    bus_.releaseBUSRQ();             // ← BUG: BUSRQ vorzeitig freigegeben!
}
```

ZVE1 übernimmt den Bus, noch bevor ZVE2 den INIR-Transfer ausgeführt hat.

### 12.3 Warum /STR bei 0x01FA kein Commit-Signal ist

Der EPROM-Code zeigt klar die Bedeutung der /WE-Leitung (Bit0 von Port 0x10):

| /WE (Bit0) | Bedeutung im ZVE2-Kontext |
|------------|--------------------------|
| `0` (write enable aktiv) | ZVE2 hat Schreib-DMA abgeschlossen → BUSRQ freigeben |
| `1` (write enable inaktiv) | ZVE2 fordert neuen Sektor-Lese-Transfer an → BUSRQ **halten** |

Alle /STR-Pulse von ZVE2 im Lese-DMA haben **Bit0=1** (/WE inaktiv = Lesen):
- `0x01FA`: `0xB5 = 1011 0101` → Bit0=1, Bit3=0 → Lese-Refresh, **kein Commit**
- `0x0224`: `0xB5` → gleiches Muster (kein falling edge da prev auch Bit3=0)
- `0x022D`: `[0x03FD]`-Wert (= `0x87 = 1000 0111`) → Bit0=1 (kein Commit)

Nur im **Schreib-DMA** schreibt ZVE2 am Ende mit `/WE=0` (Bit0=0), um den Commit
zu signalisieren (z.B. `0xF6 = 1111 0110`: Bit3=0, Bit0=0).

### 12.4 Korrekte BUSRQ-Freigabe laut EPROM

Der EPROM-Code liest nach dem /STR-Refresh **128 Bytes** per INIR aus Port 0x16:

```
INIR-Transfer (ZVE2, nach IDAM-Treffer):
  0239  IN A,(16h)    ; Dummy-Read (erstes Datenbyte)
  023B  EXX
  023C  INI           ; 1 Byte Port 0x16 → (HL++), B--
  023E  INI
  0240  INI
  0242  INIR          ; restliche B Bytes bis B=0 → sector_buf_ vollständig gelesen
```

Erst wenn INIR das **letzte Byte** aus Port 0x16 liest, ist ZVE2 fertig.
Die BUSRQ-Freigabe gehört deshalb in `K5122::ioRead(0x16)`, nach dem letzten Byte —
**nicht** in `handleCtrlPortAWrite()` beim /STR-Refresh.

Die bestehende Freigabe in `ioRead(0x16)` (k5122.cpp, ca. Zeile 95–99) ist korrekt:

```cpp
// Auto-release BUSRQ: all sector bytes consumed (ZVE2 INIR done)
if (bus_.isBUSRQ()) {
    dma_pending_ = false;
    bus_.releaseBUSRQ();
}
```

### 12.5 Erforderliche Korrektur in `handleCtrlPortAWrite()`

Die Bedingung für BUSRQ-Freigabe muss um die Unterscheidung Lesen/Schreiben erweitert
werden:

```
AKTUELL (fehlerhaft):
  /STR-Flanke + bus_.isBUSRQ() = true
    → immer releaseBUSRQ()

KORREKT:
  /STR-Flanke + bus_.isBUSRQ() = true + /WE=0 (is_write=true)
    → doWriteSector() + releaseBUSRQ()        (Schreib-Commit)

  /STR-Flanke + bus_.isBUSRQ() = true + /WE=1 (is_write=false)
    → doReadSector()                          (Lese-Refresh: sector_buf_ neu laden)
    → BUSRQ halten! Freigabe erfolgt in ioRead(0x16) nach dem letzten INIR-Byte.
```

### 12.6 Zusammenfassung der relevanten EPROM-Adressen für B7

| Adresse | Instruktion | Bedeutung für B7 |
|---------|-------------|------------------|
| `0x01F4` | `OUT(10h, 0xBDh)` | /STR deassertieren (Bit3=1) — setzt prev_ctrl_a_.bit3=1 |
| `0x01FA` | `OUT(10h, 0xB5h)` | /STR assertieren (Bit3=0, /WE=1) — erzeugt die Bug-Flanke |
| `0x025F` | `OUT(10h, [0x03FD])` | Pfad-Byte schreiben (keine Flanke, Bit3 war bereits 0) |
| `0x0239`–`0x0242` | `IN / INI / INIR` | Korrekte Stelle der BUSRQ-Freigabe (letztes Byte aus Port 0x16) |
| `0x026B` | `LD (03F8h), 03h` | ZVE2-Fertigstellungs-Signal — erst nach INIR möglich |
