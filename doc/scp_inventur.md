# Inventur SCP/SCPX/CP/M — Betriebssysteme, Programme, Plan für die Systemdisketten

Stand 2026-10-09. Gegenstück zu `doc/disketten_bestand.md` (dort: CP/A). Ziel: je Maschine **eine** SCP-/SCPX-Systemdiskette
in der Auslieferung, gebaut von Skripten (`tools/<…>/build.py` mit `--check`-Wächter, wie `cpa_a5120`/`cpa_pc1715`),
jede mit `LIESMICH.TXT`. Maschinen: **A5120, K8915, PRG 710, PRG 710-1, PC 1715, PC 1715W**.

## 1. Methode und Umfang

Gesichtet wurden alle Abbilder (`.hfe/.img/.dmk`) unter `~/projects/robotron`, `~/projects/CPA`, `~/projects/CPA_Workbench`,
`~/projects/retro`, `~/projects/A5120.16`, `~/projects/a5120emu` sowie `disks/` und `tests/fixtures/disks/` mit
`k1520disktool` (nur lesend: `ls`, `get`, `boot-get`; nichts wurde verändert): 445 Abbilder, davon **266 verschiedene**
(bytegleiche Kopien zusammengefasst), davon **75 SCP-/SCPX-/CP/M-Abbilder** (74 für die Maschinen oben + 1 SCP 1700). Betriebssystem und Fassung wurden
aus Systemspuren und Systemdateien gelesen (Bannertexte), Programme nach **Name + Prüfsumme** unterschieden.
**Volltabelle aller Dateien** (992 Name+Prüfsumme-Paare, 860 Namen; Betriebssysteme, in denen sie vorkommen; Text aus dem Programm):
[`scp_inventur_programme.csv`](scp_inventur_programme.csv). Nicht ausgewertet: CP/A (→ `disketten_bestand.md`), UDOS/NDOS (→
`udos_programme.md`), SCP 1700 (A7100, CP/M-86) und die `.scp`-Flussabzüge (keine Dateisystemsicht ohne Wandlung).

## 2. Betriebssystem-Fassungen

| System | Fassung (aus dem Abbild) | Maschine | Diskettenformat | Abbilder (Beispiel) | Im Bestand ausgeliefert |
|--------|--------------------------|----------|-----------------|---------------------|-------------------------|
| **SCPX 1526 V 1.7 (52K)** | BIOS V 1.7 01.11.87; INIT V 1.5, SYSG V 1.2, MODF/MODX V 1.4, SEPR V 1.6, SYSP „Systemdiskette generieren V 1.7“ | **A5120** | 16×256 (`scpx640`) und 5×1024 (`scpx798`) | `SCPX_BC_ser/par`, `A5120SCPX_scp`, `scpx8inch*`, `A5120_TP_Power_Spiele`, Fixtures `scpx17_*` | `scpx17_cpa780_k5601.hfe` (16×256) |
| **SCPX V 1.5 „B. Daehmlow“** | BIOS `B151V24`, `B152V24`, `B152IFSS` (1K- bzw. 2K-Bildschirm); Systemspuren `SYL17`+`CCPBD17` | **PRG 710** | 16×256 | im Emulator mit `SYSPRG` erzeugt (AP-P5e) | `prg710_scpx15_cpa640_sysprg.hfe` |
| **SCPX 1526 V 1.7 für PRG 710/710-1** | BIOS `B17172xx`/`B17272xx` (xx = V2, ZI, FS, SD), „BW 17“; `SYSPRG` | **PRG 710-1** | 16×256 | `PRG710-1_SCP_Boot_2x80x16x256`, `…_SCP_1_2x80x16`, Fixture | `prg710-1_scpx17_cpa640_boot.hfe` |
| **SCPX 8915 V 5.3** | CCP/BDOS „SCPX V0/2“ (REZ Zella-Mehlis 1988); BIOS-Fassungen **„55 K“ (Disk 900)**, **„V24 XON/XOFF“ (Disk 901)**, 901 mit Autostart `dbase` (Disk 904) | **K8915** | 5×1024 (`cpa800`) | Fixtures `k8915scpx_*` | `k8915scpx_boot1.hfe` (= 901) |
| **SCP 1715 V0004** | 06/12/85, 48 KB (CCP/BDOS V0/4) | PC 1715 | 5×1024 | `scp60_new.img` (Dateien nennen 0004) | — |
| **SCP 1715 V0005/1** | 20/09/86, 48 KB (CCP „SCPX V0/3“) | PC 1715 | 5×1024 | `real_discs/SCP780_*`, `disk_II/IV/IX/XV` | — |
| **SCP 1715 V0006** | 03/08/87, 48 KB | PC 1715 | 5×1024 | `SCP6_PC1715`, `r1715`, `SOFT1715`, `SUPPLIED`, `scp_spiele` … | `pc1715_scp1715_v0006_boot.hfe` |
| **SCP 1715 V0007** | 01/11/88, 50 KB, **nachladbarer CCP** (`CCP.SPR`) | PC 1715 | 16×256 (`scpx640`) | `scp07_td0`, `SCP7_PC1715`, `1715_scp07_boot` | `pc1715_scp1715_v0007_cpa640_boot.hfe` |
| **CP/Z 2.2** | „BDOS PC/BC 3/88“, Variante 03.01.88 | PC 1715 | 16×256 | `CPZ_PC1715` | `pc1715_cpz22_boot.hfe` |
| **SCP 3.0 (CP/M 3)** | Lader „PC 1715W V0001 25/05/87 (C) R-BWS“, `SCP3.SYS`, 14 Zeichensatzdateien `SC6xx.ZGF` | **PC 1715W** | 5×1024 | `PC1715W_SCP30`, `scp3`, `scp30`, `disk_I…XXII` (14×) | `pc1715w_scp30_system.hfe` |

Ergebnis in Worten: je Maschine gibt es **eine** tragende Fassung außer beim **PC 1715**, wo vier SCP-Stände (V0004 – V0007)
und CP/Z 2.2 vorkommen, und beim **K8915**, wo die drei Disketten dieselbe SCPX-8915-Familie mit verschiedener BIOS-Anpassung sind.

**SCPX hat die Variantenwahl eingebaut** (anders als CP/A, wo man mehrere `@OS.COM` ablegt): die BIOS-Fassungen liegen als
`*.SYS`-Dateien auf der Diskette, und ein Generator schreibt das gewählte System auf die Systemspuren — am A5120 `SYSP`
(fragt Tastatur K7606/K7636 · K7637 · BC25, Bildschirm 64×16 · 80×24, Laufwerksanzahl und -typ) bzw. `SYSG` (kopiert Systemspuren
von Laufwerk zu Laufwerk), am PRG `SYSPRG`, am K8915 `DISGEN` (Einstellungen der Fassung, V.24, Laufwerke). Eine SCPX-Systemdiskette
trägt deshalb **alle** BIOS-Module und das Werkzeug, mit dem der Anwender sich sein System erzeugt; die Bootvariante auf
den Systemspuren ist die Vorgabe.

## 3. Systemprogramme je Fassung (aus den Abbildern)

| Fassung | Systemdateien und Dienstprogramme |
|---------|-----------------------------------|
| SCPX 1.7 A5120 | `SYL17.SYS`, `CCPBD17.SYS`, `BIOSG617/G717/K617/K717.SYS`; `SYSP`, `SYSG`, `INIT`, `MODF`, `MODX`, `SEPR`, `PIP`, `STAT`, `POWER` (+ `HARDY` auf der 5×1024-Diskette; Spiele/TP auf `A5120_TP_Power_Spiele`) |
| SCPX PRG 710 / 710-1 | `SYL17`, `CCPBD17`, BIOS-Module (s. §2), `SYSPRG`, `SYSG`, `FORMAT`, `PIP`, `STAT`, `SUBM`, `EDIT`, `DU` (+ auf der 710-1-Diskette: `ASM`, `LINK`, `LIB`, `DIMA`, `SDIR`, `MODF`, `POWER`, TP, `TURBO`, `INSTALL`, `CODP`, `CONV1`, PROM-Dateien) |
| SCPX 8915 | `DISGEN`, `FORMAT`, `POWER`, `STAT`, **`RADE.COM`** (RAM-Disk E:, v 1.5, 16.10.1989, nutzt die zweite Speicherbank — auf 901/904, nicht auf der 55-K-Fassung 900), TP 3.0 |
| SCP 1715 V0004–V0006 | `INIT`, `SGEN`, `INSTSCP`, `DISKPAR`, `KEYS`, `SCP-DOS`, `SCP1715`, `PIP`, `POWER`, `STAT`, `TLC` (+`TLC.PAR`), `DIMA`, `DISKCOPY`, `UNERA`, `XDIR`, `XSUB`, `SUBM`, `PCTEST` |
| SCP 1715 V0007 | `CCP.SPR`, `INIT`, `INSTSCP`, `SGEN` (Datendisketten tragen meist nur diese vier) |
| SCP 3.0 | `SCP3.SYS`, `CCP.COM`, `COPYSYS`, `INIT`, `INITDIR`, `PIP`, `SET`, `SETDEF`, `DEVICE`, `SHOW`, `DIR`, `TYPE`, `ERASE`, `RENAME`, `SAVE`, `GET`, `PUT`, `DUMP`, `GENCOM`, `LINK`, `SUBMIT`, `SPACE`, `HELP`, `DATE`, `MODCS`/`LOADCS.RSX`/`MODIO`/`MODFD`, `SC6xx.ZGF`, `PROFILE.SUB`, TP-Familie |
| CP/Z 2.2 | `CPZINIT`, `CPZGEN`, `CPZDUP`, `CPMADR`, `CPMSTAT`, `DISKCOPY`, `DIMA`, `DIP`, `DOCTOR`, `DU`, `FDIR`/`SDIR`/`UDIR`/`XDIR`, `GIDE`/`GIDETEST`, `NSWEEP`, `SH`, `STAT`, `TURBO`, `WS330`, `ZAP`, `ZEXALL`, `ZSLAP`, … |

## 4. Weitere Programme nach Art (Volltabelle: CSV)

Quer über alle Systeme gibt es:
- **Sprachen/Entwicklung:** `BASIC`/`BASI`/`MBASIC`/`MBASICF1` (Interpreter, R-BWS 12/1984), `BASCOM`+`BASLIB.REL`+`BRUN` (Compiler, läuft am 1715 nur im Verbund mit `L80`+`BCLOAD`), `PASCAL`+`PASSAVE`+`PASINST` (Turbo/+ 1987 für den 1715), `TURBO`+`TURBO.MSG`/`.OVR` (Turbo Pascal 2.00A/3), `F80`+`FORLIB`, `CC`/`CC2` (C), `M80`/`L80`/`LINK`/`LIB`/`RMAC`/`LINKMT`/`MLOAD`, `ASM`, `Z1`, `ZSID`/`SID`/`ZAP80`.
- **Text:** `WM` (WordMaster 1.07 + russische Hilfe), **TP** (Robotron-Textprozessor in mindestens vier Ständen: 1.3 „ROBOTRON“ für A5120/30 unter SCPX, 1.3 „NKM“ (PRG-710-1-Diskette), **3.0 „Anpassung an CP/A“ 20.04.88**, SCP-3.0-Fassung `TP120`/`TPG`/`TPK`; dazu „Textprogramm (deutsch) V1/3 09/85 für SCPX-RBWS“ auf Disk 904) samt `TPHT`/`TPOVLY*`/`TPDRUCK` und Installern (`INSTALL`, `TPINSCPA`, `TPINSTD`), WordStar (`WS330`).
- **Dienst:** `PIP`, `STAT`, `POWER`, `DIENST`, `DU`, `SDIR`/`XDIR`, `DIMA`, `DISKCOPY`, `UNERA`, `DUMP`, `SUBM`/`XSUB`, `CLS`.
- **Kommunikation/Prüfung:** `TLC`, `SERTEST` (eigen), `PCTEST` (Robotron, 1715-Werkstest), `HARDY` (Rechnertest), `RAMTEST`, `EM*` (eigen), `RAFCPM`/`RAF512`/`RAFTEST`/`RAFQUICK`, `LBREAD`/`LBPUNCH` (eigen).
- **Datenbank/Anwendung:** `DBASE`+Overlays, `REDABAS`, `KP`, `SC` (Tabellenkalkulation), `REFOR*` (Formulargenerator), Anwendungs- und Spieldisketten (viele Spiele- und Beispielprogramme, überwiegend auf den 1715-Disketten `scp_spiele`, `SUPPLIED` und auf `A5120_TP_Power_Spiele`).

## 5. Analyse: was passt zu welcher Maschine

Maßstab: läuft das Programm **dort** (am Emulator geprüft oder eindeutig an die Hardware gebunden), nützt es dem Anwender der Auslieferung,
und ist die Herkunft in dieser Sammlung belegt (Fremdsoftware nur, wenn sie ohnehin schon auf einer ausgelieferten Diskette liegt).

### A5120 — SCPX 1.7 (`scpx17_cpa780_k5601.hfe`, 16×256)
- **Bleibt/ist da:** alle vier BIOS-Module + `SYSP`, `SYSG`, `INIT`, `MODF`, `MODX`, `SEPR`, `PIP`, `POWER`, `STAT`, `LBREAD`, `LBPUNCH`.
- **Neu (vom Anwender verlangt):** **EM256-Programme** `EM256ADR`, `EM16ABL`, `EM256FUL`, `EM256TST` — gebaut für CP/A, **unter SCPX noch nicht geprüft** (CP/A-Adressen im BIOS?); Test im Emulator gehört zum Auftrag.
- **Vorschlag:** `SERTEST` (V0.3), `ROMREAD`, `RAFCPM`/`RAF512`/`RAFTEST`/`RAFQUICK`, `HARDY` (liegt auf der 5×1024-Diskette; Format entscheiden), `TP 3.0`+Installer, `WM` (+deutsche Hilfe), `DIENST`, `TLC`, `BASIC`, `PASCAL`/`TURBO`, `M80`/`LINK`. TP/WM/Sprachen sind CP/M-2.2-Programme und liefen auf CP/A; unter SCPX Test nötig.
- **Offen:** 16×256 oder 5×1024 als Auslieferungsformat? (Die 5×1024-Diskette trägt `HARDY`; SCPX 1.7 liest beide, MODF stellt um.)

### K8915 — SCPX 8915 V 5.3 (`k8915scpx_boot1.hfe` = Fassung 901)
- **Ist da:** `DISGEN`, `FORMAT`, `POWER`, `RADE` (**RAM-Disk E: mit der zweiten Speicherbank — gefordert, vorhanden**), TP 3.0, `SERTEST`, `RAFCPM`/`RAF512`, `LBREAD`/`LBPUNCH`.
- **Fehlt:** `STAT` (nur auf einem Teil der Fassungen), `PIP`, ein Editor/Entwicklungsumgebung. Vorschlag: `STAT`, `PIP`, `WM`, `DIENST`, `BASIC`, `PASCAL`/`TURBO` nach Test.
- **Fassungen:** 55 K (900) hat **kein** `RADE`. Vorschlag: Auslieferung = 901 (V24 XON/XOFF) mit `RADE`; die 55-K-Fassung bleibt Fixture. [Zu klären: soll die 55-K-Variante als zweites BIOS-Modul auf der Diskette liegen? `DISGEN` stellt keine Fassung um — die BIOS-Fassung steckt auf den Systemspuren.]
- **Gen 2 (V2, 64 KB, K3528):** hat nur 64 KB, also keine zweite Bank — `RADE` entfällt dort.

### PRG 710 — SCPX V 1.5 (`prg710_scpx15_cpa640_sysprg.hfe`)
- **Ist da:** `B151V24`, `B152V24`, `B152IFSS`, `SYSPRG`, `SYSG`, `FORMAT`, `PIP`, `STAT`, `SUBM`, `EDIT`, `DU`, `LBREAD`/`LBPUNCH`.
- **Vorschlag:** `TP`-Fassung „NKM“ (liegt auf der 710-1-Diskette; Eignung für den PRG-Bildschirm noch zu prüfen), `DIMA`, `POWER`, `SDIR`, `ASM`/`LINK`/`LIB`, Turbo Pascal, `RAFCPM`/`RAF512`. **`SERTEST` kennt den PRG nicht** (SIO-Lage anders; eigener Zweig nötig) — als offene Frage an den Anwender.

### PRG 710-1 — SCPX 1.7 (`prg710-1_scpx17_cpa640_boot.hfe`, 61 Dateien, 134 KB frei)
- **Ist da:** alles aus der 710-Diskette plus alle V1.7-Module `B17172xx`/`B17272xx`, `INSTALL`, TP, `TURBO`, `ASM`, `LINK`, `LIB`, `DIMA`, PROM-/Entwicklungsdateien (`LC80PRG.DAT`, `ROM*.DAT`, `HIST.UTL`, `TRACE.UTL`, `DIT`).
- **Vorschlag:** Aufräumen (Anwenderdateien `KLINGEL.DAT`, `RITE.DAT`, `ROM0.DAT`, `0001.DAT`, `001`, `POM02.001`, `ROM01/02.001`, `DIT.*`, `LC80PRG.DAT` sind Anwenderdaten, keine Systemprogramme), `LIESMICH.TXT`. Die V1.5-Module liegen auf beiden PRG-Disketten (710-1 trägt sie mit); die Trennung 710/710-1 ist die Frage „eine gemeinsame PRG-Diskette oder zwei“ (Vorschlag: zwei, wie gefordert).

### PC 1715 — SCP 1715 (V0004 – V0007) und CP/Z 2.2
- **Ist da (V0006):** `INIT`, `SGEN`, `INSTSCP`, `PIP`, `POWER`, `STAT`, `DIMA`, `DISKCOPY`, `UNERA`, `PCTEST`. Die SCP-Disketten der Sammlung (`SOFT1715`, `SUPPLIED`, `scp_spiele`) tragen die 1715-eigene Software: `BASIC`, `MBASIC`, `BASCOM`, `PASCAL`, `F80`, `CC`, `TLC`, `DBASE`, `SC`, `REFOR`, Spiele.
- **Vorschlag:** **eine** SCP-1715-Diskette (V0006 als Standard, weil emuliert und getestet) mit Werkzeugen und Sprachen (`BASIC`, `PASCAL`, `TLC`, `M80`/`L80`, …) und `SERTEST` (V0.2 läuft unter CP/A am 1715; unter SCP zu prüfen); V0007 ist eine **zweite Fassung mit anderem Format** (16×256, nachladbarer CCP), V0004/V0005 sind ältere Stände ohne Systemdiskette in der Sammlung. CP/Z 2.2 bleibt eigene Diskette (anderes Betriebssystem, `CPZGEN`).
- **Offen:** Genügt „V0006 + V0007 + CP/Z“ als Auslieferung (drei Disketten für drei Betriebssysteme), oder soll V0007 entfallen? Die Zielvorgabe „je Maschine × Betriebssystem eine Diskette“ spricht für drei.

### PC 1715W — SCP 3.0 (`pc1715w_scp30_system.hfe`)
- **Ist da:** das SCP-3.0-System samt Zeichensatzdateien; Werkzeuge nur die des Systems (`GET`/`PUT`/`SET`/`SHOW`/`LINK` …), keine Sprachen, kein Editor.
- **Auf Disketten der Sammlung (SCP 3.0, 14 Abbilder):** `TP120`/`TPG`/`TPK`+Installer `TPINSTD`, `RDBAS*` (REDABAS), `DRUCK`, `SDIR`, `R`; die SCP-3.0-Programmdiskette (`PC1715W_Programme`, `disk_V/XI/XIV/XXII`, 58 Dateien) ist die ergiebigste Quelle.
- **`SERTEST` für den 1715W: eigener Zweig, wird entwickelt** (Auftrag an einen Sonnet-Agenten; läuft parallel).

## 6. Bauplan (nach Freigabe dieses Papiers)

1. **Sechs (bzw. acht, s. PC 1715) Bauskripte** nach dem Muster `tools/cpa_a5120/`: `tools/scpx_a5120/`, `tools/scpx_k8915/`, `tools/scpx_prg710/`, `tools/scpx_prg710_1/`, `tools/scp_pc1715/` (V0006 [+V0007]), `tools/scp_pc1715w/`; je `build.py` (+`--check`, Wächter in `tests/cli/CMakeLists.txt`), `inhalt/`, `README.md` (Herkunft + Prüfnachweis), `inhalt/LIESMICH.TXT`. Systemspuren aus `disks/boot_*.bin` bzw. dem `boot-get` des heutigen Abbilds (SCPX braucht die Systemspuren plus ein gültiges Dateisystem).
2. **Namensschema** `<maschine>_<system>_<…>_system.hfe` (`a5120_scpx17_…`, `k8915_scpx8915_…`, `prg710_scpx15_…`, `prg710-1_scpx17_…`, `pc1715_scp1715_…`, `pc1715w_scp30_…`); Altnamen werden umbenannt, Fixtures behalten ihre Namen.
3. **Jede Diskette wird im Emulator gestartet und ihre Programme werden angefasst**, wie bei den CP/A-Disketten (Nachweis in der README des Bauordners).
4. `SERTEST` kommt je Maschine erst dazu, wenn es dort geprüft ist (1715W: nach dem Agentenauftrag; PRG: offen).

## 7. Offene Fragen an den Anwender
1. A5120: Auslieferungsformat **16×256** (`scpx640`, heute) oder **5×1024** (`scpx798`, mit `HARDY`)?
2. K8915: nur **901** (V24, mit `RADE`) oder zusätzlich die 55-K-Fassung?
3. PC 1715: **drei Disketten** (SCP V0006, SCP V0007, CP/Z 2.2) oder weniger?
4. PRG 710 / 710-1: soll es einen **SERTEST** für die PRG-Hardware geben (zweiter Agentenauftrag)?
5. Dürfen die Anwenderdaten auf der 710-1-Diskette (`KLINGEL.DAT`, `RITE.DAT`, `ROM*.DAT` …) auf der ausgelieferten Fassung entfallen?
