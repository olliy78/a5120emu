# K5020-Treiber im Abgleich — Bandformat, Signalfolgen, Bootband (AP K1)

Stand 2026-10-14, Zweig `kassette_mc80`.  Grundlage für K3 (`peripherals/kassette`), K4 (`cards/k5020`),
K6 (`k1520tape`, Bootband) und K7/K8.  Hardware-Seite: `k5020.md` (K0), Laufwerk: `k5200.md` (K2).

**Ergebnis in einem Satz:** Die Karte legt am Band **nichts** fest außer der Bitkodierung — Präambel,
Postambel, CRC, Bandmarke, Lücken und Positionierung macht ausschließlich die Software; die fünf
untersuchten Treiber benutzen dafür aber **dasselbe Blockformat** (§2), sie widersprechen sich nirgends,
nur ihre Zeiten und ihre Art, das Blockende zu erkennen, unterscheiden sich (§5).

## 0. Quellen und Zitierweise

| Kürzel | Quelle | wie gelesen |
|---|---|---|
| **ROM** `addr` | `doc/EPROMS/zre.prn` (Lade-ROM V0.9, Kassettenteil 027E–03EE), Gegenstück `doc/kassette/fex/startkass.mac` | kommentiertes Listing |
| **KMBG** `addr` | `doc/kassette/fex/KMBG.REL` — Robotron-Bibliothek für SCPX, Microsoft-REL, Modul `kmbg`, Einsprünge `KMBG` = 022EH, `STATUS` = 097FH, Programmlänge 09D9H, keine externen Bezüge | eigener REL-Leser, gebunden bei 0000H, `tools/z80_disasm2.py`; Adressen = relativ zum Modulanfang.  Die ISR-Körper (0567, 0590, 0766, 079E) liegen hinter berechneten Vektoren und wurden von Hand dekodiert |
| **BCU** `addr` | `KMBG.REL` **auf der BCU-880-Diskette** `doc/a5120_alt/bcu/satz2_OhneSYL.1/disk.img` (CP/M 26×128, 2 Systemspuren, 1-KB-Blöcke, Versatz 6; Datei 4608 B) — **ein anderes Programm als die SCPX-Bibliothek gleichen Namens**: BCU-eigenes Seitenrelokationsformat (`01`, Länge 0E80H, Code ab Dateiversatz 100H, Bitkarte dahinter), Lader meldet „kmbg opened“ | Code ab Dateiversatz 100H bei 0000H disassembliert; Adressen = relativ zum Codeanfang |
| **BCU-INIT** | `INIT.COM` derselben Diskette (256 B) | disassembliert |
| **EGOS** `addr` | MC80.30-Magnetbandsoftware `doc/EPROMS/MC80/mc8030_mame/zve_3.rom`+`zve_4.rom` = 1000H–1FFFH | Stichproben (Blockaufbau, CRC, Lese-ISR); vollständige Auswertung ist Sache von M1/M7 |
| **K5261** `addr` | Firmware `doc/EPROMS/K5261/kmb2521{I,M,A}.bin`, gelegt als **I = 0000H, M = 0400H, A = 0800H** (I beginnt mit `DI; LD SP,0FF0H; IM 2`, A ruft sich selbst bei 09xxH) — K5020 dort auf **F8H–FFH** | Stichproben wie EGOS |
| **FEX** `Datei:Zeile` | `doc/kassette/fex/*` (Programme des Anwenders, nicht Robotron) | Text |
| **IMG** | `doc/kassette/fex/dirk2014/images/tape000[0-3].bin` — mit SCPX-`KMBG.REL` gelesene Blöcke im SIMH-Format (`ki5120.asm`: Länge 4 B LE, Daten, Statusbyte, Länge 4 B) | Python |
| [abg] | abgeleitet/gerechnet | |
| [?] | unsicher / nicht belegt | |

Zeiten aus Zählschleifen sind für **2,4576 MHz** gerechnet (ZRE K2526 am A5120, Entwurf 29 E2); die
Schleife `DEC HL; LD A,H; OR L; JR NZ` kostet 26 Takte (= 10,58 µs).

Die AP-Karte nennt „vier Treiber“: Lade-ROM, BCU, KMBG.REL, EGOS.  Die BCU-Diskette trägt aber einen
**eigenen** `KMBG.REL` (s. o.), dazu kommt die K5261-Firmware — abgeglichen sind deshalb **fünf**.

---

## 1. Was die Karte festlegt und was nicht

| Gegenstand | festgelegt durch | Beleg |
|---|---|---|
| Bitkodierung (Richtungstaktschrift/PE, 12 000 bzw. 6 000 Bit/s), Bitsynchronisation, Byte-Rahmung | **Karte** | `k5020.md` §6 |
| Präambel `AAH`, Postambel `AAH` | **Software** schreibt sie als gewöhnliche Datenbytes über Daten-PIO A | KMBG 03DF/043C (`LD (HL),0AAH`, Puffer 0118 = `AAH`), BCU 03DE/0405, EGOS 18C2/18ED, K5261 0898 |
| CRC | **Software** (§2.2) | KMBG 05AB, BCU 0C56, EGOS 1DD6, K5261 0982; BD K5020 S.2 („durch ZRE realisiert“) |
| Bandmarke | **Software**: ein Block ohne Daten (§2.3) | KMBG 03DF, BCU 05FF, EGOS 19CF |
| Lücken (Vorlücke, Blocklücke, Initiallücke, Endlücke) | **Software** über Laufzeit von `TRV`/`AUF` (§4) | alle Treiber |
| Lage der BOT-/EOT-Marke, Vorspann | **Kassette/Laufwerk**, Karte reicht `AEB` nur durch | §3.1 |

Die Präambelkontrolle der Karte (K0 §6) entscheidet nur, ob der Bitstrom synchron eingelesen wird; der
Präambel-**Inhalt** wird nicht geprüft — er kommt als erstes Datenbyte bei der CPU an (§2.1).

## 2. Blockaufbau auf dem Band

### 2.1 Bytefolge

```
… Lücke | AAH | Daten 1…256 Byte | CRC low | CRC high | AAH | Lücke …
          Präambel                                       Postambel
```

- **Alle fünf Treiber schreiben genau so** (KMBG 03F8–043C: Puffer 0118 = `AA`, Daten ab 0119, dann D (low), E
  (high), `AA`, Zahl der Ausgaben = Länge + 4; BCU 03DB–0405; EGOS 18A1–18C2; K5261 0964–0979 + 0898).
- **Die Karte gibt Präambel und Postambel als Bytes an die CPU weiter**, beim Lesen wie beim Echo
  (Lese-nach-Schreiben):
  - KMBG zählt beim Lesen `n + 4` empfangene Bytes (Puffer 0014 = `AA`, Daten ab 0015; Längenprüfung
    `(000D) − 4` gegen die erwartete Länge, KMBG 0706–0714) und vergleicht nach dem Schreiben das Echo
    über **Länge + 4** Bytes mit dem Ausgabepuffer einschließlich beider `AA` (KMBG 0533–0556).
  - Belegt am echten Gerät: IMG `tape0000.bin`, Satz 2 — 256 Datenbytes, gelesen mit Längenwunsch 104H,
    zurückgemeldet **260** = 256 + 4, Status 02H (Längenfehler, weil 260 ≠ 256); hinter den Daten stehen
    `5E FE` (CRC) und `AA` (Postambel).  Das zweite `AA` dahinter ist **kein** Bandinhalt, sondern das
    Konstantenbyte 0118 der Bibliothek, das `LDIR` mit 104H Bytes ab 0015 noch mitkopiert [abg].
  - Das Lade-ROM verwirft das erste Byte (ROM 03BC) und nimmt danach **131** = 128 + 2 + 1 Bytes (ROM 0359,
    `BC = 8331H`) — passt genau zu Präambel + 128 Daten + CRC + Postambel.
  - BCU, EGOS, K5261 erkennen das Blockende an einem `AA`, nach dem `BMS` abfällt (§5.2).
- **Blocklänge** (Daten): KMBG 1…256 (prüft nur ≤ 256, KMBG 03F8–040A; Länge 0 wird nicht abgewiesen, liefe
  aber in ein `LDIR` mit 64 K [abg] — praktisch nicht benutzbar); **BCU 12…256** (KMBG-Fehler „BLOCKLENGHT?“,
  BCU 03C0–03D8: `C < 0CH` bei `B = 0` → Fehler); BD K5020: 2…256, BD K5200: 4…256 (`k5200.md` §1).
  **Für das Modell: 1…256 zulassen**, keine Hardwaregrenze [abg].

### 2.2 CRC

**CRC-16/ARC**: Polynom x¹⁶ + x¹⁵ + x² + 1 (0x8005), **reflektiert** gerechnet (0xA001), **Startwert 0000H**,
kein Ausgangs-XOR; gerechnet **nur über die Datenbytes** (nicht über Prä-/Postambel); auf dem Band
**niedriges Byte zuerst**.  Prüfwert für `"123456789"` = **BB3DH**.

| Treiber | Routine | Form | Byte-Reihenfolge |
|---|---|---|---|
| KMBG | 05AB (tabellenlos, byteweise, `DE = 0`) | D = low, E = high | schreibt D, E (0432–043C); vergleicht D, E (0727–072E) |
| BCU | 0C56 (bitweise, `RRC C` = LSB zuerst, XOR `D^01H`, `E^20H`) | D = low, E = high | schreibt D, E (0401); vergleicht (0A0B–0A10) |
| EGOS | 1DD6 (wie BCU, `RLA` holt das Rückkopplungsbit) | D = low, E = high | schreibt D, E (18BE) |
| K5261 | 0982 (wie BCU, Register vertauscht: `D^20H`, `E^01H`) | E = low, D = high | schreibt E, D (0976) |
| ROM | — | liest die CRC mit ein (2 der 131 Bytes) und **prüft sie nicht** | |

Gegenprobe: KMBG 05AB und BCU 0C56 in Python nachgerechnet, gleich CRC-16/ARC für Zufallsdaten und den
Prüfwert; der echte Block IMG `tape0000.bin` (256 B) hat CRC **FE5EH**, auf dem Band `5E FE` — passt.
ISO 3407 selbst liegt nicht vor; dass die Norm dasselbe Polynom vorschreibt, ist **[?]**.

### 2.3 Bandmarke

Eine **Bandmarke ist ein Block ohne Daten**: `AA 00 00 AA` (CRC über null Bytes = 0000H).

- KMBG 03DF: Puffer `AA 00 00 AA`, Ausgabelänge 4; BCU 05FF–0636 ebenso; EGOS 19CF–19DB ebenso.
- Erkennen: KMBG nach dem Lesen, wenn **genau 4** Bytes kamen und `(0014) = AAH`, `(0015) = 00H`
  (KMBG 0751–0762) → Status Bit 5; beim Bandmarkensuchen zusätzlich `(0016) = 00H` (KMBG 08B2–08DB).
  BCU: Datenlänge `n − 3 = 0` (BCU 09F2–0A1F).  K5261: nur 2 Bytes hinter der Präambel (K5261 08F7–0906).
- Belegt am Gerät: IMG `tape0000.bin` Satz 3 und `tape0001.bin` — Länge 4, Status 22H (Bandmarke + Längenfehler).

### 2.4 Rückwärtslesen

KMBG liest beim Rückwärts-Bandmarkensuchen (Befehl 33H, Bit 5 = rückwärts) die Bandmarke als
**`AA FF FF AA`** (KMBG 08C1–08F8: vergleicht vorwärts mit `00`, rückwärts mit `FF`).  Rückwärts gelesene Bytes
kommen also **invertiert** an; da die Zeitfolge umgekehrt ist, kommen sie außerdem in umgekehrter Reihenfolge
und mit umgekehrter Bitfolge [abg] (für `AA` und `00`/`FF` nicht unterscheidbar — `AA` ist unter
„umkehren + invertieren“ unverändert, deshalb geht die Präambel-/Postambelerkennung in beide Richtungen).
**Für K4:** Rückwärtslesen liefert je Block die Bytes `~bitrev(b)` in umgekehrter Folge [abg].  Benutzt wird
das nur zum Positionieren (KMBG 07C7 ff., 32H/33H); die Daten selbst liest kein Treiber rückwärts.

### 2.5 Bitreihenfolge auf dem Band

Ob die Karte ein Byte LSB oder MSB zuerst seriell ausgibt, steht in keiner der Unterlagen **[?]**; für ein
byteweises Modell (`.k52`) ohne Belang, erst für `.wav` (K11) zu klären.

## 3. Signale der Steuer-PIO aus Sicht der Software

### 3.1 `AEB` (Steuer-PIO A Bit 7) — **Berichtigung zu K0 §3.1**

**Bit 7 = 1: Magnetband unter der Lichtschranke; Bit 7 = 0: `AEB` aktiv** (Klarsicht-Vorspann oder
BOT-/EOT-Marke).  Die Karte reicht den am Laufwerk low-aktiven Pegel (`k5200.md` §2.2) unverändert durch.
K0 schrieb „1 = Hellzone“ (nach einer Handnotiz); das widerlegen drei Treiber unabhängig voneinander:

- **ROM 02F9–0349**: steht bei Bit 7 = 1 das Band irgendwo → Umspulen (`UMS`) bis zum Vorspann; bei Bit 7 = 0
  (Vorspann) kurz rückwärts (≤ 3 × 0,43 s): erscheint dabei Bit 7 = 1, war es das **End**-Vorspannband →
  Umspulen; sonst ist es der Anfang → vorwärts, **warten bis Bit 7 = 1** (Vorspann verlassen, ROM 0349–034C),
  dann lesen.  Mit der K0-Lesart liefe das ROM am Bandanfang endlos zwischen 02F9 und 0317.
- **KMBG**: Schreiben/Lesen vom Vorspann aus: `TRV`, warten bis Bit 7 = 1, dann bis Bit 7 = 0 (**BOT-Marke**),
  dann 140 ms Initiallücke (KMBG 0488–04B0, 0685–06BF); `AEB`-Interrupt mit Steuerwort `17H` =
  „aktiv bei 0“ (KMBG 04BB–04C5) setzt Status Bit 6 „EOT“ (ISR 0590).
- **BCU**: dieselbe Folge 1 → 0 an der BOT-Marke (BCU 0AFE–0B1B), `AEB`-Interrupt ebenfalls mit `17H`
  (Initialisierung BCU 0CB9: `FC CF 80 17 7F`).

Das Band hat damit längs: Vorspann (0) · Magnetband (1) · **BOT-Marke (0, kurz)** · Magnetband (1) · …
· EOT-Marke (0) · … · Vorspann (0).  Die Länge der Marke ist nirgends genannt **[?]**; KMBG entprellt
mit 2 ms (KMBG 0380–039C), BCU mit ≈ 1,4 ms (`DJNZ` 255, BCU 0B0A).

### 3.2 Übrige Leitungen — Gebrauch in den Treibern

| Leitung | Gebrauch | Beleg |
|---|---|---|
| `/RES1`, `/RES2` (A 1/0) | 0 = reservieren; KMBG reserviert mit `RES 1/0`, gibt mit `SET` frei und wartet danach 1,39 s; BCU wartet 2 × 150 ms und prüft dann `/NIB` | KMBG 0304–032E, BCU 0510–0537 |
| `/AWA1`, `/AWA2` (A 3/2) | anwählen; **beide 0 = RSI** (Rücksetzen) vor jedem Lese-/Schreibauftrag: KMBG `AND F3H` (0443, 064B), BCU `AND 73H` (0AD4), ROM `AND F3H` (033B) | |
| `HGE` (A 6) | (a) **kurzer HGE-Puls bei stehendem Band nach der Anwahl** vor jedem Lesen/Schreiben: KMBG `OR 48H`→`RES 6` (0457–0472, 065C–0675), ROM (033E), BCU (0C0B); (b) mit `TRV`/`TRR` Suchlauf; (c) BCU: HGE **nach** `AUF` = Schreiben ohne Echoprüfung (BCU 043E–0449, vgl. K0 §3.1) | |
| `TRV`/`TRR` (A 4/5) | Transport; **beide = UMS** nur im ROM (0317–0320) und in BCU „Rückspulen“ (05BE–05C5) | |
| `AUF` (B 1) | Schreiben; KMBG/BCU schalten `AUF` **vor** `TRV` ein und erst nach Bandstillstand aus (§4) | |
| `AZV` (B 0) | nur **BCU** wertet aus: 19 cm/s → alle Wartezeiten verdoppelt (BCU 0A9D und Aufrufer); KMBG und ROM nicht | |
| `KSB` (B 2) | BCU merkt sich die Seite (BCU 055A); FEX zeigt sie an | |
| `/NIB1`, `/NIB2` (B 4/3) | 1 = bereit; ROM wartet nach `UMS` auf 1 (0326–0329); KMBG/BCU prüfen vor jedem Auftrag | |
| `BMS` (B 5) | Blockende-Erkennung (BCU/EGOS/K5261, §5.2), Blockzählen im Suchlauf (BCU 063B–06A8), Lückensuche beim Block-Rücksetzen (BCU 0C0B–0C54) | |
| `STA` (B 6) | KMBG: vor `AUF` muss `STA = 0` sein, sonst „nicht bereit“ (047D, 05D8); **BCU erkennt den Schreibschutz so: `AUF` setzen (Band steht), `STA` lesen → 1 = „READ ONLY!“** (BCU 054C–0568); BCU-Interrupt auf `STA` beim Schreiben (Maske `BFH`, 0CBE) | |
| `ZF` (B 7) | **von keinem Treiber benutzt** (KMBG-Maske `DFH` überwacht nur Bit 5 und gibt den Interrupt nie frei; BCU-Maske `BFH`) | |

Folgerung für K4: `STA` muss bei stehendem Band **sofort** `AUF ∧ AVE` zeigen (BCU fragt ohne Bandlauf ab).

### 3.3 Interrupts in den Treibern

| Treiber | Daten-PIO A (Ausgabe) | Daten-PIO B (Eingabe) | Steuer-PIO A (`AEB`) | Steuer-PIO B | Zeitgeber |
|---|---|---|---|---|---|
| ROM | — | Vektor F0H (erstes Byte, 03BC) → F4H (`INI`, 03C7); `97H`,`97H` an 33H | aus | aus | ZRE-CTC Kanal 1 (0DH), `A7H`/`FFH` = 26,6 ms je Tick, Gesamtfrist 20 × 256 Ticks ≈ **136 s** (ROM 02E8–02F7, ISR 03D4) |
| KMBG | ISR 0567 (`OUT (30H)` je Byte) | ISR 0766 (Byte ablegen, CTC neu starten) | ISR 0590 (`17H`/`7FH`/`83H`: bei `AEB = 0` → Status Bit 6) | nie freigegeben | ZRE-CTC Kanal 1 (0DH), Vektor `F7FAH` → ISR 079E: nach jedem Byte `A7H`,`B0H` = **18,3 ms ohne Byte → Blockende** |
| BCU | ISR 0898 (sendet, nach letztem Byte Abschlussfolge) | ISR 095D (Lesen) bzw. 0915 (Echovergleich je Byte) | ISR 0A39 | ISR 0A28 (`STA`) | ZRE-CTC Kanal 1, `A5H`,`60H` = **10 ms**-Ticks (0A4A/0A5D) als Ablaufsteuerung |

Alle Treiber programmieren die Daten-PIO wie in K0 §5.4; KMBG und BCU tragen ihre Vektoren in die
SCPX-/BCU-Interrupttabelle bei `F7xxH` ein (KMBG 025A–0286 über `(F7EBH)`, BCU 0B90–0BB8).  KMBG
benutzt **feste Portadressen 30H–37H und 0DH** — es läuft nur am A5120 (FEX für den K8915 kam deshalb
ohne KMBG aus, `doc/kassette/README.md`).

## 4. Funktionen als Signalfolgen

Schreibweise: `A ← x` = Steuer-PIO Tor A, `B ← x` = Tor B (Bitnamen nach K0).  LW 1 als Beispiel
(LW 2: `AWA2` statt `AWA1` usw.).  „warte t“ = Zählschleife bei 2,4576 MHz [abg].

### 4.1 Reservieren / Freigeben (Entriegeln)
- KMBG 15H/16H: `RES1 ← 0` bzw. `1`, dann warte 1,39 s (0318–032E, `sub_03D4` mit B = 2).
- BCU 31H: `RES1 ← 0`, warte 2 × 150 ms, `/NIB1` prüfen (sonst „RESERVE?“), anwählen, `AUF ← 1`, `AZV`/`KSB`/
  `STA` lesen (Geschwindigkeit, Seite, Schreibschutz), `AUF ← 0`, abwählen (0510–0584).  BCU 41H: `RES ← 1` (0587).

### 4.2 Schreiben eines Blocks (KMBG 02H, BCU 00H, EGOS)
1. Puffer bauen `AA | Daten | CRC lo | CRC hi | AA` (§2.1).
2. RSI (`AWA1 = AWA2 = 0`), anwählen + HGE-Puls (§3.2).
3. **Steht das Band im Vorspann (`AEB = 0`)**: KMBG: `AUF ← 1`, `TRV ← 1`, warten bis `AEB = 1`, dann bis `AEB = 0`
   (BOT-Marke), dann **140 ms** (0479–04B0).  BCU: `AUF ← 1`, `TRV ← 1`, über die BOT-Marke, **400 ms** löschen
   (800 ms bei 19 cm/s), `TRV ← 0`, 40 ms, `AUF ← 0` — dann normal weiter (0AEB–0B4F).
   **Sonst**: KMBG `AUF ← 1`, `TRV ← 1`, warte **31,7 ms** (05D8–05EC); BCU `AUF ← 1`, `TRV ← 1`, warte **40 ms**
   (70 ms bei 19 cm/s) (0431–0458).
4. Echo einschalten (Daten-PIO B, Blindeingabe), Daten-PIO A frei, erstes Byte `AA` direkt ausgeben, den Rest
   interruptgesteuert (KMBG 04D9–04F2, BCU 0468–0482).
5. Nach dem letzten Byte: KMBG warte **28,9 ms**, `TRV ← 0`, warte **52,9 ms**, `AUF ← 0` (050D–0531); BCU warte
   30 ms, `TRV ← 0`, warte 70 ms, `AUF ← 0` (08B7–08EB).
6. **Lese-nach-Schreiben**: KMBG vergleicht das ganze Echo (Länge + 4) mit dem Puffer (0533–0556), BCU je Byte im
   Echo-ISR (0915).  Fehler → **einen Block zurück und neu schreiben**, KMBG bis 8-mal (`(000F) = 8`, 043E, 0558),
   BCU bis 7-mal („7xWRITE“), BCU nach weiteren Fehlschlägen zusätzlich 160 ms Band löschen und dahinter neu
   (0763–07DD).  Endgültig: KMBG Status Bit 4.
7. Beim Schreiben verlangt KMBG `STA = 0` vor `AUF` (047D/05D8).

Daraus die **Blocklücke beim KMBG-Schreiben** [abg]: 28,9 ms × 381 mm/s ≈ 11 mm Nachlauf + 4 mm Stoppweg +
≈ 8 mm Anlauf/31,7 ms ≈ **23 mm** (Nennwert K5200 20,3 mm, `k5200.md` §3).

### 4.3 Lesen eines Blocks (KMBG 01H, BCU 02H)
1. KMBG: Empfangspuffer 0014–0117 löschen, RSI, anwählen + HGE-Puls (0625–0675).
2. Im Vorspann: `TRV ← 1`, warten bis `AEB = 1`, bis `AEB = 0` (BOT), sofort weiter (KMBG 067F–06BF, Frist 2,45 s);
   BCU über die BOT-Marke und **10 ms** dahinter stoppen (0AEB, Befehl 02H → B = 1 Tick).
3. `AEB`-Interrupt frei (Bandende), Daten-PIO B frei + Blindeingabe, `TRV ← 1` (KMBG 06C1–06DD).
4. Blockende: KMBG **18,3 ms ohne Byte** (CTC); BCU/EGOS/K5261 `AA` + `BMS = 0` (§5.2).  BCU gibt nach
   **680 ms** ohne Block auf (1,34 s bei 19 cm/s, 04C4–04D2).
5. `TRV ← 0`, warte 52,9 ms (KMBG 06FA–0703).
6. Länge prüfen (Status Bit 1), Bandmarke prüfen (Bit 5), CRC prüfen; CRC-Fehler → **einen Block zurück** (rückwärts
   lesen bis Blockende) und neu lesen, KMBG bis 5-mal (`(0010) = 5`), BCU bis 7-mal („7xREAD“); danach KMBG
   Status Bit 4, Daten trotzdem übergeben (0743–074F).

### 4.4 Block vor/zurück, Bandmarke vor/zurück (KMBG 12H/32H, 13H/33H; BCU 10H/14H, 70H)
- KMBG liest dabei **mit Normalgeschwindigkeit** Block für Block (vorwärts oder rückwärts) und hält nach einem Block
  bzw. nach der Bandmarke (KMBG 0837–0911, §2.4).
- BCU „Block zurück/vor“ (0C0B): HGE-Puls, `TRR`/`TRV` normal, warten bis `BMS = 1`, dann bis `BMS` ≥ **27 ms** am
  Stück 0 ist (54 ms bei 19 cm/s; Störimpulse ≤ 0,7 ms werden überbrückt, 0BF0), stoppen.
- BCU „Bandmarke suchen“ (70H, 063B–0752): **Suchlauf** (`HGE` + `TRV`/`TRR`), Blöcke an `BMS` zählen (Block = in einem
  Fenster von 73 Abtastungen mindestens 15-mal `BMS = 1`), nach der vorgegebenen Zahl stoppen, mit Normalgeschwindigkeit
  bis in die nächste Lücke, dann den Block lesen.  Damit muss `BMS` **auch bei HGE** je vorbeilaufendem Block anschlagen.

### 4.5 Schnell vor-/zurückspulen (KMBG 14H/34H)
`HGE` + `TRV` bzw. `TRR` bis `AEB = 0` (dreimal im Abstand von 2 ms bestätigt), stoppen, warte 1,39 s (KMBG 0331–03A2).
Steht das Band beim Start schon auf `AEB = 0`, fährt KMBG erst **3,04 s** blind, prüft dann (0351–037E).
Das ist **kein** `UMS`: es hält an der nächsten Marke (BOT/EOT) oder im Vorspann.

### 4.6 Rückspulen bis Bandanfang
- ROM: `UMS` (`TRV` + `TRR`), sofort abwählen (RSI), warten bis `/NIB = 1` (ROM 0317–0329).  **Für K3/K4: `/NIB` muss
  unmittelbar nach dem `UMS`-Befehl auf 0 gehen** — das ROM fragt ohne Pause ab.
- BCU 20H/21H (05A6–05D8): `TRR` bis `AEB = 0` (Frist ≈ 0,7 s), dann `UMS`, warten bis bereit.

### 4.7 Bandmarke schreiben (KMBG 11H, BCU 50H) — wie 4.2 mit `AA 00 00 AA`.

### 4.8 Band löschen
- KMBG 18H: `AUF` + `TRV` ab Position (bzw. ab BOT wie 4.2.3) **bis `AEB = 0`** (zweimal im Abstand 0,34 s bestätigt =
  EOT-Marke), dann stoppen, 52,9 ms, `AUF ← 0` (05FA–0622).
- BCU 60H: `AUF` + `TRV` für **1,27 s** (2,54 s bei 19 cm/s), dann stoppen (05EF, 0B50).

### 4.9 Statusbyte der SCPX-Bibliothek (Rückgabe in A, für K8)
Bit 0 Befehl/Parameter falsch · Bit 1 Blocklänge ≠ Wunsch · Bit 2 Gerät nicht bereit (auch Zeitüberschreitung) ·
Bit 4 CRC-/Vergleichsfehler nach allen Wiederholungen · Bit 5 Bandmarke · Bit 6 `AEB` (EOT) während des Auftrags ·
Bit 3/7 unbenutzt [?].  HL = Zahl der empfangenen Bytes **einschließlich Prä-/Postambel und CRC** (KMBG 073D).
Befehlscodes (FEX `kassrout.mac:121–145`, KMBG 02C8–02F9): Bit 6 = LW 2, Bit 5 = rückwärts; 01 lesen, 02 schreiben,
11 Bandmarke schreiben, 12 Block vor, 13 Bandmarke vor, 14 schnell vor, 15 reservieren, 16 freigeben, 18 löschen.
`(0044)` wird auf 64H gesetzt und als Frist abgefragt, aber nirgends heruntergezählt — totes Überbleibsel [abg].

## 5. Unterschiede zwischen den Treibern (keine Widersprüche)

### 5.1 Zusammenfassung

| | ROM | KMBG (SCPX) | BCU | EGOS | K5261 |
|---|---|---|---|---|---|
| Blockformat | liest `AA`+128+CRC+`AA` | §2 | §2 | §2 | §2 |
| CRC | nicht geprüft | ARC | ARC | ARC | ARC |
| Präambel bei Lesen | verworfen, beliebig | muss für Bandmarke `AA` sein | beliebig (gespeichert) | **`AA` oder `AB`** (1BF7–1C03) | **`AA` oder `AB`** (`AND FEH`, 0825–082B) |
| Blockende | Zählen (131 Bytes) | 18,3 ms ohne Byte (CTC) | `AA` + `BMS = 0` nach ≈ 0,38 ms | `AA` + `BMS = 0` nach ≈ 1,35 ms (`DJNZ` 255) | `AA` + `BMS` nach `DJNZ 16` |
| Bandmarke | — | `AA 00 00 AA` | dto. | dto. | dto. |
| AZV (19 cm/s) | nein | nein | ja, Zeiten ×2 | (über Profil) [?] | [?] |
| Lese-nach-Schreiben | — | Gesamtvergleich | je Byte | je Byte [?] | je Byte (0835) |
| Portbasis | 30H fest | 30H fest | 30H fest | (IX+2), AKB/AKM | F8H |

Dass EGOS und K5261 `ABH` als Präambel durchgehen lassen, deutet auf ein am Gerät beobachtetes Kippen des letzten
Präambelbits hin [?]; das Modell liefert immer `AA`.

### 5.2 Folgerungen für das Kartenmodell (K4)
- `BMS` muss nach dem letzten Flusswechsel des Blocks **binnen ≈ 0,38 ms** (BCU, schärfste Grenze) auf 0 gehen und
  **innerhalb** eines Blocks durchgehend 1 bleiben (sonst hält BCU jedes Daten-`AA` für die Postambel).  K0: Abfall
  nach 108 µs ohne Flusswechsel — passt.
- Die Daten-PIO B muss **Präambel und Postambel** liefern und während `AUF` das **Echo** (≈ 10 ms = 3,81 mm hinter dem
  Schreibkopf, `k5200.md` §2.1), außer wenn `HGE` nach `AUF` gesetzt wurde.
- Ohne Karte liest das ROM an 34H `FFH` → `AND 7CH ≠ 0` → Halt 03B0 (ROM 02B7–02BB); mit Karte und ohne bereites
  Laufwerk (`/NIB = 0`) → je LW 5 Versuche, dann Halt.

## 6. Bandaufteilung je Software (Blockfolgen)

### 6.1 Lade-ROM: **Bootband** (für K6 `bootband` und K7)

| Größe | Wert | Beleg |
|---|---|---|
| Laufwerk | zuerst LW 1 (`A ← 09H`), nach 5 Fehlversuchen LW 2 (`06H`), dann Halt | ROM 02BE, 039C–03AD |
| Position | Band wird erst zum Anfang gespult (§3.1), gelesen ab dem Vorspann vorwärts | ROM 02F9–034C |
| Scharfschalten | ≈ **0,43 s** nach Verlassen des Vorspanns (≈ 163 mm) — der erste Block muss dahinter beginnen | ROM 0349–0350 [abg] |
| Blöcke | **4** aufeinanderfolgende Blöcke, je **128 Datenbytes** (AA + 128 + CRC + AA), Kennung keine; Lücken beliebig | ROM 0353–0376 |
| Ladeadresse | **0400H**, lückenlos (je Block 131 Bytes ab Zeiger, Zeiger danach −3) → 0400H–05FFH | ROM 02D4, 036C–0372 |
| Prüfung | `(0400H..0402H) = 'S','Y','L'` | ROM 01B6–01C5 |
| Einsprung | `CALL 0434H` (Platte: 0437H) — Versatz **34H** im Bootbereich | ROM 038D |
| nach Rückkehr | `JP 0177H` = Neustartpfad des ROMs (ROM-Ausblendung, Speicher löschen, `JP 0074H`) — ein Bandprogramm kehrt sinnvollerweise **nicht** zurück | ROM 0390, 0177–0191 |
| Fristen | Gesamtablauf ≤ ≈ 136 s (CTC), danach Fehlerpfad | ROM 02E8–02F7, 03D4–03ED |
| Länge eines Blocks | genau 128 Daten: ein längerer Block liefe in den nächsten Zyklus, ein kürzerer zöge den Folgeblock hinein [abg] | |

Ein mit der SCPX-Bibliothek (Länge 128) oder BCU geschriebenes Band erfüllt das.  Eine Floppy-SYL-Spur 0 passt
**nicht** als Bootband-Inhalt: dort liegt bei Versatz 34H ein `NOP` vor der K5122-Initialisierung
(`tools/bootsec/src/bootsec_syl.mac`, 0031–0037).

### 6.2 SCPX + `KMBG.REL` — kein Dateiformat
Die Bibliothek kennt nur Blöcke und Bandmarken; Dateiaufteilung macht das Anwenderprogramm (FEX, eigene Programme).
`ki5120.asm` des Mitstreiters liest Block für Block bis zur Bandmarke und schreibt je Bandmarke eine Datei
(IMG).

### 6.3 BCU 880, Laufwerk F: (BDOS-Aufsatz)
Der BCU-Treiber fängt BDOS-Aufrufe 0DH–28H ab (Sprungtabelle BCU 02F0) und bildet Dateien so ab [abg aus
BCU 0113–02EF]:
```
BOT … [Kopf 16 B = FCB 0..15] [Daten 128 B]… [17 B = FCB 0..16 ?] [Bandmarke] [1,27 s gelöscht]
       [Kopf nächste Datei] …
```
- `MAKE` sucht bis zum Ende (Lesefrist ohne Block), setzt einen Block zurück, liest die letzte Bandmarke und schreibt den
  16-Byte-Kopf (BCU 01B4, 027E–02DC, 02E4).  Gefunden wird eine Datei an einem **16-Byte-Block** mit passendem Namen
  (`?` erlaubt); andere Dateien werden per Bandmarkensuche (70H) übersprungen.
- Satzlänge 128 B, über FCB-Byte 13 einstellbar (BCU 016E–0183).
- `CLOSE` nach dem Schreiben: Block mit **17 Bytes** aus dem FCB, Bandmarke, 1,27 s löschen, zurückspulen, freigeben
  (BCU 0231–025E).  Warum 17 statt 16 Bytes und wie beim Lesen ein solcher Block von Daten unterschieden wird, ist **[?]**
  (`READ SEQ` kopiert jede Länge ≠ 0 in den DMA-Puffer, BCU 01D2–01F0).
- `DELETE`: einen Block zurück, Kopf mit Byte 0 = `E5H` neu schreiben (BCU 0135–0144).
- `INIT.COM` („Formatieren“) = BDOS 50H → „KASSETTE IN DRIVE F! (Y)“, dann Bandmarke + 1,27 s löschen + Rückspulen
  (BCU 0263–027D, `INIT.COM` 0100–011D).

### 6.4 EGOS (MC80) und K5261
Dateiformat nicht ausgewertet (M7).  EGOS führt Kommandos `DIRECTORY`, `KINIT`, `WRITE`, `KLOG` (Zeichenketten in
`zve_3`, Tabelle ab 1000H) — ein Verzeichnis auf dem Band ist zu erwarten [?].

### 6.5 FEX-Formate (Basis-/Kompakt-/Ausbaustufe, FLX K1600, frei)
**Nicht belegbar.**  `Formate.txt` nennt nur die Namen und den Versuchsaufbau (Blocklänge 20, Füllzeichen `EDH`); die
Beschreibungen sind leer, und das Robotron-Programm FEX selbst liegt weder im Repo noch in
`~/projects/robotron/sonst/{Fex,FexMitKMBG,FexNeu,FEXohneKMBG}` (gesucht nach „Basisstufe“, „FLX“, „K1600“ in allen
Text-, `.COM`- und Diskettenabbilddateien).  K6 kann daher vorerst nur **Rohblöcke** und das **Bootband** anbieten.

## 7. Messdaten (IMG)

| Datei | Inhalt | Deutung |
|---|---|---|
| `tape0000.bin` | 4 B Status 02 · 260 B Status 02 · 4 B Status 22 | ein unbekannter 4-Byte-Block (nicht `AA 00…`, keine Bandmarke) [?] · 256-B-Testtext (CRC passt, §2.2) · Bandmarke |
| `tape0001.bin` | 4 B Status 22 | Bandmarke |
| `tape0002.bin` | 74 B und 68 B Status 12 · Bandmarke | CRC-Fehler nach 5 Versuchen; Inhalt auch bitverschoben/invertiert nicht als Text lesbar [?] |
| `tape0003.bin` | leer | |

## 8. Für die Folge-APs

- **K3** (Medium): Block = Datenbytes (1…256), Präambel/Postambel/CRC kann das Medium selbst erzeugen (Format §2),
  muss aber **auch fehlerhafte** CRCs und fremde Präambeln speichern können (Treiber wiederholen bei Fehlern; EGOS/K5261
  akzeptieren `AB`); Bandmarke = Block mit 0 Daten — eigener Typ oder Länge 0, beides gleichwertig [abg].  Lücken als
  Länge; Band hat Vorspann · BOT-Marke · … · EOT-Marke · Vorspann (§3.1); BOT-Marke so legen, dass der erste Block
  > 163 mm hinter dem Vorspann liegt (§6.1); Umspulen einer Seite < 136 s (ROM-Frist).
- **K4** (Karte): `AEB`-Polarität §3.1; Präambel/Postambel als Bytes; Echo bei `AUF`; `BMS` §5.2, auch bei HGE je Block;
  `STA = AUF ∧ AVE` sofort; Rückwärtslesen §2.4; `/NIB` fällt sofort bei `UMS`.  Testfall „Init-Folge des Lade-ROMs“:
  ROM 0298–02B5 + 0355–0362.
- **K6** (`bootband`): §6.1 — 4 × 128 B, `SYL` am Anfang, Einsprung bei Versatz 34H, CRC nach §2.2 eintragen
  (das ROM prüft sie nicht, die übrigen Leser schon).
- **K7**: Testprogramm bei 0434H darf nicht zurückkehren.
- **K8** (SCPX): Statusbyte §4.9; KMBG braucht SCPX ≥ 1.1 (Prüfung `DE36H/DE37H ≥ '1'`, KMBG 024B–0257) und einen
  freien Platz in der Interrupttabelle (`(F7EBH) ≥ 47H`, KMBG 025A–0264).
- **A8** (BCU F:): §6.3.

## 9. Offene Punkte

1. [?] Bitreihenfolge seriell auf dem Band (für `.wav`).
2. [?] Länge der BOT-/EOT-Marke; Abstand BOT-Marke ↔ Vorspannende (K2: „450 mm“ unsicher).
3. [?] Bedeutung des 17-Byte-Blocks beim BCU-`CLOSE`.
4. [?] FEX-Formate (Programm fehlt).
5. [?] EGOS-/K5261-Dateiformat (M7).
6. [?] Ob ISO 3407 dieselbe CRC vorschreibt.
