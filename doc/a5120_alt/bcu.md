# BCU 880 auf dem frühen A5120 — AP A7

Stand 2026-10-16 (§9: Experiment A7b, §10: Modell und Disketten in der Oberfläche), Zweig `kassette_mc80`.  Grundlage: die Abbilder `doc/a5120_alt/bcu/*/disk.img`
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
| *Eigenbau* (kein Original) | V2.1-System + SYL-Aufbau der V1.2 | — | V0.6 | `Neu` + K7636 | **bootet bis `A>`**, `dir b:` listet — nur mit V1.2-Plattentreiber, §9 (`V21EigenbauBootet`) |
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
ist damit **unbekannt** — kein Abzug, kein Name.  Ein Nachbau aus Kaltstartcode der SYL-Diskette + V2.1-System ist eine erfundene Diskette; als gekennzeichneter Versuch steht er in §9 (Eigenbau, ohne Beleg).

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
4. ~~Kassette (KMBG, Laufwerk F:) → AP A8~~ — erledigt, §8.  Die V1.2 hat den Kassettentreiber
   **nicht** im BIOS; er kommt als `KMBG.REL` per `OPEN` dazu.
5. 17-Byte-Abschlussblock: READ SEQ liefert ihn als zusätzlichen Satz (§8.3); ob das am Gerät
   genauso war oder ein späteres BCU es ausblendet, ist offen [?].

## 8. Kassette als Laufwerk F: (AP A8)

Stand 2026-10-15.  Wächter `BcuKassette.FormatiertSchreibtLiest` (`tests/system/test_bcu_kassette.cpp`,
`format_integration`).  Gerät: Ausführung `Kmbg` (= `Alt` + K5020 fest), Lade-ROM V0.6, Satz 2.
Disassemblat des Treibers: `KMBG.REL` von `satz2_OhneSYL.1`, Code ab Dateiversatz 100H, Adressen
relativ (wie `doc/kassette/treiber.md` „BCU“); im Lauf liegt er ab C900H.

### 8.1 Bedienung

```
A>                      (Typzeichen 80H: erst eine leere Zeile, §4)
   — Systemdiskette „ohne SYL“ nach A: legen (s. u.) —
A>OPEN B:KMBG           kmbg opened
A>B:INIT                **FORMATIERUNG KASSETTE IN DRIVE F**  /  KASSETTE IN DRIVE F! (Y)  → Y
A>PIP F:=B:STAT.COM
A>PIP B:STAT2.COM=F:STAT.COM
```

- **`OPEN`** ist ein eingebautes CCP-Kommando (Befehlstafel D958H: DIR ERA TYPE SAVE REN USER
  **OPEN** TOD RES).  Es lädt eine `.REL` nach 0100H, ruft deren Kopfprogramm (Seitenrelokation
  unter `(0029H)`, Meldung „kmbg opened“), trägt das Modul ein und senkt `(0029H)` (CCP DDCA–DDFD).
  Erst damit hängt sich der Treiber in den BDOS-Einsprung (`(0006H)` → Modul+14H).  `B:KMBG`
  sucht `KMBG.COM` („B:KMBG?“); `B:KMBG.REL` lädt zwar (beliebiger Typ → `.REL`, CCP DE19H), ruft
  aber nur den Lader — der Treiber bleibt ohne Einsprung.
- **Warmstart braucht die Diskette „ohne SYL“ in A:.**  WBOOT (ECC7H) lädt das System von A:
  Zylinder 0–1; auf der SYL-Diskette steht dort der Lader → Absturz (Bild voller Müll).  Der
  Kaltstart von der SYL-Diskette selbst braucht keinen Warmstart; jedes `^C` und jedes Programm,
  das mit `JP 0` endet (PIP, INIT-Abbruch), schon.  Am Gerät also: von der SYL-Diskette starten,
  dann die Arbeitsdiskette einlegen [abg].
- **F: ist Gerät 2** (K5200 an X4).  Der Treiber kennt E: und F: (BDOS 0EH: E = 4/5; FCB-Laufwerk
  5/6), Merker 0018H (E:) / 0019H (F:); `sub_0046` setzt Bit 0 für E: → Steuer-PIO-Bits `/RES1`,
  `/AWA1`, `/NIB1`; für F: Bit 0 = 0 → `/RES2`, `/AWA2`, `/NIB2` (KMBG 0357–037C).  Fehler bei
  leerem Gerät meldet INIT nicht (Befehle ohne Bit 7).
- **INIT verlassen:** INIT fragt nach dem Formatieren „AGAIN?“ und endet nur auf 0DH (`INIT.COM`
  011A).  Die K7636.01 hat keinen Code 0DH, und der CONIN von Satz 2 übersetzt nicht (§4) — jede
  andere Taste formatiert erneut bzw. fragt wieder.  Ausweg über die BDOS-Ausgabebremse (E020H:
  `^S` wartet, folgt `^C` → `JP 0`): **DEL LINE (13H) während des Formatierens drücken**, nach dem
  Formatieren **INS LINE (03H)** → Warmstart.  Ob der Bediener eine Tastatur mit 0DH hatte, ist
  offen (§7.1).

### 8.2 Formatieren (BDOS 50H, KMBG 0263–027D)

`Y` → Befehlsfolge D1H (Bandmarke schreiben), E1H (1,27 s löschen), 21H (zurückspulen), 41H
(freigeben), jede mit Reservieren (B1H) davor.  Von Position 0 (Kassette frisch eingelegt, Kopf am
Vorspannanfang) **geht es ohne Rückspulen**: der Treiber fährt mit `TRV` und wartet **ohne Frist**
auf das Magnetband (0B04–0B08), dann auf die BOT-Marke (0B17) — anders als die SCPX-Bibliothek mit
ihrer 2,45-s-Frist (Merkposten Kassette 9).  Rückspulen (05A6): steht das Band im Vorspann, `TRR`
bis zum Magnetband (Frist 65 536 Schleifen), dann `UMS`; gewartet wird nur, bis `/NIB` fällt —
das Ende des Umspulens nicht.  Ergebnis auf dem Band: ein Block, die Bandmarke `AA 00 00 AA`.

### 8.3 Dateiablage (bestätigt treiber.md §6.3)

`PIP F:=B:STAT.COM` (STAT.COM = 23 Sätze) hinterlässt 27 Blöcke:

| Block | Inhalt |
|---|---|
| 0 | Bandmarke von INIT |
| 1 | Kopf, 16 B = FCB 0–15: `00 'STAT    COM' 00 00 00 17` — PIP schreibt `STAT.$$$`, das Umbenennen (BDOS 17H) schreibt den Kopf mit dem neuen Namen neu; Byte 15 = Satzzahl |
| 2–24 | 23 × 128 B Daten |
| 25 | Abschluss, 17 B = FCB 0–16 **mit dem Arbeitsnamen**: `00 'STAT    $$$' 00 40 17 00` |
| 26 | Bandmarke |

**READ SEQ (01D2–01F0) liest bis zur Bandmarke** (Länge 0 → Dateiende) und kopiert jeden anderen
Block in den DMA-Puffer — auch den 17-B-Abschluss.  `PIP B:STAT2.COM=F:STAT.COM` liefert daher
24 Sätze: die 23 richtigen und einen 24., dessen erste 17 Byte der Abschlussblock sind (Rest =
alter Pufferinhalt).  Gastverhalten laut Code [abg]; am Gerät nicht nachgeprüft [?].

### 8.4 Kernfehler, die BCU aufdeckte

Beim ersten Schreiben meldete BCU `7xWRITE`: das Echo (Lese-nach-Schreiben, je Byte verglichen in
der Daten-PIO-B-ISR 0915) blieb nach wenigen Bytes aus.  Ursache waren zwei Abweichungen vom Z80/
U880 im Kern, beide jetzt behoben und einzeln abgesichert:

1. **Interrupt direkt nach `EI`** (`a5120.cpp`): die Maschine nahm einen anstehenden Interrupt
   schon vor dem auf `EI` folgenden Befehl an — zwischen `EI` und `RETI` der ISR (0945/0946); das
   RETI lief nie.  Jetzt `Z80::ei_schatten`/`interruptAnnehmbar()` (Wächter
   `Z80Test.EiSchatten_InterruptErstNachDemFolgendenBefehl`).  Dieselbe Festlegung hatte die
   P8000-8-Bit-Karte schon für sich (`karte8.cpp`, Befund P22); die übrigen Maschinen (K8915, PRG,
   PC 1715) prüfen weiter nur IFF1 [?].
2. **RETI von Port B, während Port A anfordert** (`Z80PIO::onRETI`): B galt als gesperrt, weil A
   eine *anstehende* Anforderung hatte; B blieb in Bedienung (IUS) und sperrte sich bis zum
   nächsten fremden RETI.  Bei der RETI-Dekodierung gibt ein nur anstehender Kanal IEO frei; jetzt
   sperrt für B nur noch A in Bedienung (Wächter `RetiChain.PioRetiVonBLoeschtIusAuchWennAAnfordert`).
   Die gleiche Lesart steckt noch in `Z80CTC`/`Z80SIO::onRETI` (Kanal-IEI mit „anstehend“) und
   über Bausteingrenzen in `K1520Bus::updateInterruptChain` [?, nicht angefasst].

`K5020::verloreneBytes()` steigt im Lauf (≈ 34) — Bytes, die beim Suchen/Überfahren an der
Daten-PIO B ankommen, während der Treiber sie nicht liest; die Datei selbst kommt vollständig zurück.

## 9. Experiment A7b — V2.1 im SYL-Bootaufbau (EIGENBAU, kein Beleg)

Stand 2026-10-15.  **Diese Diskette hat es nie gegeben.**  Sie ist ein Versuch, das V2.1-System
der „ohne SYL“-Diskette `satz2_OhneSYL.2` ohne das fehlende Start-EPROM (§5) im Emulator zum Laufen
zu bringen, indem der Bootaufbau der V1.2-SYL-Diskette `satz2_SYL_auf_Spur1` nachgeahmt wird.
Bau: `tools/bcu_v21_eigenbau.py [--treiber] <SYL.img> <OhneSYL2.img> <aus.img>` (liest nur; die
Quellabbilder bleiben unverändert).  Ergebnis `tests/fixtures/disks/bcu21_ss40-26x128_k5600-10_eigenbau-syl-v21.img`
(= `--treiber`).  Wächter `BootIntegrationBcu.V21EigenbauBootet` (Ausführung `Neu` + K7636, Lade-ROM V0.6).

### 9.1 Was kopiert wurde

| Zielort auf der Diskette | Herkunft | Änderung |
|---|---|---|
| Zyl. 0 und Zyl. 1 Sektoren 7–26, Rest von Zyl. 1 | `satz2_SYL_auf_Spur1` | keine |
| Zyl. 1 Sektoren 1–6 (SYL-Lader, 0400H) | `satz2_SYL_auf_Spur1` | **nur** Lader-Kopf: Ladeadresse (0403/0404) D680H → **D600H**, Länge (0405/0406) 2180H → **1B00H** (A, 54 Sektoren) bzw. **1E80H** (B, 61 Sektoren). Beides liegt außerhalb der Lader-CRC 0407–06FD (§3), die Selbstprüfung bleibt gültig |
| Zyl. 2 Sektoren 1–2 → D600–D6FF | Kaltstartteil D680–D77F der V1.2-Diskette (§4) | `LD HL,D681H`→D601H (Offset 20H), `JP PE,D6A8H`→D628H (2CH), Tafelzeiger D707H→**D687H** (35H), Ausgang `JP F112H`→**`JP D759H`** (85H).  Die Tafeln (D707–D77F → D687–D6FF) und das Füllen F450–FFFF mit 00H bleiben |
| ab D700H, 52 Sektoren (ab Zyl. 2 Sektor 3) | `satz2_OhneSYL.2`, Zyl. 0–1 (D700–F0FF; D700H = Kopfsektor `40 20 'BCU 880 V.24/P'`) | Variante A: keine; Variante B: 11 Absolutadressen (Tabelle `TREIBER_REFS` im Skript) |
| ab F100H, 7 Sektoren (nur B) | V1.2-SYL-Diskette, System ab D780H, Bytes F100–F47F | keine |

Das System der V2.1 endet bei F0FFH; das Verzeichnis der „ohne SYL“-Diskette beginnt auf Zyl. 2.
Weder V2.1 noch V1.2 („ohne SYL“, `OhneSYL.1`) tragen den **residenten Plattentreiber ab F100H**
(HOME/SELDSK/SETTRK/READ/WRITE-Rümpfe, Neuladen für WBOOT, Tafeln; die BIOS-Sprungleiste EC00H
zeigt dorthin: V2.1 EC18→F18B, EC1B→F156, EC1E→F18D, EC27→F1DA, WBOOT `CALL F11DH`).  Die
SYL-Diskette der V1.2 bringt ihn mit (F100–F7FF im 67-Sektor-System); bei „ohne SYL“ muss er aus
dem Start-EPROM kommen [?, Schluss aus dem Fehlen und aus dem Kopf, s. u.].

### 9.2 Ergebnis

- **Variante A** (Kaltstart + V2.1-System, kein Treiber): Lader, Kaltstart und BIOS-Einsprung
  ECD0H laufen, die V2.1 meldet **`BCU 880 V.24-2.1` / `HFO/S`** auf dem Bild (80×24 ab F800H, K7024:
  `Neu` + K7636; die 80 Spalten ab F800H folgen aus dem CONOUT des V2.1-BIOS, EF21H/EEDDH: Zeile 50H), bleibt
  dann aber nach dem ersten Plattenzugriff stehen: die Sprungleiste springt nach F156H usw.
  in leeren RAM (`PUSH HL`-Folge im Trace).  Ohne Test.
- **Variante B** (+ V1.2-Treiber F100–F47F, 11 umgebogene Adressen): **bootet bis `A>`**, `dir b:` mit
  der V2.1-Diskette in B: listet `ANMELD ASM` usw.  Das ist der Test.  Die V2.1-Banner-Zeile
  (`HFO/SP`: das `P` ist das Byte ECC2H, das der V2.1-Kopf von 20H auf 50H setzt; der Kopf selbst läuft nur ab D759H) erscheint.
- **Der V1.2-Treiber taugt als Ersatz**, weil V1.2 und V2.1 dieselbe BIOS-/BDOS-Aufteilung haben:
  die Sprungleiste EC00–EC3F ist bis auf vier Zeiger gleich, die Absprünge in F1xx–F3xx und alle
  Daten ab F450H (Variablen F450–F46E) haben gleiche Adressen oder wenige Bytes Versatz (Tabelle im Skript).
  Dass die 11 Umbiegungen **richtig** sind, ist nur durch den Lauf belegt (HOME/SELDSK/SETTRK/READ
  und ein `dir`); WRITE und Neuladen (WBOOT) sind **nicht** erprobt.

### 9.3 Grenzen und offene Punkte

1. **Eigenbau.**  Kopf (D700H, D710H–D758H: Stub nach F100H, Fenster F182–F184/F126/F12C/F239)
   wird **übersprungen** (`JP D759H`): dieser Teil setzt ein Start-EPROM-Gerüst bei F100H
   voraus, das es nicht gibt, und würde den V1.2-Treiber zerstören.  Wie das echte Gerät diesen
   Kopf benutzte, bleibt offen [?].
2. Der V1.2-Treiber ist eine **andere Fassung** als der Treiber des V2.1-Geräts (Einsprünge um bis
   0x13 versetzt).  Unterschiede im Verhalten (Wartezeiten, Fehlerbehandlung, DPB-Tafeln F2E1/F2FB
   ↔ F2E7/F301) sind nicht untersucht [?].
3. WBOOT (`^C`, `JP 0`) ist nicht erprobt: es lädt D700H–F0FF neu von Zyl. 0–1 der Diskette in A:, die
   hier das System auf Zyl. 2 trägt (§8.1: das gilt für jede SYL-Diskette).
4. Die Tore 40H, 53H, 55H, 57H (V2.1-Kopf/Kaltstart: SIO/Karten) haben im Emulator keine Karte — die
   Schreibzugriffe verpuffen; V2.1 läuft trotzdem bis `A>` (wie V1.2, §4).
5. Das Start-EPROM selbst und damit „wie startete eine ‚ohne SYL‘-Diskette wirklich“ ist durch den
   Versuch **nicht** geklärt; er zeigt nur, dass das V2.1-System **sonst nichts Fremdes braucht**
   (kein anderes Bild-/Tastaturgerät, gleiche K5122-Tore), sobald Kaltstart und Plattentreiber vorhanden sind.

## 10. In der Oberfläche: Modell „A5120 BCU 880“, Disketten und Bänder in `disks/`

Stand 2026-10-16.  Wächter: `BootIntegrationBcu.LieferdisketteBootetImModellBcu`, `cli_bcu_a5120`,
`py_a5120_modelle` (`test_modell_bcu_baut_lade_rom_v06_und_k7636`,
`test_bcu_lieferdiskette_bootet_ueber_die_bindung`), `py_c_api`, `py_packaging`
(`test_bcu_disketten_und_baender_sind_in_der_vorgabeauswahl`), `BcuKassette.FormatiertSchreibtLiest`
(Beispielband).

### 10.1 Modell und C-ABI

- **C-ABI** `k1520_create_a5120_konfig(ausfuehrung, laderom, tastatur, d0..d3)` (additiv;
  `k1520_create_a5120_ausfuehrung(a, …)` = `…_konfig(a, 0, 0, …)`), Abfragen `k1520_a5120_laderom`
  (0 V0.9_2, 1 V0.9_1, 2 V0.6) und `k1520_a5120_tastatur` (1 K7637, 2 K7636, wie gebaut).
  Python `K1520Emulator(laderom="v09_2"|"v09_1"|"v06", tastatur="vorgabe"|"k7637"|"k7636")`
  (am PC 1715 bleibt `tastatur` das Tastatur-ROM `s600`/`tast618`), Eigenschaften `laderom`/`tastatur`.
- **Modell** `a5120-bcu` „A5120 BCU 880 (Lade-ROM V0.6)“ im Profil `a5120emu`: Kern-Maschine
  `a5120-alt` (K7023 64×16 = Satz 2) mit `laderom="v06"`, `tastatur="k7636"` über das neue
  Profilfeld `modell_kernwahl` (`Programmprofil.kern_parameter`).  Konfiguration: fehlt
  `general.model`, bleibt es der A5120 „neu“; sonst ändert sich nichts an alten Konfigurationen.
- **Die Laufwerke stellt das Modell NICHT um** (Merkposten a5120_alt Nr. 14).  Die Disketten haben
  40 Spuren; im 80-Spur-Laufwerk K5601 (Vorgabe der Oberfläche) wird Doppelschritt eingestellt
  („Double Step aktiviert“), das V0.6 findet den Lader auf Zylinder 1 dann nicht — nachgeprüft:
  K5601 → kein `A>` in 40 Mio. Takten, K5600.10 → `A>` nach 9 Mio. Takten.  Der Anwender stellt A:/B:
  auf K5600.10 (Handbuch „BCU 880“).
- Mit dem Kästchen *Kassette* ergibt das Modell dieselbe Maschine wie die Ausführung `Kmbg` mit V0.6
  (beide stecken die K5020 über `installK5020()`, `a5120.cpp`) — die Bestückung von `BcuKassette.*`.

### 10.2 Disketten und Bänder (`tools/bcu_a5120/build.py`)

| Datei in `disks/` | Herkunft |
|-------------------|----------|
| `a5120_bcu/a5120_bcu12_k5600-10_start-syl.hfe` | seit §11 **gebaut**: `create --fs bcu880_syl --boot disks/bootsektoren/boot_bcu12_syl_k7023.bin`, zurückgewandelt = Beleg `satz2_SYL_auf_Spur1/disk.img` (vorher: `save-as … --fs cpa_auto`) (die Diskette trägt kein Dateisystem, das `--fs` braucht nur die Wandlung) |
| `a5120_bcu/a5120_bcu12_k5600-10_arbeit-sys1.hfe` | Beleg `satz2_OhneSYL.1/disk.img` („SYS1“), `save-as` (CP/M erkannt) |
| `a5120_bcu/bcu_kassette_formatiert.k52` | im Lauf von `BcuKassette.FormatiertSchreibtLiest` nach `B:INIT` (Gerät 2) mit `K1520_BEISPIELBAND=<pfad>` geschrieben: Seite A ein Block = Bandmarke `AA 00 00 AA`, 134 392 µm hinter dem Bandanfang |
| `a5120_bandtest/bandtest_boot.k52` | Kopie der Fixture `tests/fixtures/kassette/bandtest_boot.k52` — **Demo, Eigenbau** (`tools/kassette/bandtest/`) |

Die Belege bleiben unverändert (das Skript liest Kopien); die Wandlung ist deterministisch und
wird zurückgewandelt geprüft (`.hfe` → `.img` = Beleg, bytegleich).  `--check` (Wächter `cli_bcu_a5120`)
wandelt erneut und vergleicht, prüft das Bootband gegen die Fixture und die Kassette auf „genau eine
Bandmarke“; der Test `BcuKassette` vergleicht das Beispielband blockweise (ohne Lücken, die an der
Laufzeit des Treibers hängen) mit dem, was INIT schreibt.  Ins Paket kommen alle vier
(`DISKS_DEFAULT`), `seed_user_disks()` kennt dafür `*.k52`.

### 10.3 Befund: kein Kassettentreiber unter CP/A und SCPX

- **CP/A** hat keinen Kassettentreiber und kein Kassettenlaufwerk: das BIOS hält nur den
  Interruptplatz frei — Quelltext des BIOS (`tests/fixtures/disks/cpa_cpa780_k5601_noclock.prn`,
  „Belegung der Interruptsaeule durch das BIOS“): „Kassetten werden ausserhalb des BIOS bedient und
  benoetigen Interruptsaeulenplatz von 0c0h..0cfh!!“ (gleich in allen fünf `cpa_cpa780_*.prn`).  Ein
  Programm, das diese Plätze nutzt, liegt nicht vor [?].
- **SCPX 1.7** hat nur die Robotron-Bibliothek `KMBG.REL` (`doc/kassette/fex/KMBG.REL`, treiber.md
  §6.2: kein Dateiformat, Aufruf mit C = Befehl) — kein Laufwerk, kein Formatierprogramm.  Auf der
  Systemdiskette `disks/a5120_scpx17/` liegen deshalb `KMBG.REL` (zum Binden eigener Programme) und
  das Prüfprogramm `KMBGTEST.COM` (`tools/kassette/kmbgtest/`, Wächter `ScpxKmbg.*`); `BANDTEST` nicht
  (es ist ein Bootband, kein SCPX-Programm).  Ob die SCPX-BIOS-Module selbst die K5020 ansprechen,
  ist nicht disassembliert [?]; das Fehlen eines Kassettenprogramms auf der Hardy-Diskette spricht
  dagegen.
- Ein eigener Kassettentreiber oder ein Formatierprogramm für CP/A oder SCPX wird **nicht** gebaut
  (Anwenderentscheid 2026-10-16) — das wäre Eigenbau ohne Vorbild.  Formatieren und Dateiablage gibt
  es nur unter BCU 880 (§8).

## 11. DiskTool: BCU-Disketten lesen, schreiben, bauen; SYSGEN (2026-10-16)

**Ausgangsbefund:** `k1520disktool info` erkannte die Startdiskette „SYL auf Spur 1“ nicht
(„Verzeichnisplatz 0 ist kein gültiger Eintrag“) — zu Recht: das BIOS (OFF = 2) sucht sein
Verzeichnis ab Zylinder 2, und dort liegt bei dieser Diskette das System (§2). Die Arbeitsdiskette
„ohne SYL“ wurde dagegen schon als `cpa_auto` gelesen (CP/A-Regel für 40 × 26 × 128 = DPB der BCU:
1-KB-Blöcke, 64 Plätze, OFF 2, Versatz 6); das ist nicht zu ändern, nur zu benennen.

### 11.1 Zwei Profile in `data/formats.yaml`

| Profil | Aufbau | Erkennung | Bootabbild (`boot-get`/`boot-put`/`create --boot`) |
|---|---|---|---|
| `bcu880_syl` | Zylinder 0 leer, **1 = SYL-Lader** (6 Sektoren), 2–4 = System; **kein Dateibereich** (`nur_boot`) | automatisch, **SYL-Lader als positiver Nachweis** (Kennung + CRC 0407–06FD gegen 06FE/06FF) | 5 Zylinder = 16 640 B (`disks/bootsektoren/boot_bcu12_syl_k7023.bin`, `…_k7024.bin`) |
| `bcu880` | System auf Zylinder 0–1 („ohne SYL“, so schreibt es SYSGEN), Dateisystem ab Zylinder 2 | nur `--fs bcu880` (`detect: false`; die Diskette liest die Erkennung ohnehin als `cpa_auto`) | 2 Zylinder = 6 656 B (`boot_bcu12_ohnesyl.bin`) |

- **Eine Startdiskette trägt keine Dateien** (`FsProfile::nur_boot`): `put`/`rm`/`attr`/`mkdir`/Rettung
  werden mit Begründung verweigert (eine Datei ab Zylinder 5 sähe BCU nie), `ls` ist leer, freier Platz 0.
  Lesen und Schreiben des **Bootabbilds** gehen — das ist das „Lesen und Schreiben“ dieser Diskette.
  Dateien gehören auf die Arbeitsdiskette (`bcu880`).
- **Das Bootabbild wird VOR dem Schreiben geprüft** (`bcuBootabbildproblem`): `bcu880_syl` verlangt auf
  Zylinder 1 den SYL-Lader mit stimmiger Selbstprüfung und die vom Lader verlangte Systemlänge
  (Offset 5/6 = Bytes ab Zylinder 2); `bcu880` verlangt die Kennung „BCU 880“. Ein CP/A-Bootabbild, ein
  „ohne SYL“-Abbild auf einer Startdiskette oder ein beschädigter Lader gibt eine Diskette, die heil
  aussieht und nicht startet — deshalb Ablehnung, und `create` legt dann **nichts** an.
- **Bau:** `k1520disktool create neu.hfe --fs bcu880_syl --boot disks/bootsektoren/boot_bcu12_syl_k7023.bin`
  ergibt die ausgelieferte Startdiskette **sektorgleich** zum Beleg (Wächter
  `DisktoolBcu.BautDieStartdisketteSektorgleichNach`). K7023 (64×16, Satz 2) und K7024 (80×24, Satz 1)
  unterscheiden sich im Bildtreiber, nicht im Aufbau; das Abbild entscheidet, welches Gerät es bedient.
  Arbeitsdiskette: `create arbeit.hfe --fs bcu880 --boot disks/bootsektoren/boot_bcu12_ohnesyl.bin`, dann
  `put` — von BCU gelistet, Warmstart (`^C`) von ihr geht (`DisktoolBcu.GebauteDisketteBootetUndListetDieDateien`).
- **Packaging:** `tools/bcu_a5120/build.py` (von `packaging/build_payload.sh` für die Beispieldisketten
  vorausgesetzt, Wächter `cli_bcu_a5120`) **baut** die Startdiskette jetzt aus dem Bootabbild mit
  `create --boot`, prüft die Rückwandlung gegen den Beleg und die Bootabbilder gegen `boot-get` der Belege.
  Neue Startdiskette ohne Beleg: dasselbe `create` mit dem Abbild der gewünschten Fassung.

### 11.2 SYSGEN.COM der Diskette

`SYSGEN.COM` (1280 B) ist das **unveränderte Digital-Research-SYSGEN VER 2.0**: liest/schreibt über die
BIOS-Sprungleiste (SELDSK +18H, SETTRK +1BH, SETSEC +1EH, SETDMA +21H, READ +24H, WRITE +27H) die
Spuren **0–1 mit 26 Sektoren in Folge** (Parameter 0128H = 2, 0129H = 26, Sektortafel 012AH = 1…26,
Puffer ab 0900H); Laufwerke nur A–D.
- **Bootfähige Disketten:** ja, aber **nur den Aufbau „ohne SYL“** — SYSGEN kopiert genau die Spuren, die
  das Profil `bcu880` als Bootabbild führt. Im Emulator nachgewiesen
  (`DisktoolBcu.SysgenSchreibtDieSystemspurenDerArbeitsdiskette`: Quelle B:, Ziel C: = vom Werkzeug
  angelegte Leerdiskette; danach sind die Systemspuren byteweise `boot_bcu12_ohnesyl.bin`). Eine
  **Startdiskette „SYL auf Spur 1“ kann SYSGEN nicht erzeugen**: Lader auf Zylinder 1 und System ab
  Zylinder 2 liegen außerhalb dessen, was es anfasst (und Zylinder 2 ist das Verzeichnis). Das geht nur
  mit dem DiskTool (§11.1). Ob eine mit SYSGEN gemachte „ohne SYL“-Diskette am Gerät **startet**, hängt am
  Start-EPROM, das fehlt (§5) — als **Warmstart-Quelle in A:** ist sie nachgewiesen (§8.1).
- **Bootfähige Kassetten:** nein. SYSGEN spricht nur die BIOS-Plattenfunktionen für A–D an; der
  Kassettentreiber `KMBG.REL` hängt sich am **BDOS-Einsprung** ein (§8.1) und stellt F: als Dateigerät
  bereit — SYSGEN erreicht F: nicht (Laufwerksname außerhalb A–D wird abgewiesen). Das Lade-ROM bootet
  zwar von Kassette (Bootband `SYL`, `doc/merkposten/kassette.md`), ein BCU-Systemband gibt es aber weder als
  Beleg noch als Werkzeug; es wäre ein Eigenbau (Lader + 8,5-KB-System in Bandblöcken) — nicht gebaut.
- **Tastatur-Falle beim Ausprobieren:** SYSGEN (wie INIT, §8.1) wartet auf **0DH**. Satz 1 (K7024) übersetzt
  ET1 → 0DH im CONIN und läuft durch; Satz 2 (K7023) reicht 0AH durch und käme nicht über „THEN TYPE RETURN“ (aus dem Code, `CP 0DH` / `JNZ` bei 0240H; nicht gelaufen).

