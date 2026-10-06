# P8000 — 8-Bit-Rechnerkarte: Auswertung der Schaltpläne (AP P2a)

Stand 2026-10-06. Quelle: `~/projects/robotron/P8000/8bit_Schaltplan/` — Stromlaufplan
**P 8000/8.1, Ms 889 254-2**, 4 Blätter (`Stromlaufplan_01…04.tiff`, Blatt 3 zusätzlich als `.ps`),
Bestückungsplan `Leiterplatte.tiff` (Ms 889 211-2), Stückliste `Stueckliste.pdf` (8 S., Scan).
Abgleich gegen `doc/p8000/hw_8bit.md` (AP1, Handbuchauszug) und — wo es eine Frage entscheidet —
gegen den 8-Bit-Monitor `doc/p8000/eproms/8BIT/MON8_{1,2}_3.1` (mit `tools/z80_disasm2.py`).

## 0. Lesehinweise und Stand der Unterlagen

- **Welcher Index?** Alle vier Blätter tragen „Zugehörige Lp Ms 889 130 **(1)**" ⇒ Leiterplatte
  **Index 1** (Handbuch Kap. 3 §3). Änderungsstände der Zeichnung: Bl. 1/2 ÄZ 0 (317273, 26.10.87)
  und ÄZ 1 (ÄM 317274, 26.11.87); Bl. 3/4 zusätzlich **ÄZ 2 = ÄM 317617 (22.2.88)**. Bestückungsplan
  ÄZ 0…3 (317273, 317274, 317616, 317617). Stückliste: ÄM 317274, 01.12.87, „p 8000/8.1 bestueckt".
  **Ein Plan der Leiterplatte Index 3 liegt nicht vor** — die Index-3-Unterschiede stehen nur im
  Handbuch (`hw_8bit.md` §9) und sind hier **nicht** gegengeprüft (Frage 9).
- **Blätter sind gedreht** gezeichnet (Schriftfeld rechts unten nach 90° Linksdrehung). Rasterangaben
  unten beziehen sich auf das **zurückgedrehte** Blatt: Buchstabe = Zeile (A oben … H unten, am linken
  Rand), Ziffer = Spalte (1 links … 10 rechts, unterer Rand). Angaben sind ±1 Feld genau.
- **Die Zahl im Bausteinsymbol ist eine laufende Elementnummer, kein Typ.** Den Typ liefert die
  Stückliste über die Positionsbezeichnung (z. B. 1D1–3D1 = DL000). Die Gatterart ergibt sich aus
  Symbol + Stückliste und wurde jeweils an der Pinbelegung des TTL-Typs gegengeprüft (z. B. 7402: Gatter 1
  Ausgang Pin 1, Eingänge 2/3).
- **Pfeile an Signalnamen mit „(Bl. n)"** sind Blattverweise, keine Signalrichtung (z. B.
  `RES-RFF (Bl.1)` zeigt auf Bl. 2 aus dem Blatt heraus, obwohl das Signal hereinkommt).
- Sicherheitsgrade: **gelesen** = Verbindung/Beschriftung eindeutig im Bild; **abgeleitet** = aus
  gelesenen Teilen und Bausteinfunktion gefolgert bzw. Linie über mehrere Ausschnitte verfolgt;
  **unsicher** = Linienführung nicht eindeutig verfolgbar oder nur plausibel.

### 0.1 Bausteine (Stückliste, gelesen)

| Bezeichnung | Typ | Funktion (Plan) |
|---|---|---|
| D28 | UA880D | CPU (Bl. 1 B2) |
| D32 | UA858D | DMA (Bl. 1 B4) |
| D33 | U8272D | FDC (Bl. 4 B2) |
| 1D29 / 2D29 / 3D29 | UA855D | **PIO2** (Bl. 4 E2) / **PIO1** (Bl. 1 B9) / **PIO0** (Bl. 3 E2) |
| 1D30 / 2D30 | UA8560D (SIO/0-Bonding: RxTxCB, DTRB, SYNCB) | **SIO0** (Bl. 3 B5) / **SIO1** (Bl. 3 E5) |
| 1D31 / 2D31 | UA857D | **CTC0** (Bl. 1 D9) / **CTC1** (Bl. 3 D3) |
| D17 | DL8127D | Takt + Power-on-Reset, Quarz C16 = **16,000 MHz** |
| D25 | **MH7489 (ČSSR)** | ADP 16 × 4 Bit, **Open-Collector, invertierende Ausgänge** |
| 1D18 / 2D18 | DS8205D (≙ 74138) | E/A-Dekoder |
| 1D19 / 2D19 | DS8282D (nicht invertierend) | Datenport 1 (10H) / Datenport 2 (14H) |
| 1D20…5D20 | DS8286D | Bustreiber (Adresse, Steuerbus, Speicher-/Port-Datenbus) |
| 1D34…4D34 | U214D20 (1K × 4) | SRAM 2 KB |
| 1D35…8D35 | U2164C20 | DRAM 64 KB |
| 1D36 / 2D36 | „EPROM vollst." 0-889381 / 0-889382 (Fassung 24-pol., Plan zeigt 28-pol.-Belegung in Klammern) | EPROM 1 / EPROM 2 |
| D12 | DL093 (7493) | Baudtakt-Teiler ÷8, Quarz C15 = **9,832 MHz** (MQ 02-9832,0k) |
| 1D11…8D11 | DL074 | u. a. RFF, RESI, Baud-÷2 |
| 1D13…, 1D15/2D15, 1D16…5D16 | DL193 / DL253 / DL257 | Schreibvorkompensation, Takt-/Signal-Multiplexer Floppy |
| 1D24…3D24, 1D26…4D26, 1D37…4D37, 1U1…4U1 | K170UP2 / 7406 / B084 / MB111 | V.24-Empfänger / OC-Treiber / V.24-Sender (OPV) / IFSS-Optokoppler |

## 1. Speicherbanksteuerung (Bl. 2 C2–D4, B5–F5; Bl. 1 B6)

### 1.1 Befund

**ADP = D25 MH7489** (gelesen, Bl. 2 C2): A0–A3 = **A12–A15**, D0–D3 = **PD0–PD3** (gepufferter
Port-Datenbus), Ausgänge /O1, /O2, /O3 (Pins 5/7/9) mit Pull-ups 1R7/2R7/3R7 = **/PROM-SEL, /SRAM-SEL,
/DRAM-SEL**; /O4 (Pin 11) offen. Pinbelegung = 7489/74189 (1 A0, 2 /CS, 3 /WE, 4 D1, 5 /Q1 …) — passt.
Weil die Ausgänge invertieren, ist ein geschriebenes „1"-Bit ein aktives (Low-)Select — wie im Handbuch.

**RFF = 6D11 (7474)** (gelesen, Bl. 2 D2): /R ← **/RES**, /S ← **/RES-RFF**, D und C fest High.
Also: **Reset ⇒ Q = 0, /Q = 1; jeder Zugriff auf 04H ⇒ Q = 1.** Das Handbuch nennt den Reset-Zustand
„RFF gesetzt" — im Plan heißt er Q = 0. Nur der Name ist vertauscht, die Wirkung stimmt.

Verknüpfung (gelesen bis auf die mit * markierten Eingänge, die abgeleitet sind):

| Signal | Logik | Wirkung |
|---|---|---|
| ADP /CS (Pin 2) | `/Q_RFF AND /WEADP` (2D5, 7408) | vor RES_RFF nur während eines WEADP-Zugriffs freigegeben, danach **dauernd** |
| ADP /WE (Pin 3) | `/WEADP OR /WR*` (3D10, 7432) | nur bei **Schreiben** auf 00H–03H |
| /PROM_eff | `/PROM-SEL AND Q_RFF` (2D5) | **vor RES_RFF: PROM überall aktiv**, danach laut ADP |
| X (Speicherfreigabe) | `MEMDI OR /MREQ*` (3D10, MEMDI = NOT /MEMDI über 2D3) | |
| PROM-CE (aktiv H) | `NOR(/PROM_eff, X)` (1D2) | |
| SRAM-CE (aktiv H) | `NOR(/SRAM-SEL, X)` (1D2) | |
| /DRAM | `/DRAM-SEL OR MEMDI` (3D10) → DRAM-Steuerung | |

/RES-RFF kommt vom Dekoder (Y1 von 1D18, 04H–07H) **ohne /RD- oder /WR-Bedingung** ⇒ IN **oder** OUT
setzt Q (wie Handbuch). „Nur einmal nach Reset": Q bleibt 1 bis zum nächsten /RES — ein zweiter Zugriff
ist wirkungslos, nicht verboten (gelesen).

**EPROM-Abbildung** (gelesen, Bl. 2 B4–C5): beide EPROMs liegen an **A0–A11** (2732), /OE = /RD.
Chipauswahl über die Wickelbrücke **XP2**: Standard (Brücke 1XR1, Bestückungsplan „XP2 = XR2 3-4") =
**A12**: `/CE(1D36) = NAND(PROM-CE, NOT A12)`, `/CE(2D36) = NAND(PROM-CE, A12)` (1D1 + 1D4).
Alternativ A13 (für 2 × 2764; dann liegt A12 auf deren Pin 2 — im Plan bereits verdrahtet). A13–A15 bzw.
A14/A15 werden **nicht** ausgewertet.

**SRAM** (gelesen, Bl. 2 D5–F5): 4 × U214 an A0–A9, `/CE(1D34,2D34) = NAND(SRAM-CE, A10)`,
`/CE(3D34,4D34) = NAND(SRAM-CE, NOT A10)`; A11 nicht ausgewertet (wie Handbuch). /WE gemeinsam (/WR,
abgeleitet).

**WAIT-Generator** (Bl. 2 F2–F4, abgeleitet): Eingang ist PROM-CE (Linie über Bl. 2 C4→F2 verfolgt),
zwei 7474 (2D23) als Schieberegister, Ausgang 2D3 (OC) auf **/WAIT**. Er wirkt also **nur bei
EPROM-Zugriffen** (auch beim M1-Holen) — das löst die Frage aus `hw_8bit.md` §3.1. Die Taktung
der beiden Stufen (ΦTTL bzw. invertiert) ist nicht eindeutig zu lesen. Das Handbuch nennt **zwei
Wartetakte**.

### 1.2 Folgerungen für den Emulator

1. **Nach Reset (Q_RFF = 0) liefert jede Speicheradresse EPROM**: 8 KB, gespiegelt mit Periode
   8 KB über 64 KB (A12 wählt EPROM 1/2, A13–A15 egal). **Abweichung vom Handbuch** („EPROM-Bereich
   ab Adresse 0000H freigegeben") — der Text verschweigt die Spiegelung. *abgeleitet (sicher).*
   Schreibzugriffe gehen in dieser Phase ins Leere (kein RAM selektiert, ADP-Ausgänge inaktiv).
2. **Ist PROM_SEL in einem 4-KB-Fenster gesetzt, entscheidet das A12 des Fensters, welches EPROM
   dort erscheint**: Fenster mit gerader Seitennummer (0000H, 2000H, …, E000H) zeigen EPROM 1 (1D36),
   ungerade (1000H, 3000H, …) EPROM 2 (2D36). Das EPROM ist also **nicht frei verschiebbar**:
   PROM_SEL auf 8000H zeigt die *erste* Hälfte. Auch das steht nicht im Handbuch. *gelesen.*
3. ADP-Zelle = `addr >> 12`, Wert = Bits 0–2 des Datenbytes. **Achtung, voller 16-Bit-Port:**
   `OUT (00H),A` legt A auf A8–A15 ⇒ Zelle = A >> 4 und Inhalt = A & 7 aus *demselben* Byte. Der
   Kern muss die E/A-Adresse 16 Bit breit an die Karte geben. *abgeleitet.*
4. **Mehrfachselektion**: keine Verriegelung in der Hardware. Lesen ⇒ mehrere Treiber auf MD0–MD7
   (Buskonflikt, Wert elektrisch unbestimmt); Schreiben ⇒ in **alle** selektierten RAM-Bänke
   (SRAM-/WE und DRAM hängen beide an /WR). Vorschlag für den Emulator: lesen = UND-Verknüpfung
   der beteiligten Bänke (Low gewinnt) plus Log-Warnung, schreiben in alle Bänke. Messen: Frage 3.
   *abgeleitet / Lesewert unsicher.*
5. **Keine Bank selektiert**: MD-Bus wird nicht getrieben, der Lesewert ist offen. Vorschlag: FFH.
   Messen: Frage 2. *unsicher.*
6. **/MEMDI** (X13 B9, Pull-up 12R18) sperrt alle drei Bänke. Auf der Karte treibt niemand /MEMDI,
   es kommt nur über den Service-Stecker. Im Emulator: dauernd inaktiv. *gelesen.*
7. Gegenprobe Software (MON8 3.1, Rang 3): bei 000F–004B (Kaltstart) bzw. 0D5F–0D77 schreibt
   `OUT (C),D` mit B = 00H, 10H, …, F0H 16 × `00H`, dann `01H` → Zelle 0, `IN A,(04H)` (RES_RFF per
   **IN**) und `01H` → Zelle 1 ⇒ danach EPROM auf 0000H–1FFFH, sonst nichts. Passt zu 1–3.

## 2. E/A-Dekoder (Bl. 1 B6–C7, A7)

**gelesen:** 1D18 und 2D18 (DS8205) mit A = A2, B = A3, C = A4; E3 = `NOR(A6, A7)` (1D2);
1D18 /E1 = A5, 2D18 /E1 = NOT A5 (2D4); **/E2 beider = Ausgang 2D1 (7400) =
`NAND(/M1, NOT /IORQ)`** (abgeleitet aus 2D4/2D1, Linien zu /IORQ und /M1 nur im Groben verfolgt) —
dekodiert wird nur bei IORQ ohne M1. A0/A1 gehen direkt an die Bausteine; **A8–A15 werden nicht
ausgewertet.**

| Ausgang | Bereich (Spiegel) | Signal | Bemerkung |
|---|---|---|---|
| 1D18 Y0 | 00H–03H | /WEADP | nur schreibend wirksam (§1) |
| 1D18 Y1 | 04H–07H | /RES-RFF | IN oder OUT |
| 1D18 Y2 | 08H–0BH | /CE-CTC0 | A0/A1 = Kanal |
| 1D18 Y3 | 0CH–0FH | /CE-PIO0 | A0 = C/D, A1 = B/A |
| 1D18 Y4 | **10H–13H** | /CE-L1 → DS8282 1D19 (STB = NOR(/CE-L1, /WR)) | nur Schreiben, 4-fach gespiegelt |
| 1D18 Y5 | **14H–17H** | /CE-L2 → DS8282 2D19 | dto. |
| 1D18 Y6 | 18H–1BH | /CE-PIO1 | |
| 1D18 Y7 | 1CH–1FH | /CE-PIO2 | |
| 2D18 Y0 | **20H–23H** | /CE1-FDC | FDC A0 = A0 ⇒ 22H/23H spiegeln 20H/21H |
| 2D18 Y1 | 24H–27H | /CE-SIO0 | A0 = C/D, A1 = B/A |
| 2D18 Y2 | 28H–2BH | /CE-SIO1 | |
| 2D18 Y3 | 2CH–2FH | /CE-CTC1 | |
| 2D18 Y4…Y6 | 30H–33H, 34H–37H, 38H–3BH | /CE-RES1…3 | nur an X13 (A3, B3, B15); auf der Karte **nichts** |
| 2D18 Y7 | **3CH–3FH** | /CE-DMA | DMA hat keine Adressleitung ⇒ 4-fach gespiegelt |
| — | **40H–FFH** | — | E3 verlangt A6 = A7 = 0 ⇒ **kein Baustein antwortet** |

- Lesen einer nicht belegten Adresse (30H–3BH ohne X13-Gerät, 40H–FFH, 00H–17H lesend):
  Datenbus offen, Vorschlag FFH. *unsicher (Messung Frage 2b).*
- **Lesen von 10H/14H liefert nichts**: die DS8282 haben /OE fest auf Masse und nur Ausgänge zur
  16-Bit-Seite. Lesbar sind die Ports für den U880 nicht. *gelesen.*
- D9 (7430) + 2D4 bilden aus WEADP, RES-RFF, CE-L1, CE-L2 (+ ein fünfter Eingang, vermutlich FDC)
  **/DPRQ** = Freigabe des Port-Datenbus-Treibers 5D20 (D ↔ PD). Den Emulator betrifft das nicht.
  *gelesen / fünfter Eingang unsicher.*
- `/PM1 = /M1 AND /RES` (2D1 + 7D4) geht an die **PIOs** (Reset der UA855 über M1 ohne IORQ/RD).
  *abgeleitet.*

## 3. Interruptkette und Interruptquellen (Bl. 1 B4, Bl. 3 F2–G3, Bl. 4 D1)

**gelesen:** DMA IEI = +5 V über 4R18 (erster der Kette). Verdrahtung über die Leitungen IPK1–IPK8:

```
DMA ─IPK1→ PIO2 ─IPK2→ CTC0 ─IPK3→ ┐
                                  D7(7411) IPK4 = IPK1∧IPK2∧IPK3 → SIO0 ─IPK5→ SIO1 ─IPK6→
D7(7411) = IPK4∧IPK5∧IPK6 ─ XP6 (Brücke 4XR2, X13 IEOT/IEIT) → PIO0 ─IPK7→ PIO1 ─IPK8→ CTC1 (IEO offen)
```

Die beiden 7411-Gatter sind eine **Vorausschau** (Look-ahead), damit sich die Laufzeit der Kette
nicht aufsummiert. Funktional ergibt sich dieselbe Reihenfolge wie im Handbuch: **DMA – PIO2 – CTC0 –
SIO0 – SIO1 – \* – PIO0 – PIO1 – CTC1**. Im Emulator genügt eine gewöhnliche Daisy-Chain in dieser
Reihenfolge. *gelesen (Gatter), abgeleitet (Äquivalenz).*

Quellen an /INT (alle Open-Drain bzw. OC, Pull-up 2R18):

| Quelle | Weg | Befund |
|---|---|---|
| DMA | INT/PULSE (Pin 37, Pull-up 2N1) → 7D4 → 2D3 (OC-NAND, beide Eingänge zusammen) → /INT | gelesen; **dasselbe Signal schaltet auch TC des FDC** (§6) |
| PIO0/1/2, CTC0/1, SIO0/1 | direkt /INT | gelesen |
| **FDC INT** | **nicht an /INT**, sondern an **PIO2-B4** (Eingang) | abgeleitet (Linie Bl. 4 B2→E2 verfolgt). Ein FDC-Interrupt erreicht die CPU nur, wenn PIO2 Port B im Bitbetrieb mit Maske für B4 arbeitet — oder die Software fragt ab |
| Index-Impuls | geht **nur an FDC IDX** (über 1D16/D8); **nicht an einen CTC** | gelesen (CTC-Eingänge s. §5) |
| X13 /INT (B23) | Service | gelesen |

## 4. RESET und NMI (Bl. 1 E2–G6)

**RESET (abgeleitet aus gelesenen Gattern):**
- D17 (DL8127) RESETIN (Pin 15) liegt am RC-Glied C6 ⇒ **Power-on-Reset**. RESETOUT (Pin 14) →
  3D3 (OC-NAND, Pin 12) und /S von **6D11 (RESI-Flipflop)**.
- **/RESP** (Taste, X11 A1) → 3D3 Pin 13 und /R von 6D11.
- `/RES = RESETOUT_n AND /RESP` (3D3 Pin 11 → 3D3 Pin 9/10 → Pin 8, Pull-ups 6R18/7R18). /RES geht an
  CPU, DMA, CTCs, SIOs, FDC-Logik und X13 A20, **nicht zur 16-Bit-Karte** (die hängt an PIO0-B7, §7).
- **RESI** = Q von 6D11: Power-on setzt (High), Taste löscht (Low) → **PIO2-A7** ✓ Handbuch. Wird bei
  Power-on die Taste gedrückt, gewinnt der zuletzt losgelassene Eingang.
- Die PIOs haben keinen Reset-Pin; sie werden über /PM1 (§2) zurückgesetzt.

**NMI (gelesen, Bl. 1 F6):**
- /NMIP (X11 B1, 6 µs) → 7D4 (Pin 3→4) = NMIP aktiv High.
- /NMI-UM (Pull-up 5R18, aus PIO0-B7) → 7D4 (Pin 1→2).
- `/NMI (U880) = NAND(/NMI-UM, NMIP)` (3D3 Pin 4/5→6, OC) und `/NMI-U8000 = NAND(NOT /NMI-UM, NMIP)`
  (3D3 Pin 1/2→3, OC) → X1 B5.
- Nach Reset ist PIO0-B7 Eingang ⇒ 5R18 zieht /NMI-UM auf High ⇒ NMI an U880 ✓ Handbuch.
- Die NMI-Quellen sind **die Taste und X13 A23 (Service), sonst nichts**. Die 16-Bit-Karte kann der
  U880 keinen NMI geben; über die Kopplung läuft nur INT_8 (§7).

## 5. CTC-Verschaltung und Baudtakte (Bl. 1 D9, Bl. 3 C3–F4, B5–F6)

- **Baudtakt (gelesen):** Oszillator 3D4 (7404) mit Quarz **C15 = 9,832 MHz** → D12 (7493, Eingang B
  → QD) **÷8 = 1,229 MHz = CPBAUD** → X13 A15 (über 3D4 invertiert als /CPBAUD) und an die CLK/TRG-
  Eingänge.
- **CTC0 (1D31, 08H):** CLK/TRG0 = CPBAUD, ZC/TO0 → 8D11 (÷2) → **BAUD3** → SIO1 RxTxCB (tty3).
  **CLK/TRG1, CLK/TRG2, CLK/TRG3, ZC/TO1, ZC/TO2: nur Stummel, unbeschaltet.** Die Kanäle 1–3
  („Floppy", „System", „frei") sind also reine Zeitgeber (Vorteiler ×16/×256 vom Φ = 4 MHz). Einen
  Hardware-Weg für Index-Impuls, Motorabschaltung oder Einzelschritt gibt es nicht. Diese Funktionen
  macht die Software in den Timer-Interrupts. Klärt W10/Frage 3 in `hw_8bit.md`, soweit der Plan es
  kann. *gelesen.* Für den Emulator: CLK/TRG1–3 als festen Pegel modellieren — offener TTL/NMOS-
  Eingang, vermutlich High (*unsicher*). Ein Kanal im Zählermodus oder mit externem Trigger würde nie
  loslaufen.
- **CTC1 (2D31, 2CH):** CLK/TRG0, 1, 2 gemeinsam an CPBAUD; ZC/TO0 → 7D11 ÷2 → **BAUD0**,
  ZC/TO1 → 7D11 ÷2 → **BAUD1**, ZC/TO2 → 8D11 ÷2 → **BAUD2**; CLK/TRG3 nicht gezeichnet (unbeschaltet).
  *gelesen.*
- Zuordnung (gelesen): BAUD0 → SIO0 RxCA/TxCA (tty0, über Umschaltlogik), BAUD1 → SIO0 RxTxCB (tty1),
  BAUD2 → SIO1 RxCA + TxCA (tty2), BAUD3 → SIO1 RxTxCB (tty3) ✓ Handbuch Tab. 3.3-12.
- Baudrate = 1 229 000 / ZK / 2 / SIO-Teiler; ZK = 1, ×32 ⇒ **19 203 Bd** (+0,016 %). Im Emulator
  9,832 MHz exakt nehmen, nicht 9,8304.
- Der FDC-Takt hängt **nicht** am CTC (§6).

## 6. Floppy: U8272, PIO2, DMA (Bl. 4; Bl. 1 B4, D5, E8)

### 6.1 FDC D33 (gelesen, wo nicht anders vermerkt)
- Daten an PD0–PD7, A0 = A0, /RD, /WR, /CS = **/CE-FDC = NAND(CE1-FDC, /BUSAK)** (2D4/2D1, Bl. 1 D5):
  FDC-Register nur, solange die CPU den Bus hat.
- **/DACK = NAND(CE1-FDC, BUSAK)**: der DMA muss mit **Portadresse 20H–23H** arbeiten (der Dekoder
  läuft auch im DMA-Zyklus, /IORQ vom DMA). *abgeleitet.*
- **DRQ → 5D11 (7474, D = DRQ, C = ΦTTL) → Q → DMA RDY**, also ein Takt Synchronisierung, aktiv High
  (Bl. 1 E8). *gelesen.*
- **TC = INT_DMA OR PIO2-A6** (1D2 NOR + 7D4, Bl. 4 D1): Ende des DMA-Blocks (INT/PULSE) **oder**
  Software über PA6. *gelesen (A6-Abzweig im Zoom eindeutig).*
- **INT → PIO2-B4** (§3). **RES (Pin 1) ← PIO2-B7.** *abgeleitet: Linie Bl. 4 D2→E2 verfolgt;
  Pull-up auf B7 über 2N1 wahrscheinlich ⇒ FDC nach Reset im Reset, bis die Software B7 = 0 ausgibt —
  unsicher.*
- **US0/US1 (Pins 28/29) nicht beschaltet.** Die Laufwerksauswahl macht **allein PIO2-B0…B3**. Der FDC
  meldet zwar die Unit-Nummer aus dem Befehl, angesprochen wird aber das Laufwerk, das per PIO
  gewählt ist. *gelesen (Pins fehlen im Plan).*
- **CLK (Pin 19) = CPI**, Schreibtakt WRCLK, Fenster DW, VCO, RDDATA: analoger Datenseparator
  (VT1/VT2, 1D37 B084, 1D11/2D11, 3D14/2D14) und Vorkompensation (D13 DL164, 1D15/2D15 DL253,
  PS0/PS1). Im Emulator nicht nachzubilden, Bitstrom-/Sektorebene genügt.
- **CPI = 1D16 (DL257) Y2: A2 = 2ΦTTL 8 MHz, B2 = ΦTTL 4 MHz; S = PIO2-A4** (Linie Bl. 4 E2→D10 über
  das ganze Blatt verfolgt, abgeleitet). **PA4 = 0 ⇒ 8 MHz (8″), PA4 = 1 ⇒ 4 MHz (5¼″).** Dieselbe
  Umschaltung wählt den Takt des Datenseparators (Y3 = CP\*). FM/MFM kommt aus dem **MFM-Ausgang des
  FDC** (Befehlsbit) und schaltet die Teiler von WRCLK (3D16, S = MFM). **Ein Dichte-Signal zum
  Laufwerk (/ND) gibt es bei Index 1 nicht.**
- **RDY (Pin 35) ← /RDY** der Laufwerke (eine Sammelleitung, Pull-up 1R1, Schmitt-Inverter D8).
  Gemeldet wird also die Bereitschaft des per PIO gewählten Laufwerks.
- **WP/TS, FLT/TRK0, LCT/DIR, FR/STP** über den Multiplexer 2D16 (S = RW/SEEK) — Standardschaltung:
  RW: Y1 = /WP, Y2 = /FW, Y3 = Y4 = Masse (LCT und FR zum Laufwerk **nicht** benutzt); SEEK: Y1 = /TS,
  Y2 = /TRK0, Y3 = DIR, Y4 = STP. → `/DIR`, `/STP` über 7406.
- **/HDS = NOT HDS** (3D5 + 4D26). **/HDL = NOT(HDL_FDC OR PA5)** (6D4/2D21/3D26) — PA5 = 1 erzwingt
  „Kopf laden" (nur X10/8″). *abgeleitet; Eingang „HDL_FDC" im Bild nicht ganz sicher.*
- **/FR (Fault Reset, X10)**: aus 1D5 (7408) + D8 + 4D26, Eingänge nicht sicher verfolgt. *unsicher.*

### 6.2 PIO2 (1D29, 1CH–1FH) — Bitbelegung

| Bit | Richtung | Signal | Grad |
|---|---|---|---|
| PA0…PA3 | Aus | **/MO0…/MO3** (Motor, 0 = ein; je 7404 + 7406 ⇒ Pegel = Pinwert) | gelesen |
| PA4 | Aus | **8″/5¼″**: 0 = 8 MHz FDC-Takt, 1 = 4 MHz | abgeleitet |
| PA5 | Aus | Kopf laden erzwingen (/HDL) | abgeleitet |
| PA6 | Aus | **TC** per Software (ODER mit DMA-INT) | gelesen |
| PA7 | Ein | **RESI** (1 = Power-on, 0 = Taste) | gelesen |
| PB0…PB3 | Aus | **/SE0…/SE3** (0 = gewählt) | gelesen |
| PB4 | Ein | **FDC INT** | abgeleitet |
| PB5, PB6 | Ein (vermutl.) | Leitungen aus dem Laufwerks-/Separatorbereich, **Herkunft nicht verfolgbar** (Kandidaten: Index, /RDY, /TS) | **unsicher** |
| PB7 | Aus | **FDC RESET** | abgeleitet |

Pull-up-Netzwerke 1N1 (PA0…PA6) und 2N1 (PB0…PB3, wohl auch PB4…PB7). Nach Reset sind alle Bits
Eingänge ⇒ hoch ⇒ Motoren aus, kein Laufwerk gewählt, 5¼″-Takt, TC aktiv, ggf. FDC im Reset.
Gegenprobe Software: MON8 3.1 @0019 schreibt `CFH, 80H, 03H` an 1DH ⇒ Port A Bitbetrieb, **nur A7
Eingang** — passt genau zu dieser Tabelle. Port B fasst der Monitor nicht an.

### 6.3 DMA (D32, 3CH)
/CE = /CE-DMA (3CH–3FH), RDY = synchronisiertes DRQ (s. o.), /BUSRQ (Pull-up R17) gemeinsam mit X13
B20 an CPU /BUSRQ, /BAI = /BUSAK, /BAO → X13 A10, IEI fest High, INT/PULSE → /INT **und** TC.
*gelesen.* Im Emulator: wiederverwendbar ist `core/primitives/z80_dma` + `upd765` wie beim PC 1715W,
aber **TC kommt hier aus dem DMA-INT, nicht aus dem DMA-Endsignal des 1715W**. Vor P5 vergleichen.

### 6.4 Laufwerksstecker (Bl. 4 A8–C10)
- **X8 und X9 sind gleich verdrahtet** (SE0–SE3, DIR, STP, HDS, WE, WRDATA, RDDATA, RDY, IDX, TRK0,
  WP, TS); einziger Unterschied B6: X8 = /MO0 **nur über Wickelbrücke** (Index-0-Verträglichkeit),
  X9 = **/MO1**. **Abweichung vom Handbuch:** Index 1 nennt nur X8, X9 erst bei Index 3 (dort
  34-polig). Der vorliegende Plan (Lp-Index 1, ÄZ 2 von 22.2.88) hat X9 schon. *gelesen.*
- **X10 (extern): kein /HDS und kein /TS** ⇒ externe Laufwerke (8″) sind bei Index 1 **einseitig**.
  Dazu /MO0…/MO3, /HDL, /FR, /FW. *gelesen* — klärt Frage 10 in `hw_8bit.md`.
- Index: **eine** Sammelleitung /IDX (X8/X9 A3, X10 A9).

## 7. Kopplung X1/X2 zur 16-Bit-Karte (Bl. 3 A1–G2)

**gelesen** (Pin für Pin gleich Handbuch Tab. 3.3-15; neu ist die Zuordnung zu den Bausteinbits):

| Port | Bit | Signal | Stecker | Richtung |
|---|---|---|---|---|
| **10H** (DS8282 1D19) | D0…D7 | D0…D7/8-16 | X1 B4, A6, A7, B13, B11, B10, B9, B8 | 8→16 |
| **14H** (DS8282 2D19) | D0 | **INT_16** | X1 A13 | 8→16 |
| | D1…D6 | **V1…V6/8-16** | X1 A12, A11, A10, A9, A8, B7 | 8→16 |
| | D7 | **RDY/8-16** | X1 A5 | 8→16 |
| PIO0 A (0CH) | A0…A7 | D0…D7/16-8 | X2 B2, B9, B8, B7, B1, A10, A8, A1 | 16→8 |
| | ARDY | **RDY/16-8** | X2 B3 | 8→16 (Hardware-Handshake Modus 1) |
| | /ASTB | **DD_8/EO_16** | X2 B5 | 16→8 |
| PIO0 B (0EH) | B0 | **INT_8** (Pull-up 1N1) | X2 A6 | 16→8 |
| | B1…B4 | **V1…V4/16-8** | X2 A5, A4, A3, A2 | 16→8 |
| | B5 | **Rücklesen von DD_8/EO_16** (an /ASTB angeschlossen) | X2 B5 | Ein |
| | B6 | **EO_8/DD_16** | X1 A3 | **Ein** (berichtigt nach P2b: X3:A3 wird von PIO1-ARDY der 16-Bit-Karte getrieben; ein Ausgang stünde dort im Konflikt) |
| | B7 | **/RESET_U8000 und zugleich /NMI-UM** | X1 B1 | 8→16 |
| /NMI-U8000 | — | aus NMI-Logik (§4) | X1 B5 | 8→16 |
| | BRDY, /BSTB | unbeschaltet | — | |

Befunde, die das Handbuch nicht sagt:
1. **Ein Bit, zwei Funktionen:** PIO0-B7 treibt **sowohl** /RESET_U8000 **als auch** die
   NMI-Umschaltung. Nach Reset (Eingang, Pull-up 5R18) ist B7 = 1: NMI geht an den U880, die
   16-Bit-Karte ist (vermutlich) im Reset. Gibt der U880 B7 = 0 aus, gibt er die 16-Bit-Karte frei
   und lenkt die NMI-Taste dorthin. So erklärt sich der Handbuchsatz „mit Freigabe des 16-Bit-Rechners
   … /NMI_UM = Low". Polarität laut P2b gelesen: X3:B1 RESET ist **high-aktiv** mit Pull-up, B7 = 1 hält die 16-Bit-Karte also im Reset.
   *gelesen (Knoten), Polarität 16-Bit-Seite abgeleitet.*
2. **Kein Hardware-Strobe beim OUT 10H/14H:** die DS8282 übernehmen mit `STB = NOR(/CE-Lx, /WR)` und
   geben sofort aus (/OE fest Low). RDY/8-16 (14H Bit 7) und EO_8/DD_16 (PIO0-B6) setzt **die
   Software**. Der Weg 8→16 ist reines Bit-Banging, nur der Weg 16→8 nutzt den PIO-Handshake
   (/ASTB → ARDY). *gelesen.*
3. **DS8282 ohne Reset:** nach dem Einschalten ist der Inhalt beider Latches **undefiniert** und
   bleibt es bis zum ersten OUT — darunter INT_16 und RDY/8-16. Für den Emulator: Startwert
   festlegen (Vorschlag 00H) und messen (Frage 6). *gelesen (kein Reset-Pin), Wert unsicher.*
4. Kein /RES, kein /BUSRQ und kein Takt gehen über X1/X2 (soweit auf Bl. 3 gezeichnet; Masse- und
   Versorgungspins sind auf den Blättern nicht dargestellt). *gelesen.*

## 8. Serielle Kanäle (Bl. 3 B5–H10)

- **SIO0, SIO1 = UA8560 im SIO/0-Bonding** (B-Kanal mit RxTxCB, DTRB, SYNCB). Kanalzuordnung
  tty0 = SIO0-A, tty1 = SIO0-B, tty2 = SIO1-A, tty3 = SIO1-B ✓. *gelesen.*
- **/W/RDYA und /W/RDYB beider SIOs liegen an /WAIT** (Bl. 3 C4/E4). Programmiert die Software die
  Wait-Funktion (WR1 Bit 7 = 0, Bit 6 = 0), hält die SIO die CPU an. Der Emulator braucht deshalb
  einen /WAIT-Weg von `Z80SIO` in den CPU-Takt (oder einen Nachweis, dass UDOS/Monitor das nie
  einschalten). *gelesen.*
- **IFSS-Umschaltung ist per Software lesbar:** je Kanal liegt die Steckerleitung „IFSS" (Pin 9,
  Pull-up 13R18/14R18/15R18/16R18; im Kabel nach SG gebrückt = IFSS) an **/SYNC** des Kanals und an
  der Empfangsweiche `RxD = (V.24-Daten AND IFSS_n) OR NOR(IFSS-Optokoppler, IFSS_n)` (5D5 7408,
  3D2 7402, 1D10 7432). Im Asynchronbetrieb erscheint der /SYNC-Pegel in **RR0 Bit 4** ⇒ die
  Software kann feststellen, ob V.24 oder IFSS gesteckt ist. *abgeleitet (Pinbelegung der Gatter
  geprüft).* Gesendet wird immer auf beiden Wegen (V.24-OPV 1D37…4D37 und IFSS-Stromtreiber).
- **tty0-Takt:** RxCA/TxCA kommen aus einer Weiche (2D10 7432, 4D5 7408, 4D4) zwischen BAUD0 und
  extern RC/TC (Steckerbrücken RxCE/TxCE nach SG, Pull-ups 17R18/18R18); CPO (Pull-up) schaltet BAUD0
  als TC (113) an die DÜE. ✓ Handbuch. *gelesen/abgeleitet.*
- **Modemleitungen:** /RTSA und /DTRA (SIO0) sowie /DTRB, /DTRA, /DTRB (übrige Kanäle) gehen über
  OPV-Treiber nach außen; **/DCD aller vier Kanäle** kommt über K170UP2-Empfänger herein (✓ Leitung
  108). **/CTSA (tty0)** entsteht aus /RTSA und zwei weiteren Leitungen über 2D2/3D4 (Lokalschleife:
  mit eingeschaltetem RTS gilt auch CTS). Die genaue Logik ist nicht sicher lesbar (Handbuch: CTS/DSR
  „nur nach Schaltungsänderung"). Die /CTS-Eingänge der übrigen drei Kanäle sind nicht gezeichnet
  (vermutlich fest). *unsicher.* Für den Emulator: CTS = RTS (tty0) bzw. CTS aktiv (übrige), bis
  gemessen.

## 9. Index 1 ↔ Index 3

Vorhanden ist nur der Plan zu **Leiterplatte Index 1** (Zeichnungsstand bis ÄZ 2/3, Feb. 1988). Was
Index 3 ändert (6 Lagen, X9 34-polig mit /ND, Kopplung mit sechs getrennten Handshake-Leitungen und
RUN, DEE-Belegung der V.24-Buchsen, XP7), steht nur im Handbuch (`hw_8bit.md` §9). Aus dem Plan
lässt es sich weder bestätigen noch widerlegen. Zwei Index-3-Merkmale stecken schon im Index-1-Plan:
**X9** (mit /MO1, ohne /ND) und die Wickelbrücke für /MO0 an X8. Beim Gerät des Anwenders zuerst den
Leiterplattenindex feststellen (Frage 9).

## 10. Abweichungen vom Handbuch (Kurzliste)

| # | Handbuch | Plan | Folge |
|---|---|---|---|
| A1 | nach Reset „EPROM ab 0000H" | EPROM in **allen** 16 Seiten, 8-KB-Spiegel | Speicherkarte nach Reset |
| A2 | EPROM „in 4-K-Stufen verschiebbar" | welche EPROM-Hälfte erscheint, bestimmt **A12 des Fensters** | ADP-Modell |
| A3 | RFF „gesetzt" nach Reset | FF 6D11 **rückgesetzt** (Q = 0) | nur Benennung |
| A4 | /NMI_UM = PIO0-B7, /RESET_U8000 eigene Zeile | **dasselbe Bit** B7 | Kopplungsmodell |
| A5 | PIO0 „für Eingabe" | Port B gemischt: B6/B7 Ausgang, B5 liest /ASTB zurück | PIO-Modus 3 |
| A6 | CTC0 K1/K2 „Floppy/System" | keine Hardwareverbindung, reine Zeitgeber | keine CLK/TRG-Quellen |
| A7 | — | FDC-INT an PIO2-B4, TC = DMA-INT ∨ PA6, FDC-RESET = PIO2-B7 | Floppy-Modell |
| A8 | Index 1: nur X8 | X8 **und** X9 | Laufwerk 1 intern |
| A9 | — | X10 ohne HDS/TS ⇒ externe Laufwerke einseitig | Laufwerksprofile |
| A10 | — | SIO-W/RDY an /WAIT; /SYNC = IFSS-Erkennung | SIO-Modell |
| A11 | — | E/A: A8–A15 frei, 40H–FFH unbelegt, 4-fach-Spiegel bei 10H/14H/20H/3CH | Dekoder |

## 11. Offene Fragen an das Gerät (Messvorschläge)

Messprogramme als CP/M-.COM gehen hier nicht; sie müssen unter UDOS oder als Monitor-Ladeprogramm
laufen (AP P3b, Muster `tools/em256/`). Jede Messung gibt ihre Werte als Hex-Zeile aus.

1. **EPROM-Abbildung (A2):** nach dem Kaltstart `01H` in die ADP-Zellen 8 und 9 schreiben (B = 80H,
   90H; C = 00H), danach je 16 Byte ab 0000H, 1000H, 8000H, 9000H lesen und vergleichen. Erwartet:
   8000H = 0000H, 9000H = 1000H. Zellen danach auf den alten Wert zurück (Monitor-Belegung vorher
   aus seinen RAM-Variablen bzw. auf `00H` = leer setzen, falls dort nichts lag).
2. **Leerer Lesewert:** a) eine Seite ohne Select (`00H`) lesen, b) `IN A,(C)` auf 30H, 40H, 80H, FFH
   sowie lesend auf 00H, 10H, 14H. Erwartet FFH, sonst den gemessenen Wert in den Kern übernehmen.
3. **Mehrfachselektion:** Seite mit `03H` (PROM+SRAM) bzw. `05H` (PROM+DRAM) belegen, vorher SRAM/DRAM
   mit bekanntem Muster füllen, lesen; schreiben und danach mit Einzelselect zurücklesen
   (bestätigt „Schreiben in alle Bänke").
4. **Wartetakte:** dieselbe Schleife (z. B. 1000 × `LD A,(HL)`) einmal mit HL im EPROM, einmal im
   DRAM, Dauer mit CTC0-Kanal 3 als Zeitgeber messen. Erwartet: +2 Takte je EPROM-Zugriff (auch beim
   Befehlsholen).
5. **PIO2 Port B:** Bitbetrieb, B0–B3 Ausgang (Laufwerk 0 wählen), B4–B7 Eingang; 1EH in einer
   Schleife 100 000× lesen und je Bit „wechselt / fest 0 / fest 1" sowie die Wechselzahl ausgeben —
   einmal ohne Diskette, einmal mit Diskette und Motor an (PA0 = 0). Index wechselt etwa 5× pro
   Sekunde, /RDY wird mit Diskette fest. Ordnet B5/B6 zu und bestätigt B4 (FDC INT nach `SENSE DRIVE
   STATUS`/`RECALIBRATE`).
6. **Latch-Startwert:** nicht vom U880 lesbar. Auf der 16-Bit-Seite nach dem Einschalten (vor jedem
   OUT 10H/14H der 8-Bit-Seite) X1-Leitungen über PIO der 16-Bit-Karte lesen — zusammen mit AP P2b
   planen.
7. **IFSS-Erkennung:** RR0 aller vier Kanäle lesen, einmal mit gestecktem V.24-Kabel, einmal mit
   IFSS-Brücke (Pin 9–7). Erwartet Bit 4 wechselt.
8. **CTS tty0:** RR0 Bit 5 mit RTS aus/an (WR5 Bit 1) lesen, ohne Kabel.
9. **Leiterplattenindex** des Anwendergeräts (Aufdruck „Ms 889 130 (x)", Typenschild „V: x1xx" vs.
   „43xx") und Bestückung XP2 (A12/A13), XP6, XP8; Fotos der Wickelfelder.
10. **FDC-Reset-Polarität:** PIO2-B7 als Ausgang = 1, `IN A,(20H)` (MSR) lesen, dann B7 = 0, MSR
   erneut. Erwartet 00H im Reset, 80H danach.
