# P8000 — Quellenkatalog (AP0)

Stand 2026-10-06. Nur Befunde; was nicht gefunden wurde, steht als „nicht gefunden".
Gerät: Robotron/EAW P8000 = 8-Bit-Teil (U880, Monitor MON8, WDC, Terminal) + 16-Bit-Teil
(U8001, 3× U8010-MMU, Monitor MON16), Betriebssystem WEGA (UNIX System III-Derivat) bzw. UDOS 2.2/3.x.

## 0. Wichtigste Befunde

1. **Alle Original-EPROM-Abzüge sind frei im Netz** (pofo.de „EPROM-Sammlung") und liegen jetzt
   unter `doc/p8000/eproms/` (77 Dateien, 720 KB, SHA-256 in `eproms/SHA256SUMS.txt`).
   MON8 3.1, MON16 3.1 und WDC 4.2 stimmen **bit-genau** mit den MAME-Hashes (`skeleton/p8k.cpp`)
   überein, also unabhängig bestätigt.
2. **Quelltexte der Firmware** (MON8, MON16, WDC 4.2, Terminal 4.2/5.0, sa.diags = Hardwaretest) und
   des **WEGA-Kernels** (teils Original, teils rückübersetzt) liegen auf GitHub `OlliL/P8000`
   (keine Lizenzdatei). Enthält auch den Boot-Pfad (`boot0.*.s`) und die Kernel-Treiber (`uts/dev/*`).
3. Lokal gibt es **keinen** ROM-Abzug, aber echte Start-Datenträger: WEGA-Startdiskette (UDOS 2.2,
   Urlader `boot0.*`, `boot`, `wega`-Kernel, `sa.*`) und die komplette WEGA-3.0-Sammlung.
4. MAME hat `p8000`/`p8000_16` nur als **Skeleton, MACHINE_NOT_WORKING** (2009, Fabio Priuli). Kein
   Referenzemulator mit MMU/WDC/WEGA-Boot. Z8001-Kern im Repo ist vorhanden.
5. **Nicht gefunden:** WDC-/Terminal-Hardware-Dokumentation außer Hardwarehandbuch (dieses enthält aber
   Kap. „Winchester-Disk-Controller Firmware"); MMU-Registerbelegung muss aus Hardwarehandbuch + Quellen
   (`mmu.h`, `p.init.s`, `mch.s`) erarbeitet werden. Kein WEGA-Festplattenabbild (nur Disketten + 
   Installations-ZIP von pofo.de, s. 2.2).

## 1. Lokale Quellen

### 1.1 `~/projects/robotron/P8000/` (732 MB)

| Pfad | Art | Größe | Inhalt | Eignung |
|---|---|---|---|---|
| `doc/P8000_Hardwarehandbuch.pdf` | PDF, 261 S. | 452 K | EAW-Hardwarehandbuch V1.3 (Stand 06/1988): Konstruktion, Stromversorgung, 8-Bit-Rechner, Floppy, **16-Bit-Rechner Index 1 + 4**, DRAM-Karte, Terminalrechner (+Tastaturen, Varianten), Winchester-Beisteller, **WDC-Firmware (Kap. 5)**, Kennzeichnung/Leiterplattenindex | **Hauptquelle** für Adress-/E/A-Belegung, Karten, WDC-Protokoll. Text per `pdftotext` durchsuchbar |
| `doc/WEGA_Systemhandbuch.pdf` | PDF, 110 S. | 272 K | WEGA-Systemhandbuch V1.2 („IS/M-Software") | Systemgenerierung, Gerätedateien, Plattenaufteilung |
| `doc/Einfuehrung_in_die_Software.pdf` | PDF, 92 S. | 224 K | Einführung Software (Ausgabe 12/86): UDOS-Dienste, Monitor, Boot | Monitorbefehle, Bootweg |
| `doc/install_WEGA_3.1.log` | Text | 44 K | Konsolenmitschnitt: Hardwaretest 3.1, Monitor, `sa.format` (WDC_4.2, Laufwerksliste K5504.50 = 1024 Zyl/5 Köpfe/18 Sekt.), mkfs, WEGA-Installation | **Sehr gut** als Soll-Ablauf für Systemtest |
| `8bit_Schaltplan/` | 5 TIFF (Stromlaufplan 01–04, Leiterplatte), `Stromlaufplan_03.ps` 4,2 M, `Stueckliste.pdf` 4,7 M | 11 M | 8-Bit-Rechner (ZRE) | Bilder, keine Auswertung (Auftrag) |
| `16bit_Schaltplan/` | `Stromlaufplan_Index_1_05…14.tif` (10 Blatt), `Index_4_01…14` (14 Blatt; 4_02, 4_04, 4_11 je in zwei Ständen 1988-07-01 und 1989-03-22/07-19 als .jpg), `Stromlaufplan_GLE_01/02.jpg`, `Leiterplatte_Index_1_01/02.tiff` | 17 M | 16-Bit-Rechner Index 1 und 4 (MMU, CPU, Speicher, Bus), GLE = Grafik-Terminalkarte | Index 4 ist der spätere Stand; Index-Unterschiede in 4_02/4_04/4_11 beachten |
| `discs/WEGA3.0/` | 17 × `.cqm` (CopyQM, 380–630 K) + 17 × `_cqm.scp` (Flux, ~38 MB) | 660 M | siehe 1.2 | CQM genügt; .scp nur als Rohflux |
| `pic/` | 8 JPG | 45 M | Fotos (2024-11-25) des Geräts | nur Orientierung |

### 1.2 WEGA 3.0 Disketten (`discs/WEGA3.0/`)

Beschriftung im CQM-Kopf: `w30start`/`w30strt2` = „P8000SYS"; strt2 trägt den Kommentar
„P8000-SOFTWARE WEGA-Startdisk. 889 347/3.0 PRS.: 52290". Format 640K DS (16×256) bzw. 720K DS.

| Datei | Zweck |
|---|---|
| `w30start`, `w30strt2` | UDOS-Startdisketten (Urlader, `sa.*`, Kernel `wega`) — Zweitstand strt2 |
| `w30root1…5` | WEGA-Wurzeldateisystem (root1 trägt Boot-Block/„boot") |
| `w30usr1…9` | /usr-Dateisystem |
| `w30doc1` | Dokumentation (man/doc) |

Es sind CopyQM-Abbilder; das DiskTool liest sie nicht direkt, Konvertierung (`libdsk`/`dsk2…`) nötig.
Die Abbilder wurden nicht geöffnet (nur Kopfdaten gelesen).

### 1.3 `~/Documents/K1520emu/Disketten/`

| Pfad | Inhalt | Eignung |
|---|---|---|
| `P8000/` (vom DiskTool extrahiert, 42 Dateien + `.fileinfo`) | **Ja, extrahierte WEGA-Startdisketteninhalte.** `boot` (6790 B) = Z8001-Zwischenlader („boot", Text: `md(0,16000)wega`, `BOOTING FROM …`); `boot0.fd/.rm/.ud` (466/532/508 B) = Urlader-Sektor 0 für Floppy/Remote/UDOS-Floppy; `wega`, `wega.n26`, `wega.n52` (je 96 400 B) = WEGA-Kernel (3.2, 12.02.88; Plattenaufteilung /usr 0/13000, swap 13000/3000, / 16000/7000, /tmp 23000/4000, /z 27000/60732 Blöcke; Kernel-Speicher 139 520 B); `sa.diags` (25 598 B, „P8000 Hardwaretest U8001 V1.0", enthält U8000-Softwaremonitor-Meldungen), `sa.format`, `sa.install`, `sa.mkfs`, `sa.verify`, `sa.shipdisk`, `sa.cat` = Standalone-Programme (Z8001); `FORMAT/STATUS/SET…` = UDOS-2.2-Z80-Dienstprogramme; `README` (27 K), `INHALT` (190 K), `udos-dateiangaben.txt` | **Sehr gut.** `boot0.*` + `boot` + `wega` + `sa.*` = vollständiger Bootweg ohne Festplatte |
| `*.fileinfo` | DiskTool-Sidecar (`fs=udos name= typ= eig= start= satz= block= rest= segment=0000:6790 mem=… erst= geaend=`); Angaben, die UDOS im Verzeichnis führt, damit `put` sie wiederherstellt | Rein Werkzeug; `segment`/`mem` zeigen Ladeadresse |
| `P8000_WEGA-STARTDISKETTE.zip` (648 K) | `.hfe` (2 008 064 B, k5601_16x256, UDOS 2.2) + `.txt` (Verzeichnis, 42 Dateien, 633 KB belegt) + `diskarchive.yaml` + `dateien/` (dieselben Dateien) | Sicherung der echten Diskette (Greaseweazle) |
| `udos_P8000.hfe`, `udos_P8000_neu.hfe` | 2 008 064 B, DiskTool-Arbeitsstände der Startdiskette | Arbeitskopie |
| `001/`, `003/`, `005/`, `PRG/` | andere Maschinen | nicht P8000 |

Header der Z8001-Dateien (`boot`, `sa.*`, `wega`): UDOS-Ladeformat mit Segment-Header (z. B. `wega`: `e611 0001 …`;
`boot`: `e707 0000 1a5e …`) — Aufbau noch nicht entziffert (offen für AP1; Quellen: `s.out.h`, `z.out.h` im GitHub-Repo).

### 1.4 Weitere lokale Fundstellen

| Pfad | Inhalt | Eignung |
|---|---|---|
| `~/projects/prg710/disks/P8000_UDOSwegaBOOT01.scp` (38 M) | Flux der Startdiskette (UDOS „wega"-Boot) | Rohflux, redundant zu .hfe |
| `~/Documents/obsidian/notes/P8000.md` (8 K) | Eigenes Protokoll des echten Geräts (9600 8N1 an tty1; K5504.50): **Hardwaretest U880 V3.0 / U8001 V3.0**, Monitor-Dialoge, Fehler 52/53 (RAM-Test AAAA/5555), `sa.format`/`sa.verify`-Läufe mit WDC_V.3.4.05, Plattenfehler 81 / Blinkcode, Bad-Track-Tabellen; Links pofo.de, ycdt | **Gut**: Referenzausgaben des echten Geräts (Monitorprompts, Texte) |
| `~/projects/UDOS/UDOS/P8000/` | UDOS-2.2-Originale vom P8000: `FORMAT.000/.400/.MAC` (Formatierer, TPA 4000H; Fehlertexte „UNKNOWN FORMAT, PLEASE USE SETFD"), `STATUS(80).000/.400/.COM/.MAC` | Z80-Teil; Formatierlogik des 8-Bit-Teils/UDOS |
| `~/projects/UDOS/README.md` (37 K) | UDOS-Quellenfundus; P8000 in §5.6 | Verweisquelle |
| `~/projects/robotron/A7100/Doku/A7100_SCP1700_und_UDOS_P8000_Hand_Buecher/` | `UDOS-Software Mikroprozessorsoftware.pdf`, `UDOS-Software Programmiersprachen.pdf`, `Begleitzettel.pdf` (+ SCP1700-Handbücher) | UDOS-Handbücher (Assembler U880, U8000-Cross) |
| `~/projects/robotron/A7100/` (JPG 43 Stk.) | Fotos A7100 | irrelevant für P8000 |
| `~/projects/robotron/MUTOS/` + `~/projects/robotron/A5120/discs/MUTOS8000*` | MUTOS 8000 (Z8000-UNIX für A5120.16/EM): Handbuch-PDFs, Disketten (`M8KROOT/USR/BIN/CMD` .TD0/.scp/.img) | **Verwandtes** Z8000-UNIX (ZRE-Variante, andere MMU); als Vergleich für Z8001-OS-Code |
| `~/projects/A5120.16/docs/` | Z8000-Reference-Manual (txt), 16-Bit-ZRE-Beschreibung K1520, tiffe-Mirror inkl. `MON8000Systemhandbuch.pdf` (Monitor der EM-Karte, **nicht** MON16 des P8000) | Z8001-Befehls-/MMU-Referenz |
| `~/projects/CPA_Workbench/tools/16bitTest/` | Z8001-Assembler `z8001asm.py` (82 K), `build.py`, Firmware `fw_*.s`, Testprogramme `em256*.mac`, `docs/Z8000 CPU User's Reference Manual.md` | Werkzeug: Z8001-Programme bauen |
| `~/projects/K1520_rebuild/U8000-CPU.pdf` | U8000-CPU-Datenblatt | Referenz |
| `~/projects/robotron/Robotron_Technik/www.robotrontechnik.de/html/computer/p8000.html`, `p8000compact.html`, `eigenbau/p8000ram.html`, `zubehoer/epromer.html`, `…/bilder/Sonstige/P8000/…` | lokaler Mirror der robotrontechnik.de-Seiten (Beschreibung, Screenshots, RAM-Eigenbau, EPROMmer) | Beschreibung, keine Dumps |
| `~/projects/robotron/tiffe_de_Robotron/` | tiffe.de-Mirror; P8000: nur über `MON8000…` (A5120.16) — kein P8000-Eintrag gefunden | — |
| `~/projects/eprommer/eprommer.md`, `~/projects/robotron/GOTEK/dsk.txt` | erwähnen „Hardwaretest" nur nebenbei | irrelevant |

### 1.5 Im Repo

| Pfad | Inhalt |
|---|---|
| `tests/fixtures/disks/udosP8000_640k_wega.hfe` (2 008 064 B) | WEGA-Startdiskette (UDOS 2.2) als Fixture; Wächter `Udos1715P8000.*` |
| `doc/udos1715_diskettenformat.md` | §3.0a, §8: P8000-Diskette = NDOS-Dialekt wie PC 1715, Füllbyte `77H`, `179H=01`, Systembereich = ganzer Kopf 0 der Spuren 0/21/22/23; 2560/2560 Sektoren am echten Laufwerk verifiziert |
| `core/primitives/z8000.{h,cpp}` + `z8000/` | Z8001/2-Kern (Buszyklus mit Status ST0..3, N/S, SN0..6, Befehlstabelle, MAME-Oracle-Test `tests/oracle/`). Schnittstelle: `read`/`write`-Rückrufe je Buszyklus — MMU als Karte außen davor möglich (so am A5120.16 gelöst). MMU-Modell (U8010) fehlt |
| `core/cards/em/`, `doc/design/17_a5120_16.md` | EM256-Karte (U8001 am K1520) — Vorbild für Segmentweiche/RAM |
| `doc/trascripted/CPU_U8001_8002.md` | Transkription Datenblatt U8001/8002 |

### 1.6 Funde per Suche (EPROM/ROM lokal)

| Gesucht | Ergebnis |
|---|---|
| „U880-Softwaremonitor", „U8000-Softwaremonitor", MON8/MON16-Abzug | lokal **nicht vorhanden**; nur Textstrings in `Disketten/P8000/sa.diags`/`boot` und Mitschnitt `P8000.md`. Jetzt aus dem Netz beschafft (2.1) |
| „Hardwaretest" | als Programm `sa.diags` (Z8001, 25 598 B, V1.0 auf der WEGA-3.0-Startdiskette) und als Teil der Monitor-ROMs (V3.0/3.1 „Hardwaretest U880/U8001") |
| WDC-Firmware `WDC_V` | lokal nicht als Abzug; Version steht im Mitschnitt (`WDC_V.3.4.05`, später 4.2 = `WDC_4.2`) |
| Terminal-/Zeichensatz-ROM (P8T) | lokal nicht vorhanden (die gefundenen `*zg*.bin` gehören PC1715/K7024/KC) |
| ZRE-/Terminalrechner-ROMs | lokal nicht vorhanden |

## 2. Web-Recherche

### 2.1 pofo.de — Olli Lehmann (OlliL), die zentrale Quelle

Server: `http://www.pofo.de/` (HTTPS liefert falsches Zertifikat; HTTP genügt, `curl -k`).
Lizenz: kein Hinweis auf Impressum/Seiten; private Archivseite, Material unter Original-EAW-Urheberrecht
(Handbücher mit „Nachdruck nur mit Genehmigung"). Für Einzelnutzung/Emulatorbau üblich, **Weiterverteilung im Paket
vorab klären** (Notiz für `doc/design`).

| URL | Inhalt | Download |
|---|---|---|
| `/P8000/misc/EPROM-Sammlung/` | **alle EPROM-Abzüge**: `8BIT/` (MON8 2.1/3.0/3.1 je 2×4 KB, `*_64KB`), `16BIT/` (MON16 2.1/3.0/3.1/3.3 je 1L/1H/2L/2H, `rom.64511`, `full/` = zusammengeführte 16-KB-Fassung, `make_full.sh` = Zusammenführregel L/H-Interleave), `WDC/` (2.4 bis 4.2, je 2×4 KB; `eprom_diffs.txt`), `TERMINAL/` (P8T 1.3.1, 1.4.1, 5.0, 6.0 + ZG1/ZG2; Zeichensätze P8TDZS deutsch, P8TEZS englisch), `KEYBOARD/` (K7673.01/.09, K801.02), `HDD/` (Platinen-EPROMs HH-1090, ROBOTRON VS3, ST251), `EMR/` (EMR-Emulator V9.1) | **ja, alles geladen** → `doc/p8000/eproms/` (ohne `WDC/nonworking`, `WDC/selfmade`) |
| `/P8000/misc/WEGA_3.1.20160725.zip` (5,99 MB) | WEGA-3.1-Systemabbild (für WDC-Emulator/SD-Karte) | ja, **nicht** geladen (zu groß für Repo; Anwender: s. 3) |
| `/P8000/misc/emulation/P8000emu_0.99beta.zip` (11,8 MB) | Matt Knoth „P8000emu" 0.99 beta (Windows-MSI, Binär, Quelle nicht dabei), 2009; Quelle der MAME-Skeletons; `knothusa.net` nicht erreichbar | ja, nicht geladen; geschlossen |
| `/P8000/misc/floppies/` | CopyQM: `epromimg.cqm` (EPROM-Inhalte als Floppy), `k5jb01/02.cqm` (K5JB-TCP/IP), `webackup.cqm` (Kermit, Backup-Skripte), `copyqm.zip` | ja |
| `/P8000/misc/sources/tools/` | C-Werkzeuge: `z8dis.c`, `z80dis.c` (Disassembler), `read_cqm.c`, `extract_cqm_udos.c`, `makeudos_cqm.c`, `dumpsout/` | ja |
| `/P8000/misc/releases/` | `timer-1.0/1.1` (Uhr-/RTC-Software) | ja |
| `/P8000/notes/books/…` | Handbücher je Jahrgang: Hardwarehandbuch (1988_06), Einführung (1986_12, 1989), IS/M-Systemhandbuch, **WEGA_Systemhandbuch (1988_01, 1989_01)**, WEGA_Dienstprogramme A–D, WEGA_Cross_Software, WEGA_DATA, WEGA_EMSCP/EMUDOS, WEGA_DOS, WEGA_WORD, UDOS_Systemhandbuch/Dienstprogramme/Mikroprozessorsoftware, OS_M, **WDOS_Erweiterungsmodul_Ergebnisbericht** | ja (PDF) |
| `/P8000/notes/plaene/` | Schaltpläne: `8-Bit-Rechner/`, `16-Bit-Rechner/` (GLE, Index_1, Index_4), `Winchester-Disk-Controller/` (Index_1, **Index_3**: 9+ JPG), `ZRE_U8000/` (3 TIFF), `Anzeige/`, `Dynamischer-RAM/`, `Tapeinterface/`, `Uhr/`, `WDOS/`, `Stromversorgung-*`, `z8000-Programmer…`; eigene: `eigene/P8000_WDC_Emulator/` | ja |
| `/P8000/notes/` | `EA_Bus_fuer_P8000.pdf`, `MP_1987.pdf`, `P8000_FAQ.txt`, `UDOS/WEGA_README_3.0/3.1`, `WEGA_3.0_to_3.1.txt`, `Z8000_ASM_Ch12.pdf`, `Ms_nummern.txt`, `U2164C_DRAM.pdf`, Kurzinfos | ja |
| `/P8000/notes/install/` | `install_WEGA_3.1.log`, `disks.txt`, `harddisks/` | ja |
| `/P8000/wdcemu.php`, `rtc.php`, `kernel.php`, `/P8000/arch/` | WDC-Emulator (AVR), RTC-Karte, Kernel-Quellenübersicht, Notizarchiv | Web |
| `/S8000/` | Zilog S8000 (Vorbild/Verwandte) | Web |
| `http://pics.pofo.de/P8000/` | Fotogalerie, inkl. Platinen | Web |

### 2.2 GitHub

| URL | Inhalt | Lizenz |
|---|---|---|
| `https://github.com/OlliL/P8000` (master; letzter Push 2016-07) | `firmware/MON8` (U880-Monitor, ~110 KB Quelle, `K.MON8.S`, `U880SM.S`, `FLOPPY.S`…), `firmware/MON16` (U8000-Monitor, `p.init/boot/ldsd/gesa/term/test/brk/comm/disk/ram/crc.s` + Kommentar-Docs `mon16.doc`, `mon16.kp.doc`, `mon16.crc.doc`), `firmware/WDC` (`wdc.firm.s` 88 KB + README + `z80-asm.patch`), `firmware/TERMINAL` (P8T 4.2/5.0), `inh.p8000.doc`; `WEGA/src/uts` (**Kernel**: conf, dev, sys; MMU in `sys/mkseg.c`, `conf/mch.s`, `head/sys/mmu.h`), `WEGA/src/cmd` (inkl. `BOOT0/boot0.{fd,md,rm,ud}.s`, **`standalone/sa.diags`** 8 Quelldateien ≈ 300 KB, `sa.format/mkfs/install/verify`), `WEGA/src/lib/libc`, `WEGA/src/head` (Header), `UDOS/WEGA` (Koppel-/Prom-Quellen), `WEGA/contrib` (u. a. `eigentest/s.u880tr.s`, K5JB, Z8000-C-Compiler `lcc`) | keine Lizenzdatei (`license: null`); Original-Quellen teils ZFT/KEAW, teils rückübersetzt |
| `https://github.com/OlliL/P8000_WDC_Emulator` (Zweige 1.x, 2.x, master; 2016-08) | AVR-Firmware, Schaltpläne (Layout-Dateien `.sch/.brd/.net`, PDF), **Signalmitschnitte** des WDC-PIO-Protokolls (`SignalCaptures/*_CMD_21/22/28_OK.vcd/.csv`) | keine Lizenzdatei, „freely available" laut Forum |
| `https://github.com/OlliL/Z8000-Disassembler` | Z8000-Disassembler | keine Lizenzdatei |
| `https://github.com/tpaxia/z8000_test` | Z8000-CPU-Tests (MAME-Runner) | MIT |
| `https://github.com/micro3x/WegaPowerAdmin.Emulator` | Treffer der Suche, Bezug zu WEGA fraglich | nicht geprüft |
| Weitere P8000-Emulatoren | **nicht gefunden** (Suche „p8000", „wega", „u8001 emulator" ergibt nur obige) | — |

Hinweis: GitHub-Code-Suche war ohne Anmeldung nicht möglich; eine zusätzliche Tiefensuche (Anmeldung) wäre denkbar.

### 2.3 MAME

* `src/mame/skeleton/p8k.cpp` (575 Zeilen, Lizenz BSD-3-Clause, „Skeleton driver based on Matt Knoth's emulator", © Fabio Priuli 2009); Systeme `p8000` (8-Bit-Board) und `p8000_16` (16-Bit-Board), beide `MACHINE_NOT_WORKING`.
* ROM-Namen/Hashes (CRC32/SHA1) — identisch zu den heruntergeladenen Abzügen:
  `mon8_1_3.1` ad1bb118 / 2332963a…, `mon8_2_3.1` daced7c2 / f1f778e7…,
  `wdc4.2_1-2c43.bin` 2646f1ee / f62574ad…, `wdc4.2_2-5d66.bin` 5d496b65 / 42166d7e…,
  `mon16_{1h,1l,2h,2l}_3.1_udos` 0c3c28da / e8857bdc / cddf58d5 / 395ee7aa,
  Zeichensatz `p8t_zs` f9321251 (≙ `TERMINAL/P8TEZS`, SHA1 a6a796b5…), `p8tdzs.2` 32736503 (≙ `P8TDZS`, SHA1 6a1d7c55…).
* Speicherbild des 8-Bit-Teils (aus dem Kopfkommentar): drei 64-KB-Bänke (A = ROM 0000–1FFF, B = statisches RAM 2000–2FFF, C = DRAM 64 K), Umschalten per `OUT (C),code` (0=nichts, 1=A, 2=B, 4=C; B = Startadresse, C = 0).
  Monitorbefehle B D F G M N O P Q R S T X. Dies ist die einzige maschinenlesbare Modellbeschreibung und ist als **Hypothese** zu behandeln (Skeleton).
* Die U8010-MMU und der WDC sind in MAME **nicht modelliert** (nur Skeleton).

### 2.4 Robotrontechnik.de, ycdt, Wikipedia, weitere

| URL | Befund |
|---|---|
| `https://www.robotrontechnik.de/html/forum/thwb/showtopic.php?threadid=12917` / `…=13629` | Forumsthreads „P8000 WDC Emulator v2/v2.1.1" (OlliL): AVR+IDE/SD; v2.1.1 mit Sammelbestellung 2016; Firmware `.elf` auf pofo.de |
| `https://www.robotrontechnik.de/html/forum/thwb/showtopic.php?threadid=4751` | „SCSI Interface für P8000" (Forum, nicht ausgewertet) |
| `https://www.robotrontechnik.de/html/computer/p8000.html` (lokal gespiegelt) | Gerätebeschreibung, Screenshots (WegaCalc, WegaWord), EPROMmer-Zubehör (`zubehoer/epromer.html`) |
| `http://www.ycdt.net/p8000/` (Betreiber unbekannt; ycdtot.com ist Spiegel) | Seite „Schaltpläne und Datenblätter Programme, Dokumentationen": enthält **EPROM-Binärabzüge** `wega3.1-8_1-3522.bin`, `wega3.1-8_2-cb96.bin` (= MON8 3.1, Prüfsummen 3522/CB96 wie pofo), `wdc4.2_1-2c43.bin`, `wdc4.2_2-5d66.bin`, FSE-/TED-Dokumente (`p8000-fse-dok0…3.pdf`, `p8000-ted-dok.pdf`, `p8000-p8t.pdf`), WEGA-Konfig-Beispiele (`inittab`, `cshrc`, `crontab`), `u880-cross-bsp.pdf`; Seite schlecht erreichbar per TLS (HTTP mit User-Agent funktioniert). Dateien nicht einzeln geladen (Hashes decken sich mit MAME/pofo) |
| `https://en.wikipedia.org/wiki/P8000` | Übersicht (U880 4 MHz 64 KB; 16-Bit U8001 bis 4 MB; 4 serielle Ports 8-Bit; 2 Floppys 5,25″; Winchester-Controller extra Gehäuse; Terminal ADM-31/VT100; Compact 1989 mit 80286-Option) — Sekundärquelle |
| `https://waste.informatik.hu-berlin.de/Diplom/robotron/studienarbeit/files/hardware/p8000/p8000.html` | HU-Berlin-Studienarbeit „P 8000" (nicht ausgewertet) |
| `https://dewiki.de/Lexikon/P8000` | Wikipedia-Spiegel |
| kc85-/Z1013-/tiffe-Foren (`mpm-kc85_de`, `tu-dresden_de_kc-club`, `sax_de`, lokal gespiegelt) | **keine** P8000-Abzüge gefunden |

## 3. Was fehlt — vom Anwender (echtes Gerät) zu beschaffen

Priorität A (Blocker für ein genaues Modell, ohne Gerät nicht herleitbar):

1. **Welche ROM-Stände steckt das Gerät?** Anzeige beim Einschalten: Hardwaretest/Monitor **V3.0** (laut Mitschnitt) — passt zu `MON8_*_3.0`/`MON16_*_3.0`, nicht zu 3.1 in MAME. Bitte **eigene EPROMs auslesen** (MON8 1/2, MON16 1L/1H/2L/2H, WDC 1/2, Terminal) und mit der Sammlung vergleichen — oder zumindest Prüfsummen/Aufkleber melden.
2. **MMU-Registerbelegung und Segmentzuordnung des 16-Bit-Teils** (3× U8010): Hardwarehandbuch/`mch.s`/`mkseg.c` genügen für den Entwurf; **Messwerte** am Gerät: `mmu`-Dump im U8000-Monitor (falls Befehl vorhanden), MAXSEG-Meldung (`MAXSEG=<07>` vs. `<0F>` laut Mitschnitt), Speicherbestückung, gesetzte Jumper „SEGMENTED/NON-SEGMENTED".
3. **Testläufe des Hardwaretests (`sa.diags`, Monitor-T/X-Befehle) mit vollständiger Ausgabe**, insbesondere der Fehlerbilder 52/53 aus dem Mitschnitt: sind das echte Defekte oder erwartetes Verhalten (RAM-Test-Muster)? Entscheidend für das Soll des Emulators.

Priorität B (Emulationsqualität):

4. **WDC-Protokollmitschnitt** (PIO-Handshake) unter WEGA-Boot von der Festplatte, oder Bestätigung, dass die Mitschnitte in `P8000_WDC_Emulator/SignalCaptures/` für die Befehlsfolge ausreichen; ferner **WDC-RAM-Dump** (BTT/PAR-Tabellen, `Read BTT from HD in WDC-RAM`).
5. **Echte Festplattenabbilder** (K5504.50, 1024/5/18/512) mit installiertem WEGA — der Emulator braucht mindestens eine Boot-Platte; aus `WEGA_3.1.20160725.zip` (pofo.de) oder per `sa.install` von den 17 WEGA-3.0-Disketten (lokal vorhanden) erzeugbar. Sicherung der echten Platte per `dd`/WDC-Emulator wäre die beste Referenz.
6. **Terminal-ROMs und Tastaturtyp des echten Geräts** (Typ-1 mit P8T 1.4.x, K801.02/K7673.01): welcher Zeichensatz-EPROM (P8TDZS/P8TEZS) steckt; Foto des Terminalrechners; ob **ADM-31 oder VT100** voreingestellt.
7. **Zeitverhalten**: Takt (4 MHz bestätigt?), WAIT-Zustände des 16-Bit-Teils, RAM-Zugriffsmuster (Testprogramm mit Zykluszählern nach Art von `em256ful`), Refresh.

Priorität C (nachrangig):

8. Seriennummern, Leiterplattenindex 16-Bit (Index 1 vs. 4) und ob GLE-Terminal vorhanden — wirkt auf Schaltplanauswahl.
9. Uhr-/RTC-Karte (`rtc.php`, Timer-Software 1.1), Kassettenlaufwerk (`Tapeinterface`) und Drucker-Schnittstelle — nur falls im Zielumfang.
10. P8000 compact/EMR (80286-Teil) und WDOS — nicht im Zielumfang vermerkt; ggf. verwerfen.
