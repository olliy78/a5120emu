# Feature-Requests: Debug-/Trace-Tools für Dual-CPU-/Floppy-Analysen

**Anlass:** Die Diagnose des SCPX-`.COM`-Lade-Bugs (`doc/analyse_scpx_com_load.md`) war
unverhältnismäßig aufwändig — viele volle Boot-Läufe (~13 M Takte), `trace`+`grep`-Schleifen und
manuelles Registerraten. Dieses Dokument sammelt Tool-Erweiterungen, die solche Arbeiten
drastisch verkürzen. Es ist nach **Nutzen/Aufwand** priorisiert.

> **Wichtigste Erkenntnis zuerst:** Ein großer Teil des Aufwands war **kein** fehlendes Feature,
> sondern **fehlende Auffindbarkeit** vorhandener Befehle. Konkret in dieser Session verschenkt:
> - Der Matcher lief auf **ZVE2**, ich setzte aber `b 0xEA12` (= nur **ZVE1**) → Breakpoint schlug
>   nie an → Ausweichen auf `trace`+`grep` über 12 000 Zeilen. **`b2 0xEA12`** hätte direkt gehalten.
> - Drei einzelne `wp 0xEC0C` / `wp 0xEC0D` / `wp 0xEC0E` statt des vorhandenen **Range-Watch
>   `wp 0xEC0C..0xEC0E`** (kann sogar `==v` / `!=v` / `changed`).
> - Register von ZVE2 mühsam aus Trace-Zeilen gelesen statt **`r 2`**.
>
> ⇒ **Billigste Sofortmaßnahme (§0):** eine knappe „Dual-CPU-/Floppy-Read-Recipe" in
> `tools/how_to_debug_and_trace.md` + ein maschinenlesbarer Cheat-Sheet-Block, der genau diese
> Fälle abdeckt. Kein Code, größter Hebel.

---

## §0 Discoverability / Workflow (höchster ROI, minimaler Aufwand)

1. **„Multi-CPU-Read-Recipe" dokumentieren.** Ein Kochrezept für „ein BIOS-Read hängt/scheitert":
   `b2 <matcher>` statt `b`, `r 2` für ZVE2, `wp <lo>..<hi> changed`, `dev` (Kopf/Transfer),
   `d 0x0000 8` (Koroutinen-/Warmstart-Vektor). Genau die Kette, die hier 8+ Läufe gebraucht hat.
2. **`b`/`bd`/`s`/`r`/`rj` bei Bus-Master-ZVE2 hinweisen.** Wenn `busMasterIsZVE2()` gerade gilt und
   der Nutzer `b`/`s`/`rj` (ZVE1) absetzt, eine einzeilige Notiz ausgeben: „Bus-Master ist ZVE2 —
   evtl. `b2`/`s2`/`r 2` gemeint?". Verhindert genau den Fehlgriff dieser Session.
3. **`rj2` ergänzen (ZVE2 als JSON).** `rj` gibt nur ZVE1. Für Skript-/Agent-Auswertung fehlt das
   ZVE2-Pendant (`r 2` ist nur menschenlesbar). Trivial, hoher Nutzen für automatisierte Läufe.
4. **`help` nach Themen gruppieren + Beispiele.** Die Existenz von `b2`, `wp A..B ==v`, `iow/iob`,
   `bint/bnmi/breti` war nicht präsent. Ein `help floppy` / `help dualcpu` mit 2–3 Beispielen je
   Thema hätte die halbe Analyse erspart.

## §1 Strukturiertes K5122-Read-Attempt-Logging (**größter Einzel-Hebel**)

**Problem:** Um herauszufinden, *warum* ein Read scheitert, musste ich den Matcher-Disassembler
(E9E6–EA1A) von Hand lesen, `DE`/`SP`/Template mitverfolgen und den Stream-Bytestrom mental
demodulieren — über mehrere Läufe. Die Karte *weiß* aber genau, was passiert.

**Vorschlag:** Ein einschaltbares, strukturiertes Read-Log in `K5122` (Gate wie
`K1520_LOG_BOOST`), das pro **IDAM-Präsentation** eine Zeile liefert:
```
K5122 READ D0 cyl=4 head=1  IDAM(c=4,h=1,s=9,n=1 crc=OK)  target(c=4,h=1,s=9)  MATCH
K5122 READ D0 cyl=4 head=1  IDAM(c=4,h=1,s=3,n=1 crc=OK)  target(c=4,h=1,s=9)  skip(sec)
```
Also: welche Adressmarken unter dem Kopf vorbeikommen, welches Feld beim Soll-Ist-Vergleich
abweicht (cyl/head/sec/size/CRC), plus CRC-gültig-ja/nein. **Das hätte in EINEM Lauf gezeigt:**
Kopf korrekt, Sektoren gültig, Matcher findet aber keine Marke → der Fehler liegt im
CPU-Kontext, nicht im Medium. Aufwand mittel; erspart die komplette Matcher-Handdekodierung.

## §2 Verlässlicher Savestate-am-Prompt (**größter Zeit-Hebel**)

**Problem:** `doc/analyse_scpx_com_load.md` §1 warnt: `loadstate` reproduziert den Bug **nicht treu**
(der Zustand bei `E079` führt zu anderem Read-Verlauf) → **jeder** Versuch braucht den vollen
~13 M-Takte-Boot (Minuten). Bei ~10 Iterationen ist das der Löwenanteil der Kosten.

**Vorschlag:** Untersuchen und beheben, **warum** loadstate divergiert — vermutlich fehlt
Timing-/Peripherie-Zustand (CTC-Phasen, Index-Zähler, `byte_acc_`/`str_inactive_cycles_` der K5122,
laufender Streaming-Transfer, ZVE2-Reset/Wait-Latches). Ziel: ein Savestate direkt am Prompt, aus
dem `keys STAT\r` denselben Bug erzeugt. **Ein zuverlässiger Prompt-Snapshot senkt die
Iterationskosten um ~10×.** Aufwand mittel–hoch, aber zahlt sich bei jeder künftigen Laufzeit-Analyse aus.

## §3 Trace mit Schleifen-Kollaps / State-Dedup / trace-until-change

**Problem:** Der Matcher loopt millionenfach identisch. `trace E9E6 EA1A ; g 30000` erzeugte 12 158
Zeilen, davon 99 % Wiederholung. Das Finden der einen relevanten Zustandsänderung ist teuer.

**Vorschlag(e):**
- **Loop-Collapse:** identische aufeinanderfolgende Blöcke zu `… ×N` zusammenfassen (`trace … --fold`).
- **State-Dedup:** nur Zeilen schreiben, in denen sich Register/relevante Speicherzellen ggü. dem
  letzten Besuch **derselben PC** ändern (`trace … --on-change`).
- **`trace --distinct <N>`:** stoppt nach N *verschiedenen* Zuständen statt nach N Instruktionen.
Damit wird aus 12 000 Zeilen eine Handvoll — die tatsächlich informativen Übergänge.

## §4 Dual-CPU-Statuszeile / `where`

**Problem:** „Wo stehen beide CPUs gerade?" brauchte `dev` + `rj` + Trace-Lesen. Der eine Satz
„ZVE1 pollt E8B5, ZVE2 loopt Matcher E9E6–E9FB, DE=E8C1, SP=EC1D" war die Kernerkenntnis.

**Vorschlag:** Ein `w`/`where`-Befehl, der in **einer** Ausgabe zeigt: ZVE1 PC+1-Zeilen-Disasm,
ZVE2 PC+1-Zeilen-Disasm, BUSRQ, K5122-`dev`-Kurzform (Kopf-cyl/head, transferring, headPos), und ob
gerade ein Loop erkannt wurde. Ideal auch als `--json` für Agenten.

## §5 Event-/Hardware-Breakpoints für den Floppy-Pfad

**Problem:** Ich wollte „halte, wenn ein Read-Transfer startet" bzw. „wenn `[0x0000]` geschrieben
wird" — Ersteres gibt es nicht, Zweiteres ging nur über `wp`.

**Vorschlag:** Breakpoints auf Bus-/Karten-Ereignisse: `bbusrq` (assert/release), `bstr` (/STR-Edge),
`bxfer` (K5122 read/write transfer start/end), sowie `b`-on-mem-write-**mit-Wert** (`bw <A>==<v>`).
Baut auf den vorhandenen Callbacks auf; macht „genau im interessanten Moment anhalten" ohne
Cycle-Raterei möglich.

## §6 Non-destruktiver / Copy-on-Write-Disk-Mount

**Problem:** Beide Tools mounten die Disk **schreibend** → jeder Lauf braucht eine `cp` der Fixture
(sonst Korruption). Das ist Boilerplate in jedem Skript und eine Fehlerquelle (vergessene Kopie →
zerstörte Fixture).

**Vorschlag:** `--read-only` (Writes verwerfen) bzw. Copy-on-Write-Mount als **Default** für
`k1520dbg`/`boot_trace`. Dann entfällt das `mktemp`-Ritual und die Fixture ist strukturell sicher.

## §7 „Boot-to-Prompt"-Makro + validierter Boot-Cache

**Problem:** In jedem Skript stand `b 0xE079 ; g ; bd 0xE079`. Und der Boot ist bei identischer
Disk deterministisch — wird aber jedes Mal neu gefahren.

**Vorschlag:** `--until-prompt` (läuft bis zum ersten CONIN-Wartepunkt, dann REPL) plus ein
**disk-hash-gekeyter Boot-State-Cache** (`--boot-cache`): einmal booten, Snapshot ablegen,
Folgeläufe laden. Setzt §2 (treuer Savestate) voraus, multipliziert dessen Nutzen.

## §8 Symbol-/Label-Datei für das SCPX-BIOS (analog `.prn`)

**Problem:** `E9C8`, `E8B5`, `EC0D`, `DE03` musste ich von Hand als „Matcher-Setup",
„Poll-Wait", „Mini-Stack", „Warmstart" führen. Das `.prn`-Listing-Feature deckt CP/A-BIOS ab,
für SCPX gibt es keine Symbole.

**Vorschlag:** Eine ladbare Symboltabelle (`sym <file>` / `sym add <name> <addr>` gibt es bereits —
nur fehlt die SCPX-Datei) mit den in `doc/analyse_scpx_com_load.md` §7 gesammelten Adressen. Dann bekommen Disasm,
Trace und PC-Histogramme automatisch `matcher_setup`/`poll_wait`/`ministack`-Labels.

## §9 PC-Histogramm/Hotspot in `k1520dbg` (nicht nur `boot_trace`)

**Problem:** „Wo hängt ZVE1/ZVE2 fest?" beantwortete ich per Trace-Lesen. `boot_trace` hat
PC-Histogramme, `k1520dbg` nicht.

**Vorschlag:** `hist <cycles> [lo hi]` — läuft N Takte, zählt PCs beider CPUs, gibt Top-Adressen +
Labels aus. Ein Blick statt eines Trace-Files. Zeigt sofort „ZVE1 zu 100 % in E8B5-Schleife".

## §10 Build-Footgun: `build_trace/CMakeCache.txt` ist eingecheckt

**Problem:** `boot_trace` ließ sich im Worktree **nicht** bauen — `build_trace/CMakeCache.txt` ist
mit **absolutem Pfad** committet (`.../a5120emu/build_trace`), der im Worktree
(`.../.claude/worktrees/…`) nicht stimmt → CMake bricht ab. Ich musste auf `k1520dbg` (build/)
ausweichen.

**Vorschlag:** `build/` **und** `build_trace/` in `.gitignore` aufnehmen und aus der
Versionierung entfernen; `tools/dev.sh` konfiguriert sie ohnehin frisch. Beseitigt einen stillen
Blocker für jede Arbeit in Worktrees (u. a. Background-Jobs/parallele Agenten).

---

## Priorisierung (Nutzen ÷ Aufwand)

| Prio | Item | Aufwand | Wirkung in dieser Session |
|------|------|---------|---------------------------|
| ★★★ | §0 Discoverability/Recipe | sehr klein | hätte ~50 % der Läufe erspart |
| ★★★ | §10 build_trace-Cache entcommitten | trivial | stiller Worktree-Blocker weg |
| ★★★ | §1 K5122-Read-Attempt-Log | mittel | „warum scheitert der Read" in 1 Lauf |
| ★★★ | §2 treuer Savestate-am-Prompt | mittel–hoch | ~10× schnellere Iteration |
| ★★ | §3 Trace-Fold/Dedup | mittel | 12 000 → ~10 relevante Zeilen |
| ★★ | §4 Dual-CPU `where` | klein | Kernbefund auf einen Blick |
| ★★ | §6 Read-only/COW-Mount | klein | kein `cp`-Ritual, Fixture sicher |
| ★★ | §5 Floppy-Event-Breakpoints | mittel | „im richtigen Moment halten" |
| ★ | §7 Boot-to-Prompt + Cache | mittel | baut auf §2 |
| ★ | §8 SCPX-Symbole | klein | selbst-annotierte Traces |
| ★ | §9 `hist` in k1520dbg | klein | Hotspot statt Trace-Lesen |

**Empfehlung:** Mit §0 + §10 (beide fast kostenlos) starten, dann §1 und §2 — diese drei zusammen
hätten die SCPX-Analyse von ~15 Werkzeug-Läufen auf eine Handvoll reduziert.

---

## Umsetzungsstand (2026-07-10)

| Item | Status | Umsetzung |
|------|--------|-----------|
| §0 Discoverability | ✅ | `help floppy`/`help dualcpu`, `rj2`, Bus-Master-`[hint]` bei ZVE1-Kommando während ZVE2 Bus-Master ist, Cheat-Sheet in `how_to_debug_and_trace.md` §0b |
| §1 K5122-Read-Attempt-Log | ✅ | `K5122::startReadTransfer`: `--log-level info` → `>>> READ … N Sekt, M CRC-Fehler`; `--log-level debug` → `RD-ID[i] cyl/head/sec/size/id_crc/data_crc`. Die „Soll-Ist"-Hälfte (was das OS sucht) bleibt CPU-seitig (ZVE2) — dafür `b2 <matcher>`/`hist` |
| §3 Trace-Fold | ✅ (Teil) | `boot_trace --fold` kollabiert **identische aufeinanderfolgende** `-w`/`-z`-Zeilen (Idle-/Poll-Spins). `--on-change`/`--distinct` NICHT umgesetzt — für registerändernde Hot-Loops decken `hist` (k1520dbg) und `--coverage` denselben Bedarf ab |
| §4 Dual-CPU `where` | ✅ | `k1520dbg where`/`w` (+`--json`): beide PCs+Disasm, `/BUSRQ`, Bus-Master, K5122-Kopf/Transfer |
| §5 Floppy-Event-BPs | ✅ (Teil) | `bbusrq` (/BUSRQ-Flanke), `bxfer` (K5122-Read-Transfer-Flanke), je Instruktion gepollt. `bstr` (/STR-Momentanflanke) NICHT — nicht pollbar; `bw A==v` deckt bereits `wb A == v` ab |
| §6 Read-only/COW-Mount | ✅ | **COW als Default** in `k1520dbg` und `boot_trace` (Temp-Kopie, Writes verworfen, Auto-Cleanup); `--rw`/`--read-only`/`--cow` |
| §8 SCPX-Symbole | ✅ | `tools/scpx1526.sym` (aus §7), via `-s` ladbar |
| §9 `hist` in k1520dbg | ✅ | `hist <cyc> [lo hi]`: PC-Hotspots beider CPUs mit Symbol/`.prn` |
| §10 build_trace-Cache | ✅ | `build_trace/CMakeCache.txt` + `cmake.check_cache` aus der Versionierung entfernt (waren bereits in `.gitignore`) |
| **§2 Treuer Savestate-am-Prompt** | ⏸ **zurückgestellt** | **Forschungsintensiv, kein umrissener Fix.** Die Divergenz-Ursache (`loadstate` reproduziert den SCPX-Read-Bug nicht) ist selbst offen (`analyse_scpx_com_load.md` §1) und hängt mit dem ungelösten Per-Byte-/BUSRQ-Dual-CPU-Modell (Schicht 3, §9.3) zusammen. Ohne byte-genaues HW-Transfer-Ende-Signal ist der Prompt-Zustand nicht deterministisch serialisierbar — ein Fix hier ist ein eigenes Projekt, nicht eine Tool-Erweiterung. Als schnellerer Ersatz nutzbar: `boot_trace --save-state`/`k1520dbg savestate` mit dem Wissen um die bekannte Divergenz. |
| **§7 Boot-to-Prompt + Cache** | ⏸ **zurückgestellt** | Baut laut Doku selbst auf §2 (treuer Savestate) auf; ohne §2 kein verlässlicher Cache. Zurückgestellt zusammen mit §2. |

Getestet: 590 ctest + 1 disabled + 58 Legacy-Harness grün, keine Regression. Doku aktualisiert:
`how_to_debug_and_trace.md` (§0b-Rezept + COW), `k1520dbg.md`, `boot_trace.md`.

---

## Runde 2 (2026-07-10) — weitere Ergänzungen (aus der Umsetzungs-Erfahrung)

Beim Umsetzen von Runde 1 sichtbar gewordene Lücken, wieder nach Bezug zur SCPX-Bug-Klasse
(Dual-CPU-Floppy + Interrupt-Timing) und Nutzen/Aufwand priorisiert.

### §11 Interrupt-Trace (`itrace <file>` · `boot_trace --itrace`) — höchster Bezug
Der ungelöste SCPX-`.COM`-Bug **ist** ein Interrupt-Timing-Problem (ein CTC-Interrupt pusht die
Matcher-PC auf den EC0D-Mini-Stack und korrumpiert das CRC-Template, `doc/analyse_scpx_com_load.md`
§5); auch der Uhr-Bug war CTC-INT. `bint` *hält nur an*. Was fehlt: ein **nicht-anhaltendes Log
jeder angenommenen INT/NMI** — Zyklus, unterbrochene PC, ISR-Vektor(-PC), SP —, das man gegen ein
Matcher-Fenster korreliert. Baut auf `eventbp::classify` (schon für `bint` da) → **kein Core-Eingriff**.

### §12 Echter Schleifen-Kollaps / Perioden-Erkennung (`boot_trace --fold` erweitern) — ehrlicher §3-Abschluss
Das umgesetzte `--fold` kollabiert nur **identische** Zeilen; die realen Hot-Loops (IDAM-Matcher,
Delay-Counter) sind *registerändernd* und bleiben stehen — exakt der 12 000-Zeilen-Fall, den §3
wollte. **Online-Perioden-Erkennung** (erkennt einen sich wiederholenden **PC-Zyklus** der Länge
p≤~32 → „loop @LO..HI ×N Iterationen") zerschlägt auch diese. Aufwand mittel.

### §13 Eingebauter Disk-Integritäts-Check (`disk verify` in k1520dbg) — ersetzt Wegwerf-Harness
Für „ist das Medium gut?" wurde in der SCPX-Analyse eine Einmal-Harness (`scratchpad/scpx_dump.cpp`)
geschrieben. Ein `disk verify [B]`-Kommando fährt `DiskImage::open`+`readTrack`+`TrackCodec::parseTrack`
über **alle** Spuren und meldet je Spur Sektorzahl/IDs + CRC-Health (und eine Gesamt-Zusammenfassung).
Beantwortet die Medienfrage dauerhaft in einem Befehl. Aufwand klein.

### §14 Snapshot-Diff (`snap diff <a> <b>`) — nagelt „ZVE1 überschrieb [0000]" fest
Die Reverse-Debugging-Infrastruktur hält volle `MachineSnapshot` (RAM+beide Z80). Ein Diff zweier
benannter Snapshots (welche **Register** + welche **RAM-Zellen** sich änderten, kompakt als Ranges)
macht „was hat sich zwischen zwei Stopps geändert?" trivial — Kern der Schicht-3-Analyse. Aufwand klein.

### §15 `bxfer` auch für Schreib-Transfers + bedingte Event-BPs
Das umgesetzte `bxfer` deckt nur den **Lese**-Transfer (`transferring_`). `write_mode_` (Schreib-
Transfer) fehlt — trivial nachzuziehen (`bxfer write`). Optional: `bbusrq`/`bxfer if <cond>`.

### §16 Ladbares Handshake-Dashboard (`vars -f <datei>` / `vars add`)
Das hartkodierte `vars` (CP/A-DPB-Felder) auf eine **ladbare Watch-Set-Datei** verallgemeinern
(`name addr [w]` je Zeile), sodass man pro OS ein Set hinterlegt (SCPX: `[EC0B]`,`[0000]`,`EBFA`,Kopf).
Aufwand klein.

### §17 `reverse-continue` (`rc`)
Statt N Snapshots einzeln zurückzusteppen: bis zum **vorigen Breakpoint-Treffer** (oder einer PC-
Bedingung) rückwärts über den Snapshot-Ring. Aufwand klein.

### §18 Kommentiertes SCPX-BIOS-`.prn` (Lesepfad) — an Disasm-Agent delegiert
Analog `doc/EPROMS/zre.prn`: ein via `-l` ladbares, kommentiertes MACRO-80-Listing des SCPX-Lesepfads
(`E6E2`–`EA1A`, Seek/Matcher/CRC), damit Trace/Disasm inline die Original-Semantik zeigen. Reines
Reverse-Engineering → an den `boot-disasm-analyst`-Subagenten ausgelagert (kein Tool-Build betroffen).

**Umsetzungsreihenfolge:** §13,§14,§15,§16,§17 (klein) → §11,§12 (mittel) → §18 (Agent, parallel).

### Umsetzungsstand Runde 2 (2026-07-10)

| Item | Status | Umsetzung / Verifikation |
|------|--------|--------------------------|
| §11 Interrupt-Trace | ✅ | `k1520dbg itrace <file>` + `boot_trace --itrace <file>` (CSV). Verifiziert: fängt die Index-Puls-ISR `int@0137→ISR 01C7` alle ~490k Takte. Reuse `eventbp::classify`, kein Core-Eingriff. |
| §12 Echter Loop-Collapse | ✅ | `boot_trace --fold` erkennt jetzt PC-**Zyklen** (Periode ≤32, nur PC) → `↻ loop @A period=P ×N`. Verifiziert: registerändernder EC42-Delay-Loop **6000 → 19 Zeilen**. |
| §13 disk verify | ✅ | `k1520dbg disk verify [B]`: 164 Spuren/2560 Sektoren/0 CRC-Fehler auf `scpx_boot.hfe`; meldet ehrlich die 4 markenlosen 82-Spur-Padding-Spuren. |
| §14 snap diff | ✅ | `snap diff <a> <b>`: geänderte Register (beide CPUs) + RAM-Bereiche. Verifiziert (2041 B in 8 Bereichen). |
| §15 bxfer write + cond | ✅ | `bxfer [read\|write] … [if <cond>]`, `bbusrq … [if <cond>]`. Verifiziert (`bxfer write start`, `bbusrq if [0xEBFA]==4`). |
| §16 vars -f | ✅ | `vars -f <datei>` / `vars add` / `vars clear` (name/addr/[w], Symbole erlaubt). Verifiziert. |
| §17 reverse-continue | ✅ | `rc` springt zum vorigen Breakpoint-Treffer (eigener BP-Treffer-Ring in `onStop`, da der Snapshot-Ring Post-Instruktions-PCs hält). Verifiziert. |
| §18 SCPX-Lesepfad-`.prn` | ✅ | `doc/EPROMS/scpx_readpath.prn` (157 Zeilen, 35 kommentiert; Matcher E9E6–EA1A voll annotiert inkl. Schicht-1/2-Fehlerstellen) + Generator `tools/gen_scpx_readpath_prn.py`. Verifiziert: lädt via `lst`, annotiert Disasm. (Der delegierte Agent war nur langsam, nicht abgestürzt; selbst fertiggestellt.) |

Getestet: volle ctest+Legacy-Harness grün (s. Testlauf). Doku: `how_to_debug_and_trace.md` §0b (erweitert),
`k1520dbg.md`, `boot_trace.md`.
