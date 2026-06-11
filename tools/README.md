# a5120emu – Werkzeugkasten (tools/)

Hilfsprogramme für Analyse, Disassemblierung und Boot-Tracing der K1520-Emulation.
Dieser Werkzeugkasten wächst iterativ: Wenn bei einer Analyse eine Funktion fehlt
oder falsch ist, wird sie hier ergänzt/gefixt und unter „Bekannte Lücken" notiert.

## z80_disasm2.py — generischer Z80-Disassembler (empfohlen)

Multi-Pass-Disassembler (recursive descent + linear) mit eigener, vollständiger
Instruktionstabelle (ED/CB/DD/FD). Konfigurierbare ORG, Entry-Points, Symbol-Labels
und Kommentare. Ausgabe als Listing `ADDR  hexbytes  instruktion  ; kommentar`.

```
python3 tools/z80_disasm2.py <datei> [optionen]

  --org ADDR            Lade-/Ursprungsadresse (hex 0x.. / ..H oder dez). Default 0
  --entry ADDR          zusätzlicher Recursive-Descent-Einstieg (wiederholbar).
                        Nötig für Code, der nur zur Laufzeit angesprungen wird
                        (z.B. IM2-ISRs, ZVE2 @ 01DD über RAM-Vektor JP 01DD).
  --range START:END     diesen Bereich linear dekodieren (wiederholbar)
  --linear              gesamtes Image nach Recursive Descent linear dekodieren
  --label NAME=ADDR      symbolisches Label für eine Adresse (wiederholbar)
  --comment ADDR=TEXT    Inline-Kommentar an einer Adresse (wiederholbar)
  --no-strings          ASCII-String-Erkennung abschalten
  --title TEXT          Titelzeile im Listing-Header
```

### Kanonischer Aufruf für das ZRE-Boot-ROM (K2526)

ZVE2-DMA-Code (0x01DD) und der Index-Puls-ISR (0x01C7) werden nur zur Laufzeit über
RAM-Vektoren erreicht und müssen daher als `--entry` geseedet werden:

```
python3 tools/z80_disasm2.py doc/EPROMS/zre.rom --org 0 \
  --entry 0x007A --entry 0x01C7 --entry 0x01DD \
  --label LOADADDR=0x03F0 --label EXP_CYL=0x03F3 --label EXP_HEAD=0x03F4 \
  --label EXP_SEC=0x03F5 --label EXP_SIZE=0x03F6 --label IDXCNT=0x03F7 \
  --label DONEFLAG=0x03F8 --label PATHBYTE=0x03FD --label BYTECNT=0x07F1 \
  --label SECCNT=0x07F2 --label ISR_IDXDEC=0x01C7 --label ZVE2_STROBE=0x01DD \
  --label ZVE2_DONE=0x0267 --title "ZRE Boot-ROM K2526"
```

## disasm_difftest.py — Regressionswächter für den Disassembler

Vergleicht die handgeschriebene Engine von `z80_disasm2.py` an **jedem** Byte-Offset
gegen den unabhängigen Drittanbieter-Decoder `z80dis`. Instruktionslänge ist die
objektive Invariante; numerische Operanden werden notationsunabhängig verglichen
(hex `..H` / `0x..` / dezimal). Findet Off-by-one-Operanden, falsche Längen, Lücken.

```
venv/bin/python tools/disasm_difftest.py doc/EPROMS/zre.rom --org 0
venv/bin/python tools/disasm_difftest.py boot_disk/format.com --org 0x100
```

Benötigt `z80dis` (`venv/bin/pip install z80dis`). Exit 0 = deckungsgleich.

**Verifikationsstand 2026-06-05:** zre.rom (1024 Offsets) und format.com (17408
Offsets) — je 0 Längen- und 0 Operanden-Abweichungen. Vor jedem Engine-Umbau
laufen lassen.

## boot_trace.cpp — Boot-Diagnose (gegen libk1520core)

Standalone-Tool, das die Boot-Sequenz **beider** CPUs mitloggt. ZVE1 (Haupt-CPU)
und ZVE2 (DMA-CPU) werden je **per Instruktion** über Trace-Callbacks verfolgt —
Milestones, PC-Histogramme, `[03F8]`-Done-Flag-Verlauf, RAM/VRAM-Dumps. Da ZVE2
nur läuft, während /BUSRQ gehalten wird, ist der Bootsektor-DMA ohne den ZVE2-
Callback unsichtbar; das Tool zeigt jetzt direkt, **wo ZVE2 einfriert** und ob es
die Completion (`[03F8]=3`) erreicht.

Build über `build_trace` (LOG_LEVEL=5):

```
cd build_trace && cmake -DLOG_LEVEL=5 -DCMAKE_BUILD_TYPE=Debug .. && cmake --build . --target boot_trace
# -L lenkt den (sehr ausführlichen) Emulator-Log in eine Datei, damit die
#    Trace-Zusammenfassung lesbar bleibt:
./build_trace/boot_trace -c 3000000 -L /tmp/emu.log disks/cpadisk.img
```

Optionen: `-c <zyklen>`, `-s` (jede ZVE1-ROM- und ZVE2-Instruktion ausgeben),
`-n <anzahl>` (ZVE1-Single-Step-Limit), `-v` (alle ZVE2-Instruktionen statt nur
erste 600), `-L <datei>` (Emulator-Log umleiten).

Verifizierter Befund 2026-06-05 (cpadisk.img): ZVE2 startet, liest Sektor 1 und
**friert bei PC=0x0242 (INIR) ein** — `[03F8]` erreicht nie 3, ZVE1 hängt in der
Warteschleife 0x0168. Das Tool druckt diese Diagnose automatisch. Siehe
doc/analyse_zre_rom_boot.md.

## k1520dbg.cpp — interaktiver Debugger (empfohlen für Post-Boot-Analyse)

Ein gdb-artiger Debugger um `A5120Machine`. **Hauptvorteil gegenüber `boot_trace`:**
boot_trace filtert nach **absoluten** Zyklen ab dem Einschalten — praktisch für die
Boot-ROM-Phase, aber unhandlich, sobald das geladene OS oder ein Programm läuft
(man muss erst die absolute Zyklenzahl finden, dann neu starten). k1520dbg löst das
mit **Breakpoints**, einem **Marker für relative Zyklen** (`mark`) und einer
interaktiven Sitzung.

```
# interaktiv:
./build/k1520dbg disks/cpadisk_02.hfe
# skript-/pipe-getrieben:
printf 'mark 0x37A0\nb 0xC7A3\ng\nr\nq\n' | ./build/k1520dbg disks/cpadisk_02.hfe
./build/k1520dbg disks/cpadisk_02.hfe -x mein_skript.dbg
```

Zeitbasis ist der **ZVE1-Taktzähler** (instruktionsgenau). Der traceCallback feuert
*vor* der Instruktion → an einem Breakpoint zeigt `SP` auf die Rücksprungadresse
(„break before execute"). Befehle:

| Befehl | Wirkung |
|--------|---------|
| `g [N]` | bis zum nächsten Breakpoint laufen (oder N ZVE1-Zyklen) |
| `gu <PC>` | bis ZVE1 PC erreicht (temporärer Breakpoint) |
| `s [N]` | N ZVE1-Instruktionen einzelschritt-tracen (Default 1) |
| `b <PC>` / `b2 <PC>` | Breakpoint auf ZVE1 / ZVE2; `bd`/`bd2` löschen; `bl` listen |
| `mark [PC]` | **relativen Zyklenzähler** jetzt nullen, oder bei PC scharfschalten |
| `r` | Register beider CPUs (inkl. Schattenregister `AF'..HL'`) + Status |
| `d <A> [N]` | Hex+ASCII-Dump (Default 64); `e <A> <b..>` Bytes poken |
| `u <A> [N]` | Disassemblieren via `z80_disasm2.py` (aus Repo-Wurzel starten) |
| `t <lo> <hi> [N]` / `t2 ..` | ZVE1/ZVE2-Instruktionen im PC-Fenster tracen (Cap N); `t off` |
| `wp <A>` / `wd <A>` / `wl` | RAM-Schreibzugriffe beobachten |
| `keys <text>` | Text tippen (`\r`=Enter, `\t`, `\e`); treibt die Maschine weiter |
| `screen` | 80×24-Text-VRAM ausgeben; `vars` benannte RAM-Variablen |
| `reset` / `q` | Power-Cycle / beenden |

**Der Marker ist der Kern:** `mark 0x37A0` nullt den Zähler beim Eintritt in das
geladene OS; alle späteren Trace-/`r`-Ausgaben zeigen `+<relzyklen>`. Für die Analyse
eines später geladenen **Programms** setzt man den Marker auf dessen Einsprungpunkt —
so misst man Zyklen/Verhalten ab Programmstart, nicht ab Power-On. Beispiel
(Lokalisierung des `dir`-Hangs):

```
mark 0x37A0        # Null-Marke = OS-Eintritt
b 0xDB91           # SELDSK (BIOS-Vektor @D200[9])
g ; r ; d 0xD1B2 16
b 0xC7A3           # Divide-Schleife (Hang)
g ; r ; vars
```

## kbd_test.cpp — Tastatur-/Boot-Smoke-Test (nicht-interaktiv)

`./build/kbd_test <disk> [text]` bootet, tippt `text`+Enter und gibt den 80×24-
Bildschirm + Tastatur-Port-Verkehr aus. Schneller Einzeltest, ob Tasten ankommen
und ob ein Befehl Wirkung zeigt. (`floppy_diag.cpp` ist ein Scratch-Tracer für die
ZVE2-Lesezugriffe des laufenden OS — durch k1520dbg weitgehend abgelöst.)

## Bekannte Lücken / Backlog

- **z80_disasm2.py**: Behoben 2026-06-05 — war auf format.com (ORG 0100H) fest
  verdrahtet; jetzt generisch. Gleichzeitig latenter Engine-Bug gefixt: die
  unprefixed 3-Byte-Loads `LD (nn),A/HL` und `LD A/HL,(nn)` (Opcodes 22/2A/32/3A)
  lasen das Operandenwort um ein Byte verschoben (`w(2)` statt `w(1)`) → falsche
  Adressen. Selbsttest in der Datei.
- **z80_disasm.py**: läuft wieder (`z80dis` 2026-06-05 ins venv installiert), aber
  ORG ist auf 0x0100 hartkodiert (CP/M). Nur als Zweitmeinung; z80dis dient v.a.
  als Orakel für disasm_difftest.py. Kanonisch bleibt z80_disasm2.py.
- **DD/FD-Shadow-Präfix**: vor einem Nicht-Index-Opcode zeigt z80_disasm2.py
  `DB DDH` + Folgebefehl, z80dis ignoriert das Präfix (hardware-näher). Betrifft
  nur fehlausgerichtete Offsets/Datenbytes; ausgerichteter Code ist bit-genau.
  Niedrige Priorität.
- **z80_disasm3.py**: enthält hartkodierte FORMAT.COM-Labels (WBOOT/BDOS/…). Nicht
  generisch; nur für format.com sinnvoll.
- **boot_trace.cpp**: Behoben 2026-06-05 — verfolgt jetzt beide CPUs per
  Instruktion (ZVE1+ZVE2 Trace-Callbacks), bricht nicht mehr bei 0x01DD ab,
  trackt `[03F8]` statt `[0xFFFC]`, `romLabel()` deckt ZVE2-/Warteschleifen-
  Regionen ab, `-L` lenkt den Emulator-Log um, Boot-Erfolg = ZVE1 in geladenem
  Code (PC≥0x0400). Erfordert die K2526/A5120-Accessors `zve2PC()`,
  `setZVE2TraceCallback()`, `isZVE2InReset/Waiting()`, `isBUSRQ()`.
- **boot_trace ZVE2-INIR-Histogramm**: INIR zählt im ZVE2-PC-Histogramm pro Byte
  (PC bleibt auf 0x0242). Kosmetisch, niedrige Priorität.
