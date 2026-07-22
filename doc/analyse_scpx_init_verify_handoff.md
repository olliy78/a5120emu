# SCPX INIT.COM — Verify-Problem (BAD TRACKS): Handoff für neue Sessions

> **Zweck:** In sich geschlossene Beschreibung des offenen „INIT meldet BAD TRACKS"-Problems
> samt allem, was bereits ausgeschlossen ist, und dem entscheidenden nächsten Experiment.
> Als frische Chat-Session startbar. Stand: 2026-07-22, Branch `scpx_boot`, alle Tests grün.
>
> Kontext & Vorgeschichte: `doc/analyse_scpx_init_format.md` (Dialog, Formatblöcke, Drehzahl-
> Gate), `doc/open_points.md §5`, Memory `project_scpx_init_format`. Commits dieser Arbeit:
> `feaae01` (Drehzahl-Gate gelöst), `939dda5` (HW-treues Kopf-/Lese-Modell, Teilfortschritt).
>
> **Voraussetzung erledigt:** Der isolierte Read-Pfad-/Head-Select-Bug (INITs `/WE`=0-Verify-
> Strobe wurde als Vollspur-FORMAT fehlklassifiziert; das committete „bit2-Pegel bei jedem
> Port-A-Write"-Kopfmodell flippte INITs Kopf-1-Verify) ist **gefixt**: die Seitenwahl wird nur
> noch am Pfad-/Lese-Steuerwort und am `/STR`-Format-Write-Edge aus bit2 gelatcht
> (`K5122::setHead`), der Kopf-1-Verify-Read **streamt jetzt korrekt** (`>>> READ … 16 Sekt,
> 0 CRC`). Guard-Tests: `K5122Test.HeadLatch_*`, `K2526ZVE2FloppyChain.ZVE2ReadsHead1FieldViaBus`.
> Das behebt das BAD-TRACKS-Problem **nicht** (die gelesenen Bytes waren nie der Blocker, s.u.).

## Grundlagen

- **Was ist INIT.COM:** SCPX-Gegenstück zu CP/A FORMAT.COM. Dialoggeführter Formatierer, der
  den **K5122 DIREKT** programmiert (kein BIOS-Call). Datei:
  `~/projects/CPA_Workbench/Disketten/A5120_SCPX_Boot/init.com` (5248 B, CP/M-.COM → geladen ab
  `0x0100`, Adressen absolut). Symbolfile: `tools/scpx1526.sym`.
- **Aktueller Stand:** INIT bootet, misst Drehzahl (Gate gelöst, `feaae01`), formatiert alle 80
  Zylinder bis `FORMATTING COMPLETE`, meldet dann aber
  `BAD TRACKS: 00,01,03,05,07,…,79` (= `{Zyl 0} ∪ {alle ungeraden Zylinder}`; **gerade Zyl
  2–78 sind gut**).
- **Live-Recipe (INIT bis zur Format-Verify-Phase fahren):**
  ```
  K1520DBG_LOGLEVEL=info ./build/k1520dbg disks/scpx_boot.hfe -x <script.dbg>
  ```
  `<script.dbg>`:
  ```
  gu 0xE079            # bis SCPX-Prompt (os_running_)
  keys INIT\r
  g 15000000
  keys A\r             # Laufwerk A:
  g 15000000
  keys \r              # Format-Default (DD-DS 16×256)
  g 8000000
  keys \r              # Disk einlegen
  g 8000000
  keys Y\r             # scratch bestätigen → Formatieren startet
  g 9000000            # in die Verify-Phase
  ...                  # hier Breakpoints/Reads beobachten
  q
  ```
  `K1520DBG_LOGLEVEL=<off|error|warn|info|debug|trace>` hebt den Emulator-Log an (dieses
  Env-Feature ist committet). K5122 loggt auf INFO `>>> READ …` und `>>> FORMAT-WRITE …`.
- **Retry-Signatur (unstrittig, robust):** `>>> FORMAT-WRITE`-Zähler je `(Zyl,Kopf)`:
  **nur `(gerade Zyl, Kopf 0)` = 1 (Verify OK); alles andere = 5 (Verify 5× fehlgeschlagen →
  Spur bad).** Messen:
  `rg -o "FORMAT-WRITE D0 C=[0-9]+ H=[01]" LOG | sort -t= -k2 -n -k3 | uniq -c`.

---

## B.1 Symptom (exakt)

`BAD TRACKS: {Zyl 0} ∪ {alle ungeraden Zylinder}`; gerade Zyl 2–78 gut. Retry-Signatur:
**nur `(gerade Zyl, Kopf 0)` besteht den Verify**; Kopf 1 (alle Zyl) + Kopf 0 (ungerade Zyl)
scheitern 5× → bad. **Zwei unabhängige Faktoren:** (i) Kopf-1-scheitert-immer, (ii) Kopf-0-
scheitert-auf-ungeraden-Zylindern.

## B.2 Was BEREITS AUSGESCHLOSSEN ist (nicht erneut untersuchen)

1. **Byte-Steal durch ZVE1 (WIDERLEGT).** `IN(16H)` mit ausführender CPU instrumentiert
   (`bus_master_zve2_`): **alle** Verify-Reads sind ZVE2, **null** ZVE1-Reads. Keine konkurrierende
   ZVE1-Lesung, die Bytes „stiehlt".
2. **Read-Decode / gelesene Bytes (AUSGESCHLOSSEN).** Mit dem Head-Select-Fix streamt der
   Kopf-1-Verify-Read **korrekt**: `>>> READ D0 C=… H=1 … 16 Sekt, 0 CRC-Fehler`, alle IDAM/DATA-
   Marken an den richtigen Stream-Offsets, ID-Feld liest `cyl/head/sec/size` korrekt zurück,
   Daten = `0xE5`. **Trotzdem lehnt INIT die Spur ab.** ⇒ Die von INIT gelesenen Bytes sind
   richtig; das Problem ist NICHT der Read.
3. **Sektor-Reihenfolge / Interleave (AUSGESCHLOSSEN).** `TrackCodec::buildTrack`
   (`core/peripherals/floppy_drive/track_codec.cpp:114`) iteriert die Sektoren in Eingabe-
   reihenfolge (keine Sortierung); `parseFormatStream` liefert sie in INITs physischer
   Schreibreihenfolge. RD-IDs = FMT-IDs = `1 2 3 … 16` (sequenziell). Kein Interleave-Verlust.
4. **6+ Read-Pfad-/Head-Select-Fixes ändern das Ergebnis NICHT** (identische Retry-Zählung).
   ⇒ Der Fehlschlag ist **read-byte-unabhängig**.
5. **Drehzahl-Gate ist gelöst** (`feaae01`): `[12A6]` liegt im Fenster; das ist NICHT die Ursache
   der BAD TRACKS.

## B.3 Wo die Ursache liegen MUSS

Da die gelesenen Bytes korrekt sind und der Fehlschlag zwei robuste, read-unabhängige Faktoren
hat, sitzt sie in **INITs Verify-VERGLEICHSLOGIK** und/oder einem **Dual-CPU-Pacing-Phase-/Seek**-
Faktor. Kandidaten:

- **Daten-CRC / Prüfsumme** `0x1186 SBC HL,DE` (HL = die 2 Daten-CRC-Bytes, DE = Sollwert,
  `= 0x7827` bei einem bestehenden Sektor) → `JR NZ 0x119C` = bad.
- **DAM-Check** `0x1170/0x1172 CP L` (L=`0xFB`) → `JR NZ 0x1197` = bad.
- **ID-Vergleich** (ID-Feld wird per `INIR` nach `0x13E4` gelesen: `cyl,head,sec,size,+`).
- **Rotative/Pacing-Phase** oder **Seek** (die Even/Odd-Zyl-Parität für Kopf 0 riecht danach:
  gerade Zyl richten sich aus, ungerade nicht). Denkbar: eine per-Track gemessene Zeit, oder der
  physische Zylinder weicht bei ungeraden Zyl ab, oder die ZVE1↔ZVE2-Byte-Pacing-Phase am Verify-
  Start alterniert und nur die phasenrichtigen (gerade-Zyl-Kopf0) bestehen.

INITs Track-Engine ist ein **selbstmodifizierender Coroutine-Dispatcher** über `[0x12A4]`
(State-Pointer, `JP (HL)` bei `0x0F01/0x0F0B/0x0F13`), States `0x0E57`/`0x0F14`(Erfolg)/`0x0FD1`
(Retry+Bad-Liste)/`0x0F2D`(nächste Seite/Zyl). Die Sektor-Verify-Innenschleife: `0x1150–0x1195`
(`INIR` ID → MK1-Resync ID→DATA → DAM-Check `0x1172` → Daten „while `==H`(`0xE5`)"-Loop → CRC in
`HL` → `SBC HL,DE` `0x1186` → Erfolg `0x11A8` / bad `0x119C`).

## B.4 Der ENTSCHEIDENDE nächste Schritt (Experiment)

**Zyklengenauer Entscheidungs-Trace, Seite-an-Seite: gerade-Zyl-Kopf0 (PASS) vs. gerade-Zyl-
Kopf1 (FAIL) — GLEICHER Zylinder.** Das isoliert den **Kopf-Faktor** vom **Seek-Faktor** (gleicher
physischer Zylinder → Seek fällt raus; wenn Kopf1 dann immer noch scheitert und Kopf0 nicht, liegt
es an etwas Kopf-/Phasenabhängigem, NICHT am Zylinder).

Vorgehen:
1. INIT bis in die Verify-Phase fahren (Recipe oben). Der Kopf-1-Read streamt bereits (Fix in place).
2. ZVE2-Breakpoints setzen: `b2 0x1197` (DAM-bad), `b2 0x119C` (CRC-bad / gemeinsamer Bad-State),
   `b2 0x11A8` (Erfolg). Ermitteln, **welcher** Bad-Zweig für gerade-Kopf1 feuert.
3. An diesem Zweig (und beim bestehenden gerade-Kopf0 zum Vergleich) **ALLES dumpen, was INIT
   vergleicht:**
   - ID-Scratch-Puffer `0x13E4` (`x/6xb 0x13E4` → cyl/head/sec/size/CRC),
   - Daten-CRC `HL` und Soll `DE` an `0x1186` (`rj2`),
   - **physischer Zylinder** (`dev` → K5122 `cur_cyl_`),
   - **`/BUSRQ`-Phase / Byte-Pacing-Zustand** am Verify-Start (`dev` zeigt `/BUSRQ`, headPos).
4. **Frage:** Was unterscheidet den bestehenden gerade-Kopf0-Verify vom scheiternden gerade-Kopf1-
   Verify, **wenn die gelesenen Bytes identisch sind?** Genau dieser Unterschied ist die Ursache.
5. Danach analog für den Zyl-Paritätsfaktor: gerade-Kopf0 (PASS) vs. ungerade-Kopf0 (FAIL) —
   isoliert den **Seek-Faktor** (verschiedene Zylinder, gleicher Kopf). `dev` → `cur_cyl_` prüfen,
   ob der physische Zylinder bei ungeraden Zyl von INITs erwartetem abweicht (Seek-Off-by-one /
   Doppelschritt-Verdacht — INIT-Doppelschritt vs. unser Einzelschritt?).

## B.5 Werkzeuge / Referenzen

- Disassembler: `python3 tools/z80_disasm2.py --org 0x100 <init.com> --entry 0x1150 …`
  (mehrere `--entry`); Symbole `-s tools/scpx1526.sym`.
- Debugger: `tools/how_to_debug_and_trace.md`, `tools/k1520dbg.md` (`b2`/`rj2`/`dev`/`x`/`hist`).
- Delegation: die reine Disassembly der Vergleichs-/Seek-Logik an den `boot-disasm-analyst`.
- Vorarbeit dieser Sessions: `doc/open_points.md §5`, `doc/analyse_scpx_init_format.md`,
  Memory `project_scpx_init_format`.
- **Kein Regressions-Guard-Test existiert** (nichts zu schützen, bis es besteht).
