# boot_trace — Boot-/DMA-Tracer für die A5120/K1520-Emulation

`boot_trace` ist ein **nicht-interaktives** Diagnosewerkzeug, das eine Boot-Sequenz
von Power-On bis zu einem Zyklenlimit durchläuft und dabei **beide CPUs** der ZRE/
K2526 — Haupt-CPU **ZVE1** und DMA-Prozessor **ZVE2** — per Instruktion mitverfolgt.
Es ist auf den **Bootpfad** zugeschnitten: Es weiß, wo das Boot-ROM, die ZVE2-DMA-
Routinen und die Warteschleifen liegen, erkennt Meilensteine und meldet automatisch,
**wo die DMA-Kette stehenbleibt**.

> **boot_trace vs. k1520dbg.** `boot_trace` filtert nach **absoluten** Zyklen ab
> Power-On und produziert am Ende eine **Zusammenfassung** (Histogramme, Done-Flag-
> Verlauf, VRAM-Dump). Es ist die richtige Wahl, um die *grobe Phase* eines Hangs zu
> finden und reproduzierbare Reports zu erzeugen. Für das interaktive *Sezieren* —
> Breakpoints, Single-Step, Register ändern — nimm `k1520dbg` (siehe
> `tools/k1520dbg.md`). Typische Reihenfolge: **boot_trace lokalisiert → k1520dbg
> seziert.**

---

## 1. Bauen

`boot_trace` wird mit der Compile-Obergrenze `LOG_LEVEL=5` gebaut, damit *jede*
Logstelle zur Laufzeit verfügbar ist (die tatsächliche Ausgabe ist laufzeitgesteuert,
Default `ERROR` → der Lauf ist leise und schnell):

```sh
# eigenes Trace-Build-Verzeichnis (Konvention):
cmake -B build_trace -DLOG_LEVEL=5 -DCMAKE_BUILD_TYPE=Debug
cmake --build build_trace --target boot_trace -j

# (das normale build/ enthält boot_trace ebenfalls; build_trace/ ist die
#  reine Trace-Variante mit voller Log-Decke)
```

---

## 2. Aufruf & Optionen

```
boot_trace [DISK] [optionen]
```

| Option | Wirkung |
|--------|---------|
| `-c <zyklen>` | Boot-Zyklenlimit (vor Erreichen des geladenen Codes) |
| `-p <zyklen>` | **nach** dem Boot weiterlaufen (`0x0437`+) — aktiviert den Post-Boot-Report (Port-Histogramm, VRAM-Schreibzähler + -Bereich, PC-Histogramm des geladenen Codes, 80-Spalten-VRAM-Textdump) |
| `--drive <n>` | Disk auf Laufwerk `n` mounten (Default 0 = A:) |
| `-L <datei>` | den (sehr ausführlichen) **Emulator-Log** in eine Datei umleiten, damit die Trace-Zusammenfassung lesbar bleibt (`-L /dev/null` verwirft ihn) |
| `--log-level <off\|error\|warn\|info\|debug\|trace>` | Laufzeit-**Basislevel** des Loggers |
| `--log-pc LO:HI[:level]` | Log-**Gate**: effektives Level anheben, solange *einer* der beiden CPU-PCs im Bereich liegt |
| `--log-cycle FROM:TO[:level]` | Log-Gate über ein **Zyklenfenster** |
| `-s` | jede ZVE1-ROM- und ZVE2-Instruktion einzeln ausgeben (Single-Step-Trace) |
| `-n <anzahl>` | ZVE1-Single-Step-Limit |
| `-v` | **alle** ZVE2-Instruktionen ausgeben (statt nur der ersten ~600) |
| `-w LO:HI` | ZVE1-Instruktionsfenster tracen (PC-Bereich) |
| `-z LO:HI` | ZVE2-Instruktionsfenster tracen (PC-Bereich) |
| `-W <n>` | Cap für die `-w`/`-z`-Fenster (max. Zeilen) |
| `-d LO:HI [datei]` | RAM-Bereich am Ende dumpen (optional in Datei, z. B. zum externen Disassemblieren) |
| `--watch a,b,…` | Schreibzugriffe auf diese RAM-Adressen mitloggen (kommagetrennt) |
| `--watchio p,q,…` | Zugriffe auf diese I/O-Ports mitloggen |
| `-l <f.prn>[@off]` | MACRO-80-`.prn`-Listing laden → Trace-Zeilen & PC-Histogramme mit dem **kommentierten Original-Quelltext** annotieren (wiederholbar; s. §2a) |

### 2a. Listing-Annotation (`-l`)

Die CP/A-Quellen (BIOS, BDOS, …) liegen als gut kommentierte **MACRO-80-`.prn`-Listings**
vor (z. B. `CPA_Workbench/build/bios.prn`). Mit `-l <datei.prn>` hängt `boot_trace` an
jede Trace-Zeile (`-s`/`-v`/`-w`/`-z`/Step) **und** an jeden PC im Histogramm (ZVE1, ZVE2,
Loaded-code) die **Original-Quellzeile** (Label, Mnemonic, `;Kommentar`) an, deren Adresse
im Listing steht — kein Raten mehr, was ein Codestück tut:

```
  0x041E :   7422     ; bit 0,a ;;Zeichen da?
  0x0420 :   7422     ; jr z,coitn2 ;;nein
  0x0426 :   7422     ; in a,(c)
```

`-l` ist **wiederholbar** — mehrere Listings decken zusammen unterschiedliche
Adressbereiche ab. Wo keine `.prn`-Zeile passt, bleibt es bei der bisherigen Ausgabe.

**Adress-Offset `@OFFSET`.** Das Listing trägt absolute Lade-Adressen. Läuft der Code
reloziert an einer anderen Adresse, hängt man einen vorzeichenbehafteten Offset an, der
zu jeder Listing-Adresse addiert wird (Listing-Adresse + Offset = Laufzeit-Adresse):

```sh
boot_trace -l bios.prn@0x100   …     # Listing-Adresse D227 → matcht Laufzeit-PC D327
boot_trace -l stage3.prn@-0x800 …    # Offset darf negativ sein
```

`OFFSET` versteht `0x1F00`, `1800h`, `512`, `-0x100`. Parser & Grenzen wie bei `k1520dbg`
(s. `tools/k1520dbg.md` §6a): nur absolute Adressen, ein BIOS-Listing deckt nur den
BIOS-Bereich ab.

---

## 3. Die Log-Gates — leise laufen, gezielt boosten

Der Logger ist **laufzeitgesteuert und gegated** (`core/logger.h`). Statt „auf
Level 5 bauen → Multi-GB-Log → grep" lautet das Muster: **leise laufen, nur das
interessante Fenster boosten.**

* **Basislevel** (`--log-level`): globale Untergrenze. Default `error` → schnell.
* **PC-Gate** (`--log-pc LO:HI[:level]`): hebt das Level an, solange ein CPU-PC im
  Bereich liegt.
* **Zyklen-Gate** (`--log-cycle FROM:TO[:level]`): hebt das Level im Zyklenfenster an.

> **Fallstrick:** Ein `--log-pc` auf eine **Spin-Loop**-Adresse feuert, solange die
> CPU dort parkt (zig Millionen Zyklen → Multi-GB-Log). Immer mit einem engen
> `--log-cycle` koppeln — oder gleich nur ein Zyklenfenster verwenden.

---

## 4. Rezepte

```sh
# Voller, leiser Lauf bis ins geladene OS; Emulator-Log weg, nur Report:
./build_trace/boot_trace -L /tmp/emu.log disks/cpadisk01.img

# Wo bleibt die DMA-Kette stehen? (Standard-Diagnose)
./build_trace/boot_trace -c 3000000 -L /tmp/emu.log disks/cpadisk.img
#   → meldet ZVE2-Einfrierpunkt, [03F8]-Done-Flag-Verlauf, Boot reached: YES/NO

# Post-Boot: Bildschirm-Banner + Port-/VRAM-Histogramm:
./build_trace/boot_trace -p 9000000 disks/cpadisk01.img

# TRACE nur im 3rd-Stage-Lesepfad (PC-Fenster), Rest leise:
./build_trace/boot_trace -c 40000000 -p 38000000 \
    --log-cycle 38000000:39000000:trace disks/cpadisk01.img

# ZVE2-Instruktionstrace einer Leseroutine + RAM-Dump zum Disassemblieren:
./build_trace/boot_trace -z 0x1F7D:0x2076 -W 400 \
    -d 0x1C00:0x2080 /tmp/loader.bin disks/cpadisk01.img
python3 tools/z80_disasm2.py --org 0x1C00 --entry 0x1F7D /tmp/loader.bin
```

---

## 5. Was die Zusammenfassung zeigt

Am Ende eines Laufs druckt `boot_trace`:

* **Meilensteine** des Bootpfads (ROM-Phasen, ZVE2-Start, Completion `[03F8]=3`,
  Sprung ins geladene Code bei `0x0437`).
* **`Boot reached: YES/NO`** — ob ZVE1 in geladenen Code (PC ≥ 0x0400) gelangt ist.
* **Done-Flag-Verlauf** `[03F8]` (0=läuft, 1=ISR-Timeout, 3=fertig).
* **PC-Histogramme** für ZVE1 und ZVE2 (wo wird Zeit verbracht / wo eingefroren).
* bei `-p`: **I/O-Port-Histogramm**, **VRAM-Schreibzähler + -Bereich**, PC-Histogramm
  des geladenen Codes und ein **80×24-Textdump** des VRAM (`0xF800`) — das
  Bildschirm-Banner wird sichtbar.

Hintergrund zum Bootpfad, zur ZVE1↔ZVE2-Handshake-Logik und zur DMA-Datenflusss
im Detail: `doc/analyse_zre_rom_boot.md`, `doc/K1520_architecture.md` §8.5/§14.x.
