# WEGA-Datenträger (AP P14)

Stand 2026-10-06. Nur gesicherte Befunde; alles Ungeprüfte ist als solches gekennzeichnet.

## 1. Werkzeug und Ablage

- **`tools/p8000/cqm2img.py`** wandelt CopyQM (`.cqm`) in ein rohes `.img`
  (Zylinder → Kopf → Sektor 1..N) und gibt Geometrie, Bezeichnung, Kommentar, SHA-256 aus.
  `--info`, `--json`, `-d ZIELDIR`. Wächter `tests/python/test_cqm2img.py` (Mini-CQM
  selbst erzeugt; echte Abbilder nur optional, `K1520_WEGA_CQM` = Ordner).
- Die Abbilder liegen **nicht im Repo**: `~/Documents/K1520emu/Disketten/P8000/WEGA3.0/w30*.img`
  (17 Stück, 11,5 MB). Erzeugt mit
  `python3 tools/p8000/cqm2img.py -d ~/Documents/K1520emu/Disketten/P8000/WEGA3.0 ~/projects/robotron/P8000/discs/WEGA3.0/*.cqm`.
- **Keine `.hfe`**: dafür wäre `tools/dev.sh tool k1520disktool` (Bauzustand) nötig; in diesem
  Lauf war Bauen untersagt. Ein `.img` genügt für den Emulator (Formatwahl `k5601_16x256`
  für start/strt2, 80×2×9×512 = „720K" für die Dateisystemdisketten).

## 2. CopyQM-Format (selbst entziffert, an 17 Abbildern gegengeprüft)

Kopf 0x85 Byte (`"CQ"`, Sektorgröße LE16 @3, Sektoren/Spur @0x10, Seiten @0x12,
Bezeichnung @0x1C, Spuren gespeichert/gesamt @0x5A/0x5B, Pruefwert @0x5C, Kommentarlänge
@0x6F, **Kopfprüfbyte @0x84: Summe 0x00..0x84 = 0**), dann Kommentar, dann Datenstrom aus
int16-Blöcken (n<0: ein Byte |n|-mal; n>0: n Rohbytes), über Sektorgrenzen hinweg. Volltext im
Dateikopf des Werkzeugs.

**Vollständigkeit** (alle 17 bestanden): Kopfprüfbyte stimmt; der Strom liefert **exakt**
80·2·Sektoren·Sektorgröße Byte und endet **exakt** am Dateiende. Der 32-Bit-Wert @0x5C ist
keine der gängigen CRC-32-Varianten (CRC-32, -32C, MSB-/LSB-first, Init 0/FFFFFFFF, Xor 0/FFFFFFFF
durchprobiert) und bleibt **ungeprüft**; die Vollständigkeit ist stattdessen über die
Flussabbilder bewiesen (§4).

## 3. Katalog

Alle: 80 Zylinder, 2 Köpfe, MFM, 250 kbit/s (Flussintervalle 4/6/8 µs). Herkunft:
`~/projects/robotron/P8000/discs/WEGA3.0/` (Anwender-Sicherung 2024-11, CopyQM + SCP-Fluss).
Kopfbezeichnung: strt2 `640K CQM floppy image, LibDsk v1.3.5` (von libdsk neu geschrieben,
Kommentar `P8000-SOFTWARE WEGA-Startdisk. 889 347/3.0 PRS.: 5229` — letzte Stelle fehlt;
der Strom beginnt exakt nach 53 Byte, die „0" gehört nicht mehr zum Kommentar), alle anderen
`640K/720K Double-Sided` (CopyQM-nativ), Kommentarfeld leer.

| Datei | Zweck / Inhalt (laut `/CONTENTS` bzw. UDOS-Dateien) | Geometrie | Dateisystem | SHA-256 des `.img` |
|---|---|---|---|---|
| w30start | UDOS-Startdiskette: Urlader-Sektor, `boot`, `wega` (Kernel 3.2 vom 12.02.88), `sa.*`, README/INHALT | 16×256 | UDOS 2.2 (Dateien als zusammenhängende Läufe wiedergefunden) | 3f4d7452db5d0186…3fa7 |
| w30strt2 | dasselbe, zweiter Stand (andere README/INHALT, Firmware-3.2-Hinweis; 16 Läufe, 26 660 B Unterschied zu start) | 16×256 | UDOS | 7fccdaa032824682…90ff |
| w30root1 | `/`: Boot-Block-Abzug `pb.image`, `boot`, `sa.*`, `/dev`-Liste, `/bin` a–c… | 9×512 | UNIX (System III) | 68eb30974d18a8a5…ee90 |
| w30root2 | `/bin` cc…echo… | 9×512 | UNIX | f24a01a4acf35063…9755 |
| w30root3 | `/bin` newgrp…rm… | 9×512 | UNIX | 017cb63a46342ad8…18d7 |
| w30root4 | `/etc` (getty, init, mkfs, mount …) | 9×512 | UNIX | 2b1a29a9c36c652b…c9cc |
| w30root5 | `/lib` (cpp, libc.a, crt0.o, scparse …) | 9×512 | UNIX | 241de4ee5eae5de4…e419 |
| w30usr1 | `/usr`: Gerüst (adm, bin, include, lib, man, src, sys …), `boot`, `/usr/bin` a–d | 9×512 | UNIX | 1ee350a9655c5ec5…caf0 |
| w30usr2 | `/usr/bin` nroff…sdiff (plz, plzsys …) | 9×512 | UNIX | 205f28cb14d28cc7…bcca |
| w30usr3 | `/usr/bin` vls…yacc, lex, lint; `/usr/lib` | 9×512 | UNIX | 93eaf6966221755d…ffd1 |
| w30usr4 | `/usr/lib` (plzcg2/3, slib*.a, text …) | 9×512 | UNIX | 398d04fda2148fa1…8441 |
| w30usr5 | `/usr/lib/tmac`, `/usr/lib/macros`, `/usr/include` | 9×512 | UNIX | df2427a154c61f81…ee79 |
| w30usr6 | `/usr/man/man0`, `man1` (`*.z` = komprimiert) | 9×512 | UNIX | 0863e9876e2649da…94d9 |
| w30usr7 | `/usr/man/man2…` | 9×512 | UNIX | 515b1e57b1f57e1a…c694 |
| w30usr8 | Abrechnung (`acct*`), `spell`, `/usr/pub`, `/usr/dict` | 9×512 | UNIX | 54605261dd286d84…0ae2 |
| w30usr9 | SCCS (`admin`, `get`, `delta` …), `/usr/lib/me`, `termv7`, `mkmenu`/`zmenu` | 9×512 | UNIX | 581526ea146520fb…d059 |
| w30doc1 | Dokumentation: `/wsh/wshbuch.dr.z`, `/wdpband_A…` (`*.z`) | 9×512 | UNIX | a4c0649afe21f3d9…f315 |

(Vollständige SHA-256: `sha256sum ~/Documents/K1520emu/Disketten/P8000/WEGA3.0/*.img`; die
Prüfsummen sind deterministisch, da das Werkzeug nur entpackt.)

### UNIX-Dateisystem (Befund an allen 15 Dateisystemdisketten)

Nicht kompatibel zu `.img`-Formatprofilen des DiskTools (noch kein WEGA-Dateisystem, AP P17).
Gesichert: Byte-Reihenfolge **big-endian**; Block = **512 B**; Block 0 = Boot-Block (auf root1
Z8001-Code; sonst Dienstdaten), **Block 1 = Superblock** im System-III-`filsys`-Aufbau:
`s_isize` (16 Bit) = **59** Inode-Blöcke, `s_fsize` (32 Bit) = **1440** Blöcke (= ganze Diskette),
danach `s_nfree` und `s_free[]` als 32-Bit-Blocknummern (fallend 0x4D8, 0x4D7 …); **kein**
System-V-Magic. **Inodes ab Block 2, 64 B** (Modus 16, nlink 16, uid 16, gid 16, Größe 32,
13×3-Byte-Blockadressen, 3 Zeiten); **Inode 2 = Wurzel** (Modus 040777 auf allen 15), Verzeichniseintrag
16 B (Inodenr. 16 Bit + 14 Zeichen). Mit diesem Aufbau lassen sich Verzeichnisse und Dateien
(inkl. einfach/doppelt indirekt) lesen — `/CONTENTS` jeder Diskette wurde so gelesen.
Jede Diskette ist ein eigenes, vollständiges Mini-Dateisystem; `sa.install` kopiert sie in das
Zielsystem (kein Block-Image-Restore). Die Platten-Dateisysteme sind dagegen 7000 (/) bzw.
13000 (/usr) Blöcke groß (`sa.mkfs`-Log).

## 4. Vergleich mit den Flussabbildern

Eigener Kurz-Dekoder (SCP → MFM-PLL → IDAM/DAM mit CRC-16), nicht im Repo (Scratch):
**alle 17 CQM-Konvertate stimmen mit ihrer `*_cqm.scp` bei jedem Sektor überein** (je 1440 bzw.
2560 CRC-gültige Sektoren, 0 Abweichungen, 0 fehlend). Damit sind Sektorreihenfolge, Geometrie und
Vollständigkeit unabhängig von CopyQM belegt. SCP-Aufbau: v0.9, 3 Umdrehungen, Spur = Zylinder·2+Kopf, Auflösung 25 ns.
`~/projects/prg710/disks/P8000_UDOSwegaBOOT01.scp` (160 Spuren) ist die Anwender-Startdiskette in
einem älteren/benutzten Stand: 2549 von 2560 Sektoren gleich `w30start`, 10 abweichend (Spuren 12,
18, 26 …, vermutlich Verzeichnis/Datum), 1 nicht lesbar; mit `w30strt2` 125 Abweichungen. Nicht gegen
`tests/fixtures/disks/udosP8000_640k_wega.hfe` verglichen (HFE-Leser nur im Kern, nicht gebaut).
Die lokal extrahierten Dateien (`~/Documents/K1520emu/Disketten/P8000/`: `boot`, `wega`, `sa.*`,
`boot0.fd/ud`) stehen als zusammenhängende Läufe in start und strt2 (`wega` ab Byte 2048, `boot` ab
218 624 — gleiche Lage in beiden).
`.fileinfo` = DiskTool-Sidecar (`fs=udos name= typ= eig= start= satz= block= rest= segment= mem= erst= geaend=`;
Kopfsektorangaben der UDOS-Datei, stehen nicht in der Datei selbst; Ladeadresse z. B. `wega`
`segment=0000:30864`, `mem=0000:788F:0000`).

## 5. Installation WEGA → Platte (nach `install_WEGA_3.1.log`)

Gerät: Start-Diskette in Laufwerk 0 (`ud(0,0)`), Quelldisketten nacheinander in Laufwerk 1 (`fd(1,0)`);
Platte K5504.50 (WDC 4.2). Reihenfolge:

1. Hardwaretest, Monitor, `O U` → `boot` von der UDOS-Floppy (start/strt2).
2. `ud(0,0)sa.format` (Format Hard-Disk 4.1), `sa.verify`.
3. `sa.mkfs`: **13000** Blöcke auf `md(0,0)` (/usr), **7000** auf `md(0,16000)` (/).
4. `sa.install`, Datum, Ausgabe `md(0,16000)` (Wurzel): Quelle **root1 → root2 → root3 → root4 → root5**
   (Abfrage „next input disk" je Diskette, nach der letzten `n`; Ausgabe zeigt je Verzeichnis die
   Dateien, `A` = alle).
5. `sa.install` erneut, Ausgabe `md(0,0)` (/usr): **usr1 → … → usr9** (neun Disketten; **doc1 gehört
   nicht dazu**, ist reine Dokumentation, im Log nicht eingelegt).
6. Kernel starten: `md(0,16000)wega` (Kernel 3.2; Aufteilung /usr 0/13000, swap 13000/3000,
   / 16000/7000, /tmp 23000/4000, /z 27000/60732 Blöcke) → Einbenutzerbetrieb `#1 /etc/new.install`:
   `mkfs` für /tmp und /z, Dateisysteme benennen, **Boot-Block schreiben**
   (`dd if=/pb.image of=/dev/rmd0 conv=sync count=1`), Prüfläufe der Dateisysteme (Log §/dev/root, /dev/rusr), Neugenerierung `wega`/`boot`
   (`/usr/sys/conf`), danach `> boot` von Platte, Mehrbenutzer.
Anforderung für den Emulator (P15): zweites Floppy-Laufwerk (Quelle) neben dem UDOS-Start-Laufwerk,
WDC/Platte mit der obigen Aufteilung, Diskettenwechsel im Quelllaufwerk während des Laufs.
WEGA-Systemhandbuch nur für Anmeldung/Passwort ausgewertet; Abweichungen 3.0 ↔ 3.1 s. §5a.

### 5a. Nachtrag P15 (2026-10-07): im Emulator nachvollzogen

Ablauf wie §5, mit `tests/system/test_p8000_wega_install.cpp` (Zwischenstände in
`~/.cache/k1520emu/p8000_wega`).  Abweichungen der Fassung 3.0 vom 3.1-Protokoll: `sa.format`/`sa.verify`
4.1 aus dem 3.1-Abbild (die Startdiskette hat V1.4 für Firmware 3.x); Quelldiskette erst NACH dem
Laden von `sa.install` in Laufwerk 1 legen (UDOS sucht die Datei sonst auch dort: „Error C4"); Format
der Quelldisketten im Emulator `k5601_9x512`; Kern meldet nach `md(0,16000)wega` gleich `#1` (kein
„Single-User Mode"); `/etc/new.install` fragt `/dev/z (Standard 60732)` und listet die Plattentypen;
`/etc/inittab` startet im Zustand 2 getty an console, tty0, tty2, tty4–7 (tty3 aus).  Superuser:
`wega` (uid 0), Passwort bei Auslieferung `root` (Systemhandbuch).  `/etc/passwd` kennt kein `root`.

## 6. Offen

- `.hfe`-Fassungen (DiskTool-Lauf nach Bauzustand: `tools/dev.sh tool k1520disktool …`).
- Pruefwert @0x5C des CopyQM-Kopfes (Variante unbekannt).
- Verzeichnisse von start/strt2 nicht einzeln durchgesehen (UDOS-Erkennung nur über Dateifunde);
  Bedeutung der 10 abweichenden Sektoren der alten Anwender-Startdiskette.
