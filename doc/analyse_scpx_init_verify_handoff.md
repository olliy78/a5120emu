# SCPX INIT.COM — Verify-Problem (BAD TRACKS): ✅ GELÖST 2026-07-22

> **STATUS: GELÖST.** INIT.COM formatiert Laufwerk A: (Default DD-DS 16×256) jetzt
> vollständig und meldet **`BAD TRACKS: - NO -`**. Ursache war ein **Index-Puls-Phasen-
> Problem** (NICHT der Read-Pfad, NICHT die Vergleichslogik — s. u.). Fix: `K5122::commitFormatTrack`
> setzt `index_cycle_acc_ = 0` (koppelt die Index-Phase ans Spur-Ende, wie auf echter HW).
> Guard: `ScpxInit.InitFormatsDriveAWithNoBadTracks` (`tests/cpp/test_scpx_init.cpp`, Label
> `format_integration`). 592/592 ctest + 58/58 Legacy + 6/6 format_integration grün (CP/A
> FORMAT.COM unversehrt). Die unten stehende Analyse ist als **Fallstudie** aufbewahrt.
>
> ## Root Cause & Fix (Kurzfassung)
>
> INITs Per-Spur-Verify synchronisiert über das **Index-Interrupt-Flag `[0x12A8]`**:
> - Index-ISR (`0x124D`, läuft auf **ZVE1**) setzt `[0x12A8]=0xFF` bei jedem Disketten-Index.
> - Pro Spur löscht ZVE1 das Flag (`0x0EF0`, `LD (12A8H),0`), dann verlangt ZVE2 am Spur-Beginn
>   `[0x12A8]` **clear** (`0x1115 BIT 0,(HL); 0x1119 JR NZ,L1197` = bad) und wartet anschließend
>   auf den Index (`0x111B`, bit0 muss gesetzt werden).
>
> Der Index-Puls lief **frei** (`index_cycle_acc_`, Periode 490000) relativ zum Byte-Takt.
> Das Fenster **ZVE1-Clear (`0x0EF0`) → ZVE2-Check (`0x1115`)** ist ≈ eine Index-Periode lang,
> sodass pro Spur genau ein Index **ins Fenster** fiel → `[0x12A8]` gesetzt → 0x1119 = bad.
> Da die Phase stabil war, scheiterten alle Spuren außer der ersten deterministisch (Kopf 1 /
> ungerade Zylinder). Auf echter HW endet der Vollspur-FORMAT-Write **genau am Index** (Schreiben
> von Index zu Index = 1 Umdrehung), sodass der Clear direkt hinter einem Index liegt und der
> nächste Index erst in die 0x111B-Warteschleife fällt. Der Fix koppelt die Index-Phase daran:
> `commitFormatTrack()` (Format-Write-Ende) setzt `index_cycle_acc_ = 0`. (Restliche Erst-Versuch-
> Fehlschläge werden von INITs 5-fach-Retry abgefangen → am Ende keine Bad-Tracks.)
>
> ---
>
> ## Historische Analyse (Fallstudie — Read-Pfad/Vergleichslogik waren NICHT die Ursache)
>
> Kontext & Vorgeschichte: `doc/analyse_scpx_init_format.md` (Dialog, Formatblöcke, Drehzahl-
> Gate), `doc/open_points.md §5`, Memory `project_scpx_init_format`. Commits:
> `feaae01` (Drehzahl-Gate), `939dda5` (HW-treues Kopf-/Lese-Modell), `f96ea01` (Head-Latch).
>
> **Read-Pfad/Head-Select war ein Nebenschauplatz** (INITs `/WE`=0-Verify-Strobe wurde als
> Vollspur-FORMAT fehlklassifiziert; das „bit2-Pegel bei jedem Port-A-Write"-Kopfmodell flippte
> INITs Kopf-1-Verify): gefixt in `f96ea01` (`K5122::setHead` latcht nur am Pfad-/Lese-Steuerwort
> und `/STR`-Format-Write-Edge). Der Kopf-1-Verify-Read streamt seitdem korrekt (`16 Sekt, 0 CRC`),
> was aber die BAD TRACKS **nicht** behob — weil die gelesenen Bytes nie der Blocker waren (der
> Fehlschlag kam VOR dem Read, am Index-Flag-Check 0x1119).

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
