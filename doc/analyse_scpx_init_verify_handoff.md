# SCPX INIT.COM — Verify-Problem & Read-Pfad-Bug: Handoff für neue Sessions

> **Zweck:** Zwei in sich geschlossene, unabhängig angehbare Aufgaben, jeweils als frische
> Chat-Session startbar. **Teil A** = ein isolierter, echter Read-Pfad-Bug im K5122 (fixbar
> ohne den Rest). **Teil B** = das eigentliche „INIT meldet BAD TRACKS"-Problem samt allem,
> was bereits ausgeschlossen ist. Stand: 2026-07-22, Branch `scpx_boot`, alle Tests grün,
> Working Tree sauber (keine der Fix-Versuche committet).
>
> Kontext & Vorgeschichte: `doc/analyse_scpx_init_format.md` (Dialog, Formatblöcke, Drehzahl-
> Gate), `doc/open_points.md §5`, Memory `project_scpx_init_format`. Commits dieser Arbeit:
> `feaae01` (Drehzahl-Gate gelöst), `939dda5` (HW-treues Kopf-/Lese-Modell, Teilfortschritt),
> `bcffd98`/`56d8e2e`/`1ddc739`/`d62025d` (Handoff-Doku RE-Sessions 1–3).

## Gemeinsame Grundlagen (für beide Teile)

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
  Env-Feature wurde in `tools/k1520dbg.cpp` ergänzt und ist committet). K5122 loggt auf INFO
  `>>> READ …` und `>>> FORMAT-WRITE …`.
- **Retry-Signatur (unstrittig, robust):** `>>> FORMAT-WRITE`-Zähler je `(Zyl,Kopf)`:
  **nur `(gerade Zyl, Kopf 0)` = 1 (Verify OK); alles andere = 5 (Verify 5× fehlgeschlagen →
  Spur bad).** Messen:
  `rg -o "FORMAT-WRITE D0 C=[0-9]+ H=[01]" LOG | sort -t= -k2 -n -k3 | uniq -c`.

---

# TEIL A — Read-Pfad-Bug: `/WE=0`-Verify-Strobe wird als FORMAT fehlklassifiziert

**Das ist ein echter, isolierter Bug.** Er ist **nicht** gefixt (alle Versuche verworfen).
Er allein behebt das INIT-Problem NICHT (s. Teil B), ist aber eine saubere Korrektheits-
Verbesserung und Voraussetzung dafür, dass INITs Kopf-1-Verify überhaupt Daten liest.

## A.1 Symptom

INITs Sektor-Verify-Leseschleife liest für **Kopf 1** über `IN(16H)` den **`0xFF`-PIO-Fallback**
statt echter Spurdaten. Folge: `head_pos_` steht, der MK1-ID→DATA-Resync bleibt auf der IDAM,
INIT liest `FE` statt der Datenmarke `FB` → Spur „bad". (Kopf 0 ist nicht betroffen.)

## A.2 Ursache (exakt, verifiziert)

INITs Verify-Leseschleife nutzt für **Kopf 1** Steuerworte mit **bit0(`/WE`)=0** (z. B. `0xB0`,
`0xB2` — abgeleitet aus Kopf-0-Worten mit gelöschtem Kopf-bit2). Unser K5122 behandelt in
`handleCtrlPortAWrite` **jede `/STR`-Fallflanke mit `/WE`=0** als **Vollspur-FORMAT**:

- `core/cards/k5122/k5122.cpp:581` — `bool is_write = !(data & 0x01);` (`/WE`=0 → „Schreiben").
- `core/cards/k5122/k5122.cpp:626` — `write_mode_ = true; transferring_ = false;` (ZVE1-Kontext-
  Vollspur-FORMAT-Pfad, der „alte synthetische /STR-Schreibpfad").

Danach fällt INITs `IN(16H)` in den Fallback, weil der Streaming-Pfad `transferring_ && !write_mode_`
verlangt:
- `core/cards/k5122/k5122.cpp:65` — `if (port == 0x16 && transferring_ && !write_mode_) { …stream… } else { …0xFF… }`.

**Echte HW (Handbuch `doc/trascripted/Floppy Anschlußsteuerung K 5122.md`):** A0 = `/WE`
(`/WE`=0 = Schreib-Freigabe inkl. Takterzeugung). INITs Verify pulst `/WE`=0 und **re-schreibt
die gerade gelesenen Bytes** (`IN(16H); OUT(14H),A`) = ein **Read-Verify-by-Rewrite**, der die
Daten nicht ändert. Der Datenseparator LIEST dabei unabhängig von `/WE`. Unser synthetischer
Vollspur-Format-Pfad kann das nicht abbilden und rastet stattdessen in den vollen Schreibmodus.

## A.3 Warum es NICHT trivial ist (5 Fehlschläge dokumentiert — nicht wiederholen)

Der Diskriminator „echter FORMAT vs. Verify-Puls" ist unerwartet schwierig:

1. **Gate auf `!transferring_`** → scheitert: `transferring_`=0 am `/STR`-Write-Edge (der Kopf-1-
   Read ist an dieser Stelle noch nicht armiert).
2. **`write_mode_` verzögern bis 1. `OUT(14H)`-Byte** → scheitert: INIT schreibt **ein Setup-
   Byte** auf `OUT(14H)` **vor** dem `IN(16H)` (`0x113D OUT(14H),A; 0x113F IN A,(16H)`), das
   `write_mode_` verfrüht engagiert.
3. **Verzögern + spekulativ Read armieren + „2. `OUT(14H)` ohne `IN(16H)` = Format"** → scheitert:
   die write-idle-Erkennung committet den leeren Puls und löscht `write_mode_`, und/oder der
   spekulative Read wird vom **`/STR`=1-Read-Ende** (`update()`, `core/cards/k5122/k5122.cpp:389`,
   `str_inactive_cycles_ >= strEndSampleCycles()` → `transferring_=false`) zerrissen, weil INIT
   `/STR` kurz wieder hebt.
4. **`ioRead`-Switch bei `write_mode_`** → scheitert: `write_mode_` ist beim `IN(16H)` bereits
   von der write-idle-Erkennung gelöscht (Zustand: `transferring_=0, write_mode_=0`).
5. **`str_write_pending_`-Flag (überlebt write-idle, per `IN(16H)` geschaltet)** → scheitert: INITs
   `OUT(14H)`-**Echo** löscht das Flag (ununterscheidbar von einem echten Format-Byte), bevor das
   `IN(16H)` kommt.

**Lektion:** Nur ein tatsächliches `IN(16H)` unterscheidet Verify von Format zuverlässig
(ein echter FORMAT liest NIE `IN(16H)`). Aber INIT verschränkt `OUT(14H)`(Echo) und `IN(16H)`,
und der `/STR`-Puls + die write-idle- und `/STR`=1-Ende-Mechanik reißen jeden naiv armierten Read ab.

## A.4 Empfohlene Fix-Richtung (zwei Optionen)

**Option 1 — Head-Select korrigieren (die sauberste Teil-Lösung; brachte den Kopf-1-Read
nachweislich zum Streamen):** Das committete Modell (`k5122.cpp:511–514`) setzt den Kopf aus bit2
bei **jedem** Port-A-Schreiben. Das ist falsch: INITs Kopf-1-Verify **alterniert** `0x81`
(Pfad-Byte, Kopf 1) und `0xB5` (Resync-Strobe, bit4/5=MK1/MR, bit2 **inzidentell** 1) — bit2-Pegel
flippt so den Kopf bei jedem `0xB5` auf 0.
- **Fix:** Kopf-Wahl aus bit2 **nur** setzen bei (a) dem Pfad-/Lese-Steuerwort
  (`(data & 0xF9) == 0x81`, `k5122.cpp:544`) und (b) dem **`/STR`-WRITE-Edge** (Format,
  `is_write`, bei `k5122.cpp:581`). NICHT bei jedem Write, NICHT am `/STR`-**READ**-Edge (INITs
  `0xA1`-Read-Strobe hat bit2 inzidentell und darf die vom Format gesetzte Seite nicht
  überschreiben). Ein Helfer `setHead(v)` mit 1-Kopf-Force auf 0.
- **Wirkung (gemessen):** Kopf-1-Verify streamt dann korrekt (`>>> READ … 16 Sekt, 0 CRC`,
  18 Streaming- statt 0). **ABER** behebt INIT NICHT (s. Teil B) und **bricht 3 Unit-Tests**, die
  den `/STR`-**Read**-Edge-Head-Latch testen — diese müssen aufs realistische Pfad-Byte-Modell
  umgestellt werden: `K5122Test.HeadLatch_Bit2_Head0_und_Head1_UnterschiedlicheDaten`,
  `K5122Test.HeadLatch_NurAmSTRGelatcht` (in `tests/cpp/test_k5122.cpp`, Helfer `strobeRead(head)`
  schreibt einen `/STR`-Read-Strobe mit bit2 → auf ein vorangestelltes Pfad-Byte `0x81`/`0x85`
  umstellen), `K2526ZVE2FloppyChain.ZVE2ReadsHead1FieldViaBus` (`tests/cpp/test_k2526.cpp`, nutzt
  `0x85`+`0xF3` /STR head1 → auf `0x81` Pfad-Byte umstellen).

**Option 2 — `/WE`=0-Verify-Strobe als persistenten Read modellieren:** Am `/STR`-Write-Edge
NICHT sofort `write_mode_` setzen und `transferring_` NICHT abreißen, wenn es INITs Verify-Puls
ist; `write_mode_` erst engagieren, wenn ein echter Format-Datenstrom läuft; UND den Read gegen
die `/STR`=1-Ende-Erkennung (`k5122.cpp:389`) schützen, solange INIT mittendrin liest. Das ist
der schwierigere Weg (s. die 5 Fehlschläge). Praktisch am ehesten in Kombination mit Option 1.

**Invarianten, die NICHT brechen dürfen:** der echte Vollspur-FORMAT (CP/A FORMAT.COM,
`tools/dev.sh test-format` = 5/5 `format_integration`) und der Boot nutzen `/STR`+`/WE`=0
**legitim** zum Schreiben — sie dürfen weiter voll funktionieren. Nach jeder Änderung:
`tools/dev.sh test` (592/592 + 58/58) **und** `tools/dev.sh test-format` (5/5).

## A.5 Diagnose-Recipe für Teil A

- Zustand am `/STR`-Write-Edge / beim `IN(16H)` per temporärem `LOG_INFO` in `k5122.cpp`
  (`current_head_`, `transferring_`, `write_mode_`, `we_writing_`).
- OUT(10H)-Sequenz für Kopf 1 (zeigt `0x81`/`0xB5`-Alternation und die `/WE`=0-Strobes).
- ZVE2-Breakpoints an INITs Verify-Innenschleife: `b2 0x1172` (`CP L`, L=`0xFB` DAM-Check),
  `b2 0x1197` (DAM-bad), `b2 0x114E` (erster Marken-Check). `rj2` = ZVE2-Register.

---

# TEIL B — Das INIT-Verify-Problem (BAD TRACKS)

**Das ist das eigentliche Ziel.** Read-Pfad-Fixes allein lösen es NICHT (s.u.).

## B.1 Symptom (exakt)

`BAD TRACKS: {Zyl 0} ∪ {alle ungeraden Zylinder}`; gerade Zyl 2–78 gut. Retry-Signatur:
**nur `(gerade Zyl, Kopf 0)` besteht den Verify**; Kopf 1 (alle Zyl) + Kopf 0 (ungerade Zyl)
scheitern 5× → bad. **Zwei unabhängige Faktoren:** (i) Kopf-1-scheitert-immer, (ii) Kopf-0-
scheitert-auf-ungeraden-Zylindern.

## B.2 Was BEREITS AUSGESCHLOSSEN ist (nicht erneut untersuchen)

1. **Byte-Steal durch ZVE1 (WIDERLEGT).** `IN(16H)` mit ausführender CPU instrumentiert
   (`bus_master_zve2_`): **alle** Verify-Reads sind ZVE2, **null** ZVE1-Reads. Keine konkurrierende
   ZVE1-Lesung, die Bytes „stiehlt".
2. **Read-Decode / gelesene Bytes (AUSGESCHLOSSEN).** Mit dem Head-Select-Fix (Teil A, Option 1)
   streamt der Kopf-1-Verify-Read **korrekt**: `>>> READ D0 C=… H=1 … 16 Sekt, 0 CRC-Fehler`,
   alle IDAM/DATA-Marken an den richtigen Stream-Offsets, ID-Feld liest `cyl/head/sec/size`
   korrekt zurück, Daten = `0xE5`. **Trotzdem lehnt INIT die Spur ab.** ⇒ Die von INIT gelesenen
   Bytes sind richtig; das Problem ist NICHT der Read.
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
1. Head-Select-Fix aus Teil A (Option 1) provisorisch anwenden, **damit der Kopf-1-Read streamt**
   (sonst vergleicht man Äpfel/Birnen). Nur lokal, nicht committen.
2. INIT bis in die Verify-Phase fahren (Recipe oben).
3. ZVE2-Breakpoints setzen: `b2 0x1197` (DAM-bad), `b2 0x119C` (CRC-bad / gemeinsamer Bad-State),
   `b2 0x11A8` (Erfolg). Ermitteln, **welcher** Bad-Zweig für gerade-Kopf1 feuert.
4. An diesem Zweig (und beim bestehenden gerade-Kopf0 zum Vergleich) **ALLES dumpen, was INIT
   vergleicht:**
   - ID-Scratch-Puffer `0x13E4` (`x/6xb 0x13E4` → cyl/head/sec/size/CRC),
   - Daten-CRC `HL` und Soll `DE` an `0x1186` (`rj2`),
   - **physischer Zylinder** (`dev` → K5122 `cur_cyl_`),
   - **`/BUSRQ`-Phase / Byte-Pacing-Zustand** am Verify-Start (`dev` zeigt `/BUSRQ`, headPos).
5. **Frage:** Was unterscheidet den bestehenden gerade-Kopf0-Verify vom scheiternden gerade-Kopf1-
   Verify, **wenn die gelesenen Bytes identisch sind?** Genau dieser Unterschied ist die Ursache.
6. Danach analog für den Zyl-Paritätsfaktor: gerade-Kopf0 (PASS) vs. ungerade-Kopf0 (FAIL) —
   isoliert den **Seek-Faktor** (verschiedene Zylinder, gleicher Kopf). `dev` → `cur_cyl_` prüfen,
   ob der physische Zylinder bei ungeraden Zyl von INITs erwartetem abweicht (Seek-Off-by-one /
   Doppelschritt-Verdacht — INIT-Doppelschritt vs. unser Einzelschritt?).

## B.5 Werkzeuge / Referenzen

- Disassembler: `python3 tools/z80_disasm2.py --org 0x100 <init.com> --entry 0x1150 …`
  (mehrere `--entry`); Symbole `-s tools/scpx1526.sym`.
- Debugger: `tools/how_to_debug_and_trace.md`, `tools/k1520dbg.md` (`b2`/`rj2`/`dev`/`x`/`hist`).
- Delegation: die reine Disassembly der Vergleichs-/Seek-Logik an den `boot-disasm-analyst`.
- Vorarbeit dieser Sessions: `doc/open_points.md §5`, `doc/analyse_scpx_init_format.md`,
  Memory `project_scpx_init_format` (RE-Sessions 1–3 mit allen Zwischenbefunden).
- **Kein Regressions-Guard-Test existiert** (nichts zu schützen, bis es besteht).
