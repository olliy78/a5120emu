# HowTo: Debuggen & Tracen der A5120/K1520-Emulation

Praxis-Anleitung zu den beiden Werkzeugen **`boot_trace`** (nicht-interaktiver Boot-/
DMA-Tracer) und **`k1520dbg`** (interaktiver gdb-artiger Debugger). Vollständige
Referenzen: `tools/boot_trace.md` und `tools/k1520dbg.md`. Dieses Dokument zeigt anhand
realer Aufgaben, *welches* Werkzeug man wann und *wie* einsetzt.

Alle Beispiele laufen über `tools/dev.sh` oder direkt gegen ein gebautes Binary in
`build/`. Pfad-Platzhalter: `DISK` = ein A5120-Image (`.img`/`.hfe`), `BIOS` =
`~/projects/CPA_Workbench/build/bios.prn` (kommentiertes BIOS-Listing).

> ⚠️ **Schreib-Korruption vermeiden.** Beide Tools mounten die Disk **nicht**
> schreibgeschützt — ein Lauf kann das Image verändern/zerstören. Arbeite gegen eine
> **Kopie**, nie gegen ein committetes Fixture:
> ```sh
> D=$(mktemp --suffix=.img); cp DISK $D;  boot_trace … $D;  rm -f $D
> ```

---

## 0. Welches Werkzeug wann?

| Frage / Aufgabe | Werkzeug |
|-----------------|----------|
| „Wo bleibt der Boot / die DMA-Kette stehen?" | **boot_trace** (Standard-Diagnose) |
| „Welcher Code lief überhaupt? Zwei Läufe vergleichen?" | **boot_trace** `--coverage` / `--diff` |
| „Ich brauche einen reproduzierbaren Report / maschinenlesbare Fakten." | **boot_trace** `--json` / `--csv` |
| „Halte *hier* an, zeig mir Register/Speicher, lass mich steppen." | **k1520dbg** (Breakpoints, `s`/`n`, `r`/`x`) |
| „Ich war einen Schritt zu weit." / „Wie kam ich hierher?" | **k1520dbg** `rs` / `bt` |
| „Bedingt anhalten, Watchpoint, Logpoint." | **k1520dbg** `b … if`, `wp`/`wb`, `logpoint` |
| „Interrupt-/Uhr-Problem, Chip-Zustand." | **k1520dbg** `bint`/`breti` + `dev ctc/pio/sio` |

Faustregel: **boot_trace lokalisiert die grobe Phase → k1520dbg seziert sie.**

---

## 1. Schnellstart

```sh
# boot_trace: leiser Volllauf bis ins geladene OS, nur der Report
boot_trace -L /dev/null $D

# k1520dbg: am ersten interessanten PC anhalten und Register zeigen
printf 'b 0x0437\ng\nr\nq\n' | k1520dbg $D     # 0x0437 = Boot-Handoff in den Loader
```

`k1520dbg` liest Kommandos aus einer Pipe (ein Schuss) oder interaktiv. Der Agent/Skript-
Einsatz nutzt fast immer die Pipe bzw. `-x skript.dbg`.

---

## 2. „Wo bleibt der Boot / die DMA-Kette stehen?"

Die häufigste Diagnose. boot_trace kennt den Bootpfad und meldet den Einfrierpunkt:

```sh
boot_trace -c 3000000 -L /tmp/emu.log $D
#   → Meilensteine, [03F8]-Done-Flag-Verlauf, "Boot reached: YES/NO",
#     ZVE2-Einfrierpunkt + PC-Histogramme beider CPUs
```

Schlüssel-Adressen/-Variablen des CP/A-Boots (tauchen im Report auf):

* `[0x03F8]` Done-Flag: `0` läuft · `1` Index-Puls-ISR-Timeout · `3` ZVE2 fertig
* `0x0135–0x0139` ZVE1-Index-Wait-Spinloop · `0x0168` ZVE1-Wartet-auf-Done-Flag
* `0x01C7` Index-Puls-ISR · `0x0437` Sprung in den geladenen Loader

**Gezielt bis zu einem Zustand laufen** statt Zyklen zu raten — und dort dumpen:

```sh
boot_trace -L /dev/null --until '[0x03F8]==3' -d 0x03F0:0x0400 $D
#   stoppt, sobald ZVE2 fertig meldet; dumpt die Handshake-Variablen
```

**Nur das interessante Fenster ausführlich loggen** (sonst Multi-GB-Log):

```sh
boot_trace -c 40000000 --log-cycle 38000000:39000000:trace -L /tmp/emu.log $D
```

---

## 3. Ein geladenes Programm sezieren (k1520dbg)

Geladene Programme (CP/A, `format.com`, …) laufen nativ; man fängt sie am Einsprung ab
und misst relativ ab dort.

```text
# 1) booten, Programm starten, am TPA-Einsprung anhalten
b 0x0100                # CP/M-TPA-Einsprung
g                       # bootet das OS …
keys format\r           #   tippt den Aufruf; läuft bis 0x0100
mark                    # Null-Marke = Programmstart → alle Zeiten relativ
sym format.sym          # optionale symbolische Namen

# 2) sezieren
u                       # Disassembly ab Einsprung
disp A                  # A bei jedem Stopp zeigen
disp [D1BE]w            # interessante OS-Variable mitführen
n                       # Schritt ÜBER die BDOS-/BIOS-CALLs
b BDOS if C==0x1A       # nur bei BDOS-Funktion 1Ah anhalten (bedingt)
g
bt                      # exakter Aufruf-Backtrace: wer hat gerufen?
```

Nützlich dabei: `x/8xb HL` (Speicher ab Pointer-Register), `x/10i 0x1E40` (10 Instruktionen
disassemblieren), `wb 0x6005 == 99` (anhalten, wenn dorthin ein bestimmter Wert geschrieben
wird), `vars` (benannte CP/A-RAM-Variablen).

---

## 4. Quelltext statt rohem Disassembly (`.prn`-Listing)

Statt Maschinencode von Hand zu lesen, das passende MACRO-80-Listing laden — der Debugger
hängt die **Original-Quellzeile mit Kommentar** an und macht alle Labels zu Symbolen:

```sh
k1520dbg -l $BIOS $D
```
```text
(dbg) b BIOS27          # Label aus dem Listing direkt als Adresse (= 0xD227)
(dbg) g ; u 0xD227 1
  D227: C3 F3DD  JP $DDF3   ; BIOS27: JP read  ;oA Resultat; A=0 ok, A=1 sonst
(dbg) list read          # Quelltext-Ausschnitt um ein Label
```

Läuft der Code reloziert (andere Adresse als im Listing), Offset anhängen:
`-l stage3.prn@-0x800`. Genauso für boot_trace: `boot_trace -l $BIOS …` annotiert Trace-
Zeilen **und** PC-Histogramme. (Ein BIOS-Listing deckt nur den BIOS-Bereich ab — für andere
Stages deren eigene `.prn` dazuladen.)

---

## 5. Interrupt-/Uhr-Probleme analysieren

Die Kombination aus **Ereignis-Breakpoints** und **Chip-Zustand** ist genau auf die
CTC-Interrupt-/Q240-NMI-Bug-Klasse zugeschnitten:

```text
bint                    # anhalten, sobald ein Interrupt angenommen wird
g
** interrupt → ISR 01C7 : ZVE1 PC=01C7
dev ctc                 # CTC-Kanäle + Daisy-Chain: wer ist pending/ius, IEI/IEO?
breti ; bint off ; g    # bis zum RETI weiterlaufen → ISR-Dauer/-Verschachtelung
```

`dev ctc`/`dev pio`/`dev sio` zeigen je Kanal/Port den Konfig- und Interrupt-Status inkl.
`pending`/`ius`/`iei` und der Daisy-Chain `IEI/IEO` — die Sicht, die einen Spurious-
Interrupt-Sturm oder eine blockierte Kette sofort sichtbar macht. `bnmi` fängt Q240-
Schutzverletzungen (NMI auf `0x0066`).

---

## 6. Diskettenzugriff / ZVE2-DMA verfolgen

Sobald Floppy-I/O läuft, übernimmt **ZVE2**. Parallel ZVE2-Breakpoints setzen und den
Floppy-Port beobachten:

```text
b2 0x1F7D               # ZVE2-Leseroutine (Beispiel: 3rd-stage)
iow 0x16                # Floppy-Datenport: jeder Zugriff
g                       # läuft bis ZVE2 den Breakpoint trifft
r 2                     # beide CPUs: was tut ZVE1, was ZVE2?
dev                     # K5122-Zustand: Laufwerk, Zylinder/Kopf, READING, headPos
s2 5                    # 5 ZVE2-Instruktionen steppen
```

`s2` wirkt nur während `/BUSRQ` (aktive DMA) — sonst meldet es das. Für eine reine
Trace-Aufnahme einer ZVE2-Routine eignet sich boot_trace `-z 0x1F7D:0x2076 -W 400`.

---

## 7. „Einen Schritt zu weit" / „Wie kam ich hierher?"

Der Emulator macht Reverse-Debugging billig:

```text
g 5000000               # irgendwohin laufen
s 3                     # 3 Schritte zu weit
rs                      # ← exakt zurück auf den Zustand vor dem 's 3'
bt                      # exakter CALL/RST/RET-Backtrace (kein Stack-Raten)
snap probe              # Zwischenstand benennen
g 200000 ; restore probe   # etwas ausprobieren und exakt zurückspringen
```

Modell: ein Snapshot = RAM + beide CPUs + ROM-Mapping (exakt für CPU+RAM; keine Geräte-
Interna). Nach `rs`/`restore` baut sich der `bt`-Aufrufstapel beim Weiterlaufen neu auf
(`bt scan` geht sofort).

---

## 8. Schneller iterieren — einmal booten, oft fortsetzen

Der ~2-s-Boot ist die teuerste, ständig wiederholte Investition. **Einmal** bis zum Punkt
X laufen, Zustand auf Platte sichern, danach beliebig oft ab X fortsetzen:

```sh
# einmal: bis zur interessanten Stelle booten und Checkpoint schreiben
boot_trace -L /dev/null --until 'PC==0x0437' --save-state /tmp/ckpt.bin $D

# danach beliebig oft: ab dem Checkpoint, ohne Reboot
boot_trace -L /dev/null --load-state /tmp/ckpt.bin -p 200000 $D
```

In k1520dbg analog: `savestate /tmp/ckpt.bin` / `loadstate /tmp/ckpt.bin`. (RAM+CPU+ROM-
Mapping werden reproduziert; für Post-Boot-Code exakt.)

---

## 9. Code-Coverage & zwei Läufe vergleichen

Welcher Code lief tatsächlich — und was unterscheidet zwei Läufe (z. B. zwei Disk-Images
oder vor/nach einem Fix)?

```sh
boot_trace -L /dev/null --until 'PC==0x0437' --coverage a.csv $A_DISK
boot_trace -L /dev/null --until 'PC==0x0437' --coverage b.csv $B_DISK
boot_trace --diff a.csv b.csv      # nur-A / nur-B / hit-diff je CPU, ohne Emulation
```

Für einen **vollen** maschinenlesbaren Trace eines Fensters: `--csv trace.csv -w 0x0100:0x0140`.

---

## 10. Einsatz durch einen KI-Coding-Agenten (Scripting, token-sparsam)

Ein Agent arbeitet im **Batch** (ein Shell-Aufruf, ganze Ausgabe zurück) — kein
persistenter interaktiver Modus. Empfohlene Muster:

```sh
# boot_trace: genau eine JSON-Ergebniszeile + Exit-Code zum Verzweigen
boot_trace -L /dev/null --quiet --until '[0x03F8]==3' --json $D
#   {"boot_reached":false,"cycles":2083043,…,"until":{"set":true,"met":true,"cycle":2082933,"pc":"0x0168"}}
#   exit 0 = Bedingung erreicht, 2 = nicht erreicht

# k1520dbg: Skript fahren, Register maschinenlesbar abholen
printf 'b 0x0437\ng\nrj\nq\n' | k1520dbg $D     # rj = {"pc":"0x0437",…}
```

Bausteine für den Agenten:

* **`--quiet`** (boot_trace): unterdrückt die Narrative → nur `--json`/`--coverage`/`-d`.
* **`--json`** / **`rj`**: maschinenlesbare Fakten statt Prosa-Parsing.
* **Exit-Codes**: boot_trace `--until` 0/2, Mount-Fehler 1 → Verzweigung am `$?`.
* **`--save-state`/`loadstate`**: Boot einmal, dann ab Checkpoint iterieren.
* **`-x skript.dbg`** / Pipe: ganze k1520dbg-Sitzung in einem Aufruf.

---

## 11. Tipps & Fallstricke

* **Disk-Korruption** (s. o.): immer gegen eine Temp-Kopie laufen.
* **Log-Gate auf Spin-Loop** → Multi-GB-Log: `--log-pc` immer mit engem `--log-cycle`
  koppeln, oder nur ein Zyklenfenster nutzen.
* **Low-RAM `0x0000–0x07FF`** ist die ZVE1/ZVE2-Boot-/DMA-Region — Pokes dorthin werden
  von laufender DMA überschrieben; zum Experimentieren freien RAM (`0x6000`) nutzen.
* **Zahlenbasis**: Adressen/Zähler sind base-0 (`d 6000` = dezimal!), `e`-Pokes und
  Watch-Werte sind hex. Hex immer `0x…`/`…H` schreiben.
* **`s2`** wirkt nur während aktiver DMA (`/BUSRQ`); ZVE2 mit `b2` am DMA-Einsprung fangen.
* **Geräte-Interna nicht im Snapshot**: `restore`/`loadstate` mitten in aktiver DMA/Timer
  kann beim Weiterlaufen driften — für stabile Post-Boot-Punkte gedacht.

---

Tiefergehende Hintergründe: `tools/k1520dbg.md`, `tools/boot_trace.md`,
`doc/analyse_zre_rom_boot.md`, `doc/K1520_architecture.md`.
