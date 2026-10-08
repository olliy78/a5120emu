# P8000 — Hauptspeicher-Ausbau der 16-Bit-Seite (Befund P23b)

Stand 2026-10-08, AP P23b (Entwurf 25 §12 Punkt 7).  Anlass: Anwendertest — „Die RAM-Konfiguration
sieht eigenartig aus.  Standard war 4 × 256 KB, es gab auch 4 × 1 MB, später eine 16-MB-Karte, von
der WEGA nur knapp 8 MB nutzt."

Kennzeichnung wie in den anderen P8000-Befunden: *gelesen* = steht so in der Quelle; *abgeleitet* =
aus Quelltext/Plan geschlossen, nicht am Gerät gemessen.

## 1. Quellen

| Quelle | Inhalt |
|--------|--------|
| Handbuch §8/§9 = `doc/p8000/hw_16bit.md` §6 | DRAM-Karte Index 0/1 (256 KB) und Index 3 (1 MB bzw. 256 KB), Moduladresse, Parität, Refresh |
| robotrontechnik.de, *RAM-Karte für P8000* (`/html/eigenbau/p8000ram.htm`, Stand 10.02.2020) mit **Schaltplan** (`p8000ram_schaltplan.pdf`) und **GAL-Gleichungen** (`p8000ram.zip` → `P8000RAM.EQN`, Grömer/Tiffe 25.09.2009) | die 16-MB-Karte |
| MON16 `firmware/MON16/p.test.s` (OlliL-Repo, `~/projects/robotron/P8000/github_P8000/`) | Testschritt 70/71 (MAXSEG), 72–76 (RAM-Test) |
| WEGA `src/uts/sys/main.c`, `src/uts/conf/mch.s`, `src/head/sys/map.h` | Speichererkennung des Kerns, physischer Zugriff über die MMU, `coremap` |
| `doc/p8000/schaltplan_16bit.md` §B4 | MMU aus ⇒ phys. = SN · 64 K + Offset, **A23 = 0** |
| `~/projects/robotron/P8000/doc/install_WEGA_3.1.log` | Gerät des Anwenders: `MAXSEG=<0F>`, WEGA „user memory size = 876288" (1 MB gesamt) |

## 2. Welche Ausbauten es gab

| Ausbau | Karten | Adressen (MMU aus) | MAXSEG | Parität |
|--------|--------|--------------------|--------|---------|
| **Standard: 4 × 256 KB = 1 MB** | 4 × DRAM-Karte 256 KB (Index 0/1 oder Index 3 Var. 2), Modul 0–3 | 000000–0FFFFF | `0F` | ja |
| 2 × 256 KB = 512 KB | 2 Karten, Modul 0–1 | 000000–07FFFF | `07` | ja |
| 1 × 1 MB | DRAM-Karte Index 3 Var. 1, Modul 0 | 000000–0FFFFF | `0F` | ja |
| **4 × 1 MB = 4 MB** | 4 × Index 3 Var. 1, Modul 0–3 | 000000–3FFFFF | `3F` | ja |
| **RAM-Karte 16 MB** (2009) mit 2 / 4 / 8 / 16 MB | 1 Karte, keine Moduladresse | ab 000000, bei 8/16 MB **ohne Segment 7FH** | `1F` / `3F` / `7E` / `7E` | nein |

- *gelesen* (robotrontechnik.de): „Viele P8000 … sind nur mit 256-KByte-RAM-Karten ausgerüstet, womit
  sich also in der Summe nur 1 MB Speicher realisieren lässt."  Steckplätze für Uhrenkarte/DOS-
  Erweiterungsmodul verringern das weiter — der Speicherbus hat vier Plätze (X10–X13).
- Das Anwenderprotokoll mit `MAXSEG=<07>` entspricht 512 KB (zwei 256-KB-Karten); das
  Installationsprotokoll (`install_WEGA_3.1.log`) zeigt `MAXSEG=<0F>` = 1 MB, und WEGA 3.1 läuft dort
  mit 876 288 Byte Anwenderspeicher.

### 2.1 Robotron-Karten (Index 0/1/3)
*gelesen* (Handbuch §8.4/§9.4): Moduladresse per Wickelbrücken; die 256-KB-Karte dekodiert A18–A23
(Modul 0–63, 256-KB-Schritte), die 1-MB-Karte A20–A23 (0–15).  Je Byte ein Paritätsbit, Fehler →
`PE−` (Open-Collector) und rote LED.  „Mehrere Karten werden zu einem zusammenhängenden Bereich
gelegt" — eine Sperre gegen Lücken gibt es nicht.  Refresh RAS-only über die CPU (Wert 15 im
Refreshzähler).  Ein Kartenschaltplan fehlt weiterhin (Annahmen [D1]–[D5] in `dram16.h`).

### 2.2 RAM-Karte 16 MB (Kleinserie 2009)
*gelesen* aus Schaltplan und GAL-Gleichungen:
- Vier 30-polige SIMMs (×8, **ohne Paritätsbit**) zu 1 MB oder 4 MB, paarweise (High-/Low-Byte):
  Brücke **J2** = 1 oder 2 Bänke, **J3** = 1-MB- oder 4-MB-SIMMs ⇒ **2, 4, 8 oder 16 MB**.
- **Keine Moduladresse.**  Der GAL U9 wählt allein aus A21–A23 und den Brücken:
  `2MB = /A21·/A22·/A23`, `4MB = /A22·/A23` (A21 = Bank), `8MB = /A23·7FH`, `16MB = A23` (Bank 1)
  plus 8-MB-Term (Bank 0).  Die Karte liegt also **immer ab 000000H**.
- **U16 (7430, NAND über A16–A22)** liefert `7FH−`: im 8-MB-Term (A23 = 0) wird **Segment 7FH =
  7F0000–7FFFFF nicht gewählt**.  Bei 16 MB bleibt der obere Bereich 800000–FFFFFF vollständig, auch
  FF0000–FFFFFF (dort gilt der 16-MB-Term ohne `7FH`).  Text der Seite: „Bei 8 MB hat die Firmware
  des Rechners einen Bug.  Um dem entgegenzuwirken, ist der IC U16 vorhanden … es können max. 8 MB
  minus 64 KByte genutzt werden.  Sollte es mal ein Firmware-Update geben, wäre U16 zu entfernen."
- `PE−` (B32) und `CLR-PAR−` (B24) sind am Stecker **nicht angeschlossen**; Refresh anfangs RAS-only
  (U12), seit Ende 2009 CAS-before-RAS.  U17 + LEDs zeigen den Zugriff an.

## 3. Wie MON16 und WEGA den Speicher finden

**MON16, Testschritt 70** (*gelesen*, `p.test.s` „Test auf externen Speicher"): MMU aus, segmentierter
Zugriff, vom Segment 0 aufwärts je Segment Offset 8000H, sonst FFFEH beschreiben und zurücklesen; das
erste Segment, in dem beides scheitert, beendet die Suche, das vorige ist `MAX_SEGNR` (= Ausgabe
`MAXSEG=<xx>`).  Das erste **Loch** beendet die Suche, auch wenn darüber noch Karten stecken.  Mit MMU
aus ist die physische Adresse SN · 64 K + Offset und **A23 = 0** (`schaltplan_16bit.md` §B4): MON16
sieht höchstens 8 MB.

**WEGA** (*gelesen*, `main.c`): ab dem Ende des Kerns je Click (256 B) `spbyte(pa, 0x19)` /
`fpbyte` — physisch über einen umgeschriebenen Code-MMU-Deskriptor (Segment 3CH, Basis = pa >> 8,
`mch.s`), also mit 24 Bit —, bis ein Click nicht antwortet.  Auch hier: das erste Loch beendet die
Zählung.  Die gefundenen Clicks gehen als **ein** freier Block in die `coremap`.

## 4. Warum „knapp 8 MB"

1. **Firmwarefehler MON16 bei 128 Segmenten** (*abgeleitet* aus `p.test.s`): Testschritt 70 zählt
   die Segmentnummer mit `incb rh6` hoch und hat **keine Obergrenze**.  Die Segmentnummer eines
   Registerpaars umfasst nur Bit 8–14 (`z8000.cpp` maskiert ebenso mit 7FH); nach 7FH folgt 80H ≙
   Segment 0, das antwortet — antworten alle 128 Segmente, findet die Schleife nie ein Loch.  Das
   ist der „Bug bei 8 MB", den U16 umgeht: Segment 7FH fehlt, MON16 meldet `MAXSEG=<7E>`.
2. **WEGA zählt bis zum Loch bei 7F0000H** (*abgeleitet* aus `main.c`): auf der 16-MB-Karte endet die
   Zählung am gesperrten Segment 7FH ⇒ höchstens 7F0000H = 8 MB − 64 KB, abzüglich Kern.  Die obere
   Hälfte (800000–FFFFFF) bleibt ungenutzt, obwohl die MMU sie adressieren könnte.
3. Ohne das Loch wären es beim 16-MB-Ausbau 65 536 − Kern Clicks in **einem** `coremap`-Eintrag;
   `struct map` führt `m_size` als **`short`** (`map.h`) — über 32 767 Clicks (8 MB) liefe die Größe
   über (*abgeleitet*).  Die Seite nennt als Grund „dazu müssten die MMUs der Rechner ständig
   umgeschaltet werden" (*gelesen*, nicht weiter belegt).

Ergebnis: Der P8000 adressiert über die MMU physisch 16 MB (A0–A23), ohne MMU 8 MB; MON16 und WEGA
nutzen von der 16-MB-Karte die untere Hälfte ohne Segment 7FH.

## 5. Was der Emulator kann (nach P23b)

- `P8000Dram16` (`core/cards/p8000/dram16.{h,cpp}`): bis 4 Karten, Typen `K256`, `M1` (Robotron,
  Moduladresse, Parität) und **`R2`/`R4`/`R8`/`R16`** (RAM-Karte 16 MB: ab 0, ohne Moduladresse, ohne
  Parität, bei 8/16 MB ohne Segment 7FH).  Ohne MEMSEL kein Zyklus (offener Bus = FFFFH der Karte16).
  Überlappung, doppelte Belegung ab 0, Moduladresse außerhalb, > 4 Karten ⇒ Klartextfehler.
- Konfigurationstext `dram=` (C-ABI `k1520_create_p8000`, `P8000Dram16::parse`): Langform wie bisher
  (`1M@0+256K@4`, neu `16M@0` …), Kurzform `4x256K`, `NxT` (N = 1–4, T = 256K|1M, ab Modul 0), `2M`,
  `4M`, `8M`, `16M`.  Fehler kommen als `P8000: …` über `k1520_last_init_error`.
- `P8000Dram16::maxSegment` rechnet Testschritt 70 nach.  Am laufenden MON16 3.1 geprüft:
  2 × 256 K ⇒ `07`, 4 × 256 K ⇒ `0F`, 4 × 1 M ⇒ `3F`, 16 MB ⇒ `7E` (Wächter
  `P8000Karte16Ram.*`, die beiden langen in `p8000_ram_konfig_lang` / `tools/dev.sh test-format`).
- **Vorgaben:** Kern (`P8000Machine::Config::dram`) unverändert eine 1-MB-Karte (`1M@0`) — alle
  Kern-, M2- und WEGA-Wächter bleiben gleich.  Programm `p8000emu`: **4 × 256 KB** (Standardausbau).
  Gleicher Adressraum, gleiches MAXSEG, WEGA 3.1 läuft am Gerät des Anwenders damit (1 MB).
- **Save-State:** die Kartentypen stehen als Byte im P8KS-Fingerabdruck und im DRAM-Abschnitt v1; die
  neuen Werte sind angehängt, ältere Stände laden unverändert.  Eine andere Bestückung wird beim
  Laden abgewiesen — deshalb zieht die Oberfläche alte Werte auf die **gleiche** Bestückung um
  (`1M@0` → `1x1M`), nicht auf die neue Vorgabe.
- Oberfläche: Auswahl in *Einstellungen ▸ Allgemein ▸ Hauptspeicher (16-Bit)* (`app/p8000_ram.py`),
  Schlüssel `general.dram`; eine eigene gültige Langform aus `p8000emu.yaml` erscheint als
  „benutzerdefiniert".

**Nicht nachgebildet / offen:** RAM-Karte ohne U16 (nach einem Firmware-Update) — dann liefe MON16
Testschritt 70 endlos (§4.1), nicht geprüft; WEGA mit der 16-MB-Karte am Emulator nicht gebootet
(Erwartung nach §4.2: Anwenderspeicher ≈ 7F0000H − Kern); Refreshart der RAM-Karte (ohne Wirkung,
[D4]); Uhrenkarte/DOS-Erweiterungsmodul auf den Speicherbus-Plätzen.
