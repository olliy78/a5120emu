# P8000-Terminal: Hardware-/Funktionsreferenz

Stand 2026-10-05, AP P1. Quelle: **P8000-Hardwarehandbuch**, Kap. 4 (Terminal, S. 4-1…4-42) und — als Kurzfassung — Kap. 2 §5/§6
(Interfacekabel, S. 2-12…2-32). Konventionen wie `hw_8bit.md` (S., Z., **[unklar: …]**, **[Deutung: …]**). Bilder (Zeichensatztafeln
Bild 4.1-2/4.1-3, Tastaturansichten Bild 4.4-1/4.4-2, Terminalstruktur, Rückansicht) liegen **nur als Grafik** vor; Tastencodes der
Flachtastatur K801 sind daher **nicht** im Text. Firmware-Abzüge: `doc/p8000/eproms/TERMINAL/` (AP3).

Der Emulator-Bedarf (AP12/13): ein Terminal pro tty-Kanal, Betriebsarten **ADM31** und **VT100**, 80×24, Attribute, zwei Zeichensätze.
Dieses Dokument ist die Spezifikation der Terminalfunktionen.

---

## 1. Allgemeine Beschreibung (S. 4-3…4-8)

- Alphanumerisches Terminal, **80 Zeichen × 24 Zeilen**, **zwei durch Tastatur umschaltbare Zeichensätze** (Zeichensatz 1 = 128 ASCII-Zeichen, Zeichensatz 2
  „vorzugsweise Deutsch"), **zwei umschaltbare Betriebsarten ADM31 / VT100 (ANSI 3.64)**, ein-/ausschaltbare Videoattribute, **V.24 oder IFSS, 9600 Baud**;
  **nicht grafikfähig**.
- Drei Geräte: **Monitor** (eigene Stromversorgung), **Tastatur**, **Terminalsteuerrechner** (enthält Terminalrechner + Netzteil: +5, +12, −12 V; Sicherungen
  2 × T 0,2 A; Netzschalter vorn; Monitor wird über den Terminalsteuerrechner versorgt/geschaltet).
- **Terminalrechner:** Einchip-Mikrorechner **UB 8840 M** (Z8-Klon mit parallelem + seriellem Interface, Timer) + **CRT-Controller 8275 (KR 580 WG 75)**;
  Firmware in **4-KB-EPROM 2732**. Aufgaben des Einchip-Rechners: Kommunikation mit Tastatur und Host, Escape-Sequenzen verarbeiten/erzeugen, CRT-Controller
  initialisieren, Zeichen in den **Bildwiederholspeicher** schreiben, interne Signale.
- **Watch-Dog:** das **Vertikalsynchronsignal** (zyklisch ausgegeben) wird überwacht; bleibt es aus, erzeugt die Überwachungsschaltung **Reset**, Programm
  startet neu.
- CRT-Controller liest zyklisch je eine Zeichenzeile aus dem Bildwiederholspeicher → Videosignale `VIDEO1`, `VIDEO2`, sowie H-/V-Sync, gemischt zu `BSYN`.
- **Akustisches Signal:** Piezophon, ausgelöst durch **BEL (07H)**.
- **Zeichensatz (S. 4-5…4-7):** Standard = 128 ASCII-Zeichen (Zeichengenerator **P8T-ZG1**), **8 × 12-Punktmatrix** (Anlage A nennt „7 × 11 Punkte" —
  **[Widerspruch W1]**); die 32 Steuerzeichen (00H–1FH) sind **unsichtbar**, im **Programm-Mode** (nur ADM31, nach `ESC U`) werden **alle** Zeichen sichtbar
  dargestellt (ohne Steuerfunktion) — „Bild 4.1-3" (im Text verwiesen, im Auszug nicht vorhanden). Zweiter Generator **P8T-ZG2** (Steckfassung; i. A. deutscher
  Zeichensatz mit Umlauten und ß). Umschalten **ZG1 ↔ ZG2** mit der Taste **<SI/SO>** (rastend, LED); nach dem Einschalten **immer ZG1**.
- **Betriebsarten:** ADM31-Mode und VT100-Mode, Umschalten mit **<MODE>**; **nach Einschalten ADM31**. **Video-Attribute** (Blinken, Invers, „Normal Hell")
  belegen **eine Zeichenposition im Bildwiederholspeicher** (erscheint als Leerzeichen); mit **<VIDEO>** ein/aus. **On-Line/Off-Line** mit **<ON/OFF>**
  (Off-Line: Tastatur-Eingaben werden lokal angezeigt; nach Einschalten On-Line).
- **Einschaltmeldung** (oben links, danach Cursor in der 2. Zeile): `ADM31/9600 baud/Video Attr. on`, nach <MODE>: `VT100/9600 baud/Video Attr. on`; mit <VIDEO>:
  `…/Video Attr. off`. **Jede Umschaltung bewirkt eine Neuinitialisierung des Terminals.**

## 2. Hostinterface (S. 4-8, 4-10/-11, 4-36…)

- 25-polige Buchse (Typ 203-25-EBS-GO 4006/01-2) auf der Rückseite des Steuerrechners, **XB5 „COMPUTER"**; je nach Kabel **V.24** (Auslieferung) oder **IFSS**.
- **Übertragung:** vollduplex, **9600 Bit/s**, Datenformat durch den SIO-Kanal des Einchip-Rechners festgelegt: **1 Startbit, 8 Datenbits, 2 Stopbits, keine Parität**.
- Genutzte V.24-Leitungen: Masse, Sendedaten, Empfangsdaten, **DTR** (bei eingeschaltetem Terminal logisch 0 = +12 V).
- **Umschaltung V.24 ↔ IFSS:** automatisch beim Stecken des IFSS-Kabels (Brücke zwischen Stift 7 und 9 im Stecker); Aktiv-/Passivmodus s. §7.
- **Flusssteuerung:** **XON/XOFF**: das Terminal sendet **CTRL-S (DC3, 13H)**, wenn sein Empfangspuffer gefüllt ist (Host muss das Senden einstellen, Zeichen können sonst
  verloren gehen), **CTRL-Q (DC1, 11H)**, wenn es wieder empfangsbereit ist. Wird das Protokoll von der Hostsoftware nicht realisiert, kann die Arbeitsweise gestört werden.
- **BREAK-Taste:** sendet ein BREAK — „über die gesamte Länge des seriellen Signals (einschließlich Start- und Stopbits) ein Nullsignal".
- **Baudrate nicht einstellbar** (fest 9600; Kopfzeile der Einschaltmeldung). Hostseitig muss tty entsprechend auf 9600 8N2 stehen
  **[unklar: ob WEGA/UDOS 8N2 oder 8N1 programmieren — Terminal sendet 2 Stopbits, empfängt tolerant?]**.

## 3. Tastaturfunktionen (S. 4-12…4-16)

Tastenfunktionen sind für unterschiedliche Tastaturen gleich (Umrechnung durch die Terminalfirmware). Tasten ohne Wirkung am Bildschirm im On-Line-Betrieb: sie senden
ASCII-Zeichen bzw. -Folgen zum Host.

| Gruppe | Tasten | Wirkung |
|---|---|---|
| Alphanumerisch/Sonder | Groß-/Kleinbuchstaben, Ziffern, Sonderzeichen, **<DEL>** | normale Schreibmaschinenbelegung; **DEL = 7FH**; abgesetzter Zifferntastenblock wird nicht unterschieden |
| Umschalttasten | **<SI/SO>**, **<Shift>**, **<Caps lock>**, **<CTRL>** | <SI/SO> und <Caps lock> **vor**, <Shift> und <CTRL> **gemeinsam mit** anderer Taste; <SI/SO> + beliebige Zeichentaste schaltet ZG1↔ZG2 (rastend, LED); <Caps lock> rastend, wirkt nur auf Buchstaben (LED aus = klein); <CTRL> erzeugt Steuerzeichen (Tab. 4.3-5) |
| Steuerzeichen-Tasten | <BS>, <HT>, <LF>, <VT>, <FF>, <CR>, <HOME>, <ESC>, <NL> | siehe Tab. 4.3-6 |
| Terminal-Steuertasten | <MODE>, <VIDEO>, <ON/OFF> | nur intern ausgewertet, **kein** Zeichen zum Host; Neuinitialisierung bei MODE/VIDEO |
| Funktionstasten | <BREAK>, <LINE ERASE>, <PAGE ERASE>, <LINE INSERT>, <CHAR INSERT>, <LINE DELETE>, <CHAR DELETE>, <TAB>, <BACKTAB> | Tab. 4.3-7; BREAK = Nullsignal |
| Sondertaste | <repetierend> (nur PC-1715-Tastatur) | gemeinsam mit Zeichentaste → Dauerwiederholung; <ESC> und Pfeiltasten <VT>,<BS>,<LF>,<FF> sind automatisch repetierend |

### 3.1 ASCII-Controlzeichen über <CTRL> (Tab. 4.3-5, S. 4-14)

| Taste | ASCII | Hex | Operation (Handbuchtext) |
|---|---|---|---|
| CTRL @ | NUL | 00 | no operation |
| CTRL a/A … z/Z | SOH, STX, ETX, EOT, ENQ, ACK, BEL, BS, HT, LF, VT, FF, CR, SO, SI, DLE, DC1, DC2, DC3, DC4, NAK, SYN, ETB, CAN, EM, SUB | 01 … 1A | Start of Heading, Start of Text, End of Text, End of Transmission, Inquiry-who are, Acknowledge, **Audible Alarm (07)**, **Back Space (08)**, **Horizontal Tab (09)**, **Line Feed (0A)**, **Vertikal Tab (0B)**, **„Forespace" (0C = FF)**, **Carriage Return (0D)**, Shift Out (0E), Shift In (0F), Data Link Escape, Device Control 1…4 (11–14), Not Acknowledged, Synchronous Idle, End of Transm. Block, Cancel, End of Medium, Substitute |
| CTRL [ | ESC | 1B | Escape Sequence |
| CTRL (\\ — Taste im Auszug fehlt) | FS | 1C | File Separator **[unklar: Backslash im Text verloren]** |
| CTRL ] | GS | 1D | Group Separator |
| CTRL ^ | RS | 1E | **Cursor Home** |
| CTRL _ | US | 1F | **New Line** **[unklar: Wirkung von US/New Line am Bildschirm nicht in Tab. 4.3-8]** |

### 3.2 Steuerzeichen-Tasten (Tab. 4.3-6, S. 4-15)

| Taste | ADM31: ASCII (Hex) | VT100: Zeichen/Folge |
|---|---|---|
| <VT> | VT (0B) | `ESC [ A` |
| <LF> | LF (0A) | `ESC [ B` |
| <FF> | FF (0C) | `ESC [ C` |
| <BS> | BS (08) | `ESC [ D` |
| <HOME> | RS (1E) | `ESC [ H` |
| <HT> | HT (09) | HT (09) |
| <NL> | BS (08) | BS (08) |
| <CR> | CR (0D) | CR (0D) |
| <ESC> | ESC (1B) | ESC (1B) |

(Referenzkarte, Z. 7802–7818, nennt für ADM31 `<FF> 09H` — **[Widerspruch W2]**; und `<TAB>` ADM31 = 09H, VT100 = `ESC [ I`.)

### 3.3 Funktionstasten (Tab. 4.3-7, S. 4-16)

| Taste | ADM31 | VT100 |
|---|---|---|
| <LINE ERASE> | `ESC T` | `ESC [ K` |
| <PAGE ERASE> | `ESC Y` | `ESC [ J` |
| <LINE INSERT> | `ESC E` | `ESC [ L` |
| <CHAR INSERT> | `ESC Q` | `ESC [ @` |
| <LINE DELETE> | `ESC R` | `ESC [ M` |
| <CHAR DELETE> | `ESC W` | `ESC [ P` |
| <TAB> | 09H | `ESC [ I` |
| <BACKTAB> | `ESC I` | `ESC [ Z` |

## 4. Terminalfunktionen — Steuerzeichen (Tab. 4.3-8, S. 4-16…4-18)

Allgemein: Sichtbare Zeichen erscheinen an der Cursorposition, Cursor rückt weiter; Zeile = 80 Zeichen, nach Zeilenende nächste Zeile; nach Beschreiben der 24. Zeile
**Rollen** (alle Zeilen um eine nach oben, unten leere Zeile, Cursor am Anfang). Letzte Zeile = normale Schreibposition.

| Zeichen | Operation | Wirkung ADM31 | Wirkung VT100 |
|---|---|---|---|
| BEL 07 | Audible Alarm | akustisches Signal | gleich |
| BS 08 | Back Space | Cursor eine Position nach links; **bis Schirmanfang** (Zeile 1/Spalte 1; umbricht auf vorherige Zeile) | nach links **bis Zeilenanfang** |
| HT 09 | Horizontal Tab | nächste Tabposition rechts (Tabposition = durch 8 teilbare Spalte); **beliebig oft, am Ende der letzten Zeile wird gerollt** | **bis Zeilenende** |
| LF 0A | Line Feed | Cursor eine Zeile abwärts, Spalte bleibt; in letzter Zeile **Rollen** | gleich |
| VT 0B | Vertikal Tab | Cursor eine Zeile **hoch**, Spalte bleibt; Funktion endet in Zeile 1 | gleich |
| FF 0C | „Forespace" | Cursor **eine Position nach rechts**; **beliebig oft, am Ende letzter Zeile Rollen** | nach rechts **bis Zeilenende** |
| CR 0D | Carriage Return | Cursor an Zeilenanfang | gleich |
| RS 1E | Cursor Home | Cursor an den Bildanfang | gleich |
| ESC 1B | Escape | Einleitung einer Escape-Sequenz | gleich |
| DC3 13 (CTRL-S) / DC1 11 (CTRL-Q) | Flusssteuerung | **vom Terminal gesendet** (§2) | gleich |

## 5. Escape-Sequenzen ADM31-Mode (S. 4-18…4-22)

Alle Sequenzen mit ESC, **kein Leerzeichen** innerhalb; die ADM31-Sequenzen sind nicht standardisiert, hier „im Stil des ANSI 3.64" benannt.

| Name | Funktion | Host-Syntax |
|---|---|---|
| **CBT** Cursor Backward Tab | Cursor auf nächste Tabposition links (durch 8 teilbar); endet an HOME | `ESC I` |
| **CHT** Cursor Horizontal Tab | Cursor auf nächste Tabposition rechts; endet am Bildschirmende | `ESC i` |
| **CDE** Character Delete | löscht Zeichen unter Cursor, Rest der Zeile um 1 nach links; am Zeilenende Leerzeichen | `ESC W` |
| **CIN** Character Insert | Zeichen ab Cursor um 1 nach rechts bis Zeilenende; letztes Zeichen geht verloren; unter Cursor Leerzeichen | `ESC Q` |
| **HVP** Position | Cursor setzen: Y = Zeile 1…24, X = Spalte 1…80, jeweils als ASCII-Zeichen **beginnend bei 20H** (Zeile 1: Y = 20H, Zeile 24: Y = 37H; Spalte 1: X = 20H, Spalte 80: X = 6FH); zu kleine/große Werte → erste bzw. letzte mögliche Position | `ESC = Y X` |
| **LDE** Line Delete | löscht Cursorzeile; Cursor an Zeilenanfang; folgende Zeilen nach oben; unten Leerzeile | `ESC R` |
| **LER** Line Erase | löscht ab Cursor bis Zeilenende | `ESC T` |
| **LIN** Line Insert | Zeilen ab Cursorzeile um 1 nach unten; Leerzeile mit Cursor am Anfang; letzte Zeile geht verloren | `ESC E` |
| **PER** Page Erase | löscht ab Cursor bis Schirmende | `ESC Y` |
| **PMF** Program Mode Off | schaltet Programm-Mode aus | `ESC u` oder `ESC X` |
| **PMN** Program Mode On | Programm-Mode ein: alle 128 ASCII-Zeichen werden angezeigt | `ESC U` |
| **SDE** Screen Delete | löscht gesamten Schirm, **Cursor bleibt** | `ESC *` oder `ESC :` |
| **SGR** Select Graphic Rendition | Videoattribut für ein „Feld" | `ESC G <Parameter>` |

**SGR (ADM31), Tab. 4.3-9:** Parameter = ein Zeichen `0`…`7`:

| Parameter | Wirkung |
|---|---|
| 0 | Video-Attribute aus (Reset auf Standard) |
| 1 | Feld mit Space füllen („designated areas are blanked") |
| 2 | Zeichen im Feld blinken |
| 3 | wie `ESC G 1` |
| 4 | Zeichen im Feld invers |
| 5 | Feld mit Space füllen, invers ein („reversed and blank") |
| 6 | Zeichen im Feld blinken invers („reversed and blinking") |
| 7 | wie `ESC G 5` |

Attribut-Regeln (beide Moden): Attribute stehen als **Pseudozeichen** im Bildwiederholspeicher (Leerzeichen am Schirm, Cursor dort unsichtbar, durch sichtbares Zeichen
überschreibbar). Der CRT-Controller hebt ein Attribut erst am **Schirmende** auf; die Terminal-Software setzt deshalb am **Zeilenende (80. Position)** ein
Attribut-Aufhebungszeichen (entspricht `ESC G 0` bzw. VT100 `ESC [ m`) bei jeder bildverändernden Funktion → **jedes Attribut wirkt nur innerhalb einer Zeile**; der Bereich
zwischen Attributen heißt **Feld**.

## 6. Escape-Sequenzen VT100-Mode (S. 4-22…4-29)

Darstellung nach ANSI 3.64. **Parameter fehlt oder 0 → 1** (bei Bewegungs-/Lösch-/Einfügebefehlen; bei Cursorposition → Zeile/Spalte 1).

| Name | Funktion | Host-Syntax | Begrenzung/Hinweis |
|---|---|---|---|
| **CBT** | Cursor Backward Tab (n-te Tabposition links; Tab = durch 8 teilbar) | `ESC [ n Z` | endet in Spalte 1 |
| **CHT** | Cursor Horizontal Tab (n-te rechts) | `ESC [ n I` | endet in letzter Spalte |
| **CUB** | Cursor Backward | `ESC [ n D` | endet in Spalte 1 |
| **CUD** | Cursor Down | `ESC [ n B` | endet in letzter Zeile |
| **CUF** | Cursor Forward | `ESC [ n C` | nicht über letzte Spalte |
| **CUU** | Cursor Up | `ESC [ n A` | endet in erster Zeile |
| **CUP** | Cursor Position | `ESC [ row ; col H` | Default 1;1 |
| **HVP** | Horizontal and Vertical Position | `ESC [ row ; col f` | wie CUP |
| **DCH** | Delete Character (n Zeichen rechts vom Cursor, Rest nach links, am Zeilenende Leerzeichen) | `ESC [ n P` | |
| **DL** | Delete Line (n Zeilen ab Cursorzeile; folgende nach oben; unten Leerzeilen; Cursorposition bleibt) | `ESC [ n M` | |
| **DSR** | Device Status Report → **Cursor Positions Report** | `ESC [ 6 n` ⇒ Antwort `ESC [ row ; col R` | |
| **ED** | Erase in Display: 0 = Cursor bis Schirmende, 1 = Schirmanfang bis Cursor, 2 = alles; Cursor bleibt; ohne Parameter = 0 | `ESC [ n J` | |
| **EL** | Erase in Line: 0 = Cursor bis Zeilenende, 1 = Zeilenanfang bis Cursor, 2 = ganze Zeile; Cursor bleibt; ohne = 0 | `ESC [ n K` | |
| **ICH** | Insert Character (n Zeichen ab Cursor nach rechts; Überlauf geht verloren; Leerzeichen; Cursor bleibt) | `ESC [ n @` | |
| **IL** | Insert Line (n Leerzeilen **vor/ab** der Cursorzeile; Zeilen ab Cursorzeile nach unten; Überlauf unten geht verloren; Cursor bleibt) | `ESC [ n L` | Text: „nach der Zeile ein, in der der Cursor steht" **[unklar: vor oder nach — Beschreibung der Zeilenverschiebung „ab der Cursorzeile" spricht für „vor"]** |
| **IND** | Index (eine Zeile nach unten, Spalte bleibt; in letzter Zeile wird eine Leerzeile angefügt und gerollt, erste Zeile geht verloren) | `ESC D` | |
| **NEL** | Next Line (Cursor in erste Spalte der nächsten Zeile; endet am Bildschirmende) | `ESC E` | Text: „Funktion endet am Bildschirmende" (**[unklar: scrollt NEL in letzter Zeile?]**) |
| **RI** | Reverse Index (eine Zeile nach oben; in erster Zeile wird Leerzeile eingefügt, letzte Zeile geht verloren) | `ESC M` | |
| **SGR** | Select Graphic Rendition (bis zu **3 Parameter**, `;`-getrennt; ohne = 0) | `ESC [ p ; p ; p m` | Parameter Tab. 4.3-10 |
| **TEKSC** | Save Cursor | `ESC 7` | |
| **TEKRC** | Restore Cursor | `ESC 8` | |

**SGR (VT100), Tab. 4.3-10:** 0 = alle Attribute (blink, bold, underscore, reverse) aus; 1 = Bold (high light); 4 = Underscore; 5 = Blink; 7 = Reverse video.
Aufhebung am Zeilenende wie in §5.

Referenzkarte (S. 4-30…4-32) gibt zusätzlich: ADM31 „Tab `ESC i`", „Backtab `ESC I`", Home `ESC [ H` bzw. `ESC [ f` im VT100-Mode, Index down `ESC D`, Next line `ESC E`,
Index up `ESC M`, Save/Restore cursor `ESC 7`/`ESC 8` („Save cursor attributes" — der Handbuchtext sagt nur „Cursorposition").

## 7. Tastaturen und Varianten

### 7.1 Tastaturen (S. 4-33…4-35)
- Terminals werden mit der **Tastatur des robotron PC 1715** ausgeliefert (Bild 4.4-1), später durch die **Flachtastatur K 801** abgelöst (Bild 4.4-2; dafür besonders entwickelt).
  Keine kundenspezifische Belegung bestellbar. Tastencodes der Tastaturen werden in der **Terminalfirmware** in die gewünschten Codes/Folgen umgesetzt.
- PC-1715-Tastaturbeschriftungen (Bild 4.4-1, nur Funktionsliste im Text): <CR>, <ESC>, <BACKTAB>, <VT>, <TAB>, <BS>, <HOME>, <FF>, <NL>, <LF>, <ON/OFF>, <repetierend>,
  <Umschaltung Zeichensatz>, <HT>, <LINE INSERT>, <DEL>, <MODE>, <VIDEO>, <.>, <,>, <BS>, <->, <+>, <LINE ERASE>, <LINE INSERT>, <CHAR INSERT>, <PAGE ERASE>, <LINE DELETE>,
  <CHAR DELETE>, <BREAK>.
- Tastaturschnittstelle (seriell: Takt + Daten): **XS1 (Typ GLE/1)**: AB1 Tastaturtakt, AB2 +5 V, AB3 Masse, AB4 Tastaturdaten, AB5 nicht belegt; **XB1 (Typ 2)**: 1 +5 V,
  2 Tastaturtakt, 4 Tastaturdaten, 5 Masse. Monitorstecker **XB4**: GLE/Typ 1: AB1 VIDEO2, AB2 Masse, AB3 BSYNC, AB4 Masse, AB5 VIDEO1; Typ 2: 2 Masse, 6 VIDEO2, 7 VIDEO1, 9 BSYNC.
- Firmware-Abzüge zu Tastaturen (`KEYBOARD.txt`): K801.02 (GLE/Typ 1), K7673.01/.09 (Typ 2) — für AP12/13.

### 7.2 Ausführungsvarianten des Terminalrechners (S. 4-36…4-42)

| | **Typ GLE** (Entwicklungsmuster, vor 3/87) | **Typ 1** (Produktion) | **Typ 2** (für Flachtastatur, wahlweise PC-1715-Tastatur; ab 3/89) |
|---|---|---|---|
| Programmspeicher | 2716 oder 2732 (Wickelbrücke 15-16-17) | 2716 oder 2732 (Wickelbrücke); **Lieferzustand 2732** | — (Brücken nicht änderbar) |
| Interface V.24/IFSS | per **Wickelbrücke** wählbar (4/5/6); Lieferzustand V.24 | **automatisch durch Kabelstecker** (Brücke 7–9) | automatisch durch Kabelstecker |
| IFSS-Modus aktiv/passiv | Wickelbrücken 7–12; Lieferzustand Sender aktiv, Empfänger passiv | Wickelbrücken 7–12; Lieferzustand Sender aktiv, Empfänger passiv | **Brücken im Kabelstecker** (Tab. 4.5-4); Lieferzustand **Sender passiv, Empfänger passiv** |
| XB5 „COMPUTER" (25-pol) IFSS-Pins | 10 SD+ (A), 19 SD− (E), 14 ED− (E), 13 ED+ (A), 12 Q+ 20 mA (A) | 19 SD+ (A), 10 SD− (E), 13 ED− (E), 14 ED+ (A), 12 Q+, 9 V.24/IFSS-Umschaltung (E) | wie Typ 1 + 16 (zweite Q+ 20 mA), 21 (+5 V) |
| XB5 V.24 | 2 = 103 TD (A), 3 = 104 RD (E), 7 SG, 20 = 108.2 DTR (A) | gleich | gleich |
| Tastatur | XS1 | XS1 | **XB1** |

Wickelbrücken-Zeichnungen (Bild 4.5-1/-2): nur als ASCII-Skizze; die Stellung der Brücken bei „V.24" bzw. „IFSS" (Bild 4.5-1: V.24: Brücke 5–6 … **[unklar: ASCII-Skizze
unleserlich; Zuordnung 4/5/6 nicht rekonstruierbar]**).

**Tab. 4.5-4 (Typ 2) — Übertragungsmodus per Kabelstecker 123-25:** Brücken zwischen Q+ (12/16), SD−/SD+/ED−/ED+, SG (7) und IFSS (9)/SG bestimmen Sender/Empfänger aktiv/passiv
(Spalten: passiv/passiv; aktiv/aktiv; aktiv/passiv; passiv/aktiv). **[unklar: ASCII-Schaltbild (Z. 8279–8294) durch Umbruch zerstört; nur die Zeile „Sender: passiv aktiv aktiv passiv /
Empfänger: passiv aktiv passiv aktiv" ist lesbar.]**

## 8. Schnittstellenkabel Computer ↔ Terminal (Kap. 2 §5/§6, Kurzfassung)

- **Auslieferung: V.24-Kabel** Nr. 889061 (Computer–Terminal), Drucker 889064, Programmer 889329, WDC 889441. V.24-Kabel max. 15 m (HYF(C)Y 5×1×0,14); IFSS
  max. 500 m (HYY 2×2×0,25), galvanisch getrennt (vollständige Entkopplung, wenn eine Seite aktiv, die andere passiv — bevorzugt).
- **Index 1 (V: 11xx, Kap. 2 §5.1)** — Computer = **DÜE**: 1:1-Kabel: Computer-Pin 2 (RD/103) ↔ Terminal-Pin 2 (TD/103); 3 ↔ 3 (TD/104 ↔ RD/104); 20 (DCD/108) ↔ 20 (DTR/108);
  8 (DTR/109) ↔ 8 (DCD/109); 7 ↔ 7; Schirm nur einseitig.
- **Index 3/4 (V: 43xx, ab 3/89, Kap. 2 §6.1)** — Computer = **DEE**: gekreuzt: Computer 2 (TD/103) ↔ Terminal 3 (RD/104); 3 (RD/104) ↔ 2 (TD/103); 20 (DTR/108) ↔ 8 (DCD/109); 8 (DCD/109) ↔ 20 (DTR/108); 7 ↔ 7.
- IFSS-Kabel Terminal: Varianten je nach Seite aktiv/passiv (Computer aktiv/aktiv & Terminal passiv/passiv — bei Index 1 mit **Z-Diode SZX 21/8,2** zwischen Stift 12 und 7, Anode an 7);
  ab Typ 2/3/89: beide passiv (Computer) ↔ aktiv (Terminal) mit zwei Stromquellen (16 = 2Q+) — Pinzuordnung siehe Handbuch (Kap. 2 §5.1 Z. 909–1004, §6.1 Z. 1341–1362);
  die **Pin-Paare der IFSS-Kabel unterscheiden sich zwischen Index 1 (SD+ 10 ↔ ED+ 13 …) und Index 3/4 (SD+ 19 ↔ ED+ 14 …)**.
- **Drucker** (EPSON LX-86, K6311…K6314, K6304, K1152): Datenformat **1 Start, 8 Daten, 2 Stop, keine Parität**; Treiber (UDOS, OS/M, WEGA) unterstützen **DTR-Protokoll
  (Hardware, empfohlen)** und **XON/XOFF**; Kabel Index 1: 2↔2, 3↔3, 20↔20, 7↔7 (kein 8/DTR); Index 3/4: 2↔3, 3↔2, 8↔20, 7↔7.
- **Remote-Systeme** (z. B. P8000-8, PC 1715, A5120, die mit einem Remote-Programm als Terminal arbeiten): Kabel an tty0: Index 1: 2↔3, 3↔2, 20↔8, 8↔20, 7↔7 (kreuzend); Index 3/4: 2↔3, 3↔2, 20↔8, 8↔20.
  *(Auszug: der Computer-seitige Anschluss gilt für tty0, bei P8000-8 als Remote-System.)* — zu A5120/K7028 siehe unten.
- **Modem AM-12TD / DNÜ K8172** nur an **tty0** (8-Bit) und **tty4** (16-Bit); Takt-Brücken (TxCE 25, RxCE 11, CPO 18 gegen SG 7) nur für tty0; für tty4 nur per Wickelbrücken auf der 16-Bit-Karte.
- Verbindung zu **A5120/PC 1715** (K7028-V.24/IFSS, X5 V.24): V.24-Kabel Pins (Index 1): Computer 2 ↔ A3, 3 ↔ B4, 20 ↔ B8, 8 ↔ A9, 7 ↔ AB1 (Buchsenleiste 223-13 TGL 29331/04).
  Bezug zur K1520-Familie dieses Repos (K7028/K8025-Hub, Serielle Schnittstellen nach außen) siehe Entwurf 19.

## 9. Widersprüche

- **W1** — Zeichenmatrix: Kap. 4 §1.5 „**8 × 12**-Punktmatrix", Anlage A §3 „**7 × 11** Punkte" (Z. 6838, 9322). Beide können gelten (Zelle 8×12, Zeichen 7×11), im Text nicht aufgelöst.
- **W2** — Taste <FF>: Tab. 4.3-6 (ADM31) FF = **0CH**; Referenzkarte (Z. 7804) listet `<FF> 09H` (= HT). Text der Steuerzeichen (FF = Cursor rechts) stützt 0CH → Referenzkarte vermutlich Tippfehler.
- **W3** — Referenzkarte ADM31: „<LINE ERASE> ESC T" **und** „<LINE ERASE> ESC R" (Z. 7813, 7817); gemeint ist LINE DELETE = `ESC R` (Tab. 4.3-7).
- **W4** — „Bild 4.1-2" (Zeichensatz) bzw. „Bild 4.1-3" (Programm-Mode-Darstellung): im Text wird der Programm-Mode mit Bild 4.1-3 begründet (Z. 7382), in §1.5 aber „Bild 4.1-2" (Z. 6847); Bild 4.1-3 existiert im
  Auszug nicht (nur Bild 4.1-2).
- **W5** — Tab. 4.5-1 (GLE) ED+/ED− Beschriftung: „13 IFSS A ED+ Stromeingang Empfänger" (Spalte nennt ED+ „Stromeingang" obwohl A) — Beschreibungstext unsauber; Pin-Spalte maßgeblich.
- **W6** — VT100 `IL` (Insert Line): Text „nach der Zeile ein, in der der Cursor steht" vs. „Alle Zeilen ab der Cursorzeile werden nach unten verschoben" — offen, ob vor oder nach (praktisch: ANSI = vor der Cursorzeile).
- **W7** — Das Handbuch nennt für `HVP` im ADM31-Mode „Zeilen-Nr. 1…24" aber die Koordinaten fangen bei **20H** an (Y/X als Wert+20H, 0-basiert gedacht, „Y and X number start 0", Z. 7794): Zeile 1 = 20H.

## 10. Offene Fragen an Schaltplan/Gerät

1. **Zeichensatztafeln** (Bild 4.1-2/-3; ZG1 = P8T-ZG1, ZG2 = deutscher Satz): aus dem Zeichengenerator-Abzug (`P8T_1_6.0_ZG1/ZG2`, `P8TDZS`, `P8TEZS` in `doc/p8000/eproms/TERMINAL/`) ableiten; Matrix 8×12; Mapping von
   Steuerzeichen im Programm-Mode.
2. **Tastencodes K801 / PC 1715:** Scan-/Seriencodes der Tastatur (Firmware-Tabelle) — nur im Abzug (`K801.02`, K7673.*).
3. **Attribut-Details:** Blinkfrequenz, „Normal Hell/Bold" (Intensität), Underscore-Darstellung, wie der 8275 Attribute mappt (Zeichen-Attribute vs Feld-Attribute), Cursorform (Abzüge P8TCU/P8TCVB/…: Rechteck, blinkend, Unterstrich).
4. **Tabulatoren:** feste 8er-Stopps oder setzbar (nur „durch 8 teilbar" belegt); Terminalverhalten bei Auto-Wrap/Umbruch in letzter Spalte (ADM31 vs VT100).
5. **Reset-/Start-Zustand** (Cursor, Zeichensatz, Attribute) und was <MODE>/<VIDEO> löschen („Neuinitialisierung": Bild gelöscht?).
6. **Seriell:** hostseitiges Verhalten bei XOFF (Puffergröße des Terminals für `CTRL-S`), BREAK-Länge, Zeichen-Rückkopplung im Off-Line-Mode.
7. **Gerätemessung:** am echten Terminal: Reaktion auf `ESC [ 6 n`, Verhalten von US (1FH), FS (1CH), `ESC =` mit Randwerten, Programm-Mode-Darstellung — und welcher Terminaltyp/Firmware (GLE 3.1, Typ 1 4.1, Typ 2 5.0, WDOS 6.0) im Gerät des Anwenders steckt.
