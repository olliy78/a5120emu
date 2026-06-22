# boot_trace — Boot-/DMA-Tracer für die A5120/K1520-Emulation

`boot_trace` ist ein **nicht-interaktives** Diagnosewerkzeug, das eine Boot-Sequenz von
Power-On bis zu einem Zyklenlimit (oder einer Bedingung) durchläuft und dabei **beide
CPUs** der ZRE/K2526 — Haupt-CPU **ZVE1** und DMA-Prozessor **ZVE2** — per Instruktion
mitverfolgt. Es ist auf den **Bootpfad** zugeschnitten: Es kennt Boot-ROM, ZVE2-DMA-
Routinen und Warteschleifen, erkennt Meilensteine und meldet, **wo die DMA-Kette
stehenbleibt**.

> **boot_trace vs. k1520dbg.** boot_trace lokalisiert die *grobe Phase* und erzeugt
> reproduzierbare Reports/Exports; `k1520dbg` (s. `tools/k1520dbg.md`) seziert interaktiv.
> Praxis-Rezepte für beide: **`tools/how_to_debug_and_trace.md`**.

---

## 1. Bauen

`boot_trace` wird mit der Compile-Obergrenze `LOG_LEVEL=5` gebaut (jede Logstelle zur
Laufzeit verfügbar; tatsächliche Ausgabe ist laufzeitgesteuert, Default `ERROR` → leise):

```sh
cmake -B build_trace -DLOG_LEVEL=5 -DCMAKE_BUILD_TYPE=Debug   # Trace-Build (Konvention)
cmake --build build_trace --target boot_trace -j
# (das normale build/ enthält boot_trace ebenfalls)
```

---

## 2. Aufruf & Optionen

```
boot_trace [DISK] [optionen]
```

| Option | Wirkung |
|--------|---------|
| `-c <zyklen>` | Boot-Zyklenlimit |
| `-p <zyklen>` | **nach** dem Boot weiterlaufen (`0x0437`+) — aktiviert den Post-Boot-Report (Port-/Loaded-code-Histogramm, VRAM-Schreibzähler, 80-Spalten-VRAM-Textdump) |
| `--until <cond>` | **anhalten, sobald `<cond>` gilt** (läuft über den Boot-Handoff hinaus bis zur Bedingung oder zum `-c`-Limit), dann Report (§3) |
| `--coverage [file]` | **Code-Coverage**: ausgeführte ZVE1-Byte-Ranges + ZVE2-Adresszahl; mit `file` zusätzlich CSV `cpu,pc,hits` (§4) |
| `--diff a.csv b.csv` | **Run-Diff** zweier `--coverage`-CSVs (nur-A/nur-B/hit-diff je CPU) — **ohne** Emulation (§4) |
| `--csv <file>` | **maschinenlesbarer Per-Instruktions-Trace** (`seq,cyc,cpu,pc,bytes,disasm,…regs`); durch `-w`/`-z`/`--until` eingrenzbar, Cap 5 Mio. Zeilen (§4) |
| `--save-state <file>` | am Lauf-Ende (z. B. mit `--until`) den **Maschinenzustand** sichern (Checkpoint) |
| `--load-state <file>` | mit gesichertem Zustand **starten** statt zu booten (RAM+CPU+ROM-Mapping + Tastatur-Subsystem reproduziert; Floppy-K5122/VRAM nicht) |
| `--json` | am Ende **eine JSON-Zeile** (`boot_reached,cycles,rom_enabled,final_pc,zve1_addrs,zve2_addrs,zve2_instr,until{set,met,cycle,pc}`) |
| `--quiet` | **unterdrückt die menschliche Narrative** (Banner, Progress, Milestones, Summary, Histogramme, VRAM). Es bleiben nur `--coverage`/`--json`/`-d` und Warnungen/Fehler — `--quiet --json` = genau 1 Zeile statt ~880 |
| `--drive <n>` | Disk auf Laufwerk `n` mounten (Default 0 = A:) |
| `-L <datei>` | den (ausführlichen) **Emulator-Log** umleiten, damit der Report lesbar bleibt (`-L /dev/null` verwirft ihn) |
| `--log-level <off…trace>` · `--log-pc LO:HI[:lvl]` · `--log-cycle FROM:TO[:lvl]` | Logger-Basislevel und **Gates** (§5) |
| `-s` / `-n <anzahl>` | jede ZVE1-ROM- & ZVE2-Instruktion einzeln (disassembliert) / Single-Step-Limit |
| `-v` | **alle** ZVE2-Instruktionen (statt nur der ersten ~600) |
| `-w LO:HI` / `-z LO:HI` / `-W <n>` | ZVE1- / ZVE2-Instruktionsfenster tracen (disassembliert) / Zeilencap |
| `-d LO:HI [datei]` | RAM-Bereich am Ende dumpen (optional in Datei) |
| `--watch a,b,…` / `--watchio p,q,…` | Schreibzugriffe auf RAM-Adressen / Zugriffe auf I/O-Ports mitloggen |
| `-l <f.prn>[@off]` | `.prn`-Listing → Trace-Zeilen & Histogramme mit kommentiertem Original-Quelltext annotieren (wiederholbar, §4) |

**Exit-Code** (Skript-/Agenten-Verzweigung): mit `--until` → `0` erreicht / `2` nicht
(Limit zuerst); sonst `0` wenn der Boot-Handoff erreicht wurde, sonst `1`. (`--diff`: `0`/`1`.)

> **Per-Instruktions-Traces sind disassembliert** (`-s`/`-w`/`-z`/Step) — aus dem **Live-
> Speicher** decodiert, exakt auch bei selbstmodifizierendem Loader-Code. Wo geladener Code
> ≠ `.prn`-Listing ist, ist die **Disassembly** maßgeblich (die `.prn`-Annotation stammt aus
> dem *statischen* Listing).

---

## 3. Lauf-bis-Bedingung (`--until`)

Statt eine feste Zyklenzahl zu raten, hält `--until <cond>` den Lauf an, sobald die
Bedingung gilt; danach folgt der normale Report (oder nur die gewünschten Exports). Der
Lauf geht über den Boot-Handoff hinaus, man braucht also kein `-p`.

```sh
boot_trace --until '[0x03F8]==3'  disk.img            # bis ZVE2 das Done-Flag setzt
boot_trace --until 'PC==0x0437'   disk.img            # bis ZVE1 den Boot-Handoff erreicht
boot_trace --until '[0xD1BE]w!=0' -d 0xD1B0:0xD1D0 disk.img   # … dann RAM dumpen
```

Bedingungen (pro ZVE1-Instruktion geprüft): `PC<op>A`, `[A]<op>V`, `[A]w<op>V` (16-Bit LE),
`<op> ∈ == != < > <= >=`; `A`/`V` base-0. Am Ende: `MET at cycle … (PC=…)` bzw. `not met`.
Kombinierbar mit `-d`/`-w`/`-z`/`--coverage`/`--csv`, um genau im erreichten Zustand zu
dumpen/tracen, und mit `--save-state`, um dort einen Checkpoint zu setzen.

---

## 4. Maschinenlesbare Ausgaben: `.prn`, Coverage, Diff, CSV

**`.prn`-Annotation (`-l`).** Hängt an jede Trace-Zeile und jeden PC im Histogramm die
kommentierte Original-Quellzeile aus dem MACRO-80-Listing an (`-l bios.prn`). Wiederholbar;
Offset `@OFFSET` reloziert die Listing-Adressen (Details wie bei `k1520dbg`, dort §6).

```
  0x0420 :   7422     ; jr z,coitn2 ;;nein
```

**Code-Coverage (`--coverage`).** Welcher ZVE1-Code tatsächlich lief: jede ausgeführte
Instruktion wird einmal decodiert (Länge), ihre Bytes als abgedeckt markiert und zu Ranges
zusammengefasst, plus die Zahl distinkter ZVE2-Adressen.

```
=== Code coverage (ZVE1) ===
  164 distinct instr addresses, 278 bytes covered in 46 range(s):
    0x0000-0x0008  (9 bytes)
  ZVE2: 65 distinct instr addresses executed
```

**Run-Diff (`--diff`).** Mit `--coverage <file.csv>` entsteht ein sortiertes CSV `cpu,pc,hits`.
`boot_trace --diff a.csv b.csv` vergleicht zwei davon — *ohne* Emulation — und meldet je CPU
`only-A` / `only-B` / `hit-diff`:

```
ZVE1: A=164 addrs, B=149 addrs | only-A=15 only-B=0 common=149 hit-diff=2
   only-A: 016C 016D … 0437
```

**Per-Instruktions-Trace (`--csv`).** Schreibt jede ausgeführte Instruktion (ZVE1+ZVE2) als
CSV-Zeile (`disasm` gequotet). Voller Trace → mit `-w`/`-z`/`--until` eingrenzen; Cap 5 Mio.

```
seq,cyc,cpu,pc,bytes,disasm,af,bc,de,hl,ix,iy,sp
3,92702,ZVE1,0x0109,45,"LD B,L",EE10,0000,0400,0400,0000,0000,07E0
```

---

## 5. Log-Gates — leise laufen, gezielt boosten

Der Logger ist laufzeitgesteuert und gegated (`core/logger.h`). Statt „auf Level 5 bauen →
Multi-GB-Log → grep" gilt: **leise laufen, nur das interessante Fenster boosten.**

* **Basislevel** `--log-level` (Default `error` → schnell).
* **PC-Gate** `--log-pc LO:HI[:level]` — Level anheben, solange ein CPU-PC im Bereich liegt.
* **Zyklen-Gate** `--log-cycle FROM:TO[:level]` — Level im Zyklenfenster anheben.

> **Fallstrick:** Ein `--log-pc` auf eine **Spin-Loop**-Adresse feuert, solange die CPU dort
> parkt (zig Mio. Zyklen → Multi-GB-Log). Immer mit engem `--log-cycle` koppeln — oder gleich
> nur ein Zyklenfenster verwenden.

---

## 6. Was die Zusammenfassung zeigt

Am Ende eines (nicht `--quiet`-)Laufs druckt `boot_trace`:

* **Meilensteine** des Bootpfads (ROM-Phasen, ZVE2-Start, Completion `[03F8]=3`, Sprung ins
  geladene Code bei `0x0437`) und **`Boot reached: YES/NO`**.
* **Done-Flag-Verlauf** `[03F8]` (0=läuft, 1=ISR-Timeout, 3=fertig) und den ZVE2-Einfrierpunkt.
* **PC-Histogramme** für ZVE1 und ZVE2 (wo Zeit verbracht / wo eingefroren wird).
* bei `-p`: **I/O-Port-Histogramm**, **VRAM-Schreibzähler + -Bereich**, Loaded-code-Histogramm
  und ein **80×24-Textdump** des VRAM (`0xF800`) — das Bildschirm-Banner wird sichtbar.

Hintergrund zum Bootpfad und zur ZVE1↔ZVE2-DMA-Logik: `doc/analyse_zre_rom_boot.md`,
`doc/K1520_architecture.md` §8.5/§14.x.
