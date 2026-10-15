# MC80.21/.22 — Hardware und ROMs (AP M10)

Stand: 2026-10-14.  Ergebnis von AP M10 (`doc/design/32_arbeitspakete_kassette_a5120alt_mc80.md` §6,
Entwurf `doc/design/31_mc80.md` §3.2).  Gegenstück zum MC80.3x: `hardware_3x.md`.  Das kommentierte
Betriebssystem steht in **`bsys_2x.prn`** (erzeugt von `tools/gen_mc8020_bsys_prn.py`, Wächter
`cli_mc8020_bsys_prn_passt_zur_quelle`).  Nur Auswertung von Unterlagen und Code — es gibt noch keinen
Emulator des MC80.2x, nichts hier ist am laufenden System bestätigt.

**Quellenkürzel**

| Kürzel | Quelle | Güte |
|---|---|---|
| [HB] | MC 80 **Nutzerhandbuch**, VEB Elektronik Gera, Ausgabe 12/86, 135 S. (tiffe.de `Robotron/MC80.2X/MC80.2x-Handbuch.pdf`, 43 MB, **nicht im Repo**; Textebene fehlerhaft in `mc80_2x_handbuch_ocr.txt`).  Angabe: gedruckte Seite (PDF-Seite) | Schreibmaschine, gut; die OCR verliert Tabellen und Bilder |
| [BS] | Original-Listing „MC 80 – Betriebssystem –“, Module S0.1–S0.3, Z-Nr. 50001:4100.00 (`doc/original_docs/mc80/mc80_22-bsys-doku.pdf`, 29 Blatt, ohne Textebene; Abschrift `tools/mc80/bsys_2x_abschrift.txt`).  Angabe: Blatt | Nadeldruck, gut lesbar |
| [UP] | „Unterprogramme des Betriebssystems MC80.21/22“ (`mc80_22-up-doku.pdf`, 2 S., mit Text) | gut |
| [ROM] | Abzüge `doc/EPROMS/MC80/mc8020/`, ausgewertet in `bsys_2x.prn` (Adresse = CPU-Adresse) | belastbar, wo der Code eindeutig ist |
| [MAME] | `src/mame/ddr/mc8020.cpp` (MAME master, abgerufen 2026-10-14) | Skelett |

**Nur im 43-MB-Original** (die Textebene im Repo gibt sie nicht oder falsch wieder; für diese Arbeit
wurde das Original heruntergeladen und als Bild gelesen): die Tabelle der **Geräteadressen** III-13 (36)
— in der OCR fehlen alle Adressen —, die **E/A-Adressen des Magnetbandprogramms** IV-9 (48), die
Speicheradressen III-12 (35), die Bilder 2.1 (Bedienelemente), **2.3 (Tastaturfoto)**, 3.1
(Blockschaltbild), 3.2–3.5 (Bestückung, Brücken) und die Tabellen 3.1/3.2 (Systembus- und
Koppelbusbelegung, in der OCR verstümmelt).  Wer diese Stellen nachprüfen will, braucht das Original.

---

## 1. Befund in Kürze (Abweichungen von Entwurf 31 / MAME)

1. **CTC F0H–F3H und PIO F4H–F7H sind die der Displaylogik DPL, nicht die der ZRE.**  Die ZRE-CTC/-PIO
   liegen auf **80H–87H** und gehören dem Anwender [HB III-13 (36)]; das Betriebssystem fasst sie nie an
   [ROM].  MAME `mc8020.cpp` und Entwurf 31 §3.2 („CTC F0H–F3H, PIO F4H–F7H“ auf der ZRE) sind zu berichtigen.
2. **MBI-Ports bestätigt: EFH = Laufwerksregister, F8H–FBH = MBI-CTC, FCH–FFH = MBI-PIO** [HB IV-9 (48)],
   gegengeprüft am Code [ROM 0600H–0BFFH].  Die Vermutung des Entwurfs („F8H–FFH + EFH“) stimmt.
3. **Das Original-Listing [BS] beschreibt ab 05B6H eine andere Fassung**, von der es **keinen Abzug** gibt
   („MB-BSY“ für das KMBG, ohne Register EFH, mit Taste „LAUF“).  Bis 05B5H ist es mit den Abzügen gleich
   (jede der 389 abgeschriebenen Zeilen mit Marke oder Kommentar liegt auf einem Befehlsanfang der Abzüge; Stichproben der Bytes) —
   §9, Anhang von `bsys_2x.prn`.
4. **Nur MAME „ver2“ passt zum Handbuch** (Arbeitszellen 4.3.2.1); ver1 hat andere Zellen, **ver3 ist ein
   unstimmiger Mischsatz** (§9).
5. **Takt 2,4576 MHz bestätigt**: [HB I-9 (9)] „Systemtaktzyklus 407 ns ± 0,1 %“ = 1/2,4576 MHz.  MAME stimmt.
6. **Bild = 8 × 32 Zeichen im 6-Bit-Code, BWS 0C00H–0CFFH, zeichenweise per Interrupt ausgegeben**
   (alle 64,5 µs ein Zeichen an DPL-PIO A); Analogkurven über OTIR an DPL-PIO B (§5).  MAME bildet nur den
   Textteil ab (eigener, nicht originaler Zeichensatz) und keine Interrupts der DPL.
7. **Tastatur: 7 Zeilen × 8 Spalten**, Zeile über DPL-PIO A (40H…01H), Spalten über DPL-PIO B, abgefragt im
   Zeilenende-Interrupt (§6).  MAMEs Matrix stimmt Taste für Taste mit der Tastentabelle TTA überein.
8. **Bandformat MC80.2x = 128-Byte-Blöcke mit Präambel/Postambel AAH, Blocknummer, CRC-16 (Polynom A001H)**,
   Bandmarke `AA 00 00 AA`, Kennzeichnungsblock mit BLN FFFFH [HB 4.3; ROM] — anderes Format als der K5020-Weg
   der A5120 (Folge für K1/K3, §7.6).

---

## 2. Gerät und Takt

| Merkmal | Wert | Quelle |
|---|---|---|
| Varianten | MC 80.21 und MC 80.22: je 1 × 16 KB ROM/EPROM (PFS K3820) + 1 × 16 KB dRAM (OPS K3525), 5 freie Steckplätze 215×170 | [HB I-7 (7)] |
| ZRE | **K2521** (oder K2523), U880D, 3 KB ROM S0.1–S0.3 0000H–0BFFH (3 × U555/U505), 1 KB statischer RAM 0C00H–0FFFH, PIO + CTC (CTC nur auf K2521) für den Anwender | [HB III-3 (27), I-9 (9)] |
| Takt | 2,4576 MHz (Taktzyklus 407 ns) | [HB I-9 (9)] |
| Systembus | K1520 nach TGL 37271/01, 64 KB, 256 E/A-Adressen, IM 2 | [HB I-9 (9), III-1 (25)] |
| Feste Steckplätze | DPL, MBI und EPR (PPE) auf **Platz 9, 10, 11** (nicht ziehen); 1…8 frei | [HB III-15 (38)] |
| Interruptkette | höchste Priorität rechts (Platz 11) abnehmend nach links; **DPL und MBI auf den beiden höchsten Plätzen (11, 10)**; 3 Anwenderquellen ohne Beschleunigung; ZRE darf nicht höchstpriorisiert gewickelt werden | [HB III-16 (39)] |
| ZRE-Wickel | X6–X7 Takt auf Koppelbus, X8:1–X9:1 MEMDI, X14:2–X15:2 IEI in Prioritätskette | [HB III-4 (28)] |
| Bildröhre | B 31 G 21, 8 Zeilen × 32 Zeichen, Zeichengenerator „U 402 D BM 510“ ([BS] Bl. 7: „U402 BM001“), 63 Zeichen | [HB I-9 (9), (23)] |
| Kassette | SK 1200 (modifiziertes Zweimotorenlaufwerk), Digitalkassette 490, 4,76 cm/s, 2 Spuren, 128 KB/Seite, Richtungstaktschrift 32 Bit/mm (ISO 3407) | [HB I-11 (11), (33/34)] |

Wiederverwendung im Kern: `core/cards/k2521` (ZRE), `k3820` (PFS), K3525 (A2).  Neu: DPL, BSA (nur
Ablenkung/Hochspannung, keine Register [HB (33)]), MBI, PPE.

## 3. Speicher

[HB III-12 (35)], gegengeprüft [ROM]:

| Bereich | Inhalt |
|---|---|
| 0000–0BFF | ROM S0.1–S0.3 (ZRE) |
| 0C00–0CFF | BWS 8 × 32 (ZRE-SRAM) |
| 0D00–0D0F | Arbeitszellen = IM-2-Tabelle, I = 0DH [BS Bl. 2] |
| 0D10–0D17 | Startadressen Grafik (Analog-BWS für Zeile 2/4/6/8) |
| 0D18–0D1B | Tabulatorpositionen (ROM legt 5 Byte 0D18–0D1C an [BS Bl. 2]) |
| 0D1C–0D2B | Arbeitszellen (0D1D KPS, 0D1E Zeichen unter dem Kursor, 0D1F Blinkzähler, 0D20 Repetierzähler, 0D21–0D27 Tastenspeicher) |
| 0D2C–0DEF | Interruptstartadressen Anwender (0D2C wird vom Bandprogramm für die MBI-PIO B belegt [ROM]) |
| 0DF0–0EFF | Arbeitszellen Magnetband (die Fassung ver2 benutzt auch 0DE5–0DEB [ROM; HB IV-9]) |
| 0F00–0FFF | Stapel (SP = 1000H) |
| 2000–23FF | S0.4: Magnetband-Menü, Pausen-ISR, Texte (`mo01`) |
| 2400–27FF | frei (Copy-Modus, Drucker/Lochband) |
| 2800–2BFF | Handler EPROM-Programmierung S2 (`mo03`) |
| 2C00–3FFF | Objektcodeeditor S1.5 Testmodus (2C00) und S1.1–S1.4 (`mo04`–`mo08`) |
| 4000–5FFF | EPAS/BASEX (`mo09`–`mo16`) |
| 6000–BFFF | frei; 6000–7FFF Prozesssteuerprogramm: 6000H = 00 und 6001H = JP → Start nach RESET, außer UC+OFF beim RESET gehalten [BS Bl. 3; HB II-6] |
| C000–FFFF | dRAM K3525 (Wickel C000–FFFF [HB III-5 (29)]); F000–F01F Editorzellen, ab F020 Markentabelle |

- **Menütabellen** werden **rückwärts** gelesen ab 05B5H, 3FFFH, 5FFFH, 77FFH, FFFFH [BS Bl. 13–14; UP];
  Eintrag = Startadresse (hi, lo), Kennbyte 80H, Name rückwärts mit Bit 7 am letzten Zeichen, FFH = Ende.
  Handschriftlich sind im Listing 5FFF/77FF/FFFF zu 7FFF/5FFF/77FF verbessert; die Abzüge haben die
  gedruckten Werte [BS Bl. 14; ROM 04D1H].
- **RAM-Schreibsperre** (nur Steckeinheit „4 KByte dRAM mit Schreibsperre“, U202): `OUT 01H` gibt, `OUT 02H`
  sperrt den 1-KB-Bereich, dessen A10–A15 bei `OUT (n),A` aus A auf A8–A15 liegt; Datenbus beliebig; **IN
  auf diese Ports verfälscht die Programmierung** [HB III-6/7 (30/31)].  RSP gibt beim Start 1000H–FFFFH frei
  (60 × `OUT 01H` mit A = 10H, 14H … FCH) [BS Bl. 13].  Port 02H benutzt das Betriebssystem nie [ROM].
  Ob die OPS K3525 der .21/.22 die Sperre hat, sagt das Handbuch nicht [?].
- **RST/NMI** [HB VI-7 (101); BS Bl. 2]: RST 08H…30H → 6007H, 600AH … 6016H (Anwender), RST 38H → 2C00H
  (Editor-Testmodus), NMI → 6004H; das Betriebssystem benutzt den NMI nicht.  Zusätzlich stehen bei 000BH,
  0013H, 001BH, 0023H, 002BH, 0033H die Sprünge BSN/TST/BSS/BSL/BKS/BKL (Aufruf mit `CALL`) [BS Bl. 2; UP].

## 4. E/A-Adressen

[HB III-13 (36)] für 00H–FFH, Einzelheiten [HB IV-9 (48)], [BS], [ROM]:

| Adresse | Belegung | Quelle |
|---|---|---|
| 00H | intern belegt | [HB] |
| 01H / 02H | Schreibfreigabe / -sperre RAM (s. §3) | [HB] |
| 03H–0FH | intern belegt | [HB] |
| 10H–7FH | frei für Anwender | [HB] |
| 80H–83H | ZRE-CTC 0–3 (Anwender) | [HB] |
| 84H, 85H, 86H, 87H | ZRE-PIO A Daten, B Daten, A Steuer, B Steuer (Anwender) | [HB] |
| 88H–8FH | intern belegt | [HB] |
| 90H–DFH | frei für Anwender | [HB] |
| E0H–FFH | intern belegt: | [HB] |
| E0H–E8H | EPROM-Programmiereinheit (§8) | [ROM `mo03`] |
| EFH | MBI: Register Laufwerksteuerung (REG), nur schreiben | [HB IV-9; ROM] |
| F0H–F3H | DPL-CTC 0–3 | [BS Bl. 2–4; HB VI-6/7] |
| F4H / F5H | DPL-PIO A Daten / Steuer | [BS Bl. 2–3] |
| F6H / F7H | DPL-PIO B Daten / Steuer | [BS Bl. 3] |
| F8H–FBH | MBI-CTC 0–3 (CT0 … CT0+3) | [HB IV-9] |
| FCH / FDH | MBI-PIO A Daten / Steuer (PIA-1 / PIA) | [HB IV-9] |
| FEH / FFH | MBI-PIO B Daten / Steuer (PIB-1 / PIB) | [HB IV-9] |

Reihenfolge Daten/Steuer/Daten/Steuer bei DPL und MBI, Daten/Daten/Steuer/Steuer bei der ZRE-PIO.

**IM-2-Vektoren** (I = 0DH) [BS Bl. 2–3; ROM]:

| Zelle | Quelle | Routine |
|---|---|---|
| 0D00 | DPL-CTC 0 (Vektorbasis 00H) | DPI Zeichenausgabe, 00C4H |
| 0D02 | DPL-CTC 1 | TI1 = nur EI/RETI (Synchronisation der Analogzeile mit HALT), 00CAH |
| 0D04 | DPL-CTC 2 | TSI Zeilenende, 00CDH |
| 0D06 | DPL-CTC 3 | BREAK (Taste BK), frei für den Anwender; Init `OUT F3H,C7H / OUT F3H,01H` [HB VI-6 (100)] |
| 0D08–0D0E | MBI-CTC 0–3 (Vektorbasis 08H) | Bandprogramm: K0 Byte lesen (ILB), K3 Sync/Byte schreiben/Pause (ISY/ISB/IPA/IVL) |
| 0D2C | MBI-PIO B (Vektor 2CH) | IPB: Auswurf / Bandende |
| 0D30–0DEF | ZRE-CTC/-PIO u. a. | Anwender [HB VI-4 (98)] |

## 5. Displaylogik DPL und Bild

Die DPL erzeugt X-/Y-Ablenkung, Helligkeit und die Tastenmatrix-Ansteuerung; das Bild entsteht
„oszillografisch“, **keine Fernsehnorm**; je X-Ablenkung wird eine Zeichenzeile oder eine Analogkurve
vollständig geschrieben [HB III-7 (31)].  Die BSA liefert Hochspannung/Ablenkung, ohne Register [HB (33)].

### 5.1 Bausteine und Init [BS Bl. 2–3]

| Baustein | Init | Bedeutung |
|---|---|---|
| DPL-PIO A (F4/F5) | Betriebsart 0 Ausgabe (`0FH`) | Zeichencode des nächsten Zeichens (Bit 0–5), **Bit 7 = Z-Signal „analog“** [BS Bl. 4]; außerhalb der Bildausgabe Zeilenwahl der Tastenmatrix |
| DPL-PIO B (F6/F7) | Betriebsart 3 (`FFH`), Maske FFH (alles Eingang) beim Tastaturlesen; Betriebsart 0 (`0FH`) Ausgabe sonst | Ausgabe: **Zeilenposition in Bit 7,6,5, invertiert** (`CPL`) bzw. **Y-Wert der Analogkurve**; Eingabe: Spalten der Tastenmatrix |
| DPL-CTC 0 (F0) | Vektor 00H, `C7H` Zähler mit Interrupt, ZK **1** | ein Interrupt **je Zeichentakt** → DPI |
| DPL-CTC 1 (F1) | `47H`, ZK D0H (ohne Interrupt) | „Auszählen der Strahlrücklaufzeit“; bei Analogzeilen mit Interrupt (`C1H`) zur Synchronisation |
| DPL-CTC 2 (F2) | erst `07H`/07H (Zeitgeber, „Impuls f. Strahlrücklauf, Tastatur freigeben“), dann `C7H`/21H | Zähler mit Interrupt nach **33 Zeichen** (Leerzeichen + 32) → TSI Zeilenende |
| DPL-CTC 3 (F3) | vom Anwender | BREAK-Taste BK (Zähler, ZK 1) [HB VI-6] |

### 5.2 Textausgabe

- **DPI** (CTC 0, alle **64,5 µs**, Laufzeit 27,6 µs [HB III-7 (31)]): im Zweitregistersatz `OUTI` — (HL) an
  C = F4H, HL+1 [BS Bl. 3].  Der Zweitregistersatz gehört damit dem Display [HB VI-8].
- **TSI** Zeilenende: nächste Zeile; Zeilenanfang im BWS ausrichten (`AND E0H`), Zeilenposition auf PIO B
  (Bit 7–5 invertiert), Leerzeichen 20H als erstes Zeichen „bei Strahlvorlauf“; nach der letzten Zeile
  Tastatur lesen; Kursorblinken und Repetierzähler laufen hier mit ~50 Hz [BS Bl. 3–4].
- **Zeichencode im BWS = 6 Bit**: ASCII 20H–3FH → 20H–3FH, 40H–5FH → 00H–1FH, Kleinbuchstaben werden zu
  Großbuchstaben, alles andere wird nicht geschrieben [BS Bl. 5, 7].  Der **Kursor** ist Code **1FH**
  (Unterstrich), das Zeichen darunter liegt in 0D1E [BS Bl. 5, 7].  BWS-Adresse = 0C00H + 32·Zeile + Spalte.
- Zeichengenerator U402 (ROM auf der DPL, **kein Abzug** im Repo [?]); laut [HB Bild 2.3 (23)] zeigt er für die
  Tastensymbole `@ & ^ \` andere Bilder (Raute, Promille, Negation [?], Ø) — Foto zu klein.  MAMEs `prom[]`
  ist ausdrücklich nicht der U402-Inhalt.
- Steuerzeichen von BSN [BS Bl. 5–6; HB V-3]: 08 links, 09 rechts, 0A runter, 0B hoch, 0C Bild löschen,
  0D Zeilenanfang, 1A Zeichen löschen (CL), 18 Zeile löschen, 02 Bildanfang, 03 Tabulator.

### 5.3 Analogkurven (Grafik)

[HB V-13 (93); BS Bl. 4]: Steht in 0D10/0D12/0D14/0D16 eine Adresse ≠ 0000, wird **statt Zeile 2/4/6/8**
eine Kurve aus dem 256-Byte-Bereich ab dieser Adresse gezeichnet (Abszisse = Adresse, Ordinate = Byte
0…FFH).  Ablauf ANL: PIO A Bit 7 = 1 (Z-Signal analog), erstes Byte per `OUTI` an PIO B, DPI aus, TI1 an,
`HALT` bis Zeilenanfang, dann `OTIR` mit B = FEH an PIO B, danach DPI wieder an.  Damit gehen 255 Werte
hinaus; das Handbuch nennt **253 Abszissenwerte** [?Differenz].  Last: Unterbrechung je Analogzeile bis
2,7 ms, Interruptlast bis 72 % [HB VI-8 (102)].

## 6. Tastatur

79 kontaktlose Hall-Tasten TSR 19F in einer **8 × 7-Matrix** [HB (33), (21)].  RESET (rot) wirkt
über ein Monoflop direkt auf den Rechner (RAM bleibt), **BK** löst über die DPL einen Interrupt aus
(DPL-CTC 3), Ebenen-Flipflop mit Leuchtdiode, CTR = dritte Ebene [HB (33)].

**Abfrage** [BS Bl. 3, 8–9]: im TSI nach der letzten Bildzeile, PIO B auf Eingabe, PIO A nacheinander
**40H, 20H, 10H, 08H, 04H, 02H, 01H**, je Zeile ein `INI` von F6H nach 0D21H…0D27H.  TST wertet aus: Bit 7 der
Zeile 01H = **UC**, Bit 6 = **CTR**; Tastennummer = 8·Zeilenindex + Bitnummer; Nummern 11H–2AH sind A–Z
(ASCII = Nr. + 30H), die übrigen gehen über die Tabelle **TTA 0305H** (UC: +1CH); CTR macht aus 40H–5FH
00H–1FH.  Repetieren nach 0,6 s [UP], Kursorblinken 320 ms [?] [BS Bl. 8].  Rückgabe A, Cy, Z [UP].

Matrix (aus TTA, Taste ungeschaltet / mit UC; „Bit“ = Bit des PIO-B-Eingangs) — stimmt mit [MAME] überein:

| PIO-A | Bit 0 | Bit 1 | Bit 2 | Bit 3 | Bit 4 | Bit 5 | Bit 6 | Bit 7 |
|---|---|---|---|---|---|---|---|---|
| 40H | 00: `0` / `"` | 01: `1` / `!` | 02: `2` / `@` | 03: `3` / `#` | 04: `4` / `$` | 05: `5` / `%` | 06: `6` / `&` | 07: `7` / `'` |
| 20H | 08: `8` / `(` | 09: `9` / `)` | 0A: `+` / `*` | 0B: `,` / `<` | 0C: `\` / `^` | 0D: `;` / `:` | 0E: `-` / `/` | 0F: `.` / `>` |
| 10H | 10: Leer | 11: `A` | 12: `B` | 13: `C` | 14: `D` | 15: `E` | 16: `F` | 17: `G` |
| 08H | 18: `H` | 19: `I` | 1A: `J` | 1B: `K` | 1C: `L` | 1D: `M` | 1E: `N` | 1F: `O` |
| 04H | 20: `P` | 21: `Q` | 22: `R` | 23: `S` | 24: `T` | 25: `U` | 26: `V` | 27: `W` |
| 02H | 28: `X` | 29: `Y` | 2A: `Z` | 2B: `=` / `?` | 2C: `[` / `]` | 2D: TAB 03 | 2E: (00, unbelegt) | 2F: CL 1A |
| 01H | 30: ENTER 06 | 31: hoch 0B | 32: OFF 16 | 33: runter 0A | 34: rechts 09 | 35: links 08 | CTR | UC |

Die **Lage der Tasten** (Foto Bild 2.3 [HB (23)]: Schreibmaschinenanordnung QWERTZ, CL rechts oben,
BK links, OFF und ENTER neben der Leertaste) ist im Scan nicht lesbar genug für eine Bildschirmtastatur → M12.

## 7. Magnetbandinterface MBI

### 7.1 Aufbau [HB (33/34)]

Funktionsgruppen: programmierbare E/A-Schnittstelle (PIO), Zähler/Zeitbasis (CTC), programmierbare
Laufwerksteuerung (Register EFH), Adressierung, Aufnahme-/Wiedergabesteuerung; Datenverkehr
**interruptgesteuert** in Abhängigkeit von der Zeitbasis der MBI; Wandlung in pulsdauermodulierte
Richtungstaktschrift 32 Bit/mm (ISO 3407).  Laufwerk SK 1200 (modifiziertes Zweimotorenlaufwerk),
programmgesteuert, keine Funktionstasten [HB (24)].

### 7.2 Signale (aus dem Code [ROM], Deutung [?] wo vermerkt)

| Port/Bit | Richtung | Bedeutung | Beleg |
|---|---|---|---|
| FEH Bit 0 | aus | 1 = Lesen, 0 = Schreiben | [HB IV-15 (54), LWK] |
| FEH Bit 1 | aus | 1 = Capstan ein | [HB IV-15 (54)] |
| FEH Bit 3 | aus | 1 = Daten schreiben, 0 = Pausenpegel schreiben | [HB IV-15 (54)] |
| FEH Bit 4 | ein | Pausenpegel/Phase beim Blockanfang: wählt 8 oder 7 Bit bis zum ersten Byte | [ROM 0B8AH; BS Bl. 20 „16 Bit/15 Bit“] |
| FEH Bit 5 | ein | 1 = Schreibsperre der Kassette (Riegel ausgebrochen) | [ROM 08B3H; HB „SSP“] |
| FEH Bit 6 | ein | 0 = Bandende (BOT/EOT), löst PIO-Interrupt aus | [ROM 0B5BH, 0B7AH] [?] |
| FEH Bit 7 | ein | 1 = Kassette eingelegt; 0 → Interrupt = Auswurf → AUS, Fortsetzung bei 0600H | [ROM 08A4H, 0B69H; HB (24)] |
| FCH | ein/aus | Datenbyte (Schieberegister der MBI); Lesen: PIO A Betriebsart 1 (`4FH`), Schreiben: Betriebsart 0 (`0FH`) | [ROM] |
| EFH | aus | Laufwerksteuerung: 00 alles aus, **0AH Rücklauf [?]**, **06H Vorlauf [?]**, 11H Vorstufe/Andruck [?], **13H Lauf 4,76 cm/s [?]**, 02H Endabschaltung [?] | [ROM 07CEH, 089FH, 09AC, 0B4F] |

MBI-PIO B: Betriebsart 3, Bits 7–4 Eingang, 3–0 Ausgang, Interrupt ODER low-aktiv über Bit 7 und 6,
Vektor 2CH (Tabelle PIOT 21BBH: `2C FF F0 17 3F`), freigegeben mit `83H` [ROM].

### 7.3 Zeitbasis und Interrupts (Deutung aus dem Code [ROM])

- **Schreiben** (WIN): CTC 2 Zeitgeber, Vorteiler 16, ZK 50 → **3072 Hz**; CTC 0 Zähler ZK 8 ohne Interrupt;
  **CTC 3 Zähler ZK 1 mit Interrupt** → ein Interrupt je Byte (ISB gibt das nächste Byte an FCH).  Die
  Verkettung CTC 2 → (Bitzelle) → CTC 0 → CTC 3 liegt in der MBI-Hardware [?].  Bei 3072 Hz = 2 Halbzellen
  je Bit ergäben sich 1536 Bit/s = 192 Byte/s; das passt zu 32 Bit/mm × 47,6 mm/s = 1523 Bit/s und zur
  Vorlücke 1400H Byte-Zeiten ≈ 27 s (Handbuch „ca. 30 s“) [?].
- **Lesen** (LIN): CTC 1 und CTC 2 `47H`/ZK 4BH (Zähler ohne Interrupt, Zweck unklar [?]); CTC 3 `F7H`/ZK 1 =
  „Synchronisationsinterrupt (Blockendeerkennung)“ [HB (55)] → ISY setzt SPH Bit 7 und startet CTC 0 (ZK 8
  bzw. 7 je nach FEH Bit 4); **CTC 0 Interrupt nach 8 Bit** → ILB liest FCH in den Ringpuffer 0E00–0F1FH.
- **Pausen** (Blocklücken, konstante Magnetisierung): PAZ = 51H Byte-Zeiten, gezählt im CTC-3-Interrupt IPA.

### 7.4 Bandformat [HB IV-5/6 (44/45); ROM WVO0/BLE0]

| Struktur | Bytes auf Band |
|---|---|
| Datenblock | `AA` · BLN (2, **höherwertiges Byte zuerst**) · 128 Daten (BYE) · CRC (2, **niederwertiges zuerst**) · `AA` = 86H Byte |
| Kennzeichnungsblock (Name) | wie Datenblock mit BLN = FFFFH, Daten = 128 Byte Namensfeld (BWS-Zeilen 4–7, Dateiangaben am Ende von Zeile 7) |
| Bandmarke (Dateitrenner) | `AA 00 00 AA` |
| Vorlücke | vor Block 0001 ca. 30 s konstante Magnetisierung (über Vorspann und BOT) |

CRC-16 über BLN + Daten, rechtsschiebend mit Polynom **A001H** (= x¹⁶+x¹⁵+x²+1, „CRC-16/ARC“), Startwert
0000; beim Lesen wird BLN+Daten+CRC durchgerechnet, Rest 0 = gut [ROM 0A3FH, 080AH].  Datei = Kennzeichnungsblock
+ Datenblöcke + Bandmarke; Blocknummern laufen vom Bandanfang durch, ein Inhaltsverzeichnis gibt es nicht;
Schreiben ab BLN > 1 sucht den Vorgängerblock und verlangt dahinter eine Bandmarke (sonst „FDAT“).

### 7.5 Softwareschnittstelle

Sprungleiste 062DH–065CH (BLE, MRD, MWR, MCL, RCK, AUS, LIN, WIN, WVO, GRZ, R1S, LES, LWK, PIN, VGL, ZEI),
Arbeitszellen 0DE5–0DFF und Fehlermeldungen: [HB IV-9…IV-18 (48–57)], vollständig kommentiert in
`bsys_2x.prn`.  Die Adressen der Sprungleiste gelten nur für **ver2** (in ver1 ebenso, aber andere Zellen).

### 7.6 Hinweise für die Kassetten-APs

- Das MC80.2x-Band ist **nicht** das K5020-Format: Bytes kommen als ganzes Byte über ein Schieberegister
  (PIO A), nicht bitweise; Block 86H Byte mit AAH-Rahmen.  Ein gemeinsames Medium `peripherals/kassette`
  muss Blöcke + Lücken + Bandmarken abbilden können; der MBI-Kartenteil (Schieberegister, Bittakt,
  Blockende-Erkennung) ist eigene Arbeit in M11.
- Entwurf 31 sagt „Profil SK1200/LW1200“ für .2x; die MBI-Fassung ver2 steuert das Laufwerk über **EFH**
  (Motoren/Magnete), die KMBG-Fassung des Listings nur über FEH + Taste „LAUF“ — zwei Laufwerksvarianten.

## 8. EPROM-Programmiereinheit (PPE) E0H–E8H

[HB (34)]: „über 3 Ausgabe- und 1 Eingaberegister“, Spannungen über Relais, alles programmgesteuert;
Fassung U555 C und kompatible, Betriebsarten Testen/Lesen/Programmieren/Vergleichen [HB I-12 (12)].  Aus dem Code
von `mo03` (2800H, nur Byte-Suche und Ausschnitte, kein Disassemblat):

| Port | Gebrauch in `mo03` | Deutung |
|---|---|---|
| E0H / E1H | Adresse A0–A7 invertiert / Steuerwort 0FH | PIO-Kanal [?] |
| E2H / E3H | Steuersignale (FFH, 78H, 70H, 7FH, 1AH, 32H, C8H/CAH) / 0FH | Programmier-/Lesesteuerung, Relais [?] |
| E4H / E5H | Adresse A8–A15 invertiert / 0FH | PIO-Kanal [?] |
| E6H / E7H | Daten (aus bzw. ein) / 0FH bzw. 4FH | Datenkanal [?] |
| E8H | `07H, FFH`, dann Lesen bis Wert < Schwelle | CTC-Kanal als Impulsdauer-Zeitgeber [?] |

Die Steuerworte 0FH/4FH sind PIO-Betriebsarten — das widerspricht der Prosa „Register“; ohne Schaltplan
offen.  Für den Kern erst in Etappe 4 nötig (Entwurf 31 §6).

## 9. ROM-Fassungen

| MAME-BIOS | Teile | Befund |
|---|---|---|
| ver1 | s01, s02, s03, mo01_v2 | Bandprogramm wie ver2, aber Zellen anders (CRC 0DEFH, FEZ 0DEEH, PAZ 0DECH, BYZ 0DF1H, SPH 0DF3H, PUZ 0DF4H, BLZ 0DF6H, BYE 1 Byte 0DF8H) — passt **nicht** zum Handbuch |
| **ver2** | s01, s02_v2, s03_v2, mo01 | = Handbuch „Programmversion 12.01.84“ (alle Zellen IV-9/10 stimmen) → **Vorgabe für M11**, `bsys_2x.prn` |
| ver3 | s01, s02_v3, s03, mo01_v2 | s02_v3 = s02 + KMBG-Menü 05B6–05FFH (Menüeintrag BAND → 05B6H), aber Bandteil ab 0600H aus s02: MEN springt in fremden Code → **unstimmig, nicht verwenden** [?Mischabzug] |
| (Listing [BS]) | — | 0000–05B5 = alle Abzüge; 05B6–0BFF = Fassung „MB-BSY“ für KMBG, **kein Abzug**; Abschrift im Anhang von `bsys_2x.prn` |

`mo01` vs. `mo01_v2`: dieselbe S0.4 für ver2 bzw. ver1 (Zellen- und Adressverschiebung, 404 Byte).
`mo03` = EPROM-Programmierung S2 (2800H), `mo04` = Editor-Testmodus (2C00H, benutzt DPL-CTC 3 für den
Schrittbetrieb [HB VI-6]), `mo05`–`mo08` Editor, `mo09`–`mo16` EPAS/BASEX (4000H–5FFFH); `mo02` (2400H) fehlt
am Gerät [MAME; HB „frei“].

## 10. Vergleich mit MAME `mc8020.cpp`

| MAME | Befund |
|---|---|
| CTC F0–F3, PIO F4–F7 „ZRE“ | sind die **DPL**; ZRE liegt auf 80–87 (§4) |
| PIO A schreibt Zeile, PIO B liest Spalten | stimmt (§6); Matrix stimmt Taste für Taste |
| CTC-Takte geraten (K2 = Takt/64, ZC2 → TRG0/1) | falsch: K0 zählt den Zeichentakt der DPL, K2 die Zeichen je Zeile (§5.1) |
| Bild 32×8 aus 0C00, eigener Zeichensatz | Inhalt stimmt, Zeichensatz nicht original; keine Interrupt-getriebene Ausgabe, keine Analogkurven |
| keine MBI, keine PPE, keine RAM-Schreibsperre, kein BREAK | fehlt |

## 11. Offen / für M11

- U402-Zeichengenerator (DPL) fehlt als Abzug → Zeichensatz vorerst nachbauen (6-Bit, 5×7 [?]).
- Genaue Zeitbasis der DPL (Zeichentakt 64,5 µs; Rücklaufzeiten über CTC 1/2) und der MBI (Verkettung der
  CTC-Kanäle, Bedeutung von CTC 1/2 beim Lesen, EFH-Bits) stehen nur im Code — Schaltpläne von DPL/MBI sind
  nicht im Repo (auch nicht im Handbuch).
- Für M11 reicht vermutlich ein **zeichenweises Modell**: DPI je 64,5 µs Maschinenzeit, TSI nach 33 Zeichen,
  Tastatur im TSI; Bild aus dem BWS rekonstruieren (Textrückgewinnung trivial, 6-Bit-Code).
- Tastenlage für die Bildschirmtastatur (M12): Foto des Originals ist zu klein.
