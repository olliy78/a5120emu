# P8000 — 16-Bit-Rechner (Index 1 und Index 4) und DRAM-Karten: Hardware-Referenz

Stand 2026-10-05, AP P1. Quelle: **P8000-Hardwarehandbuch**, Kap. 3 §6 (16-Bit-Rechner Index 1,
S. 3-61…3-86), §7 (Index 4, S. 3-87…3-112), §8 (DRAM-Karte Index 1, S. 3-113…3-119), §9 (DRAM-Karte
Index 3, S. 3-120…3-129). Schaltpläne/Bilder: AP2 (`schaltplan_16bit.md`), nicht hier.
Konventionen wie in `hw_8bit.md` (S. = Handbuchseite, Z. = Zeile im pdftotext-Auszug,
**[unklar: …]**, **[Deutung: …]**). §6 und §7 sind wörtlich parallel; Abweichungen stehen in §10.

Indexlage: Index 0/1 = Produktionsstand 1987/88 (Typenschild „V: 1xxx"; Index 0 teilweise noch im Gerät),
Index 2/3 interne Arbeitsstände, **Index 4** = Produktion ab 3/89 (V: 4xxx). **16-Bit-Index ≥ 4 nur mit
8-Bit-Index ≥ 3** (S. 3-87). Index 0 hat zusätzlich Wickelbrücken 1…16 (MMU-Betriebsart), SBR, SNVR,
Wickelstifte 30–37 — alles ab Index 1 entfallen (S. 3-68 ff.).

---

## 1. Überblick (S. 3-61…3-64 / 3-87…3-91)

- 16-Bit-CPU **UB8001** (Z8001-Klon, **segmentiert**), 4 MHz; drei Speicherverwaltungsbausteine **UB8010**
  (Z8010-MMU) + spezielle Steuerlogik; Hauptspeicher auf **DRAM-Karten** (max. 4 gesteckt, 5. Steckplatz
  „zukünftige Erweiterungen"), nichtstandardisierter **Speicherbus**; On-Board 16 KB EPROM + 2 KB SRAM.
- Peripherie: UA855 (PIO ×3), UA856 (SIO ×2), UA857 (CTC ×2), 4- und 8-Bit-Registerbausteine („spezielle
  Logik", per E/A angesprochen).
- Kapazität Hauptspeicher: Index 1 „256 KB bis 1 MB (zukünftig 4 MB)", Index 4 „256 KB bis **4 MB**";
  MMU adressiert **max. 16 MB** (24-Bit-Adresse, Z. 4875, 5889).
- Anschlüsse: Index 1: fünf 25-polige Buchsen (X4–X7 = tty4–tty7, X8 = Winchester-Kopplung extern); Kopplung zum
  8-Bit-Rechner **X2/X3** (zwei 26-polig); X1 Stromversorgung (10-polig); Speicherbus **X9…X13** (96-polig);
  Index 4: vier oder fünf Buchsen, **zusätzlich X16 = interner WDC-Anschluss** (Bestückungsvariante:
  entweder X8 extern **oder** X16 intern).

## 2. Rechnerkern

### 2.1 Takt (Tab. 3.6-1 / 3.7-1, S. 3-65 / 3-92)
Quarz 16 MHz, Clockgenerator DL8127:

| Signal | Pegel | Wert |
|---|---|---|
| INT4PHI− | TTL | 16 MHz |
| INT2PHI | TTL | 8 MHz |
| INTPHI | TTL | 4 MHz (= Bus-`CLOCK`) |
| CLOCK8000 | MOS | 4 MHz für UB8000-Schaltkreise |
| CLOCK80 | MOS | 4 MHz für 8-Bit-Peripherie |

### 2.2 Reset (Tab. 3.6-2 / 3.7-2, S. 3-65/-66 bzw. 3-92/-93)
`MRESET−` entsteht aus:

| Quelle | Bedeutung |
|---|---|
| `PRES−` | Power-on-Reset, DL8127, ≈ 1 s |
| `RESET` | von der **8-Bit-Karte** abgeleitet (X3:B1/„Freigabe 16-Bit"); **liegt statisch an** nach Einschalten bzw. Reset-Taste; **muss durch Koppelsoftware aufgehoben werden** |
| `BUSTESTRESET−` | Prüfzwecke über X9 |
| `SOFTRESET−` | **nur Index 4**: Scheinausgabe an **FFE9H** — „für gezieltes Stillsetzen der CPU" |

Daraus `BUSMRESET−` für den Speicherbus. Wirkung von MRESET− (Handbuch-Aufzählung):
- **SCR Bit 0…3 = 0:** On-Board-Speicher **aktiv**, Hauptspeicher direkt von der CPU adressiert (**MMU aus**), nur
  **nicht-segmentierte Anwenderprogramme**, **Paritätsfehlermeldungen nicht akzeptiert**.
- MMU-Steuerregister gelöscht, aber das **Master-Enable-Flag in den MMUs nicht auf Null**.
- SIO: alle Sender/Empfänger gesperrt, Interrupts gelöscht, Steuerregister neu zu initialisieren.
- CTC: Kanäle gestoppt, Interrupt-Freigaben gelöscht.
- PIO: Port-Masken-Register gelöscht, READY inaktiv, Interrupt-Freigabe-FF zurückgesetzt, **Betriebsart 1**;
  **Interrupt-Vektor-Register nicht gelöscht**.
- Nach Ende von MRESET− startet die UB8001 den Monitor aus dem On-Board-EPROM; Ausschrift auf der
  Systemconsole **„U8000-Softwaremonitor Version x.x - Press NMI"**; per NMI-Taste kann WEGA gestartet oder im
  Monitor weitergearbeitet werden.

**[unklar: wie die UB8001 das Anlaufziel (RESET-Vektor, PSW aus Programmstatus-Area) aus dem EPROM bezieht:
Handbuch zeigt nur „startet das ... Monitorprogramm".]** *Gegenprobe am Abzug:* `MON16_1H_3.0` beginnt
`00 C0 80 01 …` (Z8000-FCW/PC-Paar) — Sache von AP3.

### 2.3 NMI (Tab. 3.6-3/-4 bzw. 3.7-3/-4)
Drei Quellen, per Lesen des **NMI-Identifiers** zu unterscheiden:

| Signal | Bedeutung |
|---|---|
| `MANUAL NMI−` | NMI-Taste, **nur wenn die 8-Bit-Karte die Durchschaltung freigegeben hat** (`/NMI_U8000`, `hw_8bit.md` §2.3) |
| `BUSPE−` | Paritätsfehler einer DRAM-Karte; rücksetzen durch RESET oder `CLR PARITY−` |
| `POWER FAIL−` | Stromversorgung — **derzeit nicht erzeugt** („Fußnote (1)") |

NMI-Identifier: wird **während des NMI-Quittierzyklus** gelesen und im System-Stack abgelegt; erfasst werden
die unteren 4 Bit des **Low-Daten-Bytes**, **AD3 immer 0**:

| Quelle | AD3 AD2 AD1 AD0 |
|---|---|
| Manual | 0 0 0 1 |
| Power fail | 0 0 1 0 |
| Paritätsfehler | 0 1 0 0 |

### 2.4 Interrupt-Generierung (S. 3-67 / 3-94)
Neben dem NMI kennt die UB8001 `VI−` und `NVI−`. **Alle Interruptanforderungen der Interface-Bausteine gehen auf
`VI−`, `NVI−` wird auf der Karte nicht erzeugt.** Prioritätskette, nicht veränderbar:
```
 CTC0 — CTC1 — SIO0 — SIO1 — PIO0 — PIO1 — PIO2 — * —— X13 — X12 — X11 — X10 — X9 (BUS)
```
Die Kette läuft über die Busstecker (`B_IEI` = a24, `B_IEO` = c24; Bild 3.6-4, Z. 4478–4484): Karteneingang
`BUS IEO` → X13a24 (B_IEI) … c24 (B_IEO) → X12 a24 … → X9 a24 (Ende). **X9-c24 gehört „nicht mehr zur
Kette"** (dort liegt bei Index 1 `TESTDATA−`, Fußnote (2)). Quittier-/RETI-Zyklen (§4.1).

### 2.5 Trap-Generierung (S. 3-68 / 3-95)
`SEGT−` (Segmentation Violation) wird von den **UB8010-MMUs** erzeugt und der CPU zugeführt (Trap).
Index 0 konnte `SEGT−` zusätzlich per externer Logik erzeugen (Betriebssystem unterstützt das nicht).
Auswertung: Register **TRPL**, **IF1L** (§4.3, §6.3); Details Speicherverwaltung §6.

### 2.6 Systemkonfiguration — SCR (Tab. 3.6-5 / 3.7-5, S. 3-69 / 3-95)

8-Bit-Register, **E/A-Adresse FFC1H**, nur die **unteren vier Bit** schreibbar/lesbar:

| Bit | Art | Signal | Funktion |
|---|---|---|---|
| 0 | r/w | `BD MEM ON−` | **0** = On-Board-Speicher ein, 1 = aus |
| 1 | r/w | `MMU ONH` | **0** = MMU aus, 1 = MMU ein |
| 2 | r/w | `SEG USR` | 0 = nichtsegmentiertes User-Programm, 1 = segmentiertes User-Programm |
| 3 | r/w | `CLR PARITY−` | 0 = Paritätsfehler löschen bzw. **nicht akzeptieren**; 1 = Paritätsfehler wird akzeptiert (erzeugt `NMI−`) |
| 4–7 | r | Wickelstifte 30/31, 32/33, 34/35, 36/37 | Index 0: Abfrage Wickelbrücken (offen = 1, geschlossen = 0); **Index 1/4: fest High** |

Wichtig (Hervorhebung des Handbuchs): **„Das Signal MMU ON wirkt nicht auf die MMU-Schaltkreise, sondern
schaltet die Adressbustreiber des Speicherbus auf die MMU-Adressen um (bei MMU ON = 1)!"** — d. h. MMU ONH = 0
legt die **lokale** (untranslated) Adresse auf den Speicherbus; die MMUs laufen intern weiter.
Reset-Zustand: Bits 0–3 = 0 → On-Board an, MMU aus, nicht-seg., Parität aus.
**[unklar: SCR lesend — liefert Bit 4–7 = 1111, Bit 0–3 = geschriebener Zustand? („r/w", 4 Bit)]** — nur
Index 0: Wickelstifte; sonst „fest High".

## 3. Speicheradressierung (S. 3-70…3-75 / 3-96…3-101)

### 3.1 Adressen
- Logische Adresse der UB8001: **Segmentnummer (7 Bit) + Offset (16 Bit)**, zwischengespeichert als **lokaler
  Adressbus**; dieser adressiert den **On-Board-Speicher** (wenn `BD MEM ON−` = 0). Er kann zu Testzwecken
  auch den Hauptspeicher adressieren.
- Hauptspeicher normal über den **„translated" Adressbus** (24 Bit, von den UB8010 aus der logischen Adresse
  berechnet), aktiv mit `MMU ONH` = 1.
- **Wait-Zyklen:** Zugriffe auf den On-Board-Speicher lösen automatisch einen **T2-Wait** aus; Hauptspeicher
  auch mit MMU **ohne Wait-Zyklen**.
- Byte-/Wortzugriffe: Speicher = ≥ 2 Bänke (Low-Datenbus D0–D7, High-Datenbus D8–D15). Wortzugriff: beide Bänke
  parallel, **A0 = 0**. Bytezugriff: **A0 = 1 → Low-Byte, A0 = 0 → High-Byte**. *Byte-Lesen:* A0 egal, beide
  Bytes auf den Bus, CPU wählt selbst. *Byte-Schreiben:* CPU legt das Byte auf **beide** Busteile, Speicher
  wählt per A0 die Bank.

### 3.2 On-Board-Speicher (18 KB)
Liegt in **Segment 0**:

| Offset | Inhalt |
|---|---|
| 0000H–3FFEH | EPROM (16 KB; 4 × 2732 = 2 Bänke Low/High) |
| 4000H–47FFH | SRAM (2 KB) |
| 4800H–5FFFH | **SRAM-Spiegel** (unvollständige Dekodierung: „im Bereich 4000 bis 6000 viermal in Abständen von 2 KByte") |
| 6000H–7FFFH | **leer** |
| ab 8000H | Hauptspeicher (physisch <00>8000H, wenn On-Board aktiv) |

- EPROM-Anordnung (Bild 3.6-3 / 3.7-3, Z. 4345–4376): vier Fassungen **1H = High Even, 2H = Low Even,
  1L = High Odd, 2L = Low Odd** (Beschriftung 1H/2H/1L/2L; **[unklar: Zuordnung der Position „High/Low" im Bild
  vs. „High-Datenbus (Even) / Low-Datenbus (Odd)" im Text]**: Text sagt „Low-Datenbus (Odd-Adresse) und High-
  Datenbus (Even-Adresse)", Bild nennt „1H (High Even)" usw.).
- Abzüge: `MON16_1L/1H/2L/2H_x.y` (je 4 KB) — 1/2 = obere/untere 4K-Hälfte, L/H = Low-/High-Byte (siehe AP3).
- On-Board-Speicher nach Reset aktiv; abschaltbar mit **SCR Bit 0**.
- **[unklar: Verhalten/Busabschluss beim Zugriff auf nicht belegte Offsets 6000–7FFF und was ein Hauptspeicher-
  zugriff in 0000–7FFF bei aktivem On-Board liefert (Zugriff auf Hauptspeicher physisch 0–7FFF ist dann unsichtbar).]**

### 3.3 Hauptspeicher / DRAM-Karten
Siehe §8/§9. Moduladresse per Wickelbrücken; Karten decodieren **A18…A23** (256-KB-Karte) bzw. **A20…A23**
(1-MB-Karte); `MEMSEL` gibt die Speichersteuerung frei.

### 3.4 Speicherbus X9…X13 (Tab. 3.6-7/-8 bzw. 3.7-7/-8, S. 3-73/-74 bzw. 3-99/-100)
Fünf 96-polige Stecker, Reihen a/b/c, Pin 1–32. Gemeinsame Signale (alle Reihen identisch, Test- und
Sondersignale **nur am Stecker X9**):

| Pin | Reihe a | Reihe b | Reihe c |
|---|---|---|---|
| 1 | BUS ST0 | +5 V | BUS ST1 |
| 2 | BUS ST2 | +5 V | BUS ST3 |
| 3 | BUS R/W− | +5 V | BUS B/W− |
| 4 | BUS DS− | +5 V | BUS CLOCK |
| 5 | BUS BUSACK− | +5 V | BUS STOP− |
| 6 | BUS VI− | BUS N/S− (T) | BUS NVI− |
| 7 | BUS REQ− | BUS NMI− (T) | BUS MRESET− |
| 8 | BUS WAIT− (Idx 1: nicht nutzbar; Idx 4: mit TEST WAIT− verbunden) | RUN/HALT− (T) | BUS I/O− |
| 9 | BUS RFSN− | SSNO (T) | BUS MREQ− |
| 10 | BUS AS− | SSNC (T) | BUS M1− |
| 11 | BUS IORQ− | TEST RESET− (T) | BUS RD− |
| 12 | BUS A23 | Idx 1: leer / **Idx 4: TESTWAIT− (T)** | BUS A22 |
| 13 | BUS A21 | TESTDATA− (T; Idx 1 auf X9-c24, Fußnote 2) | BUS A20 |
| 14 | BUS A19 | Idx 1: leer / **Idx 4: TAPEQUIT−** | BUS A18 |
| 15 | BUS A17 | BAUD CLOCK | BUS A16 |
| 16 | BUS A15 | — | BUS A14 |
| 17 | BUS A13 | — | BUS A12 |
| 18 | BUS A11 | Idx 1: leer / **Idx 4: BUSNVIACK− (T)** | BUS A10 |
| 19 | BUS A9 | Idx 1: leer / **Idx 4: BUS2CLOCK (T)** | BUS A8 |
| 20 | BUS A7 | — | BUS A6 |
| 21 | BUS A5 | — | BUS A4 |
| 22 | BUS A3 | Idx 1: leer / **Idx 4: MSDOSNMI− (T)** | BUS A2 |
| 23 | BUS A1 | — | BUS A0 |
| 24 | B_IEI | CLR PARITY− | B_IEO |
| 25–31 | BUS AD15, AD13, AD11, AD9, AD7, AD5, AD3 | MASSE | BUS AD14, AD12, AD10, AD8, AD6, AD4, AD2 |
| 32 | BUS AD1 | BUS PE− | BUS AD0 |

(T) = Testzweck, nur an X9. Die Tabelle stammt aus zwei getrennten Textstellen (Z. 4396–4432, 5409–5444);
die Index-Unterschiede (**Index 4 neu:** TESTWAIT−, TAPEQUIT−, BUSNVIACK−, BUS2CLOCK, MSDOSNMI−) sind aus dem
direkten Vergleich entnommen. Fußnoten: Idx 1 (2) `TESTDATA−` auf X9-c24; (3) `BUSWAIT−` bei Index 0/1 nicht nutzbar; Idx 4 (2)
`BUS WAIT−` und `TEST WAIT−`-Stifte miteinander verbunden.

Signalbeschreibung (Tab. 3.6-8 / 3.7-8), „A" = Ausgang der Karte, „E" = Eingang, „E (OC)" = Open-Collector-Quelle,
„bi" bidirektional:

| Signal | Funktion | E/A |
|---|---|---|
| A0…A23 | Adressen | A |
| AD0…AD15 | Daten | bi |
| ST0…ST3 | Status (CPU-Status 4 Bit) | A |
| R/W− | Read/Write | A |
| B/W− | Byte/Word | A |
| DS− | Data Strobe | A |
| CLOCK | INTPHI (4 MHz) | A |
| BUSACK− | Bus Acknowledge | A |
| STOP− | Stop | E |
| VI− | Vectored Interrupt | E (OC) |
| NVI | Non-Vectored Interrupt | E |
| REQ− | Bus Request | E |
| MRESET− | Master Reset | A |
| I/O− | Status 2 (I/O-Referenz) | A |
| RFSN− | Status 1 (Speicher-Refresh) | A |
| MREQ− | Memory Request | A |
| AS− | Address Strobe | A |
| M1− | U880-M1-Zyklus | A |
| IOREQ− | U880 I/O Request | A |
| RD− | U880 Read | A |
| CLR PARITY− | Clear Parity Error | A |
| PE− | Parity Error | E (OC) |

**[unklar: Bedeutung von N/S−, SSNO, SSNC, RUN/HALT−, TAPEQUIT−, BUSNVIACK−, BUS2CLOCK, MSDOSNMI−, TEST…
— im Text nur als Test-/Zukunftssignale benannt.]** Hinweis: Auf den DRAM-Steckern (§8.3) erscheinen
nur 9 Steuersignale (R/W−, DS−, RFSH−, AS−, CL_PAR−, PE−, B/W−, RES−, MEMRQ−).

## 4. Peripherie

### 4.1 Übersicht und Systemsignale (S. 3-75 / 3-101)
- UA880-Familie (PIO UA855, SIO UA856, CTC UA857) hinter spezieller Steuerlogik, die M1−, RD−, IORQ−
  **nach Z80-Art erzeugt**:

| Zugriff | IORQ− | RD− | M1− |
|---|---|---|---|
| Lesen | 0 | 0 | 1 |
| Schreiben | 0 | 1 | 1 |
| Interrupt-Quittierzyklus | 0 | 1 | 0 |
| **RETI** | 1 | 0 | 0 |

- Datenverkehr ausschließlich über den **Low-Datenbus** der UB8001 (→ alle Peripherieadressen **ungerade**).
- **Return-from-Interrupt:** der RETI-Zyklus wird durch die Ausgabe der **zwei unmittelbar
  aufeinanderfolgenden Datenbytes `ED`, `4D` (hex) an die RETI-Port-Adresse (FFE1H)** erzeugt — Emulator: Schreiben ED,4D
  auf FFE1H → RETI-Dekodierung der Daisy-Chain (IEO-Freigabe).
- Nutzung dieser Signale für Erweiterungskarten „nur bedingt möglich".

### 4.2 E/A-Adressen (Tab. 3.6-10/-11, S. 3-76/-77 bzw. 3-102/-103, Z. 4530–4586, 5542–5598)

Peripherie (identisch Index 1 und 4):

| Adresse | Baustein/Register | Kanal / Funktion |
|---|---|---|
| FF81 | SIO0 Kanal A Daten | serieller Kanal 0 (**tty4**) |
| FF83 | SIO0 Kanal B Daten | serieller Kanal 1 (**tty5**) |
| FF85 | SIO0 Kanal A Control | |
| FF87 | SIO0 Kanal B Control | |
| FF89 | SIO1 Kanal A Daten | serieller Kanal 2 (**tty6**) |
| FF8B | SIO1 Kanal B Daten | serieller Kanal 3 (**tty7**) |
| FF8D | SIO1 Kanal A Control | |
| FF8F | SIO1 Kanal B Control | |
| FF91 / FF93 | PIO0 Port A / B Daten | Kopplung 8-Bit-Rechner |
| FF95 / FF97 | PIO0 Port A / B Control | |
| FF99 / FF9B | PIO1 Port A / B Daten | Kopplung 8-Bit-Rechner |
| FF9D / FF9F | PIO1 Port A / B Control | |
| FFA1 / FFA3 | PIO2 Port A / B Daten | Kopplung Winchester-Controller |
| FFA5 / FFA7 | PIO2 Port A / B Control | |
| FFA9 | CTC0 Kanal 0 | BAUD0: SIO0-A (tty4) |
| FFAB | CTC0 Kanal 1 | BAUD1: SIO0-B (tty5) |
| FFAD | CTC0 Kanal 2 | BAUD2: SIO1-A (tty6) |
| FFAF | CTC0 Kanal 3 | **Single-Step-Steuerung** |
| FFB1 | CTC1 Kanal 0 | BAUD3: SIO1-B (tty7) |
| FFB3 | CTC1 Kanal 1 | „–" (nicht belegt) |
| FFB5 | CTC1 Kanal 2 | **Systemuhr** |
| FFB7 | CTC1 Kanal 3 | **Systemuhr** |

Aus der Tabelle ableitbar: SIO: A1 = Kanal (0=A, 1=B), A2 = Control/Daten (1 = Control); PIO: A1 = Port, A2 = Control;
CTC: A1 A2 = Kanal. Bausteinblöcke 8 Adressen.

Spezielle Logik (Tab. 3.6-11 / 3.7-11):

| Adresse | Index 1 | Index 4 | Art |
|---|---|---|---|
| FFB9 | — | **LEDAUS** (Anzeige „RUN" ausschalten) | w |
| FFC1 | **SCR** System-Configuration-Register (4 Bit) | SCR | r/w |
| FFC9 | **SBR** System-Break-Register — **nur Index 0** | — (frei) | r/w |
| FFD1 | **NBR** Normal-Break-Register | NBR | r/w (8 Bit) |
| FFD9 | **SNVR** Segment-Violation-Register — **nur Index 0** | **LEDEIN** (Anzeige „RUN" einschalten) | r bzw. w |
| FFE1 | **RETI** (Return from Interrupt) | RETI | w, keine Speicherung |
| FFE9 | — | **SOFTRESET** (→ `SOFTRESET−`) | w |
| FFF1 | **TRPL** Trap-Low-Byte-Register | TRPL | r (8 Bit) |
| FFF9 | **IF1L** Instruction-Fetch-First-Word-Register | IF1L | r (8 Bit) |

- „w": nur Schreib-Port, keine Speicherung; „r": nur Lese-Register.
- **[unklar: die UB8010-MMU-Steuerregister sind nicht in diesen Tabellen.]** Die Z8010 wird von der UB8001 per
  **Special I/O** (SIN/SOUT-Befehle, Status 0101-Zyklen) angesprochen — **Adressdekodierung der drei MMUs
  (MMU1/2/3), Ansteuerung `/SE`-Select bzw. Special-I/O-Adressbits — nicht im Handbuch** (→ Schaltplan, Z8010/UB8010-Datenblatt).
  Analog: wie `NBR` mit der Special-I/O-Ebene zusammenhängt.

### 4.3 Serielle Kanäle (S. 3-77…3-80 / 3-103…3-106)

| Kanal | SIO / Kanal | Buchse | Funktion | CTC / Kanal |
|---|---|---|---|---|
| tty4 | SIO0 / A | X4 | V.24 (für DNÜ K8172) | CTC0 / 0 (Takt per Brücken umschaltbar) |
| tty5 | SIO0 / B | X5 | V.24 abgerüstet | CTC0 / 1 |
| tty6 | SIO1 / A | X6 | V.24 abgerüstet / **IFSS** | CTC0 / 2 |
| tty7 | SIO1 / B | X7 | V.24 abgerüstet / **IFSS** | **CTC1 / 0** |

- IFSS nur für **tty6/tty7** (Index 1: aktiver Sender/passiver Empfänger, Q+ an Pin 12; **Index 4: passiv
  und potentialgetrennt, auch aktiv betreibbar, Pin 12 = eigene Stromquelle, Pin 16 = Nachbarkanal-Quelle**).
- **Index 1:** Schnittstelle als **DÜE** (Tab. 3.6-13: Pin 2 = 103 RD (E), 3 = 104 TD (A), 5 RTS (A), 8 DTR (A),
  20 DCD (E), 15 TC, 17 RC, 24 TC; CTS/DSR (4/6) nur nach Schaltungsänderung); tty5–7: Pins 2 RD, 3 TD, 7 SG, 8 DTR (A), 20 DCD (E).
- **Index 4:** **DEE** (Tab. 3.7-13: Pin 2 = 103 TD (A), 3 = 104 RD (E), 4 RTS (A), 5 CTS (E), 6 DSR (E), 8 DCD (E),
  20 = 108.2 DTR (A), 15/17/24 Takt); tty5–7: Pins 2 TD (A), 3 RD (E), 7 SG, 8 DCD (E), 20 DTR (A).
- **IFSS-Pins:** Index 1 (Tab. 3.6-16): 10 SD+, 13 ED+, 14 ED−, 19 SD−, 12 Q+; **Index 4 (Tab. 3.7-16):
  10 SD−, 13 ED−, 14 ED+, 19 SD+, 12 Q+, 16 Q+**. Aktivierung: Pin 9 (IFSS) mit Pin 7 (SG) verbinden.
- **tty4 Takt** (Index 1 Tab. 3.6-14, Wickelfeld 17…26; Index 4 Tab. 3.7-14, Wickelfeld 1…10):

| Taktbereitstellung | Index 1 | Index 4 |
|---|---|---|
| Sendetakt intern | 21–26 | 5–10 |
| Empfangstakt intern | 18–22 | 2–6 |
| Sendetakt extern über 114 (TC) | 20–25 | 4–9 |
| Empfangstakt extern über 115 (RC) | 19–24 | 3–8 |
| Sendetakt zur DÜE über 113 (TC) | 17–23 | 1–6 |

  (tty5–7: nur interner Takt.)
- Kabel: Kap. 2 §5 (Index 1: V: 11xx) und §6 (ab 3/89, V: 43xx): Schema siehe `hw_terminal.md` §8.

### 4.4 Baudratengenerator (S. 3-80 / 3-106)
Quarz-Taktgenerator 9,832 MHz → 8-fach-Vorteiler → 1,229 MHz → vier CTC-Kanäle (CTC0 K0–K2, CTC1 K0) → 2-fach-Teiler
→ SIO TxC/RxC. Tabelle Zeitkonstante × SIO-Vorteiler **identisch** zu `hw_8bit.md` §5.2 (19200: 1/×32; 9600: 2/×32 oder 1/×64; …;
75: 256/×32 oder 128/×64). Bereich 75–19200 Bit/s. `BAUD CLOCK` auch am Speicherbus (X9–15b).

### 4.5 Parallele Schnittstellen (S. 3-81 / 3-107)
PIO0–PIO2 (UA855). **PIO0 + PIO1 → Kopplung zum 8-Bit-Rechner** (FF91…FF9F), **PIO2 → Winchester-Controller** (FFA1…FFA7).

**Winchester-Anschluss (Tab. 3.6-19 Index 1 X8; Tab. 3.7-19 Index 4 X8/X16)** — E/A aus Sicht der 16-Bit-Karte:

| Signal | E/A | Index 1 X8 | Index 4 X8 | Index 4 X16 | Funktion (Handbuch) |
|---|---|---|---|---|---|
| D0 | E/A | 14 | 14 | A7 | Datenbit 0 |
| D1 | E/A | 2 | 2 | B9 | Datenbit 1 |
| D2 | E/A | 5 | 5 | A3 | Datenbit 2 |
| D3 | E/A | 17 | 17 | A5 | Datenbit 3 |
| D4 | E/A | 4 | 4 | A2 | Datenbit 4 |
| D5 | E/A | 16 | 16 | B4 | Datenbit 5 |
| D6 | E/A | 3 | 3 | B7 | Datenbit 6 |
| D7 | E/A | 15 | 15 | B8 | Datenbit 7 |
| `WDARDY−` | A | 1 | 1 | A1 | Control |
| `ASTB−` | E | 18 | 18 | B2 | Control |
| `STATUS0` | E | 21 | 21 | A9 | Control |
| `STATUS1` | E | 10 | 10 | B3 | Control |
| `STATUS2` | E | 22 | 22 | A6 | Control |
| `TE−` | A | 25 | 25 | B6 | Control |
| `TR−` | E | 9 | 9 | A11 | Control |
| `RST−` | A | 24 | 24 | — | Reset (**low-aktiv**) |
| `RST` | — | **frei** | 13 | B5 | Reset **(high-aktiv, für WDC Index ≥ 3)** |
| GND / 5P | — | 7 / 20 | 7 / 20 | A4, A8 / B13 | Masse / +5 V |
| nicht benutzt | | 12 (`WDARDY`), 23, 11, 19 (`BRDY`), 6 (`BSTB−`), 8, 13 frei | wie links, 13 = RST | — | |

Beachte (Index 1 vs 4): 16-Bit-Karte ab Index 4 hat **beide** Reset-Ausgänge (`RST−` low, `RST` high) und
„kann mit WDC-Karten eines beliebigen Index gekoppelt werden"; **WDC ab Index 3 nur mit 16-Bit ab Index 4**
(`hw_wdc.md`). Welche der Signale Port A (Daten + ASTB/ARDY) bzw. Port B (STATUS0–2, TE, TR) von PIO2 sind
und die Bedeutung von TE−/TR−: **[unklar, nicht im Handbuch]**; die WDC-Seite kennt nur „drei Steuerbits
und drei Statusbits" (`hw_wdc.md` §3).

**Kopplung zum 8-Bit-Rechner (Tab. 3.6-20 / 3.7-20 — X2/X3, nicht von außen zugänglich)**:

| Signal | Pin | E/A (16-Bit-Seite) | Funktion (Index 1 / Index 4) |
|---|---|---|---|
| D0/8-16 … D7/8-16 | X3: B4, A6, A7, B13, B11, B10, B9, B8 | E | Datenbits |
| D0/16-8 … D7/16-8 | X2: B2, B9, B8, B7, B1, A10, A8, A1 | A | Datenbits |
| V1…V6/8-16 | X3: A12, A11, A10, A9, A8, B7 | E | Vektor 1–6 |
| V1…V4/16-8 | X2: A5, A4, A3, A2 | A | Vektor 1–4 |
| `ASTB1−` / `RDY/8-16` | X3:A5 | E | „Eingabedaten vorhanden" |
| `ARDY1` / `E0-8` | X3:A3 | A | „Eingabedaten übernommen" |
| `ARDY0` / `DD-8` | X2:B5 | A | „Ausgabedaten vorhanden" |
| `ASTB0−` / `RDY/16-8` | X2:B3 | E | „Ausgabedaten übernommen" |
| `E0-16` (nur Index 4) | X2:A9 | E | „Ausgabedaten vorhanden" **[unklar: s. hw_8bit §10 W2]** |
| `DD-16` (nur Index 4) | X2:A11 | E | „Eingabedaten übernehmen" |
| `INT8` / `INT-8` | X2:A6 | A | Interrupt zum 8-Bit-Rechner |
| `INT16` / `INT-16` | X3:A13 | E | Interrupt vom 8-Bit-Rechner |
| `RESET` | X3:B1 | E | Freigabe 16-Bit-Rechner (statisch, §2.2) |
| `NMIU8000−` | X3:B5 | E | NMI auf 16-Bit-Rechner |
| `BRDY0`, `BSTB0−` (P0 Port B) | X2:A7, X2:B4 | A, E | nicht benutzt („–") |
| `BRDY1`, `BSTB1−` (P1 Port B) | X3:B3, X3:A4 | A, E | nicht benutzt |
| Masse | Idx 1: X2:A13, B13; X3:B12 — Idx 4: X2:B11, B12, B13 (X3:B12 in Tab. 3.7-20 nicht aufgeführt) | | |
| `RUN-LED` | X2:A2, A13 (Idx 4) | A | RUN-LED ein/aus (**Doppelbelegung A2 vs V4/16-8, s. hw_8bit W6**) |
| `5P` | X3:B2 (Idx 4) | | +5 V |

Zugehörigkeit zu den PIOs (Namen nach Z80-PIO-Port-A-Handshake): **ASTB1/ARDY1 = PIO1 Port A** (Eingabe 8→16),
**ASTB0/ARDY0 = PIO0 Port A** (Ausgabe 16→8); die jeweils zweiten Ports tragen nicht benutzte BRDY/BSTB →
vermutlich übernehmen **Port B beider PIOs die Vektorbits** (V1–V4/16-8 an PIO0-B, V1–V6/8-16 an PIO1-B)
**[Deutung, nicht belegt]**. Mit Index 4 kommen `E0-16`/`DD-16` zusätzlich als Eingänge.

**Kreuzprüfung Kopplung** gegen `hw_8bit.md` §6: Pin-für-Pin-Identität bestätigt (X1(8-Bit)↔X3(16-Bit), X2↔X2).
Gegen die Textangaben: Anzahl „32 Daten- und 8 Handshake-Signale" stimmt nicht (s. hw_8bit §10 W4).

## 5. MMU-Steuerung (Speicherverwaltung) (S. 3-84…3-86 / 3-110…3-112)

### 5.1 Hauptfunktionen
Drei UB8010 verwalten dynamisch bis **16 MB**: Zuweisung an Betriebssystem/Tasks, gemeinsame Speicherbereiche,
Schutz, Erkennung offensichtlich falscher Speichernutzung, Trennung System/User. Gesteuert „von der CPU über eine
spezielle Logik, die die verschiedenen Softwarebetriebsarten berücksichtigt".
- **Betriebssystem:** Index 1: ursprünglich segmentiert **oder** nichtsegmentiert (Wickelbrücken 1…16, nur Index 0);
  jetzt fest **segmentiert**. Index 4: „Betriebssystemkern: CPU im **System-Mode**, arbeitet grundsätzlich segmentiert".
- **Anwenderprogramme:** segmentiert oder nichtsegmentiert; **vor dem Start segmentierter Anwenderprozesse muss das
  `SEG USR`-Bit im SCR gesetzt werden.**

### 5.2 MMU-Zuordnung nach Programmart (wörtlich-nah)
- MMU direkt nur für segmentierte Programme möglich. Für nichtsegmentierte: **zusätzliche Hardware** =
  8-Bit-Register (**Breakregister**), Komparatoren, Logik. Die drei MMUs sind dann:
  **MMU1 = Code-MMU**, **MMU2 = Data-MMU**, **MMU3 = Stack-MMU** eines nichtsegmentierten Programms.
- Befehlslesezyklus (Auswertung **CPU-Status 11xx**) → Code-MMU aktiv; Daten-/Stack-Zugriff → Data-/Stack-MMU.
- **Data/Stack-Umschaltung:** Vergleich des im **Breakregister** (Index 4: **NBR**, E/A FFD1H) abgelegten 8-Bit-Worts mit dem **High-Teil der
  laufenden Adresse**: *Adresse < Breakadresse → Data-MMU, Adresse > Breakadresse → Stack-MMU.* **[unklar: Gleichheit;
  welche Adressbits = „High-Teil" (vermutlich Offset A15–A8); Segmentnummer des nichtsegmentierten Prozesses = 63 laut
  Zustand (2) unten.]**
- Index 0 hatte zwei Breakregister (**SBR** System, **NBR** Normal) für nichtsegmentierte **System**- und **Anwender**programme;
  ab Index 1 nur NBR.

Die Steuerlogik unterscheidet **drei Zustände**:

| # | Zustand | SEG USR | MMUs |
|---|---|---|---|
| 1 | **Betriebssystem segmentiert** (CPU im **System-Mode**) | — | Code, Data, Stack alle von **MMU1**; MMU2, MMU3 und Breakregister **nicht aktiviert** |
| 2 | **Anwenderprozess nichtsegmentiert** (CPU im **Normal-Mode**, **Segmentnummer 63**) | **0** | Code-/Data-/Stack-MMU (MMU1/2/3); NBR aktiv, steuert Data/Stack |
| 3 | **Anwenderprogramm segmentiert** (Normal-Mode) | **1** | **MMU2 und MMU3** adressieren 128 mögliche Segmente (je Code/Data/Stack): **MMU2 = Segmente 0…63, MMU3 = Segmente 64…127**, Umschaltung in Hardware durch **Segmentleitung 6** (`SN6 = 0 → MMU2`, `SN6 = 1 → MMU3`); **beide MMUs werden für den Bereich 0…63 programmiert, ihr URS-Flag ist Null** |

**Was das Handbuch NICHT sagt** (und der Emulator braucht): wie die MMU „Segmentnummer 63 / Normal-Mode" erkennt
(**[Deutung]** vermutlich das Z8000-Statussignal N/S; am Speicherbus liegt `N/S−` als Testsignal auf X9-b6, im Text nicht erklärt); wie `SEG USR` die Selektion von MMU1 vs
MMU2/3 und die Segmentnummernbildung (SN6 abtrennen, URS) steuert; wie die Segmentnummer 7 Bit auf die MMU-Eingänge
läuft (UB8010 erwartet 6 Bit Segmentnummer + 1 Bit Auswahl). **→ Offene Fragen (§9).**

### 5.3 Segmenttrap und Suppress (S. 3-86 / 3-111)
- Die MMUs generieren **zwei Signale** bei Verletzung der Zugriffsbedingungen:
  **`SEGT−`** → CPU löst Trap aus; **`SUP−`** (Suppress) verhindert bei Segmentverletzung das **Beschreiben** des
  Arbeitsspeichers durch Unterdrücken von **`BUSDS−`**.
- Externe Logik für `SEGT−`/`SUP−` entfällt ab Index 1 (war für nichtsegmentiertes BS).
- Auswertung eines Segmenttraps durch die CPU: zwei externe 8-Bit-Register:
  - **TRPL (FFF1H, r):** niederwertige 8 Adressbits des **laufenden Speicherzyklus**;
  - **IF1L (FFF9H, r):** niederwertige 8 Adressbits des **ersten Befehlswortes** (Instruction Fetch First Word).
  Dazu liefert die MMU ihre eigenen Fehlerregister (Z8010: Violation Type, Violation Segment Number, Violation Offset High, Instruction Fetch Segment/Offset)
  — **[unklar: nicht im Handbuch; Datenblatt UB8010 = Z8010 nötig]**.
- Ein weiteres Register **SNVR** (FFD9H, Index 0) speicherte die Segmentnummer für das nichtsegmentierte BS;
  **entfällt ab Index 1**.

## 6. Hauptspeicher-Karten (DRAM) — §8 Index 1, §9 Index 3

### 6.1 Variantenübersicht

| | DRAM Index 0/1 (§8) | DRAM Index 3 (§9) |
|---|---|---|
| Format | 140 × 150 mm, 4 Lagen, 96-pol. Stecker (Bus), Karte trägt Messerleiste | gleich |
| Kapazität | **256 KB** (36 × 64-Kbit-ICs) | Variante 1: **1 MB** (36 × 256-Kbit-ICs); Variante 2: **256 KB** (36 × 64-Kbit-ICs) |
| Bänke | 4 Bänke × 64 KB, je 9 ICs (8 + **Paritätsbit**/Byte) | 1 MB: 4 × 256 KB; 256 KB: 4 × 64 KB |
| Bedienung | Byte: 1 Bank; Wort: 2 Bänke parallel | gleich |
| Refresh | CPU-gesteuert, **RAS-Only**; kein Eigenrefresh, keine Überwachung | gleich |
| Wait-Zyklen | keine (volle CPU-Geschwindigkeit) | keine |
| Moduladresse | 0…63 (A18–A23, 256-KB-Schritte) | 1 MB: **0…15** (A20–A23, 1-MB-Schritte); 256 KB: 0…63 (A18–A23) |
| Einsatz | alle 16-Bit-Kartenversionen | alle; löst Index 1 ab; **Interface gegenüber Index 0/1 unverändert** |

### 6.2 Struktur und Adressverwendung
Funktionsgruppen: Speichermatrix, Interface, Moduladressdekoder, Speicheradressmultiplexer, Datenbus, Speichersteuerung,
Fehlerüberwachung/-meldung. Schnittstelle: 24 Adress-, 16 Daten-, 9 Steuersignale + Versorgung.
- `A0`, `A17`: Speichersteuerung; **A17 wählt eines der beiden Bankpaare** (wort- oder byteweise); bei Bytezugriff
  unterscheidet A0 Low (A0 = 1) / High (A0 = 0).
- 256-KB-Karte: `A1…A16` → Adressmultiplexer (**64 K × 2 Gruppen à 8 Adressen**, Signal `MUX−`); `A18…A23` → Moduladressdekoder
  (`MEMSEL`).
- 1-MB-Karte: `A1…A16, A18, A19` → Multiplexer (18 Bit, 2 × 9 Adressen); `A20…A23` → Moduladressdekoder.
- Daten: bidirektionaler 16-Bit-Datenbus → intern unidirektional (Read-Modify-Write-Zyklus beim Schreiben).
- **Parität:** je Byte ein Paritätsbit beim Schreiben erzeugt, beim Lesen verglichen; Fehler → **`PE−`** (Open-Collector), rote LED
  auf der Karte; Quittierung nur von außen (**RESET− oder CLEAR-PARITY−**); die Karte arbeitet ohne Quittierung weiter.
  **Parität gerade/ungerade und genauer `PE−`-Zeitpunkt: [unklar]**.

### 6.3 Interface (Tab. 3.8-1 / 3.9-1, S. 3-116/-117 bzw. 3-125/-126) — identisch für beide Indexstände

| Pin | Signal | Funktion | LF |
|---|---|---|---|
| a3 | R/W− | Read/Write | 2 |
| a4 | DS− | Data Strobe | 2 |
| a9 | RFSH− | Refresh | 2 |
| a10 | AS− | Address Strobe | 1 |
| b24 | CL_PAR− | Clear Parity | 1 |
| b32 | PE− | Parity Error | OC-Ausgang |
| c3 | B/W− | Byte/Word | 2 |
| c7 | RES− | Reset | 1 |
| c9 | MEMRQ− | Memory Request | 1 |
| a12/a13/a14/a15/a16/a17/a18/a19/a20/a21/a22/a23 | A23, A21, A19, A17, A15, A13, A11, A9, A7, A5, A3, A1 | Adressen (a-Reihe) | 2,2,2,3,5,5,5,5,5,5,5,5 |
| c12 … c23 | A22, A20, A18, A16, A14, A12, A10, A8, A6, A4, A2, A0 | Adressen (c-Reihe) | 2,2,2,5,5,5,5,5,5,5,5,2 |
| a25 … a32 | AD15, AD13, AD11, AD9, AD7, AD5, AD3, AD1 | Daten | bidir. (In: 6, Out: 22 mA) |
| c25 … c32 | AD14, AD12, AD10, AD8, AD6, AD4, AD2, AD0 | Daten | bidir. |
| b1–b5 | 5P | +5 V | |
| b25–b31 | GND | Masse | |

(Lastfaktor LF = 1 ≙ 0,36 mA Low. Stromaufnahme max. 1600 mA, typ. 1400 mA; 64-Kbit: 55 mA Betrieb/40 mA Refresh/5 mA
Ruhe je IC; 256-Kbit: 40–50 / 35–40 / 5 mA.)

### 6.4 Moduladresse (§8.4 / §9.4)
- 256-KB-Karte (Idx 1 und Idx 3 Var. 2): Moduladresse **n** = `A23…A18`, Bereich `n × 0x40000 … n × 0x40000 + 0x3FFFF`
  (n = 0…63; 0 = 000000–03FFFF, 1 = 040000–07FFFF, …, 63 = FC0000–FFFFFF).
- 1-MB-Karte: **n** = `A23…A20`, Bereich `n × 0x100000 …` (n = 0…15; 0 = 000000–0FFFFF, 1 = 100000–1FFFFF, …, 15 = F00000–FFFFFF).
- Einstellung über **Wickelbrücken als Umschalter** (je eine Verbindung geschlossen), binär kodiert:
  - 256 KB: sechs Gruppen (11 13 12 | 9 14 10 | 7 15 8 | 5 16 6 | 3 17 4 | 1 18 2), Bit mit „o—o o" (Mitte↔rechts) = 0 bzw.
    „o o—o" = 1 — Zuordnung zur Tabelle im Handbuch (3.8-3 / 3.9-6) nur als ASCII-Skizze: **[unklar: Pfeil-/Strichdarstellung
    in den Tabellen 3.8-3, 3.9-4, 3.9-6 ist durch Zeilenumbruch verstümmelt; Mod 0 = alle Gruppen „o o--o", Mod 63 = alle „o--o o"; Gruppe
    „1 18 2" ist niederwertigstes Bit: Mod 1 kippt nur diese Gruppe]**.
  - 1 MB: vier Gruppen (11 13 12 | 9 14 10 | 7 15 8 | 5 16 6); Wickelstifte 1…4 bleiben frei; 17 und 18 liegen auf +5 V.
- Mehrere Karten werden zu einem **zusammenhängenden** Bereich gelegt (Moduladressen ohne Lücken).
  **[unklar: Verhalten bei Überlappung zweier Karten/Lücken; Hauptspeicher darf bei aktivem On-Board erst ab 008000 beginnen.]**

### 6.5 Refresh (§8.5 / §9.5)
- **Refresh-Counter der UB8001** so programmieren, dass jede der Zeilenadressen im Intervall gelöscht wird:
  64 Kbit: **128 Zeilen in 2 ms** (A0…A6); 256 Kbit: **256 Zeilen in 4 ms** (A0…A7) → **mindestens alle 15 µs**
  ein Refreshzyklus → **Wert 15 in den Refresh-Counter des U8001** (beide Kartentypen). Zeit zwischen Refreshzyklen
  so groß wie zulässig wählen (thermische Belastung). Die Karte hat selbst keine Refreshüberwachung — bei CPU-Stillstand
  (z. B. nach `SOFTRESET`/Stop) **geht der Speicherinhalt verloren**.
- Bussignale: `RFSN−` = Status 1 (Speicher-Refresh), `RFSH−` an der Karte; Refresh-Adresse = A0…A6/A7 (Zähler der CPU im Z8000-Refresh-Zyklus).
  Für den Emulator: Refresh nur als Zählerregister (kein Inhaltsverlust nötig) — **Entscheidung für P6**.

## 7. Unterschiede Index 1 ↔ Index 4 (16-Bit-Karte)

| Punkt | Index 1 (§6) | Index 4 (§7) | Quelle |
|---|---|---|---|
| Reset-Quellen | PRES−, RESET, BUSTESTRESET− | **+ SOFTRESET−** (Scheinausgabe FFE9H) | Tab. 3.6-2 ↔ 3.7-2 |
| Spezielle Logik | SBR FFC9 / SNVR FFD9 (nur Index 0) | **LEDAUS FFB9, LEDEIN FFD9, SOFTRESET FFE9**; FFC9 frei | Tab. 3.6-11 ↔ 3.7-11 |
| Breakregister | NBR (und SBR nur Idx 0) | nur NBR | S. 3-85 ↔ 3-111 |
| SCR Bit 4–7 | Idx 0 Wickelstifte; Idx 1 fest High | fest High (Tabelle ohne Zeilen 4–7) | Tab. 3.6-5 ↔ 3.7-5 |
| Trap | SEGT− von MMU; Idx 0 zusätzlich extern | SEGT− von MMU | S. 3-68 ↔ 3-95 |
| Speicherbus Zusatzsignale (X9, b-Reihe) | TESTDATA− (b13); (b12, b14, b18, b19, b22 leer) | **TESTWAIT− (b12), TAPEQUIT− (b14), BUSNVIACK− (b18), BUS2CLOCK (b19), MSDOSNMI− (b22)**, TESTDATA− (b13) | Tab. 3.6-7 ↔ 3.7-7 |
| BUS WAIT− | bei Index 0/1 nicht nutzbar | mit TEST WAIT− verbunden | Fußnote |
| Interface-Typ seriell | **DÜE** | **DEE** | S. 3-77 ↔ 3-103 |
| tty4 Taktbrücken | Wickelfeld 17…26 | Wickelfeld 1…10 | Tab. 3.6-14 ↔ 3.7-14 |
| IFSS | Sender **aktiv**, Empfänger passiv, Q+ Pin 12; Pins 10 SD+, 13 ED+, 14 ED−, 19 SD− | passiv/potentialgetrennt, Q+ an 12 **und 16**; Pins 10 SD−, 13 ED−, 14 ED+, 19 SD+ | Tab. 3.6-16 ↔ 3.7-16 |
| WDC-Anschluss | nur X8 (extern, 25-polig) | X8 extern **oder** X16 intern; zusätzliches Reset-Signal **RST (high)** (X8-13 / X16-B5) | Tab. 3.6-19 ↔ 3.7-19 |
| Kopplung | `ASTB1/ARDY1/ARDY0/ASTB0`-Namen, 4 Handshake-Pins, Masse X2:A13,B13; X3:B12 | + **E0-16 (X2:A9), DD-16 (X2:A11)**, RUN-LED (X2:A2/A13), 5P (X3:B2), Masse X2:B11–13 | Tab. 3.6-20 ↔ 3.7-20 |
| Kopplungspartner | 8-Bit Index 1 | **8-Bit Index ≥ 3 zwingend** | S. 3-87 |
| Karten-Konstruktion | 5 Buchsen (4 tty + WDC), Wickelstifte 1–16, 30–49 (Index 0; ab Idx 1 entfallen 1–16, 30–37, 38/39, 40, 41, 42–49) | 4 oder 5 Buchsen, Wickelstifte nur 1–13 (Bild 3.7-2) | Bild 3.6-2 ↔ 3.7-2 |
| Hauptspeicher-Maximum | 256 KB…1 MB (4 MB „künftig") | **256 KB…4 MB** | S. 3-61 ↔ 3-87 |
| Systemanlauf-Text, NMI, Interrupt, E/A-Peripherie, Baudtabellen, MMU-Logik | identisch | identisch | wörtlich gleich |

## 8. Widersprüche

- **W1** — Kopplungsstecker Index 1: Text/Tab. 3.6-20 „X2, X3", **Bild 3.6-2 „X3 Kopplung" + „X4 Kopplung"** (X4 ist tty4)
  (Z. 4049, 4057). Index 4 konsistent. Vermutlich Bildfehler.
- **W2** — Tab. 3.7-20: `DD-8` (X2:B5, A) und `E0-16` (X2:A9, E) tragen beide den Text „Ausgabedaten vorhanden" mit
  gegensätzlicher Richtung; 8-Bit-Tab. 3.4-15 beschreibt EO_16 als „Übergabe Daten 16→8" (siehe `hw_8bit.md` §10 W2).
- **W3** — Tab. 3.7-20: X2:A2 doppelt belegt (`V4/16-8` **und** „RUN-LED ein/aus"), `hw_8bit.md` §10 W6.
- **W4** — „32 Daten- und 8 Handshake-Signale" (Z. 4812, 5823) ↔ Tabellen (26 Daten-/Vektorleitungen; 4/6 Handshake).
- **W5** — SCR: Tab. 3.6-5 führt Bit 4–7 mit Wickelstiften 30–37 als **lesbar**; Fußnote: nur Index 0 hat die Stifte,
  „Bei Index 1 liegen Bit 4 bis Bit 7 fest auf High". Index 4 (Tab. 3.7-5) kennt die Bits 4–7 gar nicht, nennt SCR in Tab. 3.7-11 aber „4 Bit".
- **W6** — Index-1-Speicherbus: „Fußnote (3) BUSWAIT− kann bei Index 0 und 1 nicht genutzt werden" ↔ Reihe a, Pin 8
  trägt `BUS WAIT−`. (Für Index 4 verbunden mit `TEST WAIT−`.) Wirkung für den Emulator: `WAIT−` auf dem Speicherbus ist unbenutzt.
- **W7** — Unterschiedliche EPROM-Beschriftung: Bild 3.6-3 „1H (High Even), 2H (Low Even), 1L (High Odd), 2L (Low Odd)"
  ↔ Text „zwei Speicherbänke, jeweils mit dem Low-Datenbus (Odd-Adresse) und dem High-Datenbus (Even-Adresse) verbunden" —
  die Bild-Beschriftung (H/L = Position oben/unten oder High/Low Byte?) ist mehrdeutig.
- **W8** — Hauptspeicher-Kapazität: Index 1 „256 KB bis 1 MB (zukünftig 4 MB)" ↔ DRAM-Karte Index 3 bietet 1 MB/Karte und 4 Karten gesteckt = 4 MB
  (Anlage A: „256 KByte bzw. 1 MByte je Karte"). Kein Widerspruch in der Sache, aber die Zahl „4 MB" ist je nach Kapitel „künftig".
- **W9** — Tab. 3.6-12-Verweise: Text „(vgl. Tabelle 3.6-11)" für serielle Kanäle (richtig 3.6-12), „vgl. Tabelle 3.-7" (richtig 3.6-7). Kosmetisch.

## 9. Offene Fragen an Schaltplan/Gerät

1. **UB8010-Ansteuerung:** Special-I/O-Adressen (SIO/SOUT) der drei MMUs, Auswahlsignale, Wickelbrücken (Index 0), wie die
   MMU-Steuerlogik MMU1/2/3 wählt (CPU-Status 11xx, N/S, `SEG USR`, `SN6`), URS-Flag, Master-Enable nach Reset.
2. **Segmentnummernbildung:** Verdrahtung der 7-Bit-Segmentnummer an die MMUs; Segment 63 bei nichtsegmentierten Prozessen; SN6 abtrennen bei MMU2/3.
3. **Break-Register (NBR):** welche Adressbits (High-Teil) und welcher Vergleich (< / ≤); Reset-Wert; Verhalten im System-Mode.
4. **TRPL/IF1L/Fehlerregister:** Zeitpunkt des Latch (Ende des fehlerhaften Zyklus?), IF1L nur Low-Byte; Nutzung mit den MMU-Violation-Registern.
5. **On-Board-Speicher:** Verhalten bei Zugriff auf 4800–5FFF (Spiegel) / 6000–7FFF (leer = Busfehler/0xFF?), Lesewert bei SCR-`BD MEM ON−`; Verhalten von Wait-Zyklus.
6. **SCR lesen:** Rückgabewert (Bit 4–7?), Zugriff als Byte/Wort (nur ungerade Adressen!).
7. **Reset-Ablauf:** wer gibt `RESET` (X3:B1) frei (Bit des 8-Bit-Seite), Zeitablauf; Programmstatus-Area/Reset-Vektor-Adresse der UB8001.
8. **Interrupts:** Vektorwerte der Z80-Peripherie bei UB8001 (nur Low-Byte): Abbildung auf Programmstatus-Area (Z8000 VI-Vektor-Tabelle); `N/S−`, `RUN/HALT−`.
9. **Refresh:** Z8000-Refresh-Counter-Wert (15) und Frequenz; Verhalten der Karte bei fehlendem Refresh; `RFSN−`-Signalweg.
10. **Parität:** gerade/ungerade; Verhalten bei Paritätsfehler an `PE−` → `BUSPE−` → NMI (NMI-Identifier 0100).
11. **Kopplung (16-Bit-Seite):** Zuordnung PIO0/PIO1-Bits (Port A Daten, Port B Vektor?), Betriebsmodi (Mode 0/1/2/3), Interruptsemantik `INT8`/`INT16`, neue Index-4-Leitungen (EO/DD).
12. **WDC-Anschluss:** Port-Zuordnung (PIO2 A/B), Bedeutung `TE−`/`TR−`/`WDARDY−`, STATUS0–2-Kodierung (siehe `hw_wdc.md`).
13. **RUN-LED:** `LEDAUS`/`LEDEIN` (FFB9/FFD9): Zeitverhalten, Zusammenhang mit X2:A13 und 8-Bit-X11-A4.
14. **Gerätemessung:** Welchen Index (V: 1xxx oder 4xxx) hat das Gerät des Anwenders? Monitor 3.1/3.3? (Index bestimmt Kopplung, SOFTRESET, LED-Register.)
