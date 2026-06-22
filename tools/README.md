# a5120emu – Werkzeugkasten (tools/)

Hilfsprogramme für Analyse, Disassemblierung, Boot-Tracing und interaktives Debuggen
der K1520-Emulation. Dieser Werkzeugkasten wächst iterativ: Wenn bei einer Analyse
eine Funktion fehlt oder falsch ist, wird sie hier ergänzt/gefixt und unter
„Bekannte Lücken" notiert.

## Überblick — welches Werkzeug wofür

| Werkzeug | Zweck | Detail-Doku |
|----------|-------|-------------|
| **HowTo (Einstieg)** | praxisnahe Anleitung: welches Werkzeug wann, mit Szenarien (Boot-Hang, Programm sezieren, Interrupt-/Uhr-Analyse, Coverage/Diff, Save-State, KI-Agent) | **[how_to_debug_and_trace.md](how_to_debug_and_trace.md)** |
| **`k1520dbg`** | interaktiver gdb-artiger Debugger (ZVE1 **und** ZVE2): Breakpoints (bedingt/Event/ignore), Step into/over/out, **Reverse-Step + Snapshots + Save-State**, Watch mem/io, **Logpoints + Trace-to-File**, `x`-Examine, `.prn`-Annotation + Label-Import, Chip-State (`dev ctc/pio/sio`), JSON-Register, **exakter History-Backtrace** | **[k1520dbg.md](k1520dbg.md)** |
| **`boot_trace`** | nicht-interaktiver Boot-/DMA-Tracer: Report (Histogramme, Done-Flag, VRAM-Banner), `--until`, `--coverage`/`--diff`, `--csv`, `--save-state`/`--load-state`, `--json`/`--quiet`, `.prn`-Annotation | **[boot_trace.md](boot_trace.md)** |
| **`z80_disasm2.py`** | generischer, vollständiger Z80-Disassembler für Listings (kanonisch) | **[z80_disasm.md](z80_disasm.md)** |
| `z80dis_min.h` | eingebauter Ein-Instruktions-Decoder (C++) für `k1520dbg` & `boot_trace` | [z80_disasm.md](z80_disasm.md) |
| `prn_listing.h` | header-only Parser für MACRO-80-`.prn`-Listings (Adresse → kommentierte Quelle); von `k1520dbg` & `boot_trace` per `-l` genutzt | [k1520dbg.md](k1520dbg.md) §6 |
| `callstack_tracker.h` | header-only exakter CALL/RST/RET-Aufrufstapel für den History-`bt` von `k1520dbg` | [k1520dbg.md](k1520dbg.md) §7 |
| `expr_eval.h` | header-only Ausdrucks-Evaluator (Arithmetik/Bit/Vergleiche/`[expr]`) für `k1520dbg` (`if`/`disp`/`x`/`logpoint`) | [k1520dbg.md](k1520dbg.md) §3 |
| `event_bp.h` · `mem_watch.h` | header-only Event-BP-Klassifikation (Interrupt/NMI/RETI) bzw. Watchpoint-Matching für `k1520dbg` | [k1520dbg.md](k1520dbg.md) §4 |
| `dbg_commands.h` | header-only Kommandoliste + Präfix-Matcher für die `k1520dbg`-Tab-Completion (readline) | [k1520dbg.md](k1520dbg.md) §1 |
| `coverage_diff.h` | header-only Parser+Diff (+Range-Collapse) für `boot_trace --coverage`/`--diff` | [boot_trace.md](boot_trace.md) §4 |
| `until_cond.h` | header-only `--until`-Bedingung (Parser + Auswertung) für `boot_trace` | [boot_trace.md](boot_trace.md) §3 |
| `disasm_difftest.py` | Regressionswächter: `z80_disasm2.py` gegen `z80dis` | [z80_disasm.md](z80_disasm.md) |
| `kbd_test` | Tastatur-/Boot-Smoke-Test (boot, tippe Befehl, dump Screen) | unten |
| `eprom_to_h.py` | EPROM-Binär → committetes C-Array (`*_data.h`) | — |
| `img_to_hfe.py` | Roh-`.img` → HFE-v1-Diskettenabbild | — |

**Einstieg & Reihenfolge.** Task-orientierte Anleitung mit Szenarien:
**[how_to_debug_and_trace.md](how_to_debug_and_trace.md)**. Faustregel: mit `boot_trace` die
grobe Phase / den Hang-Punkt **lokalisieren**, dann mit `k1520dbg` (Breakpoint + `mark` +
Step, oder `b … if`/`bint`/`dev ctc`) **sezieren**.

**Korrekt & effizient (gilt auch für KI-Agenten):**

- ⚠️ **Beide Tools mounten die Disk schreibbar — ein Lauf kann das Image KORRUMPIEREN.**
  Immer gegen eine **Temp-Kopie** laufen, nie gegen ein committetes Fixture:
  `D=$(mktemp --suffix=.img); cp DISK $D;  boot_trace … $D;  rm -f $D`.
- **Aufruf über `tools/dev.sh`** (baut zuerst → nie ein stale Binary):
  `tools/dev.sh trace <args>` (boot_trace) bzw. `tools/dev.sh tool k1520dbg <args>`.
- **boot_trace token-sparsam:** `-L /dev/null` verwirft den Emulator-Log; `--quiet --json`
  liefert genau **eine** maschinenlesbare Ergebniszeile (statt ~880) + sinnvollen Exit-Code
  (`--until`: 0 erreicht / 2 nicht). Statt Zyklen raten: **`--until <cond>`**.
- **k1520dbg im Batch:** per Pipe (`printf 'b 0x0437\ng\nrj\nq\n' | k1520dbg $D`) oder
  `-x skript.dbg`; `rj` druckt Register als JSON. REPL/readline ist für Menschen.
- **Einmal booten, oft fortsetzen:** `--save-state`/`--load-state` (boot_trace) bzw.
  `savestate`/`loadstate` (k1520dbg) sichern RAM+CPU+ROM-Mapping in eine Datei → der ~2-s-Boot
  ist eine Einmal-Investition. `-l <bios.prn>` zeigt kommentierten Quelltext statt rohem Disasm.

---

## k1520dbg — interaktiver Debugger

Vollständiges Handbuch: **[k1520dbg.md](k1520dbg.md)**. Kurz:

```sh
cmake --build build --target k1520dbg -j
./build/k1520dbg disks/cpadisk_autofs_clock_noautoexec.img                 # interaktiv
printf 'b 0xC7A3\ng\nr\nq\n' | ./build/k1520dbg disks/cpadisk_autofs_clock_noautoexec.img   # pipe
./build/k1520dbg disks/cpadisk_autofs_noclk_noautoexec.hfe -x skript.dbg -s symbole.sym
./build/k1520dbg -l ~/projects/CPA_Workbench/build/bios.prn disks/cpadisk_autofs_clock_noautoexec.img  # Disasm mit Quelltext
printf 'g 5000000\nbt\nsnap A\ns 3\nrs\nrestore A\nq\n' | ./build/k1520dbg disks/cpadisk_autofs_clock_noautoexec.img  # bt/snap/reverse
```

Kann **beide CPUs** debuggen (ZVE1 ohne Suffix, ZVE2 mit `2`: `b2`, `s2`, `set 2`),
bedingte Breakpoints (`b A if (HL&0xFF)==0xF7`, voller Ausdrucks-Evaluator),
Ereignis-Breakpoints (`bint`/`bnmi`/`breti`), Step into/over/out (`s`/`n`/`fin`),
**Reverse-Step + Snapshots + Save-State** (`rs`/`snap`/`savestate`), Watchpoints mit
Bereich/Wert-Bedingung (`wp 0x6000..0x60FF changed`), Logpoints/Trace-to-File,
`x`-Examine (gdb-Formate), Chip-Zustand (`dev ctc/pio/sio`), exakter History-`bt`,
`.prn`-Quelltext-Annotation + Label-Import, JSON-Register (`rj`). `help` listet alles.

## boot_trace — Boot-/DMA-Tracer

Vollständige Doku: **[boot_trace.md](boot_trace.md)**. Kurz (Build über `build_trace`
mit `LOG_LEVEL=5`):

```sh
cmake -B build_trace -DLOG_LEVEL=5 -DCMAKE_BUILD_TYPE=Debug
cmake --build build_trace --target boot_trace -j
./build_trace/boot_trace -L /tmp/emu.log disks/cpadisk_autofs_clock_noautoexec.img      # leiser Volllauf
./build_trace/boot_trace -p 9000000 disks/cpadisk_autofs_clock_noautoexec.img           # Post-Boot-Report
./build_trace/boot_trace -p 9000000 -l ~/projects/CPA_Workbench/build/bios.prn \
    disks/cpadisk_autofs_clock_noautoexec.img                                           # Histogramme mit BIOS-Quelltext
```

Verfolgt ZVE1 **und** ZVE2 per Instruktion, meldet den DMA-Einfrierpunkt und den
`[03F8]`-Done-Flag-Verlauf. Weiter: `--until <cond>` (bis zu einem Zustand laufen),
`--coverage`/`--diff` (welcher Code lief / zwei Läufe vergleichen), `--csv`
(maschinenlesbarer Trace), `--save-state`/`--load-state` (Checkpoint statt Reboot),
`--json`/`--quiet` (eine Ergebniszeile für Skript/Agent). Log-Gates (`--log-pc`,
`--log-cycle`) heben das Level nur im interessanten Fenster an — leise laufen, gezielt boosten.

## z80_disasm2.py / Disassembler

Vollständige Doku: **[z80_disasm.md](z80_disasm.md)**. Kanonischer Aufruf für das
Boot-ROM und der Hinweis zu `--entry` (Laufzeit-Vektoren wie ZVE2 @ `01DD`) dort.
`disasm_difftest.py` ist der Regressionswächter (gegen `z80dis`) — vor jedem
Engine-Umbau laufen lassen.

## kbd_test — Tastatur-/Boot-Smoke-Test (nicht-interaktiv)

`./build/kbd_test <disk> [text]` bootet, tippt `text`+Enter und gibt den 80×24-
Bildschirm + Tastatur-Port-Verkehr (Port 0x5C/0x5D, mit Quell-PC jedes Zugriffs)
+ ein Foreground-PC-Histogramm aus. Schneller Einzeltest, ob Tasten ankommen und
ob ein Befehl Wirkung zeigt.

**Sondersyntax im `text`:** `|` = Enter mittendrin (z.B. erst die Uhrzeit, dann
ein Kommando in einem Lauf), `^X` = Ctrl+X, `~` = bare Ctrl+C. Beispiel:
`kbd_test disks/cpadisk_autofs_clock_noautoexec.img "120000|DIR"` (Uhr stellen, dann `DIR`).
Auf der **Uhr-Disk nach Zeiteingabe** funktioniert die CCP-Eingabe vollständig
(Echo/Kommando); cpadisk_autofs_noclk_noautoexec erreicht keinen interaktiven CCP (eigenes Thema).
Die K7637 modelliert die 9600-Baud-Latenz — Tasten erscheinen erst ~2604 Takte
nach `keyPress` am SIO (`K7637::service()` aus der Run-Loop).

(`floppy_diag.cpp` ist ein Scratch-Tracer für die ZVE2-Lesezugriffe des laufenden
OS — durch `k1520dbg` weitgehend abgelöst.)

---

## Bekannte Lücken / Backlog

- **z80_disasm2.py**: Behoben 2026-06-05 — war auf format.com (ORG 0100H) fest
  verdrahtet; jetzt generisch. Gleichzeitig latenter Engine-Bug gefixt: die
  unprefixed 3-Byte-Loads `LD (nn),A/HL` und `LD A/HL,(nn)` (Opcodes 22/2A/32/3A)
  lasen das Operandenwort um ein Byte verschoben. Selbsttest in der Datei.
- **z80dis_min.h** (eingebauter Decoder): IXH/IXL in kombinierten `(IX+d)`-Befehlen
  kosmetisch vereinfacht (Länge korrekt). Niedrige Priorität — für solche
  Sonderfälle `z80_disasm2.py` heranziehen.
- **k1520dbg `bt`**: heuristisch (Stack-Scan nach `CALL`-Vorbyte); Falsch-Positive
  möglich. `fin` braucht einen sauberen Stack-Rahmen.
- **z80_disasm.py / z80_disasm3.py**: ORG bzw. Labels hartkodiert; nur als
  Zweitmeinung/Orakel bzw. nur für format.com. Kanonisch bleibt z80_disasm2.py.
- **DD/FD-Shadow-Präfix**: vor einem Nicht-Index-Opcode zeigt z80_disasm2.py
  `DB DDH` + Folgebefehl, z80dis ignoriert das Präfix (hardware-näher). Betrifft
  nur fehlausgerichtete Offsets/Datenbytes; ausgerichteter Code ist bit-genau.
- **boot_trace ZVE2-INIR-Histogramm**: INIR zählt im ZVE2-PC-Histogramm pro Byte
  (PC bleibt auf 0x0242). Kosmetisch, niedrige Priorität.
