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
| system | `cpa` = CP/A · `scpx17` = SCPX 1526 V1.7 · `k8915scpx` = SCPX 8915 · `udos` = UDOS 4.3 · `udos1715` = UDOS1715/NDOS (PC 1715) |
| diskformat | physisches Format des Mediums: `cpa780` (5¼″ 80 Spuren DS MFM, 26×128 Sys + 5×1024 Daten), `5x1024`, `mini` |
| laufwerkskonfiguration | Laufwerkstypen, die das BIOS des Systems für A:/B:/C: annimmt |
| merkmale | `clock`/`noclock` (Uhrzeit-Abfrage beim Kaltstart), `hardy` (HARDY.COM an Bord); beim K8915 BIOS-Fassung (`bios55k`, `v24xonxoff`), Besonderheit (`autodbase`) und Diskettennummer des Anwenders (`disk900`) |

## Dateien

| Datei | Inhalt | benutzt von |
|-------|--------|-------------|
| `cpa_cpa780_k5601_clock.img` / `.hfe` | CP/A **mit Uhr**, A:/B:/C: = K5601 | `test_boot_integration` (Hauptfixture), alle CLI-Tests, `make_bootdisk` (Preset cpa780) |
| `cpa_cpa780_k5601_noclock.img` / `.hfe` | CP/A **ohne Uhr**, A:/B:/C: = K5601 | `test_boot_integration` (Boot von B:/C:, .img vs .hfe) |
| `cpa_cpa780_combo5zoll_noclock.img` | CP/A ohne Uhr, A: K5601 · **B: K5600.10** · **C: K5600.20** | `make_bootdisk` (Presets k5600_10_fmt1, k5600_20_fmt1) |
| `cpa_cpa780_combo8zoll_noclock.img` | CP/A ohne Uhr, A: K5601 · **B: MF3200** · **C: K5602.10/MF6400** | `make_bootdisk` (Presets mf3200_fmt7, mf6400_fmt1) |
| `cpa_cpa780_k5601_noclock-em256.img` | CP/A ohne Uhr, K5601, **@OS.COM mit `em256 equ 1`** (A5120.16, RAM-Floppy M: im EM256) | `Em256RamFloppy.*` |
| `cpa_cpa780_k5601_noclock.img` + **`../cpm/em256adr.com`** | G1-Prüfprogramm (A5120.16) — wird im Test auf die Temp-Kopie geschrieben | `Em256Adr.*` |
| `cpa_cpa780_k5601_noclock.img` + **`../cpm/em16abl.com`** | S4-Abnahme/G2-Vorlage (A5120.16, U8001) — ebenso | `Em16Abl.*` |
| `scpx17_cpa780_k5601.hfe` | SCPX 1526 V1.7, System im **16×256**-Datenformat | `ScpxIntegration.*`, `ScpxInit.*` |
| `scpx17_5x1024_k5601_hardy_norm.hfe` | SCPX 1526 V1.7, System im **5×1024**-Datenformat, mit `HARDY.COM` — mit Normlücken neu aufgebaut (`save-as` → `.img` → `.hfe`, 80 Zylinder); ersetzt seit AP-F1 die frühere, vom Emulator gespeicherte Fassung mit Lücke 2 = 11 (am Gerät nicht lesbar) | `test_hardy`, DiskTool-Tests |
| `udos_boot_scp.hfe` | UDOS 4.3, bootfähig (SCP-Laufwerkstyp) | `UdosIntegration.*`, `test_udos_format` |
| `bootsec_cpa780.bin` | erwarteter Inhalt des Bootsektors einer cpa780-Diskette | `test_boot_integration` (Bootsektor-Vergleich) |
| `mixed_udos_ss40_over_cpa800.hfe` | **gemischtes Layout**: cpa800, darüber UDOS ss40 im Doppelschritt — Kopf 0 gerade Zylinder 26×128 (UDOS), ungerade 5×1024 (Altbestand), Kopf 1 ganz 5×1024 | `test_disktool_gui` (roh öffnen, Schnitte), `test_gw_physical` |
| `cpa_mini.img` / `cpa_mini.hfe` | synthetische Mini-Diskette (2 KB / 26 KB), kein Systemabbild | `test_hfe_image`, `test_disk_image_raw` |
| `udos_ds77_k5601_fremdsync.hfe` | UDOS 4.3, an einem **fremden** K1520-Rechner (K5601) beschrieben: Datenfeld-Sync mit nur ein bis zwei echten Sync-Marken (die übrigen 0xA1 regulär kodiert), ID-CRC **ohne** A1-Präambel, 34 + 12 Dateien | `DiskVolume.LiestEineDisketteMitFremderSyncSitte`, `test_gw_physical` (Naht) |
| `udos1715_640k_pc1715_system.img` | **UDOS1715/NDOS** (PC 1715), Systemdiskette „SYSTEM": 80×32×256, 67 Dateien, darunter das Systemhandbuch `UDOS.TEXT` | `Udos1715.*`, `Udos1715Belegung.*`, `Udos1715Schreiben.*` |
| `udosP8000_640k_wega.hfe` | **UDOS1715/NDOS** vom **Robotron P8000** (UDOS 2.2), „WEGA-STARTDISKETTE": 80×32×256, 42 Dateien (UDOS-Dienstprogramme + die WEGA-Urlader und `sa.*`-Werkzeuge). **Anderer Rechner als der PC 1715**, gleiche Diskettensitte — nur mit `77H` statt `00` hinter dem Belegungsplan | `Udos1715P8000.*` |
| `scp1700_640k_a7100_system.hfe` | **SCP1700/CP/M-86** (A7100), Systemdiskette: 80×2×16×256 MFM — aber **Spur 0 Kopf 0 in FM mit halber Datenrate** (16×128, 125 kbit/s), 46 Dateien | `Scp1700.*` |
| `k8915scpx_boot1.hfe` | **SCPX 8915 V5.3, Fassung „V24 (XON/XOFF)“** (K8915) = **Diskette 901** des Anwenders (`***901.VOL`; Greaseweazle-Abzug, gleich `disks/k8915scpx_boot1.hfe`; Name aus der Zeit, als es die einzige war): `cpa800`, Systemspuren 5×1024 ab Zylinder 0, DISGEN-Einstellung B: = 16×256, Autostart `rade`, `RADE.COM`, `DISGEN`, `FORMAT`, Turbo Pascal | `K8915Scpx.*` |
| `k8915scpx_cpa800_k5601_bios55k-disk900.hfe` | **SCPX 8915 V5.3, Fassung „55 K … BIOS-Version 5.3“** = Diskette 900 (`***900.VOL`): IOBYTE-Weiche, anderer Druckertreiber (7 Bit, ungerade Parität), B: = 5×1024, Autostart `rade`; nur Systemprogramme (DISGEN, FORMAT, PIP, POWER, RADE, SOFTKEY, STAT, SUBM, XSUB, DUMP) | `K8915Scpx.Fassung55KVonDiskette900BisZumPrompt` |
| `k8915scpx_cpa800_k5601_v24xonxoff-autodbase-disk904.hfe` | **Fassung „V24 (XON/XOFF)“** wie 901, anders per DISGEN konfiguriert = Diskette 904 „Grundsoftware“ (`***904.VOL`): Autostart `rade`/`dbase`/`use lohn`/`do lohn`, eigene F-Tasten, B: = 5×1024; **ohne** RADE.COM und DBASE.COM (Autostart läuft absichtlich ins Leere), dafür REDABAS, KP, Turbo Pascal | `K8915Scpx.Grundsoftware904AutostartLaeuftInsLeere` |
| `prg710_udos43_k5601_boot01.hfe` | **UDOS 4.3 für den PRG 710** (`PRG710_UDOS43_Boot01`, Greaseweazle-Abzug des Anwenders): `udos_ds77`-Spuren 26×128 MFM, Bootsektor `18 03 'SYL'`, Resident mit 8279-Tastatur und low-aktivem Marken-FF. **Nur Seite 0 abgezogen** (HFE-Kopf 1 Seite, Zylinder 28/46/50/51/77 leer) — bootet deshalb nur bis in den Residenten, der Seite 1 liest (AP-P3) | `Prg710Boot.StarttasteLaedtDenBootsektor/Prg710` (AP-P1d), `Prg710Udos.Boot01ZweitladerOhneCrcFehler` (AP-P3) |
| `prg710-1_udos_k5601_boot.hfe` | **UDOS für den PRG 710-1** (`PRG710-1_UDOS_Boot`, „UDOS PG710-1“): `udos_ds77`, Tastatur K7672 an SIO A32-B, Marken-FF high-aktiv; ohne `DATE`/`COPY` (`OS.INIT` endet in „NONEXISTENT COMMAND“) | `Prg710Boot.StarttasteLaedtDenBootsektor/Prg710_1` (AP-P1d), `Prg710Boot.Zweitlader710_1SendetEscKlammerFragezeichen11h` |
| `prg710_udos43_k5601_mrs_boot.hfe` | **UDOS 4.3 für den PRG 710, beidseitig** (`PRG710_UDOS43_MRS_Boot`): Seite 0 System + UDOS-Kommandos (`CAT`, `COPY`, `DATE`, `FORMAT` …), Seite 1 (Laufwerk 4) MRS; `OS.INIT` mit Begrüssungsbild, „JULI 1987“, Datumsabfrage → „UDOS PRG710“ | `Prg710Udos.*/Prg710` (AP-P3) |
| `prg710-1_udos43_k5601_v43_189.hfe` | **UDOS.PRG710-1 V4.3 1/89** (`UDOS.PRG710-1_V4.3_1_89`): „UDOS V 4.3 APRIL 1989“, Datumsabfrage → „UDOS PG710-1“; alle Dateien geheim (`CAT … P=&`) | `Prg710Udos.*/Prg710_1` (AP-P3) |
| `prg710-1_scpx17_cpa640_boot.hfe` | **SCPX 1526 V1.7 für den PRG 710-1** (`PRG710-1_SCPX_Boot`, Greaseweazle-Abzug): `scpx640` (16×256, 80 Zylinder beidseitig), Systemspuren = `SYL17` + `CCPBD17` + `B17272V2` („SCPX 1526 - V 1.7 (52K)“); dazu `SYSPRG`, `SYSG`, `FORMAT`, alle BIOS-Module (`B15x` für den 710, `B17x` für den 710-1), Turbo Pascal | `Prg710Scpx.*` (AP-P5e) |
| `prg710_scpx15_cpa640_sysprg.hfe` | **SCPX für den PRG 710, im Emulator erzeugt** (AP-P5e): leere `scpx640`-Diskette → am 710-1 `SYSPRG` mit Tastatur K7609, wobei `B152V24.SYS` als `B17209V2.SYS` bereitlag (das echte Modul fehlt auf allen Disketten) → Systemspuren `SYL17` + `CCPBD17` (V1.7) + `B152V24` („SCPX  V 1.5  B. Daehmlow“); `PIP`, `STAT` per `PIP`, dazu per DiskTool `SYSPRG`, `SYSG`, `FORMAT`, `DU`, `EDIT`, `SUBM`, die Module | `Prg710Scpx.Prg710BootetVonDerSysprgDiskette` |

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

## Warum die PC-1715-Fixture ein `.img` ist

Weil sie es sein DARF, und weil das 640 KB statt 2 MB im Verzeichnisbaum bedeutet.
UDOS1715/NDOS hält die Dateiverkettung in eigenen Zeigersektoren *innerhalb* der
Sektoren — anders als ZDOS auf dem A5120, dessen Kontrollblock hinter der Daten-CRC
liegt und ein rohes Sektorabbild unbrauchbar macht. Genau das prüft
`FsCatalog.Udos1715ProfileSindImgFaehigUndEinseitigGezaehlt` mit; die spurbasierte
Aufnahme derselben Diskette liegt als `disks/udos1715_640k_pc1715_system.hfe` im
Arbeitsverzeichnis. Hintergrund: `doc/udos1715_diskettenformat.md` §8.

## Zwei Fixtures, weil zwei **verschiedene Rechner** dasselbe Dateisystem benutzen

Der **PC 1715** und der **Robotron P8000** sind nicht verwandt — anderer Hersteller-
zweig, andere Bauart, anderer Zweck der Diskette (Systemdiskette gegen
WEGA-Startdiskette).  Gemeinsam ist ihnen nur die Sitte, nach der sie eine Diskette
anlegen; deshalb tragen `udos1715_640k_pc1715_system.img` (PC 1715) und
`udosP8000_640k_wega.hfe` (P8000) dasselbe Dateisystem an denselben Offsets, und
deshalb liegen hier **zwei** Fixtures statt einer.  Die P8000-Diskette
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

`scpx17_cpa780_k5601.hfe` trägt ein **16×256**-System, `scpx17_5x1024_k5601_hardy_norm.hfe` ein
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

## Die EM256-Diskette: Original-BIOS, drei Zeilen angepasst

`cpa_cpa780_k5601_noclock-em256.img` ist `cpa_cpa780_k5601_noclock.img` mit ausgetauschtem
`@OS.COM`.  Gebaut aus `~/projects/CPA_Workbench/src/bc_a5120/bios_org.mac` (Workbench
`bedeb6f`, Robotron-Originalstand mit `em256 equ 1`, `em256adr = 4000H`, `modadr = A8H`) ohne
GUI, mit den Schritten von `tools/cpa_builder.py::build_os` (M80/LINKMT unter `tools/cparun`,
`/p:B980`).  Geändert gegenüber `bios_org.mac` — nur, was die Testmaschine betrifft, nichts
am EM-Teil (`biosremc.mac`/`biosrem.mac` unverändert):
`diskA equ 11580` (statt 10877, 8″), `uhrvar equ 0` (keine Uhrzeitabfrage),
`kltbef: db 0` (statt `SUBM AUTOEXEC`).  Eingespielt mit
`k1520disktool rm/put`.  Kaltstart am A5120 **ohne** EM meldet „RAM-Floppy ?? mit ??? kByte".

## `cpm/em256adr.com`, `cpm/em16abl.com`, `cpm/em256ful.com`: A5120.16-Prüfprogramme

Keine Disketten, sondern CP/A-Programme — **Kopien** der eingecheckten
`tools/em256/*.com` (Quellen `tools/em256/src/`, Bau `python3 tools/em256/build.py`;
bis 2026-10-02 in der CPA-Workbench `tools/16bitTest`).  Sie müssen bytegleich
bleiben: `python3 tools/disketten_beigaben.py --tool build/k1520disktool` zieht sie mit
den Disketten nach, der Wächter `cli_beigaben_auf_den_disketten` prüft es.

- **`em256adr.com`** (G1, doc/design/17_a5120_16.md §3): misst Portbasis und
  Attributspeicher des EM256.  `test_em256_adr` schreibt es auf eine `TempDisk` von
  `cpa_cpa780_k5601_noclock.img` und startet es — mit EM256 (beide `EM::A22Lesart`) und
  ohne (Pfad `EM256ADR_COM` aus `tests/integration/CMakeLists.txt`).
- **`em16abl.com`** (S4-Abnahme/G2-Vorlage): fährt die belegten Abläufe des 16-Bit-Mode;
  `test_em16_abl` (Pfad `EM16ABL_COM`).
- **`em256ful.com`** (v2.0, G2b): Gruppen A–E, im Emulator 21/21; `test_em16_abl`
  (`Em256Ful.*`, Pfad `EM256FUL_COM`), mit EM256 und ohne EM.

## PC 1715 / PC 1715W (AP-0c, 2026-10-03)

Sechs Abzüge für die Etappen des PC 1715 (`doc/design/21_pc1715.md`); Auswahl und Begründung,
Bestand aller übrigen Abzüge: `doc/pc1715/disketten.md`.  Alle `.hfe`, je ~2 MB, nur über
`TempDisk`.  **Bootfähig** heisst hier: Spur 0 Sektor 1 beginnt mit dem Kopf `03 F0`, den der
Urlader S502 prüft (`r1715bt.lst` 008F–0092).  Noch von **keinem Test benutzt** — die
Spalte nennt die Etappe, die sie braucht.

| Datei | Inhalt | gebraucht für |
|-------|--------|---------------|
| `pc1715_scp1715_v0006_boot.hfe` | **SCP 1715 V0006** (03/08/87, 48 KB), Systemdiskette `cpa800` 5×1024, 4 Systemspuren, 9 Dateien (`INIT`, `INSTSCP`, `SGEN`, `PIP`, `POWER` …) | Etappe 2 (Boot bis `A>`) |
| `pc1715_scp1715_v0007_cpa640_boot.hfe` | **SCP 1715 V0007** (01/11/88, 50 KB, nachladbarer `CCP.SPR`), `cpa640` 16×256, 4 Dateien; aus `.scp` gewandelt (`gw convert … ::bitrate=250`) | Etappe 2 (zweites Format, Doppelschritt/16×256) |
| `pc1715_cpa1715_boot_4lw.hfe` | **CP/A 1715** Bootdiskette (`BOOT_CPA_4LW`): `cpa800`, **keine** Systemspuren, `03F0`-Kopf **in Verzeichnisplatz 0**, `@OS.COM` + Quellen/M80 (57 Einträge). Das DiskTool erkennt **kein** Dateisystem (Verzeichnisplatz 0 ungültig) — Prüfstein für AP-D | Etappe 3, AP-D |
| `pc1715_udos1715_system.hfe` | **UDOS 1715 / NDOS**, Systemdiskette „SYSTEM", 80×32×256, 67 Dateien; gleiche Diskette wie `udos1715_640k_pc1715_system.img` (Abzug nicht bytegleich), aber spurgenau | AP-5b |
| `pc1715_cpz22_boot.hfe` | **CP/Z 2.2** („BDOS PC/BC 3/88"), `cpa640` 16×256, 4 Systemspuren, 35 Dateien (Dienstprogramme, TURBO, WS) | Etappe 3 (drittes Betriebssystem) |
| `pc1715w_scp30_system.hfe` | **SCP 3.0 des PC 1715W** („SCP 3.0 – LOADER PC 1715W V0001 25/05/87"): `cpa800`, 4 Systemspuren, 42 Dateien (`SCP3.SYS`, `SC6xx.ZGF`); aus `PC1715W_SCP30.scp` gewandelt, 80 Zylinder (81/82 unformatiert) | AP-W3 |
| `pc1715_cpa1715_workbench.hfe` | **CP/A 1715 aus der CPA_Workbench** (Variante `pc_1715`, `cpa800`, `@OS.COM` 24.05.88 ohne BIOS-Monitor, Bootkopf `03F0` aus `prebuilt/pc_1715`, dazu `PCTEST.COM`, `FORMATP.COM`, M80/LINKMT …); Vorgabe-Konfiguration → fragt beim Start nach der Uhrzeit. `gw convert` aus dem `cpadisk.img` des Diskettenbaus (Workbench-Stand `10d6f1e`) | AP-3b, AP-4d |
