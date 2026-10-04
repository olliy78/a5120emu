<!-- Angelegt 2026-10-04 (Abschluss Entwurf 22, doc/design/22_raf512.md).  Diese Datei
     gilt WIE CLAUDE.md, sobald an der RAM-Floppy RAF gearbeitet wird — sie ist nur nicht
     in jeder Anfrage geladen.  Begruendung: doc/merkposten/README.md -->

# RAM-Floppy RAF 128/512/2M — Merkposten

K-1520-RAM-Floppy des ZWG der AdW als **steckbare Option** in allen drei Maschinen
(A5120 auch als A5120.16, K8915, PRG 710/710-1), fest auf **E/A 88H (Daten) / 89H
(Steuerung)**.  Karte `core/cards/raf/` (Klasse `RAF`), Bestückung
`K1520Machine::installRaf(RAF::Typ)` (`core/machines/machine.h`), C-ABI `k1520_raf_*`,
Python `K1520Emulator(raf=…)`, Oberfläche *Einstellungen ▸ Allgemein ▸ RAM-Disk*
(`app/raf.py`), Werkzeuge `k1520dbg --raf` + Befehl `raf`, `boot_trace --raf`.
Entwurf, Herleitungen und Befunde je AP: **`doc/design/22_raf512.md`**; Originale
(Treiber, ZWG-Prüfprogramme, Prüfanleitung, PROM-Abzüge): **`doc/raf512/`**.

## Festlegungen, die man nicht aufweichen darf

| # | Festlegung | Wächter |
|---|------------|---------|
| 1 | **A8–A15 tragen die Adresse** (Z80: Register B bei `OUT (C),r`/`INIR`/`OTIR`), geholt über `K1520Bus::ioAddress()`.  Jede CPU-Karte muss die **volle** 16-Bit-E/A-Adresse an den Bus geben — die ZRE 045-8762 schnitt sie auf 8 Bit ab, am K8915 meldete der Treiber trotz Karte „Keine RAF-Karte vorhanden!". | `Raf.*` (echter Z80-Code), `ZRE8762.VolleEaAdresseKommtAmBusAn` |
| 2 | **Sperre = Steuerlatch & Sperrmaske** (RAF512 `0x9000` = A15 ODER A12, RAF128 `0x8400`, RAF-2M `0xC000`); gesperrt liest FFH, Schreiben verpufft.  A12 ist zugleich das erste Bit über 512 K — so bricht der Kapazitätstest der Treiber bei Spur 32 ab. | `Raf.*`, `RafZwg.RaftestTyp5VollerDurchgangUndSperrmuster` |
| 3 | **Nicht ausgewertete Bits spiegeln** (RAF512: A13/A14 → Spur 64 = Spur 0).  So nachbilden, nicht „schöner" — RAFTEST erkennt daran die Bauart. | `Raf.*` (Sektor 8192 → 0), RAF128-Gegenprobe in AP-R6 |
| 4 | **Bytezeiger = A8–A14, kein Autoinkrement.**  `INIR`/`OTIR` laufen deshalb rückwärts: Treiber ohne Parität legen Pufferbyte i bei Sektor×128+127−i ab, das Paritäts-BIOS (AP-R7) Byte 0 bei 0 und Byte k ≥ 1 bei 128−k.  Wer den Inhalt mit `peek`/`raf.bin` liest, muss das wissen; `k1520dbg raf <sektor>` zeigt die Lesart **ohne** Parität. | `Raf.*`, `RafA5120.*` (Kennung `RAFvalid.SYS`), `RafCpaBios.*` (`RFPvalid.SYS`) |
| 5 | **RESET erhält den Inhalt, Netz-Ein verwirft ihn** (Füllwert 00H, reproduzierbar); RESET setzt nur das Latch auf gesperrt.  Das ist der Zweck der Karte. | `RafMaschine.*`, `RafA5120/K8915/Prg710.*…UeberstehtReset`, `RafCpaBios.MStehtBereitUndUeberstehtReset` |
| 6 | **Gesteckt wird nach dem Anlegen, vor dem ersten `run()`/`reset()`** — danach `false` + `rafFehler()`.  Keine weitere `k1520_create_*`-Variante je Kombination. | `RafMaschine.*`, `py_test_raf_api` |
| 7 | **Überall 88H/89H, nicht konfigurierbar** (Anwenderentscheid F3a); `RAF::Config::basis` nur für Tests. | `Raf.AbweichendeBasisUndPortbelegung` |
| 8 | **Save-State v8** (nur A5120): eigener RAF-Teil hinter dem Geräteteil; v7 lädt ohne RAF-Angabe; Stand mit RAF in Maschine ohne/mit anderer RAF → `false` + `stateError()`, **nichts** verändert.  Die Rückwärts-Historie des Debuggers (`rs`/`rc`) nimmt nur das Latch mit (200 × 2 MB). | `RafMaschine.SaveState*`, `RafMaschine.SnapshotOhneRafInhaltStelltNurDasLatchZurueck` |
| 9 | **Stand-by-Ablage je Programm** (`raf_<programm>.bin` im gemeinsamen `config_dir()`), nur bei gesetztem Kästchen; falsche Größe → nicht laden, Hinweis in der Statuszeile, beim nächsten Sichern ersetzen; eine nie eingeschaltete Maschine sichert nicht.  Auch Aus/Ein in der Oberfläche läuft über Sichern → Einschalten → Laden. | `py_test_raf_gui`, `py_test_raf_standby_gui` |
| 10 | **EM256 und RAF gleichzeitig sind erlaubt** (A8–AF vs. 88/89).  CP/A mit `em256=1` hat M: im EM, `RAF512.COM` legt P: daneben an. | `RafMaschine.A5120_16_EmUndRafKoexistieren`, `RafA5120.A5120_16Em256UndRaf512Nebeneinander` |

## Gastbefunde (kein Emulatorfehler)

- **K8915, SCPX 8915 V5.3:** M: ist frei, `RAFCPM` legt M: an — belegt wäre es erst am
  K8915 mit Festplatte.  `PIP.COM` trägt nur die Fixture-Diskette 900.
- **RAFTEST** hat keinen Adressdialog: Ports im PATCH AREA (Dateiversatz 22H/23H,
  Vorgabe 8FH/8EH); Vorgabebereich nur Sektor 0…3.  `LogAnlzTrig` pulst PIO 84H/86H —
  am PRG 710 die PIO der K2521, dort nicht fahren.
- **RAFQUICK** ist mit Endadresse CCC0H übersetzt, seine Variablen liegen im BDOS des
  CP/A (C400H): `x` endet im zerstörten Warmstart.  Tests halten mit Leertaste an.
- **Treiber-Beigaben** liegen nur auf Disketten, auf denen der Treiber im Gast geprüft ist
  (`tools/disketten_beigaben.py`, Wächter `cli_beigaben_auf_den_disketten`).

## Offen

Anwenderfragen F4 (Steckplatz im A5120.16) und F7 (RAF-Diskette ins Paket), Entwurf §9.
Der Adress-PROM-Abzug `RAFAD12` zeigt nur eine 1-aus-4-Auswahl; seine Zuordnung zu
A0–A7 hängt an den Wickelbrücken und ist nicht im Modell.
