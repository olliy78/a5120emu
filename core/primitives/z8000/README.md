# U8001/U8002-Primitive (`core/primitives/z8000.{h,cpp}`)

Arbeitspaket S3 aus `doc/design/17_a5120_16.md`. Generische CPU wie `Z80`: kennt keine
Karte und keine Maschine. Dekodierung und Takte kommen aus **der** Befehlstabelle dieses
Verzeichnisses (`z8k_table.h`, `z8k_codec.h`, aus S2 hierher verschoben; Disassembler und
Assembler in `tools/z8000/` benutzen sie von hier).

| Datei | Inhalt |
|---|---|
| `../z8000.h` | Klasse `Z8000`, `Z8kBusCycle`, `Z8kStatus`, `Z8kConfig` |
| `../z8000.cpp` | Ausführung, Ausnahmen, Pins |
| `z8k_table.h`, `z8k_codec.h` | Befehlstabelle, Dekoder (S2) |
| `tests/unit/primitives/test_z8000.cpp` | 81 Tests je Befehlsgruppe, Programme mit z8kasm |
| `tests/oracle/` | Differenzprüfung gegen MAMEs z8000 (`tools/dev.sh test-oracle`) |

## Busschnittstelle

Jeder Zugriff nach aussen ist **ein** Buszyklus, so wie er an den Pins steht:

```cpp
struct Z8kBusCycle { Z8kStatus st; bool system; bool word; bool read; uint8_t seg; uint16_t addr; };
std::function<uint16_t(const Z8kBusCycle&)>       read;    // liefert AD0..15
std::function<void(const Z8kBusCycle&, uint16_t)> write;   // bekommt AD0..15
```

`st` ist der Leitungscode ST3..0 (Zilog Tabelle 9.1). Benutzt werden:

| Status | Wann |
|---|---|
| `MemInstrFirst` (1101) | erstes Befehlswort; auch das „Scheinholen" vor einer Interruptquittung |
| `MemInstr` (1100) | weitere Befehlsworte, Daten von LDR/LDRB/LDRL (Program Reference), Resetvektor (0002/0004/0006), **Program-Status-Area-Lesen** bei Trap/Interrupt |
| `MemData` (1000) | Datenzugriffe |
| `MemStack` (1001) | Zugriffe über den Stapelzeiger (R15 bzw. RR14 als IR/BA/BX-Basis, §5.4.3) und alle impliziten Stapelzugriffe (CALL/RET/SC/Trap/Interrupt/IRET) |
| `Io` (0010), `SpecialIo` (0011) | E/A; `seg` = 0 (undefiniert laut Handbuch) |
| `ViAck`/`NviAck`/`NmiAck` (0111/0110/0101) | Quittung, liefert die 16-Bit-Kennung |
| `Refresh` (0001) | nur mit RE = 1 bzw. im Stop-Zustand, `addr` = Zeile |

- **N/S** (`system`) ist der Modus zum Zeitpunkt des Zyklus; der Trap-/Interruptrahmen läuft
  schon im Systemmodus (§7.6.1), das Scheinholen noch im alten.
- **SN** (`seg`): segmentiert das Segment der Adresse; Z8001 unsegmentiert = PC-Segment
  (§4.3.2); Z8002 immer 0.
- **Daten:** Speicherwort: `addr` gerade (A0 gelöscht). Speicherbyte: `addr` mit A0, gelesen
  wird die Hälfte (gerade → AD8..15), geschrieben liegt das Byte auf **beiden** Hälften
  (§9.4.2). E/A-Byte lesen: **Lage nach A0 wie beim Speicher** (ungerade AD0..7, gerade
  AD8..15); §9.4.3 nennt nur die zulässigen Fälle (Standard-Byteports ungerade, Spezial
  gerade). Am A5120.16 gemessen: `INB RL0,%0080` liest das auf AD8..15 liegende Status-8.
- **WAIT:** der Rückruf ruft `cpu.addWaitCycles(n)`; die Takte zählen im laufenden `step()`.
- **Takte:** `step()` = ein Befehl bzw. ein Durchlauf eines Wiederholungsbefehls, ein
  Trap-/Interrupteintritt, ein Halt-/Stop-/Bus-Takt. Tabellenwert + `perN·n` + WAIT. Eigene
  Werte: HALT 8, dann 3 je Schritt; Stop 3 je Refresh; BUSREQ 1 je Schritt; DIV/DIVL bei /0
  −94/−714, bei Überlauf −82/−693; MULT(L) mit Multiplikator 0 → 18/30 Takte;
  **Interrupteintritt geschätzt** (SC-Takte − 3 für das nicht nötige Holen + 3 Scheinholen + 8 Quittung; das
  Handbuch nennt keine Zahl).

## Pins

`setResetLine` (hält fest, Loslassen → Resetsequenz), `reset()` (Impuls), `setNMI` (Flanke),
`setVI`/`setNVI` (Pegel, maskiert über VIE/NVIE), `setStop`, `setBusReq` → `busAck()`/`onBusAck`,
`setMI` (µI aktiv = L) und `moActive()`/`onMO` (µ0 aktiv = L; nach Reset inaktiv).

## Wie S4 anschliesst (EM-Steuerkarte, Plan §7.3/§7.4)

- **Segmentweiche A42:** Mode 0 → Segment aus A33; Mode 1 → `system` × (Befehl =
  `st ∈ {MemInstr, MemInstrFirst}` / sonst Daten); Mode 2 → `cycle.seg & 3` (SN0/SN1).
  Achtung: auch das PSA-Lesen bei Traps/Interrupts ist Status 1100 (Programmspeicher).
- **A33/A35:** `st == Io && !read && (addr & 0x80)` → A33 ← AD0..7, A35 ← AD8..15.
  **Lesen** (`st == Io && read && (addr & 0x80)`): Status-8 auf AD8..15 → nur Wort-`IN` sieht es.
- **VI:** A34-INT → `setVI(true)`; die Quittung (`st == ViAck`) liefert Vektor (low) + Status-8
  (high) und löscht INT. Vektortabelle Z8001: PSAP + %3C + 2·(Kennung & %FF).
- **Einzelbefehlszähler A53:** Zyklen mit `st == MemStack` zählen, bei QD → `setNVI`.
- **µI = /TRQ8:** `setMI(trq8Aktiv)`. **TREN = µ0:** `onMO`. **RESET16:** `setResetLine`.
  **STOP (PIO B3):** `setStop`. **BUSRQ/BUSAK:** `setBusReq`/`onBusAck`.
- Resetvektor kommt über `read` mit `MemInstr`, System, Segment 0 — in Mode 0 bei
  gelöschtem A33 also Segment 0.
- Save-State: Register, FCW, PC, PSAP, REFRESH und `cycles` sind öffentliche Member; der
  Ablaufzustand (Pins, Halt, Stop mit schon geholtem erstem Wort, laufender
  Wiederholungsbefehl als seine Befehlsworte, NMI-Merker) über `runState()`/`setRunState()`
  (`Z8kRunState`, POD). Wächter `Z8000Zustand.AblaufzustandMittenImLdirUebertragbar`.

## Antworten auf die offenen Punkte aus Plan §8

1. **Byte-OUT auf Standard-E/A — liegt das Byte auf beiden Hälften?** Das Handbuch sagt es
   nur für Speicher-Schreibzyklen (§9.4.2: „the CPU places the same byte on both halves");
   für E/A nur, welche Hälfte gilt (§9.4.3: Standard AD0..7, Spezial AD8..15). MAME schreibt
   den verdoppelten Wert mit Lanenmaske. **Am A5120.16 belegt (2026-09-29):** `OUTB %0081`
   mit 01H ⇒ A35 (AD8..15) = 01H. Der Kern verdoppelt
   (`Z8kConfig::ioByteOnBothHalves = true`, abschaltbar: dann nur die gültige Hälfte). Folge
   für A35: ein `OUTB` an eine Adresse mit Bit 7 schreibt A33 **und** A35 mit demselben Byte.
   Beim **Lesen** gilt die Hälfte nach A0 (gerade AD8..15), auch für Standard-E/A an
   gerader Adresse: `INB RL0,%0080` liest am Gerät Status-8 (A36 treibt nur AD8..15).
2. **Ungerade Register als Zeiger (segmentiert, `@R3`):** Das Handbuch verlangt ein
   Registerpaar mit gerader Nummer. MAME liest einen Zeiger als RR(n & ~1) — `@RR3` ≡ `@RR2`;
   beim **Schreiben** einer Adresse (LDA/LDAR in RR3) schreibt MAME dagegen R3 zweimal — in
   sich uneinheitlich. Der Kern ignoriert Bit 0 **überall** (lesen wie schreiben, RQ Bit 0/1).
   Für em256ful heisst das: `@R3` greift über RR2 (R2 = Segmentwort, R3 = Offset). Am Baustein
   nicht belegt. Wächter `Z8000Register.UngeradesRegisterpaarAlsZeigerIstDasGeradePaar`.
3. **Unbelegte Kodierungen:** 7078 erste Worte passen auf keine Tabellenzeile. Das Handbuch
   sagt nur, die CPU deute jedes Muster als *irgendeinen* Befehl. Der Kern führt sie als NOP
   der schon gelesenen Länge aus (7 Takte), zählt sie (`illegalCount()`) und meldet sie
   (`onIllegal`) — wie MAME (`zinvalid`, ebenfalls ohne Wirkung). Kodierungen, die
   *formal* passen, aber nach Handbuch ungültig sind (Bytebit 8..15, Schiebeweite > Breite,
   RLDB mit Link = Quelle, Blockbefehl mit überlappenden Registern), laufen mit den
   Regeln des Kerns; das Orakel vergleicht sie nicht.

## Weitere Festlegungen (mit Beleg bzw. Grund)

- **Stapelzeiger bei PUSH/POP (auch CALL/RET/Trap/IRET):** ein ungerader Zeiger wird beim
  Fortschalten gerade (`o += delta − Bit 0`). Handbuch schweigt; MAME tut es seit 9e78116399
  („correct misaligned stack pointers", am System 8000 erprobt). Blockbefehle schalten
  ihre Zeiger ohne Ausrichtung.
- **Gesichertes PC-Segmentwort** (CALL, Trap-/SC-/Interruptrahmen): `0sss ssss 0000 0000`.
  MAME setzt Bit 15 (`make_segmented_addr`). Handbuch zeigt nur „PC SEGMENT"; für LDAR sagt
  es ausdrücklich „reserved bits cleared". LDA: reservierte Bits = 0 (Handbuch: undefiniert).
- **PC-Offset läuft im Segment über** (Adressrechnung ohne Übertrag, §5.5). MAME trägt ins
  Segment.
- **DAB:** Z80-artig aus der Handbuchtabelle (Tiefkorrektur bei H ∨ Einer > 9, Hochkorrektur
  bei C ∨ Wert > %99; subtrahierend C unverändert). MAME (741d827a) prüft die Zehnerstelle
  **nach** der Einerkorrektur und liefert für 96 + 64 (%FA) %00/C = 0 statt %60/C = 1 —
  widerspricht der Tabellenzeile „C=0, 9–F, H=0, A–F → +66, C=1".
- **Undefinierte Flags** bleiben unverändert, ausser wo derselbe ALU-Weg sie ohnehin setzt
  (CPI/CPD: C/S aus dem Vergleich). TRIB/TRDB: RH1 = Übersetzungswert.
- **Unterbrechbare Blockbefehle:** ein Durchlauf je `step()`, PC bleibt am Befehl; kommt ein
  Interrupt, wird der Befehl verlassen (+7 Takte, gesicherter PC = der Befehl selbst) und
  nach IRET neu geholt. BUSREQ wird zwischen Durchläufen bedient.
- **EPA-Befehle:** EPA = 0 → Extended-Instruction-Trap (Kennung = erstes Wort, gesicherter
  PC = zweites Wort). EPA = 1 ohne Z8070 → NOP der vollen Länge (7 Takte), keine EPU-Zyklen.
- **Privilegierte Befehle** werden am ersten Wort erkannt (kein zweites Holen), gesicherter
  PC = Wort hinter dem ersten.
- **IRET auf der Z8001 unsegmentiert** (laut Handbuch undefiniert): segmentierter Rahmen.
- **LDCTL:** FCW nur Bit 2..7 und 11..15; Z8002 ohne NSPSEG/PSAPSEG (Schreiben wirkungslos,
  Lesen 0); REFRESH Bit 1..15.
- **Segment-Trap (AP P8, P8000):** `setSEGT` (nur Z8001; am A5120.16 nie benutzt, dort SEGT
  fest H). Pegel, nicht maskierbar, am Befehlsende abgetastet; Rangfolge NMI > SEGT > VI > NVI
  (§7.7), Scheinholen, Quittung Status 0100 (Kennung von den MMUs), PSA-Eintrag %10·2.
  Gekellert wird der nächste Befehl bzw. ein unterbrochener Wiederholungsbefehl selbst. Der
  Z8001 bricht **nicht** ab: der verletzende Befehl (bzw. Durchlauf) läuft zu Ende, das
  Unterdrücken von Schreibzugriffen ist SUP an der Karte. Der Pegel steht **nicht** in
  `Z8kRunState` (Aufbau = A5120-Save-State v7); die Karte stellt ihn beim Laden wieder her.
- **Beobachtung (AP P8):** `onException`/`lastException` (Art, Kennung, Ort, gekellerter und
  neuer Status, auch Reset), `lastCycle`/`lastCycleData`, `statusCount(st)` je Statuscode,
  Pegel-/Merkerabfragen (`nmiPending`, `segtLine`, …). Abdeckungsliste Handbuch → Test:
  `doc/p8000/z8000_abdeckung.md`.

## MAME-Orakel

`tools/dev.sh test-oracle` konfiguriert `build_oracle/` mit `-DK1520_Z8K_MAME_ORACLE=ON`, lädt
MAMEs `src/devices/cpu/z8000/{z8000.cpp,z8000.h,z8000cpu.h,z8000ops.hxx,z8000tbl.hxx}` zu
Stand `741d827a` (SHA256 geprüft) und übersetzt sie gegen `tests/oracle/mame_shim/`
(Ersatz für `emu.h`, eigener Code). Je Tabellenzeile 200 Zufallsfälle in vier Modi (Z8001
segmentiert System/Normal, Z8002 System/Normal): gleicher Zustand (beide Registerbänke,
Flags, PC, Speicher als Hashfunktion der Adresse) → ein Schritt → Vergleich von Registern,
FCW (ohne reservierte Bits), PC, allen geschriebenen Bytes, E/A-Schreibzugriffen.

Seit AP P8 (2026-10-07) zusätzlich **Flagmatrix** (jede Zeile × 64 Flagbelegungen × 4 Modi,
98 993 Fälle) und **Ausnahmen mit Quittung** (NMI/SEGT/VI/NVI, 7000 Fälle), beide ohne
Abweichung. Stand 2026-09-28: **309 264 Fälle, 0 Abweichungen**; bekannt und gemeldet: DAB (s. o.,
19 Fälle); angeglichen und gezählt: Bit 15 im gesicherten PC-Segmentwort (~10 500 Fälle).
Vom Orakel **nicht** abgedeckt: Z8001 unsegmentiert (MAME: „TODO"), Statusfolge/Scheinholen der Quittung,
Takte (MAME grob), µI/µ0-Befehle, LDCTL REFRESH, EPA — dafür die Unit-Tests.
