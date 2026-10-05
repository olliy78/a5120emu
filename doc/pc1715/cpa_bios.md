# CP/A-BIOS des PC 1715 als Hardwarebeleg

Quelle: `~/projects/CPA/CPAA5120/src/pc_1715/` (BIOS „CP/A für PC1715“, Stände 1988,
Kopfdatei `biop.mac`). Ausgewertet am 2026-10-03 für Plan `doc/design/21_pc1715.md`.
Alle Angaben sind **[Quelle]**: so programmiert das BIOS die Hardware. Wo das von
Servicehandbuch oder MAME abweicht, steht es dabei.

## 1. Floppy: ein gemeinsamer Treiber für K5120/22 und PC 1715

`biopdskt.mac` (29.03.88) ist **ein** Treiber für zwei Karten, gewählt mit
`fdc equ f1715` bzw. `fdc equ k5120` („K5120/22“). Die Legende im Treiberkopf heißt
„Legende AMF“ und beschreibt beide Karten bitweise. Die Unterschiede sind vollständig
aufgezählt; alles andere (Lesen, Schreiben, Formatieren, Markensuche, Steppen) ist
derselbe Code:

| | K5120/22 | PC 1715 | Beleg |
|---|---|---|---|
| Steuer-PIO A/B (Daten, Steuer) | 10H/11H, 12H/13H | **04H/05H, 06H/07H** | `biopdskt.mac` 74–106 |
| Daten-PIO A (Schreiben), B (Lesen) | 14H/15H, 16H/17H | **00H/01H, 02H/03H** | dto. |
| Select-Latch (Bit 7–4 /SEL LW3–0, Bit 3–0 /LCK) | 18H | **20H** | dto. |
| Motorregister (Bit 7–4 /MOT) | – | **21H**, wird mit **demselben Byte** wie 20H beschrieben | `biopdskc.mac`, `biopdskt.mac` 224, 684 |
| Tor B Bit 1 Markenmeldung | **/MKE, 0 = Marke** | **MKE, 1 = Marke** | Legende; `biopdskp.mac` 211 (`jr nz` statt `jr z`) |
| Tor B Bit 2 | /SYN (Eingang) bzw. 8″-MFM-Takt | **MFM: Ausgang, 1 = MFM** | Legende |
| Tor B Bit 4 | /FA Fehler AMF (Eingang) | **FO: Ausgang, 0 = 5″, 1 = 8″** | Legende |
| Tor B Richtungsmaske (Mode 3) | F7H | **E3H** (Bit 2, 3, 4 Ausgang) | `biopdskc.mac` |
| Tor B Interruptsteuerwort | 17H (ODER, **low**, Maske) | **37H (ODER, high, Maske)**, Maske FDH = nur Bit 1 | dto. |
| 5″ FM | nicht möglich | **möglich** | `biop.mac` („kann AMF5122 nicht!“), `ft.kom` Bit 3 |

Weitere Einzelheiten, die das Modell tragen muss:

- **Zwei Interrupts der Steuer-PIO:** Tor A (Ausgabe) mit eigenem Vektor `ivdsk1` =
  **Indexinterrupt** (Strobe /ASTB = Indexloch), im Leerlauf mit `OUT (05H),03H` gesperrt;
  Tor B = **Interrupt bei Marke** (`ivdsk2`, `diomrk`). Der Motorstart zählt
  Indexinterrupte (mind. 1, bei 40-Spur-5″ mind. 4 Umdrehungen).
- **Laufwerkserkennung beim Kaltstart:** Select-Byte `F7H` „ohne Lock“, bis 85 Schritte
  Richtung Spur 0, bis `/T0` (Tor B Bit 7) kommt. Die Prüfung ist auskommentiert —
  alle 4 Laufwerke gelten als vorhanden („8″-Beistell-Laufwerke ohne Strom“).
- **Lesen eines Datenfelds** (`biopdskp.mac` ~200): Tor A `B5H` = Marke rücksetzen,
  dann `85H/81H` (MFM) bzw. `87H/83H` (FM) = Markenerkennung ein; höchstens 160 Abfragen
  von Tor B Bit 1; danach **A1-Bytes überlesen** (`cp 0A1h`), erstes Nicht-A1 = Marke;
  Rest mit `INI` an 02H. Das passt zur Festlegung „Marke = erstes Byte der Sync-Gruppe,
  das kein A1 ist“ (CLAUDE.md, fremde Sync-Sitte).
- **Laufwerkswahl** `LD A,77H / RLCA (LW+1)×` → `OUT (20H)` und `OUT (21H)` — dieselbe
  Nibble-Belegung wie die 8212 am A5120 (Wächter `K5122Test.DriveSelect_HighNibbleIstSelect`).
- Fehlercodes `C D R L S T U W` wie beim A5120-CP/A.

**Folge für den Kern:** K5122 im `/WAIT`-Betrieb mit einer Konfiguration „1715“:
Portlage, Motorregister 21H, MKE high-aktiv (die Vorgabe des Modells; low-aktiv braucht
nur der PRG 710), Tor B Bit 2/4 als Ausgänge (FM/MFM und 5″/8″ vom Rechner gesetzt statt
nur am Medium abgelesen), Indexinterrupt über Tor A.

## 2. Bildschirm

`biopcrt.mac`, `biopcrtc.mac`:

- Ports: Befehl **19H**, Parameter **18H**, **34H** = Bildpuffer/Zeichensatz.
- **34H = `high(Anfang >> 2) | 40H × Zeichensatz`**, d. h. Bit 5–0 = A15–A10 des
  Bildpuffers (1-K-Raster), Bit 6 = Zeichengenerator 2. Bildpuffer bei CP/A **F800H**
  (→ 3EH). Deckt sich mit AP-1b.
- Init-Folge: `OUT 34H` → Reset `00H` an 19H → 4 Parameter an 18H → Start `20H` an 19H.
  Parameter aus den Gleichungen:
  `P1 = Spalten−1`, `P2 = (VRTC−1)·64 + Zeilen−1`, `P3 = (Unterstrich−1)·16 + Linien−1`,
  `P4 = Feldattributmodus·64 + Cursorformat·16 + HRTC−1`.
  CP/A: **Feldattribute nicht transparent** (`fam = 1`, wie BC A5120/30), Cursor
  **Block invers ruhend** (`csf = 2`), Linien 12 (80 × 24) bzw. 15 (64 × 16).
- **CP/A fährt 25 bzw. 17 Zeilen**: 24/16 Textzeilen + **Statuszeile** (`cpastz = 1`,
  inverses Feldattribut 90H). HRTC wird dafür um 2 gekürzt. Das 8275-Modell darf die
  Zeilenzahl also nicht auf die Werte des Servicehandbuchs festlegen.
- Cursor: Befehl `80H` an 19H, dann Spalte, Zeile an 18H; nur alle 50 ms über den
  Zeittakt gestellt.
- **Automatische Formatumschaltung beim Kaltstart** (`babdyn = 10`): kommt binnen 10 s
  keine Uhrzeiteingabe (oder ESC), schaltet das BIOS auf das andere Bildformat
  (64 × 16 ↔ 80 × 24) und fragt erneut. Für Tests: Uhrzeit eingeben oder ESC, sonst
  wechselt das Bild unter dem Wächter.
- **CTC-Kanal 2 (0AH)** wird beim 1715 als Merker missbraucht: Bit 6 = 0 heißt „großer
  Bildschirm“, für A5120-Programme, die `IN A,(0AH); BIT 6,A` prüfen. Reine Software,
  aber das CTC-Modell muss den geladenen Zeitkonstantenwert zurücklesen lassen (Zählerstand
  bei gestopptem Kanal).
- Zweiter Zeichensatz per SI/SO nur mit `zs2var = 1` (in dieser Generierung aus).

## 3. Tastatur

`biop.mac` 600 ff., `biopkbdc.mac`, `biopkbd.mac`:

- SIO-A **Empfänger** = Tastatur (Daten 0CH, Status 0EH), **kein CTC-Takt** — „Takt von
  PC1715-Tastatur geliefert“. Init: Kanal-Reset, WR4 = 04H (×1, 1 Stoppbit, ohne
  Parität), WR1 = 00H (**kein Interrupt**, gepollt über den 5-ms-Zeittakt), WR3 = C1H.
- Tastencodes kommen als Rohcodes und werden im BIOS „physisch umkodiert“
  (`kbd*`), Funktionstasten PF0–PF15 (PF13–15 = 1715-eigene Tasten, PF14 = Monitor,
  PF15 = Stopp), Ziffernblock mit `00`, CE, Komma; Stringbelegung frei definierbar.
  Codetabelle → zusammen mit `doc/pc1715/tastatur.md` (S600) gegenprüfen.

## 4. Schnittstellen und Zeittakt

| Gerät | Daten | Status | CTC Senden/Empfangen | Treiber |
|---|---|---|---|---|
| Drucker (TTY:) X4 | 0CH | 0EH | 08H/08H | DTR (nur Senden) |
| V.24 (UC1:) X5 | 0DH | 0FH | 09H/09H | DTR, 9600 8N1 |
| IFSS A (Zusatzkarte) | 14H | 16H | 10H/11H | DC1/DC3 |
| IFSS B (Zusatzkarte) | 15H | 17H | 12H/12H | DC1/DC3 |

- **System-CTC 08H:** Kanal 0 Drucker-Takt, Kanal 1 V.24-Takt, Kanal 2 Merker
  Bildformat (s. o.), **Kanal 3 = Uhr/Zeittakt** (`bioptimc.mac`, `timctc = 3`,
  Interruptvektor F8H-Bereich).
- SIO-Interruptvektoren ab D0H (`biopnuc.mac` 919 ff.), PIO-Vektoren `ivdsk1/2`.
- Kein Speicherschutz („hardwaremäßig bei PC1715 nicht möglich“).

## 5. CPA_Workbench (`~/projects/CPA_Workbench`)

Die Workbench baut CP/A für `bc_a5120`, `pc_1715` und `pc_1715_870330` (M80/LINKMT
über `cparun`, Diskettenbild über `cpmcp`, `.hfe`/`.scp` über `gw`).

- **`src/pc_1715`** = dieselben Quellen wie oben (24.05.88), einziger Unterschied
  `umlaut equ 0` statt `1` in `biop.mac`. **`src/pc_1715_870330`** = ältere Fassung
  30.03.87 (`BIOS*.MAC`, aufgeteilt in `BIOSSD1/2`, plus `SCPURLAD.DAT`) — zweite,
  unabhängige Belegquelle.
- **Bootkopf des PC 1715** (`prebuilt/pc_1715/bootsec.bin`, 128 B): Wort `F003H`,
  Ladeparameter, danach ein **echter Verzeichniseintrag `@OS.COM`** (Blöcke 3–9) und am
  Ende eine Formatbeschreibung (`50 05 03 1B …` = 80 Zylinder, 5 Sektoren, 1024 B). Er
  liegt **im ersten Verzeichnissektor** einer cpa800-Diskette ohne Systemspuren — das
  erklärt, warum das DiskTool „Verzeichnisplatz 0 ungültig“ meldet (AP-0c, AP-D).
- **Vermutlich ein Fehler in der Workbench:** `cpa_builder.py` nimmt für den
  Diskettenbau **immer** `prebuilt/bc_a5120/bootsec.bin` (`BOOTSEC_PREBUILT_FIXED`,
  Zeile 400). Dessen Kopf ist „`SYL`“ (A5120-Lader) statt `F003H`. Damit dürfte eine
  `pc_1715`-Diskette aus der Workbench am PC 1715 **nicht** booten; auch
  `prebuilt/pc_1715_870330/bootsec.bin` ist eine Kopie des A5120-Laders. Ungeprüft —
  sobald der Emulator CP/A 1715 bootet (AP-3), ist das ein Testfall.
- **`additions/pc_1715/`:** `CPA1715G.COM` (Systemübertragung), `FORMATP.COM`/
  `formatpx.com` (Formatierer) und **`pctest.com` = Werks-Testprogramm „Testprogramm fuer
  PC 1715“**: Speicher, V.24, Drucker, Floppy, Zusatzinterface (2 × V.24 oder 2 × IFSS),
  mit Rückkopplungssteckern 320-032 (V.24), 330-032 (IFSS), 330-042 (Drucker). Als
  Abnahmeprüfung des Emulators geeignet (wie `SERTEST.COM` am A5120): Rückkopplungen über
  `SerialHub`-Schleife.

## 6. Konsequenzen für den Plan

1. **Floppy:** Die Gleichsetzung mit der K5122 ist durch den BIOS-Quelltext belegt,
   nicht nur durch Analogie. Die Unterschiede aus §1 sind die vollständige Liste für die
   Konfiguration „1715“ (AP-2).
2. **CP/A 1715 eignet sich als erstes Wächter-OS:** Quelltext, baubar, jede Portzugriff
   benannt. Allerdings mit Statuszeile (25 Zeilen) und Formatumschaltung nach 10 s.
3. **CTC-Kanal 2 als Lese-Merker** und **25-Zeilen-Betrieb des 8275** in die Tests
   von AP-1a/AP-2 aufnehmen.
4. **Abnahme mit `pctest.com`** (Werkstest) als eigenes AP nach den Schnittstellen
   (AP-4a); **CP/A 1715 aus der Workbench bauen und booten** als Wächter, sobald AP-3
   steht — dabei den vermuteten Bootkopf-Fehler der Workbench prüfen.
