# Fehlende Features — `k1520dbg` & `boot_trace`

Stand 2026-06-21. Gemessen an dem, was man von einem gdb-artigen Debugger plus einem
Instruktions-Tracer erwartet — und an dem, was der **Emulator-Kontext** besonders billig
macht. Reihenfolge = grob nach Hebelwirkung. Erledigte Punkte sind als ✅ markiert.

> Hinweis: Vorhandene Stärken sind in `tools/k1520dbg.md` / `tools/boot_trace.md`
> dokumentiert; diese Datei listet bewusst nur die **Lücken**.

---

## Tier 1 — höchster Hebel, beim Emulator fast geschenkt

Adressiert den Hauptschmerz: jeder Versuch = ~8 s Reboot, Analyse aus RAM-Dumps.

1. **✅ Snapshots / Reverse-Step** *(implementiert 2026-06-21)*
   - `snap <name>` / `restore <name>` / `snap list` — benannte Voll-Snapshots.
   - `rs [N]` — Reverse-Step: macht die letzten `N` Vorwärts-Kommandos rückgängig
     (automatischer Snapshot-Ring vor jedem `g`/`s`/`n`/…).
   - **Grenze v1:** Snapshot = 64 KB RAM + beide Z80-Registersätze + Run-Loop-Flags.
     Geräte-Interna (CTC/PIO/SIO-Zähler, K5122-Kopfposition) und das ROM-Mapping sind
     **nicht** enthalten → exakt für CPU+RAM (interaktives Stepping in geladenem Code),
     aber `restore` mitten in einer aktiven DMA-/Timer-Phase kann beim *Weiterlaufen*
     driften. Siehe `A5120Machine::captureState/restoreState`.
   - **Noch offen:** echtes `rc` (reverse-continue bis Breakpoint rückwärts);
     voller Geräte-Snapshot (Core-Serialisierung aller Karten).

2. **✅ History-Backtrace** *(implementiert 2026-06-21)*
   - `bt` nutzt jetzt einen mitlaufenden, **exakten** CALL/RST/RET-Aufrufstapel statt
     des heuristischen Stack-Scans (`bt scan` erzwingt weiter den alten Scan).
   - **Grenze v1:** Interrupt-Frames (IM2-Vektor/NMI) werden nicht als Frame erfasst;
     gewöhnliche CALL/RET-Verschachtelung ist exakt, ISR-Eintritte erscheinen (noch)
     nicht im Stack (korrumpieren ihn aber auch nicht). Tracker: `tools/callstack_tracker.h`.
   - **Noch offen:** Interrupt-/RETI-Frames; Call/Return-Trace mit Einrückung.

3. **✅ Logpoints / „tracen ohne Anhalten"** *(implementiert 2026-06-21)*
   - `trace <file> [lo hi]` / `trace off` — schreibt **jede** ausgeführte Instruktion
     (ZVE1 *und* ZVE2, lückenlos über DMA) disassembliert + `.prn`-annotiert in eine
     Datei; optionales PC-Fenster, Sicherheits-Cap (2 M Zeilen).
   - `logpoint <A> [expr..]` (`lp`) / `lpd <A>` / `lpl` — gdb `dprintf`: am PC drucken
     (PC + Disasm + ausgewertete Ausdrücke) und **weiterlaufen**, nie anhalten.
   - **Noch offen:** `commands`-Blöcke an Breakpoints; ZVE2-Logpoints (nur ZVE1).

---

## Tier 2 — Debugger-Grundausstattung, die noch fehlt

4. **✅ Reichere Watchpoints** *(implementiert 2026-06-21)*
   - **Wert-Watchpoint**: `wp/wpr/wb <A> [== NN | != NN | changed]` — feuert nur bei
     passendem Byte bzw. echter Wertänderung.
   - **Bereichs-Watchpoint**: `wp/wpr/wb <A..B>` überwacht eine ganze Region.
   - `wl` zeigt Trefferzähler, `wd <A>` löscht alle den Punkt abdeckenden Watches, `wd all`.
   - Auslösende CPU (`ZVE1`/`ZVE2.PC`) war bereits via `busWho()` korrekt.

5. **✅ Breakpoint-Verwaltung** *(implementiert 2026-06-21)*
   - `be <A>` / `bdis <A>` (ZVE2: `be2`/`bdis2`) — Breakpoint aktivieren/deaktivieren
     (behalten, aber inaktiv) statt löschen.
   - `bi <A> <N>` (ZVE2: `bi2`) — die nächsten `N` Treffer ignorieren (gdb `ignore`).
   - `bl` zeigt `[disabled]` + `ignore=N`.

6. **✅ Bessere Daten-Inspektion** *(implementiert 2026-06-21)*
   - gdb-artiges **`x/<count><fmt><size> <addr>`**: Formate `x d u c t o a i s`,
     Größen `b`/`w` (LE); `i` disassembliert (+`.prn`), `s` liest NUL-Strings,
     `a` zeigt Adresse + Symbol. Format/Größe/Count persistieren wie in gdb; jede
     Zeile mit Adress-Symbol. `x` läuft ab der letzten Adresse weiter.
   - **✅ Arithmetik im Ausdrucks-Evaluator** *(nachgezogen 2026-06-21)*: vollwertiger
     rekursiv-absteigender Parser — `+ - * / %`, `& | ^`, `<< >>`, unär `- ~`, Vergleiche,
     Klammern, `[expr]`/`[expr]w` mit Ausdrucks-Index. Greift in `if`-Bedingungen, `disp`,
     `logpoint` und der `x`-Adresse.

7. **✅ Source-Sicht** *(implementiert 2026-06-21)*
   - `list`/`l [A] [N]` — zeigt die `.prn`-Quellzeilen *um* `A` (Default: PC bzw.
     weiter ab letzter Position), markiert die deckende Zeile mit `=>`.
   - **`.prn`-Labels werden beim `-l`/`lst`-Laden automatisch als Symbole importiert**
     (ohne bestehende `-s`-/User-Symbole zu überschreiben) → `b BIOS27`, `list BIOS27`,
     `u BIOS27` direkt nutzbar; `bl`/Disasm zeigen die Namen.

---

## Tier 3 — hardware-spezifisch, hoher Wert für genau dieses Projekt

8. **✅ Break-on-Interrupt / NMI / RETI** *(implementiert 2026-06-21)*
   - `bint` / `bnmi` / `breti` `[on|off]` — anhalten bei Annahme eines Interrupts
     (IFF1 1→0 + SP-Push) bzw. NMI (Sprung `0x0066`) bzw. vor einem RETI/RETN
     (Opcode `ED 4D`/`ED 45`). Erkennung per Zustands-Signatur im Trace-Callback,
     **kein Core-Eingriff**. Trifft genau die CTC-Interrupt-/Q240-NMI-Bug-Klasse.
   - **Noch offen:** Filter auf einen bestimmten Interrupt-Vektor.

9. **✅ Zustand der anderen Chips** *(implementiert 2026-06-21)*
   - `dev ctc` (System-CTC K2526), `dev pio` (BS-PIO K2526), `dev sio`/`dev sio2`
     (K8025 Tastatur/DFÜ) — je Kanal/Port der Konfig- und Interrupt-Status inkl.
     **pending/IUS/IEI** der Daisy-Chain + IEI/IEO. Genau die Sicht, die die beiden
     CTC-Bugs sofort gezeigt hätte; kombiniert mit `bint`/`breti` (#8).
   - Core: `Z80CTC/Z80PIO/Z80SIO::debugState()` + `A5120Machine`-Accessoren
     (`ctcState`/`bsPioState`/`kbdSioState`/`dfueSioState`). Test: `Z80CTC.DebugState…`.

---

## Tier 4 — Tracer (`boot_trace`)

10. **✅ Fenster-Traces disassemblieren** *(implementiert 2026-06-21)*
    - `-s`/`-w`/`-z`/Step zeigen die Instruktion jetzt **disassembliert** (aus dem
      Live-Speicher via `tools/z80dis_min.h`, exakt auch bei selbstmodifizierendem
      Code) statt roher `b0 b1 b2`-Bytes — zusätzlich zur `.prn`-Annotation.
11. **✅ Stop-/Trace-bis-Bedingung** *(implementiert 2026-06-21)*
    - `boot_trace --until <cond>` hält den Lauf an, sobald die Bedingung gilt (läuft
      dabei über den Boot-Handoff hinaus), dann normaler Report. `cond`: `PC<op>A`,
      `[A]<op>V`, `[A]w<op>V` mit `<op> ∈ == != < > <= >=`. Mit `-d`/`-w`/`-z` kombinierbar,
      um genau im erreichten Zustand zu dumpen/tracen.
12. **Maschinenlesbarer Trace (CSV/JSON)**, **Run-Diff** (zwei Läufe vergleichen → wo
    divergieren sie), **Coverage** (welche Adressen liefen / nie).

---

## Usability (beide)

13. **Readline**: History/Pfeiltasten/Tab-Completion (heute rohes `getline`).
    **Session-Log**, **Aliases/`define`-Makros**, `source` weiterer Skripte mitten in der Sitzung.
