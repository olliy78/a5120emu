# Analyse: CP/A-Boot des A5120 bis zum Bedien-Prompt

Laufende Analyse des **letzten** Abschnitts der A5120-Boot-Kette: vom fertig
geladenen Betriebssystem (`@OS.COM`) bis zum interaktiven Prompt, an dem man Befehle
wie `dir` eintippen kann. Die vorgelagerten Stufen (ZRE-Boot-ROM → Chained Loader →
CP/A-Bootsystem → `@OS.COM`-Laden) sind in `doc/analyse_zre_rom_boot.md` und
`doc/analyse_bootloader.md` beschrieben; dieses Dokument beginnt **am OS-Eintritt**.

Stand: 2026-06-11. Werkzeug: `tools/k1520dbg` (interaktiver Debugger, siehe
`tools/README.md`).

---

## 0. Kurzfassung (TL;DR)

Mit den Testdisketten `disks/cpadisk_02.{img,hfe}` (System **ohne Uhr**, Autostart
`dir`) und `disks/cpadisk_mitUhr_01.{img,hfe}` (System **mit Uhr**, fragt Uhrzeit ab):

1. **Tastatur (K7637) — GEFIXT.** War komplett funktionslos (BIOS erkannte sie als
   „K7606"). Zwei Ursachen behoben → das BIOS erkennt jetzt „K7637", Tasten kommen im
   OS an, die Uhrzeit lässt sich am Prompt eingeben (Uhr läuft danach). §2.
2. **Floppy-Lesepfad des laufenden OS — FUNKTIONIERT.** Alle ZVE2-Lesezugriffe
   gelingen (IDAM gefunden, Daten gelesen, CRC ok); `(cyl2,head0,sec1)` liefert
   korrekt die CP/M-Directory. **Kein** Lesefehler. §3.
3. **Offener Blocker für `dir`: Arbeits-DPB `[0xD1BE]` wird nie befüllt (Henne-Ei).**
   SELDSK (`@0xDB91`) **baut den korrekten DPB** bei `0xD26F` (`spt=40`, `slc=3` =
   1024 B) — die Formaterkennung funktioniert. Aber die Arbeitskopie nach `[0xD1BE]`
   (Wrapper `C70E…C730`, Gate `C711: RET Z`) läuft nie, weil SELDSK `HL=0` liefert:
   SELDSKs **eigene Bestätigungs-Lesung** teilt über die BDOS-Sektor-Translation durch
   `[0xD1BE].spt`, das aber erst *nach* SELDSKs Rückkehr gefüllt würde → Division
   durch 0 (Hang `@0xC7A3`), SELDSK kehrt nie zurück. Per Experiment bestätigt: DPB
   nach `[0xD1BE]` poken → Hang löst sich, Lauf erreicht den TPA. §4–§5a.

Das widerlegt zwei frühere Annahmen: weder „fehlende Uhr" (doc
`refactoring_floppy_emulator.md` §15.5) noch „Floppy-Read scheitert" — beide Disketten
(mit/ohne Uhr) haben `[0xD1BE]==0`, und die Reads gelingen.

---

## 1. Ausgangslage: geladenes OS und sein Speicherlayout

Nach dem `@OS.COM`-Laden springt die Boot-Kette in den OS-Eintritt. Das OS relokiert
sich per `LDIR` (ZVE1 `@0x37EC`–`0x37FA`) an seine Arbeitsadresse und springt in den
BIOS-Sprungvektor `@0xD200` (`JP D200` am Ende der Relokation, `@0x37FA`).

**BIOS-Sprungvektor `@0xD200`** (17 `JP`-Einträge, via `k1520dbg` lokalisiert):

| # | Funktion | Ziel | | # | Funktion | Ziel |
|---|----------|------|-|---|----------|------|
| 0 | BOOT   | E7F0 | | 9 | **SELDSK** | **DB91** |
| 1 | WBOOT  | E7FC | |10 | SETTRK | DD88 |
| 2 | CONST  | E67E | |11 | SETSEC | DD8D |
| 3 | CONIN  | E68A | |12 | SETDMA | DD92 |
| 4 | CONOUT | E696 | |13 | READ   | DE0C |
| 5 | LIST   | E6D2 | |14 | WRITE  | DD9B |
| 6 | PUNCH  | E6BA | |15 | LISTST | E6C6 |
| 7 | READER | E6AE | |16 | SECTRAN| DD97 |
| 8 | HOME   | DD79 | |   |        |      |

**Wichtige RAM-Variablen des geladenen OS** (Arbeitsadressen):

| Adresse | Bedeutung |
|---------|-----------|
| `0xD1B2/D1B4` | Zeiger auf aktuelle Disk-/DMA-Strukturen (DPB-bezogen) |
| `0xD1B8` | Zeiger auf DPB-Quelldaten (für die DPB-Kopie); bleibt **0** |
| `0xD1BE` | 15-Byte-DPB ab hier; `[D1BE]` = Divisor der Sektor-/Größenrechnung; bleibt **0** |
| `0xD1CD` | abgeleiteter DPB-Zeiger |
| `0xD23F` | DPH (Disk Parameter Header) Laufwerk A: (von SELDSK berechnet) |
| `0xD26F` | IX-Basis der SELDSK-Strukturen (Skew-/Sektortabelle) |
| `0xE0F8/E0F9/E10F` | Format-Erkennungs-/DPB-Aufbau-Scratch (SELDSK) |
| `0xF800` | Text-VRAM (80×24) |

Alle `D1xx`-Variablen werden **nur einmal** geschrieben — `=0` durch das Relokations-
`LDIR` (`@0x37FA`) — und danach **nie wieder**. Das ist der Kern des Problems (§4).

---

## 2. Tastatur K7637 (GEFIXT 2026-06-11)

Die serielle Tastatur K7637 hängt an der K8025 (SIO A32 Kanal A): Daten-Port `0x5C`,
Status-Port `0x5D`. Das BIOS macht beim Kaltstart eine **automatische
Tastaturerkennung** (`coityp` in `CPA_Workbench/src/bc_a5120/bioskbdc.mac`): es sendet
`0x00` (Reset) an die Tastatur und wartet, dass sie ihren **Typ-Code** zurücksendet
(`IN 0x5D` bit0; dann `IN 0x5C`; `AND 0F0h`; `CP typc37` mit `typc37=0x80`). Bleibt die
Antwort aus, wird fälschlich eine parallele **K7606** angenommen → das OS pollt
nicht-existente PIO-Ports → keine Taste erreicht je das OS.

**Zwei Bugs, beide behoben:**

1. `K7637::processTxCommands()` wurde **nie** aus der Run-Loop aufgerufen
   (`core/machines/a5120/a5120.cpp`) → die TX-FIFO der Tastatur wurde nie geleert.
   Fix: Aufruf am Ende der Step-Schleife.
2. Die K7637 sendete ihren Typ-Code nie. Fix: `K7637::processTxCommands()` antwortet
   jetzt auf **jedes** Kommando-Byte mit `TYPE_CODE = 0x80`
   (`core/peripherals/k7637/k7637.{h,cpp}`). Verirrte Type-Code-Bytes sind harmlos —
   `0x80` steht nicht in der K7637-Scancode-Tabelle und dekodiert zu 0 (ignoriert).
   Das deckt auch den LED-Handshake ab (`lampen`/`lmpout` warten nach jedem Kommando
   auf dieselbe Antwort).

**Verifiziert:** Banner zeigt „Tastatur: K7637"; injiziertes „dir⏎" liest das OS aus
Port `0x5C` als `64 69 72 0D`; auf `cpadisk_mitUhr_01` wird die Uhrzeit `12:00:00` am
Prompt „Bitte Uhrzeit eingeben!" übernommen, danach läuft die Uhr. Alle 144 Tests grün.

> Einschränkung: Die K7637-Emulation liefert aktuell rohes ASCII. Sondertasten
> (Cursor, F-Tasten) werden **nicht** als K7637-Scancodes gesendet und daher nicht über
> die BIOS-`cp37`-Tabelle übersetzt — für reines Tippen irrelevant, bei Bedarf nachrüsten.

---

## 3. Floppy-Lesepfad des laufenden OS (FUNKTIONIERT)

Mit `tools/floppy_diag` bzw. `k1520dbg` nachgewiesen (cpadisk_02 + mitUhr):

- Das laufende OS liest über die ZVE2-Routine `@0x1F7D` (aus der Loader-Phase noch
  resident). **Jeder** Read gelingt: IDAM gefunden (erreicht `0x2038`), Daten gelesen,
  Spuren `c2h0/c2h1/c3h0/c3h1` Sektoren 1–5 **sequentiell, ohne Sektor-Wiederholung**
  → die Daten-CRC stimmt ebenfalls (ein CRC-Fehler würde denselben Sektor erneut lesen).
- Die Geometrie stimmt: `(cyl2,head0,sec1)` → Image-Offset `0x3B00` = echte CP/M-
  Directory (`@OS COM`, `CPABCGEN COM`, `FORMAT COM`, …). Der K5122-Feldmodell-Pfad
  (`buildRobotronTrack`, IDAM `A1 FE cyl head id sc`, Daten-CRC Seed `0xCDB4`) liefert
  diese 1024-B-Spuren korrekt.
- **Diese 20 Reads passieren VOR dem OS-Eintritt** (≈5,5–8,6 Mio. Zyklen; OS-Eintritt
  bei ≈8,5 Mio.) — es ist der **Boot-Loader**, der das System lädt. **Nach** dem
  OS-Eintritt liest das OS bis zum Hang **gar nicht** mehr.

⇒ Der `dir`-Blocker ist **kein** Floppy-Lese-Problem.

---

## 4. Der Hang: Division durch 0 in der DPB-basierten Rechnung

Nach „TPA ist OK!" dreht der **Vordergrund** dauerhaft in der Schleife `@0xC7A3`
(bei 150 Mio. Zyklen weiterhin dort; `0xE3xx/E6xx`-Stichproben sind nur der
periodische Timer-ISR):

```
C780  XOR A; LD (C805),A ; LD BC,(D1E2) ; HL=[[D1B4]] ; HL=[[D1B2]] ; ... ; H=A
C7A3  INC HL              ; \
      PUSH HL             ; |  16-Bit-Division durch wiederholte Addition:
      LD HL,(D1BE)        ; |    DE += [D1BE]   je Iteration
      ADD HL,DE ; EX DE,HL; |    Abbruch, wenn DE > BC (sub_C800 liefert Carry)
      ...                  ; |  Mit [D1BE]==0 wächst DE nie → Endlosschleife.
C7B1  CALL sub_C800       ; |  sub_C800@0xC800: (BC < DE)? → Carry
C7B4  JR NC, C7A3         ; /
```

`[0xD1BE]` ist der **Divisor** (records/sectors pro Spur o.ä. aus dem DPB) und bleibt
0. Beim Hang: `BC=0x0101` (Dividend), `DE=0`, `[D1BE]=0`. Division durch 0 → Hang.

`[D1BE]` wird befüllt von der **DPB-Kopierroutine** `@0xC714/0xC730`:

```
C70E  CALL DB91 (SELDSK)        ; -> Rücksprung C711
C714  LD E,(HL);INC HL;LD D,(HL); ... LD (D1B2),HL ; LD (D1B4),HL ; LD (D1CD),HL
C730  CALL C701  (memcpy 8 Byte nach D1B6)
C737  LD HL,D1BE; LD C,0F; CALL C701   ; <-- kopiert 15-Byte-DPB aus [[D1B8]] nach [D1BE]
```

`sub_C701` ist ein simples `memcpy (DE)->(HL), C Bytes`. **`C711`/`C714`/`C71D`
(`LD (D1B2),HL`) werden nie erreicht** (Watchpoint: `[D1B2]` wird nur vom Relokations-
`LDIR` `=0` geschrieben) — d. h. nach `CALL DB91` (SELDSK) läuft die Ausführung **nicht**
nach `C711` weiter. Also bricht SELDSK vorher ab.

---

## 5. SELDSK `@0xDB91`: automatische Formaterkennung bricht ab (offener Punkt)

SELDSK **wird** aufgerufen (Breakpoint `@0xDB91` trifft ≈4,8 Mio. Zyklen nach
OS-Eintritt; `[SP]→C711`, Aufrufer `@0xC70E`). Ablauf (per `k1520dbg`-Single-Step):

1. `DB91: CALL E88B` — berechnet aus dem Laufwerk (C=0) den DPH-Zeiger `HL=0xD23F`
   (Laufwerk A:), Drive-Check `< 3`.
2. `DB94…DBF2` — initialisiert Scratch bei `0xE0F8/E10F` und eine Tabelle ab `IX=0xD26F`.
3. `DBF3: PUSH IX; LD B,1A` … `DBF7: LD (IX+19),A; INC IX; INC A; DJNZ` — füllt eine
   **26-Einträge-Tabelle** (0x1A) mit aufsteigenden Werten (Skew/Sektor-Translation).
   *Hinweis:* 26 = Sektorzahl der 128-B-Systemspur, nicht 5 (1024-B-Datenspur).
4. `DC01: CALL DF2F` — **dauert ≈1,5 Mio. Zyklen ⇒ ein Disk-Read** (die eigentliche
   „automatische Formaterkennung"). Danach Verzweigungen `JR NZ` bei `DC04/DC14/DC4A`
   je nach Leseergebnis; bei `DC48` wird `A=0x03` (size_code 1024) probiert
   (`CALL DF2C`), `0xDF1D/0xDF2C/0xDF2F` sind die Lese-/Prüf-Helfer.

5. **Es ist eine Mehrfach-Probe-Schleife, die kein Format bestätigt** (per
   `k1520dbg`-Tracing, `t`/`b`/`s`). Die Erkennung liest **wiederholt** Track 0
   (`c0h0`, 128-B-Systemspur) **und** Datenspuren (`c2/c3`, 1024-B) — die komplette
   Lesesequenz läuft mehrfach durch (Retry-Schleife; `c3h0` z. B. 15× pro Durchlauf).
   **Die Reads selbst gelingen** (IDAM gefunden → `0x2038`, Daten + CRC ok, via
   `floppy_diag` bestätigt) — die **Format-Bestätigungslogik auf ZVE1-Seite verwirft
   aber das Ergebnis** und probiert erneut. Wichtige Punkte im Datenpfad:
   `DC01: CALL DF2F` (= ein Disk-Read, ~1,5 Mio. Zyklen) → `sub_DF37` kopiert 9
   Format-Parameter-Bytes nach `0xE112` (`12 00 03 FF 01 01 01 00 00`; `03` =
   size_code 1024) → `BIT 5,(E112)`=0 (kein `JP E3D6`) → `DF4E: CALL E88B`
   (DPH-Lookup, liefert `0xD23F` für Laufwerk 0, **kein** Abbruch) → … → `E00B:
   BIT 4,(E112)=1 → JR NZ E042` → `E04D: RET` mit **`HL=0x0101`, `DE=0x0003`**
   (Ergebnis „Format nicht bestätigt"). `[0xD1B8]` (Zeiger auf den gewählten DPB)
   wird dabei **nie** gesetzt → die DPB-Kopie `C730/C737` läuft nie → `[D1BE]=0`.
   *(Korrektur: `0xE00B` ist **kein** Fehlerausgang — es ist `BIT 4,(E112)` im
   normalen Probe-Fluss; eine frühere Lesart war falsch.)*

**Befund:** Die SELDSK-Formaterkennung **liest die (gemischt 128-B/1024-B-)Diskette
korrekt**, kann das Format aber **nicht bestätigen** und wählt keinen DPB aus
(`[D1B8]` bleibt 0). Das später benötigte `[D1BE]` (= `dpbspt`, 128-B-Sektoren/Spur)
bleibt 0 → Division durch 0. Der Wert `HL=0x0101` aus der Erkennung taucht als
Dividend `BC=0x0101` in der Hang-Schleife wieder auf.

### 5a. Durchbruch (2026-06-11): der DPB ist KORREKT — nur die Arbeitskopie fehlt

Per `k1520dbg` weiter eingegrenzt — **die Formaterkennung funktioniert in Wahrheit**:

- SELDSK **baut einen vollständig korrekten DPB** bei `0xD26F`:
  `28 00` = `dpbspt = 0x28 = 40` (= 5×1024/128, exakt richtig!), `dpbbls=04` (2K),
  `dpbslc=03` (1024 B), `dpbsiz=0x018F`, `dpbdir=0x00BF`, … Die DPH bei `0xD23F`
  (`+0x0A → 0xD26F`) zeigt korrekt auf diesen DPB.
- Der **Arbeits-DPB `[0xD1BE]` wird nie befüllt** — die Routine, die ihn aus dem
  ausgewählten DPB kopiert, läuft nie. Der Wrapper:
  `C70E: CALL D21B (=SELDSK über Vektor D200[9]); C711: LD A,H; OR L; RET Z;
  C714…: DPH→Arbeitsvariablen [D1B0/B2/B4/CD]; C730/C737: DPB→[D1BE] kopieren`.
  **`C713: RET Z`** überspringt die ganze Einrichtung, **wenn SELDSK `HL==0`
  (Fehler) zurückgibt.**
- **Henne-Ei (per Stack-/Watch-Trace bestätigt):** SELDSK kehrt nie nach `C711`
  zurück — seine **eigene Bestätigungs-Lesung** geht über die normale BDOS-Sektor-
  Translation (`D17D→CC49→CAEC→C9E7→Divide @0xC7A3`), die durch `[D1BE].dpbspt`
  teilt. Da `[D1BE]` zu diesem Zeitpunkt 0 ist (wird **erst nach** SELDSKs Rückkehr
  via `C730` kopiert), Division durch 0 → SELDSK hängt, bevor es zurückkehrt. Zwischen
  `C70E` und dem Hang werden `[D1BE]`/`[D1B2]` nachweislich **nie** geschrieben.
- **Beweis-Experiment:** Am Hang `[0xD1BE..]` mit dem gültigen DPB aus `0xD26F`
  überschreiben (`k1520dbg`: `gu 0xC7A3` → `e 0xD1BE 0x28 0x00 …`) und weiterlaufen →
  der Hang **löst sich**, die Ausführung läuft in den TPA (`PC≈0xAC71`), der
  Bildschirm wird gelöscht. ⇒ Bestätigt: der DPB ist korrekt, **es fehlt allein die
  Befüllung der Arbeitsvariablen** (`[D1BE]` + `[D1B0/B2/B4/CD]`).

### 5b. Quell-Abgleich (2026-06-11) — SELDSK nimmt den Fehlerpfad `seldle`

Abgleich mit `CPA_Workbench/src/bc_a5120/biosdsk.mac` (`seldsk` ab Z.40) +
`biosdpbm.mac` (DPB-Struktur) + `biosnuc.mac` (`dgetpb` Z.772):

- `seldsk: call dgetpb` (HL→DPH, IX→DPB) `; bit 0,e; ret nz` — der **LOGIN-Pfad**
  (E bit0=0) macht die Formaterkennung. Der Wrapper `C70E` ruft mit E=0 (LOGIN).
- Die Erkennung (`drdfrm`) liest mit **physischen** Routinen `dsidtr`/`dsidtt`/`dbtran`
  und baut den DPB komplett über **IX** (kein `[D1BE]`): `dpbspt`, `dpbslc`,
  Kapazität `((Tracks-ofs)*spt/2^bls)-1` (Z.370-389) — alles IX. Bei Erfolg
  `selext → selexx: pop hl; ret` (HL=DPH≠0). Bei einem fehlgeschlagenen/unsicheren
  Read-Check (`seldle`, Z.221-224) `ld (ix+dpbslc),0ffh; ld hl,0; ex (sp),hl; jp
  selext` → Rückgabe **HL=0**.
- **Empirisch (k1520dbg):** Es gibt **zwei** seldsk-Aufrufe — einer (Kaltstart) baut
  `D26F` korrekt (dpbslc=03, Geometrie in der Statuszeile angezeigt); der zweite
  (≈13,34 Mio. Zyklen, vom `dir`-Pfad via Wrapper `C70E`) endet mit Rücksprung
  **`->0000`** = der `seldle`-`ex (sp),hl`-Fehlerausgang ⇒ `HL=0`. Damit `C711:
  RET Z` → keine `[D1BE]`-Kopie.
- **Henne-Ei bleibt:** Der Hang `@0xC7A3` (Divide durch `[D1BE].spt=0`) tritt
  **während** dieses zweiten seldsk auf (vor `C711`); seine physischen
  Erkennungs-Reads gehen über die **Sektor-Translation des Disk-Transfer-Moduls**
  (`dbtran`/`dsidtr`), die durch die *globale* `[D1BE].spt` teilt. Dieses
  Transfer-Modul (`diskio`/8272-/K5122-Treiber) liegt **nicht** in den vorhandenen
  Quellen, daher endet der Quell-Abgleich hier.

**Konsequenz für den Fix:** Auf echter Hardware muss `[D1BE].spt` schon **vor** den
Erkennungs-Reads gültig sein (sonst träfe der Hang die echte Maschine). Entweder
hinterlässt der **Kaltstart-Login** von A: ein gültiges `[D1BE]` (in unserer Emulation
geschieht das nicht — der erste Login schlägt vermutlich aus demselben Grund fehl bzw.
kopiert nie), oder ein Erkennungs-Read liefert in unserer K5122-Emulation eine andere
**Rückmeldung** (Status/Sektor-ID-Spur/Index-Puls-Sektorzahl) als die echte Hardware,
sodass `seldsk` `seldle` nimmt. **Zum endgültigen Fix fehlt das Disk-Transfer-Quellmodul**
(Translation/`dbtran`) bzw. eine Build-Listing-Adresszuordnung des geladenen OS.

**Eigentliche offene Frage (Fix-Ziel):** Auf echter Hardware muss `[D1BE].dpbspt`
**vor** SELDSKs Bestätigungs-Lesung gültig sein (sonst träfe der Hang auch die echte
Maschine). Es fehlt also ein **Schritt, der den (Trial-)DPB/`spt` schon vor/in SELDSK
in die Arbeitsvariablen schreibt** — bzw. ein Zweig in SELDSK, der in unserer
Emulation anders genommen wird (vermutlich abhängig von einer Lese-Rückmeldung des
K5122, die die echte Hardware liefert). Den genau zu finden ist der nächste Schritt:
SELDSK des geladenen OS (`0xDB91`, BDOS-Login/`C9xx`-Translation) gegen
`CPA_Workbench/src/bc_a5120/biosdsk.mac`/`biosnuc.mac` abgleichen und die Stelle
suchen, an der `[D1BE]`/`spt` (oder ein Default-DPB) gesetzt werden **sollte**.

**Konkreter nächster Schritt** (mit `k1520dbg`): die **eine Bestätigungsprüfung**
finden, die unsere korrekt gelesene Diskette nicht besteht. Kandidaten: (a) die
Auswertung der gemischten Geometrie (Zahl der 26×128-Systemspuren, `dpbfsm`) — die
Erkennung liest dazu Track 0 *und* Datenspuren und vergleicht; (b) ein nach dem Read
erwartetes **Status-/Längenkriterium**, das die echte K5122-Hardware liefert und
unsere Emulation nicht (z. B. tatsächlich gelesene Bytezahl/Sektorgröße, Index-Puls-
Sektorzählung). Abzugleichen mit `CPA_Workbench/src/bc_a5120/biosdpb.mac` (DPB-Tabelle,
Feld `dpbspt`/`dpbslc`/`dpbfsm` — siehe `biosdpbm.mac` für die Struktur) und der
SELDSK-Quelle. Repro/Trace: `printf 't 0xDB00 0xE120 6000\ngu 0xC7A3\nq\n' |
build/k1520dbg disks/cpadisk_02.hfe` und die K5122-Reads via
`boot_trace --log-cycle 9000000:16000000:info`.

---

## 6. Reproduktion / Werkzeug-Rezepte

```sh
cmake --build build -j --target k1520dbg kbd_test

# Bildschirm-Smoke-Test (zeigt Banner; ohne DPB kein dir/Prompt):
./build/kbd_test disks/cpadisk_02.hfe dir

# Hang lokalisieren (Marker am OS-Eintritt, Breakpoint auf die Divide-Schleife):
printf 'mark 0x37A0\nb 0xC7A3\ng\nr\nvars\nq\n' | ./build/k1520dbg disks/cpadisk_02.hfe

# SELDSK-Formaterkennung verfolgen:
printf 'b 0xDB91\ng\ns 80\nq\n'                 | ./build/k1520dbg disks/cpadisk_02.hfe
printf 't 0xDB91 0xDD00 600\ngu 0xC7A3\nq\n'    | ./build/k1520dbg disks/cpadisk_02.hfe

# mit Uhr: Zeit eintippen, dann weiter (Tastatur verifizieren):
./build/kbd_test disks/cpadisk_mitUhr_01.hfe "12:00:00"
```

Schlüsseladressen (geladenes OS): SELDSK `0xDB91`, DPB-Setup `0xC714`/`0xC730`,
memcpy `0xC701`, Divide-Schleife `0xC7A3`/`sub_C800@0xC800`, Format-Helfer
`0xDF1D/DF2C/DF2F`, BIOS-Vektor `0xD200`, OS-Eintritt `0x37A0`, Relokation `0x37EC–37FA`.

---

## 7. Status & offene Punkte

- [x] Tastatur K7637 erkannt und funktionsfähig (§2).
- [x] Floppy-Lesepfad des laufenden OS verifiziert korrekt (§3).
- [x] Hang als Division-durch-0 (`[D1BE]==0`) lokalisiert (§4).
- [x] **Durchbruch (§5a):** SELDSK baut den **korrekten DPB** (`0xD26F`, `spt=40`,
      `slc=3`) — die Formaterkennung funktioniert. Der **Arbeits-DPB `[0xD1BE]` wird
      nie befüllt** (Wrapper-Gate `C711: RET Z`, weil SELDSK `HL=0` liefert). Ursache:
      **Henne-Ei** — SELDSKs eigene Bestätigungs-Lesung teilt durch `[D1BE].spt=0`
      (Divide-Hang `@0xC7A3`), bevor SELDSK zurückkehrt; `[D1BE]` würde aber erst
      *nach* der Rückkehr gefüllt. Per Experiment bestätigt (DPB nach `[D1BE]` poken
      → Hang löst sich, Lauf erreicht TPA).
- [ ] **Offen (Fix):** den Schritt finden, der `[D1BE]`/`spt` (oder einen Default-DPB)
      **vor** SELDSKs Bestätigungs-Lesung setzen müsste — bzw. den Emulations-bedingt
      anders genommenen Zweig in SELDSK (wahrscheinlich abhängig von einer
      K5122-Lese-Rückmeldung). Abgleich: geladenes SELDSK `0xDB91` + BDOS-Translation
      `0xC9xx`/`0xD17D` gegen `biosdsk.mac`/`biosnuc.mac`. Danach `dir` + Prompt;
      Integrationstests verschärfen (`tests/cpp/test_boot_integration.cpp`).
