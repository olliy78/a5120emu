# Werkzeugkasten (`tools/`)

Werkzeuge für Analyse, Disassemblierung, Boot-Tracing und interaktives Debuggen der
K1520-Emulation. Der Kasten wächst iterativ: fehlt bei einer Analyse eine Funktion, wird
sie hier ergänzt und unter „Bekannte Lücken" notiert.

> **Einstieg für die Emulatorentwicklung:**
> **[how_to_debug_and_trace.md](how_to_debug_and_trace.md)** — welches Werkzeug wann, mit
> durchgerechneten Szenarien (Boot-Hänger, Programm sezieren, Interrupt-/Uhr-Analyse,
> Coverage/Diff, Save-State).
>
> **Einstieg für Anwender:** **[doc/handbuch_k1520dbg.md](../doc/handbuch_k1520dbg.md)** —
> Handbuch zum Debugger für alle, die *eigene* Gastprogramme untersuchen (mit und ohne
> Quelltext, Blick in PIO/SIO/CTC, Rezepte). Für die Auslieferung an Anwender vorgesehen:
> `doc/design/13_distribution.md` §10a.
>
> **Faustregel:** mit `boot_trace` die Phase **lokalisieren**, dann mit `k1520dbg`
> **sezieren**.

## Aufrufen

**Immer über `tools/dev.sh`** — es baut das passende Verzeichnis vorher neu. Es gibt zwei
Bauverzeichnisse mit denselben Werkzeugnamen (`build/` mit LOG_LEVEL=3, `build_trace/` mit
LOG_LEVEL=5); direkt aus einem davon zu starten testet leicht veraltete Objektdateien.

```sh
tools/dev.sh tool k1520dbg <args>     # build/ bauen, dann build/k1520dbg
tools/dev.sh trace <args>             # build_trace/ bauen, dann boot_trace
tools/dev.sh tool floppy_diag <args>  # ebenso für jedes andere Werkzeug
```

**Disketten sind sicher.** `k1520dbg` und `boot_trace` mounten **Copy-on-Write**: sie legen
eine Temp-Kopie an und arbeiten auf der, ein committetes Fixture kann also nicht beschädigt
werden. Die Diskette einfach direkt übergeben. `--rw` nur, wenn ein Schreibvorgang
bestehen bleiben soll (z. B. Formatier-Versuche) — dann auf einer eigenen Kopie arbeiten.

**Sparsam laufen lassen** (zählt besonders für Agenten):

- `boot_trace`: `-L /dev/null` verwirft den Emulator-Log; **`--quiet --json`** liefert genau
  *eine* maschinenlesbare Ergebniszeile statt ~880, dazu einen sinnvollen Exit-Code
  (`--until`: 0 erreicht / 2 nicht). Statt Zyklen zu raten: **`--until <cond>`**.
- `k1520dbg`: im Stapelbetrieb über eine Pipe (`printf 'b 0x0437\ng\nrj\nq\n' | …`) oder
  `-x skript.dbg`; `rj` druckt Register als JSON. Die REPL mit readline ist für Menschen.
- **Einmal booten, oft fortsetzen:** `--save-state`/`--load-state` (boot_trace) bzw.
  `savestate`/`loadstate` (k1520dbg) sichern RAM+CPU+ROM-Mapping in eine Datei — der ~2 s
  lange Boot wird zur Einmalinvestition.
- `-l <listing.prn>` bzw. `-l <quelle.mac@auto>` zeigt kommentierten Originalquelltext
  statt rohem Disassemblat.

## Werkzeuge

### Gebaut aus C++ (CMake, in `build/`)

| Werkzeug | Zweck | Doku |
|---|---|---|
| **`k1520dbg`** | Interaktiver gdb-artiger Debugger für **beide** CPUs: bedingte und Ereignis-Breakpoints, Step into/over/out, Reverse-Step + Snapshots + Save-State, Watch auf Speicher/Ports, Logpoints, `x`-Examine, `.prn`/`.MAC`-Annotation, Chip-Zustand (`dev`, `ivt`), History-`bt` | **[k1520dbg.md](k1520dbg.md)** |
| **`boot_trace`** | Nicht-interaktiver Boot-/DMA-Tracer: Report mit Histogrammen, `[03F8]`-Done-Flag und VRAM-Banner; `--until`, `--coverage`/`--diff`, `--csv`, `--fold`, `--itrace`, Log-Gates | **[boot_trace.md](boot_trace.md)** |
| `format_driver` | Skriptgesteuerter Treiber für interaktive Gastprogramme: bootet CP/A mit zwei Disketten, sendet Tastenfolgen, gibt zwischen den Schritten den 80×24-Text aus. Rückgrat der Formatier-Pipeline (`FORMAT.COM`/`FORMATB.COM`) | `doc/format.md` |
| `kbd_test` | Tastatur-/Boot-Smoke: bootet, tippt Text + Enter, gibt Bildschirm, Tastatur-Portverkehr (0x5C/0x5D mit Quell-PC) und ein PC-Histogramm aus | unten |
| `floppy_diag` | Lese-Diagnose am **laufenden** OS: verfolgt jeden ZVE2-Lesezugriff über `0x1F7D` — gesuchte (cyl, head, sector, size) und ob das IDAM gefunden wurde | — |
| `mk_disk_template` | Erzeugt gültig vorformatierte **einseitige** Leerdisketten (HFE) im CP/A-Systemlayout — 8″-FM/MFM und 5¼″-SS | `tests/fixtures/README.md` |
| `bench_run` | Leistungsmessung mit fester Last: bootet bis zum Prompt und misst danach die reine Emulationsrate von `A5120Machine::run()` in Mcycles/s und als Faktor gegen 2,5 MHz | — |

### Header-only Bausteine

Sie bilden die Innereien von `k1520dbg` und `boot_trace` und sind **einzeln getestet**
(`tests/debugtools/`, 89 Fälle) — beim Ändern dort mitziehen.

| Header | Inhalt | Doku |
|---|---|---|
| `z80dis_min.h` | Ein-Instruktions-Z80-Decoder (C++) | [z80_disasm.md](z80_disasm.md) |
| `prn_listing.h` | Parser für MACRO-80-`.prn`-Listings: Adresse → kommentierte Quellzeile, optional Objektbytes | [k1520dbg.md](k1520dbg.md) §6 |
| `mac_listing.h` | **Assembler für Fremdquellen** (`.MAC`/`.ASM` ohne Adressspalte): Adressen + Objektbytes, `Mxxxx`-Anker, Versatz-Abgleich `@auto`. Die Opcode-Tabelle wird zur Laufzeit aus `z80dis_min.h` rückwärts erzeugt | [k1520dbg.md](k1520dbg.md) §6.1 |
| `callstack_tracker.h` | Exakter CALL/RST/RET-Aufrufstapel für den History-`bt` | [k1520dbg.md](k1520dbg.md) §7 |
| `expr_eval.h` | Ausdrucks-Evaluator (Arithmetik/Bit/Vergleiche/`[expr]`) für `if`/`disp`/`x`/`logpoint` | [k1520dbg.md](k1520dbg.md) §3 |
| `event_bp.h` · `mem_watch.h` | Ereignis-Breakpoint-Klassifikation (Interrupt/NMI/RETI) bzw. Watchpoint-Abgleich | [k1520dbg.md](k1520dbg.md) §4 |
| `dbg_commands.h` | Kommandoliste + Präfix-Matcher für die Tab-Vervollständigung | [k1520dbg.md](k1520dbg.md) §1 |
| `coverage_diff.h` | Parser + Diff (+ Bereichskollaps) für `--coverage`/`--diff` | [boot_trace.md](boot_trace.md) §4 |
| `until_cond.h` | Parser und Auswertung der `--until`-Bedingung | [boot_trace.md](boot_trace.md) §3 |

### Python

| Skript | Zweck |
|---|---|
| **`z80_disasm2.py`** | **Kanonischer** generischer Z80-Disassembler (`--org`, wiederholbare `--entry`/`--label`) — [z80_disasm.md](z80_disasm.md) |
| `z80_disasm.py` · `z80_disasm3.py` | Ältere, format.com-spezifische Fassungen (ORG bzw. Labels fest verdrahtet). Nur als Zweitmeinung |
| `disasm_difftest.py` | Regressionswächter: `z80_disasm2.py` gegen das `z80dis`-Paket. Vor jedem Umbau der Disassembler-Engine laufen lassen |
| `eprom_to_h.py` | EPROM-Binärdatei → committetes C-Array (`*_data.h`) |
| `img_to_hfe.py` | Rohes `.img` → HFE-v1-Diskettenabbild |
| `fb_ocr.py` | Framebuffer-OCR: zerlegt den 640×288-Puffer der K7024 wieder in Text. Benutzt von `tests/python/test_gui_smoke.py` |
| `capture_format_menus.py` | Greift die FORMAT.COM-Formatmenüs je Laufwerkstyp live aus dem Emulator ab (Grundlage der Format-Matrix) |
| `gen_zre_prn.py` · `gen_scpx_readpath_prn.py` | Erzeugen die kommentierten `.prn`-Listings (`doc/EPROMS/zre.prn`, SCPX-Lesepfad) für die `-l`-Annotation |
| `analyze_eprom.py` · `disasm_k2526.py` · `analyze_vram.py` | Einzelanalysen am ZRE-EPROM bzw. am Bildwiederholspeicher |

Zusatzabhängigkeit nur für `disasm_difftest.py`: `venv/bin/pip install -r tools/requirements.txt`.

### Datendateien

| Datei | Zweck |
|---|---|
| `scpx1526.sym` | SCPX-1526-BIOS-Symbole — `k1520dbg -s tools/scpx1526.sym …` |
| `scpx.vars` | Handshake-/Lese-Dashboard für `vars -f tools/scpx.vars` |

### Unterprojekte

| Verzeichnis | Inhalt |
|---|---|
| [`bootsec/`](bootsec/README.md) | Vollständig kommentiertes Disassemblat des CP/A-SYL-Bootladers (`src/*.mac`), Build-Skript und Analysewerkzeuge |
| [`romread/`](romread/README.md) | `romread.com` — CP/M-Programm, das das Boot-EPROM der ZRE/K2526 **echter Hardware** ausliest |

---

## kbd_test — Tastatur-/Boot-Smoke

```sh
tools/dev.sh tool kbd_test <disk> [text]
```

Bootet, tippt `text` + Enter und gibt den 80×24-Bildschirm, den Tastatur-Portverkehr
(Ports 0x5C/0x5D mit dem Quell-PC jedes Zugriffs) und ein Vordergrund-PC-Histogramm aus.
Schneller Einzeltest, ob Tasten ankommen und ob ein Befehl Wirkung zeigt.

**Sondersyntax im `text`:** `|` = Enter mittendrin, `^X` = Strg+X, `~` = blankes Strg+C.

```sh
tools/dev.sh tool kbd_test disks/cpa_cpa780_k5601_clock.img "120000|DIR"   # Uhr stellen, dann DIR
```

Auf der Uhr-Diskette funktioniert die CCP-Eingabe nach der Zeiteingabe vollständig;
`cpa_cpa780_k5601_noclock` erreicht keinen interaktiven CCP (eigenes Thema). Die K7637
modelliert die 9600-Baud-Latenz — Tasten erscheinen erst ~2604 Takte nach `keyPress` am SIO.

---

## Bekannte Lücken

- **`z80dis_min.h`**: IXH/IXL in kombinierten `(IX+d)`-Befehlen kosmetisch vereinfacht
  (Länge korrekt). Für solche Sonderfälle `z80_disasm2.py` heranziehen.
- **`k1520dbg bt`**: der Stack-Scan-`bt` ist heuristisch (sucht das `CALL`-Vorbyte),
  Falschpositive sind möglich; `fin` braucht einen sauberen Stapelrahmen. Der
  History-`bt` (`callstack_tracker.h`) ist exakt.
- **DD/FD-Schattenpräfix**: vor einem Nicht-Index-Opcode zeigt `z80_disasm2.py`
  `DB DDH` + Folgebefehl, `z80dis` ignoriert das Präfix (hardwarenäher). Betrifft nur
  fehlausgerichtete Offsets und Datenbytes; ausgerichteter Code ist bitgenau.
- **`boot_trace` ZVE2-INIR-Histogramm**: `INIR` zählt pro Byte, weil der PC auf `0x0242`
  stehen bleibt. Kosmetisch.
- **Tastatur-Diagnose** (`dev kbd`, `keys --echo`, `keys \S`, adaptives `keys`) ist
  vorgeschlagen, aber nicht umgesetzt: [`feature_request_keyboard_diagnostics.md`](feature_request_keyboard_diagnostics.md).

> **2026-08-09 entfernt:** die Wegwerf-Analysewerkzeuge `scpx_extract.cpp` (Dateien aus
> einer SCPX-Diskette extrahieren) und `mk_blank.cpp` (formatierte Leerdiskette anlegen).
> Beide wurden von CMake nicht gebaut. Ersatz: `k1520dbg disk verify` für die
> Medienprüfung und `A5120Machine::createDisk` bzw. „Neue Diskette" in der Oberfläche
> zum Anlegen. Ältere Analysetexte unter `doc/` nennen sie noch — dort sind sie
> Protokoll, kein Handlungsvorschlag.
