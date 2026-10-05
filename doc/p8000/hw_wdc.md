# P8000 — Winchester-Beisteller und Winchester-Disk-Controller (WDC): Hardware-Referenz

Stand 2026-10-05, AP P1. Quelle: **P8000-Hardwarehandbuch**, Kap. 6 (S. 6-1…6-17): §1 Konstruktion, §2 Stromversorgung,
§3 Winchester-Laufwerk (ST506-Signale knapp), **§4 WDC-Hardware** (4.1 Funktionsbeschreibung, 4.2 Entwicklungsstände
Index 0/1/3), **§5 WDC-Firmware**; dazu Kap. 3 (16-Bit-Seite: Winchester-Anschluss, `hw_16bit.md` §4.5), Kap. 1 §5 (Index-Kennung),
Anlage A §2. Konventionen wie `hw_8bit.md` (S., Z., **[unklar: …]**, **[Deutung: …]**).

> **Wichtigster Befund:** Das Kapitel 6 des Handbuchs (nur **drei Seiten Firmware**, S. 6-15…6-17) enthält **weder die
> Kommando-Codes noch die Kommandoblock-Belegung noch die Statuscodierung des Hostinterface-Protokolls noch die
> Parametertabellen**. Die Tabellennummerierung springt von „Tabelle 6.3-6" (Laufwerk K5504.50) direkt auf **„Tabelle 6.5-3 WDC-Fehler"**
> — die Tabellen **6.5-1 und 6.5-2** (vermutlich Kommandos/Parameter) fehlen im Handbuch (PDF hat 261 Seiten, kein
> Bildmaterial dort; Z. 9182). Das Protokoll muss aus **Firmware-Abzügen** (`doc/p8000/eproms/WDC/`) und aus
> **WEGA-Quellen** (`sa.format`-Log, `wd`-Treiber) gewonnen werden — siehe §8.

---

## 1. Beistellergerät (S. 6-3…6-6, Anlage A §2)

- Kompaktgerät wie P8000-Computer; innen: Kompaktnetzteil, **Winchester-Laufwerk 5¼″**, **WDC-Leiterkarte**, Anzeigeeinheit;
  vorne Netzschalter, 3 LEDs (+12 V, +5 V, −12 V), Bereitschaftsanzeige des Laufwerks; hinten Netz, 2 Sicherungen,
  **Steckverbinder zum P8000-Computer** (25-polig, `hw_16bit.md` Tab. 3.6-19/3.7-19).
- Netzteil: +5 V/10 A, +12 V/4 A, −12 V/0,1 A. WDC: +5 V/3,0 A (+12/−12 V werden vom WDC **nicht** verwendet, nur zum
  Anzeigestecker); Laufwerk: +5 V/1,0 A, +12 V/0,6 A (Anlauf 2,0 A).
- Anlage A: 1 × oder 2 × 5¼″ Winchester; Schnittstelle WDC↔Laufwerk **ST506**, WDC↔Computer **parallel**.

## 2. Laufwerke (S. 6-7…6-9, Tab. 6.3-1…6.3-6; Anlage A)

Das Laufwerk ist mit ST506-Schnittstelle ausgestattet (**XS 20-polig, XR 34-polig**, Direktstecker). Zylinder werden von
**außen nach innen** ab 0 gezählt, Köpfe ab 0; Spur = Zylinder + Kopf; **Defektspurtabellen** stehen außen auf dem Laufwerk
und sind beim Formatieren zu berücksichtigen; Parkspur/Transportspur (`sa.shipdisk`) — manche Typen parken selbständig, bei
anderen per Kommando.

| Typ | Index (V: …d) | Zylinder | Köpfe | Vorkompensation | Startzeit | min. Step | Kapazität (18 × 512 B) |
|---|---|---|---|---|---|---|---|
| NEC D5126 | 1 | 615 (0…614) | 4 (0…3) | ab Zylinder 128 | max. 15 s | 18 ms | 22 140 KB |
| NEC D5146 | 2 | 615 (0…614) | 8 (0…7) | ab Zylinder 128 | max. 15 s | 18 ms | 44 280 KB |
| Robotron VS1/VS2/VS3 | 4 | 820 (0…819) | 6 (0…5) | keine | max. 23 s | 8 ms | 44 280 KB |
| **Robotron K5504.50** | 5 | **1024 (0…1023)** | **5 (0…4)** | keine | max. 15 s | 5 ms | **46 080 KB** |

Nachrechnung: K5504.50: 1024 × 5 × 18 × 512 B = 47 185 920 B = **46 080 KiB** ✓; VS: 820 × 6 × 18 × 512 = 45 342 720 B =
44 280 KiB ✓; D5126: 615 × 4 × 18 × 512 = 22 671 360 B = 22 140 KiB ✓; D5146: 615 × 8 × … = 44 280 KiB ✓.
Die Zahlen sind für **jeden** Typ **18 Sektoren × 512 Byte** (Firmware-Vorgabe, §5).

**ST506-Signale XR (34-polig, gerade Pins Signal / ungerade GND), Tab. 6.3-1:** 2, 4 reserviert (E); 6 WRITE GATE (E);
8 SEEK COMPLETE (A); 10 TRACK 0 (A); 12 FAULT WRITE (A); 14 HEAD SELECT 0 (E); 16 „zu XS-Pin 7" (E);
18 HEAD SELECT 1 (E); 20 INDEX (A); 22 READY (A); 24 STEP (E); 26 DRIVE SELECT 1; 28 DS 2; 30 DS 3; 32 DS 4 (E); 34 DIRECTION IN (E).
**XS (20-polig), Tab. 6.3-2:** 1 DRIVE SELECTED (A); 3, 5, 9, 10 reserviert; 7 „zu XR-Pin 16"; 11, 12, 15, 16, 19, 20 GND (inkl.
2, 4, 6, 8); 13/14 ±MFM WRITE DATA (E); 17/18 ±MFM READ DATA (A). **Hinweis:** nur HEAD SELECT 0 und 1 am XR (bis 4 Köpfe
direkt); die Kopfwahl 4…7 der D5146/VS-Laufwerke (HEAD SELECT 2) geht über die „reservierten" Pins **[unklar:
Pin 2/4 „reserviert" könnte HEAD SELECT 2 sein; Handbuch schweigt]**.

## 3. WDC-Hardware (S. 6-10…6-14)

### 3.1 Aufbau
- 6-Lagen-Leiterkarte 380 × 250 mm. Disk-Schnittstelle nach **ST506/412**, **bis 3 Laufwerke** (XS1 und XS2 — XS2 für weitere Laufwerke).
- **„Intelligenter Controller":** **UA880** (CPU), **UA857** (CTC), **8 KB EPROM** (2 × U2732 = 2 × 4 KB; Firmware), **6 KB statischer RAM**
  (12 × U214 D20), Host-Schnittstellensteuerung, Disk-Schnittstellensteuerung.
- **Vier Funktionsblöcke** (Bild 6.4-1, nur Blockbild): RAM + Steuerlogik; Host-Schnittstellensteuerung + Hostinterface;
  Disk-Schnittstellensteuerung + Diskinterface; zentrale Steuerung (CPU, CTC, EPROM, I/O-Ports).
- **WDC-RAM (6 KB)** ist **Pufferspeicher** zwischen Host und Medium; **CPU und beide Schnittstellen greifen unabhängig/auch gleichzeitig
  zu** (Dual-Port-ähnlich über Steuerlogik; Details **[unklar]**).
- **Host-Schnittstelle:** 8 Bit parallel + zusätzliche Steuerbits; realisiert **blockweise Übertragung** Host ↔ RAM, sowohl für
  Daten als auch für **Kommando- und Quittungsinformation** (einheitlicher Hardwarevorgang).
- **Disk-Schnittstellensteuerung:** parallel↔seriell, **MFM- oder FM-codiert**, mit Marken und CRC-Bytes; Rückgewinnung, CRC-Prüfung,
  Eintrag ins RAM. Zentrale Steuerung ist über **I/O-Ports direkt** mit dem Diskinterface (Ringleitung) verbunden (Laufwerk-/Kopfwahl,
  Kopfbewegung, Schreibfreigabe in Firmware). **CTC** erzeugt Interrupts nach Ereignissen (Indeximpuls, Marke erkannt, Datenblock
  über Host eingetragen).
- Steckverbinder (Bild 6.4-2): STV (Strom), XR, XS1, XS2, Anzeige, Zusatzleiterkarte (entfällt ab Index 3), Service-Steckverbinder X13
  (3-reihig A/B/C), **XH (Hoststecker)**, CPU, EPROM 1/2. **[unklar: Port-/Adressbelegung der WDC-Z80 — E/A-Adressen von CTC, Host-Ports,
  Disk-Steuerung stehen nicht im Handbuch → nur aus dem Firmware-Disassembly/Schaltplan, AP2/AP3.]**

### 3.2 Entwicklungsstände (S. 6-14, §4.2)

| Index | Merkmale | Host-Reset |
|---|---|---|
| **0** (Entwicklungsmuster, GLE vor 3/87 und Produktions-Index 0) | Laufwerkskabel XR/XS **angelötet**, abweichende Steckeranordnung; Zusatzleiterkarte **liegend**; Host: 25-pol. Sub-D-Buchse; Prüfstecker: 58-pol. direkte EFS-Buchse | **RST− low-aktiv** (Pin 24) |
| **1** | Zusatzleiterkarte **stehend**; Host 25-pol. Sub-D; Prüfstecker 58-pol. **indirekte** EFS-Buchse | low-aktiv |
| 2 | interner Arbeitsstand | — |
| **3** | **keine Zusatzleiterkarte** mehr | **high-aktiv** (`RST`), anderer Stift am Hoststecker |

- Interface der Indizes 0 und 1 identisch.
- **Kopplungsregel:** 16-Bit-Karten **ab Index 4** besitzen **beide** Reset-Ausgänge (`RST−` Pin 24, `RST` Pin 13 / X16-B5) und arbeiten
  mit **jedem** WDC-Index; **WDC ab Index 3 nur mit 16-Bit-Karten ab Index 4**.
- Typenschild „V: abcd": c = WDC-Index, d = Laufwerk (1/2/4/5, §2).

## 4. Hostinterface (Seite WDC; Seite 16-Bit-Karte siehe `hw_16bit.md` §4.5)

Pinbelegung des 25-poligen Stecker (X8 der 16-Bit-Karte = Stecker am Beisteller; Index-4-Karte alternativ X16 intern):

| Signal | Pin (X8) | Richtung (aus Sicht der **16-Bit-Karte**) | Funktion |
|---|---|---|---|
| D0…D7 | 14, 2, 5, 17, 4, 16, 3, 15 | E/A | Datenbyte (Kommando/Daten) |
| `WDARDY−` | 1 | A | Control (PIO „ARDY"-Name → Handshake: Karte bereit?) |
| `ASTB−` | 18 | E | Control (PIO-Strobe vom WDC) |
| `STATUS0`, `STATUS1`, `STATUS2` | 21, 10, 22 | E | **drei Statusbits** des WDC |
| `TE−` | 25 | A | Control |
| `TR−` | 9 | E | Control |
| `RST−` (low) | 24 | A | Reset (WDC Index 0/1) |
| `RST` (high, Index 4) | 13 | A | Reset (WDC Index ≥ 3) |
| GND, 5P | 7, 20 | | Masse, +5 V |
| nicht benutzt | 12 (`WDARDY`), 23, 11, 19 (`BRDY`), 6 (`BSTB−`) | | Port-B-Handshake |

- Das Handbuch (S. 6-15) sagt: „Die Verständigung des WDC mit dem Host-Rechner erfolgt über **zwei jeweils acht Bit breite
  Parallelports**. Ein Port dient zur **byteweisen Übertragung von Kommandos und Daten**. Der andere Port wird **bitweise** betrieben.
  Er enthält **drei Steuerbits** zur unmittelbaren Koordination der Byteübertragung auf dem anderen Port und **drei Statusbits**
  zur Meldung des inneren Zustandes des WDC."
- Daraus folgt: der 16-Bit-Seite entspricht **PIO2** (FFA1…FFA7): **Port A** = Daten (D0–D7, Handshake `WDARDY−`/`ASTB−`),
  **Port B** = bitweise (Steuerbits + `STATUS0–2`, `TE−`, `TR−`) **[Deutung]**. Wer von den Signalen `ASTB−`, `TE−`, `TR−`, `WDARDY−`
  die „drei Steuerbits" sind, steht **nicht** im Text **[unklar]**.
- **WDC-Zustände** (Statuscodierung durch STATUS0–2 — **Zuordnung der 3 Bit zu den Zuständen nicht angegeben**):
  - WDC besetzt bzw. in Arbeit
  - WDC bereit zum **Kommandoempfang**
  - WDC bereit zum **Datenempfang**
  - WDC bereit zum **Datensenden**
  - WDC **Fehlerstatus**
  (fünf Zustände in drei Bit; Kodierung **[unklar]**.)
- **Übertragungsgröße:** bei Lese-/Schreibkommandos **jeweils ein Sektor** (512 B) je Kommando; bis Firmware 3.0 max. **512 Byte je Kommando** zwischen
  WDC und Grundgerät.

## 5. Firmware (S. 6-15…6-17, §5)

### 5.1 Organisation
- Programm in **zwei 4-KB-EPROMs** (2 × 2732). **Laufwerksabhängig:** je Laufwerkstyp anderer EPROM-Inhalt.
- Sicherung: **CRC-Prüfsummen und deren Komplemente** beider EPROMs stehen **am Ende des zweiten EPROM**. Fehlercode **20H** = EPROM-Prüfsummenfehler.
- **Kennzeichnung:** `WDC_x_y.y.zz` — x = EPROM-Nr. (**1** = Adressen 0…0FFFh, **2** = 1000h…1FFFh), y.y = Versionsnummer, zz = Laufwerkstyp
  (01 NEC D5126, 02 NEC D5146, 04 Robotron VS1/VS2/VS3, 05 Robotron K5504.50). Diese Kennung steht **auch im EPROM 1 auf den Adressen
  2…0Dh** (die EPROM-Nr. durch „V" ersetzt). *Gegenprobe am Abzug:* `WDC_1_3.0_04` = `18 0C "WDC_V.3.0.04"` ✓ (JR + Kennung); `WDC_1_4.2` =
  `18 08 "WDC_4.2 "`.
- **Laufwerksparameter, die die Firmware unterscheiden:** Zylinderzahl, Kopfzahl, **Zylindernummer der Vorkompensations-Einschaltung**, Startzeit
  (Power-on bis ready), min. Kopfbewegungszeit zwischen Nachbarzylindern, Zeitunterschiede für **Einzelschritt- und gepufferten Schrittbetrieb**
  (Hinweis: *Abzug-Differenzliste* `eprom_diffs.txt` zeigt z. B. Bytes 00F8–00F9 = „start of praecompensation" für 3.0: `_04` ↔ `_02`).
- **Aufteilung:** Programmierung der **Host-Schnittstelle**, der **Disk-Schnittstelle**, der **Organisationsaufgaben der CPU**; die Schnittstellen laufen
  nach der Initialisierung selbständig und melden sich per **Interrupt** zurück; Schnittstellen und CPU können auch gleichzeitig auf das RAM zugreifen.

### 5.2 Plattenformat und Defektspurtabelle
- **18 Sektoren/Spur × 512 Byte** (Firmware-Vorgabe).
- **Defektspurtabelle (Bad Track Table, BTT):** liegt auf **jedem Laufwerk im Datenbereich von Zylinder 0, Kopf 0, Sektor 1**;
  die Firmware **übergeht** defekte Spuren im Normalbetrieb. **[unklar: Format der BTT (Eintragsstruktur, Länge, Endekennung), Plausibilitätsprüfung
  (Fehler 06), Reservespuren/Ersatzspuren — nicht im Handbuch; aus `sa.format`-Log (WEGA) bzw. Disassembly ableiten.]**
- Der BTT-Ablauf in `sa.format` (WEGA-Handbuch/Log, nicht hier): Abfrage „Manual Input of Bad Track ?", Eingabe Zylinder/Kopf.

### 5.3 Initialisierung nach RESET (S. 6-15/-16)
1. CPU startet das EPROM-Programm.
2. **CRC beider EPROMs berechnet** und mit den am Ende des 2. EPROM gespeicherten Werten verglichen.
3. **WDC-RAM gelöscht**, Arbeitszellen initialisiert; Stack vorbereitet; Adressen der Interruptserviceroutinen in RAM-Zellen eingetragen.
4. **Ready-Status der Laufwerke** über die Disk-Schnittstelle abgefragt.
5. **Köpfe schrittweise zu Zylinder 0** bewegt (Track0-Signal).
6. **BTTs der angeschlossenen Laufwerke ins WDC-RAM** gelesen.
7. **Host-Schnittstelle auf Kommandoempfang programmiert**; Status wird ausgegeben.
- **Fehler während der Initialisierungsphase** werden **erst nach dem ersten empfangenen Kommando** an den Host gemeldet; dieses Kommando wird **nicht ausgeführt** —
  soll es ausgeführt werden, muss es **ein zweites Mal** gesendet werden.

### 5.4 Kommandoblock
- Jede Aktivität wird über einen **Kommandoblock von immer neun Bytes** mitgeteilt; ungenutzte Bytes beliebig belegt.
- **Byte 0 = Kommandocode**, die folgenden Bytes je nach Code unterschiedlich verwendet.
- Bei Lese-/Schreibkommandos wird **je Kommando das Datenfeld eines Sektors** gelesen/geschrieben.
- **[unklar: Kommandocodes, Bytebelegung (Laufwerk, Zylinder, Kopf, Sektor/Block, Anzahl, Option) und Datenphase (Handshake-Folge,
  Rückmeldung/Statusbyte) fehlen im Handbuch (Tabellen 6.5-1/6.5-2).]** Fehlercodes erwähnen Eigenschaften:
  Kommandocode (01), Head-Nummer (02), Drive-Nummer (03), Zylindernummer (04), **Blocknummer im Kommando (15)** ⇒ das Kommando enthält zumindest
  *Kommandocode, Drive, Head, Zylinder, Sektor/Block*; die Reihenfolge ist offen.

### 5.5 Fehlercodes (Tab. 6.5-3, S. 6-17, Z. 9182–9208)

| Code | Bedeutung |
|---|---|
| 00 | kein Fehler, Aktion ok |
| 01 | unerlaubter Kommandocode |
| 02 | Head-Nummer im Kommando zu groß |
| 03 | Drive-Nummer im Kommando zu groß |
| 04 | unerlaubte Zylindernummer |
| 05 | Lesefehler beim Einlesen der Defektspurtabelle |
| 06 | Plausibilitätsfehler in der eingelesenen Defektspurtabelle |
| 07 | nach RESET nicht Track0 erreicht (kein Track0-Signal) |
| 08 | kein Laufwerk „ready" |
| 09 | Sektor nicht gefunden nach Distanzwiederholung |
| 0A | kein „FB" als Datenfeldkennzeichen |
| 0B | keine Marke erkannt (Zeitüberwachung) |
| 0C | kein Kennfeld gefunden, Fehler im Kennfeld |
| 0D | CRC-Fehler im Kennfeld (noch kein exaktes Kennfeld gefunden) |
| 0E | CRC-Fehler im Kennfeld (exaktes Kennfeld gefunden) |
| 0F | CRC-Fehler im Datenfeld |
| 11 | Track0-Signal bei gefordertem Zylinder 0 nicht aktiv |
| 12 | Sektornummer des zu schreibenden Sektors falsch |
| 13 | „Fault Write"-Signal aktiv |
| 14 | Schreibfehler bei „Read after Write" |
| 15 | Blocknummer im Kommando zu groß |
| 20 | EPROM-Prüfsummenfehler |

(Code 10H und 16H–1FH fehlen in der Tabelle.) „Block"-Nummer ≠ Sektor (Fehler 15 vs 12)? — die Zuordnung Block ↔ (Zylinder, Kopf, Sektor) **[unklar]**.
Hinweis (P13): „Kennfeld" = ID-Feld, „FB" = Datenfeldkennzeichen (Data-Address-Mark); der Text nennt „MFM- oder FM-codiert, mit Marken und CRC-Bytes" und Read-after-Write — Gap-/Interleave-Daten stehen nicht im Handbuch.

### 5.6 Firmware-Versionen (S. 6-17)
- **Bis Version 2.5:** Fehler bei der Arbeit mit dem Beisteller möglich (**„Fehler A", Behandlung defekter Spuren**); **ab 2.6** beseitigt.
- **Bis Version 3.0:** nur **ein angeschlossenes Laufwerk** nutzbar; **max. 512 Byte je Kommando** zwischen WDC und Grundgerät (ab 3.x offenbar mehr/mehrere Laufwerke —
  **[unklar: nur aus dem Satz „Bis Version 3.0" gefolgert]**).
- Abzüge in `doc/p8000/eproms/WDC/`: 2.4_04, 3.0_{01,02,04}, 3.2_{04,05}, 3.3_{04,05}, 3.4_{04,05} (nur EPROM 1 nötig), 4.0_05, 4.2, 3.6_05 (läuft nicht), 4.0_04
  (selbstgebaut) — siehe `WDC.txt`. Datei `eprom_diffs.txt` listet die Parameterunterschiede (Vorkompensation u. a.).

## 6. Hardware-Unterschiede Index 0/1 ↔ 3 (Zusammenfassung)

| Punkt | WDC Index 0 / 1 | WDC Index 3 |
|---|---|---|
| Zusatzleiterkarte | liegend (Idx 0) / stehend (Idx 1) | entfällt |
| Host-Reset | low-aktiv `RST−`, Pin 24 | **high-aktiv** `RST`, anderer Stift (X8-13 / X16-B5 der 16-Bit-Karte) |
| Laufwerkskabel | Idx 0 angelötet / Idx 1 gesteckt | gesteckt |
| Prüfstecker | 58-pol. EFS (direkt / indirekt) | Service X13 |
| Koppelbar mit 16-Bit | Index 0…4 (alle) | **nur Index ≥ 4** |
| Host-Verbindung | 25-pol. Sub-D | 25-pol. Sub-D bzw. intern X16 (Karte Index 4 Variante) |

## 7. Widersprüche

- **W1** — Kap. 6 verweist mit „Tabelle 6.5-3" auf eine **dritte** Tabelle einer Reihe, deren Tabellen 6.5-1/6.5-2 fehlen (Z. 9182); das TOC kennt
  „5 Winchester-Disk-Controller Firmware 6-15" ohne Tabellenverzeichnis. Vermutlich fehlende Seiten bei der Scan-/Konvertierung des Handbuchs (nicht im PDF).
- **W2** — Host-Pin „13": Index 1 Tab. 3.6-19 „13 — frei", Index 4 Tab. 3.7-19 „13 — `RST` Reset" (X16-B5) — wie in §3.2 beschrieben (Index-Unterschied, kein Fehler).
- **W3** — Index 4 Tab. 3.7-19: **RST− (X8-24) hat keinen X16-Pin** („−"), `RST` hat X16-B5 → intern (X16) nur der high-aktive Reset verfügbar; passt zur Regel
  „WDC ab Index 3 nur an Karte ab Index 4"; offen ob der interne Anschluss ältere WDC (RST− low) überhaupt bedienen kann.

## 8. Offene Fragen an Schaltplan/Gerät (und Quellenhinweise)

1. **Kommandocodes, Kommandoblock-Byteliste, Datenphase:** nicht im Handbuch. Quellen: Disassembly der Firmware (`doc/p8000/eproms/WDC/WDC_*`, Z80, `z80dis` im selben
   Ordner), WEGA-Treiber (`wd`-Treiber, WEGA-Systemhandbuch, `sa.format`/`sa.shipdisk`-Dialoge), UDOS-Hardwaretests.
2. **Hostprotokoll:** Handshake-Folge auf PIO2-Port A/B (wer treibt `TE−`/`TR−`; Bedeutung `STATUS0–2`; wann `ARDY`/`ASTB`).
3. **BTT-Format und Lage:** Zylinder 0/Kopf 0/Sektor 1; Struktur; Zusammenspiel mit `sa.format` („Manual Input of Bad Track").
4. **Disk-Seite (kein Flux nötig, P13):** WDC-Z80-Ports (Disk-Steuerung, ICs für MFM-Codec), Sektorenfolge/Interleave, ID-Feld-Struktur
   („Kennfeld"), Gap-Längen, Marken (`FB` Datenmarke bestätigt; ID-Marke vermutlich `FE`), CRC-Polynom, Vorkompensation, Schrittzeit/Gepuffertes Stepping.
5. **WDC-Z80-Speicher-/E/A-Karte:** ROM 0000–1FFF, RAM 6 KB (Adressen?), CTC-/Port-Adressen, Interruptvektoren, Host-/Disk-Port-Zuordnung.
6. **Mehrere Laufwerke:** ab welcher Firmware; Laufwerksanwahl (DRIVE SELECT 1–4 über welche Ports).
7. **Kopfwahl > 2 Köpfe:** HEAD SELECT 2 (D5146, VS) an XR-Pin 2/4 „reserviert"?
8. **Transport-/Parkkommando:** `sa.shipdisk`-Befehl; Parkspur je Laufwerk.
9. **Gerätemessung:** welcher WDC-Index/Firmware am Gerät des Anwenders (Typenschild „V: abcd"); reale Plattenparameter (Zylinder/Köpfe) → Plattenabbild P13.
