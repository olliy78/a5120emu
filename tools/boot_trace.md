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
| `--raf <typ>` | RAM-Floppy (`raf128`\|`raf512`\|`raf2m`\|`none`) vor dem ersten Lauf stecken, an allen Maschinen (§10) |
| `--ptape` | Lochstreifen-Karte K6022 (SIF1000, E0H–E7H) vor dem ersten Lauf stecken, an allen Maschinen (§10a) |
| `--until <cond>` | **anhalten, sobald `<cond>` gilt** (läuft über den Boot-Handoff hinaus bis zur Bedingung oder zum `-c`-Limit), dann Report (§3) |
| `--coverage [file]` | **Code-Coverage**: ausgeführte ZVE1-Byte-Ranges + ZVE2-Adresszahl; mit `file` zusätzlich CSV `cpu,pc,hits` (§4) |
| `--diff a.csv b.csv` | **Run-Diff** zweier `--coverage`-CSVs (nur-A/nur-B/hit-diff je CPU) — **ohne** Emulation (§4) |
| `--csv <file>` | **maschinenlesbarer Per-Instruktions-Trace** (`seq,cyc,cpu,pc,bytes,disasm,…regs`); durch `-w`/`-z`/`--until` eingrenzbar, Cap 5 Mio. Zeilen (§4) |
| `--save-state <file>` | am Lauf-Ende (z. B. mit `--until`) den **Maschinenzustand** sichern (Checkpoint) |
| `--load-state <file>` | mit gesichertem Zustand **starten** statt zu booten (RAM+CPU+ROM-Mapping + Tastatur-Subsystem + Floppy-K5122/Kopfposition reproduziert; gemountete Images/VRAM nicht) |
| `--json` | am Ende **eine JSON-Zeile** (`boot_reached,cycles,rom_enabled,final_pc,zve1_addrs,zve2_addrs,zve2_instr,until{set,met,cycle,pc}`) |
| `--quiet` | **unterdrückt die menschliche Narrative** (Banner, Progress, Milestones, Summary, Histogramme, VRAM). Es bleiben nur `--coverage`/`--json`/`-d` und Warnungen/Fehler — `--quiet --json` = genau 1 Zeile statt ~880 |
| `--drive <n>` | Disk auf Laufwerk `n` mounten (Default 0 = A:) |
| `-L <datei>` | den (ausführlichen) **Emulator-Log** umleiten, damit der Report lesbar bleibt (`-L /dev/null` verwirft ihn) |
| `--log-level <off…trace>` · `--log-pc LO:HI[:lvl]` · `--log-cycle FROM:TO[:lvl]` | Logger-Basislevel und **Gates** (§5) |
| `-s` / `-n <anzahl>` | jede ZVE1-ROM- & ZVE2-Instruktion einzeln (disassembliert) / Single-Step-Limit |
| `-v` | **alle** ZVE2-Instruktionen (statt nur der ersten ~600) |
| `-w LO:HI` / `-z LO:HI` / `-W <n>` | ZVE1- / ZVE2-Instruktionsfenster tracen (disassembliert) / Zeilencap |
| `--fold` | **Schleifen-Kollaps** für `-w`/`-z`: erkennt online einen sich wiederholenden **PC-Zyklus** (Periode ≤ 32, nur PC — Register egal) und fasst weitere Durchläufe zu `↻ loop @A period=P ×N` zusammen. Der Loop-Rumpf wird bei den ersten zwei Durchläufen (mit echten Registern) gedruckt, danach gefaltet. Zerschlägt sowohl Idle-/Poll-Spins **als auch registerändernde Hot-Loops** (IDAM-Matcher, Delay-Counter — der 12 000-Zeilen-Fall). |
| `--itrace <file>` | **Interrupt-Trace**: jede angenommene INT/NMI als CSV-Zeile (`seq,cyc,kind,int_pc,isr_pc,sp,vector,device`). `vector`/`device` kommen aus der Bus-Quittung; `device=SPURIOUS` heißt **kein Gerät hat geantwortet** (der 0xFF ist der offene Bus, kein programmierter Vektor) — genau die Verwechslung, an der ein Fremd-OS-Interruptsturm hängt. Für Interrupt-Timing-Bugs gegen ein `-w`/`--log-cycle`-Fenster korrelieren. |
| `-d LO:HI [datei]` | RAM-Bereich am Ende dumpen (optional in Datei) |
| `--watch a,b,…` / `--watchio p[:zve1\|zve2],…` | Schreibzugriffe auf RAM-Adressen / I/O-Ports mitloggen — **ab Takt 0** (also inkl. Boot-ROM; die Zahl stimmt jetzt mit dem Port-Histogramm überein) und mit **`von=ZVE1/ZVE2`** (Bus-Master, nicht raten). Optionaler CPU-Filter je Port: `--watchio 0x11:zve2` |
| `--em [em256\|em064]` | **A5120.16**: Maschine mit Erweiterungsmodul (Vorgabe `em256`); **jede Kommunikationstransaktion** der Karte wird eine Zeile `[#n EM cZYKLUS] …` — U880-Zugriffe auf PIO A32, A36 (Status-8), A34 (Vektor-8 → VI), A35 (lesen), A22 (je Seite), U8001-Zugriffe auf A33/A35 und Status-8, VI-/NVI-/NMI-Quittungen sowie die Pegelwechsel Moduswechsel (FF A29), INT-16, TREN, BUSRQ16, BUSAK16, RESET16, NVI, STOP. Zusammenfassung und `--json` (`em{mode,events,u8001_pc,u8001_instr}`) nennen den Endstand |
| `--cpu u8000` | **jeden U8001-Befehl** als Zeile `[#n U8 cZYKLUS] <<seg>>%off BEFEHL FCW=… R0..R3 SP` (disassembliert über die Segmentweiche, Deckel `-W`); impliziert `--em`. Am sinnvollsten ab einem Zustand im 16-Bit-Lauf (`--load-state`, z. B. aus `k1520dbg … bmode 16 … savestate`) |
| `-l <f.prn>[@off]` | `.prn`-Listing → Trace-Zeilen & Histogramme mit kommentiertem Original-Quelltext annotieren (wiederholbar, §4) |
| Mount-Modus | **Default = Copy-on-Write** (Disk wird in Temp kopiert, nur die Kopie gemountet, Writes verworfen, Temp beim Beenden gelöscht → committetes Fixture strukturell sicher, kein `mktemp`-Ritual). `--rw` = Original schreibend · `--read-only`/`--ro` = schreibgeschützt · `--cow` = COW explizit |

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
boot_trace --until 'screen ~ "A>"'      disk.img      # bis der Bildschirm den Prompt zeigt
boot_trace --until 'screen ~ /V 1\.[0-9]/' disk.img   # … oder ein Regex-Muster
```

Bedingungen (pro ZVE1-Instruktion geprüft): `PC<op>A`, `[A]<op>V`, `[A]w<op>V` (16-Bit LE),
`<op> ∈ == != < > <= >=`; `A`/`V` base-0. Zusätzlich `screen ~ "text"` / `screen ~ /regex/`
— hält an, sobald der 80×24-Text-VRAM (ab `0xF800`) das Muster enthält (Substring bzw.
ECMAScript-Regex); der Bildschirm wird dabei nur periodisch (~alle 64k Zyklen) gerendert.
Am Ende: `MET at cycle … (PC=…)` bzw. `not met`.
Kombinierbar mit `-d`/`-w`/`-z`/`--coverage`/`--csv`, um genau im erreichten Zustand zu
dumpen/tracen, und mit `--save-state`, um dort einen Checkpoint zu setzen.

---

## 4. Maschinenlesbare Ausgaben: `.prn`, Coverage, Diff, CSV

**Quell-Annotation (`-l`).** Hängt an jede Trace-Zeile und jeden PC im Histogramm die
kommentierte Original-Quellzeile an (`-l bios.prn`). Wiederholbar; Offset `@OFFSET`
reloziert die Adressen (Details wie bei `k1520dbg`, dort §6). Neben `.prn`-Listings werden
auch **Fremdquellen** `.MAC`/`.ASM`/`.z80`/`.src` angenommen: sie werden von
`tools/mac_listing.h` assembliert (Opcode-Längen, `ORG`/`EQU`/`DB`/`DW`/`DS`, `Mxxxx`-
Adressanker) und ergeben dieselbe Tabelle — s. `k1520dbg.md` §6.1. Den Ladeversatz
selbst bestimmen (`@auto`) kann nur `k1520dbg` (dazu muss die Maschine schon laufen).

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

**K5122-Read-Attempt-Log** (Diagnose „warum scheitert ein Read?"): Die Floppy-Karte
protokolliert jeden Lese-Transfer strukturiert — kein Handdekodieren des ZVE2-Matchers mehr.

* `--log-level info` → je Read ein Einzeiler: `>>> READ D0 C=4 H=1 … 26 Sekt, 0 CRC-Fehler`
  (Kopfposition + Sektorzahl + CRC-Gesundheit auf einen Blick).
* `--log-level debug` (nur im **Trace-Build**, `LOG_LEVEL=5`) → zusätzlich je Adressmarke:
  `RD-ID[i] cyl= head= sec= size= id_crc=OK data_crc=OK`. Gezielt boosten mit
  `--log-cycle <read-fenster>:debug`.

Damit sieht man in **einem** Lauf: Kopf richtig (C/H), alle Sektor-IDs, CRC gültig → wenn der
Read trotzdem scheitert, liegt der Fehler **CPU-seitig** (ZVE2-Matcher/Handshake), nicht am
Medium. *Welchen* Sektor das OS sucht (das Soll), kennt nur ZVE2 → dafür `k1520dbg b2 <matcher>`.

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

## 7. K8915 (`--machine k8915`)

Seit AP-E4d (2026-09-29) fährt `boot_trace` auch den K8915 — in einem eigenen Zweig
(`tools/boot_trace_k8915.cpp`), weil vom A5120-Bericht (ZVE2, `[03F8]`, Meilensteine des
A5120-ROMs) nichts passt. Ablauf: Netz-Ein → Selbsttest → Coldstart-Meldung → **CR wird
selbst getippt** → Lader → SCPX → Abbruch am **stabilen Prompt**.

```sh
tools/dev.sh trace --machine k8915 --skip-selftest tests/fixtures/disks/k8915scpx_boot1.hfe \
    -l doc/EPROMS/K8915/k8915_zre.prn -l doc/EPROMS/K8915/k8915_zre.prn@0xF000:00D0-03FF \
    -l doc/EPROMS/K8915/scpx8915_v53_bios.prn
tools/dev.sh trace --machine k8915 --quiet --json DISK      # eine Zeile, Exit 0 = Prompt
```

| Option | Wirkung |
|---|---|
| `--machine k8915` | K8915 statt A5120; `-c` ist dann 250 Mio. Takte (voller Boot mit Selbsttest ≈ 84 Mio., ≈ 7 s) |
| `--skip-selftest` | wie in k1520dbg: `JP` bei 0000H/0005H, kein Selbsttest |
| `--no-cr` | nach „* Coldstart *“ **kein** CR tippen (der Lader wartet dann) |
| `--stall <takte>` | Stillstand = so lange weder Bildänderung noch Schreibzugriff auf 10H–18H/61H/A8H (Vorgabe 30 Mio.) → Abbruch, Exit 1 |
| `--events <datei>` / `--events-cap <n>` | Ereignisprotokoll in eine Datei statt nach stderr; höchstens n Zeilen (Vorgabe 3000) |
| weiterhin | `-c -l --until --quiet --json --coverage --csv --itrace -w -W -d --watch --watchio --drive -L --log-* --rw/--ro/--cow` |
| wirkungslos (Warnung) | `-s -v -p -z --fold --save-state --load-state`, `--watchio …:zve2` |

**Ereignisprotokoll** (`[ev c<takt> PC=…]`, PC = der zugreifende Befehl): jeder Zugriff
auf die K5122 (10H–18H, mit Portnamen), das Anzeigefeld 61H (mit den leuchtenden Lampen),
das Speicherregister A8H (mit dem entstehenden Speicherbild `map=R...1111…`: je 4-KB-Seite
`R` ROM, `1`/`2` Bank, `.` Bus), jeder angenommene Interrupt mit Vektor, Quellbaustein
und ISR. Gleiche aufeinanderfolgende Ereignisse werden gefaltet (`×N`); an den
Datenports 14H/16H zählen dabei weder Wert noch PC (der Lese-ISR liest ausgerollt), der
PC-Bereich steht dann als `PC=E965..E9A0` da. Die Listing-Annotation wird beim Entstehen
des Ereignisses festgehalten.

**PC-Histogramm** mit Kennung `ROM` (Befehl kam aus dem Boot-ROM — 0100H im ROM und
0100H im TPA sind verschiedener Code), nächstem Listing-Label (`<L055E+2>`, auch aus
allein stehenden Labelzeilen) und Listingzeile, beide beim ersten Auftreten festgehalten.
Dazu Portstatistik, Endzustand (A8H/Speicherbild, 61H) und das Bild der K7024.

**Abbruch/Exit:** Prompt = letzte Bildzeile `X>` **und** Bild 2 Mio. Takte unverändert
(sonst hielte schon das `A>` vor dem Autostart `rade`) → 0; `--until` erfüllt → 0 /
nicht erfüllt → 2; Stillstand oder `-c` → 1. `--json`: `machine, prompt, stall, cycles,
final_pc, a8, lamps, cpu_addrs, instr, events, ints, until{…}`.
Wächter: `cli_bt_k8915_prompt`, `cli_bt_k8915_events`, `cli_bt_k8915_stillstand`.

**Generation 2 (`--machine k8915-g2`, AP-V6b):** gleicher Zweig, Kopf „K8915 Gen 2 Boot Trace“,
`--json` mit `"machine":"k8915-g2"`, `map=` mit `Z`/`M`/`.` (ZRE/K3528-RAM/Bus). Zusätzlich tippt
das Werkzeug `CR` **bei einem Selbsttestfehler** (ERROR-Lampe 61H, ≥ 16 × BEL, Buchstabe in 1776H;
Meldung „Selbsttestfehler <Test> <Buchstabe> → CR getippt“; mit der Vorgabe — repariertes 177, F9 —
läuft der Selbsttest fehlerfrei durch, das Tippen bleibt dann untätig) und
wie am V3 nach „\* Coldstart \*“; `--no-cr` schaltet beides ab. `--skip-selftest` gibt es nicht
(Warnung). Annotation: `-l k8915g2_zre.prn@0xFC00:0021-03FF -l k8915g2_zre.prn@:0400-0BFF`.
Ohne Diskette: `--until 'screen ~ "Coldstart"'` (sonst Stillstand nach dem Lader-Versuch).
Wächter: `cli_bt_k8915g2_coldstart`, `cli_bt_k8915g2_prompt` (901 bis `A>`, A8H = 87H).


## 8. PRG 710 / 710-1 (`--machine prg710|prg710-1`)

Seit AP-P1d (2026-10-02, `doc/design/20_prg710.md`) fährt `boot_trace` auch beide
Numerik-Geräte (`tools/boot_trace_prg710.cpp`, Optionen wie beim K8915). Ziel der
Grundform ist Etappe 1: Netz-Ein → „NKM-LOADER“ → Laufwerke → **Tastaturabfrage**.
Seit AP-P5c gibt es `--keys` und den UDOS-Prompt als Ziel.

```sh
tools/dev.sh trace --machine prg710                 # ohne Diskette bis zur 8279-Abfrage
tools/dev.sh trace --machine prg710-1 --quiet --json
tools/dev.sh trace --machine prg710 --events /tmp/ev.txt --events-cap 100000 DISK
```

- **Ereignisprotokoll**: K5122 (10H–18H), ZRE-CTC (80H–83H), Speicherverwaltung E8H–EBH
  mit der Seite aus **A12–A15 der E/A-Adresse** (`[Seite F]`) und dem Speicherbild nach
  dem Schreiben (`map=Z123456789ABCDEV`: je 4-KB-Seite `Z` ZRE, `V` Seite F mit VRAM,
  Ziffer = physische OPS-Seite, `-` leer), Tastatur (710: 8279 C8H/C9H; 710-1: CTC 58H,
  SIO A32-B 5EH/5FH), Interrupts. Faltung wie beim K8915.
- **PC-Histogramm** mit Kennung `ZRE` (Befehl aus ROM/ZRE-RAM, d. h. vor der Umschaltung
  auf die RAM-Kopie), Portstatistik, Endzustand der Speicherverwaltung, Bild der K7024.
- **Abbruch/Exit:** Stillstand (`--stall`, Vorgabe hier 5 Mio. Takte ohne Bildänderung
  und ohne Schreibzugriff auf einen beobachteten Port) **in der Tastaturabfrage** des ROMs
  (710: 007DH/0160H, 710-1: 0070H/0163H, aus der RAM-Kopie) → 0, sonst 1; `--until` → 0/2;
  `-c` Vorgabe 50 Mio. `--json`: `machine, keywait, stall, cycles, final_pc, map, instr,
  events, ints, until{…}`.
- **`--keys "<text>"`** (AP-P5c): Tasten, `<ET>` = ET1 (710: `QK_TASTE_BASE|37H` an der K7609) bzw.
  Return (710-1: K7672 → 0DH). Der Text wird in Blöcken getippt (Block = bis einschließlich
  `<ET>`), jeweils sobald die Maschine steht (Stillstand): z. B. `--keys "<ET>021086<ET>"` =
  Starttaste, Datum + ET. `-c` ist mit `--keys` 250 Mio.; Abbruch: **`%` als letzte Bildzeile
  im Stillstand** → Exit 0, JSON-Feld `"prompt"` (hinter `"stall"`).
- `--coverage --csv --itrace --watch` (Speicher-Schreibzugriffe der CPU) wirken wie am K8915;
  `--skip-selftest --no-cr` gibt es nicht (Warnung).
  Mit Diskette wird über `defaultFormatName` gemountet (bei `.hfe` nur Platzhalter).

Wächter: `cli_bt_prg710_tastatur`, `cli_bt_prg710-1_tastatur`, `cli_bt_prg710_udos_prompt`, `cli_bt_prg710-1_udos_prompt`.

## 9. PC 1715 (`--machine pc1715`)

Grundform aus AP-2 (`doc/design/21_pc1715.md`), ausgebaut in AP-4b. Eine CPU, Floppy =
K5122 in der Konfiguration „1715“ (`/WAIT`), Bild über den 8275 aus dem Haupt-RAM.

```sh
tools/dev.sh tool boot_trace --machine pc1715 tests/fixtures/disks/pc1715_scp1715_v0006_boot.hfe
tools/dev.sh tool boot_trace --machine pc1715 --quiet --json <abbild>   # {"prompt":true,…}
```

- Diskette in Laufwerk 0 (`--drive n` für ein anderes), COW-Mount wie üblich.
- Protokolliert: FD-PIOs 00H–07H (Datenport 00H/02H ohne Wert gefaltet), SE-/MO-Register
  20H/21H, 8275 18H/19H, ROM ein/aus 24H–2BH, BWS-Register 34H, Interrupts (mit IM).
- Abbruch bei Stillstand (Vorgabe 10 Mio. Takte ohne Bildänderung/Steuerzugriff), `-c`
  (Vorgabe 60 Mio.) oder `--until`. Exit 0, wenn beim Stillstand eine Zeile `A>`…`P>`
  bzw. `%` im Bild steht. Das Bild zeigt 25 Zeilen (CP/A-Statuszeile).
- Ein CP/A 1715 hat eine laufende Uhr in der Statuszeile — dort gibt es keinen
  Stillstand (Exit 1 an der Taktgrenze, Prompt im Bild ablesen). Vorsicht mit
  `--until 'screen ~ "A>"'`: die Statuszeile („A0\A> …“) trifft schon vorher.
- **`--keys 'dir<ET>'`** (AP-4b): tippt über die Tastatur1715 (U880 + S600); `<ET>` = Return.
  Blockweise wie beim PRG: ein Block endet mit `<ET>` und wird getippt, sobald die Maschine
  steht. Je Taste 150 000 Takte halten + 100 000 Pause (5 000-Takt-Pakete, Entprellung des
  Tastatur-ROMs). Zusammenfassung: `Tasten: n von m getippt`.
- Weiter wie beim PRG: `--csv`, `--itrace`, `--coverage [--coverage-csv]`, `-w/-W`, `--watch`,
  `--watchio`, `-d`, `--events[-cap]`, `-l …/s502.prn`. Das 34H-Ereignis nennt Bildbasis und
  ZG, 24H–2BH „Overlay an/aus“; das Schlussbild zeigt ROM/BWS (das PRG-`map`).
- Wächter: `bt_pc1715_scp_prompt.cli`, `bt_pc1715_tastatur.cli` (`dir` → INSTSCP).
- Nicht vorhanden: Savestates, ZVE2-Schalter.

**`--machine pc1715w`** (AP-W3): dieselbe Grundform für den PC 1715W — Urlader S550, U8272 +
UA858, Bankregister. Portnamen nach `doc/pc1715/pc1715w_hardware.md` §1 (DMA 00H, CTC2 04H,
U8272 1CH/1DH, KRFD 20H, BR 24H, MOS 28H, KON 34H); **24H zählt nicht als Steuerzugriff** (das
BIOS schaltet die Bank bei jedem Aufruf, auch im Leerlauf — sonst gäbe es keinen Stillstand).
Das Schlussbild zeigt `BR`/`KRFD`/`MOS`/U8272-MSR/DMA statt ROM/BWS; JSON `"machine":"pc1715w"`.
Wächter `bt_pc1715w_scp30_prompt.cli` (SCP 3.0 bis `A>`).

## 9a. P8000 (`--machine p8000`)

Grundform aus AP P7c (`doc/design/25_p8000.md`): **nur die 8-Bit-Seite** (U880, MON8 3.1, Floppy
X8/X9, Kern-Terminal an tty1); 16-Bit-Teil und WDC folgen mit P10/P13.

```sh
tools/dev.sh tool boot_trace --machine p8000 tests/fixtures/disks/udosP8000_640k_wega.hfe
tools/dev.sh tool boot_trace --machine p8000 --quiet --json --keys '<CR><CR>' \
    --until 'screen ~ "Hardware Error in Connection"' <abbild>
```

- Ohne `--keys` läuft die Maschine durch den Hardwaretest (≈ 64 Mio. Takte = 16 s Maschinenzeit)
  bis „U880-Softwaremonitor Version 3.1 - Press RETURN" und steht dann (Stillstand, Exit 0).
  `--keys '<CR><CR>'`: erstes Return ⇒ `>`, zweites ⇒ BOOT (UDOS bis `%`, ≈ 110 Mio. Takte).
  `<CR>` und `<ET>` sind gleichwertig; blockweise wie beim PRG, bei Stillstand getippt, der Block
  wird auf einmal eingereiht (der Terminal-Anschluss liefert im Zeichentakt).
- Protokolliert: ADP/RFF 00H–07H, Latches 10H–17H, PIO2 (Floppy-Port) 1CH–1FH, U8272 20H/21H
  (Datentor 21H ohne Wert gefaltet), UA858-DMA 3CH–3FH, Interrupts. Das Schlussbild ist das
  Terminal (80 × 24).
- Abbruch: Stillstand (Vorgabe **20 Mio.** Takte ohne Terminaländerung/Steuerzugriff), `-c`
  (Vorgabe **400 Mio.**), `--until`. Exit 0, wenn beim Stillstand die Cursorzeile `>`/`%` lautet
  oder „Press RETURN" im Bild steht.
- Weiter wie beim PC 1715: `--csv`, `--itrace`, `--coverage`, `-w/-W`, `--watch`, `--watchio`,
  `-d`, `--events[-cap]`, `-l`. `--raf`/`--ptape` werden mit Warnung ignoriert.
- **`--machine p8000-16`** (AP P11): dasselbe mit 16-Bit-Karte und Kopplung (`karte16`);
  das Schlussbild nennt zusätzlich den U8001 (Reset/läuft, PC, FCW, Uhr).  Beispiel:
  `--machine p8000-16 --keys '<CR>x<CR>'` ⇒ „U8000-Softwaremonitor Version 3.1 - Press NMI".
- **`--wdc 4.2|4.0.05|3.4.05`, `--hd <abbild>`** (nur `p8000-16`, AP P13d): WDC an der 16-Bit-PIO2 bzw.
  Winchester an Laufwerk 0 (`--hd` setzt `--wdc 4.2`; das Abbild wird **beschrieben** — eine Kopie
  übergeben).  Ereignisse: je Statuswechsel des WDC, „WDC Kommando cc LW n …" beim Übernehmen eines
  Kommandoblocks, „WDC Fehler nn" bei Status 7; das Schlussbild nennt Zustand, Kommando- und Fehlerzahl.
- Noch nicht: `--p8000 <konfig>`, Ereignisse des 16-Bit-Teils.  Für alles Interaktive
  (aktive CPU, MMU-Sicht, DMA-/Kopplungsprotokoll) gibt es seit P12 `k1520dbg --machine p8000|p8000-16`
  (`tools/k1520dbg.md` §11d) — `boot_trace` lokalisiert, `k1520dbg` seziert.
- Wächter: `bt_p8000_banner.cli`, `bt_p8000_tastatur.cli`, `bt_p8000_16_monitor.cli`.

## 10. RAM-Floppy (`--raf`)

`--raf raf512` (auch `raf128`, `raf2m`) steckt die Karte auf 88H/89H, bevor die Maschine
läuft — gleichermaßen am A5120 (auch mit `--em`), `--machine k8915` und `--machine prg710[-1]`.
So lässt sich ein Treiber-/Boot-Lauf mit RAM-Floppe verfolgen (`--watchio 0x88,0x89`);
Inhalt ansehen nach dem Lauf im Debugger (`k1520dbg --raf …`, Befehl `raf`, §11c dort).
Unbekannter Typ → Exit 2. Wächter: `cli_bt_raf`, `cli_bt_raf_k8915`, `cli_bt_raf_typ`.

## 10a. Lochstreifen (`--ptape`)

`--ptape` steckt die ADA K6022 (Lochbandstanzer E0H–E3H, -leser E4H–E7H;
`doc/design/23_lochstreifen.md`) vor dem ersten Lauf — an jeder Maschine, auch am
PRG 710/710-1, der sie seit AP-L1 **nicht mehr fest** trägt.  Ohne `--ptape` antwortet
auf E0H–E7H niemand (FFH).  Ein Band einlegen kann `boot_trace` nicht; zum Verfolgen
eines Treibers `--watchio 0xE0,0xE4`.

### P8000-Originalterminal (AP P20c/P20d)

- `--machine p8000 --konsole original` (auch `p8000-16`): tty1 am Originalterminal Typ 2 + K7673.09 statt am
  Kern-Terminal; `--keys` tippt mit Haltezeit über die K7673 (≈ 640 000 Takte je Zeichen), der Bericht zeigt die
  Terminalzeile des Originals.
- `--machine p8000-terminal`: das Terminal ohne Rechner — Netz-Ein bis zur Einschaltmeldung („ADM31/9600 baud…"),
  300 ms warten, `--keys` (Zeichen, `<CR>`) über die K7673, danach Bild, Cursor/LEDs/Klingel und die Bytes, die das
  Terminal auf der Leitung XB5 gesendet hat (beginnt mit dem 00H des Starts).  `-c` = Grenze in Z8-Takten
  (Vorgabe 2 s = 7 372 800), `--json` mit `meldung`, `keys_typed`, `sent`, `row0`.  Exit 0 = Meldung da, alle
  Tasten getippt.  Wächter `bt_p8000_terminal`, `bt_p8000_konsole_original`.
