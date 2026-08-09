# INIT.COM (SCPX 1526) — Dialoggesteuertes Disketten-Formatieren

Analyse des SCPX-Formatierprogramms `INIT.COM` (das SCPX-Äquivalent zu CP/A `FORMAT.COM`).
Basis: extrahierte Datei `…/CPA_Workbench/Disketten/A5120_SCPX_Boot/init.com` (5248 B),
disassembliert mit `tools/z80_disasm2.py --org 0x100`. Ziel: Bedienung/Dialoge dokumentieren,
um danach gezielte Formatier-Tests im Emulator zu fahren.

Zugehörig: [[project_scpx_boot]], [[project_scpx_com_load_bug]] (Laufzeit-.COM-Laden gelöst),
`doc/analyse_scpx_com_load.md` (SCPX-BIOS-Lesepfad), `doc/format.md` (CP/A-FORMAT-Pipeline).

---

## 1. Zweck & Grobablauf

`INIT.COM` ist ein CP/M-`.COM` (lädt nach `0x0100`), das über einen **Textdialog** ein
Diskettenformat auswählen lässt und die Zieldiskette **direkt über die K5122-Controllerports**
formatiert (kein BIOS-Format-Call — s. §6). Ablauf:

```
Versions-Check (SCPX ≥ 1.5)
  → Banner
  → Laufwerksname abfragen        (A/B/C…)
  → Laufwerkstyp bestimmen        ((IX+21) aus Laufwerks-Deskriptor)
  → passendes Format-Menü zeigen  (1 von 6, je Typ)
  → Formatauswahl / <ENTER>=Default
  → "Diskette einlegen … <ENTER>"
  → "ALLE Dateien werden gelöscht (Y/N)"
  → Formatieren (Fortschritt "FORMATTING CYL.: nn")
  → Ergebnis: "FORMATTING COMPLETE" + BAD-TRACKS-Liste
  → optional Bad-Tracks in Fehlerdatei (ERRFILE.SAV) eintragen
  → "ONCE MORE (Y/N)"  → Schleife oder Ende
```

## 2. Vollständiger Dialogfluss (mit Codeadressen)

| # | Adresse | Ausgabe / Eingabe | Bemerkung |
|---|---------|-------------------|-----------|
| 0 | `0100` | *(still)* Versions-Check | liest `(003E)+30`, verlangt Version ≥ `"15"`; sonst `SORRY, SCPX 1.5 (OR UPWARD) IS REQUIRED !` und Abbruch |
| 1 | `0167` | `INIT 1520(SCPX)  V 1.5` + `====` | Banner (setzt eigenen SP `1554H`) |
| 2 | `01A9` | `PLEASE ENTER DRIVE NAME:` | liest **1 Zeichen** (`sub_0681`). Ungültig → `INVALID DRIVE NAME ?!?!`, wiederholt |
| 3 | `01FE` | *(still)* Laufwerks-Deskriptor holen | `C = eingegebener_Buchstabe − 'A'`; prüft gegen Laufwerkszahl `(003E)+62`; Deskriptor-Zeiger aus Tabelle `(003E)+41`, Stride `0x10`, +`0x0A` → **`IX` = Deskriptor** |
| 4 | `022C` | `DISK FORMAT MAY BE:` | Kopfzeile des Menüs |
| 5 | `024F` | *(still)* Menü wählen | `A=(IX+21) & 0x83` → Zeigertabelle (§3) → **Deskriptor `IY`** = {Menütext, Parametertabelle}. Ist der Eintrag `0` → `S Y S T E M  E R R O R - INIT ABORTED` |
| 6 | `02A8` | *das gewählte Menü* (1 von 6, §4) | z. B. `0 = DD-DS 16*256 (DEFAULT) … 4 = DD-SS 5*1024` |
| 7 | `02B3` | `PLEASE SELECT FORMAT OR HIT <ENTER> FOR DEFAULT:` | `sub_0681`; `<ENTER>` ⇒ Ziffer `'0'` (Default). Kein Treffer in Parametertabelle → `INVALID SELECTION !?!?`, Menü erneut |
| 8 | `0323` | *(still)* Formatblock wählen | Parametertabelle (3-Byte-Einträge `[Ziffer][Ptr]`, `0xFF`-terminiert) → **Formatblock-Zeiger** (§5). Sonderfall Default `0x0CDD`: falls `(IX+32) ≠ 'O'` → Ersatzblock `0x0CED` |
| 9 | `0355` | `PLACE DISK TO BE FORMATTED INTO DRIVE B AND PRESS <ENTER>` | wartet auf `<ENTER>`. Der Laufwerksbuchstabe im Text ist **dynamisch** (zeigt das gewählte Laufwerk; Patch bei `033A`–`0351`), nicht fix „A". |
| 10 | `03AB` | `WARNING ! ALL FILES ON THIS DISK WILL BE SCRATCHED (Y/N):` | nur `Y` fährt fort, sonst zurück/Abbruch |
| 11 | `0400` | `W A I T !  (FORMATTING CYL.: 00)` | Fortschritt; die `00` wird je Zylinder hochgezählt |
| 12 | `0553` | `FORMATTING COMPLETE` + `BAD TRACKS:` (`- NO -` wenn keine) | Ergebnis; defekte Spuren werden aufgelistet |
| 13 | `0934` | (bei Defekt) `SYSTEM TRACKS DEFECTLY` bzw. `DIRECTORY TRACKS DEFECTLY - CORRECTION NOT POSSIBLE` | Systemspuren/Directory defekt → nicht nutzbar |
| 14 | `0615` | `MAP BAD TRACKS TO ERROR FILE ? (Y/N):` | trägt Bad-Tracks in **`ERRFILE.SAV`** ein (Belegung), damit CP/M sie meidet |
| 15 | `0660` | `ONCE MORE ? (Y/N):` | `Y` → zurück zu Schritt 2; sonst `ENDE`, SP zurück, `RET` (Warmstart) |

**Fehlermeldungen während des Formatierens** (Handler `L0FAC`, Code in `B`):
`DISK WRITE PROTECTED !`, `BAD DRIVE SPEED !`, `TRACK 00 NOT FOUND !`,
`INIT ERROR - BAD SECTOR (PRESS <ENTER> TO REPEAT OR ^C TO ABORT)`, `UNDEFINED ERROR - INIT ABORTED !`.

## 3. Laufwerkstyp → Menü-Auswahl

Maßgeblich ist **`(IX+21) & 0x83`** aus dem Laufwerks-Deskriptor (`IX`). `BIT 7` wählt die
Zeigertabelle, `Bits 1..0` den Index darin (`0254`–`0268`):

| `(IX+21)&0x83` | Tabelle | Deskriptor | Menü (§4) |
|----------------|---------|-----------|-----------|
| `0x00` | A `099F` | `09AF` | **M0** SD-SS (4×1024 / 26×128) |
| `0x01` | A `099F` | `09B3` | **M1** SD-DS |
| `0x02` | A `099F` | `09B7` | **M2** DD-SS (8×1024 …) |
| `0x03` | A `099F` | `09BB` | **M3** DD-DS (8×1024 …) |
| `0x80` | B `09A7` | `0000` | **SYSTEM ERROR** |
| `0x81` | B `09A7` | `0000` | **SYSTEM ERROR** |
| `0x82` | B `09A7` | `09BF` | **M4** DD-SS 16×256 … |
| `0x83` | B `09A7` | `09C3` | **M5** DD-DS 16×256 … |

> Für unsere **SCPX-Bootdiskette** (`disks/scpx17_cpa780_k5601.hfe`, Laufwerk A: = 5,25″ **DS 80-Spur MFM,
> 16×256**) ist der native Typ **DD-DS 16×256** → **Menü M5** (`(IX+21)&0x83 = 0x83`).
> **✓ Empirisch bestätigt** (2026-07-11, `k1520dbg`): `INIT` → `A` zeigt exakt M5
> (`0=DD-DS 16*256 (DEFAULT) … 4=DD-SS 5*1024`), Dialog läuft bis `PLEASE SELECT FORMAT`.

Deskriptorformat (je 4 B): `field0` = Zeiger auf Menütext, `field1` = Zeiger auf Parametertabelle.

## 4. Die sechs Format-Menüs (Menütexte)

Kürzel: **SD**=Single Density (FM) / **DD**=Double Density (MFM); **SS**/**DS**=Seiten;
danach *Sektoren/Spur × Sektorgröße*.

```
M0 (0x09C7):  0 = SD-SS   4*1024  (DEFAULT)      M1 (0x0A07):  0 = SD-DS   4*1024  (DEFAULT)
              1 = SD-SS  26* 128                               1 = SD-SS   4*1024
                                                               2 = SD-SS  26* 128

M2 (0x0A60):  0 = DD-SS   8*1024  (DEFAULT)      M3 (0x0AB9):  0 = DD-DS   8*1024  (DEFAULT)
              1 = SD-SS   4*1024                                1 = DD-SS   8*1024
              2 = SD-SS  26* 128                                2 = SD-DS   4*1024
                                                               3 = SD-SS   4*1024
                                                               4 = SD-SS  26* 128

M4 (0x0B44):  0 = DD-SS  16* 256  (DEFAULT)      M5 (0x0B9D):  0 = DD-DS  16* 256  (DEFAULT)
              1 = DD-SS  26* 128                                1 = DD-SS  16* 256
              2 = DD-SS   5*1024                                2 = DD-SS  26* 128
                                                               3 = DD-DS   5*1024
                                                               4 = DD-SS   5*1024
```

## 5. Format-Parameterblöcke (Geometrie)

Jede Menü-Ziffer verweist über die Parametertabelle (Deskriptor-`field1`) auf einen **16-Byte-
Formatblock** in `0x0C6D…0x0D0D`. Diese Blöcke parametrieren die Low-Level-Formatroutine (§6).
Erkannte Felder (aus Quervergleich): **Byte 7 = Sektoren/Spur**, **Byte 9 = Sektoren/Zylinder**
(= Sektoren/Spur × Seiten), Größe folgt aus dem Menünamen. Byte 0 ist ein Formatcode
(Bit 7/6 ≈ Dichte/Seiten), Byte 6 ein größen-/gap-abhängiger Wert, Byte 10/11 Dichte-/Seiten-
Steuerworte (verwandt mit den SCPX-Steuerworten 0x81/0x83).

| Block | Format | B7 Sekt/Spur | B9 Sekt/Zyl | Rohbytes (0..15) |
|-------|--------|:---:|:---:|------|
| `0C6D` | SD-SS 4×1024   | 04 | 04 | `03 00 00 04 00 25 47 04 03 04 01 82 00 08 10 02` |
| `0C7D` | SD-SS 26×128   | 1A | 1A | `00 00 00 04 00 25 16 1A 02 1A 03 04 00 08 08 10` |
| `0C8D` | SD-DS 4×1024   | 04 | 08 | `07 00 00 04 00 25 47 04 02 08 81 02 00 10 10 04` |
| `0C9D` | DD-SS 8×1024   | 08 | 08 | `43 00 00 00 00 4D 6E 08 02 08 81 04 00 10 10 04` |
| `0CAD` | DD-DS 8×1024   | 08 | 10 | `47 00 00 00 00 4D 6E 08 01 10 81 04 00 20 10 08` |
| `0CBD` | DD-SS 16×256   | 10 | 10 | `E1 00 00 04 00 1A 30 10 03 10 03 82 00 08 10 08` |
| `0CCD` | DD-SS 26×128   | 1A | 1A | `C0 00 00 04 00 1A 23 1A 02 1A 03 04 00 08 08 10` |
| `0CDD` | DD-DS 16×256 *(Default M5)* | 10 | 20 | `E5 00 00 04 00 1A 30 10 02 20 83 02 00 10 10 10` |
| `0CED` | DD-DS 16×256 *(Ersatz, `(IX+32)≠'O'`)* | 10 | 20 | `E5 00 00 04 00 1A 30 10 02 20 03 82 00 08 10 08` |
| `0CFD` | DD-SS 5×1024   | 05 | 05 | `D3 00 00 04 00 1A 6E 05 03 05 01 83 00 10 10 04` |
| `0D0D` | DD-DS 5×1024   | 05 | 0A | `D7 00 00 04 00 1A 6E 05 02 0A 81 03 00 10 10 04` |

Byte 5 unterscheidet Spuren/Systembereich (`0x25`/`0x4D`/`0x1A`), Byte 13/14/15 sind Gap-/
Sync-Längen. *(Feinbedeutung der restlichen Bytes ist für die Bedienung irrelevant; sie fließt
1:1 in die FDC-Programmierung.)*

## 6. Wie INIT.COM formatiert (wichtig fürs Testen)

INIT.COM benutzt **keinen BIOS-Format-Call**, sondern programmiert den **K5122-Controller direkt**
(Routine ab `0x0D30`):

- Installiert einen **eigenen Interrupt-Vektor**: sichert `(F7E0H)`, setzt ihn auf eigenen ISR
  `0x124A`, mit `DI`/`EI`-Klammer.
- **Kommando-/Setup-Sequenz** an Port `11H`: `OUT(11H),E0` · `OUT(11H),97` · `OUT(11H),FF`.
- **Modus** `OUT(12H),04`; **Statuspolling** `IN(12H)` (Bit 7 = Datenanforderung `RLCA`,
  Bit 0 = `RRCA`, Bit 5 = Index/Write-Protect).
- **Byte-Ausgabe/Strobe** an Port `10H`: `OUT(10H),E` · `OUT(10H),E&7F` · `OUT(10H),E|80`
  (klassischer /STR-Low/High-Strobe — dasselbe Strobe-Schema wie ZVE2 beim Lesen).
- Port `18H` erhält einen Formatcode aus dem Block.
- **Fehlercodes** (Reg `B`) → `L0FAC` → Meldung: `0x52`…`0x54` = BAD SECTOR / WRITE PROTECT /
  DRIVE SPEED / TRACK 00.

> **Testrisiko:** Der CP/A-`FORMAT.COM`-Schreibpfad im K5122 ist erprobt
> ([[project_format_track_write_missing]]), aber INIT.COM fährt eine **eigene Port-/ISR-Sequenz**
> mit eigenem Timing. Ob unser K5122-Modell genau diese Sequenz (v. a. den eigenen ISR an `F7E0`
> + das `OUT(11H)`-Kommandowort + Port `18H`) korrekt bedient, ist der offene Punkt, den die Tests
> in §7 klären. Der Formatier-Schreibpfad (`parseFormatStream`/`commitFormatTrack`) ist vorhanden;
> die Frage ist die Port-Protokoll-Kompatibilität, nicht das Medium.

## 7. Testplan (Formatieren im Emulator)

Vorbereitung: Boot `disks/scpx17_cpa780_k5601.hfe` bis `A>` (interaktiver Prompt, `os_running_`-Gate aktiv,
.COM-Laden funktioniert). Werkzeuge: `k1520dbg -s tools/scpx1526.sym` (batch via `-x`/Pipe;
`keys` braucht **literales `\r`** für Enter), `boot_trace --until PC==0xE079` als Boot-Check.
COW-Mount ist Default → Fixture unkorrumpierbar; für persistente Schreibtests `--rw` auf **Temp-Kopie**.

Stufen:

1. **Dialog-Smoke (nur lesen, keine Formatierung):** `INIT\r` laden → Banner prüfen →
   `A\r` (Laufwerk) → **Menü M5 verifizieren** (bestätigt `(IX+21)&0x83=0x83`) → an
   `PLEASE SELECT FORMAT` mit `^C` abbrechen. Kein Schreibzugriff nötig.
2. **Default-Format (DD-DS 16×256) auf Temp-Kopie** (`--rw`): `INIT\r A\r 0\r`
   (oder `<ENTER>`=Default) → `<ENTER>` (Disk einlegen) → `Y` (scratch) → bis
   `FORMATTING COMPLETE` + `BAD TRACKS: - NO -` laufen lassen. Danach die Temp-Kopie mit
   `tools/scpx_extract` / `k1520dbg disk verify` prüfen (2560 Sektoren, 0 CRC-Fehler).
3. **Alternativformate aus M5** (`1`=DD-SS 16×256, `2`=DD-SS 26×128, `3`=DD-DS 5×1024,
   `4`=DD-SS 5×1024): je auf leere Temp-Disk passender Geometrie (`k1520_create_disk` /
   `DiskImage::create`), formatieren, verifizieren.
4. **Regression:** erfolgreicher Pfad als GoogleTest analog `ScpxIntegration.BootThenDirStatPipLoadComFiles`
   festhalten (`tests/integration/test_boot_integration.cpp`), Boot→INIT→Format→Verify.

Erwartete Stolpersteine (aus §6): eigener ISR `F7E0`, `OUT(11H)`-Kommandowort, Port `18H`-Code,
Per-Byte-/BUSRQ-Timing im Schreibmodus. Falls INIT.COM früh mit `BAD DRIVE SPEED`/`TRACK 00 NOT
FOUND` abbricht, liegt es an genau dieser Port-/Timing-Emulation — dann §6 als Fixpunkt heranziehen.

## 7a. Empirischer Formatier-Test (2026-07-11) — ✗ INIT-Format schlägt fehl

Durchgeführt: Leere DD-DS-16×256-Diskette (`k5601_16x256`) via `tools/mk_blank` angelegt, als **B:**
gemountet, im Emulator `INIT` → `B` → Format `0` (DD-DS 16×256) → `<ENTER>` → `Y` gefahren
(`k1520dbg`, COW). Ergebnis:

```
W A I T !           (FORMATTING CYL.: 00)
BAD DRIVE SPEED !
```

**INIT bricht auf Zylinder 00 mit `BAD DRIVE SPEED !` ab.** Ursache (durch k1520dbg-Trace
schrittweise verifiziert — s. Handoff `doc/plan_k5122_rotation_timing.md`):

> ⚠️ **Korrektur einer früheren Fehldiagnose.** Zuerst vermutet: INITs Strobes würden über
> `doStep()` den Kopf von Spur 0 wegsteppen (Port-10H-Bit7 = `/ST`). **Falsch** — die
> Rekalibrier-Routine `sub_0DC4` liest beim ersten `IN(12H)` sofort `/TO=0` (Kopf auf Spur 0,
> `handleCtrlPortAWrite` refresht Port B in Zeile 651) und INIT erreicht die echte Format-Engine
> bei `0x0E14`. Der Fehler kommt **nicht** aus dem Recalibrate, sondern aus der **Drehzahlmessung**.

- **Fehlercode = `0x55`** (nicht `0x54`; `0x54`=UNDEFINED, `0x53`=WRITE PROTECTED). `0x55` wird bei
  **`0x0E36`** gesetzt, erreicht über die JP-(HL)-State-Machine (`0x0F01`/`0x0F13`).
- **Drehzahl-Fenster-Prüfung `0x0E19`–`0x0E36`:** INIT liest `[12A6]` (gemessener Zählwert) und
  verlangt **`5219 ≤ [12A6] ≤ 5323`** für MFM (`BC=1463H`, Toleranz `DE=0068H`; FM-Zweig
  `BC=1868H`/`DE=007DH` = `6248…6373`). Außerhalb → `LD B,55H` → BAD DRIVE SPEED (nach `[129F]`
  Retries).
- **Messung `0x0FF2`–`0x1012`:** eine Schleife `OUT(14H),A · INC BC · CP (HL) · OUT(14H),A · INC BC ·
  JR Z` läuft, bis die **Index-ISR** `[12A8]` umschaltet, und legt `BC` (= Anzahl akzeptierter
  Datenport-Schreibzugriffe pro **Umdrehung**) in `[12A6]` ab. Also: **wie viele `OUT(14H)`-Bytes
  passen in eine Index→Index-Periode.**
- **Warum es scheitert:** Die Index-Periode ist bei uns **korrekt** (`indexPeriodCycles = 2450000·60/300
  = 490000 Takte` = reale 300 rpm @ 2,45 MHz). Aber die **Datenport-Schreibzugriffe (`OUT 14H`)
  werden NICHT rotationsgetaktet**: real hält der Controller die CPU je Byte per `/WAIT`/`/BUSRQ` an,
  bis die Scheibe ein Byte-Slot weitergedreht hat (~93 Takte/Byte bei MFM); unser Modell taktet
  entweder gar nicht (CPU läuft frei) oder über die **boot-getunte Per-Byte-Drossel
  `kBytePeriodCycles = 150`** — beides liefert einen `[12A6]`-Wert außerhalb `5219…5323` → BAD DRIVE
  SPEED. Der Kern ist **kein programmspezifischer Sonderfall**, sondern eine fehlende
  Hardware-Eigenschaft: der rotationsgekoppelte, per CPU-`/WAIT` erzwungene **reale Byte-Takt**.

**Folge:** Ein per INIT frisch formatierter Datenträger lässt sich derzeit **nicht** erzeugen. Der
saubere Fix (Umbau des K5122 auf einen rotationsgekoppelten realen Byte-Takt) ist geplant in
**`doc/plan_k5122_rotation_timing.md`**.

**Zugriffstest auf eine korrekt formatierte DD-DS-16×256-B: — ✓ funktioniert.** Statt der
(fehlgeschlagenen) INIT-Formatierung eine via `tools/mk_blank … k5601_16x256` erzeugte Leerdiskette
(= exakt DD-DS 16×256) als B: gemountet und im laufenden SCPX getestet:

| Kommando | Ergebnis |
|----------|----------|
| `DIR B:` | `NO FILE` (leere Disk korrekt gelesen) ✓ |
| `STAT B:` | `Bytes Remaining On B: 620k` ✓ |
| `PIP B:=A:STAT.COM` | kehrt sauber zu `A>` zurück ✓ |
| `DIR B:` (danach) | `B: STAT     COM` — Datei auf B: kopiert ✓ |

→ Der Emulator **kann** eine DD-DS-16×256-Diskette auf B: erzeugen, lesen (DIR/STAT) und
beschreiben (PIP) — nur INIT.COMs *eigener* Formatierer läuft (noch) nicht. Für Formatier-Tests bis
zum K5122-Format-Protokoll-Fix daher `mk_blank`/`createDisk` als Format-Ersatz nutzen.

**Fix (geplant, nicht umgesetzt):** kein programmspezifischer Sonderfall, sondern
Hardware-treue nachrüsten — den K5122 auf einen **rotationsgekoppelten realen Byte-Takt** umbauen,
der ALLE Datenport-Transfers (`OUT 14H` schreiben / `IN 16H` lesen) per CPU-`/WAIT` an die reale
Byte-Rate der drehenden Scheibe bindet (die boot-getunte `kBytePeriodCycles`-Drossel ersetzt). Dann
sieht **jedes** Programm (CP/A-BIOS, INIT, künftige OS) automatisch die echte Drehzahl. Vollständiger
Umbauplan mit Phasen/Regressionsschutz: **`doc/plan_k5122_rotation_timing.md`**.

## 8. Werkzeuge

- Disassembly: `tools/z80_disasm2.py --org 0x100 …/init.com`.
- Extrahierte SCPX-Dateien: `…/CPA_Workbench/Disketten/A5120_SCPX_Boot/` (init/stat/pip/… .com,
  bios*.sys, ccpbd17.sys, syl17.sys).
- Formatblöcke/Deskriptoren neu auslesen: kleines Python über die `.com`-Bytes (Basis `0x0100`),
  s. Sitzungs-Scratchpad.
