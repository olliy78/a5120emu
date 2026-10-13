# MC80.3x (.30/.31/.33) — Hardware-Abschrift (AP M0)

Stand: 2026-10-14.  Ergebnis von AP M0 (`doc/design/32_…` §6, Entwurf `doc/design/31_mc80.md` §3.1).
Nur Hardware; Software-Schnittstellen (EGOS) und Disassemblat sind M1.

**Quellenkürzel** (Seitenangaben = GEDRUCKTE Seitenzahl bzw. Blatt, nicht PDF-Seite, wo nicht anders vermerkt)

| Kürzel | Quelle (`doc/original_docs/mc80/`) | Güte |
|---|---|---|
| [EGOS] | `mc80.3x-egos.pdf` (EGOS 30.1, Teil 1; Seite = gedruckte Zahl, PDF-Seite = Zahl+1) | Schreibmaschine, gut lesbar |
| [EGOS2] | dasselbe PDF ab PDF-Seite 24: Magnetband-Treiber (Teil 2) | gut |
| [BDA] | `mc80.3x-bda.pdf` Betriebsdokumentation MC 80.30/31, 08/85 | gut |
| [HW88] | `kdt_hardware_mc80.30_1988.pdf` Hardware-Doku, KdT Gera 10/88 (Blatt = PDF-Seite) | Kopien von Plänen, grobe Skizzen |
| [KDT89] | `kdt_mc80.30_1989.pdf` KdT-Lehrgang, **handgeschrieben** (Seite = PDF-Seite) | lesbar, nur als Hinweis |
| [SPE2] | `spe2-doku.pdf` Technische Beschreibung SPE 2, Stand 9.4.85 | gut |
| [ROM] | eigene Durchsicht der MAME-Abzüge `doc/EPROMS/MC80/mc8030_mame/zve_1…4`, `spe_1/2` (Adressen = CPU-Adressen) — **kein** Disassemblat, nur gezielte Byte-Suche | Befund, belastbar bei Prüfsumme/Gegenprobe |
| [MAME] | `mc8030.cpp` (Auswertung im Entwurf 31 §3.1; Quelle lag im Scratchpad einer früheren Sitzung) | Skelett, vieles geraten |

**Nicht ausgewertet** (für M0 nicht nötig oder unleserlich): `spe2-sp1/sp2.pdf` und `zve_bst_3o.pdf` (Schaltpläne/Bestückung als Bild, ohne Textebene — Plan-Lesen wäre eigene Arbeit), `pk-3-*.pdf` (Laufwerksmechanik/Schnittstelle PK-3, gehört zu Entwurf 29), `mc80.3x-basic/grab/oce/ram-m/sw-0aff/sysinfo/bsys-hex` (Software; M1/M4), `mc80_22-*` (MC80.2x).

---

## 1. Befund in Kürze (Abweichungen von Entwurf 31 / MAME)

1. **Segmentumschaltung = `OUT 85H` = ZVE-PIO KANAL B (Datenport), nicht Kanal A.**  A enthält genau ein gesetztes Bit (Einsernummer 01H, 02H, 04H … 80H). [EGOS S.17, S.7] — [ROM] bestätigt: Reset-Code schreibt `0FH` nach `87H` (PIO B Steuerwort = Betriebsart 0, Ausgabe) und danach nur `85H`; **im ganzen ROM 0000–27FFH gibt es keinen Zugriff auf 84H/86H** (PIO A bleibt dem Anwender, geht an den Koppelbus).  Entwurf 31 §3.1 („PIO A = Segmentumschaltung“) ist damit falsch.
2. **Takt: 2,4576 MHz** (Quarz 9830,4 kHz ÷ 4).  „2,5 MHz“ in [BDA]/Entwurf ist die gerundete Nennangabe, MAME hat recht (§2.1).
3. **MAME bildet SPE und Segmente nicht ab**: 4000H–BFFFH ist dort EIN flaches RAM, es gibt keine Segmente, keine Sperr-/Freigabeports B0H–B3H der SPE 2 und keine ROM/RAM-Auswahl.  Ein getreuer Kern muss beides bringen (§4).
4. **VIS2a: MAME kennt nur „hell/dunkel“** (Funktionscode-Bit 0) — Funktionscodes 2/3 (IN-Register, negiert) und alle Bildkommandos (Löschen, Rollen, Aus/Ein) fehlen; die Adressierung (Y = Portadresse A8–A15, X>>3 + Halbbild) stimmt dagegen mit [BDA] überein (§5).
5. **Schreibschutz: Port 8CH/8DH** (zwei Ports, wie bei der SPE: gerade/ungerade).  MAME mappt `88H–8FH` und tut nichts.  **Offener Widerspruch im ROM (§3.3)**: das Reset-ROM schreibt 256-mal nach 8CH und beschreibt danach ungehindert FC00H–FFFFH.
6. **Unterbrechungspriorität**: MAME rät (ASP-CTC, ASP-PIO, ASP-SIO, ZVE-PIO, ZVE-CTC).  Belegt ist nur: innerhalb der ASP gilt SIO > CTC > PIO ([BDA] S.18, [HW88] Bl.6); die Reihenfolge der Karten ist offen (§7).
7. **Tastatur 9600 Baud, 7 Datenbit, ungerade Parität, 2 Stopbit** (MAME-Annahme) wird durch den SIO-Init im ROM **bestätigt** (§6.2) — und der Baudratenteiler rechnet nur mit 2,4576 MHz glatt auf 9600 auf.

---

## 2. Rechnerkern ZVE (50300.3010 / 50302.3010)

### 2.1 CPU, Takt

- U880D (Z80), IM 2, I-Register `FFH` (Vektortabelle bei FF00H–FFFFH), Refresh durch die CPU für den dRAM [BDA S.7, S.21; ROM: `LD A,FF; LD I,A; IM 2` bei 077BH].
- **Quarz 9830,4 kHz** auf der ZVE ([HW88] Bl.5 „Quarz 9830,4 KHz“; Takt-Ausgang X3 „bei Slave aufzutrennen (zentraler Takt)“).  ÷4 = **2,4576 MHz**.
- **Klärung 2,5 vs. 2,4576 MHz**: [BDA S.16] und [EGOS] sprechen von „halbiertem Systemtakt 1,25 MHz“, also nominal 2,5 MHz.  Gegenprobe gerechnet: ASP-CTC Kanal 0 wird im ROM als Zähler (`47H`) mit Zeitkonstante 4 programmiert (§6.2) und taktet über ein Halbierer-Flipflop die SIO mit ×16-Takt.  Mit 1,2288 MHz (= 2,4576/2) ergibt das 1,2288 MHz ÷ 4 ÷ 2 ÷ 16 = **9600,0 Bd** exakt, mit 1,25 MHz 9765,6 Bd.  Die Baudratenliste [BDA S.17] (150 … 9600) ist ebenfalls eine 9600·2⁻ⁿ-Reihe.  **Festlegung: 2 457 600 Hz**; „2,5 MHz“ ist Prosa-Rundung.  Übereinstimmung mit [MAME] (`XTAL(2'457'600)`).
- **Offen [?]:** ob die CPU wirklich direkt ÷4 bekommt (Plan [HW88] Bl.5 zeigt Quarz + Teiler nicht aufgelöst).  Für die Emulation unerheblich.

### 2.2 Speicherkarte (CPU-Sicht)

| Bereich | Inhalt | Quelle |
|---|---|---|
| 0000–0FFF | Betriebssystemkern, VIS-Treiber, Zeichengenerator, BSYS-Init (ZVE-EPROM 1/2) | [EGOS S.4] |
| 1000–1FFF | Magnetbandsoftware (ZVE-EPROM 3/4) | [EGOS S.4] |
| 2000–23FF / 2400–27FF | Sprungprogramme, Segmentumschaltung, Relativlader / Geräteverwaltungskommandos (SPE-2-ROM, 2708) | [EGOS S.4], [BDA S.15] |
| 2800–3FFF | Rest des SPE-2-ROM-Bereichs (hier liegen die EPROM-Zusatzmodule, MAME „for extension“) | [BDA S.15], [MAME] |
| 4000–BFFF | segmentierbares RAM, **jeweils nur 1 Segment à 32 KB eingeschaltet**, bis 8 Segmente | [EGOS S.4, S.17] |
| C000–FFFF | ZVE-RAM 16 KB (2 Blöcke × 8 K, Anfangsadresse per Wickel X7), nicht segmentiert | [BDA S.15], [HW88] Bl.5 |
| FC00–FFFF | Schreibschutz-Fenster 1 KB (§3) | [EGOS S.4] |

- ZVE-EPROMs: 4 Fassungen für 2716 (2 K) [BDA S.14, [HW88] Bl.5 (X5/X6/X8: U555 = 2708-Alternative / U2716)]; Standard 8 KB = 0000–1FFF.  Es gibt eine **„1-k-Variante“** der ZVE-Fassung ([HW88] Bl.5) — für MC80.30E-Fassungen relevant [?], nicht ausgewertet.
- RAM-Anfangsadresse der ZVE per Wickel X7 wählbar (C000 / E000 / A000 / 8000 / 6000 / 4000 / 2000 laut Beschriftung, 2 × 8 K) — der Standard ist C000 [BDA S.15].
- **Reset:** `JP 0776H` bei 0000H; RST-Vektoren 08H–38H springen alle in die RAM-Aussprungtabelle bei **FEEBH…FEFDH** (3 Byte je RST, `C3 lo hi`), NMI (0066H) → `JP FEE8H`; Anwender-Aussprünge für Sondertasten FED0–FEE5 ([EGOS S.5, S.22], [ROM] 0000–0066).
- Reset-Folge im ROM ([ROM] 0776H ff.): `LD SP,F800H` · `LD I,FFH` · IM 2 · EI · 256 × `OUT (C),B` mit BC=008CH (§3) · `OUT (87H),0FH` (PIO B Betriebsart 0) · … `OUT (85H),n` (Segment). Danach Initialisierung der Systemzellen (FB40–FBFF), ADT-Vorbelegung `LDIR 0C71H → FC00H` (17 Byte), SIO/CTC-Init (§6).

### 2.3 E/A-Karte (Bytes, A8–A15 sind bei OUT (C) mit auf dem Bus, §3/§5)

Gesamtübersicht [HW88] Bl.2 (Skizze, Zeile = High-Nibble, Spalte = Low-Nibble) und [EGOS S.5–6] / [BDA S.15–18]:

| Port | Baustein | Quelle |
|---|---|---|
| 10–1F | reserviert Floppy-AFS K1520 (K5122) | [HW88] Bl.2 |
| 30–3F | Massenspeicher: Kassettenansteuerung AKB (.31) / AKM (.30, .33) | [HW88] Bl.2, [EGOS S.6] |
| 80–83 | ZVE-CTC Kanal 0–3 (Anwender) | [EGOS], [BDA S.15] |
| 84 / 85 / 86 / 87 | ZVE-PIO: **A-Daten / B-Daten / A-Steuer / B-Steuer** (Daten-/Steuerwort, B bei A0=1, Steuerung bei A1=1) | [BDA S.15] |
| 8C (8D) | RAM-Steuerung (Schreibschutz FC00–FFFF); die Skizze belegt 8C **und** 8D | [EGOS S.5], [HW88] Bl.2 |
| B0–B3 | SPE 2: B0 ROM sperren / B1 ROM freigeben / B2 RAM sperren / B3 RAM freigeben (Auslieferung) | [SPE2 S.6], [KDT89 S.27] |
| C0–CF | VIS 2a (§5) | [EGOS], [HW88] Bl.7 |
| D0 / D1 | SIO ASP Kanal A Daten / Steuer | [BDA S.18] |
| D2 / D3 | SIO ASP Kanal B Daten / Steuer | |
| D4–D7 | CTC ASP Kanal 0–3 | |
| D8 / D9 / DA / DB | PIO ASP A-Daten / A-Steuer / B-Daten / B-Steuer | [BDA S.18] (Achtung: **hier** A-Steuer = D9, anders als bei der ZVE-PIO) |
| E0–EF | EPROM-Programmierung (EPR); die Skizze [HW88] Bl.2 deutet nur auf **E8** (±1) — [?] | [EGOS S.6] |

Die ZVE-Karte selbst legt den 16er-Block der eigenen CTC/PIO über Wickelfeld X4 fest ([HW88] Bl.5, Tabelle „E/A Ports“ 00–FF in 16er-Schritten); Standard 80–8F.  Die Port-Dekodierung der Bausteine nutzt nur A0–A1/A2 (MAME mappt mit `mirror(0xff00)`; A8–A15 bleiben für die Karten mit `OUT (C)` relevant).

Die EGOS-Tabelle nennt „90–BFH zur Zeit nicht verwendet“ ([EGOS S.5]) — das widerspricht SPE 2 auf B0–B3 [SPE2]; die SPE 2 ist die verlässlichere Quelle (eigene Beschreibung der Karte).

---

## 3. Segmentierung und Schreibschutz

### 3.1 Segmentregister (ZVE-PIO B, Port 85H)

- Schalten per `OUT 85H`, **A = Segmentnummer als Einsernummer** (nur ein Bit darf gesetzt sein): Segment 1→01H, 2→02H, 3→04H, 4→08H … 8→80H [EGOS S.7, S.17].
- Maschinencodesegment/Datensegment/Externsegment stehen in Merkzellen **FBFBH / FBFEH / FBFFH** (SEGC/SEGD/SEGE) [EGOS S.19]; das Hochfahren setzt SEGD=01H (ROM 078DH).
- Das Ausgangssegment ist vor dem Wechsel zu sichern (SEGC) und zum Schluss wiederherzustellen [EGOS S.17].
- **Elektrisch:** die PIO-B-Ausgänge gelangen über den Koppelbus an die /MEMDI-Eingänge der Speicherkarten; je Segment eine Leitung, „MEMDI = HIGH → Karte (bzw. ihr RAM-/ROM-Block) freigegeben“.  An der ZVE-Koppelbus-Steckleiste sind nur **/MEMDI 1 (A21)** und **/MEMDI 2 (C21)** fest belegt ([BDA S.14]); für Bank 3 nennt die SPE-4-Skizze **/MEMDI 3 (C20)** ([KDT89 S.26/28]; in [BDA S.14] sind Kontakte 18–20 „–“) — die Verdrahtung der übrigen PIO-B-Bits auf Segmente ist **[?]** (vermutlich Wickelfeld X4 der ZVE; nicht aus dem Text ableitbar).  Der Bus-Eingang **/MEMDI (X1 C9)** sperrt „alle Speicher“ ([KDT89 S.25]).
- PIO-B-Anfangszustand: nach Reset Eingänge (Z80-PIO), das ROM schaltet erst auf Betriebsart 0 (`0FH` → 87H); Segmentbits sind bis zum ersten `OUT 85H` also nicht „1“ gezwungen.
- **Emulationsvorgabe:** Segmentregister 8 Bit, Karte *n* ist freigegeben, wenn ihr Bit gesetzt ist.  SPE 2 mit RAM bei 4000H–BFFFH: Segment 1 = Karte 1 (bei zwei SPE 2 Segment 2 = Karte 2, siehe KDT89 S.2: „2 × SPE 2“-Skizzen).  EPROM-Segmente ab Segment-Nr. 3 (Bit 3 = 08H aufwärts) [EGOS S.4]; MC80.31E: RAM 01H/02H, EPROM 10H/20H [Entwurf 31].  Zuordnung Bit → Karte ist **Konfiguration (Wickel)**, keine Konstante.

### 3.2 SPE 1 / SPE 2 / SPE 4

**SPE 2 (50300.3520)** [SPE2], [HW88] Bl.10, [BDA S.15–16]:
- 32 KB dRAM (2 × 8 × U256D) + 8/16 KB EPROM (8 Fassungen 2708 oder 2716), 215×170 mm; Refresh über den Systembus.
- **RAM-Anfangsadresse** per DIL-Schalter S5202/1–4 in 4-KB-Schritten (Brücken wie 1000H=Schalter 1, 2000H=2, 7000H=1+2+3, 8000H=4); **ROM-Anfangsadresse** per S5201/1–4 genauso.  Auslieferung: ROM 2000H–3FFFH (2708, 8 KB), RAM 4000H–BFFFH.  Überlappen sich ROM und RAM bei gleichzeitiger Freigabe, ist zum Schutz der Schaltkreise der ROM gesperrt [BDA S.15].
- **Zugriffs-/Schreibschutzsteuerung in 1-KB-Schritten**: 16-Bit-Ausgabeadresse; **A0 = 1 Freigabe, A0 = 0 Sperre**; A2–A7 = E/A-Adresse (Wickel X5203–X5206 hoher Nibble, X5210/X5216 ROM bzw. X5211/X5216 RAM niedriger Nibble, paarweise), **A10–A15 wählen den 1-KB-Block**; die Daten (A-Register) sind belanglos („nicht benötigt“) [SPE2 S.2, S.4, S.6; KDT89 S.26].
  - Auslieferung: **ROM sperren B0H, ROM freigeben B1H, RAM sperren B2H, RAM freigeben B3H** [SPE2 S.6].
  - Zugriffsbeispiele (Original): `LD A,04H / OUT (B1H),A / ADD A,04H / CP 80H / JR NZ` = 1 KB je Schritt über 2000H–7FFFH (A8–A15 stehen bei `OUT (n),A` auf dem Bus; Blocknummer steht im A-Register → A15–A8 = A) [SPE2 S.4–5].  Vorsicht: dort steht „A = Anfangsadresse“, d. h. A-Register-Hochbyte = Adresse; für `OUT (C),r` ist es B [KDT89 S.26].
  - **Anfangsinitialisierung** per X5207 (ROM)/X5208 (RAM) und Sperr-/Schreibschutzart per X5209 ([SPE2 S.4]); Auslieferung **nicht gewickelt** (= „mit Anfangsinitialisierung“).  **[?] Widerspruch im Text:** §3.6 sagt, *ungewickelt* heisse „mit Anfangsinitialisierung“, §4.2 sagt, bei Anfangsinitialisierung sei die Karte nach Reset *vollständig gesperrt* und müsse per OUT freigegeben werden; [KDT89 S.27] schreibt „Anfangsinit. (Freigabe beider Bereiche)“.  Vorschlag: **Auslieferungsstand = nach Reset gesperrt, EGOS gibt frei** (Gegenprobe in M1: Freigabe-`OUT B1H/B3H` im ROM suchen; ein direktes `OUT (B1H)` kommt in 0000–27FF NICHT vor, also läuft es über `OUT (C),r`).
  - /MEMDI: **MEMDI** sperrt/gibt die ganze Karte frei, **MEMDI 1** nur den RAM-Block, **MEMDI 2** nur den ROM-Block [SPE2 S.3].
- Stromversorgung 5P/5N/12P.

**SPE 1 (U224/U214, 16 KB CMOS-sRAM, batteriegepuffert)** [KDT89 S.25–27]: 16 KB zusammenhängend, Anfang in 4-KB-Stufen (Schalter), Schlafzustand 5PG = 2…5 V (40 mA), Sperrung bei 5P < 4,75 V; Freigabe/Schutz ebenfalls per OUT-Ports (A1–A7 = E/A-Adresse, A10–A15 = Bereich; Wickel 1…4 Schlafspannung, 5…10 Sperrart, 11…22/23…38 E/A-Adresse, 39…50 Bereichsgröße).  Nach Reset **inaktiv**; Standardprogrammierung: Bereich 4000–7FFF, E/A-Adresse **B0H/B1H**, Schreibschutz für 4000–5FFF (Beispiel `LD B,40H; LD C,0B0H; OUT (C),r`).  **[?]** Die handgeschriebenen KDT-Seiten sind nicht überall sicher lesbar — SPE 1 wird in Etappe 1 nicht gebraucht (SPE-2-Konfigurationen genügen).

**SPE 4 (50319.3180)** [KDT89 S.27], [HW88] Bl.11: 24 Fassungen (28 pol.) 2716/2732/2764 → 48/96/192 KB EPROM, 3 Bänke I–III zu je 2 Segmenten, Auswahl 3 oder 6 Segmente per Wickel 34–36, Bank je über /MEMDI 1/2/3; „A3 muss immer 0 sein“, nach Reset Bank I aktiv; Programmierung `OUT n` (A beliebig) / `OUT r` (C vorher laden).  Verwendung: nur-lesender segmentierbarer Speicher (MC80 LA-LABS).  Nicht in Etappe 1.

### 3.3 Schreibschutz FC00H–FFFFH (RAM-Steuerung, Port 8CH/8DH)

- 1 KB FC00H–FFFFH schreibgeschützt (Systemzellen: ADT FC00–FC6F, Anwenderzellen FC70–FECF, Sondertasten-Aussprünge FED0–FEE7, NMI/RST-Aussprünge FEE8–FEFF, Erweiterungen FF00–FFAF, INT-Vektoren FFB0–FFFF) [EGOS S.4–5].  „Port 8CH sperrt/gibt frei“ [EGOS S.5].  Die Skizze [HW88] Bl.2 zeigt „RAM-Steuerung“ auf **8C–8D**, analog zum SPE-Schema (gerade = sperren, ungerade = freigeben) — Vermutung.
- **[ROM]-Befund (wichtig für M2):** das Reset-ROM schreibt bei 077CH ff. **256 × `OUT (C),B`** mit C=8CH, B=00H,FFH,FEH…01H (A8–A15 = B = Daten) und beschreibt **danach** ungehindert FC00H (`LDIR` bei 0C68H), FFFCH (`LD (FFFCH),HL` bei 03D6H, SIO-A-Rx-Vektor) und FFEEH (bei 0405H).  Es gibt im ganzen ROM **kein** Schreiben auf 8DH.  Entweder bedeutet das gerade 8CH hier „Freigabe“ (das Gegenteil des SPE-Schemas), oder der Schutz ist nach Reset zunächst aus.  **Vorgabe für M2:** nach Reset sind FC00–FFFF schreibbar; der Schreibschutz-Zustand wird über ein Flag modelliert, das Port 8CH/8DH setzt/löscht (Richtung [?] — als Testfall: `Schreibschutz aktiv ⇒ Schreibzugriff wirkungslos`, vorläufig nie automatisch aktiv).  Wirkung eines Zugriffs auf einen geschützten Bereich: keine NMI-Auslösung belegt [?].

---

## 4. Speicher-Emulationsmodell (Folgerung für M2)

- ROM 0000–1FFF immer (ZVE), 2000–3FFF ROM der SPE 2 (zunächst frei: ROM-Sperre per B0/B1 gegen Reset-Zustand [?], siehe §3.2), 4000–BFFF Fenster auf das per `OUT 85H` gewählte Segment (RAM der SPE 2 oder EPROM-Segment), C000–FFFF ZVE-RAM.
- „Kein Segment gewählt“ (Register 0) ⇒ das Fenster 4000–BFFF ist leer: Lesen liefert Busfloat (FFH, [MAME]: `unmap_value_high`).
- SPE-2-ROM-Auswahl ist 1-KB-granular; das MAME-ROM 2000–27FF ist `spe_1/spe_2` (je 1 KB, 2708); 2800–3FFF sind Erweiterung (die 31E-Segment-ROMs `mc80.31e-2400…3800` / `3s-2000…3c00` laden dorthin).

---

## 5. Bildschirm VIS 2a (50300.3220)

### 5.1 Eigenschaften

- Vollgrafik **512 × 256 Punkte** (Bildwiederholspeicher 16 KB, **nicht** am Systembus erreichbar, nur über E/A) [BDA S.19], [KDT89 S.33].  [BDA S.7] „Verwaltung des Bildwiederholspeichers durch eigenständigen Rechner“ — **[?]** ob damit Logik oder ein Mikroprozessor gemeint ist; für den Kern genügt die Registerschnittstelle.
- Text: 25 Zeilen × 80 Zeichen entstehen **per Software** (Zeichengenerator im ZVE-ROM, Zeichenzelle 6×10-Pixel-Raster mit Zeilenhöhe 0AH = 10 Pixel, doppelt hoch 14H; EGOS-Zellen FB40–FB4B) [EGOS S.6].  Monitor: **Schwarz/Weiß-Monitor K7222 (MON 2) bzw. grüner Schirm B31 G22** („Farbe: Grün“ in [BDA S.7, A7/KDT89 S.34]).  Für die GUI: grünes Phosphor, Helligkeitsregler als Parameter.
- **Zeit:** Bildfrequenz 50 Hz, Zeilenperiode **64 µs**, 312 Zeilen, Bildsynchronimpuls 192 µs, Zeilensynchronimpuls 5,74 µs; Video-Fenster **512 Bildpunkte in 40,96 µs ⇒ 12,5 MHz Punkttakt** (Quarz 12,5 MHz, 800 Punkte/Zeile), Austastlücke vorne 8,1 µs [HW88] Bl.8, [KDT89 S.33].  Die Bildwiederholung läuft unabhängig von der CPU (kein WAIT bekannt).
- Mehrere VIS 2a lassen sich **kaskadieren** (Master + Slaves, SYN/Takt-Leitung auf Koppelbus): 2 Karten = 4, 3 = 8 Helligkeits-/Farbstufen [BDA S.19], [HW88] Bl.9.  Nicht in Etappe 1.

### 5.2 Adressierung

- Adressbits **AB4–AB7** wählen die Karte (Wickel X6/X7), Standard **C0H–CFH** [HW88] Bl.7.  Wickel X8 wählt, ob die Karte auf 0–7 oder 8–F des Nibbles antwortet (Standard „8…F“ markiert) [HW88] Bl.7 [?: Skizze].
- Kaskade: **Gruppenadresse** Cx (alle Karten, nur Bildkommandos) und **Geräteeinzeladresse** je Karte: R = Bx, G = Ax, B = 9x ([HW88] Bl.9, [KDT89 S.33]: „4 × 16 = 64 Ports, über oder unter 80H“).  Gilt nur bei Kaskade.
- Zugriff: **`OUT (C),r`**: **r = X-Koordinate (0…255)**, **B = Y-Koordinate (0…255) bzw. Opcode**, **C = Kommando** [BDA S.19], [KDT89 S.33].  Das Z80-`OUT (C),r` legt B auf A15–A8, C auf A7–A0 — Port-Dekodierung sieht also Y auf A8–A15 ([MAME] dekodiert genau so: `select(0xff00)`).
  - **C Bit 7–4** = Modul-/Geräteadresse (C), **Bit 3** = Bildhälfte (**0 = linke, 1 = rechte** 256-Pixel-Hälfte; X zählt je Hälfte 0…255), **Bit 2–0** = Funktionscode:

| C2 C1 C0 | Bedeutung [BDA S.19] |
|---|---|
| 0 0 0 | Bildpunkt dunkel |
| 0 0 1 | Bildpunkt hell |
| 0 1 0 | Bildpunkt → IN-Register |
| 0 1 1 | negierter Bildpunkt → IN-Register |
| 1 0 x | Opcode in B auswerten (Bildkommando) |

  - **Lesen:** über `IN`; Funktionscode 010/011 lädt das IN-Register mit dem (negierten) Bildpunkt [BDA S.19: „Beschreiben und Lesen … über OUT und IN“].  Genauer Ablauf (welcher IN-Befehl/Port, Bitlage) **[?]** — in M3 aus dem EGOS-Treiber ableiten.
- **Opcodes (B, bei C2=1, C1=0)** [BDA S.20]:

| C0 | B (Hexnibble 1 / 2) | Wirkung |
|---|---|---|
| 0 | 0 / 0,4,8,C | Bild dunkel löschen, Rollzähler zurücksetzen |
| 1 | 0 / 0,4,8,C | Bild hell löschen, Rollzähler zurücksetzen |
| beliebig | 0 / 1,5,9,D | Bild ausschalten |
| beliebig | 0 / 2,6,A,E | Bild einschalten |
| 0 | 1 / n | Bild um n Zeilen nach oben rollen (Hintergrund dunkel) |
| 0 | 1 / 0 | Horizontalrollzähler löschen |
| 1 | 1 / n | n Zeilen nach oben (Hintergrund hell) |
| 1 | 1 / 0 | Vertikalrollzähler löschen |
| 0 | 2 / n | n Zeilen nach unten (dunkel) |
| 0 | 2 / 0 | Vertikalrollzähler löschen |
| 1 | 2 / n | n Zeilen nach unten (hell) |
| beliebig | 5 / – | Lichtstift-Initialisierung (Helltastung) |

  [?] Die Tabelle hat offensichtliche Schreibfehler („Horizontalrollzähler“ bei 1/0 mit C0=0 vs. „Vertikal“ bei C0=1; `2/0` mit „Vertikal“ zweimal) — Bedeutung des Zählers (Rollen = Bildanfangsadresse) muss in M3 am Treiber nachgeprüft werden.  Rollen ändert nur den Anfang der Anzeige, der Speicher bleibt (EGOS-Zelle FB4A „Rollzähler“).
- **Beispiele aus den Quellen:** Löschen dunkel `LD C,0C4H; LD B,0; OUT (C),A` (C=C4H: Gerät C, Hälfte 0, Funktion 100) [KDT89 S.33]; Punktkette `LD C,0C1H … OUT (C),A; INC A; INC B` [KDT89 S.33].  [ROM] 0225H: `LD A,(FB4B); SET 2,A; LD C,A; OUT (C),B` — Opcode-Weg mit Hintergrund-Bit in FB4B.
- **Bildspeicherorganisation laut [MAME]** (in sich stimmig mit [BDA]): `Adresse = Y·64 + Hälfte·32 + (X >> 3)`, Bit = `X & 7`; 16 KB (`0x4000`).  Ob Bit 0 der linke oder rechte Pixel ist, ist bei MAME mit `x^0x3fff` + `bit 7 zuerst` + `ORIENTATION_FLIP_X` verdreht → **[?] am echten Bild von EGOS prüfen** (Textausrichtung ist der Test).
- **Lichtstift** (XS3: Anschluss LIMP/LSTAT) und **externer Monitor** (XS4): außerhalb des Plans.
- **QFA (50319.3130)** Farb-/Zeichenadapter: Bildwiederholspeicher als Dual-Port-RAM 2 KB Zeichen + 2 KB Attribut, 64 × 32 Zeichen à 8×8, 8 Farben, 12-MHz-Takt; Speicherbereich per Wickel (4 KB-Stufen, z. B. F000–FFFF/A000–AFFF), /MEMDI-Generierung, Zugriff nur in der Dunkeltastperiode wählbar ([HW88] Bl.12, [KDT89 S.30–32]).  Optionale Karte, Etappe 4.

---

## 6. Peripherieanschluss ASP (50300.3050): SIO, CTC, PIO, Tastatur

### 6.1 Ausstattung, Adressen, Interrupt

- Enthält SIO, CTC, PIO (U 856/857/855-Familie), Busanpassung, **Tastaturschnittstelle (IFSS-Kanal XS502)**, IFSS (20-mA-Stromschleife duplex asynchron XS503, bis 500 m), IFSP (parallel, XS504) [BDA S.16–18].
- **Kartenadresse AB4–AB7 per Wickel** (Standard D) [HW88] Bl.6; Ports §2.3.
- **Priorität in der Kette: SIO (höchste) > CTC > PIO** [BDA S.18 „Mit steigender E/A-Adresse fällt die Priorität“; [HW88] Bl.6 Bild IEI → SIO → CTC → PIO → IEO].  (MAME hat die Reihenfolge CTC, PIO, SIO — falsch.)
- Reset: ASP liefert einen zeitlich begrenzten Rücksetzimpuls bei Reset-Taste oder externem Reset [BDA S.17].
- IFSS-Kanal: Stromschleife 20 mA (0 = 0…3 mA, 1 = 15…25 mA), 5–8 Bit, Parität oder 1 Stopbit, **Baudraten 150, 200, 300, 600, 1200, 2400, 4800, 9600** [BDA S.17]; V.24 laut [KDT89 S.35] als Variante.

### 6.2 Bausteininitialisierung durch das ROM (ableitbar und belegt)

[ROM] 03E0H ff., Datensatz 043BH ff. (`OTIR` je Baustein):

| Baustein | Bytes (hex) | Bedeutung |
|---|---|---|
| CTC ASP (D4H) | `47 04` | Kanal 0: Zähler-Betrieb, ZK folgt (`47H`), **Zeitkonstante 4** ⇒ ÷4 des Eingangstakts, ZC0 → SIO-A-Sende-/Empfangstakt ([BDA S.16]: Kanal 0 → SIO A, Kanal 1 → SIO B; je ein ÷2-Flipflop dazwischen) |
| SIO ASP Kanal B (D3H) | `18 02 F0 01 04` | Kanalreset, **WR2 = F0H** (Interruptvektor-Basis; mit I=FFH → FFF0H–FFFFH), WR1 = 04H (Status beeinflusst Vektor) |
| SIO ASP Kanal A (D1H) | `18 14 4D 03 61 05 AA 01 18` | Reset; **WR4 = 4DH** (×16, **2 Stopbit**, **Parität ungerade**), **WR3 = 61H** (Rx **7 Bit**, Auto-Enables, Rx an), **WR5 = AAH** (Tx **7 Bit**, DTR, RTS, Tx an), **WR1 = 18H** (Rx-Interrupt bei jedem Zeichen) |
| CTC ASP | `E8` nach D4H, `07` nach D7H | Vektorbasis **E8H** (FFE8–FFEF); Kanal 3: Timer-Betrieb `07H` (ZK folgt), Interruptweg über **CTC-Kanal 3** (FFEEH = Zeiger auf Routine bei 046FH) |

- **⇒ Tastatur: 9600 Bd, 7 Datenbit, ungerade Parität, 2 Stopbit** (SIO-Kanal A der ASP) — Bestätigung der [MAME]-Annahme.
- Interruptvektoren (aus ROM + [EGOS S.5]): SIO-A-Rx-Vektor = FFF0H + 0CH = **FFFCH** → Routine bei **044BH** (`IN A,(D0H)`, Zeichen &7FH: Codes ≥20H = Zeichen, 1CH–1FH = Sonderpräfix [?]); CTC-ASP-Kanal 3 → FFEEH → 046FH (Zeitgeber der Tastaturwiederholung/Zeitüberwachung [?]; Routine setzt `OUT (C),91H/01H` an CTC-Kanal 3, Adresse in FB44H).  Die Vektortabelle: FFD8–FFDF CTC ZVE, FFE0–FFE3 PIO ZVE, FFE4–FFE7 PIO ASP, FFE8–FFEF CTC ASP, FFF0–FFFF SIO ASP; FFB0–FFBB AKB (nur .31), FFBC–FFD7 AKM [EGOS S.5].

### 6.3 Tastatur (abgesetzt, eigener Einchip-Mikrorechner U 881)

[BDA S.21, S.12], [EGOS S.6, S.21]:
- U881 (mit PIO/CTC/SIO) fragt die Matrix ab, **wiederholt Tasten** selbst, überträgt **seriell über 4 Leitungen** (IFSS-Kanal XS502, ≤ 5 m) an die SIO-A der ASP; Piezophon („Pieptöne“ programmierbar) und **3 LEDs** (Ebene, Bereitschaft) vom Rechner ansteuerbar.
- Tasten: 61 alphanumerische (3 Interpretationsebenen: 1. Ebene Buchstaben, 2. Ebene via Umschalttasten (16)/Feststeller (22), CTRL-Ebene), 8 Funktionstasten (Sondertasten 0–7 → Aussprung FED0H + 3·n mit `C3 lo hi`), 20 numerischer Block; Sonderfunktionen RES (Gerät rücksetzen), CL, ENTER, OFF, Cursor, Tab, CTRL, CRLF [BDA S.12].  Layout: [BDA A5] („Tastenübersicht“).
- Die Rx-Interruptroutine muss ≲ 10 ms bleiben; solange sind alle INT gesperrt [EGOS S.21].
- **Richtung Rechner → Tastatur** ([ROM] 0505H–051CH, 1D18H): `OUT (D0H),n` mit **07H = Summer (BEL)** und **12H/14H** (Ebenen-/Statusanzeige) [?: nur Byte-Befund, Bedeutung der Codes aus dem Kontext geraten].
- **Emulationsvorgabe** (wie Entwurf 31): Tastatur auf Zeichenebene als Gerät an SIO A (9600 7O2), Code ≥ 20H ASCII/ISO 646, Steuercodes, Sondertaste n = Code 0–7 über festen Präfix [?].  Welche Codes die RESET-/Sondertasten genau liefern, ist am Disassemblat (M1) zu ermitteln.

### 6.4 ASP-PIO

PIO ASP D8–DB wird vom ROM 0000–27FF nicht angesprochen (kein `OUT D8…DB`) → **IFSP-Schnittstelle (parallel) nur für Anwender** [ROM]; im Kern als einfacher PIO-Baustein ohne Gegenstelle.

---

## 7. Unterbrechungen, Reset, NMI

- **Kette** (Prioritäten): innerhalb ASP SIO > CTC > PIO (belegt).  Reihenfolge der Karten ZVE / ASP / weitere untereinander **[?]**: [MAME] rät (ASP-CTC, ASP-PIO, ASP-SIO, ZVE-PIO, ZVE-CTC), [BDA S.14] kennt nur IEI/IEO am Bus.  Vorschlag: nach Steckplatz-Reihenfolge ZVE (Kartenplatz 1) > … > ASP, solange kein Befund; mit EGOS prüfbar, weil nur die ASP-Quellen (SIO-Rx, CTC 3) und der Tastendruck Interrupts im Grundzustand auslösen (die ZVE-CTC/PIO sind im Reset-ROM nicht scharf).
- **NMI** kommt von der Monitorstromversorgung MST 2 (Netzausfall-Erkennung → Rettungsvorgänge) [BDA S.18, A4].  NMI-Aussprung FEE8H.
- **Reset:** Reset-Taste (rot) + Power-On-Reset der ZVE; Taste „RES“ der Tastatur ist Gerät-Reset [BDA S.12, S.17].  RAM-Inhalt bleibt erhalten, „Betriebssystem wird abgearbeitet“; Kennbyte-03-Routinen aus der RAM-Kette werden bei jedem Reset abgearbeitet [EGOS S.20].
- INT-Modus 2, Anwender dürfen weder Modus noch I-Register ändern, kein langes DI/WAIT > 10 µs (Refresh!) [BDA S.21].

---

## 8. EPROM-Programmiereinheit EPR (E0H–EFH)

[BDA S.8, S.21], [EGOS S.6], [HW88] Bl.2:
- Steckeinheit EPR für **2708 (U2708/K573RF1, 1K×8)** und **2716 (K573RF2, 2K×8)**; Betriebsarten Testen / Lesen / Programmieren / Vergleichen / CRC-Kontrolle; enthält Busanpassung, **CTC UB 857 D** (Zeitgeber für Programmierimpulse/Sonderspannungserzeugung) und Überwachung der Programmierspannung (Schutzspannung max. 27 V an der Fassung); Anpasslogik entkoppelt die Bussignale; dazu **EPF** (Aufnahmevorrichtung) und **Löschkammer** (4 Schaltkreise, ca. 20 min, eigener Netzschalter).
- Port-Belegung innerhalb E0–EF (Adress-/Datenregister, Steuerwort, CTC-Kanäle) und Protokoll sind in den vorliegenden Unterlagen **nicht beschrieben**; [MAME] ignoriert Schreibzugriffe.  **Vorgabe:** E0–EF schreiben = verwerfen, lesen = FFH, Gerät „nicht vorhanden“ (EGOS-Handler liefert dann Fehler); optionale Nachbildung erst Etappe 4 (Handler liegt in den Segment-ROMs, Disassemblat nötig).

---

## 9. Massenspeicher-Anschluss AKB / AKM (30H–3FH)

(Details Kassette: Entwurf 29, `doc/kassette/`.)
- **AKB** (K1520-Steckeinheit „AKB Typ 8029“) koppelt die Laufwerke **LW 1200** bzw. **zwei K 5200** an (Kombinat Robotron); nur **MC80.31** (K5020 + K5200); Vektoren **FFB0–FFBB** [BDA S.21], [EGOS S.5].
- **AKM** (Z.-Nr. 50300.3110) für **MC80.30/.33**: Basisadresse über Schalter SI01–SI04 für AB4–AB7 frei wählbar, Auslieferung MC80.3x **30H–3FH**; Wickel für KNG/WGB (Schreiben/Lesen) Auswahl, Pausenpegel, Nachtakte (16) und RESET-Zuschaltung; IFSS-Modus nach TB 4.5 [HW88] Bl.4.  Vektoren **FFBC–FFD7**.
- **PK-3 / CM 5214** (MC80.33, Treiber `mc80.33-1000.bin` ab 1000H): Kassettenbandspeicher, **32 Bit/mm nach PE (ISO 3407)**, 2 Spuren, Nenngeschwindigkeit **0,254 m/s (10 ips)**, Suchlauf 1 m/s, Informationsrate max. 8000 Bit/s, Interface-Signale PO, RP, RW, RS, PRZ, PSZ, IZ / KZ, GOT, ZD, STRAB, BET, IO, ZB, TACH [pk-3-doku Bl.3, pk-3-doku2] — nur Stichworte, Auswertung gehört zu K-APs.
- **Magnetband-Treiber** (ZVE-ROM 1000–1FFF): Laufwerksstatus-Byte (Bit 6 = 38 cm/s K5200 / 19 cm/s), Fehlercodes, Kommandoschnittstelle über IX [EGOS2 S.3–4]; Auswertung in M1/K-Paketen.

---

## 10. Weitere Angaben aus den Quellen (für den Kern unmittelbar relevant)

- **Systemzellen/Speicher** (RAM-Kette, Kennbytes, ADT, INIT/CAT/DEFINE/…): [EGOS S.6–9, S.15–17], [BDA S.22–25] — Merkposten für M1/M4, keine Hardware.
- **IY-/RIO-Vektor, logische Geräte 1–14H**: [EGOS S.15–17].
- **Bildzellen**: FB40–FB4B ZG-Adresse (bei Reset **C000H**: ZG liegt im ZVE-RAM-Fenster ab Reset-Kopie, nicht im ROM!) — **Hinweis**: der Zeichengenerator wird laut [EGOS S.6] „bei RESET 0C000H“ adressiert; das ROM 0000–0BFF enthält also die Vorlage, die nach C000H kopiert wird (Gegenprobe M1) — [?].
- **Referenz Interruptpriorität-Beschleunigung** („/IEP, /PRDY, /RESET, TAKT0“ am Koppelbus-Stecker: „Signal für externe Beschleunigung der Interruptprioritätenkette“) [BDA S.14] — nur bei Mehrrechnerkopplung.

---

## 11. Offene Punkte (Zusammenfassung)

| # | Punkt | Wer klärt |
|---|---|---|
| 1 | Richtung/Wirkung von 8CH (256-fach im Reset geschrieben, danach FC00-FFFF beschreibbar), 8DH | M1 (alle Zugriffe auf 8C/8D im Disassemblat); M2: Flag, vorläufig offen |
| 2 | Verdrahtung PIO-B-Bit → Segment/MEMDI-Leitung (nur MEMDI1/2 am ZVE-Koppelbus belegt, MEMDI3 aus SPE-4-Skizze) | Konfiguration im Kern; Schaltplan `zve_bst_3o.pdf`/`spe2-sp*.pdf` lesen (Bild) |
| 3 | SPE-2-Zustand nach Reset: gesperrt (Text §4.2) oder frei (Text §3.6/KDT)? Wie gibt EGOS ROM/RAM frei (`OUT (C),r` mit C=B1H/B3H)? | M1 |
| 4 | Reihenfolge der Interruptkette über Karten | M1/M3 am laufenden EGOS |
| 5 | VIS2a: IN-Lesepfad (IN-Register), Bit-Reihenfolge im Byte/Spiegelung, Rollzähler-Semantik, Opcode-Tabellen-Schreibfehler | M3 |
| 6 | Tastaturcodes zur SIO (Sonderpräfix 1CH–1FH?, LED-/Summer-Befehle 12H/14H/07H) | M1 (ISR 044BH, 0505H) |
| 7 | E/A-Lage des EPR (E8 laut Skizze vs. E0–EF laut EGOS), Protokoll | Etappe 4 |
| 8 | ZVE 1-k-Variante / Alternativbestückung (U555) | niedrig |
| 9 | Ob die CPU-Taktteilung genau ÷4 ist (für Takt egal) | niedrig |

## 12. Konsequenzen für die Folge-APs (Kurzfassung)

- **M2:** Segmentregister auf **Port 85H (PIO B)** modellieren, Segmentnummer als Bitmaske, `Z80PIO` B-Ausgabe → `setSegmentMask`; SPE 2 mit B0H–B3H (1-KB-Sperre, A0 = Freigabe) **und** `OUT (C),r` (Hochbyte = B); Schreibschutz offen nach Reset.  Takt **2 457 600 Hz**.
- **M3:** VIS 2a Funktionscodes 0/1 (Punkt), 2/3 (IN-Register), 4–7 (Opcodes); ASP: CTC ASP (ZC0 → SIO A, ZC1 → SIO B über ÷2; Eingangstakt = Takt/2), SIO A Tastatur 9600 7O2; Interruptvektoren FFE8/FFF0 mit I = FFH.
- **Tests für M2/M3 aus den Quellen:** SIO-Init-Stream (§6.2) als Referenz („nach Reset trägt SIO A WR4=4DH, WR3=61H, WR5=AAH“); `OUT 87H=0FH`; ROM schreibt FFFCH/FFEEH nach dem 8CH-Loop erfolgreich.
