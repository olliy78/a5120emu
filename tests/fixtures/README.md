# Test-Fixtures

Unveränderliche Testdaten. **Nur Dateien, die ein registrierter Test wirklich braucht** —
`disks/` im Projektwurzelverzeichnis ist demgegenüber das *Arbeits*verzeichnis für manuelle
Läufe (Debugger, GUI, Formatier-Experimente) und darf sich jederzeit ändern.

Tests mounten grundsätzlich **Kopien** (Temp-Datei oder Copy-on-Write) — eine Fixture wird
nie beschrieben.

## Namensschema

```
<system>_<diskformat>_<laufwerkskonfiguration>_<merkmale>.<ext>
```

Segmente mit `_`, Werte innerhalb eines Segments mit `-`. Bei **allen** CP/A-Disketten gilt
`autofs` (automatische Formaterkennung) und `noautoexec` (kein AUTOEXEC beim Start) — deshalb
stehen diese beiden Eigenschaften nicht im Namen.

| Segment | Werte |
|---------|-------|
| system | `cpa` = CP/A · `scpx17` = SCPX 1526 V1.7 · `udos` = UDOS 4.3 · `udos1715` = UDOS1715/NDOS (PC 1715) |
| diskformat | physisches Format des Mediums: `cpa780` (5¼″ 80 Spuren DS MFM, 26×128 Sys + 5×1024 Daten), `5x1024`, `mini` |
| laufwerkskonfiguration | Laufwerkstypen, die das BIOS des Systems für A:/B:/C: annimmt |
| merkmale | `clock`/`noclock` (Uhrzeit-Abfrage beim Kaltstart), `hardy` (HARDY.COM an Bord) |

## Dateien

| Datei | Inhalt | benutzt von |
|-------|--------|-------------|
| `cpa_cpa780_k5601_clock.img` / `.hfe` | CP/A **mit Uhr**, A:/B:/C: = K5601 | `test_boot_integration` (Hauptfixture), alle CLI-Tests, `make_bootdisk` (Preset cpa780) |
| `cpa_cpa780_k5601_noclock.img` / `.hfe` | CP/A **ohne Uhr**, A:/B:/C: = K5601 | `test_boot_integration` (Boot von B:/C:, .img vs .hfe) |
| `cpa_cpa780_combo5zoll_noclock.img` | CP/A ohne Uhr, A: K5601 · **B: K5600.10** · **C: K5600.20** | `make_bootdisk` (Presets k5600_10_fmt1, k5600_20_fmt1) |
| `cpa_cpa780_combo8zoll_noclock.img` | CP/A ohne Uhr, A: K5601 · **B: MF3200** · **C: K5602.10/MF6400** | `make_bootdisk` (Presets mf3200_fmt7, mf6400_fmt1) |
| `scpx17_cpa780_k5601.hfe` | SCPX 1526 V1.7, System im **16×256**-Datenformat | `ScpxIntegration.*`, `ScpxInit.*` |
| `scpx17_5x1024_k5601_hardy.hfe` | SCPX 1526 V1.7, System im **5×1024**-Datenformat, mit `HARDY.COM` | `test_hardy` |
| `udos_boot_scp.hfe` | UDOS 4.3, bootfähig (SCP-Laufwerkstyp) | `UdosIntegration.*`, `test_udos_format` |
| `bootsec_cpa780.bin` | erwarteter Inhalt des Bootsektors einer cpa780-Diskette | `test_boot_integration` (Bootsektor-Vergleich) |
| `mixed_udos_ss40_over_cpa800.hfe` | **gemischtes Layout**: cpa800, darüber UDOS ss40 im Doppelschritt — Kopf 0 gerade Zylinder 26×128 (UDOS), ungerade 5×1024 (Altbestand), Kopf 1 ganz 5×1024 | `test_disktool_gui` (roh öffnen, Schnitte), `test_gw_physical` |
| `cpa_mini.img` / `cpa_mini.hfe` | synthetische Mini-Diskette (2 KB / 26 KB), kein Systemabbild | `test_hfe_image`, `test_disk_image_raw` |
| `udos_ds77_k5601_fremdsync.hfe` | UDOS 4.3, an einem **fremden** K1520-Rechner (K5601) beschrieben: Datenfeld-Sync mit nur ein bis zwei echten Sync-Marken (die übrigen 0xA1 regulär kodiert), ID-CRC **ohne** A1-Präambel, 34 + 12 Dateien | `DiskVolume.LiestEineDisketteMitFremderSyncSitte`, `test_gw_physical` (Naht) |
| `udos1715_640k_pc1715_system.img` | **UDOS1715/NDOS** (PC 1715), Systemdiskette „SYSTEM": 80×32×256, 67 Dateien, darunter das Systemhandbuch `UDOS.TEXT` | `Udos1715.*`, `Udos1715Belegung.*`, `Udos1715Schreiben.*` |
| `udos1715_640k_p8000_wega.hfe` | **UDOS1715/NDOS** vom **Robotron P8000** (UDOS 2.2), „WEGA-STARTDISKETTE": 80×32×256, 42 Dateien (UDOS-Dienstprogramme + die WEGA-Urlader und `sa.*`-Werkzeuge). Dieselbe Sitte wie der PC 1715 — nur mit `77H` statt `00` hinter dem Belegungsplan | `Udos1715P8000.*` |
| `scp1700_640k_a7100_system.hfe` | **SCP1700/CP/M-86** (A7100), Systemdiskette: 80×2×16×256 MFM — aber **Spur 0 Kopf 0 in FM mit halber Datenrate** (16×128, 125 kbit/s), 46 Dateien | `Scp1700.*` |

Die **gemischte** Diskette entstand am echten Laufwerk: erst vollständig als cpa800
formatiert, dann mit UDOS `ss40` im Doppelschritt überschrieben.  Sie ist die einzige
Fixture, auf der **kein** Katalogformat passt — und der Prüfstein für drei Zusagen:
roh öffnen (das Abbild wird auch ohne Erkennung hergegeben), die Schnitte
(*ungerade Spuren entfernen* + *Seite 1 entfernen* → `udos_ss40` mit 44 Dateien) und
die Toleranz gegen Schadstellen: **Spur 25 fehlt der Sektor 1** (25 Sektoren mit den
IDs 2…26 statt 26 mit 1…26).  Das ist echt und soll so bleiben — genau daran fiel auf,
dass eine solche Spur als *anderes Format* galt statt als Schaden.

Die beiden **Combo**-Disketten konfigurieren im BIOS die Laufwerke B:/C: als andere
Laufwerkstypen (DPB-Codes 10540/10580 bzw. 00877/10877). Dadurch bietet FORMAT.COM je
gewähltem Laufwerk die zugehörigen Formate an (5¼″ einseitig, 8″ SD/DD) — so sind auch
Fremdformate testbar, obwohl physisch immer dasselbe Laufwerk emuliert wird.
Details: `doc/format.md` §11 und §5/§3.5.

## Warum die UDOS1715-Fixture ein `.img` ist

Weil sie es sein DARF, und weil das 640 KB statt 2 MB im Verzeichnisbaum bedeutet.
UDOS1715/NDOS hält die Dateiverkettung in eigenen Zeigersektoren *innerhalb* der
Sektoren — anders als ZDOS auf dem A5120, dessen Kontrollblock hinter der Daten-CRC
liegt und ein rohes Sektorabbild unbrauchbar macht. Genau das prüft
`FsCatalog.Udos1715ProfileSindImgFaehigUndEinseitigGezaehlt` mit; die spurbasierte
Aufnahme derselben Diskette liegt als `disks/udos1715_640k_pc1715_system.hfe` im
Arbeitsverzeichnis. Hintergrund: `doc/udos1715_diskettenformat.md` §8.

## Zwei UDOS1715-Disketten, weil zwei Rechner dasselbe Format verschieden füllen

`udos1715_640k_pc1715_system.img` (PC 1715) und `udos1715_640k_p8000_wega.img`
(Robotron P8000) tragen dasselbe Dateisystem an denselben Offsets.  Die P8000-Diskette
kam trotzdem als „kein gueltiger UDOS1715-Diskettenbelegungsplan" zurück: ihr
Formatierer lässt zwischen Belegungsplan und Zählern den `77H`-Nachlauf der ZDOS-Sitte
stehen, und `179H` trägt `01`.  Sie ist deshalb der Prüfstein dafür, dass die
Unterscheidung zu ZDOS am **Zählerabgleich** hängt und nicht am Füllmuster
(`doc/udos1715_diskettenformat.md` §3.0a).  Zweiter Prüfstein: ihr Systembereich ist
größer — Kopf 0 der Spuren 0, 21, 22 und 23 ist ganz gesperrt.

Sie liegt als **`.hfe`** vor, obwohl UDOS1715 `.img` erlaubt: 13 Sektoren tragen hinter
der Daten-CRC die **Schreibnaht** eines nachträglich überschriebenen Sektors
(`4E xx yy yy …`, z. B. c12h0 Sektor 10).  Inhaltlich ist das nichts — aber
`rawCompatible()` sieht dort Bytes außerhalb der Nutzdaten und verweigert `.img`.  Eine
Fixture, die das Werkzeug selbst nicht schreiben würde, wäre ein schlechter Prüfstein;
`Udos1715P8000.WegaStartdisketteWirdErkannt` hält genau das fest.

## Die SCP1700-Diskette ist die einzige mit ZWEI Datenraten

`scp1700_640k_a7100_system.hfe` ist eine Aufnahme vom echten Laufwerk (Greaseweazle F1,
300 min⁻¹).  Ihre Bootspur c0h0 läuft mit **125 kbit/s in FM**, alle übrigen 159 Spuren
mit 250 kbit/s in MFM — Mischdichte gibt es sonst auch (8″-System-34), Mischrate nicht.
Sie ist damit der Prüfstein dafür, dass der Abtastfaktor **je Spur** bestimmt wird und
die halbe Rate an der Spur hängenbleibt (`TrackImage::cell_factor`).

Zwei Eigenheiten sind echt und sollen so bleiben: die Bootspur trägt **19 Adressmarken
für 16 Sektoren** (hinter Sektor 16 stehen noch einmal 1…4 — sie wurde in einem Zug
über den Index hinaus geschrieben), und ihr **Sektor 10 ist beschädigt** (kein
Adressfeld, auch nach vielen Umdrehungen nicht).  Deshalb meldet `info` die
Systemspuren als „nicht lesbar"; das Dateisystem ist davon unberührt.
Hintergrund: `doc/scp1700_diskettenformat.md`.

## Die beiden SCPX-Disketten sind NICHT austauschbar

`scpx17_cpa780_k5601.hfe` trägt ein **16×256**-System, `scpx17_5x1024_k5601_hardy.hfe` ein
**5×1024**-System. Beides sind verschiedene SYSP-Generierungen, keine Kopien voneinander:

- `ScpxIntegration.WrongFormatReadTerminatesInsteadOfFreezing` braucht gerade den
  *Formatkonflikt* zwischen dem 16×256-System und einer 5×1024-Diskette in B:.
- `ScpxInit.Builds5x1024SystemViaInitModfSyspAndBoots` erzeugt aus dem 16×256-System per
  INIT/MODF/SYSP ein 5×1024-System. Läuft derselbe Ablauf von einem **5×1024**-System aus,
  hat die erzeugte Diskette pro Datenspur **einen defekten Sektor** (`disk verify`:
  „5 Sekt, 1 CRC-Fehler" auf jeder Spur) und das generierte System kann keine `.COM`-Datei
  mehr laden. Reproduziert am 2026-08-07; ungeklärt, siehe `doc/testsystem_rework.md` §7.

## Keine Leerdisketten hier

Leere, gültig formatierte Disketten werden zur **Testzeit erzeugt**, nicht
committet — die Erzeugung ist selbst getestet (`A5120DiskApi`, `CreateDiskDefault`,
`tests/python/test_binding.py`), also ist eine eingecheckte Vorlage nur ein
Artefakt, das driften kann:

- einseitige Formate: `mk_disk_template` (8″-FM/MFM, 5¼″-SS)
- doppelseitige: `DiskImage::create` über die C-API — in der Boot-Disk-Pipeline
  `gen_named_template()` in `tests/system/drivers/make_bootdisk.py`

## Zugriff aus Tests

CMake reicht das Verzeichnis als Compile-Define herein:

- `A5120_TEST_DISK_DIR` — Integrations-/Systemtests (`diskPath("…")`)
- `FIXTURE_DIR` — Unit-Tests der Floppy-Schicht

Beide zeigen auf `tests/fixtures/disks`.

> **Namen von origin/main bleiben, wie sie dort heißen** (`udos_boot_scp.hfe`).  Unser
> Schema gilt für die Disketten, die es zum Umbauzeitpunkt gab; neue von origin
> umzubenennen würde jeden künftigen Merge unnötig erschweren. Python-Treiber (`make_bootdisk.py`, `format_all.py`)
bilden denselben Pfad über `ROOT/tests/fixtures/disks`.
