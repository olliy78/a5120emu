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

3. **Logpoints / Breakpoint-Commands (tracen ohne Anhalten)**
   - gdb `dprintf` / `commands … continue`. In `k1520dbg` fehlt jeder
     „laufen lassen **und** mitschreiben"-Modus — man steppt von Hand.
   - Gewünscht: `trace on <file>` (jede ausgeführte Instruktion mit `.prn`-Annotation
     in eine Datei) bzw. `logpoint <A> <expr>` (drucken + automatisch weiterlaufen).

---

## Tier 2 — Debugger-Grundausstattung, die noch fehlt

4. **Reichere Watchpoints.** Heute bricht `wb` bei *jedem* Schreibzugriff. Es fehlen:
   - **Wert-Watchpoint** (`break when [A] changes` / `[A]==X`),
   - **Bereichs-Watchpoint** (Region statt Einzelbyte),
   - Angabe der *tatsächlich auslösenden CPU* (heute immer ZVE1-PC, auch wenn ZVE2 schrieb).

5. **Breakpoint-Verwaltung.** Fehlt: **enable/disable** (statt löschen),
   **ignore-count / „beim N-ten Treffer"** (Trefferzähler existiert schon — das
   Gegenstück fehlt).

6. **Bessere Daten-Inspektion.** Der Ausdrucks-Evaluator kann nur `REG/[mem]/(rr) OP wert`.
   Es fehlen Arithmetik und **`x/`-artige Formate** (`x/10i`, `x/8xw`, signed/word-Sicht);
   `d` ist nur Hex+ASCII. Außerdem **Symbol-/`.prn`-Auflösung im Dump** und in Sprungzielen.

7. **Source-Sicht (jetzt, wo `.prn` existiert).** Ein `list`/`l` zeigt die `.prn`-Quellzeilen
   *um* den PC (nicht nur die eine) — gdb `list`. Und: **Labels aus der `.prn` automatisch
   als Symbole importieren** (`b BIOS27` direkt nutzbar).

---

## Tier 3 — hardware-spezifisch, hoher Wert für genau dieses Projekt

8. **Break-on-Interrupt / NMI / RETI.** Angesichts der Historie (CTC-Spurious-Interrupt-
   Sturm, Q240-NMI-Schutzverletzung) wäre `break int [vec]`, `break nmi`, `break reti`
   extrem wertvoll — heute gar nicht möglich.

9. **Zustand der anderen Chips.** Es gibt nur `dev` (K5122). Es fehlt `dev ctc` / `dev pio`
   / `dev sio`: Kanalzustände, **pending/IUS/IEI** der Interrupt-Daisy-Chain. Genau das
   hätte die beiden CTC-Bugs sofort sichtbar gemacht.

---

## Tier 4 — Tracer (`boot_trace`)

10. **Fenster-Traces disassemblieren.** `-w`/`-z` drucken rohe Bytes `b0 b1 b2`. `k1520dbg`
    nutzt schon `z80dis_min.h` — denselben Decoder in die Fenster-Zeilen ziehen (zusätzlich
    zur `.prn`-Annotation) wäre klein und nützlich.
11. **Stop-/Trace-bis-Bedingung.** `boot_trace` hat Zyklen-/PC-Fenster nur fürs *Loggen*,
    kein „laufe bis `[A]==X`, dann dumpe".
12. **Maschinenlesbarer Trace (CSV/JSON)**, **Run-Diff** (zwei Läufe vergleichen → wo
    divergieren sie), **Coverage** (welche Adressen liefen / nie).

---

## Usability (beide)

13. **Readline**: History/Pfeiltasten/Tab-Completion (heute rohes `getline`).
    **Session-Log**, **Aliases/`define`-Makros**, `source` weiterer Skripte mitten in der Sitzung.
