# EPROMmer des PRG 710 — Befund aus `PROG` und `PROG.COM` (AP-P7a)

Quelle: Disassembly der beiden Programmierprogramme (Werkzeug `tools/z80_disasm2.py`,
`--org 6000H` bzw. `--org 0100H`, `--linear`), dazu `PROG.DOK` (UDOS-Diskette
`PRG_UDOS_software`, Seite 1). Kein Kartenbefund — alles hier ist **aus der Software
belegt**; was nur daraus geschlossen ist, trägt `[?]`. Plan: `doc/design/20_prg710.md`
§3.4, AP-P7.

| Programm | System | Fassung | Datei | Ladeadresse | MD5 |
|----------|--------|---------|-------|-------------|-----|
| `PROG` (+ `OVR.PROG`, UDOS-Overlay ohne E/A) | UDOS 4.3 | „PROM - PROGRAMMER V3.1“, 29.04.88 | 9856 B, Typ P | 6000H–867FH, Puffer 4000H–4FFFH | `e652ddaf…` |
| `PROG.COM` | SCPX 1526 | „PROM - PROGRAMMER V1.1“ | 8576 B | 0100H | `b69d4afc…` |

`PROG` liegt gleich auf allen UDOS-Disketten beider Geräte (710 und 710-1), `PROG.COM` auf
allen SCPX-Disketten des 710-1. Die Hardwareroutinen beider Fassungen sind Befehl für
Befehl gleich (Unterschied unten, §5).

## 1. Ports

| Port | Baustein | Belegung | Fundstelle (V3.1 / V1.1) |
|------|----------|----------|--------------------------|
| **84H** | ZRE-PIO Port A (K2521) | **Bit 0 = PROM-Typ: 0 = 1 KB (U555), 1 = 2 KB (U2716)**; übrige Bits unbenutzt | `60E7`–`60F8` / `01E5`–`01F6`, Init `61B3` / `02AB` |
| **86H** | ZRE-PIO Steuerwort Port A | `FFH`, `00H` = Betriebsart 3, alle Bits Ausgang | `61AC` / `02A6` |
| **D0H** | PIO Port A Daten | **Datenleitungen D0–D7 des PROM**: Lesen `IN (D0H)`, beim Brennen `OUT (D0H)` | `6C41`, `6C11` / `0ABF`, `0A8F` |
| **D1H** | PIO Port B Daten | **Adresse A0–A7** | `6C0C`, `6C36` / `0A8A`, `0AB4` |
| **D2H** | PIO Steuerwort Port A | Betriebsart 3; Maske `FFH` (Lesen, Eingang) bzw. `00H` (Brennen, Ausgang) | `61BB`, Tabellen `6DC3`/`6DCA` / `02B3`, `0C4A`/`0C51` |
| **D3H** | PIO Steuerwort Port B | Betriebsart 3, Maske `00H` (Ausgang) | `61BF`, Tabellen wie oben |
| **D4H** | Steuerregister (Latch) `[?]` | Spannungen, Impuls, A8–A10 (§2) | `LD C,D4H` `6C6D` / `0AEB`, dann `OUT (C),E` |

**Folgerungen:**
- D0H–D3H ist eine **Z80-PIO mit B/A an AB0 und C/D an AB1** (D0 A-Daten, D1 B-Daten,
  D2 A-Steuer, D3 B-Steuer) — dieselbe Ordnung, die die Kartenbeschreibung der K2521 für
  84H–87H nennt. **Damit ist auch `K2521::Config::pio_ab_an_a0` belegt** (das Steuerwort
  geht an 86H, die Daten an 84H; mit der `Z80PIO`-Zählung wäre 86H B-Daten).
- Der Datenweg ist **bidirektional über die Maske**: zum Brennen schaltet das Programm
  Port A per `OTIR` auf Ausgang (`D2 02 CF 00` + `02 CF 00` an D3H, `6DCA`), danach zurück
  auf Eingang (`D2 02 CF FF` + `02 CF 00`, `6DC3`).
- **Ein Sockel**: es gibt keine Sockelwahl; „QUELL-PROM GESTECKT ?“ / „COPY-PROM STECKEN“ =
  der Anwender wechselt das PROM im selben Sockel. Typ = Bit 0 von 84H.
- **Kein Löschen über Software**: kein Port, kein Menüpunkt. „E“ ist nur die
  Löschkontrolle (alle Bytes FFH). Gelöscht wird im UV-Gerät.

## 2. Steuerregister D4H `[?]` (Bitbedeutung aus dem Ablauf geschlossen)

| Bit | Wirkung (geschlossen) | Beleg |
|-----|-----------------------|-------|
| 0, 1 | **Programmierspannung(en) ein** — immer gemeinsam gesetzt/gelöscht | `6B11`–`6B15`, `6B72`/`6B74` |
| 2 | **Programmierimpuls** | `6B4D`/`6B55`, `6BA2`/`6BAE` |
| 3 | U555: **erste Versorgung** (vor Bit 4 ein, nach Bit 4 aus — die Folge der −5 V eines 2708); U2716: im Programmierbetrieb zusätzlich gesetzt (vermutlich /OE hoch) | `6C5E`, `6CB6`, `6B0D` |
| 4 | **Betriebsspannung des Sockels** | `6C79`, `6CAA` |
| 5–7 | **A8–A10** (E = (E AND 1FH) + 20H · Seite) | `6C86`–`6C99`, `6CEA` |

Abläufe:
- **Einschalten** (`6C59`): `E` = 08H (U555) bzw. 00H (U2716) → 100 ms → Bit 4 → 50 ms.
- **Ausschalten** (`6C9E`): U2716 `E` = 00H; U555 Bit 4 aus → 10 ms → Bit 3 aus.
- **Lesen** (`6C33`): `OUT (D4H),E` (A8–A10), `OUT (D1H),L`, zwei Leerunterprogramme,
  `IN (D0H)`.
- Die Wartezeiten zählt `6C48` (BC = n → n × ≈ 1,0 ms, Innenschleife `LD B,BBH : DJNZ`).
  **Kein CTC**, alles Befehlsschleifen bei 2,4576 MHz.

## 3. Brennen

Vorher: Einschalten, bei U2716 Bit 3, dann **Bit 0+1 (Programmierspannung)**, 1 s warten,
Port A der PIO auf Ausgang.

| | U555 (1 KB, 2708-artig) | U2716 (2 KB) |
|---|---|---|
| Verfahren | **100 Durchläufe** über den ganzen Bereich (BCD-Zähler 00–99, `6B3C`–`6B6A`) | **ein Impuls je Byte** (`6B97`–`6BC0`) |
| Impuls | `LD B,96H : DJNZ` = **≈ 1970 Takte ≈ 0,80 ms** | `6C48` mit BC = 32H = **≈ 125 000 Takte ≈ 50,8 ms** |
| Bytes FFH | V3.1: **auch FFH wird gepulst**; V1.1: übersprungen | übersprungen (`6C09`: `INC A` → Z) |
| zwischen den Durchläufen | V3.1: Ausgleichspause ((400H − n)/2 + 1) ms (`6DEE`), V1.1: keine | — |
| Summe | ≈ 100 × 0,8 ms = 80 ms je Byte | 50,8 ms je Byte ≠ FFH |

Danach: Programmierspannung aus (Bit 0/1), 25 ms, Ausschalten, **Vergleich** PROM ↔ Puffer
(„PROM-FEHLER ADRESSE … DATEN-BYTE … PROM-BYTE“). Vor dem Brennen wird auf Löschzustand
geprüft („PROM NICHT LEER, TROTZDEM PROGRAMMIEREN“).

**Eigenheit (beide Fassungen):** nach dem Brennen ruft das Programm `6DD1` (`OTIR`), das C
auf D3H stehen lässt; die folgenden `OUT (C),A`/`OUT (C),E` (`6B76`, `6C9E`) gehen deshalb
**an das Steuerwort von PIO-Port B** statt an D4H (Bit 0 = 0 → Interruptvektor, folgenlos).
Das Steuerregister behält die Spannungen bis zum nächsten Einschalten (`6C59` der
Vergleichslesung, ≈ 25 ms später). Das Modell muss das ohne Fehlermeldung durchlassen.

## 4. Bedienung (`PROG.DOK`, Menütexte)

Start: „IST EINE AENDERUNG DIESER WERTE ERWUENSCHT (J/N) ?“ (Verarbeitungsbreite 1/2 Byte,
PROM-Größe **1 oder 2 K-Byte** = Typ, Prozessor U880/I8086/U8000, Negation). Vorgabe
**2 KB**. Antworten sind Einzeltasten (kein ET); ET am Prompt `#` ist „KOMMANDO EXISTIERT
NICHT“.

| Befehl | Wirkung | Hardware |
|--------|---------|----------|
| `P` | Datei (Typ P) → PROM, danach Vergleich | Lesen, Brennen |
| `R` | PROM → Datei (Typ P) | Lesen |
| `C` | PROM kopieren: Quell-PROM lesen (Puffer 4000H), „COPY-PROM STECKEN“, brennen | Lesen, Brennen |
| `E` | Löschkontrolle: „PROM GELOESCHT“ / „PROM NICHT GELOESCHT“ | Lesen |
| `V`, `S` | Vergleich PROM ↔ Datei bzw. ↔ PROM; Suchen in Dateien | Lesen |
| `T` | Typ, Breite, Prozessor, Negation ändern (setzt 84H Bit 0) | 84H |
| `D` | Puffer anzeigen/ändern | — |
| `M` | UDOS-Debugger (nur V3.1) | — |
| `H`, `Q` | Hilfe, Ende | — |

Prüfsumme in `C`, `P`, `R`: Summe mod 65536 und SDLC-CRC (reine Software).

## 5. Unterschiede V3.1 (UDOS) ↔ V1.1 (SCPX)

- U555: V1.1 überspringt FFH-Bytes beim Pulsen, V3.1 pulst alle und hält die
  Ausgleichspause ein.
- V1.1 fragt die Link-Adresse der Datei ab („EINGABE LINK-ADRESSE“), kein `M`.
- Ports, Bitbelegung, Impulse, Wartezeiten: gleich.

## 6. Was nicht aus der Software hervorgeht (Fragen, Plan §8.6)

1. Welche der Bits 0/1 welche Spannung schaltet (U555: 26 V an PROGRAM und +12 V an CS/WE?
   U2716: 25 V an VPP?) — das Modell verlangt beide.
2. Was die Datenleitungen beim Brennen führen, wenn Port A auf Eingang steht (Modell:
   offen = 1, also kein Brennen).
3. Was am Gerät bei **falschem Typ** im Sockel geschieht (U2716 mit U555-Einstellung u. ä.).
   Modell: Lesen geht (Adresse auf die Größe des PROM begrenzt), Brennen bleibt ohne
   Wirkung, Protokolleintrag.
4. Ob D4H allein dekodiert ist oder D4H–D7H spiegeln.
5. **Ob der PRG 710-1 den EPROMmer hat** — `PROG`/`PROG.COM` liegen auf seinen Disketten;
   die ATP 590068 (8279) braucht er für die Tastatur nicht. Modell: in beiden Varianten
   gesteckt, abschaltbar (`Config::eprommer`).
6. Löschlampe softwaregesteuert: unter UDOS/SCPX nicht (kein Port in `PROG`), unter BS600
   laut [Web] ja — ohne BS600-Diskette nicht prüfbar.
