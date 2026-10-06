# P8000 — 16-Bit-Rechnerkarte: Auswertung der Schaltpläne (AP P2b)

Stand 2026-10-06. Quelle:
`~/projects/robotron/P8000/16bit_Schaltplan/` — Stromlaufplan **P 8000/16.4, Ms 889 255-2**
(Index 4, Bl. 1–14; Bl. 2, 4, 11 zusätzlich in späteren Ständen 1989 als `.jpg`) und Index 1
(Bl. 5–14, Bl. 1–4 fehlen). Abgleich gegen `hw_16bit.md` (AP1) und — wo es eine Frage
entscheidet — gegen MON16 (`doc/p8000/eproms/16BIT/`, Quellen github.com/OlliL/P8000).
Rang: Schaltplan > Handbuch > Software.

## 0. Lesehinweise

- Blätter liegen **ungedreht** (Schriftfeld rechts unten). Rasterangaben: Buchstabe = Zeile
  (A oben … H unten, linker Rand), Ziffer = Spalte (1 links … 10 rechts), ±1 Feld.
- Sicherheitsgrade wie in `schaltplan_8bit.md`: **gelesen** / **abgeleitet** / **unsicher**.
- Pfeile mit „(Bl. n)" sind Blattverweise.

### 0.1 Blattübersicht Index 4 (Ms 889 255-2/-3, „P 8000/16.4", ÄZ 2 = ÄM 405807 vom 1.7.88)

| Bl. | Inhalt |
|---|---|
| 1 | CPU D35 (UB8001), Adress-/Statuslatches (IAD/LAD/ISNAD/ZST), Datenpuffer ADIN |
| 2 | Statusdekoder (2× DS8205), TRPL-/IF1L-Latches, NMI-Quittung, RUN-LED-FF, Bussteuersignale |
| 3 | drei UB8010: 3D36 = **Code-MMU (1)**, 2D36 = **Data-MMU (2)**, 1D36 = **Stack-MMU (3)** |
| 4 | On-Board-EPROM (4× 2732) und SRAM (4× U214) |
| 5 | MMU-Auswahllogik (MCODE/MDATA/MSTACK), NBR + Vergleicher, On-Board-Dekoder, NMI-Identifier |
| 6 | Takt DL8127/D23, Reset-Bildung (MRESET, PIORESET) |
| 7 | E/A-Dekoder (2× DS8205), Z80-Signale M1/RD/IORQ, RETI-Erkennung |
| 8 | Speicherbus-Adresstreiber (MMU- vs. lokale Adresse), Datenbustreiber |
| 9 | SCR (D18 DL175), Wartelogik (T2-Wait, Single-Step), SYSDS (Suppress) |
| 10 | CTC0/CTC1, Baudtakt-Teiler, Paritäts-FF |
| 11 | SIO0/SIO1, V.24/IFSS |
| 12 | PIO0/PIO1 (Kopplung X2/X3) |
| 13 | PIO2 (WDC X8/X16), Interruptkette |
| 14 | Speicherbus X9–X13 (Steckerverdrahtung) |

Index 1 liegt nur mit Bl. 5–14 vor (Ms 889 2xx, s. §9).

## 1. MMU-Steuerlogik (Index 4, Bl. 3, 5, 9)

### 1.1 Anschluss der drei UB8010 (Bl. 3) — gelesen

Pinbelegung aus dem Symbol (Data-MMU beschriftet): 36 CLK, 5 /RESET, 47 R/W, 48 N/S, 1 /CS,
45 /DS, 46 /AS, 41–44 ST3…ST0, 24…30 SN6…SN0, 31–34/37–40 AD15…AD8, 2 DMASYNC, 4 /SUP,
3 /SEGT, 6…22 A23…A8.

| | Code-MMU 3D36 | Data-MMU 2D36 | Stack-MMU 1D36 |
|---|---|---|---|
| **/CS** (Pin 1) | **LAD1** | **LAD2** | **LAD3** |
| **N/S** (Pin 48) | **MCODE−** (Bl. 5) | **MDATA−** | **MSTACK−** |
| SN6 (Pin 24) | SN6 vom CPU-Latch | **fest Masse** | **fest Masse** |
| SN5…SN0, AD15…AD8, ST3…ST0 | gemeinsam (ISNAD/IAD/ZST) | gemeinsam | gemeinsam |
| CLK / /RESET / R/W / /DS | CLOCK 8000 / MRESET− / ZR/W− / IDS− | dto. | dto. |
| /AS | Q̄ von D31 (74S112, S = NAND(IAS, PHIDELAY), Takt INT2PHI) — **verzögerter AS** | dto. | dto. |
| DMASYNC (Pin 2) | ZBUSACK− | ZBUSACK− | ZBUSACK− |
| A8…A23 | **TRAD8…TRAD23** (wired, Tristate) | dto. | dto. |
| /SEGT, /SUP | gemeinsam, Pull-up 3N1 → `SEGT−` (Bl. 1), `SUP−` (Bl. 9) | | |

Folgerungen:
- **Special-I/O-Auswahl:** /CS liegt direkt an einem gelatchten Adressbit — **AD1 = 0 wählt die
  Code-MMU, AD2 = 0 die Data-MMU, AD3 = 0 die Stack-MMU** (low-aktiv, mehrere gleichzeitig möglich,
  z. B. Low-Byte F0H = alle drei). Die Z8010 reagiert auf /CS nur in Special-I/O-Zyklen (Status
  0011); die Befehlsnummer steht wie im Z8010-Datenblatt in AD8–15. Eine eigene Special-I/O-
  Dekodierung gibt es nicht. *gelesen (Verdrahtung), Folgerung abgeleitet.*
- **N/S als Auswahlsignal:** Die Logik von Bl. 5 schaltet je Speicherzyklus genau eine MMU über ihren
  N/S-Eingang „aktiv". Das setzt voraus, dass die Software die MMUs im **Mode-Register mit
  MST = 1 (Multiple Segment Tables) und NMS** so programmiert, dass nur die MMU mit passendem
  N/S-Pegel übersetzt (N/S-Pegel aktiv = Low = „System"; die gewählte MMU sieht N/S = 0, die anderen
  N/S = 1). Welches NMS-Bit die Software setzt, ist **Software-Frage** (Gegenprobe §1.4). *abgeleitet.*
- **SN6** sieht nur die Code-MMU. Data- und Stack-MMU haben SN6 = 0 fest — passt zur Handbuchangabe
  „beide für 0…63 programmiert, URS = 0"; die Unterscheidung 0…63 / 64…127 macht die externe Logik.
- Alle drei treiben denselben übersetzten Bus TRAD8–23; die nicht gewählten MMUs müssen ihre
  Ausgänge im Tristate halten (Z8010-Verhalten bei nicht übersetzendem Zyklus).

### 1.2 Auswahllogik MCODE/MDATA/MSTACK (Bl. 5 G7–G9, C8–D9) — gelesen, Gatter verfolgt

Bausteine: D29 (74S51, AND-OR-Invert), 1D3 (NAND), 1D4 (2× NOR), 1D8/7D8/3D8 (DL010, NAND3), D28/2D6
(Inverter). Eingänge: `SEGUSER`/`NONSEGUSER` (SCR Bit 2 und Komplement, Bl. 9), `IN/S−` (gelatchtes
N/S, 1 = Normal), `IST2`, `IST3` (gelatchter Status), `ISNAD6` (gelatchtes SN6), `A>B` (NBR-Vergleich).

```
N  = NAND(IST3, K)            K: Leitung von Bl. 5 Mitte, nicht sicher verfolgt (vermutl. „kein On-Board-Zugriff")
P  = NAND(SEGUSER, IN/S−)                 → P = 0: segmentiertes Anwenderprogramm im Normal-Mode
Q  = NAND(IN/S−, NONSEGUSER, NOT IST2)    → Q = 0: nichtsegm. Anwender, Daten-/Stackzyklus (kein Befehlsholen)
X  = NOR(P, N)          Y = NOR(Q, N)
MCODE−  = NAND(P, Q, NOT N)
MDATA−  = NOT( (NOT ISNAD6 ∧ X) ∨ (A>B ∧ Y) )
MSTACK− = NOT( (ISNAD6 ∧ X)     ∨ (NOT A>B ∧ Y) )
```

Damit (nur Speicherzyklen, IST3 = 1):

| Lage | gewählte MMU |
|---|---|
| System-Mode (N/S = System) | **Code-MMU** — für Befehl, Daten und Stack |
| Normal-Mode, SCR Bit 2 = 1 (SEG USR), SN6 = 0 | **Data-MMU** |
| Normal-Mode, SEG USR, SN6 = 1 | **Stack-MMU** |
| Normal-Mode, SEG USR = 0, Befehlsholen (ST = 1100/1101) | **Code-MMU** |
| Normal-Mode, SEG USR = 0, Daten-/Stackzyklus (ST = 10xx), Adresse High < NBR | **Data-MMU** |
| dto., Adresse High ≥ NBR | **Stack-MMU** |

✓ Handbuch §5.2 (drei Zustände). **Neu gegenüber dem Handbuch:**
- Der Status „Stack" (1001) wird **nicht** ausgewertet; Data/Stack hängt im nichtsegmentierten Fall
  **allein am NBR-Vergleich**, im segmentierten allein an SN6.
- **Gleichheit:** `A>B` mit A = NBR, B = Adresse A15–A8 ⇒ Adresse-High **= NBR → Stack-MMU**
  (Handbuch: „unklar"). *gelesen (74S85-Kaskade, A = Latch, B = LAD), Kaskadenrichtung abgeleitet.*
- Die Segmentnummer, die die CPU im nichtsegmentierten Normal-Mode ausgibt, geht unverändert (SN0–5)
  an Data-/Stack-/Code-MMU; „Segment 63" ist also eine Eigenschaft der CPU-Ausgabe bzw. der
  MMU-Programmierung, nicht der Logik. *abgeleitet.*

### 1.3 NBR (Bl. 5 E5–E7) — gelesen

- Latch **3D25 (DS8282)**, Eingänge ADIN0–7, `STB = NOR(SELNBRKREG−, IR/W−)` (Schreiben auf FFD1).
  Bitzuordnung: **NBR-Bit n ↔ Adressbit A(n+8)** (A1…A8 des Latches sind ADIN7…0 gezeichnet,
  Y1…Y4 → oberer 74S85 2D32 gegen LAD15–12, Y5…Y8 → unterer 1D32 gegen LAD11–8).
- Rücklesen über **8D22 (DL541)** auf ADIN7…0, bitgleich (FFD1 lesbar ✓ Handbuch „r/w").
- **/OE des NBR-Latches = Q von 4D16** (DL074). D = „SN1…SN5 alle 0", getaktet mit INTPHI ∧ ZDS−;
  /R = Zugriff auf FFD1 (erzwingt Q = 0 → Ausgänge an, nötig fürs Rücklesen), /S = Zugriff auf
  **FFC9** (`SELSYSBRKREG−`, Index 4 laut Handbuch „frei"). Bei Q = 1 (Segment 0/1/64/65) sind die
  Latch-Ausgänge hochohmig; die Vergleichereingänge A schweben dann (TTL ⇒ „1") ⇒ A>B ⇒ Data-MMU.
  Die Bedeutung ist **unsicher** (Restlogik des Index-0-SBR?). Für den Emulator unerheblich, solange
  der nichtsegmentierte Anwender nicht in Segment 0/1 läuft. *gelesen (Verbindungen), Deutung unsicher.*
- Reset: DS8282 hat keinen Reset ⇒ NBR nach Einschalten **undefiniert**.

### 1.4 Gegenprobe Software (WEGA-Kern `uts/conf/mch.s`, MON16 `p.init.s`)

- Konstanten in Kern und Monitor: `STACK_MMU := %00F6`, `DATA_MMU := %00FA`, `CODE_MMU := %00FC`,
  `ALL_MMU := %F0` — Low-Byte-Bit 3/2/1 = 0 wählt Stack/Data/Code. **✓ deckt sich bit-genau mit der
  /CS-Verdrahtung LAD3/LAD2/LAD1.** Befehle in AD8–15 (`+%0100` SAR, `+%0B00` SDR, `+%0800` Basis,
  `+%1100` Reset VTR, `+%1500` CPU-Inhibit setzen, `+%0200…%0700` Violation-/Status-Register).
- Mode-Register im Kern: `soutb STACK_MMU, #%D3` = MSEN, TRNS, **MST = 1, NMS = 0**, URS = 0, ID = 3.
  ✓ Das passt genau zur Auswahl über N/S: die gewählte MMU sieht N/S = 0 (= NMS) und übersetzt.
  **Folge für den Emulator:** Die Z8010 prüft das SYS-Attribut gegen ihren N/S-Eingang — der hier
  nicht der CPU-Modus ist. Ein SYS-Segment (Kern: Segment 3EH, Attribut 22H) löst in der Code-MMU
  daher **keine** SYS-Verletzung aus, wenn ein Anwenderbefehl zugreift; der Schutz läuft
  stattdessen darüber, dass Anwenderzyklen gar nicht in die Code-MMU gehen (außer beim
  nichtsegmentierten Befehlsholen). *abgeleitet.*
- `mmu.h`: `SBREAK 0xFFC9`, `UBREAK 0xFFD1`; nichtsegmentierter Anwender: `TEXT -1`, `DATA 0x3F`,
  `STAK 0x7F`; Kern läuft segmentiert, Anwender wird mit PC `<%3F>%0000` (nichtsegmentiert) gestartet.
  Dass Stack = Segment **7FH** (SN6 = 1) heißt: bei SEG USR = 1 landet der Stack in der Stack-MMU
  über SN6 — bei SEG USR = 0 über den NBR. Beides programmiert die MMU-Segmentnummer 3FH.
- MON16 `ENTRY_`: `sout ALL_MMU, r3` mit r3 = 0 („MMU in Tristate bringen", Mode = 0), Refresh
  `ldctl REFRESH, #%9E00`, FCW = 4000H (nichtsegmentiert, System) — s. §2.

## 2. On-Board-Speicher (Bl. 4, Bl. 5 B4–C6)

**Dekoder 5D24 (DS8205), gelesen:** A0 = LAD13, A1 = LAD14, A2 = Masse; E1 = `BDMEMON−` (SCR Bit 0),
E2 = NOT `IMEMREQ`, E3 = Fenster `W = NOR(ISNAD0, ISNAD6) ∧ NOT LAD15 ∧ Q(4D16)` (3D9, DL011), wobei
Q(4D16) = „SN1…SN5 = 0" (§1.3). Ausgänge Y0 = `BDMEMA` (0000–1FFF), Y1 = `BDMEMB` (2000–3FFF),
Y2 = `BDMEMC` (4000–5FFF), **Y3 (6000–7FFF) unbeschaltet**.

| Bereich (Segment 0) | Inhalt | Beleg |
|---|---|---|
| 0000–1FFF | EPROM-Paar 1D41 („LOW EVEN", D8–15) + 3D41 („LOW ODD", D0–7) | gelesen |
| 2000–3FFF | EPROM-Paar 2D41 („HIGH EVEN") + 4D41 („HIGH ODD") | gelesen |
| 4000–47FF, gespiegelt bis 5FFF | SRAM 4× U214 (1K×4), A0–A9 = LAD1–10 ⇒ 1 K Worte = 2 KB, LAD11/12 nicht dekodiert ⇒ **4 Spiegel** ✓ Handbuch | gelesen |
| 6000–7FFF | **nichts** — liegt aber im On-Board-Fenster (W hängt nicht an LAD13/14) ⇒ vermutlich auch kein Hauptspeicherzugriff, Lesewert = schwebender Bus | abgeleitet / Lesewert unsicher |

- **Fenster = nur Segment 0** (SN0…SN6 = 0), Offset 0000–7FFF, solange SCR Bit 0 = 0 — **unabhängig von
  MMU ON und vom CPU-Modus**. Das Handbuch sagt nur „Segment 0". *gelesen.*
- EPROM: 2732, A0–A11 = LAD1–12, /CE fest Masse, `/OE = BDMEMx− ∨ IW/R−` (nur Lesen). SRAM:
  `/WE = NAND(IMEMREQ, IW/R)`, byteweises Schreiben über getrennte /CS je Byte-Spur
  (`/CS = BDMEMC− ∨ (Schreiben ∧ Byte ∧ LAD0 passt nicht)`, 2D9/3D14/5D6). *gelesen.*
- **EPROM-Bezeichnung (löst `hw_16bit.md` W7):** „LOW/HIGH" im Plan = untere/obere 8 KB, „EVEN/ODD" =
  Byte-Spur (Even = D8–15, Odd = D0–7). Die Abzüge `MON16_nX_v` folgen dem: **n = 1 → 0000–1FFF,
  n = 2 → 2000–3FFF; H = Even/D8–15, L = Odd/D0–7** (`make_full.sh` verschränkt H1/L1, dann H2/L2;
  `MON16_1H_3.1` beginnt `00 C0 80 01`, `1L` mit `00 00 00 D0` ⇒ Wort 0000H = 0000, 0002H = C000,
  0004H = 8000, 0006H = 01D0). *gelesen + Abzug geprüft.*
- **Reset-Vektor (Gegenprobe Abzug/Quelle):** Z8001 liest nach Reset FCW = **C000H** (segmentiert,
  System) aus 0002H, PC-Segment **8000H** (= Segment 0) aus 0004H, PC-Offset aus 0006H. MON16
  `ENTRY_` schaltet sofort auf FCW = 4000H (nichtsegmentiert, System) und läuft so im Segment 0.
- **Umschaltung nach dem Boot:** nur über SCR Bit 0 (Software). Kein Hardware-Automatismus. Nach
  Reset ist SCR = 0 (D18 DL175, /CLR = MRESET−, Bl. 9) ⇒ On-Board an, MMU aus. *gelesen.*
- Wartezyklus: On-Board-Zugriffe erzeugen `T2WAIT−` (Bl. 9 A6, 4D16 mit IMEMH) — s. §7.
- `IMEML`/`IMEMH` (Bl. 5 → Bl. 7, 8, 9): IMEML = NAND(W, BDMEMON, …) = **0 bei On-Board-Zugriff**;
  sperrt die MMU-Auswahl (`N` in §1.2) und die übersetzte Adresse (Bl. 8, 2D8). IMEMH (3D7) =
  On-Board aktiv (für Wait). *abgeleitet, Leitungen über Bl. 5 verfolgt, Zuordnung IMEMH/IMEML zu
  3D7/3D8 nicht ganz sicher.*

## 3. Speicherbus und Adressbildung (Bl. 8, 10, 14)

**Adresstreiber (Bl. 8 D2–E8), gelesen** — fünf DL541, ausgangsseitig paarweise verbunden:

| Busadresse | MMU ON (SCR Bit 1 = 1) | MMU OFF |
|---|---|---|
| BUSA0–7 | LAD0–7 (5D22, immer) | LAD0–7 |
| BUSA8–15 | **TRAD8–15** (6D22, MMU-Ausgang) | LAD8–15 (11D22) |
| BUSA16–22 | **TRAD16–22** (3D22) | **ISNAD0–6** = Segmentnummer (7D22) |
| BUSA23 | TRAD23 | **0** (7D22-Eingang A8 an Masse) |

- Physische Adresse bei **MMU aus = Segmentnummer × 64 K + Offset** (A23 = 0, also bis 8 MB) —
  das Handbuch sagt nur „lokale Adresse". Damit liegt „<00>8000H" (Handbuch) tatsächlich bei
  physisch 008000H, Segment 1 bei 010000H usw. *gelesen.*
- MMU ein: A0–A7 kommen **immer** direkt aus dem Offset (Z8010-Prinzip, Seitengröße 256 B), A8–A23
  aus der gewählten UB8010.
- Freigabe der übersetzten Adresse: `NAND3(MMUONH, IST3, IMEML)` (2D8, Bl. 8 B3) ⇒ nur bei
  Speicherzyklus und **nicht** bei On-Board-Zugriff. *gelesen; Weiterführung zu den OE-Eingängen
  der Treiber abgeleitet.* „MMU ON wirkt nicht auf die MMU-Schaltkreise" ✓ Handbuch: SCR Bit 1
  schaltet nur diese Treiber um.
- Busmasterwechsel: `BUSACKH` (Bl. 8 D1) gibt die Treiber frei; DMASYNC der MMUs = ZBUSACK− (§1.1).

**DRAM-Karten:** Für die Hauptspeicherkarten liegt in diesem Ordner **kein Schaltplan** vor
(nur 16-Bit-Karte). Moduladressdekodierung, Zeilen-/Spaltenmultiplex, Paritätsart: weiterhin nur
Handbuch (`hw_16bit.md` §6). Plan „Dynamischer-RAM" laut `quellen.md` bei pofo.de — **nicht
ausgewertet** (Frage 3 offen auf Kartenseite).

**Parität → NMI (Bl. 10 F1–F3), gelesen:** `BUSPE−` (Pull-up 3N1) → Inverter 5D6 → **J** von D31
(74S112), K = 0, Takt = **SYSDS** (Ende des Datenstrobes), **/R = CLRPARITY−** (SCR Bit 3).
Q → `LATCHMEMERROR` (NMI-Identifier Bit 2, Bl. 5), Q̄ → `LATCHMEMERROR−` (NMI-Logik Bl. 2).
⇒ Paritätsfehler wird **am Ende des Zyklus gespeichert** und bleibt stehen, bis SCR Bit 3 = 0
geschrieben wird; solange Bit 3 = 0 ist, wird gar nicht gespeichert ✓ Handbuch. MON16 setzt dazu
Bit 3 kurz auf 0 und wieder auf 1 (`p.init.s` Z. 1477–1481).

**Refresh:** keine eigene Logik auf der Karte; Status 0001 → `BUS RFSN−` (9D22, Bl. 2). MON16 lädt
`REFRESH = 9E00H` (Enable, Rate 1EH = 30 × 4 Takte … laut Z8000-Format) — Handhabung im Emulator:
Zählerregister, kein Inhaltsverlust. *Software gelesen.*

## 4. Interrupt, NMI, Trap (Bl. 2, 5, 7, 10–13)

### 4.1 Statusdekoder (Bl. 2 C4–E5) — gelesen
2D24 (DS8205, freigegeben bei IST3 = 0) liefert Status 0…7, 1D24 (E3 = IST3) Status 8…15.
Daraus u. a. `VIACK−`, `I/OREF−` (0010), `MMU/EPUREF−`, `STACKMEMREQ−`, `ENABNMI−` (NMI-Quittung
0101), `BUS RFSN−`, `BUSNVIACK−` (Index 4, Bus X9 b18). Special-I/O (0011) erscheint nur als
Bussignal `MMU/EPUREF−` (Bl. 8 → TESTDATA-Gatter) — für die MMUs selbst genügt /CS (§1.1).

### 4.2 NMI (Bl. 2 F1–G4, Bl. 5 A5–A8) — gelesen/abgeleitet
- `X3:B5 NMIU8000−` (von der 8-Bit-Karte, §5) → Inverter 5D6 → `MANUALNMI`.
- NMI-Identifier-Puffer **1D22 (DL541, Bl. 5)**, /OE = `ENABNMI−` (Status 0101):

| AD-Bit | Quelle | Handbuch |
|---|---|---|
| 0 | `MANUALNMI` | 0001 ✓ |
| 1 | `POWER FAIL` (X1:B4 über D10) | 0010 ✓ |
| 2 | `LATCHMEMERROR` (Paritäts-FF) | 0100 ✓ |
| **3** | **`MSDOSNMI`** (Speicherbus X9 b22, Index 4) | Handbuch: „AD3 immer 0" ✗ |
| 4–7 | nicht getrieben (8D22/1D22 nur 4 Bit) | |

  **Abweichung:** Index 4 liefert in Bit 3 die Bus-NMI-Quelle `MSDOSNMI` (vermutlich eine
  MS-DOS-Koprozessorkarte). Die Bits sind **Pegel**, nicht gespeichert (außer Parität); ein
  kurzer Tastenimpuls kann beim Quittieren schon vorbei sein. *gelesen.*
- Der CPU-NMI entsteht aus MANUALNMI, LATCHMEMERROR− und POWER FAIL− (1D14/2D3/3D8, Bl. 2 F3–F5)
  und wird zusätzlich als `BUS NMI−` (X9 b7) bzw. `ONBDNMI−` (Bl. 7) geführt; die genaue
  Gatterfolge ist im Scan nicht vollständig verfolgbar. *unsicher.*

### 4.3 Trap-Register TRPL/IF1L (Bl. 2 A3–C6) — gelesen
- **TRPL = 2D20 (DL374)**: D = LAD0–7, **Takt = `SEGT`** (Flanke beim Aktivwerden des Segment-
  traps), /OE = `TRPLOBYTE−` (FFF1). ⇒ TRPL hält das Offset-Low-Byte **des Zyklus, in dem die MMU
  /SEGT zog**, bis zum nächsten Trap. Kein Reset.
- **IF1L = 3D20 → 1D20 (2× DL374)**: 3D20 übernimmt LAD0–7 bei jedem Befehlsholzyklus
  (Takt = IDS− ∨ Status-Dekoder 1D24 Y4/Y5 — welcher der beiden Ausgänge, ist im Scan nicht sicher
  unterscheidbar; Handbuch: erstes Befehlswort), 1D20 friert das Byte ein (**Takt = SEGT**, im Stand 1989 eindeutig zu sehen),
  /OE = `IFILOBYTE−` (FFF9). Bitgleich (ADINn = LADn). *gelesen.*
- `SEGT−` (MMU, Pull-up) geht direkt an den SEGT-Eingang der UB8001 (Bl. 1). Ein externer
  Trap-Erzeuger existiert bei Index 4 nicht ✓ Handbuch.

### 4.4 Suppress (Bl. 9 E2–E5) — gelesen
`SYSDS = AND4(SUP−, …, IMEML, IDS−-abgeleitet)` (2D12 DL021 mit 1D16-FF, getaktet INTPHI ∧ IAS):
**SUP− = 0 unterdrückt den Datenstrobe zum Speicherbus** (SYSDS → Bl. 1, 10) ✓ Handbuch. Der
On-Board-Speicher hängt nicht an SYSDS. Folge für den Emulator: Schreibzugriff mit Verletzung
wird nicht ausgeführt, der Trap folgt. *gelesen (Gatter), Zuordnung der Eingänge 1D16 abgeleitet.*

### 4.5 Vektorinterrupt, Kette, RETI (Bl. 7, 10–13) — gelesen
- Alle INT-Ausgänge (CTC0, CTC1, SIO0, SIO1, PIO0, PIO1, PIO2) liegen an **`VI−`** (Bl. 1);
  NVI wird nicht erzeugt ✓.
- Kette (Bl. 13 A–D): CTC0 IEI = 5P → CTC0.IEO → CTC1 → SIO0 → SIO1 → PIO0 → PIO1 → PIO2.IEI
  (PIO1.IEO, Bl. 12) → PIO2.IEO; daraus `SYSIEO` (Bl. 7) und `BUSIEO` (Bl. 14 → X13 B_IEI).
  **Index 4 hat ein Vorgriffsgatter**: `1D12 = AND4(CTC0.IEO, CTC1.IEO, SIO0.IEO, SIO1.IEO)` und
  `3D7 = AND(… , PIO0.IEO, PIO1.IEO)` → `3D9 = AND3(…, PIO2.IEO)` — die Kette zum Bus wird also
  parallel ausgewertet, nicht nur durchgereicht. Logisch gleich der Reihenkette ✓ Handbuch.
- Z80-Signale für die Peripherie werden auf Bl. 7 (2D16-Flipflops 118, 1D11, 5D7/4D14) aus Status,
  DS und `VIACK−` erzeugt; RETI = Schreiben auf **FFE1** (1D11 NAND aus LAD3/4/5 und FFE1-Zweig).
  Die Bytefolge ED 4D erzeugt die Hardware **nicht** selbst — die Software schreibt sie ✓ Handbuch.
- E/A-Dekoder (Bl. 7 A4–C7), **gelesen**: `D13 = NAND8(LAD0, LAD7…11, AND(LAD12,13,14),
  AND(LAD15, NOT I/OREF−))` ⇒ **A15–A7 = 1, A0 = 1, Status 0010 (Standard-E/A)**; LAD6 wählt
  zwischen 4D24 (FF80–FFBF) und 3D24 (FFC0–FFFF), LAD3–5 das Bauteil, **A1/A2 gehen nur an die
  Bausteine**. Ergebnis = Handbuchtabelle, **keine Spiegel**, gerade Adressen unbelegt. 3D24 Y4
  (FFE1) ist am Dekoder offen; RETI hat einen eigenen Zweig (1D11 → `FFE1`).

## 5. Kopplung zur 8-Bit-Karte (Bl. 12, Bl. 6, Bl. 2) — gegen `schaltplan_8bit.md` §7

### 5.1 Bitbelegung (Bl. 12) — gelesen

| 16-Bit-Baustein | Bit | Signal | Stecker | Gegenstelle 8-Bit (P2a §7) |
|---|---|---|---|---|
| **PIO0 A** (FF91) | A0…A7 | D0…D7/16-8 | X2 B2, B9, B8, B7, B1, A10, A8, A1 | PIO0 A (0CH) ✓ |
| | ARDY | **DD-8** | X2 B5 | → 8-Bit PIO0 /ASTB ✓ |
| | /ASTB | **RDY/16-8** | X2 B3 | ← 8-Bit PIO0 ARDY ✓ |
| **PIO0 B** (FF93) | B0 | **INT-8** | X2 A6 | → 8-Bit PIO0 B0 ✓ |
| | B1…B4 | V1…V4/16-8 | X2 A5, A4, A3, A2 | → 8-Bit PIO0 B1–B4 ✓ |
| | B5 | **DD-16** (Index 4) | X2 A11 | Index-1-Plan 8-Bit: nicht gezeichnet |
| | B6 | **E0-16** (Index 4) | X2 A9 | dto. |
| | B7 | P0-B7 | X2 B6 | dto. |
| | BRDY, /BSTB | P0-BRDY, P0-BSTB | X2 A7, B4 | unbenutzt |
| **PIO1 A** (FF99) | A0…A7 | D0…D7/8-16 | X3 B4, A6, A7, B13, B11, B10, B9, B8 | ← DS8282 10H ✓ |
| | ARDY | **E0-8** | X3 A3 | → 8-Bit PIO0 **B6** |
| | /ASTB | **RDY/8-16** | X3 A5 | ← DS8282 14H Bit 7 ✓ |
| **PIO1 B** (FF9B) | B0 | **INT-16** | X3 A13 | ← 14H Bit 0 ✓ |
| | B1…B6 | V1…V6/8-16 | X3 A12, A11, A10, A9, A8, B7 | ← 14H Bit 1–6 ✓ |
| | B7 | P1-B7 | X3 B6 | — |
| | BRDY, /BSTB | P1-BRDY, P1-BSTB | X3 B3, A4 | unbenutzt |
| — | | **RESET** | X3 B1 | ← 8-Bit PIO0 B7 |
| — | | **NMIU8000−** | X3 B5 | ← 8-Bit NMI-Logik |
| — | | **TRESET−** | **X3 A1** | — (neu, s. 5.2) |
| — | | RUN-LED | **X2 A13, X3 A2** | (Bl. 2 F1) |
| — | | Masse / 5P | X2 B11–13, X3 B12 / X3 B2 | |

Befunde:
1. **Port A beider PIOs = Daten mit Hardware-Handshake, Port B = Vektor-/Steuerbits** — bestätigt
   die Deutung in `hw_16bit.md` §4.5. Die 16-Bit-Seite empfängt über **PIO1 A (Modus 1, Eingabe)**:
   die 8-Bit-Seite legt das Byte per OUT 10H an und erzeugt /ASTB per Software (14H Bit 7);
   PIO1-ARDY („E0-8", Eingabe übernommen) geht an 8-Bit-PIO0-B6. Die 16-Bit-Seite sendet über
   **PIO0 A (Modus 0, Ausgabe)**: ARDY („DD-8") strobt direkt in die 8-Bit-PIO0 A, deren ARDY
   („RDY/16-8") strobt zurück in /ASTB.
2. **Widerspruch zu P2a §7:** dort ist 8-Bit-PIO0-**B6** als *Ausgang* „EO_8/DD_16, 8→16" geführt.
   Auf der 16-Bit-Seite treibt X3:A3 aber **PIO1-ARDY** (Ausgang) ✓ Handbuch Tab. 3.6-20 „ARDY1/E0-8,
   A". ⇒ 8-Bit-PIO0-B6 muss **Eingang** sein (Richtung im P2a-Plan nur aus dem Namen geschlossen).
3. **RUN-LED liegt an X2:A13 und X3:A2**, nicht an X2:A2 (= V4/16-8). Löst `hw_16bit.md` W3 /
   `hw_8bit.md` W6: die Handbuchangabe „X2:A2" ist ein Schreibfehler für X3:A2. Quelle: RS-Flipflop
   3D3 (Bl. 2 F1): gesetzt von `MRESET−` ∨ LEDEIN (FFD9), gelöscht von LEDAUS (FFB9); Ausgang nach
   Reset = 0 (= LED an, abgeleitet).
4. Index-4-Leitungen DD-16/E0-16/P0-B7/P1-B7 sind **Port-B-Bits** (Software-Handshake), keine
   eigenen Hardware-Strobes.

### 5.2 Reset/NMI über die Kopplung (Bl. 6 C1–E5) — gelesen
- **X3:B1 `RESET` ist high-aktiv** und hat einen Pull-up 4N1 nach 5P: Treibt die 8-Bit-Seite nichts
  (PIO0-B7 nach deren Reset = Eingang, Pull-up auch dort), **steht die 16-Bit-CPU im Reset**. Erst
  `B7 = 0` auf der 8-Bit-Seite gibt sie frei. **Polarität damit geklärt** (P2a Frage 1): 8-Bit-PIO0-B7
  = 1 ⇒ U8001 im Reset + NMI-Taste an U880; B7 = 0 ⇒ U8001 läuft + NMI-Taste an U8001.
- Weg: RESET → Inverter 5D6 → RC (R15/C10) → `1D12 = AND4(PRES−, NOT RESET, BUSTESTRESET−,
  SOFTRESET−)` → /S von **D15** (DL112 JK-FF) → Q̄ = `MRESET−`. Freigabe synchron: J = 0, K =
  `POWER FAIL−` (normal 1), Takt aus der Taktkette ⇒ MRESET− geht mit dem nächsten Takt nach
  Wegfall aller Quellen inaktiv. *Gatter gelesen, Takteingang abgeleitet.*
- **`PIORESET−` = AND(PRES−, X3:A1 `TRESET−`)** (4D7) — **nicht** MRESET−. Die drei PIOs werden über
  `M1 ∧ PIORESET−` zurückgesetzt (Bl. 12 4D7) und **überleben** damit einen Reset durch die
  8-Bit-Seite (X3:B1) und `SOFTRESET` (FFE9). CTC und SIO hängen an MRESET−. *gelesen.* X3:A1
  (`TRESET−`, Pull-up) fehlt in den Handbuchtabellen und im 8-Bit-Plan Index 1 ⇒ dort offen
  (Pull-up ⇒ inaktiv).
- `NMIU8000−` (X3:B5) → MANUALNMI (§4.2).
- Handbuch-Abweichung: Tab. 3.6-2/3.7-2 behandelt PIO wie die übrigen Bausteine („MRESET− setzt
  PIO zurück") — **falsch für die PIOs**.

## 6. Winchester-Anschluss PIO2 (Bl. 13) — gelesen

| PIO2-Bit | über | Signal | X8 | X16 | Pegel |
|---|---|---|---|---|---|
| A0…A7 | 1D26 (DS8286, bidirektional) | D0…D7 | 14, 2, 5, 17, 4, 16, 3, 15 | A7, B9, A3, A5, A2, B4, B7, B8 | bitgleich ✓ Handbuch |
| ARDY | Bl. 1 (Inverter) | `WDARDY−` | 1 | A1 | ARDY = 1 ⇒ WDARDY− = 0 |
| ARDY (direkt) | — | `WDARDY` | 12 | — | |
| /ASTB | Abschluss 1R1/1R2 | `ASTB−` | 18 | B2 | |
| B0, B1, B2 | 1D21 (DL540, **invertierend**) | ST0, ST1, ST2 | 21, 10, 22 | A9, B3, A6 | **Bn = NOT STn** |
| B3 | 1D21 | — | 23 | — | Ausgang, unbenutzt |
| B4 | 1D21 | — | 11 | — | Ausgang, unbenutzt |
| **B5** | 2D5 (OC-NAND) / 1D21 | **`RST−`** (OC, Pull-up R10) / **`RST`** | 24 / 13 | — / B5 | **B5 = 1 ⇒ WDC im Reset** (RST− = 0, RST = 1); Pull-up 3N1 auf B5 |
| **B6** | 1D21 + 1D26-T | **`TE−`** + Richtung des Datentreibers | 25 | B6 | TE− = NOT B6; B6 = 1 ⇒ T = H (Pfeil Stecker → PIO, abgeleitet) |
| **B7** | 1D21 | **`TR−`** (Eingang, Abschluss 2R1/2R2) | 9 | A11 | **B7 = NOT TR−** |

Folgerungen:
- **Host-Reset des WDC:** nach Reset der 16-Bit-Seite sind die PIO-Bits Eingänge, der Pull-up
  hält B5 = 1 ⇒ **WDC bleibt im Reset, bis die Software B5 = 0 ausgibt**. Beide Polaritäten (X8:24
  low, X8:13/X16:B5 high) kommen aus demselben Bit ✓ Handbuch „Index 4: beide Reset-Ausgänge".
- Daten-Handshake über Port A (ARDY/ASTB), Richtung des Bustreibers und `TE−` über B6 — also
  vermutlich PIO2-A im Modus 0/1 mit Software-Richtungswechsel. Drei Statusbits (invertiert) und
  ein Rückmeldebit TR ✓ `hw_wdc.md` §3 („drei Steuer-, drei Statusbits"). Die Bedeutung von TE/TR
  klärt erst die WDC-Firmware (`firmware/WDC/wdc.firm.s`). *Pegel gelesen, Bedeutung offen.*
- PIO2 hängt in der Interruptkette hinter PIO1, INT an VI−.

- **Gegenprobe MON16 `p.disk.s`** (Kopfkommentar „Anschlussbelegung"): B0 = /ST0 (Interrupt Request),
  B1 = /ST1 (Anforderung der Daten), B2 = /ST2 (Error), B3/B4 frei (Ausgang), **B5 = RESET (WDC)**,
  **B6 = TE und Richtung Datentreiber, 0 = write, 1 = read**, **B7 = TR (Transfer Request), Eingang**;
  Port B Bitbetrieb `%87` (B7, B2–B0 Eingang), Port A wechselt zwischen Modus 0 (`%0F`) und Modus 1
  (`%4F`). ✓ deckt sich vollständig mit dem Plan (inkl. Inversion der Statusbits durch 1D21).

## 7. Takt, CTC, Wartezyklen (Bl. 6, 9, 10)

- **Takt (Bl. 6 A1–C8), gelesen:** D23 = DL8127, Quarz C9 **16 MHz**; Teiler D19 (DL193) +
  2D7/4D6 ⇒ INT4PHI, INT2PHI, INTPHI, CLOCK 8000 (MOS-Pegel über 1VT2) und CLOCK 80 (2VT2) ✓
  Handbuch. DL8127-Eingänge: ST1–ST3 (Status, für Wait), **SSNO/SSNC** (X9 b9/b10, Einzelschritt-
  Taster vom Bus), **RUN/HALT−** (X9 b8), **STEP WAIT−** (Bl. 9). Pin 14 = Power-on-Reset (RC C5).
- **Baudtakt (Bl. 10 C5–D8), gelesen:** Oszillator 6D6 (2 Inverter, Quarz C8 — Wert im Scan nicht
  lesbar) → **D17 (DL093 = 7493) ÷8 → X2:B10**. Die CTC-Takteingänge kommen dagegen von
  **`BUS BAUD CLK` (Bl. 14 = X9 b15)**, nicht direkt vom Teiler. Ob X2:B10 und X9 b15 auf der Karte
  verbunden sind (oder der Baudtakt über den Rückwandbus/die 8-Bit-Karte läuft), ist auf den
  Blättern nicht zu sehen. *unsicher* — Handbuch: „9,832 MHz → ÷8 → 1,229 MHz", `BAUD CLOCK` an X9-15b.
- **CTC0 (2D39, FFA9–FFAF):** CLK/TRG0–2 = BUS BAUD CLK; ZC/TO0 → 3D16 ÷2 → **BAUD0**, ZC/TO1 →
  5D16 ÷2 → **BAUD1**, ZC/TO2 → 5D16 ÷2 → **BAUD2**. **CLK/TRG3 = OR(IDS−, STACKMEMREQ−)**
  (2D14): **ein Zählimpuls je Stack-Speicherzyklus (Status 1001)**. *gelesen.*
- **Single-Step (Gegenprobe MON16 `p.brk.s` Z. 1000–1006):** Kanal 3 im **Zählermodus (C7H)** mit
  Zeitkonstante `STKCTR` („4 Stackoperationen"), dann IRET: die Stack-Lesezyklen des IRET zählen den
  Kanal ab, sein Interrupt kommt nach dem nächsten Anwenderbefehl. **Für den Emulator: CTC0-K3-Trigger
  = jeder Speicherzyklus mit Status 1001**, nicht ein Befehlstakt. Abweichung zu „Single-Step-
  Steuerung" im Handbuch: es gibt keine Hardware-Einzelschrittschaltung, nur diesen Zähler.
- **CTC1 (1D39, FFB1–FFB7):** CLK/TRG0 = BUS BAUD CLK, ZC/TO0 → 6D16 ÷2 → **BAUD3**; **CLK/TRG1 =
  `TAPEQUIT−`** (X9 b14, Index 4); CLK/TRG2 = BUS BAUD CLK (abgeleitet); **ZC/TO2 → CLK/TRG3**
  (Kaskade, Systemuhr K2→K3) *abgeleitet*. CTC-Takt Φ = CLOCK 80 (4 MHz), RESET = MRESET−.
- **Wartezyklen (Bl. 9 A2–A8), gelesen:** `T2WAIT− = AND3(STEP WAIT-Zweig, BUSWAIT− (Pull-up 4N1,
  X9 a8), NOT(Q 4D16 ∨ Q 1D16))` (3D9/3D4). 4D16 wird von `IMEMH` (On-Board) gesetzt und mit
  INTPHI getaktet ⇒ **genau ein Wartetakt T2 bei On-Board-Zugriffen** ✓ Handbuch; 1D16 erzeugt den
  verzögerten AS (`AS PHI DELAY`). Index 4: BUSWAIT− wirkt (Handbuch ✓).

## 8. Serielle Kanäle (Bl. 11) — Kurzbefund

- SIO0 = **1D38**, SIO1 = **2D38**, beide **UA8560** (SIO/0-Bonding: RxTxCB, kein CTSB/SYNCB-Paar
  wie beim 8-Bit-Teil) ✓ tty4 = SIO0-A, tty5 = SIO0-B, tty6 = SIO1-A, tty7 = SIO1-B.
- Takte: BAUD0 → SIO0 RxCA/TxCA über **Wickelfeld XL1 1…10** (✓ Handbuch Index 4 „1…10"), BAUD1 →
  SIO0 RxTxCB, BAUD2 → SIO1 RxCA/TxCA, BAUD3 → SIO1 RxTxCB. *gelesen.*
- Kette: SIO0.IEI = CTC1.IEO, SIO1.IEI = SIO0.IEO; beide INT → VI−. CLK = CLOCK 80, RESET = MRESET−.
- **/W/RDY ist nicht beschaltet** (anders als beim 8-Bit-Teil, `schaltplan_8bit.md` §8). *gelesen
  (keine Leitung am Pin), unsicher wegen Scanqualität.*
- SIO0 CTSA aus RTSA und Steckerleitungen über 2D4/2D6 (Lokalschleife wie 8-Bit-Teil); SIO1 DCD/SYNC
  über 1D14/1D7/3D4/6D6/2D14 = IFSS-/V.24-Umschaltung je Kanal (Muster wie 8-Bit §8, nicht
  Gatter für Gatter verfolgt). Treiber 8 084 (V.24-Sender), 75154 K170UP2 (Empfänger), MB104 +
  Stromquellen (IFSS, Optokoppler). *unsicher in der Einzellogik.*

## 9. Index 1 ↔ Index 4

Index 1 = „P 8000/16.1", gleiche Zeichnungsnummer Ms 889 255, Bl. 5–14 vorhanden (Bl. 1–4 = CPU,
Statusdekoder, MMUs, On-Board-Speicher **fehlen**). Index 4 = „P 8000/16.4", ÄZ 2 vom 1.7.88; die
`.jpg`-Fassungen sind spätere Änderungsstände (Bl. 2 und 4: ÄZ 3 vom 21.3.89; Bl. 11: ÄZ 4 vom
19.7.89, ÄM 406054 — Nummer schwer lesbar). Inhaltliche Unterschiede dieser Nachträge wurden **nicht
vollständig** herausgearbeitet; im Vergleich der Übersichten und der Trap-Latches (Bl. 2) fiel nichts
Funktionales auf (dort: TRPL und IF1L-Endstufe beide mit `SEGT` getaktet — bestätigt).

| Punkt | Index 1 (Plan) | Index 4 (Plan) | Handbuch |
|---|---|---|---|
| MMU-Auswahl, NBR, On-Board-Dekoder (Bl. 5) | identisch | identisch | ✓ |
| NMI-Identifier Bit 3 | **fest Masse** | **MSDOSNMI** | „AD3 immer 0" (nur Idx 1 richtig) |
| E/A-Dekoder (Bl. 7) | FFC9 SELSYSBRKREG, **FFD9 SELSNVIOLREG** (→ Bl. 1, fehlt), **FFE9 SOFTRESET**, FFB9 unbelegt | FFB9 LEDAUS, FFD9 LEDEIN, FFE9 SOFTRESET, FFC9 SELSYSBRKREG (→ Bl. 5) | SOFTRESET „nur Index 4" ✗; SNVR „nur Index 0" ✗ (Index-1-Plan dekodiert es noch) |
| Reset-Bildung (Bl. 6) | X3:B1 RESET, BUSTESTRESET−, SOFTRESET−, PRES−; FF 5D16 (DL074) | dieselben Quellen; FF D15 (DL112), getrennter **PIORESET−** mit **X3:A1 TRESET−** | Idx 1 ohne SOFTRESET ✗ |
| PIO-Reset | über MRESET− (Bl. 6 → Bl. 12) | **nur PRES−/TRESET−** (PIORESET−) | „MRESET setzt PIO zurück" |
| Wartelogik | auf Bl. 6 (BWAIT−, BUSWAIT−, MMU/EPUREF, I/OREF) | auf Bl. 9 (STEP WAIT, BUSWAIT−, T2WAIT) | Idx 1: BUSWAIT „nicht nutzbar" |
| Kopplung PIO0-B5/B6 | **Wickelbrücken**: 4XR1 verbindet PIO0-B5 (X2:A11) mit PIO1-ARDY (E0-8, X3:A3), 5XR1 PIO0-B6 (X2:A9) mit PIO0-ARDY (DD-8, X2:B5) ⇒ Rücklesen der eigenen Handshakes | B5/B6 als **DD-16/E0-16** zum 8-Bit-Teil (Index ≥ 3) | Idx 1 ohne Angabe |
| X3:B2 | Masse | 5P | Idx 4: 5P ✓ |
| WDC (Bl. 13) | nur **RST−** (X8:24); PIO2 BRDY/BSTB an X8:19/X8:6 geführt; X8:13 frei | **RST−** und **RST** (X8:13/X16:B5); X16 | ✓ |
| RUN-LED-Register | — | LEDAUS/LEDEIN, X2:A13/X3:A2 | ✓ |

## 10. Abweichungen vom Handbuch (Kurzliste)

| # | Handbuch | Plan | Folge für den Emulator |
|---|---|---|---|
| B1 | MMU-Adressdekodierung nicht beschrieben | /CS = LAD1/2/3 (Code/Data/Stack), N/S-Eingang = Auswahl, NMS = 0 | Z8010-Modell mit /CS je Low-Byte-Bit, N/S von der Auswahllogik |
| B2 | NBR-Gleichheit „unklar" | Adresse-High ≥ NBR → Stack | Vergleich `>=` |
| B3 | Stack-/Datenzyklus | Status 1001 wird nicht ausgewertet | nur NBR bzw. SN6 |
| B4 | MMU aus = „lokale Adresse" | BUSA16–22 = SN0–6, A23 = 0 | phys = SN·64K + Offset |
| B5 | On-Board „Segment 0" | Fenster 0000–7FFF in Seg. 0, unabhängig von MMU ON/Modus; 6000–7FFF leer | Lesewert 6000–7FFF messen |
| B6 | „AD3 immer 0" | Idx 4: MSDOSNMI | Bit 3 = 0 (keine MS-DOS-Karte) |
| B7 | MRESET− setzt PIO zurück | Idx 4: PIOs nur über PRES−/TRESET− | Kopplungs-PIOs überleben Reset durch 8-Bit-Seite |
| B8 | RUN-LED an X2:A2 | X2:A13 und **X3:A2** | W3 aufgelöst |
| B9 | — | X3:B1 RESET high-aktiv mit Pull-up ⇒ 16-Bit im Reset, solange 8-Bit-PIO0-B7 nicht 0 ausgibt | Kopplungsmodell P11 |
| B10 | „Single-Step-Steuerung" CTC0/3 | Zähler auf Stack-Zyklen (Status 1001) | CTC-Trigger vom Buszyklus |
| B11 | SOFTRESET nur Idx 4, SNVR nur Idx 0 | Idx-1-Plan hat beide | nur bei Idx-1-Gerät relevant |
| B12 | EPROM-Beschriftung mehrdeutig (W7) | 1/2 = untere/obere 8 KB, H/L = D8–15/D0–7 | Ladeordnung `make_full.sh` ✓ |
| B13 | P2a: 8-Bit-PIO0-B6 Ausgang | 16-Bit-PIO1-ARDY treibt X3:A3 ⇒ B6 ist Eingang | `schaltplan_8bit.md` §7 korrigieren |

## 11. Offene Fragen an das Gerät

1. **Leiterplattenindex** der 16-Bit-Karte (V: 1xxx / 4xxx, Aufdruck „Ms 889 1xx (n)"), damit
   feststeht, ob Reset-/PIO-Verhalten nach Index 1 oder 4 gilt.
2. **Lesewert 6000–7FFF** (Segment 0, On-Board an) und **SCR lesen** (Bit 4–7 = 1?) im MON16:
   `D <00>6000` bzw. `IB FFC1`.
3. **NBR/OE-Rätsel (§1.3):** nichtsegmentiertes Anwenderprogramm mit Segment 0/1 vs. 3FH, NBR =
   80H; Zugriffe unter/über 8000H — welche MMU antwortet? (Nur falls WEGA das je tut; sonst
   zurückstellen.)
4. **Baudtakt:** Ist X2:B10 (Teilerausgang) mit X9 b15 (BUS BAUD CLK) verbunden (Durchgangsprüfung),
   bzw. was speist den CTC-Takt, wenn keine Karte an X9 steckt? Quarzwert C8 auf Bl. 10.
5. **Kopplung Index 1:** Wickelbrücken 4XR1/5XR1 gesteckt? (Bestimmt, ob PIO0-B5/B6 die eigenen
   Handshakes zurücklesen.) X3:A1 (TRESET−) auf der 8-Bit-Seite belegt?
6. **NMI-Identifier:** NMI-Taste kurz drücken, im Monitor den gespeicherten Identifier ansehen — ist
   Bit 0 gesetzt (Pegel noch da) oder 0 (Impuls schon vorbei)?
7. **SIO /W/RDY**: wirklich offen? (Sichtprüfung Pin 10/30 der UA8560 1D38/2D38.)
8. **Paritätsart** und **DRAM-Moduladresse**: Plan der DRAM-Karte (pofo.de „Dynamischer-RAM")
   beschaffen; am Gerät Wickelbrücken der gesteckten Karten fotografieren.
9. Unterschiede der 1989er Blätter (Bl. 2, 4: ÄZ 3; Bl. 11: ÄZ 4) — Änderungsmitteilungen
   ÄM 405874(?)/406054 im Gerät bzw. Unterlagen vorhanden?
