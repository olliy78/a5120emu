# PC 1715 / PC 1715W — Diskettenbestand (AP-0c, 2026-10-03)

Gesichtet: alle Abzüge unter `~/projects/robotron/PC1715/` und `PC1715W/` mit
`k1520disktool` (`info`, `ls`, `measure`, `check`, `boot-get`, `save-as` nach
`/tmp/claude-1000/pc1715_disks/`) — **nur lesende Befehle**, kein Original angefasst.
Zusätzlich Rohsicht auf die Bootsektoren (Python über die `.img`-Kopien) und die
Urlader-Prüfung aus `~/projects/robotron/PC1715/EPROM/r1715bt.html` (S502).

## 1. Was „bootfähig" heisst

Der Urlader S502 liest Spur 0 Sektor 1 (`sub_0_83`, `r1715bt.lst` 0083–0100) und akzeptiert
sie nur, wenn das **Wort in Byte 0/1 `F002h` oder `F003h` ist** (`03 F0` auf allen Abzügen);
Bytes 2/3 → BC, 4/5 → DE (hier immer `00 00 / 01 00`), Offset **0x64–0x7F** = drei
Laufwerks-Parametersätze (`50 05 03 1B 03 01 01 00` = 80 Zylinder, 5 Sektoren …, [?] Einzel-
deutung AP-0d), ab 0x0C eine Tabelle, nach der weitere Sektoren geladen werden [?]. Das
ist der Test, den diese Liste „bootfähig (S502)" nennt; **ob dahinter lauffähiger Code steht,
zeigt erst der Emulator** (Etappe 2). Eine Diskette mit „SYL"-Kopf (`53 59 4C`, A5120/SCPX-Stil)
besteht diesen Test **nicht**.

## 2. Bestandstabelle

Spalten: Format · Dateisystem laut DiskTool · Dateien · Betriebssystem laut Banner im
Systembereich · S502-boot. Mehrfachabzüge derselben Diskette (`.scp` ⊃ `.hfe` ⊃ `.img`)
nur einmal; `.scp`, zu denen es keine `.hfe`/`.img` gab, wurden gewandelt (§4, „⇄").

### 2.1 SCP 1715 (SCP V0004–V0007, 5×1024 cpa800 bzw. 16×256 cpa640) — bootfähig

| Abzug | Format / FS | Dateien | OS (Banner) | Bemerkung |
|---|---|---|---|---|
| `SCP6_PC1715.hfe` | cpa800 / cpa_auto (4 Systemspuren) | 9 | SCP **V0006** 03/08/87, 48 KB | Systemdiskette **(Fixture)** |
| `CPA/R1715.hfe`, `1715_floppyImages/{r1715.img, SCP_60.hfe/.img}` | cpa800 | 8 | V0006 | Kopien (8 Dateien: `DISKPAR INIT INSTSCP KEYS SCP-DOS SGEN TLC`) |
| `1715_floppyImages/anasana.img` / `BASIC.img` / `FFF.img` / `G1715.img` | cpa800 | 24 / 20 / 12 / 25 | V0006 | Spiele/BASIC (kyrill. `.ARJ`), `FFF` = Systemprogramme |
| `1715_floppyImages/SOFT1715.img`, `SUPPLIED.img`, `scp_spiele.hfe/.img`, `ADDITION.img` | cpa800 | 55 / 68 / 66 / 67 | V0006 | `scp_spiele` trägt zusätzlich `@OS.COM` (CP/A-Datei). **`ADDITION.img`: Erkennung scheitert** („Verzeichnisplatz 81 ungültig"); mit `--fs cpa_auto` 67 Dateien |
| `scp60_new.img` (+ `⇄ scp60_new_img.scp`) | cpa800 | 44 | V0006 (Dateien nennen 0004) | `.scp` ist **10×512**-Rückweg (Fehlbeschreibung, §5) |
| `1715_floppyImages/DS.img` (666 624 B), `scp6_pascal.img` (754 688 B) | **kein Katalogformat** (Grösse) | – | V0006 | Boot-Kopf da; nur über `.hfe`/Vermessung mountbar |
| `real_discs/SCP780_BASIC.img`, `SCP780_Angebote_Programme.img`, `SCP780_SCP05_1.img`, `SCP_Mustertexte.img` | cpa800 / cpa_auto | 42 / 110 / 91 / 42 | **V0005/1** 20/09/86 (BASIC: auch 0004) | Anwendungsdisketten mit Systemspuren |
| `real_discs_2/disk_II, IV, IX, XV.hfe`, `⇄ scp5_TR_REDABAS`, `⇄ redabas_TPinst_jnit_sgen_lx86` | cpa800 | 31 / 36 / 54 / 23 / 54 / 31 | SCP 5 („SCPX V0/3") | REDABAS-/Turbo-Pascal-Arbeitsdisketten |
| `scp07_td0_scp.img` = `⇄ scp07_td0.hfe` | **cpa640** 16×256 / scpx640 | 4 (`CCP.SPR INIT INSTSCP SGEN`) | SCP **V0007** 01/11/88, 50 KB, nachladbarer CCP | **(Fixture)** — zweites Format |

### 2.2 SCP 3.0 (Lader „PC 1715W", `SCP3.SYS`, `SC6xx.ZGF`) — bootfähig, **gehört zum 1715W**

Liegt zum grossen Teil im **PC1715**-Baum: derselbe Lader, 5×1024 cpa800, 4 Systemspuren.

| Abzug | Dateien | Bemerkung |
|---|---|---|
| `PC1715W/PC1715W_SCP30.scp` ⇄ | 42 | **Fixture**, „SCP 3.0 – LOADER PC 1715W V0001 25/05/87 (C) R-BWS", `SCP3.SYS`, 14 × `SC6xx.ZGF` |
| `PC1715W/PC1715W_Programme.scp` ⇄ | 58 | gleiche Belegung wie `real_discs_2/disk_V/XI/XIV/XXII.hfe` (je 58 Dateien, 724 KB) |
| `1715_floppyImages/scp3.img`, `scp30.img`, `r1715ms.img`, `r1715mt.img` | 48 / 43 / 42 / 23 | `scp3` mit Anwenderdateien, `r1715mt` = WordStar/Turbo |
| `real_discs_2/disk_I, III, VI, VII, VIII, X, XIII, XII` | 45 / 42 / 41 / 70 / 53 / 47 / 11 / 78\* | Anwenderdisketten (Texte, BASIC); \*XII nur mit `--fs cpa_auto` erkannt |

### 2.3 CP/A 1715 — Quelltext-Bauwerk, drei Bootdisketten

| Abzug | Format / FS | Dateien | S502-boot | Befund |
|---|---|---|---|---|
| `CPA/BOOT_CPA_4LW.hfe` (+`.img`, `.SCP`, Kopie in `discs/`) | cpa800 / **nicht erkannt** | 57 Einträge (`@OS.COM`, `PIP`, `FORMATP`, `M80`, `BIOP*.MAC` …) | **ja** (`03F0`) | **Fixture**; Diagnose §3 |
| `CPA/CPA_MAXI.hfe` = `CPA_MAXI_TD0.img` (bytegleich) | cpa800 / **nicht erkannt** | 2 (`@OS.COM`, `CPA1715G.COM`) | **ja** | minimale Bootdiskette |
| `CPA/robotron_cpa.hfe/.img`, `1715_floppyImages/robotron_cpa*.img/.hfe`, `⇄ robotron_cpa_scp` | cpa800 / nicht erkannt | wie BOOT_CPA_4LW | ja | Mehrfachkopien; `robotron_cpa_hfe2/hfe3/hfes.hfe` **nicht ladbar**, `…_hfea2.hfe` (4 MB) vollständig unformatiert |
| `CPA/cpa1715.hfe`, `discs/cpa1715.img` | cpa800 / cpa800 (**keine Systemspuren**) | 57 (CP/A-1715-Quellen: `BIOS*.MAC`, `BDOS.ERL`, `-CPA.022`) | nein (kein `03F0`) | Quelldiskette — `cpa1715.img` meldet zusätzlich „UDOS1715"-Texte |
| `CPA/CPA1715_2.hfe`, `CPA1715_CQM.img` | cpa800 / cpa800 | 44 (`@OS.COM`, `BIOP*.MAC`), 794 KB belegt | nein | Quell-/Arbeitsdiskette |
| `⇄ CPA_Spiele2`, `1715_Spiele.hfe` | cpa800 (CPA_Spiele2: 2 Zylinder unformatiert) | 59 / 14 | nein | Spiele; `@OS.COM` ohne Boot-Kopf |

### 2.4 CP/Z 2.2 und UDOS 1715

| Abzug | Format / FS | Dateien | OS | S502-boot |
|---|---|---|---|---|
| `CPZ_PC1715.hfe` | cpa640 / scpx640, 4 Systemspuren | 35 | **CP/Z 2.2** („BDOS PC/BC 3/88", „Variante vom 03.01.88") | ja — **Fixture** |
| `UDOS1715.hfe` | k5601_16x256 / udos1715 „SYSTEM" | 67 | NDOS / UDOS 1715 | ja (`03F0`, 4 KB Systemspuren) — **Fixture** |
| `1715_floppyImages/UDOS1715_patched.img` (+`.scp`) | wie oben | 67 | UDOS 1715, „patched" | ja (Bytevergleich mit der vorhandenen Fixture `udos1715_640k_pc1715_system.img`: **nicht** gleich; `UDOS1715.hfe` ebenfalls nicht — drei Stände) |
| `real_discs/UDOS_Arbeitsdiskette_BC1_6.img` / `⇄ …_.scp` | `.img`: 543 232 B, **kein Format**; `.scp`: udos_ds77 / udos_ds77, 2 Seiten (`BM01.S`) | – | UDOS 4.3 (A5120-Sitte, K5601) | nein |

### 2.5 SYL-Disketten (cpa640 16×256, „ROBOTRON LOADER") — **nicht** S502-bootfähig

`real_discs_2/disk_XVI, XVIII, XX, XXIV.hfe` (52 / 25 / 12 / 8 Dateien, SCPX-Dateisystem),
`⇄ spiele_TP_power` (= XVIII), `PC1715W_CPMsourcen_buildtools` ⇄ (liefert dieselbe Liste wie XXIV:
`BASIC BRUN INIT POWER SYSG TEST.BAS` — Name und Inhalt passen nicht zusammen), `disk_XIX`
(leer, 14 CRC-Fehler-Sektoren). Kopf `53 59 4C 95 …` ist der des A5120-SCPX-Systemladers; am
1715 ohne anderes Startmedium nicht startbar [?], als Datendisketten lesbar. `disk_XVII`:
dBase/WordStar-Diskette (19 Dateien, kein Boot-Kopf).

### 2.6 Leer, Müll, fremd

| Abzug | Befund |
|---|---|
| `1715_floppyImages/test01/02.hfe`, `780K_leer_5x1024_4S.hfe`, `780k_leer_1024_128.hfe`, `800k_leer.hfe` | leer (Füllbyte `F6`/`E5`), Verzeichnis ohne Dateien, **kein Boot-Kopf** (`53 53 …` = Füllmuster) |
| `real_discs/SCP_MV621322_txt.img`, `⇄ SCP_3`, `⇄ SCP_2`, `real_discs_2/disk_XXV, XXVI` | leer bzw. Textreste hinter gelöschtem Verzeichnis (`SCP_2`: 1 Datei `MVG2131S.TXT`) |
| `real_discs_2/disk_XXI, XXIII` | Sektor 1 beginnt `53 59 4C 44 41 43` („SYLDAC"), Verzeichnis Müll, 107/118 Blockzeiger ungültig — andere Diskettensitte, nicht entschlüsselt |
| `real_discs/SCP.img` | **MS-DOS FAT** 720 KB (9×512) |
| `real_discs/Textdiskette_DCP.img` | scp1700 (CP/M-86), 5 Dateien (`ANASS1x.TXT`), 640 KB |
| `real_discs/SCP_36_Spiele.img`, `⇄ robotron_cpa_img`, `⇄ scp60_new_img` | 10×512-Format — siehe §5 |
| `real_discs/SL1_1_1x80X26.img` (`⇄ SL1_1`), `BS600_40/41Tracks_SS0.img`, `⇄ BS600_40/80Tracks`, `⇄ scp6_stuff`, `Henningsdorf.img` | fremde Formate (SYL-Kopf, einseitig 26×128, Terminal BS600), uneinheitliche Sektorgrössen — nicht katalogisiert |
| `Specialist/SD.IMG` (emulator) | fremder Rechner, nicht gesichtet |

## 3. Kurzdiagnose `CPA_MAXI.hfe` / `BOOT_CPA_4LW.hfe` („Verzeichnisplatz 0 ungültig")

Die Roh-Sicht auf Zylinder 0/Kopf 0 (`save-as --fs cpa800` → `.img`, bytegleich zu
`CPA_MAXI_TD0.img`) zeigt: **die Diskette hat keine Systemspuren, das Verzeichnis beginnt auf
Zylinder 0 — und der Boot-Kopf des Urladers steht IM Verzeichnis.**

```
0000  03 F0 00 00 01 00 00 00 00 A0 37 00 80 37 2F 00 …   Verzeichnisplatz 0  = Boot-Kopf (Benutzerbyte 03h)
0020  00 '@OS     COM' …                                     Platz 1 = @OS.COM
0040  00 'CPA1715GCOM' …                                     Platz 2
0060  F0 F0 00 00 50 05 03 1B 03 01 01 00 50 05 03 1B …     Platz 3 = Parametersätze (Benutzerbyte F0h)
0080  E5 …                                                  Platz 4.. = Dateien / frei
```

Der Urlader prüft nur Bytes 0/1 (`F003h`) und kopiert ab 0x64 die Parametersätze; CP/A (ohne
Systemspuren, Lader liest `@OS.COM` als Datei) benutzt dieselben 128 Byte als Verzeichnisplätze
mit Benutzerbyte `03h` bzw. `F0h`, die für CP/M „nicht existent" sind. Das DiskTool
(`cpa800`, „keine Systemspuren") prüft jeden Platz 0 auf einen **gültigen Namen**; `03 F0 00 00 …`
fällt durch (Name enthält `00`/`F0`) → „Verzeichnisplatz 0 ist kein gültiger Eintrag", obwohl
alle weiteren Plätze gesund sind (`--fs cpa800` erzwingt es und listet 3 bzw. alle Dateien, nur
mit Warnung „3 Blockzeiger hinter dem Datenbereich" bei CPA_MAXI wegen Platz 3).
Auf SCP-Disketten tritt es nicht auf, weil dort Kopf+Parameter in den Systemspuren liegen.
**Vorschlag AP-D**, keine Codeänderung jetzt: im Profil `cpa800`/ohne Systemspuren Platz 0 mit
`03 F0`-Kopf und Platz 3 mit Benutzerbyte `F0` als „Bootbereich" vom Namenstest ausnehmen (und
beim Schreiben unangetastet lassen); die erzeugte Diskette bleibt dann bootfähig. Gleichzeitig
ist es die einzige **bootfähige 1715-Diskette ohne Systemspuren** — wichtig für `create --boot`.

**Umgesetzt in AP-D (2026-10-04):** Profil `cpa1715` (`dir_boot`), `ls`/`get`/`check` gehen,
`put`/`rm` lassen Platz 0/3 unangetastet, `create --fs cpa1715 --boot` — siehe
`doc/merkposten/disktool.md`.

## 4. Wandlung `.scp` → `.hfe`

`gw` ist installiert (Host Tools 1.23, kein Gerät nötig). **`gw convert in.scp out.hfe` allein
scheitert** („HFE: Requires bitrate to be specified"); es geht mit
`gw convert in.scp out.hfe::bitrate=250` (5¼″ DD, 300 U/min; 0,2 s je Abzug). Ergebnisse in
`/tmp/claude-1000/pc1715_disks/` (`PC1715W_*.hfe`, Unterordner `scp1715/` = 15 `.scp`
des PC1715-Baums, zu denen es keine `.hfe`/`.img` gab). Alle drei 1715W-Abzüge sind
5×1024/80 Zylinder/2 Köpfe, Zylinder 80/81 unformatiert (nur gelesen, nicht belegt). Die
Wandlung nimmt die erste Umdrehung — die Sektoren dekodieren fehlerfrei
(`check` ohne Befund bei SCP 3.0).

## 5. Auffälligkeiten

- **Der PC1715-Baum enthält ~20 Disketten des 1715W** (SCP 3.0-Lader, `SC6xx.ZGF`), mehr, als
  die Ordnernamen vermuten lassen; die „SCP 6.0"-Disketten dagegen sind die des PC 1715.
- **Zwei Formate je Gerät**: SCP 1715 läuft auf 5×1024 (cpa800) **und** 16×256 (cpa640 —
  `SCP V0007`, CP/Z); der Urlader kennt beide über die Parametersätze ab 0x64.
- **10×512-Abzüge** (`robotron_cpa_img`, `scp60_new_img`, `SCP_36_Spiele`): am Greaseweazle
  aus einem `.img` auf ein 512-B-Format zurückgeschrieben — am 1715 nicht benutzbar, nicht als
  Fixture geeignet.
- **Mehrere Abzüge desselben Inhalts** (`robotron_cpa`×6, `BOOT_CPA_4LW`×4, 58-Dateien-Satz ×5):
  nicht ausgewertete Unterschiede; als Fixture nur je einer.
- `ADDITION.img` und `real_discs_2/disk_XII` werden nur mit `--fs cpa_auto` erkannt
  (Verzeichnisplatz 81 bzw. 0 mit beschädigter Zeile) — Erkennung des DiskTools ist hier zu
  streng, keine Beschädigung der Diskette gefunden.
- Die Fixture `pc1715_udos1715_system.hfe` ist **nicht** bytegleich mit `udos1715_640k_pc1715_system.img`
  (Abzug nach 188 801 B abweichend, vermutlich Zeitstempel/Belegung) — bewusst beide.

## 6. Fixture-Auswahl (nach `tests/fixtures/disks/`, je ~2 MB, `.hfe`)

| Fixture | Begründung |
|---|---|
| `pc1715_scp1715_v0006_boot.hfe` | kleinste bootfähige SCP-1715-Systemdiskette (9 Dateien), 5×1024 — Etappe 2 bis `A>` |
| `pc1715_scp1715_v0006_pctest.hfe` | dieselbe SCP-V0006-Systemdiskette + `PCTEST.COM` (heile Fassung aus `1715_floppyImages/SOFT1715.img`, 8 960 B), 10 Dateien — Werkstest unter SCP (AP-4f, `Pc1715Pctest.Scp*`) |
| `pc1715_scp1715_v0007_cpa640_boot.hfe` | zweites Format (16×256, SCP V0007, nachladbarer CCP) — deckt die Parametersätze für die zweite Geometrie |
| `pc1715_cpa1715_boot_4lw.hfe` | **die** CP/A-1715-Bootdiskette (ohne Systemspuren, Boot-Kopf im Verzeichnis); Prüfstein für AP-D und Etappe 3 (Quelltext des BIOS auf der Diskette) |
| `pc1715_cpa1715_workbench.hfe` | CP/A 1715 aus der CPA-Workbench gebaut (AP-3b, nach Behebung des Bootkopf-Fehlers dort), `cpa1715`, 22 Dateien, trägt `PCTEST.COM` (AP-4d) — **V 0.1, beschädigt** (Satz 56 = 1D00–1D7FH ganz E5H, Kern des Speichertests fehlt; gleich in `scp60_new.img` und `CPA_Workbench/additions/pc_1715/pctest.com`, AP-4f) |
| `pc1715_udos1715_system.hfe` | UDOS 1715 spurgenau (als `.img` existiert sie schon); Boot bis Prompt (AP-5b) |
| `pc1715_cpz22_boot.hfe` | drittes 1715-Betriebssystem CP/Z 2.2 (cpa640), billig und bootfähig |
| `pc1715w_scp30_system.hfe` | SCP 3.0 des 1715W (aus `.scp` gewandelt) — AP-W3 |

Nicht übernommen: `CPA_MAXI` (Duplikat der CPA-Bootdiskette in klein; bei Bedarf nachholen),
die 20 Anwenderdisketten, die SYL-Disketten (nicht S502-bootfähig), alle `.scp` (35–40 MB).
`tests/fixtures/README.md` trägt die sechs Dateien ein.
