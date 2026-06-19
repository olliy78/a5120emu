# a5120emu – Werkzeugkasten (tools/)

Hilfsprogramme für Analyse, Disassemblierung, Boot-Tracing und interaktives Debuggen
der K1520-Emulation. Dieser Werkzeugkasten wächst iterativ: Wenn bei einer Analyse
eine Funktion fehlt oder falsch ist, wird sie hier ergänzt/gefixt und unter
„Bekannte Lücken" notiert.

## Überblick — welches Werkzeug wofür

| Werkzeug | Zweck | Detail-Doku |
|----------|-------|-------------|
| **`k1520dbg`** | interaktiver gdb-artiger Debugger (ZVE1 **und** ZVE2): Breakpoints, bedingte BPs, Step into/over/out, Watch mem/io, Symbole, Disassembly, Register-Edit, Backtrace | **[k1520dbg.md](k1520dbg.md)** |
| **`boot_trace`** | nicht-interaktiver Boot-/DMA-Tracer mit Zusammenfassung (Histogramme, Done-Flag, VRAM-Banner) — findet *wo* die Kette hängt | **[boot_trace.md](boot_trace.md)** |
| **`z80_disasm2.py`** | generischer, vollständiger Z80-Disassembler für Listings (kanonisch) | **[z80_disasm.md](z80_disasm.md)** |
| `z80dis_min.h` | eingebauter Ein-Instruktions-Decoder (C++) für `k1520dbg` | [z80_disasm.md](z80_disasm.md) |
| `disasm_difftest.py` | Regressionswächter: `z80_disasm2.py` gegen `z80dis` | [z80_disasm.md](z80_disasm.md) |
| `kbd_test` | Tastatur-/Boot-Smoke-Test (boot, tippe Befehl, dump Screen) | unten |
| `eprom_to_h.py` | EPROM-Binär → committetes C-Array (`*_data.h`) | — |
| `img_to_hfe.py` | Roh-`.img` → HFE-v1-Diskettenabbild | — |

**Empfohlene Reihenfolge bei einem Boot-/Lade-Problem:** mit `boot_trace` die grobe
Phase und den Hang-Punkt **lokalisieren**, dann mit `k1520dbg` (Breakpoint + mark +
Step) **sezieren**. Geladenen Code mit `boot_trace -d` / `k1520dbg save` sichern und
mit `z80_disasm2.py` disassemblieren.

---

## k1520dbg — interaktiver Debugger

Vollständiges Handbuch: **[k1520dbg.md](k1520dbg.md)**. Kurz:

```sh
cmake --build build --target k1520dbg -j
./build/k1520dbg disks/cpadisk01.img                 # interaktiv
printf 'b 0xC7A3\ng\nr\nq\n' | ./build/k1520dbg disks/cpadisk01.img   # pipe
./build/k1520dbg disks/cpadisk_02.hfe -x skript.dbg -s symbole.sym
```

Kann **beide CPUs** debuggen (ZVE1 ohne Suffix, ZVE2 mit `2`: `b2`, `s2`, `set 2`),
bedingte Breakpoints (`b A if [03F8]!=3`), Step into/over/out (`s`/`n`/`fin`),
Speicher- und I/O-Watchpoints (`wp`/`wb`/`iow`/`iob`), Symbole (`sym`/`-s`),
eingebautes Disassembly an jedem Stopp, Register-Edit (`set`), Backtrace (`bt`) und
den relativen Zyklen-Marker (`mark`). `help` listet alle Befehle.

## boot_trace — Boot-/DMA-Tracer

Vollständige Doku: **[boot_trace.md](boot_trace.md)**. Kurz (Build über `build_trace`
mit `LOG_LEVEL=5`):

```sh
cmake -B build_trace -DLOG_LEVEL=5 -DCMAKE_BUILD_TYPE=Debug
cmake --build build_trace --target boot_trace -j
./build_trace/boot_trace -L /tmp/emu.log disks/cpadisk01.img      # leiser Volllauf
./build_trace/boot_trace -p 9000000 disks/cpadisk01.img           # Post-Boot-Report
```

Verfolgt ZVE1 **und** ZVE2 per Instruktion, meldet den DMA-Einfrierpunkt und den
`[03F8]`-Done-Flag-Verlauf. Log-Gates (`--log-pc`, `--log-cycle`) heben das Level
nur im interessanten Fenster an — leise laufen, gezielt boosten.

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
`kbd_test disks/cpadisk_mitUhr_01.img "120000|DIR"` (Uhr stellen, dann `DIR`).
Auf der **Uhr-Disk nach Zeiteingabe** funktioniert die CCP-Eingabe vollständig
(Echo/Kommando); cpadisk_02 erreicht keinen interaktiven CCP (eigenes Thema).
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
