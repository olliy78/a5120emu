# PC 1715 — Tastatur (U880 + EPROM S600): Auswertung für den Nachbau

Stand 2026-10-03, AP-0d. Quellen und Belegstufen: **[SH]** Servicehandbuch `pc_serv.pdf`
§1.8, **[Plan]** `pc_tasts.pdf` (Schaltbild, Vektortext ausgewertet), **[ROM]** S600 =
`doc/EPROMS/PC1715/pc1715_s600_tastatur.bin` (bytegleich zu `eprom/s600.bin`, CRC32 `B7070122` wie in MAME), **[LAUF]** das ROM auf dem `Z80`-Kern des Repos mit gestellter Matrix ausgeführt
(Referenzharness unten; jede Zahl mit dieser Marke ist gemessen, nicht gedeutet), **[MAME]**
`rt1715.cpp` (nur Gegenprobe), **[CPA]** `CPA_Workbench/src/pc_1715/biopkbd*.mac`.

## 1. Kurzfassung

* Die Tastatur-CPU ist ein **U880 ohne RAM, ohne Stapel**: das ROM besteht aus Registerarbeit
  (alle 16 Register + Schattensatz + `I`-Register als Speicher). Kein `CALL`/`PUSH`/`RET` im
  Codepfad; keine Speicherschreibzugriffe [LAUF]. Das ROM ist als 2 KB über den **ganzen**
  Adressraum gespiegelt (Sprünge gehen nach `4012H`, `4148H`, `425AH` …; die Codetabellen werden
  über `LD SP,4460H/4560H` + `LD E,(HL)` gelesen) — [MAME] spiegelt mit `mirror(0xf800)` ebenso.
* **Matrix 13 Spalten × 8 Zeilen.** Spalte *c* = Adressbit A*c* (genau **eines** je `IN`), `IN` mit
  **A15 = 1**; Zeilen = Datenbit D0–D7. **Aktivpegel: gedrückt = 1, nicht gedrückt = 0**
  (Transistor V4.x schaltet den Latch A4 (8282) auf HIGH, [SH 1.8.3.2.3]). Gegenprobe [LAUF]: mit
  „gedrückt = 0“ (Ruhewert FFH) erzeugt das ROM **nie** ein Zeichen.
* **Ausgabe**: `OUT (C),A` bei **A15 = 0**; Bit = **D0**, Takt = **/WR** (je `OUT` ein Taktimpuls
  = ein Bit). **8N1, LSB zuerst**, Leerlauf = 1, **kein Paritätsbit**; je Taste **zwei Rahmen**:
  **Statusbyte `E0H…EFH`** und danach **Zeichencode**. Beim Loslassen kommt nichts.
* Das ROM sendet **nichts bei Reset** und nichts, solange keine Taste (oder Sondertaste) betätigt
  wird. Empfänger ist SIO-A, Rx-Takt = /WR der Tastatur (Plan §3.6).

## 2. Hardware [SH 1.8], [Plan]

| | |
|---|---|
| CPU | U880 (Z80), 2-K-EPROM A8 (U556 = U2716) |
| Takt | RC-Taktgenerator aus TTL-Gattern (A7, R10.3 220 Ω, C3 2200 pF): **700 kHz ± 10 %** [SH 1.8.3.4]; MAME rechnet mit 683 kHz. Für den Nachbau 700 kHz nehmen — die Absolutzeit geht nur in Latenz und Wiederholrate ein, der Empfänger wird von /WR getaktet |
| Reset | `/RS` low ca. 100 ms nach dem Einschalten, dann high [SH 1.8.3.3]; **keine Rücksetzung durch den Rechner** (Tastatur hat keinen Rückkanal) |
| WAIT/INT/NMI/BUSRQ | unbeschaltet (Pull-ups), kein Interrupt im ROM |
| Matrix | 13 Spalten (Beschriftung im Schaltbild `00 10 20 30 40 50 60 70 08 18 28 38 48` = Spalte 0…12), 8 Zeilen (Transistoren V4.1…V4.8) → 8282 (A4) |
| LEDs | zwei Flipflops DL074 (A6A: D = A13, A6B: D = A14), je eine Leuchte über 130 Ω an /Q |
| Ausgang | `D0` und `/WR` je über einen 7406-Treiber (A1B, A1A) zum Rechner: „TA DATEN“, „TA TAKT“ |

### Portdecodierung (Tastatur-CPU)

| `IN`-Adresse | Wirkung |
|---|---|
| **A15 = 1**, genau ein Bit A0…A12 = 1 | Latch A4 liest die Zeilen der gewählten Spalte auf D0–D7; **gedrückt = 1** |
| A15 = 0 | Latch hochohmig; der gelesene Wert ist **gleichgültig** (das ROM verwirft ihn; mit 00H/FFH/5AH/A5H identisches Verhalten [LAUF]). **A13/A14 dieser `IN`s sind der Leuchtenwert** (s. §8) |
| **`OUT`**, A15 = 0 | D0 = Datenbit, /WR = Takt; A0–A14 beliebig [SH 1.8.3.2.3] |

Die CPU führt 16-Bit-Portadressen **über `IN r,(C)`/`OUT (C),r`** aus (BC = Adresse); der Kern
muss `readPort`/`writePort` mit dem **vollen** BC liefern (der `Z80`-Kern des Repos tut das).

## 3. Abfrage: Reihenfolge, Takt, Entprellung [LAUF]

* Abfragereihenfolge der Spalten je Durchlauf: **7, 6, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 0**
  (Portadressen `8080H, 8040H, 8020H, 9000H, 8010H, 8800H, 8008H, 8400H, 8004H, 8200H, 8002H,
  8100H, 8001H`).
* **Ein Durchlauf = 5865 Takte** (8,4 ms bei 700 kHz); vor jeder Spalte steht ein `IN` mit A15 = 0
  (Leuchtenwert), nach Spalte 0 geht es von vorn los. Zwischen den Durchläufen ändert sich nichts.
* **Entprellung**: eine Taste muss in **drei aufeinanderfolgenden Durchläufen** gedrückt gesehen
  werden [SH 1.8.3.2.3]. Gemessen: Druck von 12 000 Takten (2 Durchläufe) bleibt unbemerkt,
  18 000 Takte genügt. **Latenz Druck → erster Rahmen ≈ 21 700 Takte (31 ms)**, je nach Phase
  ± 5900 Takte.
* **Roll-over / Doppeltastenerkennung**: wird eine **zweite** Taste gedrückt, solange eine
  erste gehalten wird, geht deren Code **sofort** (nach 3 Durchläufen) hinaus. Werden **zwei
  Tasten im selben Durchlauf neu erkannt**, wird **nichts** gesendet, bis eine losgelassen ist;
  die gehaltene sendet dann (Fehlbedienungsunterdrückung [SH]).
* **Autorepeat nur mit der Taste REP** (Matrix 8,6): ohne REP wird eine gehaltene Taste auch nach
  28 s nicht wiederholt [LAUF]. Mit REP gedrückt und Zeichentaste gehalten: erster Rahmen nach
  der Entprellung, **Wiederholung beginnt ≈ 0,67–0,69 Mio Takte (≈ 1 s) später**, dann alle
  **70 718 Takte (≈ 101 ms, ≈ 9,9/s)**; jede Wiederholung ist wieder Status + Zeichen.
* Beim Loslassen: **kein Break-Code**.

## 4. Bitrahmen der Ausgabe [LAUF]

Jeder `OUT (C),A` ist **ein Bit** (D0) und **ein Taktimpuls** (/WR). Rahmen (je 13 `OUT`):

```
 1  1  0  d0 d1 d2 d3 d4 d5 d6 d7  1  1
 |  |  |  `-- 8 Datenbit, LSB zuerst --'  `- Stopp/Leerlauf
 |  |  `-- Startbit
 `--`-- Leerlauf (zwei Impulse)
```

Zeitabstände der `OUT`-Befehle in Taktzyklen der Tastatur-CPU (Zählung am Ende der Instruktion;
gemessen am Beispiel „a“):

| Folge | Abstand |
|---|---|
| Leerlauf1 → Leerlauf2 → Startbit | 16, 16 |
| Startbit → d0 | 23 (Leerlauf2 → Start des 2. Rahmens: 20) |
| d0 → d1 … d6 → d7 | **32** je Bit (45,7 µs bei 700 kHz) |
| d7 → Stopp1 → Stopp2 | 34, 16 |
| Ende Rahmen 1 → Beginn Leerlaufpaar Rahmen 2 | 160 |
| Start Rahmen 1 → Start Rahmen 2 | **493** (0,70 ms) |

Folgerungen für den Empfänger (SIO-A, WR4 = 04H, WR3 = C1H, Rx-Takt = /WR, ×1):

* Pro Rahmen **zwei Bytes in 0,7 ms** hintereinander: der SIO-Empfang braucht **mindestens zwei
  Zeichen Puffer** (die CP/A-Abfrage läuft nur alle 5 ms über den Zeittakt, `biopkbd.mac`
  `kbdst1`). Der Z80SIO-Kern muss seine Empfangs-FIFO-Tiefe entsprechend führen.
* Es gibt **keinen** durchlaufenden Takt: der SIO sieht Taktflanken **nur, wenn die
  Tastatur-CPU schreibt**. Im Nachbau ist es am einfachsten, die `OUT`-Folge in der Tastaturklasse
  zu Rahmen zusammenzusetzen (Start = 0 nach ≥ 1 Eins, dann 8 Datenbit, dann Stopp) und das
  Byte bei Rahmenende an Kanal A des SIO zu geben; die Taktzeit (≈ 0,42 ms je Rahmen) fällt
  gegenüber dem 5-ms-Abfragetakt des BIOS nicht ins Gewicht.
* Bitwerte sind **1:1** die SIO-RxD-Pegel (Startbit = 0). Die 7406-Inverter (D0, /WR) und die
  Gegeninverter auf der ZRE heben sich auf — maßgeblich ist, dass CP/A am SIO ein 8N1-Zeichen
  mit Leerlauf 1 erwartet. [MAME] gibt `rxa_w(D0)` und je Schreiben einen `rxca`-Impuls 0→1.

## 5. Inhalt der zwei Rahmen

**Rahmen 1 = Statusbyte `E0H | …`**, bei **jedem** Tastendruck vor dem Zeichen:

| Bit | Bedeutung (Zustand beim Senden) |
|---|---|
| 0 (01H) | **CTRL** gedrückt (Matrix 8,0) |
| 1 (02H) | **Shift** gedrückt (links 8,1 oder rechts 8,4) |
| 2 (04H) | **SI/SO**-Zustand aktiv („Zeichensatz 2“, rastend) |
| 3 (08H) | **LOCK** (Feststellung) aktiv (rastend) |
| 4–7 | immer `E` (1110) |

CP/A wertet nur Bit 0 (`ctrlst`) und Bit 2 (zweiter Zeichensatz, `zs2var`) aus (`biopkbd.mac`
`kbdst1`: Byte ≥ E0H = Status); Code < E0H ist die Taste.

**Rahmen 2 = Zeichencode** aus der Codetabelle (§6). Die Tabelle wählt das **ROM** nach
**Shift/LOCK**: vier Tabellen (`0460H` normal, `04E0H` Shift, `0560H` LOCK, `05E0H` Shift+LOCK);
**LOCK schaltet nur Buchstaben auf Großschreibung** (die Zifferntasten bleiben unverschoben,
Tabelle 3 = Tabelle 1 außer a–z); **Shift+LOCK == Shift** [LAUF, Tabellen gegen das ROM
geprüft: für alle 95 geprüften Positionen — Spalte 8 und (4,3) ausgenommen — stimmt die gemessene Ausgabe mit der Tabelle, in allen vier Zuständen]. **CTRL
verändert den Code nicht** (nur Statusbit 0; `CTRL`+`4` sendet `E1 34`), ebenso SI/SO.

Unbelegte Tabellenstellen liefern **`00H`** (Zeichen 00H, kein Fehler).

## 6. Matrix und Codetabelle (S600)

Zelle = Code unverschoben / Code mit Shift [Taste im Schaltbild `pc_tasts.pdf`]. `Nx` =
Ziffernblock, `F1…F15` = Funktionstasten (CP/A-Namen aus `biopkbd.mac`), `S` = Taste „S“. Die
Positionen **sind gegen die MAME-Matrix und das Schaltbild abgeglichen**: alle Zeichentasten
stimmen mit MAME überein außer (3,5) (MAME: ' vom Hosttastenlayout; ROM: `:`/`*`) und (3,1)
(MAME/Schaltbild: keine Taste, ROM trägt dort `ET`). Zeilen *r* = Datenbit Dr.

| Spalte | Zeile 0 | Zeile 1 | Zeile 2 | Zeile 3 | Zeile 4 | Zeile 5 | Zeile 6 | Zeile 7 |
|---|---|---|---|---|---|---|---|---|
| 0 (A0, `00`) | `r` / `R` [D04] | D0 S [A54] | `4` / `$` [E04] | D0 S [B54] | `v` / `V` [B04] | `f` / `F` [C04] | CE CE [D54] | BD N- [C54] |
| 1 (A1, `10`) | `e` / `E` [D03] | AC N, [A53] | `3` / `#` [E03] | B3 N3 [B53] | `c` / `C` [B03] | `d` / `D` [C03] | B9 N9 [D53] | B6 N6 [C53] |
| 2 (A2, `20`) | `w` / `W` [D02] | BB N00 [A52] | `2` / `"` [E02] | B2 N2 [B52] | `x` / `X` [B02] | `s` / `S` [C02] | B8 N8 [D52] | B5 N5 [C52] |
| 3 (A3, `30`) | `@` / `` ` `` [D11] | 9E ET [–] | `-` / `=` [E11] | 9D <-' [A15] | 9E ET [A10] | `:` / `*` [C11] | 00 [–] | 8A Curv [A16] |
| 4 (A4, `40`) | `p` / `P` [D10] | 00 [–] | `0` / `_` [E10] | 00 [–]* | `/` / `?` [B10] | `;` / `+` [C10] | D2 F2 [E52] | D1 F1 [E51] |
| 5 (A5, `50`) | `[` / `{` [D12] | 00 [–] | `^` / `~` [E12] | 8C '\ [B16] | `z` / `Z` [B01] | `]` / `}` [C12] | 82 INS [D16] | 8B Cur^ [C16] |
| 6 (A6, `60`) | `u` / `U` [D07] | 8E F15 [A17] | `7` / `'` [E07] | 86 Cur-> [B17] | `m` / `M` [B07] | `j` / `J` [C07] | 7F DEL [D17] | 89 ->\| [C17] |
| 7 (A7, `70`) | `q` / `Q` [D01] | B0 N0 [A51] | `1` / `!` [E01] | B1 N1 [B51] | `\` / `\|` [B00] | `a` / `A` [C01] | B7 N7 [D51] | B4 N4 [C51] |
| 8 (A8, `08`) | **CTRL** [D04] | **Shift links** [B99] | 1B ESC [E00] | – (frei) | **Shift rechts** [B11] | **LOCK** [C00] | **REP** [E15] | **SI/SO** [E16] |
| 9 (A9, `18`) | `i` / `I` [D08] | CD F14 [A56] | `8` / `(` [E08] | C2 F13 [B56] | `,` / `<` [B08] | `k` / `K` [C08] | C1 F11 [D56] | C0 F12 [C56] |
| 10 (A10, `28`) | `o` / `O` [D09] | D3 F3 [E53] | `9` / `)` [E09] | D4 F4 [E54] | `.` / `>` [B09] | `l` / `L` [C09] | CF F5 [E55] | 83 F10 [E56] |
| 11 (A11, `38`) | `t` / `T` [D05] | A3 F9 [A55] | `5` / `%` [E15] | A2 F8 [B55] | `b` / `B` [B05] | `g` / `G` [C05] | A0 F6 [D55] | A1 F7 [C55] |
| 12 (A12, `48`) | `y` / `Y` [D06] | 20 SP [A05] | `6` / `&` [E16] | 88 Cur<- [B15] | `n` / `N` [B06] | `h` / `H` [C06] | 8D ->(Tab) [D15] | 87 \|<- [–] |

\* **Positionen ohne Taste im Schaltbild** (3,1), (3,6), (4,1), (4,3), (5,1), (8,3), (12,7): das
ROM trägt dort `00` bzw. (3,1) `ET`; **(4,3)** ist eine Eigenheit des ROMs: Berührung schaltet wie
SI/SO den Zustand um **und** sendet einen Rahmen mit Code `00` (Status zeigt dann den neuen Zustand).
Eine reale Taste gibt es dort nicht — im Nachbau wegzulassen. Ebenso doppelt vorhanden: `S` in (0,1)
und (0,3) (zwei Ziffernblock-Tasten „S“, MAME „Keypad S *1/*3“), `ET` in (3,1)/(3,4). Das
Schaltbild ordnet die Zeichen den Spalten `00…48` / Zeilen **in der Reihenfolge der gezeichneten
Streifen von oben nach unten** zu = Zeile 0…7 (Tastenkennungen `Dxx` obere Buchstabenreihe, `Exx`
Zifferreihe, `Cxx` Mittelreihe, `Bxx` untere Reihe, `Axx`/`B5x`/`D5x`/`C5x`/`E5x` Block rechts +
Cursor).

Die CP/A-Namen der Codes ≥ 80H stammen aus der Tabelle `kbdcpt` in `biopkbd.mac` („Kursorfeld“,
„Sondertasten“, „Funktionstasten“, „Ziffernfeld“): `87 |<-`, `8B ^`, `89 ->|`, `88 <-`, `8C '\`,
`86 ->`, `9D <-'`, `8A v`, `9E ET`, `8D` (→ rechts oben, wird in CP/A zu Tab 09H), `CE CE`, `82 INS`,
`D0 S`, `D1…D4 F1…F4`, `CF F5`, `A0…A3 F6…F9`, `83 F10`, `C1 F11`, `C0 F12`, `C2 F13`, `CD F14`,
`8E F15`, `B0…B9 Ziffernblock 0…9`, `BB 00`, `AC ,`, `BD -`. `1B` (ESC) und `7F` (DEL; MAME beschriftet (6,6) als Rücktaste) sendet die Tastatur selbst.

**Zweite Tastaturvariante TAST_618** (`pc1715_tast618_tastatur.bin`, CRC32 `6052A81E`): bis auf
**54 Byte in den Codetabellen** identisch zu S600 (gleiche Matrix, gleiche Logik, gleiche Rahmen,
gleiche Sondertasten); es ist die **QWERTZ-Fassung** mit anderer Zeichenbelegung. Abweichende
Positionen (unverschoben / Shift; Tabelle LOCK = unverschoben mit Großbuchstaben, Shift+LOCK =
Shift, wie bei S600):

| Position | S600 | TAST_618 |
|---|---|---|
| (1,2) | `3` / `#` | `3` / `@` |
| (3,0) | `@` / `` ` `` | `}` / `]` |
| (3,2) | `-` / `=` | `~` / `+` |
| (3,5) | `:` / `*` | `{` / `[` |
| (4,2) | `0` / `_` | `0` / `=` |
| (4,4) | `/` / `?` | `-` / `_` |
| (4,5) | `;` / `+` | `\|` / `\` |
| (5,0) | `[` / `{` | `?` / `^` |
| (5,2) | `^` / `~` | `*` / `` ` `` |
| (5,4) | `z` / `Z` | `y` / `Y` |
| (5,5) | `]` / `}` | `#` / `'` |
| (6,2) | `7` / `'` | `7` / `/` |
| (7,4) | `\` / `\|` | `<` / `>` |
| (9,4) | `,` / `<` | `,` / `;` |
| (10,4) | `.` / `>` | `.` / `:` |
| (12,0) | `y` / `Y` | `z` / `Z` |

Der Nachbau kann beide ROMs über dieselbe Klasse fahren; welche Variante in welches Gerät
gehört, ist **[?]** (Länderfassung; S600 ist die in MAME und in den Plänen genannte).

## 7. Sondertasten im Einzelnen [LAUF]

| Taste | Verhalten |
|---|---|
| CTRL (8,0) | solange gedrückt Statusbit 0; verändert **keinen** Code |
| Shift links/rechts (8,1)/(8,4) | solange gedrückt Statusbit 1, wählt Shift-Tabelle |
| **LOCK** (8,5) | **rastend**: erster Druck schaltet **sofort** ein (Statusbit 3, LED A14 = 1); der nächste Druck schaltet erst beim **Loslassen** aus. Sendet selbst keinen Rahmen |
| **SI/SO** (8,7) | **rastend**, schaltet mit dem **Druck** um (Statusbit 2, LED A13 = 1); sendet selbst keinen Rahmen. Der Zeichensatzwechsel selbst geschieht im Rechner (CP/A `crt1zs`/`crt2zs`) |
| REP (8,6) | selbst kein Zeichen; erlaubt Autorepeat (§3) |
| ESC (8,2) | normale Zeichentaste, Code `1B`, auch mit Shift |

Zustand von LOCK und SI/SO bleibt bis zum nächsten Druck erhalten und steht in **jedem** Statusbyte.

## 8. LED-Leitungen

Das ROM führt vor **jeder** Spalte einen `IN` mit **A15 = 0** aus (Leuchtenwert), A13 = SI/SO-
Leuchte, A14 = LOCK-Leuchte: **1 = Funktion aktiv** (D-Flipflop A6A/A6B, Anzeige an /Q über
130 Ω ⇒ D = 1 leuchtet). Die Abfrage-`IN`s (A15 = 1) tragen immer A13 = A14 = 0 und dürfen die
Leuchten **nicht** setzen — der Nachbau wertet daher die Leuchtenbits **nur bei `IN` mit A15 = 0**
aus [LAUF: sonst flackerten die Leuchten mit jeder Spalte; der Gatteraufbau A2.2B/A2.2C
(Flipflop-Takt) ist im Scan nicht lesbar — **[?]** nach dem ROM-Verhalten geschlossen]. Das
Rechner-seitige Bild ist die Statuszeile (CP/A `lampen`); an der Maschine selbst sind die
Leuchten für den Emulator nur Anzeige.

## 9. Anleitung: den S600 auf dem vorhandenen `Z80`-Kern laufen lassen

* **Speicher**: 2 KB ROM, gespiegelt über alle 64 K (`readByte(a) = rom[a & 07FFH]`);
  Schreibzugriffe wegwerfen (finden nicht statt). **Kein RAM, kein Stapel.**
* **`readPort(addr16)`**: `addr16 & 8000H` → Spaltenmaske `addr16 & 1FFFH` (ein Bit) → Zeilenbyte
  = ODER der gedrückten Positionen dieser Spalte, **gedrückt = 1**; sonst Leuchtenwert aus
  Bit 13/14 merken, Rückgabewert egal.
* **`writePort(addr16, v)`**: Bit D0 = `v & 1` ins Rahmenwerk (§4); jeder Aufruf ist ein Takt.
* Zykluszählung der Instruktionen übernimmt der Kern; die Tastatur-CPU läuft mit 700 kHz zur
  Maschinenzeit (Verhältnis 700 000 / 2 457 600).
* Reset: `PC = 0`, alle Register 0; das ROM löscht seine Register selbst.

Referenzharness (≈ 35 Zeilen; Ausgabe gegen die Tabelle in §10 prüfbar):

```cpp
Z80 cpu;  uint8_t rom[2048];  bool m[13][8] = {};     // m[spalte][zeile] = gedrückt
cpu.readByte  = [&](uint16_t a)             { return rom[a & 0x7ff]; };
cpu.writeByte = [&](uint16_t, uint8_t)      {};
cpu.readPort  = [&](uint16_t p) -> uint8_t {
    if (!(p & 0x8000)) return 0xff;                    // Leuchtenwert: A13=p>>13&1, A14=p>>14&1
    uint8_t r = 0;
    for (int c = 0; c < 13; c++) if (p & (1 << c))
        for (int k = 0; k < 8; k++) if (m[c][k]) r |= 1 << k;     // gedrückt = 1
    return r; };
cpu.writePort = [&](uint16_t, uint8_t v)    { bit_clock(v & 1, cpu.cycles); };  // ein Takt je OUT
cpu.reset();  while (running) cpu.step();
```

## 10. Referenzläufe (Prüfwerte, Takte der Tastatur-CPU; Druck bei t = 20 000)

| Eingabe | Rahmen (Statusbyte, Zeichen) | Start Rahmen 1 / 2 |
|---|---|---|
| `a` (7,5) | `E0 61` | 41 719 / 42 212 |
| `a` mit Shift links (8,1) gehalten | `E2 41` | — |
| `4` (0,2) mit CTRL (8,0) | `E1 34` | — |
| `4` (0,2) mit Shift | `E2 24` (`$`) | — |
| LOCK gedrückt+losgelassen, dann `a` | `E8 41` (LED A14 → 1) | — |
| SI/SO gedrückt+losgelassen, dann `a` | `E4 61` (LED A13 → 1); zweiter Druck: `E0 61` | — |
| Ruhe | keine `OUT` | — |
| `a` gehalten + REP gehalten | `E0 61` und dann `E0 61` alle 70 718 Takte | 1. Wiederholung ≈ 0,67 Mio Takte nach dem ersten Zeichen |

## 11. Abweichungen gegenüber dem Plan §3.7

* „Spalte A0–A12, `IN` bei A15 = 1, LEDs A13/A14 beim `IN`“ — richtig, **aber** die LED-Bits
  gelten nur bei `IN` mit A15 = 0 (§8). Bitrahmen wie vermutet 8N1 **ohne Parität**.
* „Start-, Daten- und Stoppbit einzeln“: stimmt; **zusätzlich** je Taste **zwei Rahmen** (Status
  `E0H…EFH` + Zeichen) und je Rahmen ein Leerlaufpaar davor und ein Stoppbitpaar danach.
* „Nur eine Richtung“ stimmt. **Neu**: **Autorepeat nur mit REP**, LOCK-/SI/SO-Rastung **im ROM**,
  keine Break-Meldung.
* „Matrix aus MAME übernehmbar“: ja, bis auf die genannten Zeilen/Spalten (3,5), (3,1) und die
  Eigenheit (4,3); die vier Wertepaare je Taste (normal/Shift/LOCK/Shift+LOCK) stammen aus dem ROM.
* „Entprellung (3 Abfragen)“ bestätigt (§3).
* Tastaturtakt: Plan nennt keinen; Handbuch 700 kHz ± 10 %.
* Zweite ROM-Fassung TAST_618: **QWERTZ-Tabellenvariante**, gleiche Logik (§6).
