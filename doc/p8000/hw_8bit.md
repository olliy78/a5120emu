# P8000 — 8-Bit-Rechner (Index 1 und Index 3): Hardware-Referenz

Stand 2026-10-05, AP P1 (`doc/design/25_p8000.md`). Quelle: **P8000-Hardwarehandbuch**
(`~/projects/robotron/P8000/doc/P8000_Hardwarehandbuch.pdf`, 261 S.), Kap. 3 §3 (8-Bit-Rechner
Index 1, S. 3-10…3-33), §4 (Index 3, S. 3-33…3-57), §5 (Floppy-Laufwerke, S. 3-58…3-61), dazu
Kap. 2 §5/§6 (Interfacekabel), Kap. 5 (EPROM-Programmer) und Anlage A. Schaltpläne/Bilder sind
**nicht** Gegenstand dieser Datei (AP2: `schaltplan_8bit.md`).

Konventionen:
- Seitenangaben „S. 3-nn" = Handbuchseite (Kopfzeile der Seite); „Z. nnnn" = Zeile im
  `pdftotext -layout`-Auszug (`P8000_Hardwarehandbuch.txt`), damit jede Stelle wiederfindbar ist.
- **[unklar: …]** = im Text nicht eindeutig, verstümmelt oder widersprüchlich — nicht geraten.
- **[Deutung: …]** = eigener Schluss aus Namen/Tabellen, im Handbuch nicht ausgesprochen.
- Beide Indexstände liegen im Handbuch **Wort für Wort parallel** vor (§3 ↔ §4, Tabellen 3.3-x ↔
  3.4-x). Wo nichts anderes steht, gilt der Text für **beide**; Abweichungen sind in
  „Unterschiede Index 1 ↔ 3" (§9) gesammelt.
- Indexkennzeichnung am Gerät: Typenschild „V: a b c d" — a = 16-Bit-Karten-Index, **b = 8-Bit-
  Karten-Index**, c = WDC-Index, d = Laufwerk (1 NEC D5126, 2 NEC D5146, 4 Robotron VS1–VS3,
  5 K5504.50) (Kap. 1 §5, Z. 437–461). Also „V: 11xx" = Index 1/1, „V: 43xx" = 16-Bit 4 + 8-Bit 3.
  Kopplungsregel (S. 3-33, Z. 2870): **8-Bit Index ≥ 3 nur mit 16-Bit Index ≥ 4** (und umgekehrt
  16-Bit ≥ 4 nur mit 8-Bit ≥ 3, S. 3-87, Z. 4997).

---

## 1. Überblick (S. 3-10…3-14 bzw. 3-33…3-37)

- Einkartenrechner 380 × 250 mm (Index 1: 4 Lagen, Index 3: 6 Lagen), **kein universelles Bussystem**.
- CPU **UA880** (Z80-Klon) 4 MHz; Peripherie UA855D (PIO) ×2, UA856D (SIO) ×2, UA857D (CTC) ×2,
  UA858D (DMA) ×1, **U8272D** (FDC) ×1 (Anlage A, Z. 9275–9279); **3 PIOs** laut Text (PIO0/1/2) —
  Anlage A nennt „2 × UA855D" **[unklar/Widerspruch: Text und E/A-Tabelle zeigen PIO0, PIO1, PIO2,
  Anlage A zählt zwei; siehe §10 W9]**.
- Speicher: 64 KB DRAM, 8 KB EPROM (2 × 2732), 2 KB statischer RAM (Bild 3.3-1 Struktur: *Bild, nicht
  im Text*).
- Anschlüsse hinten: fünf 25-polige Buchsen (X3 Programmer, X4–X7 tty0–tty3) + 39-polige Leiste
  (X10, externer Floppy-Beisteller); Rückseite gegenüber: **Kopplung X1/X2** (zwei 26-polig) zur
  16-Bit-Karte; X8 (Index 1) / X9 (Index 3) interne Floppy-Laufwerke; X11 Tasten-/Anzeigemodul;
  X12 Stromversorgung; X13 Service-Steckverbinder.
- Wickelfelder XP6 (Interruptkette), XP8 (Takt; Index 3 zusätzlich XP7).

## 2. Rechnerkern

### 2.1 Systemtakt (S. 3-14 / 3-37, Z. 2056–2078, 2975–2997)
| Signal | Wert | Zweck |
|---|---|---|
| Quarz | 16 MHz | DL8127 (Clockgenerator) |
| PHI | 4 MHz | CPU + E/A-Bausteine |
| PHI_TTL | 4 MHz | taktsynchrone TTL-Logik |
| 2PHI_TTL | 8 MHz | 8″-Floppy-Laufwerke |

Externe Einspeisung (Prüfung): PHIE/2PHIE über X13. Wickelbrücken **XP8** intern 2-3, 5-6, 8-9 /
extern 1-2, 4-5, 7-8 (Index 1, Tab. 3.3-1); Index 3 **XP7** intern 2-3 / extern 1-2 und **XP8** intern
2-3 5-6 / extern 1-2 4-5 (Tab. 3.4-1). Baud-Takt separat: 9,832 MHz → /8 = 1,229 MHz (§5.2).

### 2.2 Reset (S. 3-14 / 3-37, Z. 2081–2103, 3000–3022)
- **Power-on-Reset** ≈ 1 s, vom DL8127, Open-Collector-Signal `/RES` an alle Bausteine und X13.
- **Bediener-Reset** (Taste <Reset>): „Tasten- und Anzeigemodul" erzeugt `/RESP` (Low, ≈ 6 µs) über
  X11 (A1).
- **Reset-Quelle lesbar:** Signal `RESI` an **PIO2-A7** (Tab. 3.3-2 / 3.4-2): Power-on = High,
  Bediener = Low.
- Wirkung auf die Speicherbanksteuerung: RFF wird gesetzt (§3.2) → nur EPROM ab 0000H sichtbar.

### 2.3 NMI (S. 3-15 / 3-38, Z. 2105–2118, 3024–3037)
- Taste <NMI> → `/NMIP` (Low, ≈ 6 µs) über X11 (B1).
- Umschaltung durch `/NMI_UM` = **PIO0-B7**: nach Reset High → NMI geht an die **8-Bit-CPU**
  („priorisiert"). Mit Freigabe der 16-Bit-Karte setzt der 8-Bit-Rechner `/NMI_UM` = Low und
  lenkt **jeden weiteren NMI-Impuls als `/NMI_U8000`** über X1 (B5) zur 16-Bit-Karte.
- Bedeutung: NMI-Taste dient dem BREAK (Monitor) — auf der 16-Bit-Seite meldet der Monitor
  „Press NMI" (siehe `hw_16bit.md` §2).

### 2.4 Interrupt-Prioritätskette (S. 3-15 / 3-38, Z. 2121–2131, 3040–3050)
```
 DMA - PIO2 - CTC0 - SIO0 - SIO1 -! * !- PIO0 - PIO1 - CTC1        (höchste Priorität links)
                                  IEIT IEOT (X13)
```
- Kette ist über X13 (IEIT = A28, IEOT = B28 bzw. C28 bei Index 3) an der Stelle „*" auftrennbar;
  dazu muss die Verbindung **XP6/1-2** entfernt werden. Standardzustand: geschlossen.
- Alle Bausteine arbeiten im Z80-Modus 2 (Vektor); **[unklar: Vektortabellen-Adresse/I-Register-
  Wert steht nicht im Handbuch — Sache der Software/Monitor-Disassembly]**.

## 3. Speicherbanksteuerung

### 3.1 Prinzip (S. 3-16…3-19 / 3-39…3-42, Z. 2134–2285, 3053–3204)
- Gesamtadressraum der Karte 82 KB (64 + 16 + 2) bei 64 KB CPU-Adressraum.
- Drei Bänke, **je 4-KB-Seite** ein-/ausschaltbar (16 Seiten):

| Bank | Kürzel | Größe | Select-Signal |
|---|---|---|---|
| Festwertspeicher | EPROM | „4…16 KB" (bestückt 8 KB, 2 × 2732) | `PROM_SEL` |
| statischer RAM | SRAM | 2 KB | `SRAM_SEL` |
| dynamischer RAM | DRAM | 64 KB | `DRAM_SEL` |

- Steuerung = **Adressport ADP** (16 Wörter × 4 Bit, adressiert über **A12…A15**, lesend betrieben)
  + **Reset-Flipflop RFF**.
- ADP-Wort: Bit 0 = `PROM_SEL`, Bit 1 = `SRAM_SEL`, Bit 2 = `DRAM_SEL`, Bit 3 nicht benutzt.
  **In jedem Wort darf höchstens eine „1" stehen** (sonst Buskonflikt; Handbuch gibt kein
  Verhalten bei Mehrfach-1 an).
- **Nach Reset** ist RFF gesetzt: ADP gesperrt, `PROM_SEL` aktiv ab 0000H (gesamter Bereich laut
  Text „ab Adresse 0000H freigegeben"), **RAM-Bänke nicht zugreifbar**. Für sicheren EPROM-Zugriff
  erzeugt ein WAIT-Generator **zwei Wait-Takte** (S. 3-16). **[unklar: ob der Wait-Generator nur
  für `PROM_SEL`/EPROM oder auch für SRAM gilt; Text spricht von „Festwertspeichern".]**
- **[unklar: Abbildung der 8 KB EPROM auf die 4-KB-Seiten.]** Handbuch sagt „Festwertspeicher 4…16 KB"
  und zeigt nur zwei Fassungen „EPROM 1/EPROM 2" (Bild 3.3-2). Wie A12/A13 den EPROM-Inhalt bei
  `PROM_SEL` in mehreren Seiten adressieren (Spiegelung?), steht nicht im Text → AP2/Schaltplan.
  *Gegenprobe am Abzug (kein Handbuch):* `doc/p8000/eproms/8BIT/MON8_1_3.0` beginnt bei 0000H mit
  `JR` + `(C)KEAW_01/88` → EPROM 1 liegt auf 0000H–0FFFH (4 KB, Z80-Reset-Einsprung).

### 3.2 E/A-Zugriff auf die Steuerung
| E/A-Adr. | Funktion | Zugriff |
|---|---|---|
| 00H | `WEADP` — Schreiben ins ADP | write-only („Write Only Register") |
| 04H | `RES_RFF` — Rücksetzen RFF | Scheinausgabe **oder** Scheineingabe (IN/OUT), **nur einmal nach Reset** möglich |

### 3.3 Programmierung (S. 3-17 / 3-40)
- Befehl `OUT (C),r` mit C = `WEADP` (00H). Der CPU-Befehl legt **Register B auf den High-Adressbus**
  → B = Anfangsadresse der Ziel-4K-Seite im High-Nibble (A12–A15) wählt die ADP-Zelle:

| Ziel-Adressbereich | B |
|---|---|
| 0000H–0FFFH | 00H |
| 1000H–1FFFH | 10H |
| 2000H–2FFFH | 20H |
| … | … |
| E000H–EFFFH | E0H |
| F000H–FFFFH | F0H |

- Steuerwort (r, nur D0–D2 relevant, D3–D7 = 0): `00H` kein Select, `01H` PROM_SEL, `02H` SRAM_SEL, `04H` DRAM_SEL.
- **Initialisierung** nach Power-on: **alle 16** ADP-Wörter beschreiben (0000H…F000H), danach ein
  IN/OUT auf `RES_RFF` (04H) → RFF zurück, ADP aktiv.
- **SRAM-Doppeladressierung:** SRAM hat nur 2 KB, Select gilt aber 4 KB, A11 wird nicht dekodiert →
  der SRAM erscheint in „seiner" 4K-Seite **zweimal**; die dort sonst liegende Bank (z. B. DRAM)
  ist für die ganze 4K-Seite abgeschaltet.
- **Beispiel (Handbuch):** SRAM ab 2000H:
  ```
  ld c,WEADP   ; 00H
  ld a,02h     ; SRAM_SEL
  ld b,20h     ; Seite 2000H
  out (c),a    ; → SRAM auf 2000H–27FFH und nochmals 2800H–2FFFH
  ```
- Emulationsfolgerung: Zustandsmaschine = `adp[16]` (3 Bit), `rff`; Speicherzugriff
  `sel = rff ? PROM : adp[addr>>12]`; bei `sel==0` **[unklar: liest der Bus dann 0xFF/offen?
  Handbuch schweigt]**.

## 4. E/A-Adressen (Tab. 3.3-7 / 3.4-7, S. 3-21 / 3-44, Z. 2329–2378, 3248–3297)

Index 1 und Index 3 **identisch**; per I/O-Dekoder fest.

| Adresse | Baustein / Funktion | Hinweis |
|---|---|---|
| 00H | WEADP (ADP schreiben) | §3 |
| 04H | RES_RFF | §3 |
| 08H | CTC0 Kanal 0 | Baudgenerator 3 (tty3) |
| 09H | CTC0 Kanal 1 | „Floppy-Disk" |
| 0AH | CTC0 Kanal 2 | „System-Kanal" |
| 0BH | CTC0 Kanal 3 | frei |
| 0CH / 0DH | PIO0 Port A Daten / Control | U8000-Kopplung |
| 0EH / 0FH | PIO0 Port B Daten / Control | U8000-Kopplung |
| 10H | Datenport 1 (DS8282) | U8000-Kopplung |
| 14H | Datenport 2 (DS8282) | U8000-Kopplung |
| 18H / 19H | PIO1 Port A Daten / Control | Programmer (EPROMmer) |
| 1AH / 1BH | PIO1 Port B Daten / Control | Programmer |
| 1CH / 1DH | PIO2 Port A Daten / Control | Floppy-Disk |
| 1EH / 1FH | PIO2 Port B Daten / Control | Floppy-Disk |
| 20H | FDC U8272 Statusregister | |
| 21H | FDC U8272 Datenregister | |
| 24H / 25H | SIO0 Kanal A Daten / Control | tty0 |
| 26H / 27H | SIO0 Kanal B Daten / Control | tty1 |
| 28H / 29H | SIO1 Kanal A Daten / Control | tty2 |
| 2AH / 2BH | SIO1 Kanal B Daten / Control | tty3 |
| 2CH | CTC1 Kanal 0 | Baudgenerator 0 (tty0) |
| 2DH | CTC1 Kanal 1 | Baudgenerator 1 (tty1) |
| 2EH | CTC1 Kanal 2 | Baudgenerator 2 (tty2) |
| 2FH | CTC1 Kanal 3 | frei |
| 30H–3BH | Reserve | über X13 zugänglich (`/CE-RES1..3`) |
| 3CH | DMA UA858D | Floppy-Disk |

Beobachtungen: Bausteinadressen sind 4er-Blöcke (A2/A3 wählen Baustein, A0/A1 Kanal/Port);
Z80-übliche Bitzuordnung (PIO: A1 = Port B, A0 = Control; SIO: A1 = Kanal B, A0 = Control;
CTC: A1A0 = Kanal) ist aus der Tabelle ablesbar. DS8282-Ports 10H/14H belegen **nur** je eine
Adresse (Aliase 11H–13H bzw. 15H–17H **[unklar: dekodiert/frei]**). 3CH für den DMA: ob andere
Adressen 3DH–3FH aliasen **[unklar]**. Lesbarkeit der DS8282-Ports **[unklar]**: Text nennt sie
nur „Latch für Ausgabezwecke" (§6.2).

## 5. Serielle Schnittstellen

### 5.1 Kanalzuordnung (Tab. 3.3-8/-12 bzw. 3.4-8/-12, S. 3-22/-25 bzw. 3-45/-48)

| Kanal | SIO / Kanal | Buchse | Funktion | Baudgenerator (CTC / Kanal) |
|---|---|---|---|---|
| tty0 | SIO0 / A (24H/25H) | X4 | V.24 für DNÜ/Modem **oder** IFSS | CTC1 / 0 (2CH) — Takt umschaltbar (extern RxCE/TxCE) |
| tty1 | SIO0 / B (26H/27H) | X5 | V.24 abgerüstet / IFSS | CTC1 / 1 (2DH) |
| tty2 | SIO1 / A (28H/29H) | X6 | V.24 abgerüstet / IFSS | CTC1 / 2 (2EH) |
| tty3 | SIO1 / B (2AH/2BH) | X7 | V.24 abgerüstet / IFSS | **CTC0 / 0 (08H)** |

(Die Zuordnung steht in Tab. 3.3-12 = 3.4-12, Z. 2515–2524. Anmerkung (1) dort: tty0 kann über
`RxCE`/`TxCE` mit extern geliefertem Takt (114/115) statt CTC-Takt laufen; `CPO` führt den
internen Takt als 113 zur DÜE aus.)

### 5.2 Baudratengenerator (S. 3-25 / 3-48, Z. 2491–2549, 3424–3483)
- Kette: Taktgenerator **9,832 MHz** → **8-fach-Vorteiler** → **1,229 MHz** (`/CPBAUD`, X13-A15) →
  CTC-Kanal (Zähler-Modus, Zeitkonstante) → **2-fach-Teiler** → SIO RxC/TxC → SIO-Vorteiler (×32 / ×64).
- Nachrechnung (nicht im Handbuch): 1,2288 MHz / (CTC-ZK × 2 × 32) = 19200/ZK Baud — passt zur Tabelle.

| Baud | CTC-Zeitkonstante | SIO-Vorteiler (×) |
|---|---|---|
| 19200 | 1 | 32 |
| 9600 | 2 (alt.: 1) | 32 (alt.: 64) |
| 4800 | 4 (2) | 32 (64) |
| 2400 | 8 (4) | 32 (64) |
| 1200 | 16 (8) | 32 (64) |
| 600 | 32 (16) | 32 (64) |
| 300 | 64 (32) | 32 (64) |
| 150 | 128 (64) | 32 (64) |
| 75 | 256 (128) | 32 (64) |

(Klammerwerte = Alternative mit SIO-Vorteiler 64 und halbierter Zeitkonstante — Lesart passt zur
Rechnung oben.) Das Handbuch legt nicht fest, ob die CTCs als Zähler mit externem Takt `/CPBAUD`
(CLK/TRG) laufen; die Skizze (Bild 3.3-2/3.4-2 Teilerkette) liegt nur als Bild vor.

### 5.3 Schnittstellenleitungen (Kurzfassung; Volltabellen im Handbuch)
- **Index 1 (DÜE-Sicht, Tab. 3.3-9/-11):** tty0 X4: Pin 2 = 103 (`RD`, E), 3 = 104 (`TD`, A),
  (4 = 105 CTS, 6 = 107 DSR nur nach Schaltungsänderung), 5 = 106 RTS (A), 7 = 102 SG, 8 = 109 DTR (A),
  15 = 114 TC von DÜE, 17 = 115 RC von DÜE, 20 = 108 DCD (E), 24 = 113 TC zur DÜE; Takt-/Modus-
  Eingänge 11 RxCE, 25 TxCE, 9 IFSS, 18 CPO; IFSS: 10 SD+, 13 ED+, 14 ED−, 19 SD−, 12 Q+.
  tty1–3: Pins 2, 3, 7, 8, 20 + IFSS 9/10/12/13/14/19.
- **Index 3 (DEE-Sicht, Tab. 3.4-9/-11):** Pin 2 = 103 **TD (A)**, 3 = 104 **RD (E)**, 4 = 105 RTS (A),
  5 = 106 CTS (E), 6 = 107 DSR (E), 7 SG, **8 = 109 DCD (E), 20 = 108.2 DTR (A)**, 15/17/24 Takt,
  9 = nIFSS, 11 RxCE, 25 TxCE, 18 CPO, 21 = +5 V (Ausgang); IFSS **19 SD+, 14 ED+, 13 ED−, 10 SD−**,
  12 = nQ+ (eigene Stromquelle), 16 = mQ+ (Nachbarkanal; Paare 0↔1, 2↔3, **nicht** 1↔2).
  tty1–3: 2, 3, 7, 8, 20, 9, 21 + IFSS-Stifte.
- tty0 = Buchse X4 mit DNÜ/Modem K8172 anschließbar (nur tty0 und, auf der 16-Bit-Karte, tty4).
- Interfacekabel (Kap. 2 §5 für V: 11xx, §6 ab 3/89 für V: 43xx): **Index 1 = DÜE** (Terminalkabel
  1:1: 2↔2, 3↔3, 20↔20, 8↔8, 7↔7), **Index 3 = DEE** (Kabel kreuzt 2↔3, 20↔8). Kabel max. 15 m
  (V.24) / 500 m (IFSS). Terminal, Drucker (Datenformat 1 Start, 8 Daten, 2 Stop, keine Parität;
  DTR- oder XON/XOFF-Protokoll) und Remote-Systeme — Details in `hw_terminal.md` §8.
- Anlage A: asynchron, max. 19200 Baud; 8-Bit-Computer 4 × V.24, 4 × IFSS wählbar.

### 5.4 Rollen der Kanäle im System (Kap. 2 §1.2/1.3/2, S. 2-4…2-9)
- **Console = tty1** (SIO0-B, Buchse X5; im Handbuch „-Console-tty1-"), **Drucker = tty3** (SIO1-B, X7, „-Printer-tty3").
- 8-Bit-Konfiguration: Terminal an tty1, Drucker an tty3, Programmer an X3. Bootmeldung auf der Console:
  `P8000   Hardwaretest U880 - Version x.x` / `U880-Softwaremonitor Version x.x. - Press RETURN`; Systemdiskette in
  **Laufwerk 0 (unteres)**, <CR> lädt das Betriebssystem. Kommando **„X"** im U880-Monitor wechselt zum U8000-Monitor
  **ohne Startdiskette** (dann keine Diskettenarbeit unter WEGA, weil UDOS auf der 8-Bit-Seite nicht geladen wurde).
- 16-Bit-Konfiguration, Zuordnung der Anwender-Terminals: Superuser tty1 (Console), 2. Nutzer **tty6**, 3. **tty7**, 4. **tty0**,
  5. **tty2**, 6. **tty4**, 7. **tty5** (tty0–3 = 8-Bit-Karte, tty4–7 = 16-Bit-Karte). Vor dem Abschalten RESET-Taste drücken.
- Disketten: Minidisketten 5¼″, doppelseitig, doppelte Dichte, **96 tpi** (80 Spuren).

## 6. Parallele Schnittstellen

### 6.1 PIO1 → Programmer / EPROMmer (X3) (S. 3-26 / 3-50, Z. 2561–2589, 3498–3526; Kap. 5)

Ungetriebene UA855-Ports an 25-poliger Buchse X3 (Typ 202/203-25-EBS):

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | GND | 14 | +5 V |
| 2 | /ASTB | 15 | /BSTB |
| 3–6 | A0–A3 | 16–19 | B0–B3 |
| 7 | GND | 20 | +5 V |
| 8–11 | A4–A7 | 21–24 | B4–B7 |
| 12 | ARDY | 25 | BRDY |
| 13 | +12 V | | |

**Programmer-Protokoll (Kap. 5 §3, S. 5-6, Z. 8411–8441)** — Port A = Daten (bidirektional über
DS8286) und Adressen/Steuerbits (über 3 × DS8282-Latches); Port B steuert:

| Bit | Wirkung |
|---|---|
| B0 | `OE-`: aktiviert DS8286 zur Ausgabe der Daten |
| B1 | Richtung DS8286: T = PIO→EPROM, T− = EPROM→PIO |
| B2, B3 | 2-Bit-Binär-Dekoder: wählt, welcher Latch (1–3) die Port-A-Daten übernimmt |
| B4 | `OE-` aller drei Latches (Adressen/Steuerbefehle ausgeben) **und** schaltet +5 V an den EPROM |
| B5 | Programmierimpuls |
| B6 | Rückmeldung Kurzschluss des EPROM (High = Fehler; Ansprechwert 200…300 mA) — Eingang |
| B7 | Rücksetzen der Kurzschlussüberwachung (High-Impuls ≥ 50 ms) |

Unterstützte EPROMs: U2708, U2716, U2732(A), U2764 (keine automatische Typerkennung;
Pinbelegungen der Fassung Tab. 5.3-1…4 im Handbuch). Software: UPROG (UDOS), pburn(1) (WEGA).
Kabel Nr. 889329, 0,70 m. Die Zuordnung der Port-A-Latches (welche Adressbits/Steuerbits in
welchem Latch) ist im Text **nicht** tabelliert **[unklar: nur Bild 5.3-1]**.

### 6.2 U8000-Kopplung — 8-Bit-Seite (PIO0 + 2 × DS8282) (S. 3-26/-27 bzw. 3-50/-51)

„Die Kopplung ... wird mit einem UA855D (PIO0) und zwei Latch DS8282 realisiert. Die beiden
PIO-Ports werden für **Eingabezwecke** und die beiden Latch-Ports für **Ausgabezwecke** benutzt."
(Z. 2591–2596). Latches liegen auf E/A **10H** und **14H**, PIO0 auf 0CH–0FH.

**Pinbelegung X1 / X2 der 8-Bit-Karte** — Richtung aus Sicht der **16-Bit-Karte** gemäß
`hw_16bit.md` Tab. 3.6-20/3.7-20 ergänzt (E = Eingang der 16-Bit-Karte):

| Signal | Stecker/Pin | Richtung (8→16 / 16→8) | Funktion (Handbuch-Text, Index 1) |
|---|---|---|---|
| D0/8-16 … D7/8-16 | X1 B4, A6, A7, B13, B11, B10, B9, B8 | 8→16 | Datenbits 0–7 |
| D0/16-8 … D7/16-8 | X2 B2, B9, B8, B7, B1, A10, A8, A1 | 16→8 | Datenbits 0–7 |
| V1…V6/8-16 | X1 A12, A11, A10, A9, A8, B7 | 8→16 | Vektorbits 1–6 |
| V1…V4/16-8 | X2 A5, A4, A3, A2 | 16→8 | Vektorbits 1–4 |
| RDY/8-16 | X1 A5 | 8→16 | „Bereit für Daten 8→16" |
| EO_8 / DD_16 (Index 1) | X1 A3 | 16→8 | „Übernahme Daten 8→16" |
| DD_8 / EO_16 (Index 1) | X2 B5 | 16→8 | „Übernahme Daten 16→8" |
| RDY/16-8 | X2 B3 | 8→16 | „Bereit für Daten 16→8" |
| INT_8 | X2 A6 | 16→8 | Interrupt zum 8-Bit-Rechner |
| INT_16 | X1 A13 | 8→16 | Interrupt zum 16-Bit-Rechner |
| /RESET_U8000 | X1 B1 | 8→16 | „Freigabe 16-Bit" |
| /NMI_U8000 | X1 B5 | 8→16 | NMI zur 16-Bit-Karte (§2.3) |

Index 3 (Tab. 3.4-15, Z. 3571–3598): **sechs statt vier Handshake-Leitungen** — die gemeinsamen
Leitungen werden aufgetrennt und getrennt benannt:

| Signal | Pin | Funktion (Handbuch) |
|---|---|---|
| RDY/8-16 | X1 A5 | Bereit für Daten 8→16 |
| EO_8 | X1 A3 | Freigabe Daten 8→16 |
| DD_8 | X2 B5 | Freigabe Daten 16→8 |
| **EO_16** | **X2 A9** (neu) | Übergabe Daten 16→8 |
| **DD_16** | **X2 A11** (neu) | Übergabe Daten 8→16 |
| RDY/16-8 | X2 B3 | Bereit für Daten 16→8 |
| **RUN** | **X2 A13** (neu) | Rechnerstatus 16→8 (zum Tasten-/Anzeigemodul, X11-A4) |

GND: X1 B12, X2 B11, B12, B13. Frei, „vom 16-Bit-Rechner belegt": X1 A2, B2, B3, A4, B6; X2 B4, B6, A7,
B10; X2 A12 „frei". (Auf der 16-Bit-Seite sind das P0/P1-BRDY/BSTRB und +5 V, siehe `hw_16bit.md`.)

**[Deutung: Protokoll aus den Pin-Namen — nicht im Text ausgeführt]**
- Kanal **16→8:** 16-Bit-PIO0 Port A (Ausgabe, Modus 0) legt Daten auf D/16-8; sein `ARDY0` ist
  Leitung X2-B5 (8-Bit-Name DD_8) = „Ausgabedaten vorhanden" → 8-Bit-**PIO0** Port A (Eingabe,
  Modus 1; dort `/ASTB`); 8-Bit-`ARDY` ist X2-B3 (RDY/16-8) = „Ausgabedaten übernommen" → 16-Bit
  `/ASTB0`. Passt zu Z80-PIO-Handshake.
- Kanal **8→16:** 8-Bit-CPU schreibt Port 10H (DS8282-Latch); X1-A5 (RDY/8-16 bzw. 16-Bit `/ASTB1`)
  = „Eingabedaten vorhanden" für 16-Bit-PIO1 Port A (Eingabe); `ARDY1` (X1-A3) = „übernommen".
  Wie das Latch-Strobesignal erzeugt wird und ob die 8-Bit-CPU `ARDY1` über PIO0-Bits liest oder
  per Interrupt (`INT_8`) erfährt, ist **[unklar]**.
- Vektorbits (V…) und INT_8/INT_16 bilden vermutlich einen **gegenseitigen Interruptweg mit
  Vektor**. Die Vektorbits liegen (per Namen) auf PIO0-Port B (16→8) bzw. DS8282-Port 2 (8→16,
  14H: 6 Vektorbits + vermutlich RESET/NMI-Steuerung = 8 Bit) — **[unklar, nur Vermutung]**.
- `/RESET_U8000` hält die 16-Bit-Karte nach Power-on/Reset-Taste **statisch** im Reset
  („muss durch Koppelsoftware aufgehoben werden", `hw_16bit.md` Tab. 3.6-2). Welches Bit
  (PIO0-B-n oder Latch-Bit) ihn setzt/löscht: **[unklar]**; für NMI ist es **PIO0-B7**.

### 6.3 Kopplung Gegenstelle: Kreuzprüfung der Pinbelegung
Alle Pins stimmen zwischen 8-Bit-Seite (X1↔16-Bit **X3**, X2↔16-Bit **X2**) und den Tabellen der 16-Bit-
Seite (Tab. 3.6-20 / 3.7-20) überein (Index 1: vollständig; Index 3 ↔ 4: vollständig, aber
Richtungs-/Textfragen in §10 W2, W6). Das Ergebnis der E/A-Adressen ist **kein** Pin-Thema: die
8-Bit-Seite liest/schreibt über 0CH–0FH + 10H + 14H, die 16-Bit-Seite über PIO0/PIO1 (FF91…FF9F) —
in den Handbuchtabellen gibt es keine Adresse, die „auf beiden Seiten gleich" wäre; die Zuordnung
läuft ausschließlich über die Handshake-Pins.

## 7. Floppy-Disk-Schnittstelle

### 7.1 Aufbau (S. 3-27 / 3-52, Z. 2645–2653, 3602–3608; Anlage A)
- Bausteine **U8272D (FDC, 20H/21H) + UA858D (DMA, 3CH) + PIO2 (1CH–1FH) + CTC0 Kanal 1 (09H)
  („Floppy-Disk")**. Bis zu **4 Laufwerke**: zwei interne (Drive 0 = unteres = „A", Drive 1 = oberes
  = „B") am Stecker **X8 (Index 1) / X9 (Index 3)** und externe am 39-poligen **X10 „Floppy"**.
- Die Verwendung von PIO2-Bits (Laufwerkswahl, Motor, Dichte, Terminal Count, Reset-Quelle `RESI` =
  PIO2-A7) und der CTC0-Kanäle 1/2 ist im Handbuch **nicht tabelliert**; nur A7 (RESI) ist
  genannt. → **Offene Fragen** (§11): PIO2 A0–A6, B0–B7, DMA-Kanal-Zuordnung, U8272-Interrupt-Weg
  (DMA-/PIO2-Kette?), TC-Erzeugung, READY-Verdrahtung, Takt (4 MHz vs 8 MHz bei 8″).
- Interrupt-Priorität: **DMA → PIO2 → CTC0 → …** (§2.4) — die FDC-Bausteine stehen an der Spitze.

### 7.2 Laufwerke (S. 3-58…3-61, Z. 3820–3950)
- 2 × 5,25″ doppelseitig 80 Spuren: **TEAC FD-55FV-13U**, **FD-55FV-03U** (Typ „1.6") oder
  **Robotron K5601**. Betrieb **MFM**.
- Formate (§5.1, Z. 3833–3843): 40 × 16 × 256 (einseitig), 80 × 16 × 256 (einseitig), **80 × 32 ×
  256 (doppelseitig, Grundeinstellung)**, 80 × 18 × 512 (DS), 80 × 10 × 1024 (DS). Einstellbar mit
  Systemkommando **SETFD**. Anlage A (Z. 9243–9249): 5,25″ DD, DS 80 × 16 × 256 / 18 × 512 /
  10 × 1024; DD, SS 80 × 16 × 256 und 40 × 16 × 256; **8″ (nur extern) SD, SS 77 × 26 × 128**.
- „32 Sektoren × 256" bei DS = 16 Sektoren je Seite (vermutlich; **[unklar: Sektornummerierung
  über beide Seiten fortlaufend 1–32 oder je Seite]**).
- Jumper der eingebauten FD-55FV (Bild 3.5-1, Z. 3935–3950): DS0–3 (nur eine Brücke! Zuordnung der
  Select-Signale), **ML** (*) Motor ein, wenn DS0–3 **oder** MOTOR ON aktiv, **RY** (*) Soft-Sektor,
  **E0, E2** (*) Index-/Datenimpulse nur bei DS0–3 aktiv, U1/U2/HL/IU Kopf laden/LED, RE Kopf nach
  Power-on auf Spur 0, FG, XT (Hardsektor). (*) = Verbindung vorhanden.
- Laufwerks-Stecker (Tab. 3.5-2): Pin 4 `/IU;/HDL`, 6 /SE3, 8 /IDX, 10 /SE0, 12 /SE1, 14 /SE2,
  16 /MO, 18 /DIR, 20 /STP, 22 /WRDATA, 24 /WE, 26 /TRK0, 28 /WP, 30 /RDDATA, 32 /HDS, 34 /RDY;
  ungerade Pins GND. Spannungen (Tab. 3.5-1): +12 V, 2 × GND, +5 V.

### 7.3 Laufwerkssteckverbinder der 8-Bit-Karte

**Interner Anschluss X8 (Index 1)**, Tab. 3.3-16 (Z. 2655–2686): A1 `/TS` (two side), A2 `/SE3`, A3
`/IDXM` (Index Mini), A4 `/SE0`, A5 `/SE2`, A6 GND, A7 `/STP`, A8 `/WRDATA`, A9 `/WE`, A10–12 GND, A13
`/RDY`, B1 `/HDS`, B2–4 GND, B5 `/SE1`, **B6 frei (bei Platinenindex 0: `/MO0` Motor on Drive 0)**, B7 `/DIR`, B8–9 GND,
B10 `/TRK0`, B11 `/WP`, B12 `/RDDATA`, B13 GND.

**Interner Anschluss X9 (Index 3)**, Tab. 3.4-16 (Z. 3612–3650, 34-polig, gerade Pins Signal,
ungerade GND): 2 `/ND` (Normal Density), 4 `/HDL`, 6 `/SE3`, 8 `/IDX`, 10 `/SE0`, 12 `/SE1`, 14 `/SE2`, 16 frei,
18 `/DIR`, 20 `/STP`, 22 `/WRDATA`, 24 `/WE`, 26 `/TRK0`, 28 `/WP`, 30 `/RDDATA`, 32 `/HDS`, 34 `/RDY`.

**Externer Anschluss X10 „Floppy"**: für den 8″-Beisteller des PC 1715 gedacht; Stecker vor Anschluss
prüfen/ändern (Tab. 3.3-17 / 3.4-17, Z. 2698–2735, 3663–3700). Signale: A2 `/MO3`, A3 `/MO1`, A4 `/RDY`,
A5 `/TRK0`, A6 `/WP`, A7 `/FW` (Fault Write), A8 `/RDDATA`, A9 `/IDXS` (Index 1) bzw. `/IDX` (Index 3),
A10 `/FR` (Fault Reset), A11 `/SE0`, C2 `/MO2`, C3 `/MO0`, C4 `/HDL`, C5 `/SE1`, C6 `/STP`, C8
`/WRDATA`, C9 `/WE`, C10 `/DIR`, C11 `/SE2`, C12 `/SE3`; Index 3 zusätzlich **B11 `/ND`, B12 `/HDS`**
(Index 1: B10–B13 „frei", d. h. **kein /HDS am X10 in der Index-1-Tabelle**
— **[unklar: Auslassung in der Tabelle oder Funktionsunterschied]**); alle B1–B9, A1, C1 GND.

### 7.4 Laufwerksauswahl / Motor / Dichte — was das Handbuch sagt
- **Laufwerkswahl:** vier getrennte Leitungen `/SE0…/SE3` (aktiv Low) — **kein** gemeinsames Select-
  Byte; Quelle der Leitungen (PIO2?) **[unklar]**.
- **Motor:** Interne Laufwerke: Motor läuft, sobald das Laufwerk selektiert ist (Jumper ML) bzw. über
  Pin 16 `/MO`; X8 führt in Index 1 **kein** Motorsignal (nur im Index 0 `/MO0` an B6), X9 (Index 3)
  ebenfalls keines. Externe Laufwerke: `/MO0…/MO3` einzeln am X10. CTC0 hat laut Text die Aufgabe
  „**Motorabschaltung der FD-Laufwerke**" (S. 3-20 / 3-43) → Motor-Timeout über CTC0-Kanal 1
  („Floppy-Disk", 09H) **[Deutung]**.
- **Dichte:** Index 3 führt `/ND` (Normal Density; X9-2, X10-B11), Index 1 hat **kein** Dichtesignal.
  FM/MFM-Wahl ist Sache des U8272 (Befehlsbit MFM); 8″ SD (FM) nur extern. Taktfrequenz 4 MHz
  (PHI) bzw. 8 MHz (2PHI_TTL) „für 8″-Floppy-Laufwerke" (§2.1) — wie der U8272-Takt auf 8″ umgeschaltet wird, ist
  **[unklar]**.
- **Seitenwahl** `/HDS` (X8-B1, X9-32, X10-B12), `/TS` (X8-A1; „Two Side" = Doppelseitenkennung
  zurück) — Letzteres als Eingang oder Ausgang **[unklar]**.

## 8. Sonstige Schnittstellen (S. 3-29…3-33 / 3-54…3-57)

- **X11 Tasten-/Anzeigemodul** (Buchsenleiste 2 × 5): A1 `/RESP`, A2 GND, A3 +12 V (Anzeige), B1 `/NMIP`,
  B2 GND, B3 −12 V (Anzeige), A5/B5 +5 V; Index 1: A4/B4 frei; **Index 3: A4 `RUN`** (Status-Anzeige
  16-Bit), **B4 `UNIT16`** (Anzeige „16-Bit-Rechner aktiv").
- **X12 Stromversorgung** (2 × 5): A1/A2/B1/B2 GND, A3 +12 V, A4/A5/B5 +5 V, B3 −12 V, B4 frei.
- **X13 Service** (2 × 29; „Seiten A und B gegenüber üblicher Zählweise vertauscht"; Index 3: Reihe C statt B):
  A3 `/CE-RES1`, A4 D7, A5 D5, A6 D3, A7 D1, A8 `/WR`, A9 `/MREQ`, A10 `/BAO`, A11 IA14, A12 IA12, A13 IA10,
  A14 IA8, A15 `/CPBAUD` (1,229 MHz), A16 IA6, A17 IA4, A18 IA2, A19 IA0, A20 `/RESET`, A21 PHI_TTL, A22 PHIE,
  A23 `/NMI`, A24 `/WAIT`, A25 `/RFSH`, A26 `/M1`, A28 IEIT, A29 +5 V; B3 `/CE-RES2`, B4 D6, B5 D4, B6 D2,
  B7 D0, B8 `/RD`, B9 `/MEMDI`, B11 IA15, B12 IA13, B13 IA11, B14 IA9, B15 `/CE-RES3`, B16 IA7, B17 IA5,
  B18 IA3, B19 IA1, B20 `/BUSRQ`, B23 `/INT`, B24 `/IORQ`, B25 2PHIE, B26 `/HALT`, B27 `/BUSAK`, B28 IEOT,
  B29 +5 V. Für den Emulator relevant: es gibt `/MEMDI`, `/BUSRQ`, `/BUSAK` am Service-Stecker (kein
  eingebauter DMA-Master außer UA858D).

## 9. Unterschiede Index 1 ↔ Index 3 (8-Bit-Karte)

| Punkt | Index 1 (Kap. 3 §3) | Index 3 (Kap. 3 §4) | Quelle |
|---|---|---|---|
| Leiterplatte | 4 Lagen | **6 Lagen** | S. 3-10 / 3-34 |
| Takt-Wickelfelder | nur XP8 (3 Brücken) | **XP7 + XP8** | Tab. 3.3-1 / 3.4-1 |
| Serielle Schnittstelle gilt als | **DÜE** (Terminalkabel 1:1) | **DEE** (Kabel gekreuzt) | S. 3-22 / 3-45; Kap. 2 §5 ↔ §6 |
| tty0 Pins (Tab. 3.3-9 ↔ 3.4-9) | 2 = 103 `RD`(E), 3 = 104 `TD`(A), 5 RTS, 8 DTR, 20 DCD | 2 = 103 `TD`(A), 3 = 104 `RD`(E), 4 RTS, 5 CTS, 6 DSR, 8 DCD, **20 = 108.2 DTR** | s. §5.3 |
| CTS/DSR tty0 | Pins 4/6 nur nach Schaltungsänderung | Pins 5/6 normal beschaltet | |
| +5-V-Ausgang an tty0–3 | nein | **ja, Pin 21** | Tab. 3.4-9/-11 |
| IFSS-Stifte (tty0) | SD+ 10, ED+ 13, ED− 14, SD− 19, Q+ 12; Sender **aktiv**, Empfänger passiv | **SD+ 19, ED+ 14, ED− 13, SD− 10**, nQ+ 12, mQ+ 16 (Nachbarkanal); Sender/Empfänger **passiv, potentialgetrennt**, auch aktiv betreibbar | S. 3-24 ↔ 3-47 |
| IFSS-Umschaltung Pin 9 | „IFSS" (mit SG verbinden) | „nIFSS" (mit SG verbinden) | gleiche Wirkung |
| Interner FDD-Stecker | X8, 2 × 13 (A/B), `/TS`, `/IDXM`, B6 frei (Idx 0: `/MO0`) | **X9, 34-polig**, `/ND`, `/HDL`, `/IDX` | Tab. 3.3-16 ↔ 3.4-16 |
| X10 externer Floppy | A9 `/IDXS`, kein /ND/HDS | A9 `/IDX`, **B11 `/ND`, B12 `/HDS`** | Tab. 3.3-17 ↔ 3.4-17 |
| Kopplung | 4 Handshake-Leitungen (`EO_8/DD_16`, `DD_8/EO_16` zusammengefasst), kein RUN | **6 getrennt** (`EO_8, DD_8, EO_16, DD_16`), **+ RUN (X2-A13)** | Tab. 3.3-15 ↔ 3.4-15 |
| X11 | A4/B4 frei | **A4 RUN, B4 UNIT16** | Tab. 3.3-18 ↔ 3.4-18 |
| X13 Reihe | A/B (Seiten vertauscht) | **A/C** | Tab. 3.3-20 ↔ 3.4-20 |
| Kopplungspartner | 16-Bit Index 1 (auch 0) | **16-Bit Index ≥ 4 zwingend** | S. 3-33 |
| Interfacekabel | Kap. 2 §5 (V: 11xx) | Kap. 2 §6 (V: 43xx, ab 3/89) | |
| Speicherbankteuerung, E/A-Tabelle, Interruptkette, Baudtabellen | identisch | identisch | wörtlich gleich |

## 10. Widersprüche

- **W1** — Bildnummer: „Bild 3.3-2" bezeichnet sowohl Steckverbinder/Wickelstifte (Z. 2051) als auch
  Teilerkette Baudraten (Z. 2513); der Verweis „vgl. Bild 3.3-2" (Z. 2494) trifft nur auf Letztere.
  In Index 3 analog „Bild 3.4-2" doppelt. Kosmetisch.
- **W2** — Kopplung Index-3 ↔ 16-Bit Index 4: Handbuch Tab. 3.7-20 (Z. 5863–5868) gibt für X2:B5
  (`DD-8`, „A") **und** X2:A9 (`E0-16`, „E") **beide** „Ausgabedaten vorhanden" an — entgegengesetzte
  Richtungen, gleicher Text; 8-Bit Tab. 3.4-15 nennt `DD_8` = „Freigabe Daten 16→8", `EO_16` =
  „Übergabe Daten 16→8". Funktion ist so nicht eindeutig.
- **W3** — 8-Bit Index 1: Tab. 3.3-9 ist die DÜE-Fassung (Pin 2 = RD, Pin 3 = TD), obwohl dort in der
  gleichen Spalte die V.24-Leitungsnummern 103/104 stehen (103 = TD nach V.24). Index 3 hat Pin 2 =
  103 = TD. Der Unterschied ist **kein Tippfehler**, sondern DÜE→DEE (Kap. 2 §5↔§6), muss aber für den
  Emulator (Pinbelegung der Terminal-Anbindung) beachtet werden.
- **W4** — „32 Daten- und 8 Handshake-Signale" (Index-1-Text der 16-Bit-Seite, Z. 4812) ↔ gezählt
  8 + 8 Daten + 6 + 4 Vektor = 26 Datenleitungen und 4–6 Handshake + 2 INT + RESET/NMI. Die Zahlen
  passen weder zu den Tabellen noch ineinander.
- **W5** — „Die beiden PIO-Ports werden für Eingabezwecke ... benutzt" (Z. 2593) ↔ `/NMI_UM` = PIO0-**B7**
  als **Ausgang** (Z. 2111) und das (ungeklärte) RESET-Freigabebit. Mindestens Port B ist gemischt
  (Modus 3 Bitbetrieb?) **[unklar]**.
- **W6** — 16-Bit-Index-4-Tab. 3.7-20 (Z. 5874): „X2:A2, A13 — RUN-LED ein/aus"; **X2:A2 trägt in
  derselben Tabelle auch `V4/16-8`** (Z. 5861). Doppelbelegung oder Tabellenfehler; 8-Bit-Seite
  Index 3 nennt nur A13 als RUN, A2 weiter als V4/16-8 (Z. 3569, 3581).
- **W7** — Kopplungsstecker der 16-Bit-Seite Index 1: Text/Tab. 3.6-20 „X2, X3", Bild 3.6-2 „X3: Kopplung" und
  **zweites „X4: Kopplung"** (Z. 4049–4059), das auch als Buchse tty4 vorkommt. Index 4 (Bild 3.7-2) ist
  konsistent (X2, X3). Bei Index 1 vermutlich Bildfehler.
- **W8** — Tabellenverweise falsch/uneinheitlich: Index 1 ruft „Tabelle 3.6-11" für serielle Kanäle
  (richtig 3.6-12), „Tabelle 6.5-6" für das MMU-Wickelfeld (richtig 3.6-6), „Tabelle 3.-7"
  (Busstecker). Kosmetisch.
- **W9** — Anlage A „PIO: 2 × UA855D (8-Bit-Karte)", „3 × UA855D (16-Bit)" ↔ Text/E/A-Tabelle: 8-Bit
  PIO0, PIO1, PIO2 (3 Stück), 16-Bit PIO0–PIO2 (3). Entweder zählt Anlage A die 8-Bit-Karte falsch
  oder PIO2 der 8-Bit-Karte ist nicht dieser Typ **[unklar]**; E/A-Tabelle 1CH–1FH ist eindeutig PIO.
- **W10** — Einzelschrittbetrieb: S. 3-20 („CTC0 ... Steuerung des Einzelschrittbetriebes") ↔ E/A-Tabelle:
  CTC0-Kanal 3 „frei", Kanal 2 „System-Kanal", Kanal 1 „Floppy-Disk". Auf der 16-Bit-Seite heißt
  CTC0-K3 „Single Step Steuerung". Zuordnung der 8-Bit-Funktionen zu Kanälen unklar.

## 11. Offene Fragen an Schaltplan/Gerät

1. **PIO2-Belegung** (1CH–1FH): A7 = RESI (gesichert); A0–A6, B0–B7: Laufwerksselect, Motor, Dichte, TC, READY,
   Index, Reset FDC, DMA-Handshake? Richtung je Bit.
2. **FDC/DMA-Verdrahtung:** DMA-Kanal (UA858D) ↔ U8272 DRQ/DACK/TC; Weg der FDC-Interrupts (INT an Z80 über
   Kette „DMA/PIO2"?); U8272-Takt (4/8 MHz), READY-Quelle, Write-Precompensation, `/FW`/`/FR` Beschaltung.
3. **CTC0-Kanäle 1 und 2:** Eingänge CLK/TRG, Ausgänge ZC/TO (Motor-Timeout? Systemuhr?). CTC0-K3 „frei"
   wirklich unbeschaltet? CTC-Eingangstakt der Baudkanäle (1,229 MHz direkt?).
4. **Kopplung 8-Bit-Seite:** Zuordnung PIO0-Bits (Port A: D/16-8? Port B: V1–V4/16-8, RDY, INT_8, NMI_UM = B7, Reset?)
   und DS8282-Bits (10H: D/8-16; 14H: V1–V6, RESET_U8000, ?); Lesbarkeit der Latches; wie
   `/RESET_U8000` gesetzt/gelöscht wird; Erzeugung des Latch-Strobes (`RDY/8-16`, `EO_8`, `DD_16`) bei
   OUT 10H/14H; Index-3-Unterschied der beiden neuen Leitungen.
5. **EPROM-Abbildung:** 2 × 2732 (2 × 4 KB) hinter `PROM_SEL`; wie A12/A13 den EPROM-Inhalt in 4K-Seiten
   abbilden (Spiegelung); WAIT-Generator-Bedingung.
6. **ADP-Verhalten** bei „kein Select aktiv" (Busabschluss, Lesewert) und bei Mehrfach-Select.
7. **Interruptvektoren/IM2-Basis** (Software), Verdrahtung IEI/IEO am Wickelfeld XP6 (Standardzustand).
8. **Programmer-Latches:** welche Adress-/Steuerbits liegen in welchem DS8282 (Bild 5.3-1).
9. **Index 3: /ND-Signalquelle** (PIO2?) und ob der U8272 FM/MFM nur per Befehl wählt.
10. **Index 1 X10-Beschaltung:** fehlt `/HDS` wirklich (B10–B13 frei) oder ist die Tabelle unvollständig?
11. **Gerätemessung:** Reset-Takt-Phase (Power-on 1 s, Taste 6 µs) für Timing-Tests am echten Gerät; welche Index-Stände
    hat das Gerät des Anwenders (Typenschild „V: abcd")?
