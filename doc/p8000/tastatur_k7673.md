# Flachtastatur K7673.09 am P8000-Terminal Typ 2 (Befund AP P19a)

Stand 2026-10-07. Gegenstück: `doc/p8000/terminal_typ2.md`. Grade wie dort: **[G]** gelesen, **[A]** abgeleitet,
**[U]** unsicher.

Quellen: Abzug `doc/p8000/eproms/KEYBOARD/K7673.09` (2048 B, SHA-256 `0b0f319b07e0e1c7…`, 16-Bit-Summe 7CDEH;
`KEYBOARD.txt` nennt keine Prüfsumme), Disassemblat mit `tools/z8_disasm.py` (Code 000C–02E2H, Tabellen
02E3–03F3H, Rest FFH); Terminal-Firmware 5.0 (`p8t.init.s` IRP33, `p8t.main.s` TGETCHAR/Tabellen, `p8t.esc.s`
ESC_OUT_SEQ); Hardwarehandbuch Kap. 4 (XB1); robotrontechnik.de „Tastaturen“
(<https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm>). **Kein Schaltplan der K7673** gefunden.

---

## 1. Kurzfassung
- **Prozessor: Z8** (Befehlssatz und Registerbelegung eindeutig: `SRP #F0`, P01M/P2M/P3M, IMR, T1/PRE1) [G].
  Programm in einem **2716** (2 KB), nur 1 KB benutzt [G]. Bauform: Z8 mit externem Programmspeicher, d. h.
  UB 8820 M/8821 M-Klasse (64-polig, eigener Programmbus — sonst wären P0/P1 nicht als Matrixeingänge frei) [A].
  Quarz **unbekannt** [U].
- **Protokoll = IBM-XT-artig, Scancode-Satz 1**: Make-Code beim Drücken, Code | 80H beim Loslassen, Vorbyte E0
  für Zusatztasten, E1-Folgen, **AA nach dem Einschalten**, FF bei Pufferüberlauf, Wiederholung = Make-Code erneut
  [G: Firmware]. Übertragung **seriell Takt + Daten, nur Tastatur → Terminal**, 9 Takte (Startbit + 8 Daten, LSB
  zuerst) [G]. Kein Rückkanal [G].
- **Belegung ist robotron-eigen** (nicht IBM): z. B. 1DH = TAB, 38H = CTRL, 3BH–3DH = SI/SO, MODE, VIDEO, 3EH =
  BREAK, 54H = ON/OFF, 00H = „+“ [G: Terminaltabelle × Tastaturtabelle].
- **Verwandtschaft:** K801.02 (GLE/Typ 1) hat denselben Programmanfang und dieselbe Struktur (Tabelle an anderer
  Stelle) [G: Abzugvergleich]; robotrontechnik: „Die einzig erkennbaren Unterschiede zwischen der K801.02 und der
  K7673.09 scheinen im Inhalt des EPROMs und im Tastaturlayout zu stecken“, „Ein Austausch mit der K801.2 ist …
  möglich“ [G Web]. K7673.01 = gleiches Protokoll, zusätzlich RAM-Selbsttest und LED-Test beim Start [G]. Zur
  **K7672** (K8915): gleiche Z8-Familie, aber dort Hardware-UART 9600 Bd mit ESC-Befehlen und Zeichen-/Scancode-
  Modus (`doc/EPROMS/K7672/README.md`); die K7673.09 hat keinen UART (`P3M = 01H`) und keine Befehle [G].
- **Empfehlung (§6): Verhaltensmodell, datengetrieben aus dem Abzug** — nicht die Firmware auf einem Z8 laufen lassen.

## 2. Hardware aus der Firmware erschlossen [G: Init 000CH–001CH, Schleife 004AH–0069H, Senden 0240H–029BH]

| Z8 | Einstellung | Belegung |
|---|---|---|
| P0, P1 | `P01M = 4DH`: Eingänge, Stapel intern | **16 Matrixspalten** (Ruhe = 1, gedrückt = 0; Firmware `COM`) |
| P2 | `P2M = 00H`: Ausgänge, Start `P2 = 8FH` | P2.0–P2.2 = **Zeilenauswahl 0–7** (Dekoder, Schleife `INC P2`); P2.4/P2.5/P2.6 = **LEDs** (Umschalten per `XOR P2`); P2.3, P2.7 fest 1 [U Zweck] |
| P3.0 | Eingang | **Sendefreigabe** vom Terminal: vor jedem Byte bis zu 15 Abfragen auf 0, sonst später erneut [G Code / U Bedeutung] |
| P3.1 | Eingang | 1 ⇒ **Neustart** (`JP 001FH`), auch mitten im Senden geprüft [G Code / U Herkunft: Reset/Brücke] |
| P3.6 | Ausgang | **Takt**: Impuls „0“ (Schleife 20) dann „1“ (Schleife 4) je Bit [G] |
| P3.7 | Ausgang | **Daten, invertiert** am Pin: Startbit Pin = 0, Datenbits = NOT bit, LSB zuerst; Ruhe P3 = 80H [G] |
| T1 | PRE1 = A3H (÷40, intern, fortlaufend), T1 = FAH (250), IMR = 20H | Zeitbasis der Wiederholung; Tick = 8·40·250 / f_Quarz [A] |

Pinzuordnung XB1 (Terminalseite) [G: HB Tab. 4.5-3]: 1 +5 V, 2 Takt, 4 Daten, 5 Masse. Ob zwischen Z8-Pin und
Kabel Treiber (invertierend) liegen, ist unbekannt [U]; das Terminal hat eine Polaritätsbrücke (XOR 7486).

### 2.1 Matrixabtastung, Entprellung, Mehrfachtasten [G: 0048H–0115H, 02C9H]
1. 8 Zeilen × (P0, P1) einlesen → 16 Byte (3BH–4AH), zwischen den Zeilen kurze Warteschleife und Sendeversuch
   aus dem Puffer.
2. Zweite Abtastung gleich der ersten? Sonst von vorn. **40 aufeinanderfolgende gleiche Abtastungen** (38H bis 28H)
   gelten als stabil.
3. Mehr als **3 gedrückte Tasten** ⇒ Abtastung verworfen (Schutz gegen Phantomtasten).
4. Vergleich mit dem letzten gültigen Stand (4BH–5AH): neu gedrückt ⇒ Make, losgelassen ⇒ Break.

### 2.2 Code-Erzeugung [G: 0131H, 0146H–0208H]
- Tabelle 02E3H, je Tastenposition 2 Byte `[Merker, Code]`, Index = Zeile·16 + Port·8 + Bit (Port 0 = P0, 1 = P1).
- Merker Bit 0 = vorher **E0** senden; Merker Bit 3 = **Folge**: Länge = Merker>>4, Adresse = (Merker & 7)·256 + Code.
- Break = Code | 80H (bei E0-Tasten: E0, dann Code | 80H).  **Berichtigt (P20b, Differenzialtest):** Folgen werden auch
  beim Loslassen gesendet, jedes Byte mit Bit 7 (die „00"-Taste: `E1 D2 E1 D2`); nur PAUSE (02FFH) sendet beim Loslassen nichts.
- **Sonderfälle**: Taste mit Code 1DH (Tabellenadresse 02EFH; bei IBM Strg, im Terminal 5.0 aber **TAB**) gedrückt merken; PAUSE (02FFH) bei gedrückter 1DH-Taste
  sendet stattdessen den Eintrag 03EDH (Folge `E0 46 E0 C6`). PAUSE wird nicht wiederholt.
- **Rastende Tasten mit LED** (Make toggelt die LED, nur einmal je Druck): Code 54H (ON/OFF) → P2.4, 3AH (CAPS LOCK)
  → P2.5, 3CH (MODE) → P2.6. Die Codes selbst werden normal gesendet; den Zustand führt das **Terminal** getrennt
  (LED und Terminalzustand können auseinanderlaufen, z. B. nach Terminal-Neustart) [A]. Keine LED für SI/SO,
  obwohl das Handbuch sie für <SI/SO> nennt [G HB §1.5] — Widerspruch, evtl. nur an anderen Tastaturen [U].
- Ausgabepuffer 24H–34H (16 B); voll ⇒ FFH als letzter Eintrag (Überlauf) [G 0209H].
- Einschalten: Register löschen, **AA** in den Puffer [G 0043H].

### 2.3 Wiederholung [G: 00A7H–00C9H, IRQ5 0117H]
- Nur die **zuletzt gedrückte** Taste (20H/21H = ihre Tabellenadresse); Loslassen dieser Taste beendet.
- IRQ5 zählt 0DH bis **50** (Verzögerung) und 0CH bis **10** (Abstand); bei Gleichstand wird der Make-Code (samt E0)
  erneut gesendet und 0CH gelöscht. Jede neue Taste löscht 0DH.
- In Zeit: Verzögerung = 50 Ticks, Abstand = 10 Ticks, Tick = 80 000 / f_Quarz. **Bei 8 MHz** wären das 10 ms ⇒
  500 ms / 100 ms (10 Zeichen/s) [U: Quarz unbelegt — Vorgabe für das Modell, am Gerät messen].
- Die Terminal-Firmware 5.0 wiederholt nicht selbst [G].

## 3. Übertragung Tastatur → Terminal [G Tastatur 0240H; G/A Terminal IRP33]
```
P3.0 prüfen (0 = frei) ─ Startbit: P3.7 = 0, Takt-Impuls ─ 8 × (P3.7 = NOT bit, LSB zuerst; Takt-Impuls) ─ P3 = 80H
```
Im Terminal schieben zwei 74LS299 die Bits ein, das volle Byte löst P33 aus, die Firmware liest P2 (um 1 gedreht)
und setzt das Register per Zugriff auf C000H zurück. Ein zweites Byte vor dem Rücksetzen wird vermutlich über
P3.0 („frei“) zurückgehalten [U: Verdrahtung P3.0 ↔ Terminal nicht belegt].

## 4. Tastenbelegung (Matrix → Code → Bedeutung im Terminal 5.0)

Spalte „Terminal“: Zeichen ohne / mit SHIFT nach `NORMAL_Tab`/`SHIFT_Tab` (ABZ 056CH/05C5H); Steuertasten nach
`MAIN`/`TGETCHAR`/`ESC_OUT_SEQ` (ADM31-Folge / VT100-Folge). „—“ = Code 80H = ohne Wirkung (F-Tasten F1–F10,
Taste 55H, 57H, 58H). CTRL macht aus `@`…`DEL` die Steuerzeichen (AND 9FH), CAPS LOCK wirkt nur auf Buchstaben.
Im Zeichensatz 2 (SI/SO) werden `[ \ ]` ↔ `{ | }` getauscht (+/−20H), damit Ä Ö Ü / ä ö ü auf den Umlauttasten
liegen. **Die Tastenkappen-Beschriftung kennt keine Quelle**; die Bedeutungen unten sind das, was das Terminal
daraus macht. Die K7673.09 sendet für alle drei SHIFT-Positionen 2AH; 36H (in 5.0 ohne SHIFT-Wirkung, `NORMAL_Tab[36H] = 80H`)
kommt nicht vor [G].

| Zeile | Pin | Code(s) | Terminal 5.0 |
|---|---|---|---|
| 0 | P0.0 | 02 | `1` / `!` |
| 0 | P0.1 | 04 | `3` / `@` |
| 0 | P0.2 | 06 | `5` / `%` |
| 0 | P0.3 | 08 | `7` / `/` |
| 0 | P0.4 | 0A | `9` / `)` |
| 0 | P0.5 | 0C | `~` / `?` |
| 0 | P0.6 | 1D | HT |
| 0 | P0.7 | 4A | `-` |
| 0 | P1.0 | E0 4D | Cursor rechts (ADM31 FF, VT100 ESC[C) |
| 0 | P1.1 | E0 52 | CHAR DELETE (ESC W / ESC[P) |
| 0 | P1.2 | E0 49 | CHAR INSERT (ESC Q / ESC[@) |
| 0 | P1.3 | E0 35 | / |
| 0 | P1.6 | Folge E1 1D 45 E1 9D C5 | PAUSE; mit CTRL davor stattdessen E0 46 E0 C6 (BREAK-Folge) — im Terminal 5.0 ohne Wirkung (C5H) |
| 1 | P0.0 | 10 | `q` / `Q` |
| 1 | P0.1 | 12 | `e` / `E` |
| 1 | P0.2 | 14 | `t` / `T` |
| 1 | P0.3 | 16 | `u` / `U` |
| 1 | P0.4 | 18 | `o` / `O` |
| 1 | P0.5 | 1A | `]` / `}` |
| 1 | P0.6 | 2A | SHIFT |
| 1 | P1.0 | E0 50 | Cursor ab (LF / ESC[B) |
| 1 | P1.1 | E0 53 | LINE DELETE (ESC R / ESC[M) |
| 1 | P1.2 | E0 51 | LINE INSERT (ESC E / ESC[L) |
| 1 | P1.3 | 48 | `8` |
| 1 | P1.6 | 57 | — |
| 1 | P1.7 | 58 | — |
| 2 | P0.0 | 1E | `a` / `A` |
| 2 | P0.1 | 20 | `d` / `D` |
| 2 | P0.2 | 22 | `g` / `G` |
| 2 | P0.3 | 24 | `j` / `J` |
| 2 | P0.4 | 26 | `l` / `L` |
| 2 | P0.5 | 28 | `[` / `{` |
| 2 | P0.6 | 3A | CAPS LOCK |
| 2 | P0.7 | 4E | `=` |
| 2 | P1.0 | E0 4B | Cursor links (BS / ESC[D) |
| 2 | P1.3 | 4C | `5` |
| 2 | P1.6 | 42 | — |
| 3 | P0.0 | 2C | `y` / `Y` |
| 3 | P0.1 | 2E | `c` / `C` |
| 3 | P0.2 | 30 | `b` / `B` |
| 3 | P0.3 | 32 | `m` / `M` |
| 3 | P0.4 | 34 | `.` / `:` |
| 3 | P0.6 | 2A | SHIFT |
| 3 | P0.7 | E0 1C | Enter → CR |
| 3 | P1.0 | 1C | CR |
| 3 | P1.3 | 50 | `2` |
| 3 | P1.6 | 3E | BREAK |
| 3 | P1.7 | 3F | — |
| 4 | P0.0 | 03 | `2` / `"` |
| 4 | P0.1 | 05 | `4` / `$` |
| 4 | P0.2 | 07 | `6` / `&` |
| 4 | P0.3 | 09 | `8` / `(` |
| 4 | P0.4 | 0B | `0` / `=` |
| 4 | P0.5 | 0D | `'` / ``` |
| 4 | P0.6 | 56 | `<` |
| 4 | P0.7 | 0E | DEL |
| 4 | P1.0 | 52 | `0` |
| 4 | P1.1 | E0 47 | PAGE ERASE (ESC Y / ESC[J) |
| 4 | P1.2 | 45 | BS |
| 4 | P1.3 | 37 | `*` |
| 4 | P1.4 | 49 | `9` |
| 4 | P1.6 | E0 37 | ohne Wirkung (B7H) |
| 4 | P1.7 | 46 | — |
| 5 | P0.0 | 11 | `w` / `W` |
| 5 | P0.1 | 13 | `r` / `R` |
| 5 | P0.2 | 15 | `z` / `Z` |
| 5 | P0.3 | 17 | `i` / `I` |
| 5 | P0.4 | 19 | `p` / `P` |
| 5 | P0.5 | 1B | `+` / `*` |
| 5 | P0.6 | 01 | ESC |
| 5 | P0.7 | 0F | BACKTAB (ESC I / ESC[Z) |
| 5 | P1.0 | Folge E1 52 E1 52 | „00“-Taste (Terminal: 52 & EFH \| 80H = C2H) |
| 5 | P1.1 | E0 4F | HOME (RS / ESC[H) |
| 5 | P1.2 | 47 | `7` |
| 5 | P1.3 | 49 | `9` |
| 5 | P1.4 | 3B | SI/SO |
| 5 | P1.6 | 43 | — |
| 5 | P1.7 | 44 | — |
| 6 | P0.0 | 1F | `s` / `S` |
| 6 | P0.1 | 21 | `f` / `F` |
| 6 | P0.2 | 23 | `h` / `H` |
| 6 | P0.3 | 25 | `k` / `K` |
| 6 | P0.4 | 27 | `\` / | |
| 6 | P0.5 | 2B | `#` / `^` |
| 6 | P0.6 | 2A | SHIFT |
| 6 | P0.7 | 29 | BS |
| 6 | P1.0 | 53 | `,` |
| 6 | P1.2 | 4B | `4` |
| 6 | P1.3 | 4D | `6` |
| 6 | P1.5 | 38 | CTRL |
| 6 | P1.6 | 40 | — |
| 6 | P1.7 | 41 | — |
| 7 | P0.0 | 2D | `x` / `X` |
| 7 | P0.1 | 2F | `v` / `V` |
| 7 | P0.2 | 31 | `n` / `N` |
| 7 | P0.3 | 33 | `,` / `;` |
| 7 | P0.4 | 35 | `-` / `_` |
| 7 | P0.7 | 39 | Leertaste |
| 7 | P1.1 | E0 48 | Cursor auf (VT / ESC[A) |
| 7 | P1.2 | 4F | `1` |
| 7 | P1.3 | 51 | `3` |
| 7 | P1.4 | 54 | ON/OFF |
| 7 | P1.5 | E0 38 | ohne Wirkung (B8H) |
| 7 | P1.6 | 3C | MODE |
| 7 | P1.7 | 3D | VIDEO |

105 belegte Positionen von 128; E0 37H und E0 38H sind belegt, haben im Terminal 5.0 aber keine Wirkung.

## 5. Bezug zu anderen Tastaturen
| | K7673.09 | K801.02 | K7673.01 | K7672.03 (K8915) | PC-1715-Tastatur |
|---|---|---|---|---|---|
| Prozessor | Z8, 2716 | Z8, 2716 (gleicher Code-Anfang) | Z8, 2716 | UB 8820 M, 2 × 2716 | eigener U880 (`core/peripherals/tastatur1715/`) |
| Leitung | Takt + Daten, Bit-Bang | Takt + Daten (für Typ GLE/1, Firmware 3.1/4.1) | Takt + Daten (robotrontechnik: „IFSS“ [U]) | UART 9600 Bd | eigen |
| Codes | XT-Satz 1, robotron-Belegung | eigen (Terminal 4.x übersetzt) | XT-Satz 1 | Zeichen oder XT-Scancodes (DCP) | eigen |
| Rückkanal | nein | nein | nein | ESC-Befehle, DC1/DC3, BEL | — |
| Einsatz | P8000-Terminal Typ 2 | GLE/Typ 1, auch Typ 2 | Typ 2, erste EC 1834 | K8915 | PC 1715, alternativ am Terminal |
Gemeinsamer Baustein für das Projekt: nur der **Z8** (falls man ihn baut); Modelle teilen sich sonst nichts.

## 6. Entscheid: Firmware auf Z8 oder Verhaltensmodell?

**Empfehlung: Verhaltensmodell, das die Codetabelle zur Laufzeit aus dem Abzug liest** (02E3H, 128 × 2 Byte +
Folgen ab 03E3H). Begründung:
1. Die Firmware ist vollständig verstanden (≈ 700 Byte Code, keine Befehle vom Terminal, kein Rückkanal); es gibt
   kein verborgenes Verhalten, das nur ein emulierter Prozessor zeigte.
2. Der **Quarz ist unbekannt** — die Firmware-Zeiten (Bittakt, Entprellung, Wiederholung) ließen sich auf einem
   emulierten Z8 ohnehin nur raten; das Modell nimmt dieselben Zählerwerte (40 Abtastungen, 50/10 Ticks) mit einem
   einstellbaren Tick (Vorgabe 10 ms, Echtzeit wie `tasten_uhr.h` bei der K7672).
3. Eine Z8-Instanz verlangte eine **Bit-Kopplung** Takt/Daten zwischen zwei Prozessoren und die Matrixnachbildung
   inkl. 40-facher Entprellung — teurer Laufzeit- und Testaufwand für dasselbe Ergebnis.
4. Das Terminal braucht den Z8-Kern trotzdem (`terminal_typ2.md` §10). Sobald er steht, kann eine
   „Firmware-Tastatur“ als Gegenprobe nachgezogen werden (Test: gleiche Bytefolge wie das Modell für eine
   Tastenfolge) — als Wächter, nicht als Vorgabe.

**Modell (Vorschlag für P20b) [A]:**
- Eingabe: physische Taste = Matrixposition (Zeile, Port, Bit) — Bildschirmtastatur nach Muster
  `QK_TASTE_BASE | Matrix` der K7672; Host-Tastatur über eine Zuordnung Qt-Taste → Position.
- Ausgabe: Bytes nach §2.2 (Make/Break, E0, Folgen, PAUSE-Sonderfall, AA beim Einschalten, FF bei Überlauf),
  höchstens 3 gleichzeitig gedrückte Tasten, Wiederholung der zuletzt gedrückten Taste.
- LEDs P2.4/5/6 (ON/OFF, CAPS, MODE) als Zustand für die Oberfläche.
- Übergabe an das Terminal als ganze Bytes (`terminal_typ2.md` §7), Abstand ≥ 1 Byte-Zeit (Vorgabe ≈ 1 ms [U]).

## 6a. Umsetzung (P20b, 2026-10-08)

`core/peripherals/p8000_terminal_hw/tastatur_k7673.{h,cpp}` (`TastaturK7673`), Entwurf 28 §5.  Zeitkonstanten am
Z8-Kern gemessen: Zeile → Zeile 445 Takte, Runde 3564 (+ 90 beim Kopieren des Kandidaten, + 9782 bei der Auswertung),
Byte 5344 (Bit 522: Daten 144 nach der steigenden Flanke, Takt tief 180–522).  Wächter `TastaturK7673Diff.*`:
alle 128 Positionen gleiche Codefolge wie die Firmware (Zeit ± 1,5 ms), Wiederholung, Mehrfachtasten, PAUSE, LEDs.

## 7. Offen
1. Quarzfrequenz, Bittakt, Wiederholzeiten (Messung, Beschaffungsliste in `terminal_typ2.md` §11).
2. Tastenkappen: Foto liegt vor (§8); offen nur die Annahmen dort (F1–F11, CE, „+“, rechtes CTRL, Variante .01/.09).
3. Herkunft P3.0 (Sendefreigabe) und P3.1 (Neustart) — Kabel hat nur Takt und Daten (XB1/2, XB1/4).
4. Ob zwischen Z8-Pin und Leitung invertierende Treiber sitzen (Pegel auf XB1).

## 8. Tastenkappenbeschriftung nach Foto (Anwender, 2026-10-08)

Quelle: `doc/p8000/bilder/tastatur_k7673_foto.jpg`.  Deutsches Tastenfeld (QWERTZ, ISO mit `< >`-Taste, zwei SHIFT `⇕`,
zwei CTRL), Kappen mit DIN-66003-Zweitbelegung (`@ §`, `Ü }`, `Ö |`, `Ä {`, `? ~ ß`).  Matrixpositionen/Scancodes
unverändert (EPROM, Test `term_matrix_scancode`); geändert sind Anordnung, Beschriftung und die Zuordnung der Kappen.
Umsetzung: `app/ui/k7673_layout.py` (`BESCHRIFTUNG`, `BILD`, `LED_FELD`).

Abweichungen gegenüber der früheren Vermutung (Position → Kappe nach Foto):

| Position (Zeile, Spalte) | Code | früher | Foto / Annahme |
|---|---|---|---|
| (6,7) / (4,7) | 29 / 0E | BS unten / DEL Hauptreihe | BS und DEL beide oben rechts in der Zifferreihe |
| (4,10) | 45 | „BS (zweite)" | **CE** des Ziffernblocks (Annahme: Position der IBM-NumLock; Terminal macht BS daraus) |
| (5,8) | E1 52 E1 52 | oben im Ziffernblock | **00** unten im Ziffernblock |
| (1,6) | 2A (.09) | SHIFT links | **+** des Ziffernblocks (Annahme, s. u.) |
| (3,6) / (6,6) | 2A | SHIFT rechts / Mitte | SHIFT links / rechts (`⇕`) |
| (7,13) | E0 38 | ohne Wirkung | **CTRL rechts** (Annahme: Nachbarposition von CTRL links (6,13)) |
| 11 Positionen „ohne Wirkung" + PAUSE | 3F 40 41 42 43 44 46 57 58 E0 37 PAUSE | schmale Leiste | **F1…F11** in dieser Scancode-Reihenfolge (Annahme; ein zwölfter Kandidat fehlt am Foto) |
| (0,9) (0,10) (1,9) (1,10) | E0 52/49/53/51 | CHAR/LINE DEL/INS | `\|←\|` CHAR DELETE, `\|→\|` CHAR INSERT, `⤒` LINE DELETE, `⤓` LINE INSERT (Annahme nach Pfeilrichtung) |
| (4,9) / (5,9) | E0 47 / E0 4F | PAGE ERASE / HOME | **CLEAR** / `↖` |
| (0,6) / (5,7) | 1D / 0F | TAB / BACKTAB | `⇥` links vor Q / `⇤` rechts hinter `+` |
| (4,12) | 49 | zweite 9 | am Foto **keine Taste** (nur als Zusatzfeld unten) |
| LEDs | — | auf den Tasten | Anzeigefeld OFF / CAPS / MOD oben rechts |

**Variante K7673.01 gegen .09** (Vergleich der Codetabellen; .01 hat die Tabelle bei 0322H statt 02E3H, davon
abgesehen und von Programmunterschieden — RAM-Selbsttest, LED-Test — sind nur **vier** Einträge verschieden):
(1,6) .09 = 2A SHIFT, .01 = **00H** (Terminal: „+"); (6,6) .09 = 2A, .01 = **36H** (rechter Shift, im Terminal 5.0
ohne Wirkung, `NORMAL_Tab[36H] = 80H`); (0,14) und (5,8) tragen gleiche Folgen, nur mit anderer Folgenadresse.
Das Foto passt zur Position (1,6) = „+" — das sendet nur die **.01**; unter .09 macht es SHIFT.  Ob die Tastatur des
Anwenders .01 oder .09 trägt, ist offen (EPROM-Aufdruck/Selbsttest nachsehen); für den Emulator wäre .01 als zweite
Variante eine reine Tabellenänderung im Kern.
