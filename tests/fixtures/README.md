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
| system | `cpa` = CP/A · `scpx17` = SCPX 1526 V1.7 · `udos` = UDOS 4.3 |
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
| `cpa_mini.img` / `cpa_mini.hfe` | synthetische Mini-Diskette (2 KB / 26 KB), kein Systemabbild | `test_hfe_image`, `test_disk_image_raw` |

Die beiden **Combo**-Disketten konfigurieren im BIOS die Laufwerke B:/C: als andere
Laufwerkstypen (DPB-Codes 10540/10580 bzw. 00877/10877). Dadurch bietet FORMAT.COM je
gewähltem Laufwerk die zugehörigen Formate an (5¼″ einseitig, 8″ SD/DD) — so sind auch
Fremdformate testbar, obwohl physisch immer dasselbe Laufwerk emuliert wird.
Details: `doc/format.md` §11 und §5/§3.5.

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
