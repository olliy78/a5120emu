# Entwurf 28 — Original-P8000-Terminal Typ 2 + Tastatur K7673.09 (AP P19b)

Stand 2026-10-08.  Grundlage: Befund P19a (`doc/p8000/terminal_typ2.md`, `doc/p8000/tastatur_k7673.md`),
Z8-Kern P19c (`core/primitives/z8.h`, `doc/p8000/z8_abdeckung.md`), Plan `doc/design/25_p8000.md` §11.
Umsetzung P20a (Terminalrechner) und P20b (Tastatur); Anbindung an `P8000Machine` und C-ABI sind
**nicht** Teil davon (P20c/P20d).

## 1. Ziel und Grundsatz

Der Terminalrechner Typ 2 wird **mit seiner Original-Firmware `P8T_1_5.0`** auf dem Z8-Kern betrieben.
Das Bild ergibt sich aus **Firmware + 8275-Modell + Zeichengenerator** — nicht aus einer Nachbildung der
Escape-Sequenzen.  Das vereinfachte Kern-Terminal (P6, `core/peripherals/p8000_terminal/`) bleibt als
schlanke Betriebsart erhalten; beide bieten dieselbe Außenschnittstelle (§6), damit die Maschine sie
austauschen kann.  Weicht das Original vom Handbuch ab, **gewinnt das Original** (Differenzialtest §8).

## 2. Klassenschnitt (`core/peripherals/p8000_terminal_hw/`, Bibliothek `k1520_p8000_terminal_hw`)

| Klasse | Datei | Inhalt |
|---|---|---|
| `P8000TerminalHw` | `terminal_hw.{h,cpp}` | Terminalrechner: `Z8` (UB 8840, 7,3728 MHz), EPROM 2732 am Programmbus, 2 KB BWS 1000–17FF, `I8275` an 1C00/1C01 (/DM), Zeilen-DMA (Zähler laden + Übergabe an den 8275), Zeichengenerator 2 × 2716 mit RS-Flipflop (Strobe 4000H/2000H), Klingel (8000H), Tastatur-Schieberegister (2 × 74LS299, Rücksetzen C000H, P2 + P33), Watchdog (74123 an P36), serielle Leitung P30/P37 als Bytestrom, Bildausgabe (Zellen + Pixel) |
| `TastaturK7673` | `tastatur_k7673.{h,cpp}` | Verhaltensmodell der K7673.09: Codetabelle zur Laufzeit aus dem EPROM-Abzug, Abtastung/Entprellung/3-Tasten-Grenze/Wiederholung wie die Firmware, Ausgabe als Pegelfolge Takt + Daten |
| `P8000TerminalEinheit` | `terminal_einheit.{h,cpp}` | eigenständige Einheit = Terminalrechner + Tastatur + Zeitführung; `SerialAnschluss` des Terminals für den Hub („P8000 Terminal" als eigenes Programm) |
| `TerminalHwKopplung` | `terminal_einheit.{h,cpp}` | Gegenstelle zum `SerialAnschluss` eines Rechnerkanals (tty1 direkt, Variante „P8000 + Terminal") |
| `TerminalGeraet` | `core/peripherals/p8000_terminal/terminal_geraet.h` | gemeinsame Außenschnittstelle beider Terminals (§6), mit `KernTerminalGeraet` (P6) und `HwTerminalGeraet` |
| ROM-Daten | `rom_p8t.h`, `rom_k7673.h` | `P8T_1_5.0`, `P8TEZS`, `P8TDZS`, `K7673.09` als C-Felder (`tools/eprom_to_h.py`) |

`P8000Machine` und die C-ABI blieben in P20a/b unberührt; Anbindung s. §9 (P20c/d).

## 3. Zeitführung

- **Uhr der Einheit = interner Z8-Takt** (7,3728 MHz / 2 = 3 686 400 Hz).  Alles im Terminal (8275-Takt,
  Tastatur, serielle Leitung, Watchdog) wird in diese Zeit umgerechnet.
- **Zeichentakt** des 8275 = 17,998 MHz / N, N = `Config::zeichentaktTeiler` (Vorgabe 8 [U, B4]).  Die
  Ereignisse eines Bildes stehen in Zeichentakten fest (Zeile = Zeichen + Horizontalrücklauf, Zeichenzeile =
  Rasterzeilen × Zeile, Bild = (Zeilen + VRTC-Zeilen) × Zeichenzeile — **alle Größen aus den Reset-
  Parametern, die die Firmware dem 8275 gibt**, vor der Programmierung Vorgabewerte 80/22/13/24/3).  Der
  Bildanfang wird mit Ganzzahlbruch fortgeschrieben (Zähler + Rest, Nenner 17 998 000) — **keine Drift**,
  gleiche Technik wie der Bruchtakt der CTC (P5d).
- **Rechnerseite (4 MHz)**: `TerminalHwKopplung::takt(maschinenTakte)` rechnet Maschinentakte mit Rest in
  Z8-Takte um (× 3 686 400 / φ); ebenfalls driftfrei.
- **Tastatur**: eigene Quarzfrequenz `TastaturK7673::Config::quarzHz` (Vorgabe 8 MHz [U, B3/B6]); ihre
  Zeiten (Abtastzyklus, Bittakt, Wiederholtick = 80 000 / f) werden in Z8-Takte des Terminals umgerechnet.

## 4. Terminalrechner im Einzelnen

- **Bus**: Programmbus < 1000H = EPROM.  `LDE` (/DM) 1000–17FF RAM, 1C00/1C01 8275 (A0), übrige
  Datenadressen FFH.  `LDC`-Schreiben ≥ 2000H: Dekoder A15–A13 (001 ZG2, 010 ZG1, 100 Klingel, 110 TRES).
  Spiegel innerhalb 1000–1FFF unbelegt [U] — es wird nur dekodiert, was die Firmware benutzt.
- **Zeilen-DMA**: Jeder /DM-Lesezyklus im BWS lädt die DMA-Zähler (Adresse liegt am Bus, 74193 laden)
  [A].  Fallende Flanke P35 (DMA ein) ⇒ die 80 Byte ab dieser Adresse gehen in den Zeilenpuffer der
  angeforderten Zeile.  DRQ des 8275: für Zeile 0 am Anfang der letzten VRTC-Zeichenzeile, für Zeile r
  am Anfang der Zeichenzeile r−1 (Datenblatt: Puffer wird eine Zeile vorher gefüllt); am Pin P31 als
  kurzer Low-Impuls (IRQ2).  **Am Bildende (VRTC, P32 fallend = IRQ0)** baut `I8275::frame()` das Bild
  aus den Zeilenpuffern; eine nicht bediente Zeile liefert F1 (Zeilenende + DMA-Stopp) = leer [A].
- **Bildausgabe**: `I8275::Cell` je Zelle (Feldattribute nicht transparent, Cursor, Blinkphase vom 8275)
  und ein **Pixelbild 640 × 312** (Zelle 8 × 13 aus dem 2716: Byte n = Rasterzeile n, Bit 7 links).
  Videoregel [A]: Punkt = ZG ∧ ¬VSP, Unterstrich (LTEN) bzw. Unterstrich-Cursor in der Rasterzeile
  `underlineLine()`, dann XOR RVV; Blinken = VSP in der Dunkelphase; Highlight = hellere Stufe (VIDEO2 [U]).
  Der Zeichensatz gilt **für das ganze Bild** (RS-Flipflop wählt das 2716, nicht je Zeichen).
- **Text/Zellen** wie beim Kern-Terminal: logische Zeile z über die Zeilentabelle 1780H, Byte 80H–BFH =
  Feldattribut (`TerminalZelle::feld`).  Damit lesen Tests BWS **und** 8275-Zellen.
- **Watchdog**: P36 (VSYN) triggert den 74123 nach; bleibt der Impuls `Config::watchdogMs` (Vorgabe
  100 ms [U]) aus, Reset des Z8 (8275 nicht).  0 = aus.
- **Tastaturschnittstelle**: Leitungen Takt/Daten am Stecker XB1.  Steigende Taktflanke schiebt
  ¬Daten ein (Polaritätsbrücke so, dass das Startbit der K7673 die 1 wird); nach Startbit + 8 Bit
  (LSB zuerst) „Byte voll“: P2 = Byte um eins links gedreht, P33 low (IRQ1); weitere Takte bis TRES
  werden verworfen [A].  Zusätzlich `tastaturByte()` für Prüfungen ohne Tastaturmodell.
- **Serielle Leitung**: Rechner → Terminal als Pegel an P30 (`Z8::p30Quelle`, Rahmen 1 Start + 8 Daten +
  Stopp, 384 Takte je Bit bei 9600 Bd, Rahmen lückenlos hintereinander); Terminal → Rechner über
  `Z8::uartGesendet`.  Ein mit verdoppeltem T0 gesendetes 00H (Firmware `BREAK`) wird als **Break**
  gemeldet.

## 5. Tastatur K7673.09 (Verhaltensmodell)

Codetabelle 02E3H (128 × [Merker, Code]), Folgen ab Merker & 7 · 256 + Code, Sonderfälle 02EFH (1DH-Taste),
02FFH (PAUSE, mit 1DH ⇒ 03EDH, keine Wiederholung), LED-Tasten 03DBH/032FH/03DFH (P2.4/5/6, nur einmal
je Druck) — alle Adressen aus dem Abzug gelesen, nichts abgeschrieben.  Algorithmus wie Firmware 0048H–0115H:
Abtastung 8 Zeilen × 16 Spalten, 41 gleiche Abtastungen = stabil, > 3 gedrückte Tasten ⇒ verworfen, je
Spaltenbyte erst Make, dann Break (Bit aufsteigend), Wiederholung der zuletzt gedrückten Taste (50/10
Ticks), AA beim Einschalten, Puffer 16 Byte mit FF als Überlaufmarke.  Ausgabe: Takt/Daten wie Firmware
0240H (Ruhe Takt 0/Daten 1, je Bit Daten = ¬Bit, Takt low→high mit Daten stabil).  Schnittstelle:
`druecke/loslassen(Zeile, Spalte 0–15)`, Bequemlichkeit `tastenFuerZeichen(c)` (SHIFT/CTRL + Taste aus
der Terminaltabelle).  Wächter: **Differenzialtest gegen die Firmware auf dem Z8** für jede Matrixposition.

## 6. Außenschnittstelle (`TerminalGeraet`)

`takt(maschinenTakte)` (Kopplung an den `SerialAnschluss` des Rechnerkanals), `text(z)`, `zelle(z, s)`,
`zeile()/spalte()` (Cursor), `zeichenTaste(c, ctrl)`, `taste(TerminalTaste)`, `klingel()`,
`baudAbweichend()`, Save-State.  Kern-Terminal: Adapter um `Terminal` + `TerminalAnschluss`.
Originalterminal: `P8000TerminalEinheit` + `TerminalHwKopplung`; Tasten gehen als Matrixdruck an die
K7673 (Haltezeit so, dass die Entprellung sie sieht).

## 7. Eigenständiger Betrieb, Mehrinstanz, Save-State, Debugger

- `P8000TerminalEinheit` hat eine **eigene Laufschleife** (`laufe(z8Takte)`) und einen eigenen
  `SerialHub` mit **einem** Anschluss „Terminal (XB5)" (Format 9600 8N2 in Z8-Takten) — Telnet/RFC 2217/Datei
  wie jede Schnittstelle.  Mehrere Einheiten in mehreren Prozessen ⇒ Arbeitsplätze (P21/P22).
- **Save-State** `P8TH` v1: Z8, RAM, 8275, RS-FF, Schieberegister, DMA-/Bildzeit, Watchdog, Leitung,
  Tastaturmodell.  Rückrufe nicht.
- **Debugger**: `k1520dbg --terminal [--script …]` hängt den Kontext `tools/dbg_z8.h` an den Z8 der
  Einheit; Zusatzbefehle `term` (Bild), `host <text>` (Zeichen vom Rechner), `key <c>` (Taste),
  `lauf <ms>` (Echtzeit-unabhängig, Einheit läuft samt Video).
- C-ABI (`K1520_MACHINE_P8000_TERMINAL`, Rahmenpuffer, Tasten) folgt in P20d.

## 8. Tests

- `test_p8000_terminal_hw` (unit/peripherals): Einschaltbild (BWS + Zellen + Pixel), 8275-Funktionen
  (Abdeckungsliste `doc/p8000/terminal_abdeckung.md`), DMA/Zeilentabelle, Rollen, Zeichensatz, Klingel,
  Watchdog, Schieberegister, Save-State.
- `test_p8000_terminal_diff`: die Fälle des Kern-Terminals (`test_p8000_terminal.cpp`) **durch die
  Firmware** — Abweichungen sind dort mit Begründung festgehalten (W2/W3/W6 entschieden).
- `test_tastatur_k7673`: Modell, und Differenzialtest gegen die K7673-Firmware auf dem Z8-Kern.
- Integration: Taste „a“ ⇒ Terminal sendet „a“; Host sendet `ESC [ 2 J` ⇒ Bild leer.
- CLI: `k1520dbg --terminal` Skriptlauf.

## 9. Anbindung (P20c/P20d, Nachtrag 2026-10-08)

- **Variante „P8000 + P8000 Terminal"**: `P8000Machine::Config::terminal = Original` baut statt des
  `KernTerminalGeraet` ein `HwTerminalGeraet` an tty1; die Laufschleife ruft je Befehl `konsole_->takt(n)`
  (Kopplung rechnet Maschinentakte mit Rest in Z8-Takte, driftfrei, §3).  Netz-Ein: Terminal zuerst,
  `terminal_vorlauf_ms` = 1500, danach `TerminalHwKopplung::synchronisiere()` (Zeitbezug ab der aktuellen Z8-Zeit).
  Rahmenpuffer der Maschine = Pixelbild 640 × 312; `keyboardLeds()` = LEDs der K7673.  Save-State P8KS v4
  (Terminalart im Fingerabdruck, Abschnitt Terminal = P8TH + Kopplung).
- **Variante „P8000 Terminal"**: `P8000TerminalMachine` (`core/machines/p8000/`) — `K1520Machine` um eine Einheit,
  Leitung am Hub der Einheit (Vorgabe Telnet-Client 127.0.0.1:5000), `run()` in Z8-Takten, Save-State „P8TM" v1.
  Gewählt statt einer eigenen Bibliothek oder eines Handles ohne `K1520Machine`, weil die C-ABI jeden Handle als
  `K1520Machine*` behandelt (Seriell, Bild, Tasten, LEDs, Klingel gehen dann ohne Sonderweg).
- **C-ABI**: `K1520_MACHINE_P8000_TERMINAL = 5`, `k1520_create_p8000_terminal(konfig)`; `k1520_term_*` bedienen beide
  Terminalarten (`k1520_term_kind`), neu `_framebuffer`, `_frame_count`, `_matrix_key`, `_scancode_key`,
  `_matrix_scancode`, `_leds`; `k1520_state_*` auch für P8TM.  Tasten: Matrixkode `0x04000000 | Zeile << 8 | Spalte`.
- **Werkzeuge**: `boot_trace --machine p8000-terminal` (bis zur Einschaltmeldung, `--keys`, gesendete Bytes),
  `boot_trace`/`k1520dbg --konsole original`; `k1520dbg --terminal` (P20a) bleibt der Z8-Prüfstand.
- **Tests**: `test_p8000_terminal_original` (Banner im Originalbild, `O U` … `boot` bis `:` über die K7673, Matrixtaste,
  Save-State-Rundreise, Mehrplatz über Loopback-Telnet), `py_c_api`, `py_binding`, `bt_p8000_terminal`,
  `bt_p8000_konsole_original`.
