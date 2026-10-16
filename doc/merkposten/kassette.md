<!-- Angelegt 2026-10-14 (AP K5, Entwurf 29).  Gilt WIE CLAUDE.md, sobald an der Kassette
     (K5020/K5200) gearbeitet wird.  Begruendung: doc/merkposten/README.md -->

# Kassette K5020 / K5200 — Merkposten

AKB **K5020** (zwei U855-PIOs, zwei Laufwerke K5200) als **steckbare Option** nach dem
Vorbild des Lochstreifens (`doc/merkposten/lochstreifen.md`).  Karte `core/cards/k5020/`,
Laufwerk/Medium/`.k52` `core/peripherals/kassette/`, Bestückung
`K1520Machine::installK5020()` (`core/machines/machine.h`), C-ABI `k1520_tape_*`, Python
`K1520Emulator(tape=True)` + `tape_*`, Werkzeuge `boot_trace`/`k1520dbg --tape <k52>`
(`--tape2`, `--tape-card`), Debugger-Befehl `tape`.  Entwurf `doc/design/29_kassette_k5020_k5200.md`,
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
| 9 | **`KMBG.REL` (SCPX) braucht eine zurückgespulte Kassette**: steht sie im Vorspann, fährt KMBG mit Arbeitsgeschwindigkeit los und wartet nur 65 536 × 92 Takte = **2,45 s** auf das Magnetband (KMBG 048E–04A4, Modul-relativ), sonst Status 04.  Bei 381 mm/s sind das ≈ 0,9 m — der 1-m-Vorspann des Kernmodells (`Geometrie::vorspann_um` [?]) ist ab Position 0 nicht zu schaffen; nach `UMS` (Lichtschranke kurz im Vorspann) geht es.  Der Test spult deshalb vorher in einem eigenen `KassettenLaufwerk` zurück.  Ob der Vorspann kürzer sein müsste oder eine neue Kassette nicht am Vorspannanfang liegt, ist offen [?]. | `ScpxKmbg.SchreibtUndLiestEinenBlock` |
| 10 | **Blockformat ist reine Software** (kein Hardware-Rahmen): `AA`, 1…256 Daten, CRC-16/ARC (low zuerst, Start 0000, nur über die Daten), `AA`; Bandmarke = Block ohne Daten `AA 00 00 AA` (rückwärts `AA FF FF AA`).  Ein Block mit 128 Daten sind **132 B** auf dem Band — die 131 sind nur die Leselänge des Lade-ROMs.  Lücke zwischen Blöcken 10 mm ist Annahme [?]. | `K52Codec.*`, `KassettenMedium.*`, `K5020Rom.LiestEinBootbandMitVierBloeckenUndPruefSyl` |
| 11 | **Init-Worte nach Karte getrennt:** Daten-PIO nur `0FH`/`4FH`; `CFH,80H,17H,7FH` / `CFH,FDH,17H,DFH` gehören zur Steuer-PIO.  Die Steuer-PIO braucht den Interrupt bei **High (FEX, `37H`) UND bei Low (KMBG, `17H`)** — Entwurf 29 §4.1 hatte das vertauscht; `zre.prn:437` („Bits 1,0 löschen“) ist falsch (AND F3H löst das interne Rücksetzen aus). | `K5020Test.*` |
| 12 | **Z80PIO::setExtern übernimmt Pegel nur im Modus 3 und holt sie beim Moduswechsel nicht nach.**  Die K5020 legt deshalb nach JEDEM Steuerwort die Rückleitungen neu an (`ext_neu_`); Steuer-PIO braucht Flankentrigger (`setModus3Flanke`), Daten-PIO `modus1ReadyNachLesen`.  Ob `Z80PIO` selbst beim Moduswechsel neu einlesen sollte (die P8000-Karten nutzen `setExtern`), ist offen [?] — nicht still ändern. | `K5020Rom.*`, `KassetteBoot.*` |
| 13 | **UMS wirkt auf die steigende Flanke**; STA = AUF ∧ AVE.  Die K5020 kennt UMS/WID/KNG/AVE-Schreibschutz nicht (für die CPU nur indirekt).  K5020-Reaktionen: BMS-Verzögerung 108 µs, „HGE nach AUF“ → `wiedergabe(false)`, TRV+TRR → `umspulen()`. | `K5020Test.*`, `KassettenLaufwerk.*` |
| 14 | **Bitzelle ist eine ZEIT, kein Teiler** (83,33 µs bei 12 000 Bit/s; 6 000 Bit/s nominal [K2]); die Bitrate ergibt sich aus Dichte (31,5 Bit/mm) × Geschwindigkeit.  Zeiten LW1200/SK1200/PK3/K5200-19 sind Annahmen [?]. | `KassettenLaufwerk.*` |
| 15 | **Bootband:** 4 × 128 B nach `0400H`, `SYL` in 0400–0402, Einsprung `0434H` (das Programm darf nicht zurückkehren).  `k1520tape bootband prog.bin band.k52` verlangt `SYL` am Anfang.  **A5120 bootet ohne Diskette vom Band** (`tools/kassette/bandtest/`, Fixture `tests/fixtures/kassette/bandtest_boot.k52`, ~27 Mio. Takte).  Die Fixture nie direkt mounten. | `KassetteBoot.*`, `cli_bandtest_*`, `cli_k1520tape_*` |
| 16 | **`--tape` bindet die Datei** und schreibt zurück (wie ein gemountetes Abbild) — nie die eingecheckte Fixture angeben, sondern eine Kopie.  `k1520dbg`: `--tape` steht **vor** dem Diskpfad. | `cli_dbg_tape*`, `cli_bt_tape` |
| 17 | **FEX-Formate sind nicht belegbar**; `k1520tape` kann nur Rohblöcke und Bootband.  Der CLI-Runner kennt nur Textdateien. | `cli_k1520tape_*` |
| 18 | **Oberfläche (K9):** Profilfeld `tape_wahl` (am P8000 kein Kästchen); `general.tape` (true/false), Vorgabe aus (auch in den `data/default_config_*.yaml`); Abschnitt `kassette:` je Laufwerk Datei + Schreibschutz — **fehlt er, bleibt alles**.  Kasten „Kassette“ nur mit Karte, **kein Tastenkürzel**.  Ein Wechsel des Kästchens baut die Maschine neu.  `tape_flush` je Laufwerk beim Beenden, Neuaufbau und Auswerfen.  **Abweichung vom Lochstreifen:** `tape_load` legt eine fehlende `.k52` leer an. | `py_kassette_gui` (`test_vorgabe_aus_und_kasten_folgt_der_karte`, `test_p8000_hat_keine_kassette`, `test_auslieferung_hat_die_karte_aus`, `test_keine_kuerzel`, `test_wiederherstellung_aus_der_konfiguration`) |
| 19 | **Handbuch** (`app/help/handbuch.md` „Kassette“) gehört zum Vertrag: Abschnitt und Hinweis „KMBG/FEX erst zurückspulen“ (Festlegung 9) bleiben; Kürzeltabelle unverändert. | `py_help` |
| 20 | **BCU 880 (Ausführung `Kmbg`): F: ist GERÄT 2** (E: = Gerät 1), Treiber per `OPEN B:KMBG` (nicht `B:KMBG`), Warmstart braucht die Diskette „ohne SYL“ in A:; der BCU-Treiber wartet beim Überfahren des Vorspanns **ohne Frist** — eine frisch eingelegte Kassette (Position 0) geht, anders als bei Festlegung 9.  READ SEQ liefert den 17-B-Abschlussblock als zusätzlichen Satz (Gastverhalten).  Dabei gefunden und behoben: Interrupt erst NACH dem Befehl hinter `EI` (`Z80::ei_schatten`, A5120-Laufweg) und `Z80PIO::onRETI` (B wird auch frei, wenn A nur anfordert) — sonst `7xWRITE` (Echo verloren).  Details `doc/a5120_alt/bcu.md` §8. | `BcuKassette.FormatiertSchreibtLiest`, `Z80Test.EiSchatten_*`, `RetiChain.PioRetiVonBLoeschtIusAuchWennAAnfordert` |
| 21 | **MC80 (AP M7, `doc/mc80/kassette.md`):** K5020 an **30H**, steckbar wie überall (`Mc80Machine::powerOn` schließt die Bestückung NICHT ab — sonst schlug `installK5020` immer fehl).  **.30** = `Takt::ExternerCtc` (Schreibtakt = ASP-CTC K2 läuft als Zähler) + Profil **LW1200**; **.31** = Bustakt + **K5200-19** [?] (EGOS' Postambelprobe ≈ 2 ms je AAH verträgt keine 12 000 Bit/s).  Neue Kassetten am MC80 = `Geometrie::kompaktkassette()` (**80 mm Vorspann, keine Marken** [?]) über `K5020::Config::neue_kassette` — EGOS hat 250 ms nach Bandstart den AEB-Interrupt scharf und spult „2 s vor, dann UMS“ zurück.  Zwei AAH hintereinander verlieren bei 6 000 Bit/s ein Byte (Treibergrenze). | `Mc80Kassette.SpeichernUndLaden/*`, `Mc80KassettePraeambel.*`, `Mc80K5020.*`, `K5020Test.ExternerTakt*`, `py_mc80emu_gui` |
| 22 | **Beispielbänder in `disks/`** (2026-10-16): `a5120_bandtest/bandtest_boot.k52` (= Fixture, Demo/Eigenbau) und `a5120_bcu/bcu_kassette_formatiert.k52` (aus dem BCU-INIT-Lauf, `K1520_BEISPIELBAND=<pfad>` im Test `BcuKassette` schreibt es neu; verglichen werden die Bloecke, nicht die Luecken).  Gebaut/geprüft von `tools/bcu_a5120/build.py`; im Paket, `seed_user_disks()` kennt `*.k52`.  **Kein Kassettentreiber für CP/A/SCPX bauen** (CP/A: „ausserhalb des BIOS“, nur Interruptplatz 0C0H–0CFH; SCPX: `KMBG.REL` + `KMBGTEST.COM` liegen auf der SCPX-Diskette) — `doc/a5120_alt/bcu.md` §10.3. | `cli_bcu_a5120`, `BcuKassette.FormatiertSchreibtLiest`, `py_packaging`, `cli_scpx_a5120` |

## Bekannte Eigenheiten

- Nach Kern-/Werkzeugänderungen einmal `tools/dev.sh build` vor dem Volllauf (7 CLI-Fälle waren nur
  eine veraltete `k1520dbg`-Binary).  Die Python-Ebene im Worktree braucht `venv/` (Symlink).
- `KMBG.REL` ist ein Fremd-Modul: `tools/cpm_bau.py` kennt dafür `module=`; Test-Programme in
  `tools/kassette/kmbgtest/`, `bandtest/`.

## Offen

- **Vorspannlänge/Lage einer frisch eingelegten Kassette gegen die KMBG-Frist 2,45 s** (Festlegung 9) [?]:
  entweder ein kürzerer Vorspann im Modell oder eine Einlegeposition hinter dem Vorspann; bis zur
  Klärung gilt „erst zurückspulen“ (im Handbuch vermerkt).
- Stellung in der Interruptkette am A5120/K8915/PRG [?] — ohne Gasttreiber unbelegt.
- `Z80PIO::setExtern` beim Moduswechsel (Festlegung 12) [?].
- MC80: externer Takt (Frequenz/Weg X3B06), Geschwindigkeit am .31, Vorspann/Marken echter MC80-Kassetten [?]
  (`doc/mc80/kassette.md` §6); .33 (AKM + PK 3) gesperrt, ROM-Hälfte fehlt (`doc/mc80/pk3.md`).
- optional `.wav` nach ISO 3407 (K11, zurückgestellt).
