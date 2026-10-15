# BCU 880 auf dem frühen A5120 — AP A7

Stand 2026-10-15, Zweig `kassette_mc80`.  Grundlage: die Abbilder `doc/a5120_alt/bcu/*/disk.img`
(+ `DT-INFO.TXT`, `.td0` als Beleg), Disassemblat mit `tools/z80_disasm2.py`, Läufe im Emulator
(`boot_trace`/`k1520dbg --laderom v06`).  Wächter: `BootIntegrationBcu.*`
(`tests/integration/test_boot_bcu.cpp`).

## 1. Ergebnis

| Satz | Etikett / DT-INFO | BCU | Lade-ROM | Gerät im Emulator | Stand |
|---|---|---|---|---|---|
| `satz2_SYL_auf_Spur1` | „D15 SIOS 1SS Copy1“, „Bootfähig: Nein“ | **V1.2** FZW/BL, Bild **64×16 ab FC00H** | **V0.6** | Ausführung `Alt` (K7023.01, K7636, 3× K5600.10) | **bootet bis `A>`, `dir b:` listet die Systemdiskette** (`V12BisPrompt`) |
| `satz1_SYL_auf_Spur1` | „BCU Kaltstart“, „Bootfähig: Nein“ | **V1.2** FZW/BL, Bild **80×24 ab F800H** | **V0.6** | `Neu` + `Tastatur::K7636` (K7024, K7636, Laufwerke K5600.10) | **bootet bis `A>`, `dir b:` listet die Systemdiskette** (`V12Bab2BisPrompt`) |
| `satz2_OhneSYL.1` | „SYS1“ | V1.2 | — | — | nicht startbar (§5); **als Datendiskette in B: lesbar** (LSHEXOUT, SYSGEN, MBASIC, STAT, KMBG.REL, INIT, LOAD, DDT, DISASS, FORMAT, SYS80, SYS8, SDT80, EDIT80, EPROM, DDT80T, PIP, SUBMIT, XSUB) |
| `satz2_OhneSYL.2` | „BCK 40 Sp.“, „Bootfähig: Ja“ | **V2.1** (V.24/P) | — | — | **nicht startbar** (§5; `V21OhneSylStartetMitKeinemVorhandenenLaderom`) |
| `satz2_OhneSYL.3/.4` | „SYS80“ (80 Spuren) / „Kaltstart SIOS 250“ | V2.1 (/S bzw. /P) | — | — | nicht startbar (§5) |
| `satz1_OhneSYL` | „Wordstar“, „Bootfähig: Nein“ | V1.2 | — | — | nicht startbar (§5) |

„Bootfähig“ in DT-INFO ist die Angabe des Archivars, nicht nachgeprüft; bei den „SYL“-Sätzen
ist sie falsch (beide starten mit V0.6), bei `OhneSYL.2` ist sie mit keinem vorhandenen ROM
nachvollziehbar.

**BCU 880 V2.1 bootet nicht** — dafür fehlt das Start-EPROM (§4).  Die AP-Karte verlangte
`…V21BisPrompt`; statt eines erfundenen Starts hält ein Test den Befund fest.

Die Disketten sind **MFM, 250 kbit/s, 26×128, Sektoren 1–26 in Folge, 40 Zylinder einseitig**
(Spur- und Sektorköpfe der `.td0`: Kopf-Byte Bit 7 = 0, Datenrate 0, `N` = 0).  Katalogformat
**`k5601_ss40_26x128`** — gab es schon; `data/formats.yaml` bleibt unverändert.

## 2. Aufbau der Disketten „SYL auf Spur 1“

| Zylinder | Inhalt |
|---|---|
| 0 | Satz 1: Sektoren 1–4, 6 = `40H`, Rest `C4H`/Reste; Satz 2: `E5H`.  Wird beim Start nicht gelesen |
| 1, Sektoren 1–6 | **SYL-Lader** (768 B, nach 0400H, §3); Sektoren 7–26 `E5H` |
| 2, 3, 4 (Sektor 1–15) | BCU-System, 67 Sektoren = 2180H Bytes nach **D680H–F7FFH** |
| 4 (ab 16) – 39 | leer (`E5H`) |

**Die SYL-Disketten sind reine Startdisketten, keine Dateidisketten:** der DPB der V1.2
(EC73H/EC82H: 26×128, Blöcke 1 KB, 64 Einträge, **OFF = 2**) legt das Verzeichnis auf
Zylinder 2 — dort liegt hier das System; `dir a:` listet Systemcode als namenlose Einträge
(`A:   :  `).  Gearbeitet wird mit einer Diskette im „ohne SYL“-Aufbau (System Zylinder 0–1,
Verzeichnis ab Zylinder 2) in B:, z. B. `satz2_OhneSYL.1`.

Das System ab Zylinder 2 Sektor 3 (= D780H) ist **byteweise das System der Diskette
`satz2_OhneSYL.1` ab Zylinder 0 Sektor 2** (Unterschiede nur in ungenutzten Puffern 00H↔FFH):
dieselbe V1.2, um zwei Zylinder verschoben, davor zwei Sektoren Kaltstartcode (D680H–D77FH).

## 3. Der SYL-Lader (Zylinder 1, geladen von V0.6 nach 0400H)

Adressen = Ladeadresse.  Disassemblat: `python3 -I tools/z80_disasm2.py <zyl1.bin> --org 0x400
--entry 0x42e --linear`.

| Adresse | Bedeutung |
|---|---|
| 0400–0402 | `SYL` (Prüfung durch das Lade-ROM, laderom.md §3 #11) |
| 0403/0404 | **Ladeadresse D680H** (zugleich Einsprung) |
| 0405/0406 | **Länge 2180H** → 67 Sektoren à 128 B (`(len·2) >> 8`, 0489–049A) |
| 0407–042D | CRC-16 (CCITT, Tabellenlos über Schiebe-/XOR-Folge) über `E` Bytes ab `IY` |
| **042E** | **Einsprung** (= V0.6 `CALL 042EH`): Selbstprüfung CRC 0407–06FD gegen **06FE/06FF** (`B5 83`); Fehler → `JP 0148H` (Neustart im V0.6) |
| 045F ff. | `I` = 07H, ZVE2-ISR-Vektoren 0760/0762, **ZVE2-Einsprung `(0001)` = 0632H** |
| 04AB–04D0 | Schritt nach innen, 2800H-Schleife je Schritt |
| 04E1–0530 | Parametertafel 07E0 (`FE`, Zyl, 0, Sektor, 0), Start ZVE2 (`OUT 04H`) |
| 0532–056C | je Sektor **CRC per Software** über die 128 Datenbytes, Startwert **E295H** (MFM, `[03FD]` Bit 1 = 0) bzw. **BF84H** (FM) = CRC über `A1 A1 A1 FB` bzw. `FB`; Vergleich mit der von ZVE2 mitgelesenen Daten-CRC (Tafel ab 0770H) |
| 056F–058B | fertig: `(004C)` = Endadresse, `I` = 0, K5122 aus (`OUT 11H/13H`=03, `10H`=FFH, `18H`=FFH), **`JP (HL)` = D680H** |
| 05BD–05C4 | endgültiger Fehler: Fehlerlampe + Signal (`OUT 03H,9FH`), `JR $` |
| 0632–06C8 | **ZVE2-Leser**: Markenerkenner der K5122 (`OUT (C),D` mit D = B5H/B1H, `E` = `[03FD]`), `FE`, Zyl/Kopf/Sektor (3 Bytes) vergleichen, Lücke 11 (FM) / 22 (MFM) Bytes, `FB`, `INIR` 128 B, 2× `INI` CRC in die Tafel |

Der Lader benutzt dasselbe ZVE2-Verfahren wie das CP/A-SYL-Stück der V0.9 (Markenerkenner
statt Softwaresuche), braucht aber die **Ladeadresse 042EH** und **Zylinder 1** — also V0.6.
V0.6 wählt MFM über Port 12H Bit 0 (laderom.md §5.1); im Emulator ist es bei laufendem
Laufwerk 0 → MFM, was zu den Disketten passt.

## 4. Kaltstart, BIOS, Geräte

**Kaltstart D680H:** `IM 2`, `I` = F7H, `SP` = 0080H, `OUT (40H),3` [?], ROM-Abschaltung über
Port 0AH, **löscht F450H–FFFFH mit 00H** (auch den Bildspeicher), K5122-Init (Tore 11H/13H/15H/
17H/10H), K8025 (5FH, 58H), Tore **92H/93H/96H/97H** (eine PIO-Karte bei 90H [?]), CTC 0CH/0EH,
dann `JP F112H`.  Die Tore 40H, 9xH und EEH (SYL-Lader 0439: `OUT (EEH),65H`) haben im Emulator
keine Karte — die Schreibzugriffe verpuffen, BCU läuft trotzdem bis zum Prompt.  Welche Karten
dort steckten, ist nicht belegt [?].

**BIOS-Sprungleiste EC00H** (BOOT EC00, WBOOT EC03, CONST EC06 → EF24, CONIN EC09 → EF2E,
CONOUT EC0C → EDF3, …); Kennung „BCU 880 VERS 1.2 / FZW/BL“ bei ECABH.

**Tastatur:** CONST/CONIN lesen **Tor 01H Bit 3** (/UINT, 0 = Code bereit) und **Tor 06H** — die
K7636 am U-Bus der K2526 (doc/a5120_alt/k7636.md).  Die beiden Fassungen unterscheiden sich:

- Satz 1: CONIN übersetzt über die Tafel EF58H→EF65H (13 Codes, u. a. **ET1 FFH → 0DH**,
  1BH→1BH, ET2 FEH als Vorsatz → Code `AND 1FH` = Control).  Passt zur K7636.01.
- Satz 2: CONIN ist `IN A,(06H); RET` — **keine Übersetzung**.  ET1 (FFH) kommt als 7FH (Rubout)
  an; die Zeile beendet die Taste **↵ (A15, Code 0AH)** — die BDOS-Zeileneingabe endet auf CR wie
  auf LF.  Ob das Gerät eine K7636-Variante mit anderer Codetafel hatte, ist offen [?].

**Typzeichen beim Kaltstart:** der Kaltstart schickt der K7636 Kommando 00H (Reset, D69CH:
`XOR A; OUT (08H),A; OUT (03H),A`) und holt das Antwort-Typzeichen 80H nie ab.  Das BDOS liest es
beim ersten Prompt über CONST vorab, es wird das erste Zeichen der ersten Eingabezeile (CONOUT
zeigt es als Zelle 80H hinter `A>`); die erste Zeile läuft deshalb ins Leere.  Gastverhalten bei
Reset-Antwort laut BD 4.5 (k7636.md §5) — am Gerät nicht nachgeprüft [?].  Die Tests schicken
zuerst eine leere Zeile.

**Bild:** CONOUT schreibt direkt in den Bildspeicher, Kursor = Bit 7 der Zelle, Codes ≥ 84H mit
gelöschtem Bit 7, 80H–83H unverändert.

- Satz 1: **80×24 ab F800H** (Zeile 50H, Ende FF7FH, Rollen `LDIR` 0730H) = **K7024** (BAB2).
- Satz 2: **64×16 ab FC00H** (Zeile 40H, Ende FFFFH, Rollen 03C0H) = **K7023** (BAB1).

Im Emulator deshalb: Satz 2 auf `Ausfuehrung::Alt`, Satz 1 auf `Neu` mit
`Config::tastatur = K7636` (K7024 + K7636; neue Konfigurationsgröße dieses AP).  Satz 1 auf
`Alt` läuft auch bis `A>`, schreibt aber in den RAM unter FC00H — das Bild der K7023 bleibt leer.

## 5. Die Disketten „ohne SYL“ — warum keine startet

Zylinder 0 Sektor 1 trägt einen eigenen Kopf: `40 20 'BCU 880 V.24/P'` (V2.1; bei V1.2 nur
`40H`-Füllung), ab Offset 10H Code (`PUSH AF … JP D734H`, ab 34H `LD DE,F100H; LD HL,D717H; LD
BC,001DH; LDIR … JP ECD0H` — also für die Ladeadresse **D700H** gebaut [?]).  Ab Sektor 2 folgt
das System in derselben Lage wie auf den SYL-Disketten ab Zylinder 2 Sektor 3 (D780H).  Den
Kaltstartteil, den die SYL-Disketten in D680H–D77FH mitbringen (IM 2, Löschen, Karten-Init),
hat diese Fassung nicht — **das Start-EPROM muss ihn selbst enthalten**.

Keines der vier vorhandenen Lade-ROMs nimmt die Disketten an (laderom.md §5.2): V0.9_1/_2/a
lesen Zylinder 0 und verlangen `SYL` in 0400H, V0.6 liest Zylinder 1 und verlangt es dort.
Wächter `BootIntegrationBcu.V21OhneSylStartetMitKeinemVorhandenenLaderom` (V0.9_2, V0.9_1,
V0.6: kein Befehl ab 0400H in 12 Mio. Takten, kein „BCU“ auf dem Bild).

**Berichtigung zu Entwurf 30 §3.2 und laderom.md §5.2:** „EPROM BCU 880 VERSION 2.2 CREATED
1983 by FZW/BL“ ist **kein Start-EPROM**, sondern die Kennung des Programms **`EPROM.COM`** —
ein EPROM-Programmierprogramm unter BCU (Verzeichniseintrag `EPROM   COM` auf `OhneSYL.1`
Zyl. 2 Sektor 25 und `OhneSYL.3` Zyl. 2 Sektor 1; Programmkopf `C3 32 01` = `JP 0132H`, Texte
„E P R O M - P R O G R A M M I …“, „QUIT TO BCU 880-SYSTEM“).  Das Start-EPROM für „ohne SYL“
ist damit **unbekannt** — kein Abzug, kein Name.  Ein Nachbau aus Kaltstartcode der SYL-Diskette
+ V2.1-System wäre eine erfundene Diskette und unterbleibt.

## 6. Bedienung

```
tools/dev.sh tool boot_trace --ausfuehrung alt --laderom v06 --format k5601_ss40_26x128 \
    -p 60000000 tests/fixtures/disks/bcu12_ss40-26x128_k5600-10_syl-spur1-satz2.img
tools/dev.sh tool k1520dbg --tastatur k7636 --laufwerke K5600.10,K5600.10,K5600.10,none \
    --laderom v06 --format k5601_ss40_26x128 tests/fixtures/disks/bcu12_ss40-26x128_k5600-10_syl-spur1-satz1.img
```

Start bis `A>` ≈ 9 Mio. Takte (V0.6 läuft zuerst durch seine Spur-0-Suche und fünf Indexpulse).
Datendiskette dazu: `-b doc/a5120_alt/bcu/satz2_OhneSYL.1/disk.img` (der Debugger mountet als
Kopie; `.img` mit `--format`), dann `keys \r` (Typzeichen verbrauchen) und `keys dir b:\r`.

## 7. Offen

1. Satz 2: welche Tastatur(-variante) ET1 als 0DH lieferte [?] (§4); ob die echte K7636 auf
   Reset wirklich 80H nachschiebt, den BCU nie abholt [?] (§4).
2. Karten an 40H, 90H–97H, EEH [?] (§4) — für den Prompt nicht nötig.
3. Start-EPROM für „ohne SYL“ (V1.2/V2.1) fehlt (§5); damit fehlen auch WordStar/Assembler
   der „ohne SYL“-Sätze im Emulator (die Dateien selbst sind per DiskTool lesbar [?, nicht geprüft]).
4. Kassette (KMBG, Laufwerk F:) → AP A8: `KMBG.REL` liegt auf `satz2_OhneSYL.1` (Fixture
   `bcu12_ss40-26x128_k5600-10_ohnesyl-sys1.img`); ob die V1.2 der SYL-Disketten den Treiber schon
   im BIOS hat, ist nicht geprüft (Texte „NOT READY/END OF PAPER/TIME OUT“ gehören zum Drucker).
