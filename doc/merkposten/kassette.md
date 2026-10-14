<!-- Angelegt 2026-10-14 (AP K5, Entwurf 29).  Gilt WIE CLAUDE.md, sobald an der Kassette
     (K5020/K5200) gearbeitet wird.  Begruendung: doc/merkposten/README.md -->

# Kassette K5020 / K5200 — Merkposten

AKB **K5020** (zwei U855-PIOs, zwei Laufwerke K5200) als **steckbare Option** nach dem
Vorbild des Lochstreifens (`doc/merkposten/lochstreifen.md`).  Karte `core/cards/k5020/`,
Laufwerk/Medium/`.k52` `core/peripherals/kassette/`, Bestückung
`K1520Machine::installK5020()` (`core/machines/machine.h`), C-ABI `k1520_tape_*`, Python
`K1520Emulator(tape=True)` + `tape_*`, Werkzeuge `boot_trace`/`k1520dbg --tape <k52>`
(`--tape2`, `--tape-card`), Debugger-Befehl `tape`.  Entwurf `doc/design/29_kassette.md`,
Stand/Karten `doc/design/32_...`, Abschrift `doc/kassette/k5020.md`, `treiber.md`.

| # | Festlegung | Wächter |
|---|------------|---------|
| 1 | **Gesteckt wird nach dem Anlegen, vor dem ersten `run()`/`reset()`**; danach, doppelt oder bei belegtem Tor `false` + `k5020Fehler()` (C-ABI `k1520_tape_error`).  Tore werden **vor** dem Anmelden geprüft. | `K5020Maschine.A5120`/`A5120_16`/`K8915`/`Prg710`/`Prg710_1`/`Pc1715`, `py_tape_api` |
| 2 | **Basis je Maschine** (`K1520Machine::k5020Basis()`): A5120 (auch .16) **30H**, K8915 **D0H**, PRG 710/710-1 **30H** (frei, geprüft), PC 1715 **D0H** — dort liegt 30H–37H unter LT107 (2CH–33H) und BWS (34H); nur mechanisch, kein Gastcode.  P8000: nicht steckbar (kein K1520-Bus). | `K5020Maschine.*` (Basis je Maschine) |
| 3 | **Takt und Reset im Laufweg** jeder Maschine: `k5020Takt(used)` mit Instruktionstakten (bei `true` `markIntDirty`), `k5020Reset()` im systemweiten /RESET (Bänder bleiben liegen), `k5020AutoFlush()` am Ende jeder `run()`-Scheibe (gebundene `.k52` nach 0,5 s Schreibpause).  `takt_hz` = CPU-Takt der Maschine (`k6022TaktHz()`). | `K5020Maschine.*_Z80SchreibtBlockUndLiestEcho` |
| 4 | **Interruptkette:** hinten angehängt, **Daten-PIO vor Steuer-PIO**; der PRG 710 baut seine Kette in `interruptKetteSetzen()` neu (K6022 vor dem Fernschreiber, K5020 ganz hinten).  Stellung am Gerät unbelegt [?]. | `K5020Maschine.Prg710_1_Z80...` |
| 5 | **Nicht im Save-State.**  Neue Teile bräuchten eine neue Fassungsnummer (vgl. RAF, v8). | — |
| 6 | **Laufwerke 0/1 = Geräte 1/2 (X3/X4).**  `k1520_tape_load` BINDET die `.k52` (fehlende Datei = leere Kassette, sofort angelegt); `_new` legt eine Kassette ohne Datei ein; Auswerfen schreibt vorher und scheitert bei reserviertem/laufendem Gerät. | `py_tape_api` |
| 7 | **AEB-Polarität:** Bit 7 der Steuer-PIO A = 1 → Magnetband unter der Lichtschranke, 0 → AEB aktiv (Vorspann/Marke); `k5020.md` ist berichtigt, `treiber.md` §3.1 maßgeblich. | `K5020Test.AebFolgtDemAngewaehltenGeraet...` |
| 8 | **Gastprogramm im Test:** ISRs dürfen sich nicht überlappen (kIsrA 0x4200, kIsrB 0x4280) und die Hauptschleife wartet **ohne HALT** auf das Fertigmerkmal (Wettlauf zwischen Abfrage und ISR). | `K5020Maschine.*_Z80...` |

## Offen

- Oberfläche (K9), CLI `k1520tape` (K6, fertig), Bootband (K7), SCPX/`KMBG.REL` (K8), MC80 (M7).
- Stellung in der Interruptkette am A5120/K8915/PRG [?] — ohne Gasttreiber unbelegt.
