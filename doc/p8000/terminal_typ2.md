# P8000-Terminal Typ 2 — Hardware und Firmware 5.0 (Befund AP P19a)

Stand 2026-10-07. Zweck: Grundlage für P19b (Entwurf) und P20a–d (Nachbau des Terminalrechners **mit
Original-Firmware**). Tastatur K7673.09: `doc/p8000/tastatur_k7673.md`.

**Sicherheitsgrade:** **[G]** gelesen (Quelle wörtlich), **[A]** abgeleitet (aus mehreren gelesenen Stellen
gefolgert), **[U]** unsicher/Vermutung. Quellenkürzel:

| Kürzel | Quelle |
|---|---|
| HB | `~/projects/robotron/P8000/doc/P8000_Hardwarehandbuch.pdf` (V1.3, 06/1988), Kap. 4; Zeilenangaben „HB Z.“ = `pdftotext -layout` |
| SLP | **Stromlaufplan Terminal Typ 2**, Nachzeichnung (gEDA) von O. Lehmann 2009, pofo.de `notes/plaene/eigene/P8000_Terminal_Typ2/` — lokal `~/projects/robotron/P8000/pofo_terminal/Typ2/Stromlaufplan.pdf` (3 Blatt: Bl. 1 Stromversorgung, Bl. 2 Rechner/Schnittstellen/Tastatur, Bl. 3 CRTC/BWS/ZG/Video); **keine EAW-Originalzeichnung** |
| STL | Stückliste dazu (`…/Typ2/Stueckliste.pdf`, „p8000/Terminal LP2, bestueckt“, st 889005) |
| FW | Firmware-Quelle 5.0, `~/projects/robotron/P8000/github_P8000/firmware/TERMINAL/p8t.{init,main,up,esc,vt100}.s` (OlliL/P8000) |
| ABZ | Abzug `doc/p8000/eproms/TERMINAL/P8T_1_5.0` (4096 B); Disassemblat `tools/z8_disasm.py` |

Ebenfalls lokal abgelegt: Typ-1-Plan + Stückliste (`…/pofo_terminal/Typ1/`). GLE-Plan liegt auf pofo.de
(`…/eigene/P8000_Terminal_GLE/`), nicht geladen. Die Bilder `16bit_Schaltplan/Stromlaufplan_GLE_01/02.jpg`
gehören laut `doc/p8000/quellen.md` zur GLE-Grafik-/Terminalkarte der Entwicklungsmuster und wurden für
Typ 2 **nicht** herangezogen (SLP ersetzt sie).

---

## 0. Kernbefund — der Plan in §11 muss berichtigt werden

1. **Der Terminalrechner hat KEINEN U880.** Prozessor ist der **Einchip-Mikrorechner UB 8840 M** (Z8, 64-polige
   Ausführung mit eigenem Programmspeicherbus, Westtyp Z8612) [G: HB Z. 6798, STL lfd. 3 „ub 8840 m / Z8612“,
   SLP Bl. 2 D2 „U884“; FW-Kopf „Einchip-Mikrorechner U 882“]. §11 („eigener U880“, „dritter/vierter Z80“,
   „SIO/PIO/CTC“) ist falsch. **Für die Original-Firmware wird ein Z8-Kern gebraucht** — im Projekt gibt es
   keinen (nur den Disassembler `tools/z8_disasm.py`; die K7672 des K8915 ist ein Verhaltensmodell).
2. **Kein SIO/PIO/CTC**: serielle Schnittstelle = UART des Z8 (P30/P37, Baudrate aus Zähler T0), Tastatur =
   Schieberegister 2 × 74LS299 an Port 2 + Interrupt P33, Bildspeicher-DMA = diskrete Zähler 5 × 74LS193 mit
   Z8-Tristate-Kunstgriff [G: STL, SLP; FW].
3. Videobaustein **8275** (KR580WG75) [G: HB, STL lfd. 23]; **Bildwiederholspeicher 2 KB** (4 × U214 = 2114)
   [G: STL lfd. 12]; zwei Zeichengeneratoren **2716** (1D21/2D21 = „Z61/Z62“) [G: STL lfd. 22, SLP Bl. 3].
4. Quarze **7,3728 MHz** (Z8) und **17,998 MHz** (Punkttakt) [G: STL lfd. 24/25]. 7,3728 MHz deckt sich exakt
   mit den Zeitkonstanten der Firmware (9600 Bd, „1,08 µs“) [A].
5. **Firmware 5.0 erwartet eine IBM-XT-Tastatur** (Scancode-Satz 1 mit Make/Break, Vorbytes E0/E1, AA = Selbsttest
   gut, FC = Fehler) [G: FW `p8t.init.s` IRP33, `p8t.main.s` TGETCHAR, Kopf „Anpassung an IBM (XT) Tastatur“].
   Die K7673.09 liefert genau dieses Protokoll (`tastatur_k7673.md`).
6. Abzug `P8T_1_5.0` **≙ Quelle 5.0** (Stichproben bitgleich, §5.1). Firmware **6.0 ist für eine andere
   Speicherkarte** geschrieben (§5.3).

---

## 1. Blockbild Terminalrechner Typ 2 [G: SLP Bl. 2/3, STL; A: Zusammenschau]

```
            7,3728 MHz                          17,998 MHz ── ÷N (3D8 74193) ── CCLK
               │                                     │
   2732 ──PM── UB8840M (Z8) ──P0/P1 Bus── 8275 ── CC0–6/LC0–3 ── 2716 ZG1 | 2716 ZG2 ── 74299 ── Video
   (D18)       │  │   │   │           │            (D22)              (Z61/Z62 Auswahl-FF)   (3D1)   VIDEO1/2
               │  │   │   │           └── DMA-Zähler 74193×3 → RA0–RA9 ── 2114×4 (2 KB BWS) ──┘
               │  │   │   └── P2 ← 74299×2 (Tastatur-Schieberegister) ← XB1 Takt/Daten
               │  │   └── P30/P37 UART ── SN75154 / TL084 (V.24) | CNY17 (IFSS) ── XB5
               │  └── P36 VSYN (Software) ── 74123 Watchdog → /RESET ; BSYNC
               └── A13–A15-Dekoder 74138 (1D10): Bell, ZG-Auswahl, Tastatur-Rücksetzen
```

## 2. Prozessor UB 8840 M (Z8)

| Punkt | Befund | Grad / Quelle |
|---|---|---|
| Typ | UB 8840 M, 64-polig (Z8612-Gegenstück), Programmspeicher über eigenen Bus A0–A11/O0–O7 an EPROM 2732 (D18) | [G] STL 3, SLP Bl. 2 (D2 Pins A00–A11, O0–O7, 2732 D18) |
| Firmware | 4 KB 2732, Programmadressen 0000–0FFF; Vektoren 0000–000B, Start 000CH | [G] HB Z. 6810, ABZ |
| Quarz | 7,3728 MHz (Q1 an XTAL1/2) ⇒ interner Takt 3,6864 MHz, Zählertakt XTAL/8 = 921,6 kHz = 1,085 µs | [G] STL 24 / [A] FW: „26*1.08=28 mys“, `ZVSY 192*1.08` |
| Ports | `P01M = 96H`: P0 = A8–A15 (P00–P03 A8–A11, P04–P07 A12–A15), P1 = AD0–AD7, Stapel intern; `P2M = FFH` (P2 Eingang); `P3M = 51H`: P30/P37 serielle E/A, P34 = /DM, P2 Gegentakt; `P3 = 2FH` | [G] FW init, ABZ 0024H–002FH |
| Stapel | SPL = 80H (intern, Registerdatei) | [G] FW |

### 2.1 Interrupts (Z8 IRQ0–5, Vektortabelle ABZ 0000H) [G: FW `INTERRUPT ARRAY [IRP32 IRP33 IRP31 IRP30 IRT0 IRT1]`, ABZ 00EE/00FC/00A2/019F/00FB/00D8]

| IRQ | Pin/Quelle | Routine | Wirkung |
|---|---|---|---|
| IRQ0 | P32 ← 8275 **IRQ** (Bildende; über Inverter 1D12) | IRP32 00EEH | P36 := 1 (VSYN ein), T1 := 192 (≈ 208 µs), IMR := 2AH |
| IRQ1 | P33 ← Tastatur-Schieberegister „Byte voll“ (Brückenfeld 22–33, D3/D9) | IRP33 00FCH | P2 lesen, `RR`, Scancode auswerten; danach Schreibzugriff C000H = Schieberegister zurücksetzen |
| IRQ2 | P31 ← 8275 **DRQ** (gegattert mit P35, 2D4 7402) | IRP31 00A2H | Zeilen-DMA starten (§3.3) |
| IRQ3 | P30 serieller Empfang | IRP30 019FH | Zeichen in Ringpuffer 45 B, 00H/7FH verworfen, Bit 7 gelöscht, XOFF bei 36 Zeichen |
| IRQ4 | T0 / Senden | IRT0 00FBH | nur `IRET` (Senden wird gepollt) |
| IRQ5 | T1 | IRT1 00D8H | DMA-Ende: P35 := 1, P01M := 96H, IMR := 0FH, P36 := 0 (VSYN aus) |

Zuordnung IRQ0 = P32, IRQ1 = P33, IRQ2 = P31, IRQ3 = P30 ist Z8-Norm [A]; die Quellen an den Pins [G: SLP Bl. 3
P32/P31-Netz, FW-Kommentare „IRP32 -> VTRC“, „Zeilenrefresh“, „Tastatur-Eingabe-Routine (P33-Int.)“]. Ob P32 am
8275-IRQ oder an VRTC hängt, ist im SLP nicht eindeutig zu lesen [U]; der FW-Kommentar sagt „VTRC“, die
`ENABLE_INT`-Folge des 8275 wird nie gesendet ⇒ eher **VRTC** [U] — für P20a: Bildende-Ereignis einmal je Bild.

## 3. Speicherkarte und E/A (alle Zugriffe über den externen Bus P0/P1)

Z8: `LDC` = Programmspeicher (/DM high), `LDE` = Datenspeicher (/DM = P34 low). Unterhalb 1000H liegt der
Programmspeicher am eigenen Bus (EPROM); Zugriffe ≥ 1000H gehen über P0/P1 nach außen [A: Z8612-Architektur].

| Adresse | Art | Funktion | Grad / Quelle |
|---|---|---|---|
| 0000–0FFF | LDC/Holen | EPROM 2732 | [G] |
| 1000–177F | LDE | **Bildwiederholspeicher**, 24 × 80 B = 1920 B | [G] FW `BRAM := %1000` |
| 1780–1797 | LDE | **Zeilenadresstabelle** (24 B, codiert) | [G] FW `ZADR_TAB` |
| 1798–17FF | LDE | frei im 2-KB-RAM | [A] |
| 1C00 | LDE | 8275 Parameter (A0 = 0) | [G] FW `CRT_PAR` |
| 1C01 | LDE | 8275 Kommando schreiben / Status lesen (A0 = 1) | [G] FW `CRT_COM` |
| 2000 (nur A15–A13 = 001) | LDC Schreiben, Scheinausgabe | **Zeichensatz 2** wählen (ZG „Alternativ“ = Umlaute) | [G] FW `ZG1 := %2000`; SLP 74138 1D10 → RS-FF 1D15/2D15 → Z61/Z62 |
| 4000 (010) | LDC Schreiben | **Zeichensatz 1** (ASCII) wählen; beim Start | [G] FW `ZG0 := %4000` |
| 8000 (100) | LDC Schreiben | **Signalton** (Impuls → Piezo SPK1) | [G] FW `BELL_ADR`; SLP Bl. 2 Speaker an 7400/2C11 |
| C000 (110) | LDC Schreiben | **Tastatur-Schieberegister zurücksetzen** (TRES) | [G] FW `TRES := %C0` |

Dekodierung: 1D10 (74138) an P05–P07 = A13–A15, Freigabe über /DS und R/W [G: SLP Bl. 2 rechts]; die Datenspeicher-
seite (RAM/8275) über 2D10 (74138, G1 = P04 = A12) [G: SLP Bl. 3]. Welche Adressbits innerhalb 1000–1FFF
ausgewertet werden (Spiegel), ist im Plan nicht sicher lesbar [U] — die Firmware benutzt nur die genannten Adressen.

### 3.1 Bildwiederholspeicher und Zeilentabelle [G: FW `p8t.init.s` ZADR1, IRP31; `p8t.up.s` CHAR_ADRESSE/ADR_DEC/ROLL]

- Je Bildzeile 80 Byte, physisch ab 1000H fortlaufend. Die **logische Reihenfolge** steht in der Tabelle
  1780H: Eintrag = `(HI & 0FH) | LO` (LO ist immer ein Vielfaches von 10H, weil 80 = 50H); Rückrechnung
  `LO = E & F0H`, `HI = (E & 0FH) | 10H`. **Rollen = Tabelle rotieren**, nicht Speicher verschieben (ROLL/ROLL_DOWN).
- Byte im BWS = Zeichencode 00H–7FH (7 Bit; Host-Zeichen werden mit `AND 7FH` maskiert) oder **Feldattribut
  80H–BFH** (8275-Format `10UR GGBH`: 80H normal, +01H Highlight, +02H Blinken, +10H Invers, +20H Unterstrich).
  Die Firmware schreibt an jedes Zeilenende (Spalte 80 = letzte) 80H („Reset to Standard Video“) [G: FW
  CLEAR_LINE/CLEAR_SCREEN]. 8275 im **nicht-transparenten** Attributbetrieb ⇒ Attribut belegt eine Zelle [G: §4].
- Zeichen 00H–1FH: der ZG enthält dafür Bilder (Programm-Mode `ESC U` schreibt sie unverändert) [G: HB §1.5; FW PUTA].

### 3.2 Zugriff der CPU auf den BWS
Die CPU schreibt/liest nur, wenn kein DMA läuft (Merker STAT0.0 / STAT0.1, `DI` um den Zugriff) [G: FW WRITE_CHAR,
READ_CHAR, LOAD_CURSOR]. Kein Wartezyklus nötig — Arbitrierung rein in Software.

### 3.3 Zeilen-DMA (diskret, Z8 im Tristate) [G: FW IRP31/IRT1; G: SLP Bl. 3 Zähler 5D8/2D8/1D8, Ladeeingänge P10–P17 und P00–P03]
1. 8275 fordert eine Zeile an (DRQ → P31 → IRQ2).
2. IRP31 holt den codierten Eintrag der nächsten Tabellenzeile, rechnet die Adresse aus und führt `LDE r4,@rr2`
   aus — der Lesezyklus legt die Zeilenanfangsadresse auf den Bus, die **drei 74193 (12 Bit) laden sie**.
3. `P01M := 9EH` (P0, P1, /AS, /DS, R/W hochohmig), T1 := 26 (≈ 28 µs) starten, **P35 := 0 = DMA ein**.
4. Die Hardware zählt mit dem Zeichentakt durch die Zeile und schreibt die RAM-Bytes in den 8275 (DACK/WR) —
   80 Zeichen ≈ 35,6 µs bei 2,25 MHz [U: Takt §4.2] (T1 läuft mit 28 µs; Abweichung ungeklärt, s. §9).
5. IRT1: P35 := 1, `P01M := 96H`, IRQ2-Anforderung löschen, Tabellenzeiger weiter (nach 24 Zeilen zurück auf 1780H).
Für die Emulation genügt [A]: bei DRQ die 80 Bytes ab der geladenen Adresse dem 8275 übergeben; Zeitverhalten
nur so weit, dass die Firmware-Merker (STAT0.0) stimmen.

## 4. CRT-Controller 8275 und Video

### 4.1 Programmierung [G: FW RESET_DISPLAY, START_DISPLAY, LOAD_CURSOR; Parameter `RES_DISPL_PAR = [4FH 97H CCH 5AH]`, ABZ bestätigt]

| Byte | Wert | Bedeutung (8275-Datenblatt) |
|---|---|---|
| Reset-Kommando | 00H | dann 4 Parameter an 1C00H |
| P1 | 4FH | normale Zeilen, **80 Zeichen/Zeile** |
| P2 | 97H | **3 Zeilen Vertikalrücklauf**, **24 Zeilen** |
| P3 | CCH | Unterstrich in Rasterzeile 12 (0-basiert), **13 Rasterzeilen je Zeile** |
| P4 | 5AH | Zeilenzähler nicht versetzt, **nicht-transparente Feldattribute**, Cursor **blinkender Unterstrich**, Horizontalrücklauf (10+1)·2 = **22 Zeichentakte** |
| Preset Counters | E0H (zweimal) | |
| Start Display | 20H | Burst-Abstand 0, 1 DMA-Zyklus je Burst |
| Load Cursor | 80H, Spalte, Zeile | bei jeder Cursorbewegung |

Alternative Cursorformen stehen als Konstanten RES4–RES6 in der Quelle (ruhender Block 6AH, blinkender Block 4AH,
ruhender Unterstrich 7AH) — die Typ-1-Varianten P8TCU/P8TCUB/P8TCVB unterscheiden sich laut `TERMINAL.txt` genau
darin [G/A]. Typ 2 5.0: blinkender Unterstrich.

### 4.2 Takte und Bildgeometrie

| Größe | Wert | Grad |
|---|---|---|
| Punkttakt | Quarz 17,998 MHz (Q2, Oszillator 2D12) | [G] STL 25, SLP Bl. 3 |
| Zeichentakt | Punkttakt ÷ N über 74193 3D8 (Ladewert P0–P2 = 1, P3 = 0 ⇒ 7, Borrow → Load) | [G] Beschaltung / **[U] N = 8 oder 7** |
| Zeichenzelle | 8 Punkte breit (ZG-Byte, 74299 3D1 schiebt 8 Bit), 13 Rasterzeilen; Glyphe 7 × 11 in 8 × 12 | [G] ZG-Abzug, 8275-Parameter (löst HB-Widerspruch W1) |
| Zeile | 80 + 22 = 102 Zeichentakte | [A] |
| Bild | (24 + 3) × 13 = 351 Rasterzeilen | [A] |
| bei N = 8 | Zeichentakt 2,250 MHz, Zeilenfrequenz 22,06 kHz, Bildfrequenz **62,8 Hz**, sichtbar 640 × 312 Punkte (24 × 13) | [A]/[U] |
| bei N = 7 | 2,571 MHz, 25,2 kHz, 71,8 Hz | [U] |
| VSYN | **von der Firmware erzeugt**: P36 := 1 bei Bildende-Interrupt, := 0 nach T1 = 192 × 1,085 µs ≈ 208 µs | [G] FW IRP32/IRT1 |
| BSYNC | H- und V-Sync gemischt (8275 HRTC + P36) → XB4/9 | [G] HB §1.3, SLP Bl. 3 |
| VIDEO1/VIDEO2 | zwei Videoausgänge (XB4/7, XB4/6); 74175 D17 latcht RVV/VSP/HLGT/LTEN, 7486 D7 invertiert — VIDEO2 vermutlich Helligkeitsstufe (Highlight) | [G] Pins / **[U] Funktion** |

Für den Rahmenpuffer (P21) reicht [A]: 640 × 312 (bzw. 640 × 288 ohne Rasterzeile 12) — Zeichentakt N nur für
die Zeitführung (Bildende-Interrupt, Watchdog) relevant; Messung am Gerät (§8, Beschaffung D2).

### 4.3 Zeichengeneratoren [G: SLP Bl. 3 1D21/2D21 2716: A0–A3 = LC0–LC3, A4–A10 = CC0–CC6; ABZ geprüft]
- 2716 = 128 Zeichen × 16 Byte; Byte n = Rasterzeile n, **Bit 7 = linker Punkt**; Rasterzeilen 0–11 belegt,
  12–15 leer. Probe: `P8TEZS` 41H „A“, 5BH „[“; `P8TDZS` 5BH „Ä“ [G: Abzug ausgewertet].
- Auswahl ZG1/ZG2 über RS-Flipflop aus 1D10 (Schreibzugriff 4000H/2000H) → Chipselect Z61/Z62 [G].
- **Zuordnung für Typ 2 [G: `TERMINAL.txt`]:** `P8TEZS` = „Typ2 Terminal – U2616D39 PROM englischer Zeichensatz“,
  `P8TDZS` = deutscher Satz (Umlaute für `[ \ ] { | } ~`). FW: 4000H = „ADM31-ASCII“, 2000H = „Alternativ (Umlaute)“
  [G]. Welcher Abzug in welcher Fassung (Z61 vs. Z62) steckt, ist nicht belegt [U] — naheliegend Z61 = `P8TEZS`.
- `P8T_1_6.0_ZG1/ZG2` sind 4 KB (2732) und passen **nicht** in die 2716-Fassungen von Typ 2 [A].
- Attribute: Blinken/Invers/Unterstrich/Highlight macht der 8275 (LA0/LA1, RVV, VSP, LTEN, HLGT); Cursor ebenfalls
  [G: 8275-Datenblatt; SLP Pins verdrahtet].

## 5. Firmware 5.0

### 5.1 Abgleich Abzug ↔ Quelle [G]
- `P8T_1_5.0`: Prüfsumme (16-Bit-Summe) **BBE7H**, SHA-256 `11283bf8dd558503…`; `TERMINAL.txt` nennt keine Prüfsumme.
- Kennung ABZ 000EH: `(C) ZFT/KEAW 5.0 Dec88` = FW `MESSAGE` ✓.
- Stichproben bitgleich: Initialisierung 0024H–009FH, alle sechs ISR, Tastaturtabellen `NORMAL_Tab` (ABZ 056CH, 89 B)
  und `SHIFT_Tab` (ABZ 05C5H, 54 B), Meldungstexte.
- **Nicht** gebaut: die Quelle verlangt den WEGA-Assembler `u8as` (`makefile`), der lokal nicht läuft. Ein voller
  Bitvergleich bräuchte einen Z8-Assembler — für P20 nicht nötig, weil der Abzug selbst verwendet wird.
- Belegt bis 0F6DH, Rest FFH.

### 5.2 Ablauf [G: FW]
- **Init (`INITIAL`)**: Ports/Timer (T0 = 2, PRE0 = 3 ⇒ 7 372 800 / (128·3·2) = **9600 Bd**; T1 einmalig), Register
  06H–7FH löschen, Zeilentabelle aufbauen, Empfangspuffer, `on_line`, ADM31-Modus, ZG1 wählen, Tastatur-SR
  zurücksetzen, 8275 initialisieren → `MAIN`.
- **MAIN**: Bild löschen, Cursor, 8275 starten, `EI`, Schein-Sendebyte; Einschaltmeldung
  `ADM31/9600 baud/Video Attr. on (c)zft/keaw` (bzw. VT100…/off); **wartet auf das erste Tastaturbyte**
  (STAT3.4 „Eigentest“) und zeigt bei FC „Error Tastatur“ [G: TOUT10]. ⇒ Ohne Tastatur/AA bleibt die Meldung
  unvollständig; das Modell muss beim Einschalten **AA senden**.
- Hauptschleife: Tastaturzeichen vor Hostzeichen; Hostzeichen nur im On-Line-Betrieb.
- **Tasten intern**: F0H ON/OFF, F1H BREAK (sendet 00H mit halbierter Baudrate = Dauer-Null), F2H SI/SO (ZG-Umschaltung,
  in TGETCHAR), F3H MODE (ADM31↔VT100, Neuinitialisierung), F4H VIDEO; Codes ≥ 80H → `ESC_OUT_SEQ` (Funktionstasten,
  Tabelle in `tastatur_k7673.md` §4).
- **Escape-Parser**: `PUTA` → `ESC_FLG` → `ESC_SEQUENZ` (ADM31, `p8t.esc.s`) bzw. `ESC_VT100` (`p8t.vt100.s`);
  DSR `ESC [ 6 n` → `CPR` (Cursor Position Report) [G: vt100.s Z. 323].
- **Senden** (`SIO_OUT`): gepollt über IRQ-Bit 4, 00H wird nie gesendet. **XON/XOFF**: DC3 bei 36 Zeichen im
  Empfangspuffer (SBUFL − 9), DC1 wenn leer; Überlauf bei 45 setzt Fehlermerker, danach Zeichen verworfen bis leer.
- Empfang: **8 Datenbits, Bit 7 wird gelöscht**, 00H und 7FH verworfen [G: IRP30] (P15 hat das im Kern-Terminal schon nachgebildet).

### 5.3 Unterschiede 5.0 ↔ 6.0 [G: Disassemblat 6.0 und Zeichenketten; A: Folgerung]
| | 5.0 (Typ 2) | 6.0 („WDOS Terminal“) |
|---|---|---|
| BWS / Tabelle | 1000H / 1780H | **3000H / 3E00H** |
| Zeilen | 24, 13 Rasterzeilen (P3 = CCH) | **24 oder 25**, 12 Rasterzeilen (P3 = 6BH), Cursor ruhender Block |
| Steuerzugriffe | `LDC` 4000/2000/8000/C000 | `LDE` 4000 bzw. E000, 1000, 8000, C000 |
| Baudrate | fest 9600 | wählbar 19200…150 (Texttabelle), Register 27H |
| Meldung | `ADM31/9600 baud/Video Attr. on` | `ADM-31/…/ASCII` bzw. `/IBM`, `/DTSCH`, `/VA_off`, `/CU_off`, `/Word`, `v.: 6.0`, `/KB_err` |
| Zeichengeneratoren | 2716 (2 KB) | 2732 (4 KB, `_ZG1/_ZG2`) |
| Tastatur | XT-Scancodes | XT-Scancodes (gleiche ISR-Struktur, beide Umschalttasten 2AH/36H) |
6.0 läuft auf einer **anderen/umgebauten Speicherkarte** (größerer RAM, 4-KB-ZG); auf Typ-2-Hardware nach SLP
nicht ohne Weiteres lauffähig [A]. „WDOS“: Bezug zu `notes/books/WDOS_Erweiterungsmodul` auf pofo.de [U].
**Empfehlung:** P20 baut 5.0 nach; 6.0 nur als späteres Zusatzmodell, falls der Anwender die Hardware dazu kennt.

## 6. Serielle Schnittstelle zum Rechner (XB5 „COMPUTER“)

| Punkt | Befund | Grad |
|---|---|---|
| Baustein | UART des Z8: Empfang P30, Senden P37 | [G] FW P3M = 51H, SLP Bl. 2 |
| Format | **9600 Bd, 8 Datenbits, keine Parität**; Z8-UART sendet 1 Start + 8 Daten + **2 Stoppbits** (Z8-Eigenschaft), empfängt mit 1 Stoppbit | [G] HB „8N2“ / [A] Z8-Datenblatt |
| Baudrate | aus T0 (fest 9600, Register BDR = 2); BREAK = T0 verdoppelt, ein 00H gesendet | [G] FW |
| V.24 | TxD über 3D12/TL084 D16, RxD über SN75154 D14; DTR = fest aktiv bei eingeschaltetem Terminal | [G] HB, SLP Bl. 2 |
| IFSS | Optokoppler CNY17 (1D20, 2D20), Stromquellen Q+ 12/16 | [G] SLP, STL 21 |
| Umschaltung | automatisch über XB5/9 (Brücke 7–9 im IFSS-Stecker), 1D4 7402 wählt die Empfangsquelle für P30 | [G] HB / SLP |
| Flusssteuerung | XON/XOFF (Terminal → Host); kein Hardware-Handshake in der FW | [G] |
| Brücken | Bild 4.5-3: Wickelbrücken 7–12, 15 „vom Anwender nicht zu verändern“; SLP: Brückenfeld 1–3, 19–21 (Tastatur-Polarität), 22–33 (Tastatur-Bitlage), 7–10 (IFSS) | [G] / Stellung **[U]** |
Für die Emulation: Zeichenstrom 9600 Bd 8N1/8N2 am `SerialAnschluss`; IFSS/V.24 ohne Belang.

## 7. Tastaturschnittstelle (XB1) [G: HB Tab. 4.5-3; SLP Bl. 2; FW IRP33]
- XB1: 1 +5 V, 2 Tastaturtakt, 4 Tastaturdaten, 5 Masse (Pins 3 und 6–9 frei). Im SLP als DB9 gezeichnet [G].
- Takt und Daten laufen über Schmitt-Trigger D9 und XOR 7486 D7 (Polarität per Brücke 1–3/19–21) auf zwei
  kaskadierte **74LS299 (2D1, 1D1)**: Daten → SR, Takt → CLK. 1D1-Ausgänge → P20–P27; Bitlage für das
  „Byte-voll“-Signal → P33 über Brückenfeld 22–33 und D3/D9 [G Verdrahtung, U Brückenstellung].
- Firmware: `LD r1,P2 ; RR r1` — P2 enthält das Byte **um eine Stelle nach links gedreht** (P2.0 = Bit 7,
  P2.1 = Bit 0 … P2.7 = Bit 6) [G Befehl, A Deutung]. Danach Schreibzugriff C000H setzt das Register zurück.
- Ein Rückkanal Terminal → Tastatur (LEDs, Befehle) existiert in 5.0 **nicht** [G: FW sendet nichts an die Tastatur].
- **Modellvorschlag P20a [A]:** Tastatur liefert fertige Bytes; das Terminalmodell stellt `P2 = ROL(byte)` bereit,
  löst eine fallende Flanke an P33 aus und sperrt bis zum C000H-Zugriff (danach nächstes Byte). Die echte
  Bitübertragung (9 Takte: Startbit + 8 Daten, LSB zuerst) muss nicht nachgebildet werden.

## 8. Sonstiges
- **Piezo**: Schreibzugriff 8000H → Impuls (7400/2C11) → Piezophon SPK1 [G]; Tondauer/Frequenz [U].
- **Watchdog**: P36 (VSYN) → 74123 D5 → /RESET des Z8 [G: SLP Bl. 3 „74123 … /RESET“; HB §1.3]. Zeitkonstante
  (R/C) [U]. Emulation: Reset, wenn VSYN länger als z. B. 100 ms ausbleibt — nur nötig, wenn man Abstürze nachbilden will.
- **XB6** (EFS39, 39-polig, „EMR“): Anschluss für den In-Circuit-Emulator EMR an die Z8-Pins [G Stecker / U Zweck].
- **XS1/XS2/XS3**: Stromversorgungs-/Tastatur-Alternativstecker (XS1 = Tastatur Typ 1-Belegung) [G SLP / U].

## 9. Offene Fragen / Widersprüche
1. **Zeichentakt-Teiler N** (7 oder 8) ⇒ Bildfrequenz 62,8 vs. 71,8 Hz; Messung.
2. T1-DMA-Fenster 28 µs < 80 Zeichen × 0,44 µs = 35,6 µs bei N = 8 — entweder N = 7 (2,571 MHz ⇒ 31 µs, passt
   auch nicht) oder der 8275 bekommt die Zeile in Bursts, die über das Fenster hinausreichen. Für die Emulation
   unerheblich (Zeile wird als Ganzes übergeben), für die Zeitführung beim Nachweis „Firmware läuft stabil“ zu beachten.
3. P32 an 8275-IRQ oder VRTC (§2.1).
4. Funktion VIDEO2 (Highlight-Stufe?).
5. Brückenstellungen 1–33 im Auslieferzustand (Foto).
6. Welcher ZG-Abzug steckt in Z61/Z62 des Anwendergeräts.
7. HB nennt „UB 8840 M“, FW-Kopf „U 882“ — die Quelle stammt aus Typ-1-Zeiten; für den Nachbau gleichgültig
   (gleicher Befehlssatz, gleiche Peripherie).

## 10. Folgerungen für P19b/P20 [A]
- Neuer Primitivbaustein **`Z8`** (UB8820/8840-Kern: 256 Register, Ports P0–P3 mit P01M/P2M/P3M, Zähler T0/T1 mit
  Vorteilern, UART, 6 Interrupts mit IPR/IMR/IRQ, LDC/LDE mit externem Bus, Tristate über P01M). Befehlssatz
  klein (≈ 43 Befehle, 1 Byte Opcode); Vorlagen: Zilog Z8 Technical Manual, MAME `src/devices/cpu/z8/`
  (nur Gegenprobe), `tools/z8_disasm.py` (Befehlsmatrix schon vorhanden). Auch für K7672/K7673 nutzbar.
- Neuer Primitivbaustein **`I8275`**: existiert bereits (`core/primitives/i8275`, PC 1715) — auf Feldattribute im
  nicht-transparenten Modus, Cursorformen, Light-Pen-frei prüfen.
- Karte **„P8000-Terminalrechner Typ 2“**: Z8 + 2732 + 2 KB BWS + DMA-Zähler (Verhaltensmodell) + 8275 + 2 ZG +
  Dekoder (Bell/ZG/TRES) + Tastatur-Schieberegister (Byte-Modell) + UART-Anschluss an `SerialAnschluss`.
- Abnahme P20a: Einschaltmeldung `ADM31/9600 baud/Video Attr. on (c)zft/keaw` nach AA von der Tastatur.

## 11. Beschaffungsliste für den Anwender (nach Priorität)

| Nr. | Was | Wozu | Wie |
|---|---|---|---|
| **B1** | Welches Terminal steht beim Anwender? Aufkleber auf dem Firmware-EPROM (D18) und auf den ZG-EPROMs (Z61/Z62), Typ-Schild der Tastatur | Entscheidet, ob Typ 2 + 5.0 + K7673.09 überhaupt das Zielmodell ist (`quellen.md` Frage 6) | Foto der Platine von oben (ganz und Ausschnitte EPROMs/Brückenfelder) |
| **B2** | Abzüge der eigenen EPROMs: Terminal-Firmware, beide ZG, Tastatur | Vergleich mit `P8T_1_5.0`/`P8TEZS`/`P8TDZS`/`K7673.09` (SHA-256 in `doc/p8000/eproms/SHA256SUMS.txt`) | EPROM-Leser; ZG = 2716, Firmware = 2732 |
| **B3** | Foto der **Tastatur K7673.09** von oben (Kappenbeschriftung) und der Platine innen (Prozessor-Aufdruck, Quarz-Aufdruck) | Layout für die Bildschirmtastatur (P21), Quarz ⇒ Wiederholzeiten | gut ausgeleuchtet, Kappen lesbar |
| **B4** | Frequenzen messen: Bildfrequenz an XB4/9 (BSYNC) oder am Monitor-Menü, Zeilenfrequenz, Zeichentakt (8275 CCLK, Pin 30) | Teiler N (7/8), Bildwiederholrate | Frequenzzähler/Oszilloskop; ersatzweise Handy-Kamera gegen den Bildschirm ist zu ungenau |
| **B5** | Tastatur-Leitung XB1/2 (Takt) und XB1/4 (Daten) beim Druck einer Taste (z. B. „a“) | Bittakt, Pegel (invertiert?), Pause zwischen Make und Break | Oszilloskop, ein Zweikanal-Bild genügt; Masse XB1/5 |
| **B6** | Wiederholung: Taste halten, Zeit bis zum ersten Wiederholzeichen und Wiederholrate | Tick-Länge (§2.3 Tastatur) | am laufenden WEGA `cat > /dev/null` bzw. Stoppuhr über 10 s mitzählen |
| **B7** | Stellung der Brückenfelder 1–3, 19–21, 22–33 (Tastatur) und 7–12, 15 (Bild 4.5-3) | Bitlage/Polarität der Tastatur-Schieberegister | Foto |
| **B8** | Bildschirmfoto der Einschaltmeldung, des Zeichensatzes im Programm-Mode (`ESC U`, dann alle Codes 00–7F senden), von Attributen (`ESC G 2/4/…`) und vom Cursor | Abnahmebilder für P20a/P21, Highlight/VIDEO2-Wirkung | Foto frontal |
| **B9** | Verhalten bei Tastatur ab: kommt „Error Tastatur“ oder bleibt die Meldung stehen? | Bestätigt §5.2 (Warten auf AA) | Terminal ohne Tastatur einschalten |

## 12. Aufwand und Risiko der Umsetzung (P19b–P21) [A]

| Teil | Aufwand | Risiko |
|---|---|---|
| **Z8-Kern** (neu, Primitive + Matrix-/Gegenprobentests nach Grundsatz §10.11a; Orakel MAME `z8`) | **M–L** (Befehlssatz klein, aber Ports/Zähler/UART/Interrupt-Logik und externer Bus mit Tristate-Kunstgriff) | mittel: Zeitverhalten Zähler/UART (Prescaler, Modulo-n, Single-Pass), IRQ-Priorität (IPR), Flanken an P30–P33 |
| 8275 | **S** — vorhanden (PC 1715); Feldattribute nicht-transparent, Cursor blinkender Unterstrich, 13 Rasterzeilen prüfen | gering |
| Karte Terminalrechner (Speicherkarte, Dekoder, DMA-Verhaltensmodell, ZG, Bell, Watchdog) | **M** | mittel: Zeitführung DMA ↔ T1-Fenster (§9 Nr. 2); falscher Bildende-Takt bringt VSYN/Cursorblinken durcheinander |
| Tastatur K7673.09 als Verhaltensmodell | **S** | gering (Firmware vollständig gelesen); Layout/Kappen fehlen (B3) |
| Rahmenpuffer 640 × 312, ZG aus Abzug, Attribute | **S–M** | gering; VIDEO2-Bedeutung [U] |
| Anbindung SerialAnschluss (tty1 direkt / Hub) | **S** — Zeichenebene, 9600 Bd | gering; 2 Stoppbits beim Senden beachten (Gast-SIO tolerant) |
| Zeitführung in `P8000Machine` (vierter Prozessor neben U880/U8001/WDC) | **S–M** | gering: Terminal ist nur über die serielle Leitung gekoppelt ⇒ lose Kopplung, eigene Uhr wie WDC (`laufeBis`) |
| Mehrinstanz „P8000 Terminal“ (eigenes Programm) | **M** (Oberfläche) | gering |
Gesamt: der dominierende neue Baustein ist der **Z8-Kern**; ohne ihn keine Original-Firmware. Rest ist Kleinarbeit
auf vorhandenen Mustern. Größte Unsicherheiten: Zeichentakt (B4) und Tastenkappen (B3) — beide blockieren die
Umsetzung nicht (Vorgaben N = 8, Tastenbild aus der Codetabelle), sind aber für „authentisch“ nötig.
